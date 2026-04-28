#include "room.h"

#include <algorithm>
#include <cmath>

void Room::reset_battle_state_locked() {
  battleStarted = false;
  battleTick = 0;
  nextBattleEntityId = 1;
  battlePlayersByUid.clear();
  battleEnemies.clear();
  battleBullets.clear();
  pendingBattleEvents.clear();
  for (const auto &uid : uids) {
    battleReadyStates[uid] = false;
  }
}

Protocol::BattleFrameRsp Room::build_battle_frame_locked(
    const std::vector<Protocol::BattleEventDTO> &events) const {
  Protocol::BattleFrameRsp frame;
  frame.serverTick = battleTick;
  frame.playerEntities.reserve(uids.size());
  for (const auto &uid : uids) {
    auto it = battlePlayersByUid.find(uid);
    if (it != battlePlayersByUid.end()) {
      frame.playerEntities.push_back(it->second);
    }
  }
  frame.enemyEntities = battleEnemies;
  frame.bulletEntities = battleBullets;
  frame.events = events;
  return frame;
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

bool Room::move_battle_player(const std::string &uid,
                              const Protocol::BattleVector2 &input) {
  std::lock_guard<std::mutex> lock(roomMutex);
  if (!battleStarted) {
    return false;
  }
  auto it = battlePlayersByUid.find(uid);
  if (it == battlePlayersByUid.end()) {
    return false;
  }

  it->second.direction = input;
  return true;
}

bool Room::shoot_battle_player(const std::string &uid,
                               const Protocol::BattleVector2 &direction) {
  std::lock_guard<std::mutex> lock(roomMutex);
  if (!battleStarted) {
    return false;
  }
  auto playerIt = battlePlayersByUid.find(uid);
  if (playerIt == battlePlayersByUid.end()) {
    return false;
  }

  playerIt->second.direction = direction;

  Protocol::BattleBulletEntity bullet;
  bullet.entityId = nextBattleEntityId++;
  bullet.entityType = Protocol::EntityType::PLAYER_BULLET;
  bullet.position = playerIt->second.position;
  bullet.direction = direction;
  battleBullets.push_back(bullet);

  Protocol::BattleEventDTO event;
  event.eventType = Protocol::BattleEventType::BULLET_SPAWN;
  event.eventTick = battleTick;
  Protocol::BattleEventDTO::SpawnParameter spawn;
  spawn.entityId = bullet.entityId;
  spawn.entityType = Protocol::EntityType::PLAYER_BULLET;
  spawn.bulletEntity = bullet;
  event.spawnParameter = spawn;
  pendingBattleEvents.push_back(std::move(event));
  return true;
}

bool Room::tick_battle(Protocol::BattleFrameRsp &frame) {
  std::lock_guard<std::mutex> lock(roomMutex);
  if (!battleStarted) {
    return false;
  }

  ++battleTick;

  for (auto &[uid, player] : battlePlayersByUid) {
    const auto &dir = player.direction;
    double len = std::sqrt(dir.x * dir.x + dir.y * dir.y);
    if (len > 1e-18) {
      double speed = player.attribute.velocity;
      player.position.x += (dir.x / len) * speed;
      player.position.y += (dir.y / len) * speed;
    }
  }

  for (auto &bullet : battleBullets) {
    bullet.position.x += bullet.direction.x;
    bullet.position.y += bullet.direction.y;
  }

  frame = build_battle_frame_locked(pendingBattleEvents);
  pendingBattleEvents.clear();
  return true;
}
