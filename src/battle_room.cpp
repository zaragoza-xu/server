#include "room.h"

#include <algorithm>
#include <cmath>

namespace {
constexpr double kPi = 3.14159265358979323846;

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

bool is_outside_battle(const Protocol::BattleVector2 &position,
                       const BattleConfig &cfg) {
  return position.x < cfg.battleMin || position.x > cfg.battleMax ||
         position.y < cfg.battleMin || position.y > cfg.battleMax;
}

Protocol::BattleVector2 clamp_battle(Protocol::BattleVector2 position,
                                     const BattleConfig &cfg) {
  position.x = std::clamp(position.x, cfg.battleMin, cfg.battleMax);
  position.y = std::clamp(position.y, cfg.battleMin, cfg.battleMax);
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
                                double maxDistance, const BattleConfig &cfg) {
  // Server-side movement is capped even when clients report a far-away target.
  const Protocol::BattleVector2 delta{to.x - from.x, to.y - from.y};
  const double distSquared = length_squared(delta);
  if (!std::isfinite(distSquared) || distSquared <= 0.0) {
    return clamp_battle(from, cfg);
  }
  if (distSquared <= maxDistance * maxDistance) {
    return clamp_battle(to, cfg);
  }
  const double dist = std::sqrt(distSquared);
  return clamp_battle({from.x + delta.x / dist * maxDistance,
                       from.y + delta.y / dist * maxDistance},
                      cfg);
}

std::optional<BattleEnemyPool>
pool_for_node(Protocol::MapNode::NodeType nodeType) {
  switch (nodeType) {
  case Protocol::MapNode::NodeType::NORMAL:
    return BattleEnemyPool::NORMAL;
  case Protocol::MapNode::NodeType::ELITE:
    return BattleEnemyPool::ELITE;
  case Protocol::MapNode::NodeType::BOSS:
    return BattleEnemyPool::BOSS;
  case Protocol::MapNode::NodeType::EVENT:
    return std::nullopt;
  }
  return BattleEnemyPool::NORMAL;
}
} // namespace

void Room::reset_battle_state_locked() {
  battleStarted = false;
  battleTick = 0;
  nextBattleEntityId = 1;
  nextBattleSpawnTick = 1;
  battlePlayersByUid.clear();
  battleEnemyStates.clear();
  enemyPosByUid.clear();
  battleBullets.clear();
  pendingBattleEvents.clear();
  for (const auto &uid : uids) {
    battleReadyStates[uid] = false;
  }
}

void Room::end_battle_locked(bool won) {
  const bool runEnded = !won || is_last_map_node_locked();
  reset_battle_state_locked();
  phase = runEnded ? Phase::END : Phase::MAP;
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
  // frame.enemyEntities.reserve(battleEnemyStates.size());
  // for (const auto &[entityId, enemyState] : battleEnemyStates) {
  //   frame.enemyEntities.push_back(enemyState.entity);
  // }
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

double Room::battle_time_locked() const {
  return static_cast<double>(battleTick) / battleConfig.frameRate;
}

double Room::battle_difficulty_locked() const {
  return 1.0 + battleConfig.difficultyGrowth * battle_time_locked();
}

std::vector<Room::EnemySpawnSpec>
Room::enemy_pool_locked(double time, double difficulty) const {
  std::vector<EnemySpawnSpec> pool;
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

    EnemySpawnSpec spec;
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

std::optional<Room::EnemySpawnSpec>
Room::pick_enemy_locked(const std::vector<EnemySpawnSpec> &pool,
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

Protocol::BattleVector2 Room::spawn_pos_locked() {
  std::vector<Protocol::BattleVector2> anchors;
  anchors.reserve(battlePlayersByUid.size());
  for (const auto &[uid, player] : battlePlayersByUid) {
    if (player.attribute.currentHP > 0) {
      anchors.push_back(player.position);
    }
  }

  Protocol::BattleVector2 anchor{0.0, 0.0};
  if (!anchors.empty()) {
    std::uniform_int_distribution<size_t> playerRoll(0, anchors.size() - 1);
    anchor = anchors[playerRoll(battleRng)];
  }

  std::uniform_real_distribution<double> angleRoll(0.0, kPi * 2.0);
  std::uniform_real_distribution<double> radiusRoll(
      battleConfig.spawnRadiusMin, battleConfig.spawnRadiusMax);

  Protocol::BattleVector2 candidate{anchor.x + battleConfig.spawnRadiusMin,
                                    anchor.y};
  for (int tries = 0; tries < 16; ++tries) {
    const double angle = angleRoll(battleRng);
    const double radius = radiusRoll(battleRng);
    candidate = {anchor.x + std::cos(angle) * radius,
                 anchor.y + std::sin(angle) * radius};
    if (!is_outside_battle(candidate, battleConfig)) {
      return candidate;
    }
  }
  return clamp_battle(candidate, battleConfig);
}

void Room::spawn_enemies_locked(const std::vector<EnemySpawnSpec> &spawns) {
  battleEnemyStates.reserve(battleEnemyStates.size() + spawns.size());
  for (const auto &spec : spawns) {
    BattleEnemyState enemyState;
    const int entityId = nextBattleEntityId++;
    enemyState.entity.entityId = entityId;
    enemyState.entity.entityType = Protocol::EntityType::ENEMY;
    enemyState.entity.enemyType = spec.enemyType;
    enemyState.entity.position = spec.position;
    enemyState.entity.direction = Protocol::BattleVector2{0.0, 0.0};
    enemyState.entity.attribute.maxHP = spec.maxHP;
    enemyState.entity.attribute.currentHP = spec.maxHP;
    enemyState.attackRange = spec.attackRange;
    enemyState.maxSpeed = spec.maxSpeed;
    enemyState.attackDamage = spec.attackDamage;
    enemyState.attackCooldownTicks = spec.attackCooldownTicks;
    // Initial target is part of the spawn frame so clients can animate at once.
    const bool targetChanged = update_target_locked(enemyState);

    push_event_locked(EventType::ENEMY_SPAWN, SpawnParam(enemyState.entity));
    if (targetChanged) {
      push_enemy_intent_locked(enemyState);
    }

    battleEnemyStates.emplace(entityId, std::move(enemyState));
  }
}

void Room::tick_spawn_locked() {
  if (battleTick < nextBattleSpawnTick) {
    return;
  }
  while (battleTick >= nextBattleSpawnTick) {
    const int intervalTicks = std::max(
        1, static_cast<int>(std::round(battleConfig.spawnIntervalSeconds *
                                       battleConfig.frameRate)));
    nextBattleSpawnTick += intervalTicks;
  }

  const double time = battle_time_locked();
  const double difficulty = battle_difficulty_locked();
  double budget = battleConfig.baseSpawnBudget * difficulty;
  const auto pool = enemy_pool_locked(time, difficulty);

  std::vector<EnemySpawnSpec> spawns;
  while (1) {
    auto picked = pick_enemy_locked(pool, budget);
    if (!picked.has_value()) {
      break;
    }
    picked->position = spawn_pos_locked();
    budget -= picked->cost;
    spawns.push_back(std::move(*picked));
  }
  spawn_enemies_locked(spawns);
}

bool Room::has_enemy_locked(int entityId) const {
  return battleEnemyStates.find(entityId) != battleEnemyStates.end();
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
  for (auto &[enemyId, enemyState] : battleEnemyStates) {
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
      const double rangeSquared =
          enemyState.attackRange * enemyState.attackRange;
      if (distance_squared(enemyState.entity.position, player->position) <=
          rangeSquared) {
        enemyState.entity.direction = {0.0, 0.0};
        continue;
      }
      const auto oldPosition = enemyState.entity.position;
      enemyState.entity.position = step_to(oldPosition, player->position,
                                           enemyState.maxSpeed, battleConfig);
      enemyState.entity.direction =
          norm_or_zero({enemyState.entity.position.x - oldPosition.x,
                        enemyState.entity.position.y - oldPosition.y});
      continue;
    }

    const auto oldPosition = enemyState.entity.position;
    const Protocol::BattleVector2 target{x / count, y / count};
    enemyState.entity.position =
        step_to(oldPosition, target, enemyState.maxSpeed, battleConfig);
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
  const double hitRadius = battleConfig.bulletRadius + battleConfig.enemyRadius;
  const double hitDistanceSquared = hitRadius * hitRadius;

  std::vector<BulletState> liveBullets;
  liveBullets.reserve(battleBullets.size());

  for (auto &bulletState : battleBullets) {
    auto &bullet = bulletState.entity;
    bullet.position.x += bullet.direction.x;
    bullet.position.y += bullet.direction.y;

    if (is_outside_battle(bullet.position, battleConfig)) {
      push_event_locked(EventType::BULLET_HIT_WALL,
                        HitParam(bullet.entityId, EntityType::PLAYER_BULLET, 0,
                                 EntityType::WALL, bullet.position));
      push_event_locked(EventType::ENTITY_DESTROY,
                        DestroyParam(bullet.entityId, EntityType::PLAYER_BULLET,
                                     DestroyReason::BULLET_HIT_WALL));
      continue;
    }

    auto enemyIt = battleEnemyStates.end();
    double nearestHitDistance = hitDistanceSquared;
    for (auto it = battleEnemyStates.begin(); it != battleEnemyStates.end();
         ++it) {
      const double candidateDistance =
          distance_squared(bullet.position, it->second.entity.position);
      if (candidateDistance <= nearestHitDistance) {
        nearestHitDistance = candidateDistance;
        enemyIt = it;
      }
    }

    if (enemyIt == battleEnemyStates.end()) {
      liveBullets.push_back(std::move(bulletState));
      continue;
    }

    auto &enemy = enemyIt->second.entity;
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
  for (auto &[enemyId, enemyState] : battleEnemyStates) {
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
  phase = Phase::BATTLE;
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
    player.attribute.maxHP = battleConfig.playerMaxHP;
    player.attribute.currentHP = battleConfig.playerMaxHP;
    auto ownedIt = ownedItemsByUid.find(uid);
    if (ownedIt != ownedItemsByUid.end()) {
      player.items = ownedIt->second;
    }
    battlePlayersByUid[uid] = player;

    ++spawnIndex;
  }
}

bool Room::set_battle_ready(const std::string &uid,
                            Protocol::BattleWaitRsp &rsp, bool &allReady) {
  std::lock_guard<std::mutex> lock(roomMutex);
  if (phase != Phase::MAP || mapNodeId < 0) {
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

bool Room::sync_battle(const std::string &uid,
                       const Protocol::BattleVector2 &playerPosition,
                       const Protocol::BattleVector2 &playerDirection,
                       const std::vector<Protocol::BattlePos> &enemyPositions) {
  std::lock_guard<std::mutex> lock(roomMutex);
  if (phase != Phase::BATTLE || !battleStarted) {
    return false;
  }

  auto *player = live_player_locked(uid);
  if (player == nullptr) {
    return false;
  }
  if (is_valid_vector(playerPosition) && is_valid_vector(playerDirection)) {
    player->position = clamp_battle(playerPosition, battleConfig);
    player->direction = playerDirection;
  }

  auto &enemyById = enemyPosByUid[uid];
  enemyById.clear();
  // Invalid or out-of-bounds enemy reports are dropped before the next tick.
  for (const auto &enemyPosition : enemyPositions) {
    if (!has_enemy_locked(enemyPosition.entityId) ||
        !is_valid_vector(enemyPosition.position) ||
        !is_valid_vector(enemyPosition.direction) ||
        is_outside_battle(enemyPosition.position, battleConfig)) {
      continue;
    }
    enemyById[enemyPosition.entityId] = enemyPosition;
  }
  return true;
}

bool Room::shoot_battle_player(const std::string &uid,
                               const Protocol::BattleVector2 &direction) {
  std::lock_guard<std::mutex> lock(roomMutex);
  if (phase != Phase::BATTLE || !battleStarted) {
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
  bullet.type = Protocol::BattleBulletType::PLAYER_BULLET;
  const double directionLength = std::sqrt(directionLengthSquared);
  bullet.direction = Protocol::BattleVector2{
      direction.x / directionLength * battleConfig.bulletSpeed,
      direction.y / directionLength * battleConfig.bulletSpeed};
  bulletState.sourcePlayerId = player->entityId;
  bulletState.damage = battleConfig.bulletDamage;
  battleBullets.push_back(bulletState);

  push_event_locked(EventType::BULLET_SPAWN, SpawnParam(bullet));
  return true;
}

bool Room::tick_battle(Protocol::BattleFrameRsp &frame, bool *ended) {
  std::lock_guard<std::mutex> lock(roomMutex);
  if (ended != nullptr) {
    *ended = false;
  }
  if (phase != Phase::BATTLE || !battleStarted) {
    return false;
  }

  ++battleTick;

  bool battleWon = battle_time_locked() >= battleConfig.durationSeconds;
  bool battleEnded = battleWon;

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
  }

  frame = build_battle_frame_locked();
  pendingBattleEvents.clear();
  if (battleEnded) {
    end_battle_locked(battleWon);
    if (ended != nullptr) {
      *ended = true;
    }
  }
  return true;
}
