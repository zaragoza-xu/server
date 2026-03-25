#pragma once

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

class User;

class Room {
private:
  int roomId;
  std::string roomName;
  std::shared_ptr<User> creator;
  std::unordered_map<std::string, std::shared_ptr<User>> members;

public:
  Room(int roomId, const std::string &roomName, std::shared_ptr<User> creator);

  int get_id() const { return roomId; }
  const std::string &get_name() const { return roomName; }
  std::shared_ptr<User> get_creator() const { return creator; }

  bool add_member(std::shared_ptr<User> user);

  bool remove_member(const std::string &uid) {
    return members.erase(uid) > 0;
  }

  const std::unordered_map<std::string, std::shared_ptr<User>> &get_members()
      const {
    return members;
  }

  bool is_member(const std::string &uid) const {
    return members.count(uid) > 0;
  }

  size_t get_member_count() const { return members.size(); }
};
