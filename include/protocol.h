#pragma once

#include <array>
#include <cstddef>
#include <list>
#include <string>
#include <string_view>
#include <vector>

#include <nlohmann/json.hpp>

using json = nlohmann::json;

// Protocol definitions using JSON format
namespace Protocol {

constexpr std::string_view HEADER = "CHAT";
constexpr int HEADER_SIZE = 4;
constexpr int MAX_MESSAGE_SIZE = 65536;

enum class LoginRequestType {
  LOGIN = 0,
  REGISTER = 1,
  LOGOUT = 2,
  ERROR = 100
};

enum class HomeRequestType {
  CREATE_ROOM = 0,
  JOIN_ROOM = 1,
  LEAVE_ROOM = 2,
  LIST_ROOMS = 3,
  SEND_MESSAGE = 4,
  //  HEARTBEAT = 5,
  EDIT_PROFILE = 6,
  SET_READY = 7,
  ERROR = 100
};

enum Code : int {
  // Bitmask: low bits indicate status, higher bits carry detail flags.
  SUCCESS = 1,
  FAIL = 1 << 1,
  ERROR = 1 << 2,

  // Lower bits are status flags, higher bits are detail flags.
  TIME_OUT = 1 << 3,
  DESERIALIZE_FAIL = 1 << 4,
  CONNECTION_ERROR = 1 << 5,
  BAD_REQUEST = 1 << 6,
  NOT_FOUND = 1 << 7,
  ROOM_STATE_ERROR = 1 << 8,

  SERVICE_SUCCESS = SUCCESS,
  SERVICE_FAIL = FAIL,
  SYSTEM_ERROR = ERROR
};

struct PlayerBasicInfo {
  std::string uid;
  std::string name;
  int color = -1;

  NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(PlayerBasicInfo, uid, name, color)
};

struct PlayerData {
  PlayerBasicInfo basicInfo;

  NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(PlayerData, basicInfo)
};

struct RoomInfo {
  int roomId;
  size_t maximumPeople;
  std::vector<PlayerBasicInfo> basicInfos;
  std::vector<std::string> readyUids;

  NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(RoomInfo, roomId, maximumPeople,
                                              basicInfos, readyUids)
};

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

  static constexpr std::array<CodeMessageEntry, 4> CODE_MESSAGE_TABLE{{
      {Protocol::NOT_FOUND, "not found"},
      {Protocol::ROOM_STATE_ERROR, "room state error"},
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

struct EmptyRsp {};

struct RegisterRsp {
  std::string uid;

  NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(RegisterRsp, uid)
};

struct LoginRsp {
  PlayerData playerData;

  NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(LoginRsp, playerData)
};

struct CreateRoomRsp {
  int roomId = -1;

  NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(CreateRoomRsp, roomId)
};

struct JoinRoomRsp {
  RoomInfo roomInfo;

  NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(JoinRoomRsp, roomInfo)
};

struct SetReadyRsp {
  RoomInfo roomInfo;

  NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(SetReadyRsp, roomInfo)
};

struct ListRoomsRsp {
  std::vector<RoomInfo> roomInfos;

  NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(ListRoomsRsp, roomInfos)
};

struct SendMessagePush {
  PlayerBasicInfo info;
  int roomId = -1;
  std::string content;

  NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(SendMessagePush, info, roomId,
                                              content)
};
} // namespace Protocol
