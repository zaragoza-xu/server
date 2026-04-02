#pragma once
#include <chrono>
#include <memory>
#include <mutex>
#include <unordered_map>

#include <asio/awaitable.hpp>
#include <asio/io_context.hpp>
#include <asio/ip/tcp.hpp>
#include <asio/steady_timer.hpp>

#include "protocol.h"

class Channel;
class User;
class Room;

class Server : public std::enable_shared_from_this<Server> {
private:
  int port;
  asio::ip::tcp::acceptor acceptor;
  asio::io_context &ioContext;
  asio::steady_timer heartbeatTimer;
  std::chrono::seconds heartbeatInterval{5};
  std::chrono::seconds heartbeatTimeout{30};
  // User and room state are shared across channels and protected by mutexes.
  std::unordered_map<std::string, Protocol::PlayerBasicInfo>
      userInfos; // uid -> profile info (persisted)
  std::unordered_map<std::string, std::shared_ptr<User>>
      users; // uid -> online user session
  std::unordered_map<int, std::shared_ptr<Room>> rooms; // room_id -> Room
  mutable std::mutex usersMutex;
  mutable std::mutex roomsMutex;
  mutable std::mutex userInfosMutex;
  int nextRoomId;
  int nextUid = 1;

  asio::awaitable<void> accept_loop();
  asio::awaitable<void> heartbeat_monitor();

public:
  Server(asio::io_context &context, int port);
  ~Server() = default;

  // User management
  auto register_user() -> std::shared_ptr<User>;
  auto login_user(const std::string &uid) -> std::shared_ptr<User>;
  void logout_user(const std::string &uid);
  std::shared_ptr<User> get_user(const std::string &uid) const;
  bool user_exists(const std::string &uid) const;

  // Room management
  std::shared_ptr<Room> create_room(const size_t maximumPeople,
                                    std::shared_ptr<User> user);
  std::shared_ptr<Room> get_room(int room_id) const;
  bool join_room(std::shared_ptr<Room> room, std::shared_ptr<User> user);
  bool leave_room(int room_id, const std::string &uid);
  void list_rooms(std::vector<Protocol::RoomInfo> &roomInfos) const;

  // Message broadcasting
  // asio::awaitable<void> broadcast_to_room(int room_id,
  //                                         const std::string &message);
};