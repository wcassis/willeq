// T66 -- coverage for /equip (eq_commands.cpp RegisterEquipmentCommands()),
// instance 8 of refactor/deferred.md's "PATTERN: real features reachable
// only through a UI gesture, invisible to automation". Prior to this unit,
// EverQuest::SendMoveItem(fromSlot, toSlot, quantity) (eq_merchant.cpp,
// opcode HC_OP_MoveItem=0x420f) was fully implemented but no command or
// intent producer ever invoked it -- T63 had to write inventory.item_id
// directly into the database and restart the client to get a character
// holding a weapon at all.
//
// Modelled on test_eq_commands_vendor.cpp's (T48) and
// test_eq_commands_bank_train.cpp's (T50) MakeEq()/TestBridge/
// ProcessChatInput seam -- /equip lives in the SAME registry
// (EverQuest::m_command_registry) those commands do.
//
// These tests assert what the command pushed onto the bridge's intent queue
// directly (bridge.drainIntents()) -- the SAME MoveItemIntent the inventory
// UI's drag-and-drop gesture pushes (inventory_manager.cpp) -- rather than
// driving all the way through ProcessBridgeIntents() -> SendMoveItem() ->
// QueuePacket(): this unit's territory is the command's reachability into
// the intent system, not re-verifying SendMoveItem/ZoneProcessMoveItem
// themselves (out of scope per the plan's fence). That hop, and whether a
// real server actually equips a real item, is explicitly OUTSTANDING --
// sibling T65 holds the eqemu lock this round; see the plan's Review.

#include <gtest/gtest.h>

#include "client/bridge/game_state_bridge.h"
#include "client/events/renderer_intents.h"
#include "client/inventory/inventory_constants.h"
#include "client/inventory/inventory_manager.h"
#include "client/inventory/item_instance.h"
#include "zone_stream_test_utils.h"

using namespace zonetest;
using namespace eqt::inventory;

namespace {

std::vector<std::string> DrainSystemMessages(eqt::bridge::GameStateBridge& bridge) {
    std::vector<std::string> out;
    for (auto& ev : bridge.drainEvents()) {
        if (ev.type == eqt::state::GameEventType::SystemMessage) {
            out.push_back(std::get<eqt::state::ChatMessageData>(ev.data).message);
        }
    }
    return out;
}

// T71 -- /equip's local-state sync fix (see the plan's Review) requires a
// real item at fromSlot (or a real cursor item, for cursor-source moves)
// before it will push anything -- matching InventoryManager::pickupItem()'s
// own "nothing there to pick up" refusal. Tests exercising the happy path
// now seed the item they intend to move first, the same way a live session
// would have one (via CharInventory/#summonitem) before ever running /equip.
void SeedItem(EverQuest& eq, int16_t slot, uint32_t itemId) {
    ItemInstance item;
    item.itemId = itemId;
    item.name = "Test Item";
    item.slots = 0xFFFFFFFFu;  // fits any equipment slot -- these tests exercise
                                // /equip's wiring, not canEquipItem()'s own
                                // (already-covered-elsewhere) legality logic.
    eq.GetInventoryManager()->setItem(slot, item);
}

// T71 -- the local-state sync fix also pushes EquipmentChangedIntent
// alongside MoveItemIntent whenever an endpoint is an equipment slot (the
// same pairing InventoryManager::pickupItem()/placeItem() already do for
// drag/drop), so happy-path intent counts grew from 1 to (up to) 2. This
// pulls the MoveItemIntent out of a mixed intent list rather than assuming
// its position.
const eqt::events::MoveItemIntent* FindMoveItemIntent(
    const std::vector<eqt::events::RendererIntent>& intents) {
    for (const auto& intent : intents) {
        if (std::holds_alternative<eqt::events::MoveItemIntent>(intent)) {
            return &std::get<eqt::events::MoveItemIntent>(intent);
        }
    }
    return nullptr;
}

class EqCommandsEquipTest : public ::testing::Test {
protected:
    EqCommandsEquipTest() : eq(MakeEq()) { eq.setBridge(&bridge); }
    ~EqCommandsEquipTest() override { eq.setBridge(nullptr); }

    EverQuest eq;
    TestBridge bridge;
};

// ---------------------------------------------------------------------------
// Argument parsing / usage
// ---------------------------------------------------------------------------

TEST_F(EqCommandsEquipTest, NoArgs_PrintsUsage_PushesNoIntent) {
    eq.ProcessChatInput("/equip");

    auto msgs = DrainSystemMessages(bridge);
    ASSERT_EQ(msgs.size(), 1u);
    EXPECT_NE(msgs[0].find("Usage"), std::string::npos);
    EXPECT_TRUE(bridge.drainIntents().empty());
}

TEST_F(EqCommandsEquipTest, MissingSecondSlot_PrintsUsage_PushesNoIntent) {
    eq.ProcessChatInput("/equip 22");

    auto msgs = DrainSystemMessages(bridge);
    ASSERT_EQ(msgs.size(), 1u);
    EXPECT_NE(msgs[0].find("Usage"), std::string::npos);
    EXPECT_TRUE(bridge.drainIntents().empty());
}

TEST_F(EqCommandsEquipTest, NonNumericSlot_PrintsUsage_PushesNoIntent) {
    eq.ProcessChatInput("/equip general 13");

    auto msgs = DrainSystemMessages(bridge);
    ASSERT_EQ(msgs.size(), 1u);
    EXPECT_NE(msgs[0].find("Usage"), std::string::npos);
    EXPECT_TRUE(bridge.drainIntents().empty());
}

TEST_F(EqCommandsEquipTest, TrailingGarbageOnSlot_PrintsUsage_PushesNoIntent) {
    // Same consumed-length discipline as T48's ParseVendorSlot -- "22abc"
    // must be rejected outright, not silently truncated to 22.
    eq.ProcessChatInput("/equip 22abc 13");

    auto msgs = DrainSystemMessages(bridge);
    ASSERT_EQ(msgs.size(), 1u);
    EXPECT_NE(msgs[0].find("Usage"), std::string::npos);
    EXPECT_TRUE(bridge.drainIntents().empty());
}

// ---------------------------------------------------------------------------
// Slot validation -- reject out-of-range/unrecognized slots with a clear
// message rather than emitting a malformed packet (the plan's explicit bar).
// ---------------------------------------------------------------------------

TEST_F(EqCommandsEquipTest, OutOfRangeSourceSlot_PrintsInvalidSourceSlot_PushesNoIntent) {
    // 9999 is not equipment/general/bag/cursor/bank/shared-bank/trade.
    eq.ProcessChatInput("/equip 9999 13");

    auto msgs = DrainSystemMessages(bridge);
    ASSERT_EQ(msgs.size(), 1u);
    EXPECT_NE(msgs[0].find("Invalid source slot"), std::string::npos);
    EXPECT_NE(msgs[0].find("9999"), std::string::npos);
    EXPECT_TRUE(bridge.drainIntents().empty());
}

TEST_F(EqCommandsEquipTest, OutOfRangeDestinationSlot_PrintsInvalidDestinationSlot_PushesNoIntent) {
    eq.ProcessChatInput("/equip 22 9999");

    auto msgs = DrainSystemMessages(bridge);
    ASSERT_EQ(msgs.size(), 1u);
    EXPECT_NE(msgs[0].find("Invalid destination slot"), std::string::npos);
    EXPECT_NE(msgs[0].find("9999"), std::string::npos);
    EXPECT_TRUE(bridge.drainIntents().empty());
}

TEST_F(EqCommandsEquipTest, NegativeSlot_RejectedAsInvalid_PushesNoIntent) {
    // -1 is SLOT_INVALID's own sentinel value -- must not be forwarded as a
    // "valid" slot just because it parses as an integer.
    eq.ProcessChatInput("/equip -1 13");

    auto msgs = DrainSystemMessages(bridge);
    ASSERT_EQ(msgs.size(), 1u);
    EXPECT_NE(msgs[0].find("Invalid source slot"), std::string::npos);
    EXPECT_TRUE(bridge.drainIntents().empty());
}

// ---------------------------------------------------------------------------
// Happy path -- the right intent, with the right fields, is pushed for
// every recognized slot category the plan names.
// ---------------------------------------------------------------------------

TEST_F(EqCommandsEquipTest, GeneralToPrimary_PushesMoveItemIntentWithExactSlots) {
    // The plan's own worked example: "e.g. equip a weapon: /equip 22 13".
    SeedItem(eq, 22, 6011);  // Rusty Mace (T63/T65/T71's shared test item)
    eq.ProcessChatInput("/equip 22 13");

    // The confirmation message is asserted separately below
    // (GeneralToPrimary_PrintsSlotNamesInConfirmation) -- just drain it here.
    DrainSystemMessages(bridge);
    auto intents = bridge.drainIntents();
    // toSlot 13 (Primary) is an equipment slot, so the T71 fix also pushes
    // EquipmentChangedIntent{13} alongside MoveItemIntent (see the comment
    // on FindMoveItemIntent above).
    ASSERT_EQ(intents.size(), 2u);
    const auto* mi = FindMoveItemIntent(intents);
    ASSERT_NE(mi, nullptr);
    EXPECT_EQ(mi->fromSlot, 22);
    EXPECT_EQ(mi->toSlot, 13);
    EXPECT_EQ(mi->quantity, 0u);  // 0 = move entire stack (MoveItemIntent's own documented default)
}

TEST_F(EqCommandsEquipTest, GeneralToPrimary_PrintsSlotNamesInConfirmation) {
    SeedItem(eq, 22, 6011);
    eq.ProcessChatInput("/equip 22 13");

    auto msgs = DrainSystemMessages(bridge);
    ASSERT_EQ(msgs.size(), 1u);
    // toSlot 13 is a canonical equipment slot (SLOT_PRIMARY) -- confirmation
    // should name it via getSlotName(), not just print the raw number.
    EXPECT_NE(msgs[0].find("Primary"), std::string::npos) << msgs[0];
    EXPECT_NE(msgs[0].find("13"), std::string::npos) << msgs[0];
}

TEST_F(EqCommandsEquipTest, UnequipPrimaryToGeneral_PushesMoveItemIntent) {
    // /equip is direction-agnostic -- it wraps SendMoveItem(from, to, qty)
    // exactly, so moving OUT of an equipment slot (unequip) works the same
    // way moving into one does. Scope discipline: this is not a special
    // "unequip" command with its own semantics, just the same wrapper.
    SeedItem(eq, 13, 6011);
    eq.ProcessChatInput("/equip 13 22");

    auto intents = bridge.drainIntents();
    // fromSlot 13 (Primary) is an equipment slot -- same EquipmentChangedIntent
    // pairing as above.
    ASSERT_EQ(intents.size(), 2u);
    const auto* mi = FindMoveItemIntent(intents);
    ASSERT_NE(mi, nullptr);
    EXPECT_EQ(mi->fromSlot, 13);
    EXPECT_EQ(mi->toSlot, 22);
}

TEST_F(EqCommandsEquipTest, CursorToSecondary_PushesMoveItemIntent) {
    // CURSOR_SLOT (30) -> SLOT_SECONDARY (14): the other common real
    // sequence (pick an item up, then equip it).
    SeedItem(eq, CURSOR_SLOT, 9997);  // Dagger (T63/T65/T71's shared test item)
    eq.ProcessChatInput("/equip 30 14");

    auto intents = bridge.drainIntents();
    // toSlot 14 (Secondary) is an equipment slot; this path goes through
    // InventoryManager::placeItem(), which already pairs MoveItemIntent with
    // EquipmentChangedIntent for drag/drop -- same pairing here now.
    ASSERT_EQ(intents.size(), 2u);
    const auto* mi = FindMoveItemIntent(intents);
    ASSERT_NE(mi, nullptr);
    EXPECT_EQ(mi->fromSlot, CURSOR_SLOT);
    EXPECT_EQ(mi->toSlot, SLOT_SECONDARY);
}

TEST_F(EqCommandsEquipTest, BankSlotToGeneral_RecognizedAsValid_PushesMoveItemIntent) {
    // Bank slots (2000-2015) are a recognized category too -- /equip is a
    // generic slot-to-slot wrapper over SendMoveItem, not equip-only.
    SeedItem(eq, BANK_BEGIN, 9990);  // arbitrary stackable test item
    eq.ProcessChatInput("/equip 2000 22");

    auto intents = bridge.drainIntents();
    ASSERT_EQ(intents.size(), 1u);
    const auto& mi = std::get<eqt::events::MoveItemIntent>(intents[0]);
    EXPECT_EQ(mi.fromSlot, BANK_BEGIN);
    EXPECT_EQ(mi.toSlot, 22);
}

// ---------------------------------------------------------------------------
// T71 -- local-state sync. Prior to this fix, /equip never touched
// InventoryManager's own item cache: it only ever pushed the wire-level
// MoveItemIntent, so the client's own view of "what's in this slot" (which
// UpdatePlayerAppearanceFromInventory() reads verbatim) went stale after
// every successful move until an unrelated event refreshed it. These tests
// assert the local model directly, not just the intent that was pushed.
// ---------------------------------------------------------------------------

TEST_F(EqCommandsEquipTest, DirectSwap_UpdatesLocalInventoryModel) {
    // Two occupied equipment slots, no cursor staging -- the server's own
    // SwapItem() (proven live, see the plan's Review) does a single atomic
    // swap here; the local model must match it.
    SeedItem(eq, SLOT_PRIMARY, 6011);    // Rusty Mace
    SeedItem(eq, SLOT_SECONDARY, 9997);  // Dagger
    eq.ProcessChatInput("/equip 13 14");

    auto* inv = eq.GetInventoryManager();
    ASSERT_NE(inv, nullptr);
    ASSERT_NE(inv->getItem(SLOT_PRIMARY), nullptr);
    ASSERT_NE(inv->getItem(SLOT_SECONDARY), nullptr);
    EXPECT_EQ(inv->getItem(SLOT_PRIMARY)->itemId, 9997u);    // Dagger moved in
    EXPECT_EQ(inv->getItem(SLOT_SECONDARY)->itemId, 6011u);  // Mace moved in
}

TEST_F(EqCommandsEquipTest, DirectMoveToEmptySlot_ClearsSourceLocally) {
    SeedItem(eq, 22, 6011);
    eq.ProcessChatInput("/equip 22 13");

    auto* inv = eq.GetInventoryManager();
    ASSERT_NE(inv, nullptr);
    EXPECT_EQ(inv->getItem(22), nullptr);  // source cleared, not left duplicated
    ASSERT_NE(inv->getItem(SLOT_PRIMARY), nullptr);
    EXPECT_EQ(inv->getItem(SLOT_PRIMARY)->itemId, 6011u);
}

TEST_F(EqCommandsEquipTest, EmptySourceSlot_PrintsErrorAndPushesNoIntent) {
    // No item was ever seeded at 13 -- /equip must refuse rather than send
    // a request for a move that cannot mean anything.
    eq.ProcessChatInput("/equip 13 14");

    auto msgs = DrainSystemMessages(bridge);
    ASSERT_EQ(msgs.size(), 1u);
    EXPECT_NE(msgs[0].find("empty"), std::string::npos) << msgs[0];
    EXPECT_TRUE(bridge.drainIntents().empty());
}

TEST_F(EqCommandsEquipTest, CursorToSecondary_UpdatesLocalInventoryModel) {
    SeedItem(eq, SLOT_SECONDARY, 9990);  // pre-existing occupant, displaced to cursor
    SeedItem(eq, CURSOR_SLOT, 9997);     // Dagger, picked up
    eq.ProcessChatInput("/equip 30 14");

    auto* inv = eq.GetInventoryManager();
    ASSERT_NE(inv, nullptr);
    ASSERT_NE(inv->getItem(SLOT_SECONDARY), nullptr);
    EXPECT_EQ(inv->getItem(SLOT_SECONDARY)->itemId, 9997u);  // Dagger placed
    // The displaced original occupant goes back onto the cursor -- matching
    // InventoryManager::placeItem()'s own drag/drop swap semantics (proven
    // live in this unit against a real eqemu server, see the Review).
    ASSERT_TRUE(inv->hasCursorItem());
    EXPECT_EQ(inv->getCursorItem()->itemId, 9990u);
}

}  // namespace
