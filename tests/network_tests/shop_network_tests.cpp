#include <string>

#include <gtest/gtest.h>

#include "integration_helpers.h"
#include "protocol.h"

namespace {

using json = nlohmann::json;

TEST(ShopNetworkTest, ShopServer_InitLongEnvelopeAndMoveCursorPush) {
  network_tests::MultiServiceHarness harness;
  const std::string uid =
      network_tests::auth_register_login(harness);
  network_tests::lobby_create_room(harness, uid, 2);
  network_tests::lobby_ready(harness, uid);

  auto shop = harness.make_shop_client();
  shop->send_json(
      json{{"type", static_cast<int>(Protocol::ShopRequestType::SHOP_INIT)},
           {"uid", uid}});

  const json initLine = shop->read_json();
  ASSERT_EQ(initLine.at("type"),
            static_cast<int>(Protocol::ShopRequestType::SHOP_INIT));
  ASSERT_TRUE(initLine.at("data").contains("items"));
  ASSERT_TRUE(initLine.at("data").contains("playerInfos"));
  const auto &items = initLine.at("data").at("items");
  ASSERT_FALSE(items.empty());

  const std::string itemId =
      items.at(0).at("itemId").get<std::string>();

  // SHOP_MOVE_CURSOR returns NoResponseRsp directly; the expected sync is an
  // async server push on the same connection.
  shop->send_json(
      json{{"type",
            static_cast<int>(Protocol::ShopRequestType::SHOP_MOVE_CURSOR)},
           {"uid", uid},
           {"itemId", itemId}});

  const json sync = shop->read_json();
  EXPECT_EQ(sync.at("type"),
            static_cast<int>(Protocol::ShopResponseType::SHOP_SYNC));
  ASSERT_TRUE(sync.at("data").contains("items"));
}

TEST(ShopNetworkTest, ShopInitWhenNotInRoomReturnsRoomStateError) {
  network_tests::MultiServiceHarness harness;
  const std::string uid = network_tests::auth_register_login(harness);

  auto shop = harness.make_shop_client();
  shop->send_json(
      json{{"type", static_cast<int>(Protocol::ShopRequestType::SHOP_INIT)},
           {"uid", uid}});
  const json rsp = shop->read_json();
  EXPECT_EQ(rsp.at("code"),
            (Protocol::SERVICE_FAIL | Protocol::ROOM_STATE_ERROR));
}

TEST(ShopNetworkTest, ShopInitWhenUidNeverLoggedInReturnsNotFound) {
  network_tests::MultiServiceHarness harness;
  const std::string uid = network_tests::auth_register_only(harness);

  auto shop = harness.make_shop_client();
  shop->send_json(
      json{{"type", static_cast<int>(Protocol::ShopRequestType::SHOP_INIT)},
           {"uid", uid}});
  const json rsp = shop->read_json();
  EXPECT_EQ(rsp.at("code"), (Protocol::SERVICE_FAIL | Protocol::NOT_FOUND));
}

TEST(ShopNetworkTest,
     ShopInitUsesPayloadUidForAnotherMemberInSameRoomP0Contract) {
  network_tests::MultiServiceHarness harness;
  const std::string alice = network_tests::auth_register_login(harness);
  const std::string bob = network_tests::auth_register_login(harness);
  const int roomId = network_tests::lobby_create_room(harness, alice, 2);
  network_tests::lobby_join_room(harness, roomId, bob);
  network_tests::lobby_ready(harness, alice);
  network_tests::lobby_ready(harness, bob);

  auto shop = harness.make_shop_client();
  shop->send_json(
      json{{"type", static_cast<int>(Protocol::ShopRequestType::SHOP_INIT)},
           {"uid", bob}});
  const json rsp = shop->read_json();
  ASSERT_EQ(rsp.at("type").get<int>(),
            static_cast<int>(Protocol::ShopRequestType::SHOP_INIT));
  bool foundBob = false;
  for (const auto &entry : rsp.at("data").at("playerInfos")) {
    if (entry.at("playerInfo").at("uid").get<std::string>() == bob) {
      foundBob = true;
      break;
    }
  }
  EXPECT_TRUE(foundBob);
}

TEST(ShopNetworkTest, ShopBuyInvalidItemReturnsShopInvalidItem) {
  network_tests::MultiServiceHarness harness;
  const std::string uid = network_tests::auth_register_login(harness);
  network_tests::lobby_create_room(harness, uid, 2);
  network_tests::lobby_ready(harness, uid);

  auto shop = harness.make_shop_client();
  shop->send_json(
      json{{"type", static_cast<int>(Protocol::ShopRequestType::SHOP_BUY)},
           {"uid", uid},
           {"itemId", "__not_a_catalog_item__"}});
  const json rsp = shop->read_json();
  EXPECT_EQ(rsp.at("code"),
            (Protocol::SERVICE_FAIL | Protocol::SHOP_INVALID_ITEM));
}

TEST(ShopNetworkTest, ShopBuyAfterPeerBuysReturnsItemTaken) {
  network_tests::MultiServiceHarness harness;
  const std::string alice = network_tests::auth_register_login(harness);
  const std::string bob = network_tests::auth_register_login(harness);
  const int roomId = network_tests::lobby_create_room(harness, alice, 2);
  network_tests::lobby_join_room(harness, roomId, bob);
  network_tests::lobby_ready(harness, alice);
  network_tests::lobby_ready(harness, bob);

  auto shopAlice = harness.make_shop_client();
  shopAlice->send_json(
      json{{"type", static_cast<int>(Protocol::ShopRequestType::SHOP_INIT)},
           {"uid", alice}});
  const json initAlice = shopAlice->read_json();
  ASSERT_EQ(initAlice.at("type").get<int>(),
            static_cast<int>(Protocol::ShopRequestType::SHOP_INIT));
  const std::string itemId =
      initAlice.at("data").at("items").at(0).at("itemId").get<std::string>();

  shopAlice->send_json(
      json{{"type", static_cast<int>(Protocol::ShopRequestType::SHOP_BUY)},
           {"uid", alice},
           {"itemId", itemId}});
  const json alicePush = shopAlice->read_json();
  EXPECT_EQ(alicePush.at("type").get<int>(),
            static_cast<int>(Protocol::ShopResponseType::SHOP_SYNC));

  auto shopBob = harness.make_shop_client();
  shopBob->send_json(
      json{{"type", static_cast<int>(Protocol::ShopRequestType::SHOP_BUY)},
           {"uid", bob},
           {"itemId", itemId}});
  const json bobRsp = shopBob->read_json();
  EXPECT_EQ(bobRsp.at("code"),
            (Protocol::SERVICE_FAIL | Protocol::SHOP_ITEM_TAKEN));
}

} // namespace
