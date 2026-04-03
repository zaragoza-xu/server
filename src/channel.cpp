#include "channel.h"

#include <algorithm>
#include <array>
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

struct CodeMessageEntry {
  int mask;
  const char *message;
};

constexpr std::array<CodeMessageEntry, 5> CODE_MESSAGE_TABLE{{
    {Protocol::NOT_FOUND, "not found"},
    {Protocol::ROOM_STATE_ERROR, "room state error"},
    {Protocol::BAD_REQUEST, "bad request"},
    {Protocol::DESERIALIZE_FAIL, "deserialize failed"},
}};

std::string map_message_from_code(int code) {
  if (code == Protocol::SERVICE_SUCCESS)
    return "ok";
  for (const auto &entry : CODE_MESSAGE_TABLE) {
    if ((code & entry.mask) != 0) {
      return entry.message;
    }
  }
  return "error";
}

template <typename Req, Protocol::Envelope (Channel::*Method)(const Req &)>
Protocol::Envelope dispatch_typed(Channel &self, const json &j) {
  const auto req = j.get<Req>();
  return (self.*Method)(req);
}
} // namespace

const Channel::CommandTable &Channel::command_table() {
  static const CommandTable table{{
      {Protocol::CommandType::LOGIN,
       &dispatch_typed<Protocol::LoginReq, &Channel::on_login>},
      {Protocol::CommandType::CREATE_ROOM,
       &dispatch_typed<Protocol::CreateRoomReq, &Channel::on_create_room>},
      {Protocol::CommandType::JOIN_ROOM,
       &dispatch_typed<Protocol::JoinRoomReq, &Channel::on_join_room>},
      {Protocol::CommandType::LIST_ROOMS,
       &dispatch_typed<Protocol::ListRoomsReq, &Channel::on_list_rooms>},
      {Protocol::CommandType::LEAVE_ROOM,
       &dispatch_typed<Protocol::LeaveRoomReq, &Channel::on_leave_room>},
      {Protocol::CommandType::HEARTBEAT,
       &dispatch_typed<Protocol::HeartbeatReq, &Channel::on_heartbeat>},
  }};
  return table;
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
      if (delimPos == std::string::npos)
        break;
      if (delimPos == 0) {
        pending.erase(0, 1);
        continue;
      }
      if (delimPos > Protocol::MAX_MESSAGE_SIZE) {
        logging::log("Invalid payload length: {}", delimPos);
        co_return;
      }

      std::string msg = pending.substr(0, delimPos);
      if (!msg.empty() && msg.back() == '\r')
        msg.pop_back();
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

Protocol::Envelope Channel::make_env(int code, const json &data) {
  Protocol::Envelope env;
  env.code = code;
  env.message = map_message_from_code(code);
  env.data = data;
  return env;
}

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

// ----------------------------------------------------------------------

Protocol::Envelope Channel::on_register(const Protocol::LoginReq &req) {
  if (!req.uid.empty()) {
    return make_env(Protocol::SERVICE_FAIL | Protocol::BAD_REQUEST);
  }

  Protocol::RegisterRsp rsp;
  const int code = server->register_user(rsp);
  return make_env(code, json(rsp));
}

Protocol::Envelope Channel::on_login(const Protocol::LoginReq &req) {
  if (req.uid.empty()) {
    return on_register(req);
  }
  Protocol::LoginRsp rsp;
  const int code = server->login_user(req.uid, rsp);
  return make_env(code, code == Protocol::SERVICE_SUCCESS ? json(rsp)
                                                          : json::object());
}

Protocol::Envelope Channel::on_create_room(const Protocol::CreateRoomReq &req) {
  Protocol::CreateRoomRsp rsp;
  const int code = server->create_room(req.maximumPeople, req.uid, rsp);
  return make_env(code, code == Protocol::SERVICE_SUCCESS ? json(rsp)
                                                          : json::object());
}

Protocol::Envelope Channel::on_join_room(const Protocol::JoinRoomReq &req) {
  Protocol::JoinRoomRsp rsp;
  const int code = server->join_room(req.roomId, req.uid, rsp);
  return make_env(code, code == Protocol::SERVICE_SUCCESS ? json(rsp)
                                                          : json::object());
}

Protocol::Envelope Channel::on_list_rooms(const Protocol::ListRoomsReq &) {
  Protocol::ListRoomsRsp rsp;
  const int code = server->list_rooms(rsp);
  return make_env(code, code == Protocol::SERVICE_SUCCESS ? json(rsp)
                                                          : json::object());
}

Protocol::Envelope Channel::on_leave_room(const Protocol::LeaveRoomReq &req) {
  const int code = server->leave_room(req.uid);
  return make_env(code);
}

Protocol::Envelope Channel::on_heartbeat(const Protocol::HeartbeatReq &req) {
  const int code = server->heartbeat(req.uid);
  return make_env(code, code == Protocol::SERVICE_SUCCESS
                            ? json{{"uid", req.uid}}
                            : json::object());
}

// Parse, dispatch by type, and respond with a single envelope.
asio::awaitable<void> Channel::handle_message(std::string &msg) {
  Protocol::Envelope responseEnv = make_env(Protocol::SYSTEM_ERROR);

  try {
    const auto &commandTable = command_table();
    auto j = json::parse(msg);
    Protocol::CommandType type = j.value("type", Protocol::CommandType::ERROR);

    auto it = std::find_if(
        commandTable.begin(), commandTable.end(),
        [type](const CommandDescriptor &entry) { return entry.type == type; });

    if (it == commandTable.end())
      responseEnv = make_env(Protocol::SERVICE_FAIL | Protocol::BAD_REQUEST);
    else
      responseEnv = it->dispatch(*this, j);

  } catch (const std::exception &e) {
    logging::log("Parse or dispatch failed: {}", e.what());
    responseEnv = make_env(Protocol::SYSTEM_ERROR | Protocol::DESERIALIZE_FAIL);
  }

  // Send response outside try-catch to avoid co_await issue
  bool sent = co_await send_message(json(responseEnv).dump());
  if (!sent) {
    logging::log("Failed to send response");
    co_return;
  }
}