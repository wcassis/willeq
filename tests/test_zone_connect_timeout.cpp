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
    // B1079: a zone that ANSWERS is no longer disarmed at the first phase
    // change (that was B1072's shape -- the bound then stopped at 20 % the
    // moment the session came up, which is exactly the gap #399 found).
    // Instead the bound walks the whole span and only clears on COMPLETE: a
    // load that progresses through every phase and finishes is left alone,
    // never reconnected, never escalated.
    Client c;
    c.ServerInfo();
    // Walk the loading span the way a healthy zone-in does it, one phase
    // ahead at a time, comfortably inside each phase's own budget.
    const LoadingPhase phases[] = {
        LoadingPhase::ZONE_RECEIVING_PROFILE,
        LoadingPhase::ZONE_RECEIVING_SPAWNS,
        LoadingPhase::ZONE_REQUEST_PHASE,
        LoadingPhase::ZONE_PLAYER_READY,
        LoadingPhase::ZONE_AWAITING_CONFIRM,
        LoadingPhase::GRAPHICS_LOADING_ZONE,
        LoadingPhase::GRAPHICS_FINALIZING,
        LoadingPhase::COMPLETE,
    };
    int at = 0;
    for (const auto phase : phases) {
        c.eq.SetLoadingPhase(phase);
        c.TickAt(++at * 1000);   // 1 s per phase, far under any bound
        EXPECT_NE(c.eq.ZoneConnectOpenCount(), 1 + at) << "no reconnect while advancing";
    }
    // COMPLETE disarmed the bound; later ticks do nothing more.
    EXPECT_FALSE(c.eq.ZoneConnectWatchdog().Armed());
    const int opens = c.eq.ZoneConnectOpenCount();
    c.TickAt(100 * 1000);
    EXPECT_EQ(c.eq.ZoneConnectOpenCount(), opens);
    EXPECT_EQ(c.eq.GetLoadingPhase(), LoadingPhase::COMPLETE);
    EXPECT_FALSE(c.eq.LoginWorld().CharacterSelectScreen());
}

// ---------------------------------------------------------------------------
// B1079: the per-phase bound over the pure state machine
// ---------------------------------------------------------------------------

namespace {
// The production table in phase_index order (mirrors EverQuest::
// ZonePhaseBoundsMs, which the client-side tests read). Fully qualified:
// phase_bound / phase_index are NESTED namespaces in the header, not symbols,
// so a using-declaration is not possible.
const int* KBounds() {
    static const int b[eqt::zone::phase_index::kCount] = {
        eqt::zone::phase_bound::kZoneConnectingMs,
        eqt::zone::phase_bound::kZoneReceivingProfileMs,
        eqt::zone::phase_bound::kZoneReceivingSpawnsMs,
        eqt::zone::phase_bound::kZoneRequestPhaseMs,
        eqt::zone::phase_bound::kZonePlayerReadyMs,
        eqt::zone::phase_bound::kZoneAwaitingConfirmMs,
        eqt::zone::phase_bound::kGraphicsLoadingZoneMs,
        eqt::zone::phase_bound::kGraphicsLoadingModelsMs,
        eqt::zone::phase_bound::kGraphicsCreatingEntitiesMs,
        eqt::zone::phase_bound::kGraphicsFinalizingMs };
    return b;
}
}  // namespace

TEST(ZoneConnectWatchdog, PerPhaseTableIsPinnedToTheHeader) {
    const int* b = KBounds();
    EXPECT_EQ(b[eqt::zone::phase_index::kZoneConnecting], 30000);
    for (int i = 1; i <= 5; ++i)
        EXPECT_EQ(b[i], 10000) << "network tail phases are each 10 s";
    for (int i = 6; i < eqt::zone::phase_index::kCount; ++i)
        EXPECT_EQ(b[i], 80000) << "graphics span phases share one 80 s budget";
}

TEST(ZoneConnectWatchdog, AZoneSilentAfterZoneEntryIsEscalated) {
    // The #399 shape: the session came up, OP_ZoneEntry went out, the zone
    // then went silent in ZONE_RECEIVING_PROFILE. B1072 disarmed here and the
    // client hung; B1079 keeps the bound running and escalates at the
    // profile phase's own budget.
    eqt::zone::ZoneConnectWatchdog w;
    const auto t0 = Clock::time_point{} + std::chrono::hours(1);
    w.Arm("10.0.0.2", 7016, t0, KBounds(), 1);
    // Session up (past connect), now sitting in the profile phase.
    EXPECT_EQ(w.Tick(eqt::zone::phase_index::kZoneReceivingProfile, t0), Action::None);
    // Inside the 10 s profile budget: wait.
    EXPECT_EQ(w.Tick(eqt::zone::phase_index::kZoneReceivingProfile, t0 + milliseconds(9999)), Action::None);
    // The budget runs out: escalate (NOT a retry — the session is up, so a
    // retry would duplicate OP_ZoneEntry).
    EXPECT_EQ(w.Tick(eqt::zone::phase_index::kZoneReceivingProfile, t0 + milliseconds(10000)), Action::GiveUp);
    EXPECT_FALSE(w.Armed());
    EXPECT_EQ(w.PhaseIndex(), eqt::zone::phase_index::kZoneReceivingProfile);
}

TEST(ZoneConnectWatchdog, AForwardMoveGetsTheLaterPhasesBudget) {
    // Forward movement to a later tracked phase re-baselines to that phase's
    // own budget (a load that reaches spawns is progressing, so it gets the
    // spawns budget). A later tick at the SAME phase does not extend it
    // further (the 60 Hz re-baseline defect).
    eqt::zone::ZoneConnectWatchdog w;
    const auto t0 = Clock::time_point{} + std::chrono::hours(1);
    w.Arm("h", 1, t0, KBounds(), 1);
    w.Tick(eqt::zone::phase_index::kZoneReceivingProfile, t0);
    // The profile budget runs out -> escalate (this is the #399 shape, and it
    // proves the forward re-baseline below does not accidentally hide it).
    EXPECT_EQ(w.Tick(eqt::zone::phase_index::kZoneReceivingProfile, t0 + milliseconds(10000)), Action::GiveUp);
    w.Arm("h", 1, t0, KBounds(), 1);
    // Move on in time: a healthy load reaches spawns 4 s into the profile.
    EXPECT_EQ(w.Tick(eqt::zone::phase_index::kZoneReceivingProfile, t0 + milliseconds(4000)), Action::None);
    EXPECT_EQ(w.Tick(eqt::zone::phase_index::kZoneReceivingSpawns, t0 + milliseconds(4000)), Action::None);  // budget t0+14000
    // Repeated ticks at the SAME phase do not push the deadline:
    EXPECT_EQ(w.Tick(eqt::zone::phase_index::kZoneReceivingSpawns, t0 + milliseconds(13000)), Action::None);
    // ...and the re-baselined budget ends it at t0+14000, not the old profile.
    EXPECT_EQ(w.Tick(eqt::zone::phase_index::kZoneReceivingSpawns, t0 + milliseconds(14000)), Action::GiveUp);
}

TEST(ZoneConnectWatchdog, ABackwardMoveNeverExtendsTheBudget) {
    // A backward move (a load regressing from spawns to the connect phase,
    // the ping-pong shape) keeps the running deadline: it cannot extend the
    // budget. Here the connect window from Arm (t0+30000) is still running,
    // so a regression is a no-op and the load still escalates on time.
    eqt::zone::ZoneConnectWatchdog w;
    const auto t0 = Clock::time_point{} + std::chrono::hours(1);
    w.Arm("h", 1, t0, KBounds(), 1);
    w.Tick(eqt::zone::phase_index::kZoneReceivingProfile, t0);      // budget t0+10000
    EXPECT_EQ(w.Tick(eqt::zone::phase_index::kZoneReceivingSpawns, t0 + milliseconds(4000)), Action::None);  // budget t0+14000
    // 8 s in, the load regresses to the connect phase: deadline held at t0+14000.
    EXPECT_EQ(w.Tick(eqt::zone::phase_index::kZoneConnecting, t0 + milliseconds(8000)), Action::None);
    // ...and it still escalates at the running deadline, not a fresh 30 s.
    EXPECT_EQ(w.Tick(eqt::zone::phase_index::kZoneConnecting, t0 + milliseconds(14000)), Action::GiveUp);
}

TEST(ZoneConnectWatchdog, ARegressionOutOfTheGraphicsSpanKeepsTheLongBudget) {
    // A load that enters the graphics span and then REGRESSES into the
    // network tail keeps the span's long budget (one stuck load = one 80 s
    // budget), so the downgrade cannot both shorten and restart it.
    eqt::zone::ZoneConnectWatchdog w;
    const auto t0 = Clock::time_point{} + std::chrono::hours(1);
    w.Arm("h", 1, t0, KBounds(), 1);
    w.Tick(eqt::zone::phase_index::kZoneReceivingProfile, t0);
    w.Tick(eqt::zone::phase_index::kGraphicsLoadingZone, t0 + milliseconds(5000));   // budget t0+85000
    // 60 s into the span, the load regresses back to the network tail:
    EXPECT_EQ(w.Tick(eqt::zone::phase_index::kZoneReceivingProfile, t0 + milliseconds(65000)), Action::None);
    // ...the span's budget is still what ends it, not a short 10 s profile.
    EXPECT_EQ(w.Tick(eqt::zone::phase_index::kZoneReceivingProfile, t0 + milliseconds(85000)), Action::GiveUp);
}

TEST(ZoneConnectWatchdog, TheGraphicsSpanIsOneSharedBudget) {
    // The four GRAPHICS_* phases are the loading thread's own contiguous
    // progress: entering the span sets ONE deadline, and later graphics
    // phases do not re-baseline it.
    eqt::zone::ZoneConnectWatchdog w;
    const auto t0 = Clock::time_point{} + std::chrono::hours(1);
    w.Arm("h", 1, t0, KBounds(), 1);
    w.Tick(eqt::zone::phase_index::kZoneReceivingProfile, t0);
    w.Tick(eqt::zone::phase_index::kGraphicsLoadingZone, t0 + milliseconds(2000));   // budget t0+2000+80000
    w.Tick(eqt::zone::phase_index::kGraphicsLoadingModels, t0 + milliseconds(30000));
    w.Tick(eqt::zone::phase_index::kGraphicsCreatingEntities, t0 + milliseconds(60000));
    // Inside the shared 80 s span (from the span's first entry at t0+2000):
    EXPECT_EQ(w.Tick(eqt::zone::phase_index::kGraphicsFinalizing, t0 + milliseconds(80000)), Action::None);
    // ...and the span's own budget ends it, not some later graphics phase:
    EXPECT_EQ(w.Tick(eqt::zone::phase_index::kGraphicsFinalizing, t0 + milliseconds(2000 + 80000)), Action::GiveUp);
}

TEST(ZoneConnectWatchdog, CompleteDisarmsWithoutEscalation) {
    eqt::zone::ZoneConnectWatchdog w;
    const auto t0 = Clock::time_point{} + std::chrono::hours(1);
    w.Arm("h", 1, t0, KBounds(), 1);
    w.Tick(eqt::zone::phase_index::kZoneReceivingProfile, t0);
    EXPECT_EQ(w.Tick(eqt::zone::phase_index::kDone, t0 + milliseconds(50)), Action::None);
    EXPECT_FALSE(w.Armed());
}


TEST(ZoneConnectTimeout, TheScenarioLeverRedirectsOnlyTheAttemptsItWasGiven) {
    Client c;
    c.eq.SimulateUnreachableZoneConnects(2, 1);
    c.ServerInfo();                 // attempt 1 -> port 1
    c.TickAt(kT + 1);               // the retry -> port 1
    EXPECT_EQ(c.eq.ZoneConnectOpenCount(), 2);
    EXPECT_EQ(c.eq.ZoneConnectWatchdog().Port(), 9) << "the bound still names world's port";
}

// B1079: the "zone answered then went silent" escalation, over the real client.
// The lever pins the handshake past ZONE_CONNECTING (the zone's PlayerProfile
// arrived), the connect is a dead port (port 9, nothing answers), and the
// bound must walk the PROFILE phase's own 10 s budget and escalate WITHOUT a
// connect retry (a re-connect would duplicate OP_ZoneEntry) -- landing on
// character select on the profile budget, not the 30 s connect bound.
TEST(ZoneConnectTimeout, SilentZoneEscalatesOnTheProfileBudgetWithoutAConnectRetry) {
    Client c;
    auto& lw = c.eq.LoginWorld();
    lw.ReplayWorldPacket(HC_OP_SendCharInfo, CharInfo({"Testbuddy"}).data(), 1704);
    c.eq.Player().SetCampReturnsToCharacterSelect(true);   // a run with the screen
    c.eq.SimulateZoneSilentAfterHandshake();
    c.ServerInfo();
    // The handshake is pinned past ZONE_CONNECTING:
    EXPECT_EQ(c.eq.GetLoadingPhase(), LoadingPhase::ZONE_RECEIVING_PROFILE);
    EXPECT_EQ(c.eq.ZoneConnectOpenCount(), 1);
    // Walk the profile budget; the escalation lands on character select on the
    // profile phase's own ~10 s budget (the first tick re-baselines the
    // deadline to now + 10 s).
    int escalateAt = -1;
    for (int ms = 1000; ms <= 40000; ms += 1000) {
        c.TickAt(ms);
        if (lw.CharacterSelectScreen()) { escalateAt = ms; break; }
    }
    ASSERT_GT(escalateAt, 0) << "the silent zone escalated (never a hang)";
    // The zone answered, so NO connect retry: exactly one open, still.
    EXPECT_EQ(c.eq.ZoneConnectOpenCount(), 1) << "a zone that answered must not be re-connected";
    EXPECT_GE(escalateAt, 10000) << "no sooner than the profile bound";
    EXPECT_LT(escalateAt, 30000) << "well inside the connect bound (the B1079 distinction)";
    EXPECT_FALSE(c.eq.IsQuitRequested()) << "never a process exit";
}

// ---------------------------------------------------------------------------
// The zone_connect_timeout scenario over a fake world
// ---------------------------------------------------------------------------

struct FakeWorld : eqt::scenario::IScenarioWorld {
    bool zonedIn = true, atSelect = false, leverOk = true;
    long long gen = 1, opens = 1;
    long long silences = 0;  // B1080: the teardown audio-silence count
    int leverAttempts = 0, leverPort = 0, enters = 0;
    bool silentLever = false, silentLeverOk = true;
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
    long long ZoneAudioSilenceCount() const override { return silences; }
    bool SimulateUnreachableZoneConnects(int a, int p) override { leverAttempts = a; leverPort = p; return leverOk; }
    bool SimulateZoneSilentAfterHandshake() override { silentLever = true; return silentLeverOk; }
    void SendCommand(const std::string& l) override { sent.push_back(l); }
    void Note(const std::string&) override {}
};

using eqt::scenario::StepResult;

struct ScenarioRig {
    FakeWorld w;
    std::unique_ptr<eqt::scenario::IScenario> s;
    double now = 0.0;
    ScenarioRig(bool silent = false) {
        eqt::scenario::ScenarioOptions o;
        o.silentZone = silent;
        s = eqt::scenario::MakeZoneConnectTimeoutScenario(o);
    }
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
        if (s->Name() && w.silentLever) {
            // The silent arm: one dead connect + the answered-then-silent pin.
            ASSERT_EQ(w.leverAttempts, 1);
            ASSERT_EQ(w.leverPort, 1);
        } else {
            ASSERT_EQ(w.leverAttempts, 2);
            ASSERT_EQ(w.leverPort, 1);
        }
        ASSERT_EQ(w.enters, 1);
        // B1080: in the real world the give-up's teardown (AbandonZoneConnect
        // -> CleanupZone) runs the audio silence exactly once.
        ++w.silences;
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

// B1080 (#400): the give-up must have run the zone audio silence. No
// increment since the baseline -> the leaving zone's audio kept playing:
// FAIL, with the reason naming the audio.
TEST(ZoneConnectTimeoutScenario, FailsWhenTheGiveUpDidNotSilenceTheZoneAudio) {
    ScenarioRig r;
    r.ToBound();
    r.w.opens = 3;
    r.w.atSelect = true;
    r.w.gen = 3;
    r.w.silences = 0;  // no teardown silence ran
    EXPECT_EQ(r.At(93.0), StepResult::Fail);
    EXPECT_NE(r.s->Reason().find("zone audio silence"), std::string::npos);
}

// B1080 (#400): a world that cannot read the count cannot verify the fix
// (the default IScenarioWorld body returns -1): FAIL, not a silent pass.
TEST(ZoneConnectTimeoutScenario, FailsWhenTheSilenceCountIsUnreadable) {
    ScenarioRig r;
    r.ToBound();
    r.w.opens = 3;
    r.w.atSelect = true;
    r.w.gen = 3;
    r.w.silences = -1;  // "not readable"
    EXPECT_EQ(r.At(93.0), StepResult::Fail);
    EXPECT_NE(r.s->Reason().find("unreadable"), std::string::npos);
}

// ---------------------------------------------------------------------------
// B1079: the "zone answered then went silent" arm
// ---------------------------------------------------------------------------

TEST(ZoneConnectTimeoutScenario, SilentArmArmsOneDeadConnectAndThePin) {
    ScenarioRig r(true);
    EXPECT_TRUE(r.s->WantsCampToCharacterSelect());
    r.ToBound();
    EXPECT_TRUE(r.w.silentLever) << "the silent lever is armed";
    EXPECT_EQ(r.w.leverAttempts, 1) << "the zone answered, so ONE connect (no retry)";
}

TEST(ZoneConnectTimeoutScenario, SilentArmPassesOnOneConnectWithinTheProfileBound) {
    ScenarioRig r(true);
    r.ToBound();
    r.w.opens = 2;  // 1 at arm time + ONE connect = the silent arm's shape
    r.w.atSelect = true; r.w.gen = 3;
    // 13 s after Enter World: over the profile bound (10 s) and its 1 s
    // tolerance, well inside the 30 s connect bound.
    EXPECT_EQ(r.At(32.0 + 13.0), StepResult::Pass) << r.s->Reason();
    EXPECT_EQ(r.w.enters, 1) << "no second Enter World: the silent arm is a verdict, not a recovery";
}

TEST(ZoneConnectTimeoutScenario, SilentArmFailsOnAConnectRetry) {
    ScenarioRig r(true);
    r.ToBound();
    r.w.opens = 3;  // 1 at arm time + TWO connects = a retry happened
    r.w.atSelect = true; r.w.gen = 3;
    EXPECT_EQ(r.At(32.0 + 13.0), StepResult::Fail) << "a zone that answered must not be re-connected";
}

TEST(ZoneConnectTimeoutScenario, SilentArmFailsOnTheWrongBudget) {
    ScenarioRig r(true);
    r.ToBound();
    r.w.opens = 2;
    r.w.atSelect = true; r.w.gen = 3;
    // 33 s after Enter World: past the connect bound, so the escalation ran on
    // the 30 s connect budget, not the 10 s profile bound -- the B1072 shape,
    // which is the wrong escalation for a zone that already answered.
    EXPECT_EQ(r.At(32.0 + 33.0), StepResult::Fail);
}

}  // namespace
