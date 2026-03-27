#include "room.h"

#include <linux/falloc.h>
#include <vector>

#include "protocol.h"
#include "user.h"

Room::Room(int roomId, const std::string &roomName, size_t maximumPeople,
           std::shared_ptr<User> creator)
    : roomId(roomId), roomName(roomName), creator(creator),
      maximumPeople(maximumPeople) {
  members[creator->get_uid()] = creator;
}

void Room::collect_members_info(
    std::vector<Protocol::PlayerBasicInfo> &PlayerInfos) const {
  std::lock_guard<std::mutex> lock(roomMutex);
  PlayerInfos.reserve(members.size());
  for (auto &[uid, user] : members) {
    PlayerInfos.push_back({.uid = uid,
                           .userName = user->get_username(),
                           .avatarType = user->get_avatar_type()});
  }
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
