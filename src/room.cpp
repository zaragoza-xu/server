#include "room.h"
#include "user.h"

Room::Room(int roomId, const std::string &roomName, std::shared_ptr<User> creator)
    : roomId(roomId), roomName(roomName), creator(creator) {
  members[creator->get_uid()] = creator;
}

bool Room::add_member(std::shared_ptr<User> user) {
  if (members.count(user->get_uid())) {
    return false; // Already in room
  }
  members[user->get_uid()] = user;
  return true;
}
