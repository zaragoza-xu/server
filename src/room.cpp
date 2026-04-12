#include "room.h"

#include <mutex>

#include "server.h"
#include "user.h"

Room::Room(int roomId, size_t maximumPeople, std::shared_ptr<ServerState> state,
           std::shared_ptr<User> creator)
    : state(std::move(state)), roomId(roomId), maximumPeople(maximumPeople) {
  uids.push_back(creator->get_uid());
}

Protocol::RoomInfo Room::get_info() const {
  std::scoped_lock lock(roomMutex, state->userInfosMutex);

  Protocol::RoomInfo info;
  info.roomId = roomId;
  info.maximumPeople = maximumPeople;
  info.basicInfos.reserve(uids.size());

  for (const auto &uid : uids) {
    auto it = state->userInfos.find(uid);
    if (it != state->userInfos.end()) {
      info.basicInfos.push_back(it->second);
    }
  }
  return info;
}

bool Room::add_member(std::shared_ptr<User> user) {
  std::lock_guard<std::mutex> lock(roomMutex);
  const auto &uid = user->get_uid();
  if (std::find(uids.begin(), uids.end(), uid) != uids.end()) {
    return false; // Already in room
  }
  if (uids.size() >= maximumPeople)
    return false; // room is full
  uids.push_back(uid);
  return true;
}
