#include <asio/awaitable.hpp>
#include <asio/co_spawn.hpp>
#include <asio/detached.hpp>
#include <asio/ip/tcp.hpp>
#include <asio/redirect_error.hpp>
#include <asio/use_awaitable.hpp>
#include <iostream>
#include <memory>

#include "channel.h"
#include "error.h"
#include "server.h"

using namespace asio::ip;
Server::Server(asio::io_context &context)
    : io_context_(context),
      acceptor_(context, tcp::endpoint(tcp::v4(), SVR_PORT)) {
  asio::co_spawn(io_context_, accept_loop(), asio::detached);
}

asio::awaitable<void> Server::accept_loop() {
  while (true) {
    auto chl = std::make_shared<Channel>(io_context_);

    std::error_code ec;
    co_await acceptor_.async_accept(
        chl->getSocket(), asio::redirect_error(asio::use_awaitable, ec));

    if (ec) {
      log("%s\n", ec.message());
      continue;
    }
    std::cout << "accepted connection" << std::endl;
    asio::co_spawn(
        io_context_, [chl]() -> asio::awaitable<void> { co_await chl->run(); },
        asio::detached);
  }
}