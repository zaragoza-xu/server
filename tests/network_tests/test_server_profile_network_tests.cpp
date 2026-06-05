#include <functional>

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

TEST(TestServerProfileNetworkTest, ShortBattleProfileCompletesWithinFifteenSeconds) {
  network_tests::MultiServiceHarness harness;
  auto state = harness.get_state();
  state->battleConfig = Battle::default_battle_config();
  state->battleConfig.durationSeconds = 5.0;

  const std::string uid = network_tests::auth_register_login(harness);
  const int roomId = network_tests::lobby_create_room(harness, uid, 1);
  network_tests::enter_map(harness, roomId, {uid});

  auto battle = harness.make_battle_client();
  battle->send_json(json{
      {"type", static_cast<int>(Protocol::BattleRequestType::PLAYER_READY)},
      {"uid", uid}});

  bool sawEnd = false;
  for (int i = 0; i < 80 && !sawEnd; ++i) {
    const json push = battle->read_json();
    if (push.at("type").get<int>() ==
            static_cast<int>(Protocol::BattleResponseType::BATTLE_FRAME) &&
        push_messages_contains(
            push, static_cast<int>(Protocol::BattlePushMessageType::BATTLE_END))) {
      sawEnd = true;
    }
  }
  EXPECT_TRUE(sawEnd);
}

} // namespace
