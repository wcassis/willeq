// T482 (board B631) -- how a cast the server has CONFIRMED (OP_BeginCast)
// ends, fed as the bytes eqemu puts on the Titanium wire, through the real zone
// dispatcher, into a headless EverQuest with the shipped spell database.
//
// What the caster receives (akk-stack/code, read at T482; every path below is
// zone/spells.cpp unless named):
//
//  * SUCCESS. Mob::CastedSpellFinished -> SpellFinished (:1744) first runs the
//    spell: SpellOnTarget queues OP_Action effect_flag 0 to the caster (:4008)
//    and, once it lands, OP_Action effect_flag 4 (:4676). SpellFinished then
//    takes the mana (:2765 -> Client::SetMana -> CheckManaEndUpdate, a
//    ManaChange with keepcasting 1, client.cpp:2427-2433). Back in
//    CastedSpellFinished the non-bard client tail is, contiguously:
//      SendSpellBarEnable (:1817)      -> OP_ManaChange keepcasting 0 (:5752)
//      SetLinkedSpellReuseTimer (:1821) -> OP_LinkedReuse, only for timer_id > 0
//      MemorizeSpell(slot, spell, memSpellSpellbar) (:1824, client.cpp:3346)
//                                      -> OP_MemorizeSpell scribing 3 --
//                                         "this causes the delayed refresh of
//                                         the spell bar gems" (:1819)
//    A pulsing bard song sends no keepcasting 0 at all, only the MemorizeSpell
//    (:1803, Character:EnableBardMelody defaults true, ruletypes.h:190).
//
//  * SUCCESS WITH A FULL RESIST. SpellOnTarget still queues the effect_flag 0
//    action FIRST (:3957-3962: "we do this first, that way we get the particles
//    even if the spell doesn't land"), then TARGET_RESISTED 425 as a
//    FormattedMessage (:4492) and returns false before the effect_flag 4 repeat
//    (:4531). SpellFinished returns false for that only when the spell is a
//    beneficial buff (:2590-2593); a detrimental spell carries on, the mana is
//    taken and the recast is set, and CastedSpellFinished sends the SUCCESS tail
//    above. eqemu treats a resisted nuke as a cast that happened.
//
//  * STOPPED AFTER BEGINCAST. The recast-timer arms (:1421-1423, :1437-1439)
//    send SPELL_RECAST 236 as a SimpleMessage and then StopCasting() (:1351),
//    whose only packet is OP_ManaChange keepcasting 0 (:1369-1376). The same
//    StopCasting follows SpellFinished returning false (:1749) -- e.g. a
//    beneficial buff that did not take hold, AFTER its effect_flag 0 action.
//    No OP_MemorizeSpell, no mana taken, no recast.
//
// So the keepcasting-0 ManaChange is common to both, and the packet that
// separates them is the OP_MemorizeSpell memSpellSpellbar that only the success
// tail sends. Before T482 fable ignored keepcasting 0 after BeginCast, ignored
// scribing 3, and completed its own cast as a SUCCESS on the effect_flag 0
// action (or, with no action, from update()'s duration + 5 s fallback), so a
// stopped cast was reported as a success with its recast started.

#include <gtest/gtest.h>

#define private public
#define protected public
#include "client/eq.h"
#include "client/spell/spell_manager.h"
#undef private
#undef protected

#include "client/bridge/game_state_bridge.h"
#include "client/state/event_bus.h"
#include "eqstr_test_db.h"
#include "spell_wire_payloads.h"
#include "zone_stream_test_utils.h"

#include <chrono>
#include <string>
#include <vector>

using namespace zonetest;
using namespace spellwire;

namespace {

constexpr uint16_t kSelfId = 460;
constexpr uint16_t kNpcId = 300;
constexpr uint8_t kGem = 2;
constexpr uint32_t kCastTimeMs = 2500;

// data/willeq.db
constexpr uint32_t kBurstOfFlame = 93;     // detrimental, recast 2500 ms
constexpr uint32_t kMinorShielding = 288;  // beneficial self buff

// zone/string_ids.h, common/eq_constants.h
constexpr uint32_t kSpellRecast = 236;
constexpr uint32_t kTargetResisted = 425;
constexpr uint32_t kChatRed = 13;
constexpr uint32_t kChatSpellFailure = 289;
constexpr uint32_t kMemSpellSpellbar = 3;  // zone/client.h:105

struct Seen {
    int castComplete = 0;
    uint8_t lastResult = 0xFF;
    int impactArming = 0;  // SpellCastVisualComplete with isCastAnimation
};

Seen Look(const std::vector<eqt::state::GameEvent>& events) {
    Seen s;
    for (const auto& e : events) {
        if (e.type == eqt::state::GameEventType::SpellCastComplete) {
            ++s.castComplete;
            s.lastResult = std::get<eqt::state::SpellCastCompleteData>(e.data).result;
        }
        if (e.type == eqt::state::GameEventType::SpellCastVisualComplete &&
            std::get<eqt::state::SpellCastVisualCompleteData>(e.data).isCastAnimation) {
            ++s.impactArming;
        }
    }
    return s;
}

const uint8_t kSuccess = static_cast<uint8_t>(EQ::CastResult::Success);

class SpellCastEndWire : public ::testing::Test {
protected:
    EverQuest eq = MakeEq();
    TestBridge bridge;
    EQ::SpellManager* mgr = nullptr;

    void SetUp() override {
        ASSERT_TRUE(eqt_test::ShippedStrings().isLoaded()) << "data/willeq.db is missing";
        ASSERT_TRUE(eq.InitializeSpellSystem(""));
        eq.Test_SetMySpawnIdForSeam(kSelfId);
        mgr = eq.GetSpellManager();
        ASSERT_NE(mgr, nullptr);
        ASSERT_NE(mgr->getSpell(kBurstOfFlame), nullptr);
        ASSERT_NE(mgr->getSpell(kMinorShielding), nullptr);
        mgr->setBridge(&bridge);
    }

    void TearDown() override {
        if (mgr) mgr->setBridge(nullptr);
    }

    // What beginCastFromGem() leaves behind (the same staging as
    // test_spell_interrupt_wire.cpp), then the server's BeginCast.
    void StageConfirmedGemCast(uint32_t spell, uint16_t target) {
        mgr->m_spell_gems[kGem] = spell;
        mgr->m_gem_states[kGem] = EQ::GemState::Casting;
        mgr->m_is_casting = true;
        mgr->m_current_spell_id = spell;
        mgr->m_current_gem_slot = kGem;
        mgr->m_current_target_id = target;
        mgr->m_cast_start_time = std::chrono::steady_clock::now();
        mgr->m_cast_duration_ms = kCastTimeMs;
        mgr->m_waiting_for_server_confirm = true;
        eq.GetGameState().spells().setCasting(true, spell, target, kCastTimeMs);
        eq.GetGameState().spells().setGem(kGem, spell, eqt::state::SpellGemState::Casting);

        ReplayZone(eq, HC_OP_BeginCast,
                   BeginCastWire(kSelfId, static_cast<uint16_t>(spell), kCastTimeMs));
        ASSERT_TRUE(mgr->isCasting());
        ASSERT_FALSE(mgr->m_waiting_for_server_confirm) << "BeginCast must confirm the cast";
        bridge.drainEvents();
    }

    // Some later packet from the server -- a regen tick (keepcasting 1).
    void ReplayLaterPacket() {
        ReplayZone(eq, HC_OP_ManaChange, ManaChangePayload(95, 100, 0, 1));
    }

    void ReplaySuccessTail(uint32_t spell) {
        ReplayZone(eq, HC_OP_ManaChange, ManaChangePayload(80, 100, spell, 0));
        ReplayZone(eq, HC_OP_MemorizeSpell, MemorizeSpellPayload(kGem, spell, kMemSpellSpellbar));
    }

    void ExpectCastFinishedAsSuccessWithRecast(const Seen& s) {
        EXPECT_FALSE(mgr->isCasting());
        EXPECT_FALSE(eq.GetGameState().spells().isCasting());
        EXPECT_EQ(mgr->getGemState(kGem), EQ::GemState::Refresh)
            << "the server's MemorizeSpell memSpellSpellbar is the gem refresh";
        EXPECT_EQ(s.castComplete, 1);
        EXPECT_EQ(s.lastResult, kSuccess);
    }

    void ExpectCastStoppedNotSuccess(const Seen& s) {
        EXPECT_FALSE(mgr->isCasting());
        EXPECT_FALSE(eq.GetGameState().spells().isCasting());
        EXPECT_EQ(mgr->getGemState(kGem), EQ::GemState::Ready)
            << "StopCasting starts no recast; the gem is usable at once";
        EXPECT_TRUE(eq.GetGameState().spells().isGemReady(kGem));
        EXPECT_EQ(s.castComplete, 1);
        EXPECT_NE(s.lastResult, kSuccess) << "the server stopped this cast";
        EXPECT_NE(s.lastResult, 0xFF);
    }
};

// ---------------------------------------------------------------------------
// The lead: a post-BeginCast StopCasting().
// ---------------------------------------------------------------------------

TEST_F(SpellCastEndWire, RecastTimerStopAfterBeginCastEndsTheCastNotAsSuccess) {
    StageConfirmedGemCast(kBurstOfFlame, kNpcId);
    ReplayZone(eq, HC_OP_SimpleMessage, SimpleMessagePayload(kSpellRecast, kChatRed));
    ReplayZone(eq, HC_OP_ManaChange, ManaChangePayload(100, 100, kBurstOfFlame, 0));

    EXPECT_FALSE(mgr->isCasting())
        << "keepcasting 0 re-enables the spell bar (SendSpellBarEnable / StopCasting)";

    ReplayLaterPacket();
    const Seen s = Look(bridge.drainEvents());
    ExpectCastStoppedNotSuccess(s);
    EXPECT_EQ(s.impactArming, 0) << "no OP_Action was sent; nothing may be painted";

    // Nor may the old duration + 5 s fallback turn it into a success later.
    mgr->m_cast_start_time = std::chrono::steady_clock::now() -
        std::chrono::milliseconds(kCastTimeMs + 6000);
    mgr->update(0.0f);
    const Seen later = Look(bridge.drainEvents());
    EXPECT_EQ(later.castComplete, 0);
    EXPECT_EQ(later.impactArming, 0);
    EXPECT_EQ(mgr->getGemState(kGem), EQ::GemState::Ready);
}

// Will's rule: after the stop the server accepts a new cast, so the client
// must not refuse it as AlreadyCasting (nor as a recast it never started).
TEST_F(SpellCastEndWire, NextCastIsNotRefusedAfterAPostBeginCastStop) {
    StageConfirmedGemCast(kBurstOfFlame, kNpcId);
    ReplayZone(eq, HC_OP_SimpleMessage, SimpleMessagePayload(kSpellRecast, kChatRed));
    ReplayZone(eq, HC_OP_ManaChange, ManaChangePayload(100, 100, kBurstOfFlame, 0));

    const EQ::CastResult next = mgr->beginCastFromGem(kGem, kNpcId);
    EXPECT_NE(next, EQ::CastResult::AlreadyCasting);
    EXPECT_NE(next, EQ::CastResult::GemCooldown);
}

// A buff that does not take hold: SpellOnTarget's effect_flag 0 action goes
// out first, SpellFinished returns false (:2590-2593) and CastedSpellFinished
// calls StopCasting (:1749). The action must not complete the cast as a
// success.
TEST_F(SpellCastEndWire, BuffThatDoesNotTakeHoldEndsNotAsSuccessDespiteItsAction) {
    StageConfirmedGemCast(kMinorShielding, kSelfId);
    ReplayZone(eq, HC_OP_Action, ActionPayload(kSelfId, kSelfId, 50,
                                               static_cast<uint16_t>(kMinorShielding), 0));
    ReplayZone(eq, HC_OP_ManaChange, ManaChangePayload(100, 100, kMinorShielding, 0));
    ReplayLaterPacket();

    const Seen s = Look(bridge.drainEvents());
    ExpectCastStoppedNotSuccess(s);
    EXPECT_EQ(s.impactArming, 1) << "the server's own effect_flag 0 action paints, once";
}

// The duration + 5 s fallback, reached only if the end packets never arrive,
// must not claim a success either.
TEST_F(SpellCastEndWire, TimeoutFallbackNeverReportsSuccess) {
    StageConfirmedGemCast(kBurstOfFlame, kNpcId);
    mgr->m_cast_start_time = std::chrono::steady_clock::now() -
        std::chrono::milliseconds(kCastTimeMs + 6000);
    mgr->update(0.0f);

    const Seen s = Look(bridge.drainEvents());
    EXPECT_FALSE(mgr->isCasting());
    EXPECT_EQ(s.castComplete, 1);
    EXPECT_NE(s.lastResult, kSuccess);
    EXPECT_EQ(s.impactArming, 0) << "no OP_Action arrived; nothing may be painted";
    EXPECT_NE(mgr->getGemState(kGem), EQ::GemState::Refresh);
}

// ---------------------------------------------------------------------------
// Success, in each shape the server sends it.
// ---------------------------------------------------------------------------

// No OP_Action at all (a pet summon, a spell with no SpellOnTarget): the tail
// alone ends it -- promptly, not 5 s later.
TEST_F(SpellCastEndWire, SuccessTailWithNoLandingEndsAsSuccessAndStartsTheRecast) {
    StageConfirmedGemCast(kMinorShielding, kSelfId);
    ReplayZone(eq, HC_OP_ManaChange, ManaChangePayload(80, 100, 0, 1));
    ReplaySuccessTail(kMinorShielding);

    const Seen s = Look(bridge.drainEvents());
    ExpectCastFinishedAsSuccessWithRecast(s);
    EXPECT_EQ(s.impactArming, 0) << "no OP_Action was sent; nothing may be painted";
}

// Guard: the landed pair, the mana, then the tail.
TEST_F(SpellCastEndWire, LandedSpellEndsAsSuccessWithOneImpact) {
    StageConfirmedGemCast(kBurstOfFlame, kNpcId);
    ReplayZone(eq, HC_OP_Action, ActionPayload(kNpcId, kSelfId, 50,
                                               static_cast<uint16_t>(kBurstOfFlame), 0));
    ReplayZone(eq, HC_OP_Action, ActionPayload(kNpcId, kSelfId, 50,
                                               static_cast<uint16_t>(kBurstOfFlame), 4));
    ReplayZone(eq, HC_OP_ManaChange, ManaChangePayload(80, 100, 0, 1));
    ReplaySuccessTail(kBurstOfFlame);
    ReplayLaterPacket();

    const Seen s = Look(bridge.drainEvents());
    ExpectCastFinishedAsSuccessWithRecast(s);
    EXPECT_EQ(s.impactArming, 1);
}

// Guard: a fully resisted nuke is a cast the server counts -- mana and recast
// consumed -- and the only paint is the server's effect_flag 0 action.
TEST_F(SpellCastEndWire, FullyResistedNukeEndsAsSuccessWithTheRecastConsumed) {
    StageConfirmedGemCast(kBurstOfFlame, kNpcId);
    ReplayZone(eq, HC_OP_Action, ActionPayload(kNpcId, kSelfId, 50,
                                               static_cast<uint16_t>(kBurstOfFlame), 0));
    ReplayZone(eq, HC_OP_FormattedMessage,
               FormattedMessagePayload(kTargetResisted, kChatSpellFailure, {"Burst of Flame"}));
    ReplayZone(eq, HC_OP_ManaChange, ManaChangePayload(80, 100, 0, 1));
    ReplaySuccessTail(kBurstOfFlame);
    ReplayLaterPacket();

    const Seen s = Look(bridge.drainEvents());
    ExpectCastFinishedAsSuccessWithRecast(s);
    EXPECT_EQ(s.impactArming, 1) << "exactly the server's effect_flag 0 paint, no second one";
}

// A spell with a linked recast timer puts OP_LinkedReuse between the two tail
// packets (:1820-1822). It is part of the tail, not the next word.
TEST_F(SpellCastEndWire, LinkedReuseBetweenTheTailPacketsIsStillASuccess) {
    StageConfirmedGemCast(kBurstOfFlame, kNpcId);
    ReplayZone(eq, HC_OP_ManaChange, ManaChangePayload(80, 100, kBurstOfFlame, 0));
    ReplayZone(eq, HC_OP_LinkedReuse, LinkedReusePayload(3, 1000, 988));
    ReplayZone(eq, HC_OP_MemorizeSpell,
               MemorizeSpellPayload(kGem, kBurstOfFlame, kMemSpellSpellbar));

    ExpectCastFinishedAsSuccessWithRecast(Look(bridge.drainEvents()));
}

// A bard song's tail is the MemorizeSpell alone (:1803).
TEST_F(SpellCastEndWire, SpellBarRefreshWithoutKeepcastingZeroCompletesTheCast) {
    StageConfirmedGemCast(kBurstOfFlame, kNpcId);
    ReplayZone(eq, HC_OP_MemorizeSpell,
               MemorizeSpellPayload(kGem, kBurstOfFlame, kMemSpellSpellbar));

    ExpectCastFinishedAsSuccessWithRecast(Look(bridge.drainEvents()));
}

// ---------------------------------------------------------------------------
// B760 (issue #45): an UNRELIABLE packet inside the tail.
//
// eqemu queues the success tail contiguously on the reliable stream
// (spells.cpp:1817-1824, one call, single-threaded zone), so the next RELIABLE
// packet after the keepcasting 0 is the tail's own OP_LinkedReuse /
// OP_MemorizeSpell or, for a StopCasting, whatever the server sends next. An
// unreliable send (OP_ClientUpdate, client_packet.cpp:4865; OP_SpawnAppearance
// to its target/self, mob.cpp:4142-4144) is not in that sequence: when a
// reliable tail packet is lost and resent, the unreliable one is delivered the
// moment it arrives (DaybreakConnection, OP_Packet vs a raw app packet) and so
// reaches the client INSIDE the tail. Measured live with simulated inbound
// loss (refactor/B760/README.md). It says nothing about the cast's outcome.
// ---------------------------------------------------------------------------

void ReplayUnreliable(EverQuest& eq, uint16_t opcode, const std::vector<uint8_t>& payload) {
    eq.ReplayZonePacket(opcode, payload.data(), payload.size(), /*reliable=*/false);
}

// The gem's recast follows the server's MemorizeSpell (T482).
TEST_F(SpellCastEndWire, UnreliablePacketInsideTheTailStillStartsTheRecast) {
    StageConfirmedGemCast(kBurstOfFlame, kNpcId);
    ReplayZone(eq, HC_OP_ManaChange, ManaChangePayload(80, 100, kBurstOfFlame, 0));
    ReplayUnreliable(eq, HC_OP_SpawnAppearance, SpawnAppearancePayload(kNpcId, 14, 100));
    ReplayZone(eq, HC_OP_MemorizeSpell,
               MemorizeSpellPayload(kGem, kBurstOfFlame, kMemSpellSpellbar));

    EXPECT_FALSE(mgr->isCasting());
    EXPECT_EQ(mgr->getGemState(kGem), EQ::GemState::Refresh);
}

// B760: and the RESULT is the success too, published once, at the
// MemorizeSpell -- not an Interrupted settled by the unreliable packet.
TEST_F(SpellCastEndWire, UnreliablePacketInsideTheTailDoesNotMakeASuccessInterrupted) {
    StageConfirmedGemCast(kBurstOfFlame, kNpcId);
    ReplayZone(eq, HC_OP_ManaChange, ManaChangePayload(80, 100, kBurstOfFlame, 0));
    ReplayUnreliable(eq, HC_OP_SpawnAppearance, SpawnAppearancePayload(kNpcId, 14, 100));
    ReplayUnreliable(eq, HC_OP_SpawnAppearance, SpawnAppearancePayload(kSelfId, 14, 110));
    EXPECT_EQ(Look(bridge.drainEvents()).castComplete, 0)
        << "an unreliable packet is outside the reliable tail; the outcome is still open";

    ReplayZone(eq, HC_OP_MemorizeSpell,
               MemorizeSpellPayload(kGem, kBurstOfFlame, kMemSpellSpellbar));
    ExpectCastFinishedAsSuccessWithRecast(Look(bridge.drainEvents()));
}

// B760: a StopCasting after BeginCast settles on the next RELIABLE packet --
// the server's next word -- never on an unreliable one, and never as a success.
TEST_F(SpellCastEndWire, StopCastingSettlesOnTheNextReliablePacketNotAnUnreliableOne) {
    StageConfirmedGemCast(kBurstOfFlame, kNpcId);
    ReplayZone(eq, HC_OP_SimpleMessage, SimpleMessagePayload(kSpellRecast, kChatRed));
    ReplayZone(eq, HC_OP_ManaChange, ManaChangePayload(100, 100, kBurstOfFlame, 0));
    ReplayUnreliable(eq, HC_OP_SpawnAppearance, SpawnAppearancePayload(kNpcId, 14, 100));
    EXPECT_EQ(Look(bridge.drainEvents()).castComplete, 0);
    EXPECT_EQ(mgr->getGemState(kGem), EQ::GemState::Ready)
        << "the gem and the cast bar are released at the keepcasting 0 regardless";

    ReplayLaterPacket();
    ExpectCastStoppedNotSuccess(Look(bridge.drainEvents()));
}

// Guard: a spell-bar refresh naming another spell is not our cast's tail.
TEST_F(SpellCastEndWire, SpellBarRefreshForAnotherSpellDoesNotEndTheCast) {
    StageConfirmedGemCast(kBurstOfFlame, kNpcId);
    ReplayZone(eq, HC_OP_MemorizeSpell,
               MemorizeSpellPayload(0, kMinorShielding, kMemSpellSpellbar));
    EXPECT_TRUE(mgr->isCasting());
    EXPECT_EQ(Look(bridge.drainEvents()).castComplete, 0);
}

}  // namespace
