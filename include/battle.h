#pragma once

#include "types.h"

namespace Protocol {

// ===== 战斗实体基础 =====

enum class EntityType {
  PLAYER = 0,
  ENEMY = 1,
  PLAYER_BULLET = 10,
  ENEMY_BULLET = 11,
  WALL = 20
};

struct BattleVector2 {
  float x = 0.0f;
  float y = 0.0f;
  NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(BattleVector2, x, y)
};

struct BattleEntity {
  int entityId = 0;
  EntityType entityType = EntityType::PLAYER;
  BattleVector2 position;
  BattleVector2 direction;
  NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(BattleEntity, entityId,
                                              entityType, position, direction)
};

// ===== 玩家相关 =====

struct BattlePlayerAttribute {
  // 预留扩展
};
inline void to_json(json &j, const BattlePlayerAttribute &) {
  j = json::object();
}
inline void from_json(const json &, BattlePlayerAttribute &) {}

struct BattlePlayerEntity : BattleEntity {
  std::string uid;
  BattlePlayerAttribute attribute;
  std::vector<std::string> items; // 已拥有道具的 ID
};
inline void to_json(json &j, const BattlePlayerEntity &e) {
  to_json(j, static_cast<const BattleEntity &>(e));
  j["uid"] = e.uid;
  j["attribute"] = e.attribute;
  j["items"] = e.items;
}
inline void from_json(const json &j, BattlePlayerEntity &e) {
  from_json(j, static_cast<BattleEntity &>(e));
  if (j.contains("uid"))
    j.at("uid").get_to(e.uid);
  if (j.contains("attribute"))
    j.at("attribute").get_to(e.attribute);
  if (j.contains("items"))
    j.at("items").get_to(e.items);
}

struct BattleInfo {
  RoomInfo roomInfo;
  BattlePlayerEntity playerInfo;
  std::string mapNodeId;
  NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(BattleInfo, roomInfo, playerInfo,
                                              mapNodeId)
};

// ===== 中立相关 =====

struct BattleBulletEntity : BattleEntity {
  // 预留扩展
};
inline void to_json(json &j, const BattleBulletEntity &e) {
  to_json(j, static_cast<const BattleEntity &>(e));
}
inline void from_json(const json &j, BattleBulletEntity &e) {
  from_json(j, static_cast<BattleEntity &>(e));
}

// ===== 敌人相关 =====

enum class EnemyType { BUBBLE_FISH = 0 };

struct BattleEnemyAttribute {
  int currentHP = 0;
  int maxHP = 0;
  NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(BattleEnemyAttribute, currentHP,
                                              maxHP)
};

struct BattleEnemyEntity : BattleEntity {
  BattleEnemyAttribute attribute;
  EnemyType enemyType = EnemyType::BUBBLE_FISH;
  std::string targetPlayerUid;
};
inline void to_json(json &j, const BattleEnemyEntity &e) {
  to_json(j, static_cast<const BattleEntity &>(e));
  j["attribute"] = e.attribute;
  j["enemyType"] = e.enemyType;
  j["targetPlayerUid"] = e.targetPlayerUid;
}
inline void from_json(const json &j, BattleEnemyEntity &e) {
  from_json(j, static_cast<BattleEntity &>(e));
  if (j.contains("attribute"))
    j.at("attribute").get_to(e.attribute);
  if (j.contains("enemyType"))
    j.at("enemyType").get_to(e.enemyType);
  if (j.contains("targetPlayerUid"))
    j.at("targetPlayerUid").get_to(e.targetPlayerUid);
}

} // namespace Protocol
