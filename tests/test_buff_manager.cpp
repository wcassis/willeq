// T24 — BuffManager coverage (spell/combat batch, unit 2 of 3, second half).
//
// Seam: BuffManager's constructor takes a SpellDatabase* directly
// (buff_manager.h:99) -- it is constructible standalone with NO EverQuest
// at all, per refactor/T24_spell_manager.md's fixture recipe. This is the
// cleanest isolation available anywhere in this batch; no replay seam, no
// bridge, no private-access hack needed anywhere in this file.
//
// Timing note (divergent from SpellManager, worth flagging explicitly):
// unlike SpellManager::update(), BuffManager::update(float delta_time) DOES
// honor delta_time -- it accumulates into m_tick_accumulator and decrements
// buff timers once per whole accumulated second (buff_manager.cpp:26-59),
// with no steady_clock read anywhere in the class. So expiration here is
// correctly testable by calling update(1.0f) N times (or one update(N.0f)
// call, since the accumulator loop drains multiple whole seconds per call)
// -- no sleeping, no backdating needed. This is the opposite of
// SpellManager's dead-delta_time bug; both are noted for the record in the
// Review.
//
// DB dependency: most BuffManager behavior (slot bookkeeping, expiration,
// callbacks, stacking with no db) works identically whether or not a
// SpellDatabase is attached -- checkStacking/isBeneficial/isDetrimental/
// getStatModFromBuff all explicitly handle `m_spell_db == nullptr` as "can't
// check, so don't block" or "return false/0". Only isBeneficial/
// isDetrimental/getBuffedStatMod's *real-data* branches need the SQLite DB;
// those follow test_spell_database.cpp's hasDb() + GTEST_SKIP precedent.

#include <gtest/gtest.h>

#include "client/spell/buff_manager.h"
#include "client/spell/spell_database.h"
#include "client/spell/spell_data.h"
#include "client/willeq_database.h"
#include "common/packet_structs.h"

#include <cstdlib>
#include <fstream>
#include <functional>
#include <string>

using namespace EQ;

#ifndef WILLEQ_DB_PATH
#define WILLEQ_DB_PATH "data/willeq.db"
#endif

namespace {

EQT::WilleqDatabase g_willeq_db;
bool g_db_available = false;

bool hasDb() { return g_db_available; }

std::string getWilleqDbPath() {
    if (const char* env_path = std::getenv("WILLEQ_DB_PATH")) {
        return env_path;
    }
    return WILLEQ_DB_PATH;
}

uint32_t FindSpellId(SpellDatabase& db, uint32_t lo, uint32_t hi,
                      const std::function<bool(const SpellData&)>& pred) {
    for (uint32_t id = lo; id <= hi; ++id) {
        const SpellData* s = db.getSpell(id);
        if (s && pred(*s)) {
            return id;
        }
    }
    return 0;
}

EQT::SpellBuff_Struct MakeBuff(uint32_t spellid, uint8_t effect_type, int32_t duration_ticks,
                                uint32_t player_id = 0, uint8_t level = 1, uint32_t counters = 0) {
    EQT::SpellBuff_Struct b{};
    b.effect_type = effect_type;
    b.level = level;
    b.spellid = spellid;
    b.duration = duration_ticks;
    b.counters = counters;
    b.player_id = player_id;
    return b;
}

}  // namespace

// ===================================================================
// ActiveBuff helper-method unit tests -- pure struct logic, no manager.
// ===================================================================

TEST(ActiveBuffStruct, IsPermanentAndIsExpired) {
    ActiveBuff permanent;
    permanent.remaining_seconds = 0xFFFFFFFF;
    EXPECT_TRUE(permanent.isPermanent());
    EXPECT_FALSE(permanent.isExpired());

    ActiveBuff expired;
    expired.remaining_seconds = 0;
    EXPECT_FALSE(expired.isPermanent());
    EXPECT_TRUE(expired.isExpired());

    ActiveBuff active;
    active.remaining_seconds = 30;
    EXPECT_FALSE(active.isExpired());
}

// B603 deleted isAboutToExpire()/getTimeString() (nothing called either)
// and their two tests with them.

// ===================================================================
// Player buffs (setPlayerBuffs bulk load, setPlayerBuff single-slot update)
// ===================================================================

TEST(BuffManagerPlayerBuffs, SetPlayerBuffsSkipsEmptySlotsAndConvertsDuration) {
    BuffManager mgr(nullptr);
    EQT::SpellBuff_Struct buffs[3] = {
        MakeBuff(100, static_cast<uint8_t>(BuffEffectType::Buff), 10),   // finite: 10*6=60s
        MakeBuff(0, static_cast<uint8_t>(BuffEffectType::None), 0),      // empty slot, skipped
        MakeBuff(200, static_cast<uint8_t>(BuffEffectType::Inverse), -1),  // permanent
    };
    mgr.setPlayerBuffs(buffs, 3);

    EXPECT_EQ(mgr.getPlayerBuffCount(), 2u);
    ASSERT_TRUE(mgr.hasPlayerBuff(100));
    const ActiveBuff* b100 = mgr.getPlayerBuff(100);
    ASSERT_NE(b100, nullptr);
    EXPECT_EQ(b100->remaining_seconds, 60u);
    EXPECT_EQ(b100->slot, 0);

    ASSERT_TRUE(mgr.hasPlayerBuff(200));
    const ActiveBuff* b200 = mgr.getPlayerBuff(200);
    ASSERT_NE(b200, nullptr);
    EXPECT_TRUE(b200->isPermanent());
    EXPECT_EQ(b200->slot, 2) << "slot index must reflect the original array position, not compaction";

    EXPECT_FALSE(mgr.hasPlayerBuff(0));
}

TEST(BuffManagerPlayerBuffs, SetPlayerBuffsWithNullOrZeroCountClears) {
    BuffManager mgr(nullptr);
    EQT::SpellBuff_Struct buffs[1] = {MakeBuff(100, static_cast<uint8_t>(BuffEffectType::Buff), 10)};
    mgr.setPlayerBuffs(buffs, 1);
    ASSERT_EQ(mgr.getPlayerBuffCount(), 1u);

    mgr.setPlayerBuffs(nullptr, 0);
    EXPECT_EQ(mgr.getPlayerBuffCount(), 0u);
}

TEST(BuffManagerPlayerBuffs, SetPlayerBuffSingleSlotReplacesExistingInThatSlot) {
    BuffManager mgr(nullptr);
    mgr.setPlayerBuff(5, MakeBuff(100, static_cast<uint8_t>(BuffEffectType::Buff), 10));
    ASSERT_TRUE(mgr.hasPlayerBuff(100));

    mgr.setPlayerBuff(5, MakeBuff(200, static_cast<uint8_t>(BuffEffectType::Buff), 20));
    EXPECT_FALSE(mgr.hasPlayerBuff(100)) << "slot 5's old buff must be replaced, not stacked";
    EXPECT_TRUE(mgr.hasPlayerBuff(200));
    EXPECT_EQ(mgr.getPlayerBuffCount(), 1u);
}

TEST(BuffManagerPlayerBuffs, SetPlayerBuffEmptyClearsSlotWithoutAdding) {
    BuffManager mgr(nullptr);
    mgr.setPlayerBuff(5, MakeBuff(100, static_cast<uint8_t>(BuffEffectType::Buff), 10));
    ASSERT_EQ(mgr.getPlayerBuffCount(), 1u);

    mgr.setPlayerBuff(5, MakeBuff(0, static_cast<uint8_t>(BuffEffectType::None), 0));
    EXPECT_EQ(mgr.getPlayerBuffCount(), 0u);
}

TEST(BuffManagerPlayerBuffs, SetPlayerBuffFiresApplyCallback) {
    BuffManager mgr(nullptr);
    uint16_t seenEntity = 999;
    uint32_t seenSpell = 0;
    mgr.setBuffApplyCallback([&](uint16_t e, uint32_t s) {
        seenEntity = e;
        seenSpell = s;
    });

    mgr.setPlayerBuff(0, MakeBuff(300, static_cast<uint8_t>(BuffEffectType::Buff), 10));

    EXPECT_EQ(seenEntity, 0u) << "0 == player, per the implementation convention";
    EXPECT_EQ(seenSpell, 300u);
}

TEST(BuffManagerPlayerBuffs, GetPlayerBuffBySlotAndByIdMissReturnNull) {
    BuffManager mgr(nullptr);
    EXPECT_EQ(mgr.getPlayerBuff(12345), nullptr);
    EXPECT_EQ(mgr.getPlayerBuffBySlot(3), nullptr);
}

TEST(BuffManagerPlayerBuffs, RemovePlayerBuffBySlotFiresFadeCallback) {
    BuffManager mgr(nullptr);
    mgr.setPlayerBuff(2, MakeBuff(400, static_cast<uint8_t>(BuffEffectType::Buff), 10));
    ASSERT_TRUE(mgr.hasPlayerBuff(400));

    uint16_t seenEntity = 999;
    uint32_t seenSpell = 0;
    mgr.setBuffFadeCallback([&](uint16_t e, uint32_t s) {
        seenEntity = e;
        seenSpell = s;
    });

    mgr.removePlayerBuffBySlot(2);

    EXPECT_FALSE(mgr.hasPlayerBuff(400));
    EXPECT_EQ(seenEntity, 0u);
    EXPECT_EQ(seenSpell, 400u);
}

TEST(BuffManagerPlayerBuffs, RemovePlayerBuffBySlotOnEmptySlotIsNoOp) {
    BuffManager mgr(nullptr);
    bool called = false;
    mgr.setBuffFadeCallback([&](uint16_t, uint32_t) { called = true; });

    EXPECT_NO_THROW(mgr.removePlayerBuffBySlot(9));
    EXPECT_FALSE(called);
}

// ===================================================================
// Pet buffs
// ===================================================================

TEST(BuffManagerPetBuffs, SetPetBuffsParsesArrayAndSkipsEmptySlots) {
    BuffManager mgr(nullptr);
    EQT::PetBuff_Struct pb{};
    pb.petid = 777;
    pb.buffcount = 3;
    pb.spellid[0] = 100;
    pb.ticsremaining[0] = 5;   // finite: 30s
    pb.spellid[1] = 0xFFFFFFFF;  // empty
    pb.ticsremaining[1] = 0;
    pb.spellid[2] = 200;
    pb.ticsremaining[2] = -1;  // permanent

    mgr.setPetBuffs(777, pb);

    EXPECT_EQ(mgr.getPetBuffsOwnerId(), 777u);
    const auto& buffs = mgr.getPetBuffs();
    ASSERT_EQ(buffs.size(), 2u);
    EXPECT_EQ(buffs[0].spell_id, 100u);
    EXPECT_EQ(buffs[0].remaining_seconds, 30u);
    EXPECT_EQ(buffs[1].spell_id, 200u);
    EXPECT_TRUE(buffs[1].isPermanent());
}

TEST(BuffManagerPetBuffs, SetPetBuffsWithoutDbDefaultsToBuffEffectType) {
    BuffManager mgr(nullptr);
    EQT::PetBuff_Struct pb{};
    pb.petid = 1;
    pb.buffcount = 1;
    pb.spellid[0] = 100;
    pb.ticsremaining[0] = 5;

    mgr.setPetBuffs(1, pb);
    ASSERT_EQ(mgr.getPetBuffs().size(), 1u);
    EXPECT_EQ(mgr.getPetBuffs()[0].effect_type, BuffEffectType::Buff);
}

TEST(BuffManagerPetBuffs, ClearPetBuffsResetsOwnerAndList) {
    BuffManager mgr(nullptr);
    EQT::PetBuff_Struct pb{};
    pb.petid = 1;
    pb.buffcount = 1;
    pb.spellid[0] = 100;
    pb.ticsremaining[0] = 5;
    mgr.setPetBuffs(1, pb);
    ASSERT_FALSE(mgr.getPetBuffs().empty());

    mgr.clearPetBuffs();
    EXPECT_TRUE(mgr.getPetBuffs().empty());
    EXPECT_EQ(mgr.getPetBuffsOwnerId(), 0u);
}

TEST(BuffManagerPetBuffs, SetPetBuffsReplacesPriorSet) {
    BuffManager mgr(nullptr);
    EQT::PetBuff_Struct pb1{};
    pb1.petid = 1;
    pb1.buffcount = 1;
    pb1.spellid[0] = 100;
    pb1.ticsremaining[0] = 5;
    mgr.setPetBuffs(1, pb1);
    ASSERT_EQ(mgr.getPetBuffs().size(), 1u);

    EQT::PetBuff_Struct pb2{};
    pb2.petid = 2;
    pb2.buffcount = 0;
    mgr.setPetBuffs(2, pb2);
    EXPECT_TRUE(mgr.getPetBuffs().empty());
    EXPECT_EQ(mgr.getPetBuffsOwnerId(), 2u);
}

// ===================================================================
// Entity buffs
// ===================================================================

TEST(BuffManagerEntityBuffs, SetGetRoundTrip) {
    BuffManager mgr(nullptr);
    mgr.setEntityBuff(500, 0, MakeBuff(100, static_cast<uint8_t>(BuffEffectType::Buff), 10));
    mgr.setEntityBuff(500, 1, MakeBuff(200, static_cast<uint8_t>(BuffEffectType::Inverse), -1));

    EXPECT_TRUE(mgr.hasEntityBuff(500, 100));
    EXPECT_TRUE(mgr.hasEntityBuff(500, 200));
    EXPECT_FALSE(mgr.hasEntityBuff(500, 999));
    EXPECT_FALSE(mgr.hasEntityBuff(999, 100)) << "unrelated entity must not see 500's buffs";

    const auto* buffs = mgr.getEntityBuffs(500);
    ASSERT_NE(buffs, nullptr);
    EXPECT_EQ(buffs->size(), 2u);
}

TEST(BuffManagerEntityBuffs, GetEntityBuffsUnknownEntityReturnsNull) {
    BuffManager mgr(nullptr);
    EXPECT_EQ(mgr.getEntityBuffs(12345), nullptr);
}

TEST(BuffManagerEntityBuffs, SetEntityBuffFiresApplyCallbackWithEntityId) {
    BuffManager mgr(nullptr);
    uint16_t seenEntity = 0;
    mgr.setBuffApplyCallback([&](uint16_t e, uint32_t) { seenEntity = e; });

    mgr.setEntityBuff(500, 0, MakeBuff(100, static_cast<uint8_t>(BuffEffectType::Buff), 10));
    EXPECT_EQ(seenEntity, 500u);
}

// ===================================================================
// Buff application (applyBuff/removeBuff/removeBuffBySlot) -- with
// m_spell_db == nullptr, checkStacking() explicitly allows everything
// (buff_manager.cpp:538: "Allow if we can't check"), so these exercise the
// slot-assignment and player/entity routing logic in isolation from DB
// content.
// ===================================================================

TEST(BuffManagerApply, ApplyBuffToPlayerRoutesToPlayerList) {
    BuffManager mgr(nullptr);
    mgr.applyBuff(/*target_id=*/0, /*spell_id=*/100, /*caster_id=*/1, /*caster_level=*/50,
                  /*duration_ticks=*/10);
    EXPECT_TRUE(mgr.hasPlayerBuff(100));
    EXPECT_EQ(mgr.getPlayerBuff(100)->remaining_seconds, 60u);
}

TEST(BuffManagerApply, ApplyBuffToEntityRoutesToEntityList) {
    BuffManager mgr(nullptr);
    mgr.applyBuff(/*target_id=*/500, /*spell_id=*/100, /*caster_id=*/1, /*caster_level=*/50,
                  /*duration_ticks=*/10);
    EXPECT_FALSE(mgr.hasPlayerBuff(100));
    EXPECT_TRUE(mgr.hasEntityBuff(500, 100));
}

TEST(BuffManagerApply, ApplyBuffPermanentSentinel) {
    BuffManager mgr(nullptr);
    mgr.applyBuff(0, 100, 1, 50, /*duration_ticks=*/0xFFFFFFFF);
    ASSERT_TRUE(mgr.hasPlayerBuff(100));
    EXPECT_TRUE(mgr.getPlayerBuff(100)->isPermanent());
}

TEST(BuffManagerApply, ApplyBuffSameSpellReplacesInstance) {
    BuffManager mgr(nullptr);
    mgr.applyBuff(0, 100, 1, 50, 10);
    mgr.applyBuff(0, 100, 1, 50, 20);  // refresh with a new duration
    EXPECT_EQ(mgr.getPlayerBuffCount(), 1u);
    EXPECT_EQ(mgr.getPlayerBuff(100)->remaining_seconds, 120u);
}

TEST(BuffManagerApply, ApplyBuffFiresApplyCallbackWithTargetId) {
    BuffManager mgr(nullptr);
    uint16_t seenEntity = 999;
    mgr.setBuffApplyCallback([&](uint16_t e, uint32_t) { seenEntity = e; });
    mgr.applyBuff(500, 100, 1, 50, 10);
    EXPECT_EQ(seenEntity, 500u);
}

TEST(BuffManagerApply, ApplyBuffPastMaxPlayerBuffsIsSilentlyDropped) {
    // findFreeSlot() returns -1 once all MAX_PLAYER_BUFFS (25) slots are
    // used, and applyBuff() has no other guard -- the (26th) buff is simply
    // never added. Distinct spell ids so checkStacking (nullptr db => always
    // true) and the "same spell replaces" branch don't interfere.
    BuffManager mgr(nullptr);
    for (uint32_t i = 0; i < BuffManager::MAX_PLAYER_BUFFS; ++i) {
        mgr.applyBuff(0, 1000 + i, 1, 50, 10);
    }
    ASSERT_EQ(mgr.getPlayerBuffCount(), BuffManager::MAX_PLAYER_BUFFS);

    mgr.applyBuff(0, 9999, 1, 50, 10);
    EXPECT_EQ(mgr.getPlayerBuffCount(), BuffManager::MAX_PLAYER_BUFFS)
        << "the 26th distinct buff must be dropped, not appended past capacity";
    EXPECT_FALSE(mgr.hasPlayerBuff(9999));
}

TEST(BuffManagerApply, RemoveBuffBySlotOnEntityWorks) {
    BuffManager mgr(nullptr);
    mgr.setEntityBuff(500, 3, MakeBuff(100, static_cast<uint8_t>(BuffEffectType::Buff), 10));
    ASSERT_TRUE(mgr.hasEntityBuff(500, 100));

    mgr.removeBuffBySlot(500, 3);
    EXPECT_FALSE(mgr.hasEntityBuff(500, 100));
}

// ===================================================================
// Buff expiration -- delta_time-driven (see file header). No sleeping, no
// backdating: BuffManager::update() genuinely honors its argument.
// ===================================================================

TEST(BuffManagerExpiration, UpdateDecrementsBySeconds) {
    BuffManager mgr(nullptr);
    mgr.applyBuff(0, 100, 1, 50, /*duration_ticks=*/2);  // 2*6=12s
    ASSERT_EQ(mgr.getPlayerBuff(100)->remaining_seconds, 12u);

    mgr.update(5.0f);
    EXPECT_EQ(mgr.getPlayerBuff(100)->remaining_seconds, 7u);
}

TEST(BuffManagerExpiration, UpdateFiresFadeCallbackAndRemovesOnExpiry) {
    BuffManager mgr(nullptr);
    mgr.applyBuff(0, 100, 1, 50, /*duration_ticks=*/1);  // 6s
    bool faded = false;
    uint32_t seenSpell = 0;
    mgr.setBuffFadeCallback([&](uint16_t entity, uint32_t spell) {
        faded = true;
        seenSpell = spell;
        EXPECT_EQ(entity, 0u);
    });

    mgr.update(6.0f);  // exactly drains the 6s duration to 0

    EXPECT_TRUE(faded);
    EXPECT_EQ(seenSpell, 100u);
    EXPECT_FALSE(mgr.hasPlayerBuff(100));
}

TEST(BuffManagerExpiration, PermanentBuffNeverDecrementsOrExpires) {
    BuffManager mgr(nullptr);
    mgr.applyBuff(0, 100, 1, 50, /*duration_ticks=*/0xFFFFFFFF);
    mgr.update(100000.0f);
    ASSERT_TRUE(mgr.hasPlayerBuff(100));
    EXPECT_TRUE(mgr.getPlayerBuff(100)->isPermanent());
}

TEST(BuffManagerExpiration, SubOneSecondDeltaAccumulatesAcrossCalls) {
    // The accumulator (buff_manager.cpp:29-59) only decrements on whole
    // seconds -- three 0.4s updates (1.2s accumulated) must drain exactly
    // one second, not zero and not two.
    BuffManager mgr(nullptr);
    mgr.applyBuff(0, 100, 1, 50, /*duration_ticks=*/2);  // 12s
    mgr.update(0.4f);
    mgr.update(0.4f);
    EXPECT_EQ(mgr.getPlayerBuff(100)->remaining_seconds, 12u) << "0.8s accumulated, not yet a full second";
    mgr.update(0.4f);
    EXPECT_EQ(mgr.getPlayerBuff(100)->remaining_seconds, 11u) << "1.2s accumulated, drains exactly one second";
}

TEST(BuffManagerExpiration, ExpirationAppliesToPetAndEntityBuffsToo) {
    BuffManager mgr(nullptr);
    EQT::PetBuff_Struct pb{};
    pb.petid = 1;
    pb.buffcount = 1;
    pb.spellid[0] = 100;
    pb.ticsremaining[0] = 1;  // 6s
    mgr.setPetBuffs(1, pb);
    mgr.setEntityBuff(500, 0, MakeBuff(200, static_cast<uint8_t>(BuffEffectType::Buff), 1));  // 6s

    mgr.update(6.0f);

    EXPECT_TRUE(mgr.getPetBuffs().empty());
    // Real behavior, verified rather than assumed: checkBuffExpiration()
    // (buff_manager.cpp) erases individual expired ActiveBuff entries
    // from the per-entity vector, but never erases the now-empty vector's
    // key from m_entity_buffs itself.
    // getEntityBuffs() therefore returns a non-null pointer to an empty
    // vector here, not nullptr -- distinct from a truly-unknown entity id.
    const auto* entityBuffs = mgr.getEntityBuffs(500);
    ASSERT_NE(entityBuffs, nullptr);
    EXPECT_TRUE(entityBuffs->empty());
}

// ===================================================================
// Spell classification / stat mods -- DB-gated (real is_beneficial /
// effects data).
// ===================================================================

TEST(BuffManagerClassification, WithoutDbAlwaysReturnsFalse) {
    BuffManager mgr(nullptr);
    EXPECT_FALSE(mgr.isBeneficial(1));
    EXPECT_FALSE(mgr.isDetrimental(1));
}

TEST(BuffManagerClassification, IsBeneficialMatchesRealSpellData) {
    if (!hasDb()) GTEST_SKIP() << "willeq.db not found, skipping";
    SpellDatabase db;
    ASSERT_TRUE(db.initFromDatabase());
    BuffManager mgr(&db);

    uint32_t beneficialId = FindSpellId(db, 1, 4000, [](const SpellData& s) { return s.is_beneficial; });
    uint32_t detrimentalId = FindSpellId(db, 1, 4000, [](const SpellData& s) { return !s.is_beneficial; });
    if (beneficialId == 0 || detrimentalId == 0) {
        GTEST_SKIP() << "could not find both a beneficial and detrimental spell in scanned range";
    }

    EXPECT_TRUE(mgr.isBeneficial(beneficialId));
    EXPECT_FALSE(mgr.isDetrimental(beneficialId));
    EXPECT_TRUE(mgr.isDetrimental(detrimentalId));
    EXPECT_FALSE(mgr.isBeneficial(detrimentalId));
}

TEST(BuffManagerClassification, UnknownSpellIdIsNeitherViaRealDb) {
    if (!hasDb()) GTEST_SKIP() << "willeq.db not found, skipping";
    SpellDatabase db;
    ASSERT_TRUE(db.initFromDatabase());
    BuffManager mgr(&db);
    EXPECT_FALSE(mgr.isBeneficial(999999999));
    EXPECT_FALSE(mgr.isDetrimental(999999999));
}

// getBuffedStatMod/getPlayerStatMod (T68 update): the T22-era version of
// this test asserted the stat mod was ALWAYS 0, because SpellData::effects
// was never populated -- EQT::SpellInfo had no effects field to map from at
// all (see git history for that comment). T68 populated effects, so
// getStatModFromBuff() (buff_manager.cpp:488-506) now has real data to walk.
// Exercising it against a confirmed real spell: "Strengthen" (id 40) is a
// beneficial, single-slot STR buff with base_value 4 (spot-checked directly
// against willeq.db: effect_id_1=4/STR, effect_id_2=254/unused). Applying it
// as a player buff must now make getPlayerStatMod(STR) report +4, not 0 --
// this is the newly-live behavior the whole unit exists to unblock.
//
// (First attempt used id 21 "Berserker Strength", which turned out to also
// carry an AGI debuff in slot 3 -- a genuinely multi-effect real spell, not
// a bug. Switched to a single-slot spell so the "no matching effect" test
// below isn't accidentally exercising a real second effect.)
TEST(BuffManagerStatMod, PlayerStatModAppliesRealEffectValue) {
    if (!hasDb()) GTEST_SKIP() << "willeq.db not found, skipping";
    SpellDatabase db;
    ASSERT_TRUE(db.initFromDatabase());

    const SpellData* spell = db.getSpell(40);
    ASSERT_NE(spell, nullptr);
    ASSERT_EQ(spell->name, "Strengthen");
    ASSERT_EQ(spell->getEffectCount(), 1);
    ASSERT_TRUE(spell->hasEffect(SpellEffect::STR));
    ASSERT_EQ(spell->getEffect(SpellEffect::STR)->base_value, 4);

    BuffManager mgr(&db);
    mgr.applyBuff(0, 40, 1, 50, 0xFFFFFFFF);
    ASSERT_TRUE(mgr.hasPlayerBuff(40));
    EXPECT_EQ(mgr.getPlayerStatMod(SpellEffect::STR), 4)
        << "T68: SpellData::effects is now populated; getStatModFromBuff should "
           "sum the real base_value instead of always returning 0";
}

// A beneficial spell with no AGI effect at all (still exercising the
// now-live path) must correctly report 0 -- confirms getStatModFromBuff
// filters by effect type rather than summing everything indiscriminately.
TEST(BuffManagerStatMod, PlayerStatModIsZeroWhenBuffHasNoMatchingEffect) {
    if (!hasDb()) GTEST_SKIP() << "willeq.db not found, skipping";
    SpellDatabase db;
    ASSERT_TRUE(db.initFromDatabase());

    const SpellData* spell = db.getSpell(40);  // "Strengthen" — STR only, one slot
    ASSERT_NE(spell, nullptr);
    ASSERT_FALSE(spell->hasEffect(SpellEffect::AGI));

    BuffManager mgr(&db);
    mgr.applyBuff(0, 40, 1, 50, 0xFFFFFFFF);
    ASSERT_TRUE(mgr.hasPlayerBuff(40));
    EXPECT_EQ(mgr.getPlayerStatMod(SpellEffect::AGI), 0);
}

// Main entry point: opens the real SQLite spell DB once for the whole
// binary (test_spell_database.cpp precedent) so DB-gated tests can run;
// DB-independent tests (the large majority in this file) run regardless.
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
