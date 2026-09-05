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
