#pragma once
#include <asio/awaitable.hpp>
#include <asio/io_context.hpp>
#include <asio/ip/tcp.hpp>
#include <memory>

class Channel;

class Server : public std::enable_shared_from_this<Server> {
private:
  static constexpr int SVR_PORT = 7777;
  asio::ip::tcp::acceptor acceptor_;
  asio::io_context &io_context_;
  asio::awaitable<void> accept_loop();

public:
  Server(asio::io_context &context);
  ~Server() = default;
};