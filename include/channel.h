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
  asio::ip::tcp::socket socket;
  std::array<char, 2048> buf;
  std::shared_ptr<Server> server;
  
  // Construct a uniform response envelope.
  static Protocol::Envelope make_ok_env(int code, const json &data);
  static Protocol::Envelope make_err_env(int code,
                                         const std::string &message);

  Protocol::Envelope handle_register(const json &j);
  Protocol::Envelope handle_login(const json &j);
  Protocol::Envelope handle_create_room(const json &j);
  Protocol::Envelope handle_join_room(const json &j);
  Protocol::Envelope handle_list_rooms(const json &j);
  Protocol::Envelope handle_leave_room(const json &j);
  Protocol::Envelope handle_heartbeat(const json &j);

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
