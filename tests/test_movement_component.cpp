// K07 (Batch K, refactor/K07_movement_component.md) -- eqt::movement::MovementComponent.
//
// K07 follows K01's shape: no forwarders. EverQuest no longer declares a
// single movement method; every caller goes through EverQuest::Movement().
// This file covers the component itself -- construction against a real
// EverQuest, the accessor identity, the state that moved into it
// (m_use_pathfinding via SetPathfinding/IsPathfindingEnabled, the path
// cluster via the new ResetPath/ResetPathfinder/ForceNextPositionUpdate the
// zone lifecycle now calls, and the T88 seam), and the two moved geometry
// helpers other families still call.
//
// The T234/T237 transform tests, test_position_frame_roundtrip.cpp and
// test_zone_position_convention.cpp keep their own assertions untouched --
// only their call sites were retargeted, and THEY remain the coverage of what
// the moved bodies compute. Nothing here re-derives a formula.

#include <gtest/gtest.h>

#include "client/bridge/game_state_bridge.h"
#include "client/movement/movement_component.h"
#include "zone_stream_test_utils.h"

#include <cmath>

using namespace zonetest;

namespace {

class MovementComponentTest : public ::testing::Test {
protected:
    MovementComponentTest() : eq(MakeEq()) { eq.setBridge(&bridge); }
    ~MovementComponentTest() override { eq.setBridge(nullptr); }

    EverQuest eq;
    TestBridge bridge;
};

// The accessor is the only way in now, and it must name one stable object.
TEST_F(MovementComponentTest, MovementReturnsTheSameComponentEveryTime) {
    eqt::movement::MovementComponent& a = eq.Movement();
    eqt::movement::MovementComponent& b = eq.Movement();
    EXPECT_EQ(&a, &b);
}

// m_use_pathfinding moved with the two inline accessors that read it
// (K04 correction f). Default and round-trip, through the component.
TEST_F(MovementComponentTest, PathfindingFlagMovedWithItsAccessors) {
    EXPECT_TRUE(eq.Movement().IsPathfindingEnabled());
    eq.Movement().SetPathfinding(false);
    EXPECT_FALSE(eq.Movement().IsPathfindingEnabled());
    eq.Movement().SetPathfinding(true);
    EXPECT_TRUE(eq.Movement().IsPathfindingEnabled());
}

// The path cluster moved into the component; eq_zone.cpp used to clear the
// members directly and now asks. These three are reachable and side-effect
// free on a client that never loaded a zone.
TEST_F(MovementComponentTest, ZoneLifecycleResetsAreReachable) {
    eq.Movement().ResetPath();
    eq.Movement().ResetPathfinder();
    eq.Movement().ForceNextPositionUpdate();
    SUCCEED();
}

// Both geometry helpers stayed public because other families call them
// (CalculateDistance from eq_entities_appearance.cpp, CalculateDistance2D
// from eq_bridge_intents.cpp and eq_commands_spells.cpp).
TEST_F(MovementComponentTest, DistanceHelpersMovedAndStillCompute) {
    EXPECT_FLOAT_EQ(eq.Movement().CalculateDistance2D(0.0f, 0.0f, 3.0f, 4.0f), 5.0f);
    EXPECT_FLOAT_EQ(eq.Movement().CalculateDistance(0.0f, 0.0f, 0.0f, 0.0f, 3.0f, 4.0f), 5.0f);
}

// The T88 seam moved whole: the struct, the accessor and m_test_last_sent_update.
// Untouched by any send, it reads back its zero-initialised default.
TEST_F(MovementComponentTest, PositionUpdateSeamMovedWithItsStruct) {
    eqt::movement::MovementComponent::TestSentPositionUpdate sent =
        eq.Movement().Test_GetLastComputedPositionUpdate();
    EXPECT_FLOAT_EQ(sent.x_pos, 0.0f);
    EXPECT_FLOAT_EQ(sent.y_pos, 0.0f);
    EXPECT_FLOAT_EQ(sent.z_pos, 0.0f);
    EXPECT_EQ(sent.heading_scaled, 0);
}

// StopMovement is public because eq_zone.cpp and eq_player_social.cpp call it.
// With no zone connection it is a no-op that must still be reachable and must
// leave the player not-moving.
TEST_F(MovementComponentTest, StopMovementIsReachableThroughTheAccessor) {
    eq.Movement().StopMovement();
    EXPECT_FALSE(eq.IsMoving());
}

}  // namespace
