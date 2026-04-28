#pragma once

#include <algorithm>
#include <cstddef>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "battle.h"
#include "protocol.h"

class User;
struct ServerState;

class Room {
private:
  std::weak_ptr<ServerState> state;
  int roomId;
  int mapNodeId = -1;
  size_t maximumPeople;
  std::vector<std::string> uids;
  std::unordered_map<std::string, bool> readyStates;
  std::vector<std::string> shopItemIds;
  std::string shopCatalogVersion;
  std::unordered_set<std::string> takenItems;
  std::unordered_map<std::string, std::string> selectedItemByUid;
  std::unordered_map<std::string, std::vector<std::string>> ownedItemsByUid;
  std::vector<Protocol::MapNode> mapNodes;
  std::unordered_map<std::string, int> selectedMapNodeByUid;
  std::unordered_map<std::string, bool> battleReadyStates;
  std::unordered_map<std::string, Protocol::BattlePlayerEntity>
      battlePlayersByUid;
  std::vector<Protocol::BattleEnemyEntity> battleEnemies;
  std::vector<Protocol::BattleBulletEntity> battleBullets;
  std::vector<Protocol::BattleEventDTO> pendingBattleEvents;
  int battleTick = 0;
  int nextBattleEntityId = 1;
  bool battleStarted = false;
  mutable std::mutex roomMutex;

  int get_item_index(const std::string &itemId) const;
  int get_map_node_index(int nodeId) const;
  void ensure_map_generated_locked();
  bool try_commit_map_move_locked();
  std::vector<Protocol::ShopItem> build_shop_items_locked(const size_t) const;
  void reset_battle_state_locked();
  Protocol::BattleFrameRsp build_battle_frame_locked(
      const std::vector<Protocol::BattleEventDTO> &events) const;
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
  bool move_battle_player(const std::string &uid,
                          const Protocol::BattleVector2 &input);
  bool shoot_battle_player(const std::string &uid,
                           const Protocol::BattleVector2 &direction);
  bool tick_battle(Protocol::BattleFrameRsp &frame);

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
