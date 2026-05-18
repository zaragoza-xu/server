#pragma once

#include <optional>

#include "types.h"

namespace Protocol {

// ===== 战斗实体基础 =====

enum class EntityType {
  PLAYER = 0,
  ENEMY = 1,
  PLAYER_BULLET = 10,
  ENEMY_BULLET = 11,
  WALL = 20,

  NONE = 999,
};

struct BattleVector2 {
  double x = 0.0;
  double y = 0.0;
  NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(BattleVector2, x, y)
};

struct BattleEntity {
  int entityId = 0;
  EntityType entityType = EntityType::PLAYER;
  BattleVector2 position = {0.0, 0.0};
  BattleVector2 direction = {0.0, 0.0};
  NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(BattleEntity, entityId,
                                              entityType, position, direction)
};

// ===== 玩家相关 =====

struct BattlePlayerAttribute {
  double velocity = 0.25;
  NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(BattlePlayerAttribute, velocity);
};

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
  int mapNodeId = -1;
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

enum class BattleEnemyType { BUBBLE_FISH = 0 };

enum class BattleEnemyIntent {
  IDLE = 0,
  CHASE = 1,
  ATTACK = 2,
};

struct BattleEnemyAttribute {
  int currentHP = 0;
  int maxHP = 0;
  NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(BattleEnemyAttribute, currentHP,
                                              maxHP)
};

struct BattleEnemyEntity : BattleEntity {
  BattleEnemyAttribute attribute;
  BattleEnemyType enemyType = BattleEnemyType::BUBBLE_FISH;
  std::string targetPlayerUid;
  BattleEnemyIntent currentIntent = BattleEnemyIntent::IDLE;
};
inline void to_json(json &j, const BattleEnemyEntity &e) {
  to_json(j, static_cast<const BattleEntity &>(e));
  j["attribute"] = e.attribute;
  j["enemyType"] = e.enemyType;
  j["targetPlayerUid"] = e.targetPlayerUid;
  j["currentIntent"] = e.currentIntent;
}
inline void from_json(const json &j, BattleEnemyEntity &e) {
  from_json(j, static_cast<BattleEntity &>(e));
  if (j.contains("attribute"))
    j.at("attribute").get_to(e.attribute);
  if (j.contains("enemyType"))
    j.at("enemyType").get_to(e.enemyType);
  if (j.contains("targetPlayerUid"))
    j.at("targetPlayerUid").get_to(e.targetPlayerUid);
  if (j.contains("currentIntent"))
    j.at("currentIntent").get_to(e.currentIntent);
}

enum class BattleRequestType {
  PLAYER_READY = 0,
  PLAYER_MOVE = 1,
  PLAYER_SHOOT = 2,
  ERROR = 100,
};

enum class BattleResponseType {
  BATTLE_WAIT = 0,
  BATTLE_FRAME = 1,
  ERROR = 100,
};

enum BattlePushMessageType {
  BATTLE_START = 0,
};

struct BattlePlayerReadyReq {
  BattleRequestType type;
  std::string uid;
  NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(BattlePlayerReadyReq, type, uid)
};

struct BattlePlayerMoveReq {
  BattleRequestType type;
  std::string uid;
  BattleVector2 input;
  NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(BattlePlayerMoveReq, type, uid,
                                              input)
};

struct BattlePlayerShootReq {
  BattleRequestType type;
  std::string uid;
  BattleVector2 direction;
  NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(BattlePlayerShootReq, type, uid,
                                              direction)
};

enum class BattleEventType {
  ENEMY_SPAWN = 0,
  BULLET_SPAWN = 1,

  BULLET_HIT_ENEMY = 10,
  BULLET_HIT_PLAYER = 11,
  BULLET_HIT_WALL = 12,

  ENTITY_DAMAGE = 20,
  ENTITY_DESTROY = 21,

  ENEMY_INTENT_CHANGE = 30,
};

enum class BattleEntityDestroyReason {
  UNKNOWN = 0,
  BULLET_HIT_ENTITY = 1,
  BULLET_HIT_WALL = 2,
  BULLET_TIMEOUT = 3,

  ENTITY_DEAD = 10,
  ENTITY_SELF_EXPLODE = 11,
};

// Only the parameter matching eventType should be populated.
struct BattleEventDTO {
  BattleEventType eventType = BattleEventType::ENEMY_SPAWN;
  int eventTick = 0;

  struct SpawnParameter {
    int entityId = 0;
    EntityType entityType = EntityType::PLAYER;
    BattlePlayerEntity playerEntity;
    BattleEnemyEntity enemyEntity;
    BattleBulletEntity bulletEntity;
    NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(SpawnParameter, entityId,
                                                entityType, playerEntity,
                                                enemyEntity, bulletEntity)
  };

  struct HitParameter {
    int sourceEntityId = 0;
    EntityType sourceEntityType = EntityType::PLAYER;
    int targetEntityId = 0;
    EntityType targetEntityType = EntityType::PLAYER;
    BattleVector2 hitPosition;
    NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(HitParameter, sourceEntityId,
                                                sourceEntityType,
                                                targetEntityId,
                                                targetEntityType, hitPosition)
  };

  struct DamageParameter {
    int sourceEntityId = 0;
    EntityType sourceEntityType = EntityType::PLAYER;
    int targetEntityId = 0;
    EntityType targetEntityType = EntityType::PLAYER;
    int damage = 0;
    int currentHP = 0;
    NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(DamageParameter, sourceEntityId,
                                                sourceEntityType,
                                                targetEntityId,
                                                targetEntityType, damage,
                                                currentHP)
  };

  struct DestroyParameter {
    int entityId = 0;
    EntityType entityType = EntityType::PLAYER;
    BattleEntityDestroyReason destroyReason =
        BattleEntityDestroyReason::UNKNOWN;
    NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(DestroyParameter, entityId,
                                                entityType, destroyReason)
  };

  struct IntentParameter {
    int enemyEntityId = 0;
    BattleEnemyIntent intent = BattleEnemyIntent::IDLE;
    std::string targetPlayerUid;
    NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(IntentParameter, enemyEntityId,
                                                intent, targetPlayerUid)
  };

  std::optional<SpawnParameter> spawnParameter;
  std::optional<HitParameter> hitParameter;
  std::optional<DamageParameter> damageParameter;
  std::optional<DestroyParameter> destroyParameter;
  std::optional<IntentParameter> intentParameter;
};
inline void to_json(json &j, const BattleEventDTO &e) {
  j["eventType"] = e.eventType;
  j["eventTick"] = e.eventTick;
  j["spawnParameter"] =
      e.spawnParameter ? json(e.spawnParameter.value()) : json(nullptr);
  j["hitParameter"] =
      e.hitParameter ? json(e.hitParameter.value()) : json(nullptr);
  j["damageParameter"] =
      e.damageParameter ? json(e.damageParameter.value()) : json(nullptr);
  j["destroyParameter"] =
      e.destroyParameter ? json(e.destroyParameter.value()) : json(nullptr);
  j["intentParameter"] =
      e.intentParameter ? json(e.intentParameter.value()) : json(nullptr);
}
inline void from_json(const json &j, BattleEventDTO &e) {
  j.at("eventType").get_to(e.eventType);
  j.at("eventTick").get_to(e.eventTick);
  e.spawnParameter.reset();
  e.hitParameter.reset();
  e.damageParameter.reset();
  e.destroyParameter.reset();
  e.intentParameter.reset();
  if (j.contains("spawnParameter") && !j["spawnParameter"].is_null())
    e.spawnParameter =
        j["spawnParameter"].get<BattleEventDTO::SpawnParameter>();
  if (j.contains("hitParameter") && !j["hitParameter"].is_null())
    e.hitParameter = j["hitParameter"].get<BattleEventDTO::HitParameter>();
  if (j.contains("damageParameter") && !j["damageParameter"].is_null())
    e.damageParameter =
        j["damageParameter"].get<BattleEventDTO::DamageParameter>();
  if (j.contains("destroyParameter") && !j["destroyParameter"].is_null())
    e.destroyParameter =
        j["destroyParameter"].get<BattleEventDTO::DestroyParameter>();
  if (j.contains("intentParameter") && !j["intentParameter"].is_null())
    e.intentParameter =
        j["intentParameter"].get<BattleEventDTO::IntentParameter>();
}

struct BattleWaitRsp {
  int gameFrame = 0;
  int readyCount = 0;
  int totalCount = 0;
  NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(BattleWaitRsp, gameFrame,
                                              readyCount, totalCount)
};

struct BattleFrameRsp {
  int serverTick = 0;
  std::vector<BattlePlayerEntity> playerEntities;
  std::vector<BattleEnemyEntity> enemyEntities;
  std::vector<BattleBulletEntity> bulletEntities;
  std::vector<BattleEventDTO> events;

  NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(BattleFrameRsp, serverTick,
                                              playerEntities, enemyEntities,
                                              bulletEntities, events);
};
} // namespace Protocol
