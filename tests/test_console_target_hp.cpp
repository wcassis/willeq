// T319 -- IOutputRenderer::updateCurrentTargetHP() had no production caller.
//
// The interface declared it, ConsoleRenderer and NullRenderer implemented
// it, and a test fixture stubbed it -- and nothing anywhere called it. The
// consequence was not "an unused method": ConsoleRenderer's periodic status
// line (printStatusLine(), driven from update() every
// m_statusUpdateInterval seconds once the zone is ready) prints
// "| Target: <name> <hp>%" from m_targetHpPercent, and the only writer was
// setCurrentTarget(). So in headless mode the target's HP froze at
// whatever TargetChanged happened to carry when the target was acquired,
// for as long as it stayed targeted -- a mob could be fought to 5% and the
// status line would still say 100%.
//
// The fix wires the existing method to the event that actually carries the
// new percentage (EntityStatsChanged), filtered to the current
// target. The verdict for the interface method was WIRE, not delete, and
// this is what makes the fixture stub in
// tests/command_processor_test_fixture.h pin real behaviour.

#include <gtest/gtest.h>

#include <string>

#include "client/output/console_renderer.h"
#include "client/state/event_bus.h"
#include "client/state/game_state.h"

namespace {

using eqt::state::GameEventType;

class ConsoleTargetHpTest : public ::testing::Test {
protected:
    void SetUp() override {
        console.connectToGameState(state);
        console.setStatusUpdateInterval(0.0f);  // every update() prints
        console.setZoneReady(true);
    }

    void acquireTarget(uint16_t spawnId, const std::string& name, uint8_t hp) {
        eqt::state::TargetChangedData data;
        data.spawnId = spawnId;
        data.name = name;
        data.hpPercent = hp;
        data.level = 12;
        state.events().publish(GameEventType::TargetChanged, data);
    }

    void entityStats(uint16_t spawnId, uint8_t hp) {
        eqt::state::EntityStatsChangedData data;
        data.spawnId = spawnId;
        data.hpPercent = hp;
        data.curMana = 0;
        data.maxMana = 0;
        state.events().publish(GameEventType::EntityStatsChanged, data);
    }

    // One status-line print, captured.
    std::string statusLine() {
        ::testing::internal::CaptureStdout();
        console.update(1.0f);
        return ::testing::internal::GetCapturedStdout();
    }

    eqt::state::GameState state;
    eqt::output::ConsoleRenderer console;
};

// The defect: the status line must follow the target's HP down, not stay
// at the value TargetChanged carried. Red before this unit -- prints 100%.
TEST_F(ConsoleTargetHpTest, StatusLineFollowsTheTargetsHpDown) {
    acquireTarget(42, "Grimjaw", 100);
    ASSERT_NE(statusLine().find("Target: Grimjaw 100%"), std::string::npos);

    entityStats(42, 37);
    EXPECT_NE(statusLine().find("Target: Grimjaw 37%"), std::string::npos);

    entityStats(42, 4);
    EXPECT_NE(statusLine().find("Target: Grimjaw 4%"), std::string::npos);
}

// Every entity in the zone publishes EntityStatsChanged; only the current
// target's is ours to take.
TEST_F(ConsoleTargetHpTest, AnotherEntitysStatsDoNotTouchTheTarget) {
    acquireTarget(42, "Grimjaw", 90);
    entityStats(99, 3);
    EXPECT_NE(statusLine().find("Target: Grimjaw 90%"), std::string::npos);
}

// With nothing targeted there is no target section at all, and a stats
// event for some entity must not conjure one.
TEST_F(ConsoleTargetHpTest, NoTargetMeansNoTargetSection) {
    entityStats(99, 3);
    EXPECT_EQ(statusLine().find("Target: "), std::string::npos);
}

}  // namespace
