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
    eq.ProcessChatInput("/equip 22 13");

    // The confirmation message is asserted separately below
    // (GeneralToPrimary_PrintsSlotNamesInConfirmation) -- just drain it here.
    DrainSystemMessages(bridge);
    auto intents = bridge.drainIntents();
    ASSERT_EQ(intents.size(), 1u);
    ASSERT_TRUE(std::holds_alternative<eqt::events::MoveItemIntent>(intents[0]));
    const auto& mi = std::get<eqt::events::MoveItemIntent>(intents[0]);
    EXPECT_EQ(mi.fromSlot, 22);
    EXPECT_EQ(mi.toSlot, 13);
    EXPECT_EQ(mi.quantity, 0u);  // 0 = move entire stack (MoveItemIntent's own documented default)
}

TEST_F(EqCommandsEquipTest, GeneralToPrimary_PrintsSlotNamesInConfirmation) {
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
    eq.ProcessChatInput("/equip 13 22");

    auto intents = bridge.drainIntents();
    ASSERT_EQ(intents.size(), 1u);
    ASSERT_TRUE(std::holds_alternative<eqt::events::MoveItemIntent>(intents[0]));
    const auto& mi = std::get<eqt::events::MoveItemIntent>(intents[0]);
    EXPECT_EQ(mi.fromSlot, 13);
    EXPECT_EQ(mi.toSlot, 22);
}

TEST_F(EqCommandsEquipTest, CursorToSecondary_PushesMoveItemIntent) {
    // CURSOR_SLOT (30) -> SLOT_SECONDARY (14): the other common real
    // sequence (pick an item up, then equip it).
    eq.ProcessChatInput("/equip 30 14");

    auto intents = bridge.drainIntents();
    ASSERT_EQ(intents.size(), 1u);
    const auto& mi = std::get<eqt::events::MoveItemIntent>(intents[0]);
    EXPECT_EQ(mi.fromSlot, CURSOR_SLOT);
    EXPECT_EQ(mi.toSlot, SLOT_SECONDARY);
}

TEST_F(EqCommandsEquipTest, BankSlotToGeneral_RecognizedAsValid_PushesMoveItemIntent) {
    // Bank slots (2000-2015) are a recognized category too -- /equip is a
    // generic slot-to-slot wrapper over SendMoveItem, not equip-only.
    eq.ProcessChatInput("/equip 2000 22");

    auto intents = bridge.drainIntents();
    ASSERT_EQ(intents.size(), 1u);
    const auto& mi = std::get<eqt::events::MoveItemIntent>(intents[0]);
    EXPECT_EQ(mi.fromSlot, BANK_BEGIN);
    EXPECT_EQ(mi.toSlot, 22);
}

}  // namespace
