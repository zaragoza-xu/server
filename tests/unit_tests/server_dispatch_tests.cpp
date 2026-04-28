#include <memory>

#include <gtest/gtest.h>

#include <asio/io_context.hpp>

#include "protocol.h"
#include "server.h"

namespace {

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

  // Shop init via shop server (long dispatch)
  auto shopInitEnv = shopServer->dispatch_request(json(Protocol::ShopInitReq{
      .type = Protocol::ShopRequestType::SHOP_INIT, .uid = uid}));
  EXPECT_EQ(shopInitEnv.get<Protocol::LongEnvelope>().type,
            static_cast<int>(Protocol::ShopRequestType::SHOP_INIT));
  EXPECT_FALSE(shopInitEnv.get<Protocol::LongEnvelope>()
                   .data.get<Protocol::ShopInitRsp>()
                   .items.empty());

  // Shop move cursor via shop server (long dispatch, returns null)
  auto shopMoveEnv =
      shopServer->dispatch_request(json(Protocol::ShopMoveCursorReq{
          .type = Protocol::ShopRequestType::SHOP_MOVE_CURSOR,
          .uid = uid,
          .itemId = "1",
      }));
  EXPECT_TRUE(shopMoveEnv.is_null());

  // Unknown type on shop server -> BAD_REQUEST
  auto badOnShopServer = shopServer->dispatch_request(json::object(
      {{"type", static_cast<int>(Protocol::ShopRequestType::ERROR)},
       {"uid", uid}}));
  EXPECT_EQ(badOnShopServer.get<Protocol::ShortEnvelope>().code,
            (Protocol::SERVICE_FAIL | Protocol::BAD_REQUEST));

  // Shop type sent to home server - numeric value 0 maps to CREATE_ROOM
  // on home server, so it won't be BAD_REQUEST but a different command.
  // Use a value outside home server's command table to verify rejection.
  auto badTypeOnHome =
      homeServer->dispatch_request(json::object({{"type", 99}, {"uid", uid}}));
  EXPECT_EQ(badTypeOnHome.get<Protocol::ShortEnvelope>().code,
            (Protocol::SERVICE_FAIL | Protocol::BAD_REQUEST));
}

} // namespace
