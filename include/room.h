#pragma once

#include <algorithm>
#include <cstddef>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include "protocol.h"

class User;
struct ServerState;

class Room {
private:
  std::weak_ptr<ServerState> state;
  int roomId;
  size_t maximumPeople;
  std::vector<std::string> uids;
  std::unordered_map<std::string, bool> readyStates;
  mutable std::mutex roomMutex;

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

  bool set_member_ready(const std::string &uid, bool ready) {
    std::lock_guard<std::mutex> lock(roomMutex);
    auto it = readyStates.find(uid);
    if (it == readyStates.end()) {
      return false;
    }
    it->second = ready;
    return true;
  }

  bool remove_member(const std::string &uid) {
    std::lock_guard<std::mutex> lock(roomMutex);
    auto it = std::find(uids.begin(), uids.end(), uid);
    if (it == uids.end()) {
      return false;
    }
    uids.erase(it);
    readyStates.erase(uid);
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
