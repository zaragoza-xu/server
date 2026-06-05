#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include "battle_config.h"

namespace battle_config_loader {

Battle::BattleConfig load_battle_config_from_file(
    const std::filesystem::path &path);

Battle::BattleConfig load_default_battle_config();

Battle::BattleConfig load_battle_config_from_candidates(
    const std::vector<std::filesystem::path> &candidates);

} // namespace battle_config_loader
