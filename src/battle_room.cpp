#include "battle.h"
#include "room.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <mutex>
#include <string>
#include <unordered_map>

using Battle::BattleVector2;

namespace {
bool is_valid_vector(const BattleVector2 &value) {
  return std::isfinite(value.x) && std::isfinite(value.y);
}

Protocol::BattleBulletAttribute
bullet_attribute(const Battle::ProjectileDef &projectile) {
  return {.speed = projectile.speed, .size = projectile.size};
}

double player_radius(const Battle::BattleConfig &cfg) {
  return std::max(0.0, cfg.playerRadius);
}

double enemy_range(const Battle::BattleConfig &cfg,
                   const Battle::EnemyState &enemyState) {
  return enemyState.attackRange + cfg.enemyRadius + player_radius(cfg);
}

double segment_hit_t(const BattleVector2 &from, const BattleVector2 &to,
                     const BattleVector2 &target, double radiusSquared) {
  const BattleVector2 delta{to.x - from.x, to.y - from.y};
  const double lenSquared = Battle::length_squared(delta);
  if (lenSquared <= 0.0) {
    return Battle::distance_squared(from, target) <= radiusSquared
               ? 0.0
               : std::numeric_limits<double>::infinity();
  }

  const BattleVector2 toTarget{target.x - from.x, target.y - from.y};
  const double t =
      std::clamp((toTarget.x * delta.x + toTarget.y * delta.y) / lenSquared,
                 0.0, 1.0);
  const BattleVector2 closest{from.x + delta.x * t, from.y + delta.y * t};
  return Battle::distance_squared(closest, target) <= radiusSquared
             ? t
             : std::numeric_limits<double>::infinity();
}

double segment_pair_hit_t(const BattleVector2 &a0, const BattleVector2 &a1,
                          const BattleVector2 &b0, const BattleVector2 &b1,
                          double radiusSquared) {
  constexpr double eps = 1e-9;
  const BattleVector2 u{a1.x - a0.x, a1.y - a0.y};
  const BattleVector2 v{b1.x - b0.x, b1.y - b0.y};
  const BattleVector2 w{a0.x - b0.x, a0.y - b0.y};
  const double a = Battle::length_squared(u);
  const double c = Battle::length_squared(v);

  if (a <= eps && c <= eps) {
    return Battle::distance_squared(a0, b0) <= radiusSquared
               ? 0.0
               : std::numeric_limits<double>::infinity();
  }
  if (c <= eps) {
    return segment_hit_t(a0, a1, b0, radiusSquared);
  }
  if (a <= eps) {
    const double t =
        std::clamp(-((b0.x - a0.x) * v.x + (b0.y - a0.y) * v.y) / c, 0.0, 1.0);
    const BattleVector2 closest{b0.x + v.x * t, b0.y + v.y * t};
    return Battle::distance_squared(a0, closest) <= radiusSquared
               ? 0.0
               : std::numeric_limits<double>::infinity();
  }

  const double b = u.x * v.x + u.y * v.y;
  const double d = u.x * w.x + u.y * w.y;
  const double e = v.x * w.x + v.y * w.y;
  const double denom = a * c - b * b;

  double s = denom > eps ? std::clamp((b * e - c * d) / denom, 0.0, 1.0)
                         : 0.0;
  double t = (b * s + e) / c;
  if (t < 0.0) {
    t = 0.0;
    s = std::clamp(-d / a, 0.0, 1.0);
  } else if (t > 1.0) {
    t = 1.0;
    s = std::clamp((b - d) / a, 0.0, 1.0);
  }

  const BattleVector2 pointA{a0.x + u.x * s, a0.y + u.y * s};
  const BattleVector2 pointB{b0.x + v.x * t, b0.y + v.y * t};
  return Battle::distance_squared(pointA, pointB) <= radiusSquared
             ? s
             : std::numeric_limits<double>::infinity();
}
} // namespace

void Room::reset_battle_state_locked() {
  battleStarted = false;
  battleTick = 0;
  nextBattleEntityId = 1;
  nextBattleSpawnTick = 0;
  spawnedEnemyCount = 0;
  spawnBudget = 0.0;
  battlePlayersByUid.clear();
  battleWeaponsByUid.clear();
  battleEnemyStates.clear();
  enemyPosByUid.clear();
  nextAttackTickByUid.clear();
  battleBullets.clear();
  pendingBattleEvents.clear();
  for (const auto &uid : uids) {
    battleReadyStates[uid] = false;
  }
}

Protocol::BattleFrameRsp Room::build_battle_frame_locked() const {
  Protocol::BattleFrameRsp frame;
  frame.serverTick = battleTick;
  // Frames expose durable entity state; pending events carry one-shot effects.
  frame.playerEntities.reserve(uids.size());
  for (const auto &uid : uids) {
    auto it = battlePlayersByUid.find(uid);
    if (it != battlePlayersByUid.end()) {
      frame.playerEntities.push_back(it->second);
    }
  }
  frame.events = pendingBattleEvents;
  return frame;
}

void Room::start_battle_locked() {
  reset_battle_state_locked();
  phase = Phase::BATTLE;
  battleStarted = true;

  int spawnIndex = 0;
  for (const auto &uid : uids) {
    Protocol::BattlePlayerEntity player;
    player.entityId = nextBattleEntityId++;
    player.entityType = Protocol::EntityType::PLAYER;
    player.uid = uid;
    player.position = BattleVector2{static_cast<float>(spawnIndex * 2), 0.0f};
    player.direction = BattleVector2{0.0f, 0.0f};
    player.attribute.speed = battleConfig.playerSpeed;
    player.attribute.maxHP = battleConfig.playerMaxHP;
    player.attribute.currentHP = battleConfig.playerMaxHP;
    auto ownedIt = ownedItemsByUid.find(uid);
    if (ownedIt != ownedItemsByUid.end()) {
      player.items = ownedIt->second;
      auto weapon = equip_weapon_locked(player.items);
      if (weapon != nullptr) {
        player.weaponId = weapon->weaponId;
        battleWeaponsByUid.emplace(uid, std::move(*weapon));
      }
    }
    battlePlayersByUid[uid] = player;
    nextAttackTickByUid[uid] = 0;

    ++spawnIndex;
  }
}

void Room::end_battle_locked(bool won) {
  // Direct test battles have no map node, so they end instead of returning MAP.
  const bool runEnded = !won || mapNodeId < 0 || is_last_map_node_locked();
  reset_battle_state_locked();
  phase = runEnded ? Phase::END : Phase::MAP;
}

bool Room::set_battle_ready(const std::string &uid,
                            Protocol::BattleWaitRsp &rsp, bool &allReady) {
  std::lock_guard<std::mutex> lock(roomMutex);
  // Temporary test shortcut: SHOP may enter battle without selecting a map.
  if (phase != Phase::SHOP && (phase != Phase::MAP || mapNodeId < 0)) {
    return false;
  }
  auto it = battleReadyStates.find(uid);
  if (it == battleReadyStates.end()) {
    return false;
  }

  it->second = true;
  rsp.gameFrame = battleConfig.frameRate;
  rsp.totalCount = static_cast<int>(uids.size());
  rsp.readyCount = static_cast<int>(
      std::count_if(battleReadyStates.begin(), battleReadyStates.end(),
                    [](const auto &entry) { return entry.second; }));

  allReady = rsp.totalCount > 0 && rsp.readyCount == rsp.totalCount;
  if (allReady) {
    start_battle_locked();
  }
  return true;
}

void Room::apply_enemy_reports_locked() {
  // Clients simulate enemy movement, but the server averages valid reports and
  // still enforces speed before accepting them.
  for (auto &[enemyId, enemyState] : battleEnemyStates) {
    enemyState.lastPosition = enemyState.entity.position;
    double x = 0.0;
    double y = 0.0;
    double dx = 0.0;
    double dy = 0.0;
    int count = 0;

    for (const auto &[uid, enemyById] : enemyPosByUid) {
      const auto reportIt = enemyById.find(enemyId);
      if (reportIt == enemyById.end()) {
        continue;
      }
      const auto &report = reportIt->second;
      x += report.position.x;
      y += report.position.y;
      dx += report.direction.x;
      dy += report.direction.y;
      ++count;
    }

    if (count == 0) {
      if (battleTick <= 1) {
        continue;
      }
      // If no client reports this enemy, the server advances it toward target.
      auto *player = live_player_locked(enemyState.entity.targetPlayerUid);
      if (player == nullptr) {
        continue;
      }
      const double attackRange = enemy_range(battleConfig, enemyState);
      const double rangeSquared = attackRange * attackRange;
      if (Battle::distance_squared(enemyState.entity.position,
                                   player->position) <= rangeSquared) {
        enemyState.entity.direction = {0.0, 0.0};
        continue;
      }
      const auto oldPosition = enemyState.entity.position;
      enemyState.entity.position =
          step_to(oldPosition, player->position, enemy_step(enemyState));
      enemyState.entity.direction =
          Battle::norm_or_zero({enemyState.entity.position.x - oldPosition.x,
                                enemyState.entity.position.y - oldPosition.y});
      continue;
    }

    const auto oldPosition = enemyState.entity.position;
    const BattleVector2 target{x / count, y / count};
    enemyState.entity.position =
        step_to(oldPosition, target, enemy_step(enemyState));
    const auto movement =
        Battle::norm_or_zero({enemyState.entity.position.x - oldPosition.x,
                              enemyState.entity.position.y - oldPosition.y});
    enemyState.entity.direction =
        Battle::length_squared(movement) > 0.0
            ? movement
            : Battle::norm_or_zero({dx / count, dy / count});
  }

  enemyPosByUid.clear();
}

void Room::tick_enemy_attacks_locked() {
  // Enemy attacks are authoritative: range and cooldown are checked on server.
  for (auto &[enemyId, enemyState] : battleEnemyStates) {
    if (battleTick < enemyState.nextAttackTick) {
      continue;
    }

    auto *player = live_player_locked(enemyState.entity.targetPlayerUid);
    if (player == nullptr) {
      continue;
    }
    const double attackRange = enemy_range(battleConfig, enemyState);
    if (Battle::distance_squared(enemyState.entity.position, player->position) >
        attackRange * attackRange) {
      continue;
    }

    player->attribute.currentHP =
        std::max(0, player->attribute.currentHP - enemyState.attackDamage);
    push_event_locked(EventType::ENTITY_DAMAGE,
                      DamageParam(enemyState.entity.entityId, EntityType::ENEMY,
                                  player->entityId, EntityType::PLAYER,
                                  enemyState.attackDamage,
                                  player->attribute.currentHP));
    if (player->attribute.currentHP <= 0) {
      push_event_locked(EventType::ENTITY_DESTROY,
                        DestroyParam(player->entityId, EntityType::PLAYER,
                                     DestroyReason::ENTITY_DEAD));
    }
    enemyState.nextAttackTick = battleTick + enemyState.attackCooldownTicks;
  }
}

void Room::tick_spawn_locked() {
  if (spawnedEnemyCount >= battleConfig.targetEnemySpawns) {
    return;
  }
  if (battleTick < nextBattleSpawnTick) {
    return;
  }
  const double time = battle_time_locked();
  const double difficulty = battle_difficulty_locked();
  const double budgetPerInterval = battleConfig.baseSpawnBudget * difficulty;
  while (battleTick >= nextBattleSpawnTick) {
    const int intervalTicks = std::max(
        1, static_cast<int>(std::round(battleConfig.spawnIntervalSeconds *
                                       battleConfig.frameRate)));
    spawnBudget += budgetPerInterval;
    nextBattleSpawnTick += intervalTicks;
  }

  const auto pool = enemy_pool_locked(time, difficulty);

  std::vector<Battle::EnemySpawnSpec> spawns;
  while (1) {
    if (spawnedEnemyCount + static_cast<int>(spawns.size()) >=
        battleConfig.targetEnemySpawns) {
      break;
    }
    auto picked = pick_enemy_locked(pool, spawnBudget);
    if (!picked.has_value()) {
      break;
    }
    picked->position = spawn_pos_locked();
    spawnBudget -= picked->cost;
    spawns.push_back(std::move(*picked));
  }
  spawn_enemies_locked(spawns);
}

void Room::tick_bullets_locked() {
  // Bullet resolution emits hit, damage, and destroy events in gameplay order.
  std::vector<Battle::BulletState> liveBullets;
  liveBullets.reserve(battleBullets.size());

  for (auto &bulletState : battleBullets) {
    auto &bullet = bulletState.entity;
    const double bulletRadius = bullet.attribute.size > 0.0
                                    ? bullet.attribute.size
                                    : battleConfig.bulletRadius;
    const double hitRadius = bulletRadius + battleConfig.enemyRadius;
    const double hitDistanceSquared = hitRadius * hitRadius;
    const BattleVector2 prevPosition =
        bulletState.checkedSpawnPath ? bullet.position : bulletState.spawnFrom;
    bulletState.checkedSpawnPath = true;

    bullet.position.x += bullet.direction.x;
    bullet.position.y += bullet.direction.y;
    bulletState.rangeLeft -=
        std::sqrt(Battle::length_squared(bullet.direction));

    if (bulletState.rangeLeft <= 0.0) {
      push_event_locked(EventType::ENTITY_DESTROY,
                        DestroyParam(bullet.entityId, EntityType::PLAYER_BULLET,
                                     DestroyReason::BULLET_TIMEOUT));
      continue;
    }

    auto enemyIt = battleEnemyStates.end();
    double nearestHitT = std::numeric_limits<double>::infinity();
    for (auto it = battleEnemyStates.begin(); it != battleEnemyStates.end();
         ++it) {
      if (bulletState.hitEnemyIds.count(it->first) > 0) {
        continue;
      }
      const double candidateT =
          segment_pair_hit_t(prevPosition, bullet.position,
                             it->second.lastPosition, it->second.entity.position,
                             hitDistanceSquared);
      if (std::isfinite(candidateT) && candidateT <= nearestHitT) {
        nearestHitT = candidateT;
        enemyIt = it;
      }
    }

    if (enemyIt == battleEnemyStates.end()) {
      liveBullets.push_back(std::move(bulletState));
      continue;
    }

    auto &enemy = enemyIt->second.entity;
    const int damage =
        std::max(1, static_cast<int>(std::round(bulletState.damage *
                                                bulletState.pierceScale)));
    auto sourceIt = battlePlayersByUid.find(bulletState.sourceUid);
    if (sourceIt == battlePlayersByUid.end()) {
      push_event_locked(EventType::ENTITY_DESTROY,
                        DestroyParam(bullet.entityId, EntityType::PLAYER_BULLET,
                                     DestroyReason::UNKNOWN));
      continue;
    }
    hit_enemy_locked(enemy, bullet.entityId, EntityType::PLAYER_BULLET,
                     sourceIt->second, bullet.position, bulletState.weapon,
                     damage, EventType::BULLET_HIT_ENEMY);

    const bool dead = enemy.attribute.currentHP <= 0;
    const bool canPierce = bulletState.remainingPierce > 0;
    if (!canPierce) {
      push_event_locked(EventType::ENTITY_DESTROY,
                        DestroyParam(bullet.entityId, EntityType::PLAYER_BULLET,
                                     DestroyReason::BULLET_HIT_ENTITY));
    } else {
      --bulletState.remainingPierce;
      bulletState.pierceScale *= bulletState.pierceDamageFactor;
      bulletState.hitEnemyIds.insert(enemy.entityId);
      liveBullets.push_back(std::move(bulletState));
    }
    if (dead) {
      push_event_locked(EventType::ENTITY_DESTROY,
                        DestroyParam(enemy.entityId, EntityType::ENEMY,
                                     DestroyReason::ENTITY_DEAD));
      battleEnemyStates.erase(enemyIt);
    }
  }

  battleBullets = std::move(liveBullets);
}

bool Room::tick_battle(Protocol::BattleFrameRsp &frame, bool *ended) {
  std::lock_guard<std::mutex> lock(roomMutex);
  if (ended != nullptr) {
    *ended = false;
  }
  if (phase != Phase::BATTLE || !battleStarted) {
    return false;
  }

  bool battleWon = false;
  bool battleEnded = false;

  if (!battleEnded) {
    apply_enemy_reports_locked();
    tick_bullets_locked();
    tick_spawn_locked();

    std::unordered_map<int, std::string> oldTargets;
    oldTargets.reserve(battleEnemyStates.size());
    for (const auto &[enemyId, enemyState] : battleEnemyStates) {
      oldTargets.emplace(enemyId, enemyState.entity.targetPlayerUid);
    }

    // Refresh before attacks so every enemy uses the nearest living target.
    for (auto &[enemyId, enemyState] : battleEnemyStates) {
      update_target_locked(enemyState);
    }
    tick_enemy_attacks_locked();
    // Refresh again because attacks can kill players and clear/change targets.
    for (auto &[enemyId, enemyState] : battleEnemyStates) {
      update_target_locked(enemyState);
    }
    // Coalesce target changes so one enemy emits at most one event per frame.
    for (const auto &[enemyId, enemyState] : battleEnemyStates) {
      const auto oldIt = oldTargets.find(enemyId);
      const std::string oldTarget =
          oldIt == oldTargets.end() ? std::string() : oldIt->second;
      if (oldTarget != enemyState.entity.targetPlayerUid) {
        push_enemy_intent_locked(enemyState);
      }
    }
    if (all_players_dead_locked()) {
      battleEnded = true;
      battleWon = false;
    }
    if (!battleEnded && spawnedEnemyCount >= battleConfig.targetEnemySpawns &&
        battleEnemyStates.empty()) {
      battleEnded = true;
      battleWon = true;
    }
  }

  frame = build_battle_frame_locked();
  pendingBattleEvents.clear();
  if (battleEnded) {
    end_battle_locked(battleWon);
    if (ended != nullptr) {
      *ended = true;
    }
  }

  ++battleTick;

  return true;
}

// ---- Request handlers ------------------------------------------------------

bool Room::sync_battle(const std::string &uid,
                       const BattleVector2 &playerPosition,
                       const std::vector<Protocol::BattlePos> &enemyPositions) {
  std::lock_guard<std::mutex> lock(roomMutex);
  if (phase != Phase::BATTLE || !battleStarted) {
    return false;
  }

  auto *player = live_player_locked(uid);
  if (player == nullptr) {
    return false;
  }
  if (is_valid_vector(playerPosition)) {
    player->position = playerPosition;
  }

  auto &enemyById = enemyPosByUid[uid];
  enemyById.clear();
  // Invalid enemy reports are dropped before the next tick.
  for (const auto &enemyPosition : enemyPositions) {
    if (!has_enemy_locked(enemyPosition.entityId) ||
        !is_valid_vector(enemyPosition.position) ||
        !is_valid_vector(enemyPosition.direction)) {
      continue;
    }
    enemyById[enemyPosition.entityId] = enemyPosition;
  }
  return true;
}

bool Room::shoot_battle_player(const std::string &uid,
                               const BattleVector2 &direction) {
  std::lock_guard<std::mutex> lock(roomMutex);
  if (phase != Phase::BATTLE || !battleStarted) {
    return false;
  }
  auto *player = live_player_locked(uid);
  auto *weapon = equipped_weapon_locked(uid);
  if (player == nullptr || weapon == nullptr || !is_valid_vector(direction) ||
      Battle::length_squared(direction) <= 0.0) {
    return false;
  }

  auto cooldownIt = nextAttackTickByUid.find(uid);
  if (cooldownIt != nextAttackTickByUid.end() &&
      battleTick < cooldownIt->second) {
    return false;
  }

  const auto attackDir = Battle::norm_or_zero(direction);

  nextAttackTickByUid[uid] = battleTick + cooldown_ticks_locked(*weapon);
  if (weapon->projectileCount == 0) {
    melee_attack_locked(*player, *weapon, attackDir);
    return true;

  } else {
    for (int index = 0; index < weapon->projectileCount; ++index) {
      Battle::BulletState bulletState;
      auto &bullet = bulletState.entity;
      bullet.entityId = nextBattleEntityId++;
      bullet.entityType = Protocol::EntityType::PLAYER_BULLET;
      const double startOffset = player_radius(battleConfig);
      bullet.position =
          BattleVector2{player->position.x + attackDir.x * startOffset,
                        player->position.y + attackDir.y * startOffset};
      bullet.type = Protocol::BattleBulletType::PLAYER_BULLET;
      auto projectile = weapon->projectile;
      if (projectile.speed <= 0.0) {
        projectile.speed = battleConfig.bulletSpeed;
      }
      bullet.direction = BattleVector2{attackDir.x * projectile.speed,
                                       attackDir.y * projectile.speed};
      bulletState.sourceUid = player->uid;
      bulletState.spawnFrom = player->position;
      bulletState.damage = weapon_damage_locked(*weapon);
      bulletState.rangeLeft = battle_range(*weapon);
      bullet.attribute = bullet_attribute(projectile);
      bulletState.weapon = *weapon;
      bulletState.remainingPierce =
          projectile.canPierce ? projectile.pierceCount : 0;
      bulletState.pierceDamageFactor = projectile.pierceDamageFactor;
      battleBullets.push_back(bulletState);

      push_event_locked(EventType::BULLET_SPAWN, SpawnParam(bullet));
    }
    return true;
  }

  return false;
}
