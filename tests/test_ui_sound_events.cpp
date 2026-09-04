// T328 Leg B -- one test per UI sound the official Titanium client was
// MEASURED to play (refactor/T328_ui_sounds.md, review/T328/README.md).
//
// The seam is PlaySoundIntent: every wired producer pushes it onto the
// GameStateBridge and exactly one consumer arm in ProcessBridgeIntents()
// forwards it to BridgeComponent::PlaySound(). So a plain RecordingBridge is
// enough to assert "this event asks for this sound id" with no audio device,
// no AudioManager and no EQ client data.
//
// The negative tests matter as much as the positive ones: Leg A measured four
// gestures SILENT in the official client, and the rule for this unit is that
// what was measured silent STAYS silent rather than being given a plausible
// sound. Those are pinned here so a later change cannot quietly add one.

#ifdef WITH_AUDIO

#include <gtest/gtest.h>

#include "client/audio/sound_assets.h"
#include "client/audio/ui_sounds.h"
#include "client/bridge/game_state_bridge.h"
#include "client/eq.h"
#include "client/events/renderer_intents.h"
#include "client/inventory/inventory_constants.h"
#include "client/inventory/inventory_manager.h"
#include "client/inventory/item_instance.h"
#include "common/packet_structs.h"
#include "zone_stream_test_utils.h"

#include <cstring>
#include <algorithm>
#include <vector>

using namespace EQT::Audio;
using eqt::inventory::CURSOR_SLOT;
using eqt::inventory::GENERAL_BEGIN;
using eqt::inventory::InventoryManager;
using eqt::inventory::ItemInstance;

namespace {

class RecordingBridge : public eqt::bridge::GameStateBridge {
public:
    void applyEvent(const eqt::state::GameEvent&) override {}
};

// Every sound id asked for, in order, out of a drained intent queue.
std::vector<uint32_t> DrainSoundIds(eqt::bridge::GameStateBridge& bridge) {
    std::vector<uint32_t> ids;
    for (const auto& intent : bridge.drainIntents()) {
        if (std::holds_alternative<eqt::events::PlaySoundIntent>(intent)) {
            ids.push_back(std::get<eqt::events::PlaySoundIntent>(intent).soundId);
        }
    }
    return ids;
}

ItemInstance MakeItem(uint32_t itemId, const char* name) {
    ItemInstance item;
    item.itemId = itemId;
    item.name = name;
    item.slots = 0xFFFFFFFFu;  // fits anywhere; these tests are about the sound
    return item;
}

ItemInstance MakeStack(uint32_t itemId, const char* name, int32_t quantity,
                       int32_t stackSize) {
    ItemInstance item = MakeItem(itemId, name);
    item.stackable = true;
    item.quantity = quantity;
    item.stackSize = stackSize;
    return item;
}

// The SERVER's open/close confirmation -- the only writer of
// EverQuest::m_active_tradeskill_object_id, and so the only point at which
// fable learns a container opened or closed. Same replay helper shape as
// tests/test_eq_action_handler_tradeskill.cpp.
void ReplayClickObjectAction(EverQuest& eq, uint32_t dropId, uint32_t open) {
    EQT::ClickObjectAction_Struct action;
    memset(&action, 0, sizeof(action));
    action.player_id = 1;
    action.drop_id = dropId;
    action.open = open;
    action.type = 17;  // CONTAINER_BLACKSMITHING
    action.unknown16 = 0xA;
    action.icon = 1115;
    snprintf(action.object_name, sizeof(action.object_name), "%s", "Half Elf Forge");

    std::vector<uint8_t> body(sizeof(action));
    memcpy(body.data(), &action, sizeof(action));
    eq.ReplayZonePacket(HC_OP_ClickObjectAction, body.data(), body.size());
}


// T342: the SERVER's confirmation of a completed purchase. eqemu queues
// OP_ShopPlayerBuy only after the sale succeeds -- every failure path in
// Handle_OP_ShopPlayerBuy deletes the outgoing packet and returns -- so
// arriving here IS the purchase. Payload is Merchant_Sell_Struct: npc_id,
// player_id, itemslot, unknown12, quantity, action.
void ReplayShopPlayerBuy(EverQuest& eq, uint32_t itemslot, uint32_t quantity) {
    uint32_t body[6] = {1234u, 1u, itemslot, 0u, quantity, 1u};
    eq.ReplayZonePacket(HC_OP_ShopPlayerBuy,
                        reinterpret_cast<const uint8_t*>(body), sizeof(body));
}

// T342: the SERVER's confirmation of a completed sale
// (EQT::Merchant_Sell_Response_Struct).
void ReplayShopPlayerSell(EverQuest& eq, uint32_t itemslot, uint32_t quantity,
                          uint32_t price) {
    EQT::Merchant_Sell_Response_Struct resp;
    memset(&resp, 0, sizeof(resp));
    resp.npc_id = 1234u;
    resp.itemslot = itemslot;
    resp.quantity = quantity;
    resp.price = price;
    eq.ReplayZonePacket(HC_OP_ShopPlayerSell,
                        reinterpret_cast<const uint8_t*>(&resp), sizeof(resp));
}

// T342: the auto-combine reply. reply_code 0 is a successful combine,
// 0xfffff5 the "you lacked the skills" failure.
void ReplayRecipeAutoCombine(EverQuest& eq, uint32_t recipeId, uint32_t replyCode) {
    EQT::RecipeAutoCombine_Struct combine;
    memset(&combine, 0, sizeof(combine));
    combine.object_type = 17u;
    combine.some_id = 190u;
    combine.recipe_id = recipeId;
    combine.reply_code = replyCode;
    eq.ReplayZonePacket(HC_OP_RecipeAutoCombine,
                        reinterpret_cast<const uint8_t*>(&combine), sizeof(combine));
}

}  // namespace

// ===========================================================================
// The measured ids themselves
// ===========================================================================

// review/T328/README.md, "Two IDs Leg B must add".
TEST(UISoundEvents, MeasuredIdsAreTheOnesLegAReported) {
    EXPECT_EQ(UISoundId::ITEM_CLOTH, 145u);  // itemclth.WAV
    EXPECT_EQ(UISoundId::WEAR_CLOTH, 146u);  // wearclth.WAV
    EXPECT_EQ(UISoundId::BUY_ITEM, 138u);    // BuyItem.WAV -- the coin-move sound

    // The three are distinct events and must stay distinct sounds.
    EXPECT_NE(UISoundId::ITEM_CLOTH, UISoundId::WEAR_CLOTH);
    EXPECT_NE(UISoundId::ITEM_CLOTH, UISoundId::BUY_ITEM);

    // Leg A finding 2: Chest_Op/Chest_Cl are NOT the container sounds.
    EXPECT_NE(UISoundId::ITEM_CLOTH, UISoundId::CHEST_OPEN);
    EXPECT_NE(UISoundId::ITEM_CLOTH, UISoundId::CHEST_CLOSE);
}

// ===========================================================================
// Event 1+2: container open and container close -> itemclth.WAV (145)
// ===========================================================================

TEST(UISoundEvents, ContainerOpen_AsksForItemCloth) {
    EverQuest eq("127.0.0.1", 0, "test", "test", "test", "Testchar");
    RecordingBridge bridge;
    eq.setBridge(&bridge);
    bridge.drainIntents();  // discard anything setBridge produced

    ReplayClickObjectAction(eq, /*dropId=*/190, /*open=*/1);
    ASSERT_EQ(eq.GetActiveTradeskillObjectId(), 190u);

    EXPECT_EQ(DrainSoundIds(bridge), std::vector<uint32_t>{UISoundId::ITEM_CLOTH});
    eq.setBridge(nullptr);
}

TEST(UISoundEvents, ContainerClose_AsksForTheSameItemCloth) {
    EverQuest eq("127.0.0.1", 0, "test", "test", "test", "Testchar");
    RecordingBridge bridge;
    eq.setBridge(&bridge);

    ReplayClickObjectAction(eq, /*dropId=*/190, /*open=*/1);
    bridge.drainIntents();  // drop the open sound; this test is about the close

    ReplayClickObjectAction(eq, /*dropId=*/190, /*open=*/0);
    ASSERT_EQ(eq.GetActiveTradeskillObjectId(), 0u);

    // MEASURED: close is the SAME sound as open (README rows 2, 3 and 5 --
    // the "Done" button, the "X" button and a tradeskill container close all
    // produced itemclth.WAV).
    EXPECT_EQ(DrainSoundIds(bridge), std::vector<uint32_t>{UISoundId::ITEM_CLOTH});
    eq.setBridge(nullptr);
}

// ===========================================================================
// Event 3: an item dropped into a slot -> wearclth.WAV (146)
// ===========================================================================

TEST(UISoundEvents, ItemDroppedIntoEmptySlot_AsksForWearCloth) {
    InventoryManager inv;
    RecordingBridge bridge;
    inv.setBridge(&bridge);

    inv.setItem(GENERAL_BEGIN, MakeItem(1001, "Rusty Dagger"));
    ASSERT_TRUE(inv.pickupItem(GENERAL_BEGIN));
    bridge.drainIntents();  // the pickup's MoveItemIntent, and NO sound

    ASSERT_TRUE(inv.placeItem(GENERAL_BEGIN + 1));

    EXPECT_EQ(DrainSoundIds(bridge), std::vector<uint32_t>{UISoundId::WEAR_CLOTH});
}

TEST(UISoundEvents, ItemDroppedOntoAnOccupiedSlot_AsksForWearClothOnce) {
    InventoryManager inv;
    RecordingBridge bridge;
    inv.setBridge(&bridge);

    inv.setItem(GENERAL_BEGIN, MakeItem(1001, "Rusty Dagger"));
    inv.setItem(GENERAL_BEGIN + 1, MakeItem(1002, "Cracked Staff"));
    ASSERT_TRUE(inv.pickupItem(GENERAL_BEGIN));
    bridge.drainIntents();

    // The swap branch is still one drop, so it is still one sound.
    ASSERT_TRUE(inv.placeItem(GENERAL_BEGIN + 1));

    EXPECT_EQ(DrainSoundIds(bridge), std::vector<uint32_t>{UISoundId::WEAR_CLOTH});
}

TEST(UISoundEvents, StackDepositedOntoAMatchingStack_AsksForWearCloth) {
    InventoryManager inv;
    RecordingBridge bridge;
    inv.setBridge(&bridge);

    // README row 6's second capture, E24_deposit_stack_in_bag.wav: merging a
    // stack is a drop like any other and makes the same sound.
    inv.setItem(GENERAL_BEGIN, MakeStack(1003, "Bat Wing", 5, 20));
    inv.setItem(GENERAL_BEGIN + 1, MakeStack(1003, "Bat Wing", 5, 20));
    ASSERT_TRUE(inv.pickupItem(GENERAL_BEGIN));
    bridge.drainIntents();

    ASSERT_TRUE(inv.placeOnMatchingStack(GENERAL_BEGIN + 1));

    EXPECT_EQ(DrainSoundIds(bridge), std::vector<uint32_t>{UISoundId::WEAR_CLOTH});
}

TEST(UISoundEvents, RefusedPlacementMakesNoSound) {
    InventoryManager inv;
    RecordingBridge bridge;
    inv.setBridge(&bridge);

    // Nothing on the cursor -> placeItem() refuses -> no drop, no sound.
    ASSERT_FALSE(inv.placeItem(GENERAL_BEGIN));

    EXPECT_TRUE(DrainSoundIds(bridge).empty());
}

// ===========================================================================
// Event 4: coin moved onto the cursor -> BuyItem.WAV (138)
// ===========================================================================

TEST(UISoundEvents, CoinOntoCursor_AsksForBuyItem) {
    InventoryManager inv;
    RecordingBridge bridge;
    inv.setBridge(&bridge);

    inv.pickupMoney(InventoryManager::CursorMoneyType::Platinum, 1);
    ASSERT_TRUE(inv.hasCursorMoney());

    // MEASURED (README row 8): BuyItem.WAV fired for 1pp moving from a money
    // slot to the cursor with no merchant present -- it is the generic
    // coin-move sound, not a purchase sound.
    EXPECT_EQ(DrainSoundIds(bridge), std::vector<uint32_t>{UISoundId::BUY_ITEM});
}

TEST(UISoundEvents, CoinPickupOfNothingMakesNoSound) {
    InventoryManager inv;
    RecordingBridge bridge;
    inv.setBridge(&bridge);

    inv.pickupMoney(InventoryManager::CursorMoneyType::Platinum, 0);
    inv.pickupMoney(InventoryManager::CursorMoneyType::None, 5);
    ASSERT_FALSE(inv.hasCursorMoney());

    EXPECT_TRUE(DrainSoundIds(bridge).empty());
}

// ===========================================================================
// MEASURED SILENT -- these must stay silent (review/T328/README.md rows 7, 9)
// ===========================================================================

TEST(UISoundEvents, ItemPickedUpOntoCursorIsSilent) {
    InventoryManager inv;
    RecordingBridge bridge;
    inv.setBridge(&bridge);

    inv.setItem(GENERAL_BEGIN, MakeItem(1001, "Rusty Dagger"));
    ASSERT_TRUE(inv.pickupItem(GENERAL_BEGIN));

    // Row 7: only the DROP makes a sound in the official client.
    EXPECT_TRUE(DrainSoundIds(bridge).empty());
}

TEST(UISoundEvents, PartialStackPickupIsSilent) {
    InventoryManager inv;
    RecordingBridge bridge;
    inv.setBridge(&bridge);

    inv.setItem(GENERAL_BEGIN, MakeStack(1003, "Bat Wing", 10, 20));
    ASSERT_TRUE(inv.pickupPartialStack(GENERAL_BEGIN, 4));

    EXPECT_TRUE(DrainSoundIds(bridge).empty());
}

TEST(UISoundEvents, CoinReturnedFromCursorIsSilent) {
    InventoryManager inv;
    RecordingBridge bridge;
    inv.setBridge(&bridge);

    inv.pickupMoney(InventoryManager::CursorMoneyType::Platinum, 1);
    bridge.drainIntents();  // drop the pickup's sound

    inv.returnCursorMoney();
    ASSERT_FALSE(inv.hasCursorMoney());

    // Row 9: the reverse gesture is silent in the official client.
    EXPECT_TRUE(DrainSoundIds(bridge).empty());
}

// Row 12 / finding 1: Button_1.WAV is preloaded by the official client and
// played by no button tested. There is deliberately no production caller; this
// pins that nothing in the tree asks for it.
TEST(UISoundEvents, ButtonClickIsCataloguedButNeverWired) {
    EXPECT_EQ(UISoundId::BUTTON_CLICK, SoundBank::BUTTON_1);
    EXPECT_EQ(SoundId::BUTTON_CLICK, UISoundId::BUTTON_CLICK);
}

// ===========================================================================
// T342 -- the five rows T328 Leg A could not reach, measured 2026-09-04
// against the official Titanium client (review/T342/README.md).
// ===========================================================================

// Rows "coin pickup, pp / gp / sp / cp". T328 measured ONE denomination (1pp)
// and left the other three open in case the client picked a different sound
// per coin. T342 measured all four in one session and they are the SAME sound:
// BuyItem.WAV (138) every time, with the money-slot click that opens the
// Quantity dialog silent before it and the return to the slot silent after it
// (audio/E11, E14, E17, E20b). pickupMoney() is already denomination-agnostic;
// this pins that it stays that way, so nobody "improves" it into a switch.
TEST(UISoundEvents, CoinOntoCursor_EveryDenominationAsksForTheSameBuyItem) {
    const InventoryManager::CursorMoneyType denominations[] = {
        InventoryManager::CursorMoneyType::Platinum,
        InventoryManager::CursorMoneyType::Gold,
        InventoryManager::CursorMoneyType::Silver,
        InventoryManager::CursorMoneyType::Copper,
    };

    for (auto denomination : denominations) {
        InventoryManager inv;
        RecordingBridge bridge;
        inv.setBridge(&bridge);

        inv.pickupMoney(denomination, 1);
        ASSERT_TRUE(inv.hasCursorMoney());

        EXPECT_EQ(DrainSoundIds(bridge), std::vector<uint32_t>{UISoundId::BUY_ITEM})
            << "denomination " << static_cast<int>(denomination)
            << " must ask for the same sound the oracle plays for every coin";
    }
}

// Row "buy". MEASURED: a purchase plays TWO sounds together, with the same
// onset -- BuyItem.WAV (the coin) and wearclth.WAV (the item landing in a
// slot). Reproduced across two purchases; neither chord is in the control
// windows recorded either side (audio/E42_buy_torch.wav, E43_buy_torch_2.wav).
TEST(UISoundEvents, MerchantBuy_AsksForBuyItemAndWearCloth) {
    EverQuest eq = zonetest::MakeEq();
    RecordingBridge bridge;
    eq.setBridge(&bridge);
    bridge.drainIntents();

    ReplayShopPlayerBuy(eq, /*itemslot=*/3, /*quantity=*/1);

    const std::vector<uint32_t> expected{UISoundId::BUY_ITEM, UISoundId::WEAR_CLOTH};
    EXPECT_EQ(DrainSoundIds(bridge), expected);
    eq.setBridge(nullptr);
}

// A stack purchase is still ONE purchase, so it is still one pair of sounds --
// the oracle does not repeat the sound per item bought.
TEST(UISoundEvents, MerchantBuy_OfAStackStillAsksOnce) {
    EverQuest eq = zonetest::MakeEq();
    RecordingBridge bridge;
    eq.setBridge(&bridge);
    bridge.drainIntents();

    ReplayShopPlayerBuy(eq, /*itemslot=*/3, /*quantity=*/20);

    const std::vector<uint32_t> expected{UISoundId::BUY_ITEM, UISoundId::WEAR_CLOTH};
    EXPECT_EQ(DrainSoundIds(bridge), expected);
    eq.setBridge(nullptr);
}

// Row "sell". MEASURED: a sale plays ONE sound, BuyItem.WAV. The asymmetry
// with the buy above is the oracle's: the item LEAVES a slot, and T328 Leg A
// row 7 already measured "item picked up from a slot" as silent. Reproduced
// across two sales (audio/E45_sell_curskin.wav, E47_sell_curskin_2.wav).
TEST(UISoundEvents, MerchantSell_AsksForBuyItemAndNotWearCloth) {
    EverQuest eq = zonetest::MakeEq();
    RecordingBridge bridge;
    eq.setBridge(&bridge);
    bridge.drainIntents();

    ReplayShopPlayerSell(eq, /*itemslot=*/23, /*quantity=*/1, /*price=*/8);

    const auto ids = DrainSoundIds(bridge);
    EXPECT_EQ(ids, std::vector<uint32_t>{UISoundId::BUY_ITEM});
    EXPECT_EQ(std::count(ids.begin(), ids.end(),
                         static_cast<uint32_t>(UISoundId::WEAR_CLOTH)), 0)
        << "the oracle plays no item-into-slot sound when the item leaves";
    eq.setBridge(nullptr);
}

// Rows "combine success" and "combine fail" -- BOTH MEASURED SILENT, and so
// PINNED silent. Two successful combines (a nofail Celestial Essence, twice)
// and three failures ("You lacked the skills to fashion the items together")
// produced no chord from the tone probe at all; the only sound in those
// windows was a door in the shop, which is in the control windows too.
// This is exactly the kind of row that invites a plausible invention --
// levelup.WAV for the success, LevDn.WAV for the failure -- and the oracle
// makes neither.
TEST(UISoundEvents, TradeskillCombineSuccessIsSilent) {
    EverQuest eq = zonetest::MakeEq();
    RecordingBridge bridge;
    eq.setBridge(&bridge);
    bridge.drainIntents();

    ReplayRecipeAutoCombine(eq, /*recipeId=*/1709, /*replyCode=*/0);

    EXPECT_TRUE(DrainSoundIds(bridge).empty());
    eq.setBridge(nullptr);
}

TEST(UISoundEvents, TradeskillCombineFailureIsSilent) {
    EverQuest eq = zonetest::MakeEq();
    RecordingBridge bridge;
    eq.setBridge(&bridge);
    bridge.drainIntents();

    ReplayRecipeAutoCombine(eq, /*recipeId=*/1417, /*replyCode=*/0xfffff5u);

    EXPECT_TRUE(DrainSoundIds(bridge).empty());
    eq.setBridge(nullptr);
}

// The manual "put items in the grid and hit Combine" path answers on a
// different opcode (an empty OP_TradeSkillCombine acknowledgement). Same
// measurement, same silence -- pinned on both paths so neither can drift.
TEST(UISoundEvents, TradeskillCombineAcknowledgementIsSilent) {
    EverQuest eq = zonetest::MakeEq();
    RecordingBridge bridge;
    eq.setBridge(&bridge);
    bridge.drainIntents();

    eq.ReplayZonePacket(HC_OP_TradeSkillCombine, nullptr, 0);

    EXPECT_TRUE(DrainSoundIds(bridge).empty());
    eq.setBridge(nullptr);
}

#else

#include <gtest/gtest.h>

TEST(AudioDisabledTest, UISoundEventsAudioNotEnabled) {
    GTEST_SKIP() << "Audio support not enabled in build";
}

#endif  // WITH_AUDIO
