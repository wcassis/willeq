// T240: EqActionHandler::castSpell() / castSpellOnTarget() / memorizeSpell()
// against the REAL EverQuest + EQ::SpellManager + a real (never handshaken)
// DaybreakConnection, so the outbound OP_CastSpell / OP_MemorizeSpell bytes
// can be asserted -- the T234 WireHarness idiom (test_eq_action_handler_
// transforms_real.cpp) plus the T101 DB-opening main() (test_eq_action_
// handler_spell_pet.cpp).
//
// Why this file exists: until T240 these three methods went through
// CombatManager's OWN gem map (m_spell_gems), which nothing but
// memorizeSpell() ever wrote -- the PlayerProfile load fills
// SpellManager::setSpellGems() instead -- so a spell-gem hotkey cast on a
// gem the server had memorised reported success and sent nothing (T235 live
// AFTER run). The gems here are loaded through setSpellGems(), the exact
// call eq.cpp's PlayerProfile handler makes, and the assertion is on the
// wire: exactly one OP_CastSpell whose slot/spell/target fields are the
// gem's. These tests FAIL on the pre-T240 code (measured: review/T240/
// red_cast.txt) because CombatManager's map is empty there.
//
// This replaces T93's test_eq_action_handler_transforms.cpp, whose two
// tests pinned the 1-based -> 0-based gem conversion by observing
// CombatManager::IsCasting() -- state that T240 deleted along with the map.
// The same off-by-one is pinned below on the real store (CastSpell_
// GemIndexIsOneBased), at both ends of the range.
//
// No network, no graphics, no eqemu. DB-gated cases skip without
// data/willeq.db (hasDb() precedent).

#include <gtest/gtest.h>

#define private public
#define protected public
#include "client/eq.h"
#undef private
#undef protected

#include "client/bridge/game_state_bridge.h"
#include "client/combat.h"
#include "client/eq_action_handler.h"
#include "client/spell/spell_manager.h"
#include "client/spell/cast_refusal.h"
#include "client/willeq_database.h"
#include "common/net/daybreak_connection.h"
#include "common/packet_structs.h"
#include "fake_udp_transport.h"
#include "zone_stream_test_utils.h"

#include <cstdlib>
#include <cstring>
#include <fstream>
#include <functional>
#include <memory>
#include <string>
#include <vector>

using namespace eqt;
using namespace zonetest;

namespace {

#ifndef WILLEQ_DB_PATH
#define WILLEQ_DB_PATH "data/willeq.db"
#endif

std::string getWilleqDbPath() {
    if (const char* env_path = std::getenv("WILLEQ_DB_PATH")) {
        return env_path;
    }
    return WILLEQ_DB_PATH;
}

EQT::WilleqDatabase g_willeq_db;
bool g_db_available = false;
bool hasDb() { return g_db_available; }

constexpr uint16_t kSelfId = 460;
constexpr uint16_t kGuardId = 100;

class RecordingBridge : public eqt::bridge::GameStateBridge {
public:
    void applyEvent(const eqt::state::GameEvent&) override {}
};

struct SentAppPacket {
    uint16_t opcode = 0;
    std::vector<uint8_t> body;
};

// Headless EverQuest + real EqActionHandler + real DaybreakConnection over
// FakeUdpTransport installed as m_zone_connection (T234 idiom, see header).
class WireHarness {
public:
    WireHarness()
        : eq("127.0.0.1", 0, "test", "test", "test", "Testchar"), handler(eq) {
        EQ::Net::DaybreakConnectionManagerOptions opts;
        opts.crc_length = 2;
        opts.encode_passes[0] = EQ::Net::EncodeNone;
        opts.encode_passes[1] = EQ::Net::EncodeNone;
        opts.connect_delay_ms = 0;
        opts.keepalive_delay_ms = 0;
        manager = std::make_unique<EQ::Net::DaybreakConnectionManager>(
            opts, std::make_unique<EQ::Test::FakeUdpTransport>(&c2s, &s2c));
        manager->OnNewConnection([this](std::shared_ptr<EQ::Net::DaybreakConnection> c) {
            conn = c;
        });
        manager->Connect("peer", 12345);
        EXPECT_NE(conn, nullptr);

        eq.setBridge(&bridge);
        eq.Test_SetBypassSendGuardForSeam(true);
        eq.Test_SetMySpawnIdForSeam(kSelfId);
        eq.Test_SetZoneConnectedForSeam(true);
        eq.m_zone_connection = conn;
    }

    ~WireHarness() { eq.m_zone_connection.reset(); }

    void spawnSelfAndGuard() {
        ReplayZone(eq, HC_OP_NewSpawn,
                   NewSpawnPayloadAt(kSelfId, "Testchar", 0, 10, 0, 5, /*npc_type=*/0));
        ASSERT_EQ(eq.GetMySpawnID(), kSelfId);
        ReplayZone(eq, HC_OP_NewSpawn, NewSpawnPayloadAt(kGuardId, "Guard_Hezlan000", 0, 12, 0));
        ASSERT_NE(eq.GetEntities().find(kGuardId), eq.GetEntities().end());
    }

    std::vector<SentAppPacket> sentPackets() {
        std::vector<SentAppPacket> out;
        for (auto& [seq, sent] : conn->m_streams[0].sent_packets) {
            const auto* d = static_cast<const uint8_t*>(sent.packet.Data());
            size_t n = sent.packet.Length();
            size_t off = EQ::Net::DaybreakReliableHeader::size();
            if (off < n && d[off] == 0) ++off;
            if (off + 2 > n) continue;
            SentAppPacket pkt;
            pkt.opcode = static_cast<uint16_t>(d[off] | (d[off + 1] << 8));
            pkt.body.assign(d + off + 2, d + n);
            out.push_back(std::move(pkt));
        }
        return out;
    }

    void clearSent() { conn->m_streams[0].sent_packets.clear(); }

    // Every OP_CastSpell queued since the last clearSent(), decoded.
    std::vector<EQT::CastSpell_Struct> sentCasts() {
        std::vector<EQT::CastSpell_Struct> out;
        for (auto& p : sentPackets()) {
            if (p.opcode != HC_OP_CastSpell || p.body.size() < sizeof(EQT::CastSpell_Struct)) continue;
            EQT::CastSpell_Struct cs;
            std::memcpy(&cs, p.body.data(), sizeof(cs));
            out.push_back(cs);
        }
        return out;
    }

    std::vector<EQT::MemorizeSpell_Struct> sentMemorizes() {
        std::vector<EQT::MemorizeSpell_Struct> out;
        for (auto& p : sentPackets()) {
            if (p.opcode != HC_OP_MemorizeSpell || p.body.size() < sizeof(EQT::MemorizeSpell_Struct)) continue;
            EQT::MemorizeSpell_Struct ms;
            std::memcpy(&ms, p.body.data(), sizeof(ms));
            out.push_back(ms);
        }
        return out;
    }

    std::string lastSystemMessage() {
        std::string last;
        for (auto& e : bridge.drainEvents()) {
            if (e.type == eqt::state::GameEventType::SystemMessage) {
                last = std::get<eqt::state::ChatMessageData>(e.data).message;
            }
        }
        return last;
    }

    // A directly-castable single-target spell from the real DB: what a
    // level-1 wizard's Frost Bolt is in the live leg. Returns 0 if none.
    uint32_t findSingleTargetSpell() {
        auto* mgr = eq.GetSpellManager();
        auto& db = mgr->getDatabase();
        for (uint32_t id = 1; id <= 4000; ++id) {
            const EQ::SpellData* s = db.getSpell(id);
            if (s && s->target_type == EQ::SpellTargetType::Single &&
                s->cast_time_ms > 0 && s->mana_cost > 0 && s->mana_cost < 100 &&
                s->canClassUse(EQ::PlayerClass::Wizard, 1)) {
                return id;
            }
        }
        return 0;
    }

    // The PlayerProfile load path: eq.cpp's HC_OP_PlayerProfile handler
    // ends in exactly this setSpellGems() call with the 8 mem_spells.
    void loadGemsLikePlayerProfile(const std::array<uint32_t, 8>& gems) {
        eq.GetSpellManager()->setSpellGems(gems.data(), gems.size());
    }

    EQ::Test::FakeChannel c2s;
    EQ::Test::FakeChannel s2c;
    std::unique_ptr<EQ::Net::DaybreakConnectionManager> manager;
    std::shared_ptr<EQ::Net::DaybreakConnection> conn;
    RecordingBridge bridge;
    EverQuest eq;
    EqActionHandler handler;
};

constexpr uint32_t kEmpty = 0xFFFFFFFF;

// ---------------------------------------------------------------------
// Empty gem: no DB needed. Before T240 this ALSO sent nothing -- but for
// the wrong reason (empty CombatManager map), and silently; now the refusal
// is SpellManager's NotMemorized with the /cast message.
// ---------------------------------------------------------------------
TEST(EqActionHandlerCast, CastSpell_EmptyGem_SendsNothingAndSaysSo) {
    WireHarness h;
    h.clearSent();

    h.handler.castSpell(1);

    EXPECT_FALSE(h.eq.GetSpellManager()->isCasting());
    EXPECT_TRUE(h.sentCasts().empty());
    EXPECT_EQ(h.lastSystemMessage(), "No spell in gem 1");
}

// ---------------------------------------------------------------------
// The T235 defect: a gem the server memorised (profile load ->
// setSpellGems) must cast from the hotkey path. RED on pre-T240 code.
// ---------------------------------------------------------------------
TEST(EqActionHandlerCast, CastSpell_ProfileLoadedGem_CastsThroughSpellManager) {
    if (!hasDb()) GTEST_SKIP() << "willeq.db not found, skipping";
    WireHarness h;
    ASSERT_TRUE(h.eq.InitializeSpellSystem(""));
    uint32_t id = h.findSingleTargetSpell();
    if (id == 0) GTEST_SKIP() << "no single-target level-1 wizard spell in scanned range";

    h.spawnSelfAndGuard();
    h.eq.GetGameState().player().setMana(1000, 1000);
    h.loadGemsLikePlayerProfile({id, kEmpty, kEmpty, kEmpty, kEmpty, kEmpty, kEmpty, kEmpty});
    ASSERT_EQ(h.eq.GetSpellManager()->getGemState(0), EQ::GemState::Ready);
    ASSERT_TRUE(h.eq.GetCombatManager()->SetTarget(kGuardId));
    h.clearSent();

    h.handler.castSpell(1);

    auto* mgr = h.eq.GetSpellManager();
    EXPECT_TRUE(mgr->isCasting()) << "gem 1 holds spell " << id << " via the profile-load path";
    auto casts = h.sentCasts();
    ASSERT_EQ(casts.size(), 1u) << "exactly one OP_CastSpell on the wire";
    EXPECT_EQ(casts[0].slot, 0u) << "gem 1 (1-based) -> slot 0 on the wire";
    EXPECT_EQ(casts[0].spell_id, id);
    EXPECT_EQ(casts[0].inventoryslot, 0xFFFFFFFFu);
    EXPECT_EQ(casts[0].target_id, static_cast<uint32_t>(kGuardId))
        << "targetId 0 resolves to the current CombatManager target";
    EXPECT_EQ(h.lastSystemMessage(), "Casting from gem 1");
}

// ---------------------------------------------------------------------
// castSpellOnTarget(): the explicit target is used for the cast AND
// becomes the current target (pre-T240 order preserved).
// ---------------------------------------------------------------------
TEST(EqActionHandlerCast, CastSpellOnTarget_ExplicitTargetIsCastTargetAndCurrentTarget) {
    if (!hasDb()) GTEST_SKIP() << "willeq.db not found, skipping";
    WireHarness h;
    ASSERT_TRUE(h.eq.InitializeSpellSystem(""));
    uint32_t id = h.findSingleTargetSpell();
    if (id == 0) GTEST_SKIP() << "no single-target level-1 wizard spell in scanned range";

    h.spawnSelfAndGuard();
    h.eq.GetGameState().player().setMana(1000, 1000);
    h.loadGemsLikePlayerProfile({kEmpty, id, kEmpty, kEmpty, kEmpty, kEmpty, kEmpty, kEmpty});
    ASSERT_FALSE(h.eq.GetCombatManager()->HasTarget());
    h.clearSent();

    h.handler.castSpellOnTarget(2, kGuardId);

    EXPECT_TRUE(h.eq.GetSpellManager()->isCasting());
    auto casts = h.sentCasts();
    ASSERT_EQ(casts.size(), 1u);
    EXPECT_EQ(casts[0].slot, 1u) << "gem 2 (1-based) -> slot 1";
    EXPECT_EQ(casts[0].spell_id, id);
    EXPECT_EQ(casts[0].target_id, static_cast<uint32_t>(kGuardId));
    EXPECT_EQ(h.eq.GetCombatManager()->GetTargetId(), kGuardId);
}

// ---------------------------------------------------------------------
// T93's off-by-one pin, on the real store: only gem 8 loaded -> gem 7
// refuses, gem 8 casts (far end); only gem 2 loaded -> gem 1 refuses,
// gem 2 casts (near end).
// ---------------------------------------------------------------------
TEST(EqActionHandlerCast, CastSpell_GemIndexIsOneBased) {
    if (!hasDb()) GTEST_SKIP() << "willeq.db not found, skipping";
    WireHarness h;
    ASSERT_TRUE(h.eq.InitializeSpellSystem(""));
    uint32_t id = h.findSingleTargetSpell();
    if (id == 0) GTEST_SKIP() << "no single-target level-1 wizard spell in scanned range";

    h.spawnSelfAndGuard();
    h.eq.GetGameState().player().setMana(1000, 1000);
    ASSERT_TRUE(h.eq.GetCombatManager()->SetTarget(kGuardId));
    auto* mgr = h.eq.GetSpellManager();

    h.loadGemsLikePlayerProfile({kEmpty, kEmpty, kEmpty, kEmpty, kEmpty, kEmpty, kEmpty, id});
    h.clearSent();
    h.handler.castSpell(7);
    EXPECT_FALSE(mgr->isCasting()) << "gem 7 is empty";
    EXPECT_TRUE(h.sentCasts().empty());
    h.handler.castSpell(8);
    EXPECT_TRUE(mgr->isCasting()) << "gem 8 holds the spell";
    ASSERT_EQ(h.sentCasts().size(), 1u);
    EXPECT_EQ(h.sentCasts()[0].slot, 7u);

    mgr->interruptCast();
    ASSERT_FALSE(mgr->isCasting());
    h.loadGemsLikePlayerProfile({kEmpty, id, kEmpty, kEmpty, kEmpty, kEmpty, kEmpty, kEmpty});
    h.clearSent();
    h.handler.castSpell(1);
    EXPECT_FALSE(mgr->isCasting()) << "gem 1 is empty";
    EXPECT_TRUE(h.sentCasts().empty());
    h.handler.castSpell(2);
    EXPECT_TRUE(mgr->isCasting());
    ASSERT_EQ(h.sentCasts().size(), 1u);
    EXPECT_EQ(h.sentCasts()[0].slot, 1u);
}

// ---------------------------------------------------------------------
// Gems 9-12 (ActionDispatcher allows 1-12; SpellManager has 8): refused
// cleanly, nothing sent. Pre-T240 this indexed a std::map keyed by
// SPELL_GEM_9..12 -- also empty, also silent.
// ---------------------------------------------------------------------
TEST(EqActionHandlerCast, CastSpell_GemBeyondEight_RefusedNothingSent) {
    WireHarness h;
    h.clearSent();
    h.handler.castSpell(12);
    EXPECT_FALSE(h.eq.GetSpellManager()->isCasting());
    EXPECT_TRUE(h.sentCasts().empty());
}

// ---------------------------------------------------------------------
// memorizeSpell(gem, id): now writes SpellManager (memorisation starts,
// OP_MemorizeSpell goes out) instead of the deleted CombatManager map.
// RED on pre-T240 code: the gem stays Empty and nothing is sent.
// ---------------------------------------------------------------------
TEST(EqActionHandlerCast, MemorizeSpell_ById_WritesSpellManagerAndSendsMemorize) {
    if (!hasDb()) GTEST_SKIP() << "willeq.db not found, skipping";
    WireHarness h;
    ASSERT_TRUE(h.eq.InitializeSpellSystem(""));
    uint32_t id = h.findSingleTargetSpell();
    if (id == 0) GTEST_SKIP() << "no single-target level-1 wizard spell in scanned range";
    auto* mgr = h.eq.GetSpellManager();

    ASSERT_TRUE(mgr->scribeSpell(id, 0));
    h.eq.GetGameState().player().setClass(static_cast<uint32_t>(EQ::PlayerClass::Wizard));
    h.eq.GetGameState().player().setLevel(60);
    ASSERT_EQ(mgr->getGemState(2), EQ::GemState::Empty);
    h.clearSent();

    h.handler.memorizeSpell(3, id);  // gem 3 (1-based) -> index 2

    EXPECT_EQ(mgr->getGemState(2), EQ::GemState::MemorizeProgress);
    auto mems = h.sentMemorizes();
    ASSERT_EQ(mems.size(), 1u) << "exactly one OP_MemorizeSpell on the wire";
    EXPECT_EQ(mems[0].slot, 2u);
    EXPECT_EQ(mems[0].spell_id, id);
}

}  // namespace

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);

    std::string db_path = getWilleqDbPath();
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

// =====================================================================
// T244: cast refusals name the reason with the official client string
// (eqstr_us.txt ids via cast_refusal.h). Before this unit every result the
// handler's switch did not list -- OutOfRange, InvalidTarget, NoLineOfSight,
// Stunned, ComponentMissing, ... -- fell to a bare "Cannot cast spell"
// (T240 finding 1, review/T240 run 1). These are RED on pre-T244 code by
// message text (review/T244/red_cast.txt).
// =====================================================================

namespace {

constexpr uint16_t kFarId = 101;

// A single-target spell WITH a range cap (findSingleTargetSpell() does not
// require one; range 0 means "no limit" to SpellManager::checkRange()).
uint32_t findRangedSingleTargetSpell(EverQuest& eq) {
    auto& db = eq.GetSpellManager()->getDatabase();
    for (uint32_t id = 1; id <= 4000; ++id) {
        const EQ::SpellData* s = db.getSpell(id);
        if (s && s->target_type == EQ::SpellTargetType::Single &&
            s->cast_time_ms > 0 && s->range > 0 && s->range < 300 &&
            s->mana_cost > 0 && s->mana_cost < 100) {
            return id;
        }
    }
    return 0;
}

uint32_t findSpellOfTargetType(EverQuest& eq, EQ::SpellTargetType tt) {
    auto& db = eq.GetSpellManager()->getDatabase();
    for (uint32_t id = 1; id <= 4000; ++id) {
        const EQ::SpellData* s = db.getSpell(id);
        if (s && s->target_type == tt && s->cast_time_ms > 0 && s->mana_cost < 100) {
            return id;
        }
    }
    return 0;
}

} // namespace

// Pure table: every CastResult with an official refusal string maps to the
// eqstr id verified against /mnt/projects/EverQuestP1999/eqstr_us.txt; the
// rest map to 0 (caller keeps its own wording). No DB.
TEST(EqActionHandlerCast, CastRefusalStringIds_MatchEqstr) {
    using EQ::CastResult;
    EXPECT_EQ(EQ::castRefusalStringId(CastResult::OutOfRange), 100u);
    EXPECT_EQ(EQ::castRefusalStringId(CastResult::NoLineOfSight), 108u);
    EXPECT_EQ(EQ::castRefusalStringId(CastResult::Fizzle), 173u);
    EXPECT_EQ(EQ::castRefusalStringId(CastResult::Stunned), 175u);
    EXPECT_EQ(EQ::castRefusalStringId(CastResult::NotEnoughMana), 199u);
    EXPECT_EQ(EQ::castRefusalStringId(CastResult::Silenced), 207u);
    EXPECT_EQ(EQ::castRefusalStringId(CastResult::GemCooldown), 236u);
    EXPECT_EQ(EQ::castRefusalStringId(CastResult::InvalidTarget), 251u);
    EXPECT_EQ(EQ::castRefusalStringId(CastResult::NoPet), 255u);
    EXPECT_EQ(EQ::castRefusalStringId(CastResult::ComponentMissing), 272u);
    EXPECT_EQ(EQ::castRefusalStringId(CastResult::AlreadyCasting), 12442u);
    EXPECT_EQ(EQ::castRefusalStringId(CastResult::Success), 0u);
    EXPECT_EQ(EQ::castRefusalStringId(CastResult::NotMemorized), 0u);
    EXPECT_EQ(EQ::castRefusalStringId(CastResult::SpellNotReady), 0u);
    EXPECT_EQ(EQ::castRefusalStringId(CastResult::InvalidSpell), 0u);
}

// The fallback table in EverQuest::GetStringMessage() (no string DB loaded
// in this harness) carries every id the refusal path can emit -- a missing
// row would print "[Unknown message #N]" in place of the reason.
TEST(EqActionHandlerCast, CastRefusalStrings_ResolveWithoutStringDb) {
    WireHarness h;
    EXPECT_EQ(h.eq.GetStringMessage(100), "Your target is out of range, get closer!");
    EXPECT_EQ(h.eq.GetStringMessage(108), "You cannot see your target.");
    EXPECT_EQ(h.eq.GetStringMessage(175), "You can't cast spells while stunned!");
    EXPECT_EQ(h.eq.GetStringMessage(199), "Insufficient Mana to cast this spell!");
    EXPECT_EQ(h.eq.GetStringMessage(207), "You *CANNOT* cast spells, you have been silenced!");
    EXPECT_EQ(h.eq.GetStringMessage(236), "Spell recast time not yet met.");
    EXPECT_EQ(h.eq.GetStringMessage(251), "That spell can not affect this target NPC.");
    EXPECT_EQ(h.eq.GetStringMessage(255), "You do not have a pet.");
    EXPECT_EQ(h.eq.GetStringMessage(272), "You are missing some required spell components.");
    EXPECT_EQ(h.eq.GetStringMessage(12442), "You are already casting a spell!");
}

TEST(EqActionHandlerCast, CastSpell_TargetOutOfRange_SaysOutOfRange) {
    if (!hasDb()) GTEST_SKIP() << "willeq.db not found, skipping";
    WireHarness h;
    ASSERT_TRUE(h.eq.InitializeSpellSystem(""));
    uint32_t id = findRangedSingleTargetSpell(h.eq);
    if (id == 0) GTEST_SKIP() << "no ranged single-target spell in scanned range";

    h.spawnSelfAndGuard();
    // Far NPC: NewSpawnPayloadAt() packs positions in 1/8 units (4000 -> 500
    // world units on entity.x), past any range < 300.
    ReplayZone(h.eq, HC_OP_NewSpawn, NewSpawnPayloadAt(kFarId, "a_distant_roamer", 0, 4000, 0));
    h.eq.GetGameState().player().setMana(1000, 1000);
    h.loadGemsLikePlayerProfile({id, kEmpty, kEmpty, kEmpty, kEmpty, kEmpty, kEmpty, kEmpty});
    ASSERT_TRUE(h.eq.GetCombatManager()->SetTarget(kFarId));
    h.clearSent();

    h.handler.castSpell(1);

    EXPECT_FALSE(h.eq.GetSpellManager()->isCasting());
    EXPECT_TRUE(h.sentCasts().empty());
    EXPECT_EQ(h.lastSystemMessage(), "Your target is out of range, get closer!");
}

TEST(EqActionHandlerCast, CastSpell_NoMana_SaysInsufficientMana) {
    if (!hasDb()) GTEST_SKIP() << "willeq.db not found, skipping";
    WireHarness h;
    ASSERT_TRUE(h.eq.InitializeSpellSystem(""));
    uint32_t id = h.findSingleTargetSpell();
    if (id == 0) GTEST_SKIP() << "no single-target level-1 wizard spell in scanned range";

    h.spawnSelfAndGuard();
    h.eq.GetGameState().player().setMana(0, 1000);
    h.loadGemsLikePlayerProfile({id, kEmpty, kEmpty, kEmpty, kEmpty, kEmpty, kEmpty, kEmpty});
    ASSERT_TRUE(h.eq.GetCombatManager()->SetTarget(kGuardId));
    h.clearSent();

    h.handler.castSpell(1);

    EXPECT_FALSE(h.eq.GetSpellManager()->isCasting());
    EXPECT_TRUE(h.sentCasts().empty());
    EXPECT_EQ(h.lastSystemMessage(), "Insufficient Mana to cast this spell!");
}

TEST(EqActionHandlerCast, CastSpell_AnimalSpellOnHumanoid_SaysCannotAffectTarget) {
    if (!hasDb()) GTEST_SKIP() << "willeq.db not found, skipping";
    WireHarness h;
    ASSERT_TRUE(h.eq.InitializeSpellSystem(""));
    uint32_t id = findSpellOfTargetType(h.eq, EQ::SpellTargetType::Animal);
    if (id == 0) GTEST_SKIP() << "no animal-only spell in scanned range";

    h.spawnSelfAndGuard();  // guard: bodytype 0 (humanoid) from NewSpawnPayloadAt
    h.eq.GetGameState().player().setMana(1000, 1000);
    h.loadGemsLikePlayerProfile({id, kEmpty, kEmpty, kEmpty, kEmpty, kEmpty, kEmpty, kEmpty});
    ASSERT_TRUE(h.eq.GetCombatManager()->SetTarget(kGuardId));
    h.clearSent();

    h.handler.castSpell(1);

    EXPECT_FALSE(h.eq.GetSpellManager()->isCasting());
    EXPECT_TRUE(h.sentCasts().empty());
    EXPECT_EQ(h.lastSystemMessage(), "That spell can not affect this target NPC.");
}

TEST(EqActionHandlerCast, CastSpell_PetSpellWithoutPet_SaysNoPet) {
    if (!hasDb()) GTEST_SKIP() << "willeq.db not found, skipping";
    WireHarness h;
    ASSERT_TRUE(h.eq.InitializeSpellSystem(""));
    uint32_t id = findSpellOfTargetType(h.eq, EQ::SpellTargetType::Pet);
    if (id == 0) GTEST_SKIP() << "no pet-only spell in scanned range";

    h.spawnSelfAndGuard();
    h.eq.GetGameState().player().setMana(1000, 1000);
    h.loadGemsLikePlayerProfile({id, kEmpty, kEmpty, kEmpty, kEmpty, kEmpty, kEmpty, kEmpty});
    ASSERT_TRUE(h.eq.GetCombatManager()->SetTarget(kGuardId));  // a target, but no pet
    h.clearSent();

    h.handler.castSpell(1);

    EXPECT_FALSE(h.eq.GetSpellManager()->isCasting());
    EXPECT_TRUE(h.sentCasts().empty());
    EXPECT_EQ(h.lastSystemMessage(), "You do not have a pet.");
}

// =====================================================================
// T246: the three cast pre-checks stop being `return true` stubs.
// checkPlayerState() reads the player's buff list (a Stun/Mez buff ->
// 175, Silence -> 207), checkReagents() counts consumed components in
// worn+general+bag slots, checkLineOfSight() rays the zone collision map.
// On pre-T246 code every one of these casts PROCEEDS and puts an
// OP_CastSpell on the wire (review/T246/red_cast.txt).
// =====================================================================

#include "client/hc_map.h"
#include "client/inventory/inventory_manager.h"
#include "client/spell/buff_manager.h"

#include <unistd.h>

namespace {

uint32_t findSpellWithEffect(EverQuest& eq, EQ::SpellEffect effect) {
    auto& db = eq.GetSpellManager()->getDatabase();
    for (uint32_t id = 1; id <= 4000; ++id) {
        const EQ::SpellData* s = db.getSpell(id);
        if (s && s->hasEffect(effect)) return id;
    }
    return 0;
}

// A self-targeted spell with at least one consumed component; returns the
// id and copies the first component out (the LRU cache may evict the row).
uint32_t findSelfSpellWithReagent(EverQuest& eq, uint32_t& reagent_id, uint32_t& reagent_count) {
    auto& db = eq.GetSpellManager()->getDatabase();
    for (uint32_t id = 1; id <= 4000; ++id) {
        const EQ::SpellData* s = db.getSpell(id);
        if (s && s->requiresReagents() && s->target_type == EQ::SpellTargetType::Self &&
            s->cast_time_ms > 0 && s->mana_cost < 100) {
            reagent_id = s->reagent_id[0];
            reagent_count = s->reagent_count[0];
            return id;
        }
    }
    return 0;
}

// The PlayerProfile / OP_Buff path: BuffManager::setPlayerBuff() with the
// wire struct, exactly what eq_player_info.cpp's handler calls.
void giveSelfBuff(EverQuest& eq, uint8_t slot, uint32_t spell_id) {
    EQT::SpellBuff_Struct b{};
    b.effect_type = static_cast<uint8_t>(EQ::BuffEffectType::Inverse);
    b.level = 50;
    b.spellid = spell_id;
    b.duration = 10;
    b.player_id = 999;
    eq.GetBuffManager()->setPlayerBuff(slot, b);
}

eqt::inventory::ItemInstance makeStack(uint32_t item_id, int32_t quantity) {
    eqt::inventory::ItemInstance item;
    item.itemId = item_id;
    item.name = "reagent";
    item.stackable = true;
    item.stackSize = 20;
    item.quantity = quantity;
    return item;
}

// Minimal V1 collision map: a vertical wall (two triangles) in the plane
// server_x == wall_x, spanning y,z in [-200, 200]. V1 vertices are EQ
// server coords (Z-up), as HCMap::Load's V1 branch reads them.
std::string writeWallMap(float wall_x) {
    std::vector<char> buf;
    auto u32 = [&](uint32_t v) { buf.insert(buf.end(), reinterpret_cast<char*>(&v), reinterpret_cast<char*>(&v) + 4); };
    auto u16 = [&](uint16_t v) { buf.insert(buf.end(), reinterpret_cast<char*>(&v), reinterpret_cast<char*>(&v) + 2); };
    auto f32 = [&](float v) { buf.insert(buf.end(), reinterpret_cast<char*>(&v), reinterpret_cast<char*>(&v) + 4); };
    auto vert = [&](float x, float y, float z) { f32(x); f32(y); f32(z); };
    auto tri = [&](float ay, float az, float by, float bz, float cy, float cz) {
        vert(wall_x, ay, az); vert(wall_x, by, bz); vert(wall_x, cy, cz);
        f32(1); f32(0); f32(0); f32(0);  // normals[4], unused by the loader
    };
    u32(0x01000000u);
    u32(2);   // face_count
    u16(0);   // node_count
    u32(0);   // facelist_count
    tri(-200, -200, 200, -200, 200, 200);
    tri(-200, -200, 200, 200, -200, 200);
    std::string path = "/tmp/t246_wall_" + std::to_string(getpid()) + ".map";
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    f.write(buf.data(), static_cast<std::streamsize>(buf.size()));
    return path;
}

} // namespace

TEST(EqActionHandlerCast, CastSpell_StunBuffOnPlayer_SaysStunned) {
    if (!hasDb()) GTEST_SKIP() << "willeq.db not found, skipping";
    WireHarness h;
    ASSERT_TRUE(h.eq.InitializeSpellSystem(""));
    uint32_t id = h.findSingleTargetSpell();
    uint32_t stun = findSpellWithEffect(h.eq, EQ::SpellEffect::Stun);
    if (id == 0 || stun == 0) GTEST_SKIP() << "needed spells not in scanned range";

    h.spawnSelfAndGuard();
    h.eq.GetGameState().player().setMana(1000, 1000);
    h.loadGemsLikePlayerProfile({id, kEmpty, kEmpty, kEmpty, kEmpty, kEmpty, kEmpty, kEmpty});
    ASSERT_TRUE(h.eq.GetCombatManager()->SetTarget(kGuardId));
    giveSelfBuff(h.eq, 0, stun);
    ASSERT_TRUE(h.eq.GetBuffManager()->hasPlayerBuff(stun));
    h.clearSent();

    h.handler.castSpell(1);

    EXPECT_FALSE(h.eq.GetSpellManager()->isCasting());
    EXPECT_TRUE(h.sentCasts().empty()) << "nothing on the wire while stunned";
    EXPECT_EQ(h.lastSystemMessage(), "You can't cast spells while stunned!");
}

TEST(EqActionHandlerCast, CastSpell_SilenceBuffOnPlayer_SaysSilenced) {
    if (!hasDb()) GTEST_SKIP() << "willeq.db not found, skipping";
    WireHarness h;
    ASSERT_TRUE(h.eq.InitializeSpellSystem(""));
    uint32_t id = h.findSingleTargetSpell();
    uint32_t silence = findSpellWithEffect(h.eq, EQ::SpellEffect::Silence);
    if (id == 0 || silence == 0) GTEST_SKIP() << "needed spells not in scanned range";

    h.spawnSelfAndGuard();
    h.eq.GetGameState().player().setMana(1000, 1000);
    h.loadGemsLikePlayerProfile({id, kEmpty, kEmpty, kEmpty, kEmpty, kEmpty, kEmpty, kEmpty});
    ASSERT_TRUE(h.eq.GetCombatManager()->SetTarget(kGuardId));
    giveSelfBuff(h.eq, 3, silence);
    h.clearSent();

    h.handler.castSpell(1);

    EXPECT_FALSE(h.eq.GetSpellManager()->isCasting());
    EXPECT_TRUE(h.sentCasts().empty());
    EXPECT_EQ(h.lastSystemMessage(), "You *CANNOT* cast spells, you have been silenced!");
}

// Control for the two above: a buff that is neither (the player's own
// single-target spell) does not block.
TEST(EqActionHandlerCast, CastSpell_OrdinaryBuffOnPlayer_DoesNotBlock) {
    if (!hasDb()) GTEST_SKIP() << "willeq.db not found, skipping";
    WireHarness h;
    ASSERT_TRUE(h.eq.InitializeSpellSystem(""));
    uint32_t id = h.findSingleTargetSpell();
    uint32_t heal = findSpellWithEffect(h.eq, EQ::SpellEffect::CurrentHP);
    if (id == 0 || heal == 0) GTEST_SKIP() << "needed spells not in scanned range";

    h.spawnSelfAndGuard();
    h.eq.GetGameState().player().setMana(1000, 1000);
    h.loadGemsLikePlayerProfile({id, kEmpty, kEmpty, kEmpty, kEmpty, kEmpty, kEmpty, kEmpty});
    ASSERT_TRUE(h.eq.GetCombatManager()->SetTarget(kGuardId));
    giveSelfBuff(h.eq, 0, heal);
    h.clearSent();

    h.handler.castSpell(1);

    EXPECT_TRUE(h.eq.GetSpellManager()->isCasting());
    EXPECT_EQ(h.sentCasts().size(), 1u);
}

TEST(EqActionHandlerCast, CastSpell_MissingReagent_SaysMissingComponentsSendsNothing) {
    if (!hasDb()) GTEST_SKIP() << "willeq.db not found, skipping";
    WireHarness h;
    ASSERT_TRUE(h.eq.InitializeSpellSystem(""));
    uint32_t reagent = 0, needed = 0;
    uint32_t id = findSelfSpellWithReagent(h.eq, reagent, needed);
    if (id == 0) GTEST_SKIP() << "no self-target reagent spell in scanned range";

    h.spawnSelfAndGuard();
    h.eq.GetGameState().player().setMana(1000, 1000);
    h.loadGemsLikePlayerProfile({id, kEmpty, kEmpty, kEmpty, kEmpty, kEmpty, kEmpty, kEmpty});
    ASSERT_EQ(h.eq.GetInventoryManager()->findItemSlotByItemId(reagent), eqt::inventory::SLOT_INVALID);
    h.clearSent();

    h.handler.castSpell(1);

    EXPECT_FALSE(h.eq.GetSpellManager()->isCasting());
    EXPECT_TRUE(h.sentCasts().empty()) << "no OP_CastSpell for spell " << id << " without item " << reagent;
    EXPECT_EQ(h.lastSystemMessage(), "You are missing some required spell components.");
}

TEST(EqActionHandlerCast, CastSpell_ReagentPresent_CastProceeds) {
    if (!hasDb()) GTEST_SKIP() << "willeq.db not found, skipping";
    WireHarness h;
    ASSERT_TRUE(h.eq.InitializeSpellSystem(""));
    uint32_t reagent = 0, needed = 0;
    uint32_t id = findSelfSpellWithReagent(h.eq, reagent, needed);
    if (id == 0) GTEST_SKIP() << "no self-target reagent spell in scanned range";

    h.spawnSelfAndGuard();
    h.eq.GetGameState().player().setMana(1000, 1000);
    h.loadGemsLikePlayerProfile({id, kEmpty, kEmpty, kEmpty, kEmpty, kEmpty, kEmpty, kEmpty});
    // In a bag, not a top-level slot: the count must reach bag contents.
    h.eq.GetInventoryManager()->setItem(eqt::inventory::GENERAL_BAGS_BEGIN + 3,
                                        makeStack(reagent, static_cast<int32_t>(needed)));
    h.clearSent();

    h.handler.castSpell(1);

    EXPECT_TRUE(h.eq.GetSpellManager()->isCasting());
    auto casts = h.sentCasts();
    ASSERT_EQ(casts.size(), 1u);
    EXPECT_EQ(casts[0].spell_id, id);
}

// Bone Walk needs 2 x item 13073: one carried is a refusal, two is a cast.
TEST(EqActionHandlerCast, CastSpell_ReagentCountShort_RefusedUntilStackReachesCount) {
    if (!hasDb()) GTEST_SKIP() << "willeq.db not found, skipping";
    WireHarness h;
    ASSERT_TRUE(h.eq.InitializeSpellSystem(""));
    const EQ::SpellData* bw = h.eq.GetSpellManager()->getSpell(351);
    if (!bw || bw->reagent_id[0] != 13073u || bw->reagent_count[0] != 2u) {
        GTEST_SKIP() << "spell 351 is not Bone Walk with 2 x 13073 in this DB";
    }

    h.spawnSelfAndGuard();
    h.eq.GetGameState().player().setMana(1000, 1000);
    h.loadGemsLikePlayerProfile({351, kEmpty, kEmpty, kEmpty, kEmpty, kEmpty, kEmpty, kEmpty});
    h.eq.GetInventoryManager()->setItem(eqt::inventory::GENERAL_BEGIN, makeStack(13073, 1));
    h.clearSent();

    h.handler.castSpell(1);
    EXPECT_FALSE(h.eq.GetSpellManager()->isCasting());
    EXPECT_TRUE(h.sentCasts().empty());
    EXPECT_EQ(h.lastSystemMessage(), "You are missing some required spell components.");

    // A second single chip in another slot: counts add up across slots.
    h.eq.GetInventoryManager()->setItem(eqt::inventory::GENERAL_BEGIN + 1, makeStack(13073, 1));
    h.clearSent();
    h.handler.castSpell(1);
    EXPECT_TRUE(h.eq.GetSpellManager()->isCasting());
    EXPECT_EQ(h.sentCasts().size(), 1u);
}

// A reagent sitting in the BANK does not count (eqemu checks worn +
// personal only).
TEST(EqActionHandlerCast, CastSpell_ReagentOnlyInBank_StillRefused) {
    if (!hasDb()) GTEST_SKIP() << "willeq.db not found, skipping";
    WireHarness h;
    ASSERT_TRUE(h.eq.InitializeSpellSystem(""));
    uint32_t reagent = 0, needed = 0;
    uint32_t id = findSelfSpellWithReagent(h.eq, reagent, needed);
    if (id == 0) GTEST_SKIP() << "no self-target reagent spell in scanned range";

    h.spawnSelfAndGuard();
    h.eq.GetGameState().player().setMana(1000, 1000);
    h.loadGemsLikePlayerProfile({id, kEmpty, kEmpty, kEmpty, kEmpty, kEmpty, kEmpty, kEmpty});
    h.eq.GetInventoryManager()->setItem(eqt::inventory::BANK_BEGIN, makeStack(reagent, static_cast<int32_t>(needed)));
    h.clearSent();

    h.handler.castSpell(1);

    EXPECT_FALSE(h.eq.GetSpellManager()->isCasting());
    EXPECT_TRUE(h.sentCasts().empty());
}

// Wall between caster and target -> 108 and nothing sent; the same wall
// with the target on the caster's side -> cast proceeds. spawnSelfAndGuard
// puts self at server x = 1.25 and the guard at x = 1.5 (NewSpawnPayloadAt
// packs 1/8 units; its first coordinate lands on Entity::x / m_x), so a
// wall in the plane x = 1.4 separates them; a second NPC at x = 1.0 is on
// the caster's side.
TEST(EqActionHandlerCast, CastSpell_WallBetween_SaysCannotSeeTarget) {
    if (!hasDb()) GTEST_SKIP() << "willeq.db not found, skipping";
    WireHarness h;
    ASSERT_TRUE(h.eq.InitializeSpellSystem(""));
    uint32_t id = h.findSingleTargetSpell();
    if (id == 0) GTEST_SKIP() << "no single-target level-1 wizard spell in scanned range";

    std::string path = writeWallMap(1.4f);
    auto map = std::make_shared<HCMap>();
    ASSERT_TRUE(map->Load(path));
    std::remove(path.c_str());
    h.eq.m_zone_map = map;

    // The map itself must report the wall before the cast path is blamed.
    EXPECT_FALSE(map->CheckLOS(glm::vec3(1.25f, 0.0f, 3.0f), glm::vec3(1.5f, 0.0f, 3.0f)));
    EXPECT_TRUE(map->CheckLOS(glm::vec3(1.25f, 0.0f, 3.0f), glm::vec3(1.0f, 0.0f, 3.0f)));

    h.spawnSelfAndGuard();
    ASSERT_FLOAT_EQ(h.eq.GetPosition().x, 1.25f);
    ASSERT_FLOAT_EQ(h.eq.GetEntities().at(kGuardId).x, 1.5f);
    constexpr uint16_t kNearId = 102;
    ReplayZone(h.eq, HC_OP_NewSpawn, NewSpawnPayloadAt(kNearId, "a_near_roamer", 8, 0, 0));
    h.eq.GetGameState().player().setMana(1000, 1000);
    h.loadGemsLikePlayerProfile({id, kEmpty, kEmpty, kEmpty, kEmpty, kEmpty, kEmpty, kEmpty});

    ASSERT_TRUE(h.eq.GetCombatManager()->SetTarget(kGuardId));
    h.clearSent();
    h.handler.castSpell(1);
    EXPECT_FALSE(h.eq.GetSpellManager()->isCasting());
    EXPECT_TRUE(h.sentCasts().empty());
    EXPECT_EQ(h.lastSystemMessage(), "You cannot see your target.");

    ASSERT_TRUE(h.eq.GetCombatManager()->SetTarget(kNearId));
    h.clearSent();
    h.handler.castSpell(1);
    EXPECT_TRUE(h.eq.GetSpellManager()->isCasting()) << "same map, target on the caster's side";
    ASSERT_EQ(h.sentCasts().size(), 1u);
    EXPECT_EQ(h.sentCasts()[0].target_id, static_cast<uint32_t>(kNearId));
}

// No collision map loaded (the state every test above this block runs in,
// and a zone whose .map is missing): line of sight is the server's call.
TEST(EqActionHandlerCast, CastSpell_NoMapLoaded_LineOfSightPermissive) {
    if (!hasDb()) GTEST_SKIP() << "willeq.db not found, skipping";
    WireHarness h;
    ASSERT_TRUE(h.eq.InitializeSpellSystem(""));
    uint32_t id = h.findSingleTargetSpell();
    if (id == 0) GTEST_SKIP() << "no single-target level-1 wizard spell in scanned range";
    ASSERT_EQ(h.eq.GetZoneMap(), nullptr);

    h.spawnSelfAndGuard();
    h.eq.GetGameState().player().setMana(1000, 1000);
    h.loadGemsLikePlayerProfile({id, kEmpty, kEmpty, kEmpty, kEmpty, kEmpty, kEmpty, kEmpty});
    ASSERT_TRUE(h.eq.GetCombatManager()->SetTarget(kGuardId));
    h.clearSent();

    h.handler.castSpell(1);

    EXPECT_TRUE(h.eq.GetSpellManager()->isCasting());
    EXPECT_EQ(h.sentCasts().size(), 1u);
}
