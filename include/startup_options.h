#pragma once

#include <memory>
#include <optional>
#include <string>

struct ServerState;

struct StartupOptions {
  std::string configPath = "config/server.json";
  std::optional<std::string> battleConfigPath;
  std::optional<double> durationSecondsOverride;
};

// Returns false on invalid arguments (e.g. non-positive duration).
bool parse_startup_options(int argc, char **argv, StartupOptions &out);

void apply_startup_battle_config(const std::shared_ptr<ServerState> &state,
                                 const StartupOptions &options);
