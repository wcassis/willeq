// K06 (Batch K, refactor/K06_entities_component.md) --
// eqt::entities::EntitiesComponent.
//
// K06 dropped forwarders entirely, following K01's ruling: EverQuest no
// longer declares a single entities-family sender, ZoneProcess* handler, or
// entity-lookup helper -- every caller goes through EverQuest::Entities().
// m_entities itself (the spawn-id -> Entity map) STAYS a core member,
// reached here as Context().entities: this component owns the handlers that
// mutate it, not the map storage. This file covers the component's own
// plumbing (construction, ClientContext binding) and a couple of ported
// cases that exercise the component's public surface directly; the deep
// ZoneProcess* coverage (real captures, wire-offset pins) stays in
// test_zone_entities.cpp / test_zone_connection_flow_*.cpp, whose call sites
// route through DispatchZonePacket -> m_entities_component-> unchanged.

#include <gtest/gtest.h>

#include <cstdint>
#include <string>
#include <vector>

#include "client/entities/entities_component.h"
#include "zone_stream_test_utils.h"

using namespace zonetest;

namespace {

class EntitiesComponentTest : public ::testing::Test {
protected:
    EntitiesComponentTest() : eq(MakeEq()) {}
    EverQuest eq;
};

// ===================================================================
// 1. It constructs against a real EverQuest in headless mode
// ===================================================================

TEST_F(EntitiesComponentTest, ComponentExistsAndIsStable) {
    // Entities() must hand back the SAME component every time, not a copy.
    EXPECT_EQ(&eq.Entities(), &eq.Entities());
}

TEST_F(EntitiesComponentTest, ContextEntitiesIsTheSameMapAsGetEntities) {
    // m_entities stays a core member on EverQuest (the batch's "core" rule --
    // 11 TU families read it). The component must see the SAME map, not a
    // copy: ctx_.entities and EverQuest::GetEntities() are one object.
    EXPECT_EQ(&eq.Context().entities, &eq.GetEntities());
}

// ===================================================================
// 2. ListEntities -- the entity-lookup helper, now on the component, still
//    reads the one shared m_entities map.
//
//    T362 (B070) DELETED the component's other lookup, FindEntityByName --
//    an unranked case-insensitive prefix match where the first hit in
//    spawn-id order won. Its two cases lived here. Every name a user types
//    now resolves through EntityManager::findEntityByName (the T244
//    nearest-first ranked lookup, covered in test_state_modules.cpp); the
//    last caller, /warp, was retyped to a spawn id in the same unit.
// ===================================================================

TEST_F(EntitiesComponentTest, ListEntitiesDoesNotThrowOnAnEmptyOrPopulatedTable) {
    EXPECT_NO_THROW(eq.Entities().ListEntities());
    ASSERT_NO_THROW(ReplayZone(eq, HC_OP_NewSpawn, NewSpawnPayloadAt(1, "Some_Mob", 0, 0, 0)));
    EXPECT_NO_THROW(eq.Entities().ListEntities("Some"));
}

// ===================================================================
// 3. World-object / door tracking -- m_pending_door_clicks and
//    m_world_objects (the latter stays on EverQuest; owner_.m_world_objects)
// ===================================================================

TEST_F(EntitiesComponentTest, GetWorldObjectReturnsNullptrForAnUntrackedDropId) {
    EXPECT_EQ(eq.Entities().GetWorldObject(999999), nullptr);
}

TEST_F(EntitiesComponentTest, ClearWorldObjectsIsSafeToCallOnAnEmptyTable) {
    EXPECT_NO_THROW(eq.Entities().ClearWorldObjects());
    EXPECT_TRUE(eq.Entities().GetWorldObjects().empty());
}

// ===================================================================
// 4. B464 -- the SpawnDoor debug dump names the WRONG door.
//
// The line inside ZoneProcessSpawnDoor()'s loop passed the loop INDEX where
// the door id belongs, so a two-door packet whose doors are 69 and 70 printed
// "Door 0" and "Door 1". It cost T438 a wrong turn: the line looked like it
// showed door_param == 0 for the gfaydark lift, when the record it was
// actually naming was a different door's.
//
// (The board also recorded that the bytes came from the packet base. They did
// not -- `data` is already the per-door record pointer. T448 measured that and
// pins the byte half here too, so a future "fix" cannot regress it.)
//
// The loop index is still printed, as the packet SLOT -- it is genuinely
// useful when a record is malformed -- but it is no longer passed off as the
// door id.
// ===================================================================

namespace {

// Two 80-byte Door_Struct records back to back, as OP_SpawnDoor sends them.
// Offsets 60..67 are doorId, opentype, state_at_spawn, invert_state and the
// four bytes of door_param.
std::vector<uint8_t> TwoDoorRecords() {
    std::vector<uint8_t> buf(160, 0);
    // Door 69: opentype 55, spawn state 1, invert 0, door_param 68.
    buf[60] = 69; buf[61] = 55; buf[62] = 1; buf[63] = 0;
    buf[64] = 68; buf[65] = 0; buf[66] = 0; buf[67] = 0;
    // Door 70: opentype 5, spawn state 0, invert 1, door_param 0.
    buf[80 + 60] = 70; buf[80 + 61] = 5; buf[80 + 62] = 0; buf[80 + 63] = 1;
    buf[80 + 64] = 0; buf[80 + 65] = 0; buf[80 + 66] = 0; buf[80 + 67] = 0;
    return buf;
}

}  // namespace

TEST(DoorSpawnDumpTest, EachLineNamesItsOwnDoorIdNotTheLoopIndex) {
    const std::vector<uint8_t> buf = TwoDoorRecords();

    const std::string first  = eqt::entities::FormatDoorSpawnDump(0, buf.data());
    const std::string second = eqt::entities::FormatDoorSpawnDump(1, buf.data() + 80);

    EXPECT_NE(first.find("Door 69 "), std::string::npos) << first;
    EXPECT_NE(second.find("Door 70 "), std::string::npos) << second;
    // The loop indices must not appear as the door id.
    EXPECT_EQ(first.find("Door 0 "), std::string::npos) << first;
    EXPECT_EQ(second.find("Door 1 "), std::string::npos) << second;
}

TEST(DoorSpawnDumpTest, EachLinePrintsItsOwnRecordsBytesNotTheFirstDoors) {
    const std::vector<uint8_t> buf = TwoDoorRecords();

    const std::string first  = eqt::entities::FormatDoorSpawnDump(0, buf.data());
    const std::string second = eqt::entities::FormatDoorSpawnDump(1, buf.data() + 80);

    // 69=0x45 55=0x37 1 0, then door_param 68 = 0x44 little-endian.
    EXPECT_NE(first.find("45 37 01 00 44 00 00 00"), std::string::npos) << first;
    // 70=0x46 5=0x05 0 1, then door_param 0.
    EXPECT_NE(second.find("46 05 00 01 00 00 00 00"), std::string::npos) << second;
    EXPECT_NE(first, second);
}

}  // namespace
