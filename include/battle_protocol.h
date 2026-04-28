#pragma once

#include <optional>

#include "battle.h"
#include "nlohmann/detail/macro_scope.hpp"

namespace Protocol {

enum class BattleRequestType {
  PLAYER_READY = 0,
  PLAYER_MOVE = 1,
  PLAYER_SHOOT = 2,
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

  std::optional<SpawnParameter> spawnParameter;
  std::optional<HitParameter> hitParameter;
  std::optional<DamageParameter> damageParameter;
  std::optional<DestroyParameter> destroyParameter;
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
}
inline void from_json(const json &j, BattleEventDTO &e) {
  j.at("eventType").get_to(e.eventType);
  j.at("eventTick").get_to(e.eventTick);
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