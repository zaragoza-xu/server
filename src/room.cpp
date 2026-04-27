#include "room.h"

#include <algorithm>
#include <iterator>
#include <mutex>

#include "server.h"
#include "user.h"

std::vector<Protocol::MapNode> generate_map();

int Room::get_item_index(const std::string &itemId) const {
  auto it = std::find(shopItemIds.begin(), shopItemIds.end(), itemId);
  if (it == shopItemIds.end()) {
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
    : state(std::move(state)), roomId(roomId), maximumPeople(maximumPeople) {
  const auto creator_uid = creator->get_uid();
  uids.push_back(creator_uid);
  readyStates.emplace(creator_uid, false);
  ownedItemsByUid.emplace(creator_uid, std::vector<std::string>{});

  auto shared_state = this->state.lock();
  if (shared_state && !shared_state->shopCatalogItemIds.empty()) {
    shopItemIds = shared_state->shopCatalogItemIds;
    shopCatalogVersion = shared_state->shopCatalogVersion;
  } else {
    // Fallback static catalog.
    shopItemIds = {"sword", "shield", "potion", "boots", "wand"};
    shopCatalogVersion = "embedded-v1";
  }
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

bool Room::add_member(std::shared_ptr<User> user) {
  std::lock_guard<std::mutex> lock(roomMutex);
  const auto &uid = user->get_uid();
  if (std::find(uids.begin(), uids.end(), uid) != uids.end()) {
    return false; // Already in room
  }
  if (uids.size() >= maximumPeople)
    return false; // room is full
  uids.push_back(uid);
  readyStates.emplace(uid, false);
  ownedItemsByUid.emplace(uid, std::vector<std::string>{});
  return true;
}

std::vector<Protocol::ShopItem> Room::build_shop_items_locked() const {
  std::vector<Protocol::ShopItem> items;
  items.reserve(shopItemIds.size());
  for (const auto &id : shopItemIds) {
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
  rsp.items = build_shop_items_locked();
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

bool Room::move_shop_cursor(const std::string &uid, const std::string &itemId,
                            std::vector<Protocol::ShopItem> &items) {
  std::lock_guard<std::mutex> lock(roomMutex);
  if (get_item_index(itemId) < 0) {
    selectedItemByUid.erase(uid);
    items = build_shop_items_locked();
    return true; // Invalid itemId is treated as deselection, not an error
  }
  selectedItemByUid[uid] = itemId;
  items = build_shop_items_locked();
  return true;
}

int Room::buy_shop_item(const std::string &uid, const std::string &itemId,
                        std::vector<Protocol::ShopItem> &items) {
  std::lock_guard<std::mutex> lock(roomMutex);
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

  items = build_shop_items_locked();
  return Protocol::SERVICE_SUCCESS;
}

bool Room::get_map_init(Protocol::MapInitRsp &rsp) {
  std::lock_guard<std::mutex> lock(roomMutex);
  ensure_map_generated_locked();
  rsp.map = mapNodes;
  return !rsp.map.empty();
}

bool Room::move_map(const std::string &uid, int selectId,
                    std::vector<Protocol::MapSync> &selectStatus,
                    bool &committed) {
  std::lock_guard<std::mutex> lock(roomMutex);
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
