#include <memory>

#include <gtest/gtest.h>

#include <asio/io_context.hpp>

#include "protocol.h"
#include "server.h"

namespace {

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

} // namespace
