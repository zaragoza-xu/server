#include "server.h"
#include <algorithm>
#include <cstddef>
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

namespace {

struct CodeMessageEntry {
  int mask;
  const char *message;
};

constexpr std::array<CodeMessageEntry, 4> CODE_MESSAGE_TABLE{{
    {Protocol::NOT_FOUND, "not found"},
    {Protocol::ROOM_STATE_ERROR, "room state error"},
    {Protocol::BAD_REQUEST, "bad request"},
    {Protocol::DESERIALIZE_FAIL, "deserialize failed"},
}};

Protocol::Envelope make_env(int code, const json &data = json::object()) {
  Protocol::Envelope env;
  env.code = code;
  env.message = "error";
  if (code == Protocol::SERVICE_SUCCESS) {
    env.message = "ok";
  } else {
    for (const auto &entry : CODE_MESSAGE_TABLE) {
      if ((code & entry.mask) != 0) {
        env.message = entry.message;
        break;
      }
    }
  }
  env.data = data;
  return env;
}

} // namespace

Server::Server(asio::io_context &context, int port,
               std::shared_ptr<ServerState> sharedState)
    : ioContext(context), acceptor(context, tcp::endpoint(tcp::v4(), port)),
      heartbeatTimer(context), state(std::move(sharedState)), port(port) {
  if (!state) {
    state = std::make_shared<ServerState>();
  }

  // Start accepting connections immediately on construction.
  asio::co_spawn(ioContext, accept_loop(), asio::detached);
  asio::co_spawn(ioContext, heartbeat_monitor(), asio::detached);
}

Protocol::Envelope Server::dispatch_request(const json &) {
  return make_env(Protocol::SERVICE_FAIL | Protocol::BAD_REQUEST);
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

int Server::register_user(Protocol::RegisterRsp &rsp) {
  std::scoped_lock lock(state->usersMutex, state->userInfosMutex);

  Protocol::PlayerBasicInfo storedInfo = {"", "", 0};
  storedInfo.uid = std::to_string(state->nextUid++);
  state->userInfos.emplace(storedInfo.uid, storedInfo);

  auto user = std::make_shared<User>(storedInfo);
  state->users[storedInfo.uid] = user;
  rsp.uid = storedInfo.uid;
  return Protocol::SERVICE_SUCCESS;
}

int Server::login_user(const std::string &uid, Protocol::LoginRsp &rsp) {
  std::scoped_lock lock(state->usersMutex, state->userInfosMutex);

  auto infoIt = state->userInfos.find(uid);
  if (infoIt == state->userInfos.end()) {
    return Protocol::SERVICE_FAIL | Protocol::NOT_FOUND;
  }
  auto onlineIt = state->users.find(uid);
  if (onlineIt != state->users.end()) {
    onlineIt->second->touch_heartbeat();
    rsp.basicInfo = onlineIt->second->get_info();
    return Protocol::SERVICE_SUCCESS;
  }

  auto user = std::make_shared<User>(infoIt->second);
  state->users.emplace(uid, user);
  rsp.basicInfo = user->get_info();
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
    // Remove empty rooms to keep the registry clean.
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

int Server::create_room(const size_t maximumPeople, const std::string &uid,
                        Protocol::CreateRoomRsp &rsp) {
  std::scoped_lock lock(state->usersMutex, state->roomsMutex);
  auto userIt = state->users.find(uid);
  if (userIt == state->users.end() || !userIt->second) {
    return Protocol::SERVICE_FAIL | Protocol::NOT_FOUND;
  }
  auto user = userIt->second;
  if (user->is_in_room()) {
    return Protocol::SERVICE_FAIL | Protocol::ROOM_STATE_ERROR;
  }

  int room_id = state->nextRoomId++;
  auto room = std::make_shared<Room>(room_id, maximumPeople, user);
  state->rooms.emplace(room_id, room);
  user->set_room_id(room_id);
  rsp.roomId = room_id;
  return Protocol::SERVICE_SUCCESS;
}

int Server::join_room(int room_id, const std::string &uid,
                      Protocol::JoinRoomRsp &rsp) {
  std::scoped_lock lock(state->usersMutex, state->roomsMutex);

  auto roomIt = state->rooms.find(room_id);
  if (roomIt == state->rooms.end()) {
    return Protocol::SERVICE_FAIL | Protocol::NOT_FOUND;
  }
  auto userIt = state->users.find(uid);
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
  room->collect_members_info(rsp.playerInfos);
  return Protocol::SERVICE_SUCCESS;
}

int Server::leave_room(const std::string &uid) {
  std::scoped_lock lock(state->usersMutex, state->roomsMutex);

  auto userIt = state->users.find(uid);
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
  if (!room->is_member(uid)) {
    return Protocol::SERVICE_FAIL | Protocol::ROOM_STATE_ERROR;
  }

  room->remove_member(uid);
  member->set_room_id(-1);

  // Remove empty rooms to keep the registry clean.
  if (room->get_people_count() == 0) {
    state->rooms.erase(it);
  }
  return Protocol::SERVICE_SUCCESS;
}

int Server::list_rooms(Protocol::ListRoomsRsp &rsp) {
  std::lock_guard<std::mutex> lock(state->roomsMutex);
  rsp.roomInfos.clear();
  rsp.roomInfos.reserve(state->rooms.size());
  for (const auto &[id, room] : state->rooms) {
    rsp.roomInfos.push_back({.roomId = id,
                             .maximumPeople = room->get_maximum_people(),
                             .peopleCount = room->get_people_count()});
  }
  return Protocol::SERVICE_SUCCESS;
}

int Server::heartbeat(const std::string &uid) {
  std::lock_guard<std::mutex> lock(state->usersMutex);
  auto it = state->users.find(uid);
  if (it == state->users.end() || !it->second) {
    return Protocol::SERVICE_FAIL | Protocol::NOT_FOUND;
  }
  it->second->touch_heartbeat();
  return Protocol::SERVICE_SUCCESS;
}

namespace {

Protocol::Envelope dispatch_login(std::shared_ptr<Server> server,
                                  const json &j) {
  const auto req = j.get<Protocol::LoginReq>();
  if (req.type != Protocol::LoginRequestType::LOGIN || req.uid.empty()) {
    return make_env(Protocol::SERVICE_FAIL | Protocol::BAD_REQUEST);
  }

  Protocol::LoginRsp rsp;
  const int code = server->login_user(req.uid, rsp);
  return make_env(code, code == Protocol::SERVICE_SUCCESS ? json(rsp)
                                                          : json::object());
}

Protocol::Envelope dispatch_register(std::shared_ptr<Server> server,
                                     const json &j) {
  const auto req = j.get<Protocol::LoginReq>();
  if (req.type != Protocol::LoginRequestType::REGISTER || !req.uid.empty()) {
    return make_env(Protocol::SERVICE_FAIL | Protocol::BAD_REQUEST);
  }

  Protocol::RegisterRsp rsp;
  const int code = server->register_user(rsp);
  return make_env(code, json(rsp));
}

Protocol::Envelope dispatch_create_room(std::shared_ptr<Server> server,
                                        const json &j) {
  const auto req = j.get<Protocol::CreateRoomReq>();
  Protocol::CreateRoomRsp rsp;
  const int code = server->create_room(req.maximumPeople, req.uid, rsp);
  return make_env(code, code == Protocol::SERVICE_SUCCESS ? json(rsp)
                                                          : json::object());
}

Protocol::Envelope dispatch_join_room(std::shared_ptr<Server> server,
                                      const json &j) {
  const auto req = j.get<Protocol::JoinRoomReq>();
  Protocol::JoinRoomRsp rsp;
  const int code = server->join_room(req.roomId, req.uid, rsp);
  return make_env(code, code == Protocol::SERVICE_SUCCESS ? json(rsp)
                                                          : json::object());
}

Protocol::Envelope dispatch_list_rooms(std::shared_ptr<Server> server,
                                       const json &j) {
  (void)j;
  Protocol::ListRoomsRsp rsp;
  const int code = server->list_rooms(rsp);
  return make_env(code, code == Protocol::SERVICE_SUCCESS ? json(rsp)
                                                          : json::object());
}

Protocol::Envelope dispatch_leave_room(std::shared_ptr<Server> server,
                                       const json &j) {
  const auto req = j.get<Protocol::LeaveRoomReq>();
  const int code = server->leave_room(req.uid);
  return make_env(code);
}

Protocol::Envelope dispatch_heartbeat(std::shared_ptr<Server> server,
                                      const json &j) {
  const auto req = j.get<Protocol::HeartbeatReq>();
  const int code = server->heartbeat(req.uid);
  return make_env(code, code == Protocol::SERVICE_SUCCESS
                            ? json{{"uid", req.uid}}
                            : json::object());
}

} // namespace

const std::array<LoginServer::CommandDescriptor, 2> LoginServer::COMMAND_TABLE{{
    {Protocol::LoginRequestType::LOGIN, dispatch_login},
    {Protocol::LoginRequestType::REGISTER, dispatch_register},
}};

const std::array<HomeServer::CommandDescriptor, 5> HomeServer::COMMAND_TABLE{{
    {Protocol::HomeRequestType::CREATE_ROOM, dispatch_create_room},
    {Protocol::HomeRequestType::JOIN_ROOM, dispatch_join_room},
    {Protocol::HomeRequestType::LIST_ROOMS, dispatch_list_rooms},
    {Protocol::HomeRequestType::LEAVE_ROOM, dispatch_leave_room},
    {Protocol::HomeRequestType::HEARTBEAT, dispatch_heartbeat},
}};

Protocol::Envelope LoginServer::dispatch_request(const json &request) {
  const auto type = static_cast<Protocol::LoginRequestType>(request.value(
      "type", static_cast<int>(Protocol::LoginRequestType::ERROR)));
  const auto it = std::find_if(
      COMMAND_TABLE.begin(), COMMAND_TABLE.end(),
      [type](const CommandDescriptor &entry) { return entry.type == type; });
  if (it == COMMAND_TABLE.end()) {
    return make_env(Protocol::SERVICE_FAIL | Protocol::BAD_REQUEST);
  }
  return it->dispatch(shared_from_this(), request);
}

Protocol::Envelope HomeServer::dispatch_request(const json &request) {
  const auto type = static_cast<Protocol::HomeRequestType>(request.value(
      "type", static_cast<int>(Protocol::HomeRequestType::ERROR)));
  const auto it = std::find_if(
      COMMAND_TABLE.begin(), COMMAND_TABLE.end(),
      [type](const CommandDescriptor &entry) { return entry.type == type; });
  if (it == COMMAND_TABLE.end()) {
    return make_env(Protocol::SERVICE_FAIL | Protocol::BAD_REQUEST);
  }
  return it->dispatch(shared_from_this(), request);
}

asio::awaitable<void> Server::heartbeat_monitor() {
  while (true) {
    heartbeatTimer.expires_after(heartbeatInterval);
    std::error_code ec;
    co_await heartbeatTimer.async_wait(
        asio::redirect_error(asio::use_awaitable, ec));
    if (ec) {
      continue;
    }

    std::vector<std::string> expired;
    const auto now = std::chrono::steady_clock::now();
    {
      std::lock_guard<std::mutex> lock(state->usersMutex);
      for (const auto &[uid, user] : state->users) {
        if (!user) {
          continue;
        }
        if (now - user->get_last_heartbeat() > heartbeatTimeout) {
          expired.push_back(uid);
        }
      }
    }

    for (const auto &uid : expired) {
      logout_user(uid);
    }
  }
}