#include <gtest/gtest.h>

#include "integration_helpers.h"
#include "protocol.h"

namespace {

using json = nlohmann::json;

TEST(GetStateStatusNetworkTest, ReadyTransitionsRoomPhaseToShop) {
  network_tests::MultiServiceHarness harness;
  const std::string uid = network_tests::auth_register_login(harness);

  auto lobby = harness.make_lobby_client();
  lobby->send_json(
      json{{"type", static_cast<int>(Protocol::HomeRequestType::CREATE_ROOM)},
           {"uid", uid},
           {"maximumPeople", 1}});
  const json createRsp = lobby->read_json();
  ASSERT_EQ(createRsp.at("code"), Protocol::SERVICE_SUCCESS);

  lobby->send_json(json{
      {"type", static_cast<int>(Protocol::HomeRequestType::GET_STATE_STATUS)},
      {"uid", uid}});
  const json beforeReady = lobby->read_json();
  ASSERT_EQ(beforeReady.at("code"), Protocol::SERVICE_SUCCESS);
  EXPECT_EQ(beforeReady.at("data").at("roomPhase").get<int>(), 0);

  network_tests::lobby_ready(harness, uid);

  lobby->send_json(json{
      {"type", static_cast<int>(Protocol::HomeRequestType::GET_STATE_STATUS)},
      {"uid", uid}});
  const json afterReady = lobby->read_json();
  ASSERT_EQ(afterReady.at("code"), Protocol::SERVICE_SUCCESS);
  EXPECT_EQ(afterReady.at("data").at("roomPhase").get<int>(), 1);
  EXPECT_TRUE(afterReady.at("data").at("allLobbyReady").get<bool>());
}

TEST(GetStateStatusNetworkTest, UnknownUidReturnsNotFound) {
  network_tests::MultiServiceHarness harness;
  auto lobby = harness.make_lobby_client();
  lobby->send_json(json{
      {"type", static_cast<int>(Protocol::HomeRequestType::GET_STATE_STATUS)},
      {"uid", "9999"}});
  const json rsp = lobby->read_json();
  EXPECT_EQ(rsp.at("code").get<int>(),
            Protocol::SERVICE_FAIL | Protocol::NOT_FOUND);
}

} // namespace
