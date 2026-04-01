#pragma once

#include <chrono>
#include <string>

class Channel;

class User {
private:
  int avatarType;
  std::string userName;
  int roomId;
  std::string uid;
  std::chrono::steady_clock::time_point lastHeartbeat;

public:
  User(const std::string &uid, const std::string &userName, int avatarType = 0)
      : avatarType(avatarType), userName(userName), roomId(-1), uid(uid),
        lastHeartbeat(std::chrono::steady_clock::now()) {}

  const std::string &get_username() const { return userName; }
  int get_avatar_type() const { return avatarType; }
  const std::string &get_uid() const { return uid; }

  int get_room_id() const { return roomId; }
  void set_room_id(int roomId) { this->roomId = roomId; }

  bool is_in_room() const { return roomId != -1; }

  void touch_heartbeat() { lastHeartbeat = std::chrono::steady_clock::now(); }
  std::chrono::steady_clock::time_point get_last_heartbeat() const {
    return lastHeartbeat;
  }
};
