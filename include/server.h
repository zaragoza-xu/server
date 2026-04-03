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
  int nextUid;

  asio::awaitable<void> accept_loop();
  asio::awaitable<void> heartbeat_monitor();

public:
  Server(asio::io_context &context, int port);
  ~Server() = default;

  // Service APIs: do validation and state transitions atomically.
  int register_user(Protocol::RegisterRsp &rsp);
  int login_user(const std::string &uid, Protocol::LoginRsp &rsp);
  int create_room(const size_t maximumPeople, const std::string &uid,
                  Protocol::CreateRoomRsp &rsp);
  int join_room(int room_id, const std::string &uid,
                Protocol::JoinRoomRsp &rsp);
  int leave_room(const std::string &uid);
  int list_rooms(Protocol::ListRoomsRsp &rsp);
  int heartbeat(const std::string &uid);

  // Internal/user lifecycle helpers.
  void logout_user(const std::string &uid);
  std::shared_ptr<User> get_user(const std::string &uid) const;
  bool user_exists(const std::string &uid) const;

  // Message broadcasting
  // asio::awaitable<void> broadcast_to_room(int room_id,
  //                                         const std::string &message);
};