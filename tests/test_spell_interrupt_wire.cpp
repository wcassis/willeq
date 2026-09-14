// T469 (board B604) -- the server's cast interrupt, fed as the bytes eqemu
// actually puts on the Titanium wire, through the real zone dispatcher, into a
// headless EverQuest part-way through a cast.
//
// What the server sends (akk-stack/code, read at T469):
//
//  * Every interrupt cause a client can suffer -- moving or taking damage
//    during a cast (zone/spells.cpp:1557, CastedSpellFinished's
//    channelling roll; zone/attack.cpp:4456), a stun (spells.cpp:5781), a mez
//    (spells.cpp:5840), dying (attack.cpp:1901), no target / no mana
//    (spells.cpp:400, :490), and GM #interrupt -- goes through
//    Mob::InterruptSpell(message, color, spellid) (spells.cpp:1242). For a
//    client caster that queues, to the CASTER ONLY:
//      1. OP_InterruptCast, InterruptCast_Struct {uint32 spawnid; uint32
//         messageid;} (common/patches/titanium_structs.h:426) -- 8 bytes,
//         messageid INTERRUPT_SPELL 439 "Your spell is interrupted." (or a
//         song message for bards). NO spell id. Titanium has no ENCODE for it
//         (titanium_ops.h), so the struct is the wire form verbatim.
//      2. OP_ManaChange via SendSpellBarEnable (spells.cpp:5752) --
//         keepcasting 0, spell_id = the interrupted spell.
//    and, to everyone ELSE in range (QueueCloseClients with ignore_sender =
//    true, spells.cpp:1344), OP_InterruptCast with messageid
//    INTERRUPT_SPELL_OTHER 12478 "%1's casting is interrupted!" and the
//    caster's name appended as a C string.
//
//  * A FIZZLE is different: Mob::DoCastSpell (spells.cpp:321-331) calls
//    StopCasting() (spells.cpp:1351), which sends ONLY OP_ManaChange with
//    keepcasting 0, then a MessageString SPELL_FIZZLE 173. No OP_InterruptCast
//    at all, and it happens BEFORE SendBeginCast (spells.cpp:450), so the cast
//    was never confirmed.
//
// Before T469 the dispatcher listed HC_OP_InterruptCast as deliberately
// ignored ("spell manager handles this via other packets") and the ManaChange
// arm only logged keepcasting. So an interrupted cast stayed "casting" on
// fable: the gem stayed locked, the next cast was refused client-side as
// AlreadyCasting, and update()'s duration+5 s fallback finally reported it as
// a SUCCESS -- gem recast started and an impact burst painted for a spell the
// server had cancelled.

#include <gtest/gtest.h>

#define private public
#define protected public
#include "client/eq.h"
#include "client/spell/spell_manager.h"
#undef private
#undef protected

#include "client/bridge/game_state_bridge.h"
#include "client/combat.h"
#include "client/state/event_bus.h"
#include "eqstr_test_db.h"
#include "zone_stream_test_utils.h"

#include <chrono>
#include <cstring>
#include <string>
#include <vector>

using namespace zonetest;

namespace {

constexpr uint16_t kSelfId = 460;
constexpr uint16_t kOtherCasterId = 200;
constexpr uint32_t kSpellId = 777;
constexpr uint32_t kOtherSpellId = 88;
constexpr uint32_t kCastTimeMs = 3000;
constexpr uint8_t kGem = 2;

// zone/string_ids.h
constexpr uint32_t kInterruptSpell = 439;         // Your spell is interrupted.
constexpr uint32_t kInterruptSpellOther = 12478;  // %1's casting is interrupted!

void PutLE(std::vector<uint8_t>& v, size_t at, uint32_t value, size_t width) {
    for (size_t i = 0; i < width; ++i) {
        v[at + i] = static_cast<uint8_t>((value >> (8 * i)) & 0xFF);
    }
}

// titanium_structs.h:426 InterruptCast_Struct {uint32 spawnid; uint32
// messageid; char message[0];}. The caster copy has no message; the bystander
// copy carries the caster's clean name, NUL-terminated (spells.cpp:1339-1343).
std::vector<uint8_t> InterruptCastPayload(uint32_t spawnid, uint32_t messageid,
                                          const std::string* name = nullptr) {
    std::vector<uint8_t> v(8 + (name ? name->size() + 1 : 0), 0);
    PutLE(v, 0, spawnid, 4);
    PutLE(v, 4, messageid, 4);
    if (name) {
        std::memcpy(v.data() + 8, name->c_str(), name->size() + 1);
    }
    return v;
}

// titanium_structs.h:441 ManaChange_Struct {uint32 new_mana; uint32 stamina;
// uint32 spell_id; uint8 keepcasting; uint8 padding[3];} -- 16 bytes; the
// Titanium ENCODE (titanium.cpp:1247) drops the server struct's trailing slot.
std::vector<uint8_t> ManaChangePayload(uint32_t mana, uint32_t stamina,
                                       uint32_t spell_id, uint8_t keepcasting) {
    std::vector<uint8_t> v(16, 0);
    PutLE(v, 0, mana, 4);
    PutLE(v, 4, stamina, 4);
    PutLE(v, 8, spell_id, 4);
    v[12] = keepcasting;
    return v;
}

// BeginCast_Struct {uint16 caster_id; uint16 spell_id; uint32 cast_time;}
// (eq_packet_structs.h; what Mob::SendBeginCast fills, spells.cpp:497).
std::vector<uint8_t> BeginCastWire(uint16_t caster, uint16_t spell, uint32_t ms) {
    std::vector<uint8_t> v(8, 0);
    PutLE(v, 0, caster, 2);
    PutLE(v, 2, spell, 2);
    PutLE(v, 4, ms, 4);
    return v;
}

int CountEvents(const std::vector<eqt::state::GameEvent>& events,
                eqt::state::GameEventType type) {
    int n = 0;
    for (const auto& e : events) {
        if (e.type == type) ++n;
    }
    return n;
}

class SpellInterruptWire : public ::testing::Test {
protected:
    EverQuest eq = MakeEq();
    TestBridge bridge;
    EQ::SpellManager* mgr = nullptr;

    void SetUp() override {
        eq.Test_SetMySpawnIdForSeam(kSelfId);
        ASSERT_EQ(eq.GetEntityID(), kSelfId);
        mgr = eq.GetSpellManager();
        ASSERT_NE(mgr, nullptr);
        mgr->setBridge(&bridge);
    }

    void TearDown() override {
        // `bridge` is destroyed before `eq`; leave nothing pointing at it.
        eq.m_bridge = nullptr;
        if (mgr) mgr->setBridge(nullptr);
    }

    // What beginCastFromGem() leaves behind for a gem cast (it needs a
    // DB-backed spell to get there, so the state is staged directly -- the
    // same staging test_game_state_source_writes.cpp uses).
    void StageGemCastSent() {
        mgr->m_spell_gems[kGem] = kSpellId;
        mgr->m_gem_states[kGem] = EQ::GemState::Casting;
        mgr->m_is_casting = true;
        mgr->m_current_spell_id = kSpellId;
        mgr->m_current_gem_slot = kGem;
        mgr->m_current_target_id = kSelfId;
        mgr->m_cast_start_time = std::chrono::steady_clock::now();
        mgr->m_cast_duration_ms = kCastTimeMs;
        mgr->m_waiting_for_server_confirm = true;
        eq.GetGameState().spells().setCasting(true, kSpellId, kSelfId, kCastTimeMs);
        eq.GetGameState().spells().setGem(kGem, kSpellId,
                                          eqt::state::SpellGemState::Casting);
    }

    void StageConfirmedGemCast() {
        StageGemCastSent();
        ReplayZone(eq, HC_OP_BeginCast, BeginCastWire(kSelfId, kSpellId, kCastTimeMs));
        ASSERT_TRUE(mgr->isCasting());
        ASSERT_FALSE(mgr->m_waiting_for_server_confirm) << "BeginCast must confirm the cast";
        bridge.drainEvents();
    }

    // The two packets Mob::InterruptSpell queues to a client caster, in order.
    void ReplayServerInterruptOfOwnCast() {
        ReplayZone(eq, HC_OP_InterruptCast, InterruptCastPayload(kSelfId, kInterruptSpell));
        ReplayZone(eq, HC_OP_ManaChange, ManaChangePayload(100, 100, kSpellId, 0));
    }

    void ExpectCastEndedInterrupted(const std::vector<eqt::state::GameEvent>& events) {
        EXPECT_FALSE(mgr->isCasting());
        EXPECT_EQ(mgr->getCurrentSpellId(), EQ::SPELL_UNKNOWN);
        // eqemu starts no recast on an interrupt (InterruptSpell only zeroes
        // the casting vars and re-enables the bar), so the gem is usable.
        EXPECT_EQ(mgr->getGemState(kGem), EQ::GemState::Ready);
        EXPECT_FALSE(eq.GetGameState().spells().isCasting());
        EXPECT_TRUE(eq.GetGameState().spells().isGemReady(kGem));

        bool sawInterruptedVisual = false;
        bool sawCompleteInterrupted = false;
        for (const auto& e : events) {
            if (e.type == eqt::state::GameEventType::SpellCastVisualInterrupted) {
                const auto& d = std::get<eqt::state::SpellCastVisualInterruptedData>(e.data);
                EXPECT_EQ(d.casterId, kSelfId);
                EXPECT_EQ(d.spellId, kSpellId);
                EXPECT_TRUE(d.isPlayerCast);
                sawInterruptedVisual = true;
            }
            if (e.type == eqt::state::GameEventType::SpellCastComplete) {
                const auto& d = std::get<eqt::state::SpellCastCompleteData>(e.data);
                EXPECT_EQ(d.spellId, kSpellId);
                EXPECT_EQ(d.result, static_cast<uint8_t>(EQ::CastResult::Interrupted));
                sawCompleteInterrupted = true;
            }
        }
        EXPECT_TRUE(sawInterruptedVisual) << "the cast bar and cast glow are cancelled by this event";
        EXPECT_TRUE(sawCompleteInterrupted);
        EXPECT_EQ(CountEvents(events, eqt::state::GameEventType::SpellCastVisualComplete), 0)
            << "an interrupted cast must not paint a completion";
    }
};

// The primary lead: moved / hit / stunned during a confirmed cast.
TEST_F(SpellInterruptWire, ServerInterruptOfConfirmedCastEndsTheCast) {
    StageConfirmedGemCast();
    ReplayServerInterruptOfOwnCast();
    ExpectCastEndedInterrupted(bridge.drainEvents());
}

// DoCastSpell's no-target / no-mana interrupts (spells.cpp:400, :490) come
// before SendBeginCast, so the cast was never confirmed.
TEST_F(SpellInterruptWire, ServerInterruptBeforeBeginCastEndsTheCast) {
    StageGemCastSent();
    bridge.drainEvents();
    ReplayServerInterruptOfOwnCast();
    ExpectCastEndedInterrupted(bridge.drainEvents());
}

// Will's rule: a client gate refusing what the server would accept is a
// defect. After the interrupt the server accepts a new cast; the client must
// not refuse it as AlreadyCasting.
TEST_F(SpellInterruptWire, NextCastIsNotRefusedAsAlreadyCastingAfterInterrupt) {
    StageConfirmedGemCast();
    ReplayServerInterruptOfOwnCast();
    // No spell DB in this binary, so a let-through gem cast stops at the spell
    // lookup (InvalidSpell). What must not happen is the client's own
    // casting-state gate answering first.
    EXPECT_NE(mgr->beginCastFromGem(kGem), EQ::CastResult::AlreadyCasting);
    EXPECT_FALSE(mgr->isCasting());
}

// The fallback that used to end an interrupted cast: duration + 5 s later,
// update() reported it as a SUCCESS (gem recast, impact burst).
TEST_F(SpellInterruptWire, InterruptedCastIsNeverReportedAsSuccessByTheTimeout) {
    StageConfirmedGemCast();
    ReplayServerInterruptOfOwnCast();
    bridge.drainEvents();

    mgr->m_cast_start_time = std::chrono::steady_clock::now() -
        std::chrono::milliseconds(kCastTimeMs + 6000);
    mgr->update(0.0f);

    const auto events = bridge.drainEvents();
    for (const auto& e : events) {
        if (e.type == eqt::state::GameEventType::SpellCastComplete) {
            const auto& d = std::get<eqt::state::SpellCastCompleteData>(e.data);
            EXPECT_NE(d.result, static_cast<uint8_t>(EQ::CastResult::Success))
                << "the server cancelled this cast";
        }
    }
    EXPECT_EQ(CountEvents(events, eqt::state::GameEventType::SpellCastVisualComplete), 0);
    EXPECT_NE(mgr->getGemState(kGem), EQ::GemState::Refresh)
        << "no recast starts for a cast the server interrupted";
}

// Fizzle: StopCasting's lone OP_ManaChange (keepcasting 0) before BeginCast.
TEST_F(SpellInterruptWire, FizzleManaChangeBeforeBeginCastEndsTheCast) {
    StageGemCastSent();
    bridge.drainEvents();
    ReplayZone(eq, HC_OP_ManaChange, ManaChangePayload(100, 100, kSpellId, 0));
    ExpectCastEndedInterrupted(bridge.drainEvents());
}

// Guards on the ManaChange rule -- each is green before and after T469.
//
// Client::CheckManaEndUpdate (client.cpp:2427-2433) sends keepcasting 1 on
// every mana tick, including mid-cast.
TEST_F(SpellInterruptWire, ManaTickWithKeepcastingOneNeverEndsACast) {
    StageGemCastSent();
    ReplayZone(eq, HC_OP_ManaChange, ManaChangePayload(90, 100, kSpellId, 1));
    EXPECT_TRUE(mgr->isCasting());
    ReplayZone(eq, HC_OP_BeginCast, BeginCastWire(kSelfId, kSpellId, kCastTimeMs));
    ReplayZone(eq, HC_OP_ManaChange, ManaChangePayload(80, 100, kSpellId, 1));
    EXPECT_TRUE(mgr->isCasting());
}

// A SUCCESSFUL cast also sends keepcasting 0 (CastedSpellFinished ->
// SendSpellBarEnable, spells.cpp:1817), and a spell with no landing packet
// (a pet summon) has nothing else. After BeginCast the packet is therefore
// ambiguous and must not be read as an interrupt.
TEST_F(SpellInterruptWire, KeepcastingZeroAfterBeginCastIsNotAnInterrupt) {
    StageConfirmedGemCast();
    ReplayZone(eq, HC_OP_ManaChange, ManaChangePayload(100, 100, kSpellId, 0));
    EXPECT_TRUE(mgr->isCasting());
    EXPECT_EQ(CountEvents(bridge.drainEvents(),
                          eqt::state::GameEventType::SpellCastVisualInterrupted), 0);
}

TEST_F(SpellInterruptWire, KeepcastingZeroForAnotherSpellDoesNotEndTheCast) {
    StageGemCastSent();
    ReplayZone(eq, HC_OP_ManaChange, ManaChangePayload(100, 100, kSpellId + 1, 0));
    EXPECT_TRUE(mgr->isCasting());
}

// The bystander copy: someone else's cast, carrying the caster's name.
TEST_F(SpellInterruptWire, OtherCastersInterruptCancelsTheirCastNotOurs) {
    ASSERT_NO_THROW(ReplayZone(eq, HC_OP_NewSpawn,
        NewSpawnPayloadAt(kOtherCasterId, "an_enemy_caster", 0, 10, 0)));
    ASSERT_TRUE(eq.GetCombatManager()->SetTarget(kOtherCasterId));
    StageConfirmedGemCast();
    ReplayZone(eq, HC_OP_BeginCast, BeginCastWire(kOtherCasterId, kOtherSpellId, 2000));
    ASSERT_EQ(mgr->m_target_caster_id, kOtherCasterId);
    bridge.drainEvents();

    const std::string name = "an enemy caster";
    ReplayZone(eq, HC_OP_InterruptCast,
               InterruptCastPayload(kOtherCasterId, kInterruptSpellOther, &name));

    EXPECT_EQ(mgr->m_target_caster_id, 0u);
    EXPECT_TRUE(mgr->isCasting()) << "someone else's interrupt must not end our cast";

    const auto events = bridge.drainEvents();
    bool saw = false;
    for (const auto& e : events) {
        if (e.type == eqt::state::GameEventType::SpellCastVisualInterrupted) {
            const auto& d = std::get<eqt::state::SpellCastVisualInterruptedData>(e.data);
            EXPECT_EQ(d.casterId, kOtherCasterId);
            EXPECT_EQ(d.spellId, kOtherSpellId);
            EXPECT_FALSE(d.isPlayerCast);
            EXPECT_TRUE(d.isTargetCast);
            saw = true;
        }
    }
    EXPECT_TRUE(saw) << "the caster's cast bar and glow are cancelled by this event";
    EXPECT_EQ(CountEvents(events, eqt::state::GameEventType::SpellCastComplete), 0);
}

// The packet is itself the chat line (spells.cpp:1338, "it works the same as
// a formatted message"): the caster reads eqstr 439, a bystander eqstr 12478
// with the caster's name in %1. Resolved through data/willeq.db's eqstrings
// table, the same lookup every other eqstr message uses.
TEST_F(SpellInterruptWire, InterruptMessageTextReachesChat) {
    ASSERT_TRUE(eqt_test::ShippedStrings().isLoaded()) << "data/willeq.db is missing";
    ASSERT_TRUE(eq.Strings().LoadStringFiles(""));
    eq.m_bridge = &bridge;  // chat output publishes through EverQuest's bridge

    StageConfirmedGemCast();
    ReplayServerInterruptOfOwnCast();
    const std::string fippy = "Fippy Darkpaw";
    ReplayZone(eq, HC_OP_InterruptCast,
               InterruptCastPayload(kOtherCasterId, kInterruptSpellOther, &fippy));

    std::vector<std::string> lines;
    for (const auto& e : bridge.drainEvents()) {
        if (e.type == eqt::state::GameEventType::SystemMessage) {
            lines.push_back(std::get<eqt::state::ChatMessageData>(e.data).message);
        }
    }
    ASSERT_EQ(lines.size(), 2u);
    EXPECT_EQ(lines[0], "Your spell is interrupted.");
    EXPECT_EQ(lines[1], "Fippy Darkpaw's casting is interrupted!");
}

TEST_F(SpellInterruptWire, TruncatedInterruptPacketIsIgnored) {
    StageConfirmedGemCast();
    std::vector<uint8_t> shortPayload(4, 0);
    PutLE(shortPayload, 0, kSelfId, 4);
    EXPECT_NO_THROW(ReplayZone(eq, HC_OP_InterruptCast, shortPayload));
    EXPECT_TRUE(mgr->isCasting());
}

}  // namespace
