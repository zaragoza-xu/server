#include "battle.h"
#include "room.h"

#include <algorithm>
#include <cmath>
#include <optional>

using Battle::BattleVector2;

namespace {
using namespace Protocol;
using namespace Battle;

constexpr double kPi = 3.14159265358979323846;
constexpr double kClientUnit = 100.0;

std::optional<BattleEnemyPool> pool_for_node(MapNode::NodeType nodeType) {
  switch (nodeType) {
  case MapNode::NodeType::NORMAL:
    return BattleEnemyPool::NORMAL;
  case MapNode::NodeType::ELITE:
    return BattleEnemyPool::ELITE;
  case MapNode::NodeType::BOSS:
    return BattleEnemyPool::BOSS;
  case MapNode::NodeType::EVENT:
    return std::nullopt;
  }
  return BattleEnemyPool::NORMAL;
}
} // namespace

// ---- Runtime state queries -------------------------------------------------

bool Room::has_enemy_locked(int entityId) const {
  return battleEnemyStates.find(entityId) != battleEnemyStates.end();
}

Protocol::BattlePlayerEntity *Room::live_player_locked(const std::string &uid) {
  // Dead players are intentionally invisible to target selection and actions.
  auto it = battlePlayersByUid.find(uid);
  if (it == battlePlayersByUid.end() || it->second.attribute.currentHP <= 0) {
    return nullptr;
  }
  return &it->second;
}

bool Room::all_players_dead_locked() const {
  if (battlePlayersByUid.empty()) {
    return false;
  }
  return std::all_of(
      battlePlayersByUid.begin(), battlePlayersByUid.end(),
      [](const auto &entry) { return entry.second.attribute.currentHP <= 0; });
}

double Room::battle_time_locked() const {
  return static_cast<double>(battleTick) / battleConfig.frameRate;
}

double Room::battle_difficulty_locked() const {
  return 1.0 + battleConfig.difficultyGrowth * battle_time_locked();
}

// ---- Spawn & enemy pool ----------------------------------------------------

Protocol::MapNode::NodeType Room::resolve_map_node_type_locked() const {
  const int nodeIndex = get_map_node_index(mapNodeId);
  if (nodeIndex < 0) {
    return Protocol::MapNode::NodeType::NORMAL;
  }
  return mapNodes[static_cast<size_t>(nodeIndex)].type;
}

std::vector<Battle::EnemySpawnSpec>
Room::enemy_pool_locked(double time, double difficulty) const {
  std::vector<Battle::EnemySpawnSpec> pool;
  const auto nodePool = pool_for_node(resolve_map_node_type_locked());
  if (!nodePool.has_value()) {
    return pool;
  }

  const double maxCost = difficulty * battleConfig.maxCostFactor;
  for (const auto &def : battleConfig.enemies) {
    if (def.pool != *nodePool || time < def.unlockTime || def.cost > maxCost ||
        def.weight <= 0.0) {
      continue;
    }

    Battle::EnemySpawnSpec spec;
    spec.enemyType = def.enemyType;
    spec.maxHP = def.maxHP;
    spec.attackRange = def.attackRange;
    spec.maxSpeed = def.maxSpeed;
    spec.attackDamage = def.attackDamage;
    spec.attackCooldownTicks = def.attackCooldownTicks;
    spec.cost = def.cost;
    spec.unlockTime = def.unlockTime;
    spec.weight = def.weight * std::max(1.0, difficulty);
    pool.push_back(spec);
  }
  return pool;
}

std::optional<Battle::EnemySpawnSpec>
Room::pick_enemy_locked(const std::vector<Battle::EnemySpawnSpec> &pool,
                        double budget) {
  double totalWeight = 0.0;
  for (const auto &spec : pool) {
    if (spec.cost <= budget) {
      totalWeight += spec.weight;
    }
  }
  if (totalWeight <= 0.0) {
    return std::nullopt;
  }

  std::uniform_real_distribution<double> roll(0.0, totalWeight);
  double target = roll(battleRng);
  for (const auto &spec : pool) {
    if (spec.cost > budget) {
      continue;
    }
    target -= spec.weight;
    if (target <= 0.0) {
      return spec;
    }
  }
  return std::nullopt;
}

BattleVector2 Room::spawn_pos_locked() {
  std::vector<BattleVector2> anchors;
  anchors.reserve(battlePlayersByUid.size());
  for (const auto &[uid, player] : battlePlayersByUid) {
    if (player.attribute.currentHP > 0) {
      anchors.push_back(player.position);
    }
  }

  BattleVector2 anchor{0.0, 0.0};
  if (!anchors.empty()) {
    std::uniform_int_distribution<size_t> playerRoll(0, anchors.size() - 1);
    anchor = anchors[playerRoll(battleRng)];
  }

  std::uniform_real_distribution<double> angleRoll(0.0, kPi * 2.0);
  std::uniform_real_distribution<double> radiusRoll(
      battleConfig.spawnRadiusMin, battleConfig.spawnRadiusMax);

  BattleVector2 candidate{anchor.x + battleConfig.spawnRadiusMin, anchor.y};
  for (int tries = 0; tries < 16; ++tries) {
    const double angle = angleRoll(battleRng);
    const double radius = radiusRoll(battleRng);
    candidate = {anchor.x + std::cos(angle) * radius,
                 anchor.y + std::sin(angle) * radius};
    if (!is_outside_battle(candidate)) {
      return candidate;
    }
  }
  return clamp_battle(candidate);
}

bool Room::update_target_locked(Battle::EnemyState &enemyState) {
  // Empty targetPlayerUid is the idle/no-target state in the battle protocol.
  const auto oldTargetUid = enemyState.entity.targetPlayerUid;
  const Protocol::BattlePlayerEntity *nearestPlayer = nullptr;
  double nearestDistanceSquared = 0.0;

  for (const auto &uid : uids) {
    auto *player = live_player_locked(uid);
    if (player == nullptr) {
      continue;
    }
    const double candidateDistanceSquared =
        Battle::distance_squared(enemyState.entity.position, player->position);
    if (nearestPlayer == nullptr ||
        candidateDistanceSquared < nearestDistanceSquared) {
      nearestPlayer = player;
      nearestDistanceSquared = candidateDistanceSquared;
    }
  }

  if (nearestPlayer == nullptr) {
    enemyState.entity.targetPlayerUid.clear();
    return oldTargetUid != enemyState.entity.targetPlayerUid;
  }

  enemyState.entity.targetPlayerUid = nearestPlayer->uid;
  return oldTargetUid != enemyState.entity.targetPlayerUid;
}

void Room::push_enemy_intent_locked(const Battle::EnemyState &enemyState) {
  // This event means target changed; clients derive chase/idle visuals from
  // uid.
  push_event_locked(EventType::ENEMY_INTENT_CHANGE,
                    IntentParam(enemyState.entity.entityId,
                                enemyState.entity.targetPlayerUid));
}

void Room::spawn_enemies_locked(
    const std::vector<Battle::EnemySpawnSpec> &spawns) {
  battleEnemyStates.reserve(battleEnemyStates.size() + spawns.size());
  for (const auto &spec : spawns) {
    Battle::EnemyState enemyState;
    const int entityId = nextBattleEntityId++;
    enemyState.entity.entityId = entityId;
    enemyState.entity.entityType = Protocol::EntityType::ENEMY;
    enemyState.entity.enemyType = spec.enemyType;
    enemyState.entity.position = spec.position;
    enemyState.entity.direction = Battle::BattleVector2{0.0, 0.0};
    enemyState.entity.attribute.maxHP = spec.maxHP;
    enemyState.entity.attribute.currentHP = spec.maxHP;
    enemyState.entity.attribute.attackCooldownTicks =
        std::max(1, spec.attackCooldownTicks);
    enemyState.attackRange = spec.attackRange;
    enemyState.maxSpeed = spec.maxSpeed;
    enemyState.attackDamage = spec.attackDamage;
    enemyState.attackCooldownTicks =
        enemyState.entity.attribute.attackCooldownTicks;
    enemyState.nextAttackTick = battleTick + enemyState.attackCooldownTicks;
    // Initial target is part of the spawn frame so clients can animate at once.
    const bool targetChanged = update_target_locked(enemyState);

    push_event_locked(EventType::ENEMY_SPAWN, SpawnParam(enemyState.entity));
    if (targetChanged) {
      push_enemy_intent_locked(enemyState);
    }

    battleEnemyStates.emplace(entityId, std::move(enemyState));
  }
}

// ---- Weapon & hit resolution ----------------------------------------------

const Battle::WeaponDef *
Room::equip_weapon_locked(const std::vector<std::string> &items) const {
  const Battle::WeaponDef *best = nullptr;
  for (const auto &item : items) {
    for (const auto &weapon : battleConfig.weapons) {
      if (weapon.weaponId != item) {
        continue;
      }
      if (best == nullptr || weapon.damage > best->damage) {
        best = &weapon;
      }
      break;
    }
  }
  return best;
}

const Battle::WeaponDef *
Room::equipped_weapon_locked(const std::string &uid) const {
  const auto it = battleWeaponsByUid.find(uid);
  if (it == battleWeaponsByUid.end()) {
    return nullptr;
  }
  return &it->second;
}

int Room::weapon_damage_locked(const Battle::WeaponDef &weapon) {
  double damage = weapon.damage * (1.0 + weapon.damageGrowth);
  if (weapon.critChance > 0.0 && weapon.critMultiplier > 1.0) {
    std::uniform_real_distribution<double> roll(0.0, 1.0);
    if (roll(battleRng) < weapon.critChance) {
      damage *= weapon.critMultiplier;
    }
  }
  return std::max(1, static_cast<int>(std::round(damage)));
}

int Room::cooldown_ticks_locked(const Battle::WeaponDef &weapon) const {
  if (weapon.attackSpeed <= 0.0) {
    return 0;
  }
  const double seconds = weapon.attackSpeed / (1.0 + weapon.attackSpeedGrowth);
  return std::max(
      1, static_cast<int>(std::ceil(seconds * battleConfig.frameRate)));
}

double Room::battle_range(const Battle::WeaponDef &weapon) const {
  return std::max(0.0, weapon.range / kClientUnit);
}

void Room::lifesteal_locked(const std::string &uid, int damage,
                            const Battle::WeaponDef &weapon) {
  if (weapon.lifeSteal <= 0.0 || damage <= 0) {
    return;
  }
  auto *player = live_player_locked(uid);
  if (player == nullptr) {
    return;
  }
  const int healing = static_cast<int>(std::round(damage * weapon.lifeSteal));
  player->attribute.currentHP =
      std::min(player->attribute.maxHP, player->attribute.currentHP + healing);
}

void Room::hit_enemy_locked(Protocol::BattleEnemyEntity &enemy, int hitSourceId,
                            EntityType hitSourceType,
                            Protocol::BattlePlayerEntity &player,
                            const BattleVector2 &hitPosition,
                            const Battle::WeaponDef &weapon, int damage,
                            EventType hitType) {
  push_event_locked(hitType,
                    HitParam(hitSourceId, hitSourceType, enemy.entityId,
                             EntityType::ENEMY, hitPosition));
  enemy.attribute.currentHP = std::max(0, enemy.attribute.currentHP - damage);
  push_event_locked(EventType::ENTITY_DAMAGE,
                    DamageParam(player.entityId, EntityType::PLAYER,
                                enemy.entityId, EntityType::ENEMY, damage,
                                enemy.attribute.currentHP));
  lifesteal_locked(player.uid, damage, weapon);
  // Knockback is a client physics effect; server-side hits only settle combat.
}

void Room::melee_attack_locked(Protocol::BattlePlayerEntity &player,
                               const Battle::WeaponDef &weapon,
                               const BattleVector2 &direction) {
  const double range = battle_range(weapon);
  const double rangeSquared = range * range;
  const int damage = weapon_damage_locked(weapon);

  std::vector<int> deadEnemies;
  for (auto &[enemyId, enemyState] : battleEnemyStates) {
    const auto delta =
        BattleVector2{enemyState.entity.position.x - player.position.x,
                      enemyState.entity.position.y - player.position.y};
    if (Battle::length_squared(delta) > rangeSquared) {
      continue;
    }

    const auto toEnemy = Battle::norm_or_zero(delta);
    if (toEnemy.x * direction.x + toEnemy.y * direction.y < 0.0) {
      continue;
    }
    hit_enemy_locked(enemyState.entity, player.entityId, EntityType::PLAYER,
                     player, enemyState.entity.position, weapon, damage,
                     EventType::WEAPON_HIT_ENEMY);
    if (enemyState.entity.attribute.currentHP <= 0) {
      deadEnemies.push_back(enemyId);
    }
  }

  for (const int enemyId : deadEnemies) {
    push_event_locked(
        EventType::ENTITY_DESTROY,
        DestroyParam(enemyId, EntityType::ENEMY, DestroyReason::ENTITY_DEAD));
    battleEnemyStates.erase(enemyId);
  }
}
