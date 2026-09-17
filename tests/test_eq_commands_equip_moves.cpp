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
//
// T446 split this file by functional group; the shared fixture and the
// Seed*() helpers live in eq_commands_equip_test_fixture.h. This TU holds
// argument parsing, slot validation, the happy-path intent fields, T71's
// local-model sync and T77's pre-flight destination legality. The name/list
// forms and the swap pre-flights are in test_eq_commands_equip_swaps.cpp;
// everything about bag CONTENTS is in test_eq_commands_equip_bags.cpp.

#include "eq_commands_equip_test_fixture.h"

using eqt::tests::EqCommandsEquipTest;

namespace {

// ---------------------------------------------------------------------------
// Argument parsing / usage
// ---------------------------------------------------------------------------

TEST_F(EqCommandsEquipTest, NoArgs_PrintsUsage_PushesNoIntent) {
    eq.Commands().RequestEquip("");

    auto msgs = DrainSystemMessages(bridge);
    ASSERT_EQ(msgs.size(), 1u);
    EXPECT_NE(msgs[0].find("Usage"), std::string::npos);
    EXPECT_TRUE(bridge.drainIntents().empty());
}

TEST_F(EqCommandsEquipTest, MissingSecondSlot_PrintsUsage_PushesNoIntent) {
    eq.Commands().RequestEquip("23");

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
    eq.Commands().RequestEquip("general 13");

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
    eq.Commands().RequestEquip("22abc 13");

    auto msgs = DrainSystemMessages(bridge);
    ASSERT_EQ(msgs.size(), 1u);
    EXPECT_NE(msgs[0].find("Usage"), std::string::npos);
    EXPECT_TRUE(bridge.drainIntents().empty());
}

// ---------------------------------------------------------------------------
// Slot validation -- reject out-of-range/unrecognized slots with a clear
// message rather than emitting a malformed packet (the plan's explicit bar).
//
// T494 (board B796) moved WHERE the refusal comes from, not whether there is
// one: a number that is no `inventory.slot_id` is now caught by
// parseUserSlot() at the command boundary (which can say WHY: the server does
// not send that slot to this client) instead of falling through to
// InventoryManager::moveItem()'s "Invalid source slot". The bar -- one clear
// message, no intent, no packet -- is unchanged.
// ---------------------------------------------------------------------------

TEST_F(EqCommandsEquipTest, OutOfRangeSourceSlot_PrintsInvalidSourceSlot_PushesNoIntent) {
    // 9999 is not equipment/general/bag/cursor/bank/shared-bank/trade.
    eq.Commands().RequestEquip("9999 13");

    auto msgs = DrainSystemMessages(bridge);
    ASSERT_EQ(msgs.size(), 1u);
    EXPECT_NE(msgs[0].find("does not send"), std::string::npos) << msgs[0];
    EXPECT_NE(msgs[0].find("9999"), std::string::npos);
    EXPECT_TRUE(bridge.drainIntents().empty());
}

TEST_F(EqCommandsEquipTest, OutOfRangeDestinationSlot_PrintsInvalidDestinationSlot_PushesNoIntent) {
    eq.Commands().RequestEquip("23 9999");

    auto msgs = DrainSystemMessages(bridge);
    ASSERT_EQ(msgs.size(), 1u);
    EXPECT_NE(msgs[0].find("does not send"), std::string::npos) << msgs[0];
    EXPECT_NE(msgs[0].find("9999"), std::string::npos);
    EXPECT_TRUE(bridge.drainIntents().empty());
}

TEST_F(EqCommandsEquipTest, NegativeSlot_RejectedAsInvalid_PushesNoIntent) {
    // -1 is SLOT_INVALID's own sentinel value -- must not be forwarded as a
    // "valid" slot just because it parses as an integer.
    eq.Commands().RequestEquip("-1 13");

    auto msgs = DrainSystemMessages(bridge);
    ASSERT_EQ(msgs.size(), 1u);
    EXPECT_NE(msgs[0].find("does not send"), std::string::npos) << msgs[0];
    EXPECT_TRUE(bridge.drainIntents().empty());
}

// ---------------------------------------------------------------------------
// Happy path -- the right intent, with the right fields, is pushed for
// every recognized slot category the plan names.
// ---------------------------------------------------------------------------

TEST_F(EqCommandsEquipTest, GeneralToPrimary_PushesMoveItemIntentWithExactSlots) {
    // The plan's own worked example: "e.g. equip a weapon: /equip 22 13".
    SeedItem(eq, 22, 6011);  // Rusty Mace (T63/T65/T71's shared test item)
    eq.Commands().RequestEquip("23 13");

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
    eq.Commands().RequestEquip("23 13");

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
    eq.Commands().RequestEquip("13 23");

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
    eq.Commands().RequestEquip("33 14");

    auto intents = bridge.drainIntents();
    // toSlot 14 (Secondary) is an equipment slot; this path goes through
    // InventoryManager::placeItem(), which already pairs MoveItemIntent with
    // EquipmentChangedIntent for drag/drop -- same pairing here now.
    //
    // T328 Leg B: placeItem() now also pushes PlaySoundIntent{WEAR_CLOTH} --
    // the official Titanium client plays wearclth.WAV whenever an item is
    // dropped into a slot (review/T328/README.md row 6), and a cursor-source
    // /equip IS that drop. Hence 3 intents, not 2. This is the ONLY /equip
    // arm that reaches placeItem(); the slot-to-slot arms below still push
    // MoveItemIntent alone and their counts are unchanged.
    ASSERT_EQ(intents.size(), 3u);
    const auto* mi = FindMoveItemIntent(intents);
    ASSERT_NE(mi, nullptr);
    EXPECT_EQ(mi->fromSlot, CURSOR_SLOT);
    EXPECT_EQ(mi->toSlot, SLOT_SECONDARY);
}

TEST_F(EqCommandsEquipTest, BankSlotToGeneral_RecognizedAsValid_PushesMoveItemIntent) {
    // Bank slots (2000-2015) are a recognized category too -- /equip is a
    // generic slot-to-slot wrapper over SendMoveItem, not equip-only.
    SeedItem(eq, BANK_BEGIN, 9990);  // arbitrary stackable test item
    eq.Commands().RequestEquip("2000 23");

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
    eq.Commands().RequestEquip("13 14");

    auto* inv = eq.GetInventoryManager();
    ASSERT_NE(inv, nullptr);
    ASSERT_NE(inv->getItem(SLOT_PRIMARY), nullptr);
    ASSERT_NE(inv->getItem(SLOT_SECONDARY), nullptr);
    EXPECT_EQ(inv->getItem(SLOT_PRIMARY)->itemId, 9997u);    // Dagger moved in
    EXPECT_EQ(inv->getItem(SLOT_SECONDARY)->itemId, 6011u);  // Mace moved in
}

TEST_F(EqCommandsEquipTest, DirectMoveToEmptySlot_ClearsSourceLocally) {
    SeedItem(eq, 22, 6011);
    eq.Commands().RequestEquip("23 13");

    auto* inv = eq.GetInventoryManager();
    ASSERT_NE(inv, nullptr);
    EXPECT_EQ(inv->getItem(22), nullptr);  // source cleared, not left duplicated
    ASSERT_NE(inv->getItem(SLOT_PRIMARY), nullptr);
    EXPECT_EQ(inv->getItem(SLOT_PRIMARY)->itemId, 6011u);
}

TEST_F(EqCommandsEquipTest, EmptySourceSlot_PrintsErrorAndPushesNoIntent) {
    // No item was ever seeded at 13 -- /equip must refuse rather than send
    // a request for a move that cannot mean anything.
    eq.Commands().RequestEquip("13 14");

    auto msgs = DrainSystemMessages(bridge);
    ASSERT_EQ(msgs.size(), 1u);
    EXPECT_NE(msgs[0].find("empty"), std::string::npos) << msgs[0];
    EXPECT_TRUE(bridge.drainIntents().empty());
}

TEST_F(EqCommandsEquipTest, CursorToSecondary_UpdatesLocalInventoryModel) {
    SeedItem(eq, SLOT_SECONDARY, 9990);  // pre-existing occupant, displaced to cursor
    SeedItem(eq, CURSOR_SLOT, 9997);     // Dagger, picked up
    eq.Commands().RequestEquip("33 14");

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


TEST_F(EqCommandsEquipTest, WrongSlotType_RejectedLocally_NamesItemAndSlot_PushesNoIntent) {
    // A Primary-only weapon aimed at Head (slot 2): canEquipInSlot() fails
    // first (wrong bitmask), not the restrictions check.
    SeedNamedItem(eq, 22, 7001, "Rusty Mace", EQUIPSLOT_PRIMARY);
    eq.Commands().RequestEquip("23 2");

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
    eq.Commands().RequestEquip("23 13");

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
    eq.Commands().RequestEquip("23 13");

    auto* inv = eq.GetInventoryManager();
    ASSERT_NE(inv->getItem(SLOT_PRIMARY), nullptr);
    EXPECT_EQ(inv->getItem(SLOT_PRIMARY)->itemId, 7003u);
    auto intents = bridge.drainIntents();
    EXPECT_NE(FindMoveItemIntent(intents), nullptr);
}

TEST_F(EqCommandsEquipTest, NoOpMove_SameSlot_RejectedLocally_PushesNoIntent) {
    SeedItem(eq, 22, 6011);
    eq.Commands().RequestEquip("23 23");

    auto msgs = DrainSystemMessages(bridge);
    ASSERT_EQ(msgs.size(), 1u);
    EXPECT_NE(msgs[0].find("same slot"), std::string::npos) << msgs[0];
    EXPECT_TRUE(bridge.drainIntents().empty());
}

}  // namespace
