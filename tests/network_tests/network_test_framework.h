#pragma once

#include <chrono>
#include <cstdint>
#include <memory>
#include <thread>

#include <asio/io_context.hpp>
#include <asio/ip/tcp.hpp>

#include "fake_client.h"
#include "server.h"

namespace network_tests {

inline std::uint16_t allocate_free_port() {
  asio::io_context context;
  asio::ip::tcp::acceptor probe(context, asio::ip::tcp::endpoint(
                                             asio::ip::tcp::v4(), 0));
  return probe.local_endpoint().port();
}

template <typename ServerT> class ServerHarness {
public:
  ServerHarness()
      : port(allocate_free_port()), state(std::make_shared<ServerState>()),
        server(std::make_shared<ServerT>(ioContext, static_cast<int>(port),
                                         state)) {
    server->start();
    ioThread = std::thread([this]() { ioContext.run(); });
    wait_until_running();
  }

  ~ServerHarness() {
    ioContext.stop();
    if (ioThread.joinable()) {
      ioThread.join();
    }
  }

  ServerHarness(const ServerHarness &) = delete;
  ServerHarness &operator=(const ServerHarness &) = delete;

  std::uint16_t get_port() const { return port; }

  std::shared_ptr<FakeClient> make_client() const {
    auto client = std::make_shared<FakeClient>();
    client->connect("127.0.0.1", port);
    return client;
  }

private:
  void wait_until_running() const {
    for (int i = 0; i < 50; ++i) {
      try {
        FakeClient probe;
        probe.connect("127.0.0.1", port, 1);
        probe.close();
        return;
      } catch (...) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
      }
    }
  }

  std::uint16_t port;
  std::shared_ptr<ServerState> state;
  asio::io_context ioContext;
  std::shared_ptr<ServerT> server;
  std::thread ioThread;
};

} // namespace network_tests
