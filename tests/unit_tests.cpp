#include <memory>
#include <string>

#include <gtest/gtest.h>

#include <asio/io_context.hpp>

#include "protocol.h"
#include "room.h"
#include "server.h"
#include "user.h"

namespace {

class ServerChannelBehaviorTest : public ::testing::Test {
protected:
  static void SetUpTestSuite() {
    ioContext = std::make_unique<asio::io_context>();
    server = std::make_shared<Server>(*ioContext, 0);
  }

  static void TearDownTestSuite() {
    server.reset();
    ioContext.reset();
  }

  static std::unique_ptr<asio::io_context> ioContext;
  static std::shared_ptr<Server> server;
};

std::unique_ptr<asio::io_context> ServerChannelBehaviorTest::ioContext;
std::shared_ptr<Server> ServerChannelBehaviorTest::server;

TEST(ProtocolTest, CommandTypeMapping) {
  using Protocol::HomeRequestType;
  using Protocol::LoginRequestType;

  EXPECT_EQ(static_cast<int>(LoginRequestType::LOGIN), 0);
  EXPECT_EQ(static_cast<int>(LoginRequestType::REGISTER), 1);
  EXPECT_EQ(static_cast<int>(LoginRequestType::ERROR), 100);

  EXPECT_EQ(static_cast<int>(HomeRequestType::CREATE_ROOM), 0);
  EXPECT_EQ(static_cast<int>(HomeRequestType::JOIN_ROOM), 1);
  EXPECT_EQ(static_cast<int>(HomeRequestType::LEAVE_ROOM), 2);
  EXPECT_EQ(static_cast<int>(HomeRequestType::LIST_ROOMS), 3);
  EXPECT_EQ(static_cast<int>(HomeRequestType::SEND_MESSAGE), 4);
  EXPECT_EQ(static_cast<int>(HomeRequestType::HEARTBEAT), 5);
  EXPECT_EQ(static_cast<int>(HomeRequestType::EDIT_PROFILE), 6);
  EXPECT_EQ(static_cast<int>(HomeRequestType::ERROR), 100);
}

TEST(ProtocolTest, EnvelopeJsonRoundTrip) {
  Protocol::Envelope env;
  env.code = Protocol::SERVICE_SUCCESS;
  env.message = "ok";
  env.data = json{{"roomId", 42}, {"roomName", "lobby"}};

  json j = env;
  EXPECT_EQ(j.at("code"), Protocol::SERVICE_SUCCESS);
  EXPECT_EQ(j.at("message"), "ok");
  EXPECT_EQ(j.at("data").at("roomId"), 42);

  auto parsed = j.get<Protocol::Envelope>();
  EXPECT_EQ(parsed.code, Protocol::SERVICE_SUCCESS);
  EXPECT_EQ(parsed.message, "ok");
  EXPECT_EQ(parsed.data.at("roomName"), "lobby");
}

TEST(ProtocolTest, RequestResponseJsonRoundTrip) {
  Protocol::CreateRoomReq req;
  req.type = Protocol::HomeRequestType::CREATE_ROOM;
  req.uid = "1001";
  req.maximumPeople = 6;

  json reqJson = req;
  auto reqParsed = reqJson.get<Protocol::CreateRoomReq>();
  EXPECT_EQ(reqParsed.uid, "1001");
  EXPECT_EQ(reqParsed.maximumPeople, 6);

  Protocol::PlayerBasicInfo p1{"1001", "alice", 1};
  Protocol::PlayerBasicInfo p2{"1002", "bob", 2};
  Protocol::JoinRoomRsp rsp;
  rsp.roomInfo = Protocol::RoomInfo{.roomId = 42,
                                    .maximumPeople = 6,
                                    .basicInfos = {{p1.uid, p1}, {p2.uid, p2}}};
  json rspJson = rsp;

  auto parsedRsp = rspJson.get<Protocol::JoinRoomRsp>();
  EXPECT_EQ(parsedRsp.roomInfo.roomId, 42);
  EXPECT_EQ(parsedRsp.roomInfo.maximumPeople, 6U);
  ASSERT_EQ(parsedRsp.roomInfo.basicInfos.size(), 2U);
  ASSERT_TRUE(parsedRsp.roomInfo.basicInfos.contains("1001"));
  ASSERT_TRUE(parsedRsp.roomInfo.basicInfos.contains("1002"));
  EXPECT_EQ(parsedRsp.roomInfo.basicInfos.at("1001").uid, "1001");
  EXPECT_EQ(parsedRsp.roomInfo.basicInfos.at("1002").name, "bob");

  Protocol::EditProfileReq editReq;
  editReq.type = Protocol::HomeRequestType::EDIT_PROFILE;
  editReq.playerData.basicInfo = Protocol::PlayerBasicInfo{"1001", "alice", 7};

  json editJson = editReq;
  EXPECT_EQ(editJson.at("playerData").at("basicInfo").at("uid"), "1001");
  EXPECT_EQ(editJson.at("playerData").at("basicInfo").at("name"), "alice");
  EXPECT_EQ(editJson.at("playerData").at("basicInfo").at("color"), 7);

  auto parsedEdit = editJson.get<Protocol::EditProfileReq>();
  EXPECT_EQ(parsedEdit.playerData.basicInfo.uid, "1001");
  EXPECT_EQ(parsedEdit.playerData.basicInfo.name, "alice");
  EXPECT_EQ(parsedEdit.playerData.basicInfo.color, 7);
}

TEST(RoomTest, BasicBehavior) {
  Protocol::PlayerBasicInfo creatorInfo{"1", "creator", 1};
  auto creator = std::make_shared<User>(creatorInfo);
  Room room(10, 2, creator);

  EXPECT_EQ(room.get_id(), 10);
  EXPECT_EQ(room.get_maximum_people(), 2);
  EXPECT_EQ(room.get_people_count(), 1);
  EXPECT_TRUE(room.is_member("1"));

  Protocol::PlayerBasicInfo info2{"2", "u2", 2};
  Protocol::PlayerBasicInfo info3{"3", "u3", 3};
  auto user2 = std::make_shared<User>(info2);
  auto user3 = std::make_shared<User>(info3);

  EXPECT_TRUE(room.add_member(user2));
  EXPECT_EQ(room.get_people_count(), 2);
  EXPECT_TRUE(room.is_member("2"));

  // Capacity reached.
  EXPECT_FALSE(room.add_member(user3));
  EXPECT_EQ(room.get_people_count(), 2);

  EXPECT_TRUE(room.remove_member("2"));
  EXPECT_EQ(room.get_people_count(), 1);
  EXPECT_FALSE(room.is_member("2"));

  // Removing non-existent member should return false.
  EXPECT_FALSE(room.remove_member("404"));
}

TEST_F(ServerChannelBehaviorTest, ServerRegisterLoginAndRoomLifecycle) {
  Protocol::RegisterRsp aliceRegRsp;
  ASSERT_EQ(server->register_user(
                Protocol::RegisterReq{
                    .type = Protocol::LoginRequestType::REGISTER,
                },
                aliceRegRsp),
            Protocol::SERVICE_SUCCESS);
  const auto aliceUid = aliceRegRsp.uid;
  ASSERT_FALSE(aliceUid.empty());
  EXPECT_TRUE(server->user_exists(aliceUid));

  Protocol::LoginRsp aliceLoginRsp;
  ASSERT_EQ(server->login_user(
                Protocol::LoginReq{.type = Protocol::LoginRequestType::LOGIN,
                                   .uid = aliceUid},
                aliceLoginRsp),
            Protocol::SERVICE_SUCCESS);
  EXPECT_EQ(aliceLoginRsp.playerData.basicInfo.uid, aliceUid);

  Protocol::CreateRoomRsp createRsp;
  ASSERT_EQ(server->create_room(
                Protocol::CreateRoomReq{
                    .type = Protocol::HomeRequestType::CREATE_ROOM,
                    .uid = aliceUid,
                    .maximumPeople = 2,
                },
                createRsp),
            Protocol::SERVICE_SUCCESS);
  const auto roomId = createRsp.roomId;
  EXPECT_GT(roomId, 0);

  Protocol::RegisterRsp bobRegRsp;
  ASSERT_EQ(server->register_user(
                Protocol::RegisterReq{
                    .type = Protocol::LoginRequestType::REGISTER,
                },
                bobRegRsp),
            Protocol::SERVICE_SUCCESS);
  const auto bobUid = bobRegRsp.uid;
  ASSERT_FALSE(bobUid.empty());

  Protocol::JoinRoomRsp joinRsp;
  ASSERT_EQ(
      server->join_room(
          Protocol::JoinRoomReq{.type = Protocol::HomeRequestType::JOIN_ROOM,
                                .roomId = roomId,
                                .uid = bobUid},
          joinRsp),
      Protocol::SERVICE_SUCCESS);
  EXPECT_EQ(joinRsp.roomInfo.roomId, roomId);
  EXPECT_EQ(joinRsp.roomInfo.maximumPeople, 2U);
  EXPECT_EQ(joinRsp.roomInfo.basicInfos.size(), 2U);
  EXPECT_TRUE(joinRsp.roomInfo.basicInfos.contains(aliceUid));
  EXPECT_TRUE(joinRsp.roomInfo.basicInfos.contains(bobUid));

  Protocol::ListRoomsRsp listRsp;
  ASSERT_EQ(
      server->list_rooms(
          Protocol::ListRoomsReq{.type = Protocol::HomeRequestType::LIST_ROOMS},
          listRsp),
      Protocol::SERVICE_SUCCESS);
  auto rooms = listRsp.roomInfos;
  ASSERT_FALSE(rooms.empty());
  bool found = false;
  for (const auto &r : rooms) {
    if (r.roomId == roomId) {
      found = true;
      EXPECT_EQ(r.basicInfos.size(), 2U);
      EXPECT_EQ(r.maximumPeople, 2U);
    }
  }
  EXPECT_TRUE(found);

  Protocol::EmptyRsp leaveRsp;
  EXPECT_EQ(
      server->leave_room(
          Protocol::LeaveRoomReq{.type = Protocol::HomeRequestType::LEAVE_ROOM,
                                 .uid = bobUid},
          leaveRsp),
      Protocol::SERVICE_SUCCESS);
  EXPECT_EQ(
      server->leave_room(
          Protocol::LeaveRoomReq{.type = Protocol::HomeRequestType::LEAVE_ROOM,
                                 .uid = aliceUid},
          leaveRsp),
      Protocol::SERVICE_SUCCESS);

  Protocol::ListRoomsRsp finalListRsp;
  ASSERT_EQ(
      server->list_rooms(
          Protocol::ListRoomsReq{.type = Protocol::HomeRequestType::LIST_ROOMS},
          finalListRsp),
      Protocol::SERVICE_SUCCESS);
  auto finalRooms = finalListRsp.roomInfos;
  EXPECT_TRUE(finalRooms.empty());
}

TEST(ServerDispatchTest, DerivedServerDispatchRouting) {
  asio::io_context ioContext;
  auto state = std::make_shared<ServerState>();
  auto loginServer = std::make_shared<LoginServer>(ioContext, 0, state);
  auto homeServer = std::make_shared<HomeServer>(ioContext, 0, state);

  auto registerEnv = loginServer->dispatch_request(json(Protocol::LoginReq{
      .type = Protocol::LoginRequestType::REGISTER, .uid = ""}));
  ASSERT_EQ(registerEnv.code, Protocol::SERVICE_SUCCESS);
  const auto uid = registerEnv.data.get<Protocol::RegisterRsp>().uid;
  ASSERT_FALSE(uid.empty());

  auto badOnLoginServer = loginServer->dispatch_request(json(
      Protocol::ListRoomsReq{.type = Protocol::HomeRequestType::LIST_ROOMS}));
  EXPECT_EQ(badOnLoginServer.code,
            (Protocol::SERVICE_FAIL | Protocol::BAD_REQUEST));

  auto listOnHomeServer = homeServer->dispatch_request(json(
      Protocol::ListRoomsReq{.type = Protocol::HomeRequestType::LIST_ROOMS}));
  EXPECT_EQ(listOnHomeServer.code, Protocol::SERVICE_SUCCESS);

  auto badOnHomeServer = homeServer->dispatch_request(
      json{{"type", static_cast<int>(Protocol::LoginRequestType::ERROR)},
           {"uid", uid}});
  EXPECT_EQ(badOnHomeServer.code,
            (Protocol::SERVICE_FAIL | Protocol::BAD_REQUEST));
}

TEST_F(ServerChannelBehaviorTest, LoginAfterLogoutUsesStoredProfile) {
  Protocol::RegisterRsp registerRsp;
  ASSERT_EQ(server->register_user(
                Protocol::RegisterReq{
                    .type = Protocol::LoginRequestType::REGISTER,
                },
                registerRsp),
            Protocol::SERVICE_SUCCESS);
  const std::string uid = registerRsp.uid;
  ASSERT_FALSE(uid.empty());
  server->logout_user(uid);

  EXPECT_TRUE(server->user_exists(uid));
  EXPECT_EQ(server->get_user(uid), nullptr);

  Protocol::LoginRsp relogged;
  ASSERT_EQ(server->login_user(
                Protocol::LoginReq{.type = Protocol::LoginRequestType::LOGIN,
                                   .uid = uid},
                relogged),
            Protocol::SERVICE_SUCCESS);
  EXPECT_EQ(relogged.playerData.basicInfo.uid, uid);
  EXPECT_EQ(relogged.playerData.basicInfo.name, "");
}

} // namespace
