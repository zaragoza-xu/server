#include "room.h"

#include <mutex>

#include "server.h"
#include "user.h"

Room::Room(int roomId, size_t maximumPeople, std::shared_ptr<ServerState> state,
           std::shared_ptr<User> creator)
    : state(std::move(state)), roomId(roomId), maximumPeople(maximumPeople) {
  const auto creator_uid = creator->get_uid();
  uids.push_back(creator_uid);
  readyStates.emplace(creator_uid, false);
}

Protocol::RoomInfo Room::get_info() const {
  Protocol::RoomInfo info;
  info.roomId = roomId;
  auto shared_state = state.lock();
  if (!shared_state) {
    std::lock_guard<std::mutex> room_lock(roomMutex);
    info.maximumPeople = maximumPeople;
    for (const auto &[uid, ready] : readyStates) {
      if (ready) {
        info.readyUids.push_back(uid);
      }
    }
    return info;
  }

  std::scoped_lock lock(roomMutex, shared_state->userDataMutex);

  info.maximumPeople = maximumPeople;
  info.basicInfos.reserve(uids.size());
  info.readyUids.reserve(uids.size());

  for (const auto &uid : uids) {
    auto it = shared_state->userData.find(uid);
    if (it != shared_state->userData.end()) {
      info.basicInfos.push_back(it->second.basicInfo);
    }
  }

  for (const auto &[uid, ready] : readyStates) {
    if (ready) {
      info.readyUids.push_back(uid);
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
  readyStates.emplace(uid, false);
  return true;
}
