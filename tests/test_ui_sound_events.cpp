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

#include <cstring>
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

#else

#include <gtest/gtest.h>

TEST(AudioDisabledTest, UISoundEventsAudioNotEnabled) {
    GTEST_SKIP() << "Audio support not enabled in build";
}

#endif  // WITH_AUDIO
