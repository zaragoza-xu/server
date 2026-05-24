#include <memory>
#include <string>
#include <vector>

#include <algorithm>

#include <gtest/gtest.h>

#include <asio/io_context.hpp>

#include "protocol.h"
#include "server.h"

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

  Protocol::NoResponseRsp readyRsp;
  ASSERT_EQ(
      server->set_ready(
          Protocol::SetReadyReq{.type = Protocol::HomeRequestType::SET_READY,
                                .uid = aliceUid,
                                .ready = true},
          readyRsp),
      Protocol::SERVICE_SUCCESS);
  ASSERT_EQ(
      server->set_ready(
          Protocol::SetReadyReq{.type = Protocol::HomeRequestType::SET_READY,
                                .uid = bobUid,
                                .ready = true},
          readyRsp),
      Protocol::SERVICE_SUCCESS);

  Protocol::ShopInitRsp initRsp;
  ASSERT_EQ(
      server->shop_init(
          Protocol::ShopInitReq{.type = Protocol::ShopRequestType::SHOP_INIT,
                                .uid = aliceUid},
          initRsp),
      Protocol::SERVICE_SUCCESS);
  EXPECT_FALSE(initRsp.items.empty());
  EXPECT_EQ(initRsp.playerInfos.size(), 2U);

  Protocol::NoResponseRsp moveRsp;
  ASSERT_EQ(server->shop_move_cursor(
                Protocol::ShopMoveCursorReq{
                    .type = Protocol::ShopRequestType::SHOP_MOVE_CURSOR,
                    .uid = aliceUid,
                    .itemId = "1",
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
  ASSERT_GT(afterMoveRsp.items.size(), 1U);
  // Use second item (direction=1 moves from index 0 to index 1)
  const std::string selectedItemId = afterMoveRsp.items.at(1).itemId;
  ASSERT_FALSE(selectedItemId.empty());

  Protocol::NoResponseRsp buyRsp;
  ASSERT_EQ(
      server->shop_buy_item(
          Protocol::ShopBuyItemReq{.type = Protocol::ShopRequestType::SHOP_BUY,
                                   .uid = aliceUid,
                                   .itemId = selectedItemId},
          buyRsp),
      Protocol::SERVICE_SUCCESS);

  EXPECT_EQ(
      server->shop_buy_item(
          Protocol::ShopBuyItemReq{.type = Protocol::ShopRequestType::SHOP_BUY,
                                   .uid = bobUid,
                                   .itemId = selectedItemId},
          buyRsp),
      (Protocol::SERVICE_FAIL | Protocol::SHOP_ITEM_TAKEN));

  EXPECT_EQ(
      server->shop_buy_item(
          Protocol::ShopBuyItemReq{.type = Protocol::ShopRequestType::SHOP_BUY,
                                   .uid = bobUid,
                                   .itemId = "unknown-item"},
          buyRsp),
      (Protocol::SERVICE_FAIL | Protocol::SHOP_INVALID_ITEM));
}

} // namespace
