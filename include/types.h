#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

using json = nlohmann::json;

namespace Protocol {

constexpr int MAX_MESSAGE_SIZE = 65536;

enum class LoginRequestType {
  LOGIN = 0,
  REGISTER = 1,
  LOGOUT = 2,
  ERROR = 100
};

enum class HomeRequestType {
  CREATE_ROOM = 0,
  JOIN_ROOM = 1,
  LEAVE_ROOM = 2,
  LIST_ROOMS = 3,
  SEND_MESSAGE = 4,
  HEARTBEAT = 5,
  EDIT_PROFILE = 6,
  SET_READY = 7,
  BROADCAST = 8,
  GET_STATE_STATUS = 9,
  ERROR = 100
};

enum class HomePushMessageType { ALL_READY = 0, ERROR = 100 };

enum class ShopRequestType {
  SHOP_INIT = 0,
  SHOP_MOVE_CURSOR = 1,
  SHOP_BUY = 2,
  ERROR = 100
};

enum class ShopResponseType {
  SHOP_SYNC = 0,
};

enum class MapRequestType {
  MAP_INIT = 0,
  MAP_MOVE = 1,
};

enum class MapResponseType {
  MAP_INIT = 0,
  MAP_SYNC = 1,
};

enum Code : int {
  // Bitmask: low bits indicate status, higher bits carry detail flags.
  SUCCESS = 1,
  FAIL = 1 << 1,
  ERROR = 1 << 2,

  // Lower bits are status flags, higher bits are detail flags.
  TIME_OUT = 1 << 3,
  DESERIALIZE_FAIL = 1 << 4,
  CONNECTION_ERROR = 1 << 5,
  BAD_REQUEST = 1 << 6,
  NOT_FOUND = 1 << 7,
  ROOM_STATE_ERROR = 1 << 8,
  SHOP_INVALID_ITEM = 1 << 9,
  SHOP_ITEM_TAKEN = 1 << 10,
  SHOP_NOT_MEMBER = 1 << 11,

  SERVICE_SUCCESS = SUCCESS,
  SERVICE_FAIL = FAIL,
  SYSTEM_ERROR = ERROR
};

struct PlayerBasicInfo {
  std::string uid;
  std::string name;
  int color = -1;

  NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(PlayerBasicInfo, uid, name, color)
};

struct PlayerData {
  PlayerBasicInfo basicInfo;

  NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(PlayerData, basicInfo)
};

struct RoomInfo {
  int roomId;
  size_t maximumPeople;
  std::vector<PlayerBasicInfo> basicInfos;
  std::vector<std::string> readyUids;

  NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(RoomInfo, roomId, maximumPeople,
                                              basicInfos, readyUids)
};

struct ShopItem {
  enum class Status {
    UNDO = 0,
    SELECT = 1,
    BUY = 2,
  };
  std::string itemId;
  Status itemStatus = Status::UNDO;
  std::string selectUid;

  NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(ShopItem, itemId, itemStatus,
                                              selectUid)
};

struct MapNode {
  enum class NodeType { NORMAL, ELITE, EVENT, BOSS };
  int nodeId = -1;
  NodeType type = NodeType::NORMAL;
  std::vector<int> nextId;

  NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(MapNode, nodeId, type, nextId)
};

enum class BattleRequestType {
  PLAYER_READY = 0,
  POSITION_SYNC = 1,
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
  BATTLE_END = 1,
};

enum class EntityType {
  PLAYER = 0,
  ENEMY = 1,
  PLAYER_BULLET = 10,
  ENEMY_BULLET = 11,
  WALL = 20,

  NONE = 999,
};

enum class BattleEnemyType { BUBBLE_FISH = 0 };

enum class BattleBulletType {
  SELF_BULLET = 0,
  PLAYER_BULLET = 1,
  ENEMY_BULLET = 2
};

enum class WeaponType {
  MELEE = 0,
  RANGED = 1,
};

enum class BattleEventType {
  ENEMY_SPAWN = 0,
  BULLET_SPAWN = 1,

  BULLET_HIT_ENEMY = 10,
  BULLET_HIT_PLAYER = 11,
  BULLET_HIT_WALL = 12,
  WEAPON_HIT_ENEMY = 13,

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

} // namespace Protocol
