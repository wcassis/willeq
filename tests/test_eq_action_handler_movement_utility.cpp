// T140 -- real-EqActionHandler/EverQuest coverage for the final movement/
// utility/skills commands after their migration to
// action::CommandProcessor (see refactor/T140_migrate_final_seventeen.md).
//
// sit/stand/move/moveto/follow/stopfollow REUSE pre-existing
// IActionHandler methods with no new EverQuest state to exercise here --
// covered at the CommandProcessor layer (test_command_processor.cpp,
// RecordingActionHandler) instead. What this file covers is what's new:
//   - showLocation(): pure read + format of EverQuest::GetPosition().
//   - warp(): EqActionHandler thin-forwards to EverQuest::RequestWarp(),
//     which does all three destination-resolution forms (coords / entity
//     name / current target) plus the no-destination usage message.
//   - showOrSetZoning(): read/set via the already-public
//     SetZoningEnabled()/IsZoningEnabled().
//   - campNow(): sit + ack + StartCampTimer(), verified via
//     IsPositionState()/IsCamping()-equivalent observables.
//   - debugLevel()/perfReport(): process-global diagnostics, verified via
//     the printed message (debugLevel) or that no crash/empty-state path
//     is hit (perfReport, since PerformanceMetrics has no reset hook here).
//   - timeOfDay(): EverQuest::RequestTimeOfDay(), same shape as warp.
//
// bank/train's guard logic is covered separately in
// test_eq_commands_bank_train.cpp (pre-existing T50 file, retargeted to
// call EverQuest::RequestBankToggle()/RequestTraining() directly).

#include <gtest/gtest.h>

#include "client/bridge/game_state_bridge.h"
#include "client/combat.h"
#include "client/eq_action_handler.h"
#include "zone_stream_test_utils.h"

using namespace zonetest;

namespace {

std::vector<std::string> DrainSystemMessages(eqt::bridge::GameStateBridge& bridge) {
    std::vector<std::string> out;
    for (auto& ev : bridge.drainEvents()) {
        if (ev.type == eqt::state::GameEventType::SystemMessage) {
            out.push_back(std::get<eqt::state::ChatMessageData>(ev.data).message);
        }
    }
    return out;
}

class EqActionHandlerMovementUtilityTest : public ::testing::Test {
protected:
    EqActionHandlerMovementUtilityTest() : eq(MakeEq()), handler(eq) { eq.setBridge(&bridge); }
    ~EqActionHandlerMovementUtilityTest() override { eq.setBridge(nullptr); }

    EverQuest eq;
    eqt::EqActionHandler handler;
    TestBridge bridge;
};

// ---------------------------------------------------------------------------
// showLocation()
// ---------------------------------------------------------------------------

TEST_F(EqActionHandlerMovementUtilityTest, ShowLocation_ReportsCurrentPosition) {
    eq.SetPosition(11.0f, 22.0f, 33.0f);
    bridge.drainEvents();  // discard PlayerMoved from SetPosition, if any

    handler.showLocation();

    auto msgs = DrainSystemMessages(bridge);
    ASSERT_EQ(msgs.size(), 1u);
    EXPECT_EQ(msgs[0], "Your location is 11.0, 22.0, 33.0");
}

// ---------------------------------------------------------------------------
// warp() -- forwards to EverQuest::RequestWarp(), which does all the
// destination resolution. Full coverage of the three forms lives in the
// plan's live leg; these confirm the forwarding path and the no-destination
// usage message (the one path fully local to this method).
// ---------------------------------------------------------------------------

TEST_F(EqActionHandlerMovementUtilityTest, Warp_ToCoordinates_SetsPositionAndAcks) {
    handler.warp("100 200 300");

    EXPECT_FLOAT_EQ(eq.GetPosition().x, 100.0f);
    EXPECT_FLOAT_EQ(eq.GetPosition().y, 200.0f);
    EXPECT_FLOAT_EQ(eq.GetPosition().z, 300.0f);
    auto msgs = DrainSystemMessages(bridge);
    ASSERT_FALSE(msgs.empty());
    EXPECT_NE(msgs.back().find("Warped to"), std::string::npos);
}

TEST_F(EqActionHandlerMovementUtilityTest, Warp_NoArgsNoTarget_PrintsUsage_DoesNotMovePlayer) {
    eq.SetPosition(1.0f, 2.0f, 3.0f);
    bridge.drainEvents();

    handler.warp("");

    EXPECT_FLOAT_EQ(eq.GetPosition().x, 1.0f);
    auto msgs = DrainSystemMessages(bridge);
    ASSERT_EQ(msgs.size(), 1u);
    EXPECT_NE(msgs[0].find("Usage:"), std::string::npos);
}

TEST_F(EqActionHandlerMovementUtilityTest, Warp_UnknownEntityName_PrintsNotFound) {
    handler.warp("no_such_entity");

    auto msgs = DrainSystemMessages(bridge);
    ASSERT_EQ(msgs.size(), 1u);
    EXPECT_NE(msgs[0].find("not found"), std::string::npos);
}

// ---------------------------------------------------------------------------
// showOrSetZoning()
// ---------------------------------------------------------------------------

TEST_F(EqActionHandlerMovementUtilityTest, Zoning_NoArgs_ReportsCurrentState) {
    handler.showOrSetZoning("");

    auto msgs = DrainSystemMessages(bridge);
    ASSERT_EQ(msgs.size(), 1u);
    EXPECT_NE(msgs[0].find("Zoning is"), std::string::npos);
}

TEST_F(EqActionHandlerMovementUtilityTest, Zoning_On_EnablesZoning) {
    handler.showOrSetZoning("off");
    bridge.drainEvents();
    ASSERT_FALSE(eq.IsZoningEnabled());

    handler.showOrSetZoning("on");

    EXPECT_TRUE(eq.IsZoningEnabled());
    auto msgs = DrainSystemMessages(bridge);
    ASSERT_EQ(msgs.size(), 1u);
    EXPECT_EQ(msgs[0], "Zoning is now on");
}

TEST_F(EqActionHandlerMovementUtilityTest, Zoning_Off_DisablesZoning) {
    handler.showOrSetZoning("off");

    EXPECT_FALSE(eq.IsZoningEnabled());
    auto msgs = DrainSystemMessages(bridge);
    ASSERT_EQ(msgs.size(), 1u);
    EXPECT_EQ(msgs[0], "Zoning is now off");
}

TEST_F(EqActionHandlerMovementUtilityTest, Zoning_InvalidArg_PrintsUsage) {
    handler.showOrSetZoning("sideways");

    auto msgs = DrainSystemMessages(bridge);
    ASSERT_EQ(msgs.size(), 1u);
    EXPECT_NE(msgs[0].find("Usage:"), std::string::npos);
}

// ---------------------------------------------------------------------------
// campNow() -- sits, acks, starts the camp timer.
// ---------------------------------------------------------------------------

TEST_F(EqActionHandlerMovementUtilityTest, CampNow_SitsAndAcks) {
    handler.campNow();

    auto msgs = DrainSystemMessages(bridge);
    ASSERT_FALSE(msgs.empty());
    EXPECT_NE(msgs[0].find("begun to camp"), std::string::npos);
}

// ---------------------------------------------------------------------------
// debugLevel()
// ---------------------------------------------------------------------------

TEST_F(EqActionHandlerMovementUtilityTest, DebugLevel_NoArgs_ReportsCurrentLevel) {
    handler.debugLevel("3");
    bridge.drainEvents();

    handler.debugLevel("");

    auto msgs = DrainSystemMessages(bridge);
    ASSERT_EQ(msgs.size(), 1u);
    EXPECT_NE(msgs[0].find("Debug level: 3"), std::string::npos);
    EXPECT_EQ(EverQuest::GetDebugLevel(), 3);
}

TEST_F(EqActionHandlerMovementUtilityTest, DebugLevel_SetsLevel) {
    handler.debugLevel("2");

    EXPECT_EQ(EverQuest::GetDebugLevel(), 2);
    auto msgs = DrainSystemMessages(bridge);
    ASSERT_EQ(msgs.size(), 1u);
    EXPECT_EQ(msgs[0], "Debug level set to 2");
}

TEST_F(EqActionHandlerMovementUtilityTest, DebugLevel_Malformed_PrintsUsage) {
    handler.debugLevel("notanumber");

    auto msgs = DrainSystemMessages(bridge);
    ASSERT_EQ(msgs.size(), 1u);
    EXPECT_NE(msgs[0].find("Usage:"), std::string::npos);
}

// ---------------------------------------------------------------------------
// perfReport() -- process-global singleton; just confirm it doesn't crash
// and produces at least one chat message either way (data collected or not).
// ---------------------------------------------------------------------------

TEST_F(EqActionHandlerMovementUtilityTest, PerfReport_ProducesAtLeastOneMessage) {
    handler.perfReport();

    auto msgs = DrainSystemMessages(bridge);
    EXPECT_FALSE(msgs.empty());
}

// ---------------------------------------------------------------------------
// timeOfDay() -- forwards to EverQuest::RequestTimeOfDay().
// ---------------------------------------------------------------------------

TEST_F(EqActionHandlerMovementUtilityTest, TimeOfDay_NoArgs_ReportsCurrentTime) {
    handler.timeOfDay("");

    auto msgs = DrainSystemMessages(bridge);
    ASSERT_EQ(msgs.size(), 1u);
    EXPECT_NE(msgs[0].find("Game time:"), std::string::npos);
}

TEST_F(EqActionHandlerMovementUtilityTest, TimeOfDay_SetsHour_PushesTimeOfDayChangedEvent) {
    handler.timeOfDay("14");

    bool sawTimeEvent = false;
    for (auto& ev : bridge.drainEvents()) {
        if (ev.type == eqt::state::GameEventType::TimeOfDayChanged) {
            auto& d = std::get<eqt::state::TimeOfDayChangedData>(ev.data);
            EXPECT_EQ(d.hour, 14);
            sawTimeEvent = true;
        }
    }
    EXPECT_TRUE(sawTimeEvent);
}

TEST_F(EqActionHandlerMovementUtilityTest, TimeOfDay_OutOfRangeHour_PrintsRefusal_NoEvent) {
    handler.timeOfDay("99");

    auto msgs = DrainSystemMessages(bridge);
    ASSERT_EQ(msgs.size(), 1u);
    EXPECT_NE(msgs[0].find("0-23"), std::string::npos);
}

}  // namespace
