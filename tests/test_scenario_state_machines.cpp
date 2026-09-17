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

    std::vector<std::string> commands;
    std::vector<std::string> notes;

    bool IsConnected() const override { return connected; }
    bool IsFullyZonedIn() const override { return zonedIn; }
    std::string ZoneName() const override { return zone; }
    int OtherPlayerCount() const override { return otherPlayers; }
    std::string PlayerName() const override { return playerName; }
    std::vector<uint32_t> PlayerBuffSpellIds() const override { return buffs; }
    long long PeerEquipmentMaterial(const std::string&, int) const override {
        return otherPlayers >= 1 ? peerMaterial : -1;
    }
    void SendCommand(const std::string& rawLine) override { commands.push_back(rawLine); }
    void Note(const std::string& line) override { notes.push_back(line); }

    bool sent(const std::string& needle) const {
        return std::any_of(commands.begin(), commands.end(),
            [&needle](const std::string& c) { return c.find(needle) != std::string::npos; });
    }
};

/** Advance a runner by `seconds` in 1/60 s ticks, or until it finishes. */
void Advance(ScenarioRunner& runner, double seconds) {
    const float dt = 1.0f / 60.0f;
    const int ticks = static_cast<int>(seconds * 60.0);
    for (int i = 0; i < ticks; ++i) {
        if (!runner.Tick(dt)) {
            return;
        }
    }
}

ScenarioOptions DefaultOptions() { return ScenarioOptions{}; }

}  // namespace

// ===========================================================================
// Registry
// ===========================================================================

TEST(ScenarioRegistry, KnowsTheShippedScenarios) {
    const std::vector<std::string> names = ScenarioNames();
    EXPECT_EQ(names.size(), 6u);
    EXPECT_TRUE(IsScenarioName("zone_in"));
    EXPECT_TRUE(IsScenarioName("two_client_survive"));
    EXPECT_TRUE(IsScenarioName("cast_lands"));
    // T494 (board B796): the observed-/equip pair.
    EXPECT_TRUE(IsScenarioName("equip_observed"));
    EXPECT_TRUE(IsScenarioName("equip_actor"));
    EXPECT_TRUE(IsScenarioName("equip_actor_return"));
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
    EXPECT_EQ(world.commands[2], "/say #memspell 46 0");
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
    world.buffs.push_back(46);
    Advance(runner, 1.0);
    EXPECT_TRUE(runner.finished());
    EXPECT_EQ(runner.exitCode(), kScenarioPass);
    EXPECT_NE(runner.reason().find("landed"), std::string::npos);
}

TEST(CastLandsScenario, ABuffThatWasALREADYThereIsNotALanding) {
    FakeWorld world;
    world.zonedIn = true;
    world.zone = "arena";
    world.buffs.push_back(46);  // already buffed at zone-in
    ScenarioRunner runner(MakeScenario("cast_lands", DefaultOptions()), world,
                          /*timeout=*/600.0, /*startDeadline=*/30.0);
    Advance(runner, 30.0);
    EXPECT_FALSE(runner.finished());
    world.buffs.push_back(46);  // a SECOND copy: the cast landed
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
    world.buffs.push_back(46);  // the DEFAULT spell must not satisfy this run
    Advance(runner, 2.0);
    EXPECT_FALSE(runner.finished());
    world.buffs.push_back(93);
    Advance(runner, 1.0);
    EXPECT_TRUE(runner.finished());
    EXPECT_EQ(runner.exitCode(), kScenarioPass);
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
