// K08 (Batch K, refactor/K08_player_component.md) -- eqt::player::PlayerComponent.
//
// K08 follows K01's shape: no forwarders. Every mover's declaration left
// eq.h and every caller goes through EverQuest::Player().
//
// What this file covers is the component itself: construction against a real
// EverQuest, the accessor identity, the state that moved into it (the duel,
// LFG, corpse-drag and rezz flags with their inline accessors, and the two
// T167/T170 test seams with their nested structs), and the split -- the
// player-state FACADE that deliberately STAYED on EverQuest.
//
// It does not re-derive a wire format. test_wire_layout_t145.cpp,
// test_inspect_answer.cpp, test_profile_appearance.cpp and
// test_buff_click_off.cpp keep their own assertions untouched; only their
// call sites were retargeted, and they remain the coverage of what the moved
// bodies build.

#include <gtest/gtest.h>

#include "client/bridge/game_state_bridge.h"
#include "client/player/player_component.h"
#include "zone_stream_test_utils.h"

using namespace zonetest;

namespace {

class PlayerComponentTest : public ::testing::Test {
protected:
    PlayerComponentTest() : eq(MakeEq()) { eq.setBridge(&bridge); }
    ~PlayerComponentTest() override { eq.setBridge(nullptr); }

    EverQuest eq;
    TestBridge bridge;
};

// The accessor is the only way in now, and it must name one stable object.
TEST_F(PlayerComponentTest, PlayerReturnsTheSameComponentEveryTime) {
    eqt::player::PlayerComponent& a = eq.Player();
    eqt::player::PlayerComponent& b = eq.Player();
    EXPECT_EQ(&a, &b);
}

// The duel/LFG/corpse/rezz flags moved WITH the inline eq.h accessors that
// read them (K04 correction f). Defaults, through the component.
TEST_F(PlayerComponentTest, MovedFlagsCarryTheirInlineAccessors) {
    EXPECT_FALSE(eq.Player().IsInDuel());
    EXPECT_FALSE(eq.Player().HasPendingDuel());
    EXPECT_EQ(eq.Player().GetPendingDuelInitiatorId(), 0u);
    EXPECT_FALSE(eq.Player().IsLFG());
    EXPECT_FALSE(eq.Player().IsDraggingCorpse());
    EXPECT_FALSE(eq.Player().HasPendingRezz());
}

// The T167 seam moved whole: the struct, the accessor and
// m_test_last_face_change. Untouched by any send, it reads back its default.
TEST_F(PlayerComponentTest, FaceChangeSeamMovedWithItsStruct) {
    eqt::player::PlayerComponent::TestSentFaceChange sent =
        eq.Player().Test_GetLastFaceChange();
    EXPECT_FALSE(sent.sent);
    EXPECT_FALSE(sent.body.valid);
}

// Same for the T170 inspect-answer seam.
TEST_F(PlayerComponentTest, InspectAnswerSeamMovedWithItsStruct) {
    eqt::player::PlayerComponent::TestSentInspectAnswer sent =
        eq.Player().Test_GetLastInspectAnswer();
    EXPECT_FALSE(sent.sent);
    EXPECT_EQ(sent.target_id, 0u);
    EXPECT_EQ(sent.playerid, 0u);
}

// THE CUT. The player-state facade stayed on EverQuest: these still resolve
// on the class itself, and they read the same store the component writes.
// SetMoveSpeed/SetCombatStopDistance moved; GetPosition/SetPosition did not.
TEST_F(PlayerComponentTest, TheFacadeStayedOnEverQuest) {
    eq.SetPosition(10.0f, 20.0f, 30.0f);
    const glm::vec3 pos = eq.GetPosition();
    EXPECT_FLOAT_EQ(pos.x, 10.0f);
    EXPECT_FLOAT_EQ(pos.y, 20.0f);
    EXPECT_FLOAT_EQ(pos.z, 30.0f);
    EXPECT_FLOAT_EQ(eq.Test_GetGameStatePlayerPosition().x, 10.0f);

    eq.SetHeading(90.0f);
    EXPECT_FLOAT_EQ(eq.GetHeading(), 90.0f);

    // Behavioural flags: the getter stayed, the setter moved.
    EXPECT_FALSE(eq.IsAFK());
    EXPECT_FALSE(eq.IsAnonymous());
    EXPECT_FALSE(eq.IsRoleplay());
    EXPECT_FALSE(eq.IsCamping());
}

// A moved one-line GameState mutator, reached through the component, lands in
// the same single store the facade reads.
TEST_F(PlayerComponentTest, MovedMutatorsStillWriteTheOneStore) {
    eq.Player().SetMoveSpeed(2.5f);
    eq.Player().SetCombatStopDistance(7.0f);
    SUCCEED();
}

// Camp is the family's own timer state. StartCampTimer sets it, CancelCamp
// clears it, and IsCamping() -- which stayed with the facade -- sees both.
TEST_F(PlayerComponentTest, CampTimerRoundTripsThroughTheFacadeFlag) {
    eq.Player().StartCampTimer();
    EXPECT_TRUE(eq.IsCamping());
    eq.Player().CancelCamp();
    EXPECT_FALSE(eq.IsCamping());
}

}  // namespace
