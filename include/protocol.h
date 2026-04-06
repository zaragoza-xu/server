#pragma once

#include <cstddef>
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

enum class CommandType : int {
  // Numeric command identifiers used in the JSON "type" field.
  LOGIN,
  CREATE_ROOM,
  JOIN_ROOM,
  LEAVE_ROOM,
  LIST_ROOMS,
  SEND_MESSAGE,
  HEARTBEAT,
  EDIT_PROFILE,
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
  std::string userName;
  int avatarType = -1;

  NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(PlayerBasicInfo, uid, userName,
                                              avatarType)
};

struct Envelope {
  // code/message describe status; data carries command-specific payload.
  int code = 0;
  std::string message;
  json data = json::object();

  NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(Envelope, code, message, data)
};

struct LoginReq {
  Protocol::CommandType type;
  std::string uid;

  NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(LoginReq, type, uid)
};

struct EditProfileReq {
  Protocol::CommandType type;
  PlayerBasicInfo basicInfo;

  NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(EditProfileReq, type, basicInfo);
};

struct CreateRoomReq {
  Protocol::CommandType type;
  std::string uid;
  size_t maximumPeople = 0;

  NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(CreateRoomReq, type, uid,
                                              maximumPeople)
};

struct JoinRoomReq {
  Protocol::CommandType type;
  int roomId = -1;
  std::string uid;

  NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(JoinRoomReq, type, roomId, uid)
};

struct LeaveRoomReq {
  Protocol::CommandType type;
  std::string uid;

  NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(LeaveRoomReq, type, uid)
};

struct ListRoomsReq {
  Protocol::CommandType type;
  NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(ListRoomsReq, type)
};

struct SendMessageReq {
  Protocol::CommandType type;
  std::string content;

  NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(SendMessageReq, type, content)
};

struct HeartbeatReq {
  Protocol::CommandType type;
  std::string uid;

  NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(HeartbeatReq, type, uid);
};

struct RegisterRsp {
  std::string uid;

  NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(RegisterRsp, uid)
};

struct LoginRsp {
  PlayerBasicInfo basicInfo;

  NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(LoginRsp, basicInfo)
};

struct CreateRoomRsp {
  int roomId = -1;

  NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(CreateRoomRsp, roomId)
};

struct JoinRoomRsp {
  std::vector<PlayerBasicInfo> playerInfos;

  NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(JoinRoomRsp, playerInfos)
};

struct LeaveRoomRsp {
  // PlayerBasicInfo info;
  // int roomId = -1;

  // NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(LeaveRoomRsp, info, roomId)
};

struct RoomInfo {
  int roomId;
  size_t maximumPeople;
  size_t peopleCount;

  NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(RoomInfo, roomId, maximumPeople,
                                              peopleCount)
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
