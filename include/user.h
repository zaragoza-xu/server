#pragma once

#include <memory>
#include <string>

struct ServerState;
class User {
private:
  std::weak_ptr<ServerState> state;
  std::string uid;
  int roomId;

public:
  User(std::string uid, std::shared_ptr<ServerState> state)
      : state(std::move(state)), uid(std::move(uid)), roomId(-1) {}

  const std::string &get_uid() const { return uid; }

  int get_room_id() const { return roomId; }
  void set_room_id(int roomId) { this->roomId = roomId; }

  bool is_in_room() const { return roomId != -1; }
};
