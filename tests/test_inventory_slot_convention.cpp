// T494 (board B796) -- the slot numbers a USER types are `inventory.slot_id`,
// the server's own (RoF2) numbering, not the Titanium numbering fable's
// inventory model is indexed by.
//
// The split these tests pin, measured from the server source:
//
//   * fable's inventory model is TITANIUM-numbered. The server translates on
//     the way out (common/patches/titanium.cpp:3576 ServerToTitaniumSlot) and
//     back on the way in (:3666 TitaniumToServerSlot), so what reaches
//     src/client/inventory/inventory_item_parser.cpp is already Titanium:
//     ammo 21, general 22-29, cursor 30.
//   * `inventory.slot_id` -- what a developer or a rig reads out of the
//     database and what every eqemu log line prints -- is RoF2: waist 20,
//     POWER SOURCE 21, ammo 22, general 23-30, cursor 33.
//
// So DB slot 23 (general 1) is model slot 22, and DB slot 21 exists on the
// server but is NOT SENT to a Titanium client at all. Before T494 every
// user-boundary number (/equip, /read, /scribe, /sell, /combine, and
// describeSlot()'s replies) was the raw model number, so `/equip 21 13`
// reached model slot 21 = DB slot 22 -- the off-by-one B796 caught live.
//
// The wire seeding below deliberately does NOT call the production
// serverSlotToClient(): it is a literal transcription of the server's own
// branches, so these tests cannot pass by agreeing with a wrong port.

#include "eq_commands_equip_test_fixture.h"

using eqt::tests::EqCommandsEquipTest;

namespace {

// Literal transcription of titanium.cpp:3576 ServerToTitaniumSlot(), for the
// possessions range these tests use. RoF2 constants from
// common/patches/rof2_limits.h:144-157 (slotWaist=20, slotPowerSource=21,
// slotAmmo=22, slotGeneral1..8=23..30, slotCursor=33).
int16_t WireSlotForDbSlot(int16_t dbSlot) {
    if (dbSlot >= 0 && dbSlot <= 20) return dbSlot;          // charm..waist: identity
    if (dbSlot == 22) return 21;                             // ammo: -1
    if (dbSlot >= 23 && dbSlot <= 30) return static_cast<int16_t>(dbSlot - 1);  // general1-8: -1
    if (dbSlot == 33) return 30;                             // cursor: -3
    return SLOT_INVALID;  // 21 = slotPowerSource: NO Titanium slot at all
}

// Seeds the model exactly as a zone-in packet would have: the row's DB slot
// run through the server's translation first.
void SeedFromDbSlot(EverQuest& eq, int16_t dbSlot, uint32_t itemId, const std::string& name,
                    uint32_t slotsBitmask) {
    const int16_t wire = WireSlotForDbSlot(dbSlot);
    ASSERT_NE(wire, SLOT_INVALID) << "DB slot " << dbSlot << " is never serialised to a "
                                     "Titanium client -- it cannot be seeded";
    SeedNamedItem(eq, wire, itemId, name, slotsBitmask);
}

// B796's exact live setup, minus the server: Testcaster had a Shuriken at DB
// slot 22 and the seeded Dagger* at DB slot 21.  Here the dagger is where a
// general-slot item actually lives (DB 23), so the two neighbouring rows the
// off-by-one confuses are both populated.
void SeedB796Inventory(EverQuest& eq) {
    SeedFromDbSlot(eq, 23, 9997, "Dagger*", EQUIPSLOT_PRIMARY);   // model slot 22
    SeedFromDbSlot(eq, 22, 5000, "Shuriken", EQUIPSLOT_AMMO);     // model slot 21
}

// ---------------------------------------------------------------------------
// Case 1 -- a DB general slot addresses the item at THAT DB slot.
// ---------------------------------------------------------------------------

TEST_F(EqCommandsEquipTest, EquipByDbSlot_AddressesTheRowAtThatSlotId_NotItsNeighbour) {
    SeedB796Inventory(eq);
    (void)bridge.drainIntents();
    (void)DrainSystemMessages(bridge);

    eq.Commands().RequestEquip("23 13");  // DB slot 23 = the Dagger*

    auto msgs = DrainSystemMessages(bridge);
    ASSERT_EQ(msgs.size(), 1u);
    EXPECT_NE(msgs[0].find("Dagger*"), std::string::npos) << msgs[0];
    EXPECT_EQ(msgs[0].find("Shuriken"), std::string::npos) << msgs[0];

    auto intents = bridge.drainIntents();
    const auto* mv = FindMoveItemIntent(intents);
    ASSERT_NE(mv, nullptr) << (msgs.empty() ? std::string() : msgs[0]);
    // The intent is the WIRE form: the model/Titanium slot, untouched. Only
    // the user boundary converts.
    EXPECT_EQ(mv->fromSlot, 22);
    EXPECT_EQ(mv->toSlot, 13);
}

// ---------------------------------------------------------------------------
// Case 2 -- B796 verbatim. DB slot 21 is RoF2's slotPowerSource; a Titanium
// client is never sent it, so it is not a slot a user of THIS client can
// address. It must be refused, and it must above all not resolve to DB 22.
// ---------------------------------------------------------------------------

TEST_F(EqCommandsEquipTest, EquipDbSlot21_PowerSource_Refused_DoesNotReachDbSlot22) {
    SeedB796Inventory(eq);
    (void)bridge.drainIntents();
    (void)DrainSystemMessages(bridge);

    eq.Commands().RequestEquip("21 13");

    auto msgs = DrainSystemMessages(bridge);
    ASSERT_EQ(msgs.size(), 1u);
    // B796's live message was "] Shuriken cannot go in Primary (13) -- wrong
    // slot type for this item": the argument 21 had resolved to DB slot 22.
    EXPECT_EQ(msgs[0].find("Shuriken"), std::string::npos) << msgs[0];
    EXPECT_NE(msgs[0].find("does not send"), std::string::npos) << msgs[0];

    EXPECT_TRUE(bridge.drainIntents().empty());
    // The Shuriken is still exactly where it was.
    const auto* shuriken = eq.GetInventoryManager()->getItem(21);
    ASSERT_NE(shuriken, nullptr);
    EXPECT_EQ(shuriken->itemId, 5000u);
}

}  // namespace
