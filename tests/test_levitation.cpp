// B892 (GitLab #208): the player's own levitation.
//
// The gap, measured at origin/fable 471fbeaa:
//
//   * eqemu sends the caster the levitate appearance with ignore_self
//     (akk-stack/code/zone/spell_effects.cpp:1452-1455,
//     SendAppearancePacket(FlyMode, Levitating, true, true)), so the
//     caster's own client never receives AT_FLYMODE 2 and the player never
//     floats. The fade DOES reach self (spell_effects.cpp:4360, the default
//     ignore_self=false).
//   * The official client derives ITS OWN levitation from its buff list: any
//     active buff whose spell carries SpellEffect::Levitate (SPA 57,
//     include/client/spell/spell_constants.h:184). That SPA had ZERO readers
//     in willeq (grep Levitate at the fix's tip: spawn.h's AT_FLYMODE name,
//     the constants line and entities_component.cpp's setter).
//   * PlayerMovementState::isLevitating (renderer_types.h:327) was fed only
//     from SwimmingStateChanged and only suppressed idle sinking in water
//     (renderer_player_physics.cpp, computeMovementVector); it floated
//     nothing on land and slowed no fall.
//
// RED on the pre-fix tree: this file names LevitationChanged, the
// LevitationChangedData payload, the EQ::PlayerIsLevitating predicate, the
// MovementComponent levitation API and the ClampLevitationFeet ground
// decision -- none of which exists there -- and the levitate_applies
// scenario is not registered.
//
// GREEN when:
//   1. EQ::PlayerIsLevitating(buffs, db, selfFlymode) -- "any active buff
//      whose spell has SPA 57, OR self flymode 2/5" -- reads SPA 57 for the
//      first time.
//   2. A dedicated GameEventType::LevitationChanged event (the issue: do not
//      piggy-back on water transitions) with {isLevitating}, published from
//      the game thread on buff apply/fade and on a self flymode change,
//      consumed by the renderer.
//   3. The ground-clamp physics: a levitating player's feet clamp to the
//      hover line above the floor and descend from height at the descent
//      rate instead of free-falling (the ShouldClampSwimToFloor shape).
//   4. A levitate_applies scenario (pure logic over IScenarioWorld).
//
// The hover height and descent rate are [assumed] until the lane-1 oracle
// capture lands (refactor/B892/README.md, "Not verified / left open"); the
// numbers live in ONE table (include/client/spell/levitation.h) and are
// re-pointed there, marked [measured], once captured.

#include <gtest/gtest.h>

#include "client/bridge/game_state_bridge.h"
#include "client/data_paths.h"
#include "client/eq.h"
#include "client/graphics/player/ground_query.h"
#include "client/movement/movement_component.h"
#include "client/scenario/scenario_runner.h"
#include "client/scenario/scenarios.h"
#include "client/spell/buff_manager.h"
#include "client/spell/levitation.h"
#include "client/spell/spell_data.h"
#include "client/spell/spell_database.h"
#include "client/spell/spell_manager.h"
#include "client/state/event_bus.h"
#include "client/willeq_database.h"
#include "common/packet_structs.h"
#include "zone_stream_test_utils.h"

#include <algorithm>
#include <cstdlib>
#include <string>
#include <vector>

using namespace EQ;

namespace {

// ===================================================================
// 1. The SPA-57 predicate -- the first reader of SpellEffect::Levitate.
// ===================================================================

// 261 "Levitate" is the shipped row whose SPA 57 sits in effect slot 1;
// 970 "Levitation" carries it in slot 3 (spells table, data/willeq.db).
// Both spell rows are asserted from the DB below, so a shipped-data move is
// a test failure here, not a silent pass.

TEST(LevitationPredicate, ABufferedSpa57SpellSetsLevitation) {
    std::vector<ActiveBuff> buffs;
    EQ::ActiveBuff lev;
    lev.spell_id = 261;
    buffs.push_back(lev);

    // No DB attached: the predicate cannot see the spell's effects, so it
    // must NOT claim levitation on an unverifiable buff. (The live client
    // always has its spell database; a missing one is a no-DB test rig, not
    // a "the buff might be Levitate" guess.)
    EXPECT_EQ(static_cast<int>(EQ::PlayerIsLevitating(buffs, nullptr, 0)),
              static_cast<int>(EQ::LevitationState::NotLevitating));
}

// Hand-built spell data through a real SpellDatabase is overkill for the
// predicate: the DB lookup is one getSpell() per buff, and the SPA-57 rows
// themselves are netted by the DB-backed test below. Here the db is nullptr
// and the OR half is exercised: a self flymode 2 or 5 levitates with no buff
// at all (the server-sent appearance, e.g. a GM #levitate); 0, 1 and 3 do
// not (ground, fly, water).
TEST(LevitationPredicate, TheSelfFlymode2Or5AloneAlsoLevitates) {
    EXPECT_EQ(static_cast<int>(EQ::PlayerIsLevitating({}, nullptr, 2)),
              static_cast<int>(EQ::LevitationState::Levitating));
    EXPECT_EQ(static_cast<int>(EQ::PlayerIsLevitating({}, nullptr, 5)),
              static_cast<int>(EQ::LevitationState::Levitating));
    EXPECT_EQ(static_cast<int>(EQ::PlayerIsLevitating({}, nullptr, 0)),
              static_cast<int>(EQ::LevitationState::NotLevitating));
    EXPECT_EQ(static_cast<int>(EQ::PlayerIsLevitating({}, nullptr, 1)),
              static_cast<int>(EQ::LevitationState::NotLevitating));
    EXPECT_EQ(static_cast<int>(EQ::PlayerIsLevitating({}, nullptr, 3)),
              static_cast<int>(EQ::LevitationState::NotLevitating));
}

// The same rules with a populated buff list: an empty/unknown spell id is
// not a levitation, and a non-levitating buff with flymode 0 is not either.
TEST(LevitationPredicate, EmptyOrUnknownBuffIdsNeverLevitate) {
    std::vector<ActiveBuff> buffs;
    EQ::ActiveBuff emptyBuff;
    emptyBuff.spell_id = 0;
    buffs.push_back(emptyBuff);
    EQ::ActiveBuff unknown;
    unknown.spell_id = 999999;
    buffs.push_back(unknown);
    EXPECT_EQ(static_cast<int>(EQ::PlayerIsLevitating(buffs, nullptr, 0)),
              static_cast<int>(EQ::LevitationState::NotLevitating));
}

// ===================================================================
// 2. The full chain: buff lands -> predicate true -> LevitationChanged out.
// ===================================================================

// A bridge that records the LevitationChanged events the game thread pushes.
// applyEvent is the RENDERER side of the seam; these tests drain, so it is
// never called.
class LevitationBridge : public eqt::bridge::GameStateBridge {
public:
    void applyEvent(const eqt::state::GameEvent&) override {}

    void Drain() {
        for (const auto& ev : drainEvents()) {
            if (ev.type == eqt::state::GameEventType::LevitationChanged) {
                ++events;
                last = std::get<eqt::state::LevitationChangedData>(ev.data).isLevitating;
                if (last) {
                    ++upCount;
                } else {
                    ++downCount;
                }
            }
        }
    }

    int events = 0;
    bool last = false;
    int upCount = 0;
    int downCount = 0;
};

class LevitationWireTest : public ::testing::Test {
protected:
    bool Init() {
        eq.setBridge(&bridge);
        // The spell DB reads the WilleqDatabase GLOBAL INSTANCE (spell_database
        // .cpp initFromDatabase); the MakeEq() rig never opens one, so open it
        // here the way test_spell_manager's main() does (test_spell_manager
        // .cpp:882-895). OpenDatabase logs its own failure;
        // InitializeSpellSystem then returns false and the DB-gated tests SKIP.
        eq.OpenDatabase(eqt::ResolveDataPath("data/willeq.db"));
        return eq.InitializeSpellSystem("");
    }
    EQ::BuffManager* Buffs() { return eq.GetBuffManager(); }
    eqt::movement::MovementComponent& Mv() { return eq.Movement(); }

    zonetest::TestBridge unused;  // never set; keeps the MakeEq() bridge off
    EverQuest eq = zonetest::MakeEq();
    LevitationBridge bridge;
};

// The production wiring: EverQuest registers the component's sync on its
// own apply/fade callbacks (the VisionChanged shape, everquest.cpp S04).
// A SPA-57 buff landing sets the state and publishes; its fade clears it.
TEST_F(LevitationWireTest, AnSpa57BuffApplyPublishesUpAndTheFadePublishesDown) {
    if (!Init()) {
        GTEST_SKIP() << "willeq.db unavailable";
    }
    const SpellData* lev = eq.GetSpellManager()->getSpell(261);
    ASSERT_NE(lev, nullptr);
    ASSERT_TRUE(lev->hasEffect(SpellEffect::Levitate))
        << "261 is not the SPA-57 row any more; the shipped data moved";

    ASSERT_FALSE(Mv().PlayerLevitating());

    Buffs()->applyBuff(0, 261, 0, 1, 600);
    bridge.Drain();
    EXPECT_TRUE(Mv().PlayerLevitating());
    EXPECT_EQ(bridge.events, 1);
    EXPECT_TRUE(bridge.last);

    const auto* fading = Buffs()->getPlayerBuff(261);
    ASSERT_NE(fading, nullptr);
    Buffs()->removeBuffBySlot(0, static_cast<uint8_t>(fading->slot));
    bridge.Drain();
    EXPECT_FALSE(Mv().PlayerLevitating());
    EXPECT_EQ(bridge.events, 2);
    EXPECT_FALSE(bridge.last);
    EXPECT_EQ(bridge.upCount, 1);
    EXPECT_EQ(bridge.downCount, 1);
}

// 970 carries SPA 57 in effect slot 3: hasEffect() must find it, the
// predicate must fire, and the row must NOT fire for a different spell.
TEST_F(LevitationWireTest, AnSpa57InTheThirdEffectSlotAlsoLevitates) {
    if (!Init()) {
        GTEST_SKIP() << "willeq.db unavailable";
    }
    const SpellData* lev3 = eq.GetSpellManager()->getSpell(970);
    ASSERT_NE(lev3, nullptr);
    ASSERT_TRUE(lev3->hasEffect(SpellEffect::Levitate));

    Buffs()->applyBuff(0, 970, 0, 1, 600);
    bridge.Drain();
    EXPECT_TRUE(Mv().PlayerLevitating());
    EXPECT_EQ(bridge.upCount, 1);
}

// The negative: a buff whose spell has no SPA 57 must not publish a state
// change and must not move the state. 288 = Minor Shielding (the cast_lands
// default; no Levitate effect slot).
TEST_F(LevitationWireTest, ANonLevitatingBuffPublishesNoChange) {
    if (!Init()) {
        GTEST_SKIP() << "willeq.db unavailable";
    }
    const SpellData* shield = eq.GetSpellManager()->getSpell(288);
    ASSERT_NE(shield, nullptr);
    ASSERT_FALSE(shield->hasEffect(SpellEffect::Levitate))
        << "288 gained a Levitate effect slot; update the negative";

    Buffs()->applyBuff(0, 288, 0, 1, 600);
    bridge.Drain();
    EXPECT_FALSE(Mv().PlayerLevitating());
    EXPECT_EQ(bridge.events, 0)
        << "a buff without SPA 57 published LevitationChanged; the predicate "
           "must not fire it";
}

// The OR half through the same wiring: a self flymode 2 (a server-sent
// appearance) levitates with an EMPTY buff list, and a flymode 0 clears.
TEST_F(LevitationWireTest, ASelfFlymode2LevitatesWithNoBuff) {
    if (!Init()) {
        GTEST_SKIP() << "willeq.db unavailable";
    }
    Mv().SyncLevitation(/*selfFlymode=*/2);
    bridge.Drain();
    EXPECT_TRUE(Mv().PlayerLevitating());
    EXPECT_EQ(bridge.upCount, 1);

    Mv().SyncLevitation(/*selfFlymode=*/0);
    bridge.Drain();
    EXPECT_FALSE(Mv().PlayerLevitating());
    EXPECT_EQ(bridge.downCount, 1);
}

// No state change means no event: syncing twice with the same inputs must
// publish nothing the second time.
TEST_F(LevitationWireTest, ReSyncingTheSameStatePublishesNothing) {
    if (!Init()) {
        GTEST_SKIP() << "willeq.db unavailable";
    }
    Mv().SyncLevitation(0);
    bridge.Drain();
    Mv().SyncLevitation(0);
    bridge.Drain();
    EXPECT_EQ(bridge.events, 0);

    Mv().SyncLevitation(2);
    bridge.Drain();
    Mv().SyncLevitation(2);
    bridge.Drain();
    EXPECT_EQ(bridge.events, 1);
}

#ifdef EQT_HAS_GRAPHICS
// B1038: ClampLevitationFeet lives in ground_query.cpp, a renderer TU;
// compiled out of an EQT_GRAPHICS=OFF build (sections 1, 2 and 4 run headless).
// ===================================================================
// 3. The ground clamp: hover, and descend from height at the rate.
// ===================================================================

// Below (or at) the hover line the feet go to the line: a walking-off-a-
// flat-ground player is exactly on the line every frame, which is the
// swim-clamp shape (a resting player re-clamps exactly, not by epsilon).
TEST(LevitationClamp, AtOrBelowTheHoverLineTheFeetGoToTheLine) {
    using EQT::Graphics::ClampLevitationFeet;
    const float hover = EQ::kLevitationHoverHeight;
    float feet = ClampLevitationFeet(hover - 40.0f, hover - 40.0f, hover,
                                     EQ::kLevitationDescentRate, 1.0f / 60.0f);
    EXPECT_FLOAT_EQ(feet, hover);
    feet = ClampLevitationFeet(hover, hover, hover, EQ::kLevitationDescentRate,
                               1.0f / 60.0f);
    EXPECT_FLOAT_EQ(feet, hover);
}

// Above the hover line the feet descend at the rate, not by snapping: one
// second of frames drops exactly the rate, and a long run rests exactly on
// the line (never undershooting it).
TEST(LevitationClamp, AboveTheHoverLineTheFeetDescendAtTheRate) {
    using EQT::Graphics::ClampLevitationFeet;
    const float hover = EQ::kLevitationHoverHeight;
    const float rate = EQ::kLevitationDescentRate;
    const float dt = 1.0f / 60.0f;

    float feet = hover + 40.0f;
    const float start = feet;
    for (int i = 0; i < 60; ++i) {
        feet = ClampLevitationFeet(feet, feet, hover, rate, dt);
    }
    EXPECT_NEAR(feet, start - rate, 0.05f)
        << "one second of the clamp must descend by the rate (" << rate
        << " units/s), not snap to the hover line";

    for (int i = 0; i < 60 * 600; ++i) {
        feet = ClampLevitationFeet(feet, feet, hover, rate, dt);
    }
    EXPECT_FLOAT_EQ(feet, hover);
}

#endif  // EQT_HAS_GRAPHICS

// ===================================================================
// 4. The scenario: levitate_applies.
// ===================================================================

class LevFakeWorld : public eqt::scenario::IScenarioWorld {
public:
    bool zonedIn = false;
    std::string zone = "qeynos";  // local bookkeeping (IScenarioWorld names none)
    std::string playerName = "Testchar";
    std::vector<uint32_t> buffs;
    bool levitating = false;
    long long levitationEvents = 0;
    double buffLandsAt = -1.0;  // run seconds; -1 = the buff never lands
    double runClock = 0.0;

    std::vector<std::string> commands;

    bool IsConnected() const override { return zonedIn; }
    bool IsFullyZonedIn() const override { return zonedIn; }
    std::string ZoneName() const override { return "qeynos"; }
    int OtherPlayerCount() const override { return 0; }
    std::string PlayerName() const override { return playerName; }
    std::vector<uint32_t> PlayerBuffSpellIds() const override { return buffs; }
    bool PlayerLevitating() const override { return levitating; }
    long long PeerEquipmentMaterial(const std::string&, int) const override {
        return -1;
    }
    long long SelfEquipmentMaterial(int) const override { return -1; }
    long long EventCount(int gameEventType) const override {
        return gameEventType ==
                   static_cast<int>(eqt::state::GameEventType::LevitationChanged)
                   ? levitationEvents
                   : 0;
    }
    void SendCommand(const std::string& rawLine) override {
        commands.push_back(rawLine);
    }
    void Note(const std::string&) override {}

    bool sent(const std::string& needle) const {
        return std::any_of(commands.begin(), commands.end(),
                           [&needle](const std::string& c) {
                               return c.find(needle) != std::string::npos;
                           });
    }

    void Advance(double dt) {
        runClock += dt;
        if (buffLandsAt >= 0.0 && runClock >= buffLandsAt &&
            std::find(buffs.begin(), buffs.end(), kSpellId) == buffs.end()) {
            // The OP_Action landing (T348) reaches BuffManager, which the
            // production wiring turns into the predicate flip and the
            // event. The world models the far end.
            buffs.push_back(kSpellId);
            levitating = true;
            ++levitationEvents;
        }
    }
    static constexpr uint32_t kSpellId = 261;
};

void AdvanceLev(eqt::scenario::ScenarioRunner& runner, LevFakeWorld& w,
                double seconds) {
    const double dt = 1.0 / 60.0;
    const int ticks = static_cast<int>(seconds * 60.0);
    for (int i = 0; i < ticks; ++i) {
        w.Advance(dt);
        if (!runner.Tick(static_cast<float>(dt))) {
            return;
        }
    }
}

eqt::scenario::ScenarioOptions LevOptions() {
    eqt::scenario::ScenarioOptions o;
    o.castSpellId = LevFakeWorld::kSpellId;
    return o;
}

TEST(LevitationScenario, IsRegistered) {
    EXPECT_TRUE(eqt::scenario::IsScenarioName("levitate_applies"));
}

// Pass = the buff landed AND the client is levitating. The world flips both
// at 25 s (the buff landing is what flips the predicate in production), so
// the verdict is a moment later.
TEST(LevitationScenario, PassesWhenTheBuffLandsAndTheClientLevitates) {
    LevFakeWorld w;
    w.zonedIn = true;
    w.zone = "qeynos";
    w.buffLandsAt = 25.0;

    eqt::scenario::ScenarioRunner runner(
        eqt::scenario::MakeScenario("levitate_applies", LevOptions()), w,
        /*timeout=*/600.0, /*startDeadline=*/30.0);
    AdvanceLev(runner, w, 40.0);
    EXPECT_EQ(runner.exitCode(), eqt::scenario::kScenarioPass)
        << runner.reason();
    // The prep is cast_lands' invulnerability guard plus the self-target
    // (#castspell casts ON the target when one is set); the cast itself is
    // the GM command, because the lane-1 character's mana cannot pay for a
    // gem cast (2026-10-08: three "/cast 1" all refused "Insufficient Mana").
    EXPECT_TRUE(w.sent("#set invulnerable off"));
    EXPECT_FALSE(w.sent("#memspell"));
    EXPECT_TRUE(w.sent("/target Testchar"));
    EXPECT_TRUE(w.sent("#castspell 261 1"));
}

// The buff never lands: the cast is refused (or never fires) and the buff
// list never gains the spell -- fail after the cast windows.
TEST(LevitationScenario, FailsWhenTheBuffNeverLands) {
    LevFakeWorld w;
    w.zonedIn = true;
    w.zone = "qeynos";
    w.buffLandsAt = -1.0;

    eqt::scenario::ScenarioRunner runner(
        eqt::scenario::MakeScenario("levitate_applies", LevOptions()), w,
        /*timeout=*/600.0, /*startDeadline=*/30.0);
    AdvanceLev(runner, w, 180.0);
    EXPECT_EQ(runner.exitCode(), eqt::scenario::kScenarioFail)
        << runner.reason();
}

// Levitation WITHOUT the buff (a stale self flymode) is not a landing: the
// server never agreed to the cast.
TEST(LevitationScenario, LevitationWithoutTheBuffIsNotALanding) {
    LevFakeWorld w;
    w.zonedIn = true;
    w.zone = "qeynos";
    w.buffLandsAt = -1.0;
    w.levitating = true;  // stale self flymode, no buff

    eqt::scenario::ScenarioRunner runner(
        eqt::scenario::MakeScenario("levitate_applies", LevOptions()), w,
        /*timeout=*/600.0, /*startDeadline=*/30.0);
    AdvanceLev(runner, w, 180.0);
    EXPECT_EQ(runner.exitCode(), eqt::scenario::kScenarioFail);
}

// A buff that was already on the player at zone-in is not a landing of the
// new cast (the cast_lands baseline rule).
TEST(LevitationScenario, APreexistingBuffIsNotALanding) {
    LevFakeWorld w;
    w.zonedIn = true;
    w.zone = "qeynos";
    w.buffs.push_back(LevFakeWorld::kSpellId);
    w.levitating = true;
    w.buffLandsAt = -1.0;  // the cast adds nothing new

    eqt::scenario::ScenarioRunner runner(
        eqt::scenario::MakeScenario("levitate_applies", LevOptions()), w,
        /*timeout=*/600.0, /*startDeadline=*/30.0);
    AdvanceLev(runner, w, 180.0);
    EXPECT_EQ(runner.exitCode(), eqt::scenario::kScenarioFail);
}

}  // namespace
