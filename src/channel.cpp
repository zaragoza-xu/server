#include "channel.h"

#include <cstddef>
#include <memory>
#include <nlohmann/json.hpp>
#include <string>
#include <system_error>

#include <asio/async_result.hpp>
#include <asio/awaitable.hpp>
#include <asio/redirect_error.hpp>
#include <asio/use_awaitable.hpp>
#include <asio/write.hpp>

#include "logging.h"
#include "protocol.h"
#include "room.h"
#include "server.h"
#include "user.h"

using json = nlohmann::json;

namespace {

constexpr char FRAME_DELIMITER = '\n';

} // namespace

asio::awaitable<bool> Channel::send_message(const std::string &msg) {
  // Enforce size limits and newline framing.
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
  // Accumulate data and split on newline delimiter.
  std::string pending;

  while (true) {
    std::error_code ec;
    std::size_t len = co_await socket.async_read_some(
        asio::buffer(buf), asio::redirect_error(asio::use_awaitable, ec));
    if (ec) {
      logging::log("Connection closed: {}", ec.message());
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
        logging::log("Invalid payload length: {}", delimPos);
        co_return;
      }

      std::string msg = pending.substr(0, delimPos);
      if (!msg.empty() && msg.back() == '\r') {
        msg.pop_back();
      }
      logging::log("Received: {}", msg);
      pending.erase(0, delimPos + 1);

      co_await handle_message(msg);
    }

    if (pending.size() > Protocol::MAX_MESSAGE_SIZE + 1) {
      logging::log("Payload without delimiter is too large: {}",
                   pending.size());
      co_return;
    }
  }
}

Protocol::Envelope Channel::make_ok_env(int code, const json &data) {
  Protocol::Envelope env;
  env.code = code;
  env.message = "ok";
  env.data = data;
  return env;
}

Protocol::Envelope Channel::make_err_env(int code, const std::string &message) {
  Protocol::Envelope env;
  env.code = code;
  env.message = message;
  env.data = json::object();
  return env;
}

// handle REGISTER
Protocol::Envelope Channel::handle_register(const json &j) {
  auto req = j.get<Protocol::LoginReq>();
  auto user = server->register_user();
  logging::log("User {} registered", user->get_uid());
  return make_ok_env(Protocol::SERVICE_SUCCESS,
                     json(Protocol::RegisterRsp{user->get_uid()}));
}

// handle LOGIN
Protocol::Envelope Channel::handle_login(const json &j) {
  auto req = j.get<Protocol::LoginReq>();
  auto reqUser = server->login_user(req.uid);

  if (!reqUser) {
    return make_err_env(Protocol::SERVICE_FAIL | Protocol::NOT_FOUND,
                        "uid not exists");
  }
  Protocol::LoginRsp rsp;
  rsp.basicInfo = reqUser->get_info();
  logging::log("User {} logged in", reqUser->get_uid());
  return make_ok_env(Protocol::SERVICE_SUCCESS, json(rsp));
}

// handle CREATE_ROOM
Protocol::Envelope Channel::handle_create_room(const json &j) {
  auto req = j.get<Protocol::CreateRoomReq>();
  auto reqUser = server->get_user(req.uid);
  if (!reqUser) {
    return make_err_env(Protocol::SERVICE_FAIL | Protocol::NOT_FOUND,
                        "Not logged in");
  }

  auto room = server->create_room(req.maximumPeople, reqUser);
  Protocol::CreateRoomRsp rsp;
  rsp.roomId = room->get_id();
  logging::log("Room {} created", room->get_id());
  return make_ok_env(Protocol::SERVICE_SUCCESS, json(rsp));
}

// hanlde JOIN_ROOM
Protocol::Envelope Channel::handle_join_room(const json &j) {
  auto req = j.get<Protocol::JoinRoomReq>();
  auto reqUser = server->get_user(req.uid);
  if (!reqUser)
    return make_err_env(Protocol::SERVICE_FAIL | Protocol::NOT_FOUND,
                        "Not logged in");

  auto room = server->get_room(req.roomId);
  if (!room)
    return make_err_env(Protocol::SERVICE_FAIL | Protocol::NOT_FOUND,
                        "Room does not exist");
  if (!server->join_room(room, reqUser))
    return make_err_env(Protocol::SERVICE_FAIL | Protocol::ROOM_STATE_ERROR,
                        "Failed to join room");

  Protocol::JoinRoomRsp rsp;
  room->collect_members_info(rsp.PlayerInfos);
  logging::log("User {} joined room {}", req.uid, req.roomId);
  return make_ok_env(Protocol::SERVICE_SUCCESS, json(rsp));
}

// handle LIST_ROOMS
Protocol::Envelope Channel::handle_list_rooms(const json &) {
  Protocol::ListRoomsRsp rsp;
  server->list_rooms(rsp.RoomInfos);
  return make_ok_env(Protocol::SERVICE_SUCCESS, json(rsp));
}

Protocol::Envelope Channel::handle_leave_room(const json &j) {
  auto req = j.get<Protocol::LeaveRoomReq>();
  auto reqUser = server->get_user(req.uid);
  if (!reqUser) {
    return make_err_env(Protocol::SERVICE_FAIL | Protocol::NOT_FOUND,
                        "Not logged in");
  }
  if (!reqUser->is_in_room()) {
    return make_err_env(Protocol::SERVICE_FAIL | Protocol::ROOM_STATE_ERROR,
                        "Not in any room");
  }

  const int roomId = reqUser->get_room_id();
  if (!server->leave_room(roomId, reqUser->get_uid())) {
    return make_err_env(Protocol::SERVICE_FAIL | Protocol::ROOM_STATE_ERROR,
                        "Failed to leave room");
  }

  logging::log("User {} left room", req.uid);
  return make_ok_env(Protocol::SERVICE_SUCCESS, json::object());
}

Protocol::Envelope Channel::handle_heartbeat(const json &j) {
  auto req = j.get<Protocol::HeartbeatReq>();

  std::shared_ptr<User> target = nullptr;
  if (!req.uid.empty()) {
    target = server->get_user(req.uid);
  }

  if (!target) {
    return make_err_env(Protocol::SERVICE_FAIL | Protocol::NOT_FOUND,
                        "uid not exists");
  }

  target->touch_heartbeat();
  return make_ok_env(Protocol::SERVICE_SUCCESS,
                     json{{"uid", target->get_uid()}});
}

// handle all
asio::awaitable<void> Channel::handle_message(std::string &msg) {
  // Parse, dispatch by type, and respond with a single envelope.
  Protocol::Envelope responseEnv =
      make_err_env(Protocol::SYSTEM_ERROR, "Unknown error");

  try {
    auto j = json::parse(msg);
    Protocol::CommandType type = j.value("type", Protocol::CommandType::ERROR);

    switch (type) {
    case Protocol::CommandType::LOGIN: {
      if (j.value("uid", "") == "")
        responseEnv = handle_register(j); // register if uid is empty
      else
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
    case Protocol::CommandType::HEARTBEAT: {
      responseEnv = handle_heartbeat(j);
      break;
    }
    default:
      responseEnv = make_err_env(Protocol::SERVICE_FAIL | Protocol::BAD_REQUEST,
                                 "Unknown command");
    }
  } catch (const std::exception &e) {
    responseEnv =
        make_err_env(Protocol::SYSTEM_ERROR | Protocol::DESERIALIZE_FAIL,
                     std::string(e.what()));
  }

  // Send response outside try-catch to avoid co_await issue
  bool sent = co_await send_message(json(responseEnv).dump());
  if (!sent) {
    logging::log("Failed to send response");
    co_return;
  }
}