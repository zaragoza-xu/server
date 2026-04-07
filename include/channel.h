#pragma once

#include <memory>
#include <string>

#include <asio/awaitable.hpp>
#include <asio/io_context.hpp>
#include <asio/ip/tcp.hpp>

class User;
class Server;

class Channel : public std::enable_shared_from_this<Channel> {
protected:
  asio::ip::tcp::socket socket;
  std::array<char, 2048> buf;
  std::shared_ptr<Server> server;

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
