#pragma once

#include <string>

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

