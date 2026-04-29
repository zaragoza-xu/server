#include <string>

#include <gtest/gtest.h>

#include "integration_helpers.h"
#include "protocol.h"

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

TEST(BattleNetworkTest, BattleServer_SinglePlayerReadyBroadcastsStartFrame) {
  network_tests::MultiServiceHarness harness;
  const std::string uid =
      network_tests::auth_register_login(harness);
  network_tests::lobby_create_room(harness, uid, 1);

  auto battle = harness.make_battle_client();
  battle->send_json(
      json{{"type", static_cast<int>(Protocol::BattleRequestType::PLAYER_READY)},
           {"uid", uid}});

  // There is a possible race with the server tick loop. Read a few frames
  // until we observe the BATTLE_START push.
  bool has_start = false;
  int seen_serverTick = -1;
  for (int i = 0; i < 3 && !has_start; ++i) {
    const json push = battle->read_json();
    if (push.at("type").get<int>() ==
            static_cast<int>(Protocol::BattleResponseType::BATTLE_FRAME) &&
        push_messages_contains(push,
                                static_cast<int>(
                                    Protocol::BattlePushMessageType::BATTLE_START))) {
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
  const std::string alice =
      network_tests::auth_register_login(harness);
  const std::string bob =
      network_tests::auth_register_login(harness);

  const int roomId = network_tests::lobby_create_room(harness, alice, 2);
  network_tests::lobby_join_room(harness, roomId, bob);

  auto battleAlice = harness.make_battle_client();
  auto battleBob = harness.make_battle_client();

  battleAlice->send_json(
      json{{"type", static_cast<int>(Protocol::BattleRequestType::PLAYER_READY)},
           {"uid", alice}});

  const json waitPush = battleAlice->read_json();
  EXPECT_EQ(waitPush.at("type").get<int>(),
            static_cast<int>(Protocol::BattleResponseType::BATTLE_WAIT));

  battleBob->send_json(
      json{{"type", static_cast<int>(Protocol::BattleRequestType::PLAYER_READY)},
           {"uid", bob}});

  const json startPush = battleBob->read_json();
  EXPECT_EQ(startPush.at("type").get<int>(),
            static_cast<int>(Protocol::BattleResponseType::BATTLE_FRAME));
}

TEST(BattleNetworkTest,
     BattlePlayerReadyWhenNotInRoomReturnsRoomStateError) {
  network_tests::MultiServiceHarness harness;
  const std::string uid = network_tests::auth_register_login(harness);

  auto battle = harness.make_battle_client();
  battle->send_json(
      json{{"type", static_cast<int>(Protocol::BattleRequestType::PLAYER_READY)},
           {"uid", uid}});
  const json rsp = battle->read_json();
  EXPECT_EQ(rsp.at("code"),
            (Protocol::SERVICE_FAIL | Protocol::ROOM_STATE_ERROR));
}

} // namespace

