// T446 -- /equip's name form, list form, and the swap pre-flights, split out
// of test_eq_commands_equip.cpp (1,492 lines, one under the 1,500 ceiling).
// Same target, same suite, same case names; the shared fixture and the
// Seed*() helpers are in eq_commands_equip_test_fixture.h. See
// test_eq_commands_equip_moves.cpp for why /equip is tested at this seam.

#include "eq_commands_equip_test_fixture.h"

using eqt::tests::EqCommandsEquipTest;

namespace {

// ---------------------------------------------------------------------------
// T77 -- item-name form: `/equip <item name>`.
// ---------------------------------------------------------------------------

TEST_F(EqCommandsEquipTest, NameForm_ResolvesSlotFromItemBitmask_MovesAndReports) {
    SeedNamedItem(eq, 22, 7010, "Rusty Mace", EQUIPSLOT_PRIMARY);
    eq.Commands().RequestEquip("Rusty Mace");

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
    eq.Commands().RequestEquip("rusty");

    auto* inv = eq.GetInventoryManager();
    ASSERT_NE(inv->getItem(SLOT_PRIMARY), nullptr);
    EXPECT_EQ(inv->getItem(SLOT_PRIMARY)->itemId, 7011u);
}

TEST_F(EqCommandsEquipTest, NameForm_NotFound_PushesNoIntent) {
    eq.Commands().RequestEquip("nonexistent item");

    auto msgs = DrainSystemMessages(bridge);
    ASSERT_EQ(msgs.size(), 1u);
    EXPECT_NE(msgs[0].find("No carried item matches"), std::string::npos) << msgs[0];
    EXPECT_TRUE(bridge.drainIntents().empty());
}

TEST_F(EqCommandsEquipTest, NameForm_AmbiguousAcrossDistinctItems_RejectedWithBothNamed) {
    SeedNamedItem(eq, 22, 7020, "Rusty Sword", EQUIPSLOT_PRIMARY);
    SeedNamedItem(eq, 23, 7021, "Broad Sword", EQUIPSLOT_PRIMARY);
    eq.Commands().RequestEquip("sword");

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
    eq.Commands().RequestEquip("Throwing Dagger");

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
    eq.Commands().RequestEquip("Jade Earring");

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
    eq.Commands().RequestEquip("New Earring");

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
    eq.Commands().RequestEquip("Rock");

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
    eq.Commands().RequestEquip("list");

    auto msgs = DrainSystemMessages(bridge);
    ASSERT_EQ(msgs.size(), 2u);
    EXPECT_NE(msgs[0].find("Nothing currently equipped"), std::string::npos) << msgs[0];
    EXPECT_NE(msgs[1].find("Nothing else carried"), std::string::npos) << msgs[1];
}

TEST_F(EqCommandsEquipTest, ListForm_ShowsEquippedAndCarriedWithResolution) {
    SeedNamedItem(eq, SLOT_PRIMARY, 9100, "Rusty Mace", EQUIPSLOT_PRIMARY);
    SeedNamedItem(eq, 22, 9101, "Dagger", EQUIPSLOT_SECONDARY);
    eq.Commands().RequestEquip("list");

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
    eq.Commands().RequestEquip("13 23");

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
    eq.Commands().RequestEquip("13 23");

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
    eq.Commands().RequestEquip("13 23");

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
    eq.Commands().RequestEquip("23 24");

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
    eq.Commands().RequestEquip("33 23");

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
    eq.Commands().RequestEquip("33 23");

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
    eq.Commands().RequestEquip("33 13");
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

}  // namespace
