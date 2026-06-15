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

using Battle::BattleEnemyDef;
using Battle::BattleEnemyPool;
using Battle::default_battle_config;

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

RoomHarness make_room_with_catalog(std::vector<std::string> itemIds) {
  auto state = std::make_shared<ServerState>();
  state->shopCatalogItemIds = std::move(itemIds);
  Protocol::PlayerBasicInfo creatorInfo{"1", "creator", 1};
  state->userData.emplace(creatorInfo.uid,
                          Protocol::PlayerData{.basicInfo = creatorInfo});
  auto creator = std::make_shared<User>(creatorInfo.uid, state);
  auto room = std::make_unique<Room>(10, 1, state, creator);
  return RoomHarness{state, creator, std::move(room), creatorInfo.uid};
}

Battle::WeaponDef test_gun() {
  Battle::WeaponDef weapon;
  weapon.weaponId = "test_gun";
  weapon.weaponName = "test gun";
  weapon.damage = 5.0;
  weapon.attackSpeed = 0.0;
  weapon.range = 5000.0;
  weapon.critMultiplier = 1.0;
  weapon.projectileCount = 1;
  weapon.projectile.speed = 1.0;
  weapon.projectile.size = 0.25;
  weapon.tags = {"weapon", "ranged", "test"};
  return weapon;
}

Battle::BattleEnemyDef test_enemy() {
  Battle::BattleEnemyDef enemy;
  enemy.pool = Battle::BattleEnemyPool::NORMAL;
  enemy.enemyType = Protocol::BattleEnemyType::BUBBLE_FISH;
  enemy.maxHP = 10;
  enemy.attackRange = 1.5;
  enemy.maxSpeed = 0.0;
  enemy.knockbackResist = 0.0;
  enemy.attackDamage = 0;
  enemy.attackCooldownTicks = 20;
  enemy.cost = 3.0;
  enemy.unlockTime = 0.0;
  enemy.weight = 100.0;
  return enemy;
}

RoomHarness make_room_with_weapon(Battle::WeaponDef weapon = test_gun(),
                                  std::vector<BattleEnemyDef> enemies = {}) {
  auto state = std::make_shared<ServerState>();
  state->shopCatalogItemIds = {weapon.weaponId};
  state->battleConfig = default_battle_config();
  state->battleConfig.weapons = {std::move(weapon)};
  if (!enemies.empty()) {
    state->battleConfig.enemies = std::move(enemies);
  }
  Protocol::PlayerBasicInfo creatorInfo{"1", "creator", 1};
  state->userData.emplace(creatorInfo.uid,
                          Protocol::PlayerData{.basicInfo = creatorInfo});
  auto creator = std::make_shared<User>(creatorInfo.uid, state);
  auto room = std::make_unique<Room>(10, 1, state, creator);
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

bool has_cleared_enemy_target(
    const std::vector<Protocol::BattleEventDTO> &events) {
  return std::any_of(
      events.begin(), events.end(), [](const Protocol::BattleEventDTO &event) {
        return event.eventType ==
                   Protocol::BattleEventType::ENEMY_INTENT_CHANGE &&
               event.intentParameter.has_value() &&
               event.intentParameter->targetPlayerUid.empty();
      });
}

int find_damage_to(const std::vector<Protocol::BattleEventDTO> &events,
                   Protocol::EntityType targetType) {
  for (size_t index = 0; index < events.size(); ++index) {
    const auto &event = events[index];
    if (event.eventType == Protocol::BattleEventType::ENTITY_DAMAGE &&
        event.damageParameter.has_value() &&
        event.damageParameter->targetEntityType == targetType) {
      return static_cast<int>(index);
    }
  }
  return -1;
}

std::vector<Protocol::BattleEnemyEntity>
spawned_enemies(const Protocol::BattleFrameRsp &frame) {
  std::vector<Protocol::BattleEnemyEntity> enemies;
  for (const auto &event : frame.events) {
    if (event.eventType == Protocol::BattleEventType::ENEMY_SPAWN &&
        event.spawnParameter.has_value() &&
        event.spawnParameter->enemyEntity.has_value()) {
      enemies.push_back(event.spawnParameter->enemyEntity.value());
    }
  }
  return enemies;
}

std::optional<Protocol::BattleEnemyEntity>
first_spawned_enemy(const Protocol::BattleFrameRsp &frame) {
  const auto enemies = spawned_enemies(frame);
  if (enemies.empty()) {
    return std::nullopt;
  }
  return enemies.front();
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

Battle::BattleVector2 dir_to(const Battle::BattleVector2 &from,
                             const Battle::BattleVector2 &to) {
  const double dx = to.x - from.x;
  const double dy = to.y - from.y;
  const double len = std::sqrt(dx * dx + dy * dy);
  if (len <= 0.0) {
    return {1.0, 0.0};
  }
  return {dx / len, dy / len};
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

bool tick_until_enemy_damage(Room &room, Protocol::BattleFrameRsp &frame,
                             int limit = 20) {
  for (int tick = 0; tick < limit; ++tick) {
    if (!room.tick_battle(frame)) {
      return false;
    }
    if (find_damage_to(frame.events, Protocol::EntityType::ENEMY) >= 0) {
      return true;
    }
  }
  return false;
}

bool tick_until_enemy_destroy(Room &room, Protocol::BattleFrameRsp &frame,
                              int enemyId, int limit = 120) {
  for (int tick = 0; tick < limit; ++tick) {
    if (!room.tick_battle(frame)) {
      return false;
    }
    const auto enemyDestroyed = std::find_if(
        frame.events.begin(), frame.events.end(),
        [enemyId](const Protocol::BattleEventDTO &event) {
          return event.eventType == Protocol::BattleEventType::ENTITY_DESTROY &&
                 event.destroyParameter.has_value() &&
                 event.destroyParameter->entityId == enemyId &&
                 event.destroyParameter->entityType ==
                     Protocol::EntityType::ENEMY &&
                 event.destroyParameter->destroyReason ==
                     Protocol::BattleEntityDestroyReason::ENTITY_DEAD;
        });
    if (enemyDestroyed != frame.events.end()) {
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
  ASSERT_TRUE(
      room.move_map(uid, init.map.front().nodeId, selectStatus, committed));
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
  EXPECT_EQ(frame.serverTick, 0);
  ASSERT_TRUE(first_spawned_enemy(frame).has_value());
}

void buy_and_select(Room &room, const std::string &uid,
                    const std::string &itemId) {
  ASSERT_TRUE(room.set_member_ready(uid, true));
  Protocol::ShopInitRsp shopRsp;
  ASSERT_TRUE(room.get_shop_init(shopRsp));

  std::vector<Protocol::ShopItem> items;
  ASSERT_EQ(room.buy_shop_item(uid, itemId, items), Protocol::SERVICE_SUCCESS);

  Protocol::MapInitRsp init;
  ASSERT_TRUE(room.get_map_init(init));
  ASSERT_FALSE(init.map.empty());

  std::vector<Protocol::MapSync> selectStatus;
  bool committed = false;
  ASSERT_TRUE(
      room.move_map(uid, init.map.front().nodeId, selectStatus, committed));
  ASSERT_TRUE(committed);
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
  auto harness = make_room_with_weapon();
  buy_and_select(*harness.room, harness.uid, "test_gun");
  Protocol::BattleFrameRsp frame;
  start_battle(*harness.room, harness.uid, frame);

  ASSERT_TRUE(harness.room->sync_battle(harness.uid, {0.0, 0.0}, {}));
  ASSERT_TRUE(harness.room->shoot_battle_player(harness.uid, {10.0, 0.0}));
  ASSERT_TRUE(harness.room->tick_battle(frame));

  ASSERT_EQ(frame.playerEntities.size(), 1U);
  EXPECT_DOUBLE_EQ(frame.playerEntities.front().direction.x, 0.0);
  EXPECT_DOUBLE_EQ(frame.playerEntities.front().direction.y, 0.0);

  const int spawnIndex =
      find_event(frame.events, Protocol::BattleEventType::BULLET_SPAWN);
  ASSERT_GE(spawnIndex, 0);
  ASSERT_TRUE(frame.events[spawnIndex].spawnParameter.has_value());
  const auto &spawn = *frame.events[spawnIndex].spawnParameter;
  ASSERT_TRUE(spawn.bulletEntity.has_value());
  EXPECT_EQ(spawn.entityId, spawn.bulletEntity->entityId);
  EXPECT_DOUBLE_EQ(spawn.bulletEntity->position.x,
                   harness.state->battleConfig.playerRadius);
  EXPECT_DOUBLE_EQ(spawn.bulletEntity->position.y, 0.0);
  EXPECT_DOUBLE_EQ(spawn.bulletEntity->direction.x, 1.0);
  EXPECT_DOUBLE_EQ(spawn.bulletEntity->direction.y, 0.0);
}

TEST(RoomTest, BattleRangedWeaponFallsBackWhenProjectileSpeedIsZero) {
  auto weapon = test_gun();
  weapon.projectile.speed = 0.0;
  auto harness = make_room_with_weapon(weapon);
  buy_and_select(*harness.room, harness.uid, "test_gun");

  Protocol::BattleFrameRsp frame;
  start_battle(*harness.room, harness.uid, frame);

  ASSERT_TRUE(harness.room->sync_battle(harness.uid, {0.0, 0.0}, {}));
  ASSERT_TRUE(harness.room->shoot_battle_player(harness.uid, {1.0, 0.0}));
  ASSERT_TRUE(harness.room->tick_battle(frame));

  const int spawnIndex =
      find_event(frame.events, Protocol::BattleEventType::BULLET_SPAWN);
  ASSERT_GE(spawnIndex, 0);
  ASSERT_TRUE(frame.events[spawnIndex].spawnParameter.has_value());
  const auto &spawn = *frame.events[spawnIndex].spawnParameter;
  ASSERT_TRUE(spawn.bulletEntity.has_value());
  EXPECT_DOUBLE_EQ(spawn.bulletEntity->direction.x,
                   harness.state->battleConfig.bulletSpeed);
  EXPECT_DOUBLE_EQ(spawn.bulletEntity->position.x,
                   harness.state->battleConfig.playerRadius);
  EXPECT_DOUBLE_EQ(spawn.bulletEntity->attribute.speed,
                   harness.state->battleConfig.bulletSpeed);
}

TEST(RoomTest, BattleBulletRangeUsesWorldUnits) {
  auto state = std::make_shared<ServerState>();
  state->shopCatalogItemIds = {"test_gun"};
  state->battleConfig = default_battle_config();
  state->battleConfig.baseSpawnBudget = 1.0;

  auto weapon = test_gun();
  weapon.range = 5.0;
  state->battleConfig.weapons = {weapon};
  auto enemy = test_enemy();
  enemy.cost = 1000.0;
  state->battleConfig.enemies = {enemy};

  Protocol::PlayerBasicInfo creatorInfo{"1", "creator", 1};
  state->userData.emplace(creatorInfo.uid,
                          Protocol::PlayerData{.basicInfo = creatorInfo});
  auto creator = std::make_shared<User>(creatorInfo.uid, state);
  Room room(81, 1, state, creator);
  buy_and_select(room, creatorInfo.uid, "test_gun");

  Protocol::BattleWaitRsp waitRsp;
  bool allReady = false;
  ASSERT_TRUE(room.set_battle_ready(creatorInfo.uid, waitRsp, allReady));
  ASSERT_TRUE(allReady);

  Protocol::BattleFrameRsp frame;
  ASSERT_TRUE(room.tick_battle(frame));
  ASSERT_TRUE(room.shoot_battle_player(creatorInfo.uid, {1.0, 0.0}));
  ASSERT_TRUE(room.tick_battle(frame));

  EXPECT_GE(find_event(frame.events, Protocol::BattleEventType::BULLET_SPAWN),
            0);
  EXPECT_EQ(find_event(frame.events, Protocol::BattleEventType::ENTITY_DESTROY),
            -1);
}

TEST(RoomTest, BattleShootRejectsInvalidDirections) {
  auto coldHarness = make_room();
  EXPECT_FALSE(
      coldHarness.room->shoot_battle_player(coldHarness.uid, {1.0, 0.0}));

  auto harness = make_room_with_weapon();
  buy_and_select(*harness.room, harness.uid, "test_gun");
  Protocol::BattleFrameRsp frame;
  start_battle(*harness.room, harness.uid, frame);

  EXPECT_FALSE(harness.room->shoot_battle_player(harness.uid, {0.0, 0.0}));
  EXPECT_FALSE(harness.room->shoot_battle_player(
      harness.uid, {std::numeric_limits<double>::quiet_NaN(), 1.0}));
  ASSERT_TRUE(harness.room->tick_battle(frame));
  EXPECT_TRUE(frame.events.empty());
}

TEST(RoomTest, BattleIgnoresIncompleteSharedBattleConfig) {
  using namespace Battle;
  auto state = std::make_shared<ServerState>();
  state->shopCatalogItemIds = {"pistol_1"};
  state->battleConfig.playerMaxHP = 33;
  state->battleConfig.weapons.clear();
  state->battleConfig.enemies = {
      {BattleEnemyPool::NORMAL, Protocol::BattleEnemyType::BUBBLE_FISH, 42, 2.5,
       1.4, 0.0, 8, 11, 3.0, 0.0, 100.0},
  };

  Protocol::PlayerBasicInfo creatorInfo{"1", "creator", 1};
  state->userData.emplace(creatorInfo.uid,
                          Protocol::PlayerData{.basicInfo = creatorInfo});
  auto creator = std::make_shared<User>(creatorInfo.uid, state);
  Room room(10, 1, state, creator);

  Protocol::BattleFrameRsp frame;
  start_battle(room, creatorInfo.uid, frame);

  ASSERT_EQ(frame.playerEntities.size(), 1U);
  EXPECT_EQ(frame.playerEntities.front().attribute.maxHP, 20);
  const auto enemy = first_spawned_enemy(frame);
  ASSERT_TRUE(enemy.has_value());
  EXPECT_EQ(enemy->attribute.maxHP, 10);
}

TEST(RoomTest, BattleEquipsWeaponItemOnStart) {
  auto harness = make_room_with_catalog({"pistol_1"});
  buy_and_select(*harness.room, harness.uid, "pistol_1");

  Protocol::BattleFrameRsp frame;
  start_battle(*harness.room, harness.uid, frame);

  const auto player = find_player(frame, harness.uid);
  ASSERT_TRUE(player.has_value());
  EXPECT_EQ(player->weaponId, "pistol_1");
}

TEST(RoomTest, BattleEquipsStarterPistolOnStart) {
  auto harness = make_room();

  Protocol::BattleFrameRsp frame;
  start_battle(*harness.room, harness.uid, frame);

  const auto player = find_player(frame, harness.uid);
  ASSERT_TRUE(player.has_value());
  EXPECT_EQ(player->weaponId, "pistol_1");
  ASSERT_EQ(player->items.size(), 1U);
  EXPECT_EQ(player->items.front(), "pistol_1");
}

TEST(RoomTest, BattlePistolUsesWeaponDamageAndCooldown) {
  auto harness = make_room_with_catalog({"pistol_1"});
  buy_and_select(*harness.room, harness.uid, "pistol_1");

  Protocol::BattleFrameRsp frame;
  start_battle(*harness.room, harness.uid, frame);
  const auto enemy = first_spawned_enemy(frame);
  ASSERT_TRUE(enemy.has_value());
  const Battle::BattleVector2 playerPos{enemy->position.x - 4.0,
                                        enemy->position.y};
  const auto shotDir = dir_to(playerPos, enemy->position);
  ASSERT_TRUE(harness.room->sync_battle(harness.uid, playerPos, {}));

  ASSERT_TRUE(harness.room->shoot_battle_player(harness.uid, shotDir));
  EXPECT_FALSE(harness.room->shoot_battle_player(harness.uid, shotDir));
  ASSERT_TRUE(harness.room->tick_battle(frame));
  const int spawnIndex =
      find_event(frame.events, Protocol::BattleEventType::BULLET_SPAWN);
  ASSERT_GE(spawnIndex, 0);
  ASSERT_TRUE(frame.events[spawnIndex].spawnParameter.has_value());
  ASSERT_TRUE(
      frame.events[spawnIndex].spawnParameter->bulletEntity.has_value());
  EXPECT_FALSE(json(*frame.events[spawnIndex].spawnParameter->bulletEntity)
                   .contains("weaponId"));

  if (find_damage_to(frame.events, Protocol::EntityType::ENEMY) < 0) {
    ASSERT_TRUE(tick_until_enemy_damage(*harness.room, frame));
  }

  const int damageIndex =
      find_damage_to(frame.events, Protocol::EntityType::ENEMY);
  ASSERT_GE(damageIndex, 0);
  ASSERT_TRUE(frame.events[damageIndex].damageParameter.has_value());
  EXPECT_GE(frame.events[damageIndex].damageParameter->damage, 24);
}

TEST(RoomTest, BattleKnifeHitsInRangeWithoutBullet) {
  auto harness = make_room_with_catalog({"knife_1"});
  buy_and_select(*harness.room, harness.uid, "knife_1");

  Protocol::BattleFrameRsp frame;
  start_battle(*harness.room, harness.uid, frame);
  const auto enemy = first_spawned_enemy(frame);
  ASSERT_TRUE(enemy.has_value());
  const Battle::BattleVector2 playerPos{enemy->position.x - 1.0,
                                        enemy->position.y};

  ASSERT_TRUE(harness.room->sync_battle(harness.uid, playerPos, {}));
  ASSERT_TRUE(harness.room->shoot_battle_player(harness.uid, {1.0, 0.0}));
  ASSERT_TRUE(harness.room->tick_battle(frame));

  EXPECT_EQ(find_event(frame.events, Protocol::BattleEventType::BULLET_SPAWN),
            -1);
  EXPECT_GE(
      find_event(frame.events, Protocol::BattleEventType::WEAPON_HIT_ENEMY), 0);
  const int damageIndex =
      find_event(frame.events, Protocol::BattleEventType::ENTITY_DAMAGE);
  ASSERT_GE(damageIndex, 0);
  ASSERT_TRUE(frame.events[damageIndex].damageParameter.has_value());
  EXPECT_GE(frame.events[damageIndex].damageParameter->damage, 11);
}

TEST(RoomTest, BattleKnifeRangeIncludesEntityRadii) {
  auto harness = make_room_with_catalog({"knife_1"});
  buy_and_select(*harness.room, harness.uid, "knife_1");

  Protocol::BattleFrameRsp frame;
  start_battle(*harness.room, harness.uid, frame);
  const auto enemy = first_spawned_enemy(frame);
  ASSERT_TRUE(enemy.has_value());
  const Battle::BattleVector2 playerPos{enemy->position.x - 2.5,
                                        enemy->position.y};

  ASSERT_TRUE(harness.room->sync_battle(harness.uid, playerPos, {}));
  ASSERT_TRUE(harness.room->shoot_battle_player(harness.uid, {1.0, 0.0}));
  ASSERT_TRUE(harness.room->tick_battle(frame));

  EXPECT_GE(
      find_event(frame.events, Protocol::BattleEventType::WEAPON_HIT_ENEMY), 0);
  EXPECT_GE(find_damage_to(frame.events, Protocol::EntityType::ENEMY), 0);
}

TEST(RoomTest, BattleStartPushesInitialIntentChange) {
  auto harness = make_room();
  Protocol::BattleFrameRsp frame;
  start_battle(*harness.room, harness.uid, frame);
  const auto enemy = first_spawned_enemy(frame);
  ASSERT_TRUE(enemy.has_value());

  const int eventIndex =
      find_event(frame.events, Protocol::BattleEventType::ENEMY_INTENT_CHANGE);
  ASSERT_GE(eventIndex, 0);
  EXPECT_EQ(
      count_event(frame.events, Protocol::BattleEventType::ENEMY_INTENT_CHANGE),
      1);
  ASSERT_TRUE(frame.events[eventIndex].intentParameter.has_value());
  EXPECT_EQ(frame.events[eventIndex].intentParameter->enemyEntityId,
            enemy->entityId);
  EXPECT_EQ(frame.events[eventIndex].intentParameter->targetPlayerUid,
            harness.uid);
}

TEST(RoomTest, BattleBulletHitDamagesEnemy) {
  auto harness = make_room_with_weapon();
  buy_and_select(*harness.room, harness.uid, "test_gun");
  Protocol::BattleFrameRsp frame;
  start_battle(*harness.room, harness.uid, frame);
  const auto enemy = first_spawned_enemy(frame);
  ASSERT_TRUE(enemy.has_value());
  const int enemyId = enemy->entityId;

  ASSERT_TRUE(harness.room->shoot_battle_player(
      harness.uid, dir_to({0.0, 0.0}, enemy->position)));
  ASSERT_TRUE(tick_until(*harness.room, frame,
                         Protocol::BattleEventType::BULLET_HIT_ENEMY));

  const int hitIndex =
      find_event(frame.events, Protocol::BattleEventType::BULLET_HIT_ENEMY);
  ASSERT_GE(hitIndex, 0);

  ASSERT_TRUE(frame.events[hitIndex].hitParameter.has_value());
  EXPECT_EQ(frame.events[hitIndex].hitParameter->sourceEntityType,
            Protocol::EntityType::PLAYER_BULLET);
  EXPECT_EQ(frame.events[hitIndex].hitParameter->targetEntityId, enemyId);

  const int damageIndex =
      find_damage_to(frame.events, Protocol::EntityType::ENEMY);
  ASSERT_GE(damageIndex, 0);
  ASSERT_TRUE(frame.events[damageIndex].damageParameter.has_value());
  const auto &damage = *frame.events[damageIndex].damageParameter;
  EXPECT_EQ(damage.sourceEntityType, Protocol::EntityType::PLAYER);
  EXPECT_EQ(damage.targetEntityId, enemyId);
  EXPECT_EQ(damage.damage, 5);
  EXPECT_EQ(damage.currentHP, 5);

  const int destroyIndex =
      find_event(frame.events, Protocol::BattleEventType::ENTITY_DESTROY);
  ASSERT_GE(destroyIndex, 0);
  ASSERT_TRUE(frame.events[destroyIndex].destroyParameter.has_value());
  EXPECT_EQ(frame.events[destroyIndex].destroyParameter->entityType,
            Protocol::EntityType::PLAYER_BULLET);
  EXPECT_EQ(frame.events[destroyIndex].destroyParameter->destroyReason,
            Protocol::BattleEntityDestroyReason::BULLET_HIT_ENTITY);
}

TEST(RoomTest, BattleBulletHitsEnemyMovementPath) {
  auto weapon = test_gun();
  weapon.projectile.speed = 10.0;
  weapon.range = 20.0;
  auto enemyDef = test_enemy();
  enemyDef.maxSpeed = 240.0;
  auto harness = make_room_with_weapon(weapon, {enemyDef});
  buy_and_select(*harness.room, harness.uid, "test_gun");
  Protocol::BattleFrameRsp frame;
  start_battle(*harness.room, harness.uid, frame);
  const auto enemy = first_spawned_enemy(frame);
  ASSERT_TRUE(enemy.has_value());

  const Battle::BattleVector2 playerPos{enemy->position.x - 8.0,
                                        enemy->position.y};
  Protocol::BattlePos enemyReport;
  enemyReport.entityId = enemy->entityId;
  enemyReport.position = {enemy->position.x, enemy->position.y + 2.0};
  enemyReport.direction = {0.0, 1.0};

  ASSERT_TRUE(harness.room->sync_battle(harness.uid, playerPos, {}));
  ASSERT_TRUE(harness.room->shoot_battle_player(harness.uid, {1.0, 0.0}));
  ASSERT_TRUE(
      harness.room->sync_battle(harness.uid, playerPos, {enemyReport}));
  ASSERT_TRUE(harness.room->tick_battle(frame));

  EXPECT_GE(
      find_event(frame.events, Protocol::BattleEventType::BULLET_HIT_ENEMY), 0);
  EXPECT_GE(find_damage_to(frame.events, Protocol::EntityType::ENEMY), 0);
}

TEST(RoomTest, BattleBulletCanHitEnemyInsidePlayerRadius) {
  auto weapon = test_gun();
  weapon.projectile.size = 0.05;
  weapon.projectile.speed = 1.0;
  auto harness = make_room_with_weapon(weapon, {test_enemy()});
  harness.state->battleConfig.playerRadius = 1.0;
  harness.state->battleConfig.enemyRadius = 0.2;
  buy_and_select(*harness.room, harness.uid, "test_gun");

  Protocol::BattleFrameRsp frame;
  start_battle(*harness.room, harness.uid, frame);
  const auto enemy = first_spawned_enemy(frame);
  ASSERT_TRUE(enemy.has_value());

  const Battle::BattleVector2 playerPos{enemy->position.x - 0.7,
                                        enemy->position.y};
  ASSERT_TRUE(harness.room->sync_battle(harness.uid, playerPos, {}));
  ASSERT_TRUE(harness.room->shoot_battle_player(harness.uid, {1.0, 0.0}));
  ASSERT_TRUE(harness.room->tick_battle(frame));

  EXPECT_GE(
      find_event(frame.events, Protocol::BattleEventType::BULLET_HIT_ENEMY), 0);
  EXPECT_GE(find_damage_to(frame.events, Protocol::EntityType::ENEMY), 0);
}

TEST(RoomTest, BattleBulletKillDestroysEnemy) {
  auto weapon = test_gun();
  weapon.damage = 10.0;
  auto harness = make_room_with_weapon(weapon, {test_enemy()});
  buy_and_select(*harness.room, harness.uid, "test_gun");
  Protocol::BattleFrameRsp frame;
  start_battle(*harness.room, harness.uid, frame);
  const auto enemy = first_spawned_enemy(frame);
  ASSERT_TRUE(enemy.has_value());
  const int enemyId = enemy->entityId;
  const Battle::BattleVector2 playerPos{enemy->position.x - 4.0,
                                        enemy->position.y};
  const auto shotDir = dir_to(playerPos, enemy->position);
  ASSERT_TRUE(harness.room->sync_battle(harness.uid, playerPos, {}));

  ASSERT_TRUE(harness.room->shoot_battle_player(harness.uid, shotDir));
  ASSERT_TRUE(tick_until_enemy_destroy(*harness.room, frame, enemyId));

  const int hitIndex =
      find_event(frame.events, Protocol::BattleEventType::BULLET_HIT_ENEMY);
  ASSERT_GE(hitIndex, 0);

  const int damageIndex =
      find_damage_to(frame.events, Protocol::EntityType::ENEMY);
  ASSERT_GE(damageIndex, 0);
  ASSERT_TRUE(frame.events[damageIndex].damageParameter.has_value());
  EXPECT_EQ(frame.events[damageIndex].damageParameter->currentHP, 0);

  const auto enemyDestroyed = std::find_if(
      frame.events.begin(), frame.events.end(),
      [enemyId](const Protocol::BattleEventDTO &event) {
        return event.eventType == Protocol::BattleEventType::ENTITY_DESTROY &&
               event.destroyParameter.has_value() &&
               event.destroyParameter->entityId == enemyId &&
               event.destroyParameter->entityType ==
                   Protocol::EntityType::ENEMY &&
               event.destroyParameter->destroyReason ==
                   Protocol::BattleEntityDestroyReason::ENTITY_DEAD;
      });
  EXPECT_NE(enemyDestroyed, frame.events.end());
}

TEST(RoomTest, BattleWavesKeepRunningAfterOneEnemyDies) {
  auto weapon = test_gun();
  weapon.damage = 10.0;
  auto harness = make_room_with_weapon(weapon, {test_enemy()});
  buy_and_select(*harness.room, harness.uid, "test_gun");

  Protocol::BattleFrameRsp frame;
  start_battle(*harness.room, harness.uid, frame);
  const auto enemy = first_spawned_enemy(frame);
  ASSERT_TRUE(enemy.has_value());
  const int enemyId = enemy->entityId;
  const Battle::BattleVector2 playerPos{enemy->position.x - 4.0,
                                        enemy->position.y};
  const auto shotDir = dir_to(playerPos, enemy->position);
  ASSERT_TRUE(harness.room->sync_battle(harness.uid, playerPos, {}));

  ASSERT_TRUE(harness.room->shoot_battle_player(harness.uid, shotDir));
  bool ended = false;
  ASSERT_TRUE(tick_until_enemy_destroy(*harness.room, frame, enemyId));
  EXPECT_FALSE(ended);
  EXPECT_TRUE(harness.room->tick_battle(frame, &ended));
  EXPECT_FALSE(ended);
}

TEST(RoomTest, EnemyReportsAreSpeedLimited) {
  auto harness = make_room();
  Protocol::BattleFrameRsp frame;
  start_battle(*harness.room, harness.uid, frame);
  const auto enemy = first_spawned_enemy(frame);
  ASSERT_TRUE(enemy.has_value());

  Protocol::BattlePos report;
  report.entityId = enemy->entityId;
  report.position = {0.0, 0.0};
  report.direction = dir_to(enemy->position, report.position);
  ASSERT_TRUE(harness.room->sync_battle(harness.uid, {0.0, 0.0}, {report}));
  ASSERT_TRUE(harness.room->tick_battle(frame));
  EXPECT_EQ(find_event(frame.events, Protocol::BattleEventType::ENTITY_DAMAGE),
            -1);

  const auto player = find_player(frame, harness.uid);
  ASSERT_TRUE(player.has_value());
  EXPECT_EQ(player->attribute.currentHP, player->attribute.maxHP);
}

TEST(RoomTest, EnemyFallbackMovesWhenClientsDoNotReport) {
  auto harness = make_room();
  Protocol::BattleFrameRsp frame;
  start_battle(*harness.room, harness.uid, frame);

  for (int tick = 0; tick < 40; ++tick) {
    ASSERT_TRUE(harness.room->tick_battle(frame));
    EXPECT_EQ(
        find_event(frame.events, Protocol::BattleEventType::ENTITY_DAMAGE), -1);
  }

  ASSERT_TRUE(tick_until(*harness.room, frame,
                         Protocol::BattleEventType::ENTITY_DAMAGE, 800));
  const int damageIndex =
      find_event(frame.events, Protocol::BattleEventType::ENTITY_DAMAGE);
  ASSERT_GE(damageIndex, 0);
  ASSERT_TRUE(frame.events[damageIndex].damageParameter.has_value());
  EXPECT_EQ(frame.events[damageIndex].damageParameter->sourceEntityType,
            Protocol::EntityType::ENEMY);
}

TEST(RoomTest, EnemyAttackDamagesPlayer) {
  auto enemy = test_enemy();
  enemy.attackDamage = 4;
  enemy.attackRange = 100.0;
  auto harness = make_room_with_weapon(test_gun(), {enemy});
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

TEST(RoomTest, EnemyAttackRangeIncludesEntityRadii) {
  auto enemy = test_enemy();
  enemy.attackDamage = 4;
  auto harness = make_room_with_weapon(test_gun(), {enemy});
  Protocol::BattleFrameRsp frame;
  start_battle(*harness.room, harness.uid, frame);
  const auto spawned = first_spawned_enemy(frame);
  ASSERT_TRUE(spawned.has_value());
  const Battle::BattleVector2 playerPos{spawned->position.x - 2.5,
                                        spawned->position.y};

  ASSERT_TRUE(harness.room->sync_battle(harness.uid, playerPos, {}));
  ASSERT_TRUE(tick_until(*harness.room, frame,
                         Protocol::BattleEventType::ENTITY_DAMAGE, 30));

  const int damageIndex =
      find_damage_to(frame.events, Protocol::EntityType::PLAYER);
  ASSERT_GE(damageIndex, 0);
  ASSERT_TRUE(frame.events[damageIndex].damageParameter.has_value());
  EXPECT_EQ(frame.events[damageIndex].damageParameter->damage, 4);
}

TEST(RoomTest, EnemySpawnExposesAttackCooldownAttribute) {
  auto enemy = test_enemy();
  enemy.attackCooldownTicks = 7;
  auto harness = make_room_with_weapon(test_gun(), {enemy});

  Protocol::BattleFrameRsp frame;
  start_battle(*harness.room, harness.uid, frame);
  const auto spawned = first_spawned_enemy(frame);
  ASSERT_TRUE(spawned.has_value());
  EXPECT_EQ(spawned->attribute.attackCoolDown, 7);
}

TEST(RoomTest, EnemyAttackUsesCooldownBeforeDamagingAgain) {
  auto enemy = test_enemy();
  enemy.attackDamage = 4;
  enemy.attackCooldownTicks = 5;
  enemy.attackRange = 100.0;
  auto harness = make_room_with_weapon(test_gun(), {enemy});

  Protocol::BattleFrameRsp frame;
  start_battle(*harness.room, harness.uid, frame);
  const auto spawned = first_spawned_enemy(frame);
  ASSERT_TRUE(spawned.has_value());

  ASSERT_TRUE(harness.room->sync_battle(harness.uid, spawned->position, {}));
  for (int tick = 0; tick < enemy.attackCooldownTicks - 1; ++tick) {
    ASSERT_TRUE(harness.room->tick_battle(frame));
    EXPECT_EQ(find_damage_to(frame.events, Protocol::EntityType::PLAYER), -1);
  }

  ASSERT_TRUE(harness.room->tick_battle(frame));
  int damageIndex = find_damage_to(frame.events, Protocol::EntityType::PLAYER);
  ASSERT_GE(damageIndex, 0);
  ASSERT_TRUE(frame.events[damageIndex].damageParameter.has_value());
  EXPECT_EQ(frame.events[damageIndex].damageParameter->damage, 4);
  EXPECT_EQ(frame.events[damageIndex].damageParameter->currentHP, 16);

  ASSERT_TRUE(harness.room->tick_battle(frame));
  EXPECT_EQ(find_damage_to(frame.events, Protocol::EntityType::PLAYER), -1);
}

TEST(RoomTest, BattleDefeatsWhenAllPlayersDie) {
  auto enemy = test_enemy();
  enemy.attackDamage = 4;
  enemy.attackRange = 100.0;
  auto harness = make_room_with_weapon(test_gun(), {enemy});
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
  EXPECT_TRUE(has_cleared_enemy_target(frame.events));

  const int destroyIndex =
      find_event(frame.events, Protocol::BattleEventType::ENTITY_DESTROY);
  ASSERT_GE(destroyIndex, 0);
  ASSERT_TRUE(frame.events[destroyIndex].destroyParameter.has_value());
  EXPECT_EQ(frame.events[destroyIndex].destroyParameter->entityType,
            Protocol::EntityType::PLAYER);
  EXPECT_FALSE(harness.room->tick_battle(frame));
}

TEST(RoomTest, BossNodeUsesBossEnemyPool) {
  auto harness = make_room();
  select_boss(*harness.room, harness.uid);
  Protocol::BattleFrameRsp frame;
  start_battle(*harness.room, harness.uid, frame);
  const auto enemies = spawned_enemies(frame);
  ASSERT_EQ(enemies.size(), 1U);
  EXPECT_EQ(enemies.front().attribute.maxHP, 36);
  EXPECT_EQ(enemies.front().attribute.currentHP, 36);
}

TEST(RoomTest, BattleUsesConfiguredStats) {
  auto state = std::make_shared<ServerState>();
  state->battleConfig = default_battle_config();
  state->battleConfig.playerMaxHP = 33;
  state->battleConfig.playerSpeed = 3.75;
  state->battleConfig.playerRadius = 0.8;
  state->battleConfig.enemies = {
      {BattleEnemyPool::NORMAL, Protocol::BattleEnemyType::BUBBLE_FISH, 42, 2.5,
       1.4, 0.0, 8, 11, 3.0, 0.0, 100.0},
  };

  Protocol::PlayerBasicInfo creatorInfo{"1", "creator", 1};
  state->userData.emplace(creatorInfo.uid,
                          Protocol::PlayerData{.basicInfo = creatorInfo});
  auto creator = std::make_shared<User>(creatorInfo.uid, state);
  Room room(77, 1, state, creator);

  Protocol::BattleFrameRsp frame;
  start_battle(room, creatorInfo.uid, frame);
  ASSERT_FALSE(frame.playerEntities.empty());
  EXPECT_EQ(frame.playerEntities.front().attribute.maxHP, 33);
  EXPECT_EQ(frame.playerEntities.front().attribute.currentHP, 33);
  EXPECT_DOUBLE_EQ(frame.playerEntities.front().attribute.speed, 3.75);

  const auto enemies = spawned_enemies(frame);
  ASSERT_EQ(enemies.size(), 1U);
  EXPECT_EQ(enemies.front().attribute.maxHP, 42);
  EXPECT_EQ(enemies.front().attribute.currentHP, 42);
}

TEST(RoomTest, BattleSpawnCanUseConfiguredRadiusWithoutBounds) {
  auto state = std::make_shared<ServerState>();
  state->battleConfig = default_battle_config();
  state->battleConfig.spawnRadiusMin = 10.0;
  state->battleConfig.spawnRadiusMax = 10.0;
  state->battleConfig.enemies = {
      {BattleEnemyPool::NORMAL, Protocol::BattleEnemyType::BUBBLE_FISH, 10, 1.5,
       1.0, 0.0, 4, 20, 3.0, 0.0, 100.0},
  };

  Protocol::PlayerBasicInfo creatorInfo{"1", "creator", 1};
  state->userData.emplace(creatorInfo.uid,
                          Protocol::PlayerData{.basicInfo = creatorInfo});
  auto creator = std::make_shared<User>(creatorInfo.uid, state);
  Room room(79, 1, state, creator);

  Protocol::BattleFrameRsp frame;
  start_battle(room, creatorInfo.uid, frame);
  const auto enemies = spawned_enemies(frame);
  ASSERT_EQ(enemies.size(), 1U);
  const double dist = std::sqrt(Battle::distance_squared(
      enemies.front().position, Battle::BattleVector2{0.0, 0.0}));
  EXPECT_NEAR(dist, 10.0, 1e-6);
}

TEST(RoomTest, BattleSpawnBudgetCarriesAcrossIntervals) {
  auto state = std::make_shared<ServerState>();
  state->battleConfig = default_battle_config();
  state->battleConfig.frameRate = 10;
  state->battleConfig.spawnIntervalSeconds = 0.1;
  state->battleConfig.baseSpawnBudget = 1.0;
  state->battleConfig.enemies = {
      {BattleEnemyPool::NORMAL, Protocol::BattleEnemyType::BUBBLE_FISH, 10, 1.5,
       1.0, 0.0, 4, 20, 3.0, 0.0, 100.0},
  };

  Protocol::PlayerBasicInfo creatorInfo{"1", "creator", 1};
  state->userData.emplace(creatorInfo.uid,
                          Protocol::PlayerData{.basicInfo = creatorInfo});
  auto creator = std::make_shared<User>(creatorInfo.uid, state);
  Room room(80, 1, state, creator);

  select_first_node(room, creatorInfo.uid);

  Protocol::BattleWaitRsp waitRsp;
  bool allReady = false;
  ASSERT_TRUE(room.set_battle_ready(creatorInfo.uid, waitRsp, allReady));
  ASSERT_TRUE(allReady);

  Protocol::BattleFrameRsp frame;
  ASSERT_TRUE(room.tick_battle(frame));
  EXPECT_TRUE(spawned_enemies(frame).empty());
  ASSERT_TRUE(room.tick_battle(frame));
  EXPECT_TRUE(spawned_enemies(frame).empty());
  ASSERT_TRUE(room.tick_battle(frame));
  EXPECT_EQ(spawned_enemies(frame).size(), 1U);
}

TEST(RoomTest, BattleWinsAfterTargetEnemySpawnsAreCleared) {
  auto state = std::make_shared<ServerState>();
  state->battleConfig = default_battle_config();
  state->battleConfig.frameRate = 10;
  state->battleConfig.targetEnemySpawns = 1;
  state->battleConfig.baseSpawnBudget = 1.0;
  state->battleConfig.spawnIntervalSeconds = 0.1;
  state->battleConfig.weapons = {test_gun()};
  state->battleConfig.enemies = {
      {BattleEnemyPool::NORMAL, Protocol::BattleEnemyType::BUBBLE_FISH, 1, 1.5,
       0.0, 0.0, 0, 20, 1.0, 0.0, 100.0},
  };
  state->shopCatalogItemIds = {"test_gun"};

  Protocol::PlayerBasicInfo creatorInfo{"1", "creator", 1};
  state->userData.emplace(creatorInfo.uid,
                          Protocol::PlayerData{.basicInfo = creatorInfo});
  auto creator = std::make_shared<User>(creatorInfo.uid, state);
  Room room(78, 1, state, creator);
  buy_and_select(room, creatorInfo.uid, "test_gun");

  Protocol::BattleWaitRsp waitRsp;
  bool allReady = false;
  ASSERT_TRUE(room.set_battle_ready(creatorInfo.uid, waitRsp, allReady));
  ASSERT_TRUE(allReady);

  Protocol::BattleFrameRsp frame;
  bool ended = false;
  ASSERT_TRUE(room.tick_battle(frame, &ended));
  EXPECT_FALSE(ended);
  const auto enemy = first_spawned_enemy(frame);
  ASSERT_TRUE(enemy.has_value());

  const auto shotDir = dir_to({0.0, 0.0}, enemy->position);
  ASSERT_TRUE(room.shoot_battle_player(creatorInfo.uid, shotDir));
  for (int tick = 0; tick < 30 && !ended; ++tick) {
    ASSERT_TRUE(room.tick_battle(frame, &ended));
  }
  EXPECT_TRUE(ended);
  EXPECT_FALSE(room.tick_battle(frame));
}

} // namespace
