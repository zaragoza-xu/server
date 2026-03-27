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
  using Channel::parse_command_type;
};

class ServerChannelBehaviorTest : public ::testing::Test {
protected:
  static void SetUpTestSuite() {
    ioContext = std::make_unique<asio::io_context>();
    server = std::make_shared<Server>(*ioContext);
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

  EXPECT_EQ(Protocol::command_type_from_string("register"), CommandType::REGISTER);
  EXPECT_EQ(Protocol::command_type_from_string("login"), CommandType::LOGIN);
  EXPECT_EQ(Protocol::command_type_from_string("create_room"), CommandType::CREATE_ROOM);
  EXPECT_EQ(Protocol::command_type_from_string("join_room"), CommandType::JOIN_ROOM);
  EXPECT_EQ(Protocol::command_type_from_string("leave_room"), CommandType::LEAVE_ROOM);
  EXPECT_EQ(Protocol::command_type_from_string("list_rooms"), CommandType::LIST_ROOMS);
  EXPECT_EQ(Protocol::command_type_from_string("send_message"), CommandType::SEND_MESSAGE);
  EXPECT_EQ(Protocol::command_type_from_string("unknown"), CommandType::ERROR);

  EXPECT_EQ(Protocol::command_type_to_string(CommandType::REGISTER), "register");
  EXPECT_EQ(Protocol::command_type_to_string(CommandType::ERROR), "error");
}

TEST(ProtocolTest, EnvelopeJsonRoundTrip) {
  Protocol::Envelope env;
  env.type = Protocol::CommandType::CREATE_ROOM;
  env.status = true;
  env.errorCode = 0;
  env.message = "ok";
  env.data = json{{"roomId", 42}, {"roomName", "lobby"}};

  json j = env;
  EXPECT_EQ(j.at("type"), "create_room");
  EXPECT_TRUE(j.at("status"));
  EXPECT_EQ(j.at("errorCode"), 0);
  EXPECT_EQ(j.at("message"), "ok");
  EXPECT_EQ(j.at("data").at("roomId"), 42);

  auto parsed = j.get<Protocol::Envelope>();
  EXPECT_EQ(parsed.type, Protocol::CommandType::CREATE_ROOM);
  EXPECT_TRUE(parsed.status);
  EXPECT_EQ(parsed.errorCode, 0);
  EXPECT_EQ(parsed.message, "ok");
  EXPECT_EQ(parsed.data.at("roomName"), "lobby");
}

TEST(ProtocolTest, RequestResponseJsonRoundTrip) {
  Protocol::CreateRoomReq req;
  req.uid = "1001";
  req.roomName = "test-room";
  req.maximumPeople = 6;

  json reqJson = req;
  auto reqParsed = reqJson.get<Protocol::CreateRoomReq>();
  EXPECT_EQ(reqParsed.uid, "1001");
  EXPECT_EQ(reqParsed.roomName, "test-room");
  EXPECT_EQ(reqParsed.maximumPeople, 6);

  Protocol::JoinRoomRsp rsp;
  Protocol::PlayerBasicInfo p1{"1001", "alice", 1};
  Protocol::PlayerBasicInfo p2{"1002", "bob", 2};
  rsp.PlayerBasicInfos.push_back(p1);
  rsp.PlayerBasicInfos.push_back(p2);

  json rspJson = rsp;
  ASSERT_EQ(rspJson.at("PlayerBasicInfos").size(), 2);
  auto rspParsed = rspJson.get<Protocol::JoinRoomRsp>();
  ASSERT_EQ(rspParsed.PlayerBasicInfos.size(), 2);
  EXPECT_EQ(rspParsed.PlayerBasicInfos[0].uid, "1001");
  EXPECT_EQ(rspParsed.PlayerBasicInfos[1].userName, "bob");
}

TEST(RoomTest, BasicBehavior) {
  auto creator = std::make_shared<User>(1, "creator", nullptr, 1);
  Room room(10, "r1", 2, creator);

  EXPECT_EQ(room.get_id(), 10);
  EXPECT_EQ(room.get_name(), "r1");
  EXPECT_EQ(room.get_creator()->get_uid(), "1");
  EXPECT_EQ(room.get_maximum_people(), 2);
  EXPECT_EQ(room.get_member_count(), 1);
  EXPECT_TRUE(room.is_member("1"));

  auto user2 = std::make_shared<User>(2, "u2", nullptr, 2);
  auto user3 = std::make_shared<User>(3, "u3", nullptr, 3);

  EXPECT_TRUE(room.add_member(user2));
  EXPECT_EQ(room.get_member_count(), 2);
  EXPECT_TRUE(room.is_member("2"));

  // Capacity reached.
  EXPECT_FALSE(room.add_member(user3));
  EXPECT_EQ(room.get_member_count(), 2);

  EXPECT_TRUE(room.remove_member("2"));
  EXPECT_EQ(room.get_member_count(), 1);
  EXPECT_FALSE(room.is_member("2"));

  // Removing non-existent member should return false.
  EXPECT_FALSE(room.remove_member("404"));
}

TEST_F(ServerChannelBehaviorTest, ServerRegisterLoginAndRoomLifecycle) {
  Protocol::PlayerBasicInfo aliceInfo{"", "alice-server", 1};
  auto alice = server->register_user(aliceInfo, ch1);
  ASSERT_NE(alice, nullptr);
  EXPECT_EQ(ch1->get_user()->get_uid(), alice->get_uid());
  EXPECT_TRUE(server->user_exists(alice->get_uid()));

  auto loggedIn = server->login_user(alice->get_uid(), ch2);
  ASSERT_NE(loggedIn, nullptr);
  EXPECT_EQ(loggedIn->get_uid(), alice->get_uid());

  auto room = server->create_room("room-server", 2, alice);
  ASSERT_NE(room, nullptr);
  EXPECT_EQ(alice->get_room_id(), room->get_id());

  Protocol::PlayerBasicInfo bobInfo{"", "bob-server", 2};
  auto bob = server->register_user(bobInfo, ch2);
  ASSERT_NE(bob, nullptr);
  EXPECT_TRUE(server->join_room(room, bob));
  EXPECT_EQ(bob->get_room_id(), room->get_id());

  auto rooms = server->list_rooms();
  ASSERT_FALSE(rooms.empty());
  bool found = false;
  for (const auto &r : rooms) {
    if (r->get_id() == room->get_id()) {
      found = true;
      EXPECT_EQ(r->get_member_count(), 2U);
    }
  }
  EXPECT_TRUE(found);

  EXPECT_TRUE(server->leave_room(room->get_id(), bob->get_uid()));
  EXPECT_EQ(bob->get_room_id(), -1);
  EXPECT_TRUE(server->leave_room(room->get_id(), alice->get_uid()));
  EXPECT_EQ(server->get_room(room->get_id()), nullptr);

  server->logout_user(bob->get_uid());
  server->logout_user(alice->get_uid());
  EXPECT_FALSE(server->user_exists(bob->get_uid()));
  EXPECT_FALSE(server->user_exists(alice->get_uid()));
}

TEST_F(ServerChannelBehaviorTest, ChannelParsesTypeAndBuildsEnvelope) {
  json j = {{"type", "register"}};
  EXPECT_EQ(TestChannel::parse_command_type(j), Protocol::CommandType::REGISTER);

  json noType = json::object();
  EXPECT_EQ(TestChannel::parse_command_type(noType), Protocol::CommandType::ERROR);

  auto ok = TestChannel::make_ok_env(Protocol::CommandType::LIST_ROOMS,
                                     json{{"count", 1}});
  EXPECT_TRUE(ok.status);
  EXPECT_EQ(ok.errorCode, 0);
  EXPECT_EQ(ok.data.at("count"), 1);

  auto err = TestChannel::make_err_env(Protocol::CommandType::LOGIN, 1001,
                                       "uid not exists");
  EXPECT_FALSE(err.status);
  EXPECT_EQ(err.errorCode, 1001);
  EXPECT_EQ(err.message, "uid not exists");
}

TEST_F(ServerChannelBehaviorTest, ChannelHandlerFlow) {
  Protocol::RegisterReq registerReq;
  registerReq.info.userName = "alice-channel";
  registerReq.info.avatarType = 7;
  auto registerEnv = ch1->handle_register(json(registerReq));
  ASSERT_TRUE(registerEnv.status);
  EXPECT_EQ(registerEnv.type, Protocol::CommandType::REGISTER);

  auto registerRsp = registerEnv.data.get<Protocol::LoginRsp>();
  ASSERT_FALSE(registerRsp.info.uid.empty());

  Protocol::LoginReq badLoginReq;
  badLoginReq.uid = "non-existent-uid";
  auto badLoginEnv = ch2->handle_login(json(badLoginReq));
  EXPECT_FALSE(badLoginEnv.status);
  EXPECT_EQ(badLoginEnv.errorCode, 1001);

  Protocol::CreateRoomReq createReq;
  createReq.uid = registerRsp.info.uid;
  createReq.roomName = "room-channel";
  createReq.maximumPeople = 2;
  auto createEnv = ch1->handle_create_room(json(createReq));
  ASSERT_TRUE(createEnv.status);
  auto createRsp = createEnv.data.get<Protocol::CreateRoomRsp>();
  EXPECT_GT(createRsp.roomId, 0);

  Protocol::RegisterReq registerReq2;
  registerReq2.info.userName = "bob-channel";
  registerReq2.info.avatarType = 9;
  auto registerEnv2 = ch2->handle_register(json(registerReq2));
  ASSERT_TRUE(registerEnv2.status);
  auto registerRsp2 = registerEnv2.data.get<Protocol::LoginRsp>();

  Protocol::JoinRoomReq joinReq;
  joinReq.roomId = createRsp.roomId;
  joinReq.uid = registerRsp2.info.uid;
  auto joinEnv = ch2->handle_join_room(json(joinReq));
  ASSERT_TRUE(joinEnv.status);
  auto joinRsp = joinEnv.data.get<Protocol::JoinRoomRsp>();
  EXPECT_EQ(joinRsp.PlayerBasicInfos.size(), 2U);

  auto listEnv = ch1->handle_list_rooms(json::object());
  ASSERT_TRUE(listEnv.status);
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
  leaveReq2.uid = registerRsp2.info.uid;
  auto leaveEnv2 = ch2->handle_leave_room(json(leaveReq2));
  EXPECT_TRUE(leaveEnv2.status);

  Protocol::LeaveRoomReq leaveReq1;
  leaveReq1.uid = registerRsp.info.uid;
  auto leaveEnv1 = ch1->handle_leave_room(json(leaveReq1));
  EXPECT_TRUE(leaveEnv1.status);

  server->logout_user(registerRsp2.info.uid);
  server->logout_user(registerRsp.info.uid);
}

} // namespace
