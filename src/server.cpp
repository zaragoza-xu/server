#include <asio/awaitable.hpp>
#include <asio/co_spawn.hpp>
#include <asio/detached.hpp>
#include <asio/ip/tcp.hpp>
#include <asio/redirect_error.hpp>
#include <asio/use_awaitable.hpp>
#include <iostream>
#include <memory>
#include <mutex>

#include "channel.h"
#include "error.h"
#include "protocol.h"
#include "room.h"
#include "server.h"
#include "user.h"

using namespace asio::ip;

Server::Server(asio::io_context &context)
    : ioContext(context), acceptor(context, tcp::endpoint(tcp::v4(), SVR_PORT)),
      nextRoomId(1) {
  asio::co_spawn(ioContext, accept_loop(), asio::detached);
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

auto Server::register_user(const Protocol::PlayerBasicInfo info,
                           std::shared_ptr<Channel> chl)
    -> std::shared_ptr<User> {
  std::lock_guard<std::mutex> lock(usersMutex);
  auto user =
      std::make_shared<User>(++nextUid, info.userName, chl, info.avatarType);
  users[user->get_uid()] = user;
  chl->set_user(user);
  return user;
}

auto Server::login_user(const std::string &uid,
                        std::shared_ptr<Channel> chl) -> std::shared_ptr<User> {
  std::lock_guard<std::mutex> lock(usersMutex);
  auto it = users.find(uid);
  if (it != users.end()) {
    chl->set_user(it->second);
    return it->second;
  }
  return nullptr;
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

  // Leave room after releasing usersMutex to avoid lock re-entry.
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
  return users.count(uid) > 0;
}

std::shared_ptr<Room> Server::create_room(const std::string &room_name,
                                          std::shared_ptr<User> user) {
  std::lock_guard<std::mutex> lock(roomsMutex);
  int room_id = nextRoomId++;
  auto room = std::make_shared<Room>(room_id, room_name, user);
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
    leave_room(user->get_room_id(), user->get_uid());
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

  auto memberIt = it->second->get_members().find(uid);
  std::shared_ptr<User> member =
      memberIt != it->second->get_members().end() ? memberIt->second : nullptr;

  it->second->remove_member(uid);
  if (member) {
    member->set_room_id(-1);
  }

  // Delete empty rooms
  if (it->second->get_member_count() == 0) {
    rooms.erase(it);
  }

  return true;
}

std::vector<std::shared_ptr<Room>> Server::list_rooms() const {
  std::lock_guard<std::mutex> lock(roomsMutex);
  std::vector<std::shared_ptr<Room>> result;
  for (const auto &[id, room] : rooms) {
    result.push_back(room);
  }
  return result;
}

asio::awaitable<void> Server::broadcast_to_room(int room_id,
                                                const std::string &message) {
  auto room = get_room(room_id);
  if (!room) {
    co_return;
  }

  for (const auto &[uid, user] : room->get_members()) {
    (void)uid;
    if (user && user->get_channel()) {
      co_await user->get_channel()->send_message(message);
    }
  }
}