#include "room.h"

#include "user.h"

Room::Room(int roomId, size_t maximumPeople, std::shared_ptr<User> creator)
    : roomId(roomId), maximumPeople(maximumPeople) {
  basicInfos.emplace(creator->get_uid(), creator->get_info());
}

bool Room::add_member(std::shared_ptr<User> user) {
  std::lock_guard<std::mutex> lock(roomMutex);
  if (basicInfos.count(user->get_uid())) {
    return false; // Already in room
  }
  if (basicInfos.size() >= maximumPeople)
    return false; // room is full
  basicInfos.emplace(user->get_uid(), user->get_info());
  return true;
}
