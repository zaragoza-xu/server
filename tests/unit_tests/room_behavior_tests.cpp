#include <memory>

#include <gtest/gtest.h>

#include "room.h"
#include "server.h"
#include "user.h"

namespace {

TEST(RoomTest, BasicBehavior) {
  auto state = std::make_shared<ServerState>();

  Protocol::PlayerBasicInfo creatorInfo{"1", "creator", 1};
  state->userData.emplace(creatorInfo.uid,
                          Protocol::PlayerData{.basicInfo = creatorInfo});
  auto creator = std::make_shared<User>(creatorInfo.uid, state);
  Room room(10, 2, state, creator);

  EXPECT_EQ(room.get_id(), 10);
  EXPECT_EQ(room.get_maximum_people(), 2);
  EXPECT_EQ(room.get_people_count(), 1);
  EXPECT_TRUE(room.is_member("1"));

  Protocol::PlayerBasicInfo info2{"2", "u2", 2};
  Protocol::PlayerBasicInfo info3{"3", "u3", 3};
  state->userData.emplace(info2.uid, Protocol::PlayerData{.basicInfo = info2});
  state->userData.emplace(info3.uid, Protocol::PlayerData{.basicInfo = info3});
  auto user2 = std::make_shared<User>(info2.uid, state);
  auto user3 = std::make_shared<User>(info3.uid, state);

  EXPECT_TRUE(room.add_member(user2));
  EXPECT_EQ(room.get_people_count(), 2);
  EXPECT_TRUE(room.is_member("2"));

  // Capacity reached.
  EXPECT_FALSE(room.add_member(user3));
  EXPECT_EQ(room.get_people_count(), 2);

  EXPECT_TRUE(room.remove_member("2"));
  EXPECT_EQ(room.get_people_count(), 1);
  EXPECT_FALSE(room.is_member("2"));

  // Removing non-existent member should return false.
  EXPECT_FALSE(room.remove_member("404"));
}

TEST(RoomTest, ReadyStateBehavior) {
  auto state = std::make_shared<ServerState>();

  Protocol::PlayerBasicInfo creatorInfo{"1", "creator", 1};
  state->userData.emplace(creatorInfo.uid,
                          Protocol::PlayerData{.basicInfo = creatorInfo});
  auto creator = std::make_shared<User>(creatorInfo.uid, state);
  Room room(10, 2, state, creator);

  Protocol::PlayerBasicInfo info2{"2", "u2", 2};
  state->userData.emplace(info2.uid, Protocol::PlayerData{.basicInfo = info2});
  auto user2 = std::make_shared<User>(info2.uid, state);
  ASSERT_TRUE(room.add_member(user2));

  EXPECT_TRUE(room.set_member_ready("1", true));
  EXPECT_TRUE(room.set_member_ready("2", false));
  EXPECT_FALSE(room.set_member_ready("404", true));

  const auto info = room.get_info();
  ASSERT_EQ(info.readyUids.size(), 1U);
  EXPECT_EQ(info.readyUids.front(), "1");

  ASSERT_TRUE(room.remove_member("1"));
  const auto infoAfterLeave = room.get_info();
  EXPECT_TRUE(infoAfterLeave.readyUids.empty());
}

TEST(RoomTest, GetInfoSkipsMissingProfiles) {
  auto state = std::make_shared<ServerState>();

  Protocol::PlayerBasicInfo creatorInfo{"10", "creator", 1};
  Protocol::PlayerBasicInfo info2{"20", "u2", 2};
  state->userData.emplace(creatorInfo.uid,
                          Protocol::PlayerData{.basicInfo = creatorInfo});
  state->userData.emplace(info2.uid, Protocol::PlayerData{.basicInfo = info2});

  auto creator = std::make_shared<User>(creatorInfo.uid, state);
  auto user2 = std::make_shared<User>(info2.uid, state);
  Room room(99, 2, state, creator);
  ASSERT_TRUE(room.add_member(user2));

  state->userData.erase(info2.uid);

  const auto roomInfo = room.get_info();
  ASSERT_EQ(roomInfo.basicInfos.size(), 1U);
  EXPECT_EQ(roomInfo.basicInfos.front().uid, creatorInfo.uid);
}

} // namespace
