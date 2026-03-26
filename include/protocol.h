#pragma once

#include <cstddef>
#include <nlohmann/json.hpp>
#include <string>
#include <string_view>
#include <vector>

using json = nlohmann::json;

// Protocol definitions using JSON format
namespace Protocol {

constexpr std::string_view HEADER = "CHAT";
constexpr int HEADER_SIZE = 4;
constexpr int MAX_MESSAGE_SIZE = 65536;

// Single source of truth for command names.
#define PROTOCOL_COMMAND_TYPE_TABLE(X) \
  X(REGISTER, "register")             \
  X(LOGIN, "login")                   \
  X(CREATE_ROOM, "create_room")       \
  X(JOIN_ROOM, "join_room")           \
  X(LEAVE_ROOM, "leave_room")         \
  X(LIST_ROOMS, "list_rooms")         \
  X(SEND_MESSAGE, "send_message")

enum class CommandType {
#define X(name, str) name,
  PROTOCOL_COMMAND_TYPE_TABLE(X)
#undef X
  ERROR = 100
};

inline std::string command_type_to_string(CommandType type) {
  switch (type) {
#define X(name, str)   \
  case CommandType::name: \
    return str;
  
  PROTOCOL_COMMAND_TYPE_TABLE(X)

#undef X
  case CommandType::ERROR:
  default:
    return "error";
  }
}

inline CommandType command_type_from_string(const std::string &type) {
#define X(name, str) \
  if (type == str)   \
    return CommandType::name;
  
  PROTOCOL_COMMAND_TYPE_TABLE(X)

#undef X
  if (type == "error")
    return CommandType::ERROR;
  return CommandType::ERROR;
}

inline void to_json(json &j, const CommandType &type) {
  j = command_type_to_string(type);
}

inline void from_json(const json &j, CommandType &type) {
  if (!j.is_string()) {
    type = CommandType::ERROR;
    return;
  }
  type = command_type_from_string(j.get<std::string>());
}

struct PlayerBasicInfo {
  std::string uid;
  std::string userName;
  int avatarType = -1;

  NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(PlayerBasicInfo, uid, userName,
                                              avatarType)
};

struct Envelope {
  CommandType type = CommandType::ERROR;
  bool status = false;
  int errorCode = 0;
  std::string message;
  json data = json::object();

  NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(Envelope, type, status,
                                              errorCode, message, data)
};

struct RegisterReq {
  PlayerBasicInfo info;

  NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(RegisterReq, info)
};

struct LoginReq {
  std::string uid;

  NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(LoginReq, uid)
};

struct CreateRoomReq {
  std::string uid;
  std::string roomName;
  int maximumPeople = -1;

  NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(CreateRoomReq, uid, roomName,
                                              maximumPeople)
};

struct JoinRoomReq {
  int roomId = -1;
  std::string uid;

  NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(JoinRoomReq, roomId, uid)
};

struct LeaveRoomReq {
  std::string uid;

  NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(LeaveRoomReq, uid)
};

struct ListRoomsReq {

};

struct SendMessageReq {
  std::string content;

  NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(SendMessageReq, content)
};

struct LoginRsp {
  PlayerBasicInfo info;

  NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(LoginRsp, info)
};

struct CreateRoomRsp {
  int roomId = -1;

  NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(CreateRoomRsp, roomId)
};

struct JoinRoomRsp {
  std::vector<PlayerBasicInfo> PlayerBasicInfos;

  NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(JoinRoomRsp, PlayerBasicInfos)
};

struct LeaveRoomRsp {
  PlayerBasicInfo info;
  int roomId = -1;

  NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(LeaveRoomRsp, info, roomId)
};

struct RoomInfo {
  int roomId;
  int maximumPeople;
  size_t peopleCount;

  NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(RoomInfo, roomId, maximumPeople,
                                              peopleCount)
};

struct ListRoomsRsp {
  std::vector<RoomInfo> RoomInfos;

  NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(ListRoomsRsp, RoomInfos)
};

struct SendMessagePush {
  PlayerBasicInfo info;
  int roomId = -1;
  std::string content;

  NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(SendMessagePush, info, roomId,
                                              content)
};

#undef PROTOCOL_COMMAND_TYPE_TABLE

} // namespace Protocol
