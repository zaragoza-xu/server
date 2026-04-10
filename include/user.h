#pragma once

#include <string>

#include "protocol.h"

class Channel;
class User {
private:
  Protocol::PlayerBasicInfo info;
  int roomId;

public:
  User(const Protocol::PlayerBasicInfo &info) : info(info), roomId(-1) {}

  const Protocol::PlayerBasicInfo &get_info() const { return info; }

  const std::string &get_username() const { return info.name; }
  int get_color() const { return info.color; }
  const std::string &get_uid() const { return info.uid; }

  int get_room_id() const { return roomId; }
  void set_room_id(int roomId) { this->roomId = roomId; }

  bool is_in_room() const { return roomId != -1; }
};
