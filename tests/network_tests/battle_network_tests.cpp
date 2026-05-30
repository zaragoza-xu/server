#include <cmath>
#include <functional>
#include <memory>
#include <string>

#include <gtest/gtest.h>

#include "integration_helpers.h"

namespace {

using json = nlohmann::json;

static bool push_messages_contains(const json &pushEnvelope, int target) {
  if (!pushEnvelope.contains("pushMessages")) {
    return false;
  }
  for (const auto &msg : pushEnvelope.at("pushMessages")) {
    if (msg.is_number_integer() && msg.get<int>() == target) {
      return true;
    }
  }
  return false;
}

static bool frame_has_event(const json &frame,
                            Protocol::BattleEventType eventType) {
  if (frame.at("type").get<int>() !=
      static_cast<int>(Protocol::BattleResponseType::BATTLE_FRAME)) {
    return false;
  }
  for (const auto &event : frame.at("data").at("events")) {
    if (event.at("eventType").get<int>() == static_cast<int>(eventType)) {
      return true;
    }
  }
  return false;
}

static json first_spawned_enemy(const json &frame) {
  for (const auto &event : frame.at("data").at("events")) {
    if (event.at("eventType").get<int>() ==
            static_cast<int>(Protocol::BattleEventType::ENEMY_SPAWN) &&
        event.contains("spawnParameter") &&
        !event.at("spawnParameter").is_null()) {
      return event.at("spawnParameter").at("enemyEntity");
    }
  }
  return json();
}

static json direction_to(const json &position) {
  const double x = position.at("x").get<double>();
  const double y = position.at("y").get<double>();
  const double len = std::sqrt(x * x + y * y);
  if (len <= 0.0) {
    return json{{"x", 1.0}, {"y", 0.0}};
  }
  return json{{"x", x / len}, {"y", y / len}};
}

static Battle::WeaponDef test_gun() {
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

static void use_test_gun(network_tests::MultiServiceHarness &harness) {
  auto state = harness.get_state();
  state->shopCatalogItemIds = {"test_gun"};
  state->battleConfig = Battle::default_battle_config();
  state->battleConfig.weapons = {test_gun()};
}

static void enter_map_with_test_gun(network_tests::MultiServiceHarness &harness,
                                    int roomId, const std::string &uid) {
  network_tests::lobby_ready(harness, uid);

  auto shop = harness.make_shop_client();
  shop->send_json(
      json{{"type", static_cast<int>(Protocol::ShopRequestType::SHOP_BUY)},
           {"uid", uid},
           {"itemId", "test_gun"}});
  const json buyPush = shop->read_json();
  ASSERT_EQ(buyPush.at("type").get<int>(),
            static_cast<int>(Protocol::ShopResponseType::SHOP_SYNC));

  network_tests::select_first_map_node(harness, roomId, {uid});
}

static json read_frame_until(const std::shared_ptr<FakeClient> &client,
                             const std::function<bool(const json &)> &match) {
  for (int i = 0; i < 40; ++i) {
    const json frame = client->read_json();
    if (frame.at("type").get<int>() ==
            static_cast<int>(Protocol::BattleResponseType::BATTLE_FRAME) &&
        match(frame)) {
      return frame;
    }
  }
  return json();
}

TEST(BattleNetworkTest, BattleServer_SinglePlayerReadyBroadcastsStartFrame) {
  network_tests::MultiServiceHarness harness;
  const std::string uid = network_tests::auth_register_login(harness);
  const int roomId = network_tests::lobby_create_room(harness, uid, 1);
  network_tests::enter_map(harness, roomId, {uid});

  auto battle = harness.make_battle_client();
  battle->send_json(json{
      {"type", static_cast<int>(Protocol::BattleRequestType::PLAYER_READY)},
      {"uid", uid}});

  // There is a possible race with the server tick loop. Read a few frames
  // until we observe the BATTLE_START push.
  bool has_start = false;
  int seen_serverTick = -1;
  for (int i = 0; i < 3 && !has_start; ++i) {
    const json push = battle->read_json();
    if (push.at("type").get<int>() ==
            static_cast<int>(Protocol::BattleResponseType::BATTLE_FRAME) &&
        push_messages_contains(
            push,
            static_cast<int>(Protocol::BattlePushMessageType::BATTLE_START))) {
      has_start = true;
      seen_serverTick = push.at("data").at("serverTick").get<int>();
      break;
    }
  }

  EXPECT_TRUE(has_start);
  ASSERT_GE(seen_serverTick, 0);
}

TEST(BattleNetworkTest,
     BattleServer_TwoPlayersFirstReadySeesWaitThenStartAfterSecond) {
  network_tests::MultiServiceHarness harness;
  const std::string alice = network_tests::auth_register_login(harness);
  const std::string bob = network_tests::auth_register_login(harness);

  const int roomId = network_tests::lobby_create_room(harness, alice, 2);
  network_tests::lobby_join_room(harness, roomId, bob);
  network_tests::enter_map(harness, roomId, {alice, bob});

  auto battleAlice = harness.make_battle_client();
  auto battleBob = harness.make_battle_client();

  battleAlice->send_json(json{
      {"type", static_cast<int>(Protocol::BattleRequestType::PLAYER_READY)},
      {"uid", alice}});

  const json waitPush = battleAlice->read_json();
  EXPECT_EQ(waitPush.at("type").get<int>(),
            static_cast<int>(Protocol::BattleResponseType::BATTLE_WAIT));

  battleBob->send_json(json{
      {"type", static_cast<int>(Protocol::BattleRequestType::PLAYER_READY)},
      {"uid", bob}});

  const json startPush = battleBob->read_json();
  EXPECT_EQ(startPush.at("type").get<int>(),
            static_cast<int>(Protocol::BattleResponseType::BATTLE_FRAME));
}

TEST(BattleNetworkTest, BattlePlayerReadyWhenNotInRoomReturnsRoomStateError) {
  network_tests::MultiServiceHarness harness;
  const std::string uid = network_tests::auth_register_login(harness);

  auto battle = harness.make_battle_client();
  battle->send_json(json{
      {"type", static_cast<int>(Protocol::BattleRequestType::PLAYER_READY)},
      {"uid", uid}});
  const json rsp = battle->read_json();
  EXPECT_EQ(rsp.at("code"),
            (Protocol::SERVICE_FAIL | Protocol::ROOM_STATE_ERROR));
}

TEST(BattleNetworkTest, BattleServer_PlayerShootBroadcastsSpawn) {
  network_tests::MultiServiceHarness harness;
  use_test_gun(harness);
  const std::string uid = network_tests::auth_register_login(harness);
  const int roomId = network_tests::lobby_create_room(harness, uid, 1);
  enter_map_with_test_gun(harness, roomId, uid);

  auto battle = harness.make_battle_client();
  battle->send_json(json{
      {"type", static_cast<int>(Protocol::BattleRequestType::PLAYER_READY)},
      {"uid", uid}});
  const json startFrame = read_frame_until(battle, [](const json &frame) {
    return push_messages_contains(
        frame, static_cast<int>(Protocol::BattlePushMessageType::BATTLE_START));
  });
  ASSERT_FALSE(startFrame.is_null());
  ASSERT_FALSE(startFrame.at("data").at("playerEntities").empty());
  const auto &player = startFrame.at("data").at("playerEntities").front();
  EXPECT_EQ(player.at("weaponId").get<std::string>(), "test_gun");
  EXPECT_FALSE(player.contains("weapon"));
  EXPECT_FALSE(player.contains("weaponType"));

  battle->send_json(json{
      {"type", static_cast<int>(Protocol::BattleRequestType::PLAYER_SHOOT)},
      {"uid", uid},
      {"direction", {{"x", 1.0}, {"y", 0.0}}}});
  const json shootFrame = read_frame_until(battle, [](const json &frame) {
    return frame_has_event(frame, Protocol::BattleEventType::BULLET_SPAWN);
  });
  ASSERT_FALSE(shootFrame.is_null());
  EXPECT_TRUE(
      frame_has_event(shootFrame, Protocol::BattleEventType::BULLET_SPAWN));
  for (const auto &event : shootFrame.at("data").at("events")) {
    if (event.at("eventType").get<int>() !=
        static_cast<int>(Protocol::BattleEventType::BULLET_SPAWN)) {
      continue;
    }
    const auto &bullet = event.at("spawnParameter").at("bulletEntity");
    const auto &attribute = bullet.at("attribute");
    EXPECT_TRUE(attribute.contains("speed"));
    EXPECT_TRUE(attribute.contains("size"));
    EXPECT_FALSE(bullet.contains("projectile"));
    EXPECT_FALSE(attribute.contains("canPierce"));
    EXPECT_FALSE(attribute.contains("pierceCount"));
    EXPECT_FALSE(attribute.contains("projectilePrefab"));
    EXPECT_FALSE(attribute.contains("sprite"));
    break;
  }
}

TEST(BattleNetworkTest, BattleServer_PlayerBulletHitEnemyBroadcastsDamage) {
  network_tests::MultiServiceHarness harness;
  use_test_gun(harness);
  const std::string uid = network_tests::auth_register_login(harness);
  const int roomId = network_tests::lobby_create_room(harness, uid, 1);
  enter_map_with_test_gun(harness, roomId, uid);

  auto battle = harness.make_battle_client();
  battle->send_json(json{
      {"type", static_cast<int>(Protocol::BattleRequestType::PLAYER_READY)},
      {"uid", uid}});
  const json startFrame = read_frame_until(battle, [](const json &frame) {
    return push_messages_contains(
        frame, static_cast<int>(Protocol::BattlePushMessageType::BATTLE_START));
  });
  ASSERT_FALSE(startFrame.is_null());
  const json enemy = first_spawned_enemy(startFrame);
  ASSERT_FALSE(enemy.is_null());
  const json shootDir = direction_to(enemy.at("position"));
  battle->send_json(json{
      {"type", static_cast<int>(Protocol::BattleRequestType::PLAYER_SHOOT)},
      {"uid", uid},
      {"direction", shootDir}});

  const json hitFrame = read_frame_until(battle, [](const json &frame) {
    return frame_has_event(frame,
                           Protocol::BattleEventType::BULLET_HIT_ENEMY) &&
           frame_has_event(frame, Protocol::BattleEventType::ENTITY_DAMAGE);
  });
  ASSERT_FALSE(hitFrame.is_null());

  bool sawDamage = false;
  for (const auto &event : hitFrame.at("data").at("events")) {
    if (event.at("eventType").get<int>() !=
        static_cast<int>(Protocol::BattleEventType::ENTITY_DAMAGE)) {
      continue;
    }
    const auto &damage = event.at("damageParameter");
    if (damage.at("targetEntityType").get<int>() ==
        static_cast<int>(Protocol::EntityType::ENEMY)) {
      sawDamage = true;
      EXPECT_EQ(damage.at("damage").get<int>(), 5);
      EXPECT_EQ(damage.at("currentHP").get<int>(), 5);
    }
  }
  EXPECT_TRUE(sawDamage);
}

TEST(BattleNetworkTest, BattleServer_StartFrameCarriesEnemySpawnEvent) {
  network_tests::MultiServiceHarness harness;
  const std::string uid = network_tests::auth_register_login(harness);
  const int roomId = network_tests::lobby_create_room(harness, uid, 1);
  network_tests::enter_map(harness, roomId, {uid});

  auto battle = harness.make_battle_client();
  battle->send_json(json{
      {"type", static_cast<int>(Protocol::BattleRequestType::PLAYER_READY)},
      {"uid", uid}});
  const json startFrame = read_frame_until(battle, [](const json &frame) {
    return push_messages_contains(
        frame, static_cast<int>(Protocol::BattlePushMessageType::BATTLE_START));
  });
  ASSERT_FALSE(startFrame.is_null());
  const json enemy = first_spawned_enemy(startFrame);
  ASSERT_FALSE(enemy.is_null());
  EXPECT_EQ(enemy.at("entityType").get<int>(),
            static_cast<int>(Protocol::EntityType::ENEMY));
  EXPECT_EQ(enemy.at("enemyType").get<int>(),
            static_cast<int>(Protocol::BattleEnemyType::BUBBLE_FISH));
  EXPECT_FALSE(enemy.at("attribute").contains("knockbackResist"));
}

} // namespace
