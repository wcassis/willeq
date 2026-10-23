// B1137 (#457): a spell is drawn with the measured Source/Target row of the
// record the official client DRAWS -- its spellsnew.eff record (spells_us.txt
// field 145, B1127) -- not of its spells.eff record (field 123).
//
// The B1130 survey captured one spell per field-123 record, and those 40
// spells happen to draw 40 different field-145 records, so every measured row
// is a measurement of ONE field-145 record (review/B1137/README.md sec. 4).
// Keyed by field 123 (22dce2d0), a row generalised to spells that draw a
// different record: 1656 Thunderbold (123=25, 145=85) drew 14 Strike's row
// (145=115) and 1605 Frost (123=23, 145=106) drew 372 Blast of Cold's (145=82);
// the oracle draws them differently (review/B1127/README.md sec. 1). RED on
// 22dce2d0: rowForAnim() and the `animRecords` block do not exist there.

#include <gtest/gtest.h>

#include "client/graphics/environment/spell_effects_config.h"
#include "spell_anim_rows.h"
#include "spell_pool.h"

#include <set>

using EQT::Graphics::Environment::SpellEffectsConfig;

namespace {

class AnimRows : public ::testing::Test {
protected:
    void SetUp() override {
        const int pool = spell_pool::Shipped(spell_pool::ThisBuildPlatform());
        ASSERT_GT(pool, 0);
        if (!spell_pool::LoadAt(pool)) GTEST_SKIP() << "no config/spell_effects.json";
    }
};

// Every captured row is reached through the field-145 record of the spell it
// was captured on, one row per record, and nothing else is.
TEST_F(AnimRows, EveryMeasuredRowIsReachedByTheRecordItWasCapturedOn) {
    const auto& cfg = SpellEffectsConfig::instance();
    // + 93 -> 6, the one decoded equivalence (TheDecodedEquivalence below), and
    // + 328/329/330 -> 51, the three discipline records B1134 measured beside 326.
    EXPECT_EQ(cfg.animRowCount(), std::size(spell_anim_rows::kRowAnim) + 1 + 3)
        << "config/spell_effects.json animRecords.rows";
    std::set<int> rows;
    for (const auto& r : spell_anim_rows::kRowAnim) {
        EXPECT_EQ(cfg.rowForAnim(r.anim), r.row) << "field-145 record " << r.anim;
        EXPECT_TRUE(rows.insert(r.row).second) << "row " << r.row << " named twice";
        EXPECT_EQ(cfg.sourceRecord(r.row).record, r.row) << "row " << r.row << " has no Source row";
        EXPECT_EQ(cfg.targetRecord(r.row).record, r.row) << "row " << r.row << " has no Target row";
    }
}

// The three spells B1127 proved on the oracle: each now takes the row of the
// record it draws, and the two same-field-123 pairs no longer share a row.
TEST_F(AnimRows, TheOracleProvenPairsTakeTheirOwnRecordsRow) {
    const auto& cfg = SpellEffectsConfig::instance();
    EXPECT_EQ(cfg.rowForAnim(85), 31);   // 1656 Thunderbold -> e31 313 Fire Flux (145=85)
    EXPECT_EQ(cfg.rowForAnim(115), 25);  // 14 Strike         -> r142 (145=115)
    EXPECT_EQ(cfg.rowForAnim(106), 32);  // 1605 Frost        -> e32 374 Numbing Cold (145=106)
    EXPECT_EQ(cfg.rowForAnim(82), 23);   // 372 Blast of Cold -> e23 (145=82)
    EXPECT_EQ(cfg.rowForAnim(83), 4);    // 1899 Imbue Fire Opal (123=-1) -> e4 50 Summon Food (145=83)
    EXPECT_NE(cfg.rowForAnim(85), cfg.rowForAnim(115)) << "1656 and 14 share field 123 = 25 only";
    EXPECT_NE(cfg.rowForAnim(106), cfg.rowForAnim(82)) << "1605 and 372 share field 123 = 23 only";
}

// Record 93 (Breeze V2: 174 Clarity, 697 Breeze, 1287 Cassindra`s Chant of
// Clarity -- 16 player spells) was never captured, but it is record 1 byte for
// byte except ONE cell (Middle stage slot 0 attach code, 5 vs 4), and the oracle
// draws 1287 (93) and 7 Hymn of Restoration (1) alike on a self cast
// (review/B1137 leg A: Source peak cov 0.375/0.378, cast centroid 0.57/0.57).
TEST_F(AnimRows, TheDecodedEquivalence) {
    const auto& cfg = SpellEffectsConfig::instance();
    EXPECT_EQ(cfg.rowForAnim(93), cfg.rowForAnim(1));
    EXPECT_EQ(cfg.rowForAnim(93), 6);
}

// A field-145 record no row was captured on draws the default rows: no
// field-123 stand-in (that row was measured on a different field-145 record).
TEST_F(AnimRows, AnUncapturedRecordDrawsTheDefaultRows) {
    const auto& cfg = SpellEffectsConfig::instance();
    for (int anim : {0, -1, 153, 221, 254, 314, 325, 417, 3411}) {
        EXPECT_EQ(cfg.rowForAnim(anim), -1) << anim;
    }
    EXPECT_EQ(cfg.sourceRecord(-1).record, -1);
    EXPECT_EQ(cfg.targetRecord(-1).record, -1);
}

// B1134 (#454): the discipline records. 326 (Blue: 4499 Defensive) is row 51's
// own capture; 328 (Green: 4721 Focused Will), 329 (Red: 4498 Aggressive) and
// 330 (White: 4670 Fortitude) were cast on the same observer rig and drew the
// same shape (RUN h -0.06..0.79, dx -0.32..+0.38, lit to Act+8.0-8.3; review/
// B1134/obs_rows.json). Their spellsnew.eff records differ in the name and the
// colour words (B1137); 325 (Black, shadow knight) also differs in textures
// and particle lifetimes and was not cast, so it stays on the default rows.
TEST_F(AnimRows, TheMeasuredDisciplineRecordsTakeRowFiftyOne) {
    const auto& cfg = SpellEffectsConfig::instance();
    for (int anim : {326, 328, 329, 330}) EXPECT_EQ(cfg.rowForAnim(anim), 51) << anim;
    EXPECT_EQ(cfg.rowForAnim(325), -1);
}

// The pool rule's override rows (sourceRecords.overrideRecords, spell ->
// row) are the rows those spells now draw: their field 145's.
TEST_F(AnimRows, TheOverrideRowsAreTheSpellsFieldOneFortyFiveRows) {
    const auto& cfg = SpellEffectsConfig::instance();
    const struct { uint32_t spell; int anim; } kOverride[] = {
        {38, 80}, {93, 80}, {41, 89}, {54, 82}, {656, 82}, {253, 104}, {458, 121},
    };
    for (const auto& o : kOverride) {
        EXPECT_EQ(cfg.overrideRecord(o.spell), cfg.rowForAnim(o.anim)) << o.spell;
    }
}

}  // namespace

#if defined(EQT_HAS_GRAPHICS) && defined(EQT_HAS_GLES2)

#include <irrlicht.h>

#include "client/graphics/spell_visual_fx.h"

namespace {

// The renderer picks the row from the vocabulary's spellAnim, not from its
// spellAffectIndex (the bridge payload carries both, B1127).
TEST_F(AnimRows, SpellVisualFxKeysTheRowByFieldOneFortyFive) {
    irr::SIrrlichtCreationParameters p;
    p.DriverType = irr::video::EDT_NULL;
    irr::IrrlichtDevice* device = irr::createDeviceEx(p);
    if (!device) GTEST_SKIP() << "Failed to create EDT_NULL Irrlicht device";
    {
        EQ::SpellVisualFX fx(device->getSceneManager(), device->getVideoDriver(), std::string());
        auto note = [&](uint32_t spell, int16_t f123, int16_t f145) {
            EQ::SpellVisualVocabulary v;
            v.known = true;
            v.school = EQ::SpellSchool::Evocation;
            v.spellAffectIndex = f123;
            v.spellAnim = f145;
            fx.noteSpellVocabulary(spell, v);
        };
        note(1656, 25, 85);
        note(14, 25, 115);
        note(1605, 23, 106);
        note(1899, -1, 83);
        note(4499, 51, 326);  // Defensive Discipline: B1134 measured 326 as row 51
        note(8604, 51, 325);  // Leechcurse Discipline: field 123 has a row, field 145 none
        EXPECT_EQ(fx.sourceRecordOf(1656), 31);
        EXPECT_EQ(fx.sourceRecordOf(14), 25);
        EXPECT_EQ(fx.sourceRecordOf(1605), 32);
        EXPECT_EQ(fx.sourceRecordOf(1899), 4);
        EXPECT_EQ(fx.sourceRecordOf(4499), 51);
        EXPECT_EQ(fx.sourceRecordOf(8604), -1);
    }
    device->drop();
}

}  // namespace

#endif
