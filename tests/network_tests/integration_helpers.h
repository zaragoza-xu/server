#pragma once

#include <memory>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include "multi_service_harness.h"
#include "protocol.h"

namespace network_tests {

using json = nlohmann::json;

inline std::string auth_register_login(MultiServiceHarness &harness) {
  auto client = harness.make_auth_client();
  client->send_json(
      json{{"type", static_cast<int>(Protocol::LoginRequestType::REGISTER)}});
  const json reg = client->read_json();
  EXPECT_EQ(reg.at("code"), Protocol::SERVICE_SUCCESS);

  const std::string uid = reg.at("data").at("uid").get<std::string>();
  EXPECT_FALSE(uid.empty());

  client->send_json(
      json{{"type", static_cast<int>(Protocol::LoginRequestType::LOGIN)},
           {"uid", uid}});
  const json login = client->read_json();
  EXPECT_EQ(login.at("code"), Protocol::SERVICE_SUCCESS);
  return uid;
}

/** 仅注册账号（未登录），用于验证「未上线用户」类的业务边界。 */
inline std::string auth_register_only(MultiServiceHarness &harness) {
  auto client = harness.make_auth_client();
  client->send_json(
      json{{"type", static_cast<int>(Protocol::LoginRequestType::REGISTER)}});
  const json reg = client->read_json();
  EXPECT_EQ(reg.at("code"), Protocol::SERVICE_SUCCESS);
  const std::string uid = reg.at("data").at("uid").get<std::string>();
  EXPECT_FALSE(uid.empty());
  return uid;
}

inline int lobby_create_room(MultiServiceHarness &harness,
                              const std::string &uid,
                              int maximumPeople) {
  auto client = harness.make_lobby_client();
  client->send_json(
      json{{"type", static_cast<int>(Protocol::HomeRequestType::CREATE_ROOM)},
           {"uid", uid},
           {"maximumPeople", maximumPeople}});
  const json rsp = client->read_json();
  EXPECT_EQ(rsp.at("code"), Protocol::SERVICE_SUCCESS);
  return rsp.at("data").at("roomInfo").at("roomId").get<int>();
}

inline void lobby_join_room(MultiServiceHarness &harness,
                             int roomId,
                             const std::string &uid) {
  auto client = harness.make_lobby_client();
  client->send_json(
      json{{"type", static_cast<int>(Protocol::HomeRequestType::JOIN_ROOM)},
           {"roomId", roomId},
           {"uid", uid}});
  const json rsp = client->read_json();
  EXPECT_EQ(rsp.at("code"), Protocol::SERVICE_SUCCESS);
}

inline void lobby_ready(MultiServiceHarness &harness, const std::string &uid) {
  auto client = harness.make_lobby_client();
  client->send_json(
      json{{"type", static_cast<int>(Protocol::HomeRequestType::SET_READY)},
           {"uid", uid},
           {"ready", true}});
  const json rsp = client->read_json();
  EXPECT_EQ(rsp.at("type").get<int>(),
            static_cast<int>(Protocol::HomeRequestType::BROADCAST));
}

inline int map_first_root(const json &mapArr) {
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

  for (const auto &node : mapArr) {
    const int nodeId = node.at("nodeId").get<int>();
    if (incoming.count(nodeId) == 0) {
      return nodeId;
    }
  }
  return -1;
}

inline void select_first_map_node(MultiServiceHarness &harness, int roomId,
                                  const std::vector<std::string> &uids) {
  ASSERT_FALSE(uids.empty());

  struct MapClient {
    std::string uid;
    std::shared_ptr<FakeClient> client;
  };

  std::vector<MapClient> clients;
  clients.reserve(uids.size());
  int rootSelectId = -1;

  for (const auto &uid : uids) {
    auto client = harness.make_map_client();
    client->send_json(
        json{{"type", static_cast<int>(Protocol::MapRequestType::MAP_INIT)},
             {"roomId", roomId},
             {"uid", uid}});
    const json initLine = client->read_json();
    ASSERT_EQ(initLine.at("type").get<int>(),
              static_cast<int>(Protocol::MapRequestType::MAP_INIT));
    if (rootSelectId < 0) {
      rootSelectId = map_first_root(initLine.at("data").at("map"));
    }
    clients.push_back(MapClient{uid, std::move(client)});
  }

  ASSERT_NE(rootSelectId, -1);

  for (const auto &sender : clients) {
    sender.client->send_json(
        json{{"type", static_cast<int>(Protocol::MapRequestType::MAP_MOVE)},
             {"uid", sender.uid},
             {"selectId", rootSelectId}});
    for (const auto &bound : clients) {
      const json movePush = bound.client->read_json();
      EXPECT_EQ(movePush.at("type").get<int>(),
                static_cast<int>(Protocol::MapRequestType::MAP_MOVE));
    }
  }
}

inline void enter_map(MultiServiceHarness &harness, int roomId,
                      const std::vector<std::string> &uids) {
  for (const auto &uid : uids) {
    lobby_ready(harness, uid);
  }
  select_first_map_node(harness, roomId, uids);
}

inline void auth_logout(MultiServiceHarness &harness,
                        const std::string &uid) {
  auto client = harness.make_auth_client();
  client->send_json(
      json{{"type", static_cast<int>(Protocol::LoginRequestType::LOGOUT)},
           {"uid", uid}});
  const json rsp = client->read_json();
  EXPECT_EQ(rsp.at("code"), Protocol::SERVICE_SUCCESS);
}

inline void lobby_leave_room(MultiServiceHarness &harness,
                             const std::string &uid) {
  auto client = harness.make_lobby_client();
  client->send_json(
      json{{"type", static_cast<int>(Protocol::HomeRequestType::LEAVE_ROOM)},
           {"uid", uid}});
  const json rsp = client->read_json();
  EXPECT_EQ(rsp.at("type").get<int>(),
            static_cast<int>(Protocol::HomeRequestType::LEAVE_ROOM));
}

} // namespace network_tests
