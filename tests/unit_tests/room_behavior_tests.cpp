#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include <gtest/gtest.h>

#include "room.h"
#include "server.h"
#include "user.h"

namespace {

struct RoomHarness {
  std::shared_ptr<ServerState> state;
  std::shared_ptr<User> creator;
  std::unique_ptr<Room> room;
  std::string uid;
};

RoomHarness make_room(int maximumPeople = 1) {
  auto state = std::make_shared<ServerState>();
  Protocol::PlayerBasicInfo creatorInfo{"1", "creator", 1};
  state->userData.emplace(creatorInfo.uid,
                          Protocol::PlayerData{.basicInfo = creatorInfo});
  auto creator = std::make_shared<User>(creatorInfo.uid, state);
  auto room = std::make_unique<Room>(10, maximumPeople, state, creator);
  return RoomHarness{state, creator, std::move(room), creatorInfo.uid};
}

int find_event(const std::vector<Protocol::BattleEventDTO> &events,
               Protocol::BattleEventType type, size_t start = 0) {
  for (size_t index = start; index < events.size(); ++index) {
    if (events[index].eventType == type) {
      return static_cast<int>(index);
    }
  }
  return -1;
}

int count_event(const std::vector<Protocol::BattleEventDTO> &events,
                Protocol::BattleEventType type) {
  return static_cast<int>(
      std::count_if(events.begin(), events.end(),
                    [type](const Protocol::BattleEventDTO &event) {
                      return event.eventType == type;
                    }));
}

std::optional<Protocol::BattleEnemyEntity>
find_enemy(const Protocol::BattleFrameRsp &frame, int entityId) {
  auto it = std::find_if(frame.enemyEntities.begin(), frame.enemyEntities.end(),
                         [entityId](const Protocol::BattleEnemyEntity &enemy) {
                           return enemy.entityId == entityId;
                         });
  if (it == frame.enemyEntities.end()) {
    return std::nullopt;
  }
  return *it;
}

std::optional<Protocol::BattlePlayerEntity>
find_player(const Protocol::BattleFrameRsp &frame, const std::string &uid) {
  auto it =
      std::find_if(frame.playerEntities.begin(), frame.playerEntities.end(),
                   [&uid](const Protocol::BattlePlayerEntity &player) {
                     return player.uid == uid;
                   });
  if (it == frame.playerEntities.end()) {
    return std::nullopt;
  }
  return *it;
}

double dist(const Protocol::BattleVector2 &lhs,
            const Protocol::BattleVector2 &rhs) {
  const double dx = lhs.x - rhs.x;
  const double dy = lhs.y - rhs.y;
  return std::sqrt(dx * dx + dy * dy);
}

bool tick_until(Room &room, Protocol::BattleFrameRsp &frame,
                Protocol::BattleEventType eventType, int limit = 20) {
  for (int tick = 0; tick < limit; ++tick) {
    if (!room.tick_battle(frame)) {
      return false;
    }
    if (find_event(frame.events, eventType) >= 0) {
      return true;
    }
  }
  return false;
}

std::vector<int> path_to_boss(const std::vector<Protocol::MapNode> &map) {
  std::unordered_map<int, const Protocol::MapNode *> nodesById;
  for (const auto &node : map) {
    nodesById[node.nodeId] = &node;
  }

  std::vector<int> path;
  std::function<bool(int)> visit = [&](int nodeId) {
    auto it = nodesById.find(nodeId);
    if (it == nodesById.end()) {
      return false;
    }

    path.push_back(nodeId);
    if (it->second->type == Protocol::MapNode::NodeType::BOSS) {
      return true;
    }

    for (int nextId : it->second->nextId) {
      if (visit(nextId)) {
        return true;
      }
    }

    path.pop_back();
    return false;
  };

  visit(0);
  return path;
}

void select_boss(Room &room, const std::string &uid) {
  ASSERT_TRUE(room.set_member_ready(uid, true));
  Protocol::MapInitRsp init;
  ASSERT_TRUE(room.get_map_init(init));
  const auto path = path_to_boss(init.map);
  ASSERT_FALSE(path.empty());

  for (int nodeId : path) {
    std::vector<Protocol::MapSync> selectStatus;
    bool committed = false;
    ASSERT_TRUE(room.move_map(uid, nodeId, selectStatus, committed));
    ASSERT_TRUE(committed);
  }
}

void select_first_node(Room &room, const std::string &uid) {
  ASSERT_TRUE(room.set_member_ready(uid, true));
  Protocol::MapInitRsp init;
  ASSERT_TRUE(room.get_map_init(init));
  ASSERT_FALSE(init.map.empty());

  std::vector<Protocol::MapSync> selectStatus;
  bool committed = false;
  ASSERT_TRUE(room.move_map(uid, init.map.front().nodeId, selectStatus,
                            committed));
  ASSERT_TRUE(committed);
}

void start_battle(Room &room, const std::string &uid,
                  Protocol::BattleFrameRsp &frame) {
  Protocol::BattleWaitRsp waitRsp;
  bool allReady = false;
  bool readyOk = room.set_battle_ready(uid, waitRsp, allReady);
  if (!readyOk) {
    select_first_node(room, uid);
    readyOk = room.set_battle_ready(uid, waitRsp, allReady);
  }
  ASSERT_TRUE(readyOk);
  ASSERT_TRUE(allReady);
  ASSERT_TRUE(room.tick_battle(frame));
  ASSERT_FALSE(frame.playerEntities.empty());
  ASSERT_FALSE(frame.enemyEntities.empty());
}

TEST(RoomTest, BasicBehavior) {
  auto state = std::make_shared<ServerState>();

  Protocol::PlayerBasicInfo creatorInfo{"1", "creator", 1};
  state->userData.emplace(creatorInfo.uid,
                          Protocol::PlayerData{.basicInfo = creatorInfo});
  auto creator = std::make_shared<User>(creatorInfo.uid, state);
  Room room(10, 2, state, creator);

  EXPECT_EQ(room.get_id(), 10);
  EXPECT_EQ(room.get_maximum_people(), 2);
  EXPECT_EQ(room.get_people_count(), 1);
  EXPECT_TRUE(room.is_member("1"));

  Protocol::PlayerBasicInfo info2{"2", "u2", 2};
  Protocol::PlayerBasicInfo info3{"3", "u3", 3};
  state->userData.emplace(info2.uid, Protocol::PlayerData{.basicInfo = info2});
  state->userData.emplace(info3.uid, Protocol::PlayerData{.basicInfo = info3});
  auto user2 = std::make_shared<User>(info2.uid, state);
  auto user3 = std::make_shared<User>(info3.uid, state);

  EXPECT_TRUE(room.add_member(user2));
  EXPECT_EQ(room.get_people_count(), 2);
  EXPECT_TRUE(room.is_member("2"));

  // Capacity reached.
  EXPECT_FALSE(room.add_member(user3));
  EXPECT_EQ(room.get_people_count(), 2);

  EXPECT_TRUE(room.remove_member("2"));
  EXPECT_EQ(room.get_people_count(), 1);
  EXPECT_FALSE(room.is_member("2"));

  // Removing non-existent member should return false.
  EXPECT_FALSE(room.remove_member("404"));
}

TEST(RoomTest, ReadyStateBehavior) {
  auto state = std::make_shared<ServerState>();

  Protocol::PlayerBasicInfo creatorInfo{"1", "creator", 1};
  state->userData.emplace(creatorInfo.uid,
                          Protocol::PlayerData{.basicInfo = creatorInfo});
  auto creator = std::make_shared<User>(creatorInfo.uid, state);
  Room room(10, 2, state, creator);

  Protocol::PlayerBasicInfo info2{"2", "u2", 2};
  state->userData.emplace(info2.uid, Protocol::PlayerData{.basicInfo = info2});
  auto user2 = std::make_shared<User>(info2.uid, state);
  ASSERT_TRUE(room.add_member(user2));

  EXPECT_TRUE(room.set_member_ready("1", true));
  EXPECT_TRUE(room.set_member_ready("2", false));
  EXPECT_FALSE(room.set_member_ready("404", true));

  const auto info = room.get_info();
  ASSERT_EQ(info.readyUids.size(), 1U);
  EXPECT_EQ(info.readyUids.front(), "1");

  ASSERT_TRUE(room.remove_member("1"));
  const auto infoAfterLeave = room.get_info();
  EXPECT_TRUE(infoAfterLeave.readyUids.empty());
}

TEST(RoomTest, GetInfoSkipsMissingProfiles) {
  auto state = std::make_shared<ServerState>();

  Protocol::PlayerBasicInfo creatorInfo{"10", "creator", 1};
  Protocol::PlayerBasicInfo info2{"20", "u2", 2};
  state->userData.emplace(creatorInfo.uid,
                          Protocol::PlayerData{.basicInfo = creatorInfo});
  state->userData.emplace(info2.uid, Protocol::PlayerData{.basicInfo = info2});

  auto creator = std::make_shared<User>(creatorInfo.uid, state);
  auto user2 = std::make_shared<User>(info2.uid, state);
  Room room(99, 2, state, creator);
  ASSERT_TRUE(room.add_member(user2));

  state->userData.erase(info2.uid);

  const auto roomInfo = room.get_info();
  ASSERT_EQ(roomInfo.basicInfos.size(), 1U);
  EXPECT_EQ(roomInfo.basicInfos.front().uid, creatorInfo.uid);
}

TEST(RoomTest, ShopItemCountTracksCurrentMemberCount) {
  auto state = std::make_shared<ServerState>();
  state->shopCatalogItemIds = {"sword", "shield", "potion", "boots"};
  state->shopCatalogVersion = "test-v1";

  Protocol::PlayerBasicInfo creatorInfo{"1", "creator", 1};
  Protocol::PlayerBasicInfo info2{"2", "u2", 2};
  state->userData.emplace(creatorInfo.uid,
                          Protocol::PlayerData{.basicInfo = creatorInfo});
  state->userData.emplace(info2.uid, Protocol::PlayerData{.basicInfo = info2});

  auto creator = std::make_shared<User>(creatorInfo.uid, state);
  auto user2 = std::make_shared<User>(info2.uid, state);
  Room room(88, 3, state, creator);

  ASSERT_TRUE(room.add_member(user2));
  ASSERT_TRUE(room.set_member_ready(creatorInfo.uid, true));
  ASSERT_TRUE(room.set_member_ready(info2.uid, true));

  Protocol::ShopInitRsp initRsp;
  ASSERT_TRUE(room.get_shop_init(initRsp));
  EXPECT_EQ(initRsp.items.size(), 2U);
  EXPECT_EQ(initRsp.playerInfos.size(), 2U);

  EXPECT_FALSE(room.add_member(user2));

  ASSERT_TRUE(room.remove_member(info2.uid));

  Protocol::ShopInitRsp afterLeaveRsp;
  ASSERT_TRUE(room.get_shop_init(afterLeaveRsp));
  EXPECT_EQ(afterLeaveRsp.items.size(), 1U);
  EXPECT_EQ(afterLeaveRsp.playerInfos.size(), 1U);
}

TEST(RoomTest, PhaseRejectsBattleReadyBeforeMapCommit) {
  auto harness = make_room();

  Protocol::BattleWaitRsp waitRsp;
  bool allReady = false;
  EXPECT_FALSE(harness.room->set_battle_ready(harness.uid, waitRsp, allReady));

  ASSERT_TRUE(harness.room->set_member_ready(harness.uid, true));
  EXPECT_FALSE(harness.room->set_battle_ready(harness.uid, waitRsp, allReady));

  Protocol::MapInitRsp init;
  ASSERT_TRUE(harness.room->get_map_init(init));
  EXPECT_FALSE(harness.room->set_battle_ready(harness.uid, waitRsp, allReady));
}

TEST(RoomTest, PhaseRejectsNonBattleApisDuringBattle) {
  auto harness = make_room();
  Protocol::BattleFrameRsp frame;
  start_battle(*harness.room, harness.uid, frame);

  Protocol::ShopInitRsp shopRsp;
  EXPECT_FALSE(harness.room->get_shop_init(shopRsp));

  Protocol::MapInitRsp mapRsp;
  EXPECT_FALSE(harness.room->get_map_init(mapRsp));

  std::vector<Protocol::MapSync> selectStatus;
  bool committed = false;
  EXPECT_FALSE(harness.room->move_map(harness.uid, 0, selectStatus, committed));
}

TEST(RoomTest, BattleShootSpawnsNormalizedBullet) {
  auto harness = make_room();
  Protocol::BattleFrameRsp frame;
  start_battle(*harness.room, harness.uid, frame);

  ASSERT_TRUE(
      harness.room->sync_battle(harness.uid, {0.0, 0.0}, {0.0, 1.0}, {}));
  ASSERT_TRUE(harness.room->shoot_battle_player(harness.uid, {10.0, 0.0}));
  ASSERT_TRUE(harness.room->tick_battle(frame));

  ASSERT_EQ(frame.bulletEntities.size(), 1U);
  EXPECT_DOUBLE_EQ(frame.bulletEntities.front().position.x, 1.0);
  EXPECT_DOUBLE_EQ(frame.bulletEntities.front().position.y, 0.0);
  EXPECT_DOUBLE_EQ(frame.bulletEntities.front().direction.x, 1.0);
  EXPECT_DOUBLE_EQ(frame.bulletEntities.front().direction.y, 0.0);

  ASSERT_EQ(frame.playerEntities.size(), 1U);
  EXPECT_DOUBLE_EQ(frame.playerEntities.front().direction.x, 0.0);
  EXPECT_DOUBLE_EQ(frame.playerEntities.front().direction.y, 1.0);

  const int spawnIndex =
      find_event(frame.events, Protocol::BattleEventType::BULLET_SPAWN);
  ASSERT_GE(spawnIndex, 0);
  ASSERT_TRUE(frame.events[spawnIndex].spawnParameter.has_value());
  EXPECT_EQ(frame.events[spawnIndex].spawnParameter->entityId,
            frame.bulletEntities.front().entityId);
}

TEST(RoomTest, BattleShootRejectsInvalidDirections) {
  auto coldHarness = make_room();
  EXPECT_FALSE(
      coldHarness.room->shoot_battle_player(coldHarness.uid, {1.0, 0.0}));

  auto harness = make_room();
  Protocol::BattleFrameRsp frame;
  start_battle(*harness.room, harness.uid, frame);

  EXPECT_FALSE(harness.room->shoot_battle_player(harness.uid, {0.0, 0.0}));
  EXPECT_FALSE(harness.room->shoot_battle_player(
      harness.uid, {std::numeric_limits<double>::quiet_NaN(), 1.0}));
  ASSERT_TRUE(harness.room->tick_battle(frame));
  EXPECT_TRUE(frame.bulletEntities.empty());
  EXPECT_TRUE(frame.events.empty());
}

TEST(RoomTest, BattleStartPushesInitialIntentChange) {
  auto harness = make_room();
  Protocol::BattleFrameRsp frame;
  start_battle(*harness.room, harness.uid, frame);

  const int eventIndex =
      find_event(frame.events, Protocol::BattleEventType::ENEMY_INTENT_CHANGE);
  ASSERT_GE(eventIndex, 0);
  EXPECT_EQ(
      count_event(frame.events, Protocol::BattleEventType::ENEMY_INTENT_CHANGE),
      1);
  ASSERT_TRUE(frame.events[eventIndex].intentParameter.has_value());
  EXPECT_EQ(frame.events[eventIndex].intentParameter->enemyEntityId,
            frame.enemyEntities.front().entityId);
  EXPECT_EQ(frame.events[eventIndex].intentParameter->targetPlayerUid,
            harness.uid);
}

TEST(RoomTest, BattleBulletHitDamagesEnemy) {
  auto harness = make_room();
  Protocol::BattleFrameRsp frame;
  start_battle(*harness.room, harness.uid, frame);
  const int enemyId = frame.enemyEntities.front().entityId;

  ASSERT_TRUE(harness.room->shoot_battle_player(harness.uid, {0.0, 1.0}));
  ASSERT_TRUE(tick_until(*harness.room, frame,
                         Protocol::BattleEventType::ENTITY_DAMAGE));

  const int hitIndex =
      find_event(frame.events, Protocol::BattleEventType::BULLET_HIT_ENEMY);
  ASSERT_GE(hitIndex, 0);
  ASSERT_LT(static_cast<size_t>(hitIndex + 2), frame.events.size());
  EXPECT_EQ(frame.events[hitIndex + 1].eventType,
            Protocol::BattleEventType::ENTITY_DAMAGE);
  EXPECT_EQ(frame.events[hitIndex + 2].eventType,
            Protocol::BattleEventType::ENTITY_DESTROY);

  ASSERT_TRUE(frame.events[hitIndex].hitParameter.has_value());
  EXPECT_EQ(frame.events[hitIndex].hitParameter->sourceEntityType,
            Protocol::EntityType::PLAYER_BULLET);
  EXPECT_EQ(frame.events[hitIndex].hitParameter->targetEntityId, enemyId);

  ASSERT_TRUE(frame.events[hitIndex + 1].damageParameter.has_value());
  const auto &damage = *frame.events[hitIndex + 1].damageParameter;
  EXPECT_EQ(damage.sourceEntityType, Protocol::EntityType::PLAYER);
  EXPECT_EQ(damage.targetEntityId, enemyId);
  EXPECT_EQ(damage.damage, 5);
  EXPECT_EQ(damage.currentHP, 5);

  ASSERT_TRUE(frame.events[hitIndex + 2].destroyParameter.has_value());
  EXPECT_EQ(frame.events[hitIndex + 2].destroyParameter->entityType,
            Protocol::EntityType::PLAYER_BULLET);
  EXPECT_EQ(frame.events[hitIndex + 2].destroyParameter->destroyReason,
            Protocol::BattleEntityDestroyReason::BULLET_HIT_ENTITY);

  EXPECT_TRUE(frame.bulletEntities.empty());
  const auto enemy = find_enemy(frame, enemyId);
  ASSERT_TRUE(enemy.has_value());
  EXPECT_EQ(enemy->attribute.currentHP, 5);
}

TEST(RoomTest, BattleSecondHitKillsEnemy) {
  auto harness = make_room();
  Protocol::BattleFrameRsp frame;
  start_battle(*harness.room, harness.uid, frame);
  const int enemyId = frame.enemyEntities.front().entityId;

  ASSERT_TRUE(harness.room->shoot_battle_player(harness.uid, {0.0, 1.0}));
  ASSERT_TRUE(tick_until(*harness.room, frame,
                         Protocol::BattleEventType::ENTITY_DAMAGE));

  ASSERT_TRUE(harness.room->shoot_battle_player(harness.uid, {0.0, 1.0}));
  ASSERT_TRUE(tick_until(*harness.room, frame,
                         Protocol::BattleEventType::ENTITY_DESTROY));

  const int hitIndex =
      find_event(frame.events, Protocol::BattleEventType::BULLET_HIT_ENEMY);
  ASSERT_GE(hitIndex, 0);
  ASSERT_LT(static_cast<size_t>(hitIndex + 3), frame.events.size());
  EXPECT_EQ(frame.events[hitIndex + 1].eventType,
            Protocol::BattleEventType::ENTITY_DAMAGE);
  EXPECT_EQ(frame.events[hitIndex + 2].eventType,
            Protocol::BattleEventType::ENTITY_DESTROY);
  EXPECT_EQ(frame.events[hitIndex + 3].eventType,
            Protocol::BattleEventType::ENTITY_DESTROY);

  ASSERT_TRUE(frame.events[hitIndex + 1].damageParameter.has_value());
  EXPECT_EQ(frame.events[hitIndex + 1].damageParameter->currentHP, 0);
  ASSERT_TRUE(frame.events[hitIndex + 3].destroyParameter.has_value());
  EXPECT_EQ(frame.events[hitIndex + 3].destroyParameter->entityId, enemyId);
  EXPECT_EQ(frame.events[hitIndex + 3].destroyParameter->entityType,
            Protocol::EntityType::ENEMY);
  EXPECT_EQ(frame.events[hitIndex + 3].destroyParameter->destroyReason,
            Protocol::BattleEntityDestroyReason::ENTITY_DEAD);

  EXPECT_TRUE(frame.bulletEntities.empty());
  EXPECT_TRUE(frame.enemyEntities.empty());
}

TEST(RoomTest, BattleEndStopsTickAndReturnsToMap) {
  auto harness = make_room();
  ASSERT_TRUE(harness.room->set_member_ready(harness.uid, true));
  Protocol::MapInitRsp init;
  ASSERT_TRUE(harness.room->get_map_init(init));
  ASSERT_FALSE(init.map.empty());
  ASSERT_FALSE(init.map.front().nextId.empty());

  std::vector<Protocol::MapSync> selectStatus;
  bool committed = false;
  ASSERT_TRUE(harness.room->move_map(harness.uid, init.map.front().nodeId,
                                     selectStatus, committed));
  ASSERT_TRUE(committed);

  Protocol::BattleFrameRsp frame;
  Protocol::BattleWaitRsp waitRsp;
  bool allReady = false;
  ASSERT_TRUE(harness.room->set_battle_ready(harness.uid, waitRsp, allReady));
  ASSERT_TRUE(allReady);
  ASSERT_TRUE(harness.room->tick_battle(frame));
  ASSERT_FALSE(frame.playerEntities.empty());
  ASSERT_FALSE(frame.enemyEntities.empty());
  ASSERT_TRUE(harness.room->shoot_battle_player(harness.uid, {0.0, 1.0}));
  bool ended = false;
  bool damaged = false;
  for (int tick = 0; tick < 20 && !damaged; ++tick) {
    ASSERT_TRUE(harness.room->tick_battle(frame, &ended));
    damaged =
        find_event(frame.events, Protocol::BattleEventType::ENTITY_DAMAGE) >= 0;
  }
  ASSERT_TRUE(damaged);
  EXPECT_FALSE(ended);

  ASSERT_TRUE(harness.room->shoot_battle_player(harness.uid, {0.0, 1.0}));
  for (int tick = 0; tick < 20 && !ended; ++tick) {
    ASSERT_TRUE(harness.room->tick_battle(frame, &ended));
  }
  EXPECT_TRUE(ended);
  EXPECT_TRUE(frame.enemyEntities.empty());

  EXPECT_FALSE(harness.room->tick_battle(frame));
  ASSERT_TRUE(harness.room->move_map(
      harness.uid, init.map.front().nextId.front(), selectStatus, committed));
  EXPECT_TRUE(committed);
}

TEST(RoomTest, EnemyReportsAreSpeedLimited) {
  auto harness = make_room();
  Protocol::BattleFrameRsp frame;
  start_battle(*harness.room, harness.uid, frame);
  ASSERT_FALSE(frame.enemyEntities.empty());
  const auto startPos = frame.enemyEntities.front().position;
  const int enemyId = frame.enemyEntities.front().entityId;

  Protocol::BattlePos report;
  report.entityId = enemyId;
  report.position = {20.0, 20.0};
  report.direction = {1.0, 1.0};
  ASSERT_TRUE(
      harness.room->sync_battle(harness.uid, {0.0, 0.0}, {0.0, 1.0}, {report}));
  ASSERT_TRUE(harness.room->tick_battle(frame));

  const auto enemy = find_enemy(frame, enemyId);
  ASSERT_TRUE(enemy.has_value());
  EXPECT_LE(dist(startPos, enemy->position), 1.000001);
  EXPECT_LE(enemy->position.x, 20.0);
  EXPECT_LE(enemy->position.y, 20.0);
}

TEST(RoomTest, EnemyFallbackMovesWhenClientsDoNotReport) {
  auto harness = make_room();
  Protocol::BattleFrameRsp frame;
  start_battle(*harness.room, harness.uid, frame);
  ASSERT_FALSE(frame.enemyEntities.empty());
  const int enemyId = frame.enemyEntities.front().entityId;
  const auto startPos = frame.enemyEntities.front().position;

  ASSERT_TRUE(harness.room->tick_battle(frame));

  const auto enemy = find_enemy(frame, enemyId);
  ASSERT_TRUE(enemy.has_value());
  EXPECT_LT(enemy->position.y, startPos.y);
  EXPECT_DOUBLE_EQ(enemy->position.x, startPos.x);
}

TEST(RoomTest, EnemyAttackDamagesPlayer) {
  auto harness = make_room();
  Protocol::BattleFrameRsp frame;
  start_battle(*harness.room, harness.uid, frame);
  ASSERT_EQ(frame.playerEntities.front().attribute.currentHP, 20);

  ASSERT_TRUE(tick_until(*harness.room, frame,
                         Protocol::BattleEventType::ENTITY_DAMAGE, 30));

  const int damageIndex =
      find_event(frame.events, Protocol::BattleEventType::ENTITY_DAMAGE);
  ASSERT_GE(damageIndex, 0);
  ASSERT_TRUE(frame.events[damageIndex].damageParameter.has_value());
  const auto &damage = *frame.events[damageIndex].damageParameter;
  EXPECT_EQ(damage.sourceEntityType, Protocol::EntityType::ENEMY);
  EXPECT_EQ(damage.targetEntityType, Protocol::EntityType::PLAYER);
  EXPECT_EQ(damage.damage, 4);
  EXPECT_EQ(damage.currentHP, 16);

  const auto player = find_player(frame, harness.uid);
  ASSERT_TRUE(player.has_value());
  EXPECT_EQ(player->attribute.currentHP, 16);
}

TEST(RoomTest, BattleDefeatsWhenAllPlayersDie) {
  auto harness = make_room();
  Protocol::BattleFrameRsp frame;
  start_battle(*harness.room, harness.uid, frame);

  bool ended = false;
  for (int tick = 0; tick < 140 && !ended; ++tick) {
    ASSERT_TRUE(harness.room->tick_battle(frame, &ended));
  }

  EXPECT_TRUE(ended);
  const auto player = find_player(frame, harness.uid);
  ASSERT_TRUE(player.has_value());
  EXPECT_EQ(player->attribute.currentHP, 0);
  ASSERT_FALSE(frame.enemyEntities.empty());
  EXPECT_TRUE(frame.enemyEntities.front().targetPlayerUid.empty());
  const int intentIndex =
      find_event(frame.events, Protocol::BattleEventType::ENEMY_INTENT_CHANGE);
  ASSERT_GE(intentIndex, 0);
  ASSERT_TRUE(frame.events[intentIndex].intentParameter.has_value());
  EXPECT_TRUE(
      frame.events[intentIndex].intentParameter->targetPlayerUid.empty());

  const int destroyIndex =
      find_event(frame.events, Protocol::BattleEventType::ENTITY_DESTROY);
  ASSERT_GE(destroyIndex, 0);
  ASSERT_TRUE(frame.events[destroyIndex].destroyParameter.has_value());
  EXPECT_EQ(frame.events[destroyIndex].destroyParameter->entityType,
            Protocol::EntityType::PLAYER);
  EXPECT_FALSE(harness.room->tick_battle(frame));
}

TEST(RoomTest, BattleBulletHitsWallOutOfBounds) {
  auto harness = make_room();
  Protocol::BattleFrameRsp frame;
  start_battle(*harness.room, harness.uid, frame);

  ASSERT_TRUE(
      harness.room->sync_battle(harness.uid, {19.5, 0.0}, {1.0, 0.0}, {}));
  ASSERT_TRUE(harness.room->shoot_battle_player(harness.uid, {1.0, 0.0}));
  ASSERT_TRUE(harness.room->tick_battle(frame));

  const int hitIndex =
      find_event(frame.events, Protocol::BattleEventType::BULLET_HIT_WALL);
  ASSERT_GE(hitIndex, 0);
  ASSERT_LT(static_cast<size_t>(hitIndex + 1), frame.events.size());
  ASSERT_TRUE(frame.events[hitIndex].hitParameter.has_value());
  EXPECT_EQ(frame.events[hitIndex].hitParameter->targetEntityId, 0);
  EXPECT_EQ(frame.events[hitIndex].hitParameter->targetEntityType,
            Protocol::EntityType::WALL);
  EXPECT_DOUBLE_EQ(frame.events[hitIndex].hitParameter->hitPosition.x, 20.5);

  EXPECT_EQ(frame.events[hitIndex + 1].eventType,
            Protocol::BattleEventType::ENTITY_DESTROY);
  ASSERT_TRUE(frame.events[hitIndex + 1].destroyParameter.has_value());
  EXPECT_EQ(frame.events[hitIndex + 1].destroyParameter->entityType,
            Protocol::EntityType::PLAYER_BULLET);
  EXPECT_EQ(frame.events[hitIndex + 1].destroyParameter->destroyReason,
            Protocol::BattleEntityDestroyReason::BULLET_HIT_WALL);
  EXPECT_TRUE(frame.bulletEntities.empty());
}

TEST(RoomTest, BattleBulletHitsFirstEnemyOnly) {
  auto harness = make_room();
  select_boss(*harness.room, harness.uid);
  Protocol::BattleFrameRsp frame;
  start_battle(*harness.room, harness.uid, frame);
  ASSERT_GE(frame.enemyEntities.size(), 2U);
  const int firstEnemyId = frame.enemyEntities[0].entityId;
  const int secondEnemyId = frame.enemyEntities[1].entityId;

  ASSERT_TRUE(harness.room->shoot_battle_player(harness.uid, {0.0, 1.0}));
  ASSERT_TRUE(tick_until(*harness.room, frame,
                         Protocol::BattleEventType::BULLET_HIT_ENEMY));

  const int hitIndex =
      find_event(frame.events, Protocol::BattleEventType::BULLET_HIT_ENEMY);
  ASSERT_GE(hitIndex, 0);
  ASSERT_TRUE(frame.events[hitIndex].hitParameter.has_value());
  EXPECT_EQ(frame.events[hitIndex].hitParameter->targetEntityId, firstEnemyId);

  const auto firstEnemy = find_enemy(frame, firstEnemyId);
  const auto secondEnemy = find_enemy(frame, secondEnemyId);
  ASSERT_TRUE(firstEnemy.has_value());
  ASSERT_TRUE(secondEnemy.has_value());
  EXPECT_EQ(firstEnemy->attribute.currentHP, 5);
  EXPECT_EQ(secondEnemy->attribute.currentHP, 10);
  EXPECT_TRUE(frame.bulletEntities.empty());
}

} // namespace
