#pragma once

#include <chrono>
#include <cstdint>
#include <memory>
#include <thread>

#include <asio/io_context.hpp>

#include "network_test_framework.h"

namespace network_tests {

/** 与生产 `main.cpp` 一致：多端口服务共享同一 `ServerState`。 */
class MultiServiceHarness {
public:
  MultiServiceHarness() {
    state = std::make_shared<ServerState>();

    // Allocate all ports first, then construct servers with the exact values.
    // (Member initialization order in C++ is declaration-based, not list-based.)
    authPort = allocate_free_port();
    lobbyPort = allocate_free_port();
    shopPort = allocate_free_port();
    mapPort = allocate_free_port();
    battlePort = allocate_free_port();

    loginServer = std::make_shared<LoginServer>(ioContext, static_cast<int>(authPort),
                                                 state);
    homeServer =
        std::make_shared<HomeServer>(ioContext, static_cast<int>(lobbyPort),
                                       state);
    shopServer =
        std::make_shared<ShopServer>(ioContext, static_cast<int>(shopPort),
                                     state);
    mapServer =
        std::make_shared<MapServer>(ioContext, static_cast<int>(mapPort), state);
    battleServer =
        std::make_shared<BattleServer>(ioContext,
                                         static_cast<int>(battlePort), state);

    loginServer->start();
    homeServer->start();
    shopServer->start();
    mapServer->start();
    battleServer->start();

    ioThread = std::thread([this]() { ioContext.run(); });

    wait_port(authPort);
    wait_port(lobbyPort);
    wait_port(shopPort);
    wait_port(mapPort);
    wait_port(battlePort);
  }

  ~MultiServiceHarness() {
    ioContext.stop();
    if (ioThread.joinable()) {
      ioThread.join();
    }
  }

  MultiServiceHarness(const MultiServiceHarness &) = delete;
  MultiServiceHarness &operator=(const MultiServiceHarness &) = delete;

  std::shared_ptr<ServerState> get_state() const { return state; }

  std::uint16_t get_auth_port() const { return authPort; }
  std::uint16_t get_lobby_port() const { return lobbyPort; }
  std::uint16_t get_shop_port() const { return shopPort; }
  std::uint16_t get_map_port() const { return mapPort; }
  std::uint16_t get_battle_port() const { return battlePort; }

  std::shared_ptr<FakeClient> make_auth_client() const {
    return connect_client(authPort);
  }

  std::shared_ptr<FakeClient> make_lobby_client() const {
    return connect_client(lobbyPort);
  }

  std::shared_ptr<FakeClient> make_shop_client() const {
    return connect_client(shopPort);
  }

  std::shared_ptr<FakeClient> make_map_client() const {
    return connect_client(mapPort);
  }

  std::shared_ptr<FakeClient> make_battle_client() const {
    return connect_client(battlePort);
  }

private:
  static std::shared_ptr<FakeClient> connect_client(std::uint16_t port) {
    auto client = std::make_shared<FakeClient>();
    client->connect("127.0.0.1", port);
    return client;
  }

  void wait_port(std::uint16_t port) const {
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

  asio::io_context ioContext;
  std::shared_ptr<ServerState> state;
  std::uint16_t authPort = 0;
  std::uint16_t lobbyPort = 0;
  std::uint16_t shopPort = 0;
  std::uint16_t mapPort = 0;
  std::uint16_t battlePort = 0;

  std::shared_ptr<LoginServer> loginServer;
  std::shared_ptr<HomeServer> homeServer;
  std::shared_ptr<ShopServer> shopServer;
  std::shared_ptr<MapServer> mapServer;
  std::shared_ptr<BattleServer> battleServer;
  std::thread ioThread;
};

} // namespace network_tests
