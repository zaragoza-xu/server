#include <string>

#include <gtest/gtest.h>

#include "integration_helpers.h"
#include "protocol.h"

namespace {

using json = nlohmann::json;

TEST(MapNetworkTest, MapServer_InitAndMovePush) {
  network_tests::MultiServiceHarness harness;
  const std::string uid =
      network_tests::auth_register_login(harness);
  const int roomId = network_tests::lobby_create_room(harness, uid, 1);
  network_tests::lobby_ready(harness, uid);

  auto mapClient = harness.make_map_client();
  mapClient->send_json(
      json{{"type", static_cast<int>(Protocol::MapRequestType::MAP_INIT)},
           {"roomId", roomId},
           {"uid", uid}});

  const json initLine = mapClient->read_json();
  ASSERT_EQ(initLine.at("type"),
            static_cast<int>(Protocol::MapRequestType::MAP_INIT));

  const auto mapArr = initLine.at("data").at("map");
  ASSERT_TRUE(mapArr.is_array());
  ASSERT_FALSE(mapArr.empty());
  ASSERT_FALSE(mapArr.at(0).at("nextId").empty());

  const int rootSelectId = network_tests::map_first_root(mapArr);
  ASSERT_NE(rootSelectId, -1);

  // MAP_MOVE is NoResponseRsp, so we expect the broadcast push.
  mapClient->send_json(
      json{{"type", static_cast<int>(Protocol::MapRequestType::MAP_MOVE)},
           {"uid", uid},
           {"selectId", rootSelectId}});

  const json movePush = mapClient->read_json();
  EXPECT_EQ(movePush.at("type"),
            static_cast<int>(Protocol::MapRequestType::MAP_MOVE));
  ASSERT_TRUE(movePush.at("data").contains("selectStatus"));
}

TEST(MapNetworkTest, MapInitWhenNotInRoomReturnsRoomStateError) {
  network_tests::MultiServiceHarness harness;
  const std::string uid = network_tests::auth_register_login(harness);

  auto mapClient = harness.make_map_client();
  mapClient->send_json(
      json{{"type", static_cast<int>(Protocol::MapRequestType::MAP_INIT)},
           {"roomId", 1},
           {"uid", uid}});
  const json rsp = mapClient->read_json();
  EXPECT_EQ(rsp.at("code"),
            (Protocol::SERVICE_FAIL | Protocol::ROOM_STATE_ERROR));
}

TEST(MapNetworkTest, MapInitWhenUidNeverLoggedInReturnsNotFound) {
  network_tests::MultiServiceHarness harness;
  const std::string uid = network_tests::auth_register_only(harness);

  auto mapClient = harness.make_map_client();
  mapClient->send_json(
      json{{"type", static_cast<int>(Protocol::MapRequestType::MAP_INIT)},
           {"roomId", 1},
           {"uid", uid}});
  const json rsp = mapClient->read_json();
  EXPECT_EQ(rsp.at("code"), (Protocol::SERVICE_FAIL | Protocol::NOT_FOUND));
}

TEST(MapNetworkTest, MapMoveWithUnknownNodeReturnsRoomStateError) {
  network_tests::MultiServiceHarness harness;
  const std::string uid = network_tests::auth_register_login(harness);
  const int roomId = network_tests::lobby_create_room(harness, uid, 1);
  network_tests::lobby_ready(harness, uid);

  auto mapClient = harness.make_map_client();
  mapClient->send_json(
      json{{"type", static_cast<int>(Protocol::MapRequestType::MAP_INIT)},
           {"roomId", roomId},
           {"uid", uid}});
  const json initLine = mapClient->read_json();
  ASSERT_EQ(initLine.at("type").get<int>(),
            static_cast<int>(Protocol::MapRequestType::MAP_INIT));

  mapClient->send_json(
      json{{"type", static_cast<int>(Protocol::MapRequestType::MAP_MOVE)},
           {"uid", uid},
           {"selectId", -99'999}});
  const json err = mapClient->read_json();
  EXPECT_EQ(err.at("code"),
            (Protocol::SERVICE_FAIL | Protocol::ROOM_STATE_ERROR));
}

} // namespace
