#include <memory>
#include <string>

#include <algorithm>

#include <gtest/gtest.h>

#include <asio/io_context.hpp>

#include "logging.h"
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
  EXPECT_EQ(static_cast<int>(LoginRequestType::LOGOUT), 2);
  EXPECT_EQ(static_cast<int>(LoginRequestType::ERROR), 100);

  EXPECT_EQ(static_cast<int>(HomeRequestType::CREATE_ROOM), 0);
  EXPECT_EQ(static_cast<int>(HomeRequestType::JOIN_ROOM), 1);
  EXPECT_EQ(static_cast<int>(HomeRequestType::LEAVE_ROOM), 2);
  EXPECT_EQ(static_cast<int>(HomeRequestType::LIST_ROOMS), 3);
  EXPECT_EQ(static_cast<int>(HomeRequestType::SEND_MESSAGE), 4);
  EXPECT_EQ(static_cast<int>(HomeRequestType::HEARTBEAT), 5);
  EXPECT_EQ(static_cast<int>(HomeRequestType::EDIT_PROFILE), 6);
  EXPECT_EQ(static_cast<int>(HomeRequestType::SET_READY), 7);
  EXPECT_EQ(static_cast<int>(HomeRequestType::BROADCAST), 8);
  EXPECT_EQ(static_cast<int>(HomeRequestType::GET_STATE_STATUS), 9);
  EXPECT_EQ(static_cast<int>(HomeRequestType::ERROR), 100);

  using Protocol::ShopRequestType;
  EXPECT_EQ(static_cast<int>(ShopRequestType::SHOP_INIT), 0);
  EXPECT_EQ(static_cast<int>(ShopRequestType::SHOP_MOVE_CURSOR), 1);
  EXPECT_EQ(static_cast<int>(ShopRequestType::SHOP_BUY_ITEM), 2);
  EXPECT_EQ(static_cast<int>(ShopRequestType::ERROR), 100);
}

TEST(ProtocolTest, RequestTypeNameMapping) {
  EXPECT_EQ(logging::request_type_name(Protocol::LoginRequestType::LOGIN),
            "LOGIN");
  EXPECT_EQ(logging::request_type_name(Protocol::LoginRequestType::REGISTER),
            "REGISTER");
  EXPECT_EQ(logging::request_type_name(Protocol::HomeRequestType::CREATE_ROOM),
            "CREATE_ROOM");
  EXPECT_EQ(logging::request_type_name(Protocol::HomeRequestType::SET_READY),
            "SET_READY");
  EXPECT_EQ(logging::request_type_name(Protocol::ShopRequestType::SHOP_INIT),
            "SHOP_INIT");
}

TEST(ProtocolTest, EnvelopeJsonRoundTrip) {
  Protocol::ShortEnvelope env;
  env.code = Protocol::SERVICE_SUCCESS;
  env.message = "ok";
  env.data = json{{"roomId", 42}, {"roomName", "lobby"}};

  json j = env;
  EXPECT_EQ(j.at("code"), Protocol::SERVICE_SUCCESS);
  EXPECT_EQ(j.at("message"), "ok");
  EXPECT_EQ(j.at("data").at("roomId"), 42);

  auto parsed = j.get<Protocol::ShortEnvelope>();
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
                                    .basicInfos = {p1, p2},
                                    .readyUids = {"1001"}};
  json rspJson = rsp;

  auto parsedRsp = rspJson.get<Protocol::JoinRoomRsp>();
  EXPECT_EQ(parsedRsp.roomInfo.roomId, 42);
  EXPECT_EQ(parsedRsp.roomInfo.maximumPeople, 6U);
  ASSERT_EQ(parsedRsp.roomInfo.basicInfos.size(), 2U);
  auto by_uid = [](const std::vector<Protocol::PlayerBasicInfo> &infos,
                   const std::string &uid) {
    return std::find_if(infos.begin(), infos.end(),
                        [&uid](const Protocol::PlayerBasicInfo &info) {
                          return info.uid == uid;
                        });
  };
  auto it1001 = by_uid(parsedRsp.roomInfo.basicInfos, "1001");
  auto it1002 = by_uid(parsedRsp.roomInfo.basicInfos, "1002");
  ASSERT_NE(it1001, parsedRsp.roomInfo.basicInfos.end());
  ASSERT_NE(it1002, parsedRsp.roomInfo.basicInfos.end());
  EXPECT_EQ(it1001->uid, "1001");
  EXPECT_EQ(it1002->name, "bob");
  ASSERT_EQ(parsedRsp.roomInfo.readyUids.size(), 1U);
  EXPECT_EQ(parsedRsp.roomInfo.readyUids.front(), "1001");

  Protocol::EditProfileReq editReq;
  editReq.type = Protocol::HomeRequestType::EDIT_PROFILE;
  editReq.basicInfo = Protocol::PlayerBasicInfo{"1001", "alice", 7};

  json editJson = editReq;
  EXPECT_EQ(editJson.at("basicInfo").at("uid"), "1001");
  EXPECT_EQ(editJson.at("basicInfo").at("name"), "alice");
  EXPECT_EQ(editJson.at("basicInfo").at("color"), 7);

  auto parsedEdit = editJson.get<Protocol::EditProfileReq>();
  EXPECT_EQ(parsedEdit.basicInfo.uid, "1001");
  EXPECT_EQ(parsedEdit.basicInfo.name, "alice");
  EXPECT_EQ(parsedEdit.basicInfo.color, 7);
}

TEST(RoomTest, BasicBehavior) {
  auto state = std::make_shared<ServerState>();

  Protocol::PlayerBasicInfo creatorInfo{"1", "creator", 1};
  state->userData.emplace(creatorInfo.uid,
                          Protocol::PlayerData{.basicInfo = creatorInfo});
  auto creator = std::make_shared<User>(creatorInfo.uid, state);
  Room room(10, 2, state, creator);

  EXPECT_EQ(room.get_id(), 10);
  EXPECT_EQ(room.get_maximum_people(), 2);
  EXPECT_EQ(room.get_people_count(), 1);
  EXPECT_TRUE(room.is_member("1"));

  Protocol::PlayerBasicInfo info2{"2", "u2", 2};
  Protocol::PlayerBasicInfo info3{"3", "u3", 3};
  state->userData.emplace(info2.uid, Protocol::PlayerData{.basicInfo = info2});
  state->userData.emplace(info3.uid, Protocol::PlayerData{.basicInfo = info3});
  auto user2 = std::make_shared<User>(info2.uid, state);
  auto user3 = std::make_shared<User>(info3.uid, state);

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

TEST(RoomTest, ReadyStateBehavior) {
  auto state = std::make_shared<ServerState>();

  Protocol::PlayerBasicInfo creatorInfo{"1", "creator", 1};
  state->userData.emplace(creatorInfo.uid,
                          Protocol::PlayerData{.basicInfo = creatorInfo});
  auto creator = std::make_shared<User>(creatorInfo.uid, state);
  Room room(10, 2, state, creator);

  Protocol::PlayerBasicInfo info2{"2", "u2", 2};
  state->userData.emplace(info2.uid, Protocol::PlayerData{.basicInfo = info2});
  auto user2 = std::make_shared<User>(info2.uid, state);
  ASSERT_TRUE(room.add_member(user2));

  EXPECT_TRUE(room.set_member_ready("1", true));
  EXPECT_TRUE(room.set_member_ready("2", false));
  EXPECT_FALSE(room.set_member_ready("404", true));

  const auto info = room.get_info();
  ASSERT_EQ(info.readyUids.size(), 1U);
  EXPECT_EQ(info.readyUids.front(), "1");

  ASSERT_TRUE(room.remove_member("1"));
  const auto infoAfterLeave = room.get_info();
  EXPECT_TRUE(infoAfterLeave.readyUids.empty());
}

TEST(RoomTest, GetInfoSkipsMissingProfiles) {
  auto state = std::make_shared<ServerState>();

  Protocol::PlayerBasicInfo creatorInfo{"10", "creator", 1};
  Protocol::PlayerBasicInfo info2{"20", "u2", 2};
  state->userData.emplace(creatorInfo.uid,
                          Protocol::PlayerData{.basicInfo = creatorInfo});
  state->userData.emplace(info2.uid, Protocol::PlayerData{.basicInfo = info2});

  auto creator = std::make_shared<User>(creatorInfo.uid, state);
  auto user2 = std::make_shared<User>(info2.uid, state);
  Room room(99, 2, state, creator);
  ASSERT_TRUE(room.add_member(user2));

  state->userData.erase(info2.uid);

  const auto roomInfo = room.get_info();
  ASSERT_EQ(roomInfo.basicInfos.size(), 1U);
  EXPECT_EQ(roomInfo.basicInfos.front().uid, creatorInfo.uid);
}

TEST(ServerBehaviorTest, ErrorPathsCoverage) {
  asio::io_context ioContext;
  auto state = std::make_shared<ServerState>();
  auto server = std::make_shared<Server>(ioContext, 0, state);

  Protocol::LoginRsp loginRsp;
  EXPECT_EQ(server->login_user(
                Protocol::LoginReq{.type = Protocol::LoginRequestType::LOGIN,
                                   .uid = ""},
                loginRsp),
            (Protocol::SERVICE_FAIL | Protocol::BAD_REQUEST));
  EXPECT_EQ(server->login_user(
                Protocol::LoginReq{.type = Protocol::LoginRequestType::LOGIN,
                                   .uid = "404"},
                loginRsp),
            (Protocol::SERVICE_FAIL | Protocol::NOT_FOUND));

  Protocol::CreateRoomRsp createRsp;
  EXPECT_EQ(server->create_room(
                Protocol::CreateRoomReq{
                    .type = Protocol::HomeRequestType::CREATE_ROOM,
                    .uid = "404",
                    .maximumPeople = 1,
                },
                createRsp),
            (Protocol::SERVICE_FAIL | Protocol::NOT_FOUND));

  Protocol::EmptyRsp emptyRsp;
  EXPECT_EQ(
      server->leave_room(
          Protocol::LeaveRoomReq{.type = Protocol::HomeRequestType::LEAVE_ROOM,
                                 .uid = "404"},
          emptyRsp),
      (Protocol::SERVICE_FAIL | Protocol::NOT_FOUND));
  EXPECT_EQ(server->logout_user(
                Protocol::LogoutReq{.type = Protocol::LoginRequestType::LOGOUT,
                                    .uid = ""},
                emptyRsp),
            (Protocol::SERVICE_FAIL | Protocol::BAD_REQUEST));
  EXPECT_EQ(server->logout_user(
                Protocol::LogoutReq{.type = Protocol::LoginRequestType::LOGOUT,
                                    .uid = "404"},
                emptyRsp),
            (Protocol::SERVICE_FAIL | Protocol::BAD_REQUEST));

  Protocol::RegisterRsp reg1;
  ASSERT_EQ(
      server->register_user(
          Protocol::RegisterReq{.type = Protocol::LoginRequestType::REGISTER},
          reg1),
      Protocol::SERVICE_SUCCESS);
  const auto uid1 = reg1.uid;
  ASSERT_EQ(server->login_user(
                Protocol::LoginReq{.type = Protocol::LoginRequestType::LOGIN,
                                   .uid = uid1},
                loginRsp),
            Protocol::SERVICE_SUCCESS);

  EXPECT_EQ(server->leave_room(
                Protocol::LeaveRoomReq{
                    .type = Protocol::HomeRequestType::LEAVE_ROOM, .uid = uid1},
                emptyRsp),
            (Protocol::SERVICE_FAIL | Protocol::ROOM_STATE_ERROR));

  Protocol::NoResponseRsp setReadyRsp;
  EXPECT_EQ(
      server->set_ready(
          Protocol::SetReadyReq{.type = Protocol::HomeRequestType::SET_READY,
                                .uid = uid1,
                                .ready = true},
          setReadyRsp),
      (Protocol::SERVICE_FAIL | Protocol::ROOM_STATE_ERROR));

  ASSERT_EQ(server->create_room(
                Protocol::CreateRoomReq{
                    .type = Protocol::HomeRequestType::CREATE_ROOM,
                    .uid = uid1,
                    .maximumPeople = 1,
                },
                createRsp),
            Protocol::SERVICE_SUCCESS);

  EXPECT_EQ(server->create_room(
                Protocol::CreateRoomReq{
                    .type = Protocol::HomeRequestType::CREATE_ROOM,
                    .uid = uid1,
                    .maximumPeople = 1,
                },
                createRsp),
            (Protocol::SERVICE_FAIL | Protocol::ROOM_STATE_ERROR));

  Protocol::JoinRoomRsp joinRsp;
  EXPECT_EQ(
      server->join_room(
          Protocol::JoinRoomReq{.type = Protocol::HomeRequestType::JOIN_ROOM,
                                .roomId = createRsp.roomInfo.roomId,
                                .uid = uid1},
          joinRsp),
      (Protocol::SERVICE_FAIL | Protocol::ROOM_STATE_ERROR));

  Protocol::RegisterRsp reg2;
  ASSERT_EQ(
      server->register_user(
          Protocol::RegisterReq{.type = Protocol::LoginRequestType::REGISTER},
          reg2),
      Protocol::SERVICE_SUCCESS);
  const auto uid2 = reg2.uid;
  ASSERT_EQ(server->login_user(
                Protocol::LoginReq{.type = Protocol::LoginRequestType::LOGIN,
                                   .uid = uid2},
                loginRsp),
            Protocol::SERVICE_SUCCESS);

  EXPECT_EQ(
      server->join_room(
          Protocol::JoinRoomReq{.type = Protocol::HomeRequestType::JOIN_ROOM,
                                .roomId = createRsp.roomInfo.roomId,
                                .uid = uid2},
          joinRsp),
      (Protocol::SERVICE_FAIL | Protocol::ROOM_STATE_ERROR));

  EXPECT_EQ(
      server->set_ready(
          Protocol::SetReadyReq{.type = Protocol::HomeRequestType::SET_READY,
                                .uid = "404",
                                .ready = true},
          setReadyRsp),
      (Protocol::SERVICE_FAIL | Protocol::NOT_FOUND));

  Protocol::NoResponseRsp shopMoveRsp;
  EXPECT_EQ(server->shop_move_cursor(
                Protocol::ShopMoveCursorReq{
                    .type = Protocol::ShopRequestType::SHOP_MOVE_CURSOR,
                    .uid = uid1,
                    .direction = 2,
                },
                shopMoveRsp),
            (Protocol::SERVICE_FAIL | Protocol::BAD_REQUEST));

  Protocol::EditProfileReq editReq{
      .type = Protocol::HomeRequestType::EDIT_PROFILE,
      .uid = uid2,
      .basicInfo = Protocol::PlayerBasicInfo{uid2, "updated", 6}};
  state->userData.erase(uid2);
  EXPECT_EQ(server->edit_profile(editReq, emptyRsp),
            (Protocol::SERVICE_FAIL | Protocol::NOT_FOUND));

  editReq.uid = "nobody";
  EXPECT_EQ(server->edit_profile(editReq, emptyRsp),
            (Protocol::SERVICE_FAIL | Protocol::NOT_FOUND));
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
  const auto roomId = createRsp.roomInfo.roomId;
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
  EXPECT_EQ(
      server->join_room(
          Protocol::JoinRoomReq{.type = Protocol::HomeRequestType::JOIN_ROOM,
                                .roomId = roomId,
                                .uid = bobUid},
          joinRsp),
      (Protocol::SERVICE_FAIL | Protocol::NOT_FOUND));

  Protocol::LoginRsp bobLoginRsp;
  ASSERT_EQ(server->login_user(
                Protocol::LoginReq{.type = Protocol::LoginRequestType::LOGIN,
                                   .uid = bobUid},
                bobLoginRsp),
            Protocol::SERVICE_SUCCESS);
  EXPECT_EQ(bobLoginRsp.playerData.basicInfo.uid, bobUid);

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
  auto has_uid = [](const std::vector<Protocol::PlayerBasicInfo> &infos,
                    const std::string &uid) {
    return std::any_of(infos.begin(), infos.end(),
                       [&uid](const Protocol::PlayerBasicInfo &info) {
                         return info.uid == uid;
                       });
  };
  EXPECT_TRUE(has_uid(joinRsp.roomInfo.basicInfos, aliceUid));
  EXPECT_TRUE(has_uid(joinRsp.roomInfo.basicInfos, bobUid));

  Protocol::NoResponseRsp setReadyRsp;
  ASSERT_EQ(
      server->set_ready(
          Protocol::SetReadyReq{.type = Protocol::HomeRequestType::SET_READY,
                                .uid = bobUid,
                                .ready = true},
          setReadyRsp),
      Protocol::SERVICE_SUCCESS);

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
  auto registerShort = registerEnv.get<Protocol::ShortEnvelope>();
  ASSERT_EQ(registerShort.code, Protocol::SERVICE_SUCCESS);
  const auto uid = registerShort.data.get<Protocol::RegisterRsp>().uid;
  ASSERT_FALSE(uid.empty());

  auto logoutBeforeLogin =
      loginServer->dispatch_request(json(Protocol::LogoutReq{
          .type = Protocol::LoginRequestType::LOGOUT, .uid = uid}));
  EXPECT_EQ(logoutBeforeLogin.get<Protocol::ShortEnvelope>().code,
            (Protocol::SERVICE_FAIL | Protocol::BAD_REQUEST));

  auto reloginEnv = loginServer->dispatch_request(json(Protocol::LoginReq{
      .type = Protocol::LoginRequestType::LOGIN, .uid = uid}));
  EXPECT_EQ(reloginEnv.get<Protocol::ShortEnvelope>().code,
            Protocol::SERVICE_SUCCESS);

  auto duplicateLoginEnv =
      loginServer->dispatch_request(json(Protocol::LoginReq{
          .type = Protocol::LoginRequestType::LOGIN, .uid = uid}));
  EXPECT_EQ(duplicateLoginEnv.get<Protocol::ShortEnvelope>().code,
            (Protocol::SERVICE_FAIL | Protocol::BAD_REQUEST));

  auto logoutEnv = loginServer->dispatch_request(json(Protocol::LogoutReq{
      .type = Protocol::LoginRequestType::LOGOUT, .uid = uid}));
  EXPECT_EQ(logoutEnv.get<Protocol::ShortEnvelope>().code,
            Protocol::SERVICE_SUCCESS);

  auto loginAfterLogoutEnv =
      loginServer->dispatch_request(json(Protocol::LoginReq{
          .type = Protocol::LoginRequestType::LOGIN, .uid = uid}));
  EXPECT_EQ(loginAfterLogoutEnv.get<Protocol::ShortEnvelope>().code,
            Protocol::SERVICE_SUCCESS);

  auto badOnLoginServer = loginServer->dispatch_request(json(
      Protocol::ListRoomsReq{.type = Protocol::HomeRequestType::LIST_ROOMS}));
  EXPECT_EQ(badOnLoginServer.get<Protocol::ShortEnvelope>().code,
            (Protocol::SERVICE_FAIL | Protocol::BAD_REQUEST));

  auto listOnHomeServer = homeServer->dispatch_request(json(
      Protocol::ListRoomsReq{.type = Protocol::HomeRequestType::LIST_ROOMS}));
  EXPECT_EQ(listOnHomeServer.get<Protocol::ShortEnvelope>().code,
            Protocol::SERVICE_SUCCESS);

  auto createRoomEnv =
      homeServer->dispatch_request(json(Protocol::CreateRoomReq{
          .type = Protocol::HomeRequestType::CREATE_ROOM,
          .uid = uid,
          .maximumPeople = 2,
      }));
  ASSERT_EQ(createRoomEnv.get<Protocol::ShortEnvelope>().code,
            Protocol::SERVICE_SUCCESS);

  auto setReadyEnv = homeServer->dispatch_request(json(Protocol::SetReadyReq{
      .type = Protocol::HomeRequestType::SET_READY,
      .uid = uid,
      .ready = true,
  }));
  EXPECT_TRUE(setReadyEnv.is_null());

  auto leaveEnv = homeServer->dispatch_request(json(Protocol::LeaveRoomReq{
      .type = Protocol::HomeRequestType::LEAVE_ROOM,
      .uid = uid,
  }));
  const auto leaveLong = leaveEnv.get<Protocol::LongEnvelope>();
  EXPECT_EQ(leaveLong.type,
            static_cast<int>(Protocol::HomeRequestType::LEAVE_ROOM));
  EXPECT_TRUE(leaveLong.pushMessages.empty());

  auto badOnHomeServer = homeServer->dispatch_request(json::object(
      {{"type", static_cast<int>(Protocol::LoginRequestType::ERROR)},
       {"uid", uid}}));
  EXPECT_EQ(badOnHomeServer.get<Protocol::ShortEnvelope>().code,
            (Protocol::SERVICE_FAIL | Protocol::BAD_REQUEST));
}

TEST(ServerDispatchTest, ShopServerDispatchRouting) {
  asio::io_context ioContext;
  auto state = std::make_shared<ServerState>();
  auto loginServer = std::make_shared<LoginServer>(ioContext, 0, state);
  auto homeServer = std::make_shared<HomeServer>(ioContext, 0, state);
  auto shopServer = std::make_shared<ShopServer>(ioContext, 0, state);

  // Register + login a user
  auto registerEnv = loginServer->dispatch_request(json(Protocol::LoginReq{
      .type = Protocol::LoginRequestType::REGISTER, .uid = ""}));
  const auto uid = registerEnv.get<Protocol::ShortEnvelope>()
                       .data.get<Protocol::RegisterRsp>()
                       .uid;

  loginServer->dispatch_request(json(Protocol::LoginReq{
      .type = Protocol::LoginRequestType::LOGIN, .uid = uid}));

  // Create room via home server
  auto createEnv = homeServer->dispatch_request(json(Protocol::CreateRoomReq{
      .type = Protocol::HomeRequestType::CREATE_ROOM,
      .uid = uid,
      .maximumPeople = 2,
  }));
  ASSERT_EQ(createEnv.get<Protocol::ShortEnvelope>().code,
            Protocol::SERVICE_SUCCESS);

  // Shop init via shop server
  auto shopInitEnv = shopServer->dispatch_request(json(Protocol::ShopInitReq{
      .type = Protocol::ShopRequestType::SHOP_INIT, .uid = uid}));
  EXPECT_EQ(shopInitEnv.get<Protocol::ShortEnvelope>().code,
            Protocol::SERVICE_SUCCESS);

  // Shop move cursor via shop server (long dispatch, returns null)
  auto shopMoveEnv =
      shopServer->dispatch_request(json(Protocol::ShopMoveCursorReq{
          .type = Protocol::ShopRequestType::SHOP_MOVE_CURSOR,
          .uid = uid,
          .direction = 1,
      }));
  EXPECT_TRUE(shopMoveEnv.is_null());

  // Unknown type on shop server → BAD_REQUEST
  auto badOnShopServer = shopServer->dispatch_request(json::object(
      {{"type", static_cast<int>(Protocol::ShopRequestType::ERROR)},
       {"uid", uid}}));
  EXPECT_EQ(badOnShopServer.get<Protocol::ShortEnvelope>().code,
            (Protocol::SERVICE_FAIL | Protocol::BAD_REQUEST));

  // Shop type sent to home server — numeric value 0 maps to CREATE_ROOM
  // on home server, so it won't be BAD_REQUEST but a different command.
  // Use a value outside home server's command table to verify rejection.
  auto badTypeOnHome =
      homeServer->dispatch_request(json::object({{"type", 99}, {"uid", uid}}));
  EXPECT_EQ(badTypeOnHome.get<Protocol::ShortEnvelope>().code,
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

  Protocol::LoginRsp relogged;
  ASSERT_EQ(server->login_user(
                Protocol::LoginReq{.type = Protocol::LoginRequestType::LOGIN,
                                   .uid = uid},
                relogged),
            Protocol::SERVICE_SUCCESS);
  EXPECT_EQ(relogged.playerData.basicInfo.uid, uid);
  EXPECT_EQ(relogged.playerData.basicInfo.name, "");

  Protocol::EmptyRsp logoutRsp;
  ASSERT_EQ(server->logout_user(
                Protocol::LogoutReq{.type = Protocol::LoginRequestType::LOGOUT,
                                    .uid = uid},
                logoutRsp),
            Protocol::SERVICE_SUCCESS);

  EXPECT_TRUE(server->user_exists(uid));
  EXPECT_EQ(server->get_user(uid), nullptr);
}

TEST_F(ServerChannelBehaviorTest, ShopFlowBehavior) {
  Protocol::RegisterRsp aliceRegRsp;
  ASSERT_EQ(
      server->register_user(
          Protocol::RegisterReq{.type = Protocol::LoginRequestType::REGISTER},
          aliceRegRsp),
      Protocol::SERVICE_SUCCESS);
  const auto aliceUid = aliceRegRsp.uid;

  Protocol::RegisterRsp bobRegRsp;
  ASSERT_EQ(
      server->register_user(
          Protocol::RegisterReq{.type = Protocol::LoginRequestType::REGISTER},
          bobRegRsp),
      Protocol::SERVICE_SUCCESS);
  const auto bobUid = bobRegRsp.uid;

  Protocol::LoginRsp loginRsp;
  ASSERT_EQ(server->login_user(
                Protocol::LoginReq{.type = Protocol::LoginRequestType::LOGIN,
                                   .uid = aliceUid},
                loginRsp),
            Protocol::SERVICE_SUCCESS);
  ASSERT_EQ(server->login_user(
                Protocol::LoginReq{.type = Protocol::LoginRequestType::LOGIN,
                                   .uid = bobUid},
                loginRsp),
            Protocol::SERVICE_SUCCESS);

  Protocol::CreateRoomRsp createRsp;
  ASSERT_EQ(server->create_room(
                Protocol::CreateRoomReq{
                    .type = Protocol::HomeRequestType::CREATE_ROOM,
                    .uid = aliceUid,
                    .maximumPeople = 2},
                createRsp),
            Protocol::SERVICE_SUCCESS);

  Protocol::JoinRoomRsp joinRsp;
  ASSERT_EQ(
      server->join_room(
          Protocol::JoinRoomReq{.type = Protocol::HomeRequestType::JOIN_ROOM,
                                .roomId = createRsp.roomInfo.roomId,
                                .uid = bobUid},
          joinRsp),
      Protocol::SERVICE_SUCCESS);

  Protocol::ShopInitRsp initRsp;
  ASSERT_EQ(
      server->shop_init(
          Protocol::ShopInitReq{.type = Protocol::ShopRequestType::SHOP_INIT,
                                .uid = aliceUid},
          initRsp),
      Protocol::SERVICE_SUCCESS);
  EXPECT_FALSE(initRsp.itemIds.empty());
  EXPECT_EQ(initRsp.playerInfos.size(), 2U);

  Protocol::NoResponseRsp moveRsp;
  ASSERT_EQ(server->shop_move_cursor(
                Protocol::ShopMoveCursorReq{
                    .type = Protocol::ShopRequestType::SHOP_MOVE_CURSOR,
                    .uid = aliceUid,
                    .direction = 1,
                },
                moveRsp),
            Protocol::SERVICE_SUCCESS);

  // After move_cursor, get the selected item via shop_init
  Protocol::ShopInitRsp afterMoveRsp;
  ASSERT_EQ(
      server->shop_init(
          Protocol::ShopInitReq{.type = Protocol::ShopRequestType::SHOP_INIT,
                                .uid = aliceUid},
          afterMoveRsp),
      Protocol::SERVICE_SUCCESS);
  ASSERT_GT(afterMoveRsp.itemIds.size(), 1U);
  // Use second item (direction=1 moves from index 0 to index 1)
  const std::string selectedItemId = afterMoveRsp.itemIds.at(1);
  ASSERT_FALSE(selectedItemId.empty());

  Protocol::NoResponseRsp buyRsp;
  ASSERT_EQ(server->shop_buy_item(
                Protocol::ShopBuyItemReq{
                    .type = Protocol::ShopRequestType::SHOP_BUY_ITEM,
                    .uid = aliceUid,
                    .itemId = selectedItemId},
                buyRsp),
            Protocol::SERVICE_SUCCESS);

  EXPECT_EQ(server->shop_buy_item(
                Protocol::ShopBuyItemReq{
                    .type = Protocol::ShopRequestType::SHOP_BUY_ITEM,
                    .uid = bobUid,
                    .itemId = selectedItemId},
                buyRsp),
            (Protocol::SERVICE_FAIL | Protocol::SHOP_ITEM_TAKEN));

  EXPECT_EQ(server->shop_buy_item(
                Protocol::ShopBuyItemReq{
                    .type = Protocol::ShopRequestType::SHOP_BUY_ITEM,
                    .uid = bobUid,
                    .itemId = "unknown-item"},
                buyRsp),
            (Protocol::SERVICE_FAIL | Protocol::SHOP_INVALID_ITEM));
}

} // namespace
