#include "server.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include <asio/awaitable.hpp>
#include <asio/co_spawn.hpp>
#include <asio/detached.hpp>
#include <asio/ip/tcp.hpp>
#include <asio/redirect_error.hpp>
#include <asio/use_awaitable.hpp>

#include "battle_config.h"
#include "channel.h"
#include "logging.h"
#include "protocol.h"
#include "room.h"
#include "types.h"
#include "user.h"

using namespace asio::ip;

namespace {
using namespace Battle;
struct ShopCatalogConfig {
  std::string version = "v1";
  std::vector<std::string> itemIds;
};

ShopCatalogConfig default_shop_catalog() {
  return ShopCatalogConfig{.version = "embedded-v1",
                           .itemIds = {"knife_1", "pistol_1", "knife_2",
                                       "pistol_2", "knife_3", "pistol_3",
                                       "knife_4", "pistol_4"}};
}

ShopCatalogConfig load_shop_catalog() {
  const std::vector<std::filesystem::path> candidates = {
      "config/shop_catalog.json", "../config/shop_catalog.json",
      "../../config/shop_catalog.json"};

  for (const auto &path : candidates) {
    std::ifstream input(path);
    if (!input.is_open()) {
      continue;
    }
    try {
      json j;
      input >> j;
      ShopCatalogConfig cfg = default_shop_catalog();
      if (j.contains("version") && j.at("version").is_string()) {
        cfg.version = j.at("version").get<std::string>();
      }
      if (!j.contains("itemIds") || !j.at("itemIds").is_array()) {
        continue;
      }
      cfg.itemIds.clear();
      for (const auto &entry : j.at("itemIds")) {
        if (entry.is_string()) {
          cfg.itemIds.push_back(entry.get<std::string>());
        }
      }
      if (cfg.itemIds.empty()) {
        continue;
      }
      return cfg;
    } catch (...) {
      continue;
    }
  }
  return default_shop_catalog();
}

std::string lower(std::string value) {
  std::transform(
      value.begin(), value.end(), value.begin(),
      [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
  return value;
}

std::optional<BattleEnemyPool> parse_pool(const json &value) {
  if (value.is_number_integer()) {
    switch (value.get<int>()) {
    case 0:
      return BattleEnemyPool::NORMAL;
    case 1:
      return BattleEnemyPool::ELITE;
    case 2:
      return BattleEnemyPool::BOSS;
    default:
      return std::nullopt;
    }
  }
  if (!value.is_string()) {
    return std::nullopt;
  }

  const auto text = lower(value.get<std::string>());
  if (text == "normal") {
    return BattleEnemyPool::NORMAL;
  }
  if (text == "elite") {
    return BattleEnemyPool::ELITE;
  }
  if (text == "boss") {
    return BattleEnemyPool::BOSS;
  }
  return std::nullopt;
}

std::optional<Protocol::BattleEnemyType> parse_enemy(const json &value) {
  if (value.is_number_integer()) {
    if (value.get<int>() ==
        static_cast<int>(Protocol::BattleEnemyType::BUBBLE_FISH)) {
      return Protocol::BattleEnemyType::BUBBLE_FISH;
    }
    return std::nullopt;
  }
  if (!value.is_string()) {
    return std::nullopt;
  }

  const auto text = lower(value.get<std::string>());
  if (text == "bubble_fish" || text == "bubblefish") {
    return Protocol::BattleEnemyType::BUBBLE_FISH;
  }
  return std::nullopt;
}

template <typename T> void read_num(const json &j, const char *key, T &target) {
  if (j.contains(key) && j.at(key).is_number()) {
    target = static_cast<T>(j.at(key).get<double>());
  }
}

void read_str(const json &j, const char *key, std::string &target) {
  if (j.contains(key) && j.at(key).is_string()) {
    target = j.at(key).get<std::string>();
  }
}

void read_bool(const json &j, const char *key, bool &target) {
  if (j.contains(key) && j.at(key).is_boolean()) {
    target = j.at(key).get<bool>();
  }
}

bool valid_battle_config(const BattleConfig &cfg) {
  return cfg.playerMaxHP > 0 && cfg.playerSpeed >= 0.0 &&
         cfg.playerRadius >= 0.0 && cfg.frameRate > 0 &&
         cfg.durationSeconds > 0.0 && cfg.spawnIntervalSeconds > 0.0 &&
         cfg.baseSpawnBudget > 0.0 && cfg.maxCostFactor > 0.0 &&
         cfg.spawnRadiusMin >= 0.0 &&
         cfg.spawnRadiusMax >= cfg.spawnRadiusMin && cfg.bulletSpeed > 0.0 &&
         cfg.bulletDamage > 0 && cfg.bulletRadius >= 0.0 &&
         cfg.enemyRadius >= 0.0 && battle_config_complete(cfg);
}

bool valid_enemy(const BattleEnemyDef &enemy) {
  return enemy.maxHP > 0 && enemy.attackRange >= 0.0 && enemy.maxSpeed >= 0.0 &&
         enemy.knockbackResist >= 0.0 && enemy.knockbackResist <= 1.0 &&
         enemy.attackDamage >= 0 && enemy.attackCooldownTicks >= 0 &&
         enemy.cost > 0.0 && enemy.unlockTime >= 0.0 && enemy.weight > 0.0;
}

bool valid_weapon(const Battle::WeaponDef &weapon) {
  const auto &projectile = weapon.projectile;
  return !weapon.weaponId.empty() && !weapon.weaponName.empty() &&
         weapon.damage >= 0.0 && weapon.attackSpeed >= 0.0 &&
         weapon.range >= 0.0 && weapon.knockback >= 0.0 &&
         weapon.damageGrowth >= 0.0 && weapon.attackSpeedGrowth >= 0.0 &&
         weapon.critChance >= 0.0 && weapon.critChance <= 1.0 &&
         weapon.critMultiplier >= 1.0 && weapon.lifeSteal >= 0.0 &&
         weapon.projectileCount >= 0 && projectile.speed >= 0.0 &&
         projectile.lifetime >= 0.0 && projectile.size >= 0.0 &&
         projectile.pierceCount >= 0 && projectile.pierceDamageFactor >= 0.0 &&
         projectile.bounceCount >= 0 && projectile.explosionRadius >= 0.0;
}

void read_projectile(const json &j, Battle::ProjectileDef &projectile) {
  read_num(j, "speed", projectile.speed);
  read_num(j, "lifetime", projectile.lifetime);
  read_num(j, "size", projectile.size);
  read_bool(j, "canPierce", projectile.canPierce);
  read_num(j, "pierceCount", projectile.pierceCount);
  read_num(j, "pierceDamageFactor", projectile.pierceDamageFactor);
  read_bool(j, "canBounce", projectile.canBounce);
  read_num(j, "bounceCount", projectile.bounceCount);
  read_bool(j, "explosion", projectile.explosion);
  read_num(j, "explosionRadius", projectile.explosionRadius);
}

std::optional<Battle::WeaponDef> read_weapon(const json &entry) {
  if (!entry.is_object() || !entry.contains("weaponId")) {
    return std::nullopt;
  }

  Battle::WeaponDef weapon;
  read_str(entry, "weaponId", weapon.weaponId);
  read_str(entry, "weaponName", weapon.weaponName);
  read_str(entry, "icon", weapon.icon);
  read_num(entry, "damage", weapon.damage);
  read_num(entry, "attackSpeed", weapon.attackSpeed);
  read_num(entry, "range", weapon.range);
  read_num(entry, "knockback", weapon.knockback);
  read_num(entry, "damageGrowth", weapon.damageGrowth);
  read_num(entry, "attackSpeedGrowth", weapon.attackSpeedGrowth);
  read_num(entry, "critChance", weapon.critChance);
  read_num(entry, "critMultiplier", weapon.critMultiplier);
  read_num(entry, "lifeSteal", weapon.lifeSteal);
  read_num(entry, "projectileCount", weapon.projectileCount);

  if (entry.contains("projectile") && entry.at("projectile").is_object()) {
    read_projectile(entry.at("projectile"), weapon.projectile);
  }
  if (entry.contains("tags") && entry.at("tags").is_array()) {
    weapon.tags.clear();
    for (const auto &tag : entry.at("tags")) {
      if (tag.is_string()) {
        weapon.tags.push_back(tag.get<std::string>());
      }
    }
  }
  if (!valid_weapon(weapon)) {
    return std::nullopt;
  }
  return weapon;
}

BattleConfig load_battle_config() {
  const std::vector<std::filesystem::path> candidates = {
      "config/battle_config.json", "../config/battle_config.json",
      "../../config/battle_config.json"};

  for (const auto &path : candidates) {
    std::ifstream input(path);
    if (!input.is_open()) {
      continue;
    }
    try {
      json j;
      input >> j;
      BattleConfig cfg = default_battle_config();

      read_num(j, "playerMaxHP", cfg.playerMaxHP);
      read_num(j, "playerSpeed", cfg.playerSpeed);
      read_num(j, "playerRadius", cfg.playerRadius);
      read_num(j, "frameRate", cfg.frameRate);
      read_num(j, "durationSeconds", cfg.durationSeconds);
      read_num(j, "spawnIntervalSeconds", cfg.spawnIntervalSeconds);
      read_num(j, "baseSpawnBudget", cfg.baseSpawnBudget);
      read_num(j, "difficultyGrowth", cfg.difficultyGrowth);
      read_num(j, "maxCostFactor", cfg.maxCostFactor);
      read_num(j, "spawnRadiusMin", cfg.spawnRadiusMin);
      read_num(j, "spawnRadiusMax", cfg.spawnRadiusMax);
      read_num(j, "bulletSpeed", cfg.bulletSpeed);
      read_num(j, "bulletDamage", cfg.bulletDamage);
      read_num(j, "bulletRadius", cfg.bulletRadius);
      read_num(j, "enemyRadius", cfg.enemyRadius);

      if (j.contains("enemies") && j.at("enemies").is_array()) {
        std::vector<BattleEnemyDef> enemies;
        for (const auto &entry : j.at("enemies")) {
          if (!entry.is_object() || !entry.contains("pool") ||
              !entry.contains("enemyType")) {
            continue;
          }
          auto pool = parse_pool(entry.at("pool"));
          auto enemyType = parse_enemy(entry.at("enemyType"));
          if (!pool.has_value() || !enemyType.has_value()) {
            continue;
          }

          BattleEnemyDef enemy;
          enemy.pool = *pool;
          enemy.enemyType = *enemyType;
          read_num(entry, "maxHP", enemy.maxHP);
          read_num(entry, "attackRange", enemy.attackRange);
          read_num(entry, "maxSpeed", enemy.maxSpeed);
          read_num(entry, "knockbackResist", enemy.knockbackResist);
          read_num(entry, "attackDamage", enemy.attackDamage);
          read_num(entry, "attackCooldownTicks", enemy.attackCooldownTicks);
          read_num(entry, "cost", enemy.cost);
          read_num(entry, "unlockTime", enemy.unlockTime);
          read_num(entry, "weight", enemy.weight);
          if (valid_enemy(enemy)) {
            enemies.push_back(enemy);
          }
        }
        if (!enemies.empty()) {
          cfg.enemies = std::move(enemies);
        }
      }

      if (j.contains("weapons") && j.at("weapons").is_array()) {
        std::vector<Battle::WeaponDef> weapons;
        for (const auto &entry : j.at("weapons")) {
          auto weapon = read_weapon(entry);
          if (weapon.has_value()) {
            weapons.push_back(std::move(*weapon));
          }
        }
        if (!weapons.empty()) {
          cfg.weapons = std::move(weapons);
        }
      }

      if (valid_battle_config(cfg)) {
        return cfg;
      }
    } catch (...) {
      continue;
    }
  }
  return default_battle_config();
}
} // namespace

Server::Server(asio::io_context &context, int port,
               std::shared_ptr<ServerState> sharedState)
    : ioContext(context), port(port),
      acceptor(context, tcp::endpoint(tcp::v4(), port)),
      state(std::move(sharedState)) {
  if (!state) {
    state = std::make_shared<ServerState>();
  }
  if (state->shopCatalogItemIds.empty()) {
    const auto cfg = load_shop_catalog();
    state->shopCatalogVersion = cfg.version;
    state->shopCatalogItemIds = cfg.itemIds;
  }
  if (!battle_config_complete(state->battleConfig)) {
    state->battleConfig = load_battle_config();
  }
}

void Server::start() {
  std::call_once(startOnce, [this]() {
    auto self = shared_from_this();
    asio::co_spawn(
        ioContext,
        [self]() -> asio::awaitable<void> { co_await self->accept_loop(); },
        asio::detached);
  });
}

json Server::dispatch_request(const json &, const std::shared_ptr<Channel> &) {
  logging::log("[dispatch][base] reached fallback dispatcher");
  return json(Protocol::ShortEnvelope::make_env(Protocol::SERVICE_FAIL |
                                                Protocol::BAD_REQUEST));
}

int Server::resolve_room_member_locked(const std::string &uid,
                                       std::shared_ptr<User> &member,
                                       std::shared_ptr<Room> &room) const {
  auto userIt = state->users.find(uid);
  if (userIt == state->users.end() || !userIt->second) {
    return Protocol::SERVICE_FAIL | Protocol::NOT_FOUND;
  }

  member = userIt->second;
  if (!member->is_in_room()) {
    return Protocol::SERVICE_FAIL | Protocol::ROOM_STATE_ERROR;
  }

  const int room_id = member->get_room_id();
  auto roomIt = state->rooms.find(room_id);
  if (roomIt == state->rooms.end()) {
    return Protocol::SERVICE_FAIL | Protocol::NOT_FOUND;
  }

  room = roomIt->second;
  if (!room->is_member(uid)) {
    return Protocol::SERVICE_FAIL | Protocol::ROOM_STATE_ERROR;
  }

  return Protocol::SERVICE_SUCCESS;
}

void Server::bind_channel(const json &request,
                          const std::shared_ptr<Channel> &channel) {
  if (channel && request.contains("uid")) {
    const std::string uid = request.at("uid").get<std::string>();
    if (uid.empty()) {
      return;
    }
    std::lock_guard<std::mutex> lock(userChannelsMutex);
    userChannels[uid] = channel;
  }
}

void Server::broadcast_to_members(const std::vector<std::string> &memberUids,
                                  const Protocol::LongEnvelope &message,
                                  const std::string &excludeUid) {
  const std::string payload = json(message).dump();
  std::vector<std::shared_ptr<Channel>> targets;

  {
    std::lock_guard<std::mutex> lock(userChannelsMutex);
    targets.reserve(memberUids.size());
    for (const auto &uid : memberUids) {
      if (!excludeUid.empty() && uid == excludeUid) {
        continue;
      }
      auto it = userChannels.find(uid);
      if (it == userChannels.end()) {
        continue;
      }
      auto channel = it->second.lock();
      if (!channel) {
        userChannels.erase(it);
        continue;
      }
      targets.push_back(std::move(channel));
    }
  }

  for (auto &channel : targets) {
    asio::co_spawn(
        ioContext,
        [channel, payload]() -> asio::awaitable<void> {
          co_await channel->send_message(payload);
        },
        asio::detached);
  }
}

asio::awaitable<void> Server::accept_loop() {
  while (true) {
    auto chl = std::make_shared<Channel>(ioContext, shared_from_this());

    std::error_code ec;
    co_await acceptor.async_accept(
        chl->getSocket(), asio::redirect_error(asio::use_awaitable, ec));

    if (ec) {
      logging::log("{}", ec.message());
      continue;
    }
    logging::log("accepted connection");
    asio::co_spawn(
        ioContext, [chl]() -> asio::awaitable<void> { co_await chl->run(); },
        asio::detached);
  }
}

int Server::register_user(const Protocol::RegisterReq &req,
                          Protocol::RegisterRsp &rsp) {
  std::scoped_lock lock(state->usersMutex, state->userDataMutex);

  Protocol::PlayerBasicInfo storedInfo = {"", "", 0};
  storedInfo.uid = std::to_string(state->nextUid++);
  state->userData.emplace(storedInfo.uid,
                          (Protocol::PlayerData){.basicInfo = storedInfo});

  rsp.uid = storedInfo.uid;
  return Protocol::SERVICE_SUCCESS;
}

int Server::login_user(const Protocol::LoginReq &req, Protocol::LoginRsp &rsp) {
  std::scoped_lock lock(state->usersMutex, state->userDataMutex);

  if (req.uid == "") {
    return Protocol::SERVICE_FAIL | Protocol::BAD_REQUEST;
  }
  auto infoIt = state->userData.find(req.uid);
  if (infoIt == state->userData.end()) {
    return Protocol::SERVICE_FAIL | Protocol::NOT_FOUND;
  }
  auto onlineIt = state->users.find(req.uid);
  if (onlineIt != state->users.end()) {
    return Protocol::SERVICE_FAIL | Protocol::BAD_REQUEST;
  }

  auto user = std::make_shared<User>(req.uid, state);
  state->users.emplace(req.uid, user);
  rsp.playerData.basicInfo = infoIt->second.basicInfo;
  return Protocol::SERVICE_SUCCESS;
}

int Server::logout_user(const Protocol::LogoutReq &req, Protocol::EmptyRsp &) {
  if (req.uid.empty()) {
    return Protocol::SERVICE_FAIL | Protocol::BAD_REQUEST;
  }

  std::scoped_lock lock(state->usersMutex, state->roomsMutex);
  auto it = state->users.find(req.uid);
  if (it == state->users.end()) {
    return Protocol::SERVICE_FAIL | Protocol::BAD_REQUEST;
  }
  auto user = it->second;

  if (user && user->is_in_room()) {
    std::shared_ptr<User> member;
    std::shared_ptr<Room> room;
    const int resolve_code = resolve_room_member_locked(req.uid, member, room);
    if (resolve_code != Protocol::SERVICE_SUCCESS) {
      return Protocol::ERROR | Protocol::ROOM_STATE_ERROR;
    }

    room->remove_member(req.uid);
    member->set_room_id(-1);
    if (room->get_people_count() == 0) {
      state->rooms.erase(room->get_id());
    }
  }

  state->users.erase(it);

  return Protocol::SERVICE_SUCCESS;
}

std::shared_ptr<User> Server::get_user(const std::string &uid) const {
  std::lock_guard<std::mutex> lock(state->usersMutex);
  auto it = state->users.find(uid);
  if (it != state->users.end()) {
    return it->second;
  }
  return nullptr;
}

bool Server::user_exists(const std::string &uid) const {
  std::lock_guard<std::mutex> lock(state->userDataMutex);
  return state->userData.count(uid) > 0;
}

int Server::edit_profile(const Protocol::EditProfileReq &req,
                         Protocol::EmptyRsp &) {
  std::scoped_lock lock(state->usersMutex, state->userDataMutex);
  auto userIt = state->users.find(req.uid);
  if (userIt == state->users.end() || !userIt->second) {
    return Protocol::SERVICE_FAIL | Protocol::NOT_FOUND;
  }
  auto infoIt = state->userData.find(req.uid);
  if (infoIt == state->userData.end()) {
    return Protocol::SERVICE_FAIL | Protocol::NOT_FOUND;
  }
  infoIt->second.basicInfo = req.basicInfo;
  return Protocol::SERVICE_SUCCESS;
}

int Server::create_room(const Protocol::CreateRoomReq &req,
                        Protocol::CreateRoomRsp &rsp) {
  std::scoped_lock lock(state->usersMutex, state->roomsMutex);
  auto userIt = state->users.find(req.uid);
  if (userIt == state->users.end() || !userIt->second) {
    return Protocol::SERVICE_FAIL | Protocol::NOT_FOUND;
  }
  auto user = userIt->second;
  if (user->is_in_room()) {
    return Protocol::SERVICE_FAIL | Protocol::ROOM_STATE_ERROR;
  }

  int room_id = state->nextRoomId++;
  auto room = std::make_shared<Room>(room_id, req.maximumPeople, state, user);
  state->rooms.emplace(room_id, room);
  user->set_room_id(room_id);
  rsp.roomInfo = room->get_info();
  return Protocol::SERVICE_SUCCESS;
}

int Server::join_room(const Protocol::JoinRoomReq &req,
                      Protocol::JoinRoomRsp &rsp) {
  std::scoped_lock lock(state->usersMutex, state->roomsMutex);

  auto roomIt = state->rooms.find(req.roomId);
  if (roomIt == state->rooms.end()) {
    return Protocol::SERVICE_FAIL | Protocol::NOT_FOUND;
  }
  auto userIt = state->users.find(req.uid);
  if (userIt == state->users.end() || !userIt->second) {
    return Protocol::SERVICE_FAIL | Protocol::NOT_FOUND;
  }

  auto user = userIt->second;
  auto room = roomIt->second;
  if (user->is_in_room()) {
    return Protocol::SERVICE_FAIL | Protocol::ROOM_STATE_ERROR;
  }

  bool success = room->add_member(user);
  if (!success) {
    return Protocol::SERVICE_FAIL | Protocol::ROOM_STATE_ERROR;
  }
  user->set_room_id(room->get_id());
  rsp.roomInfo = room->get_info();
  return Protocol::SERVICE_SUCCESS;
}

int Server::leave_room(const Protocol::LeaveRoomReq &req,
                       Protocol::EmptyRsp &) {
  std::vector<std::string> member_uids;
  Protocol::RoomInfo room_info_after_leave;

  {
    std::scoped_lock lock(state->usersMutex, state->roomsMutex);

    std::shared_ptr<User> member;
    std::shared_ptr<Room> room;
    const int resolve_code = resolve_room_member_locked(req.uid, member, room);
    if (resolve_code != Protocol::SERVICE_SUCCESS) {
      return resolve_code;
    }

    room->remove_member(req.uid);
    member->set_room_id(-1);

    room_info_after_leave = room->get_info();
    member_uids = room->get_member_uids();

    if (room->get_people_count() == 0) {
      state->rooms.erase(room->get_id());
    }
  }

  Protocol::LongEnvelope push;
  push.type = static_cast<int>(Protocol::HomeRequestType::BROADCAST);
  push.pushMessages = {};
  push.data = json{{"uid", req.uid}, {"roomInfo", room_info_after_leave}};
  broadcast_to_members(member_uids, push);

  return Protocol::SERVICE_SUCCESS;
}

int Server::set_ready(const Protocol::SetReadyReq &req,
                      Protocol::NoResponseRsp &) {

  std::vector<std::string> member_uids;
  Protocol::LongEnvelope push;
  {
    std::scoped_lock lock(state->usersMutex, state->roomsMutex);

    std::shared_ptr<User> member;
    std::shared_ptr<Room> room;
    const int resolve_code = resolve_room_member_locked(req.uid, member, room);
    if (resolve_code != Protocol::SERVICE_SUCCESS) {
      return resolve_code;
    }

    if (!room->set_member_ready(req.uid, req.ready)) {
      return Protocol::SERVICE_FAIL | Protocol::ROOM_STATE_ERROR;
    }
    member_uids = room->get_member_uids();

    push.type = static_cast<int>(Protocol::HomeRequestType::BROADCAST);
    push.pushMessages = room->is_all_ready()
                            ? std::list<int>{static_cast<int>(
                                  Protocol::HomePushMessageType::ALL_READY)}
                            : std::list<int>{};
    push.data = json{
        {"uid", req.uid}, {"ready", req.ready}, {"roomInfo", room->get_info()}};
  }

  broadcast_to_members(member_uids, push);
  return Protocol::SERVICE_SUCCESS;
}

int Server::shop_init(const Protocol::ShopInitReq &req,
                      Protocol::ShopInitRsp &rsp) {
  std::scoped_lock lock(state->usersMutex, state->roomsMutex);

  std::shared_ptr<User> member;
  std::shared_ptr<Room> room;
  const int resolve_code = resolve_room_member_locked(req.uid, member, room);
  if (resolve_code != Protocol::SERVICE_SUCCESS) {
    return resolve_code;
  }

  if (!room->get_shop_init(rsp)) {
    return Protocol::SERVICE_FAIL | Protocol::ROOM_STATE_ERROR;
  }
  return Protocol::SERVICE_SUCCESS;
}

int Server::shop_move_cursor(const Protocol::ShopMoveCursorReq &req,
                             Protocol::NoResponseRsp &) {
  std::vector<std::string> member_uids;
  std::vector<Protocol::ShopItem> items;
  {
    std::scoped_lock lock(state->usersMutex, state->roomsMutex);

    std::shared_ptr<User> member;
    std::shared_ptr<Room> room;
    const int resolve_code = resolve_room_member_locked(req.uid, member, room);
    if (resolve_code != Protocol::SERVICE_SUCCESS) {
      return resolve_code;
    }

    const int move_code = room->move_shop_cursor(req.uid, req.itemId, items);
    if (move_code != Protocol::SERVICE_SUCCESS) {
      return move_code;
    }
    member_uids = room->get_member_uids();
  }

  Protocol::LongEnvelope push;
  push.type = static_cast<int>(Protocol::ShopResponseType::SHOP_SYNC);
  push.pushMessages = {};
  push.data = json{{"items", items}};
  broadcast_to_members(member_uids, push);
  return Protocol::SERVICE_SUCCESS;
}

int Server::shop_buy_item(const Protocol::ShopBuyItemReq &req,
                          Protocol::NoResponseRsp &) {
  std::vector<std::string> member_uids;
  std::vector<Protocol::ShopItem> items;
  {
    std::scoped_lock lock(state->usersMutex, state->roomsMutex);

    std::shared_ptr<User> member;
    std::shared_ptr<Room> room;
    const int resolve_code = resolve_room_member_locked(req.uid, member, room);
    if (resolve_code != Protocol::SERVICE_SUCCESS) {
      return resolve_code;
    }

    const int buy_code = room->buy_shop_item(req.uid, req.itemId, items);
    if (buy_code != Protocol::SERVICE_SUCCESS) {
      return buy_code;
    }

    member_uids = room->get_member_uids();
  }

  Protocol::LongEnvelope push;
  push.type = static_cast<int>(Protocol::ShopResponseType::SHOP_SYNC);
  push.pushMessages = {};
  push.data = json{{"items", items}};
  broadcast_to_members(member_uids, push);
  return Protocol::SERVICE_SUCCESS;
}

int Server::map_init(const Protocol::MapInitReq &req,
                     Protocol::MapInitRsp &rsp) {
  std::scoped_lock lock(state->usersMutex, state->roomsMutex);

  std::shared_ptr<User> member;
  std::shared_ptr<Room> room;
  const int resolve_code = resolve_room_member_locked(req.uid, member, room);
  if (resolve_code != Protocol::SERVICE_SUCCESS) {
    return resolve_code;
  }

  if (!room->get_map_init(rsp)) {
    return Protocol::SERVICE_FAIL | Protocol::ROOM_STATE_ERROR;
  }
  return Protocol::SERVICE_SUCCESS;
}

int Server::map_move(const Protocol::MapMoveReq &req,
                     Protocol::NoResponseRsp &) {
  std::vector<std::string> member_uids;
  std::vector<Protocol::MapSync> selectStatus;
  bool committed = false;
  {
    std::scoped_lock lock(state->usersMutex, state->roomsMutex);

    std::shared_ptr<User> member;
    std::shared_ptr<Room> room;
    const int resolve_code = resolve_room_member_locked(req.uid, member, room);
    if (resolve_code != Protocol::SERVICE_SUCCESS) {
      return resolve_code;
    }

    if (!room->move_map(req.uid, req.selectId, selectStatus, committed)) {
      return Protocol::SERVICE_FAIL | Protocol::ROOM_STATE_ERROR;
    }
    member_uids = room->get_member_uids();
  }

  Protocol::LongEnvelope push;
  push.type = static_cast<int>(Protocol::MapRequestType::MAP_MOVE);
  push.pushMessages = committed ? std::list<int>{static_cast<int>(
                                      Protocol::MapResponseType::MAP_SYNC)}
                                : std::list<int>{};
  push.data = json{{"selectStatus", selectStatus}};
  broadcast_to_members(member_uids, push);
  return Protocol::SERVICE_SUCCESS;
}

int Server::list_rooms(const Protocol::ListRoomsReq &,
                       Protocol::ListRoomsRsp &rsp) {
  std::lock_guard<std::mutex> lock(state->roomsMutex);
  rsp.roomInfos.clear();
  rsp.roomInfos.reserve(state->rooms.size());
  // hide rooms that have all members ready
  for (const auto &[id, room] : state->rooms) {
    if (!room->is_all_ready())
      rsp.roomInfos.push_back(room->get_info());
  }
  return Protocol::SERVICE_SUCCESS;
}

json LoginServer::dispatch_request(const json &request,
                                   const std::shared_ptr<Channel> &) {
  return dispatch_table("login", Protocol::LoginRequestType::ERROR,
                        COMMAND_TABLE, request);
}

json HomeServer::dispatch_request(const json &request,
                                  const std::shared_ptr<Channel> &channel) {
  bind_channel(request, channel);
  return dispatch_table("home", Protocol::HomeRequestType::ERROR, COMMAND_TABLE,
                        request);
}

json ShopServer::dispatch_request(const json &request,
                                  const std::shared_ptr<Channel> &channel) {
  bind_channel(request, channel);
  return dispatch_table("shop", Protocol::ShopRequestType::ERROR, COMMAND_TABLE,
                        request);
}

json MapServer::dispatch_request(const json &request,
                                 const std::shared_ptr<Channel> &channel) {
  bind_channel(request, channel);
  constexpr auto fallback = static_cast<Protocol::MapRequestType>(100);
  return dispatch_table("map", fallback, COMMAND_TABLE, request);
}
