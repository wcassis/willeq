// B1072 (GitLab #392): the zone-IN connect is bounded.
//
// Before B1072 a ZoneServerInfo whose zone never answered left the client at
// LoadingPhase::ZONE_CONNECTING (20 %) forever. These tests pin the bound:
// ZoneComponent::kZoneConnectTimeoutMs per attempt, ONE retry of the same
// host:port, then a fresh world login that lands on character select -- over
// the pure ZoneConnectWatchdog and over a headless EverQuest driven through
// the world replay seam, with an explicit clock.

#include <gtest/gtest.h>

#include "client/eq.h"
#include "client/eq_opcodes.h"
#include "client/login_world/login_world_component.h"
#include "client/player/player_component.h"
#include "client/zone/zone_component.h"
#include "client/zone/zone_connect_watchdog.h"
#include "client/scenario/scenario.h"
#include "client/scenario/scenarios.h"

#include <chrono>
#include <cstring>
#include <string>
#include <vector>

namespace {

using Clock = std::chrono::steady_clock;
using Action = eqt::zone::ZoneConnectWatchdog::Action;
using eqt::zone::ZoneComponent;
using std::chrono::milliseconds;

constexpr int kT = ZoneComponent::kZoneConnectTimeoutMs;

// ---------------------------------------------------------------------------
// The pure state machine
// ---------------------------------------------------------------------------

TEST(ZoneConnectWatchdog, TimeoutRetriesOnceThenGivesUp) {
    eqt::zone::ZoneConnectWatchdog w;
    const auto t0 = Clock::time_point{} + std::chrono::hours(1);
    EXPECT_EQ(w.Tick(true, t0), Action::None) << "unarmed: nothing";
    w.Arm("10.0.0.1", 7005, t0, 1000, 1);
    EXPECT_TRUE(w.Armed());
    EXPECT_EQ(w.Attempt(), 1);
    EXPECT_EQ(w.Attempts(), 2);
    EXPECT_EQ(w.Tick(true, t0 + milliseconds(999)), Action::None);
    EXPECT_EQ(w.Tick(true, t0 + milliseconds(1000)), Action::Retry);
    EXPECT_EQ(w.Attempt(), 2);
    EXPECT_EQ(w.Host(), "10.0.0.1");
    EXPECT_EQ(w.Port(), 7005);
    // The retry gets a whole new window, measured from the retry.
    EXPECT_EQ(w.Tick(true, t0 + milliseconds(1999)), Action::None);
    EXPECT_EQ(w.Tick(true, t0 + milliseconds(2000)), Action::GiveUp);
    EXPECT_FALSE(w.Armed());
    EXPECT_EQ(w.Tick(true, t0 + milliseconds(9000)), Action::None) << "gives up once";
}

TEST(ZoneConnectWatchdog, LeavingZoneConnectingDisarms) {
    eqt::zone::ZoneConnectWatchdog w;
    const auto t0 = Clock::time_point{} + std::chrono::hours(1);
    w.Arm("h", 1, t0, 1000, 1);
    EXPECT_EQ(w.Tick(false, t0 + milliseconds(10)), Action::None);
    EXPECT_FALSE(w.Armed());
    EXPECT_EQ(w.Tick(true, t0 + milliseconds(5000)), Action::None) << "a later ZONE_CONNECTING needs a new Arm";
}

TEST(ZoneConnectWatchdog, ProductionBoundIsThirtySecondsAndOneRetry) {
    // review/B1072/zone_connect_interval_census.txt: 1,663 zone-ins, max
    // 14.4 s from ZONE_CONNECTING to ZONE_RECEIVING_PROFILE. A change to the
    // bound is a decision; this line makes it a visible one.
    EXPECT_EQ(ZoneComponent::kZoneConnectTimeoutMs, 30000);
    EXPECT_EQ(ZoneComponent::kZoneConnectRetries, 1);
    EXPECT_GT(ZoneComponent::kZoneConnectTimeoutMs, 2 * 14397);
}

// ---------------------------------------------------------------------------
// Over a headless client
// ---------------------------------------------------------------------------

std::vector<uint8_t> ZoneServerInfo(const std::string& host, uint16_t port) {
    // WorldProcessZoneServerInfo: host C string at payload 0, port u16 at 128.
    std::vector<uint8_t> body(130, 0);
    std::memcpy(body.data(), host.data(), host.size());
    body[128] = static_cast<uint8_t>(port & 0xff);
    body[129] = static_cast<uint8_t>(port >> 8);
    return body;
}

std::vector<uint8_t> CharInfo(const std::vector<std::string>& names) {
    std::vector<uint8_t> body(1704, 0);
    for (int i = 0; i < 10; ++i) {
        const std::string n = i < static_cast<int>(names.size()) ? names[static_cast<size_t>(i)] : "<none>";
        std::memcpy(body.data() + 1024 + 64 * i, n.data(), n.size());
        body[1694 + i] = 1;
        body[1004 + i] = 13;
    }
    return body;
}

struct Client {
    Client() : eq("127.0.0.1", 0, "test", "test", "test", "Testbuddy") {
        eq.Test_SetCaptureQueuedPacketsForSeam(true);
        eq.Test_ClearQueuedPackets();
    }
    // Port 9 (discard) on loopback: nothing answers a Daybreak session request.
    void ServerInfo() {
        const auto b = ZoneServerInfo("127.0.0.1", 9);
        eq.LoginWorld().ReplayWorldPacket(HC_OP_ZoneServerInfo, b.data(), b.size());
        t0 = Clock::now();
    }
    void TickAt(int ms) { eq.TickZoneConnect(t0 + milliseconds(ms)); }
    EverQuest eq;
    Clock::time_point t0;
};

TEST(ZoneConnectTimeout, ZoneServerInfoArmsTheBound) {
    Client c;
    c.ServerInfo();
    EXPECT_EQ(c.eq.GetLoadingPhase(), LoadingPhase::ZONE_CONNECTING);
    EXPECT_TRUE(c.eq.ZoneConnectWatchdog().Armed());
    EXPECT_EQ(c.eq.ZoneConnectWatchdog().Host(), "127.0.0.1");
    EXPECT_EQ(c.eq.ZoneConnectWatchdog().Port(), 9);
    EXPECT_EQ(c.eq.ZoneConnectOpenCount(), 1);
}

TEST(ZoneConnectTimeout, TimeoutRetriesTheSameServerThenReturnsToCharacterSelect) {
    Client c;
    auto& lw = c.eq.LoginWorld();
    lw.ReplayWorldPacket(HC_OP_SendCharInfo, CharInfo({"Testbuddy"}).data(), 1704);
    const uint32_t gen0 = lw.CharacterListGeneration();
    c.eq.Player().SetCampReturnsToCharacterSelect(true);   // a run with the screen
    c.eq.Zone().Test_SetZoneToZoneInProgressForSeam(true);  // mid zone-to-zone, as on the XU4
    c.ServerInfo();

    c.TickAt(kT - 1000);
    EXPECT_EQ(c.eq.ZoneConnectOpenCount(), 1) << "inside the bound: wait";
    EXPECT_EQ(c.eq.GetLoadingPhase(), LoadingPhase::ZONE_CONNECTING);

    // Attempt 1 runs out: ONE retry, same host:port, still connecting.
    c.TickAt(kT + 100);
    EXPECT_EQ(c.eq.ZoneConnectOpenCount(), 2);
    EXPECT_EQ(c.eq.ZoneConnectWatchdog().Attempt(), 2);
    EXPECT_EQ(c.eq.ZoneConnectWatchdog().Port(), 9);
    EXPECT_EQ(c.eq.GetLoadingPhase(), LoadingPhase::ZONE_CONNECTING);
    EXPECT_FALSE(lw.CharacterSelectScreen());

    // Attempt 2 runs out: character select, a FRESH world login, no exit.
    c.TickAt(2 * kT + 200);
    EXPECT_EQ(c.eq.ZoneConnectOpenCount(), 2) << "no third connect";
    EXPECT_FALSE(c.eq.ZoneConnectWatchdog().Armed());
    EXPECT_NE(c.eq.GetLoadingPhase(), LoadingPhase::ZONE_CONNECTING) << "never a hang at 20 %";
    EXPECT_FALSE(c.eq.IsQuitRequested()) << "never a process exit";
    EXPECT_FALSE(c.eq.Zone().Test_IsZoneToZoneInProgress()) << "zoning = 0: world sends the character list";
    EXPECT_TRUE(lw.CharacterSelectScreen());
    EXPECT_TRUE(lw.AwaitingFreshCharacterList());
    EXPECT_EQ(lw.CharacterSelectReturns(), 1u);
    EXPECT_FALSE(lw.HasSentEnterWorld());

    // Later ticks do nothing more.
    c.TickAt(10 * kT);
    EXPECT_EQ(c.eq.ZoneConnectOpenCount(), 2);
    EXPECT_EQ(lw.CharacterSelectReturns(), 1u);

    // The fresh login's handshake: no automatic Enter World with the screen up.
    c.eq.Test_ClearQueuedPackets();
    lw.ReplayWorldPacket(HC_OP_ExpansionInfo, std::vector<uint8_t>{0xff, 0, 0, 0}.data(), 4);
    for (const auto& p : c.eq.Test_GetQueuedPackets()) EXPECT_NE(p.opcode, HC_OP_EnterWorld);
    lw.ReplayWorldPacket(HC_OP_SendCharInfo, CharInfo({"Testbuddy"}).data(), 1704);
    EXPECT_GT(lw.CharacterListGeneration(), gen0);
    EXPECT_FALSE(lw.AwaitingFreshCharacterList());

    // Enter World again arms a new bound on the next ZoneServerInfo.
    EXPECT_TRUE(lw.EnterWorldFromSelect("Testbuddy"));
    c.ServerInfo();
    EXPECT_TRUE(c.eq.ZoneConnectWatchdog().Armed());
    EXPECT_EQ(c.eq.ZoneConnectWatchdog().Attempt(), 1);
    EXPECT_EQ(c.eq.ZoneConnectOpenCount(), 3);
}

TEST(ZoneConnectTimeout, HeadlessLogsInToWorldAgainInsteadOfExiting) {
    // No screen (headless, no scenario opt-in): the camp's headless landing is
    // an exit, but a failed zone-in is not the user's choice -- go back to
    // world as a fresh login and let the configured character re-enter.
    Client c;
    c.ServerInfo();
    c.TickAt(kT + 1);
    c.TickAt(2 * kT + 2);
    EXPECT_FALSE(c.eq.ZoneConnectWatchdog().Armed());
    EXPECT_NE(c.eq.GetLoadingPhase(), LoadingPhase::ZONE_CONNECTING);
    EXPECT_FALSE(c.eq.IsQuitRequested());
    EXPECT_FALSE(c.eq.LoginWorld().CharacterSelectScreen());
    EXPECT_EQ(c.eq.LoginWorld().CharacterSelectReturns(), 0u);
}

TEST(ZoneConnectTimeout, AZoneThatAnswersInTimeIsLeftAlone) {
    Client c;
    c.ServerInfo();
    // The PlayerProfile moved the phase on (ZoneProcessPlayerProfile).
    c.eq.SetLoadingPhase(LoadingPhase::ZONE_RECEIVING_PROFILE);
    c.TickAt(1000);
    EXPECT_FALSE(c.eq.ZoneConnectWatchdog().Armed());
    c.TickAt(5 * kT);
    EXPECT_EQ(c.eq.ZoneConnectOpenCount(), 1);
    EXPECT_EQ(c.eq.GetLoadingPhase(), LoadingPhase::ZONE_RECEIVING_PROFILE);
    EXPECT_FALSE(c.eq.LoginWorld().CharacterSelectScreen());
}

TEST(ZoneConnectTimeout, TheScenarioLeverRedirectsOnlyTheAttemptsItWasGiven) {
    Client c;
    c.eq.SimulateUnreachableZoneConnects(2, 1);
    c.ServerInfo();                 // attempt 1 -> port 1
    c.TickAt(kT + 1);               // the retry -> port 1
    EXPECT_EQ(c.eq.ZoneConnectOpenCount(), 2);
    EXPECT_EQ(c.eq.ZoneConnectWatchdog().Port(), 9) << "the bound still names world's port";
}

// ---------------------------------------------------------------------------
// The zone_connect_timeout scenario over a fake world
// ---------------------------------------------------------------------------

struct FakeWorld : eqt::scenario::IScenarioWorld {
    bool zonedIn = true, atSelect = false, leverOk = true;
    long long gen = 1, opens = 1;
    int leverAttempts = 0, leverPort = 0, enters = 0;
    std::vector<std::string> sent;
    bool IsConnected() const override { return true; }
    bool IsFullyZonedIn() const override { return zonedIn; }
    std::string ZoneName() const override { return zonedIn ? "qeynos2" : ""; }
    int OtherPlayerCount() const override { return 0; }
    std::string PlayerName() const override { return "Testbuddy"; }
    std::vector<uint32_t> PlayerBuffSpellIds() const override { return {}; }
    long long PeerEquipmentMaterial(const std::string&, int) const override { return -1; }
    long long SelfEquipmentMaterial(int) const override { return -1; }
    bool AtCharacterSelect() const override { return atSelect; }
    long long CharacterListGeneration() const override { return gen; }
    bool EnterWorldAs(const std::string& n) override { ++enters; atSelect = false; return n == "Testbuddy"; }
    long long ZoneConnectOpens() const override { return opens; }
    bool SimulateUnreachableZoneConnects(int a, int p) override { leverAttempts = a; leverPort = p; return leverOk; }
    void SendCommand(const std::string& l) override { sent.push_back(l); }
    void Note(const std::string&) override {}
};

using eqt::scenario::StepResult;

struct ScenarioRig {
    FakeWorld w;
    std::unique_ptr<eqt::scenario::IScenario> s =
        eqt::scenario::MakeZoneConnectTimeoutScenario(eqt::scenario::ScenarioOptions{});
    double now = 0.0;
    StepResult At(double t) {
        now = t;
        eqt::scenario::ScenarioTick tick;
        tick.sinceStart = t;
        return s->Step(w, tick);
    }
    // /camp, the countdown, the fresh list, the lever and Enter World.
    void ToBound() {
        ASSERT_EQ(At(0.0), StepResult::Running);
        ASSERT_EQ(w.sent, std::vector<std::string>{"/camp"});
        w.zonedIn = false;
        ASSERT_EQ(At(20.0), StepResult::Running);
        w.atSelect = true; w.gen = 2;
        ASSERT_EQ(At(32.0), StepResult::Running);
        ASSERT_EQ(w.leverAttempts, 2);
        ASSERT_EQ(w.leverPort, 1);
        ASSERT_EQ(w.enters, 1);
    }
};

TEST(ZoneConnectTimeoutScenario, PassesOnTwoConnectsSelectAndReentry) {
    ScenarioRig r;
    EXPECT_TRUE(r.s->WantsCampToCharacterSelect());
    r.ToBound();
    r.w.opens = 3;
    EXPECT_EQ(r.At(62.0), StepResult::Running);
    r.w.opens = 3;  // attempt + retry = +2 over the 1 at arm time
    r.w.atSelect = true; r.w.gen = 3;
    EXPECT_EQ(r.At(93.0), StepResult::Running);
    EXPECT_EQ(r.w.enters, 2) << "Enter World again";
    r.w.zonedIn = true;
    EXPECT_EQ(r.At(100.0), StepResult::Pass) << r.s->Reason();
}

TEST(ZoneConnectTimeoutScenario, FailsWithoutTheRetry) {
    ScenarioRig r;
    r.ToBound();
    r.w.opens = 2;  // only one connect
    r.w.atSelect = true; r.w.gen = 3;
    EXPECT_EQ(r.At(93.0), StepResult::Fail);
}

TEST(ZoneConnectTimeoutScenario, FailsOnAnEarlyGiveUp) {
    ScenarioRig r;
    r.ToBound();
    r.w.opens = 3;
    r.w.atSelect = true; r.w.gen = 3;
    EXPECT_EQ(r.At(50.0), StepResult::Fail) << "18 s after Enter World is under two bounds";
}

TEST(ZoneConnectTimeoutScenario, FailsOnAHang) {
    ScenarioRig r;
    r.ToBound();
    EXPECT_EQ(r.At(32.0 + 84.0), StepResult::Running);
    EXPECT_EQ(r.At(32.0 + 86.0), StepResult::Fail) << "the 20 % hang B1072 fixed";
}

TEST(ZoneConnectTimeoutScenario, FailsWhenTheLeverDidNotTake) {
    ScenarioRig r;
    r.ToBound();
    r.w.zonedIn = true;
    EXPECT_EQ(r.At(40.0), StepResult::Fail);
}

}  // namespace
