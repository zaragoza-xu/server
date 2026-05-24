#include "room.h"

#include <algorithm>
#include <cmath>

namespace {
// Battle tuning stays local until enemy/player configs become data driven.
constexpr int kPlayerMaxHP = 20;
constexpr int kEnemyMaxHP = 10;
constexpr double kEnemyAttackRange = 1.5;
constexpr double kEnemyMaxSpeed = 1.0;
constexpr int kEnemyAttackDamage = 4;
constexpr int kEnemyAttackCooldownTicks = 20;
constexpr int kBulletDamage = 5;
constexpr double kBulletSpeed = 1.0;
constexpr double kBulletRadius = 0.25;
constexpr double kEnemyRadius = 0.75;
constexpr double kBattleMin = -20.0;
constexpr double kBattleMax = 20.0;

double distance_squared(const Protocol::BattleVector2 &lhs,
                        const Protocol::BattleVector2 &rhs) {
  const double dx = lhs.x - rhs.x;
  const double dy = lhs.y - rhs.y;
  return dx * dx + dy * dy;
}

double length_squared(const Protocol::BattleVector2 &value) {
  return value.x * value.x + value.y * value.y;
}

bool is_valid_vector(const Protocol::BattleVector2 &value) {
  return std::isfinite(value.x) && std::isfinite(value.y);
}

bool is_outside_battle(const Protocol::BattleVector2 &position) {
  return position.x < kBattleMin || position.x > kBattleMax ||
         position.y < kBattleMin || position.y > kBattleMax;
}

Protocol::BattleVector2 clamp_battle(Protocol::BattleVector2 position) {
  position.x = std::clamp(position.x, kBattleMin, kBattleMax);
  position.y = std::clamp(position.y, kBattleMin, kBattleMax);
  return position;
}

Protocol::BattleVector2 norm_or_zero(const Protocol::BattleVector2 &value) {
  const double lengthSquared = length_squared(value);
  if (!std::isfinite(lengthSquared) || lengthSquared <= 0.0) {
    return {0.0, 0.0};
  }
  const double length = std::sqrt(lengthSquared);
  return {value.x / length, value.y / length};
}

Protocol::BattleVector2 step_to(const Protocol::BattleVector2 &from,
                                const Protocol::BattleVector2 &to,
                                double maxDistance) {
  // Server-side movement is capped even when clients report a far-away target.
  const Protocol::BattleVector2 delta{to.x - from.x, to.y - from.y};
  const double distSquared = length_squared(delta);
  if (!std::isfinite(distSquared) || distSquared <= 0.0) {
    return clamp_battle(from);
  }
  if (distSquared <= maxDistance * maxDistance) {
    return clamp_battle(to);
  }
  const double dist = std::sqrt(distSquared);
  return clamp_battle({from.x + delta.x / dist * maxDistance,
                       from.y + delta.y / dist * maxDistance});
}
} // namespace

void Room::reset_battle_state_locked() {
  battleStarted = false;
  battleTick = 0;
  nextBattleEntityId = 1;
  battlePlayersByUid.clear();
  battleEnemyStates.clear();
  enemyPosByUid.clear();
  battleBullets.clear();
  pendingBattleEvents.clear();
  for (const auto &uid : uids) {
    battleReadyStates[uid] = false;
  }
}

void Room::end_battle_locked() { reset_battle_state_locked(); }

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
  frame.enemyEntities.reserve(battleEnemyStates.size());
  for (const auto &enemyState : battleEnemyStates) {
    frame.enemyEntities.push_back(enemyState.entity);
  }
  frame.bulletEntities.reserve(battleBullets.size());
  for (const auto &bulletState : battleBullets) {
    frame.bulletEntities.push_back(bulletState.entity);
  }
  frame.events = pendingBattleEvents;
  return frame;
}

Protocol::MapNode::NodeType Room::resolve_map_node_type_locked() const {
  const int nodeIndex = get_map_node_index(mapNodeId);
  if (nodeIndex < 0) {
    return Protocol::MapNode::NodeType::NORMAL;
  }
  return mapNodes[static_cast<size_t>(nodeIndex)].type;
}

std::vector<Room::EnemySpawnSpec>
Room::build_spawn_plan_locked(Protocol::MapNode::NodeType nodeType) const {
  // Map node type decides the spawn count for the current battle.
  int enemyCount = 0;
  switch (nodeType) {
  case Protocol::MapNode::NodeType::NORMAL:
    enemyCount = 1;
    break;
  case Protocol::MapNode::NodeType::ELITE:
    enemyCount = 2;
    break;
  case Protocol::MapNode::NodeType::BOSS:
    enemyCount = 3;
    break;
  case Protocol::MapNode::NodeType::EVENT:
    enemyCount = 0;
    break;
  }

  std::vector<EnemySpawnSpec> spawnPlan;
  spawnPlan.reserve(static_cast<size_t>(enemyCount));
  for (int index = 0; index < enemyCount; ++index) {
    EnemySpawnSpec spec;
    spec.enemyType = Protocol::BattleEnemyType::BUBBLE_FISH;
    spec.maxHP = kEnemyMaxHP;
    spec.position =
        Protocol::BattleVector2{static_cast<double>(index * 2), 4.0};
    spawnPlan.push_back(spec);
  }
  return spawnPlan;
}

void Room::spawn_enemies_locked(const std::vector<EnemySpawnSpec> &spawnPlan) {
  battleEnemyStates.reserve(battleEnemyStates.size() + spawnPlan.size());
  for (const auto &spec : spawnPlan) {
    BattleEnemyState enemyState;
    enemyState.entity.entityId = nextBattleEntityId++;
    enemyState.entity.entityType = Protocol::EntityType::ENEMY;
    enemyState.entity.enemyType = spec.enemyType;
    enemyState.entity.position = spec.position;
    enemyState.entity.direction = Protocol::BattleVector2{0.0, 0.0};
    enemyState.entity.attribute.maxHP = spec.maxHP;
    enemyState.entity.attribute.currentHP = spec.maxHP;
    enemyState.attackRange = kEnemyAttackRange;
    enemyState.maxSpeed = kEnemyMaxSpeed;
    enemyState.attackDamage = kEnemyAttackDamage;
    enemyState.attackCooldownTicks = kEnemyAttackCooldownTicks;
    // Initial target is part of the spawn frame so clients can animate at once.
    const bool targetChanged = update_target_locked(enemyState);

    push_event_locked(EventType::ENEMY_SPAWN, SpawnParam(enemyState.entity));
    if (targetChanged) {
      push_enemy_intent_locked(enemyState);
    }

    battleEnemyStates.push_back(std::move(enemyState));
  }
}

bool Room::has_enemy_locked(int entityId) const {
  return std::any_of(battleEnemyStates.begin(), battleEnemyStates.end(),
                     [entityId](const BattleEnemyState &enemyState) {
                       return enemyState.entity.entityId == entityId;
                     });
}

bool Room::all_players_dead_locked() const {
  if (battlePlayersByUid.empty()) {
    return false;
  }
  return std::all_of(
      battlePlayersByUid.begin(), battlePlayersByUid.end(),
      [](const auto &entry) { return entry.second.attribute.currentHP <= 0; });
}

Protocol::BattlePlayerEntity *Room::live_player_locked(const std::string &uid) {
  // Dead players are intentionally invisible to target selection and actions.
  auto it = battlePlayersByUid.find(uid);
  if (it == battlePlayersByUid.end() || it->second.attribute.currentHP <= 0) {
    return nullptr;
  }
  return &it->second;
}

void Room::apply_enemy_reports_locked() {
  // Clients simulate enemy movement, but the server averages valid reports and
  // still enforces speed/bounds before accepting them.
  for (auto &enemyState : battleEnemyStates) {
    double x = 0.0;
    double y = 0.0;
    double dx = 0.0;
    double dy = 0.0;
    int count = 0;

    for (const auto &[uid, enemyById] : enemyPosByUid) {
      const auto reportIt = enemyById.find(enemyState.entity.entityId);
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
      const double rangeSquared =
          enemyState.attackRange * enemyState.attackRange;
      if (distance_squared(enemyState.entity.position, player->position) <=
          rangeSquared) {
        enemyState.entity.direction = {0.0, 0.0};
        continue;
      }
      const auto oldPosition = enemyState.entity.position;
      enemyState.entity.position =
          step_to(oldPosition, player->position, enemyState.maxSpeed);
      enemyState.entity.direction =
          norm_or_zero({enemyState.entity.position.x - oldPosition.x,
                        enemyState.entity.position.y - oldPosition.y});
      continue;
    }

    const auto oldPosition = enemyState.entity.position;
    const Protocol::BattleVector2 target{x / count, y / count};
    enemyState.entity.position =
        step_to(oldPosition, target, enemyState.maxSpeed);
    const auto movement =
        norm_or_zero({enemyState.entity.position.x - oldPosition.x,
                      enemyState.entity.position.y - oldPosition.y});
    enemyState.entity.direction = length_squared(movement) > 0.0
                                      ? movement
                                      : norm_or_zero({dx / count, dy / count});
  }

  enemyPosByUid.clear();
}

void Room::tick_bullets_locked() {
  // Bullet resolution emits hit, damage, and destroy events in gameplay order.
  constexpr double hitDistanceSquared =
      (kBulletRadius + kEnemyRadius) * (kBulletRadius + kEnemyRadius);

  std::vector<BulletState> liveBullets;
  liveBullets.reserve(battleBullets.size());

  for (auto &bulletState : battleBullets) {
    auto &bullet = bulletState.entity;
    bullet.position.x += bullet.direction.x;
    bullet.position.y += bullet.direction.y;

    if (is_outside_battle(bullet.position)) {
      push_event_locked(EventType::BULLET_HIT_WALL,
                        HitParam(bullet.entityId, EntityType::PLAYER_BULLET, 0,
                                 EntityType::WALL, bullet.position));
      push_event_locked(EventType::ENTITY_DESTROY,
                        DestroyParam(bullet.entityId, EntityType::PLAYER_BULLET,
                                     DestroyReason::BULLET_HIT_WALL));
      continue;
    }

    auto enemyIt = std::find_if(
        battleEnemyStates.begin(), battleEnemyStates.end(),
        [&bullet, hitDistanceSquared](const BattleEnemyState &enemyState) {
          return distance_squared(bullet.position,
                                  enemyState.entity.position) <=
                 hitDistanceSquared;
        });

    if (enemyIt == battleEnemyStates.end()) {
      liveBullets.push_back(std::move(bulletState));
      continue;
    }

    auto &enemy = enemyIt->entity;
    push_event_locked(EventType::BULLET_HIT_ENEMY,
                      HitParam(bullet.entityId, EntityType::PLAYER_BULLET,
                               enemy.entityId, EntityType::ENEMY,
                               bullet.position));

    enemy.attribute.currentHP =
        std::max(0, enemy.attribute.currentHP - bulletState.damage);
    push_event_locked(EventType::ENTITY_DAMAGE,
                      DamageParam(bulletState.sourcePlayerId,
                                  EntityType::PLAYER, enemy.entityId,
                                  EntityType::ENEMY, bulletState.damage,
                                  enemy.attribute.currentHP));
    push_event_locked(EventType::ENTITY_DESTROY,
                      DestroyParam(bullet.entityId, EntityType::PLAYER_BULLET,
                                   DestroyReason::BULLET_HIT_ENTITY));

    if (enemy.attribute.currentHP <= 0) {
      push_event_locked(EventType::ENTITY_DESTROY,
                        DestroyParam(enemy.entityId, EntityType::ENEMY,
                                     DestroyReason::ENTITY_DEAD));
      battleEnemyStates.erase(enemyIt);
    }
  }

  battleBullets = std::move(liveBullets);
}

void Room::tick_enemy_attacks_locked() {
  // Enemy attacks are authoritative: range and cooldown are checked on server.
  for (auto &enemyState : battleEnemyStates) {
    if (battleTick < enemyState.nextAttackTick) {
      continue;
    }

    auto *player = live_player_locked(enemyState.entity.targetPlayerUid);
    if (player == nullptr) {
      continue;
    }
    if (distance_squared(enemyState.entity.position, player->position) >
        enemyState.attackRange * enemyState.attackRange) {
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

bool Room::update_target_locked(BattleEnemyState &enemyState) {
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
        distance_squared(enemyState.entity.position, player->position);
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

void Room::push_enemy_intent_locked(const BattleEnemyState &enemyState) {
  // This event means target changed; clients derive chase/idle visuals from
  // uid.
  push_event_locked(EventType::ENEMY_INTENT_CHANGE,
                    IntentParam(enemyState.entity.entityId,
                                enemyState.entity.targetPlayerUid));
}

void Room::start_battle_locked() {
  reset_battle_state_locked();
  battleStarted = true;

  int spawnIndex = 0;
  for (const auto &uid : uids) {
    Protocol::BattlePlayerEntity player;
    player.entityId = nextBattleEntityId++;
    player.entityType = Protocol::EntityType::PLAYER;
    player.uid = uid;
    player.position =
        Protocol::BattleVector2{static_cast<float>(spawnIndex * 2), 0.0f};
    player.direction = Protocol::BattleVector2{0.0f, 0.0f};
    player.attribute.maxHP = kPlayerMaxHP;
    player.attribute.currentHP = kPlayerMaxHP;
    auto ownedIt = ownedItemsByUid.find(uid);
    if (ownedIt != ownedItemsByUid.end()) {
      player.items = ownedIt->second;
    }
    battlePlayersByUid[uid] = player;

    ++spawnIndex;
  }

  const auto nodeType = resolve_map_node_type_locked();
  const auto spawnPlan = build_spawn_plan_locked(nodeType);
  spawn_enemies_locked(spawnPlan);
}

bool Room::set_battle_ready(const std::string &uid,
                            Protocol::BattleWaitRsp &rsp, bool &allReady) {
  std::lock_guard<std::mutex> lock(roomMutex);
  auto it = battleReadyStates.find(uid);
  if (it == battleReadyStates.end()) {
    return false;
  }

  it->second = true;
  rsp.gameFrame = 60;
  rsp.totalCount = static_cast<int>(uids.size());
  rsp.readyCount = static_cast<int>(
      std::count_if(battleReadyStates.begin(), battleReadyStates.end(),
                    [](const auto &entry) { return entry.second; }));

  allReady = rsp.totalCount > 0 && rsp.readyCount == rsp.totalCount;
  if (allReady && !battleStarted) {
    start_battle_locked();
  }
  return true;
}

bool Room::sync_battle(const std::string &uid,
                       const Protocol::BattleVector2 &playerPosition,
                       const Protocol::BattleVector2 &playerDirection,
                       const std::vector<Protocol::BattlePos> &enemyPositions) {
  std::lock_guard<std::mutex> lock(roomMutex);
  if (!battleStarted) {
    return false;
  }
  auto it = battlePlayersByUid.find(uid);
  if (it == battlePlayersByUid.end()) {
    return false;
  }

  if (it->second.attribute.currentHP > 0 && is_valid_vector(playerPosition) &&
      is_valid_vector(playerDirection)) {
    it->second.position = clamp_battle(playerPosition);
    it->second.direction = playerDirection;
  }

  auto &enemyById = enemyPosByUid[uid];
  enemyById.clear();
  // Invalid or out-of-bounds enemy reports are dropped before the next tick.
  for (const auto &enemyPosition : enemyPositions) {
    if (!has_enemy_locked(enemyPosition.entityId) ||
        !is_valid_vector(enemyPosition.position) ||
        !is_valid_vector(enemyPosition.direction) ||
        is_outside_battle(enemyPosition.position)) {
      continue;
    }
    enemyById[enemyPosition.entityId] = enemyPosition;
  }
  return true;
}

bool Room::shoot_battle_player(const std::string &uid,
                               const Protocol::BattleVector2 &direction) {
  std::lock_guard<std::mutex> lock(roomMutex);
  if (!battleStarted) {
    return false;
  }
  auto *player = live_player_locked(uid);
  if (player == nullptr) {
    return false;
  }

  if (!is_valid_vector(direction)) {
    return false;
  }
  const double directionLengthSquared =
      direction.x * direction.x + direction.y * direction.y;
  if (!std::isfinite(directionLengthSquared) || directionLengthSquared <= 0.0) {
    return false;
  }

  BulletState bulletState;
  auto &bullet = bulletState.entity;
  bullet.entityId = nextBattleEntityId++;
  bullet.entityType = Protocol::EntityType::PLAYER_BULLET;
  bullet.position = player->position;
  const double directionLength = std::sqrt(directionLengthSquared);
  bullet.direction =
      Protocol::BattleVector2{direction.x / directionLength * kBulletSpeed,
                              direction.y / directionLength * kBulletSpeed};
  bulletState.sourcePlayerId = player->entityId;
  bulletState.damage = kBulletDamage;
  battleBullets.push_back(bulletState);

  push_event_locked(EventType::BULLET_SPAWN, SpawnParam(bullet));
  return true;
}

bool Room::tick_battle(Protocol::BattleFrameRsp &frame, bool *ended) {
  std::lock_guard<std::mutex> lock(roomMutex);
  if (ended != nullptr) {
    *ended = false;
  }
  if (!battleStarted) {
    return false;
  }

  ++battleTick;

  apply_enemy_reports_locked();

  tick_bullets_locked();

  bool battleEnded = battleEnemyStates.empty();

  if (!battleEnded) {
    std::vector<std::pair<int, std::string>> oldTargets;
    oldTargets.reserve(battleEnemyStates.size());
    for (const auto &enemyState : battleEnemyStates) {
      oldTargets.emplace_back(enemyState.entity.entityId,
                              enemyState.entity.targetPlayerUid);
    }

    // Refresh before attacks so every enemy uses the nearest living target.
    for (auto &enemyState : battleEnemyStates) {
      update_target_locked(enemyState);
    }
    tick_enemy_attacks_locked();
    // Refresh again because attacks can kill players and clear/change targets.
    for (auto &enemyState : battleEnemyStates) {
      update_target_locked(enemyState);
    }
    // Coalesce target changes so one enemy emits at most one event per frame.
    for (const auto &enemyState : battleEnemyStates) {
      const auto oldIt = std::find_if(
          oldTargets.begin(), oldTargets.end(), [&enemyState](const auto &old) {
            return old.first == enemyState.entity.entityId;
          });
      const std::string oldTarget =
          oldIt == oldTargets.end() ? std::string() : oldIt->second;
      if (oldTarget != enemyState.entity.targetPlayerUid) {
        push_enemy_intent_locked(enemyState);
      }
    }
    if (all_players_dead_locked()) {
      battleEnded = true;
    }
  }

  frame = build_battle_frame_locked();
  pendingBattleEvents.clear();
  if (battleEnded) {
    end_battle_locked();
    if (ended != nullptr) {
      *ended = true;
    }
  }
  return true;
}
