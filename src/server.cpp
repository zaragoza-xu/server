#include "server.h"
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

Server::Server(asio::io_context &context, int port)
    : ioContext(context), acceptor(context, tcp::endpoint(tcp::v4(), port)),
      heartbeatTimer(context), nextRoomId(1), nextUid(1) {
  // Start accepting connections immediately on construction.
  asio::co_spawn(ioContext, accept_loop(), asio::detached);
  asio::co_spawn(ioContext, heartbeat_monitor(), asio::detached);
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
  std::scoped_lock lock(usersMutex, userInfosMutex);

  Protocol::PlayerBasicInfo storedInfo = {"", "", 0};
  storedInfo.uid = std::to_string(nextUid++);
  userInfos.emplace(storedInfo.uid, storedInfo);

  auto user = std::make_shared<User>(storedInfo);
  users[storedInfo.uid] = user;
  rsp.uid = storedInfo.uid;
  return Protocol::SERVICE_SUCCESS;
}

int Server::login_user(const std::string &uid, Protocol::LoginRsp &rsp) {
  std::scoped_lock lock(usersMutex, userInfosMutex);

  auto infoIt = userInfos.find(uid);
  if (infoIt == userInfos.end()) {
    return Protocol::SERVICE_FAIL | Protocol::NOT_FOUND;
  }
  auto onlineIt = users.find(uid);
  if (onlineIt != users.end()) {
    rsp.basicInfo = onlineIt->second->get_info();
    return Protocol::SERVICE_SUCCESS;
  }

  auto user = std::make_shared<User>(infoIt->second);
  users.emplace(uid, user);
  rsp.basicInfo = user->get_info();
  return Protocol::SERVICE_SUCCESS;
}

void Server::logout_user(const std::string &uid) {
  std::shared_ptr<User> user;
  {
    std::lock_guard<std::mutex> lock(usersMutex);
    auto it = users.find(uid);
    if (it == users.end()) {
      return;
    }
    user = it->second;
    users.erase(it);
  }

  if (user && user->is_in_room()) {
    leave_room(uid);
  }
}

std::shared_ptr<User> Server::get_user(const std::string &uid) const {
  std::lock_guard<std::mutex> lock(usersMutex);
  auto it = users.find(uid);
  if (it != users.end()) {
    return it->second;
  }
  return nullptr;
}

bool Server::user_exists(const std::string &uid) const {
  std::lock_guard<std::mutex> lock(userInfosMutex);
  return userInfos.count(uid) > 0;
}

int Server::create_room(const size_t maximumPeople, const std::string &uid,
                        Protocol::CreateRoomRsp &rsp) {
  std::scoped_lock lock(usersMutex, roomsMutex);
  auto userIt = users.find(uid);
  if (userIt == users.end() || !userIt->second) {
    return Protocol::SERVICE_FAIL | Protocol::NOT_FOUND;
  }
  auto user = userIt->second;
  if (user->is_in_room()) {
    return Protocol::SERVICE_FAIL | Protocol::ROOM_STATE_ERROR;
  }

  int room_id = nextRoomId++;
  auto room = std::make_shared<Room>(room_id, maximumPeople, user);
  rooms.emplace(room_id, room);
  user->set_room_id(room_id);
  rsp.roomId = room_id;
  return Protocol::SERVICE_SUCCESS;
}

int Server::join_room(int room_id, const std::string &uid,
                      Protocol::JoinRoomRsp &rsp) {
  std::scoped_lock lock(usersMutex, roomsMutex);

  auto roomIt = rooms.find(room_id);
  if (roomIt == rooms.end()) {
    return Protocol::SERVICE_FAIL | Protocol::NOT_FOUND;
  }
  auto userIt = users.find(uid);
  if (userIt == users.end() || !userIt->second) {
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
  std::scoped_lock lock(usersMutex, roomsMutex);

  auto userIt = users.find(uid);
  if (userIt == users.end() || !userIt->second) {
    return Protocol::SERVICE_FAIL | Protocol::NOT_FOUND;
  }
  auto member = userIt->second;
  if (!member->is_in_room()) {
    return Protocol::SERVICE_FAIL | Protocol::ROOM_STATE_ERROR;
  }

  const int room_id = member->get_room_id();
  auto it = rooms.find(room_id);
  if (it == rooms.end()) {
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
    rooms.erase(it);
  }
  return Protocol::SERVICE_SUCCESS;
}

int Server::list_rooms(Protocol::ListRoomsRsp &rsp) {
  std::lock_guard<std::mutex> lock(roomsMutex);
  rsp.roomInfos.clear();
  rsp.roomInfos.reserve(rooms.size());
  for (const auto &[id, room] : rooms) {
    rsp.roomInfos.push_back({.roomId = id,
                             .maximumPeople = room->get_maximum_people(),
                             .peopleCount = room->get_people_count()});
  }
  return Protocol::SERVICE_SUCCESS;
}

int Server::heartbeat(const std::string &uid) {
  std::lock_guard<std::mutex> lock(usersMutex);
  auto it = users.find(uid);
  if (it == users.end() || !it->second) {
    return Protocol::SERVICE_FAIL | Protocol::NOT_FOUND;
  }
  it->second->touch_heartbeat();
  return Protocol::SERVICE_SUCCESS;
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
      std::lock_guard<std::mutex> lock(usersMutex);
      for (const auto &[uid, user] : users) {
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

// asio::awaitable<void> Server::broadcast_to_room(int room_id,
//                                                 const std::string &message) {
//   auto room = get_room(room_id);
//   if (!room) {
//     co_return;
//   }

//   for (const auto &[uid, user] : room->get_members()) {
//     (void)uid;
//     if (user && user->get_channel()) {
//       co_await user->get_channel()->send_message(message);
//     }
//   }
// }