/*
 * Tests for SpellVisualSelector (T72).
 *
 * Mirrors test_spells_eff_reader.cpp's GTEST_SKIP-on-missing-data pattern
 * (T70) rather than trusting fixtures. The pure-logic tests (no file/DB
 * dependency) always run; the census/index-resolution tests gate on
 * data/willeq.db and the real spells.eff being present.
 */

#include <gtest/gtest.h>

#include <cstdlib>
#include <filesystem>
#include <string>

#include "client/graphics/spell_visual_selection.h"
#include "client/spell/spell_database.h"
#include "client/willeq_database.h"

using namespace EQ;

namespace {

std::string getEqClientPath() {
    const char* env = std::getenv("EQ_CLIENT_PATH");
    return env ? env : EQ_CLIENT_DATA_PATH;
}

std::string getSpellsEffPath() {
    return getEqClientPath() + "/spells.eff";
}

std::string getWilleqDbPath() {
    const char* env_path = std::getenv("WILLEQ_DB_PATH");
    return env_path ? env_path : WILLEQ_DB_PATH;
}

}  // namespace

// ============================================================================
// Pure-logic tests: no reader loaded at all (reader == nullptr).
// ============================================================================

TEST(SpellVisualSelectorNullReaderTest, NoReaderAlwaysFallsBackToNoAffectIndex) {
    SpellVisualSelector selector(nullptr);

    SpellVisualResolution negOne = selector.resolve(-1);
    EXPECT_EQ(negOne.source, SpellVisualSource::NoAffectIndex);
    EXPECT_TRUE(negOne.isFallback());

    // Even an index that would be valid against a real file must not crash
    // or fabricate content when the reader itself is unavailable.
    SpellVisualResolution zero = selector.resolve(0);
    EXPECT_EQ(zero.source, SpellVisualSource::NoAffectIndex);
    EXPECT_TRUE(zero.isFallback());
    EXPECT_FALSE(zero.hasSourceStage());
    EXPECT_FALSE(zero.hasTravelStage());
    EXPECT_FALSE(zero.hasTargetStage());
}

// B606 (#189) deleted toString(SpellVisualSource) (zero callers) with its
// names-only test; the enum itself is live (isFallback()/the stage checks
// above exercise it through the resolver).

// ============================================================================
// File-backed tests: real spells.eff loaded, no DB dependency.
// ============================================================================

class SpellVisualSelectorFileTest : public ::testing::Test {
protected:
    SpellsEffReader reader;
    bool loaded = false;

    void SetUp() override {
        std::string path = getSpellsEffPath();
        if (!std::filesystem::exists(path)) {
            GTEST_SKIP() << "spells.eff not found at: " << path
                         << " (set EQ_CLIENT_PATH to override) -- SKIPPING, not passing.";
        }
        loaded = reader.load(path);
        ASSERT_TRUE(loaded) << "spells.eff present but failed to load: " << reader.loadError();
    }
};

// -1 is SpellAffectIndex's own "no visual" marker -- must resolve to
// NoAffectIndex even though the reader itself is loaded and healthy.
TEST_F(SpellVisualSelectorFileTest, NegativeOneResolvesToNoAffectIndex) {
    SpellVisualSelector selector(&reader);
    SpellVisualResolution res = selector.resolve(-1);
    EXPECT_EQ(res.source, SpellVisualSource::NoAffectIndex);
    EXPECT_TRUE(res.isFallback());
}

// Record 0 is T70's known fixture: Source = [GENC10_SPRITE, GENC30_SPRITE,
// GENC10_SPRITE, "Source"], Middle = [GENC30_SPRITE, "", "", ""], Target =
// [GENC20_SPRITE, GENC10_SPRITE, GENG00_SPRITE, "Target"]. Asserted exactly,
// including that GENC10_SPRITE resolves to a real texture. T409 (board
// B162) corrected the reader's digit rule to the 0-based one T389 measured
// against the official client, so the two texture assertions here MOVED:
// GENC10_SPRITE is spelc2.tga, not spelc1.tga, and GENG00_SPRITE -- the
// "00"-digit form T70 believed never resolved -- is spelg1.tga, a file that
// does exist. Both were re-checked against
// /mnt/projects/EverQuestP1999/SpellEffects/.
TEST_F(SpellVisualSelectorFileTest, Record0ResolvesWithExactStagingAndTextures) {
    SpellVisualSelector selector(&reader);
    SpellVisualResolution res = selector.resolve(0);

    ASSERT_EQ(res.source, SpellVisualSource::Resolved);
    EXPECT_FALSE(res.isFallback());

    EXPECT_TRUE(res.hasSourceStage());
    EXPECT_TRUE(res.hasTravelStage());
    EXPECT_TRUE(res.hasTargetStage());

    EXPECT_EQ(res.source_stage.sprite_names[0], "GENC10_SPRITE");
    EXPECT_EQ(res.source_stage.texture_files[0], "spelc2.tga");
    EXPECT_EQ(res.source_stage.attach_tag, "Source");

    EXPECT_EQ(res.middle_stage.sprite_names[0], "GENC30_SPRITE");
    EXPECT_EQ(res.middle_stage.sprite_names[1], "");
    EXPECT_EQ(res.middle_stage.texture_files[1], "");  // empty slot, no name to map

    EXPECT_EQ(res.target_stage.sprite_names[2], "GENG00_SPRITE");
    EXPECT_EQ(res.target_stage.texture_files[2], "spelg1.tga")
        << "the '00'-digit form DOES resolve under the 0-based rule T389 "
           "measured (T409, board B162): GENG00 -> spelg1.tga, which exists";
    EXPECT_EQ(res.target_stage.attach_tag, "Target");
}

// Record 88 is byte-identical to record 255 past the header -- the T70
// Review's correction of the plan text ("record 88 has content" is false).
// Both are empty padding; the selector must classify this as EmptyRecord,
// not Resolved, and must not report any stage as present.
TEST_F(SpellVisualSelectorFileTest, Record88IsEmptyRecordNotResolved) {
    SpellVisualSelector selector(&reader);
    SpellVisualResolution res = selector.resolve(88);

    EXPECT_EQ(res.source, SpellVisualSource::EmptyRecord);
    EXPECT_TRUE(res.isFallback());
    EXPECT_FALSE(res.hasSourceStage());
    EXPECT_FALSE(res.hasTravelStage());
    EXPECT_FALSE(res.hasTargetStage());
}

// Out-of-range indices must degrade to fallback, never crash or fabricate.
TEST_F(SpellVisualSelectorFileTest, OutOfRangeIndexIsFallback) {
    SpellVisualSelector selector(&reader);
    SpellVisualResolution res = selector.resolve(9999);
    EXPECT_TRUE(res.isFallback());
    EXPECT_EQ(res.source, SpellVisualSource::NoAffectIndex);
}

// ============================================================================
// DB-backed census: the ~946-spell fallback population T70/T72 asked to be
// designed for first. Scans the whole shipped database the same way
// test_spells_eff_reader.cpp's EveryReferencedIndexResolvesToARecordSlot
// does, and classifies every spell into exactly one of the three sources.
// ============================================================================

class SpellVisualSelectorCensusTest : public ::testing::Test {
protected:
    static EQT::WilleqDatabase* db;
    static bool db_available;
    static SpellsEffReader* eff_reader;
    static bool eff_available;

    SpellDatabase spellDb;

    static void SetUpTestSuite() {
        std::string db_path = getWilleqDbPath();
        if (std::filesystem::exists(db_path)) {
            db = new EQT::WilleqDatabase();
            if (db->open(db_path)) {
                EQT::WilleqDatabase::setGlobalInstance(db);
                db_available = true;
            }
        }

        std::string eff_path = getSpellsEffPath();
        if (std::filesystem::exists(eff_path)) {
            eff_reader = new SpellsEffReader();
            eff_available = eff_reader->load(eff_path);
        }
    }

    static void TearDownTestSuite() {
        delete db;
        db = nullptr;
        delete eff_reader;
        eff_reader = nullptr;
    }

    void SetUp() override {
        if (!db_available) {
            GTEST_SKIP() << "willeq.db not found at " << getWilleqDbPath() << ", skipping";
        }
        if (!eff_available) {
            GTEST_SKIP() << "spells.eff not found/loadable at " << getSpellsEffPath()
                         << ", skipping";
        }
        ASSERT_TRUE(spellDb.initFromDatabase()) << spellDb.getLoadError();
    }
};

EQT::WilleqDatabase* SpellVisualSelectorCensusTest::db = nullptr;
bool SpellVisualSelectorCensusTest::db_available = false;
SpellsEffReader* SpellVisualSelectorCensusTest::eff_reader = nullptr;
bool SpellVisualSelectorCensusTest::eff_available = false;

// The headline count this unit's plan asked for: how many of the shipped
// spells resolve to a real spells.eff record versus fall back (either
// no-index or empty-padding-record). T70 established the fallback
// population at ~946 (798 no-index + 148 across 5 empty-padding indices);
// this test re-derives it independently through the selection layer rather
// than trusting that arithmetic, and pins it so a future spells.eff/DB
// change is caught either way.
TEST_F(SpellVisualSelectorCensusTest, ClassifiesEverySpellIntoExactlyOneSource) {
    SpellVisualSelector selector(eff_reader);

    size_t noIndex = 0, emptyRecord = 0, resolved = 0;
    for (uint32_t id = 1; id <= 9000; ++id) {
        const SpellData* spell = spellDb.getSpell(id);
        if (!spell) continue;

        SpellVisualResolution res = selector.resolve(spell->spell_affect_index);
        switch (res.source) {
            case SpellVisualSource::NoAffectIndex: ++noIndex; break;
            case SpellVisualSource::EmptyRecord:   ++emptyRecord; break;
            case SpellVisualSource::Resolved:       ++resolved; break;
        }

        // isFallback() must agree with the source classification -- this is
        // the "distinguishable, not silently ordinary" property the plan
        // requires of the fallback path.
        EXPECT_EQ(res.isFallback(), res.source != SpellVisualSource::Resolved);
    }

    std::cout << "[SpellVisualSelector census] NoAffectIndex=" << noIndex
              << " EmptyRecord=" << emptyRecord << " Resolved=" << resolved
              << " total=" << (noIndex + emptyRecord + resolved) << std::endl;

    // T70's numbers: 798 spells with spell_affect_index == -1, and 148 more
    // across the 5 empty-padding indices (70, 71, 80, 84, 88) -- pinned as
    // lower bounds (not exact equality) since willeq.db content is allowed
    // to grow; a regression that pushes real numbers far below these would
    // mean the fallback classification broke, which is the thing the plan
    // says must not happen.
    EXPECT_GE(noIndex, 798u);
    EXPECT_GE(emptyRecord, 148u);
    EXPECT_GT(resolved, 0u);
}

// Spot-checks two of T70's five known empty-padding indices end-to-end
// through the selection layer (not just the reader directly, as
// test_spells_eff_reader.cpp already covers).
TEST_F(SpellVisualSelectorCensusTest, KnownEmptyPaddingIndexResolvesAsEmptyRecord) {
    SpellVisualSelector selector(eff_reader);

    // Index 71 -- "Flame Jet" is the sole spell referencing it (T70 Review).
    SpellVisualResolution res71 = selector.resolve(71);
    EXPECT_EQ(res71.source, SpellVisualSource::EmptyRecord);

    // Index 84 -- the elemental Blade line (Heated/Burning/Blazing/
    // Flaming/Inferno Blade).
    SpellVisualResolution res84 = selector.resolve(84);
    EXPECT_EQ(res84.source, SpellVisualSource::EmptyRecord);
}

// ============================================================================
// B1190 (#509): the legacy 0x49ab78 select rule -- pure logic, no file.
// ============================================================================

TEST(LegacySelectRuleTest, MinLevelPerSlot) {
    EXPECT_EQ(legacySlotMinLevel(0), 10);
    EXPECT_EQ(legacySlotMinLevel(1), 24);
    EXPECT_EQ(legacySlotMinLevel(2), 39);
}

TEST(LegacySelectRuleTest, MinusOneAndTypes67NeverDraw) {
    // -1 is "no slot" (the != -1 guard); 6/7 are parsed but skipped by the
    // type dispatch. None of these draw at any level.
    EXPECT_TRUE(legacyStageDrawnSlots(0, 99, {6, 7, -1}).empty());
    EXPECT_TRUE(legacyStageDrawnSlots(0, 5, {-1, -1, -1}).empty());
    // a drawable type in the middle still draws, the -1 neighbours do not
    auto mid = legacyStageDrawnSlots(0, 99, {-1, 2, -1});
    ASSERT_EQ(mid.size(), 1u);
    EXPECT_EQ(mid[0].slot, 1);
    EXPECT_EQ(mid[0].type, 2);
}

TEST(LegacySelectRuleTest, LevelGateIsPerSlot) {
    const std::array<int32_t, 3> all2 = {2, 2, 2};  // all three slots drawable
    EXPECT_TRUE(legacyStageDrawnSlots(0, 5, all2).empty());          // <10: none
    auto l10 = legacyStageDrawnSlots(0, 10, all2);                   // slot 0 in
    ASSERT_EQ(l10.size(), 1u);
    EXPECT_EQ(l10[0].slot, 0);
    auto l24 = legacyStageDrawnSlots(0, 24, all2);                   // slots 0,1 in
    ASSERT_EQ(l24.size(), 2u);
    EXPECT_EQ(l24[0].slot, 0);
    EXPECT_EQ(l24[1].slot, 1);
    EXPECT_EQ(legacyStageDrawnSlots(0, 39, all2).size(), 3u);        // all in
    EXPECT_EQ(legacyStageDrawnSlots(0, 60, all2).size(), 3u);        // Testcaster
}

// record 31 (8255 Enraged Shout) Source slots [4,2,4] -- measured from the
// real spells.eff (review/B1190). At level 60 all three draw.
TEST(LegacySelectRuleTest, EnragedShoutSourceLevel60) {
    auto s = legacyStageDrawnSlots(0, 60, {4, 2, 4});
    ASSERT_EQ(s.size(), 3u);
    EXPECT_EQ(s[0].slot, 0);
    EXPECT_EQ(s[0].type, 4);
    EXPECT_EQ(s[1].slot, 1);
    EXPECT_EQ(s[1].type, 2);
    EXPECT_EQ(s[2].slot, 2);
    EXPECT_EQ(s[2].type, 4);
}

// record 5 (8240 Appraisal) Target slots [1,1,2] -- measured. At level 60 all draw.
TEST(LegacySelectRuleTest, AppraisalTargetLevel60) {
    auto t = legacyStageDrawnSlots(2, 60, {1, 1, 2});
    ASSERT_EQ(t.size(), 3u);
    EXPECT_EQ(t[0].type, 1);
    EXPECT_EQ(t[1].type, 1);
    EXPECT_EQ(t[2].type, 2);
}

// ============================================================================
// B1190 (#509): the record decode -- the per-slot effect types at block
// offset 0x8C, pinned against the real spells.eff (gated on its presence).
// ============================================================================

TEST_F(SpellVisualSelectorFileTest, LegacyRecordEffectTypesAt0x8C) {
    // record 5 (Appraisal): Source [2,2,-1], Target [1,1,2]
    const SpellsEffRecord* r5 = reader.getRecord(5);
    ASSERT_NE(r5, nullptr);
    const auto& s5 = r5->emitter(SpellsEffEmitterRole::Source);
    EXPECT_EQ(s5.effectType(0), 2);
    EXPECT_EQ(s5.effectType(1), 2);
    EXPECT_EQ(s5.effectType(2), -1);
    const auto& t5 = r5->emitter(SpellsEffEmitterRole::Target);
    EXPECT_EQ(t5.effectType(0), 1);
    EXPECT_EQ(t5.effectType(1), 1);
    EXPECT_EQ(t5.effectType(2), 2);

    // record 31 (Enraged Shout): Source [4,2,4], Target [5,5,2]
    const SpellsEffRecord* r31 = reader.getRecord(31);
    ASSERT_NE(r31, nullptr);
    const auto& s31 = r31->emitter(SpellsEffEmitterRole::Source);
    EXPECT_EQ(s31.effectType(0), 4);
    EXPECT_EQ(s31.effectType(1), 2);
    EXPECT_EQ(s31.effectType(2), 4);
    const auto& t31 = r31->emitter(SpellsEffEmitterRole::Target);
    EXPECT_EQ(t31.effectType(0), 5);
    EXPECT_EQ(t31.effectType(1), 5);
    EXPECT_EQ(t31.effectType(2), 2);
}

TEST_F(SpellVisualSelectorFileTest, PaddingRecordHasNoDrawableContent) {
    // record 255 is T70's known padding record: no slots draw.
    const SpellsEffRecord* r255 = reader.getRecord(255);
    ASSERT_NE(r255, nullptr);
    // Every slot in every emitter is -1 (no drawable 0..5 type).
    for (int em = 0; em < 3; ++em)
        for (int s = 0; s < 3; ++s)
            EXPECT_LT(r255->emitter(static_cast<SpellsEffEmitterRole>(em)).effectType(s), 0);
}
