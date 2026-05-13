#include <string>

#include <gtest/gtest.h>

#include "integration_helpers.h"
#include "protocol.h"

namespace {

using json = nlohmann::json;

TEST(HomeNetworkTest, HomeServer_CreateRoomListRoomsAndJoin) {
  network_tests::MultiServiceHarness harness;
  const std::string alice =
      network_tests::auth_register_login(harness);
  const std::string bob =
      network_tests::auth_register_login(harness);

  const int roomId =
      network_tests::lobby_create_room(harness, alice, 2);

  auto listClient = harness.make_lobby_client();
  listClient->send_json(
      json{{"type", static_cast<int>(Protocol::HomeRequestType::LIST_ROOMS)}});
  const json listRsp = listClient->read_json();
  ASSERT_EQ(listRsp.at("code"), Protocol::SERVICE_SUCCESS);

  const auto &rooms = listRsp.at("data").at("roomInfos");
  ASSERT_TRUE(rooms.is_array());
  bool found = false;
  for (const auto &entry : rooms) {
    if (entry.at("roomId").get<int>() == roomId) {
      found = true;
      EXPECT_EQ(entry.at("maximumPeople").get<std::size_t>(), 2U);
    }
  }
  EXPECT_TRUE(found);

  network_tests::lobby_join_room(harness, roomId, bob);

  // Second join attempt should fail (room state error: already in room).
  auto joinVerifier = harness.make_lobby_client();
  joinVerifier->send_json(json{{"type",
                                 static_cast<int>(Protocol::HomeRequestType::JOIN_ROOM)},
                                {"roomId", roomId},
                                {"uid", bob}});
  const json joinAgain = joinVerifier->read_json();
  EXPECT_EQ(joinAgain.at("code"),
            (Protocol::SERVICE_FAIL | Protocol::ROOM_STATE_ERROR));
}

TEST(HomeNetworkTest, HomeServer_JoinRoomReturnsRoomWithTwoMembers) {
  network_tests::MultiServiceHarness harness;
  const std::string alice =
      network_tests::auth_register_login(harness);
  const std::string bob =
      network_tests::auth_register_login(harness);

  const int roomId =
      network_tests::lobby_create_room(harness, alice, 2);

  auto bobClient = harness.make_lobby_client();
  bobClient->send_json(json{{"type",
                               static_cast<int>(Protocol::HomeRequestType::JOIN_ROOM)},
                              {"roomId", roomId},
                              {"uid", bob}});
  const json joinRsp = bobClient->read_json();
  ASSERT_EQ(joinRsp.at("code"), Protocol::SERVICE_SUCCESS);

  const auto &infos =
      joinRsp.at("data").at("roomInfo").at("basicInfos");
  ASSERT_EQ(infos.size(), 2U);
}

TEST(HomeNetworkTest, JoinNonexistentRoomReturnsNotFound) {
  network_tests::MultiServiceHarness harness;
  const std::string uid = network_tests::auth_register_login(harness);

  auto client = harness.make_lobby_client();
  client->send_json(
      json{{"type", static_cast<int>(Protocol::HomeRequestType::JOIN_ROOM)},
           {"roomId", 9'999'999},
           {"uid", uid}});
  const json rsp = client->read_json();
  EXPECT_EQ(rsp.at("code"), (Protocol::SERVICE_FAIL | Protocol::NOT_FOUND));
}

TEST(HomeNetworkTest, JoinWhenRoomFullReturnsRoomStateError) {
  network_tests::MultiServiceHarness harness;
  const std::string alice = network_tests::auth_register_login(harness);
  const std::string bob = network_tests::auth_register_login(harness);

  const int roomId = network_tests::lobby_create_room(harness, alice, 1);

  auto bobClient = harness.make_lobby_client();
  bobClient->send_json(
      json{{"type", static_cast<int>(Protocol::HomeRequestType::JOIN_ROOM)},
           {"roomId", roomId},
           {"uid", bob}});
  const json rsp = bobClient->read_json();
  EXPECT_EQ(rsp.at("code"),
            (Protocol::SERVICE_FAIL | Protocol::ROOM_STATE_ERROR));
}

TEST(HomeNetworkTest, CreateRoomWhenNotLoggedInReturnsNotFound) {
  network_tests::MultiServiceHarness harness;
  const std::string uid = network_tests::auth_register_only(harness);

  auto client = harness.make_lobby_client();
  client->send_json(
      json{{"type", static_cast<int>(Protocol::HomeRequestType::CREATE_ROOM)},
           {"uid", uid},
           {"maximumPeople", 2}});
  const json rsp = client->read_json();
  EXPECT_EQ(rsp.at("code"), (Protocol::SERVICE_FAIL | Protocol::NOT_FOUND));
}

TEST(HomeNetworkTest, CreateRoomWhenAlreadyInRoomReturnsRoomStateError) {
  network_tests::MultiServiceHarness harness;
  const std::string alice = network_tests::auth_register_login(harness);
  network_tests::lobby_create_room(harness, alice, 2);

  auto client = harness.make_lobby_client();
  client->send_json(
      json{{"type", static_cast<int>(Protocol::HomeRequestType::CREATE_ROOM)},
           {"uid", alice},
           {"maximumPeople", 2}});
  const json rsp = client->read_json();
  EXPECT_EQ(rsp.at("code"),
            (Protocol::SERVICE_FAIL | Protocol::ROOM_STATE_ERROR));
}

TEST(HomeNetworkTest, LeaveRoomLastMemberRemovesRoomJoinReturnsNotFound) {
  network_tests::MultiServiceHarness harness;
  const std::string alice = network_tests::auth_register_login(harness);
  const std::string bob = network_tests::auth_register_login(harness);
  const int roomId = network_tests::lobby_create_room(harness, alice, 2);

  network_tests::lobby_leave_room(harness, alice);

  auto bobClient = harness.make_lobby_client();
  bobClient->send_json(
      json{{"type", static_cast<int>(Protocol::HomeRequestType::JOIN_ROOM)},
           {"roomId", roomId},
           {"uid", bob}});
  const json rsp = bobClient->read_json();
  EXPECT_EQ(rsp.at("code"), (Protocol::SERVICE_FAIL | Protocol::NOT_FOUND));
}

TEST(HomeNetworkTest, LogoutThenCreateRoomReturnsNotFound) {
  network_tests::MultiServiceHarness harness;
  const std::string uid = network_tests::auth_register_login(harness);
  network_tests::auth_logout(harness, uid);

  auto client = harness.make_lobby_client();
  client->send_json(
      json{{"type", static_cast<int>(Protocol::HomeRequestType::CREATE_ROOM)},
           {"uid", uid},
           {"maximumPeople", 2}});
  const json rsp = client->read_json();
  EXPECT_EQ(rsp.at("code"), (Protocol::SERVICE_FAIL | Protocol::NOT_FOUND));
}

TEST(HomeNetworkTest, SetReadyWhenNotInRoomReturnsRoomStateError) {
  network_tests::MultiServiceHarness harness;
  const std::string uid = network_tests::auth_register_login(harness);

  auto client = harness.make_lobby_client();
  client->send_json(
      json{{"type", static_cast<int>(Protocol::HomeRequestType::SET_READY)},
           {"uid", uid},
           {"ready", true}});
  const json rsp = client->read_json();
  EXPECT_EQ(rsp.at("code"),
            (Protocol::SERVICE_FAIL | Protocol::ROOM_STATE_ERROR));
}

TEST(HomeNetworkTest, EditProfileWhenNotLoggedInReturnsNotFound) {
  network_tests::MultiServiceHarness harness;
  const std::string uid = network_tests::auth_register_only(harness);

  auto client = harness.make_lobby_client();
  client->send_json(json{
      {"type", static_cast<int>(Protocol::HomeRequestType::EDIT_PROFILE)},
      {"uid", uid},
      {"basicInfo",
       json{{"uid", uid}, {"name", "ghost"}, {"color", 1}}}});
  const json rsp = client->read_json();
  EXPECT_EQ(rsp.at("code"), (Protocol::SERVICE_FAIL | Protocol::NOT_FOUND));
}

} // namespace

