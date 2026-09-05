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

#include <algorithm>
#include <cmath>
#include <fstream>
#include <sstream>
#include <string>

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

// ---------------------------------------------------------------------------
// T347 (B029): the water-surface Z estimate is gone and must stay gone.
//
// UpdateWaterState() computed `waterSurfaceZ` in its OnSurface arm and stashed
// it in MovementComponent::m_water_surface_z on every water-state change. The
// member had ZERO readers tree-wide, and the SwimmingStateChanged event the
// same function publishes carries only {isSwimming, swimSpeed, isLevitating},
// so nothing downstream could observe the value either -- it was a write into
// a hole. There is no runtime handle on a deleted member, so the net is a
// source guard: reintroducing the field (or the local that fed it) without a
// reader fails here, which is the review moment.
// ---------------------------------------------------------------------------

#ifndef WILLEQ_REPO_ROOT
#error "WILLEQ_REPO_ROOT must be defined (tests/CMakeLists.txt)"
#endif

std::string ReadRepoFile(const char* relative) {
    std::ifstream in(std::string(WILLEQ_REPO_ROOT) + "/" + relative);
    EXPECT_TRUE(in.is_open()) << relative;
    std::stringstream buf;
    buf << in.rdbuf();
    return buf.str();
}

TEST(MovementWaterStateGuard, NoUnreadWaterSurfaceHeightIsStored) {
    const std::string header = ReadRepoFile("include/client/movement/movement_component.h");
    ASSERT_GT(header.size(), 1000u) << "header unreadable -- the guard would pass vacuously";
    EXPECT_EQ(header.find("m_water_surface_z"), std::string::npos)
        << "MovementComponent declares a water-surface member again; B029 deleted "
           "it because nothing read it. Add the reader (or an event field) first.";

    const std::string tu = ReadRepoFile("src/client/movement/movement_state.cpp");
    ASSERT_NE(tu.find("UpdateWaterState"), std::string::npos)
        << "TU unreadable or renamed -- the guard would pass vacuously";
    EXPECT_EQ(tu.find("waterSurfaceZ"), std::string::npos)
        << "UpdateWaterState() computes a water-surface Z again; B029 deleted it "
           "because the value reached no consumer.";
}

// The pushEvent payload is what B029 checked before deleting: this is the ONLY
// channel by which UpdateWaterState() tells anyone about the water. If a
// surface height is ever wanted, it belongs here, where a consumer can see it.
TEST(MovementWaterStateGuard, SwimmingEventCarriesNoSurfaceHeight) {
    eqt::state::SwimmingStateChangedData data;
    data.isSwimming = true;
    data.swimSpeed = 1.5f;
    data.isLevitating = false;
    EXPECT_TRUE(data.isSwimming);
    EXPECT_FLOAT_EQ(data.swimSpeed, 1.5f);
    EXPECT_FALSE(data.isLevitating);

    // The field list itself, read from the header rather than guessed from
    // sizeof() (which padding makes a lie): exactly three members, none of
    // them a height.
    const std::string bus = ReadRepoFile("include/client/state/event_bus.h");
    const size_t at = bus.find("struct SwimmingStateChangedData {");
    ASSERT_NE(at, std::string::npos) << "payload struct renamed -- guard is vacuous";
    const size_t end = bus.find("};", at);
    ASSERT_NE(end, std::string::npos);
    const std::string body = bus.substr(at, end - at);
    EXPECT_EQ(body.find("urface"), std::string::npos)
        << "the swim event gained a surface field; B029's deletion can be revisited "
           "-- with a reader this time. Body:\n" << body;
    EXPECT_EQ(std::count(body.begin(), body.end(), ';'), 3)
        << "the swim event's payload is no longer exactly {isSwimming, swimSpeed, "
           "isLevitating}. Body:\n" << body;
}

}  // namespace

// =====================================================================
// T359 (board B003d) -- combat movement and Follow track a SPAWN ID.
//
// T356 fixed which entity /moveto and /follow START on (the dispatcher now
// resolves once, ranked nearest-first, and passes an id). It left the layer
// below: Follow stored only the target's NAME and UpdateMovement re-matched
// that string over the entity map every tick, and combat movement stored only
// the name and re-resolved it through EntitiesComponent::FindEntityByName --
// an unranked case-insensitive PREFIX match over a std::map keyed by spawn id,
// i.e. "the lowest spawn id that starts with this name wins".
//
// So both fixtures below spawn two identically-named mobs with the NEARER one
// spawned SECOND, giving it the HIGHER spawn id: the id-keyed reads must track
// the near one, while the name re-match picks the far one. Both cases run two
// UpdateMovement ticks, because the per-tick re-resolution is the defect --
// a single-call assertion would not see it.
// =====================================================================

namespace {

struct TwoSameNamedBeetles {
    const Entity* near_beetle = nullptr;
    const Entity* far_beetle = nullptr;
};

// far = spawn 10 (LOWER id, what the prefix matcher returns), near = spawn 20.
TwoSameNamedBeetles SpawnTwoBeetles(EverQuest& eq) {
    // NewSpawnPayloadAt takes SERVER coords; entity.x <- server_y (INVERSEXY).
    ReplayZone(eq, HC_OP_NewSpawn, NewSpawnPayloadAt(10, "a_fire_beetle", 0, 800, 0));
    ReplayZone(eq, HC_OP_NewSpawn, NewSpawnPayloadAt(20, "a_fire_beetle", 0, 240, 0));

    TwoSameNamedBeetles out;
    const auto& entities = eq.GetEntities();
    auto f = entities.find(10);
    auto n = entities.find(20);
    EXPECT_NE(f, entities.end());
    EXPECT_NE(n, entities.end());
    if (f != entities.end()) out.far_beetle = &f->second;
    if (n != entities.end()) out.near_beetle = &n->second;
    return out;
}

}  // namespace

// The pre-condition every assertion below rests on: a first-match-wins name
// resolver picks the FAR beetle, so tracking the near one cannot be a
// coincidence of iteration order. T359 asserted this by calling
// EntitiesComponent::FindEntityByName directly; T362 (B070) deleted that
// resolver with its last caller, so the pre-condition is now stated over the
// fixture it was always really about -- the far beetle carries the LOWER
// spawn id and so sorts first in the entity map.
TEST_F(MovementComponentTest, TheFarBeetleIsTheOneAFirstMatchResolverWouldPick) {
    TwoSameNamedBeetles b = SpawnTwoBeetles(eq);
    ASSERT_NE(b.near_beetle, nullptr);
    ASSERT_NE(b.far_beetle, nullptr);
    EXPECT_LT(std::abs(b.near_beetle->x), std::abs(b.far_beetle->x));
    EXPECT_EQ(b.far_beetle->spawn_id, 10u);
    EXPECT_EQ(b.near_beetle->spawn_id, 20u);
    EXPECT_LT(b.far_beetle->spawn_id, b.near_beetle->spawn_id);
    EXPECT_EQ(eq.GetEntities().begin()->second.spawn_id, 10u);
}

TEST_F(MovementComponentTest, FollowTracksTheSpawnIdAcrossTicksNotTheNameMatch) {
    TwoSameNamedBeetles b = SpawnTwoBeetles(eq);
    ASSERT_NE(b.near_beetle, nullptr);
    ASSERT_NE(b.far_beetle, nullptr);
    const float near_x = b.near_beetle->x;
    const float far_x = b.far_beetle->x;

    eq.Movement().Follow(20);
    EXPECT_EQ(eq.GetGameState().player().followTargetId(), 20u);

    eq.Movement().UpdateMovement();
    eq.Movement().UpdateMovement();

    // The follow tick sets the movement target straight onto the entity.
    EXPECT_NEAR(eq.GetGameState().player().targetX(), near_x, 5.0f)
        << "follow tick latched the far beetle (" << far_x << ") -- it re-matched the NAME";
}

TEST_F(MovementComponentTest, CombatMovementTracksTheSpawnIdAcrossTicksNotTheNameMatch) {
    TwoSameNamedBeetles b = SpawnTwoBeetles(eq);
    ASSERT_NE(b.near_beetle, nullptr);
    ASSERT_NE(b.far_beetle, nullptr);
    const float near_x = b.near_beetle->x;
    const float far_x = b.far_beetle->x;

    eq.Movement().StartCombatMovement(20);
    EXPECT_EQ(eq.GetGameState().combat().combatTargetId(), 20u);
    EXPECT_TRUE(eq.GetGameState().combat().inCombatMovement());

    eq.Movement().UpdateMovement();
    eq.Movement().UpdateMovement();

    // Combat movement stops short of the target, so the destination sits
    // between the player and the near beetle -- nowhere near the far one.
    EXPECT_LT(eq.GetGameState().player().targetX(), (near_x + far_x) * 0.5f)
        << "combat movement latched the far beetle (" << far_x << ") -- it re-resolved the NAME";
    EXPECT_NEAR(eq.GetGameState().player().targetX(), near_x, 10.0f);
}

// The despawn path: a follow target that vanishes stops driving movement, and
// does it without crashing on a dangling id. Pre-T359 a name miss simply fell
// through the scan; the id miss must fall through the same way.
TEST_F(MovementComponentTest, FollowTargetThatDespawnsJustStopsDrivingMovement) {
    SpawnTwoBeetles(eq);
    eq.Movement().Follow(20);
    ASSERT_TRUE(eq.GetGameState().player().isFollowing());

    eq.Movement().UpdateMovement();
    const float driven_x = eq.GetGameState().player().targetX();

    eq.Context().entities.erase(20);
    ASSERT_EQ(eq.GetEntities().count(20), 0u);

    EXPECT_NO_THROW(eq.Movement().UpdateMovement());
    EXPECT_FLOAT_EQ(eq.GetGameState().player().targetX(), driven_x);
}
