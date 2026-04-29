#pragma once

#include <chrono>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <thread>

#include <asio/connect.hpp>
#include <asio/io_context.hpp>
#include <asio/ip/tcp.hpp>
#include <asio/read_until.hpp>
#include <asio/streambuf.hpp>
#include <asio/write.hpp>
#include <nlohmann/json.hpp>

class FakeClient {
public:
  using json = nlohmann::json;
  using tcp = asio::ip::tcp;

  FakeClient() : socket(ioContext) {}

  void connect(const std::string &host, std::uint16_t port,
               int retries = 30) {
    std::exception_ptr lastError;
    for (int i = 0; i < retries; ++i) {
      try {
        tcp::resolver resolver(ioContext);
        auto endpoints = resolver.resolve(host, std::to_string(port));
        asio::connect(socket, endpoints);
        return;
      } catch (...) {
        lastError = std::current_exception();
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
      }
    }
    if (lastError) {
      std::rethrow_exception(lastError);
    }
    throw std::runtime_error("FakeClient failed to connect");
  }

  void close() {
    std::error_code ignored;
    socket.shutdown(tcp::socket::shutdown_both, ignored);
    socket.close(ignored);
  }

  void send_frame(const std::string &payload) {
    std::string framed = payload;
    if (framed.empty() || framed.back() != '\n') {
      framed.push_back('\n');
    }
    asio::write(socket, asio::buffer(framed));
  }

  /** Send bytes as-is (no automatic newline). */
  void send_raw(const std::string &data) {
    asio::write(socket, asio::buffer(data));
  }

  std::string read_frame() {
    asio::read_until(socket, readBuffer, '\n');
    std::istream is(&readBuffer);
    std::string line;
    std::getline(is, line);
    if (!line.empty() && line.back() == '\r') {
      line.pop_back();
    }
    return line;
  }

  void send_json(const json &j) { send_frame(j.dump()); }

  json read_json() { return json::parse(read_frame()); }

private:
  asio::io_context ioContext;
  tcp::socket socket;
  asio::streambuf readBuffer;
};
