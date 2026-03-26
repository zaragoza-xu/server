#include "room.h"
#include "user.h"
#include <linux/falloc.h>

Room::Room(int roomId, const std::string &roomName, size_t maximumPeople, std::shared_ptr<User> creator)
    : roomId(roomId), roomName(roomName), creator(creator), maximumPeople(maximumPeople) {
  members[creator->get_uid()] = creator;
}

bool Room::add_member(std::shared_ptr<User> user) {
  std::lock_guard<std::mutex> lock(roomMutex);
  if (members.count(user->get_uid())) {
    return false; // Already in room
  }
  if (members.size() >= maximumPeople)
    return false; // room is full
  members[user->get_uid()] = user;
  return true;
}
