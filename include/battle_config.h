#pragma once

#include <string>
#include <utility>
#include <vector>

#include "battle.h"

namespace Battle {
enum class BattleEnemyPool { NORMAL, ELITE, BOSS };

struct BattleEnemyDef {
  BattleEnemyPool pool = BattleEnemyPool::NORMAL;
  Protocol::BattleEnemyType enemyType = Protocol::BattleEnemyType::BUBBLE_FISH;
  int maxHP = 10;
  double attackRange = 1.5;
  double maxSpeed = 1.0;
  double knockbackResist = 0.0;
  int attackDamage = 4;
  int attackCooldownTicks = 20;
  double cost = 3.0;
  double unlockTime = 0.0;
  double weight = 100.0;
};

struct BattleConfig {
  int playerMaxHP = 20;
  int frameRate = 60;
  double durationSeconds = 180.0;
  double spawnIntervalSeconds = 0.5;
  double baseSpawnBudget = 4.0;
  double difficultyGrowth = 0.08;
  double maxCostFactor = 3.0;
  double spawnRadiusMin = 8.0;
  double spawnRadiusMax = 12.0;
  double battleMin = -20.0;
  double battleMax = 20.0;
  double bulletSpeed = 1.0;
  int bulletDamage = 5;
  double bulletRadius = 0.25;
  double enemyRadius = 0.75;
  std::vector<BattleEnemyDef> enemies;
  std::vector<Battle::WeaponDef> weapons;
};

inline bool battle_config_complete(const BattleConfig &cfg) {
  return !cfg.enemies.empty() && !cfg.weapons.empty();
}

inline BattleConfig default_battle_config() {
  BattleConfig cfg;
  cfg.enemies = {
      {BattleEnemyPool::NORMAL, Protocol::BattleEnemyType::BUBBLE_FISH, 10, 1.5,
       1.0, 0.0, 4, 20, 3.0, 0.0, 100.0},
      {BattleEnemyPool::NORMAL, Protocol::BattleEnemyType::BUBBLE_FISH, 16, 1.6,
       1.15, 0.05, 5, 18, 6.0, 20.0, 40.0},
      {BattleEnemyPool::ELITE, Protocol::BattleEnemyType::BUBBLE_FISH, 18, 1.7,
       1.1, 0.1, 5, 18, 3.0, 0.0, 100.0},
      {BattleEnemyPool::ELITE, Protocol::BattleEnemyType::BUBBLE_FISH, 28, 1.9,
       1.25, 0.15, 7, 16, 6.0, 20.0, 45.0},
      {BattleEnemyPool::BOSS, Protocol::BattleEnemyType::BUBBLE_FISH, 36, 2.0,
       1.0, 0.25, 7, 16, 3.0, 0.0, 100.0},
      {BattleEnemyPool::BOSS, Protocol::BattleEnemyType::BUBBLE_FISH, 60, 2.2,
       1.2, 0.35, 10, 14, 8.0, 30.0, 30.0},
  };

  const double knifeDamages[] = {6.0, 9.0, 12.0, 20.0};
  const double knifeSpeeds[] = {1.01, 0.93, 0.86, 0.78};
  const double knifeCrits[] = {0.20, 0.30, 0.40, 0.50};
  for (int i = 0; i < 4; ++i) {
    Battle::WeaponDef weapon;
    weapon.weaponId = "knife_" + std::to_string(i + 1);
    weapon.weaponName = "knife";
    weapon.weaponType = Protocol::WeaponType::MELEE;
    weapon.damage = knifeDamages[i];
    weapon.attackSpeed = knifeSpeeds[i];
    weapon.range = 150.0;
    weapon.knockback = 2.0;
    weapon.damageGrowth = 0.80;
    weapon.critChance = knifeCrits[i];
    weapon.critMultiplier = 2.0;
    weapon.tags = {"weapon", "melee", "knife"};
    cfg.weapons.push_back(std::move(weapon));
  }

  const double pistolDamages[] = {12.0, 20.0, 30.0, 50.0};
  const double pistolSpeeds[] = {1.20, 1.20, 1.03, 0.87};
  const double pistolCrits[] = {0.05, 0.10, 0.15, 0.20};
  for (int i = 0; i < 4; ++i) {
    Battle::WeaponDef weapon;
    weapon.weaponId = "pistol_" + std::to_string(i + 1);
    weapon.weaponName = "pistol";
    weapon.weaponType = Protocol::WeaponType::RANGED;
    weapon.damage = pistolDamages[i];
    weapon.attackSpeed = pistolSpeeds[i];
    weapon.range = 500.0;
    weapon.knockback = 15.0;
    weapon.damageGrowth = 1.00;
    weapon.critChance = pistolCrits[i];
    weapon.critMultiplier = 2.0;
    weapon.projectilePrefab = "pistol_bullet";
    weapon.projectileCount = 1;
    weapon.projectile.speed = cfg.bulletSpeed;
    weapon.projectile.lifetime = weapon.range / 100.0 / cfg.bulletSpeed;
    weapon.projectile.size = cfg.bulletRadius;
    weapon.projectile.canPierce = true;
    weapon.projectile.pierceCount = 1;
    weapon.projectile.pierceDamageFactor = 0.75;
    weapon.tags = {"weapon", "ranged", "pistol"};
    cfg.weapons.push_back(std::move(weapon));
  }
  return cfg;
}
} // namespace Battle