// T410 (board B138/B139): headless tests for the scenario lane.
//
// The whole point of splitting IScenarioWorld out of the scenarios is that
// these run with no client, no server and no network -- a FakeWorld plus a
// hand-cranked clock. Pre-fix there is no scenario, no runner and no exit
// code, so every case below fails to compile, which is the strongest
// red-before-green this shape admits.

#include <gtest/gtest.h>

#include "client/scenario/scenario_runner.h"
#include "client/scenario/scenarios.h"

#include <algorithm>
#include <string>
#include <vector>

using namespace eqt::scenario;

namespace {

class FakeWorld : public IScenarioWorld {
public:
    bool connected = false;
    bool zonedIn = false;
    std::string zone;
    int otherPlayers = 0;
    std::string playerName = "Testcaster";
    std::vector<uint32_t> buffs;
    // T494: what the server has told us the OTHER player is wearing.
    long long peerMaterial = -1;
    // T498: what THIS client derived for itself from its own inventory.
    long long selfMaterial = -1;

    std::vector<std::string> commands;
    std::vector<std::string> notes;
    // B630 fix pass: when set, the world drops its peer once the run's clock
    // reaches this many seconds (the caster's clean chain-lane departure).
    // Advance() applies it; -1 (the default) means the peer never leaves.
    double peerVanishesAt = -1.0;
    // The run's clock in seconds, advanced by Advance(). Separate from the
    // runner's because the scenario only sees time since zone-in.
    double runClock = 0.0;

    bool IsConnected() const override { return connected; }
    bool IsFullyZonedIn() const override { return zonedIn; }
    std::string ZoneName() const override { return zone; }
    int OtherPlayerCount() const override { return otherPlayers; }
    std::string PlayerName() const override { return playerName; }
    std::vector<uint32_t> PlayerBuffSpellIds() const override { return buffs; }
    long long PeerEquipmentMaterial(const std::string&, int) const override {
        return otherPlayers >= 1 ? peerMaterial : -1;
    }
    long long SelfEquipmentMaterial(int) const override { return selfMaterial; }
    void SendCommand(const std::string& rawLine) override { commands.push_back(rawLine); }
    void Note(const std::string& line) override { notes.push_back(line); }

    bool sent(const std::string& needle) const {
        return std::any_of(commands.begin(), commands.end(),
            [&needle](const std::string& c) { return c.find(needle) != std::string::npos; });
    }
};

/** Advance a runner by `seconds` in 1/60 s ticks, or until it finishes.

    When `world` is given (the B630 observer departure tests), the world's
    `peerVanishesAt` (run seconds, -1 = never) is applied at the first tick
    at or past it: OtherPlayerCount() drops to 0, simulating the caster's
    clean chain-lane departure. */
void Advance(ScenarioRunner& runner, double seconds, FakeWorld* world = nullptr) {
    const float dt = 1.0f / 60.0f;
    const int ticks = static_cast<int>(seconds * 60.0);
    for (int i = 0; i < ticks; ++i) {
        if (world) {
            world->runClock += dt;
            if (world->peerVanishesAt >= 0.0 &&
                world->runClock >= world->peerVanishesAt && world->otherPlayers > 0) {
                world->otherPlayers = 0;
            }
        }
        if (!runner.Tick(dt)) {
            return;
        }
    }
}

// B630 item 1, RED-pinned: eqemu's GM `#unmemspells`/`#memspell` apply to
// c->GetTarget() (zone/gm_commands/unmemspells.cpp:22-24, memspell.cpp:31-33).
// If a leg's prep runs while a target is still set, the spells land on the
// buddy and every D leg casts gem 0's old spell (288), not its declared id.
// The ordering that keeps the caster's own gem 0 intact is:
//     /cleartarget -> /say #unmemspells -> /say #memspell <id> 0 -> /target <peer>
// This helper asserts that exact order (strict, first occurrence of each)
// against the FakeWorld's recorded commands. Pre-fix, D1/D2 had no memspell
// prep at all and every leg's prep ran against the previous leg's persisted
// target, so this fails on the old code.
void ExpectPrepOrder(const FakeWorld& world, const std::string& memspellCmd,
                     const std::string& targetName = "Testbuddy") {
    const std::vector<std::string> order = {
        "/cleartarget",
        "/say #unmemspells",
        memspellCmd,
        "/target " + targetName,
    };
    size_t last = 0;
    bool first = true;
    for (const std::string& cmd : order) {
        auto it = std::find(world.commands.begin(), world.commands.end(), cmd);
        ASSERT_NE(it, world.commands.end())
            << "missing " << cmd << " in: "
            << [&]() {
                   std::string joined;
                   for (const std::string& c : world.commands) {
                       if (!joined.empty()) {
                           joined += " | ";
                       }
                       joined += c;
                   }
                   return joined;
               }();
        const size_t idx = static_cast<size_t>(it - world.commands.begin());
        if (!first) {
            ASSERT_GT(idx, last) << cmd << " must come after the previous step";
        }
        last = idx;
        first = false;
    }
}

ScenarioOptions DefaultOptions() { return ScenarioOptions{}; }

}  // namespace

// ===========================================================================
// Registry
// ===========================================================================

TEST(ScenarioRegistry, KnowsTheShippedScenarios) {
    const std::vector<std::string> names = ScenarioNames();
    // 6 pre-B630 + 7 B630 (issue #48) interrupted-cast legs + 10 UIGAP-live
    // UI legs (pinned by name in tests/test_scenario_ui_legs.cpp) + B888's
    // create_character + 5 UIGAP-live2 legs (pinned by name in
    // tests/test_scenario_ui_legs.cpp).
    EXPECT_EQ(names.size(), 29u);
    EXPECT_TRUE(IsScenarioName("create_character"));
    EXPECT_TRUE(IsScenarioName("zone_in"));
    EXPECT_TRUE(IsScenarioName("two_client_survive"));
    EXPECT_TRUE(IsScenarioName("cast_lands"));
    // T494 (board B796): the observed-/equip pair.
    EXPECT_TRUE(IsScenarioName("equip_observed"));
    EXPECT_TRUE(IsScenarioName("equip_actor"));
    EXPECT_TRUE(IsScenarioName("equip_actor_return"));
    // B630: the interrupted-cast pair.
    EXPECT_TRUE(IsScenarioName("cast_interrupt"));
    EXPECT_TRUE(IsScenarioName("cast_interrupt_chain"));
    EXPECT_TRUE(IsScenarioName("cast_success_tail"));
    EXPECT_TRUE(IsScenarioName("cast_recast_stop"));
    EXPECT_TRUE(IsScenarioName("cast_buff_notake"));
    EXPECT_TRUE(IsScenarioName("cast_interrupt_observe"));
    EXPECT_TRUE(IsScenarioName("cast_fizzle"));
}

TEST(ScenarioRegistry, UnknownNameIsNullNotACrash) {
    EXPECT_FALSE(IsScenarioName("arena-idle"));
    EXPECT_EQ(MakeScenario("arena-idle", DefaultOptions()), nullptr);
}

TEST(ScenarioRegistry, EveryNameConstructsAndReportsItself) {
    for (const std::string& name : ScenarioNames()) {
        auto scenario = MakeScenario(name, DefaultOptions());
        ASSERT_NE(scenario, nullptr) << name;
        EXPECT_EQ(std::string(scenario->Name()), name);
    }
}

// ===========================================================================
// The exit-code taxonomy, owned by the runner
// ===========================================================================

TEST(ScenarioRunner, NeverZonedInIsCouldNotStartNotAFailure) {
    FakeWorld world;  // never zones in
    ScenarioRunner runner(MakeScenario("zone_in", DefaultOptions()), world,
                          /*timeout=*/60.0, /*startDeadline=*/10.0);
    Advance(runner, 30.0);
    EXPECT_TRUE(runner.finished());
    EXPECT_EQ(runner.exitCode(), kScenarioCouldNotStart);
    EXPECT_NE(runner.reason().find("never reached a zone"), std::string::npos);
}

TEST(ScenarioRunner, StartDeadlineIsClampedToTheOverallTimeout) {
    // A start deadline longer than the timeout would report a run that never
    // started as a TIMEOUT, erasing the distinction exit 3 exists for.
    FakeWorld world;
    ScenarioRunner runner(MakeScenario("zone_in", DefaultOptions()), world,
                          /*timeout=*/5.0, /*startDeadline=*/600.0);
    Advance(runner, 20.0);
    EXPECT_TRUE(runner.finished());
    EXPECT_EQ(runner.exitCode(), kScenarioCouldNotStart);
}

TEST(ScenarioRunner, ZonedInButNoVerdictIsTimeout) {
    FakeWorld world;
    world.zonedIn = true;
    world.zone = "arena";
    // two_client_survive with a hold far beyond the timeout, and a peer that
    // arrives, so it stays Running until the clock runs out.
    ScenarioOptions options;
    options.holdSeconds = 10000.0;
    world.otherPlayers = 1;
    ScenarioRunner runner(MakeScenario("two_client_survive", options), world,
                          /*timeout=*/20.0, /*startDeadline=*/10.0);
    Advance(runner, 40.0);
    EXPECT_TRUE(runner.finished());
    EXPECT_EQ(runner.exitCode(), kScenarioTimeout);
    EXPECT_NE(runner.reason().find("no verdict within"), std::string::npos);
}

TEST(ScenarioRunner, AnUnfinishedRunNeverReportsSuccess) {
    // The external-quit guard: the client stopped for some other reason
    // (SIGTERM, /quit) with the scenario still running.
    FakeWorld world;
    world.zonedIn = true;
    world.zone = "arena";
    ScenarioOptions options;
    options.holdSeconds = 10000.0;
    world.otherPlayers = 1;
    ScenarioRunner runner(MakeScenario("two_client_survive", options), world,
                          /*timeout=*/600.0, /*startDeadline=*/10.0);
    Advance(runner, 5.0);
    ASSERT_FALSE(runner.finished());
    EXPECT_NE(runner.exitCode(), kScenarioPass);
    EXPECT_EQ(runner.exitCode(), kScenarioTimeout);
}

TEST(ScenarioRunner, DoesNotStepTheScenarioBeforeZoneIn) {
    // cast_lands issues its first prep command 3s after ZONE-IN, not 3s after
    // process start -- otherwise every scenario would have to re-implement
    // the pre-flight itself.
    FakeWorld world;
    ScenarioRunner runner(MakeScenario("cast_lands", DefaultOptions()), world,
                          /*timeout=*/600.0, /*startDeadline=*/500.0);
    Advance(runner, 60.0);
    EXPECT_TRUE(world.commands.empty());
    world.zonedIn = true;
    world.zone = "arena";
    Advance(runner, 5.0);
    EXPECT_TRUE(world.sent("#set invulnerable off"));
}

TEST(ScenarioRunner, ExitCodeNamesCoverTheWholeTaxonomy) {
    EXPECT_STREQ(ScenarioExitCodeName(kScenarioPass), "pass");
    EXPECT_STREQ(ScenarioExitCodeName(kScenarioFail), "fail");
    EXPECT_STREQ(ScenarioExitCodeName(kScenarioTimeout), "timeout");
    EXPECT_STREQ(ScenarioExitCodeName(kScenarioCouldNotStart), "could-not-start");
    EXPECT_EQ(kScenarioPass, 0);
    EXPECT_EQ(kScenarioFail, 1);
    EXPECT_EQ(kScenarioTimeout, 2);
    EXPECT_EQ(kScenarioCouldNotStart, 3);
}

// ===========================================================================
// zone_in
// ===========================================================================

TEST(ZoneInScenario, PassesOnTheFirstStepAfterZoneIn) {
    FakeWorld world;
    world.zonedIn = true;
    world.zone = "arena";
    ScenarioRunner runner(MakeScenario("zone_in", DefaultOptions()), world,
                          /*timeout=*/60.0, /*startDeadline=*/30.0);
    Advance(runner, 5.0);
    EXPECT_TRUE(runner.finished());
    EXPECT_EQ(runner.exitCode(), kScenarioPass);
    EXPECT_NE(runner.reason().find("arena"), std::string::npos);
}

TEST(ZoneInScenario, ZonedInWithNoZoneNameIsAFailureNotAPass) {
    // The case a "Zone connection complete" log grep cannot see at all.
    FakeWorld world;
    world.zonedIn = true;
    world.zone = "";
    ScenarioRunner runner(MakeScenario("zone_in", DefaultOptions()), world,
                          /*timeout=*/60.0, /*startDeadline=*/30.0);
    Advance(runner, 5.0);
    EXPECT_TRUE(runner.finished());
    EXPECT_EQ(runner.exitCode(), kScenarioFail);
}

// ===========================================================================
// create_character (B888, issue #204)
// ===========================================================================

namespace {
ScenarioOptions CreateOptions() {
    ScenarioOptions o;
    o.expectCharacterName = "Cinewbie";
    o.expectHomeZones = {"qeynos", "qeynos2", "qrg"};  // data/char_create.tsv QEYNOS
    return o;
}
int RunCreate(FakeWorld& world, const ScenarioOptions& o) {
    world.zonedIn = true;
    ScenarioRunner runner(MakeScenario("create_character", o), world, 60.0, 30.0);
    Advance(runner, 2.0);
    EXPECT_TRUE(runner.finished());
    return runner.exitCode();
}
}  // namespace

TEST(CreateCharacterScenario, PassesForTheCreatedCharacterInItsHomeCity) {
    FakeWorld world;
    world.playerName = "Cinewbie";
    world.zone = "qeynos2";
    EXPECT_EQ(RunCreate(world, CreateOptions()), kScenarioPass);
}

TEST(CreateCharacterScenario, TheTutorialIsAFailureEvenIfListed) {
    // Will 2026-09-23: home city only. World:TutorialZoneID 189 = tutorialb.
    FakeWorld world;
    world.playerName = "Cinewbie";
    world.zone = "tutorialb";
    ScenarioOptions o = CreateOptions();
    o.expectHomeZones.push_back("tutorialb");
    EXPECT_EQ(RunCreate(world, o), kScenarioFail);
}

TEST(CreateCharacterScenario, AnotherCityOrAnotherCharacterFails) {
    FakeWorld wrongZone;
    wrongZone.playerName = "Cinewbie";
    wrongZone.zone = "freportw";
    EXPECT_EQ(RunCreate(wrongZone, CreateOptions()), kScenarioFail);

    FakeWorld wrongName;
    wrongName.playerName = "Testchar";
    wrongName.zone = "qeynos2";
    EXPECT_EQ(RunCreate(wrongName, CreateOptions()), kScenarioFail);
}

TEST(CreateCharacterScenario, WithoutCreateCharacterItRefuses) {
    FakeWorld world;
    world.zone = "qeynos2";
    EXPECT_EQ(RunCreate(world, DefaultOptions()), kScenarioFail);
}

// ===========================================================================
// two_client_survive
// ===========================================================================

TEST(TwoClientSurviveScenario, PassesWhenThePeerArrivesAndBothHold) {
    FakeWorld world;
    world.zonedIn = true;
    world.zone = "arena";
    ScenarioOptions options;
    options.holdSeconds = 30.0;
    ScenarioRunner runner(MakeScenario("two_client_survive", options), world,
                          /*timeout=*/600.0, /*startDeadline=*/30.0);
    Advance(runner, 10.0);
    EXPECT_FALSE(runner.finished());
    world.otherPlayers = 1;
    Advance(runner, 31.0);
    EXPECT_TRUE(runner.finished());
    EXPECT_EQ(runner.exitCode(), kScenarioPass);
    EXPECT_NE(runner.reason().find("held"), std::string::npos);
}

TEST(TwoClientSurviveScenario, NoPeerAtAllIsAFailure) {
    // The hand rig printed SURVIVED when the partner never logged in.
    FakeWorld world;
    world.zonedIn = true;
    world.zone = "arena";
    ScenarioOptions options;
    options.holdSeconds = 10.0;
    ScenarioRunner runner(MakeScenario("two_client_survive", options), world,
                          /*timeout=*/600.0, /*startDeadline=*/30.0);
    Advance(runner, 200.0);
    EXPECT_TRUE(runner.finished());
    EXPECT_EQ(runner.exitCode(), kScenarioFail);
    EXPECT_NE(runner.reason().find("no second player appeared"), std::string::npos);
}

TEST(TwoClientSurviveScenario, PeerVanishingMidHoldIsAFailure) {
    FakeWorld world;
    world.zonedIn = true;
    world.zone = "arena";
    world.otherPlayers = 1;
    ScenarioOptions options;
    options.holdSeconds = 100.0;
    ScenarioRunner runner(MakeScenario("two_client_survive", options), world,
                          /*timeout=*/600.0, /*startDeadline=*/30.0);
    Advance(runner, 20.0);
    ASSERT_FALSE(runner.finished());
    world.otherPlayers = 0;
    Advance(runner, 2.0);
    EXPECT_TRUE(runner.finished());
    EXPECT_EQ(runner.exitCode(), kScenarioFail);
    EXPECT_NE(runner.reason().find("vanished"), std::string::npos);
}

TEST(TwoClientSurviveScenario, ObserverLeavingTheZoneMidHoldIsAFailure) {
    FakeWorld world;
    world.zonedIn = true;
    world.zone = "arena";
    world.otherPlayers = 1;
    ScenarioOptions options;
    options.holdSeconds = 100.0;
    ScenarioRunner runner(MakeScenario("two_client_survive", options), world,
                          /*timeout=*/600.0, /*startDeadline=*/30.0);
    Advance(runner, 20.0);
    ASSERT_FALSE(runner.finished());
    world.zonedIn = false;
    Advance(runner, 2.0);
    EXPECT_TRUE(runner.finished());
    EXPECT_EQ(runner.exitCode(), kScenarioFail);
    EXPECT_NE(runner.reason().find("left the zone"), std::string::npos);
}

// ===========================================================================
// cast_lands
// ===========================================================================

TEST(CastLandsScenario, RunsT401sPrepRecipeInOrderThenCastsFromTheGem) {
    FakeWorld world;
    world.zonedIn = true;
    world.zone = "arena";
    ScenarioRunner runner(MakeScenario("cast_lands", DefaultOptions()), world,
                          /*timeout=*/600.0, /*startDeadline=*/30.0);
    Advance(runner, 25.0);
    ASSERT_GE(world.commands.size(), 5u);
    EXPECT_EQ(world.commands[0], "/say #set invulnerable off");
    EXPECT_EQ(world.commands[1], "/say #unmemspells");
    // #memspell takes the gem 0-based; /cast takes it 1-based.
    EXPECT_EQ(world.commands[2], "/say #memspell 288 0");  // B484: 288, not 46
    EXPECT_EQ(world.commands[3], "/target Testcaster");
    EXPECT_EQ(world.commands[4], "/cast 1");
}

TEST(CastLandsScenario, PassesOnlyWhenTheBUFFAPPEARS) {
    FakeWorld world;
    world.zonedIn = true;
    world.zone = "arena";
    ScenarioRunner runner(MakeScenario("cast_lands", DefaultOptions()), world,
                          /*timeout=*/600.0, /*startDeadline=*/30.0);
    Advance(runner, 25.0);
    ASSERT_FALSE(runner.finished());
    // A cast the server refused leaves the buff list alone -- still Running.
    Advance(runner, 5.0);
    EXPECT_FALSE(runner.finished());
    // The OP_Action landing (T348) reaches BuffManager.
    world.buffs.push_back(288);  // the default spell (B484)
    Advance(runner, 1.0);
    EXPECT_TRUE(runner.finished());
    EXPECT_EQ(runner.exitCode(), kScenarioPass);
    EXPECT_NE(runner.reason().find("landed"), std::string::npos);
}

TEST(CastLandsScenario, ABuffThatWasALREADYThereIsNotALanding) {
    FakeWorld world;
    world.zonedIn = true;
    world.zone = "arena";
    world.buffs.push_back(288);  // already buffed at zone-in (B484: 288 is the default)
    ScenarioRunner runner(MakeScenario("cast_lands", DefaultOptions()), world,
                          /*timeout=*/600.0, /*startDeadline=*/30.0);
    Advance(runner, 30.0);
    EXPECT_FALSE(runner.finished());
    world.buffs.push_back(288);  // a SECOND copy: the cast landed
    Advance(runner, 1.0);
    EXPECT_TRUE(runner.finished());
    EXPECT_EQ(runner.exitCode(), kScenarioPass);
}

TEST(CastLandsScenario, ThreeSilentCastsIsAFailure) {
    FakeWorld world;
    world.zonedIn = true;
    world.zone = "arena";
    ScenarioRunner runner(MakeScenario("cast_lands", DefaultOptions()), world,
                          /*timeout=*/600.0, /*startDeadline=*/30.0);
    Advance(runner, 120.0);
    EXPECT_TRUE(runner.finished());
    EXPECT_EQ(runner.exitCode(), kScenarioFail);
    EXPECT_NE(runner.reason().find("never landed"), std::string::npos);
    const auto casts = std::count(world.commands.begin(), world.commands.end(),
                                  std::string("/cast 1"));
    EXPECT_EQ(casts, 3);
}

TEST(CastLandsScenario, TheSpellIdIsAnOptionNotAConstant) {
    FakeWorld world;
    world.zonedIn = true;
    world.zone = "arena";
    ScenarioOptions options;
    options.castSpellId = 93;
    ScenarioRunner runner(MakeScenario("cast_lands", options), world,
                          /*timeout=*/600.0, /*startDeadline=*/30.0);
    Advance(runner, 25.0);
    EXPECT_TRUE(world.sent("#memspell 93 0"));
    world.buffs.push_back(288);  // the DEFAULT spell must not satisfy this run (B484)
    Advance(runner, 2.0);
    EXPECT_FALSE(runner.finished());
    world.buffs.push_back(93);
    Advance(runner, 1.0);
    EXPECT_TRUE(runner.finished());
    EXPECT_EQ(runner.exitCode(), kScenarioPass);
}

// B484 (issue #35): the default used to be 46 (Ultravision), whose
// spells_new.classes12 is 255 -- the level-1 Wizard in the shipped config
// sat far under it, so the legacy fizzle model (which this server provably
// runs) clamped its fizzle chance to 95% and the lane passed only about 1 in
// 7 runs. The default now names a spell the class can actually cast:
// Minor Shielding, 288.
TEST(CastLandsScenario, TheDefaultSpellIsOneTheConfigClassCanCast) {
    EXPECT_EQ(DefaultOptions().castSpellId, 288u);
}

// ===========================================================================
// equip_observed / equip_actor -- T494 (board B796)
// ===========================================================================
//
// The observer's whole claim is that it asserted on something only the SERVER
// could have written. These cases pin that it never sends a command, that it
// fails when no WearChange arrives, and that it passes on the 0 -> non-zero
// transition -- with no client, no server and no network.

TEST(EquipObservedScenario, SendsNothingAndFailsWhenThePeerNeverHoldsAnything) {
    FakeWorld world;
    world.zonedIn = true;
    world.zone = "qeynos2";
    world.otherPlayers = 1;
    world.peerMaterial = 0;  // peer is empty-handed and stays that way

    ScenarioRunner runner(MakeEquipObservedScenario(DefaultOptions()), world, 600.0, 60.0);
    Advance(runner, 300.0);

    EXPECT_EQ(runner.exitCode(), kScenarioFail);
    EXPECT_TRUE(world.commands.empty()) << "the observer must send nothing";
    EXPECT_NE(runner.reason().find("never left"), std::string::npos) << runner.reason();
}

// The trap B796's own experiment fell into: a peer that was ALREADY holding
// the weapon proves nothing, so the run refuses rather than passing.
TEST(EquipObservedScenario, FailsWhenThePeerIsAlreadyHoldingSomething) {
    FakeWorld world;
    world.zonedIn = true;
    world.zone = "qeynos2";
    world.otherPlayers = 1;
    world.peerMaterial = 5;

    ScenarioRunner runner(MakeEquipObservedScenario(DefaultOptions()), world, 600.0, 60.0);
    Advance(runner, 5.0);

    EXPECT_EQ(runner.exitCode(), kScenarioFail);
    EXPECT_NE(runner.reason().find("ALREADY holding"), std::string::npos) << runner.reason();
}

// The peer logging out mid-watch is EXPECTED -- the actor equips and exits.
TEST(EquipObservedScenario, ToleratesThePeerLeavingAndPassesWhenItComesBackArmed) {
    FakeWorld world;
    world.zonedIn = true;
    world.zone = "qeynos2";
    world.otherPlayers = 1;
    world.peerMaterial = 0;

    ScenarioRunner runner(MakeEquipObservedScenario(DefaultOptions()), world, 600.0, 60.0);
    Advance(runner, 5.0);
    world.otherPlayers = 0;                 // actor equipped and logged out
    Advance(runner, 20.0);
    world.otherPlayers = 1;                 // and came straight back
    world.peerMaterial = 5;                 // wearing what the database says
    Advance(runner, 5.0);

    EXPECT_EQ(runner.exitCode(), kScenarioPass);
    EXPECT_TRUE(world.commands.empty());
    EXPECT_NE(runner.reason().find("0 -> 5"), std::string::npos) << runner.reason();
}

TEST(EquipObservedScenario, PassesOnTheZeroToNonZeroTransition) {
    FakeWorld world;
    world.zonedIn = true;
    world.zone = "qeynos2";
    world.otherPlayers = 1;
    world.peerMaterial = 0;

    ScenarioRunner runner(MakeEquipObservedScenario(DefaultOptions()), world, 600.0, 60.0);
    Advance(runner, 10.0);
    world.peerMaterial = 5;  // the server's OP_WearChange: IT5
    Advance(runner, 5.0);

    EXPECT_EQ(runner.exitCode(), kScenarioPass);
    EXPECT_TRUE(world.commands.empty());
    EXPECT_NE(runner.reason().find("0 -> 5"), std::string::npos) << runner.reason();
}

TEST(EquipObservedScenario, FailsWhenNoPeerEverAppears) {
    FakeWorld world;
    world.zonedIn = true;
    world.zone = "qeynos2";
    world.otherPlayers = 0;

    ScenarioRunner runner(MakeEquipObservedScenario(DefaultOptions()), world, 600.0, 60.0);
    Advance(runner, 200.0);

    EXPECT_EQ(runner.exitCode(), kScenarioFail);
    EXPECT_NE(runner.reason().find("no second player"), std::string::npos) << runner.reason();
}

TEST(EquipActorReturnScenario, SendsNothingAtAll) {
    FakeWorld world;
    world.zonedIn = true;
    world.zone = "qeynos2";

    ScenarioRunner runner(MakeEquipActorReturnScenario(DefaultOptions()), world, 600.0, 60.0);
    Advance(runner, 70.0);

    EXPECT_EQ(runner.exitCode(), kScenarioPass);
    EXPECT_TRUE(world.commands.empty());
}

// T498 (board B755/B823): the SELF half of the /equip proof. The observer
// process reads the server's number off the peer's spawn packet; the actor's
// own return login reads the number IT derived from the same item. Before T498
// the two came from different rules (the wire's items.idfile vs
// data/item_models.json) and could differ -- item 2894 is 10004 on the server
// and 10009 in the file.
TEST(EquipActorReturnScenario, PassesWhenItsOwnMaterialIsTheExpectedOne) {
    FakeWorld world;
    world.zonedIn = true;
    world.zone = "qeynos2";
    world.selfMaterial = 10004;

    ScenarioOptions options = DefaultOptions();
    options.expectSelfMaterial = 10004;
    ScenarioRunner runner(MakeEquipActorReturnScenario(options), world, 600.0, 60.0);
    Advance(runner, 70.0);

    EXPECT_EQ(runner.exitCode(), kScenarioPass) << runner.reason();
    EXPECT_NE(runner.reason().find("10004"), std::string::npos) << runner.reason();
}

TEST(EquipActorReturnScenario, FailsWhenItsOwnMaterialIsTheItemModelsJsonNumber) {
    FakeWorld world;
    world.zonedIn = true;
    world.zone = "qeynos2";
    world.selfMaterial = 10009;  // what data/item_models.json says for item 2894

    ScenarioOptions options = DefaultOptions();
    options.expectSelfMaterial = 10004;  // what the server's items.idfile says
    ScenarioRunner runner(MakeEquipActorReturnScenario(options), world, 600.0, 60.0);
    Advance(runner, 70.0);

    EXPECT_EQ(runner.exitCode(), kScenarioFail);
    EXPECT_NE(runner.reason().find("the self path is NOT"), std::string::npos) << runner.reason();
}

TEST(EquipActorScenario, SettlesThenTypesTheDbSlotNumber) {
    FakeWorld world;
    world.zonedIn = true;
    world.zone = "qeynos2";

    ScenarioRunner runner(MakeEquipActorScenario(DefaultOptions()), world, 600.0, 60.0);
    Advance(runner, 10.0);
    EXPECT_TRUE(world.commands.empty()) << "must not type before the inventory has arrived";

    Advance(runner, 20.0);
    // 23 is `inventory.slot_id` for general slot 1 -- the convention T494 set.
    EXPECT_TRUE(world.sent("/equip 23 13")) << "actor never sent the equip";
    EXPECT_TRUE(world.sent("/equip list"));
}

// ===========================================================================
// B630 (issue #48) + B760 (issue #45): the two-client interrupted-cast lane
// ===========================================================================
//
// These scenarios are pure drivers: they run their leg's command sequence on a
// calibrated clock and pass on completion. The wire-level assertions (cast bar
// clears, the interrupt chat, no late impact, the B760 packet order) are made
// from the run's DEBUG log by scripts/ci/b760_analyze.py, so what a headless
// state machine can pin is the SEQUENCE and its failure modes -- not a
// log grep.

ScenarioOptions B630Options(const std::string& target = "Testbuddy") {
    ScenarioOptions options;
    options.castTargetName = target;
    options.castSpellId = 378;  // O'Keils Radiation, 3 s cast, targettype 5
    options.successSpellId = 372;  // Blast of Cold
    options.recastStopSpellId = 293;  // Haze, 2 s cast / 4 s recast
    options.buffNotakeVictimId = 293;  // Haze: the caster's own buff leg
    return options;
}

TEST(CastInterruptScenario, RefusesWhenNoTargetNameIsSupplied) {
    FakeWorld world;
    world.zonedIn = true;
    world.zone = "cazicthule";
    ScenarioRunner runner(MakeScenario("cast_interrupt", DefaultOptions()), world,
                          /*timeout=*/60.0, /*startDeadline=*/10.0);
    Advance(runner, 5.0);
    EXPECT_TRUE(runner.finished());
    EXPECT_EQ(runner.exitCode(), kScenarioFail);
    EXPECT_NE(runner.reason().find("--scenario-cast-target"), std::string::npos);
}

TEST(CastInterruptScenario, PrepThenTargetThenCastThenInterruptsMidCast) {
    FakeWorld world;
    world.zonedIn = true;
    world.zone = "cazicthule";
    world.otherPlayers = 1;  // the co-located peer is tracked from the start
    ScenarioRunner runner(MakeScenario("cast_interrupt", B630Options()), world,
                          /*timeout=*/600.0, /*startDeadline=*/10.0);
    Advance(runner, 16.0);
    // The full A-leg sequence, in order: B630 item 1 adds the leading
    // /cleartarget (eqemu's GM #unmemspells/#memspell apply to c->GetTarget(),
    // so the prep must run with nothing targeted); then unmem, memspell,
    // target, cast, and the GM #interrupt ~1.5 s into the 3 s cast (cast at
    // t=14, interrupt at t=15.5, so advance past 15.5 to have all six sent).
    ASSERT_GE(world.commands.size(), 6u);
    EXPECT_EQ(world.commands[0], "/cleartarget");
    EXPECT_EQ(world.commands[1], "/say #unmemspells");
    EXPECT_EQ(world.commands[2], "/say #memspell 378 0");
    EXPECT_EQ(world.commands[3], "/target Testbuddy");
    EXPECT_EQ(world.commands[4], "/cast 1");
    EXPECT_EQ(world.commands[5], "/say #interrupt");
    // Still holding after the interrupt (the log is what proves the cast bar
    // cleared) -- not finished yet (interrupt t=15.5 + hold 10s = t=25.5).
    Advance(runner, 5.0);
    EXPECT_FALSE(runner.finished());
    Advance(runner, 10.0);
    EXPECT_TRUE(runner.finished());
    EXPECT_EQ(runner.exitCode(), kScenarioPass);
    EXPECT_NE(runner.reason().find("interrupt leg executed"), std::string::npos);
}

TEST(CastInterruptScenario, NoPeerIsAFailureNotASilentWait) {
    FakeWorld world;
    world.zonedIn = true;
    world.zone = "cazicthule";
    world.otherPlayers = 0;
    ScenarioRunner runner(MakeScenario("cast_interrupt", B630Options()), world,
                          /*timeout=*/600.0, /*startDeadline=*/10.0);
    Advance(runner, 100.0);
    EXPECT_TRUE(runner.finished());
    EXPECT_EQ(runner.exitCode(), kScenarioFail);
    EXPECT_NE(runner.reason().find("never appeared"), std::string::npos);
    EXPECT_FALSE(world.sent("/cast 1")) << "must not cast at no one";
}

TEST(CastSuccessTailScenario, PrepRunsTargetFreeThenCastsTheDeclaredSpell) {
    FakeWorld world;
    world.zonedIn = true;
    world.zone = "cazicthule";
    world.otherPlayers = 1;
    ScenarioRunner runner(MakeScenario("cast_success_tail", B630Options()), world,
                          /*timeout=*/600.0, /*startDeadline=*/10.0);
    Advance(runner, 15.0);
    // B630 item 1: the D1 prep must be cleartarget -> unmem -> memspell 372
    // -> target, in that order, so the caster's own gem 0 carries 372 and the
    // buddy's does not.
    ExpectPrepOrder(world, "/say #memspell 372 0");
    EXPECT_TRUE(world.sent("/target Testbuddy"));
    EXPECT_TRUE(world.sent("/cast 1"));
    EXPECT_FALSE(runner.finished());
    Advance(runner, 13.0);
    EXPECT_TRUE(runner.finished());
    EXPECT_EQ(runner.exitCode(), kScenarioPass);
    EXPECT_NE(runner.reason().find("success-tail leg executed"), std::string::npos);
}

TEST(CastRecastStopScenario, TwoCastsWithTheSecondInsideTheRecastWindow) {
    FakeWorld world;
    world.zonedIn = true;
    world.zone = "cazicthule";
    world.otherPlayers = 1;
    ScenarioRunner runner(MakeScenario("cast_recast_stop", B630Options()), world,
                          /*timeout=*/600.0, /*startDeadline=*/10.0);
    Advance(runner, 15.0);
    EXPECT_TRUE(world.sent("/cast 1"));
    ASSERT_EQ(std::count(world.commands.begin(), world.commands.end(),
                         std::string("/cast 1")), 1)
        << "no second cast before the gem recovers";
    // B630 item 1: D2 got its own memspell prep, target-free.
    ExpectPrepOrder(world, "/say #memspell 293 0");
    Advance(runner, 5.0);
    EXPECT_EQ(std::count(world.commands.begin(), world.commands.end(),
                         std::string("/cast 1")), 2)
        << "the second cast must land inside the server recast window";
    Advance(runner, 10.0);
    EXPECT_TRUE(runner.finished());
    EXPECT_EQ(runner.exitCode(), kScenarioPass);
    EXPECT_NE(runner.reason().find("recast-stop leg executed"), std::string::npos);
}

TEST(CastBuffNotakeScenario, MemorisesThenCastsTheOwnBuff) {
    FakeWorld world;
    world.zonedIn = true;
    world.zone = "cazicthule";
    world.otherPlayers = 1;
    ScenarioRunner runner(MakeScenario("cast_buff_notake", B630Options()), world,
                          /*timeout=*/600.0, /*startDeadline=*/10.0);
    Advance(runner, 15.0);
    // The buff leg: B630 item 1's /cleartarget, then unmem, memspell the buff
    // on gem 0, target the peer, cast.
    ASSERT_GE(world.commands.size(), 5u);
    EXPECT_EQ(world.commands[0], "/cleartarget");
    EXPECT_EQ(world.commands[1], "/say #unmemspells");
    EXPECT_EQ(world.commands[2], "/say #memspell 293 0");
    EXPECT_EQ(world.commands[3], "/target Testbuddy");
    EXPECT_EQ(world.commands[4], "/cast 1");
    ExpectPrepOrder(world, "/say #memspell 293 0");
    Advance(runner, 12.0);
    EXPECT_TRUE(runner.finished());
    EXPECT_EQ(runner.exitCode(), kScenarioPass);
    EXPECT_NE(runner.reason().find("buff-notake leg executed"), std::string::npos);
}

TEST(CastInterruptChainScenario, RunsAllFourLegsInOrder) {
    FakeWorld world;
    world.zonedIn = true;
    world.zone = "cazicthule";
    world.otherPlayers = 1;
    ScenarioRunner runner(MakeScenario("cast_interrupt_chain", B630Options()), world,
                          /*timeout=*/600.0, /*startDeadline=*/10.0);
    Advance(runner, 600.0);
    EXPECT_TRUE(runner.finished());
    EXPECT_EQ(runner.exitCode(), kScenarioPass);
    // The chain fires each leg's tell-tale command: A's #interrupt trigger and
    // four caster gem casts (A once, D1 once, D2 twice, D3 once).
    EXPECT_TRUE(world.sent("/say #interrupt")) << "leg A's trigger";
    EXPECT_EQ(std::count(world.commands.begin(), world.commands.end(),
                         std::string("/cast 2")), 0)
        << "no leg uses gem 2 in this rig";
    EXPECT_EQ(std::count(world.commands.begin(), world.commands.end(),
                         std::string("/cast 1")), 5);
}

TEST(CastInterruptChainScenario, RefusesWhenNoTargetNameIsSupplied) {
    FakeWorld world;
    world.zonedIn = true;
    world.zone = "cazicthule";
    world.otherPlayers = 1;
    ScenarioRunner runner(MakeScenario("cast_interrupt_chain", DefaultOptions()),
                          world, /*timeout=*/60.0, /*startDeadline=*/10.0);
    Advance(runner, 5.0);
    EXPECT_TRUE(runner.finished());
    EXPECT_EQ(runner.exitCode(), kScenarioFail);
    EXPECT_NE(runner.reason().find("--scenario-cast-target"), std::string::npos);
}

TEST(CastInterruptObserveScenario, TargetsTheCasterAndHoldsSendingNothingElse) {
    FakeWorld world;
    world.zonedIn = true;
    world.zone = "cazicthule";
    world.otherPlayers = 1;
    ScenarioOptions options = B630Options();
    options.holdSeconds = 20.0;
    // For the observer, castTargetName is the CASTER's name (the peer it
    // watches): the observer targets that character and then holds it.
    options.castTargetName = "Testcaster";
    ScenarioRunner runner(MakeScenario("cast_interrupt_observe", options), world,
                          /*timeout=*/600.0, /*startDeadline=*/10.0);
    Advance(runner, 30.0);
    EXPECT_TRUE(runner.finished());
    EXPECT_EQ(runner.exitCode(), kScenarioPass);
    ASSERT_EQ(world.commands.size(), 1u) << "the observer sends nothing but the target";
    EXPECT_EQ(world.commands[0], "/target Testcaster");
    EXPECT_NE(runner.reason().find("observer held"), std::string::npos);
}

TEST(CastInterruptObserveScenario, CasterDepartureAfterTheFloorIsAPass) {
    // B630 fix pass: in the chain lane the caster's clean exit ends the
    // observer early. Once the departure floor (45 s after targeting) has
    // elapsed, a vanished caster is the END OF THE CHAIN, not a defect: the
    // observer's log is what the analyzer asserts, and this path must not
    // fail the lane ~45 s after the chain finished (devrun-1's false
    // negative. The world drops its peer at t=51.5 (45.5 s after the t=6
    // target), past the 45 s floor and inside the chain.
    FakeWorld world;
    world.zonedIn = true;
    world.zone = "cazicthule";
    world.otherPlayers = 1;
    world.peerVanishesAt = 51.5;
    ScenarioOptions options = B630Options();
    options.holdSeconds = 120.0;  // the chain lane's hold
    options.castTargetName = "Testcaster";
    ScenarioRunner runner(MakeScenario("cast_interrupt_observe", options), world,
                          /*timeout=*/600.0, /*startDeadline=*/10.0);
    // Still holding while the caster is present.
    Advance(runner, 50.0, &world);
    EXPECT_FALSE(runner.finished());
    // The caster departs at t=51; the floor (45 s after the t=6 target) is
    // already met, so this is a pass with the departure reason.
    Advance(runner, 10.0, &world);
    EXPECT_TRUE(runner.finished());
    EXPECT_EQ(runner.exitCode(), kScenarioPass);
    EXPECT_NE(runner.reason().find("caster departed after"), std::string::npos);
    ASSERT_EQ(world.commands.size(), 1u) << "the observer sent only the target";
}

TEST(CastInterruptObserveScenario, CasterDepartureBeforeTheFloorIsAFailure) {
    // The negative of the floor: a caster that vanishes almost immediately
    // never ran its legs, so the observer must still fail. The world drops
    // its peer at t=10 (4 s after the t=6 target, far short of the 45 s
    // floor), so the old "caster left the zone during the hold" applies.
    FakeWorld world;
    world.zonedIn = true;
    world.zone = "cazicthule";
    world.otherPlayers = 1;
    world.peerVanishesAt = 10.0;
    ScenarioOptions options = B630Options();
    options.holdSeconds = 120.0;
    options.castTargetName = "Testcaster";
    ScenarioRunner runner(MakeScenario("cast_interrupt_observe", options), world,
                          /*timeout=*/600.0, /*startDeadline=*/10.0);
    Advance(runner, 30.0, &world);
    EXPECT_TRUE(runner.finished());
    EXPECT_EQ(runner.exitCode(), kScenarioFail);
    EXPECT_NE(runner.reason().find("caster left the zone during the hold"),
              std::string::npos);
}

TEST(CastInterruptObserveScenario, RefusesWhenNoTargetNameIsSupplied) {
    FakeWorld world;
    world.zonedIn = true;
    world.zone = "cazicthule";
    world.otherPlayers = 1;
    ScenarioRunner runner(MakeScenario("cast_interrupt_observe", DefaultOptions()),
                          world, /*timeout=*/60.0, /*startDeadline=*/10.0);
    Advance(runner, 5.0);
    EXPECT_TRUE(runner.finished());
    EXPECT_EQ(runner.exitCode(), kScenarioFail);
    EXPECT_NE(runner.reason().find("--scenario-cast-target"), std::string::npos);
}

TEST(CastFizzleScenario, RefusesWhenNoFizzleSpellIsSupplied) {
    // The default fizzle spell is 0: without --scenario-fizzle-spell the leg
    // would have to guess a spell, so it refuses instead.
    FakeWorld world;
    world.zonedIn = true;
    world.zone = "cazicthule";
    ScenarioRunner runner(MakeScenario("cast_fizzle", DefaultOptions()), world,
                          /*timeout=*/60.0, /*startDeadline=*/10.0);
    Advance(runner, 5.0);
    EXPECT_TRUE(runner.finished());
    EXPECT_EQ(runner.exitCode(), kScenarioFail);
    EXPECT_NE(runner.reason().find("--scenario-fizzle-spell"), std::string::npos);
}

TEST(CastFizzleScenario, PrepThenLoopsGemCastsOnAThrottledCadence) {
    FakeWorld world;
    world.zonedIn = true;
    world.zone = "cazicthule";
    ScenarioOptions options = B630Options();
    options.fizzleSpellId = 41;
    ScenarioRunner runner(MakeScenario("cast_fizzle", options), world,
                          /*timeout=*/600.0, /*startDeadline=*/10.0);
    Advance(runner, 14.0);
    EXPECT_EQ(world.commands[0], "/target Testbuddy");
    EXPECT_EQ(world.commands[1], "/say #unmemspells");
    EXPECT_EQ(world.commands[2], "/say #memspell 41 0");
    EXPECT_EQ(world.commands[3], "/cast 1");
    // Throttled: at 14 s only the first cast has fired.
    ASSERT_EQ(std::count(world.commands.begin(), world.commands.end(),
                         std::string("/cast 1")), 1);
    Advance(runner, 30.0);  // ~7 more casts on the 4 s cadence
    const int casts = std::count(world.commands.begin(), world.commands.end(),
                                 std::string("/cast 1"));
    EXPECT_GT(casts, 1);
    EXPECT_LT(casts, 20) << "the cadence must keep a landed cast from clobbering the next";
    // A fixed attempt count terminates the leg even if nothing fizzles.
    Advance(runner, 120.0);
    EXPECT_TRUE(runner.finished());
    EXPECT_EQ(runner.exitCode(), kScenarioPass);
    EXPECT_NE(runner.reason().find("fizzle leg executed"), std::string::npos);
}
