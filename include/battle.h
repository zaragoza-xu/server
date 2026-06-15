#pragma once

#include <cmath>
#include <optional>
#include <unordered_set>
#include <utility>

#include "types.h"

namespace Battle {
struct BattleVector2 {
  double x = 0.0;
  double y = 0.0;
  NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(BattleVector2, x, y)
};
inline double distance_squared(const BattleVector2 &lhs,
                               const BattleVector2 &rhs) {
  const double dx = lhs.x - rhs.x;
  const double dy = lhs.y - rhs.y;
  return dx * dx + dy * dy;
}

inline double length_squared(const BattleVector2 &value) {
  return value.x * value.x + value.y * value.y;
}

inline BattleVector2 norm_or_zero(const BattleVector2 &value) {
  const double lengthSquared = length_squared(value);
  if (!std::isfinite(lengthSquared) || lengthSquared <= 0.0) {
    return {0.0, 0.0};
  }
  const double length = std::sqrt(lengthSquared);
  return {value.x / length, value.y / length};
}

}; // namespace Battle

namespace Protocol {
using Battle::BattleVector2;

// ===== 战斗实体基础 =====
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
  double speed = 1.5;
  int currentHP = 0;
  int maxHP = 0;
  NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(BattlePlayerAttribute, speed,
                                              currentHP, maxHP);
};

struct BattlePlayerEntity : BattleEntity {
  std::string uid;
  BattlePlayerAttribute attribute;
  std::vector<std::string> items; // 已拥有道具的 ID
  std::string weaponId;
};
inline void to_json(json &j, const BattlePlayerEntity &e) {
  to_json(j, static_cast<const BattleEntity &>(e));
  j["uid"] = e.uid;
  j["attribute"] = e.attribute;
  j["items"] = e.items;
  j["weaponId"] = e.weaponId;
}
inline void from_json(const json &j, BattlePlayerEntity &e) {
  from_json(j, static_cast<BattleEntity &>(e));
  if (j.contains("uid"))
    j.at("uid").get_to(e.uid);
  if (j.contains("attribute"))
    j.at("attribute").get_to(e.attribute);
  if (j.contains("items"))
    j.at("items").get_to(e.items);
  if (j.contains("weaponId"))
    j.at("weaponId").get_to(e.weaponId);
}

struct BattleInfo {
  RoomInfo roomInfo;
  BattlePlayerEntity playerInfo;
  int mapNodeId = -1;
  NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(BattleInfo, roomInfo, playerInfo,
                                              mapNodeId)
};

// ===== 中立相关 =====
struct BattleBulletAttribute {
  double speed = 0.0;
  double size = 0.0;

  NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(BattleBulletAttribute, speed,
                                              size)
};

struct BattleBulletEntity : BattleEntity {
  BattleBulletType type = BattleBulletType::SELF_BULLET;
  BattleBulletAttribute attribute;
};
inline void to_json(json &j, const BattleBulletEntity &e) {
  to_json(j, static_cast<const BattleEntity &>(e));
  j["type"] = e.type;
  j["attribute"] = e.attribute;
}
inline void from_json(const json &j, BattleBulletEntity &e) {
  from_json(j, static_cast<BattleEntity &>(e));
  if (j.contains("type"))
    j.at("type").get_to(e.type);
  if (j.contains("attribute"))
    j.at("attribute").get_to(e.attribute);
}

// ===== 敌人相关 =====

struct BattleEnemyAttribute {
  int currentHP = 0;
  int maxHP = 0;
  int speed = 3;
  int attackCoolDown = 1;
  NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(BattleEnemyAttribute, currentHP,
                                              maxHP, speed, attackCoolDown)
};

struct BattleEnemyEntity : BattleEntity {
  BattleEnemyAttribute attribute;
  BattleEnemyType enemyType = BattleEnemyType::BUBBLE_FISH;
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

struct BattlePlayerReadyReq {
  BattleRequestType type;
  std::string uid;
  NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(BattlePlayerReadyReq, type, uid)
};

struct BattlePos {
  int entityId = 0;
  BattleVector2 position;
  BattleVector2 direction;

  NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(BattlePos, entityId, position,
                                              direction)
};

struct BattleSyncReq {
  BattleRequestType type;
  std::string uid;
  BattleVector2 playerPosition;
  std::vector<BattlePos> enemyPositions;
  NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(BattleSyncReq, type, uid,
                                              playerPosition, enemyPositions)
};

struct BattlePlayerShootReq {
  BattleRequestType type;
  std::string uid;
  BattleVector2 direction;
  NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(BattlePlayerShootReq, type, uid,
                                              direction)
};

// Only the parameter matching eventType should be populated.
struct BattleEventDTO {
  BattleEventType eventType = BattleEventType::ENEMY_SPAWN;
  int eventTick = 0;

  struct SpawnParameter {
    int entityId = 0;
    EntityType entityType = EntityType::PLAYER;
    std::optional<BattlePlayerEntity> playerEntity;
    std::optional<BattleEnemyEntity> enemyEntity;
    std::optional<BattleBulletEntity> bulletEntity;

    SpawnParameter() = default;
    explicit SpawnParameter(const BattlePlayerEntity &entity)
        : entityId(entity.entityId), entityType(entity.entityType),
          playerEntity(entity) {}
    explicit SpawnParameter(const BattleEnemyEntity &entity)
        : entityId(entity.entityId), entityType(entity.entityType),
          enemyEntity(entity) {}
    explicit SpawnParameter(const BattleBulletEntity &entity)
        : entityId(entity.entityId), entityType(entity.entityType),
          bulletEntity(entity) {}
  };

  struct HitParameter {
    int sourceEntityId = 0;
    EntityType sourceEntityType = EntityType::PLAYER;
    int targetEntityId = 0;
    EntityType targetEntityType = EntityType::PLAYER;
    BattleVector2 hitPosition;

    HitParameter() = default;
    HitParameter(int sourceEntityId, EntityType sourceEntityType,
                 int targetEntityId, EntityType targetEntityType,
                 const BattleVector2 &hitPosition)
        : sourceEntityId(sourceEntityId), sourceEntityType(sourceEntityType),
          targetEntityId(targetEntityId), targetEntityType(targetEntityType),
          hitPosition(hitPosition) {}

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

    DamageParameter() = default;
    DamageParameter(int sourceEntityId, EntityType sourceEntityType,
                    int targetEntityId, EntityType targetEntityType, int damage,
                    int currentHP)
        : sourceEntityId(sourceEntityId), sourceEntityType(sourceEntityType),
          targetEntityId(targetEntityId), targetEntityType(targetEntityType),
          damage(damage), currentHP(currentHP) {}

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

    DestroyParameter() = default;
    DestroyParameter(int entityId, EntityType entityType,
                     BattleEntityDestroyReason destroyReason)
        : entityId(entityId), entityType(entityType),
          destroyReason(destroyReason) {}

    NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(DestroyParameter, entityId,
                                                entityType, destroyReason)
  };

  struct IntentParameter {
    int enemyEntityId = 0;
    std::string targetPlayerUid;

    IntentParameter() = default;
    IntentParameter(int enemyEntityId, std::string targetPlayerUid)
        : enemyEntityId(enemyEntityId),
          targetPlayerUid(std::move(targetPlayerUid)) {}

    NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(IntentParameter, enemyEntityId,
                                                targetPlayerUid)
  };

  std::optional<SpawnParameter> spawnParameter;
  std::optional<HitParameter> hitParameter;
  std::optional<DamageParameter> damageParameter;
  std::optional<DestroyParameter> destroyParameter;
  std::optional<IntentParameter> intentParameter;
};
inline void to_json(json &j, const BattleEventDTO::SpawnParameter &p) {
  j["entityId"] = p.entityId;
  j["entityType"] = p.entityType;
  j["playerEntity"] =
      p.playerEntity ? json(p.playerEntity.value()) : json(nullptr);
  j["enemyEntity"] =
      p.enemyEntity ? json(p.enemyEntity.value()) : json(nullptr);
  j["bulletEntity"] =
      p.bulletEntity ? json(p.bulletEntity.value()) : json(nullptr);
}
inline void from_json(const json &j, BattleEventDTO::SpawnParameter &p) {
  if (j.contains("entityId"))
    j.at("entityId").get_to(p.entityId);
  if (j.contains("entityType"))
    j.at("entityType").get_to(p.entityType);
  p.playerEntity.reset();
  p.enemyEntity.reset();
  p.bulletEntity.reset();
  if (j.contains("playerEntity") && !j.at("playerEntity").is_null())
    p.playerEntity = j.at("playerEntity").get<BattlePlayerEntity>();
  if (j.contains("enemyEntity") && !j.at("enemyEntity").is_null())
    p.enemyEntity = j.at("enemyEntity").get<BattleEnemyEntity>();
  if (j.contains("bulletEntity") && !j.at("bulletEntity").is_null())
    p.bulletEntity = j.at("bulletEntity").get<BattleBulletEntity>();
}
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
  std::vector<BattleEventDTO> events;

  NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(BattleFrameRsp, serverTick,
                                              playerEntities, events);
};
} // namespace Protocol

namespace Battle {
struct EnemySpawnSpec {
  Protocol::BattleEnemyType enemyType = Protocol::BattleEnemyType::BUBBLE_FISH;
  Battle::BattleVector2 position;
  int maxHP = 10;
  double attackRange = 1.5;
  double maxSpeed = 1.0;
  int attackDamage = 4;
  int attackCooldownTicks = 20;
  double cost = 1.0;
  double unlockTime = 0.0;
  double weight = 1.0;
};

struct ProjectileDef {
  double speed = 0.0;
  double lifetime = 0.0;
  double size = 0.0;
  bool canPierce = false;
  int pierceCount = 0;
  double pierceDamageFactor = 0.75;
  bool canBounce = false;
  int bounceCount = 0;
  bool explosion = false;
  double explosionRadius = 0.0;
};

struct WeaponDef {
  std::string weaponId;
  std::string weaponName;
  std::string icon;
  double damage = 0.0;
  double attackSpeed = 0.0;
  double range = 0.0;
  double knockback = 0.0;
  double damageGrowth = 0.0;
  double attackSpeedGrowth = 0.0;
  double critChance = 0.0;
  double critMultiplier = 1.0;
  double lifeSteal = 0.0;
  int projectileCount = 0;
  ProjectileDef projectile;
  std::vector<std::string> tags;
};

struct EnemyState {
  Protocol::BattleEnemyEntity entity;
  BattleVector2 lastPosition;
  double attackRange = 1.5;
  double maxSpeed = 1.0;
  int attackDamage = 4;
  int attackCooldownTicks = 20;
  int nextAttackTick = 0;
};

struct BulletState {
  Protocol::BattleBulletEntity entity;
  std::string sourceUid;
  int damage = 5;
  int remainingPierce = 0;
  double pierceScale = 1.0;
  double pierceDamageFactor = 0.75;
  double rangeLeft = 0.0;
  Battle::WeaponDef weapon;
  std::unordered_set<int> hitEnemyIds;
};
} // namespace Battle
