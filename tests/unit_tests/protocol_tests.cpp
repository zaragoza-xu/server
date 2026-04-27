#include <string>
#include <vector>

#include <algorithm>

#include <gtest/gtest.h>

#include "logging.h"
#include "protocol.h"

namespace {

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

} // namespace
