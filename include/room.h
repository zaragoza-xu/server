#pragma once

#include <cstddef>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

class User;

class Room {
private:
  int roomId;
  size_t maximumPeople;
  std::string roomName;
  std::shared_ptr<User> creator;
  std::unordered_map<std::string, std::shared_ptr<User>> members;
  mutable std::mutex roomMutex;

public:
  Room(int roomId, const std::string &roomName, size_t maximumPeople, std::shared_ptr<User> creator);

  int get_id() const { return roomId; }
  const std::string &get_name() const { return roomName; }
  std::shared_ptr<User> get_creator() const { return creator; }

  bool add_member(std::shared_ptr<User> user);

  bool remove_member(const std::string &uid) {
    std::lock_guard<std::mutex> lock(roomMutex);
    return members.erase(uid) > 0;
  }

  const std::unordered_map<std::string, std::shared_ptr<User>> get_members()
      const {
    std::lock_guard<std::mutex> lock(roomMutex);
    return members;
  }

  size_t get_maximum_people() const {
    std::lock_guard<std::mutex> lock(roomMutex);
    return maximumPeople;
  }
  
  bool is_member(const std::string &uid) const {
    std::lock_guard<std::mutex> lock(roomMutex);
    return members.count(uid) > 0;
  }

  size_t get_member_count() const {
    std::lock_guard<std::mutex> lock(roomMutex);
    return members.size(); }
};
