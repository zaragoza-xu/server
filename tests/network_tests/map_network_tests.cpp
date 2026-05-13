#include <string>
#include <unordered_set>

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

  // The first MAP_MOVE must select a root node (no incoming edges). See
  // Room::move_map: when mapNodeId < 0, it rejects nodes that appear in any
  // other node's `nextId`.
  std::unordered_set<int> incoming;
  for (const auto &node : mapArr) {
    if (!node.contains("nextId") || !node.at("nextId").is_array()) {
      continue;
    }
    for (const auto &nid : node.at("nextId")) {
      if (nid.is_number_integer()) {
        incoming.insert(nid.get<int>());
      }
    }
  }

  int rootSelectId = -1;
  for (const auto &node : mapArr) {
    const int nodeId = node.at("nodeId").get<int>();
    if (incoming.count(nodeId) == 0) {
      rootSelectId = nodeId;
      break;
    }
  }
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

