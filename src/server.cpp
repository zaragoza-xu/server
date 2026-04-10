#include "server.h"

#include <algorithm>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include <asio/awaitable.hpp>
#include <asio/co_spawn.hpp>
#include <asio/detached.hpp>
#include <asio/ip/tcp.hpp>
#include <asio/redirect_error.hpp>
#include <asio/use_awaitable.hpp>

#include "channel.h"
#include "logging.h"
#include "protocol.h"
#include "room.h"
#include "user.h"

using namespace asio::ip;

Server::Server(asio::io_context &context, int port,
               std::shared_ptr<ServerState> sharedState)
    : ioContext(context), port(port),
      acceptor(context, tcp::endpoint(tcp::v4(), port)),
      state(std::move(sharedState)) {
  if (!state) {
    state = std::make_shared<ServerState>();
  }

  asio::co_spawn(ioContext, accept_loop(), asio::detached);
}

Protocol::Envelope Server::dispatch_request(const json &) {
  logging::log("[dispatch][base] reached fallback dispatcher");
  return Protocol::Envelope::make_env(Protocol::SERVICE_FAIL |
                                      Protocol::BAD_REQUEST);
}

asio::awaitable<void> Server::accept_loop() {
  while (true) {
    auto chl = std::make_shared<Channel>(ioContext, shared_from_this());

    std::error_code ec;
    co_await acceptor.async_accept(
        chl->getSocket(), asio::redirect_error(asio::use_awaitable, ec));

    if (ec) {
      logging::log("{}", ec.message());
      continue;
    }
    logging::log("accepted connection");
    asio::co_spawn(
        ioContext, [chl]() -> asio::awaitable<void> { co_await chl->run(); },
        asio::detached);
  }
}

int Server::register_user(const Protocol::RegisterReq &req,
                          Protocol::RegisterRsp &rsp) {
  std::scoped_lock lock(state->usersMutex, state->userInfosMutex);

  Protocol::PlayerBasicInfo storedInfo = {"", "", 0};
  storedInfo.uid = std::to_string(state->nextUid++);
  state->userInfos.emplace(storedInfo.uid, storedInfo);

  auto user = std::make_shared<User>(storedInfo);
  state->users[storedInfo.uid] = user;
  rsp.uid = storedInfo.uid;
  return Protocol::SERVICE_SUCCESS;
}

int Server::login_user(const Protocol::LoginReq &req, Protocol::LoginRsp &rsp) {
  std::scoped_lock lock(state->usersMutex, state->userInfosMutex);

  if (req.uid == "") {
    return Protocol::SERVICE_FAIL | Protocol::BAD_REQUEST;
  }
  auto infoIt = state->userInfos.find(req.uid);
  if (infoIt == state->userInfos.end()) {
    return Protocol::SERVICE_FAIL | Protocol::NOT_FOUND;
  }
  auto onlineIt = state->users.find(req.uid);
  if (onlineIt != state->users.end()) {
    return Protocol::SERVICE_FAIL | Protocol::BAD_REQUEST;
  }

  auto user = std::make_shared<User>(infoIt->second);
  state->users.emplace(req.uid, user);
  rsp.playerData.basicInfo = user->get_info();
  return Protocol::SERVICE_SUCCESS;
}

void Server::logout_user(const std::string &uid) {
  std::shared_ptr<User> user;
  std::scoped_lock lock(state->usersMutex, state->roomsMutex);
  auto it = state->users.find(uid);
  if (it == state->users.end()) {
    return;
  }
  user = it->second;
  state->users.erase(it);

  if (user && user->is_in_room()) {
    const int room_id = user->get_room_id();
    auto it = state->rooms.find(room_id);
    if (it == state->rooms.end()) {
      return;
    }
    auto room = it->second;
    if (!room->is_member(uid)) {
      return;
    }

    room->remove_member(uid);
    user->set_room_id(-1);
    if (room->get_people_count() == 0) {
      state->rooms.erase(it);
    }
  }
}

std::shared_ptr<User> Server::get_user(const std::string &uid) const {
  std::lock_guard<std::mutex> lock(state->usersMutex);
  auto it = state->users.find(uid);
  if (it != state->users.end()) {
    return it->second;
  }
  return nullptr;
}

bool Server::user_exists(const std::string &uid) const {
  std::lock_guard<std::mutex> lock(state->userInfosMutex);
  return state->userInfos.count(uid) > 0;
}

int Server::create_room(const Protocol::CreateRoomReq &req,
                        Protocol::CreateRoomRsp &rsp) {
  std::scoped_lock lock(state->usersMutex, state->roomsMutex);
  auto userIt = state->users.find(req.uid);
  if (userIt == state->users.end() || !userIt->second) {
    return Protocol::SERVICE_FAIL | Protocol::NOT_FOUND;
  }
  auto user = userIt->second;
  if (user->is_in_room()) {
    return Protocol::SERVICE_FAIL | Protocol::ROOM_STATE_ERROR;
  }

  int room_id = state->nextRoomId++;
  auto room = std::make_shared<Room>(room_id, req.maximumPeople, user);
  state->rooms.emplace(room_id, room);
  user->set_room_id(room_id);
  rsp.roomId = room_id;
  return Protocol::SERVICE_SUCCESS;
}

int Server::join_room(const Protocol::JoinRoomReq &req,
                      Protocol::JoinRoomRsp &rsp) {
  std::scoped_lock lock(state->usersMutex, state->roomsMutex);

  auto roomIt = state->rooms.find(req.roomId);
  if (roomIt == state->rooms.end()) {
    return Protocol::SERVICE_FAIL | Protocol::NOT_FOUND;
  }
  auto userIt = state->users.find(req.uid);
  if (userIt == state->users.end() || !userIt->second) {
    return Protocol::SERVICE_FAIL | Protocol::NOT_FOUND;
  }

  auto user = userIt->second;
  auto room = roomIt->second;
  if (user->is_in_room()) {
    return Protocol::SERVICE_FAIL | Protocol::ROOM_STATE_ERROR;
  }

  bool success = room->add_member(user);
  if (!success) {
    return Protocol::SERVICE_FAIL | Protocol::ROOM_STATE_ERROR;
  }
  user->set_room_id(room->get_id());
  rsp.roomInfo = room->get_info();
  return Protocol::SERVICE_SUCCESS;
}

int Server::leave_room(const Protocol::LeaveRoomReq &req,
                       Protocol::EmptyRsp &) {
  std::scoped_lock lock(state->usersMutex, state->roomsMutex);

  auto userIt = state->users.find(req.uid);
  if (userIt == state->users.end() || !userIt->second) {
    return Protocol::SERVICE_FAIL | Protocol::NOT_FOUND;
  }
  auto member = userIt->second;
  if (!member->is_in_room()) {
    return Protocol::SERVICE_FAIL | Protocol::ROOM_STATE_ERROR;
  }

  const int room_id = member->get_room_id();
  auto it = state->rooms.find(room_id);
  if (it == state->rooms.end()) {
    return Protocol::SERVICE_FAIL | Protocol::NOT_FOUND;
  }
  auto room = it->second;
  if (!room->is_member(req.uid)) {
    return Protocol::SERVICE_FAIL | Protocol::ROOM_STATE_ERROR;
  }

  room->remove_member(req.uid);
  member->set_room_id(-1);

  if (room->get_people_count() == 0) {
    state->rooms.erase(it);
  }
  return Protocol::SERVICE_SUCCESS;
}

int Server::list_rooms(const Protocol::ListRoomsReq &,
                       Protocol::ListRoomsRsp &rsp) {
  std::lock_guard<std::mutex> lock(state->roomsMutex);
  rsp.roomInfos.clear();
  rsp.roomInfos.reserve(state->rooms.size());
  for (const auto &[id, room] : state->rooms) {
    rsp.roomInfos.push_back(room->get_info());
  }
  return Protocol::SERVICE_SUCCESS;
}

Protocol::Envelope LoginServer::dispatch_request(const json &request) {
  logging::log("[dispatch][login] request={}", request.dump());
  const auto type = static_cast<Protocol::LoginRequestType>(request.value(
      "type", static_cast<int>(Protocol::LoginRequestType::ERROR)));
  const auto it = std::find_if(
      COMMAND_TABLE.begin(), COMMAND_TABLE.end(),
      [type](const CommandDescriptor &entry) { return entry.type == type; });
  if (it == COMMAND_TABLE.end()) {
    logging::log("[dispatch][login] unknown type={}", static_cast<int>(type));
    return Protocol::Envelope::make_env(Protocol::SERVICE_FAIL |
                                        Protocol::BAD_REQUEST);
  }
  const auto env = it->dispatch(*this, request);
  logging::log("[dispatch][login] type={} code={} message={}",
               static_cast<int>(type), env.code, env.message);
  return env;
}

Protocol::Envelope HomeServer::dispatch_request(const json &request) {
  logging::log("[dispatch][home] request={}", request.dump());
  const auto type = static_cast<Protocol::HomeRequestType>(request.value(
      "type", static_cast<int>(Protocol::HomeRequestType::ERROR)));
  const auto it = std::find_if(
      COMMAND_TABLE.begin(), COMMAND_TABLE.end(),
      [type](const CommandDescriptor &entry) { return entry.type == type; });
  if (it == COMMAND_TABLE.end()) {
    logging::log("[dispatch][home] unknown type={}", static_cast<int>(type));
    return Protocol::Envelope::make_env(Protocol::SERVICE_FAIL |
                                        Protocol::BAD_REQUEST);
  }
  const auto env = it->dispatch(*this, request);
  logging::log("[dispatch][home] type={} code={} message={}",
               static_cast<int>(type), env.code, env.message);
  return env;
}
