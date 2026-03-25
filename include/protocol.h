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

// Command types
enum class CommandType {
  REGISTER,
  LOGIN,
  CREATE_ROOM,
  JOIN_ROOM,
  LEAVE_ROOM,
  LIST_ROOMS,
  SEND_MESSAGE,

  ERROR = 100
};

struct PlayerBasicInfo {
  std::string uid;
  std::string userName;
  int avatarType = -1;

  json to_json() const {
    json j;
    j["uid"] = uid;
    j["userName"] = userName;
    j["avatarType"] = avatarType;
    return j;
  }

  static PlayerBasicInfo from_json(const json &j) {
    PlayerBasicInfo info;
    info.uid = j.value("uid", "");
    info.userName = j.value("userName", "");
    info.avatarType = j.value("avatarType", -1);
    return info;
  }
};

struct Envelope {
  CommandType type = CommandType::ERROR;
  bool status = false;
  int errorCode = 0;
  std::string message;
  json data = json::object();

  json to_json() const {
    json j;
    j["type"] = static_cast<int>(type);
    j["status"] = status;
    j["errorCode"] = errorCode;
    j["message"] = message;
    j["data"] = data;
    return j;
  }

  static Envelope from_json(const json &j) {
    Envelope env;
    env.type = static_cast<CommandType>(j.value("type", 100));
    env.status = j.value("status", false);
    env.errorCode = j.value("errorCode", 0);
    env.message = j.value("message", "");
    if (j.contains("data") && j["data"].is_object()) {
      env.data = j["data"];
    } else {
      env.data = json::object();
    }

    return env;
  }
};

struct RegisterReq {
  PlayerBasicInfo info;

  static RegisterReq from_json(const json &j) {
    RegisterReq req;
    req.info = Protocol::PlayerBasicInfo::from_json(j);
    return req;
  }

  json to_json() const { return info.to_json(); }
};

struct LoginReq {
  std::string uid;

  static LoginReq from_json(const json &j) {
    LoginReq req;
    req.uid = j.value("uid", "");
    return req;
  }

  json to_json() const { return {{"uid", uid}}; }
};

struct CreateRoomReq {
  std::string uid;
  std::string roomName;
  int maximumPeople = -1;

  static CreateRoomReq from_json(const json &j) {
    CreateRoomReq req;
    req.uid = j.value("uid", "");
    req.roomName = j.value("roomName", "");
    req.maximumPeople = j.value("maximumPeople", -1);
    return req;
  }

  json to_json() const {
    return {{"roomName", roomName}, {"maximumPeople", maximumPeople}};
  }
};

struct JoinRoomReq {
  int roomId = -1;
  std::string uid;

  static JoinRoomReq from_json(const json &j) {
    JoinRoomReq req;
    req.uid = j.value("uid", "");
    req.roomId = j.value("roomId", -1);
    return req;
  }

  json to_json() const { return {{"roomId", roomId}}; }
};

struct LeaveRoomReq {
  std::string uid;
  static LeaveRoomReq from_json(const json &j) {
    LeaveRoomReq req;
    req.uid = j.value("uid", "");
    return req;
  }

  json to_json() const { return json::object(); }
};

struct ListRoomsReq {
  static ListRoomsReq from_json(const json &) { return {}; }

  json to_json() const { return json::object(); }
};

struct SendMessageReq {
  std::string content;

  static SendMessageReq from_json(const json &j) {
    SendMessageReq req;
    req.content = j.value("content", "");
    return req;
  }

  json to_json() const { return {{"content", content}}; }
};

struct LoginRsp {
  PlayerBasicInfo info;

  json to_json() const { return info.to_json(); }
};

struct CreateRoomRsp {
  //  PlayerBasicInfo info;
  int roomId = -1;
  //  std::string roomName;

  json to_json() const {
    //    json j = info.to_json();
    json j;
    j["roomId"] = roomId;
    //    j["roomName"] = roomName;
    return j;
  }
};

struct JoinRoomRsp {
  std::vector<PlayerBasicInfo> infos;
  //  int roomId = -1;
  //  std::string roomName;

  json to_json() const {
    json j;
    for (auto info : infos)
      j["PlayerBasicInfos"].emplace_back(info.to_json());
    //    j["roomId"] = roomId;
    //    j["roomName"] = roomName;
    return j;
  }
};

struct LeaveRoomRsp {
  PlayerBasicInfo info;
  int roomId = -1;

  json to_json() const {
    json j = info.to_json();
    j["roomId"] = roomId;
    return j;
  }
};

struct RoomInfo {
  int roomId;
  int maximumPeople;
  size_t peopleCount;

  json to_json() const {
    json j;
    j["roomId"] = roomId;
    j["maximumPeople"] = maximumPeople;
    j["peopleCount"] = peopleCount;
    return j;
  }
};

struct ListRoomsRsp {
  std::vector<RoomInfo> RoomInfos;

  json to_json() const {
    json j;
    for (auto RoomInfo : RoomInfos)
      j["RoomInfos"].emplace_back(RoomInfo.to_json());
    return j;
  }
};

struct SendMessagePush {
  PlayerBasicInfo info;
  int roomId = -1;
  std::string content;

  json to_json() const {
    json j = info.to_json();
    j["roomId"] = roomId;
    j["content"] = content;
    return j;
  }
};

} // namespace Protocol
