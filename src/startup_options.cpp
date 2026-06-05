#include "startup_options.h"

#include <cstdlib>
#include <fstream>
#include <string>

#include "battle_config_loader.h"
#include "battle_config.h"
#include "logging.h"
#include "server.h"

namespace {

std::string resolve_default_config_path() {
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

bool parse_positive_double(const std::string &text, double &out) {
  try {
    const double value = std::stod(text);
    if (value <= 0.0) {
      return false;
    }
    out = value;
    return true;
  } catch (...) {
    return false;
  }
}

} // namespace

bool parse_startup_options(int argc, char **argv, StartupOptions &out) {
  out = StartupOptions{};
  out.configPath = resolve_default_config_path();

  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];
    if (arg == "--config" && i + 1 < argc) {
      out.configPath = argv[++i];
      continue;
    }
    if (arg.starts_with("--config=")) {
      out.configPath = arg.substr(9);
      continue;
    }
    if (arg == "--battle-config" && i + 1 < argc) {
      out.battleConfigPath = argv[++i];
      continue;
    }
    if (arg.starts_with("--battle-config=")) {
      out.battleConfigPath = arg.substr(16);
      continue;
    }
    if (arg == "--duration-seconds" && i + 1 < argc) {
      double duration = 0.0;
      if (!parse_positive_double(argv[++i], duration)) {
        logging::log("Invalid --duration-seconds value: {}", argv[i]);
        return false;
      }
      out.durationSecondsOverride = duration;
      continue;
    }
    if (arg.starts_with("--duration-seconds=")) {
      double duration = 0.0;
      if (!parse_positive_double(arg.substr(19), duration)) {
        logging::log("Invalid --duration-seconds value");
        return false;
      }
      out.durationSecondsOverride = duration;
      continue;
    }
  }

  return true;
}

void apply_startup_battle_config(const std::shared_ptr<ServerState> &state,
                                 const StartupOptions &options) {
  if (!state) {
    return;
  }

  Battle::BattleConfig cfg =
      options.battleConfigPath.has_value()
          ? battle_config_loader::load_battle_config_from_file(
                *options.battleConfigPath)
          : battle_config_loader::load_default_battle_config();

  if (options.durationSecondsOverride.has_value()) {
    cfg.durationSeconds = *options.durationSecondsOverride;
  }

  state->battleConfig = std::move(cfg);
  logging::log("Battle config: durationSeconds={}", state->battleConfig.durationSeconds);
}
