#include "channel.h"

#include <cstddef>
#include <iostream>
#include <memory>
#include <mutex>
#include <nlohmann/json.hpp>
#include <string>
#include <system_error>

#include <asio/async_result.hpp>
#include <asio/awaitable.hpp>
#include <asio/redirect_error.hpp>
#include <asio/use_awaitable.hpp>
#include <asio/write.hpp>

#include "error.h"
#include "protocol.h"
#include "room.h"
#include "server.h"
#include "user.h"

using json = nlohmann::json;

namespace {

constexpr char FRAME_DELIMITER = '\n';

} // namespace

asio::awaitable<bool> Channel::send_message(const std::string &msg) {
  if (msg.empty() || msg.size() > Protocol::MAX_MESSAGE_SIZE) {
    co_return false;
  }

  std::string frame = msg;
  if (frame.back() != FRAME_DELIMITER) {
    frame.push_back(FRAME_DELIMITER);
  }

  if (frame.size() > Protocol::MAX_MESSAGE_SIZE + 1) {
    co_return false;
  }

  std::error_code ec;
  co_await asio::async_write(socket, asio::buffer(frame),
                             asio::redirect_error(asio::use_awaitable, ec));
  co_return !ec;
}

asio::awaitable<void> Channel::run() {
  std::string pending;

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

    pending.append(buf.data(), len);

    while (true) {
      const std::size_t delimPos = pending.find(FRAME_DELIMITER);
      if (delimPos == std::string::npos) {
        break;
      }

      if (delimPos == 0) {
        pending.erase(0, 1);
        continue;
      }

      if (delimPos > Protocol::MAX_MESSAGE_SIZE) {
        log("Invalid payload length: %zu\n", delimPos);
        if (user) {
          server->logout_user(user->get_uid());
        }
        co_return;
      }

      std::string msg = pending.substr(0, delimPos);
      if (!msg.empty() && msg.back() == '\r') {
        msg.pop_back();
      }
      std::cout << "Received: " << msg << std::endl;
      pending.erase(0, delimPos + 1);

      co_await handle_message(msg);
    }

    if (pending.size() > Protocol::MAX_MESSAGE_SIZE + 1) {
      log("Payload without delimiter is too large: %zu\n", pending.size());
      if (user) {
        server->logout_user(user->get_uid());
      }
      co_return;
    }
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
  if (!j.contains("type") || !j["type"].is_string()) {
    return Protocol::CommandType::ERROR;
  }
  return Protocol::command_type_from_string(j.value("type", "error"));
}

// handle REGISTER
Protocol::Envelope Channel::handle_register(const json &j) {
  auto req = j.get<Protocol::RegisterReq>();
  user = server->register_user(req.info, shared_from_this());

  Protocol::LoginRsp rsp;
  rsp.basicInfo = req.info;
  rsp.basicInfo.uid = user->get_uid();
  std::cout << "User " << user->get_uid() << " registered" << std::endl;
  return make_ok_env(Protocol::CommandType::REGISTER, json(rsp));
}

// handle LOGIN
Protocol::Envelope Channel::handle_login(const json &j) {
  auto req = j.get<Protocol::LoginReq>();
  auto loggedInUser = server->login_user(req.uid, shared_from_this());

  if (!loggedInUser) {
    return make_err_env(Protocol::CommandType::LOGIN, 1001, "uid not exists");
  }
  Protocol::LoginRsp rsp;
  rsp.basicInfo.uid = loggedInUser->get_uid();
  rsp.basicInfo.avatarType = loggedInUser->get_avatar_type();
  rsp.basicInfo.userName = loggedInUser->get_username();
  std::cout << "User " << req.uid << " logged in" << std::endl;
  return make_ok_env(Protocol::CommandType::LOGIN, json(rsp));
}

// handle CREATE_ROOM
Protocol::Envelope Channel::handle_create_room(const json &j) {
  auto req = j.get<Protocol::CreateRoomReq>();
  auto reqUser = server->get_user(req.uid);
  if (!reqUser) {
    return make_err_env(Protocol::CommandType::CREATE_ROOM, 2001,
                        "Not logged in");
  }
  if (req.roomName.empty()) {
    return make_err_env(Protocol::CommandType::CREATE_ROOM, 2002,
                        "Room name cannot be empty");
  }

  auto room = server->create_room(req.roomName, req.maximumPeople, reqUser);
  Protocol::CreateRoomRsp rsp;
  rsp.roomId = room->get_id();
  std::cout << "Room " << req.roomName << " created with id " << room->get_id()
            << std::endl;
  return make_ok_env(Protocol::CommandType::CREATE_ROOM, json(rsp));
}

// hanlde JOIN_ROOM
Protocol::Envelope Channel::handle_join_room(const json &j) {
  auto req = j.get<Protocol::JoinRoomReq>();
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
  room->collect_members_info(rsp.PlayerInfos);
  std::cout << "User " << req.uid << " joined room " << req.roomId << std::endl;
  return make_ok_env(Protocol::CommandType::JOIN_ROOM, json(rsp));
}

// handle LIST_ROOMS
Protocol::Envelope Channel::handle_list_rooms(const json &) {
  Protocol::ListRoomsRsp rsp;
  server->list_rooms(rsp.RoomInfos);
  return make_ok_env(Protocol::CommandType::LIST_ROOMS, json(rsp));
}

Protocol::Envelope Channel::handle_leave_room(const json &j) {
  auto req = j.get<Protocol::LeaveRoomReq>();
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
  bool sent = co_await send_message(json(responseEnv).dump());
  if (!sent) {
    log("Failed to send response\n");
    if (user) {
      server->logout_user(user->get_uid());
    }
    co_return;
  }
}