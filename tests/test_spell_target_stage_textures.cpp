/*
 * T389 Leg B -- the spells.eff sprite name -> particle-atlas tile mapping,
 * and the routing of a resolved Target stage into an impact effect's
 * EmitterConfig::textureRegions[].
 *
 * The two name entries under test were MEASURED against the official
 * Titanium client, not argued (review/T389/, board B162):
 * GEN<L><d>0_SPRITE resolves to spel<l><d+1>.tga -- the digit is 0-BASED.
 * Frost Bolt (54) record 23's Target stage is GEND10_SPRITE +
 * GENU10_SPRITE x2, i.e. speld2.tga (cyan eight-pointed star, bright core,
 * soft glow) and spelu2.tga (small blue diagonal cross with a bright core).
 *
 * Pure logic: no Irrlicht, no renderer, no spells.eff file -- the sprite
 * names are the measured literals.
 */

#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "client/graphics/environment/particle_types.h"
#include "client/graphics/environment/spell_effects_config.h"
#include "client/graphics/environment/spell_particle_types.h"

using namespace EQT::Graphics::Environment;

namespace {

// Frost Bolt (54), spells.eff record 23, Target stage -- the three raw slots
// exactly as SpellsEffReader reads them.
const std::vector<std::string> kFrostBoltTargetStage = {
    "GEND10_SPRITE", "GENU10_SPRITE", "GENU10_SPRITE"};

SpellEffectDef oneEmitterDef() {
    SpellEffectDef def;
    def.name = "TestImpact";
    SpellEmitterDef ed;
    ed.config.textureRegionCount = 1;
    ed.config.textureRegions[0] = ParticleAtlas::SoftCircle;
    def.emitters.push_back(ed);
    return def;
}

}  // namespace

// ---------------------------------------------------------------------------
// The name -> tile map (red before T389 added the two entries: an unknown
// name falls back to SoftCircle).
// ---------------------------------------------------------------------------

TEST(SpellTargetStageTextures, GenD10MapsToTheEightPointedStarTile) {
    EXPECT_EQ(SpellEffectsConfig::instance().getTextureIndex("GEND10_SPRITE"),
              ParticleAtlas::SpellSparkle);
}

TEST(SpellTargetStageTextures, GenU10MapsToTheAngularBlueTile) {
    EXPECT_EQ(SpellEffectsConfig::instance().getTextureIndex("GENU10_SPRITE"),
              ParticleAtlas::IceCrystal);
}

TEST(SpellTargetStageTextures, TheTwoMeasuredNamesDoNotShareATile) {
    EXPECT_NE(SpellEffectsConfig::instance().getTextureIndex("GEND10_SPRITE"),
              SpellEffectsConfig::instance().getTextureIndex("GENU10_SPRITE"));
}

TEST(SpellTargetStageTextures, AnUnmeasuredNameStillFallsBackToSoftCircle) {
    // Deliberately NOT mapped: T389 measured two names and added two, and an
    // unmapped name must degrade exactly as it did before.
    EXPECT_EQ(SpellEffectsConfig::instance().getTextureIndex("GENZ90_SPRITE"),
              ParticleAtlas::SoftCircle);
}

// ---------------------------------------------------------------------------
// Routing a resolved Target stage into the emitter.
// ---------------------------------------------------------------------------

TEST(SpellTargetStageTextures, FrostBoltTargetStageFillsThreeTextureRegions) {
    SpellEffectDef def = oneEmitterDef();
    SpellPresets::applyStageSpriteNames(def, kFrostBoltTargetStage, 54);

    ASSERT_EQ(def.emitters.size(), 1u);
    const auto& cfg = def.emitters[0].config;
    EXPECT_EQ(cfg.textureRegionCount, 3);
    EXPECT_EQ(cfg.textureRegions[0], ParticleAtlas::SpellSparkle);
    EXPECT_EQ(cfg.textureRegions[1], ParticleAtlas::IceCrystal);
    EXPECT_EQ(cfg.textureRegions[2], ParticleAtlas::IceCrystal);
}

TEST(SpellTargetStageTextures, FrostBoltTargetStageChangesTheStyleDefault) {
    SpellEffectDef def = oneEmitterDef();
    const uint8_t before = def.emitters[0].config.textureRegions[0];
    SpellPresets::applyStageSpriteNames(def, kFrostBoltTargetStage, 54);
    EXPECT_NE(def.emitters[0].config.textureRegions[0], before);
}

TEST(SpellTargetStageTextures, EmptySlotsAreSkippedNotCounted) {
    SpellEffectDef def = oneEmitterDef();
    SpellPresets::applyStageSpriteNames(def, {"GEND10_SPRITE", "", ""}, 54);
    EXPECT_EQ(def.emitters[0].config.textureRegionCount, 1);
    EXPECT_EQ(def.emitters[0].config.textureRegions[0], ParticleAtlas::SpellSparkle);
}

TEST(SpellTargetStageTextures, AnAllEmptyStageLeavesTheDefinitionUntouched) {
    SpellEffectDef def = oneEmitterDef();
    SpellPresets::applyStageSpriteNames(def, {"", "", ""}, 54);
    EXPECT_EQ(def.emitters[0].config.textureRegionCount, 1);
    EXPECT_EQ(def.emitters[0].config.textureRegions[0], ParticleAtlas::SoftCircle);
}

// ===========================================================================
// T409 Leg B -- the names the corrected 0-based reader newly resolves
// (board B162).
//
// SpellsEffReader::effectNameToTexture() used to reject the "00"-digit form
// outright, so the COMMONEST names in spells.eff resolved to nothing. With
// the 0-based rule they resolve, and each one now has a real sprite whose
// shape can be compared against this atlas's sixteen tiles -- the same
// tile-vs-crop comparison T389 used for GEND10/GENU10, on the single frame
// each 4x4 sheet repeats (review/T409/sprites/).
//
// Only the sprites that fall UNAMBIGUOUSLY into a shape class this atlas
// actually has are mapped. The rest deliberately keep the SoftCircle
// fallback, and the reason is on the board (B300): several resolved sprites
// are not abstract particles at all -- spelg1.tga is a SWORD, speli1.tga a
// flaming SKULL, spelk2.tga a row of skulls -- and no 16x16 procedural tile
// can stand in for them.
// ===========================================================================

TEST(SpellSpriteTilesT409, TheGreenBurstNamesMapToTheGreenStarTile) {
    // spelb1.tga: a green/yellow multi-point burst. StarShape is the only
    // green pointed-star tile in the atlas.
    EXPECT_EQ(SpellEffectsConfig::instance().getTextureIndex("GENB00_SPRITE"),
              ParticleAtlas::StarShape);
}

TEST(SpellSpriteTilesT409, TheOrangeSunburstMapsToEmber) {
    // spelc1.tga: an orange radiant sunburst. Ember is the only orange tile.
    EXPECT_EQ(SpellEffectsConfig::instance().getTextureIndex("GENC00_SPRITE"),
              ParticleAtlas::Ember);
}

TEST(SpellSpriteTilesT409, ThePointedStarsWithABrightCoreShareTheSparkleTile) {
    // spela1.tga and spela2.tga are both pointed stars with a bright white
    // core over a soft glow -- the same shape class as speld2.tga, which
    // T389 measured onto SpellSparkle. spela2 is itself oracle-measured:
    // record 35 / spell 253 Whirling Wind drew its eight-pointed stars.
    EXPECT_EQ(SpellEffectsConfig::instance().getTextureIndex("GENA00_SPRITE"),
              ParticleAtlas::SpellSparkle);
    EXPECT_EQ(SpellEffectsConfig::instance().getTextureIndex("GENA10_SPRITE"),
              ParticleAtlas::SpellSparkle);
}

TEST(SpellSpriteTilesT409, TheMottledSphereMapsToTheMottledDiscTile) {
    // speln1.tga: the green/orange MOTTLED sphere the oracle drew for
    // record 20 / spell 41 Weaken. SnowPatch is the atlas's only mottled
    // disc; every other round tile is a smooth gradient.
    EXPECT_EQ(SpellEffectsConfig::instance().getTextureIndex("GENN00_SPRITE"),
              ParticleAtlas::SnowPatch);
}

TEST(SpellSpriteTilesT409, TheRibbonSwirlsMapToTheWispTile) {
    // spelu1.tga (the purple/blue ribbon swirl T389 saw at Frost Bolt's
    // caster) and spelj1.tga (a pale-blue ribbon swirl) are elongated
    // curved strokes; SmokeWisp is the atlas's only elongated tile.
    EXPECT_EQ(SpellEffectsConfig::instance().getTextureIndex("GENU00_SPRITE"),
              ParticleAtlas::SmokeWisp);
    EXPECT_EQ(SpellEffectsConfig::instance().getTextureIndex("GENJ00_SPRITE"),
              ParticleAtlas::SmokeWisp);
}

TEST(SpellSpriteTilesT409, TheSmokeBallMapsToWispyCloud) {
    // spelz1.tga: a white/grey swirling smoke ball.
    EXPECT_EQ(SpellEffectsConfig::instance().getTextureIndex("GENZ00_SPRITE"),
              ParticleAtlas::WispyCloud);
}

TEST(SpellSpriteTilesT409, TheGreenSphereMapsToTheGreenDisc) {
    // spele1.tga: a green/teal sphere. Both spells.eff spellings of it
    // (GENE00 and the one-off GENE01) must land on the same tile.
    EXPECT_EQ(SpellEffectsConfig::instance().getTextureIndex("GENE00_SPRITE"),
              ParticleAtlas::LeafShape);
    EXPECT_EQ(SpellEffectsConfig::instance().getTextureIndex("GENE01_SPRITE"),
              ParticleAtlas::LeafShape);
}

// The deliberate non-mappings. These names DO resolve to a real file now,
// but the file is a model texture or a figurative sprite with no tile
// counterpart, so they must keep the fallback rather than be given a
// misleading shape (board B300).
TEST(SpellSpriteTilesT409, TheFigurativeSpritesKeepTheFallback) {
    for (const char* name : {"GENG00_SPRITE",   // spelg1.tga -- a SWORD
                             "GENI00_SPRITE",   // speli1.tga -- a flaming SKULL
                             "GENK10_SPRITE",   // spelk2.tga -- a row of skulls
                             "GENH00_SPRITE"}) {  // spelh1.tga -- a lightning-wreathed sphere
        EXPECT_EQ(SpellEffectsConfig::instance().getTextureIndex(name),
                  ParticleAtlas::SoftCircle) << name;
    }
}

// A stage whose three slots are the commonest name in the whole file must
// route that one tile into all three regions -- the shape B162's census is
// really about (GENI00 appears 32 times, GENN00 15, GENG00 12).
TEST(SpellSpriteTilesT409, AWeakenStageRoutesTheMottledDiscIntoEveryRegion) {
    SpellEffectDef def = oneEmitterDef();
    SpellPresets::applyStageSpriteNames(
        def, {"GENN00_SPRITE", "GENN00_SPRITE", "GENN00_SPRITE"}, 41);
    ASSERT_EQ(def.emitters.size(), 1u);
    const auto& cfg = def.emitters[0].config;
    EXPECT_EQ(cfg.textureRegionCount, 3);
    for (int i = 0; i < 3; ++i) {
        EXPECT_EQ(cfg.textureRegions[i], ParticleAtlas::SnowPatch) << i;
    }
}
