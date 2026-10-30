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

#include <algorithm>
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

// B1127 (#447): the sheet now carries the art the oracle draws (spellsnew.edd)
// plus the spells.eff files only field-145-less spells reach; a GEN name
// whose file left the sheet (spelh1, spelb1, spelc1, spelu1, spelz1 -- every
// spell naming them has a field-145 record) falls to the procedural circle.
uint8_t expectedIndex(const char* file) {
    const int slot = SpellSpriteAtlas::slotForFile(file);
    return slot < 0 ? static_cast<uint8_t>(ParticleAtlas::SoftCircle)
                    : static_cast<uint8_t>(SpellSpriteAtlas::kBase + slot);
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
                  expectedIndex(r.file)) << r.name << " (" << r.what << ")";
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
                  expectedIndex(r.file)) << r.name;
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

TEST(SpellSpriteAnimation, EveryFrameOfEverySpriteTilesItsBlockRowMajor) {
    // B1188 (#507): a sprite owns one 4x4 CELL BLOCK (128x128 px) of the
    // sheet, and its frames tile that block row-major in the source sheet's
    // own display order -- frame f at block column (f % cols), row (f / cols),
    // each frame occupying its stored-frame sub-block (storedSpan cells on a
    // side). Frames never overlap and never leave the block.
    for (int sprite = 0; sprite < SpellSpriteAtlas::kFileCount; ++sprite) {
        const int frames = SpellSpriteAtlas::framesFor(sprite);
        const int cols = frames == SpellSpriteAtlas::kFramesPerSprite
                             ? SpellSpriteAtlas::kSheetGrid
                             : frames == SpellSpriteAtlas::kTwoByTwo ? 2 : 1;
        const int span = SpellSpriteAtlas::storedSpan(sprite);
        int bx = 0, by = 0;
        SpellSpriteAtlas::cellOrigin(SpellSpriteAtlas::blockBase(sprite), bx, by);
        for (int f = 0; f < frames; ++f) {
            int cx = 0, cy = 0;
            SpellSpriteAtlas::cellOrigin(SpellSpriteAtlas::cellFor(sprite, f), cx, cy);
            const int wantX = bx + (f % cols) * span * SpellSpriteAtlas::kCellSize;
            const int wantY = by + (f / cols) * span * SpellSpriteAtlas::kCellSize;
            EXPECT_EQ(cx, wantX) << SpellSpriteAtlas::kFiles[sprite] << " frame " << f;
            EXPECT_EQ(cy, wantY) << SpellSpriteAtlas::kFiles[sprite] << " frame " << f;
            // The whole stored frame stays inside the 128x128 block (a frame
            // flush with the block's far edge ends exactly at bx + 128).
            EXPECT_LE(cx + span * SpellSpriteAtlas::kCellSize, bx + 4 * SpellSpriteAtlas::kCellSize)
                << SpellSpriteAtlas::kFiles[sprite] << " frame " << f;
            EXPECT_LE(cy + span * SpellSpriteAtlas::kCellSize, by + 4 * SpellSpriteAtlas::kCellSize)
                << SpellSpriteAtlas::kFiles[sprite] << " frame " << f;
        }
    }
}

TEST(SpellSpriteAnimation, TheSpriteBlocksTileTheSheetWithoutOverlap) {
    // B1188: the 60 sprites tile an 8x8 grid of blocks (8 per row); no two
    // blocks share a cell, and the last four blocks stay unused.
    std::vector<bool> seen(SpellSpriteAtlas::kCapacity, false);
    for (int sprite = 0; sprite < SpellSpriteAtlas::kFileCount; ++sprite) {
        int bx = 0, by = 0;
        SpellSpriteAtlas::cellOrigin(SpellSpriteAtlas::blockBase(sprite), bx, by);
        const int bcx = bx / SpellSpriteAtlas::kCellSize;
        const int bcy = by / SpellSpriteAtlas::kCellSize;
        for (int dy = 0; dy < SpellSpriteAtlas::kSheetGrid; ++dy) {
            for (int dx = 0; dx < SpellSpriteAtlas::kSheetGrid; ++dx) {
                const int cell = bcx + dx + SpellSpriteAtlas::kColumns * (bcy + dy);
                ASSERT_LT(cell, SpellSpriteAtlas::kCapacity)
                    << SpellSpriteAtlas::kFiles[sprite] << " block out of the sheet";
                ASSERT_FALSE(seen[cell]) << "cell " << cell << " is in two blocks";
                seen[cell] = true;
            }
        }
    }
    int unused = 0;
    for (int cell = 0; cell < SpellSpriteAtlas::kCapacity; ++cell)
        if (!seen[cell]) ++unused;
    EXPECT_EQ(unused, SpellSpriteAtlas::kCapacity - SpellSpriteAtlas::kUsedCells);
}

TEST(SpellSpriteAnimation, TheStoredFrameMatchesTheSheetKind) {
    // B1188: the stored frame edge derives from the sheet's kind -- a 4x4
    // grid keeps its 32 px cell (sixteen native frames would not fit the
    // block), a 2x2 sheet keeps its 64 px frames, a single image keeps its
    // native resolution capped at the whole block (64 -> 2x2 sub-block,
    // glowflare02's 256 -> 128 = the block 2:1).
    for (int sprite = 0; sprite < SpellSpriteAtlas::kFileCount; ++sprite) {
        const int frames = SpellSpriteAtlas::framesFor(sprite);
        const int stored = SpellSpriteAtlas::storedFramePx(sprite);
        const int span = SpellSpriteAtlas::storedSpan(sprite);
        EXPECT_EQ(stored, span * SpellSpriteAtlas::kCellSize)
            << SpellSpriteAtlas::kFiles[sprite];
        if (frames == SpellSpriteAtlas::kFramesPerSprite) {
            EXPECT_EQ(stored, SpellSpriteAtlas::kCellSize)
                << SpellSpriteAtlas::kFiles[sprite] << " grid frame";
        } else if (frames == SpellSpriteAtlas::kTwoByTwo) {
            EXPECT_EQ(stored, 2 * SpellSpriteAtlas::kCellSize)
                << SpellSpriteAtlas::kFiles[sprite] << " 2x2 frame";
        } else {
            EXPECT_GE(stored, 2 * SpellSpriteAtlas::kCellSize)
                << SpellSpriteAtlas::kFiles[sprite]
                << ": a single image must keep at least its 2x2 sub-block";
        }
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

// ---------------------------------------------------------------------------
// B1124 (#444, B1118 M8) -- a SINGLE-IMAGE sheet is one frame for the whole
// life. spelc3 / spelc4 (record 0's Target and Source art, spells 93 / 38)
// and five more are 64x64 single pictures, not 4x4 grids. Before B1124 every
// sprite stepped through sixteen cells, so these drew sixteen fragments of
// one star in turn.
// ---------------------------------------------------------------------------

TEST(SpellSpriteAtlasLayout, ExactlyTheCensusedSingleImagesAreOneFrame) {
    // B1124's seven minus spelb3/spelb4 (they left the sheet, B1127), plus
    // B1127's 64x64 spellsnew.edd textures and the one 256x256 single picture
    // (glowflare02, frame field 1 in spellsnew.edd). flare_blsp50a and
    // flare_whsp50a are 2x2 grids (spellsnew.edd frame field 4).
    const std::vector<std::string> singles = {
        "gena20.bmp", "gena30.bmp", "genab1.png", "genb20.bmp", "glowflare02.tga",
        "spelc3.tga", "spelc4.tga", "spelcc.png", "spelce.png", "spelcf.png",
        "speld4.tga", "speldc.png", "speldd.tga", "spely3.tga", "spely4.tga",
        "spelyc.png", "spelyd.png", "spelye.png"};
    const std::vector<std::string> twoByTwo = {"flare_blsp50a.png", "flare_whsp50a.png"};
    for (int sprite = 0; sprite < SpellSpriteAtlas::kFileCount; ++sprite) {
        const std::string file = SpellSpriteAtlas::kFiles[sprite];
        const bool single =
            std::find(singles.begin(), singles.end(), file) != singles.end();
        const bool quad =
            std::find(twoByTwo.begin(), twoByTwo.end(), file) != twoByTwo.end();
        EXPECT_EQ(SpellSpriteAtlas::framesFor(sprite),
                  single ? 1 : quad ? SpellSpriteAtlas::kTwoByTwo : SpellSpriteAtlas::kFramesPerSprite)
            << file;
    }
    // Out of range draws the slot's first cell only, never a neighbour's.
    EXPECT_EQ(SpellSpriteAtlas::framesFor(-1), 1);
    EXPECT_EQ(SpellSpriteAtlas::framesFor(SpellSpriteAtlas::kFileCount), 1);
}

TEST(SpellSpriteAnimation, ASingleImageHoldsItsOnlyFrameForTheWholeLife) {
    for (const char* file : {"spelc3.tga", "spelc4.tga"}) {
        const int sprite = SpellSpriteAtlas::slotForFile(file);
        ASSERT_GE(sprite, 0);
        const int first = SpellSpriteAtlas::cellFor(sprite, 0);
        for (float age : {-1.0f, 0.0f, 0.1f, 0.5f, 0.75f, 0.999f, 1.0f, 3.7f}) {
            EXPECT_EQ(SpellSpriteAtlas::cellForAge(sprite, age), first)
                << file << " at normalized age " << age
                << " stepped off its one picture";
        }
    }
    EXPECT_EQ(SpellSpriteAtlas::frameForNormalizedAge(0.5f, 1), 0);
    EXPECT_EQ(SpellSpriteAtlas::frameForNormalizedAge(0.999f, 1), 0);
}

TEST(SpellSpriteAnimation, AGridSpriteStillWalksAllSixteenCells) {
    // The negative: the B1124 rule must not freeze the 26 grid sheets.
    const int sprite = SpellSpriteAtlas::slotForFile("spelc2.tga");
    ASSERT_GE(sprite, 0);
    for (int f = 0; f < SpellSpriteAtlas::kFramesPerSprite; ++f) {
        const float mid =
            (static_cast<float>(f) + 0.5f) / SpellSpriteAtlas::kFramesPerSprite;
        EXPECT_EQ(SpellSpriteAtlas::cellForAge(sprite, mid),
                  SpellSpriteAtlas::cellFor(sprite, f));
    }
}

// ===========================================================================
// B1127 (#447): a spellsnew.edd stage names its texture FILE, and that file is
// what the oracle draws. GENA20_SPRITE's own 0-based file (spela3.tga) does
// not exist; 458's art is gena20.bmp, reached through field 145.
// ===========================================================================

TEST(SpellSpriteNewEffArt, ATextureFileNameAddressesItsOwnSprite) {
    auto& cfg = SpellEffectsConfig::instance();
    for (const char* file : {"gena20.bmp", "spelua.png", "spelub.png", "spelab.tga",
                             "flare_blsp50a.png", "genb20.bmp", "spelcc.png"}) {
        EXPECT_EQ(cfg.getTextureIndex(file), spriteIndex(file)) << file;
    }
    // Any case: the client is Windows.
    EXPECT_EQ(cfg.getTextureIndex("GENA20.BMP"), spriteIndex("gena20.bmp"));
    EXPECT_EQ(cfg.getTextureIndex("GENA20_SPRITE"), ParticleAtlas::SoftCircle);
}

TEST(SpellSpriteNewEffArt, FrostBoltsTargetStageDrawsTheVioletSquigglesAndCrosses) {
    // 54's field-145 Target stage (spellsnew.eff record 82), not spells.eff's
    // GEND10/GENU10 cyan-white stars (kFrostBoltTargetStage above).
    SpellEffectDef def = oneEmitterDef();
    SpellPresets::applyStageSpriteNames(def, {"spelua.png", "spelub.png", "spelub.png", "spelab.tga"}, 54);
    const auto& c = def.emitters[0].config;
    ASSERT_EQ(c.textureRegionCount, 4);
    EXPECT_EQ(c.textureRegions[0], spriteIndex("spelua.png"));
    EXPECT_EQ(c.textureRegions[1], spriteIndex("spelub.png"));
    EXPECT_EQ(c.textureRegions[3], spriteIndex("spelab.tga"));
    EXPECT_EQ(c.colorStart.r, 1.0f);  // T441: the sprite is the colour
    EXPECT_EQ(c.colorStart.b, 1.0f);
}
