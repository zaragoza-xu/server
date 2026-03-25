#pragma once
#include "protocol.h"
#include <asio/awaitable.hpp>
#include <asio/io_context.hpp>
#include <asio/ip/tcp.hpp>
#include <memory>
#include <mutex>
#include <unordered_map>

class Channel;
class User;
class Room;

class Server : public std::enable_shared_from_this<Server> {
private:
  static constexpr int SVR_PORT = 7777;
  asio::ip::tcp::acceptor acceptor;
  asio::io_context &ioContext;
  std::unordered_map<std::string, std::shared_ptr<User>> users; // uid -> User
  std::unordered_map<int, std::shared_ptr<Room>> rooms; // room_id -> Room
  mutable std::mutex usersMutex;
  mutable std::mutex roomsMutex;
  int nextRoomId;
  int nextUid = 1;

  asio::awaitable<void> accept_loop();

public:
  Server(asio::io_context &context);
  ~Server() = default;

  // User management
  auto register_user(const Protocol::PlayerBasicInfo info,
                     std::shared_ptr<Channel> chl) -> std::shared_ptr<User>;
  auto login_user(const std::string &uid,
                  std::shared_ptr<Channel> channel) -> std::shared_ptr<User>;
  void logout_user(const std::string &uid);
  std::shared_ptr<User> get_user(const std::string &uid) const;
  bool user_exists(const std::string &uid) const;

  // Room management
  std::shared_ptr<Room> create_room(const std::string &room_name,
                                    std::shared_ptr<User> user);
  std::shared_ptr<Room> get_room(int room_id) const;
  bool join_room(std::shared_ptr<Room> room, std::shared_ptr<User> user);
  bool leave_room(int room_id, const std::string &uid);
  std::vector<std::shared_ptr<Room>> list_rooms() const;

  // Message broadcasting
  asio::awaitable<void> broadcast_to_room(int room_id,
                                          const std::string &message);
};