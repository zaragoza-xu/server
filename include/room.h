#pragma once

#include <algorithm>
#include <cstddef>
#include <memory>
#include <mutex>
#include <optional>
#include <random>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "battle.h"
#include "battle_config.h"
#include "protocol.h"

class User;
struct ServerState;

class Room {
private:
  enum class Phase { LOBBY, SHOP, MAP, BATTLE, END };

  std::weak_ptr<ServerState> state;
  int roomId;
  Phase phase = Phase::LOBBY;
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
  Battle::BattleConfig battleConfig = Battle::default_battle_config();
  std::unordered_map<std::string, bool> battleReadyStates;
  std::unordered_map<std::string, Protocol::BattlePlayerEntity>
      battlePlayersByUid;
  std::unordered_map<std::string, Battle::WeaponDef> battleWeaponsByUid;
  std::unordered_map<int, Battle::EnemyState> battleEnemyStates;
  std::unordered_map<std::string, std::unordered_map<int, Protocol::BattlePos>>
      enemyPosByUid;
  std::unordered_map<std::string, int> nextAttackTickByUid;
  std::vector<Battle::BulletState> battleBullets;
  std::vector<Protocol::BattleEventDTO> pendingBattleEvents;
  int battleTick = 0;
  int nextBattleEntityId = 1;
  int nextBattleSpawnTick = 1;
  bool battleStarted = false;
  std::mt19937 battleRng;
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

  bool is_outside_battle(const Battle::BattleVector2 &position) {
    return position.x < battleConfig.battleMin ||
           position.x > battleConfig.battleMax ||
           position.y < battleConfig.battleMin ||
           position.y > battleConfig.battleMax;
  }

  Battle::BattleVector2 clamp_battle(Battle::BattleVector2 position) {
    position.x =
        std::clamp(position.x, battleConfig.battleMin, battleConfig.battleMax);
    position.y =
        std::clamp(position.y, battleConfig.battleMin, battleConfig.battleMax);
    return position;
  }

  Battle::BattleVector2 step_to(const Battle::BattleVector2 &from,
                                const Battle::BattleVector2 &to,
                                double maxDistance) {
    // Server-side movement is capped even when clients report a far-away
    // target.
    const Battle::BattleVector2 delta{to.x - from.x, to.y - from.y};
    const double distSquared = length_squared(delta);
    if (!std::isfinite(distSquared) || distSquared <= 0.0) {
      return clamp_battle(from);
    }
    if (distSquared <= maxDistance * maxDistance) {
      return clamp_battle(to);
    }
    const double dist = std::sqrt(distSquared);
    return clamp_battle({from.x + delta.x / dist * maxDistance,
                         from.y + delta.y / dist * maxDistance});
  }

  int get_item_index(const std::string &itemId) const;
  int get_map_node_index(int nodeId) const;
  bool all_lobby_ready_locked() const;
  bool is_last_map_node_locked() const;
  void ensure_map_generated_locked();
  bool try_commit_map_move_locked();
  std::vector<Protocol::ShopItem> build_shop_items_locked(const size_t) const;
  void reset_battle_state_locked();
  Protocol::BattleFrameRsp build_battle_frame_locked() const;
  Protocol::MapNode::NodeType resolve_map_node_type_locked() const;
  double battle_time_locked() const;
  double battle_difficulty_locked() const;
  std::vector<Battle::EnemySpawnSpec>
  enemy_pool_locked(double time, double difficulty) const;
  std::optional<Battle::EnemySpawnSpec>
  pick_enemy_locked(const std::vector<Battle::EnemySpawnSpec> &pool,
                    double budget);
  Battle::BattleVector2 spawn_pos_locked();
  void spawn_enemies_locked(const std::vector<Battle::EnemySpawnSpec> &spawns);
  void tick_spawn_locked();
  bool has_enemy_locked(int entityId) const;
  bool all_players_dead_locked() const;
  const Battle::WeaponDef *
  equip_weapon_locked(const std::vector<std::string> &items) const;
  const Battle::WeaponDef *equipped_weapon_locked(const std::string &uid) const;
  int weapon_damage_locked(const Battle::WeaponDef &weapon);
  int cooldown_ticks_locked(const Battle::WeaponDef &weapon) const;
  double battle_range(const Battle::WeaponDef &weapon) const;
  void lifesteal_locked(const std::string &uid, int damage,
                        const Battle::WeaponDef &weapon);
  void hit_enemy_locked(Protocol::BattleEnemyEntity &enemy, int hitSourceId,
                        Protocol::EntityType hitSourceType,
                        Protocol::BattlePlayerEntity &player,
                        const Battle::BattleVector2 &hitPosition,
                        const Battle::WeaponDef &weapon, int damage,
                        EventType hitType);
  void melee_attack_locked(Protocol::BattlePlayerEntity &player,
                           const Battle::WeaponDef &weapon,
                           const Battle::BattleVector2 &direction);
  Protocol::BattlePlayerEntity *live_player_locked(const std::string &uid);
  void apply_enemy_reports_locked();
  void tick_bullets_locked();
  void tick_enemy_attacks_locked();
  void end_battle_locked(bool won);
  bool update_target_locked(Battle::EnemyState &enemyState);
  void push_enemy_intent_locked(const Battle::EnemyState &enemyState);
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

  Protocol::GetStateStatusRsp get_state_status_snapshot() const;

  // Add member if not present and capacity allows.
  bool add_member(std::shared_ptr<User> user);

  bool get_shop_init(Protocol::ShopInitRsp &rsp) const;
  int move_shop_cursor(const std::string &uid, const std::string &itemId,
                       std::vector<Protocol::ShopItem> &items);
  int buy_shop_item(const std::string &uid, const std::string &itemId,
                    std::vector<Protocol::ShopItem> &items);
  bool get_map_init(Protocol::MapInitRsp &rsp);
  bool move_map(const std::string &uid, int selectId,
                std::vector<Protocol::MapSync> &selectStatus, bool &committed);
  bool set_battle_ready(const std::string &uid, Protocol::BattleWaitRsp &rsp,
                        bool &allReady);
  bool sync_battle(const std::string &uid,
                   const Battle::BattleVector2 &playerPosition,
                   const Battle::BattleVector2 &playerDirection,
                   const std::vector<Protocol::BattlePos> &enemyPositions);
  bool shoot_battle_player(const std::string &uid,
                           const Battle::BattleVector2 &direction);
  bool tick_battle(Protocol::BattleFrameRsp &frame, bool *ended = nullptr);

  bool set_member_ready(const std::string &uid, bool ready);

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
