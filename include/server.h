#pragma once

#include <array>
#include <memory>
#include <mutex>
#include <type_traits>
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
  std::unordered_map<std::string, Protocol::PlayerData> userData;
  // uid -> online user session
  std::unordered_map<std::string, std::shared_ptr<User>> users;
  // room_id -> Room
  std::unordered_map<int, std::shared_ptr<Room>> rooms;
  std::vector<std::string> shopCatalogItemIds;
  std::string shopCatalogVersion = "v1";
  std::mutex usersMutex;
  std::mutex roomsMutex;
  std::mutex userDataMutex;
  int nextRoomId = 1;
  int nextUid = 1;
};

class Server : public std::enable_shared_from_this<Server> {
private:
  int port;
  asio::ip::tcp::acceptor acceptor;
  asio::io_context &ioContext;
  std::shared_ptr<ServerState> state;

  asio::awaitable<void> accept_loop();

  // Resolve an online user and their current room.
  // Caller must hold usersMutex and roomsMutex.
  int resolve_room_member_locked(const std::string &uid,
                                 std::shared_ptr<User> &member,
                                 std::shared_ptr<Room> &room) const;

public:
  Server(asio::io_context &context, int port,
         std::shared_ptr<ServerState> sharedState = nullptr);
  virtual ~Server() = default;

  using DispatchFn = json (*)(Server &, const json &);

  virtual json
  dispatch_request(const json &request,
                   const std::shared_ptr<Channel> &channel = nullptr);

  template <typename Req, typename Rsp,
            int (Server::*Method)(const Req &, Rsp &)>
  static json dispatch_entry_short(Server &server, const json &j) {
    Rsp rsp;
    Req req = j.get<Req>();
    const int code = (server.*Method)(req, rsp);
    json data = json::object();
    if (code == Protocol::SERVICE_SUCCESS) {
      if constexpr (!std::is_same_v<Rsp, Protocol::EmptyRsp> &&
                    !std::is_same_v<Rsp, Protocol::NoResponseRsp>) {
        data = json(rsp);
      }
    }
    return json(Protocol::ShortEnvelope::make_env(code, data));
  }

  template <typename Req, typename Rsp,
            int (Server::*Method)(const Req &, Rsp &)>
  static json dispatch_entry_long(Server &server, const json &j) {
    Rsp rsp;
    Req req = j.get<Req>();
    const int code = (server.*Method)(req, rsp);
    json data = json::object();
    if (code == Protocol::SERVICE_SUCCESS) {
      if constexpr (std::is_same_v<Rsp, Protocol::NoResponseRsp>) {
        return json();
      }
      if constexpr (!std::is_same_v<Rsp, Protocol::EmptyRsp> &&
                    !std::is_same_v<Rsp, Protocol::NoResponseRsp>) {
        data = json(rsp);
      }
    } else {
      return json(Protocol::ShortEnvelope::make_env(code, data));
    }

    return json(
        Protocol::LongEnvelope::make_env(static_cast<int>(req.type), data));
  }

  // Service APIs: do validation and state transitions atomically.
  int register_user(const Protocol::RegisterReq &, Protocol::RegisterRsp &);
  int login_user(const Protocol::LoginReq &, Protocol::LoginRsp &);
  int edit_profile(const Protocol::EditProfileReq &, Protocol::EmptyRsp &);
  int create_room(const Protocol::CreateRoomReq &, Protocol::CreateRoomRsp &);
  int join_room(const Protocol::JoinRoomReq &, Protocol::JoinRoomRsp &);
  int leave_room(const Protocol::LeaveRoomReq &, Protocol::EmptyRsp &);
  int set_ready(const Protocol::SetReadyReq &, Protocol::NoResponseRsp &);
  int shop_init(const Protocol::ShopInitReq &, Protocol::ShopInitRsp &);
  int shop_move_cursor(const Protocol::ShopMoveCursorReq &,
                       Protocol::NoResponseRsp &);
  int shop_buy_item(const Protocol::ShopBuyItemReq &,
                    Protocol::NoResponseRsp &);
  int map_init(const Protocol::MapInitReq &, Protocol::MapInitRsp &);
  int map_move(const Protocol::MapMoveReq &, Protocol::NoResponseRsp &);
  int list_rooms(const Protocol::ListRoomsReq &, Protocol::ListRoomsRsp &);
  int logout_user(const Protocol::LogoutReq &, Protocol::EmptyRsp &);

  // Internal/user lifecycle helpers.
  std::shared_ptr<User> get_user(const std::string &uid) const;
  bool user_exists(const std::string &uid) const;

protected:
  // uid -> active channel for server push (per-server-instance, weak to avoid
  // ownership cycle)
  std::unordered_map<std::string, std::weak_ptr<Channel>> userChannels;
  std::mutex userChannelsMutex;

  void bind_user_channel(const std::string &uid,
                         const std::shared_ptr<Channel> &channel);
  void broadcast_to_members(const std::vector<std::string> &memberUids,
                            const Protocol::LongEnvelope &message,
                            const std::string &excludeUid = "");
};

// login server spec
class LoginServer : public Server {
public:
  LoginServer(asio::io_context &context, int port,
              std::shared_ptr<ServerState> sharedState = nullptr)
      : Server(context, port, std::move(sharedState)) {}

  json
  dispatch_request(const json &request,
                   const std::shared_ptr<Channel> &channel = nullptr) override;

private:
  struct CommandDescriptor {
    Protocol::LoginRequestType type;
    DispatchFn dispatch;
  };
  const std::array<LoginServer::CommandDescriptor, 3> COMMAND_TABLE{{
      {Protocol::LoginRequestType::LOGIN,
       &Server::dispatch_entry_short<Protocol::LoginReq, Protocol::LoginRsp,
                                     &Server::login_user>},
      {Protocol::LoginRequestType::REGISTER,
       &Server::dispatch_entry_short<Protocol::RegisterReq,
                                     Protocol::RegisterRsp,
                                     &Server::register_user>},
      {Protocol::LoginRequestType::LOGOUT,
       &Server::dispatch_entry_short<Protocol::LogoutReq, Protocol::EmptyRsp,
                                     &Server::logout_user>},
  }};
};

// home server spec
class HomeServer : public Server {
public:
  HomeServer(asio::io_context &context, int port,
             std::shared_ptr<ServerState> sharedState = nullptr)
      : Server(context, port, std::move(sharedState)) {}
  json
  dispatch_request(const json &request,
                   const std::shared_ptr<Channel> &channel = nullptr) override;

private:
  struct CommandDescriptor {
    Protocol::HomeRequestType type;
    DispatchFn dispatch;
  };
  const std::array<HomeServer::CommandDescriptor, 6> COMMAND_TABLE{{
      {Protocol::HomeRequestType::EDIT_PROFILE,
       &Server::dispatch_entry_short<Protocol::EditProfileReq,
                                     Protocol::EmptyRsp,
                                     &Server::edit_profile>},
      {Protocol::HomeRequestType::CREATE_ROOM,
       &Server::dispatch_entry_short<Protocol::CreateRoomReq,
                                     Protocol::CreateRoomRsp,
                                     &Server::create_room>},
      {Protocol::HomeRequestType::JOIN_ROOM,
       &Server::dispatch_entry_short<
           Protocol::JoinRoomReq, Protocol::JoinRoomRsp, &Server::join_room>},
      {Protocol::HomeRequestType::LIST_ROOMS,
       &Server::dispatch_entry_short<Protocol::ListRoomsReq,
                                     Protocol::ListRoomsRsp,
                                     &Server::list_rooms>},
      {Protocol::HomeRequestType::LEAVE_ROOM,
       &Server::dispatch_entry_long<Protocol::LeaveRoomReq, Protocol::EmptyRsp,
                                    &Server::leave_room>},
      {Protocol::HomeRequestType::SET_READY,
       &Server::dispatch_entry_long<
           Protocol::SetReadyReq, Protocol::NoResponseRsp, &Server::set_ready>},
  }};
};

// shop server spec
class ShopServer : public Server {
public:
  ShopServer(asio::io_context &context, int port,
             std::shared_ptr<ServerState> sharedState = nullptr)
      : Server(context, port, std::move(sharedState)) {}
  json
  dispatch_request(const json &request,
                   const std::shared_ptr<Channel> &channel = nullptr) override;

private:
  struct CommandDescriptor {
    Protocol::ShopResponseType type;
    DispatchFn dispatch;
  };
  const std::array<ShopServer::CommandDescriptor, 3> COMMAND_TABLE{{
      {Protocol::ShopResponseType::SHOP_INIT,
       &Server::dispatch_entry_long<Protocol::ShopInitReq,
                                    Protocol::ShopInitRsp, &Server::shop_init>},
      {Protocol::ShopResponseType::SHOP_MOVE_CURSOR,
       &Server::dispatch_entry_long<Protocol::ShopMoveCursorReq,
                                    Protocol::NoResponseRsp,
                                    &Server::shop_move_cursor>},
      {Protocol::ShopRequestType::SHOP_BUY,
       &Server::dispatch_entry_long<Protocol::ShopBuyItemReq,
                                    Protocol::NoResponseRsp,
                                    &Server::shop_buy_item>},
  }};
};

// map server spec
class MapServer : public Server {
public:
  MapServer(asio::io_context &context, int port,
            std::shared_ptr<ServerState> sharedState = nullptr)
      : Server(context, port, std::move(sharedState)) {}
  json
  dispatch_request(const json &request,
                   const std::shared_ptr<Channel> &channel = nullptr) override;

private:
  struct CommandDescriptor {
    Protocol::MapRequestType type;
    DispatchFn dispatch;
  };
  const std::array<MapServer::CommandDescriptor, 2> COMMAND_TABLE{{
      {Protocol::MapRequestType::MAP_INIT,
       &Server::dispatch_entry_long<Protocol::MapInitReq, Protocol::MapInitRsp,
                                    &Server::map_init>},
      {Protocol::MapRequestType::MAP_MOVE,
       &Server::dispatch_entry_long<
           Protocol::MapMoveReq, Protocol::NoResponseRsp, &Server::map_move>},
  }};
};