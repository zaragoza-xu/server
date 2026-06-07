#include <string>

#include <gtest/gtest.h>

#include "network_test_framework.h"
#include "protocol.h"
#include "server.h"

namespace {

using json = nlohmann::json;

TEST(LoginNetworkTest, RegisterAndLoginOverTcp) {
  network_tests::ServerHarness<LoginServer> harness;
  auto client = harness.make_client();

  client->send_json(
      json{{"type", static_cast<int>(Protocol::LoginRequestType::REGISTER)}});
  const json registerRsp = client->read_json();
  ASSERT_EQ(registerRsp.at("code"), Protocol::SERVICE_SUCCESS);
  ASSERT_TRUE(registerRsp.at("data").contains("uid"));

  const std::string uid = registerRsp.at("data").at("uid").get<std::string>();
  ASSERT_FALSE(uid.empty());

  client->send_json(
      json{{"type", static_cast<int>(Protocol::LoginRequestType::LOGIN)},
           {"uid", uid}});
  const json loginRsp = client->read_json();
  ASSERT_EQ(loginRsp.at("code"), Protocol::SERVICE_SUCCESS);
  ASSERT_EQ(loginRsp.at("data").at("playerData").at("basicInfo").at("uid"), uid);
}

TEST(LoginNetworkTest, UnknownTypeReturnsBadRequest) {
  network_tests::ServerHarness<LoginServer> harness;
  auto client = harness.make_client();

  client->send_json(json{{"type", 9999}});
  const json rsp = client->read_json();
  EXPECT_EQ(rsp.at("code"), (Protocol::SERVICE_FAIL | Protocol::BAD_REQUEST));
}

TEST(LoginNetworkTest, MissingTypeReturnsBadRequest) {
  network_tests::ServerHarness<LoginServer> harness;
  auto client = harness.make_client();

  client->send_json(json::object());
  const json rsp = client->read_json();
  EXPECT_EQ(rsp.at("code"), (Protocol::SERVICE_FAIL | Protocol::BAD_REQUEST));
}

TEST(LoginNetworkTest, InvalidJsonReturnsDeserializeError) {
  network_tests::ServerHarness<LoginServer> harness;
  auto client = harness.make_client();

  client->send_frame("{invalid json");
  const json rsp = client->read_json();
  EXPECT_EQ(rsp.at("code"),
            (Protocol::SYSTEM_ERROR | Protocol::DESERIALIZE_FAIL));
}

TEST(LoginNetworkTest, WrongFieldTypeReturnsDeserializeError) {
  network_tests::ServerHarness<LoginServer> harness;
  auto client = harness.make_client();

  client->send_json(
      json{{"type", static_cast<int>(Protocol::LoginRequestType::LOGIN)},
           {"uid", 42}});
  const json rsp = client->read_json();
  EXPECT_EQ(rsp.at("code"),
            (Protocol::SYSTEM_ERROR | Protocol::DESERIALIZE_FAIL));
}

TEST(LoginNetworkTest, LoginWithEmptyUidReturnsBadRequest) {
  network_tests::ServerHarness<LoginServer> harness;
  auto client = harness.make_client();

  client->send_json(
      json{{"type", static_cast<int>(Protocol::LoginRequestType::LOGIN)},
           {"uid", ""}});
  const json rsp = client->read_json();
  EXPECT_EQ(rsp.at("code"), (Protocol::SERVICE_FAIL | Protocol::BAD_REQUEST));
}

TEST(LoginNetworkTest, LoginWithUnknownUidReturnsNotFound) {
  network_tests::ServerHarness<LoginServer> harness;
  auto client = harness.make_client();

  client->send_json(
      json{{"type", static_cast<int>(Protocol::LoginRequestType::LOGIN)},
           {"uid", "no_such_registered_user_id"}});
  const json rsp = client->read_json();
  EXPECT_EQ(rsp.at("code"), (Protocol::SERVICE_FAIL | Protocol::NOT_FOUND));
}

TEST(LoginNetworkTest, SecondLoginWhileOnlineReturnsBadRequest) {
  network_tests::ServerHarness<LoginServer> harness;
  auto client = harness.make_client();

  client->send_json(
      json{{"type", static_cast<int>(Protocol::LoginRequestType::REGISTER)}});
  const json registerRsp = client->read_json();
  ASSERT_EQ(registerRsp.at("code"), Protocol::SERVICE_SUCCESS);
  const std::string uid =
      registerRsp.at("data").at("uid").get<std::string>();

  client->send_json(
      json{{"type", static_cast<int>(Protocol::LoginRequestType::LOGIN)},
           {"uid", uid}});
  const json firstLogin = client->read_json();
  ASSERT_EQ(firstLogin.at("code"), Protocol::SERVICE_SUCCESS);

  client->send_json(
      json{{"type", static_cast<int>(Protocol::LoginRequestType::LOGIN)},
           {"uid", uid}});
  const json secondLogin = client->read_json();
  EXPECT_EQ(secondLogin.at("code"),
            (Protocol::SERVICE_FAIL | Protocol::BAD_REQUEST));
}

TEST(LoginNetworkTest, LogoutThenLoginAgainSucceeds) {
  network_tests::ServerHarness<LoginServer> harness;
  auto client = harness.make_client();

  client->send_json(
      json{{"type", static_cast<int>(Protocol::LoginRequestType::REGISTER)}});
  const json registerRsp = client->read_json();
  ASSERT_EQ(registerRsp.at("code"), Protocol::SERVICE_SUCCESS);
  const std::string uid =
      registerRsp.at("data").at("uid").get<std::string>();

  client->send_json(
      json{{"type", static_cast<int>(Protocol::LoginRequestType::LOGIN)},
           {"uid", uid}});
  ASSERT_EQ(client->read_json().at("code"), Protocol::SERVICE_SUCCESS);

  client->send_json(
      json{{"type", static_cast<int>(Protocol::LoginRequestType::LOGOUT)},
           {"uid", uid}});
  ASSERT_EQ(client->read_json().at("code"), Protocol::SERVICE_SUCCESS);

  client->send_json(
      json{{"type", static_cast<int>(Protocol::LoginRequestType::LOGIN)},
           {"uid", uid}});
  const json again = client->read_json();
  EXPECT_EQ(again.at("code"), Protocol::SERVICE_SUCCESS);
  EXPECT_EQ(again.at("data").at("playerData").at("basicInfo").at("uid"), uid);
}

} // namespace
