#include "room.h"

#include <vector>

#include "protocol.h"
#include "user.h"

Room::Room(int roomId, size_t maximumPeople, std::shared_ptr<User> creator)
    : roomId(roomId), maximumPeople(maximumPeople), creator(creator) {
  members.emplace(creator->get_uid(), creator);
}

void Room::collect_members_info(
    std::vector<Protocol::PlayerBasicInfo> &PlayerInfos) const {
  std::lock_guard<std::mutex> lock(roomMutex);
  PlayerInfos.clear();
  PlayerInfos.reserve(members.size());
  for (auto &[uid, user] : members) {
    PlayerInfos.push_back(user->get_info());
  }
}

bool Room::add_member(std::shared_ptr<User> user) {
  std::lock_guard<std::mutex> lock(roomMutex);
  if (members.count(user->get_uid())) {
    return false; // Already in room
  }
  if (members.size() >= maximumPeople)
    return false; // room is full
  members.emplace(user->get_uid(), user);
  return true;
}
