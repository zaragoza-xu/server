#pragma once

#include <chrono>
#include <string>

#include "protocol.h"

class Channel;
class User {
private:
  Protocol::PlayerBasicInfo info;
  int roomId;
  std::chrono::steady_clock::time_point lastHeartbeat;

public:
  User(const Protocol::PlayerBasicInfo &info)
      : info(info), roomId(-1),
        lastHeartbeat(std::chrono::steady_clock::now()) {}

  const Protocol::PlayerBasicInfo &get_info() const { return info; }

  const std::string &get_username() const { return info.userName; }
  int get_avatar_type() const { return info.avatarType; }
  const std::string &get_uid() const { return info.uid; }

  int get_room_id() const { return roomId; }
  void set_room_id(int roomId) { this->roomId = roomId; }

  bool is_in_room() const { return roomId != -1; }

  void touch_heartbeat() { lastHeartbeat = std::chrono::steady_clock::now(); }
  std::chrono::steady_clock::time_point get_last_heartbeat() const {
    return lastHeartbeat;
  }
};
