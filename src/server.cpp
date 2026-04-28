#include "server.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
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
#include "types.h"
#include "user.h"

using namespace asio::ip;

namespace {
struct ShopCatalogConfig {
  std::string version = "v1";
  std::vector<std::string> itemIds;
};

ShopCatalogConfig default_shop_catalog() {
  return ShopCatalogConfig{
      .version = "embedded-v1",
      .itemIds = {"sword", "shield", "potion", "boots", "wand"}};
}

ShopCatalogConfig load_shop_catalog() {
  const std::vector<std::filesystem::path> candidates = {
      "config/shop_catalog.json", "../config/shop_catalog.json",
      "../../config/shop_catalog.json"};

  for (const auto &path : candidates) {
    std::ifstream input(path);
    if (!input.is_open()) {
      continue;
    }
    try {
      json j;
      input >> j;
      ShopCatalogConfig cfg = default_shop_catalog();
      if (j.contains("version") && j.at("version").is_string()) {
        cfg.version = j.at("version").get<std::string>();
      }
      if (!j.contains("itemIds") || !j.at("itemIds").is_array()) {
        continue;
      }
      cfg.itemIds.clear();
      for (const auto &entry : j.at("itemIds")) {
        if (entry.is_string()) {
          cfg.itemIds.push_back(entry.get<std::string>());
        }
      }
      if (cfg.itemIds.empty()) {
        continue;
      }
      return cfg;
    } catch (...) {
      continue;
    }
  }
  return default_shop_catalog();
}
} // namespace

Server::Server(asio::io_context &context, int port,
               std::shared_ptr<ServerState> sharedState)
    : ioContext(context), port(port),
      acceptor(context, tcp::endpoint(tcp::v4(), port)),
      state(std::move(sharedState)) {
  if (!state) {
    state = std::make_shared<ServerState>();
  }
  if (state->shopCatalogItemIds.empty()) {
    const auto cfg = load_shop_catalog();
    state->shopCatalogVersion = cfg.version;
    state->shopCatalogItemIds = cfg.itemIds;
  }
}

void Server::start() {
  std::call_once(startOnce, [this]() {
    auto self = shared_from_this();
    asio::co_spawn(
        ioContext,
        [self]() -> asio::awaitable<void> { co_await self->accept_loop(); },
        asio::detached);
  });
}

json Server::dispatch_request(const json &, const std::shared_ptr<Channel> &) {
  logging::log("[dispatch][base] reached fallback dispatcher");
  return json(Protocol::ShortEnvelope::make_env(Protocol::SERVICE_FAIL |
                                                Protocol::BAD_REQUEST));
}

int Server::resolve_room_member_locked(const std::string &uid,
                                       std::shared_ptr<User> &member,
                                       std::shared_ptr<Room> &room) const {
  auto userIt = state->users.find(uid);
  if (userIt == state->users.end() || !userIt->second) {
    return Protocol::SERVICE_FAIL | Protocol::NOT_FOUND;
  }

  member = userIt->second;
  if (!member->is_in_room()) {
    return Protocol::SERVICE_FAIL | Protocol::ROOM_STATE_ERROR;
  }

  const int room_id = member->get_room_id();
  auto roomIt = state->rooms.find(room_id);
  if (roomIt == state->rooms.end()) {
    return Protocol::SERVICE_FAIL | Protocol::NOT_FOUND;
  }

  room = roomIt->second;
  if (!room->is_member(uid)) {
    return Protocol::SERVICE_FAIL | Protocol::ROOM_STATE_ERROR;
  }

  return Protocol::SERVICE_SUCCESS;
}

void Server::bind_user_channel(const std::string &uid,
                               const std::shared_ptr<Channel> &channel) {
  if (uid.empty() || !channel) {
    return;
  }
  std::lock_guard<std::mutex> lock(userChannelsMutex);
  userChannels[uid] = channel;
}

void Server::broadcast_to_members(const std::vector<std::string> &memberUids,
                                  const Protocol::LongEnvelope &message,
                                  const std::string &excludeUid) {
  const std::string payload = json(message).dump();
  std::vector<std::shared_ptr<Channel>> targets;

  {
    std::lock_guard<std::mutex> lock(userChannelsMutex);
    targets.reserve(memberUids.size());
    for (const auto &uid : memberUids) {
      if (!excludeUid.empty() && uid == excludeUid) {
        continue;
      }
      auto it = userChannels.find(uid);
      if (it == userChannels.end()) {
        continue;
      }
      auto channel = it->second.lock();
      if (!channel) {
        userChannels.erase(it);
        continue;
      }
      targets.push_back(std::move(channel));
    }
  }

  for (auto &channel : targets) {
    asio::co_spawn(
        ioContext,
        [channel, payload]() -> asio::awaitable<void> {
          co_await channel->send_message(payload);
        },
        asio::detached);
  }
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
  std::scoped_lock lock(state->usersMutex, state->userDataMutex);

  Protocol::PlayerBasicInfo storedInfo = {"", "", 0};
  storedInfo.uid = std::to_string(state->nextUid++);
  state->userData.emplace(storedInfo.uid,
                          (Protocol::PlayerData){.basicInfo = storedInfo});

  rsp.uid = storedInfo.uid;
  return Protocol::SERVICE_SUCCESS;
}

int Server::login_user(const Protocol::LoginReq &req, Protocol::LoginRsp &rsp) {
  std::scoped_lock lock(state->usersMutex, state->userDataMutex);

  if (req.uid == "") {
    return Protocol::SERVICE_FAIL | Protocol::BAD_REQUEST;
  }
  auto infoIt = state->userData.find(req.uid);
  if (infoIt == state->userData.end()) {
    return Protocol::SERVICE_FAIL | Protocol::NOT_FOUND;
  }
  auto onlineIt = state->users.find(req.uid);
  if (onlineIt != state->users.end()) {
    return Protocol::SERVICE_FAIL | Protocol::BAD_REQUEST;
  }

  auto user = std::make_shared<User>(req.uid, state);
  state->users.emplace(req.uid, user);
  rsp.playerData.basicInfo = infoIt->second.basicInfo;
  return Protocol::SERVICE_SUCCESS;
}

int Server::logout_user(const Protocol::LogoutReq &req, Protocol::EmptyRsp &) {
  if (req.uid.empty()) {
    return Protocol::SERVICE_FAIL | Protocol::BAD_REQUEST;
  }

  std::scoped_lock lock(state->usersMutex, state->roomsMutex);
  auto it = state->users.find(req.uid);
  if (it == state->users.end()) {
    return Protocol::SERVICE_FAIL | Protocol::BAD_REQUEST;
  }
  auto user = it->second;

  if (user && user->is_in_room()) {
    std::shared_ptr<User> member;
    std::shared_ptr<Room> room;
    const int resolve_code = resolve_room_member_locked(req.uid, member, room);
    if (resolve_code != Protocol::SERVICE_SUCCESS) {
      return Protocol::ERROR | Protocol::ROOM_STATE_ERROR;
    }

    room->remove_member(req.uid);
    member->set_room_id(-1);
    if (room->get_people_count() == 0) {
      state->rooms.erase(room->get_id());
    }
  }

  state->users.erase(it);

  return Protocol::SERVICE_SUCCESS;
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
  std::lock_guard<std::mutex> lock(state->userDataMutex);
  return state->userData.count(uid) > 0;
}

int Server::edit_profile(const Protocol::EditProfileReq &req,
                         Protocol::EmptyRsp &) {
  std::scoped_lock lock(state->usersMutex, state->userDataMutex);
  auto userIt = state->users.find(req.uid);
  if (userIt == state->users.end() || !userIt->second) {
    return Protocol::SERVICE_FAIL | Protocol::NOT_FOUND;
  }
  auto infoIt = state->userData.find(req.uid);
  if (infoIt == state->userData.end()) {
    return Protocol::SERVICE_FAIL | Protocol::NOT_FOUND;
  }
  infoIt->second.basicInfo = req.basicInfo;
  return Protocol::SERVICE_SUCCESS;
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
  auto room = std::make_shared<Room>(room_id, req.maximumPeople, state, user);
  state->rooms.emplace(room_id, room);
  user->set_room_id(room_id);
  rsp.roomInfo = room->get_info();
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
  std::vector<std::string> member_uids;
  Protocol::RoomInfo room_info_after_leave;

  {
    std::scoped_lock lock(state->usersMutex, state->roomsMutex);

    std::shared_ptr<User> member;
    std::shared_ptr<Room> room;
    const int resolve_code = resolve_room_member_locked(req.uid, member, room);
    if (resolve_code != Protocol::SERVICE_SUCCESS) {
      return resolve_code;
    }

    room->remove_member(req.uid);
    member->set_room_id(-1);

    room_info_after_leave = room->get_info();
    member_uids = room->get_member_uids();

    if (room->get_people_count() == 0) {
      state->rooms.erase(room->get_id());
    }
  }

  Protocol::LongEnvelope push;
  push.type = static_cast<int>(Protocol::HomeRequestType::BROADCAST);
  push.pushMessages = {};
  push.data = json{{"uid", req.uid}, {"roomInfo", room_info_after_leave}};
  broadcast_to_members(member_uids, push);

  return Protocol::SERVICE_SUCCESS;
}

int Server::set_ready(const Protocol::SetReadyReq &req,
                      Protocol::NoResponseRsp &) {

  std::vector<std::string> member_uids;
  Protocol::LongEnvelope push;
  {
    std::scoped_lock lock(state->usersMutex, state->roomsMutex);

    std::shared_ptr<User> member;
    std::shared_ptr<Room> room;
    const int resolve_code = resolve_room_member_locked(req.uid, member, room);
    if (resolve_code != Protocol::SERVICE_SUCCESS) {
      return resolve_code;
    }

    if (!room->set_member_ready(req.uid, req.ready)) {
      return Protocol::SERVICE_FAIL | Protocol::ROOM_STATE_ERROR;
    }
    member_uids = room->get_member_uids();

    push.type = static_cast<int>(Protocol::HomeRequestType::BROADCAST);
    push.pushMessages = room->is_all_ready()
                            ? std::list<int>{static_cast<int>(
                                  Protocol::HomePushMessageType::ALL_READY)}
                            : std::list<int>{};
    push.data = json{
        {"uid", req.uid}, {"ready", req.ready}, {"roomInfo", room->get_info()}};
  }

  broadcast_to_members(member_uids, push);
  return Protocol::SERVICE_SUCCESS;
}

int Server::shop_init(const Protocol::ShopInitReq &req,
                      Protocol::ShopInitRsp &rsp) {
  std::scoped_lock lock(state->usersMutex, state->roomsMutex);

  std::shared_ptr<User> member;
  std::shared_ptr<Room> room;
  const int resolve_code = resolve_room_member_locked(req.uid, member, room);
  if (resolve_code != Protocol::SERVICE_SUCCESS) {
    return resolve_code;
  }

  if (!room->get_shop_init(rsp)) {
    return Protocol::SERVICE_FAIL | Protocol::ROOM_STATE_ERROR;
  }
  return Protocol::SERVICE_SUCCESS;
}

int Server::shop_move_cursor(const Protocol::ShopMoveCursorReq &req,
                             Protocol::NoResponseRsp &) {
  std::vector<std::string> member_uids;
  std::vector<Protocol::ShopItem> items;
  {
    std::scoped_lock lock(state->usersMutex, state->roomsMutex);

    std::shared_ptr<User> member;
    std::shared_ptr<Room> room;
    const int resolve_code = resolve_room_member_locked(req.uid, member, room);
    if (resolve_code != Protocol::SERVICE_SUCCESS) {
      return resolve_code;
    }

    if (!room->move_shop_cursor(req.uid, req.itemId, items)) {
      return Protocol::SERVICE_FAIL | Protocol::SHOP_INVALID_ITEM;
    }
    member_uids = room->get_member_uids();
  }

  Protocol::LongEnvelope push;
  push.type = static_cast<int>(Protocol::ShopResponseType::SHOP_SYNC);
  push.pushMessages = {};
  push.data = json{{"items", items}};
  broadcast_to_members(member_uids, push);
  return Protocol::SERVICE_SUCCESS;
}

int Server::shop_buy_item(const Protocol::ShopBuyItemReq &req,
                          Protocol::NoResponseRsp &) {
  std::vector<std::string> member_uids;
  std::vector<Protocol::ShopItem> items;
  {
    std::scoped_lock lock(state->usersMutex, state->roomsMutex);

    std::shared_ptr<User> member;
    std::shared_ptr<Room> room;
    const int resolve_code = resolve_room_member_locked(req.uid, member, room);
    if (resolve_code != Protocol::SERVICE_SUCCESS) {
      return resolve_code;
    }

    const int buy_code = room->buy_shop_item(req.uid, req.itemId, items);
    if (buy_code != Protocol::SERVICE_SUCCESS) {
      return buy_code;
    }

    member_uids = room->get_member_uids();
  }

  Protocol::LongEnvelope push;
  push.type = static_cast<int>(Protocol::ShopResponseType::SHOP_SYNC);
  push.pushMessages = {};
  push.data = json{{"items", items}};
  broadcast_to_members(member_uids, push);
  return Protocol::SERVICE_SUCCESS;
}

int Server::map_init(const Protocol::MapInitReq &req,
                     Protocol::MapInitRsp &rsp) {
  std::scoped_lock lock(state->usersMutex, state->roomsMutex);

  std::shared_ptr<User> member;
  std::shared_ptr<Room> room;
  const int resolve_code = resolve_room_member_locked(req.uid, member, room);
  if (resolve_code != Protocol::SERVICE_SUCCESS) {
    return resolve_code;
  }

  if (!room->get_map_init(rsp)) {
    return Protocol::SERVICE_FAIL | Protocol::ROOM_STATE_ERROR;
  }
  return Protocol::SERVICE_SUCCESS;
}

int Server::map_move(const Protocol::MapMoveReq &req,
                     Protocol::NoResponseRsp &) {
  std::vector<std::string> member_uids;
  std::vector<Protocol::MapSync> selectStatus;
  bool committed = false;
  {
    std::scoped_lock lock(state->usersMutex, state->roomsMutex);

    std::shared_ptr<User> member;
    std::shared_ptr<Room> room;
    const int resolve_code = resolve_room_member_locked(req.uid, member, room);
    if (resolve_code != Protocol::SERVICE_SUCCESS) {
      return resolve_code;
    }

    if (!room->move_map(req.uid, req.selectId, selectStatus, committed)) {
      return Protocol::SERVICE_FAIL | Protocol::ROOM_STATE_ERROR;
    }
    member_uids = room->get_member_uids();
  }

  Protocol::LongEnvelope push;
  push.type = static_cast<int>(Protocol::MapRequestType::MAP_MOVE);
  push.pushMessages = committed ? std::list<int>{static_cast<int>(
                                      Protocol::MapResponseType::MAP_SYNC)}
                                : std::list<int>{};
  push.data = json{{"selectStatus", selectStatus}};
  broadcast_to_members(member_uids, push);
  return Protocol::SERVICE_SUCCESS;
}

int Server::list_rooms(const Protocol::ListRoomsReq &,
                       Protocol::ListRoomsRsp &rsp) {
  std::lock_guard<std::mutex> lock(state->roomsMutex);
  rsp.roomInfos.clear();
  rsp.roomInfos.reserve(state->rooms.size());
  // hide rooms that have all members ready
  for (const auto &[id, room] : state->rooms) {
    if (!room->is_all_ready())
      rsp.roomInfos.push_back(room->get_info());
  }
  return Protocol::SERVICE_SUCCESS;
}

json LoginServer::dispatch_request(const json &request,
                                   const std::shared_ptr<Channel> &) {
  const auto type = static_cast<Protocol::LoginRequestType>(request.value(
      "type", static_cast<int>(Protocol::LoginRequestType::ERROR)));
  logging::log("[dispatch][login] type={}({}) request={}",
               logging::request_type_name(type), static_cast<int>(type),
               request.dump());

  const auto it = std::find_if(
      COMMAND_TABLE.begin(), COMMAND_TABLE.end(),
      [type](const CommandDescriptor &entry) { return entry.type == type; });
  if (it == COMMAND_TABLE.end()) {
    logging::log("[dispatch][login] unknown type={}({})",
                 logging::request_type_name(type), static_cast<int>(type));
    return Protocol::ShortEnvelope::make_env(Protocol::SERVICE_FAIL |
                                             Protocol::BAD_REQUEST);
  }

  const auto env = it->dispatch(*this, request);
  return env;
}

json HomeServer::dispatch_request(const json &request,
                                  const std::shared_ptr<Channel> &channel) {
  if (channel && request.contains("uid") && request.at("uid").is_string()) {
    bind_user_channel(request.at("uid").get<std::string>(), channel);
  }

  const auto type = static_cast<Protocol::HomeRequestType>(request.value(
      "type", static_cast<int>(Protocol::HomeRequestType::ERROR)));
  logging::log("[dispatch][home] type={}({}) request={}",
               logging::request_type_name(type), static_cast<int>(type),
               request.dump());

  const auto it = std::find_if(
      COMMAND_TABLE.begin(), COMMAND_TABLE.end(),
      [type](const CommandDescriptor &entry) { return entry.type == type; });
  if (it == COMMAND_TABLE.end()) {
    logging::log("[dispatch][home] unknown type={}({})",
                 logging::request_type_name(type), static_cast<int>(type));
    return Protocol::ShortEnvelope::make_env(Protocol::SERVICE_FAIL |
                                             Protocol::BAD_REQUEST);
  }

  const auto payload = it->dispatch(*this, request);
  return payload;
}

json ShopServer::dispatch_request(const json &request,
                                  const std::shared_ptr<Channel> &channel) {
  if (channel && request.contains("uid") && request.at("uid").is_string()) {
    bind_user_channel(request.at("uid").get<std::string>(), channel);
  }

  const auto type = static_cast<Protocol::ShopRequestType>(request.value(
      "type", static_cast<int>(Protocol::ShopRequestType::ERROR)));
  logging::log("[dispatch][shop] type={}({}) request={}",
               logging::request_type_name(type), static_cast<int>(type),
               request.dump());

  const auto it = std::find_if(
      COMMAND_TABLE.begin(), COMMAND_TABLE.end(),
      [type](const CommandDescriptor &entry) { return entry.type == type; });
  if (it == COMMAND_TABLE.end()) {
    logging::log("[dispatch][shop] unknown type={}({})",
                 logging::request_type_name(type), static_cast<int>(type));
    return Protocol::ShortEnvelope::make_env(Protocol::SERVICE_FAIL |
                                             Protocol::BAD_REQUEST);
  }

  const auto payload = it->dispatch(*this, request);
  return payload;
}

json MapServer::dispatch_request(const json &request,
                                 const std::shared_ptr<Channel> &channel) {
  if (channel && request.contains("uid") && request.at("uid").is_string()) {
    bind_user_channel(request.at("uid").get<std::string>(), channel);
  }

  const auto type = static_cast<Protocol::MapRequestType>(request.value(
      "type", static_cast<int>(Protocol::MapRequestType::MAP_MOVE) + 100));
  logging::log("[dispatch][map] type={}({}) request={}",
               logging::request_type_name(type), static_cast<int>(type),
               request.dump());

  const auto it = std::find_if(
      COMMAND_TABLE.begin(), COMMAND_TABLE.end(),
      [type](const CommandDescriptor &entry) { return entry.type == type; });
  if (it == COMMAND_TABLE.end()) {
    logging::log("[dispatch][map] unknown type={}({})",
                 logging::request_type_name(type), static_cast<int>(type));
    return Protocol::ShortEnvelope::make_env(Protocol::SERVICE_FAIL |
                                             Protocol::BAD_REQUEST);
  }

  const auto payload = it->dispatch(*this, request);
  return payload;
}
