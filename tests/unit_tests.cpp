#include <memory>
#include <string>

#include <gtest/gtest.h>

#include <asio/io_context.hpp>

#include "channel.h"
#include "protocol.h"
#include "room.h"
#include "server.h"
#include "user.h"

namespace {

class TestChannel : public Channel {
public:
  TestChannel(asio::io_context &context, std::shared_ptr<Server> server)
      : Channel(context, std::move(server)) {}

  using Channel::handle_create_room;
  using Channel::handle_join_room;
  using Channel::handle_leave_room;
  using Channel::handle_list_rooms;
  using Channel::handle_login;
  using Channel::handle_register;
  using Channel::make_err_env;
  using Channel::make_ok_env;
};

class ServerChannelBehaviorTest : public ::testing::Test {
protected:
  static void SetUpTestSuite() {
    ioContext = std::make_unique<asio::io_context>();
    server = std::make_shared<Server>(*ioContext, 7777);
  }

  static void TearDownTestSuite() {
    server.reset();
    ioContext.reset();
  }

  void SetUp() override {
    ch1 = std::make_shared<TestChannel>(*ioContext, server);
    ch2 = std::make_shared<TestChannel>(*ioContext, server);
  }

  static std::unique_ptr<asio::io_context> ioContext;
  static std::shared_ptr<Server> server;
  std::shared_ptr<TestChannel> ch1;
  std::shared_ptr<TestChannel> ch2;
};

std::unique_ptr<asio::io_context> ServerChannelBehaviorTest::ioContext;
std::shared_ptr<Server> ServerChannelBehaviorTest::server;

TEST(ProtocolTest, CommandTypeMapping) {
  using Protocol::CommandType;

  EXPECT_EQ(static_cast<int>(CommandType::LOGIN), 0);
  EXPECT_EQ(static_cast<int>(CommandType::CREATE_ROOM), 1);
  EXPECT_EQ(static_cast<int>(CommandType::JOIN_ROOM), 2);
  EXPECT_EQ(static_cast<int>(CommandType::LEAVE_ROOM), 3);
  EXPECT_EQ(static_cast<int>(CommandType::LIST_ROOMS), 4);
  EXPECT_EQ(static_cast<int>(CommandType::SEND_MESSAGE), 5);
  EXPECT_EQ(static_cast<int>(CommandType::ERROR), 100);
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
  req.uid = "1001";
  req.maximumPeople = 6;

  json reqJson = req;
  auto reqParsed = reqJson.get<Protocol::CreateRoomReq>();
  EXPECT_EQ(reqParsed.uid, "1001");
  EXPECT_EQ(reqParsed.maximumPeople, 6);

  Protocol::PlayerBasicInfo p1{"1001", "alice", 1};
  Protocol::PlayerBasicInfo p2{"1002", "bob", 2};
  Protocol::JoinRoomRsp rsp;
  rsp.PlayerInfos = {p1, p2};
  json rspJson = rsp;

  auto parsedRsp = rspJson.get<Protocol::JoinRoomRsp>();
  ASSERT_EQ(parsedRsp.PlayerInfos.size(), 2);
  EXPECT_EQ(parsedRsp.PlayerInfos[0].uid, "1001");
  EXPECT_EQ(parsedRsp.PlayerInfos[1].userName, "bob");
}

TEST(RoomTest, BasicBehavior) {
  auto creator = std::make_shared<User>("1", "creator", 1);
  Room room(10, 2, creator);

  EXPECT_EQ(room.get_id(), 10);
  EXPECT_EQ(room.get_creator()->get_uid(), "1");
  EXPECT_EQ(room.get_maximum_people(), 2);
  EXPECT_EQ(room.get_people_count(), 1);
  EXPECT_TRUE(room.is_member("1"));

  auto user2 = std::make_shared<User>("2", "u2", 2);
  auto user3 = std::make_shared<User>("3", "u3", 3);

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
  auto alice = server->register_user();
  ASSERT_NE(alice, nullptr);
  EXPECT_TRUE(server->user_exists(alice->get_uid()));

  auto loggedIn = server->login_user(alice->get_uid());
  ASSERT_NE(loggedIn, nullptr);
  EXPECT_EQ(loggedIn->get_uid(), alice->get_uid());

  auto room = server->create_room(2, alice);
  ASSERT_NE(room, nullptr);
  EXPECT_EQ(alice->get_room_id(), room->get_id());

  auto bob = server->register_user();
  ASSERT_NE(bob, nullptr);
  EXPECT_TRUE(server->join_room(room, bob));
  EXPECT_EQ(bob->get_room_id(), room->get_id());

  std::vector<Protocol::RoomInfo> rooms;
  server->list_rooms(rooms);
  ASSERT_FALSE(rooms.empty());
  bool found = false;
  for (const auto &r : rooms) {
    if (r.roomId == room->get_id()) {
      found = true;
      EXPECT_EQ(r.peopleCount, 2U);
      EXPECT_EQ(r.maximumPeople, 2U);
    }
  }
  EXPECT_TRUE(found);

  EXPECT_TRUE(server->leave_room(room->get_id(), bob->get_uid()));
  EXPECT_EQ(bob->get_room_id(), -1);
  EXPECT_TRUE(server->leave_room(room->get_id(), alice->get_uid()));
  EXPECT_EQ(server->get_room(room->get_id()), nullptr);
}

TEST_F(ServerChannelBehaviorTest, ChannelParsesTypeAndBuildsEnvelope) {
  auto ok =
      TestChannel::make_ok_env(Protocol::SERVICE_SUCCESS, json{{"count", 1}});
  EXPECT_EQ(ok.code, Protocol::SERVICE_SUCCESS);
  EXPECT_EQ(ok.data.at("count"), 1);

  auto err = TestChannel::make_err_env(
      Protocol::SERVICE_FAIL | Protocol::NOT_FOUND, "uid not exists");
  EXPECT_EQ(err.code, (Protocol::SERVICE_FAIL | Protocol::NOT_FOUND));
  EXPECT_EQ(err.message, "uid not exists");
}

TEST_F(ServerChannelBehaviorTest, ChannelHandlerFlow) {
  Protocol::LoginReq registerReq;
  registerReq.type = Protocol::CommandType::LOGIN;
  auto registerEnv = ch1->handle_register(json(registerReq));
  ASSERT_EQ(registerEnv.code, Protocol::SERVICE_SUCCESS);
  EXPECT_TRUE(registerEnv.data.is_object());
  EXPECT_TRUE(registerEnv.data.empty());

  auto alice = server->register_user();
  ASSERT_NE(alice, nullptr);

  Protocol::LoginReq badLoginReq;
  badLoginReq.type = Protocol::CommandType::LOGIN;
  badLoginReq.uid = "non-existent-uid";
  auto badLoginEnv = ch2->handle_login(json(badLoginReq));
  EXPECT_EQ(badLoginEnv.code, (Protocol::SERVICE_FAIL | Protocol::NOT_FOUND));

  Protocol::LoginReq loginReq;
  loginReq.type = Protocol::CommandType::LOGIN;
  loginReq.uid = alice->get_uid();
  auto loginEnv = ch1->handle_login(json(loginReq));
  ASSERT_EQ(loginEnv.code, Protocol::SERVICE_SUCCESS);
  auto loginRsp = loginEnv.data.get<Protocol::LoginRsp>();
  EXPECT_EQ(loginRsp.basicInfo.uid, alice->get_uid());

  Protocol::CreateRoomReq createReq;
  createReq.type = Protocol::CommandType::CREATE_ROOM;
  createReq.uid = alice->get_uid();
  createReq.maximumPeople = 2;
  auto createEnv = ch1->handle_create_room(json(createReq));
  ASSERT_EQ(createEnv.code, Protocol::SERVICE_SUCCESS);
  auto createRsp = createEnv.data.get<Protocol::CreateRoomRsp>();
  EXPECT_GT(createRsp.roomId, 0);

  auto bob = server->register_user();
  ASSERT_NE(bob, nullptr);

  Protocol::JoinRoomReq joinReq;
  joinReq.type = Protocol::CommandType::JOIN_ROOM;
  joinReq.roomId = createRsp.roomId;
  joinReq.uid = bob->get_uid();
  auto joinEnv = ch2->handle_join_room(json(joinReq));
  ASSERT_EQ(joinEnv.code, Protocol::SERVICE_SUCCESS);
  auto joinRsp = joinEnv.data.get<Protocol::JoinRoomRsp>();
  EXPECT_EQ(joinRsp.PlayerInfos.size(), 2U);

  auto listEnv = ch1->handle_list_rooms(json::object());
  ASSERT_EQ(listEnv.code, Protocol::SERVICE_SUCCESS);
  auto listRsp = listEnv.data.get<Protocol::ListRoomsRsp>();
  bool foundRoom = false;
  for (const auto &roomInfo : listRsp.RoomInfos) {
    if (roomInfo.roomId == createRsp.roomId) {
      foundRoom = true;
      EXPECT_EQ(roomInfo.peopleCount, 2U);
    }
  }
  EXPECT_TRUE(foundRoom);

  Protocol::LeaveRoomReq leaveReq2;
  leaveReq2.type = Protocol::CommandType::LEAVE_ROOM;
  leaveReq2.uid = bob->get_uid();
  auto leaveEnv2 = ch2->handle_leave_room(json(leaveReq2));
  EXPECT_EQ(leaveEnv2.code, Protocol::SERVICE_SUCCESS);

  Protocol::LeaveRoomReq leaveReq1;
  leaveReq1.type = Protocol::CommandType::LEAVE_ROOM;
  leaveReq1.uid = alice->get_uid();
  auto leaveEnv1 = ch1->handle_leave_room(json(leaveReq1));
  EXPECT_EQ(leaveEnv1.code, Protocol::SERVICE_SUCCESS);

  server->logout_user(bob->get_uid());
  server->logout_user(alice->get_uid());
}

TEST_F(ServerChannelBehaviorTest, LoginAfterLogoutUsesStoredProfile) {
  auto registered = server->register_user();
  ASSERT_NE(registered, nullptr);

  const std::string uid = registered->get_uid();
  server->logout_user(uid);

  EXPECT_TRUE(server->user_exists(uid));
  EXPECT_EQ(server->get_user(uid), nullptr);

  auto relogged = server->login_user(uid);
  ASSERT_NE(relogged, nullptr);
  EXPECT_EQ(relogged->get_uid(), uid);
  EXPECT_EQ(relogged->get_username(), "");
}

} // namespace
