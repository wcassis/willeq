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
// test_zone_entities.cpp / test_zone_connection_flow.cpp, whose call sites
// route through DispatchZonePacket -> m_entities_component-> unchanged.

#include <gtest/gtest.h>

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
// 2. FindEntityByName / ListEntities -- the entity-lookup helpers, now on
//    the component, still read the one shared m_entities map.
// ===================================================================

TEST_F(EntitiesComponentTest, FindEntityByNameReturnsNullptrWhenNoSpawnMatches) {
    EXPECT_EQ(eq.Entities().FindEntityByName("NoSuchMob"), nullptr);
}

TEST_F(EntitiesComponentTest, FindEntityByNameFindsASpawnInjectedViaZoneProcessNewSpawn) {
    auto payload = NewSpawnPayloadAt(1234, "A_Test_Mob", 100, 200, 5);
    ASSERT_NO_THROW(ReplayZone(eq, HC_OP_NewSpawn, payload));

    Entity* found = eq.Entities().FindEntityByName("A_Test_Mob");
    ASSERT_NE(found, nullptr);
    EXPECT_EQ(found->spawn_id, 1234u);
    // The lookup is case-insensitive and treats spaces/underscores alike.
    EXPECT_EQ(eq.Entities().FindEntityByName("a_test_mob"), found);
}

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
    EXPECT_TRUE(eq.GetWorldObjects().empty());
}

}  // namespace
