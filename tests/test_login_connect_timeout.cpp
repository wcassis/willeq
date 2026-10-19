// B1099 (GitLab #419): the pre-zone connect is bounded.
//
// Before B1099 a login server that never answered left the client at
// LoadingPhase::LOGIN_CONNECTING ("Connecting to login server...") forever
// (Will, 2026-10-19, the XU4 against a stopped akk-stack). These tests pin the
// bound over the pure LoginConnectWatchdog and over a headless EverQuest whose
// login host is a dead loopback port, with an explicit clock: timeout -> the
// session torn down -> the loading screen's "Could not connect to login
// server" -> the could-not-connect record main() turns into exit 3.

#include <gtest/gtest.h>

#include "client/bridge/bridge_component.h"
#include "client/eq.h"
#include "client/fatal_exit.h"
#include "client/login_world/login_connect_watchdog.h"
#include "client/login_world/login_world_component.h"
#include "client/scenario/scenario_runner.h"

#include <chrono>
#include <string>

namespace {

using Clock = std::chrono::steady_clock;
using eqt::login_world::LoginConnectWatchdog;
using Action = LoginConnectWatchdog::Action;
namespace stage = eqt::login_world::login_stage;
using std::chrono::milliseconds;

constexpr int kT = eqt::login_world::kLoginConnectTimeoutMs;

// ---------------------------------------------------------------------------
// The pure state machine
// ---------------------------------------------------------------------------

TEST(LoginConnectWatchdog, UnarmedDoesNothing) {
    LoginConnectWatchdog w;
    const auto t0 = Clock::time_point{} + std::chrono::hours(1);
    EXPECT_EQ(w.Tick(stage::kLoginConnecting, t0 + milliseconds(10 * kT)), Action::None);
    EXPECT_FALSE(w.GaveUp());
}

TEST(LoginConnectWatchdog, TimeoutGivesUpOnceAndNamesTheStage) {
    LoginConnectWatchdog w;
    const auto t0 = Clock::time_point{} + std::chrono::hours(1);
    w.Arm(stage::kLoginConnecting, t0, 1000);
    EXPECT_TRUE(w.Armed());
    EXPECT_EQ(w.Tick(stage::kLoginConnecting, t0 + milliseconds(999)), Action::None);
    EXPECT_EQ(w.Tick(stage::kLoginConnecting, t0 + milliseconds(1000)), Action::GiveUp);
    EXPECT_FALSE(w.Armed());
    EXPECT_TRUE(w.GaveUp());
    EXPECT_EQ(w.Stage(), stage::kLoginConnecting);
    EXPECT_EQ(w.Tick(stage::kLoginConnecting, t0 + milliseconds(9000)), Action::None) << "gives up once";
}

TEST(LoginConnectWatchdog, ForwardMovementRebaselinesBackwardDoesNot) {
    LoginConnectWatchdog w;
    const auto t0 = Clock::time_point{} + std::chrono::hours(1);
    w.Arm(stage::kLoginConnecting, t0, 1000);
    // Session up at 800 ms: the authentication stage gets its own window.
    EXPECT_EQ(w.Tick(stage::kLoginAuthenticating, t0 + milliseconds(800)), Action::None);
    EXPECT_EQ(w.Tick(stage::kLoginAuthenticating, t0 + milliseconds(1799)), Action::None);
    // The login reconnect handler sets LOGIN_CONNECTING again: no extension.
    EXPECT_EQ(w.Tick(stage::kLoginConnecting, t0 + milliseconds(1790)), Action::None);
    EXPECT_EQ(w.Tick(stage::kLoginConnecting, t0 + milliseconds(1800)), Action::GiveUp);
    EXPECT_EQ(w.Stage(), stage::kLoginAuthenticating) << "the furthest stage is the one named";
}

TEST(LoginConnectWatchdog, CharacterSelectDisarms) {
    LoginConnectWatchdog w;
    const auto t0 = Clock::time_point{} + std::chrono::hours(1);
    w.Arm(stage::kWorldConnecting, t0, 1000);
    EXPECT_EQ(w.Tick(stage::kDone, t0 + milliseconds(500)), Action::None);
    EXPECT_FALSE(w.Armed());
    EXPECT_EQ(w.Tick(stage::kWorldConnecting, t0 + milliseconds(5000)), Action::None)
        << "the wait at character select is the person's, never bounded";
    EXPECT_FALSE(w.GaveUp());
}

TEST(LoginConnectWatchdog, FailureTextPerStage) {
    EXPECT_STREQ(eqt::login_world::LoginConnectFailureText(stage::kLoginConnecting),
                 "Could not connect to login server");
    EXPECT_STREQ(eqt::login_world::LoginConnectFailureText(stage::kLoginAuthenticating),
                 "Could not connect to login server");
    EXPECT_STREQ(eqt::login_world::LoginConnectFailureText(stage::kWorldConnecting),
                 "Could not connect to world server");
}

TEST(LoginConnectWatchdog, ProductionBoundIsTheZoneConnectBound) {
    // docs/decisions.md 2026-10-19 (B1099): a stated default, the measured
    // zone-connect bound (review/B1072/zone_connect_interval_census.txt) reused
    // for the same Daybreak handshake. A change is a decision; this makes it a
    // visible one.
    EXPECT_EQ(kT, 30000);
}

// ---------------------------------------------------------------------------
// The exit code
// ---------------------------------------------------------------------------

TEST(LoginConnectExitCode, CouldNotConnectIsThreeInBothModes) {
    using namespace eqt::scenario;
    EXPECT_EQ(kClientCouldNotConnectExit, 3);
    EXPECT_EQ(ProcessExitCode(false, 0, false, true), kClientCouldNotConnectExit);
    EXPECT_EQ(ProcessExitCode(false, 0, true, true), kClientFatalExit) << "a FATAL wins";
    EXPECT_EQ(ProcessExitCode(false, 0, false, false), 0) << "a clean quit is still 0";
    // --scenario: an unfinished run that never reached the server is could-not-start.
    EXPECT_EQ(ProcessExitCode(true, kScenarioTimeout, false, true), kScenarioCouldNotStart);
    EXPECT_EQ(ProcessExitCode(true, kScenarioFail, false, true), kScenarioFail) << "a verdict stands";
}

// ---------------------------------------------------------------------------
// Over a headless client
// ---------------------------------------------------------------------------

struct Client {
    // Port 9 (discard) on loopback: nothing answers a Daybreak session request.
    Client() : eq("127.0.0.1", 9, "test", "test", "test", "Testbuddy") {
        eqt::ResetCouldNotConnectForTest();
        t0 = Clock::time_point{} + std::chrono::hours(1);
    }
    ~Client() { eqt::ResetCouldNotConnectForTest(); }
    void TickAt(int ms) { eq.LoginWorld().TickLoginConnect(t0 + milliseconds(ms)); }
    const char* ScreenText() { return eq.Bridge().CurrentLoadingStatusText(); }
    EverQuest eq;
    Clock::time_point t0;
};

TEST(LoginConnectTimeout, ADeadLoginServerEndsOnTheMessageNotAHang) {
    Client c;
    auto& lw = c.eq.LoginWorld();
    lw.ConnectToLogin();
    ASSERT_EQ(c.eq.GetLoadingPhase(), LoadingPhase::LOGIN_CONNECTING);
    ASSERT_TRUE(lw.LoginManager() != nullptr);

    c.TickAt(0);
    EXPECT_TRUE(lw.LoginConnectBound().Armed()) << "entering LOGIN_CONNECTING arms the bound";
    c.TickAt(kT - 1);
    EXPECT_EQ(c.eq.GetLoadingPhase(), LoadingPhase::LOGIN_CONNECTING) << "inside the bound: wait";
    EXPECT_FALSE(lw.PreZoneConnectFailed());
    EXPECT_FALSE(eqt::CouldNotConnectRecorded());

    c.TickAt(kT);
    EXPECT_TRUE(lw.PreZoneConnectFailed());
    EXPECT_NE(c.eq.GetLoadingPhase(), LoadingPhase::LOGIN_CONNECTING) << "never a hang at 2 %";
    EXPECT_STREQ(c.ScreenText(), "Could not connect to login server");
    EXPECT_TRUE(lw.LoginManager() == nullptr) << "the dead session is torn down, no reconnect";
    EXPECT_TRUE(eqt::CouldNotConnectRecorded());
    EXPECT_NE(eqt::CouldNotConnectReason().find("Could not connect to login server"), std::string::npos);
    EXPECT_EQ(eqt::scenario::ProcessExitCode(false, 0, false, eqt::CouldNotConnectRecorded()), 3);

    // Later ticks do nothing more: the message stays, no re-arm.
    c.TickAt(10 * kT);
    EXPECT_FALSE(lw.LoginConnectBound().Armed());
    EXPECT_STREQ(c.ScreenText(), "Could not connect to login server");
}

TEST(LoginConnectTimeout, ADeadWorldServerNamesTheWorld) {
    Client c;
    c.eq.SetLoadingPhase(LoadingPhase::WORLD_CONNECTING);
    c.TickAt(0);
    EXPECT_TRUE(c.eq.LoginWorld().LoginConnectBound().Armed());
    c.TickAt(kT);
    EXPECT_TRUE(c.eq.LoginWorld().PreZoneConnectFailed());
    EXPECT_STREQ(c.ScreenText(), "Could not connect to world server");
    EXPECT_TRUE(eqt::CouldNotConnectRecorded());
}

TEST(LoginConnectTimeout, AnAnsweringServerIsLeftAlone) {
    Client c;
    c.eq.SetLoadingPhase(LoadingPhase::LOGIN_CONNECTING);
    c.TickAt(0);
    c.eq.SetLoadingPhase(LoadingPhase::LOGIN_AUTHENTICATING);
    c.TickAt(kT - 100);  // each stage has its own window
    c.eq.SetLoadingPhase(LoadingPhase::WORLD_CONNECTING);
    c.TickAt(2 * kT - 200);
    c.eq.SetLoadingPhase(LoadingPhase::WORLD_CHARACTER_SELECT);
    c.TickAt(2 * kT - 100);
    c.TickAt(100 * kT);
    EXPECT_FALSE(c.eq.LoginWorld().PreZoneConnectFailed());
    EXPECT_FALSE(eqt::CouldNotConnectRecorded());
    EXPECT_EQ(c.eq.GetLoadingPhase(), LoadingPhase::WORLD_CHARACTER_SELECT);
}

TEST(LoginConnectTimeout, ZonePhasesAreNotThisBound) {
    // The zone phases belong to ZoneConnectWatchdog (B1072/B1079).
    Client c;
    c.eq.SetLoadingPhase(LoadingPhase::ZONE_CONNECTING);
    c.TickAt(0);
    EXPECT_FALSE(c.eq.LoginWorld().LoginConnectBound().Armed());
    c.TickAt(10 * kT);
    EXPECT_FALSE(c.eq.LoginWorld().PreZoneConnectFailed());
}

}  // namespace
