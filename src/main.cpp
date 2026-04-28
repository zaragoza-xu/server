#include <cstdlib>
#include <exception>
#include <fstream>
#include <memory>
#include <set>
#include <string>

#include <asio/io_context.hpp>
#include <nlohmann/json.hpp>

#include "logging.h"
#include "server.h"

using json = nlohmann::json;

namespace {

struct RuntimeConfig {
  int authPort = 0;
  int lobbyPort = 0;
  int shopPort = 0;
  int mapPort = 0;
  int battlePort = 0;
};

std::string resolve_config_path(int argc, char **argv) {
  // Support: --config <path> or --config=<path>
  for (int i = 1; i < argc; ++i) {
    std::string arg = argv[i];
    if (arg == "--config" && i + 1 < argc) {
      return argv[i + 1];
    }
    if (arg.starts_with("--config=")) {
      return arg.substr(9);
    }
  }

  // Search default locations
  static const char *candidates[] = {
      "config/server.json",
      "../config/server.json",
  };
  for (const auto *path : candidates) {
    if (std::ifstream ifs(path); ifs.good()) {
      return path;
    }
  }
  return "config/server.json";
}

bool is_valid_port(int port) { return port >= 1 && port <= 65535; }

bool load_config(const std::string &path, RuntimeConfig &cfg) {
  std::ifstream ifs(path);
  if (!ifs.is_open()) {
    logging::log("Failed to open config file: {}", path);
    return false;
  }

  json j;
  try {
    j = json::parse(ifs);
  } catch (const json::parse_error &e) {
    logging::log("Failed to parse config file: {}", e.what());
    return false;
  }

  cfg.authPort = j.value("authPort", 0);
  cfg.lobbyPort = j.value("lobbyPort", 0);
  cfg.shopPort = j.value("shopPort", 0);
  cfg.mapPort = j.value("mapPort", 0);
  cfg.battlePort = j.value("battlePort", 0);
  return true;
}

bool validate_config(const RuntimeConfig &cfg) {
  if (!is_valid_port(cfg.authPort) || !is_valid_port(cfg.lobbyPort) ||
      !is_valid_port(cfg.shopPort) || !is_valid_port(cfg.mapPort) ||
      !is_valid_port(cfg.battlePort)) {
    logging::log("All ports must be in range 1-65535 (auth={}, lobby={}, "
                 "shop={}, map={}, battle={})",
                 cfg.authPort, cfg.lobbyPort, cfg.shopPort, cfg.mapPort,
                 cfg.battlePort);
    return false;
  }
  std::set<int> ports{cfg.authPort, cfg.lobbyPort, cfg.shopPort, cfg.mapPort,
                      cfg.battlePort};
  if (ports.size() != 5) {
    logging::log("All five ports must be different");
    return false;
  }
  return true;
}
} // namespace

int main(int argc, char **argv) {
  try {
    const std::string configPath = resolve_config_path(argc, argv);
    RuntimeConfig cfg;
    if (!load_config(configPath, cfg)) {
      return 1;
    }
    if (!validate_config(cfg)) {
      return 1;
    }

    asio::io_context io_context;
    auto sharedState = std::make_shared<ServerState>();
    auto authServer =
        std::make_shared<LoginServer>(io_context, cfg.authPort, sharedState);
    auto lobbyServer =
        std::make_shared<HomeServer>(io_context, cfg.lobbyPort, sharedState);
    auto shopServer =
        std::make_shared<ShopServer>(io_context, cfg.shopPort, sharedState);
    auto mapServer =
        std::make_shared<MapServer>(io_context, cfg.mapPort, sharedState);
    auto battleServer =
        std::make_shared<BattleServer>(io_context, cfg.battlePort, sharedState);

    authServer->start();
    lobbyServer->start();
    shopServer->start();
    mapServer->start();
    battleServer->start();

    logging::log("Config loaded from: {}", configPath);
    logging::log("Auth service listening on port {}", cfg.authPort);
    logging::log("Lobby service listening on port {}", cfg.lobbyPort);
    logging::log("Shop service listening on port {}", cfg.shopPort);
    logging::log("Map service listening on port {}", cfg.mapPort);
    logging::log("Battle service listening on port {}", cfg.battlePort);

    io_context.run();

  } catch (std::exception &e) {
    logging::log("{}", e.what());
  }

  return 0;
}