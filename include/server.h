#pragma once

#include <array>
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

  using DispatchFn = Protocol::Envelope (*)(Server &, const json &);
  virtual Protocol::Envelope dispatch_request(const json &request);

  template <typename Req, typename Rsp,
            int (Server::*Method)(const Req &, Rsp &)>
  static Protocol::Envelope dispatch_entry(Server &server, const json &j) {
    Rsp rsp;
    Req req = j.get<Req>();
    const int code = (server.*Method)(req, rsp);
    json data = json::object();
    if (code == Protocol::SERVICE_SUCCESS) {
      if constexpr (!std::is_same_v<Rsp, Protocol::EmptyRsp>) {
        data = json(rsp);
      }
    }
    return Protocol::Envelope::make_env(code, data);
  }

  // Service APIs: do validation and state transitions atomically.
  int register_user(const Protocol::LoginReq &, Protocol::RegisterRsp &);
  int login_user(const Protocol::LoginReq &, Protocol::LoginRsp &);
  int create_room(const Protocol::CreateRoomReq &,
                  Protocol::CreateRoomRsp &rsp);
  int join_room(const Protocol::JoinRoomReq &, Protocol::JoinRoomRsp &rsp);
  int leave_room(const Protocol::LeaveRoomReq &, Protocol::EmptyRsp &);
  int list_rooms(const Protocol::ListRoomsReq &, Protocol::ListRoomsRsp &rsp);
  int heartbeat(const Protocol::HeartbeatReq &, Protocol::EmptyRsp &);

  // Internal/user lifecycle helpers.
  void logout_user(const std::string &uid);
  std::shared_ptr<User> get_user(const std::string &uid) const;
  bool user_exists(const std::string &uid) const;
};

// login server spec
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
  const std::array<LoginServer::CommandDescriptor, 2> COMMAND_TABLE{{
      {Protocol::LoginRequestType::LOGIN,
       &Server::dispatch_entry<Protocol::LoginReq, Protocol::LoginRsp,
                               &Server::login_user>},
      {Protocol::LoginRequestType::REGISTER,
       &Server::dispatch_entry<Protocol::LoginReq, Protocol::RegisterRsp,
                               &Server::register_user>},
  }};
};

// home server spec
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
  const std::array<HomeServer::CommandDescriptor, 5> COMMAND_TABLE{{
      {Protocol::HomeRequestType::CREATE_ROOM,
       &Server::dispatch_entry<Protocol::CreateRoomReq, Protocol::CreateRoomRsp,
                               &Server::create_room>},
      {Protocol::HomeRequestType::JOIN_ROOM,
       &Server::dispatch_entry<Protocol::JoinRoomReq, Protocol::JoinRoomRsp,
                               &Server::join_room>},
      {Protocol::HomeRequestType::LIST_ROOMS,
       &Server::dispatch_entry<Protocol::ListRoomsReq, Protocol::ListRoomsRsp,
                               &Server::list_rooms>},
      {Protocol::HomeRequestType::LEAVE_ROOM,
       &Server::dispatch_entry<Protocol::LeaveRoomReq, Protocol::EmptyRsp,
                               &Server::leave_room>},
      {Protocol::HomeRequestType::HEARTBEAT,
       &Server::dispatch_entry<Protocol::HeartbeatReq, Protocol::EmptyRsp,
                               &Server::heartbeat>},
  }};
};