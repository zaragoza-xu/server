#pragma once

#include <vector>

#include "battle.h"

enum class BattleEnemyPool { NORMAL, ELITE, BOSS };

struct BattleEnemyDef {
  BattleEnemyPool pool = BattleEnemyPool::NORMAL;
  Protocol::BattleEnemyType enemyType = Protocol::BattleEnemyType::BUBBLE_FISH;
  int maxHP = 10;
  double attackRange = 1.5;
  double maxSpeed = 1.0;
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
};

inline BattleConfig default_battle_config() {
  BattleConfig cfg;
  cfg.enemies = {
      {BattleEnemyPool::NORMAL, Protocol::BattleEnemyType::BUBBLE_FISH, 10, 1.5,
       1.0, 4, 20, 3.0, 0.0, 100.0},
      {BattleEnemyPool::NORMAL, Protocol::BattleEnemyType::BUBBLE_FISH, 16, 1.6,
       1.15, 5, 18, 6.0, 20.0, 40.0},
      {BattleEnemyPool::ELITE, Protocol::BattleEnemyType::BUBBLE_FISH, 18, 1.7,
       1.1, 5, 18, 3.0, 0.0, 100.0},
      {BattleEnemyPool::ELITE, Protocol::BattleEnemyType::BUBBLE_FISH, 28, 1.9,
       1.25, 7, 16, 6.0, 20.0, 45.0},
      {BattleEnemyPool::BOSS, Protocol::BattleEnemyType::BUBBLE_FISH, 36, 2.0,
       1.0, 7, 16, 3.0, 0.0, 100.0},
      {BattleEnemyPool::BOSS, Protocol::BattleEnemyType::BUBBLE_FISH, 60, 2.2,
       1.2, 10, 14, 8.0, 30.0, 30.0},
  };
  return cfg;
}
