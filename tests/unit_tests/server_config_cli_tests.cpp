#include <memory>

#include <gtest/gtest.h>

#include "battle_config_loader.h"
#include "server.h"
#include "startup_options.h"

namespace {

TEST(ServerConfigCliTest, ParseDurationSecondsOverride) {
  const char *argv[] = {"server", "--duration-seconds", "5"};
  StartupOptions options;
  ASSERT_TRUE(parse_startup_options(3, const_cast<char **>(argv), options));
  ASSERT_TRUE(options.durationSecondsOverride.has_value());
  EXPECT_DOUBLE_EQ(*options.durationSecondsOverride, 5.0);
}

TEST(ServerConfigCliTest, RejectsNonPositiveDuration) {
  const char *argv[] = {"server", "--duration-seconds", "0"};
  StartupOptions options;
  EXPECT_FALSE(parse_startup_options(3, const_cast<char **>(argv), options));
}

TEST(ServerConfigCliTest, ApplyDurationOverrideToState) {
  auto state = std::make_shared<ServerState>();
  StartupOptions options;
  options.durationSecondsOverride = 7.0;
  apply_startup_battle_config(state, options);
  EXPECT_DOUBLE_EQ(state->battleConfig.durationSeconds, 7.0);
  EXPECT_TRUE(Battle::battle_config_complete(state->battleConfig));
}

TEST(ServerConfigCliTest, LoadBattleConfigFromExplicitPath) {
  const auto cfg = battle_config_loader::load_battle_config_from_file(
      "config/battle_config.json");
  EXPECT_GT(cfg.durationSeconds, 0.0);
  EXPECT_TRUE(Battle::battle_config_complete(cfg));
}

} // namespace
