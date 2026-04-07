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

struct ServerState {
  // uid -> profile info (persisted)
  std::unordered_map<std::string, Protocol::PlayerBasicInfo> userInfos;
  // uid -> online user session
  std::unordered_map<std::string, std::shared_ptr<User>> users;
  // room_id -> Room
  std::unordered_map<int, std::shared_ptr<Room>> rooms;
  std::mutex usersMutex;
  std::mutex roomsMutex;
  std::mutex userInfosMutex;
  int nextRoomId = 1;
  int nextUid = 1;
};

class Server : public std::enable_shared_from_this<Server> {
private:
  int port;
  asio::ip::tcp::acceptor acceptor;
  asio::io_context &ioContext;
  asio::steady_timer heartbeatTimer;
  std::chrono::seconds heartbeatInterval{5};
  std::chrono::seconds heartbeatTimeout{30};
  std::shared_ptr<ServerState> state;

  asio::awaitable<void> accept_loop();
  asio::awaitable<void> heartbeat_monitor();

public:
  Server(asio::io_context &context, int port,
         std::shared_ptr<ServerState> sharedState = nullptr);
  virtual ~Server() = default;

  using DispatchFn = Protocol::Envelope (*)(std::shared_ptr<Server>,
                                            const json &);

  virtual Protocol::Envelope dispatch_request(const json &request);

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

class LoginServer : public Server {
public:
  LoginServer(asio::io_context &context, int port,
              std::shared_ptr<ServerState> sharedState = nullptr)
      : Server(context, port, std::move(sharedState)) {}

  Protocol::Envelope dispatch_request(const json &request) override;

private:
  struct CommandDescriptor {
    Protocol::LoginRequestType type;
    DispatchFn dispatch;
  };
  static const std::array<CommandDescriptor, 2> COMMAND_TABLE;
};

class HomeServer : public Server {
public:
  HomeServer(asio::io_context &context, int port,
             std::shared_ptr<ServerState> sharedState = nullptr)
      : Server(context, port, std::move(sharedState)) {}
  Protocol::Envelope dispatch_request(const json &request) override;

private:
  struct CommandDescriptor {
    Protocol::HomeRequestType type;
    DispatchFn dispatch;
  };
  static const std::array<CommandDescriptor, 5> COMMAND_TABLE;
};