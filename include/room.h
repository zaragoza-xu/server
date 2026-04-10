#pragma once

#include <cstddef>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>

#include "protocol.h"

class User;

class Room {
private:
  int roomId;
  size_t maximumPeople;
  std::unordered_map<std::string, Protocol::PlayerBasicInfo> basicInfos;
  mutable std::mutex roomMutex;

public:
  Room(int roomId, size_t maximumPeople, std::shared_ptr<User> creator);

  int get_id() const { return roomId; }

  Protocol::RoomInfo get_info() const {
    std::lock_guard<std::mutex> lock(roomMutex);
    return Protocol::RoomInfo{.roomId = roomId,
                              .maximumPeople = maximumPeople,
                              .basicInfos = basicInfos};
  }

  // Add member if not present and capacity allows.
  bool add_member(std::shared_ptr<User> user);

  bool remove_member(const std::string &uid) {
    std::lock_guard<std::mutex> lock(roomMutex);
    return basicInfos.erase(uid) > 0;
  }

  size_t get_maximum_people() const {
    std::lock_guard<std::mutex> lock(roomMutex);
    return maximumPeople;
  }

  bool is_member(const std::string &uid) const {
    std::lock_guard<std::mutex> lock(roomMutex);
    return basicInfos.count(uid) > 0;
  }

  size_t get_people_count() const {
    std::lock_guard<std::mutex> lock(roomMutex);
    return basicInfos.size();
  }
};
