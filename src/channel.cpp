#include <asio/async_result.hpp>
#include <asio/awaitable.hpp>
#include <asio/redirect_error.hpp>
#include <asio/use_awaitable.hpp>
#include <asio/write.hpp>
#include <cstddef>
#include <iostream>
#include <memory>
#include <nlohmann/json.hpp>
#include <string>
#include <system_error>

#include "channel.h"
#include "error.h"
#include "protocol.h"
#include "room.h"
#include "server.h"
#include "user.h"

using json = nlohmann::json;

asio::awaitable<bool> Channel::send_message(const std::string &msg) {
  std::error_code ec;
  co_await asio::async_write(socket, asio::buffer(msg),
                             asio::redirect_error(asio::use_awaitable, ec));
  co_return !ec;
}

asio::awaitable<void> Channel::run() {
  while (true) {
    std::error_code ec;
    std::size_t len = co_await socket.async_read_some(
        asio::buffer(buf), asio::redirect_error(asio::use_awaitable, ec));
    if (ec) {
      log("Connection closed: %s\n", ec.message());
      if (user) {
        server->logout_user(user->get_uid());
      }
      co_return;
    }

    std::string msg(buf.data(), len);
    std::cout << "Received: " << msg << std::endl;

    co_await handle_message(msg);
  }
}

Protocol::Envelope Channel::make_ok_env(Protocol::CommandType type,
                                        const json &data) {
  Protocol::Envelope env;
  env.type = type;
  env.status = true;
  env.errorCode = 0;
  env.message = "ok";
  env.data = data;
  return env;
}

Protocol::Envelope Channel::make_err_env(Protocol::CommandType type,
                                         int errorCode,
                                         const std::string &message) {
  Protocol::Envelope env;
  env.type = type;
  env.status = false;
  env.errorCode = errorCode;
  env.message = message;
  env.data = json::object();
  return env;
}

Protocol::CommandType Channel::parse_command_type(const json &j) {
  if (j.contains("type") && j["type"].is_string()) {
    const std::string type = j.value("type", "");
    if (type == "register")
      return Protocol::CommandType::REGISTER;
    if (type == "login")
      return Protocol::CommandType::LOGIN;
    if (type == "create_room")
      return Protocol::CommandType::CREATE_ROOM;
    if (type == "join_room")
      return Protocol::CommandType::JOIN_ROOM;
    if (type == "leave_room")
      return Protocol::CommandType::LEAVE_ROOM;
    if (type == "list_rooms")
      return Protocol::CommandType::LIST_ROOMS;
    if (type == "send_message")
      return Protocol::CommandType::SEND_MESSAGE;
  }
  return Protocol::CommandType::ERROR;
}

// handle REGISTER
Protocol::Envelope Channel::handle_register(const json &j) {
  Protocol::RegisterReq req = Protocol::RegisterReq::from_json(j);
  user = server->register_user(req.info, shared_from_this());

  Protocol::LoginRsp rsp;
  rsp.info = req.info;
  rsp.info.uid = user->get_uid();
  std::cout << "User " << user->get_uid() << " registered" << std::endl;
  return make_ok_env(Protocol::CommandType::LOGIN, rsp.to_json());
}

// handle LOGIN
Protocol::Envelope Channel::handle_login(const json &j) {
  Protocol::LoginReq req = Protocol::LoginReq::from_json(j);
  auto loggedInUser = server->login_user(req.uid, shared_from_this());

  Protocol::LoginRsp rsp;
  rsp.info.uid = loggedInUser->get_uid();
  rsp.info.avatarType = loggedInUser->get_avatar_type();
  rsp.info.userName = loggedInUser->get_username();
  std::cout << "User " << req.uid << " logged in" << std::endl;
  return make_ok_env(Protocol::CommandType::LOGIN, rsp.to_json());
}

// handle CREATE_ROOM
Protocol::Envelope Channel::handle_create_room(const json &j) {
  Protocol::CreateRoomReq req = Protocol::CreateRoomReq::from_json(j);
  auto reqUser = server->get_user(req.uid);
  if (!reqUser) {
    return make_err_env(Protocol::CommandType::CREATE_ROOM, 2001,
                        "Not logged in");
  }
  if (req.roomName.empty()) {
    return make_err_env(Protocol::CommandType::CREATE_ROOM, 2002,
                        "Room name cannot be empty");
  }

  auto room = server->create_room(req.roomName, reqUser);
  Protocol::CreateRoomRsp rsp;
  rsp.roomId = room->get_id();
  std::cout << "Room " << req.roomName << " created with id " << room->get_id()
            << std::endl;
  return make_ok_env(Protocol::CommandType::CREATE_ROOM, rsp.to_json());
}

// hanlde JOIN_ROOM
Protocol::Envelope Channel::handle_join_room(const json &j) {
  Protocol::JoinRoomReq req = Protocol::JoinRoomReq::from_json(j);
  auto reqUser = server->get_user(req.uid);
  if (!reqUser) {
    return make_err_env(Protocol::CommandType::JOIN_ROOM, 3001,
                        "Not logged in");
  }

  auto room = server->get_room(req.roomId);
  if (!room) {
    return make_err_env(Protocol::CommandType::JOIN_ROOM, 3003,
                        "Room does not exist");
  }
  if (!server->join_room(room, reqUser)) {
    return make_err_env(Protocol::CommandType::JOIN_ROOM, 3004,
                        "Failed to join room");
  }

  Protocol::JoinRoomRsp rsp;
  for (const auto &[memberUid, member] : room->get_members()) {
    Protocol::PlayerBasicInfo info;
    info.uid = memberUid;
    info.userName = member->get_username();
    info.avatarType = member->get_avatar_type();
    rsp.infos.push_back(info);
  }
  std::cout << "User " << req.uid << " joined room " << req.roomId << std::endl;
  return make_ok_env(Protocol::CommandType::JOIN_ROOM, rsp.to_json());
}

// handle LIST_ROOMS
Protocol::Envelope Channel::handle_list_rooms(const json &) {
  Protocol::ListRoomsRsp rsp;
  auto rooms = server->list_rooms();
  rsp.RoomInfos.reserve(rooms.size());
  for (const auto &room : rooms) {
    Protocol::RoomInfo roomInfo;
    roomInfo.roomId = room->get_id();
    roomInfo.maximumPeople = -1;
    roomInfo.peopleCount = room->get_member_count();
    rsp.RoomInfos.push_back(roomInfo);
  }
  return make_ok_env(Protocol::CommandType::LIST_ROOMS, rsp.to_json());
}

Protocol::Envelope Channel::handle_leave_room(const json &j) {
  Protocol::LeaveRoomReq req = Protocol::LeaveRoomReq::from_json(j);
  auto reqUser = server->get_user(req.uid);
  if (!reqUser) {
    return make_err_env(Protocol::CommandType::LEAVE_ROOM, 4001,
                        "Not logged in");
  }
  if (!reqUser->is_in_room()) {
    return make_err_env(Protocol::CommandType::LEAVE_ROOM, 4002,
                        "Not in any room");
  }

  const int roomId = reqUser->get_room_id();
  if (!server->leave_room(roomId, reqUser->get_uid())) {
    return make_err_env(Protocol::CommandType::LEAVE_ROOM, 4003,
                        "Failed to leave room");
  }

  std::cout << "User " << req.uid << " left room" << std::endl;
  return make_ok_env(Protocol::CommandType::LEAVE_ROOM, json::object());
}

// handle all
asio::awaitable<void> Channel::handle_message(std::string &msg) {
  Protocol::Envelope responseEnv =
      make_err_env(Protocol::CommandType::ERROR, -1, "Unknown error");

  try {
    auto j = json::parse(msg);
    Protocol::CommandType type = parse_command_type(j);

    switch (type) {
    case Protocol::CommandType::REGISTER: {
      responseEnv = handle_register(j);
      break;
    }
    case Protocol::CommandType::LOGIN: {
      responseEnv = handle_login(j);
      break;
    }
    case Protocol::CommandType::CREATE_ROOM: {
      responseEnv = handle_create_room(j);
      break;
    }
    case Protocol::CommandType::JOIN_ROOM: {
      responseEnv = handle_join_room(j);
      break;
    }
    case Protocol::CommandType::LIST_ROOMS: {
      responseEnv = handle_list_rooms(j);
      break;
    }
    case Protocol::CommandType::LEAVE_ROOM: {
      responseEnv = handle_leave_room(j);
      break;
    }
    default:
      responseEnv =
          make_err_env(Protocol::CommandType::ERROR, 9001, "Unknown command");
    }
  } catch (const std::exception &e) {
    responseEnv =
        make_err_env(Protocol::CommandType::ERROR, 9002, std::string(e.what()));
  }

  // Send response outside try-catch to avoid co_await issue
  bool sent = co_await send_message(responseEnv.to_json().dump() + "\n");
  if (!sent) {
    log("Failed to send response\n");
    if (user) {
      server->logout_user(user->get_uid());
    }
    co_return;
  }
}