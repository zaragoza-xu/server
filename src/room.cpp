#include "room.h"

#include <algorithm>
#include <cstddef>
#include <iterator>
#include <mutex>
#include <random>

#include "server.h"
#include "user.h"

std::vector<Protocol::MapNode> generate_map();

int Room::get_item_index(const std::string &itemId) const {
  const size_t visible_count = std::min(uids.size(), shopItemIds.size());
  auto visible_end =
      shopItemIds.begin() + static_cast<std::ptrdiff_t>(visible_count);
  auto it = std::find(shopItemIds.begin(), visible_end, itemId);
  if (it == visible_end) {
    return -1;
  }
  return static_cast<int>(std::distance(shopItemIds.begin(), it));
}

int Room::get_map_node_index(int nodeId) const {
  auto it = std::find_if(mapNodes.begin(), mapNodes.end(),
                         [nodeId](const Protocol::MapNode &node) {
                           return node.nodeId == nodeId;
                         });
  if (it == mapNodes.end()) {
    return -1;
  }
  return static_cast<int>(std::distance(mapNodes.begin(), it));
}

bool Room::all_lobby_ready_locked() const {
  return !readyStates.empty() &&
         std::all_of(readyStates.begin(), readyStates.end(),
                     [](const auto &kv) { return kv.second; });
}

bool Room::is_last_map_node_locked() const {
  const int nodeIndex = get_map_node_index(mapNodeId);
  return nodeIndex >= 0 &&
         mapNodes[static_cast<size_t>(nodeIndex)].nextId.empty();
}

void Room::ensure_map_generated_locked() {
  if (!mapNodes.empty()) {
    return;
  }
  mapNodes = generate_map();
}

bool Room::try_commit_map_move_locked() {
  if (uids.empty() || selectedMapNodeByUid.size() != uids.size()) {
    return false;
  }

  const int target = selectedMapNodeByUid.begin()->second;
  for (const auto &uid : uids) {
    auto it = selectedMapNodeByUid.find(uid);
    if (it == selectedMapNodeByUid.end() || it->second != target) {
      return false;
    }
  }

  mapNodeId = target;
  selectedMapNodeByUid.clear();
  return true;
}

Room::Room(int roomId, size_t maximumPeople, std::shared_ptr<ServerState> state,
           std::shared_ptr<User> creator)
    : state(std::move(state)), roomId(roomId), maximumPeople(maximumPeople),
      battleRng(std::random_device{}()) {
  const auto creator_uid = creator->get_uid();
  uids.push_back(creator_uid);
  readyStates.emplace(creator_uid, false);
  battleReadyStates.emplace(creator_uid, false);
  ownedItemsByUid.emplace(creator_uid, std::vector<std::string>{});

  auto shared_state = this->state.lock();
  const std::vector<std::string> &catalog =
      (shared_state && !shared_state->shopCatalogItemIds.empty())
          ? shared_state->shopCatalogItemIds
          : []() -> const std::vector<std::string> & {
    static const std::vector<std::string> fallback = {
        "knife_1", "pistol_1", "knife_2", "pistol_2",
        "knife_3", "pistol_3", "knife_4", "pistol_4"};
    return fallback;
  }();
  if (shared_state) {
    shopCatalogVersion = shared_state->shopCatalogVersion;
    if (battle_config_complete(shared_state->battleConfig)) {
      battleConfig = shared_state->battleConfig;
    }
  } else {
    shopCatalogVersion = "embedded-v1";
  }

  shopItemIds = catalog;
  std::shuffle(shopItemIds.begin(), shopItemIds.end(),
               std::mt19937{std::random_device{}()});
}

Protocol::RoomInfo Room::get_info() const {
  Protocol::RoomInfo info;
  info.roomId = roomId;
  auto shared_state = state.lock();
  if (!shared_state) {
    std::lock_guard<std::mutex> room_lock(roomMutex);
    info.maximumPeople = maximumPeople;
    for (const auto &[uid, ready] : readyStates) {
      if (ready) {
        info.readyUids.push_back(uid);
      }
    }
    return info;
  }

  std::scoped_lock lock(roomMutex, shared_state->userDataMutex);

  info.maximumPeople = maximumPeople;
  info.basicInfos.reserve(uids.size());
  info.readyUids.reserve(uids.size());

  for (const auto &uid : uids) {
    auto it = shared_state->userData.find(uid);
    if (it != shared_state->userData.end()) {
      info.basicInfos.push_back(it->second.basicInfo);
    }
  }

  for (const auto &[uid, ready] : readyStates) {
    if (ready) {
      info.readyUids.push_back(uid);
    }
  }
  return info;
}

Protocol::GetStateStatusRsp Room::get_state_status_snapshot() const {
  Protocol::GetStateStatusRsp snapshot;
  std::lock_guard<std::mutex> lock(roomMutex);
  snapshot.roomId = roomId;
  snapshot.roomMemberCount = static_cast<int>(uids.size());
  snapshot.mapNodeId = mapNodeId;
  snapshot.battleTick = battleTick;
  snapshot.allLobbyReady = all_lobby_ready_locked();
  switch (phase) {
  case Phase::LOBBY:
    snapshot.roomPhase = 0;
    break;
  case Phase::SHOP:
    snapshot.roomPhase = 1;
    break;
  case Phase::MAP:
    snapshot.roomPhase = 2;
    break;
  case Phase::BATTLE:
    snapshot.roomPhase = 3;
    break;
  case Phase::END:
    snapshot.roomPhase = 4;
    break;
  }
  return snapshot;
}

bool Room::add_member(std::shared_ptr<User> user) {
  std::lock_guard<std::mutex> lock(roomMutex);
  if (phase != Phase::LOBBY) {
    return false;
  }
  const auto &uid = user->get_uid();
  if (std::find(uids.begin(), uids.end(), uid) != uids.end()) {
    return false; // Already in room
  }
  if (uids.size() >= maximumPeople)
    return false; // room is full
  uids.push_back(uid);
  readyStates.emplace(uid, false);
  battleReadyStates.emplace(uid, false);
  ownedItemsByUid.emplace(uid, std::vector<std::string>{});
  return true;
}

bool Room::set_member_ready(const std::string &uid, bool ready) {
  std::lock_guard<std::mutex> lock(roomMutex);
  if (phase != Phase::LOBBY) {
    return false;
  }
  auto it = readyStates.find(uid);
  if (it == readyStates.end()) {
    return false;
  }
  it->second = ready;
  if (all_lobby_ready_locked()) {
    phase = Phase::SHOP;
  }
  return true;
}

std::vector<Protocol::ShopItem>
Room::build_shop_items_locked(const size_t size) const {
  const size_t visible_count = std::min(size, shopItemIds.size());
  std::vector<Protocol::ShopItem> items;
  items.reserve(visible_count);
  for (size_t index = 0; index < visible_count; ++index) {
    const auto &id = shopItemIds[index];
    Protocol::ShopItem item;
    item.itemId = id;
    if (takenItems.count(id)) {
      item.itemStatus = Protocol::ShopItem::Status::BUY;
    } else {
      bool found = false;
      for (const auto &[sel_uid, sel_item] : selectedItemByUid) {
        if (sel_item == id) {
          item.itemStatus = Protocol::ShopItem::Status::SELECT;
          item.selectUid = sel_uid;
          found = true;
          break;
        }
      }
      if (!found) {
        item.itemStatus = Protocol::ShopItem::Status::UNDO;
      }
    }
    items.push_back(std::move(item));
  }
  return items;
}

bool Room::get_shop_init(Protocol::ShopInitRsp &rsp) const {
  auto shared_state = state.lock();
  if (!shared_state) {
    return false;
  }

  std::scoped_lock lock(roomMutex, shared_state->userDataMutex);
  if (phase != Phase::SHOP) {
    return false;
  }
  rsp.items = build_shop_items_locked(uids.size());
  rsp.playerInfos.clear();
  rsp.playerInfos.reserve(uids.size());

  for (const auto &uid : uids) {
    auto user_it = shared_state->userData.find(uid);
    if (user_it == shared_state->userData.end()) {
      continue;
    }
    Protocol::ShopPlayerInfo info;
    info.playerInfo = user_it->second.basicInfo;
    auto owned_it = ownedItemsByUid.find(uid);
    if (owned_it != ownedItemsByUid.end()) {
      info.ownedItems = owned_it->second;
    }
    rsp.playerInfos.push_back(std::move(info));
  }
  return true;
}

int Room::move_shop_cursor(const std::string &uid, const std::string &itemId,
                           std::vector<Protocol::ShopItem> &items) {
  std::lock_guard<std::mutex> lock(roomMutex);
  if (phase != Phase::SHOP) {
    return Protocol::SERVICE_FAIL | Protocol::ROOM_STATE_ERROR;
  }
  if (get_item_index(itemId) < 0) {
    selectedItemByUid.erase(uid);
    items = build_shop_items_locked(uids.size());
    return Protocol::SERVICE_SUCCESS; // Invalid itemId deselects.
  }
  selectedItemByUid[uid] = itemId;
  items = build_shop_items_locked(uids.size());
  return Protocol::SERVICE_SUCCESS;
}

int Room::buy_shop_item(const std::string &uid, const std::string &itemId,
                        std::vector<Protocol::ShopItem> &items) {
  std::lock_guard<std::mutex> lock(roomMutex);
  if (phase != Phase::SHOP) {
    return Protocol::SERVICE_FAIL | Protocol::ROOM_STATE_ERROR;
  }
  if (get_item_index(itemId) < 0) {
    return Protocol::SERVICE_FAIL | Protocol::SHOP_INVALID_ITEM;
  }
  if (takenItems.count(itemId) > 0) {
    return Protocol::SERVICE_FAIL | Protocol::SHOP_ITEM_TAKEN;
  }

  takenItems.emplace(itemId);
  auto &owned = ownedItemsByUid[uid];
  owned.push_back(itemId);

  for (auto &[select_uid, select_item] : selectedItemByUid) {
    if (select_item == itemId) {
      select_item.clear();
    }
  }

  items = build_shop_items_locked(uids.size());
  return Protocol::SERVICE_SUCCESS;
}

bool Room::get_map_init(Protocol::MapInitRsp &rsp) {
  std::lock_guard<std::mutex> lock(roomMutex);
  if (phase != Phase::SHOP && phase != Phase::MAP) {
    return false;
  }
  ensure_map_generated_locked();
  if (phase == Phase::SHOP) {
    phase = Phase::MAP;
  }
  rsp.map = mapNodes;
  return !rsp.map.empty();
}

bool Room::move_map(const std::string &uid, int selectId,
                    std::vector<Protocol::MapSync> &selectStatus,
                    bool &committed) {
  std::lock_guard<std::mutex> lock(roomMutex);
  if (phase != Phase::MAP) {
    return false;
  }
  ensure_map_generated_locked();
  committed = false;

  if (get_map_node_index(selectId) < 0) {
    return false;
  }

  if (mapNodeId >= 0) {
    const int current_index = get_map_node_index(mapNodeId);
    if (current_index < 0) {
      return false;
    }

    const auto &next_ids = mapNodes[static_cast<size_t>(current_index)].nextId;
    if (std::find(next_ids.begin(), next_ids.end(), selectId) ==
        next_ids.end()) {
      return false;
    }
  } else {
    // First committed move must start from a root node (node without incoming
    // edges).
    const bool has_incoming =
        std::any_of(mapNodes.begin(), mapNodes.end(),
                    [selectId](const Protocol::MapNode &node) {
                      return std::find(node.nextId.begin(), node.nextId.end(),
                                       selectId) != node.nextId.end();
                    });
    if (has_incoming) {
      return false;
    }
  }

  selectedMapNodeByUid[uid] = selectId;

  selectStatus.clear();
  selectStatus.reserve(uids.size());
  for (const auto &member_uid : uids) {
    Protocol::MapSync status;
    status.uid = member_uid;

    auto selected_it = selectedMapNodeByUid.find(member_uid);
    if (selected_it != selectedMapNodeByUid.end()) {
      status.selectId = selected_it->second;
    }
    selectStatus.push_back(std::move(status));
  }

  committed = try_commit_map_move_locked();
  return true;
}
