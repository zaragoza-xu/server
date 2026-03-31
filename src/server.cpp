#include "server.h"
#include <cstddef>
#include <iostream>
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
#include "error.h"
#include "protocol.h"
#include "room.h"
#include "user.h"

using namespace asio::ip;

Server::Server(asio::io_context &context, int port)
    : ioContext(context), acceptor(context, tcp::endpoint(tcp::v4(), port)),
      heartbeatTimer(context), nextRoomId(1) {
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
      log("%s\n", ec.message());
      continue;
    }
    std::cout << "accepted connection" << std::endl;
    asio::co_spawn(
        ioContext, [chl]() -> asio::awaitable<void> { co_await chl->run(); },
        asio::detached);
  }
}

auto Server::register_user() -> std::shared_ptr<User> {
  std::lock_guard<std::mutex> lock(usersMutex);

  Protocol::PlayerBasicInfo storedInfo = {"", "", 0};
  storedInfo.uid = std::to_string(nextUid++);
  userInfos[storedInfo.uid] = storedInfo;

  auto user = std::make_shared<User>(storedInfo.uid, storedInfo.userName,
                                     storedInfo.avatarType);
  users[storedInfo.uid] = user;
  return user;
}

auto Server::login_user(const std::string &uid) -> std::shared_ptr<User> {
  std::lock_guard<std::mutex> lock(usersMutex);
  auto infoIt = userInfos.find(uid);
  if (infoIt == userInfos.end()) {
    return nullptr;
  }

  auto onlineIt = users.find(uid);
  if (onlineIt != users.end()) {
    auto user = onlineIt->second;
    if (user) {
      user->touch_heartbeat();
    }
    return user;
  }

  const auto &info = infoIt->second;
  auto user = std::make_shared<User>(info.uid, info.userName, info.avatarType);
  users[uid] = user;
  return user;
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
    leave_room(user->get_room_id(), uid);
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
  std::lock_guard<std::mutex> lock(usersMutex);
  return userInfos.count(uid) > 0;
}

std::shared_ptr<Room> Server::create_room(const std::string &roomName,
                                          const size_t maximumPeople,
                                          std::shared_ptr<User> user) {
  std::lock_guard<std::mutex> lock(roomsMutex);
  int room_id = nextRoomId++;
  auto room = std::make_shared<Room>(room_id, roomName, maximumPeople, user);
  rooms[room_id] = room;
  user->set_room_id(room_id);
  return room;
}

std::shared_ptr<Room> Server::get_room(int room_id) const {
  std::lock_guard<std::mutex> lock(roomsMutex);
  auto it = rooms.find(room_id);
  if (it != rooms.end())
    return it->second;
  return nullptr;
}

bool Server::join_room(std::shared_ptr<Room> room, std::shared_ptr<User> user) {
  std::lock_guard<std::mutex> lock(roomsMutex);

  if (user->is_in_room()) {
    return false;
  }

  bool success = room->add_member(user);
  if (success)
    user->set_room_id(room->get_id());
  return success;
}

bool Server::leave_room(int room_id, const std::string &uid) {
  std::lock_guard<std::mutex> lock(roomsMutex);
  auto it = rooms.find(room_id);
  if (it == rooms.end()) {
    return false;
  }

  auto room = it->second;
  auto member = room->get_member(uid);
  if (member == nullptr)
    return false;

  room->remove_member(uid);
  member->set_room_id(-1);

  // Remove empty rooms to keep the registry clean.
  if (room->get_people_count() == 0) {
    rooms.erase(it);
  }
  return true;
}

void Server::list_rooms(std::vector<Protocol::RoomInfo> &roomInfos) const {
  std::lock_guard<std::mutex> lock(roomsMutex);
  roomInfos.clear();
  roomInfos.reserve(rooms.size());
  for (const auto &[id, room] : rooms) {
    roomInfos.push_back({.roomId = id,
                         .maximumPeople = room->get_maximum_people(),
                         .peopleCount = room->get_people_count()});
  }
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