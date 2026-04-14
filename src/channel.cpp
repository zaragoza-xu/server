#include "channel.h"

#include <cstddef>
#include <nlohmann/json.hpp>
#include <string>
#include <system_error>

#include <asio/async_result.hpp>
#include <asio/awaitable.hpp>
#include <asio/redirect_error.hpp>
#include <asio/use_awaitable.hpp>
#include <asio/write.hpp>

#include "logging.h"
#include "protocol.h"
#include "server.h"

using json = nlohmann::json;

namespace {
constexpr char FRAME_DELIMITER = '\n';
} // namespace

asio::awaitable<void> Channel::run() {
  // Accumulate data and split on newline delimiter.
  std::string pending;

  while (true) {
    std::error_code ec;
    std::size_t len = co_await socket.async_read_some(
        asio::buffer(buf), asio::redirect_error(asio::use_awaitable, ec));
    if (ec) {
      logging::log("Connection closed: {}", ec.message());
      co_return;
    }

    pending.append(buf.data(), len);

    while (true) {
      const std::size_t delimPos = pending.find(FRAME_DELIMITER);
      if (delimPos == std::string::npos)
        break;
      if (delimPos == 0) {
        pending.erase(0, 1);
        continue;
      }
      if (delimPos > Protocol::MAX_MESSAGE_SIZE) {
        logging::log("Invalid payload length: {}", delimPos);
        co_return;
      }

      std::string msg = pending.substr(0, delimPos);
      if (!msg.empty() && msg.back() == '\r')
        msg.pop_back();
      logging::log("Received: {}", msg);
      pending.erase(0, delimPos + 1);

      co_await handle_message(msg);
    }

    if (pending.size() > Protocol::MAX_MESSAGE_SIZE + 1) {
      logging::log("Payload without delimiter is too large: {}",
                   pending.size());
      co_return;
    }
  }
}

asio::awaitable<bool> Channel::send_message(const std::string &msg) {
  // Enforce size limits and newline framing.
  if (msg.empty() || msg.size() > Protocol::MAX_MESSAGE_SIZE) {
    logging::log("[channel][send] invalid payload size={}", msg.size());
    co_return false;
  }

  std::string frame = msg;
  if (frame.back() != FRAME_DELIMITER) {
    frame.push_back(FRAME_DELIMITER);
  }
  if (frame.size() > Protocol::MAX_MESSAGE_SIZE + 1) {
    logging::log("[channel][send] framed payload too large size={}",
                 frame.size());
    co_return false;
  }

  std::error_code ec;
  const std::size_t written =
      co_await asio::async_write(socket, asio::buffer(frame),
                                 asio::redirect_error(asio::use_awaitable, ec));
  if (ec) {
    logging::log("[channel][send] failed ec={} msg={} written={} payload={}",
                 ec.value(), ec.message(), written, msg);
    co_return false;
  }
  logging::log("[channel][send] ok written={} framed_size={} payload={}",
               written, frame.size(), msg);
  co_return !ec;
}

// ----------------------------------------------------------------------
// Parse, dispatch by type, and respond with a single envelope.
asio::awaitable<void> Channel::handle_message(std::string &msg) {
  json response =
      json(Protocol::ShortEnvelope::make_env(Protocol::SYSTEM_ERROR));

  try {
    auto j = json::parse(msg);
    response = server->dispatch_request(j, shared_from_this());

  } catch (const std::exception &e) {
    logging::log("Parse or dispatch failed: {}", e.what());
    response = json(Protocol::ShortEnvelope::make_env(
        Protocol::SYSTEM_ERROR | Protocol::DESERIALIZE_FAIL));
  }

  // Send response outside try-catch to avoid co_await issue
  bool sent = co_await send_message(response.dump());
  if (!sent) {
    logging::log("[channel][send] response send failed");
    co_return;
  }
}