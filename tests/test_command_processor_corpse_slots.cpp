// B810 (#199) -- the corpse slot numbers have the SAME Titanium/server split
// B796 was, and nothing converted them.
//
// THE CORPSE TABLE, transcribed from the eqemu source itself
// (/mnt/projects/akk-stack/code/common/patches, cited, not assumed):
//
//   rof2_limits.h:124-158  RoF2::invslot: waist 20, power source 21,
//     ammo 22, general1-8 23-30, general9/10 31/32, cursor 33,
//     POSSESSIONS_COUNT 34.
//   titanium_limits.h:124-153  Titanium invslot: waist 20, ammo 21,
//     general1-8 22-29, cursor 30, POSSESSIONS_COUNT 31.
//   titanium.cpp:3641  ServerToTitaniumCorpseSlot():
//     23-30 -> -1 | 33..(34+20=54) -> -3 | (34+22=56) -> -4 | else INVALID
//   titanium.cpp:3727  TitaniumToServerCorpseSlot():
//     22-29 -> +1 | 30..(31+20=51) -> +3 | (31+21=52) -> +4 | else INVALID
//
// So general 1-8 are server 23-30 / model 22-29, the corpse
// cursor-and-equipment range is server 33-54 / model 30-51, and the single
// ammo corpse slot is server 56 / model 52. Server 31/32 (general 9/10) and
// 55 have no Titanium slot; the domain starts at slotGeneral1, so 0-22
// (equipment, power source, ammo) are not corpse slots at all.
//
// WHY THE WIRE AND THE INTENT ARE ALREADY MODEL-NUMBERED: eqemu sends the
// loot window's contents as ItemPacketLoot with its own slot
// (corpse.cpp MakeLootRequestPackets, loot_slot = slotGeneral1 .. CORPSE_END),
// and every ItemPacket crosses the patch through ENCODE(OP_ItemPacket)'s
// ServerToTitaniumSlot() (titanium.cpp:1135), which over the corpse domain
// agrees with ServerToTitaniumCorpseSlot (general -1, 33-54 -3, and the -4
// arm is a DEAD arm the server never fires, because it never puts an item on
// corpse slot 36). The client's OP_LootItem comes back through
// TitaniumToServerCorpseSlot() (titanium.cpp:2980). So fable's loot window,
// LootItemIntent, PendingLootSlots, the auto-loot queue and the sent packet
// are all MODEL-numbered, and the server accepts exactly that back.
//
// The unconverted hops are the user boundary, B796's rule (wire = Titanium,
// user-facing text = inventory.slot_id): a typed /loot <N> is a SERVER
// number, converted on the way in by parseCorpseSlot(); the two chat lines
// that name the slot (eq_action_handler.cpp lootItem, action_dispatcher.cpp
// lootItem's refusal) now print corpseSlotToServer(), the same number the
// user typed. The loot popup draws NAMES only, so it prints no slots.
// The possessions pair is the wrong table for this domain: its -3 arm has
// the SAME boundaries (server 33-54) but is a DIFFERENT function with a
// DIFFERENT dead arm (server 36, the RoF2 POSSESSIONS_COUNT+slotAmmo, which
// is server 56 in the corpse table) -- over 34-54 the two tables map to the
// SAME model slots, but a corpse slot converted with the possessions
// function is still a bug: it is the wrong table, and the ammo corpse slot
// (56) cannot even be expressed in it.
//
// What this file pins: the table (every value the server's two functions
// produce over 0..60, the round-trips, the dead slots), the IN hop through
// the real CommandProcessor (typed server number -> model slot, with the
// refusal shapes), and the parser itself. The OUT hop is pinned in
// tests/test_loot_window_actions.cpp (LootingAPrintsTheServerNumber, the
// SystemMessage the take prints) and the intent's model numbering in
// tests/test_loot_panel.cpp (ClickingARowPushesLootItemIntentCarryingTheCorpseSlot).

#include "command_processor_test_fixture.h"

#include "client/inventory/inventory_constants.h"

#include <gtest/gtest.h>
#include <string>

using eqt::inventory::SLOT_INVALID;
using eqt::inventory::UserSlotResult;
using eqt::inventory::corpseSlotToServer;
using eqt::inventory::parseCorpseSlot;
using eqt::inventory::serverCorpseSlotToModel;
using eqt::tests::CommandProcessorTest;  // for the TEST_F cases below

namespace {

// The server's ServerToTitaniumCorpseSlot (titanium.cpp:3641), the
// acceptance criterion's oracle: "against the values the server's two
// functions produce".
int16_t ServerCorpseSlotToModel(uint32_t serverSlot) {
    if (serverSlot >= 23 && serverSlot <= 30) return static_cast<int16_t>(serverSlot - 1);
    if (serverSlot >= 33 && serverSlot <= 54) return static_cast<int16_t>(serverSlot - 3);
    if (serverSlot == 56) return static_cast<int16_t>(serverSlot - 4);
    return SLOT_INVALID;
}

// And the server's TitaniumToServerCorpseSlot (titanium.cpp:3727).
int16_t ModelCorpseSlotToServer(int16_t modelSlot) {
    if (modelSlot >= 22 && modelSlot <= 29) return modelSlot + 1;
    if (modelSlot >= 30 && modelSlot <= 51) return modelSlot + 3;
    if (modelSlot == 52) return modelSlot + 4;
    return SLOT_INVALID;
}

// Every slot of the domain, against what the server's IN function produces.
TEST(BCorpseSlotTable, InboundAgreesWithTheServerForEverySlot) {
    for (int s = 0; s <= 60; ++s) {
        EXPECT_EQ(serverCorpseSlotToModel(static_cast<int16_t>(s)),
                  ServerCorpseSlotToModel(static_cast<uint32_t>(s)))
            << "server corpse slot " << s << " (titanium.cpp:3641)";
    }
}

// Every model slot, against what the server's OUT function produces.
TEST(BCorpseSlotTable, OutboundAgreesWithTheServerForEverySlot) {
    for (int s = 0; s <= 60; ++s) {
        EXPECT_EQ(corpseSlotToServer(static_cast<int16_t>(s)),
                  ModelCorpseSlotToServer(static_cast<int16_t>(s)))
            << "model corpse slot " << s << " (titanium.cpp:3727)";
    }
}

// The named slots, so a broken edge fails by name rather than by number.
TEST(BCorpseSlotTable, TheNamedSlots) {
    EXPECT_EQ(serverCorpseSlotToModel(23), 22) << "general 1";
    EXPECT_EQ(serverCorpseSlotToModel(30), 29) << "general 8";
    EXPECT_EQ(serverCorpseSlotToModel(33), 30) << "the cursor range starts";
    EXPECT_EQ(serverCorpseSlotToModel(54), 51) << "the -3 arm's top";
    EXPECT_EQ(serverCorpseSlotToModel(56), 52) << "the ammo corpse slot (-4)";
    EXPECT_EQ(corpseSlotToServer(22), 23) << "general 1 back out";
    EXPECT_EQ(corpseSlotToServer(29), 30) << "general 8 back out";
    EXPECT_EQ(corpseSlotToServer(30), 33) << "the model's cursor";
    EXPECT_EQ(corpseSlotToServer(51), 54) << "the +3 arm's top back out";
    EXPECT_EQ(corpseSlotToServer(52), 56) << "the ammo corpse slot back out";
}

// The number the user SEES when a take prints a slot (eq_action_handler.cpp
// lootItem, action_dispatcher.cpp's refusal) is corpseSlotToServer(model) --
// the SAME number the user typed. Pin the round-trip that makes the print
// re-typeable: /loot <N> converts N to the model slot, and printing that
// model slot returns N.
TEST(BCorpseSlotTable, ThePrintedSlotIsTheTypedSlot) {
    for (int s = 23; s <= 56; ++s) {
        const int16_t server = static_cast<int16_t>(s);
        const int16_t model = serverCorpseSlotToModel(server);
        if (model == SLOT_INVALID) continue;  // 31/32/55 -- dead, not printed
        EXPECT_EQ(corpseSlotToServer(model), server)
            << "a take of typed slot " << server
            << " must print " << server << ", the number that can be typed back";
    }
}

// The domain edges: it starts at general 1 (server 23), so the looter's own
// equipment, power source, ammo (server 0-22) and general 9/10 (31/32) and
// 55 are NOT corpse slots -- refused, not converted.
TEST(BCorpseSlotTable, TheDomainEdges) {
    for (int16_t s = 0; s <= 22; ++s)
        EXPECT_EQ(serverCorpseSlotToModel(s), SLOT_INVALID)
            << "slot " << s << " is the looter's own gear, not a corpse slot";
    EXPECT_EQ(serverCorpseSlotToModel(31), SLOT_INVALID) << "general 9";
    EXPECT_EQ(serverCorpseSlotToModel(32), SLOT_INVALID) << "general 10";
    EXPECT_EQ(serverCorpseSlotToModel(55), SLOT_INVALID);
    EXPECT_EQ(serverCorpseSlotToModel(57), SLOT_INVALID);
}

// The corpse pair is a SEPARATE function from the possessions pair (T494),
// not a different table: over the corpse domain (server 23-56) the two agree
// on EVERY value, and over server 0-22 / 31-32 / 55 the corpse pair refuses
// where the possessions pair converts. The test pins both halves, so a "fix"
// that re-uses the possessions pair for a corpse hop -- or a corpse-domain
// refactor that starts converting the looter's gear -- fails.
TEST(BCorpseSlotTable, AgreesWithPossessionsOnTheDomainRefusesWhatItRefuses) {
    // Identical over the whole corpse domain: the two ports must not drift.
    for (int s = 23; s <= 56; ++s) {
        const int16_t server = static_cast<int>(s);
        const int16_t corpse = serverCorpseSlotToModel(server);
        const int16_t poss = eqt::inventory::serverSlotToClient(server);
        EXPECT_EQ(corpse, poss)
            << "server " << server << ": the corpse and possessions arms agree here";
    }
    // Differ over the looter's own gear: corpse refuses, possessions maps.
    for (int s = 0; s <= 20; ++s) {
        EXPECT_EQ(serverCorpseSlotToModel(static_cast<int16_t>(s)), SLOT_INVALID)
            << "corpse refuses equipment " << s;
        EXPECT_NE(eqt::inventory::serverSlotToClient(static_cast<int16_t>(s)), SLOT_INVALID)
            << "possessions maps equipment " << s << " (identity)";
    }
    EXPECT_EQ(serverCorpseSlotToModel(22), SLOT_INVALID) << "corpse refuses the ammo slot";
    EXPECT_EQ(eqt::inventory::serverSlotToClient(22), 21)
        << "possessions, by contrast, maps the ammo slot 22 -> 21";
    // General 9/10 and 55: refused by BOTH (they have no Titanium model), so
    // the difference there is a no-op -- only 0-22 is where re-using the
    // possessions pair would be observable.
    EXPECT_EQ(serverCorpseSlotToModel(31), SLOT_INVALID);
    EXPECT_EQ(eqt::inventory::serverSlotToClient(31), SLOT_INVALID);
}

// Every addressable slot round-trips in BOTH directions; every dead model
// slot is dead out. (The reverse direction -- dead SERVER slot dead out -- is
// the first loop here and the server's functions have exactly the same
// property. The model and server labelings are NOT the same set: model
// 31-52 are not corpse model slots, but several of those NUMBERS are valid
// SERVER corpse slots, so "dead out" on a model number must not be checked
// against the IN function at the same number.)
TEST(BCorpseSlotTable, RoundTripsAndDeadSlots) {
    // Server -> model: valid server slots round-trip; dead ones are dead IN.
    // (Do NOT check a dead SERVER number against the OUT function: a dead
    // server number can still be a VALID MODEL number -- e.g. server 22 is
    // dead in but model 22 maps out to server 23. The OUT function's domain
    // is model slots, not server numbers.)
    for (int s = 0; s <= 60; ++s) {
        const int16_t server = static_cast<int16_t>(s);
        const int16_t model = serverCorpseSlotToModel(server);
        if (model == SLOT_INVALID) {
            continue;  // dead in: nothing to round-trip
        }
        EXPECT_EQ(corpseSlotToServer(model), server)
            << "server " << server << " -> model " << model << " -> server";
        EXPECT_EQ(serverCorpseSlotToModel(server), model)
            << "and the IN side agrees";
    }
    // Model -> server: valid model slots round-trip; every other model slot
    // is dead out. (The IN side of a valid model slot at its OWN number is
    // not asserted here -- model 31-52 are dead-model yet several of those
    // numbers are valid SERVER corpse slots.)
    for (int s = 0; s <= 60; ++s) {
        const int16_t model = static_cast<int16_t>(s);
        const bool isCorpseModel = (model >= 22 && model <= 29) ||
                                   (model >= 30 && model <= 51) ||
                                   model == 52;
        const int16_t server = corpseSlotToServer(model);
        if (!isCorpseModel) {
            EXPECT_EQ(server, SLOT_INVALID)
                << "model " << model << " is not a corpse model slot, must be dead out";
            continue;
        }
        EXPECT_NE(server, SLOT_INVALID)
            << "model " << model << " is a corpse model slot, must have a server slot";
        EXPECT_EQ(serverCorpseSlotToModel(server), model)
            << "model " << model << " -> server " << server << " -> model";
    }
}

// ---------------------------------------------------------------------------
// IN hop: a typed /loot <N> with a window open is a SERVER number
// (inventory.slot_id -- B796's rule for everything a user types), converted
// to the model slot before it reaches the dispatcher. RecordingActionHandler
// records the number it is actually handed, so these assert the boundary.
// Before B810, /loot 23 dispatched lootItem(176, 23) and the model's
// find(23) missed the item the model held at 22 -- the acceptance
// criterion's defect.
// ---------------------------------------------------------------------------

TEST_F(CommandProcessorTest, Loot_WindowOpen_TypedServerGeneral1DispatchesTheModelSlot) {
    state.player().setLootingCorpse(176);
    auto result = processor->processCommand("/loot 23");
    EXPECT_TRUE(result.success) << result.message;
    ASSERT_TRUE(handler.calledOnceWith("lootItem(176, 22)"))
        << "typed 23 (server general 1) must dispatch the model's slot 22";
}

TEST_F(CommandProcessorTest, Loot_WindowOpen_TypedServerGeneral8DispatchesTheModelSlot) {
    state.player().setLootingCorpse(176);
    auto result = processor->processCommand("/loot 30");
    EXPECT_TRUE(result.success) << result.message;
    ASSERT_TRUE(handler.calledOnceWith("lootItem(176, 29)"));
}

TEST_F(CommandProcessorTest, Loot_WindowOpen_TypedAmmoCorpseSlotDispatchesMinusFour) {
    state.player().setLootingCorpse(176);
    // The -4 arm the issue text calls out: server 56 (POSSESSIONS_COUNT +
    // slotAmmo) is model 52, NOT model 52's possessions conversion (there
    // is none -- the possessions table does not address 56 at all).
    auto result = processor->processCommand("/loot 56");
    EXPECT_TRUE(result.success) << result.message;
    ASSERT_TRUE(handler.calledOnceWith("lootItem(176, 52)"));
}

TEST_F(CommandProcessorTest, Loot_WindowOpen_TypedEquipmentCorpseSlotDispatchesMinusThree) {
    state.player().setLootingCorpse(176);
    // Server 34 is the corpse's first equipment slot (RoF2 CorpseBitmask's
    // EQUIPMENT_BITMASK << 34): model 31.
    auto result = processor->processCommand("/loot 34");
    EXPECT_TRUE(result.success) << result.message;
    ASSERT_TRUE(handler.calledOnceWith("lootItem(176, 31)"));
}

TEST_F(CommandProcessorTest, Loot_WindowOpen_AmmoNumberIsRefusedNotDispatched) {
    state.player().setLootingCorpse(176);
    // Server slot 22 is the AMMO slot: a legal inventory.slot_id for
    // /equip, but the corpse domain starts at general 1. It must be refused
    // by name, not dispatched at model 22 (a DIFFERENT item -- general 1).
    auto result = processor->processCommand("/loot 22");
    EXPECT_FALSE(result.success);
    EXPECT_NE(result.message.find("22"), std::string::npos) << result.message;
    EXPECT_TRUE(handler.empty());
}

TEST_F(CommandProcessorTest, Loot_WindowOpen_EquipmentNumberIsRefusedNotDispatched) {
    state.player().setLootingCorpse(176);
    // Server 20 (the waist) is equipment, not a corpse slot.
    auto result = processor->processCommand("/loot 20");
    EXPECT_FALSE(result.success);
    EXPECT_NE(result.message.find("20"), std::string::npos) << result.message;
    EXPECT_TRUE(handler.empty());
}

TEST_F(CommandProcessorTest, Loot_WindowOpen_General9IsRefusedAsUnaddressable) {
    state.player().setLootingCorpse(176);
    // Server general 9 (31) is the one a database reader would type:
    // addressable in RoF2, not sent to a Titanium client. B796's refusal
    // shape: name the token, do not dispatch.
    auto result = processor->processCommand("/loot 31");
    EXPECT_FALSE(result.success);
    EXPECT_NE(result.message.find("31"), std::string::npos) << result.message;
    EXPECT_TRUE(handler.empty());
}

TEST_F(CommandProcessorTest, Loot_WindowOpen_NameFormStillResolvesTheItem) {
    state.player().setLootingCorpse(176);
    // The conversion must not swallow the name form: a word resolves the
    // item by name exactly as before B810.
    auto result = processor->processCommand("/loot Fire Beetle Eye");
    EXPECT_TRUE(result.success) << result.message;
    ASSERT_TRUE(handler.calledOnceWith("lootItemByName(\"Fire Beetle Eye\")"));
}

// ---------------------------------------------------------------------------
// The parser the command uses, as a pure function.
// ---------------------------------------------------------------------------

TEST(BCorpseSlotParser, ParseConvertsEveryAddressableServerNumber) {
    int16_t out = SLOT_INVALID;
    const struct { int server; int model; } table[] = {
        {23, 22}, {29, 28}, {30, 29}, {33, 30}, {36, 33}, {54, 51}, {56, 52}};
    for (const auto& row : table) {
        EXPECT_EQ(parseCorpseSlot(std::to_string(row.server), out),
                  UserSlotResult::Ok)
            << "server " << row.server;
        EXPECT_EQ(out, row.model);
    }
}

TEST(BCorpseSlotParser, ParseRefusesWhatTheCorpseCannotHold) {
    int16_t out = 7;
    EXPECT_EQ(parseCorpseSlot("22", out), UserSlotResult::NotAddressable)
        << "ammo is not a corpse slot";
    EXPECT_EQ(parseCorpseSlot("21", out), UserSlotResult::NotAddressable)
        << "the power source is not sent at all";
    EXPECT_EQ(parseCorpseSlot("20", out), UserSlotResult::NotAddressable)
        << "equipment is not a corpse slot";
    EXPECT_EQ(parseCorpseSlot("31", out), UserSlotResult::NotAddressable)
        << "general 9 has no Titanium slot";
    EXPECT_EQ(parseCorpseSlot("32", out), UserSlotResult::NotAddressable);
    EXPECT_EQ(parseCorpseSlot("55", out), UserSlotResult::NotAddressable);
    EXPECT_EQ(parseCorpseSlot("57", out), UserSlotResult::NotAddressable);
}

TEST(BCorpseSlotParser, ParseStillReportsTyposAsTypos) {
    int16_t out = 7;
    EXPECT_EQ(parseCorpseSlot("abc", out), UserSlotResult::NotANumber);
    EXPECT_EQ(parseCorpseSlot("", out), UserSlotResult::NotANumber);
    EXPECT_EQ(parseCorpseSlot("12x", out), UserSlotResult::NotANumber);
    EXPECT_EQ(parseCorpseSlot("99999999999999999999", out),
              UserSlotResult::NotANumber);
}

}  // namespace
