#pragma once

#include <array>
#include <list>

#include "types.h"

// Protocol definitions using JSON format
namespace Protocol {

struct ShortEnvelope {
  // code/message describe status; data carries command-specific payload.
  int code = 0;
  json data = json::object();
  std::string message;

  NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(ShortEnvelope, code, data,
                                              message)

  struct CodeMessageEntry {
    int mask;
    const char *message;
  };

  static constexpr std::array<CodeMessageEntry, 7> CODE_MESSAGE_TABLE{{
      {Protocol::NOT_FOUND, "not found"},
      {Protocol::ROOM_STATE_ERROR, "room state error"},
      {Protocol::SHOP_INVALID_ITEM, "invalid shop item"},
      {Protocol::SHOP_ITEM_TAKEN, "shop item already taken"},
      {Protocol::SHOP_NOT_MEMBER, "user not in room"},
      {Protocol::BAD_REQUEST, "bad request"},
      {Protocol::DESERIALIZE_FAIL, "deserialize failed"},
  }};

  static std::string map_message_from_code(int code) {
    if (code == Protocol::SERVICE_SUCCESS) {
      return "ok";
    }
    for (const auto &entry : CODE_MESSAGE_TABLE) {
      if ((code & entry.mask) != 0) {
        return entry.message;
      }
    }
    return "error";
  }
  static Protocol::ShortEnvelope make_env(int code,
                                          const json &data = json::object()) {
    Protocol::ShortEnvelope env;
    env.code = code;
    env.message = map_message_from_code(code);
    env.data = data;
    return env;
  }
};

struct LongEnvelope {
  int type;
  json data = json::object();
  std::list<int> pushMessages;

  NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(LongEnvelope, type, data,
                                              pushMessages)

  static Protocol::LongEnvelope
  make_env(int type, const json &data = json::object(),
           const std::list<int> &pushMessages = std::list<int>()) {
    Protocol::LongEnvelope env;
    env.type = type;
    env.data = data;
    env.pushMessages = pushMessages;
    return env;
  }
};

struct RegisterReq {
  Protocol::LoginRequestType type;

  NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(RegisterReq, type)
};

struct LoginReq {
  Protocol::LoginRequestType type;
  std::string uid;

  NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(LoginReq, type, uid)
};

struct LogoutReq {
  Protocol::LoginRequestType type;
  std::string uid;

  NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(LogoutReq, type, uid)
};

struct EditProfileReq {
  Protocol::HomeRequestType type;
  std::string uid;
  PlayerBasicInfo basicInfo;

  NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(EditProfileReq, type, uid,
                                              basicInfo);
};

struct CreateRoomReq {
  Protocol::HomeRequestType type;
  std::string uid;
  size_t maximumPeople = 0;

  NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(CreateRoomReq, type, uid,
                                              maximumPeople)
};

struct JoinRoomReq {
  Protocol::HomeRequestType type;
  int roomId = -1;
  std::string uid;

  NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(JoinRoomReq, type, roomId, uid)
};

struct LeaveRoomReq {
  Protocol::HomeRequestType type;
  std::string uid;

  NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(LeaveRoomReq, type, uid)
};

struct ListRoomsReq {
  Protocol::HomeRequestType type;
  NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(ListRoomsReq, type)
};

struct GetStateStatusReq {
  Protocol::HomeRequestType type;
  std::string uid;

  NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(GetStateStatusReq, type, uid)
};

struct SendMessageReq {
  Protocol::HomeRequestType type;
  std::string content;

  NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(SendMessageReq, type, content)
};

struct SetReadyReq {
  Protocol::HomeRequestType type;
  std::string uid;
  bool ready = false;

  NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(SetReadyReq, type, uid, ready)
};

struct ShopInitReq {
  Protocol::ShopRequestType type;
  std::string uid;

  NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(ShopInitReq, type, uid)
};

struct ShopMoveCursorReq {
  Protocol::ShopRequestType type;
  std::string uid;
  std::string itemId;

  NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(ShopMoveCursorReq, type, uid,
                                              itemId)
};

struct ShopBuyItemReq {
  Protocol::ShopRequestType type;
  std::string uid;
  std::string itemId;

  NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(ShopBuyItemReq, type, uid, itemId)
};

struct MapInitReq {
  Protocol::MapRequestType type;
  int roomId = -1;
  std::string uid;

  NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(MapInitReq, type, roomId, uid)
};

struct MapMoveReq {
  Protocol::MapRequestType type;
  std::string uid;
  int selectId = -1;

  NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(MapMoveReq, type, uid, selectId)
};

struct EmptyRsp {};

// Marker response type: long dispatch should not send any direct response.
struct NoResponseRsp {};

struct RegisterRsp {
  std::string uid;

  NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(RegisterRsp, uid)
};

struct LoginRsp {
  PlayerData playerData;

  NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(LoginRsp, playerData)
};

struct CreateRoomRsp {
  RoomInfo roomInfo;

  NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(CreateRoomRsp, roomInfo)
};

struct JoinRoomRsp {
  RoomInfo roomInfo;

  NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(JoinRoomRsp, roomInfo)
};

struct SetReadyRsp {
  RoomInfo roomInfo;

  NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(SetReadyRsp, roomInfo)
};

struct ShopPlayerInfo {
  PlayerBasicInfo playerInfo;
  std::vector<std::string> ownedItems;

  NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(ShopPlayerInfo, playerInfo,
                                              ownedItems)
};

struct ShopInitRsp {
  std::vector<ShopItem> items;
  std::vector<ShopPlayerInfo> playerInfos;

  NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(ShopInitRsp, items, playerInfos)
};

struct MapInitRsp {
  std::vector<MapNode> map;

  NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(MapInitRsp, map)
};

struct MapSync {
  std::string uid;
  int selectId = -1;

  NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(MapSync, uid, selectId)
};

struct MapSyncRsp {
  std::vector<MapSync> selectStatus;

  NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(MapSyncRsp, selectStatus)
};

struct ListRoomsRsp {
  std::vector<RoomInfo> roomInfos;

  NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(ListRoomsRsp, roomInfos)
};

struct GetStateStatusRsp {
  bool online = false;
  int roomId = -1;
  int roomPhase = 0;
  int roomMemberCount = 0;
  bool allLobbyReady = false;
  int mapNodeId = -1;
  int battleTick = 0;

  NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(GetStateStatusRsp, online, roomId,
                                              roomPhase, roomMemberCount,
                                              allLobbyReady, mapNodeId,
                                              battleTick)
};
} // namespace Protocol
