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

  using Channel::command_table;
  using Channel::make_env;
  using Channel::on_create_room;
  using Channel::on_heartbeat;
  using Channel::on_join_room;
  using Channel::on_leave_room;
  using Channel::on_list_rooms;
  using Channel::on_login;
  using Channel::on_register;
};

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
  rsp.playerInfos = {p1, p2};
  json rspJson = rsp;

  auto parsedRsp = rspJson.get<Protocol::JoinRoomRsp>();
  ASSERT_EQ(parsedRsp.playerInfos.size(), 2);
  EXPECT_EQ(parsedRsp.playerInfos[0].uid, "1001");
  EXPECT_EQ(parsedRsp.playerInfos[1].userName, "bob");
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
  ASSERT_EQ(server->register_user(aliceRegRsp), Protocol::SERVICE_SUCCESS);
  const auto aliceUid = aliceRegRsp.uid;
  ASSERT_FALSE(aliceUid.empty());
  EXPECT_TRUE(server->user_exists(aliceUid));

  Protocol::LoginRsp aliceLoginRsp;
  ASSERT_EQ(server->login_user(aliceUid, aliceLoginRsp),
            Protocol::SERVICE_SUCCESS);
  EXPECT_EQ(aliceLoginRsp.basicInfo.uid, aliceUid);

  Protocol::CreateRoomRsp createRsp;
  ASSERT_EQ(server->create_room(2, aliceUid, createRsp),
            Protocol::SERVICE_SUCCESS);
  const auto roomId = createRsp.roomId;
  EXPECT_GT(roomId, 0);

  Protocol::RegisterRsp bobRegRsp;
  ASSERT_EQ(server->register_user(bobRegRsp), Protocol::SERVICE_SUCCESS);
  const auto bobUid = bobRegRsp.uid;
  ASSERT_FALSE(bobUid.empty());

  Protocol::JoinRoomRsp joinRsp;
  ASSERT_EQ(server->join_room(roomId, bobUid, joinRsp),
            Protocol::SERVICE_SUCCESS);
  EXPECT_EQ(joinRsp.playerInfos.size(), 2U);

  Protocol::ListRoomsRsp listRsp;
  ASSERT_EQ(server->list_rooms(listRsp), Protocol::SERVICE_SUCCESS);
  auto rooms = listRsp.roomInfos;
  ASSERT_FALSE(rooms.empty());
  bool found = false;
  for (const auto &r : rooms) {
    if (r.roomId == roomId) {
      found = true;
      EXPECT_EQ(r.peopleCount, 2U);
      EXPECT_EQ(r.maximumPeople, 2U);
    }
  }
  EXPECT_TRUE(found);

  EXPECT_EQ(server->leave_room(bobUid), Protocol::SERVICE_SUCCESS);
  EXPECT_EQ(server->leave_room(aliceUid), Protocol::SERVICE_SUCCESS);

  Protocol::ListRoomsRsp finalListRsp;
  ASSERT_EQ(server->list_rooms(finalListRsp), Protocol::SERVICE_SUCCESS);
  auto finalRooms = finalListRsp.roomInfos;
  EXPECT_TRUE(finalRooms.empty());
}

TEST_F(ServerChannelBehaviorTest, ChannelParsesTypeAndBuildsEnvelope) {
  auto ok =
      TestChannel::make_env(Protocol::SERVICE_SUCCESS, json{{"count", 1}});
  EXPECT_EQ(ok.code, Protocol::SERVICE_SUCCESS);
  EXPECT_EQ(ok.data.at("count"), 1);
  EXPECT_EQ(ok.message, "ok");

  auto err =
      TestChannel::make_env(Protocol::SERVICE_FAIL | Protocol::NOT_FOUND);
  EXPECT_EQ(err.code, (Protocol::SERVICE_FAIL | Protocol::NOT_FOUND));
  EXPECT_EQ(err.message, "not found");

  auto unknown = TestChannel::make_env(Protocol::SYSTEM_ERROR);
  EXPECT_EQ(unknown.message, "error");
}

TEST_F(ServerChannelBehaviorTest, ChannelCommandTableCoverage) {
  const auto &table = TestChannel::command_table();
  ASSERT_EQ(table.size(), 6U);

  auto hasType = [&table](Protocol::CommandType type) {
    return std::any_of(table.begin(), table.end(), [type](const auto &entry) {
      return entry.type == type;
    });
  };

  EXPECT_TRUE(hasType(Protocol::CommandType::LOGIN));
  EXPECT_TRUE(hasType(Protocol::CommandType::CREATE_ROOM));
  EXPECT_TRUE(hasType(Protocol::CommandType::JOIN_ROOM));
  EXPECT_TRUE(hasType(Protocol::CommandType::LIST_ROOMS));
  EXPECT_TRUE(hasType(Protocol::CommandType::LEAVE_ROOM));
  EXPECT_TRUE(hasType(Protocol::CommandType::HEARTBEAT));
}

TEST_F(ServerChannelBehaviorTest, ChannelTypedHandlersFlowAndErrors) {
  Protocol::LoginReq badRegisterReq{.type = Protocol::CommandType::LOGIN,
                                    .uid = "not-empty"};
  auto badRegisterEnv = ch1->on_register(badRegisterReq);
  EXPECT_EQ(badRegisterEnv.code,
            (Protocol::SERVICE_FAIL | Protocol::BAD_REQUEST));

  Protocol::LoginReq registerReq{.type = Protocol::CommandType::LOGIN,
                                 .uid = ""};
  auto registerEnv = ch1->on_login(registerReq);
  ASSERT_EQ(registerEnv.code, Protocol::SERVICE_SUCCESS);
  auto registerRsp = registerEnv.data.get<Protocol::RegisterRsp>();
  ASSERT_FALSE(registerRsp.uid.empty());

  Protocol::CreateRoomReq createReq{.type = Protocol::CommandType::CREATE_ROOM,
                                    .uid = registerRsp.uid,
                                    .maximumPeople = 2};
  auto createEnv = ch1->on_create_room(createReq);
  ASSERT_EQ(createEnv.code, Protocol::SERVICE_SUCCESS);
  auto createRsp = createEnv.data.get<Protocol::CreateRoomRsp>();

  Protocol::RegisterRsp bobRegRsp;
  ASSERT_EQ(server->register_user(bobRegRsp), Protocol::SERVICE_SUCCESS);

  Protocol::JoinRoomReq joinReq{.type = Protocol::CommandType::JOIN_ROOM,
                                .roomId = createRsp.roomId,
                                .uid = bobRegRsp.uid};
  auto joinEnv = ch2->on_join_room(joinReq);
  ASSERT_EQ(joinEnv.code, Protocol::SERVICE_SUCCESS);
  auto joinRsp = joinEnv.data.get<Protocol::JoinRoomRsp>();
  EXPECT_EQ(joinRsp.playerInfos.size(), 2U);

  auto listEnv = ch1->on_list_rooms(
      Protocol::ListRoomsReq{.type = Protocol::CommandType::LIST_ROOMS});
  ASSERT_EQ(listEnv.code, Protocol::SERVICE_SUCCESS);
  auto listRsp = listEnv.data.get<Protocol::ListRoomsRsp>();
  EXPECT_FALSE(listRsp.roomInfos.empty());

  auto hbOk = ch1->on_heartbeat(Protocol::HeartbeatReq{
      .type = Protocol::CommandType::HEARTBEAT, .uid = registerRsp.uid});
  EXPECT_EQ(hbOk.code, Protocol::SERVICE_SUCCESS);

  auto hbBad = ch1->on_heartbeat(Protocol::HeartbeatReq{
      .type = Protocol::CommandType::HEARTBEAT, .uid = "does-not-exist"});
  EXPECT_EQ(hbBad.code, (Protocol::SERVICE_FAIL | Protocol::NOT_FOUND));

  auto leaveBob = ch2->on_leave_room(Protocol::LeaveRoomReq{
      .type = Protocol::CommandType::LEAVE_ROOM, .uid = bobRegRsp.uid});
  EXPECT_EQ(leaveBob.code, Protocol::SERVICE_SUCCESS);

  auto leaveAlice = ch1->on_leave_room(Protocol::LeaveRoomReq{
      .type = Protocol::CommandType::LEAVE_ROOM, .uid = registerRsp.uid});
  EXPECT_EQ(leaveAlice.code, Protocol::SERVICE_SUCCESS);

  auto leaveAgain = ch1->on_leave_room(Protocol::LeaveRoomReq{
      .type = Protocol::CommandType::LEAVE_ROOM, .uid = registerRsp.uid});
  EXPECT_EQ(leaveAgain.code,
            (Protocol::SERVICE_FAIL | Protocol::ROOM_STATE_ERROR));
}

TEST_F(ServerChannelBehaviorTest, LoginAfterLogoutUsesStoredProfile) {
  Protocol::RegisterRsp registerRsp;
  ASSERT_EQ(server->register_user(registerRsp), Protocol::SERVICE_SUCCESS);
  const std::string uid = registerRsp.uid;
  ASSERT_FALSE(uid.empty());
  server->logout_user(uid);

  EXPECT_TRUE(server->user_exists(uid));
  EXPECT_EQ(server->get_user(uid), nullptr);

  Protocol::LoginRsp relogged;
  ASSERT_EQ(server->login_user(uid, relogged), Protocol::SERVICE_SUCCESS);
  EXPECT_EQ(relogged.basicInfo.uid, uid);
  EXPECT_EQ(relogged.basicInfo.userName, "");
}

} // namespace
