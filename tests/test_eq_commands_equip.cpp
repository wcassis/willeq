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
// UI's drag-and-drop gesture pushes (inventory_manager.cpp) -- rather than
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

}  // namespace
