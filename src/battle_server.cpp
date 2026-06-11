#include "battle.h"
#include "server.h"

#include <algorithm>
#include <chrono>
#include <list>
#include <memory>
#include <mutex>
#include <system_error>
#include <vector>

#include <asio/co_spawn.hpp>
#include <asio/detached.hpp>
#include <asio/redirect_error.hpp>
#include <asio/use_awaitable.hpp>

#include "channel.h"
#include "logging.h"
#include "room.h"

void BattleServer::start() {
  Server::start();
  std::call_once(tickStartOnce, [this]() {
    auto self = std::static_pointer_cast<BattleServer>(shared_from_this());
    asio::co_spawn(
        tickTimer.get_executor(),
        [self]() -> asio::awaitable<void> { co_await self->tick_loop(); },
        asio::detached);
  });
}

asio::awaitable<void> BattleServer::tick_loop() {
  while (true) {
    auto sharedState = this->get_shared_state();
    const int frameRate =
        (sharedState && sharedState->battleConfig.frameRate > 0)
            ? sharedState->battleConfig.frameRate
            : Battle::default_battle_config().frameRate;
    tickTimer.expires_after(
        std::chrono::milliseconds(std::max(1, 1000 / frameRate)));
    std::error_code ec;
    co_await tickTimer.async_wait(
        asio::redirect_error(asio::use_awaitable, ec));
    if (ec) {
      logging::log("tick_loop exits with ec {}", ec.message());
      break;
    }

    std::vector<std::shared_ptr<Room>> rooms;
    {
      std::lock_guard<std::mutex> lock(sharedState->roomsMutex);
      rooms.reserve(sharedState->rooms.size());
      for (const auto &[roomId, room] : sharedState->rooms) {
        if (room) {
          rooms.push_back(room);
        }
      }
    }

    for (const auto &room : rooms) {
      Protocol::BattleFrameRsp frame;
      bool battleEnded = false;
      if (!room->tick_battle(frame, &battleEnded)) {
        continue;
      }
      std::list<int> pushMessages;
      if (battleEnded) {
        pushMessages.push_back(
            static_cast<int>(Protocol::BattlePushMessageType::BATTLE_END));
      }
      broadcast_to_members(
          room->get_member_uids(),
          Protocol::LongEnvelope::make_env(
              static_cast<int>(Protocol::BattleResponseType::BATTLE_FRAME),
              json(frame), pushMessages));
    }
  }
}

int Server::battle_player_ready(const Protocol::BattlePlayerReadyReq &req,
                                Protocol::NoResponseRsp &) {
  std::vector<std::string> memberUids;
  Protocol::BattleWaitRsp waitRsp;
  Protocol::BattleFrameRsp frame;
  Protocol::LongEnvelope push;
  bool allReady = false;
  {
    std::scoped_lock lock(state->usersMutex, state->roomsMutex);

    std::shared_ptr<User> member;
    std::shared_ptr<Room> room;
    const int resolveCode = resolve_room_member_locked(req.uid, member, room);
    if (resolveCode != Protocol::SERVICE_SUCCESS) {
      return resolveCode;
    }

    if (!room->set_battle_ready(req.uid, waitRsp, allReady)) {
      return Protocol::SERVICE_FAIL | Protocol::ROOM_STATE_ERROR;
    }
    memberUids = room->get_member_uids();
    if (allReady) {
      bool battleEnded = false;
      room->tick_battle(frame, &battleEnded);
      push.type = static_cast<int>(Protocol::BattleResponseType::BATTLE_FRAME);
      push.data = json(frame);
      push.pushMessages = {
          static_cast<int>(Protocol::BattlePushMessageType::BATTLE_START)};
      if (battleEnded) {
        push.pushMessages.push_back(
            static_cast<int>(Protocol::BattlePushMessageType::BATTLE_END));
      }
    }
  }
  if (allReady) {
    broadcast_to_members(memberUids, push);
    return Protocol::SERVICE_SUCCESS;
  }
  broadcast_to_members(
      memberUids,
      Protocol::LongEnvelope::make_env(
          static_cast<int>(Protocol::BattleResponseType::BATTLE_WAIT),
          json(waitRsp)));
  return Protocol::SERVICE_SUCCESS;
}

int Server::battle_sync(const Protocol::BattleSyncReq &req,
                        Protocol::NoResponseRsp &) {
  std::scoped_lock lock(state->usersMutex, state->roomsMutex);

  std::shared_ptr<User> member;
  std::shared_ptr<Room> room;
  const int resolveCode = resolve_room_member_locked(req.uid, member, room);
  if (resolveCode != Protocol::SERVICE_SUCCESS) {
    return resolveCode;
  }

  if (!room->sync_battle(req.uid, req.playerPosition, req.enemyPositions)) {
    return Protocol::SERVICE_FAIL | Protocol::ROOM_STATE_ERROR;
  }
  return Protocol::SERVICE_SUCCESS;
}

int Server::battle_player_shoot(const Protocol::BattlePlayerShootReq &req,
                                Protocol::NoResponseRsp &) {
  std::scoped_lock lock(state->usersMutex, state->roomsMutex);

  std::shared_ptr<User> member;
  std::shared_ptr<Room> room;
  const int resolveCode = resolve_room_member_locked(req.uid, member, room);
  if (resolveCode != Protocol::SERVICE_SUCCESS) {
    return resolveCode;
  }

  if (!room->shoot_battle_player(req.uid, req.direction)) {
    return Protocol::SERVICE_FAIL | Protocol::ROOM_STATE_ERROR;
  }
  return Protocol::SERVICE_SUCCESS;
}

json BattleServer::dispatch_request(const json &request,
                                    const std::shared_ptr<Channel> &channel) {
  bind_channel(request, channel);
  return dispatch_table("battle", Protocol::BattleRequestType::ERROR,
                        COMMAND_TABLE, request);
}
