#pragma once

#include <algorithm>
#include <cstddef>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "battle.h"
#include "protocol.h"

class User;
struct ServerState;

class Room {
private:
  struct BattleEnemyState {
    Protocol::BattleEnemyEntity entity;
    double attackRange = 1.5;
    double maxSpeed = 1.0;
    int attackDamage = 4;
    int attackCooldownTicks = 20;
    int nextAttackTick = 0;
  };

  struct BulletState {
    Protocol::BattleBulletEntity entity;
    int sourcePlayerId = 0;
    int damage = 5;
  };

  struct EnemySpawnSpec {
    Protocol::BattleEnemyType enemyType =
        Protocol::BattleEnemyType::BUBBLE_FISH;
    Protocol::BattleVector2 position;
    int maxHP = 10;
  };

  std::weak_ptr<ServerState> state;
  int roomId;
  int mapNodeId = -1;
  size_t maximumPeople;
  std::vector<std::string> uids;
  std::unordered_map<std::string, bool> readyStates;

  // shop info
  std::vector<std::string> shopItemIds;
  std::string shopCatalogVersion;
  std::unordered_set<std::string> takenItems;
  std::unordered_map<std::string, std::string> selectedItemByUid;
  std::unordered_map<std::string, std::vector<std::string>> ownedItemsByUid;

  // map info
  std::vector<Protocol::MapNode> mapNodes;
  std::unordered_map<std::string, int> selectedMapNodeByUid;

  // battle info
  std::unordered_map<std::string, bool> battleReadyStates;
  std::unordered_map<std::string, Protocol::BattlePlayerEntity>
      battlePlayersByUid;
  std::vector<BattleEnemyState> battleEnemyStates;
  std::unordered_map<std::string, std::unordered_map<int, Protocol::BattlePos>>
      enemyPosByUid;
  std::vector<BulletState> battleBullets;
  std::vector<Protocol::BattleEventDTO> pendingBattleEvents;
  int battleTick = 0;
  int nextBattleEntityId = 1;
  bool battleStarted = false;
  mutable std::mutex roomMutex;

  using Event = Protocol::BattleEventDTO;
  using EventType = Protocol::BattleEventType;
  using EntityType = Protocol::EntityType;
  using DestroyReason = Protocol::BattleEntityDestroyReason;
  using SpawnParam = Event::SpawnParameter;
  using HitParam = Event::HitParameter;
  using DamageParam = Event::DamageParameter;
  using DestroyParam = Event::DestroyParameter;
  using IntentParam = Event::IntentParameter;

  static void put_param(Event &event, SpawnParam param) {
    event.spawnParameter = std::move(param);
  }
  static void put_param(Event &event, HitParam param) {
    event.hitParameter = std::move(param);
  }
  static void put_param(Event &event, DamageParam param) {
    event.damageParameter = std::move(param);
  }
  static void put_param(Event &event, DestroyParam param) {
    event.destroyParameter = std::move(param);
  }
  static void put_param(Event &event, IntentParam param) {
    event.intentParameter = std::move(param);
  }

  template <typename Param>
  void push_event_locked(EventType type, Param param) {
    Event event;
    event.eventType = type;
    event.eventTick = battleTick;
    put_param(event, std::move(param));
    pendingBattleEvents.push_back(std::move(event));
  }

  int get_item_index(const std::string &itemId) const;
  int get_map_node_index(int nodeId) const;
  void ensure_map_generated_locked();
  bool try_commit_map_move_locked();
  std::vector<Protocol::ShopItem> build_shop_items_locked(const size_t) const;
  void reset_battle_state_locked();
  Protocol::BattleFrameRsp build_battle_frame_locked() const;
  Protocol::MapNode::NodeType resolve_map_node_type_locked() const;
  std::vector<EnemySpawnSpec>
  build_spawn_plan_locked(Protocol::MapNode::NodeType nodeType) const;
  void spawn_enemies_locked(const std::vector<EnemySpawnSpec> &spawnPlan);
  bool has_enemy_locked(int entityId) const;
  bool all_players_dead_locked() const;
  Protocol::BattlePlayerEntity *live_player_locked(const std::string &uid);
  void apply_enemy_reports_locked();
  void tick_bullets_locked();
  void tick_enemy_attacks_locked();
  void end_battle_locked();
  bool update_target_locked(BattleEnemyState &enemyState);
  void push_enemy_intent_locked(const BattleEnemyState &enemyState);
  void start_battle_locked();

public:
  Room(int roomId, size_t maximumPeople, std::shared_ptr<ServerState> state,
       std::shared_ptr<User> creator);

  int get_id() const { return roomId; }
  std::vector<std::string> get_member_uids() const {
    std::lock_guard<std::mutex> lock(roomMutex);
    return uids;
  }

  Protocol::RoomInfo get_info() const;

  // Add member if not present and capacity allows.
  bool add_member(std::shared_ptr<User> user);

  bool get_shop_init(Protocol::ShopInitRsp &rsp) const;
  bool move_shop_cursor(const std::string &uid, const std::string &itemId,
                        std::vector<Protocol::ShopItem> &items);
  int buy_shop_item(const std::string &uid, const std::string &itemId,
                    std::vector<Protocol::ShopItem> &items);
  bool get_map_init(Protocol::MapInitRsp &rsp);
  bool move_map(const std::string &uid, int selectId,
                std::vector<Protocol::MapSync> &selectStatus, bool &committed);
  bool set_battle_ready(const std::string &uid, Protocol::BattleWaitRsp &rsp,
                        bool &allReady);
  bool sync_battle(const std::string &uid,
                   const Protocol::BattleVector2 &playerPosition,
                   const Protocol::BattleVector2 &playerDirection,
                   const std::vector<Protocol::BattlePos> &enemyPositions);
  bool shoot_battle_player(const std::string &uid,
                           const Protocol::BattleVector2 &direction);
  bool tick_battle(Protocol::BattleFrameRsp &frame, bool *ended = nullptr);

  bool set_member_ready(const std::string &uid, bool ready) {
    std::lock_guard<std::mutex> lock(roomMutex);
    auto it = readyStates.find(uid);
    if (it == readyStates.end()) {
      return false;
    }
    it->second = ready;
    return true;
  }

  bool is_all_ready() const {
    std::lock_guard<std::mutex> lock(roomMutex);
    if (readyStates.empty())
      return false;
    return std::all_of(readyStates.begin(), readyStates.end(),
                       [](const auto &kv) { return kv.second; });
  }

  bool remove_member(const std::string &uid) {
    std::lock_guard<std::mutex> lock(roomMutex);
    auto it = std::find(uids.begin(), uids.end(), uid);
    if (it == uids.end()) {
      return false;
    }
    uids.erase(it);
    readyStates.erase(uid);
    battleReadyStates.erase(uid);
    battlePlayersByUid.erase(uid);
    enemyPosByUid.erase(uid);
    selectedItemByUid.erase(uid);
    selectedMapNodeByUid.erase(uid);
    ownedItemsByUid.erase(uid);
    return true;
  }

  size_t get_maximum_people() const {
    std::lock_guard<std::mutex> lock(roomMutex);
    return maximumPeople;
  }

  bool is_member(const std::string &uid) const {
    std::lock_guard<std::mutex> lock(roomMutex);
    return std::find(uids.begin(), uids.end(), uid) != uids.end();
  }

  size_t get_people_count() const {
    std::lock_guard<std::mutex> lock(roomMutex);
    return uids.size();
  }
};
