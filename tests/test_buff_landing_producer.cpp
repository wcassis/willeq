// T348 (refactor/T348_buff_landing_producer.md, board B055): the BUFF-LANDING
// PRODUCER.
//
// T339 registered BuffManager's apply hook and proved it correct with a
// hand-driven BuffManager (tests/test_vision_buff_fade_wire.cpp). T345 then ran
// it live and the hook never fired once: casting Ultravision changed nothing,
// six frames, two casts, a zone and a #nukebuffs.
//
// Measured cause, from review/T345/logs/live_session_vision.log. The only
// production caller of setPlayerBuff() -- and therefore of the apply hook -- was
// PlayerComponent::ZoneProcessBuff()'s apply branch, gated on an OP_Buff with
// fade != 1. eqemu never sends a Titanium client one: every [SPELL] Buff: line
// in that session carries fade=1, because the only server-side sender of
// bufffade=0 (Client::SendBuffDurationPacket) is gated on
// zone->BuffTimersSuspended(). What DOES arrive when a self-cast lands is a
// pair of OP_Action packets:
//
//     SpellAction: caster=287, target=287, spell='Ultravision' (46), effect_flag=0
//     SpellAction: caster=287, target=287, spell='Ultravision' (46), effect_flag=4
//
// so the second of the pair is the landing, and it is what these tests replay.
//
// Seam: a bare EverQuest (zone_stream_test_utils.h's MakeEq()) with its live
// SpellManager and BuffManager, fed through the real ReplayZonePacket() dispatch
// into the real CommandsComponent::ZoneProcessAction. No network, no graphics,
// no eqemu. The apply hook is registered here the way
// EverQuest::InitializeSpellSystem() registers it, so what these tests observe
// is the same callback the live client hangs its vision publish off.
//
// PRE-FIX: every LandingRaisesVision/LandingRecordsTheBuff assertion below
// fails -- the buff list stays empty and the hook fires zero times.

#include <gtest/gtest.h>

#include "client/bridge/game_state_bridge.h"
#include "client/eq.h"
#include "client/spell/buff_manager.h"
#include "client/spell/spell_data.h"
#include "client/spell/spell_manager.h"
#include "client/spell/spell_database.h"
#include "client/willeq_database.h"
#include "zone_stream_test_utils.h"

#include <cstdlib>
#include <fstream>
#include <string>
#include <vector>

#ifndef WILLEQ_DB_PATH
#define WILLEQ_DB_PATH "data/willeq.db"
#endif

using namespace zonetest;

namespace {

EQT::WilleqDatabase g_willeq_db;
bool g_db_available = false;
bool HasDb() { return g_db_available; }

// Real spells in data/willeq.db:
//   46  Ultravision   -- beneficial, duration_formula 3, cap 360  (a buff)
//   276 Serpent Sight -- beneficial, duration_formula 3, cap 270  (a buff)
//   3   Summon Corpse -- beneficial, NO duration                  (not a buff)
constexpr uint32_t kUltravision = 46;
constexpr uint32_t kSerpentSight = 276;
constexpr uint32_t kNoDurationSpell = 3;

constexpr uint16_t kMySpawn = 287;   // Testchar's spawn id in the T345 session
constexpr uint16_t kOtherSpawn = 85; // Grennet Stormhand, also in that session

// The 31-byte wire Action struct, at the offsets
// CommandsComponent::ZoneProcessAction reads (opcode-inclusive, so a caller
// writes exactly what the handler reads).
std::vector<uint8_t> ActionPayload(uint16_t target, uint16_t source,
                                   uint16_t level, uint16_t spell,
                                   uint8_t type, uint8_t effect_flag) {
    return BuildPayload(33, [&](EQ::Net::DynamicPacket& p) {
        p.PutUInt16(2, target);
        p.PutUInt16(4, source);
        p.PutUInt16(6, level);
        p.PutUInt32(8, 0);      // instrument_mod
        p.PutFloat(12, 0.0f);   // force
        p.PutFloat(16, 0.0f);   // hit_heading
        p.PutFloat(20, 0.0f);   // hit_pitch
        p.PutUInt8(24, type);
        p.PutUInt16(25, 0);     // unknown23
        p.PutUInt16(27, 0);     // unknown25
        p.PutUInt16(29, spell);
        p.PutUInt8(31, static_cast<uint8_t>(level));  // spell_level
        p.PutUInt8(32, effect_flag);
    });
}

// The two packets a landed self-cast produces, in the order T345 measured.
constexpr uint8_t kSpellEffectAction = 231;  // 0xE7
constexpr uint8_t kAnimationPacket = 0;
constexpr uint8_t kLandedPacket = 4;

// The whole chain, observed at its far end. EverQuest::InitializeSpellSystem()
// builds the BuffManager and registers T339's two vision hooks on it; those
// hooks publish a VisionChanged event through the bridge. So a bridge that
// records what was pushed sees exactly what the live client's renderer would.
class RecordingBridge : public eqt::bridge::GameStateBridge {
public:
    void applyEvent(const eqt::state::GameEvent&) override {}

    int visionEvents = 0;
    uint8_t lastVision = 0;

    void Drain() {
        for (const auto& ev : drainEvents()) {
            if (ev.type == eqt::state::GameEventType::VisionChanged) {
                ++visionEvents;
                lastVision = std::get<eqt::state::VisionChangedData>(ev.data).visionType;
            }
        }
    }
};

// A live EverQuest with a real spell DB, a real BuffManager, T339's real hooks
// and the T345 session's spawn id.
struct LandingHarness {
    EverQuest eq = MakeEq();
    RecordingBridge bridge;

    bool Init() {
        eq.setBridge(&bridge);
        if (!eq.InitializeSpellSystem("")) return false;
        eq.Test_SetMySpawnIdForSeam(kMySpawn);
        return eq.GetBuffManager() != nullptr;
    }

    EQ::BuffManager* Buffs() { return eq.GetBuffManager(); }

    // Everything published since the last look.
    RecordingBridge& Vision() { bridge.Drain(); return bridge; }

    // The animation packet, then the landing packet -- the measured pair.
    void CastOnSelf(uint32_t spell, uint16_t casterLevel = 50) {
        ReplayZone(eq, HC_OP_Action,
                   ActionPayload(kMySpawn, kMySpawn, casterLevel,
                                 static_cast<uint16_t>(spell), kSpellEffectAction,
                                 kAnimationPacket));
        ReplayZone(eq, HC_OP_Action,
                   ActionPayload(kMySpawn, kMySpawn, casterLevel,
                                 static_cast<uint16_t>(spell), kSpellEffectAction,
                                 kLandedPacket));
    }
};

}  // namespace

// ---------------------------------------------------------------------------
// The landing itself.
// ---------------------------------------------------------------------------

TEST(BuffLandingProducer, TheAnimationPacketAloneLandsNothing) {
    if (!HasDb()) GTEST_SKIP() << "willeq.db not found, skipping";
    LandingHarness h;
    ASSERT_TRUE(h.Init());

    // effect_flag 0 is the first of the pair: the cast is being ANIMATED, not
    // applied. Nothing may enter the buff list yet.
    ReplayZone(h.eq, HC_OP_Action,
               ActionPayload(kMySpawn, kMySpawn, 50, kUltravision,
                             kSpellEffectAction, kAnimationPacket));

    EXPECT_FALSE(h.Buffs()->hasPlayerBuff(kUltravision));
    EXPECT_EQ(h.Vision().visionEvents, 0);
}

TEST(BuffLandingProducer, TheLandingPacketRecordsTheBuffAndFiresTheApplyHook) {
    if (!HasDb()) GTEST_SKIP() << "willeq.db not found, skipping";
    LandingHarness h;
    ASSERT_TRUE(h.Init());

    ASSERT_FALSE(h.Buffs()->hasPlayerBuff(kUltravision));
    h.CastOnSelf(kUltravision);

    // Before T348 both of these failed: no production path put a landed buff
    // into the list, so the hook T339 registered never fired in a session.
    EXPECT_TRUE(h.Buffs()->hasPlayerBuff(kUltravision));
    EXPECT_EQ(h.Vision().visionEvents, 1) << "the landing must publish VisionChanged";
    EXPECT_EQ(h.Vision().lastVision, 2) << "Ultravision";

    const EQ::ActiveBuff* landed = h.Buffs()->getPlayerBuff(kUltravision);
    ASSERT_NE(landed, nullptr);
    EXPECT_GE(landed->slot, 0) << "a landed buff must occupy a real slot";
    // duration_formula 3 = caster_level * 30 ticks, capped at 360; the client
    // computes it because the Action packet carries no duration.
    EXPECT_EQ(landed->remaining_seconds, 360u * EQ::BUFF_TICK_SECONDS);
}

TEST(BuffLandingProducer, ARecastRefreshesRatherThanStacking) {
    if (!HasDb()) GTEST_SKIP() << "willeq.db not found, skipping";
    LandingHarness h;
    ASSERT_TRUE(h.Init());

    h.CastOnSelf(kUltravision);
    ASSERT_EQ(h.Buffs()->getPlayerBuffCount(), 1u);

    // T345's session re-cast the same spell; the server faded the old copy and
    // sent the pair again. One entry, not two.
    h.CastOnSelf(kUltravision);
    EXPECT_EQ(h.Buffs()->getPlayerBuffCount(), 1u);
    EXPECT_EQ(h.Vision().visionEvents, 2) << "each landing republishes vision";
}

TEST(BuffLandingProducer, TwoDifferentVisionBuffsBothLandAndBestWins) {
    if (!HasDb()) GTEST_SKIP() << "willeq.db not found, skipping";
    LandingHarness h;
    ASSERT_TRUE(h.Init());

    h.CastOnSelf(kSerpentSight);
    EXPECT_EQ(h.Vision().lastVision, 1) << "Infravision";

    h.CastOnSelf(kUltravision);
    EXPECT_EQ(h.Vision().lastVision, 2) << "best vision wins";
    EXPECT_EQ(h.Buffs()->getPlayerBuffCount(), 2u);
}

// ---------------------------------------------------------------------------
// What must NOT land.
// ---------------------------------------------------------------------------

TEST(BuffLandingProducer, ASpellLandingOnSomeoneElseNeverTouchesOurBuffList) {
    if (!HasDb()) GTEST_SKIP() << "willeq.db not found, skipping";
    LandingHarness h;
    ASSERT_TRUE(h.Init());

    ReplayZone(h.eq, HC_OP_Action,
               ActionPayload(kOtherSpawn, kOtherSpawn, 50, kUltravision,
                             kSpellEffectAction, kLandedPacket));

    EXPECT_FALSE(h.Buffs()->hasPlayerBuff(kUltravision));
    EXPECT_EQ(h.Vision().visionEvents, 0);
}

TEST(BuffLandingProducer, ASpellWithNoDurationIsNotABuffAndDoesNotLand) {
    if (!HasDb()) GTEST_SKIP() << "willeq.db not found, skipping";
    LandingHarness h;
    ASSERT_TRUE(h.Init());

    h.CastOnSelf(kNoDurationSpell);  // Summon Corpse: beneficial, no duration

    EXPECT_FALSE(h.Buffs()->hasPlayerBuff(kNoDurationSpell));
    EXPECT_EQ(h.Buffs()->getPlayerBuffCount(), 0u);
}

TEST(BuffLandingProducer, ANonSpellActionIsIgnored) {
    if (!HasDb()) GTEST_SKIP() << "willeq.db not found, skipping";
    LandingHarness h;
    ASSERT_TRUE(h.Init());

    // Type 231 is the spell-effect action; a melee action carries a damage
    // type here and must never be read as a spell landing.
    ReplayZone(h.eq, HC_OP_Action,
               ActionPayload(kMySpawn, kOtherSpawn, 50, kUltravision,
                             /*type=*/1, kLandedPacket));

    EXPECT_FALSE(h.Buffs()->hasPlayerBuff(kUltravision));
    EXPECT_EQ(h.Vision().visionEvents, 0);
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);

    const char* env_path = std::getenv("WILLEQ_DB_PATH");
    const std::string db_path = env_path ? env_path : WILLEQ_DB_PATH;
    std::ifstream probe(db_path);
    if (probe.good()) {
        probe.close();
        if (g_willeq_db.open(db_path)) {
            EQT::WilleqDatabase::setGlobalInstance(&g_willeq_db);
            g_db_available = true;
        }
    }
    return RUN_ALL_TESTS();
}
