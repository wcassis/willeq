// T66 -- coverage for /equip, instance 8 of refactor/deferred.md's
// "PATTERN: real features reachable only through a UI gesture, invisible to
// automation". Prior to T66, EverQuest::SendMoveItem(fromSlot, toSlot,
// quantity) (eq_merchant.cpp, opcode HC_OP_MoveItem=0x420f) was fully
// implemented but no command or intent producer ever invoked it -- T63 had
// to write inventory.item_id directly into the database and restart the
// client to get a character holding a weapon at all.
//
// T141 RETARGETED this file (T140's precedent, not T136's -- the seam
// survives, so the test is retargeted rather than deleted): /equip's logic
// moved from eq_commands.cpp into InventoryManager, and /equip itself
// MIGRATED out of the game registry into action::CommandProcessor (see
// refactor/T141_equip_extraction.md). EverQuest::m_command_registry no
// longer has an "equip" entry at all, so the old ProcessChatInput("/equip
// ...") entry point this file used (modelled on test_eq_commands_vendor.cpp/
// test_eq_commands_bank_train.cpp's game-registry seam) is GONE. These tests
// now call EverQuest::RequestEquip(args) directly -- the same thin
// orchestration method both the (removed) game-registry handler and the new
// action::EqActionHandler::equip() call, so this file still exercises the
// exact form-dispatch/InventoryManager code path a live "/equip" command
// reaches, just below the registry-selection layer. The action-registry
// wiring itself (registration/category, CommandProcessor -> ActionDispatcher
// -> EqActionHandler::equip() -> RequestEquip()) is covered separately in
// test_command_processor.cpp (T136's precedent for vendor/buy/sell) and
// test_eq_action_handler_vendor.cpp-style real-EverQuest action tests --
// see refactor/T141_equip_extraction.md's Review for the split.
//
// These tests assert what the command pushed onto the bridge's intent queue
// directly (bridge.drainIntents()) -- the SAME MoveItemIntent the inventory
// UI's drag-and-drop gesture pushes (InventoryManager::moveItem,
// inventory_cursor.cpp) -- rather than
// driving all the way through ProcessBridgeIntents() -> SendMoveItem() ->
// QueuePacket(): this unit's territory is the command's reachability into
// the intent system, not re-verifying SendMoveItem/ZoneProcessMoveItem
// themselves (out of scope per the plan's fence). That hop, and whether a
// real server actually equips a real item, is covered by this unit's live
// leg -- see refactor/T141_equip_extraction.md's Review.

#include <gtest/gtest.h>

#include "client/bridge/game_state_bridge.h"
#include "client/events/renderer_intents.h"
#include "client/inventory/inventory_constants.h"
#include "client/inventory/inventory_manager.h"
#include "client/inventory/item_instance.h"
#include "client/trade_manager.h"
#include "common/packet_structs.h"
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
    eq.RequestEquip("");

    auto msgs = DrainSystemMessages(bridge);
    ASSERT_EQ(msgs.size(), 1u);
    EXPECT_NE(msgs[0].find("Usage"), std::string::npos);
    EXPECT_TRUE(bridge.drainIntents().empty());
}

TEST_F(EqCommandsEquipTest, MissingSecondSlot_PrintsUsage_PushesNoIntent) {
    eq.RequestEquip("22");

    auto msgs = DrainSystemMessages(bridge);
    ASSERT_EQ(msgs.size(), 1u);
    EXPECT_NE(msgs[0].find("Usage"), std::string::npos);
    EXPECT_TRUE(bridge.drainIntents().empty());
}

// T77 -- a first token that does NOT look like an attempted slot number
// (doesn't start with a digit or '-') is now routed to the item-name form
// instead of the slot form, since item names are free text and may be
// multiple words (e.g. "/equip Rusty Mace"). "general 13" is therefore no
// longer a slot-usage error -- it's an item-name search for the literal
// text "general 13", which finds nothing in an empty inventory.
TEST_F(EqCommandsEquipTest, NonNumericFirstToken_TreatedAsItemNameSearch_NotFound) {
    eq.RequestEquip("general 13");

    auto msgs = DrainSystemMessages(bridge);
    ASSERT_EQ(msgs.size(), 1u);
    EXPECT_NE(msgs[0].find("No carried item matches"), std::string::npos) << msgs[0];
    EXPECT_TRUE(bridge.drainIntents().empty());
}

TEST_F(EqCommandsEquipTest, TrailingGarbageOnSlot_PrintsUsage_PushesNoIntent) {
    // "22abc" starts with a digit, so T77's firstLooksNumeric heuristic
    // still routes it through the strict slot-form parser -- same
    // consumed-length discipline as T48's ParseVendorSlot: it must be
    // rejected outright, not silently truncated to 22, and not reinterpreted
    // as an item-name search.
    eq.RequestEquip("22abc 13");

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
    eq.RequestEquip("9999 13");

    auto msgs = DrainSystemMessages(bridge);
    ASSERT_EQ(msgs.size(), 1u);
    EXPECT_NE(msgs[0].find("Invalid source slot"), std::string::npos);
    EXPECT_NE(msgs[0].find("9999"), std::string::npos);
    EXPECT_TRUE(bridge.drainIntents().empty());
}

TEST_F(EqCommandsEquipTest, OutOfRangeDestinationSlot_PrintsInvalidDestinationSlot_PushesNoIntent) {
    eq.RequestEquip("22 9999");

    auto msgs = DrainSystemMessages(bridge);
    ASSERT_EQ(msgs.size(), 1u);
    EXPECT_NE(msgs[0].find("Invalid destination slot"), std::string::npos);
    EXPECT_NE(msgs[0].find("9999"), std::string::npos);
    EXPECT_TRUE(bridge.drainIntents().empty());
}

TEST_F(EqCommandsEquipTest, NegativeSlot_RejectedAsInvalid_PushesNoIntent) {
    // -1 is SLOT_INVALID's own sentinel value -- must not be forwarded as a
    // "valid" slot just because it parses as an integer.
    eq.RequestEquip("-1 13");

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
    eq.RequestEquip("22 13");

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
    eq.RequestEquip("22 13");

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
    eq.RequestEquip("13 22");

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
    eq.RequestEquip("30 14");

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
    eq.RequestEquip("2000 22");

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
    eq.RequestEquip("13 14");

    auto* inv = eq.GetInventoryManager();
    ASSERT_NE(inv, nullptr);
    ASSERT_NE(inv->getItem(SLOT_PRIMARY), nullptr);
    ASSERT_NE(inv->getItem(SLOT_SECONDARY), nullptr);
    EXPECT_EQ(inv->getItem(SLOT_PRIMARY)->itemId, 9997u);    // Dagger moved in
    EXPECT_EQ(inv->getItem(SLOT_SECONDARY)->itemId, 6011u);  // Mace moved in
}

TEST_F(EqCommandsEquipTest, DirectMoveToEmptySlot_ClearsSourceLocally) {
    SeedItem(eq, 22, 6011);
    eq.RequestEquip("22 13");

    auto* inv = eq.GetInventoryManager();
    ASSERT_NE(inv, nullptr);
    EXPECT_EQ(inv->getItem(22), nullptr);  // source cleared, not left duplicated
    ASSERT_NE(inv->getItem(SLOT_PRIMARY), nullptr);
    EXPECT_EQ(inv->getItem(SLOT_PRIMARY)->itemId, 6011u);
}

TEST_F(EqCommandsEquipTest, EmptySourceSlot_PrintsErrorAndPushesNoIntent) {
    // No item was ever seeded at 13 -- /equip must refuse rather than send
    // a request for a move that cannot mean anything.
    eq.RequestEquip("13 14");

    auto msgs = DrainSystemMessages(bridge);
    ASSERT_EQ(msgs.size(), 1u);
    EXPECT_NE(msgs[0].find("empty"), std::string::npos) << msgs[0];
    EXPECT_TRUE(bridge.drainIntents().empty());
}

TEST_F(EqCommandsEquipTest, CursorToSecondary_UpdatesLocalInventoryModel) {
    SeedItem(eq, SLOT_SECONDARY, 9990);  // pre-existing occupant, displaced to cursor
    SeedItem(eq, CURSOR_SLOT, 9997);     // Dagger, picked up
    eq.RequestEquip("30 14");

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

// ---------------------------------------------------------------------------
// T77 -- pre-flight destination legality. Reuses InventoryManager::
// canEquipItem()/ItemInstance::canEquipInSlot() -- no new predicate is
// written for this unit; these tests confirm the EXISTING validation is now
// actually consulted before a move is executed or sent.
// ---------------------------------------------------------------------------

// Seeds an item with full control over name/slots-bitmask/reqLevel, unlike
// SeedItem() above (which deliberately fits any equip slot to isolate the
// wiring tests above from legality). slots=0 by default means "fits no
// equipment slot at all" (ItemInstance's own default) -- callers pass the
// EQUIPSLOT_* bit(s) they want.
void SeedNamedItem(EverQuest& eq, int16_t slot, uint32_t itemId, const std::string& name,
                    uint32_t slotsBitmask, uint8_t reqLevel = 0) {
    ItemInstance item;
    item.itemId = itemId;
    item.name = name;
    item.slots = slotsBitmask;
    item.reqLevel = reqLevel;
    eq.GetInventoryManager()->setItem(slot, item);
}

TEST_F(EqCommandsEquipTest, WrongSlotType_RejectedLocally_NamesItemAndSlot_PushesNoIntent) {
    // A Primary-only weapon aimed at Head (slot 2): canEquipInSlot() fails
    // first (wrong bitmask), not the restrictions check.
    SeedNamedItem(eq, 22, 7001, "Rusty Mace", EQUIPSLOT_PRIMARY);
    eq.RequestEquip("22 2");

    auto msgs = DrainSystemMessages(bridge);
    ASSERT_EQ(msgs.size(), 1u);
    EXPECT_NE(msgs[0].find("Rusty Mace"), std::string::npos) << msgs[0];
    EXPECT_NE(msgs[0].find("Head"), std::string::npos) << msgs[0];
    EXPECT_TRUE(bridge.drainIntents().empty());
    // Local model must be untouched -- pre-flight rejected before any mutation.
    auto* inv = eq.GetInventoryManager();
    EXPECT_NE(inv->getItem(22), nullptr);
    EXPECT_EQ(inv->getItem(2), nullptr);
}

TEST_F(EqCommandsEquipTest, RestrictionFailure_RejectedLocally_PushesNoIntent) {
    // Fits Primary by slot type, but reqLevel=50 against the default test
    // player (level 1, from InventoryManager's own playerLevel_ default) --
    // canEquipInSlot() passes, meetsRestrictions() fails.
    SeedNamedItem(eq, 22, 7002, "Ancient Blade", EQUIPSLOT_PRIMARY, /*reqLevel=*/50);
    eq.RequestEquip("22 13");

    auto msgs = DrainSystemMessages(bridge);
    ASSERT_EQ(msgs.size(), 1u);
    EXPECT_NE(msgs[0].find("Ancient Blade"), std::string::npos) << msgs[0];
    EXPECT_NE(msgs[0].find("does not meet"), std::string::npos) << msgs[0];
    EXPECT_TRUE(bridge.drainIntents().empty());
}

TEST_F(EqCommandsEquipTest, LegalDestination_StillMoves) {
    // Confirms the pre-flight check is not overly strict -- a legal
    // destination still goes through exactly as before.
    SeedNamedItem(eq, 22, 7003, "Rusty Mace", EQUIPSLOT_PRIMARY);
    eq.RequestEquip("22 13");

    auto* inv = eq.GetInventoryManager();
    ASSERT_NE(inv->getItem(SLOT_PRIMARY), nullptr);
    EXPECT_EQ(inv->getItem(SLOT_PRIMARY)->itemId, 7003u);
    auto intents = bridge.drainIntents();
    EXPECT_NE(FindMoveItemIntent(intents), nullptr);
}

TEST_F(EqCommandsEquipTest, NoOpMove_SameSlot_RejectedLocally_PushesNoIntent) {
    SeedItem(eq, 22, 6011);
    eq.RequestEquip("22 22");

    auto msgs = DrainSystemMessages(bridge);
    ASSERT_EQ(msgs.size(), 1u);
    EXPECT_NE(msgs[0].find("same slot"), std::string::npos) << msgs[0];
    EXPECT_TRUE(bridge.drainIntents().empty());
}

// ---------------------------------------------------------------------------
// T77 -- item-name form: `/equip <item name>`.
// ---------------------------------------------------------------------------

TEST_F(EqCommandsEquipTest, NameForm_ResolvesSlotFromItemBitmask_MovesAndReports) {
    SeedNamedItem(eq, 22, 7010, "Rusty Mace", EQUIPSLOT_PRIMARY);
    eq.RequestEquip("Rusty Mace");

    auto msgs = DrainSystemMessages(bridge);
    // "Resolved '<name>' to ..." followed by the usual "Moving item: ..."
    // confirmation -- a script can assert on either.
    ASSERT_EQ(msgs.size(), 2u);
    EXPECT_NE(msgs[0].find("Resolved"), std::string::npos) << msgs[0];
    EXPECT_NE(msgs[0].find("Rusty Mace"), std::string::npos) << msgs[0];
    EXPECT_NE(msgs[0].find("Primary"), std::string::npos) << msgs[0];

    auto* inv = eq.GetInventoryManager();
    ASSERT_NE(inv->getItem(SLOT_PRIMARY), nullptr);
    EXPECT_EQ(inv->getItem(SLOT_PRIMARY)->itemId, 7010u);
}

TEST_F(EqCommandsEquipTest, NameForm_SubstringMatch_CaseInsensitive) {
    SeedNamedItem(eq, 22, 7011, "Rusty Mace", EQUIPSLOT_PRIMARY);
    eq.RequestEquip("rusty");

    auto* inv = eq.GetInventoryManager();
    ASSERT_NE(inv->getItem(SLOT_PRIMARY), nullptr);
    EXPECT_EQ(inv->getItem(SLOT_PRIMARY)->itemId, 7011u);
}

TEST_F(EqCommandsEquipTest, NameForm_NotFound_PushesNoIntent) {
    eq.RequestEquip("nonexistent item");

    auto msgs = DrainSystemMessages(bridge);
    ASSERT_EQ(msgs.size(), 1u);
    EXPECT_NE(msgs[0].find("No carried item matches"), std::string::npos) << msgs[0];
    EXPECT_TRUE(bridge.drainIntents().empty());
}

TEST_F(EqCommandsEquipTest, NameForm_AmbiguousAcrossDistinctItems_RejectedWithBothNamed) {
    SeedNamedItem(eq, 22, 7020, "Rusty Sword", EQUIPSLOT_PRIMARY);
    SeedNamedItem(eq, 23, 7021, "Broad Sword", EQUIPSLOT_PRIMARY);
    eq.RequestEquip("sword");

    auto msgs = DrainSystemMessages(bridge);
    ASSERT_EQ(msgs.size(), 1u);
    EXPECT_NE(msgs[0].find("multiple"), std::string::npos) << msgs[0];
    EXPECT_NE(msgs[0].find("Rusty Sword"), std::string::npos) << msgs[0];
    EXPECT_NE(msgs[0].find("Broad Sword"), std::string::npos) << msgs[0];
    EXPECT_TRUE(bridge.drainIntents().empty());
}

TEST_F(EqCommandsEquipTest, NameForm_DuplicateStacksOfSameItem_NotAmbiguous_TakesFirst) {
    // Two slots holding the identical item name -- interchangeable for
    // equip purposes, so this must NOT be treated as ambiguous.
    SeedNamedItem(eq, 22, 7030, "Throwing Dagger", EQUIPSLOT_PRIMARY);
    SeedNamedItem(eq, 23, 7030, "Throwing Dagger", EQUIPSLOT_PRIMARY);
    eq.RequestEquip("Throwing Dagger");

    auto msgs = DrainSystemMessages(bridge);
    for (const auto& m : msgs) {
        EXPECT_EQ(m.find("multiple"), std::string::npos) << m;
    }
    auto* inv = eq.GetInventoryManager();
    ASSERT_NE(inv->getItem(SLOT_PRIMARY), nullptr);
    EXPECT_EQ(inv->getItem(SLOT_PRIMARY)->itemId, 7030u);
}

TEST_F(EqCommandsEquipTest, NameForm_PairedSlot_PrefersEmptyEar) {
    // An earring fits both SLOT_EAR1 (1) and SLOT_EAR2 (4) via
    // ItemInstance::canEquipInSlot()'s paired-slot handling. EAR1 already
    // occupied -- resolution must prefer the empty EAR2, not clobber EAR1.
    SeedNamedItem(eq, SLOT_EAR1, 8000, "Old Earring", EQUIPSLOT_EAR);
    SeedNamedItem(eq, 22, 8001, "Jade Earring", EQUIPSLOT_EAR);
    eq.RequestEquip("Jade Earring");

    auto* inv = eq.GetInventoryManager();
    ASSERT_NE(inv->getItem(SLOT_EAR2), nullptr);
    EXPECT_EQ(inv->getItem(SLOT_EAR2)->itemId, 8001u);
    // EAR1's original occupant is untouched.
    ASSERT_NE(inv->getItem(SLOT_EAR1), nullptr);
    EXPECT_EQ(inv->getItem(SLOT_EAR1)->itemId, 8000u);
}

TEST_F(EqCommandsEquipTest, NameForm_BothPairedSlotsFull_ReportsWouldReplace_StillResolves) {
    SeedNamedItem(eq, SLOT_EAR1, 8010, "Old Earring", EQUIPSLOT_EAR);
    SeedNamedItem(eq, SLOT_EAR2, 8011, "Older Earring", EQUIPSLOT_EAR);
    SeedNamedItem(eq, 22, 8012, "New Earring", EQUIPSLOT_EAR);
    eq.RequestEquip("New Earring");

    auto msgs = DrainSystemMessages(bridge);
    ASSERT_GE(msgs.size(), 1u);
    EXPECT_NE(msgs[0].find("would replace"), std::string::npos) << msgs[0];
    // Falls back to the first candidate (SLOT_EAR1) and still executes --
    // pre-flight above (LegalDestination_StillMoves) already confirms
    // canEquipItem() passes for this item/slot pair.
    auto* inv = eq.GetInventoryManager();
    ASSERT_NE(inv->getItem(SLOT_EAR1), nullptr);
    EXPECT_EQ(inv->getItem(SLOT_EAR1)->itemId, 8012u);
}

TEST_F(EqCommandsEquipTest, NameForm_ItemFitsNoEquipSlot_RejectedWithReason) {
    SeedNamedItem(eq, 22, 8020, "A Rock", /*slotsBitmask=*/0);
    eq.RequestEquip("Rock");

    auto msgs = DrainSystemMessages(bridge);
    ASSERT_EQ(msgs.size(), 1u);
    EXPECT_NE(msgs[0].find("A Rock"), std::string::npos) << msgs[0];
    EXPECT_NE(msgs[0].find("cannot be equipped"), std::string::npos) << msgs[0];
    EXPECT_TRUE(bridge.drainIntents().empty());
}

// ---------------------------------------------------------------------------
// T77 -- `/equip list`.
// ---------------------------------------------------------------------------

TEST_F(EqCommandsEquipTest, ListForm_EmptyInventory_ReportsNothing) {
    eq.RequestEquip("list");

    auto msgs = DrainSystemMessages(bridge);
    ASSERT_EQ(msgs.size(), 2u);
    EXPECT_NE(msgs[0].find("Nothing currently equipped"), std::string::npos) << msgs[0];
    EXPECT_NE(msgs[1].find("Nothing else carried"), std::string::npos) << msgs[1];
}

TEST_F(EqCommandsEquipTest, ListForm_ShowsEquippedAndCarriedWithResolution) {
    SeedNamedItem(eq, SLOT_PRIMARY, 9100, "Rusty Mace", EQUIPSLOT_PRIMARY);
    SeedNamedItem(eq, 22, 9101, "Dagger", EQUIPSLOT_SECONDARY);
    eq.RequestEquip("list");

    auto msgs = DrainSystemMessages(bridge);
    ASSERT_GE(msgs.size(), 4u);
    EXPECT_NE(msgs[0].find("Equipped"), std::string::npos) << msgs[0];
    bool sawMaceEquipped = false, sawDaggerCarried = false;
    for (const auto& m : msgs) {
        if (m.find("Rusty Mace") != std::string::npos && m.find("Primary") != std::string::npos) {
            sawMaceEquipped = true;
        }
        if (m.find("Dagger") != std::string::npos && m.find("Secondary") != std::string::npos) {
            sawDaggerCarried = true;
        }
    }
    EXPECT_TRUE(sawMaceEquipped);
    EXPECT_TRUE(sawDaggerCarried);
    // /equip list is read-only.
    EXPECT_TRUE(bridge.drainIntents().empty());
}


// ---------------------------------------------------------------------------
// T236 -- swap pre-flight: the DISPLACED item must be legal for the slot it
// lands in. eqemu's swap rule (common/inventory_profile.cpp,
// InventoryProfile::SwapItem, the `destination_item_instance` block):
// the item already in the destination must IsSlotAllowed(source_slot), and
// when source_slot is an equipment slot it must additionally pass the
// race/class, deity and level checks -- exactly the checks canEquipItem()
// already runs for the forward direction. IsSlotAllowed() is always true
// for any non-equipment slot (item_instance.cpp: `slot_id > EQUIPMENT_END`),
// so the only swap the server can refuse on the displaced side is one whose
// SOURCE is an equipment slot. T82 hit this live (`/equip 13 22` with a
// Skin of Milk in 22): the client swapped locally, the server replied "The
// selected slot was invalid." and resynced. Pre-flight it like T77 did for
// the forward direction: refuse locally, mutate nothing, send nothing.
// ---------------------------------------------------------------------------

TEST_F(EqCommandsEquipTest, Swap_DisplacedItemCannotGoInSourceEquipSlot_RejectedLocally_NoIntent_NoMutation) {
    // T82's live case: Primary holds a weapon, general slot 22 holds a
    // non-equippable item (slots bitmask 0). Swapping them would land the
    // Milk in Primary -- the server refuses that (swapNotAllowed).
    SeedNamedItem(eq, SLOT_PRIMARY, 7100, "Rusty Mace", EQUIPSLOT_PRIMARY);
    SeedNamedItem(eq, 22, 7101, "Skin of Milk", /*slotsBitmask=*/0);
    eq.RequestEquip("13 22");

    auto msgs = DrainSystemMessages(bridge);
    ASSERT_EQ(msgs.size(), 1u);
    EXPECT_NE(msgs[0].find("Skin of Milk"), std::string::npos) << msgs[0];
    EXPECT_NE(msgs[0].find("Primary"), std::string::npos) << msgs[0];
    EXPECT_TRUE(bridge.drainIntents().empty());
    auto* inv = eq.GetInventoryManager();
    ASSERT_NE(inv->getItem(SLOT_PRIMARY), nullptr);
    EXPECT_EQ(inv->getItem(SLOT_PRIMARY)->itemId, 7100u);
    ASSERT_NE(inv->getItem(22), nullptr);
    EXPECT_EQ(inv->getItem(22)->itemId, 7101u);
}

TEST_F(EqCommandsEquipTest, Swap_DisplacedItemFailsRestrictionsForSourceEquipSlot_RejectedLocally) {
    // Displaced item fits Primary by slot type but reqLevel=50 against the
    // level-1 test player: the server's swapLevel branch ("You are not
    // sufficient level to use this item.") on the displaced side.
    SeedNamedItem(eq, SLOT_PRIMARY, 7110, "Rusty Mace", EQUIPSLOT_PRIMARY);
    SeedNamedItem(eq, 22, 7111, "Ancient Blade", EQUIPSLOT_PRIMARY, /*reqLevel=*/50);
    eq.RequestEquip("13 22");

    auto msgs = DrainSystemMessages(bridge);
    ASSERT_EQ(msgs.size(), 1u);
    EXPECT_NE(msgs[0].find("Ancient Blade"), std::string::npos) << msgs[0];
    EXPECT_NE(msgs[0].find("does not meet"), std::string::npos) << msgs[0];
    EXPECT_TRUE(bridge.drainIntents().empty());
    auto* inv = eq.GetInventoryManager();
    EXPECT_EQ(inv->getItem(SLOT_PRIMARY)->itemId, 7110u);
    EXPECT_EQ(inv->getItem(22)->itemId, 7111u);
}

TEST_F(EqCommandsEquipTest, Swap_DisplacedItemLegalForSourceEquipSlot_StillMoves) {
    // Both directions legal (weapon <-> weapon): the swap goes through and
    // the packet is sent exactly as before -- the new check is not overly
    // strict.
    SeedNamedItem(eq, SLOT_PRIMARY, 7120, "Rusty Mace", EQUIPSLOT_PRIMARY);
    SeedNamedItem(eq, 22, 7121, "Broad Sword", EQUIPSLOT_PRIMARY);
    eq.RequestEquip("13 22");

    auto msgs = DrainSystemMessages(bridge);
    ASSERT_EQ(msgs.size(), 1u);
    EXPECT_NE(msgs[0].find("Moving item"), std::string::npos) << msgs[0];
    auto intents = bridge.drainIntents();
    const auto* mv = FindMoveItemIntent(intents);
    ASSERT_NE(mv, nullptr);
    EXPECT_EQ(mv->fromSlot, SLOT_PRIMARY);
    EXPECT_EQ(mv->toSlot, 22);
    auto* inv = eq.GetInventoryManager();
    EXPECT_EQ(inv->getItem(SLOT_PRIMARY)->itemId, 7121u);
    EXPECT_EQ(inv->getItem(22)->itemId, 7120u);
}

TEST_F(EqCommandsEquipTest, Swap_SourceIsGeneralSlot_DisplacedItemNotEquipChecked_StillMoves) {
    // Server rule: IsSlotAllowed() is unconditionally true for a
    // non-equipment source slot, so a general<->general swap involving a
    // non-equippable item is legal server-side and must stay legal here.
    SeedNamedItem(eq, 22, 7130, "Rusty Mace", EQUIPSLOT_PRIMARY);
    SeedNamedItem(eq, 23, 7131, "Skin of Milk", /*slotsBitmask=*/0);
    eq.RequestEquip("22 23");

    auto msgs = DrainSystemMessages(bridge);
    ASSERT_EQ(msgs.size(), 1u);
    EXPECT_NE(msgs[0].find("Moving item"), std::string::npos) << msgs[0];
    auto intents = bridge.drainIntents();
    ASSERT_NE(FindMoveItemIntent(intents), nullptr);
    auto* inv = eq.GetInventoryManager();
    EXPECT_EQ(inv->getItem(22)->itemId, 7131u);
    EXPECT_EQ(inv->getItem(23)->itemId, 7130u);
}

TEST_F(EqCommandsEquipTest, CursorSwap_IntoOccupiedGeneralSlot_StillMoves) {
    // The T230 fixture shape (`/equip 30 22` with 22 occupied). Server rule
    // for a cursor-source swap: the displaced item goes to the cursor, and
    // IsSlotAllowed(slotCursor) is always true -- the server APPLIES this
    // swap (T230's own run-3/run-4 zone-in inventories prove it: the item
    // swapped in run 3 is in general slot 22 at run 4's zone-in). The
    // client must keep applying it too.
    auto* inv = eq.GetInventoryManager();
    SeedNamedItem(eq, 23, 7140, "Wooden Fishing Tackle", /*slotsBitmask=*/0);
    ASSERT_TRUE(inv->pickupItem(23));  // cursor <- tackle (from a general slot)
    (void)bridge.drainIntents();
    (void)DrainSystemMessages(bridge);
    SeedNamedItem(eq, 22, 7141, "Scaled Curskin", /*slotsBitmask=*/0);
    eq.RequestEquip("30 22");

    auto msgs = DrainSystemMessages(bridge);
    ASSERT_EQ(msgs.size(), 1u);
    EXPECT_NE(msgs[0].find("Moving item"), std::string::npos) << msgs[0];
    auto intents = bridge.drainIntents();
    const auto* mv = FindMoveItemIntent(intents);
    ASSERT_NE(mv, nullptr);
    EXPECT_EQ(mv->fromSlot, CURSOR_SLOT);
    EXPECT_EQ(mv->toSlot, 22);
    ASSERT_NE(inv->getItem(22), nullptr);
    EXPECT_EQ(inv->getItem(22)->itemId, 7140u);
    ASSERT_TRUE(inv->hasCursorItem());
    EXPECT_EQ(inv->getCursorItem()->itemId, 7141u);
}

// ---------------------------------------------------------------------------
// T249 -- placeItem()'s swap branch used to validate the DISPLACED item
// against cursorSourceSlot_ (where the cursor item was picked up FROM). The
// server never sees that slot: eqemu validates the displaced item against
// the move's SOURCE slot (common/inventory_profile.cpp:359-365), and for a
// cursor placement the source is the cursor (30), which IsSlotAllowed()
// always permits. So a cursor item picked up from an equipment slot and
// dropped onto an occupied general slot is a swap the server APPLIES; the
// client must apply it too. The forward check (cursor item fits the
// destination) is the one the server does enforce and stays.
// ---------------------------------------------------------------------------

TEST_F(EqCommandsEquipTest, CursorSwap_CursorItemFromEquipSlot_DisplacedNotCheckedAgainstSource_StillMoves) {
    // Weapon picked up FROM Primary, dropped onto general 22 holding a
    // non-equippable item (slots bitmask 0). Old client: refused because the
    // Milk "cannot go in Primary" -- a slot the server never considers.
    auto* inv = eq.GetInventoryManager();
    SeedNamedItem(eq, SLOT_PRIMARY, 7150, "Rusty Short Sword", EQUIPSLOT_PRIMARY);
    ASSERT_TRUE(inv->pickupItem(SLOT_PRIMARY));  // cursor <- sword (from an EQUIPMENT slot)
    EXPECT_EQ(inv->getCursorSourceSlot(), SLOT_PRIMARY);
    (void)bridge.drainIntents();
    (void)DrainSystemMessages(bridge);
    SeedNamedItem(eq, 22, 7151, "Skin of Milk", /*slotsBitmask=*/0);
    eq.RequestEquip("30 22");

    auto msgs = DrainSystemMessages(bridge);
    ASSERT_EQ(msgs.size(), 1u);
    EXPECT_NE(msgs[0].find("Moving item"), std::string::npos) << msgs[0];
    auto intents = bridge.drainIntents();
    const auto* mv = FindMoveItemIntent(intents);
    ASSERT_NE(mv, nullptr);
    EXPECT_EQ(mv->fromSlot, CURSOR_SLOT);
    EXPECT_EQ(mv->toSlot, 22);
    ASSERT_NE(inv->getItem(22), nullptr);
    EXPECT_EQ(inv->getItem(22)->itemId, 7150u);
    ASSERT_TRUE(inv->hasCursorItem());
    EXPECT_EQ(inv->getCursorItem()->itemId, 7151u);
    EXPECT_EQ(inv->getItem(SLOT_PRIMARY), nullptr);  // Primary stays empty; nothing teleported back
}

TEST_F(EqCommandsEquipTest, CursorSwap_DirectPlaceItem_FromEquipSlot_OntoOccupiedGeneral_Swaps) {
    // Same shape below the /equip layer: InventoryManager::placeItem() is
    // what the drag-and-drop UI gesture calls.
    auto* inv = eq.GetInventoryManager();
    SeedNamedItem(eq, SLOT_PRIMARY, 7160, "Rusty Short Sword", EQUIPSLOT_PRIMARY);
    ASSERT_TRUE(inv->pickupItem(SLOT_PRIMARY));
    (void)bridge.drainIntents();
    SeedNamedItem(eq, 23, 7161, "Skin of Milk", /*slotsBitmask=*/0);

    EXPECT_TRUE(inv->placeItem(23));

    auto intents = bridge.drainIntents();
    const auto* mv = FindMoveItemIntent(intents);
    ASSERT_NE(mv, nullptr);
    EXPECT_EQ(mv->fromSlot, CURSOR_SLOT);
    EXPECT_EQ(mv->toSlot, 23);
    ASSERT_NE(inv->getItem(23), nullptr);
    EXPECT_EQ(inv->getItem(23)->itemId, 7160u);
    ASSERT_TRUE(inv->hasCursorItem());
    EXPECT_EQ(inv->getCursorItem()->itemId, 7161u);
}

TEST_F(EqCommandsEquipTest, CursorSwap_CursorItemDoesNotFitDestination_StillRefused_NoIntent_NoMutation) {
    // The forward rule the server DOES enforce (source item IsSlotAllowed
    // (destination), inventory_profile.cpp:325): Milk on the cursor cannot
    // go into Primary even though a sword sits there to be displaced.
    auto* inv = eq.GetInventoryManager();
    SeedNamedItem(eq, 22, 7170, "Skin of Milk", /*slotsBitmask=*/0);
    ASSERT_TRUE(inv->pickupItem(22));
    (void)bridge.drainIntents();
    (void)DrainSystemMessages(bridge);
    SeedNamedItem(eq, SLOT_PRIMARY, 7171, "Rusty Short Sword", EQUIPSLOT_PRIMARY);

    // placeItem() itself refuses (the drag/drop path)...
    EXPECT_FALSE(inv->placeItem(SLOT_PRIMARY));
    EXPECT_TRUE(bridge.drainIntents().empty());
    // ...and so does /equip's pre-flight (the command path).
    eq.RequestEquip("30 13");
    auto msgs = DrainSystemMessages(bridge);
    ASSERT_EQ(msgs.size(), 1u);
    EXPECT_NE(msgs[0].find("Skin of Milk"), std::string::npos) << msgs[0];
    EXPECT_EQ(msgs[0].find("Moving item"), std::string::npos) << msgs[0];
    EXPECT_TRUE(bridge.drainIntents().empty());
    ASSERT_NE(inv->getItem(SLOT_PRIMARY), nullptr);
    EXPECT_EQ(inv->getItem(SLOT_PRIMARY)->itemId, 7171u);
    ASSERT_TRUE(inv->hasCursorItem());
    EXPECT_EQ(inv->getCursorItem()->itemId, 7170u);
}

// ---------------------------------------------------------------------------
// T250 -- after placeItem()'s swap branch, the DISPLACED item becomes the
// cursor front, so cursorSourceSlot_ must name the slot it came from (the
// destination), not the ORIGINAL pickup slot. The only consumer that cares
// is relocateBagContents(cursorSourceSlot_, target): with the stale value it
// would move the wrong bag's contents when the displaced item is a bag.
// ---------------------------------------------------------------------------

void SeedBag(EverQuest& eq, int16_t slot, uint32_t itemId, const std::string& name, uint8_t bagSlots) {
    ItemInstance bag;
    bag.itemId = itemId;
    bag.name = name;
    bag.slots = 0;
    bag.bagSlots = bagSlots;
    eq.GetInventoryManager()->setItem(slot, bag);
}

TEST_F(EqCommandsEquipTest, CursorSwap_SourceSlotTracksDisplacedItem) {
    auto* inv = eq.GetInventoryManager();
    SeedNamedItem(eq, 22, 7200, "Rusty Short Sword", EQUIPSLOT_PRIMARY);
    SeedNamedItem(eq, 23, 7201, "Skin of Milk", /*slotsBitmask=*/0);
    ASSERT_TRUE(inv->pickupItem(22));
    EXPECT_EQ(inv->getCursorSourceSlot(), 22);

    ASSERT_TRUE(inv->placeItem(23));  // sword -> 23, Milk displaced onto cursor
    ASSERT_TRUE(inv->hasCursorItem());
    EXPECT_EQ(inv->getCursorItem()->itemId, 7201u);
    // The Milk came from 23 -- that is where the cursor front is FROM now.
    EXPECT_EQ(inv->getCursorSourceSlot(), 23);
}

TEST_F(EqCommandsEquipTest, CursorSwap_DisplacedBag_ContentsFollowTheBagOnNextPlace) {
    // Bag A (empty) picked up from general 22 and dropped on general 23,
    // which holds bag B containing Bread. B is now on the cursor; placing B
    // into empty general 24 must carry B's contents (23's bag range) to
    // 24's bag range. With the stale source (22), relocateBagContents(22,
    // 24) moved nothing and the Bread stayed under bag A.
    auto* inv = eq.GetInventoryManager();
    SeedBag(eq, 22, 7210, "Small Bag A", 4);
    SeedBag(eq, 23, 7211, "Small Bag B", 4);
    SeedNamedItem(eq, calcBagSlotId(23, 0), 7212, "Loaf of Bread", /*slotsBitmask=*/0);

    ASSERT_TRUE(inv->pickupItem(22));   // cursor <- A (source 22)
    ASSERT_TRUE(inv->placeItem(23));    // A -> 23, cursor <- B (source 23)
    (void)bridge.drainIntents();
    ASSERT_TRUE(inv->hasCursorItem());
    EXPECT_EQ(inv->getCursorItem()->itemId, 7211u);
    EXPECT_EQ(inv->getCursorSourceSlot(), 23);
    ASSERT_NE(inv->getItem(23), nullptr);
    EXPECT_EQ(inv->getItem(23)->itemId, 7210u);

    ASSERT_TRUE(inv->placeItem(24));    // B -> 24; B's contents must follow
    EXPECT_FALSE(inv->hasCursorItem());
    ASSERT_NE(inv->getItem(24), nullptr);
    EXPECT_EQ(inv->getItem(24)->itemId, 7211u);
    ASSERT_NE(inv->getItem(calcBagSlotId(24, 0)), nullptr);
    EXPECT_EQ(inv->getItem(calcBagSlotId(24, 0))->itemId, 7212u);
    EXPECT_EQ(inv->getItem(calcBagSlotId(23, 0)), nullptr);
    EXPECT_EQ(inv->getItem(calcBagSlotId(22, 0)), nullptr);

    auto intents = bridge.drainIntents();
    const auto* mv = FindMoveItemIntent(intents);
    ASSERT_NE(mv, nullptr);
    EXPECT_EQ(mv->fromSlot, CURSOR_SLOT);
    EXPECT_EQ(mv->toSlot, 24);
}

// ---------------------------------------------------------------------------
// T252 -- bag-on-bag swaps. eqemu's InventoryProfile::SwapItem
// (common/inventory_profile.cpp:392-393) swaps the two ItemInstances with
// _PutItem, and an ItemInstance CARRIES its bag contents, so both bags'
// contents follow their bag; there is no bag-on-bag refusal anywhere in
// SwapItem or Client::SwapItem. The client keeps contents FLAT by parent-
// slot range, so it must move both ranges without either overwriting the
// other -- in every gesture that moves a bag: direct /equip swap, direct
// move to an empty slot, and the cursor pickup/place pair.
// ---------------------------------------------------------------------------

TEST_F(EqCommandsEquipTest, DirectSwap_BagOnBag_BothContentsFollowTheirBags) {
    auto* inv = eq.GetInventoryManager();
    SeedBag(eq, 22, 7210, "Small Bag A", 4);
    SeedBag(eq, 23, 7211, "Small Bag B", 4);
    SeedNamedItem(eq, calcBagSlotId(22, 0), 7301, "Ax", 0);
    SeedNamedItem(eq, calcBagSlotId(22, 1), 7302, "Ay", 0);
    SeedNamedItem(eq, calcBagSlotId(23, 0), 7401, "Bp", 0);
    SeedNamedItem(eq, calcBagSlotId(23, 1), 7402, "Bq", 0);

    eq.RequestEquip("22 23");

    ASSERT_NE(inv->getItem(23), nullptr);
    EXPECT_EQ(inv->getItem(23)->itemId, 7210u);  // A now in 23 ...
    ASSERT_NE(inv->getItem(calcBagSlotId(23, 0)), nullptr);
    ASSERT_NE(inv->getItem(calcBagSlotId(23, 1)), nullptr);
    EXPECT_EQ(inv->getItem(calcBagSlotId(23, 0))->itemId, 7301u);  // ... with x
    EXPECT_EQ(inv->getItem(calcBagSlotId(23, 1))->itemId, 7302u);  // ... and y
    ASSERT_NE(inv->getItem(22), nullptr);
    EXPECT_EQ(inv->getItem(22)->itemId, 7211u);  // B now in 22 ...
    ASSERT_NE(inv->getItem(calcBagSlotId(22, 0)), nullptr);
    ASSERT_NE(inv->getItem(calcBagSlotId(22, 1)), nullptr);
    EXPECT_EQ(inv->getItem(calcBagSlotId(22, 0))->itemId, 7401u);  // ... with p
    EXPECT_EQ(inv->getItem(calcBagSlotId(22, 1))->itemId, 7402u);  // ... and q

    auto intents = bridge.drainIntents();
    const auto* mv = FindMoveItemIntent(intents);
    ASSERT_NE(mv, nullptr);
    EXPECT_EQ(mv->fromSlot, 22);
    EXPECT_EQ(mv->toSlot, 23);
}

TEST_F(EqCommandsEquipTest, DirectMove_BagToEmptyGeneral_ContentsFollow) {
    auto* inv = eq.GetInventoryManager();
    SeedBag(eq, 22, 7210, "Small Bag A", 4);
    SeedNamedItem(eq, calcBagSlotId(22, 0), 7301, "Ax", 0);

    eq.RequestEquip("22 24");

    EXPECT_EQ(inv->getItem(22), nullptr);
    EXPECT_EQ(inv->getItem(calcBagSlotId(22, 0)), nullptr);
    ASSERT_NE(inv->getItem(24), nullptr);
    EXPECT_EQ(inv->getItem(24)->itemId, 7210u);
    ASSERT_NE(inv->getItem(calcBagSlotId(24, 0)), nullptr);
    EXPECT_EQ(inv->getItem(calcBagSlotId(24, 0))->itemId, 7301u);
}

TEST_F(EqCommandsEquipTest, CursorSwap_BagOnBag_BothContentsFollowTheirBags) {
    // The T250 scenario with BOTH bags non-empty. Server-side a bag on the
    // cursor keeps its contents (cursor bag range 331-340 on the wire,
    // CalcSlotId(slotCursor, idx)); the client mirrors that so the two
    // contents ranges never collide mid-gesture.
    auto* inv = eq.GetInventoryManager();
    SeedBag(eq, 22, 7210, "Small Bag A", 4);
    SeedBag(eq, 23, 7211, "Small Bag B", 4);
    SeedNamedItem(eq, calcBagSlotId(22, 0), 7301, "Ax", 0);
    SeedNamedItem(eq, calcBagSlotId(22, 1), 7302, "Ay", 0);
    SeedNamedItem(eq, calcBagSlotId(23, 0), 7401, "Bp", 0);
    SeedNamedItem(eq, calcBagSlotId(23, 1), 7402, "Bq", 0);

    // 1. cursor <- A: A's contents ride along in the cursor bag range.
    ASSERT_TRUE(inv->pickupItem(22));
    ASSERT_NE(inv->getItem(calcCursorBagSlotId(0)), nullptr);
    ASSERT_NE(inv->getItem(calcCursorBagSlotId(1)), nullptr);
    EXPECT_EQ(inv->getItem(calcCursorBagSlotId(0))->itemId, 7301u);
    EXPECT_EQ(inv->getItem(calcCursorBagSlotId(1))->itemId, 7302u);
    EXPECT_EQ(inv->getItem(calcBagSlotId(22, 0)), nullptr);
    EXPECT_EQ(inv->getItem(calcBagSlotId(22, 1)), nullptr);

    // 2. A -> 23, B displaced onto the cursor with ITS contents.
    ASSERT_TRUE(inv->placeItem(23));
    ASSERT_TRUE(inv->hasCursorItem());
    EXPECT_EQ(inv->getCursorItem()->itemId, 7211u);
    ASSERT_NE(inv->getItem(23), nullptr);
    EXPECT_EQ(inv->getItem(23)->itemId, 7210u);
    ASSERT_NE(inv->getItem(calcBagSlotId(23, 0)), nullptr);
    ASSERT_NE(inv->getItem(calcBagSlotId(23, 1)), nullptr);
    EXPECT_EQ(inv->getItem(calcBagSlotId(23, 0))->itemId, 7301u);
    EXPECT_EQ(inv->getItem(calcBagSlotId(23, 1))->itemId, 7302u);
    ASSERT_NE(inv->getItem(calcCursorBagSlotId(0)), nullptr);
    ASSERT_NE(inv->getItem(calcCursorBagSlotId(1)), nullptr);
    EXPECT_EQ(inv->getItem(calcCursorBagSlotId(0))->itemId, 7401u);
    EXPECT_EQ(inv->getItem(calcCursorBagSlotId(1))->itemId, 7402u);

    // 3. B -> 22 (A's old slot): B's contents land in 22's range, A's stay in 23's.
    ASSERT_TRUE(inv->placeItem(22));
    EXPECT_FALSE(inv->hasCursorItem());
    ASSERT_NE(inv->getItem(22), nullptr);
    EXPECT_EQ(inv->getItem(22)->itemId, 7211u);
    ASSERT_NE(inv->getItem(calcBagSlotId(22, 0)), nullptr);
    ASSERT_NE(inv->getItem(calcBagSlotId(22, 1)), nullptr);
    EXPECT_EQ(inv->getItem(calcBagSlotId(22, 0))->itemId, 7401u);
    EXPECT_EQ(inv->getItem(calcBagSlotId(22, 1))->itemId, 7402u);
    ASSERT_NE(inv->getItem(calcBagSlotId(23, 0)), nullptr);
    ASSERT_NE(inv->getItem(calcBagSlotId(23, 1)), nullptr);
    EXPECT_EQ(inv->getItem(calcBagSlotId(23, 0))->itemId, 7301u);
    EXPECT_EQ(inv->getItem(calcBagSlotId(23, 1))->itemId, 7302u);
    EXPECT_EQ(inv->getItem(calcCursorBagSlotId(0)), nullptr);
    EXPECT_EQ(inv->getItem(calcCursorBagSlotId(1)), nullptr);

    // Wire: exactly the three moves the server needs, in order.
    auto intents = bridge.drainIntents();
    std::vector<std::pair<int16_t, int16_t>> moves;
    for (const auto& intent : intents) {
        if (std::holds_alternative<eqt::events::MoveItemIntent>(intent)) {
            const auto& m = std::get<eqt::events::MoveItemIntent>(intent);
            moves.emplace_back(m.fromSlot, m.toSlot);
        }
    }
    ASSERT_EQ(moves.size(), 3u);
    EXPECT_EQ(moves[0], std::make_pair(int16_t(22), int16_t(CURSOR_SLOT)));
    EXPECT_EQ(moves[1], std::make_pair(int16_t(CURSOR_SLOT), int16_t(23)));
    EXPECT_EQ(moves[2], std::make_pair(int16_t(CURSOR_SLOT), int16_t(22)));
}

// ---------------------------------------------------------------------------
// T255 -- bank bag moves. eqemu's SwapItem is the same wholesale ItemInstance
// swap for a bank parent as for a general one; the wire numbering for bank
// bag contents is Titanium 2031 + (bank-2000)*10 + idx (titanium_limits.h,
// CalcSlotId), server-side 6210 + (bank-2000)*200 + idx (emu_constants.h:
// BANK_BAGS_BEGIN = CURSOR_BAG_END + 1 with RoF2 SLOT_COUNT 200). The client
// keeps contents flat by parent range, so a bag moving general<->bank must
// carry its range across exactly as T252 made general<->general do.
// ---------------------------------------------------------------------------

TEST_F(EqCommandsEquipTest, DirectMove_BagToEmptyBank_ContentsFollow) {
    auto* inv = eq.GetInventoryManager();
    SeedBag(eq, 22, 7210, "Small Bag A", 4);
    SeedNamedItem(eq, calcBagSlotId(22, 0), 7301, "Ax", 0);
    SeedNamedItem(eq, calcBagSlotId(22, 1), 7302, "Ay", 0);

    eq.RequestEquip("22 2000");

    EXPECT_EQ(inv->getItem(22), nullptr);
    EXPECT_EQ(inv->getItem(calcBagSlotId(22, 0)), nullptr);
    EXPECT_EQ(inv->getItem(calcBagSlotId(22, 1)), nullptr);
    ASSERT_NE(inv->getItem(2000), nullptr);
    EXPECT_EQ(inv->getItem(2000)->itemId, 7210u);
    ASSERT_NE(inv->getItem(calcBankBagSlotId(2000, 0)), nullptr);
    ASSERT_NE(inv->getItem(calcBankBagSlotId(2000, 1)), nullptr);
    EXPECT_EQ(inv->getItem(calcBankBagSlotId(2000, 0))->itemId, 7301u);
    EXPECT_EQ(inv->getItem(calcBankBagSlotId(2000, 1))->itemId, 7302u);
    EXPECT_EQ(calcBankBagSlotId(2000, 0), 2031);

    auto intents = bridge.drainIntents();
    const auto* mv = FindMoveItemIntent(intents);
    ASSERT_NE(mv, nullptr);
    EXPECT_EQ(mv->fromSlot, 22);
    EXPECT_EQ(mv->toSlot, 2000);
}

TEST_F(EqCommandsEquipTest, DirectMove_BagFromBankToGeneral_ContentsFollow) {
    auto* inv = eq.GetInventoryManager();
    SeedBag(eq, 2001, 7210, "Small Bag A", 4);
    SeedNamedItem(eq, calcBankBagSlotId(2001, 3), 7301, "Ax", 0);

    eq.RequestEquip("2001 24");

    EXPECT_EQ(inv->getItem(2001), nullptr);
    EXPECT_EQ(inv->getItem(calcBankBagSlotId(2001, 3)), nullptr);
    ASSERT_NE(inv->getItem(24), nullptr);
    ASSERT_NE(inv->getItem(calcBagSlotId(24, 3)), nullptr);
    EXPECT_EQ(inv->getItem(calcBagSlotId(24, 3))->itemId, 7301u);
}

TEST_F(EqCommandsEquipTest, DirectSwap_GeneralBagOnBankBag_BothContentsFollowTheirBags) {
    auto* inv = eq.GetInventoryManager();
    SeedBag(eq, 22, 7210, "Small Bag A", 4);
    SeedBag(eq, 2000, 7211, "Small Bag B", 4);
    SeedNamedItem(eq, calcBagSlotId(22, 0), 7301, "Ax", 0);
    SeedNamedItem(eq, calcBankBagSlotId(2000, 0), 7401, "Bp", 0);

    eq.RequestEquip("22 2000");

    ASSERT_NE(inv->getItem(2000), nullptr);
    EXPECT_EQ(inv->getItem(2000)->itemId, 7210u);
    ASSERT_NE(inv->getItem(calcBankBagSlotId(2000, 0)), nullptr);
    EXPECT_EQ(inv->getItem(calcBankBagSlotId(2000, 0))->itemId, 7301u);
    ASSERT_NE(inv->getItem(22), nullptr);
    EXPECT_EQ(inv->getItem(22)->itemId, 7211u);
    ASSERT_NE(inv->getItem(calcBagSlotId(22, 0)), nullptr);
    EXPECT_EQ(inv->getItem(calcBagSlotId(22, 0))->itemId, 7401u);
}

TEST_F(EqCommandsEquipTest, CursorMove_BagGeneralToBankAndBack_ContentsRideTheCursor) {
    auto* inv = eq.GetInventoryManager();
    SeedBag(eq, 22, 7210, "Small Bag A", 4);
    SeedNamedItem(eq, calcBagSlotId(22, 2), 7301, "Ax", 0);

    ASSERT_TRUE(inv->pickupItem(22));
    ASSERT_NE(inv->getItem(calcCursorBagSlotId(2)), nullptr);
    ASSERT_TRUE(inv->placeItem(2005));
    EXPECT_FALSE(inv->hasCursorItem());
    EXPECT_EQ(inv->getItem(calcCursorBagSlotId(2)), nullptr);
    ASSERT_NE(inv->getItem(2005), nullptr);
    ASSERT_NE(inv->getItem(calcBankBagSlotId(2005, 2)), nullptr);
    EXPECT_EQ(inv->getItem(calcBankBagSlotId(2005, 2))->itemId, 7301u);

    ASSERT_TRUE(inv->pickupItem(2005));
    EXPECT_EQ(inv->getItem(calcBankBagSlotId(2005, 2)), nullptr);
    ASSERT_NE(inv->getItem(calcCursorBagSlotId(2)), nullptr);
    ASSERT_TRUE(inv->placeItem(23));
    ASSERT_NE(inv->getItem(calcBagSlotId(23, 2)), nullptr);
    EXPECT_EQ(inv->getItem(calcBagSlotId(23, 2))->itemId, 7301u);
    EXPECT_EQ(inv->getItem(calcCursorBagSlotId(2)), nullptr);
}

// ---------------------------------------------------------------------------
// T255 item 2 -- more than one bag queued on the cursor. The server keeps
// every queued bag's contents inside that bag's own ItemInstance and
// serialises each cursor item (with its sub-items) at slot 33 in
// CharInventory, so two queued bags both address the cursor bag range
// 331-340 on the wire. Only the FRONT bag's contents may live in that
// range client-side; the rest are held per queued bag and promoted into
// the range when that bag becomes the front.
// ---------------------------------------------------------------------------

// Titanium serialized item (T43's builder shape) with the container fields
// set so parseStaticData() yields a bag, plus optional depth-1 sub-items.
std::string BuildBagString(int16_t slot, uint32_t itemId, const std::string& name, uint8_t bagSlots,
                           const std::vector<std::pair<int16_t, std::string>>& subItems) {
    std::string instance = "0|0|" + std::to_string(slot) + "|0|1|0|0|0|0|0|0";
    std::vector<std::string> fields(135, "");
    fields[0] = "1";
    fields[1] = name;
    fields[2] = name;
    fields[3] = "IT63";
    fields[4] = std::to_string(itemId);
    fields[5] = "10";
    fields[6] = "1";
    fields[7] = "1";
    fields[8] = "0";
    fields[9] = "0";
    fields[10] = "0";
    fields[11] = "0";
    fields[96] = "0";
    fields[97] = std::to_string(bagSlots);
    fields[98] = "4";
    std::string staticData;
    for (size_t i = 0; i < fields.size(); ++i) {
        if (i) staticData += "|";
        staticData += fields[i];
    }
    std::string out = instance + "\"" + staticData + "\"";
    // Sub-item block: |sub0|sub1|...|sub9, each occupied index wrapped as
    // "instance|\"static\"" (titanium.cpp SerializeItem, depth 1).
    for (int16_t idx = 0; idx < bagSlots; ++idx) {
        out += "|";
        for (const auto& [subIdx, subStr] : subItems) {
            if (subIdx != idx) continue;
            std::string escaped;
            for (char c : subStr) {
                if (c == '"') escaped += "\\\"";
                else escaped += c;
            }
            out += "\"" + escaped + "\"";
        }
    }
    return out;
}

std::string BuildPlainItemString(int16_t slot, uint32_t itemId, const std::string& name) {
    std::string instance = "0|0|" + std::to_string(slot) + "|0|1|0|0|0|0|0|0";
    std::vector<std::string> fields(135, "");
    fields[0] = "0";
    fields[1] = name;
    fields[2] = name;
    fields[3] = "IT63";
    fields[4] = std::to_string(itemId);
    fields[5] = "10";
    fields[6] = "1";
    fields[7] = "1";
    std::string staticData;
    for (size_t i = 0; i < fields.size(); ++i) {
        if (i) staticData += "|";
        staticData += fields[i];
    }
    return instance + "\"" + staticData + "\"";
}

EQ::Net::DynamicPacket CharInventoryPacket(const std::vector<std::string>& items) {
    std::string body;
    for (const auto& s : items) {
        body += s;
        body.push_back('\0');
    }
    EQ::Net::DynamicPacket p;
    p.Resize(body.size());
    p.PutData(0, const_cast<char*>(body.data()), body.size());
    return p;
}

// T282: processItemPacket() is only ever handed a packet that still carries
// its 2-byte OPCODE -- ZoneComponent::DispatchZonePacket() passes the dispatched
// packet straight through, and the loot path forwards the same one. This
// helper used to omit it, which hid a 2-byte misalignment in the parse
// (instance field 0, the stack count, was the only casualty).
constexpr uint16_t kOpItemPacket = 0x3397;  // HC_OP_ItemPacket (client/eq.h)

EQ::Net::DynamicPacket ItemPacketPacket(const std::string& item,
                                        uint32_t packetType = EQT::ITEM_PACKET_INVENTORY) {
    EQ::Net::DynamicPacket p;
    p.Resize(2 + 4 + item.size() + 1);
    p.PutUInt16(0, kOpItemPacket);
    p.PutUInt32(2, packetType);
    p.PutData(2 + 4, const_cast<char*>(item.data()), item.size());
    p.PutUInt8(2 + 4 + item.size(), 0);
    return p;
}

TEST_F(EqCommandsEquipTest, CharInventory_SingleCursorBag_ContentsLandInCursorRange) {
    // Found while writing the two-bag case: the server serialises a cursor
    // item at ITS cursor slot (33), and parseSubItems() fed that raw slot to
    // calcContainerSlotId(), which knows only the client cursor (30) -- so
    // every cursor bag's contents collapsed onto SLOT_INVALID and were lost
    // at zone-in, one bag or many. Pin the one-bag case on its own.
    auto* inv = eq.GetInventoryManager();
    auto pkt = CharInventoryPacket({
        BuildBagString(SERVER_CURSOR_SLOT, 7210, "Bag A", 4, {{2, BuildPlainItemString(2, 7301, "Ax")}}),
    });
    inv->processCharInventory(pkt);

    ASSERT_EQ(inv->getCursorQueueSize(), 1u);
    ASSERT_NE(inv->getItem(calcCursorBagSlotId(2)), nullptr);
    EXPECT_EQ(inv->getItem(calcCursorBagSlotId(2))->itemId, 7301u);
    EXPECT_EQ(inv->getItem(SLOT_INVALID), nullptr);
}

TEST_F(EqCommandsEquipTest, CharInventory_TwoQueuedCursorBags_EachKeepsItsOwnContents) {
    auto* inv = eq.GetInventoryManager();
    // Server cursor queue: bag A{Ax @0} in front, bag B{Bp @0, Bq @1} behind.
    auto pkt = CharInventoryPacket({
        BuildBagString(SERVER_CURSOR_SLOT, 7210, "Bag A", 4, {{0, BuildPlainItemString(0, 7301, "Ax")}}),
        BuildBagString(SERVER_CURSOR_SLOT, 7211, "Bag B", 4, {{0, BuildPlainItemString(0, 7401, "Bp")},
                                                             {1, BuildPlainItemString(1, 7402, "Bq")}}),
    });
    inv->processCharInventory(pkt);

    ASSERT_EQ(inv->getCursorQueueSize(), 2u);
    ASSERT_NE(inv->getCursorItem(), nullptr);
    EXPECT_EQ(inv->getCursorItem()->itemId, 7210u);
    // Front bag's contents are the cursor range; B's must NOT have clobbered them.
    ASSERT_NE(inv->getItem(calcCursorBagSlotId(0)), nullptr);
    EXPECT_EQ(inv->getItem(calcCursorBagSlotId(0))->itemId, 7301u);
    EXPECT_EQ(inv->getItem(calcCursorBagSlotId(1)), nullptr);

    // Place A into 22: Ax goes with it; B becomes the front WITH its contents.
    ASSERT_TRUE(inv->placeItem(22));
    ASSERT_NE(inv->getItem(calcBagSlotId(22, 0)), nullptr);
    EXPECT_EQ(inv->getItem(calcBagSlotId(22, 0))->itemId, 7301u);
    ASSERT_EQ(inv->getCursorQueueSize(), 1u);
    EXPECT_EQ(inv->getCursorItem()->itemId, 7211u);
    ASSERT_NE(inv->getItem(calcCursorBagSlotId(0)), nullptr);
    ASSERT_NE(inv->getItem(calcCursorBagSlotId(1)), nullptr);
    EXPECT_EQ(inv->getItem(calcCursorBagSlotId(0))->itemId, 7401u);
    EXPECT_EQ(inv->getItem(calcCursorBagSlotId(1))->itemId, 7402u);

    // Place B into 23: Bp/Bq follow; nothing left in the cursor range.
    ASSERT_TRUE(inv->placeItem(23));
    EXPECT_FALSE(inv->hasCursorItem());
    ASSERT_NE(inv->getItem(calcBagSlotId(23, 0)), nullptr);
    ASSERT_NE(inv->getItem(calcBagSlotId(23, 1)), nullptr);
    EXPECT_EQ(inv->getItem(calcBagSlotId(23, 0))->itemId, 7401u);
    EXPECT_EQ(inv->getItem(calcBagSlotId(23, 1))->itemId, 7402u);
    EXPECT_EQ(inv->getItem(calcCursorBagSlotId(0)), nullptr);
    EXPECT_EQ(inv->getItem(calcCursorBagSlotId(1)), nullptr);
    // A's content stayed under A.
    ASSERT_NE(inv->getItem(calcBagSlotId(22, 0)), nullptr);
    EXPECT_EQ(inv->getItem(calcBagSlotId(22, 0))->itemId, 7301u);
}

TEST_F(EqCommandsEquipTest, ItemPacket_BagQueuedBehindCursorFront_KeepsContentsUntilPromoted) {
    auto* inv = eq.GetInventoryManager();
    SeedNamedItem(eq, CURSOR_SLOT, 7001, "Rusty Mace", EQUIPSLOT_PRIMARY);  // front, from server
    ASSERT_EQ(inv->getCursorQueueSize(), 1u);

    // A bag with one item arrives on the cursor behind the mace (OP_ItemPacket).
    auto pkt = ItemPacketPacket(
        BuildBagString(SERVER_CURSOR_SLOT, 7210, "Bag A", 4, {{1, BuildPlainItemString(1, 7301, "Ax")}}));
    EXPECT_TRUE(isCursorSlot(inv->processItemPacket(pkt)));  // returns the wire slot (33)
    ASSERT_EQ(inv->getCursorQueueSize(), 2u);
    EXPECT_EQ(inv->getCursorItem()->itemId, 7001u);
    // The mace is not a bag; the cursor range stays EMPTY while it is the front.
    EXPECT_EQ(inv->getItem(calcCursorBagSlotId(1)), nullptr);

    ASSERT_TRUE(inv->placeItem(22));  // mace -> 22; bag promoted with Ax
    EXPECT_EQ(inv->getCursorItem()->itemId, 7210u);
    ASSERT_NE(inv->getItem(calcCursorBagSlotId(1)), nullptr);
    EXPECT_EQ(inv->getItem(calcCursorBagSlotId(1))->itemId, 7301u);

    ASSERT_TRUE(inv->placeItem(2000));  // bag -> bank 2000 with Ax
    ASSERT_NE(inv->getItem(calcBankBagSlotId(2000, 1)), nullptr);
    EXPECT_EQ(inv->getItem(calcBankBagSlotId(2000, 1))->itemId, 7301u);
    EXPECT_EQ(inv->getItem(calcCursorBagSlotId(1)), nullptr);
}

TEST_F(EqCommandsEquipTest, ItemPacket_BankBagWithContents_ContentsLandInBankRange) {
    // Found in the T255 live leg (run 2): at zone-in eqemu delivers BANK
    // items as individual OP_ItemPackets, each bag serialised WITH its
    // contents -- not via CharInventory. processItemPacket() never parsed
    // sub-items, so a bank bag always loaded empty client-side and a later
    // bank->general move had nothing to relocate (the server still moved
    // them). An ItemPacket for a bag is authoritative for that bag's whole
    // contents: any stale range under that parent is replaced, never merged.
    auto* inv = eq.GetInventoryManager();
    SeedNamedItem(eq, calcBankBagSlotId(2000, 0), 7999, "Stale", 0);  // must be replaced, not kept
    auto pkt = ItemPacketPacket(
        BuildBagString(2000, 7210, "Large Bag", 6, {{0, BuildPlainItemString(0, 7301, "Rusty Short Sword")},
                                                    {1, BuildPlainItemString(1, 7302, "Rusty Long Sword")}}));
    EXPECT_EQ(inv->processItemPacket(pkt), 2000);
    ASSERT_NE(inv->getItem(2000), nullptr);
    ASSERT_NE(inv->getItem(calcBankBagSlotId(2000, 0)), nullptr);
    ASSERT_NE(inv->getItem(calcBankBagSlotId(2000, 1)), nullptr);
    EXPECT_EQ(inv->getItem(calcBankBagSlotId(2000, 0))->itemId, 7301u);
    EXPECT_EQ(inv->getItem(calcBankBagSlotId(2000, 1))->itemId, 7302u);

    eq.RequestEquip("2000 22");
    ASSERT_NE(inv->getItem(calcBagSlotId(22, 0)), nullptr);
    ASSERT_NE(inv->getItem(calcBagSlotId(22, 1)), nullptr);
    EXPECT_EQ(inv->getItem(calcBagSlotId(22, 0))->itemId, 7301u);
    EXPECT_EQ(inv->getItem(calcBagSlotId(22, 1))->itemId, 7302u);
    EXPECT_EQ(inv->getItem(calcBankBagSlotId(2000, 0)), nullptr);
}

TEST_F(EqCommandsEquipTest, CursorSwap_DisplacedBagOntoOccupiedQueue_QueuedBagContentsSurvive) {
    // Queue: A{Ax} front, B{Bp} behind (CharInventory). A dropped onto bag
    // C{Cz} at 22: C is displaced to the front with Cz; B still waits with
    // Bp; when C is placed and B promoted, Bp is intact.
    auto* inv = eq.GetInventoryManager();
    auto pkt = CharInventoryPacket({
        BuildBagString(SERVER_CURSOR_SLOT, 7210, "Bag A", 4, {{0, BuildPlainItemString(0, 7301, "Ax")}}),
        BuildBagString(SERVER_CURSOR_SLOT, 7211, "Bag B", 4, {{0, BuildPlainItemString(0, 7401, "Bp")}}),
    });
    inv->processCharInventory(pkt);
    SeedBag(eq, 22, 7212, "Bag C", 4);
    SeedNamedItem(eq, calcBagSlotId(22, 0), 7501, "Cz", 0);

    ASSERT_TRUE(inv->placeItem(22));  // A -> 22 (Ax), C -> cursor front (Cz), B behind
    ASSERT_EQ(inv->getCursorQueueSize(), 2u);
    EXPECT_EQ(inv->getCursorItem()->itemId, 7212u);
    ASSERT_NE(inv->getItem(calcBagSlotId(22, 0)), nullptr);
    EXPECT_EQ(inv->getItem(calcBagSlotId(22, 0))->itemId, 7301u);
    ASSERT_NE(inv->getItem(calcCursorBagSlotId(0)), nullptr);
    EXPECT_EQ(inv->getItem(calcCursorBagSlotId(0))->itemId, 7501u);

    ASSERT_TRUE(inv->placeItem(23));  // C -> 23 (Cz); B promoted (Bp)
    ASSERT_NE(inv->getItem(calcBagSlotId(23, 0)), nullptr);
    EXPECT_EQ(inv->getItem(calcBagSlotId(23, 0))->itemId, 7501u);
    ASSERT_EQ(inv->getCursorQueueSize(), 1u);
    EXPECT_EQ(inv->getCursorItem()->itemId, 7211u);
    ASSERT_NE(inv->getItem(calcCursorBagSlotId(0)), nullptr);
    EXPECT_EQ(inv->getItem(calcCursorBagSlotId(0))->itemId, 7401u);
}

// ---------------------------------------------------------------------------
// T259 -- trade-window slots (3000-3007) are bag parents too. Server model
// (eqemu zone/trading.cpp): a bag reaches a trade slot ONLY from the cursor
// (zone/inventory.cpp Client::SwapItem "Trade with non-cursor item" -> Kick);
// Trade::AddEntity PutItemInInventory()s the whole ItemInstance, contents
// included, addressed on the wire as 3031 + (trade-3000)*10 + idx
// (common/patches/titanium_limits.h TRADE_BAGS_BEGIN). On completion the
// giver gets NOTHING for its trade slots (Client::FinishTrade ->
// DeleteItemInInventory(trade_slot) without client_update) and the receiver
// gets one ItemPacketTrade for the bag WITH contents at a free general slot
// (PutItemInInventory(free_slot, inst, true)). On cancel the canceller's own
// items come back the same way (FinishTrade(this)); the OTHER side's items
// come back only after that client echoes OP_CancelTrade (Handle_OP_
// CancelTrade forwards the packet and FinishTrade()s only the sender).
// ---------------------------------------------------------------------------

void ActivateTradeWith(EverQuest& eq, uint32_t partnerSpawnId, const std::string& partnerName) {
    EQT::TradeRequest_Struct req{};
    req.target_spawn_id = 0;
    req.from_spawn_id = partnerSpawnId;
    eq.GetTradeManager()->handleTradeRequest(req, partnerName);
    eq.GetTradeManager()->acceptTradeRequest();
    ASSERT_EQ(eq.GetTradeManager()->getState(), TradeState::Active);
}

TEST_F(EqCommandsEquipTest, CursorMove_BagIntoTradeSlot_ContentsLandInTradeRange) {
    auto* inv = eq.GetInventoryManager();
    ActivateTradeWith(eq, 555, "Buddy");
    SeedBag(eq, 22, 7210, "Large Bag", 6);
    SeedNamedItem(eq, calcBagSlotId(22, 0), 7301, "Rusty Short Sword", 0);
    SeedNamedItem(eq, calcBagSlotId(22, 1), 7302, "Rusty Long Sword", 0);
    (void)bridge.drainIntents();

    ASSERT_TRUE(inv->pickupItem(22));
    ASSERT_NE(inv->getItem(calcCursorBagSlotId(0)), nullptr);
    ASSERT_TRUE(inv->placeItem(TRADE_BEGIN));
    EXPECT_FALSE(inv->hasCursorItem());
    ASSERT_NE(inv->getItem(TRADE_BEGIN), nullptr);
    EXPECT_EQ(inv->getItem(TRADE_BEGIN)->itemId, 7210u);
    // Wire numbering: 3031/3032 for trade slot 3000.
    ASSERT_NE(inv->getItem(3031), nullptr);
    ASSERT_NE(inv->getItem(3032), nullptr);
    EXPECT_EQ(inv->getItem(3031)->itemId, 7301u);
    EXPECT_EQ(inv->getItem(3032)->itemId, 7302u);
    EXPECT_EQ(calcContainerSlotId(TRADE_BEGIN, 0), 3031);
    EXPECT_EQ(inv->getItem(calcCursorBagSlotId(0)), nullptr);
    EXPECT_EQ(inv->getItem(calcBagSlotId(22, 0)), nullptr);

    // Exactly the two MoveItems the server needs (22->30, 30->3000), no
    // packet for the contents (they travel inside the bag server-side).
    auto intents = bridge.drainIntents();
    int moves = 0;
    for (const auto& intent : intents) {
        if (std::holds_alternative<eqt::events::MoveItemIntent>(intent)) moves++;
    }
    EXPECT_EQ(moves, 2);
}

TEST_F(EqCommandsEquipTest, DirectMove_GeneralBagStraightIntoTradeSlot_RefusedBeforeSend) {
    // eqemu Client::SwapItem: a MoveItem into a trade slot whose source is
    // not the cursor is answered with Kick("Trade with non-cursor item").
    // Pre-flight refuse it locally, nothing mutated, nothing sent.
    auto* inv = eq.GetInventoryManager();
    ActivateTradeWith(eq, 555, "Buddy");
    SeedBag(eq, 22, 7210, "Large Bag", 6);
    SeedNamedItem(eq, calcBagSlotId(22, 0), 7301, "Rusty Short Sword", 0);
    (void)bridge.drainEvents();
    (void)bridge.drainIntents();

    eq.RequestEquip("22 3000");

    auto msgs = DrainSystemMessages(bridge);
    ASSERT_EQ(msgs.size(), 1u);
    EXPECT_NE(msgs[0].find("cursor"), std::string::npos) << msgs[0];
    EXPECT_TRUE(bridge.drainIntents().empty());
    ASSERT_NE(inv->getItem(22), nullptr);
    EXPECT_EQ(inv->getItem(TRADE_BEGIN), nullptr);
    ASSERT_NE(inv->getItem(calcBagSlotId(22, 0)), nullptr);
    EXPECT_EQ(inv->getItem(3031), nullptr);
}

TEST_F(EqCommandsEquipTest, TradeFinish_OwnTradeSlotsAndContentsClearedCleanly) {
    // Giver's side: the server deletes the trade slots without any client
    // update and sends OP_FinishTrade. The client must drop the bag AND its
    // 3031+ range itself, leaving nothing behind anywhere.
    auto* inv = eq.GetInventoryManager();
    ActivateTradeWith(eq, 555, "Buddy");
    SeedBag(eq, 22, 7210, "Large Bag", 6);
    SeedNamedItem(eq, calcBagSlotId(22, 0), 7301, "Rusty Short Sword", 0);
    SeedNamedItem(eq, calcBagSlotId(22, 1), 7302, "Rusty Long Sword", 0);
    ASSERT_TRUE(inv->pickupItem(22));
    ASSERT_TRUE(inv->placeItem(TRADE_BEGIN));
    ASSERT_NE(inv->getItem(3031), nullptr);
    (void)bridge.drainEvents();

    eq.GetTradeManager()->handleFinishTrade();

    // The completion summary sees what we offered (TradeManager::hasOwnItem
    // reads the inventory trade slot; addItemToTrade() has no callers).
    bool gaveBag = false;
    for (const auto& msg : DrainSystemMessages(bridge)) {
        if (msg == "You give Large Bag to Buddy.") gaveBag = true;
    }
    EXPECT_TRUE(gaveBag);
    EXPECT_EQ(eq.GetTradeManager()->getState(), TradeState::None);
    EXPECT_EQ(inv->getItem(TRADE_BEGIN), nullptr);
    EXPECT_EQ(inv->getItem(3031), nullptr);
    EXPECT_EQ(inv->getItem(3032), nullptr);
    EXPECT_EQ(inv->getItem(22), nullptr);
    EXPECT_EQ(inv->getItem(calcBagSlotId(22, 0)), nullptr);
    EXPECT_EQ(inv->getItem(calcCursorBagSlotId(0)), nullptr);
    EXPECT_FALSE(inv->hasCursorItem());
}

TEST_F(EqCommandsEquipTest, TradeCancelledByPartner_EchoesCancel_ClearsRange_ReturnedBagKeepsContents) {
    auto* inv = eq.GetInventoryManager();
    int cancelsSent = 0;
    eq.GetTradeManager()->setSendCancelTrade([&](const EQT::CancelTrade_Struct&) { cancelsSent++; });
    ActivateTradeWith(eq, 555, "Buddy");
    SeedBag(eq, 22, 7210, "Large Bag", 6);
    SeedNamedItem(eq, calcBagSlotId(22, 0), 7301, "Rusty Short Sword", 0);
    SeedNamedItem(eq, calcBagSlotId(22, 1), 7302, "Rusty Long Sword", 0);
    ASSERT_TRUE(inv->pickupItem(22));
    ASSERT_TRUE(inv->placeItem(TRADE_BEGIN));
    ASSERT_NE(inv->getItem(3031), nullptr);
    ASSERT_EQ(cancelsSent, 0);

    // Partner cancelled: the server only returns the CANCELLER's items on its
    // own; ours come back after we echo OP_CancelTrade (eqemu
    // Handle_OP_CancelTrade -> FinishTrade(this) for the sender only).
    EQT::CancelTrade_Struct cancel{};
    cancel.spawn_id = 555;
    cancel.action = 7;
    eq.GetTradeManager()->handleCancelTrade(cancel);

    EXPECT_EQ(cancelsSent, 1);
    EXPECT_EQ(eq.GetTradeManager()->getState(), TradeState::None);
    EXPECT_EQ(inv->getItem(TRADE_BEGIN), nullptr);
    EXPECT_EQ(inv->getItem(3031), nullptr);
    EXPECT_EQ(inv->getItem(3032), nullptr);

    // A second OP_CancelTrade (the server forwards our echo back) is ignored:
    // no second echo, no loop.
    eq.GetTradeManager()->handleCancelTrade(cancel);
    EXPECT_EQ(cancelsSent, 1);

    // The returned bag arrives as ItemPacketTrade at a free general slot
    // (server 23, already translated to Titanium 22 on the wire by
    // titanium.cpp), serialised WITH its contents; T255's ItemPacket
    // sub-item parsing lands them in that slot's range.
    auto pkt = ItemPacketPacket(
        BuildBagString(22, 7210, "Large Bag", 6, {{0, BuildPlainItemString(0, 7301, "Rusty Short Sword")},
                                                  {1, BuildPlainItemString(1, 7302, "Rusty Long Sword")}}));
    EXPECT_EQ(inv->processItemPacket(pkt), 22);
    ASSERT_NE(inv->getItem(22), nullptr);
    ASSERT_NE(inv->getItem(calcBagSlotId(22, 0)), nullptr);
    ASSERT_NE(inv->getItem(calcBagSlotId(22, 1)), nullptr);
    EXPECT_EQ(inv->getItem(calcBagSlotId(22, 0))->itemId, 7301u);
    EXPECT_EQ(inv->getItem(calcBagSlotId(22, 1))->itemId, 7302u);
}

TEST_F(EqCommandsEquipTest, LocalCancel_ClearsOwnTradeRange) {
    auto* inv = eq.GetInventoryManager();
    ActivateTradeWith(eq, 555, "Buddy");
    SeedBag(eq, 22, 7210, "Large Bag", 6);
    SeedNamedItem(eq, calcBagSlotId(22, 0), 7301, "Rusty Short Sword", 0);
    ASSERT_TRUE(inv->pickupItem(22));
    ASSERT_TRUE(inv->placeItem(TRADE_BEGIN));
    ASSERT_NE(inv->getItem(3031), nullptr);

    eq.GetTradeManager()->cancelTrade();

    EXPECT_EQ(inv->getItem(TRADE_BEGIN), nullptr);
    EXPECT_EQ(inv->getItem(3031), nullptr);
}

}  // namespace
