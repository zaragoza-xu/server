#pragma once

#include <array>
#include <asio/awaitable.hpp>
#include <asio/io_context.hpp>
#include <asio/ip/tcp.hpp>
#include <memory>

class Channel : public std::enable_shared_from_this<Channel> {
protected:
  asio::ip::tcp::socket socket_;
  std::array<char, 2048> buf_;

public:
  Channel(asio::io_context &context) : socket_(context) {}

  asio::awaitable<void> run();
  asio::ip::tcp::socket &getSocket() { return socket_; }

  ~Channel() = default;
};
