#pragma once

#include <array>
#include <memory>
#include <string>

#include <asio/awaitable.hpp>
#include <asio/io_context.hpp>
#include <asio/ip/tcp.hpp>

#include "protocol.h"

class User;
class Server;

class Channel : public std::enable_shared_from_this<Channel> {
protected:
  using DispatchFn = Protocol::Envelope (*)(Channel &, const json &);
  struct CommandDescriptor {
    Protocol::CommandType type;
    DispatchFn dispatch;
  };
  using CommandTable = std::array<CommandDescriptor, 6>;

  asio::ip::tcp::socket socket;
  std::array<char, 2048> buf;
  std::shared_ptr<Server> server;

  // Build response envelope from status code and optional payload.
  static Protocol::Envelope make_env(int code,
                                     const json &data = json::object());
  static const CommandTable &command_table();

  // Typed handlers: business logic after request parsing.
  Protocol::Envelope on_register(const Protocol::LoginReq &req);
  Protocol::Envelope on_login(const Protocol::LoginReq &req);
  Protocol::Envelope on_create_room(const Protocol::CreateRoomReq &req);
  Protocol::Envelope on_join_room(const Protocol::JoinRoomReq &req);
  Protocol::Envelope on_list_rooms(const Protocol::ListRoomsReq &req);
  Protocol::Envelope on_leave_room(const Protocol::LeaveRoomReq &req);
  Protocol::Envelope on_heartbeat(const Protocol::HeartbeatReq &req);

  asio::awaitable<void> handle_message(std::string &msg);

public:
  Channel(asio::io_context &context, std::shared_ptr<Server> server)
      : socket(context), server(server) {}

  // Read-loop for framed JSON messages.
  asio::awaitable<void> run();
  asio::ip::tcp::socket &getSocket() { return socket; }

  // Send message to client
  asio::awaitable<bool> send_message(const std::string &msg);

  ~Channel() = default;
};
