#include <cstdlib>
#include <exception>
#include <getopt.h>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

#include <asio/io_context.hpp>

#include "logging.h"
#include "server.h"

namespace {

struct RuntimeConfig {
  std::optional<int> authPort;
  std::optional<int> lobbyPort;
};

enum class ParseStatus {
  Ok,
  Help,
  Error,
};

struct ParseResult {
  ParseStatus status = ParseStatus::Error;
  RuntimeConfig config;
};

bool try_parse_port(std::string_view value, int &port) {
  if (value.empty()) {
    return false;
  }
  try {
    const int parsed = std::stoi(std::string(value));
    if (parsed < 1 || parsed > 65535) {
      return false;
    }
    port = parsed;
    return true;
  } catch (...) {
    return false;
  }
}

void print_usage() {
  logging::log("Usage: server --auth-port <1-65535> --lobby-port <1-65535>");
}

ParseStatus parse_args(int argc, char **argv, RuntimeConfig &cfg) {
  static option longOptions[] = {
      {"auth-port", required_argument, nullptr, 'a'},
      {"lobby-port", required_argument, nullptr, 'l'},
      {"help", no_argument, nullptr, 'h'},
      {nullptr, 0, nullptr, 0},
  };

  opterr = 0;
  optind = 1;

  while (true) {
    int optionIndex = 0;
    const int opt = getopt_long(argc, argv, "", longOptions, &optionIndex);
    if (opt == -1) {
      break;
    }

    if (opt == 'h') {
      print_usage();
      return ParseStatus::Help;
    }

    if (opt == 'a' || opt == 'l') {
      int port = 0;
      if (!try_parse_port(optarg, port)) {
        print_usage();
        return ParseStatus::Error;
      }

      if (opt == 'a') {
        cfg.authPort = port;
      } else {
        cfg.lobbyPort = port;
      }
      continue;
    }
    print_usage();
    return ParseStatus::Error;
  }

  if (optind < argc) {
    print_usage();
    return ParseStatus::Error;
  }
  return ParseStatus::Ok;
}

bool validate_config(const RuntimeConfig &cfg) {
  if (!cfg.authPort.has_value() || !cfg.lobbyPort.has_value()) {
    print_usage();
    return false;
  }
  if (cfg.authPort == cfg.lobbyPort) {
    logging::log("auth and lobby ports must be different");
    return false;
  }
  return true;
}
} // namespace

int main(int argc, char **argv) {
  try {
    RuntimeConfig cfg;
    ParseStatus status = parse_args(argc, argv, cfg);
    if (status == ParseStatus::Help) {
      return 0;
    }
    if (status == ParseStatus::Error) {
      return 1;
    }
    if (!validate_config(cfg)) {
      return 1;
    }

    asio::io_context io_context;
    auto sharedState = std::make_shared<ServerState>();
    auto authServer =
        std::make_shared<LoginServer>(io_context, *cfg.authPort, sharedState);
    auto lobbyServer =
        std::make_shared<HomeServer>(io_context, *cfg.lobbyPort, sharedState);

    logging::log("Auth service listening on port {}", *cfg.authPort);
    logging::log("Lobby service listening on port {}", *cfg.lobbyPort);

    io_context.run();

  } catch (std::exception &e) {
    logging::log("{}", e.what());
  }

  return 0;
}