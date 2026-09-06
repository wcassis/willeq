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
#include "client/graphics/environment/spell_sprite_atlas.h"

using namespace EQT::Graphics::Environment;

namespace {

// Frost Bolt (54), spells.eff record 23, Target stage -- the three raw slots
// exactly as SpellsEffReader reads them.
const std::vector<std::string> kFrostBoltTargetStage = {
    "GEND10_SPRITE", "GENU10_SPRITE", "GENU10_SPRITE"};

// T417: the flat ordinal a resolved sprite file occupies.
uint8_t spriteIndex(const char* file) {
    const int slot = SpellSpriteAtlas::slotForFile(file);
    EXPECT_GE(slot, 0) << file;
    return static_cast<uint8_t>(SpellSpriteAtlas::kBase + (slot < 0 ? 0 : slot));
}

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

// T417 (board B300) REPLACED the tile approximations these two cases used to
// assert. GEND10/GENU10 are the two ORACLE-MEASURED names, and they now
// resolve to the sprite files the oracle actually drew, not to the nearest
// procedural tile.
TEST(SpellTargetStageTextures, GenD10MapsToItsOwnRealSprite) {
    EXPECT_EQ(SpellEffectsConfig::instance().getTextureIndex("GEND10_SPRITE"),
              spriteIndex("speld2.tga"));
}

TEST(SpellTargetStageTextures, GenU10MapsToItsOwnRealSprite) {
    EXPECT_EQ(SpellEffectsConfig::instance().getTextureIndex("GENU10_SPRITE"),
              spriteIndex("spelu2.tga"));
}

// The point of the whole unit: neither of the two measured names is a
// procedural tile any more.
TEST(SpellTargetStageTextures, TheTwoMeasuredNamesAreNotProceduralTiles) {
    EXPECT_GE(SpellEffectsConfig::instance().getTextureIndex("GEND10_SPRITE"),
              SpellSpriteAtlas::kBase);
    EXPECT_GE(SpellEffectsConfig::instance().getTextureIndex("GENU10_SPRITE"),
              SpellSpriteAtlas::kBase);
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
    EXPECT_EQ(cfg.textureRegions[0], spriteIndex("speld2.tga"));
    EXPECT_EQ(cfg.textureRegions[1], spriteIndex("spelu2.tga"));
    EXPECT_EQ(cfg.textureRegions[2], spriteIndex("spelu2.tga"));
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
    EXPECT_EQ(def.emitters[0].config.textureRegions[0], spriteIndex("speld2.tga"));
}

TEST(SpellTargetStageTextures, AnAllEmptyStageLeavesTheDefinitionUntouched) {
    SpellEffectDef def = oneEmitterDef();
    SpellPresets::applyStageSpriteNames(def, {"", "", ""}, 54);
    EXPECT_EQ(def.emitters[0].config.textureRegionCount, 1);
    EXPECT_EQ(def.emitters[0].config.textureRegions[0], ParticleAtlas::SoftCircle);
}

// ===========================================================================
// T417 -- the REAL sprites (board B300, Will's ruling 2026-09-06: "as close
// to the original as possible").
//
// T389 and T409 mapped twelve spells.eff names onto the nearest of the
// sixteen PROCEDURAL tiles and stopped, because four of the sprites are
// figurative and no 16x16 tile stands in for them honestly. Those twelve
// approximation rows are gone. Every GEN name that the 0-based rule resolves
// to a shipped file now addresses that file's own cell on the spell sprite
// sheet, and the resolution goes through the reader's ONE implementation of
// the rule -- there is no second name table to drift.
//
// These cases are RED before the change: every name below returned a
// ParticleAtlas tile ordinal (0..15), all of which are below
// SpellSpriteAtlas::kBase.
// ===========================================================================

TEST(SpellSpriteRealArt, TheFigurativeSpritesNowDrawThemselves) {
    // The four sprites B300 was opened for. Each was pinned to SoftCircle by
    // T409's TheFigurativeSpritesKeepTheFallback, which this case replaces.
    struct Row { const char* name; const char* file; const char* what; };
    const Row rows[] = {
        {"GENG00_SPRITE", "spelg1.tga", "a SWORD"},
        {"GENI00_SPRITE", "speli1.tga", "a flaming SKULL"},
        {"GENK10_SPRITE", "spelk2.tga", "a row of skulls"},
        {"GENH00_SPRITE", "spelh1.tga", "a lightning-wreathed sphere"},
    };
    for (const auto& r : rows) {
        EXPECT_EQ(SpellEffectsConfig::instance().getTextureIndex(r.name),
                  spriteIndex(r.file)) << r.name << " (" << r.what << ")";
    }
}

TEST(SpellSpriteRealArt, TheCommonestNameInTheFileIsTheSkullSprite) {
    // GENI00_SPRITE occupies 32 of spells.eff's 301 populated slots -- more
    // than any other name -- and it is the skull. That it drew a soft white
    // dot was the whole of B300.
    EXPECT_EQ(SpellEffectsConfig::instance().getTextureIndex("GENI00_SPRITE"),
              spriteIndex("speli1.tga"));
    EXPECT_GE(SpellEffectsConfig::instance().getTextureIndex("GENI00_SPRITE"),
              SpellSpriteAtlas::kBase);
}

TEST(SpellSpriteRealArt, EveryNameT409ApproximatedNowResolvesToItsOwnFile) {
    struct Row { const char* name; const char* file; };
    const Row rows[] = {
        {"GENA00_SPRITE", "spela1.tga"}, {"GENA10_SPRITE", "spela2.tga"},
        {"GENB00_SPRITE", "spelb1.tga"}, {"GENC00_SPRITE", "spelc1.tga"},
        {"GENE00_SPRITE", "spele1.tga"}, {"GENE01_SPRITE", "spele1.tga"},
        {"GENJ00_SPRITE", "spelj1.tga"}, {"GENN00_SPRITE", "speln1.tga"},
        {"GENU00_SPRITE", "spelu1.tga"}, {"GENZ00_SPRITE", "spelz1.tga"},
    };
    for (const auto& r : rows) {
        EXPECT_EQ(SpellEffectsConfig::instance().getTextureIndex(r.name),
                  spriteIndex(r.file)) << r.name;
    }
}

TEST(SpellSpriteRealArt, DistinctFilesGetDistinctCells) {
    // spela1 and spela2 shared SpellSparkle under T409; spelu1 and spelj1
    // shared SmokeWisp. Distinct art must now be distinct on screen.
    auto& cfg = SpellEffectsConfig::instance();
    EXPECT_NE(cfg.getTextureIndex("GENA00_SPRITE"),
              cfg.getTextureIndex("GENA10_SPRITE"));
    EXPECT_NE(cfg.getTextureIndex("GENU00_SPRITE"),
              cfg.getTextureIndex("GENJ00_SPRITE"));
}

TEST(SpellSpriteRealArt, TheTwoSpellingsOfSpelE1ShareOneCell) {
    // GENE00 and the one-off GENE01 name the same file, so they must land on
    // the same cell -- the reader ignores name[5].
    EXPECT_EQ(SpellEffectsConfig::instance().getTextureIndex("GENE00_SPRITE"),
              SpellEffectsConfig::instance().getTextureIndex("GENE01_SPRITE"));
}

TEST(SpellSpriteRealArt, ANameWhoseFileIsNotShippedKeepsTheProceduralFallback) {
    // The 0-based rule resolves these two, but SpellEffects/ ships neither
    // spela3.tga nor spela4.tga. A name that resolves to nothing on disk
    // must degrade, not index a wrong cell.
    EXPECT_EQ(SpellEffectsConfig::instance().getTextureIndex("GENA20_SPRITE"),
              ParticleAtlas::SoftCircle);
    EXPECT_EQ(SpellEffectsConfig::instance().getTextureIndex("GENA30_SPRITE"),
              ParticleAtlas::SoftCircle);
}

TEST(SpellSpriteRealArt, TheFourNonGenNamesKeepAProceduralTile) {
    // These four never matched the GEN rule and have no file at all, so a
    // procedural tile chosen from what the name says is the honest answer.
    auto& cfg = SpellEffectsConfig::instance();
    EXPECT_EQ(cfg.getTextureIndex("I_SNOWFLAKESPRITE"), ParticleAtlas::Snowflake);
    EXPECT_EQ(cfg.getTextureIndex("I_TFIRE_SPB"), ParticleAtlas::Ember);
    EXPECT_EQ(cfg.getTextureIndex("I_TSMOKE_SPB"), ParticleAtlas::SmokeWisp);
    EXPECT_EQ(cfg.getTextureIndex("GEN_Z_SPB"), ParticleAtlas::WispyCloud);
    for (const char* n : {"I_SNOWFLAKESPRITE", "I_TFIRE_SPB", "I_TSMOKE_SPB",
                          "GEN_Z_SPB"}) {
        EXPECT_LT(cfg.getTextureIndex(n), SpellSpriteAtlas::kBase) << n;
    }
}

TEST(SpellSpriteRealArt, TheProceduralShapeNamesAreUnaffected) {
    // config/spell_effects.json addresses tiles by shape name; those rows
    // must still resolve to the procedural atlas, below kBase.
    auto& cfg = SpellEffectsConfig::instance();
    EXPECT_EQ(cfg.getTextureIndex("SoftCircle"), ParticleAtlas::SoftCircle);
    EXPECT_EQ(cfg.getTextureIndex("Ember"), ParticleAtlas::Ember);
    EXPECT_EQ(cfg.getTextureIndex("IceCrystal"), ParticleAtlas::IceCrystal);
    EXPECT_LT(cfg.getTextureIndex("SmokeWisp"), SpellSpriteAtlas::kBase);
}

TEST(SpellSpriteRealArt, AWeakenStageRoutesTheRealSpriteIntoEveryRegion) {
    // Record 20 / spell 41 Weaken: all three slots GENN00_SPRITE, which the
    // oracle drew as speln1.tga's mottled sphere.
    SpellEffectDef def = oneEmitterDef();
    SpellPresets::applyStageSpriteNames(
        def, {"GENN00_SPRITE", "GENN00_SPRITE", "GENN00_SPRITE"}, 41);
    ASSERT_EQ(def.emitters.size(), 1u);
    const auto& cfg = def.emitters[0].config;
    EXPECT_EQ(cfg.textureRegionCount, 3);
    for (int i = 0; i < 3; ++i) {
        EXPECT_EQ(cfg.textureRegions[i], spriteIndex("speln1.tga")) << i;
    }
}

// ---------------------------------------------------------------------------
// The sheet's own layout invariants.
// ---------------------------------------------------------------------------

TEST(SpellSpriteAtlasLayout, EveryCellIndexFitsAUint8AndTheSheet) {
    // T420 (board B355): the sheet holds every sprite's whole animation, so
    // the CELL count is 16x the sprite count -- but the ORDINAL a particle
    // carries still names a sprite, which is the only reason it still fits in
    // the uint8_t EmitterConfig::textureRegions[].
    EXPECT_LE(SpellSpriteAtlas::kUsedCells, SpellSpriteAtlas::kCapacity);
    EXPECT_EQ(SpellSpriteAtlas::kUsedCells,
              SpellSpriteAtlas::kFileCount * SpellSpriteAtlas::kFramesPerSprite);
    EXPECT_LE(SpellSpriteAtlas::kBase + SpellSpriteAtlas::kFileCount, 256);
    EXPECT_GT(SpellSpriteAtlas::kBase, ParticleAtlas::TileCount);
}

TEST(SpellSpriteAtlasLayout, TheFileTableIsSortedAndUnique) {
    for (int i = 1; i < SpellSpriteAtlas::kFileCount; ++i) {
        EXPECT_LT(std::string(SpellSpriteAtlas::kFiles[i - 1]),
                  std::string(SpellSpriteAtlas::kFiles[i]))
            << "cell " << i;
    }
}

TEST(SpellSpriteAtlasLayout, CellOriginsAreOnTheCellPitch) {
    for (int cell = 0; cell < SpellSpriteAtlas::kUsedCells; ++cell) {
        int x = 0, y = 0;
        SpellSpriteAtlas::cellOrigin(cell, x, y);
        EXPECT_EQ(x % SpellSpriteAtlas::kCellSize, 0) << cell;
        EXPECT_EQ(y % SpellSpriteAtlas::kCellSize, 0) << cell;
        EXPECT_LT(x, SpellSpriteAtlas::kWidth);
        EXPECT_LT(y, SpellSpriteAtlas::kHeight);
        // Every cell edge is also an ETC1 4x4 block edge, so the pack cannot
        // bleed one sprite frame into the next.
        EXPECT_EQ(x % 4, 0) << cell;
        EXPECT_EQ(y % 4, 0) << cell;
    }
}

// ---------------------------------------------------------------------------
// T420 (board B355) -- the animation's addressing, as pure arithmetic.
// ---------------------------------------------------------------------------

TEST(SpellSpriteAnimation, EveryFrameOfEverySpriteHasItsOwnCell) {
    std::vector<bool> seen(SpellSpriteAtlas::kCapacity, false);
    for (int sprite = 0; sprite < SpellSpriteAtlas::kFileCount; ++sprite) {
        for (int f = 0; f < SpellSpriteAtlas::kFramesPerSprite; ++f) {
            const int cell = SpellSpriteAtlas::cellFor(sprite, f);
            ASSERT_GE(cell, 0);
            ASSERT_LT(cell, SpellSpriteAtlas::kUsedCells)
                << SpellSpriteAtlas::kFiles[sprite] << " frame " << f;
            ASSERT_FALSE(seen[cell]) << "cell " << cell << " is claimed twice";
            seen[cell] = true;
        }
    }
}

TEST(SpellSpriteAnimation, ASpriteAnimationIsContiguousAndOnOneRow) {
    // Sixteen frames, 32 columns: a sprite occupies exactly half a row, so a
    // contact sheet reads each animation left to right without wrapping.
    for (int sprite = 0; sprite < SpellSpriteAtlas::kFileCount; ++sprite) {
        int x0 = 0, y0 = 0, x15 = 0, y15 = 0;
        SpellSpriteAtlas::cellOrigin(SpellSpriteAtlas::cellFor(sprite, 0), x0, y0);
        SpellSpriteAtlas::cellOrigin(
            SpellSpriteAtlas::cellFor(sprite, SpellSpriteAtlas::kFramesPerSprite - 1),
            x15, y15);
        EXPECT_EQ(y0, y15) << SpellSpriteAtlas::kFiles[sprite]
                           << " straddles a row boundary";
        EXPECT_EQ(x15 - x0,
                  (SpellSpriteAtlas::kFramesPerSprite - 1) * SpellSpriteAtlas::kCellSize)
            << SpellSpriteAtlas::kFiles[sprite] << " is not contiguous";
    }
}

TEST(SpellSpriteAnimation, TheSheetPlaysThroughExactlyOnceOverTheLifetime) {
    // The plan's assertion, as arithmetic: birth is frame 0, half life the
    // middle frame, the end the last frame.
    EXPECT_EQ(SpellSpriteAtlas::frameForNormalizedAge(0.0f), 0);
    EXPECT_EQ(SpellSpriteAtlas::frameForNormalizedAge(0.5f),
              SpellSpriteAtlas::kFramesPerSprite / 2);
    EXPECT_EQ(SpellSpriteAtlas::frameForNormalizedAge(0.999f),
              SpellSpriteAtlas::kFramesPerSprite - 1);

    // Every frame gets an equal, non-empty slice of the life, in order.
    int previous = -1;
    for (int f = 0; f < SpellSpriteAtlas::kFramesPerSprite; ++f) {
        const float mid =
            (static_cast<float>(f) + 0.5f) / SpellSpriteAtlas::kFramesPerSprite;
        const int got = SpellSpriteAtlas::frameForNormalizedAge(mid);
        EXPECT_EQ(got, f) << "normalized age " << mid;
        EXPECT_GT(got, previous);
        previous = got;
    }
}

TEST(SpellSpriteAnimation, AgeOutsideTheLifetimeClampsRatherThanWraps) {
    // Below zero holds the first frame; at or past the end holds the LAST.
    // Wrapping would make the seven blank-frame-0 sprites blink out.
    EXPECT_EQ(SpellSpriteAtlas::frameForNormalizedAge(-1.0f), 0);
    EXPECT_EQ(SpellSpriteAtlas::frameForNormalizedAge(1.0f),
              SpellSpriteAtlas::kFramesPerSprite - 1);
    EXPECT_EQ(SpellSpriteAtlas::frameForNormalizedAge(17.5f),
              SpellSpriteAtlas::kFramesPerSprite - 1);
}

TEST(SpellSpriteAtlasLayout, AnUnknownFileHasNoCell) {
    EXPECT_EQ(SpellSpriteAtlas::slotForFile("spelzz9.tga"), -1);
    EXPECT_EQ(SpellSpriteAtlas::slotForFile(""), -1);
    EXPECT_EQ(SpellSpriteAtlas::slotForFile(nullptr), -1);
}
