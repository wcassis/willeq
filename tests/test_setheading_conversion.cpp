// T90: regression tests for EqActionHandler::setHeading()'s double-
// conversion bug.
//
// The defect (see refactor/T90_setheading_double_conversion.md, found by
// T84's two-client observation): setHeading() converted degrees -> EQ
// 0-512 heading units, then passed the CONVERTED value to
// EverQuest::SetHeading(), which stores its argument raw into m_heading --
// a field every other site (PlayerProfile zone-in, eq_movement.cpp's
// outbound server_heading = 90 - m_heading, the self ClientUpdate echo)
// treats as plain 0-360 degrees. Net effect: setHeading(90) produced
// m_heading == 128 (90 * 512/360), not 90.
//
// T89 established that everything else in the heading stack (the outbound
// server_heading = 90 - m_heading convention, and the inbound
// m_heading = 90 - server_heading zone-in offset) is CORRECT and must not
// be touched. This file tests only EqActionHandler::setHeading() and the
// /face path that shares it.
//
// Uses the same headless EverQuest + EqActionHandler construction as
// test_position_frame_roundtrip.cpp (T88) -- no network, no graphics.

#include <gtest/gtest.h>

#include "client/action/action_dispatcher.h"
#include "client/eq.h"
#include "client/eq_action_handler.h"
#include "client/state/game_state.h"

using namespace eqt;
using namespace eqt::action;
using eqt::state::GameState;

namespace {

// ---------------------------------------------------------------------
// Part 1: EqActionHandler::setHeading() must leave m_heading in DEGREES,
// not EQ 512-units.
// ---------------------------------------------------------------------

TEST(SetHeadingConversion, SetHeading90_LeavesHeadingAt90NotDoubleConverted) {
    EverQuest eq("127.0.0.1", 0, "test", "test", "test", "Testchar");
    eq.Net().Test_SetBypassSendGuardForSeam(true);
    eq.Test_SetMySpawnIdForSeam(460);
    EqActionHandler handler(eq);

    handler.setHeading(90.0f);

    // Before the T90 fix, this was 128.0 (90 * 512/360) -- the double
    // conversion this test exists to catch.
    EXPECT_FLOAT_EQ(eq.GetHeading(), 90.0f);
}

TEST(SetHeadingConversion, SetHeading225_LeavesHeadingAt225NotDoubleConverted) {
    EverQuest eq("127.0.0.1", 0, "test", "test", "test", "Testchar");
    eq.Net().Test_SetBypassSendGuardForSeam(true);
    eq.Test_SetMySpawnIdForSeam(460);
    EqActionHandler handler(eq);

    handler.setHeading(225.0f);

    // Before the fix: 225 * 512/360 = 320.0 (T84/T90's exact measured
    // live value).
    EXPECT_FLOAT_EQ(eq.GetHeading(), 225.0f);
}

TEST(SetHeadingConversion, SetHeading0_IsZero) {
    EverQuest eq("127.0.0.1", 0, "test", "test", "test", "Testchar");
    eq.Net().Test_SetBypassSendGuardForSeam(true);
    eq.Test_SetMySpawnIdForSeam(460);
    EqActionHandler handler(eq);

    handler.setHeading(0.0f);

    EXPECT_FLOAT_EQ(eq.GetHeading(), 0.0f);
}

TEST(SetHeadingConversion, SetHeading359_IsUnchanged) {
    EverQuest eq("127.0.0.1", 0, "test", "test", "test", "Testchar");
    eq.Net().Test_SetBypassSendGuardForSeam(true);
    eq.Test_SetMySpawnIdForSeam(460);
    EqActionHandler handler(eq);

    handler.setHeading(359.0f);

    EXPECT_FLOAT_EQ(eq.GetHeading(), 359.0f);
}

// Wrap case: negative input normalizes into [0, 360).
TEST(SetHeadingConversion, SetHeadingNegative10_WrapsTo350) {
    EverQuest eq("127.0.0.1", 0, "test", "test", "test", "Testchar");
    eq.Net().Test_SetBypassSendGuardForSeam(true);
    eq.Test_SetMySpawnIdForSeam(460);
    EqActionHandler handler(eq);

    handler.setHeading(-10.0f);

    EXPECT_FLOAT_EQ(eq.GetHeading(), 350.0f);
}

// Wrap case: input >= 360 normalizes back into [0, 360).
TEST(SetHeadingConversion, SetHeading400_WrapsTo40) {
    EverQuest eq("127.0.0.1", 0, "test", "test", "test", "Testchar");
    eq.Net().Test_SetBypassSendGuardForSeam(true);
    eq.Test_SetMySpawnIdForSeam(460);
    EqActionHandler handler(eq);

    handler.setHeading(400.0f);

    EXPECT_FLOAT_EQ(eq.GetHeading(), 40.0f);
}

// ---------------------------------------------------------------------
// Part 2: /face shares EqActionHandler::setHeading() via
// ActionDispatcher::faceEntity(). Wire the REAL handler (not the
// RecordingActionHandler fake test_action_dispatcher.cpp uses) so this
// exercises the actual double-conversion code path end to end.
// ---------------------------------------------------------------------

TEST(SetHeadingConversion, FaceEntity_RealHandler_ProducesSaneHeadingNotScaled) {
    EverQuest eq("127.0.0.1", 0, "test", "test", "test", "Testchar");
    eq.Net().Test_SetBypassSendGuardForSeam(true);
    eq.Test_SetMySpawnIdForSeam(460);
    EqActionHandler handler(eq);

    GameState state;
    state.world().setZoneConnected(true);
    state.player().setPosition(0.0f, 0.0f, 0.0f);

    // Entity due north of the player (dx=0, dy>0) -> calculateHeadingTo
    // (atan2(dx,dy)) yields heading 0.
    eqt::state::Entity e;
    e.spawnId = 5;
    e.name = "Bob";
    e.x = 0.0f;
    e.y = 10.0f;
    e.z = 0.0f;
    state.entities().addEntity(e);

    ActionDispatcher dispatcher(state);
    dispatcher.setActionHandler(&handler);

    auto result = dispatcher.faceEntity("Bob");
    ASSERT_TRUE(result.success);

    // A sane heading toward due-north is 0 degrees. Before the fix, the
    // double conversion would have scaled whatever calculateHeadingTo()
    // produced by 512/360, so a non-zero source heading would land far
    // outside [0, 360) in degree terms. At exactly north (0 deg) the
    // scaled result is coincidentally still 0, so also check a non-zero
    // direction below.
    EXPECT_FLOAT_EQ(eq.GetHeading(), 0.0f);
}

TEST(SetHeadingConversion, FaceEntity_NonCardinalDirection_HeadingStaysInDegreeRange) {
    EverQuest eq("127.0.0.1", 0, "test", "test", "test", "Testchar");
    eq.Net().Test_SetBypassSendGuardForSeam(true);
    eq.Test_SetMySpawnIdForSeam(460);
    EqActionHandler handler(eq);

    GameState state;
    state.world().setZoneConnected(true);
    state.player().setPosition(0.0f, 0.0f, 0.0f);

    // Entity to the east (dx>0, dy=0) -> calculateHeadingTo (atan2(dx,dy))
    // yields heading 90.
    eqt::state::Entity e;
    e.spawnId = 6;
    e.name = "Alice";
    e.x = 10.0f;
    e.y = 0.0f;
    e.z = 0.0f;
    state.entities().addEntity(e);

    ActionDispatcher dispatcher(state);
    dispatcher.setActionHandler(&handler);

    auto result = dispatcher.faceEntity("Alice");
    ASSERT_TRUE(result.success);

    // Before the fix: 90 * 512/360 = 128.0 -- exactly T84's measured live
    // defect value. After the fix: 90.
    EXPECT_FLOAT_EQ(eq.GetHeading(), 90.0f);
}

}  // namespace
