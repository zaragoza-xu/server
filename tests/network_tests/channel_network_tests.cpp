#include <string>

#include <gtest/gtest.h>

#include "network_test_framework.h"
#include "protocol.h"
#include "server.h"

namespace {

using json = nlohmann::json;

TEST(ChannelNetworkTest, RequestLineMayUseCrLf) {
  network_tests::ServerHarness<LoginServer> harness;
  auto client = harness.make_client();

  const json req{{"type",
                  static_cast<int>(Protocol::LoginRequestType::REGISTER)}};
  client->send_raw(req.dump() + "\r\n");

  const json rsp = client->read_json();
  ASSERT_EQ(rsp.at("code"), Protocol::SERVICE_SUCCESS);
  ASSERT_TRUE(rsp.at("data").contains("uid"));
}

TEST(ChannelNetworkTest, MultipleFramesInOneWriteAreProcessedInOrder) {
  network_tests::ServerHarness<LoginServer> harness;
  auto client = harness.make_client();

  const json req{{"type",
                  static_cast<int>(Protocol::LoginRequestType::REGISTER)}};
  const std::string batch = req.dump() + "\n" + req.dump() + "\n";
  client->send_raw(batch);

  const json first = client->read_json();
  const json second = client->read_json();
  ASSERT_EQ(first.at("code"), Protocol::SERVICE_SUCCESS);
  ASSERT_EQ(second.at("code"), Protocol::SERVICE_SUCCESS);
  const std::string uid1 = first.at("data").at("uid").get<std::string>();
  const std::string uid2 = second.at("data").at("uid").get<std::string>();
  EXPECT_FALSE(uid1.empty());
  EXPECT_FALSE(uid2.empty());
  EXPECT_NE(uid1, uid2);
}

TEST(ChannelNetworkTest, LeadingEmptyLinesAreSkipped) {
  network_tests::ServerHarness<LoginServer> harness;
  auto client = harness.make_client();

  client->send_raw("\n\n");
  client->send_json(json{{"type",
                          static_cast<int>(Protocol::LoginRequestType::REGISTER)}});

  const json rsp = client->read_json();
  ASSERT_EQ(rsp.at("code"), Protocol::SERVICE_SUCCESS);
  ASSERT_TRUE(rsp.at("data").contains("uid"));
}

TEST(ChannelNetworkTest, OversizedFrameBodyClosesConnection) {
  network_tests::ServerHarness<LoginServer> harness;
  auto client = harness.make_client();

  std::string huge(static_cast<std::size_t>(Protocol::MAX_MESSAGE_SIZE + 1),
                   'a');
  huge.push_back('\n');
  client->send_raw(huge);

  EXPECT_ANY_THROW(client->read_frame());
}

TEST(ChannelNetworkTest, PendingWithoutDelimiterTooLargeClosesConnection) {
  network_tests::ServerHarness<LoginServer> harness;
  auto client = harness.make_client();

  const std::string chunk(static_cast<std::size_t>(Protocol::MAX_MESSAGE_SIZE + 2),
                          'b');
  client->send_raw(chunk);

  EXPECT_ANY_THROW(client->read_frame());
}

} // namespace
