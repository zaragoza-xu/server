#include "battle_config_loader.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <optional>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "types.h"

using json = nlohmann::json;

namespace battle_config_loader {
namespace {
using namespace Battle;

std::string lower(std::string value) {
  std::transform(
      value.begin(), value.end(), value.begin(),
      [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
  return value;
}

std::optional<BattleEnemyPool> parse_pool(const json &value) {
  if (value.is_number_integer()) {
    switch (value.get<int>()) {
    case 0:
      return BattleEnemyPool::NORMAL;
    case 1:
      return BattleEnemyPool::ELITE;
    case 2:
      return BattleEnemyPool::BOSS;
    default:
      return std::nullopt;
    }
  }
  if (!value.is_string()) {
    return std::nullopt;
  }

  const auto text = lower(value.get<std::string>());
  if (text == "normal") {
    return BattleEnemyPool::NORMAL;
  }
  if (text == "elite") {
    return BattleEnemyPool::ELITE;
  }
  if (text == "boss") {
    return BattleEnemyPool::BOSS;
  }
  return std::nullopt;
}

std::optional<Protocol::BattleEnemyType> parse_enemy(const json &value) {
  if (value.is_number_integer()) {
    if (value.get<int>() ==
        static_cast<int>(Protocol::BattleEnemyType::BUBBLE_FISH)) {
      return Protocol::BattleEnemyType::BUBBLE_FISH;
    }
    return std::nullopt;
  }
  if (!value.is_string()) {
    return std::nullopt;
  }

  const auto text = lower(value.get<std::string>());
  if (text == "bubble_fish" || text == "bubblefish") {
    return Protocol::BattleEnemyType::BUBBLE_FISH;
  }
  return std::nullopt;
}

template <typename T>
void read_num(const json &j, const char *key, T &target) {
  if (j.contains(key) && j.at(key).is_number()) {
    target = static_cast<T>(j.at(key).get<double>());
  }
}

void read_str(const json &j, const char *key, std::string &target) {
  if (j.contains(key) && j.at(key).is_string()) {
    target = j.at(key).get<std::string>();
  }
}

void read_bool(const json &j, const char *key, bool &target) {
  if (j.contains(key) && j.at(key).is_boolean()) {
    target = j.at(key).get<bool>();
  }
}

bool valid_battle_config(const BattleConfig &cfg) {
  return cfg.playerMaxHP > 0 && cfg.frameRate > 0 &&
         cfg.durationSeconds > 0.0 && cfg.spawnIntervalSeconds > 0.0 &&
         cfg.baseSpawnBudget > 0.0 && cfg.maxCostFactor > 0.0 &&
         cfg.spawnRadiusMin >= 0.0 &&
         cfg.spawnRadiusMax >= cfg.spawnRadiusMin &&
         cfg.battleMax > cfg.battleMin && cfg.bulletSpeed > 0.0 &&
         cfg.bulletDamage > 0 && cfg.bulletRadius >= 0.0 &&
         cfg.enemyRadius >= 0.0 && battle_config_complete(cfg);
}

bool valid_enemy(const BattleEnemyDef &enemy) {
  return enemy.maxHP > 0 && enemy.attackRange >= 0.0 && enemy.maxSpeed >= 0.0 &&
         enemy.knockbackResist >= 0.0 && enemy.knockbackResist <= 1.0 &&
         enemy.attackDamage >= 0 && enemy.attackCooldownTicks >= 0 &&
         enemy.cost > 0.0 && enemy.unlockTime >= 0.0 && enemy.weight > 0.0;
}

bool valid_weapon(const Battle::WeaponDef &weapon) {
  const auto &projectile = weapon.projectile;
  return !weapon.weaponId.empty() && !weapon.weaponName.empty() &&
         weapon.damage >= 0.0 && weapon.attackSpeed >= 0.0 &&
         weapon.range >= 0.0 && weapon.knockback >= 0.0 &&
         weapon.damageGrowth >= 0.0 && weapon.attackSpeedGrowth >= 0.0 &&
         weapon.critChance >= 0.0 && weapon.critChance <= 1.0 &&
         weapon.critMultiplier >= 1.0 && weapon.lifeSteal >= 0.0 &&
         weapon.projectileCount >= 0 && projectile.speed >= 0.0 &&
         projectile.lifetime >= 0.0 && projectile.size >= 0.0 &&
         projectile.pierceCount >= 0 && projectile.pierceDamageFactor >= 0.0 &&
         projectile.bounceCount >= 0 && projectile.explosionRadius >= 0.0;
}

void read_projectile(const json &j, Battle::ProjectileDef &projectile) {
  read_num(j, "speed", projectile.speed);
  read_num(j, "lifetime", projectile.lifetime);
  read_num(j, "size", projectile.size);
  read_bool(j, "canPierce", projectile.canPierce);
  read_num(j, "pierceCount", projectile.pierceCount);
  read_num(j, "pierceDamageFactor", projectile.pierceDamageFactor);
  read_bool(j, "canBounce", projectile.canBounce);
  read_num(j, "bounceCount", projectile.bounceCount);
  read_bool(j, "explosion", projectile.explosion);
  read_num(j, "explosionRadius", projectile.explosionRadius);
}

std::optional<Battle::WeaponDef> read_weapon(const json &entry) {
  if (!entry.is_object() || !entry.contains("weaponId")) {
    return std::nullopt;
  }

  Battle::WeaponDef weapon;
  read_str(entry, "weaponId", weapon.weaponId);
  read_str(entry, "weaponName", weapon.weaponName);
  read_str(entry, "icon", weapon.icon);
  read_num(entry, "damage", weapon.damage);
  read_num(entry, "attackSpeed", weapon.attackSpeed);
  read_num(entry, "range", weapon.range);
  read_num(entry, "knockback", weapon.knockback);
  read_num(entry, "damageGrowth", weapon.damageGrowth);
  read_num(entry, "attackSpeedGrowth", weapon.attackSpeedGrowth);
  read_num(entry, "critChance", weapon.critChance);
  read_num(entry, "critMultiplier", weapon.critMultiplier);
  read_num(entry, "lifeSteal", weapon.lifeSteal);
  read_num(entry, "projectileCount", weapon.projectileCount);

  if (entry.contains("projectile") && entry.at("projectile").is_object()) {
    read_projectile(entry.at("projectile"), weapon.projectile);
  }
  if (entry.contains("tags") && entry.at("tags").is_array()) {
    weapon.tags.clear();
    for (const auto &tag : entry.at("tags")) {
      if (tag.is_string()) {
        weapon.tags.push_back(tag.get<std::string>());
      }
    }
  }
  if (!valid_weapon(weapon)) {
    return std::nullopt;
  }
  return weapon;
}

std::optional<BattleConfig> parse_battle_config_json(const json &j) {
  BattleConfig cfg = default_battle_config();

  read_num(j, "playerMaxHP", cfg.playerMaxHP);
  read_num(j, "frameRate", cfg.frameRate);
  read_num(j, "durationSeconds", cfg.durationSeconds);
  read_num(j, "spawnIntervalSeconds", cfg.spawnIntervalSeconds);
  read_num(j, "baseSpawnBudget", cfg.baseSpawnBudget);
  read_num(j, "difficultyGrowth", cfg.difficultyGrowth);
  read_num(j, "maxCostFactor", cfg.maxCostFactor);
  read_num(j, "spawnRadiusMin", cfg.spawnRadiusMin);
  read_num(j, "spawnRadiusMax", cfg.spawnRadiusMax);
  read_num(j, "battleMin", cfg.battleMin);
  read_num(j, "battleMax", cfg.battleMax);
  read_num(j, "bulletSpeed", cfg.bulletSpeed);
  read_num(j, "bulletDamage", cfg.bulletDamage);
  read_num(j, "bulletRadius", cfg.bulletRadius);
  read_num(j, "enemyRadius", cfg.enemyRadius);

  if (j.contains("enemies") && j.at("enemies").is_array()) {
    std::vector<BattleEnemyDef> enemies;
    for (const auto &entry : j.at("enemies")) {
      if (!entry.is_object() || !entry.contains("pool") ||
          !entry.contains("enemyType")) {
        continue;
      }
      auto pool = parse_pool(entry.at("pool"));
      auto enemyType = parse_enemy(entry.at("enemyType"));
      if (!pool.has_value() || !enemyType.has_value()) {
        continue;
      }

      BattleEnemyDef enemy;
      enemy.pool = *pool;
      enemy.enemyType = *enemyType;
      read_num(entry, "maxHP", enemy.maxHP);
      read_num(entry, "attackRange", enemy.attackRange);
      read_num(entry, "maxSpeed", enemy.maxSpeed);
      read_num(entry, "knockbackResist", enemy.knockbackResist);
      read_num(entry, "attackDamage", enemy.attackDamage);
      read_num(entry, "attackCooldownTicks", enemy.attackCooldownTicks);
      read_num(entry, "cost", enemy.cost);
      read_num(entry, "unlockTime", enemy.unlockTime);
      read_num(entry, "weight", enemy.weight);
      if (valid_enemy(enemy)) {
        enemies.push_back(enemy);
      }
    }
    if (!enemies.empty()) {
      cfg.enemies = std::move(enemies);
    }
  }

  if (j.contains("weapons") && j.at("weapons").is_array()) {
    std::vector<Battle::WeaponDef> weapons;
    for (const auto &entry : j.at("weapons")) {
      auto weapon = read_weapon(entry);
      if (weapon.has_value()) {
        weapons.push_back(std::move(*weapon));
      }
    }
    if (!weapons.empty()) {
      cfg.weapons = std::move(weapons);
    }
  }

  if (valid_battle_config(cfg)) {
    return cfg;
  }
  return std::nullopt;
}

std::optional<BattleConfig> load_battle_config_stream(std::ifstream &input) {
  try {
    json j;
    input >> j;
    return parse_battle_config_json(j);
  } catch (...) {
    return std::nullopt;
  }
}
} // namespace

BattleConfig load_battle_config_from_file(
    const std::filesystem::path &path) {
  std::ifstream input(path);
  if (input.is_open()) {
    if (auto cfg = load_battle_config_stream(input)) {
      return *cfg;
    }
  }
  return default_battle_config();
}

BattleConfig load_battle_config_from_candidates(
    const std::vector<std::filesystem::path> &candidates) {
  for (const auto &path : candidates) {
    std::ifstream input(path);
    if (!input.is_open()) {
      continue;
    }
    if (auto cfg = load_battle_config_stream(input)) {
      return *cfg;
    }
  }
  return default_battle_config();
}

BattleConfig load_default_battle_config() {
  return load_battle_config_from_candidates(
      {"config/battle_config.json", "../config/battle_config.json",
       "../../config/battle_config.json"});
}

} // namespace battle_config_loader
