#include <asio/async_result.hpp>
#include <asio/redirect_error.hpp>
#include <asio/use_awaitable.hpp>
#include <asio/write.hpp>
#include <cstddef>
#include <iostream>
#include <string>
#include <system_error>

#include "channel.h"
#include "error.h"

asio::awaitable<void> Channel::run() {
  while (true) {
    std::error_code ec;
    std::size_t len = co_await socket_.async_read_some(
        asio::buffer(buf_), asio::redirect_error(asio::use_awaitable, ec));
    if (ec) {
      log("%s\n", ec.message());
      co_return;
    }

    std::string msg(buf_.data(), len);
    std::cout << msg;

    co_await asio::async_write(socket_, asio::buffer(msg),
                               asio::redirect_error(asio::use_awaitable, ec));
    if (ec) {
      log("%s\n", ec.message());
      co_return;
    }
  }
}