#include "channel.h"

#include <cstddef>
#include <nlohmann/json.hpp>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>

#include <asio/async_result.hpp>
#include <asio/awaitable.hpp>
#include <asio/redirect_error.hpp>
#include <asio/use_awaitable.hpp>
#include <asio/write.hpp>

#include "logging.h"
#include "protocol.h"
#include "server.h"

namespace {
constexpr char FRAME_DELIMITER = '\n';
constexpr std::string_view RESET = "\033[0m";
constexpr std::string_view DIM = "\033[2m";
constexpr std::string_view CYAN = "\033[36m";
constexpr std::string_view GREEN = "\033[32m";
constexpr std::string_view YELLOW = "\033[33m";
constexpr std::string_view MAGENTA = "\033[35m";
constexpr std::string_view RED = "\033[31m";

bool important_log_key(std::string_view key) {
  return key == "type" || key == "code" || key == "message" ||
         key == "pushMessages" || key == "serverTick" || key == "eventType" ||
         key == "entityType" || key == "uid" || key == "roomId" ||
         key == "entityId";
}

std::string paint(std::string_view color, std::string_view text) {
  std::string out;
  out.reserve(color.size() + text.size() + RESET.size());
  out.append(color);
  out.append(text);
  out.append(RESET);
  return out;
}

json omit_nulls(const json &value) {
  if (value.is_null()) {
    return nullptr;
  }
  if (value.is_primitive()) {
    return value;
  }
  if (value.is_array()) {
    json array = json::array();
    for (const auto &child : value) {
      array.push_back(omit_nulls(child));
    }
    return array;
  }

  json object = json::object();
  for (const auto &[key, child] : value.items()) {
    if (child.is_null()) {
      continue;
    }
    object[key] = omit_nulls(child);
  }
  return object;
}

std::string format_json_log(const json &value, int depth = 0,
                            std::string_view key = {}) {
  if (value.is_object()) {
    std::ostringstream out;
    out << "{";
    bool first = true;
    for (const auto &[childKey, child] : value.items()) {
      if (!first) {
        out << ",";
      }
      first = false;
      const std::string keyText = json(childKey).dump();
      out << (important_log_key(childKey) ? paint(YELLOW, keyText)
                                          : paint(CYAN, keyText));
      out << ":";
      out << format_json_log(child, depth + 1, childKey);
    }
    out << "}";
    return out.str();
  }
  if (value.is_array()) {
    std::ostringstream out;
    out << "[";
    for (std::size_t index = 0; index < value.size(); ++index) {
      if (index > 0) {
        out << ",";
      }
      out << format_json_log(value.at(index), depth + 1);
    }
    out << "]";
    return out.str();
  }
  if (value.is_string()) {
    return json(value.get<std::string>()).dump();
  }
  if (value.is_boolean()) {
    return paint(value.get<bool>() ? GREEN : RED, value.dump());
  }
  if (value.is_number()) {
    return paint(important_log_key(key) ? YELLOW : MAGENTA, value.dump());
  }
  return paint(DIM, value.dump());
}

std::string payload_log(const std::string &payload) {
  try {
    return format_json_log(omit_nulls(json::parse(payload)));
  } catch (const std::exception &) {
    return payload;
  }
}
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
                 ec.value(), ec.message(), written, payload_log(msg));
    co_return false;
  }
  logging::log("[channel][send] ok written={} framed_size={} payload={}",
               written, frame.size(), payload_log(msg));
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

  if (response.is_null()) {
    logging::log("[channel][send] skipped: no response requested");
    co_return;
  }

  // Send response outside try-catch to avoid co_await issue
  bool sent = co_await send_message(response.dump());
  if (!sent) {
    logging::log("[channel][send] response send failed");
    co_return;
  }
}
