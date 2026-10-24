// T441 -- board B398 (where a spell's colour comes from) and B397 (the
// per-spell style overrides).
//
// B1146 (#466): the "no colour field" finding below is about spells.eff (the
// field-123 chain) and still holds there. The official client draws
// spellsnew.eff (B1127), whose spellsnew.edd emitters DO carry colour words
// (B1141, read from the client's code); those are applied after the white
// reset pinned here, by SpellPresets::applyStageEmitterColours()
// (tests/test_spell_edd_colour.cpp).
//
// ============================================================================
// B398.  WHERE THE OFFICIAL CLIENT'S PER-SPELL COLOUR COMES FROM.
// ============================================================================
//
// It does not come from a colour field, because there is no colour field.
// The chain is entirely one of ART:
//
//   spells_us.txt field 123, SPELLAFFECTINDEX
//       (/mnt/projects/akk-stack/code/common/spdat.h line 1653,
//        `uint16 spell_affect_index; // -- SPELLAFFECTINDEX`)
//     -> the record at that INDEX in /mnt/projects/EverQuestP1999/spells.eff
//        (256 fixed 2792-byte records, the index IS the position -- T70)
//     -> that record's three 928-byte stage blocks, each opening with three
//        32-byte sprite-name slots
//     -> GEN<L><d>0_SPRITE resolves 0-BASED to SpellEffects/spel<l><d+1>.tga
//        (T389 MEASURED this against the official client, board B162)
//     -> and the particle is that .tga's own texels.
//
// The four spells T422 measured, read straight out of the shipped
// spells.eff (the record indices are their spells_us.txt field 123):
//
//   spell 41  Weaken          record 20   every slot GENN00_SPRITE -> speln1
//   spell 93  Burst of Flame  record  0   Target GENC20/GENC10/GENG00
//   spell 54  Frost Bolt      record 23   Target GEND10/GENU10/GENU10
//   spell 253 Whirling Wind   record 35   every slot GENA10_SPRITE -> spela2
//
// Nothing in that chain is a colour.  T73 went looking for one inside the
// 800 unknown bytes of each stage block and REFUTED the only candidate: the
// float[12] at block offset 0x370 reaches 32.0, over 12x the claimed [0,2]
// colour range (spells_eff_reader.h, "REFUTED (as colour)").  Field 85
// RESISTTYPE is a mechanic -- it decides who resists, and the official
// client does not paint with it.
//
// THE DEFECT.  fable already drew the right sprites for the Target stage
// (T389/T417/T420) and then MULTIPLIED them by a colour chosen from the
// resist type -- `POINT_SPRITE_FS` computes `texColor * vColor`.  Weaken is
// resist type 1, magic, so speln1's green mottled discs were multiplied by
// (200,100,255) purple and came out RED-dominant, which is exactly what
// T422 measured on the observing client (0.377/0.294/0.329 against the
// oracle's 0.448/0.572/0.191).
//
// THE FIX, and its scope.  Wherever a real spells.eff stage's sprites are
// installed, the modulation drops to white and the art speaks for itself.
// The resist/school colour survives ONLY where no stage resolved -- the
// spells with no usable record at all -- because there fable has no art to
// take a colour from.  The three stages that were resolved but whose names
// were being thrown away (Source -> cast glow and spellComplete, Middle ->
// projectile trail) now route their names too, so a spell's caster-side and
// travel particles take their colour from the same place its impact does.
//
// The assertions below read the SHIPPED spell sprite sheet -- the same
// pixels the client uploads -- and compare a normalised R/G/B to T389's
// oracle measurement.  A normalised triple is what survives an additive
// blend's saturation; the absolute level does not.
//
// ============================================================================
// B397.  THE PER-SPELL OVERRIDES.
// ============================================================================
//
// `spellOverrides` was an empty object, so one impact/spellComplete setting
// served spells whose oracle populations differ by a factor of twenty.
// T422's live measurement of the official client:
//
//   spell 93   peak 715 sprites     median diam 4.8-6.9 px   duration 2.33 s
//   spell 41   peak 561 sprites     median diam 4.4-6.7 px   duration 2.6  s
//   spell 253  steady 25-36         median diam 8.0-10.5 px  duration 5.6  s
//
// The style default is T422's own wide-burst tuning: impact 480/s and
// spellComplete 600/s over a 0.6 s window = 648 sprites, sizes that measured
// 5.53 px median, total duration 2.6 s.  So:
//
//   93  population only, x1.1034 -> 530 / 662 = 715.2
//   41  population only, x0.8657 -> 416 / 519 = 561.0
//   253 population, size and duration.  A steady 30 at a 1.8 s mean lifetime
//       is 16.7 sprites/s total, split in the style's own 480:600 ratio ->
//       7.4 / 9.3; sizes x1.673 (the measured 5.53 px onto the 8.0-10.5 px
//       band's midpoint 9.25); emitterLifetime 3.6 s so 3.6 + 2.0 = 5.6 s.
//
// 54 Frost Bolt gets NO override -- T422 could not separate its merged
// ribbon into components, so there is no measured population to pin -- and
// it must therefore still resolve to the plain style.
//
// The EMISSION SHAPE is deliberately not touched here: the oracle's rate
// itself ramps and is then cut off, which a rectangular window cannot
// express, and that is board B396.
//
// B1120 (#440) SUPERSEDES THE ABSOLUTE COUNTS ABOVE, not the ratios.  T422's
// 715/561/25-36 are 8-connected blob counts, which B1118 sec. 3 shows are
// LOWER bounds (overlapping sprites merge), and B1118 measured the oracle's
// landing as a VOLUME several caster heights across (peak coverage 1.2-7.3
// caster heights^2 against fable's 0.12-0.26).  B1120 reshaped both landing
// styles into that volume (tests/test_spell_landing_volume.cpp) and raised
// the style populations with it -- spellComplete 1800/s + impact 1100/s over
// the same 0.6 s window = 1740 -- keeping T422's per-spell RATIOS to the
// style (93 x1.1034, 41 x0.8657).  253's T422 trickle cannot draw the
// oracle's frame-filling star field at any size; its rate is now capped by
// the pool instead (one 253 field + one record-0 landing <= 4096).
//
// B1123 (#443) RETIRES THE T422 RATIOS TOO.  They were blob counts from an
// llvmpipe capture (4.4-5.8 renders/s); B1118 measured the oracle's landing
// coverage at real frame rate (93 5.85, 38 3.89, 41 4.15, 253 7.28 caster
// heights^2), and B1123's lane-5 pool sweep (review/B1123/cal/, pool raised for
// calibration only) shows every one of them needs MORE sprites than the 4096
// pool holds: 93 reaches 4.7 at ~2.2x its peak, 41 its 4.15 at ~1.56x, 253
// its 5.8 at ~5.7x.  So each landing now takes the pool's own bound -- the
// largest live peak that fits beside a 253 field (4096 - 2160 = 1936) -- and:
//   93  has NO override: 93 and 38 are one spells.eff record (0) and draw one
//       picture; the oracle's 93:38 coverage ratio (1.50) is their sprite-size
//       ratio (B1119's 38 size override), not a population difference.  The
//       record-0 STYLE is at the bound (peak live 1927).
//   41  keeps its measured shape and sizes; its population is the bound
//       (x1.232 of B1120's, peak live 1925), not T422's x0.8657.
//   253 keeps the pool-capped field; its landing sprites were the style x1.12.
//
// B1123 COMPLETION (Will 2026-10-22).  The pool is one preset key per platform
// (config/constrained_presets.json `spellParticlePool`, PROVISIONAL: B1130
// re-sizes it) and NO number above is a population any more: every landing
// is derived from the pool by one rule (SpellEffectsConfig::applyPool -- a
// landing plus its own held Source peaks at half the pool; an oracle
// exception, `landingPopulation.exceptions`, caps the landing alone).  The
// pool-accounting cases (SpellPoolRule) run at EVERY platform's pool.  Sizes
// are judged on the caster-ROI instrument (roi_oracle.txt), not B1118's
// whole-frame p90.
//
// B1123 SIZES-FIRST (Will, #443 note 7118): every size matches the oracle's
// ROI p90 first and coverage follows from the pool -- coverage is never bought
// with an oversized sprite.  Every measured row now carries its own size,
// pinned in B1123SizesAreTheOraclesRoi below; coverage after the shrink is
// reported in refactor/B1123/README.md "Sizes-first leg", not asserted here.

#include <gtest/gtest.h>
#include <json/json.h>

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <string>
#include <vector>

#include "client/graphics/environment/particle_types.h"
#include "client/graphics/environment/spell_effects_config.h"
#include "client/graphics/environment/spell_particle_types.h"
#include "client/graphics/environment/spell_sprite_atlas.h"
#include "client/spell/spells_eff_reader.h"
#include "spell_landing_population.h"
#include "spell_pool.h"

using namespace EQT::Graphics::Environment;

namespace {

// --- the measured stage slots, verbatim from spells.eff -------------------
const std::vector<std::string> kWeakenTargetStage = {
    "GENN00_SPRITE", "GENN00_SPRITE", "GENN00_SPRITE"};
const std::vector<std::string> kBurstOfFlameTargetStage = {
    "GENC20_SPRITE", "GENC10_SPRITE", "GENG00_SPRITE"};
const std::vector<std::string> kFrostBoltTargetStage = {
    "GEND10_SPRITE", "GENU10_SPRITE", "GENU10_SPRITE"};
const std::vector<std::string> kWhirlingWindTargetStage = {
    "GENA10_SPRITE", "GENA10_SPRITE", "GENA10_SPRITE"};

std::string repoRoot() { return std::string(WILLEQ_REPO_ROOT); }

std::string eqClientPath() {
    const char* env = std::getenv("EQ_CLIENT_PATH");
    return env ? env : EQ_CLIENT_DATA_PATH;
}

// --- the shipped spell sprite sheet, read as pixels -----------------------
// data/textures/spell_sprite_atlas.bmp is a 32-bit BGRA bottom-up-or-top-down
// BMP written by tools/spell_sprite_atlas_builder.
struct Sheet {
    bool ok = false;
    int width = 0;
    int height = 0;      // absolute
    bool topDown = false;
    std::vector<uint8_t> pixels;  // BGRA, `height` rows of `width`

    void texel(int x, int y, int& r, int& g, int& b, int& a) const {
        const int row = topDown ? y : (height - 1 - y);
        const size_t o = (static_cast<size_t>(row) * width + x) * 4;
        b = pixels[o]; g = pixels[o + 1]; r = pixels[o + 2]; a = pixels[o + 3];
    }
};

uint32_t le32(const std::vector<uint8_t>& d, size_t o) {
    return static_cast<uint32_t>(d[o]) | (static_cast<uint32_t>(d[o + 1]) << 8) |
           (static_cast<uint32_t>(d[o + 2]) << 16) |
           (static_cast<uint32_t>(d[o + 3]) << 24);
}

Sheet loadSheet() {
    Sheet s;
    const std::string path = repoRoot() + "/data/textures/spell_sprite_atlas.bmp";
    std::ifstream f(path, std::ios::binary);
    if (!f) return s;
    std::vector<uint8_t> d((std::istreambuf_iterator<char>(f)),
                            std::istreambuf_iterator<char>());
    if (d.size() < 54 || d[0] != 'B' || d[1] != 'M') return s;
    const uint32_t off = le32(d, 10);
    const int32_t w = static_cast<int32_t>(le32(d, 18));
    const int32_t h = static_cast<int32_t>(le32(d, 22));
    const uint16_t bpp = static_cast<uint16_t>(d[28] | (d[29] << 8));
    if (bpp != 32 || w <= 0) return s;
    s.width = w;
    s.topDown = h < 0;
    s.height = h < 0 ? -h : h;
    const size_t need = off + static_cast<size_t>(s.width) * s.height * 4;
    if (d.size() < need) return s;
    s.pixels.assign(d.begin() + off, d.begin() + need);
    s.ok = true;
    return s;
}

// Alpha-weighted mean RGB over every frame (time-weighted) of every sprite an emitter's
// textureRegions[] address, normalised to sum 1. This models what an
// additive point-sprite batch adds to the framebuffer, up to the per-particle
// alpha ramp, which is common to all channels and drops out of a normalised
// triple.
struct Rgb { double r = 0, g = 0, b = 0; };

Rgb spriteArtColour(const Sheet& sheet, const EmitterConfig& cfg,
                    const glm::vec4& modulation) {
    Rgb sum;
    for (uint8_t i = 0; i < cfg.textureRegionCount; ++i) {
        const int ordinal = cfg.textureRegions[i];
        if (ordinal < SpellSpriteAtlas::kBase) continue;  // procedural tile
        const int sprite = ordinal - SpellSpriteAtlas::kBase;
        // B1124: a single image's one frame is on screen for the whole life,
        // i.e. sixteen grid frames' worth of time.
        const int frames = SpellSpriteAtlas::framesFor(sprite);
        const double hold = static_cast<double>(SpellSpriteAtlas::kFramesPerSprite) / frames;
        for (int frame = 0; frame < frames; ++frame) {
            int cx = 0, cy = 0;
            SpellSpriteAtlas::cellOrigin(SpellSpriteAtlas::cellFor(sprite, frame), cx, cy);
            for (int y = cy; y < cy + SpellSpriteAtlas::kCellSize; ++y) {
                for (int x = cx; x < cx + SpellSpriteAtlas::kCellSize; ++x) {
                    int r = 0, g = 0, b = 0, a = 0;
                    sheet.texel(x, y, r, g, b, a);
                    const double w = hold * a / 255.0;
                    sum.r += r * w;
                    sum.g += g * w;
                    sum.b += b * w;
                }
            }
        }
    }
    sum.r *= modulation.r;
    sum.g *= modulation.g;
    sum.b *= modulation.b;
    const double t = sum.r + sum.g + sum.b;
    if (t > 0) { sum.r /= t; sum.g /= t; sum.b /= t; }
    return sum;
}

char dominant(const Rgb& c) {
    if (c.r >= c.g && c.r >= c.b) return 'R';
    if (c.g >= c.b) return 'G';
    return 'B';
}

EmitterConfig impactWithStage(const std::vector<std::string>& stage, uint32_t spellId,
                               const glm::vec4& callerColour) {
    auto def = SpellPresets::Impact(callerColour, spellId);
    SpellPresets::applyStageSpriteNames(def, stage, spellId);
    return def.emitters.at(0).config;
}

// The colour fable's own getSpellColor() produced for these two resist types
// before T441 -- spell_visual_fx_nodes.cpp's colorForVocabulary().
const glm::vec4 kMagicTint(200 / 255.0f, 100 / 255.0f, 255 / 255.0f, 1.0f);
const glm::vec4 kFireTint(255 / 255.0f, 120 / 255.0f, 50 / 255.0f, 1.0f);
const glm::vec4 kColdTint(100 / 255.0f, 180 / 255.0f, 255 / 255.0f, 1.0f);

class SpellColourFromSpriteArt : public ::testing::Test {
protected:
    void SetUp() override {
        if (!SpellEffectsConfig::instance().load(repoRoot() + "/config/spell_effects.json")) {
            GTEST_SKIP() << "no config/spell_effects.json";
        }
        sheet_ = loadSheet();
        if (!sheet_.ok) {
            GTEST_SKIP() << "no data/textures/spell_sprite_atlas.bmp";
        }
    }
    Sheet sheet_;
};

// ---------------------------------------------------------------------------
// The mechanism: installing a stage's art also drops the tint.
// ---------------------------------------------------------------------------

// RED before T441: applyStageSpriteNames() left the caller's resist colour on
// colorStart/colorEnd, so `texColor * vColor` tinted the official client's own
// art with something the official client never applies.
TEST_F(SpellColourFromSpriteArt, InstallingAStagesArtDropsTheModulationToWhite) {
    const EmitterConfig c = impactWithStage(kWeakenTargetStage, 41, kMagicTint);
    EXPECT_FLOAT_EQ(c.colorStart.r, 1.0f);
    EXPECT_FLOAT_EQ(c.colorStart.g, 1.0f);
    EXPECT_FLOAT_EQ(c.colorStart.b, 1.0f);
    EXPECT_FLOAT_EQ(c.colorEnd.r, 1.0f);
    EXPECT_FLOAT_EQ(c.colorEnd.g, 1.0f);
    EXPECT_FLOAT_EQ(c.colorEnd.b, 1.0f);
}

// The alphas are the style's fade curve, not a colour, and must survive.
TEST_F(SpellColourFromSpriteArt, TheStylesAlphaRampIsNotTouched) {
    auto def = SpellPresets::Impact(kMagicTint, 41);
    const float startA = def.emitters.at(0).config.colorStart.a;
    const float endA = def.emitters.at(0).config.colorEnd.a;
    SpellPresets::applyStageSpriteNames(def, kWeakenTargetStage, 41);
    EXPECT_FLOAT_EQ(def.emitters.at(0).config.colorStart.a, startA);
    EXPECT_FLOAT_EQ(def.emitters.at(0).config.colorEnd.a, endA);
    EXPECT_GT(startA, 0.0f);
}

// A spell with NO resolvable stage keeps the resist/school colour: it has no
// art to take a colour from, and that fallback population is ~946 spells.
TEST_F(SpellColourFromSpriteArt, ASpellWithNoStageKeepsTheFallbackColour) {
    auto def = SpellPresets::Impact(kMagicTint, 41);
    SpellPresets::applyStageSpriteNames(def, {"", "", ""}, 41);
    EXPECT_FLOAT_EQ(def.emitters.at(0).config.colorStart.r, kMagicTint.r);
    EXPECT_FLOAT_EQ(def.emitters.at(0).config.colorStart.g, kMagicTint.g);
    EXPECT_FLOAT_EQ(def.emitters.at(0).config.colorStart.b, kMagicTint.b);
}

// ---------------------------------------------------------------------------
// The result, against T389's oracle measurement.
// ---------------------------------------------------------------------------

// THE B398 HEADLINE.  RED before T441: with the magic tint applied, Weaken's
// green art came out red-dominant, which is what T422 measured live
// (0.377/0.294/0.329).  The oracle draws 0.448/0.572/0.191.
TEST_F(SpellColourFromSpriteArt, WeakenIsGreenDominantLikeTheOracle) {
    const EmitterConfig c = impactWithStage(kWeakenTargetStage, 41, kMagicTint);
    const Rgb after = spriteArtColour(sheet_, c, c.colorStart);
    EXPECT_EQ(dominant(after), 'G')
        << "Weaken (41) draws speln1.tga, whose own art is green-dominant, and "
           "the oracle measured 0.448/0.572/0.191; fable computes "
        << after.r << "/" << after.g << "/" << after.b;
    EXPECT_GT(after.g, after.r);
    EXPECT_GT(after.g, after.b);

    // And the pre-T441 modulation is what broke it -- assert the negative so a
    // reintroduced tint is a red test rather than a purple burst.
    const Rgb tinted = spriteArtColour(sheet_, c, kMagicTint);
    EXPECT_EQ(dominant(tinted), 'R')
        << "the magic resist colour is what made this red; if this no longer "
           "flips the dominance, this test is not proving what it claims";
}

// Burst of Flame stays red-dominant -- T422 recorded fable and the oracle as
// agreeing here, and the fix must not disturb it.
TEST_F(SpellColourFromSpriteArt, BurstOfFlameStaysRedDominant) {
    const EmitterConfig c = impactWithStage(kBurstOfFlameTargetStage, 93, kFireTint);
    const Rgb after = spriteArtColour(sheet_, c, c.colorStart);
    EXPECT_EQ(dominant(after), 'R')
        << after.r << "/" << after.g << "/" << after.b;
}

// Frost Bolt stays blue-dominant, likewise recorded as agreeing.
TEST_F(SpellColourFromSpriteArt, FrostBoltStaysBlueDominant) {
    const EmitterConfig c = impactWithStage(kFrostBoltTargetStage, 54, kColdTint);
    const Rgb after = spriteArtColour(sheet_, c, c.colorStart);
    EXPECT_EQ(dominant(after), 'B')
        << after.r << "/" << after.g << "/" << after.b;
}

// Whirling Wind is the RESIDUAL, pinned as what it is rather than hidden.
// spela2.tga carries no red at all, so its art cannot reproduce the oracle's
// 0.258/0.309/0.214; removing the tint takes it from 0.299/0.248/0.453
// towards the art but leaves it blue-dominant. Boarded; see the plan Review.
// The one thing that IS settled is that it draws its own art untinted.
TEST_F(SpellColourFromSpriteArt, WhirlingWindDrawsItsOwnArtUntintedAndIsStillBlue) {
    const EmitterConfig c = impactWithStage(kWhirlingWindTargetStage, 253, kMagicTint);
    EXPECT_FLOAT_EQ(c.colorStart.r, 1.0f);
    const Rgb after = spriteArtColour(sheet_, c, c.colorStart);
    EXPECT_EQ(dominant(after), 'B')
        << "if this has become non-blue the residual is closed and the board "
           "entry must be struck; computed "
        << after.r << "/" << after.g << "/" << after.b;
    EXPECT_LT(after.r, 0.05) << "spela2.tga has no red in it";
}

// ---------------------------------------------------------------------------
// The chain itself, against the real spells.eff. Skips if the client data is
// not mounted; the literals above are what the assertions rest on either way.
// ---------------------------------------------------------------------------
TEST(SpellColourChain, TheMeasuredStagesAreWhatSpellsEffActuallyHolds) {
    EQ::SpellsEffReader reader;
    const std::string path = eqClientPath() + "/spells.eff";
    if (!reader.load(path)) {
        GTEST_SKIP() << "spells.eff not loadable at " << path;
    }
    struct Case { int record; const std::vector<std::string>* target; const char* what; };
    const Case cases[] = {
        {20, &kWeakenTargetStage, "41 Weaken"},
        {0, &kBurstOfFlameTargetStage, "93 Burst of Flame"},
        {23, &kFrostBoltTargetStage, "54 Frost Bolt"},
        {35, &kWhirlingWindTargetStage, "253 Whirling Wind"},
    };
    for (const auto& c : cases) {
        const EQ::SpellsEffRecord* rec = reader.getRecord(c.record);
        ASSERT_NE(rec, nullptr) << c.what;
        const auto& target = rec->emitter(EQ::SpellsEffEmitterRole::Target);
        for (size_t i = 0; i < 3; ++i) {
            EXPECT_EQ(target.sprite_names[i], (*c.target)[i])
                << c.what << " record " << c.record << " Target slot " << i;
        }
    }
}

// ---------------------------------------------------------------------------
// B397 -- the per-spell overrides.
// ---------------------------------------------------------------------------

class SpellStyleOverrides : public ::testing::Test {
protected:
    // B1123: at this build's platform pool (spell_pool.h); the pool rule's
    // cases run at every platform's pool (SpellPoolRule).
    void SetUp() override {
        const int pool = spell_pool::Shipped(spell_pool::ThisBuildPlatform());
        ASSERT_GT(pool, 0) << "config/constrained_presets.json names no spellParticlePool for this platform";
        if (!spell_pool::LoadAt(pool)) {
            GTEST_SKIP() << "no config/spell_effects.json";
        }
    }
    static EmitterConfig impact(uint32_t spellId) {
        return SpellPresets::Impact(glm::vec4(1.0f), spellId).emitters.at(0).config;
    }
    static EmitterConfig complete(uint32_t spellId) {
        return SpellPresets::SpellComplete(glm::vec4(1.0f), spellId).emitters.at(0).config;
    }
    // The PEAK phase's cohort (spawnRate over spawnWindow): the population
    // T422's ratios are about.  B1121 added a sustain phase after it.
    static float peak(const EmitterConfig& c) {
        return spell_landing_population::PeakCohort(c);
    }
    // B1121: the expected peak LIVE count of one landing (both halves, peak +
    // sustain) -- what the pool has to hold.
    static float live(uint32_t spellId) {
        return spell_landing_population::PeakLive({impact(spellId), complete(spellId)});
    }
    // Steady population of a continuous emission: rate x mean lifetime.
    static float steady(const EmitterConfig& c) {
        return c.spawnRate * 0.5f * (c.lifetimeMin + c.lifetimeMax);
    }
};

// B1123 (RED on the B1122 config, which carried 93's T422 x1.1034 override):
// Burst of Flame is record 0, the same spells.eff record and picture as
// Lightning Bolt, so it resolves to the record-0 style in every key -- no
// per-spell population.  The oracle's 93:38 coverage ratio is B1119's size
// ratio (38's size override), which this leaves alone.
TEST_F(SpellStyleOverrides, BurstOfFlameIsTheRecordZeroStyle) {
    for (const auto& pair : {std::make_pair(impact(93), impact(0)),
                             std::make_pair(complete(93), complete(0))}) {
        EXPECT_FLOAT_EQ(pair.first.spawnRate, pair.second.spawnRate)
            << "93 carries its own population again; the T422 ratio was retired by B1123";
        EXPECT_FLOAT_EQ(pair.first.sustainRate, pair.second.sustainRate);
        EXPECT_FLOAT_EQ(pair.first.sizeStartMin, pair.second.sizeStartMin);
        EXPECT_FLOAT_EQ(pair.first.emitterLifetime, pair.second.emitterLifetime);
    }
    EXPECT_FLOAT_EQ(complete(38).spawnRate, complete(93).spawnRate) << "one record, one population";
    EXPECT_FLOAT_EQ(impact(38).spawnRate, impact(93).spawnRate);
}

// B1123 completion (#443; Will 2026-10-22): THE POOL RULE, at every
// platform's shipped pool.  The pool is one preset key per platform and the
// populations are derived from it (SpellEffectsConfig::applyPool), so no
// number below is a population: each is read from the config files.
class SpellPoolRule : public SpellStyleOverrides,
                      public ::testing::WithParamInterface<const char*> {
protected:
    void SetUp() override {
        const int pool = spell_pool::Shipped(GetParam());
        ASSERT_GT(pool, 0) << "config/constrained_presets.json names no spellParticlePool for this platform";
        if (!spell_pool::LoadAt(pool)) {
            GTEST_SKIP() << "no config/spell_effects.json";
        }
    }
    static float pool() {
        return static_cast<float>(SpellEffectsConfig::instance().getGlobal().maxParticles);
    }
    // `landingPopulation.exceptions.<id>.peakLive` (0 = none): a landing the
    // oracle fills with fewer sprites than half the pool.
    static float exception(uint32_t id) {
        std::ifstream f(repoRoot() + "/config/spell_effects.json");
        Json::Value root;
        Json::CharReaderBuilder b;
        std::string errs;
        if (!Json::parseFromStream(b, f, &root, &errs)) return 0.0f;
        return root["landingPopulation"]["exceptions"][std::to_string(id)]["peakLive"].asFloat();
    }
};
INSTANTIATE_TEST_SUITE_P(Platforms, SpellPoolRule, ::testing::ValuesIn(spell_pool::kPlatforms));

// The sbc pool is the memory-bound one: never above the desktop pool.
TEST(SpellPoolPlatforms, TheSbcPoolIsTheSmallerOne) {
    const int sbc = spell_pool::Shipped("sbc"), desktop = spell_pool::Shipped("desktop");
    ASSERT_GT(sbc, 0);
    ASSERT_GT(desktop, 0);
    EXPECT_LE(sbc, desktop) << "sbc " << sbc << " desktop " << desktop;
}

// Every measured landing peaks at the rule for this pool: the landing plus its
// own held Source (B1122) at half the pool -- so two overlapping landings, or
// 253's field beside one, fit -- or, for an oracle exception, the landing alone
// at min(peakLive, that).  Computed through the shipped presets (what the
// worker is handed), not the config structs.  RED on the pre-rule tree
// (populations were hand-typed for one pool).
TEST_P(SpellPoolRule, EveryLandingPeaksAtTheRuleForThisPool) {
    namespace sp = EQT::Graphics::Environment::spell_population;
    const float bound = 0.5f * pool();
    const auto& cfg = EQT::Graphics::Environment::SpellEffectsConfig::instance();
    for (uint32_t id : {0u, 93u, 38u, 54u, 656u, 41u, 253u, 458u, 285u}) {
        // B1131: the held Source is the spell's RECORD's (rate, hold); 0 and 285
        // stand for a spell whose record is not listed (the default row).
        const int rec = cfg.overrideRecord(id);
        const auto imp = SpellPresets::Impact(glm::vec4(1.0f), id, rec);
        const auto com = SpellPresets::SpellComplete(glm::vec4(1.0f), id, rec);
        const std::vector<sp::Schedule> halves = {
            spell_landing_population::ScheduleOf(imp.emitters.at(0).config),
            spell_landing_population::ScheduleOf(com.emitters.at(0).config)};
        float withSource = 0.0f;
        for (bool spray : {false, true}) {
            const auto src = SpellPresets::SourceForSpell(glm::vec4(1.0f), id, rec, spray, 3.6f, 1.0f);
            const auto o = sp::OverlapOf(halves, imp.emitters.at(0).triggerDelay,
                                         spell_landing_population::ScheduleOf(src.emitters.at(0).config),
                                         cfg.sourceRecord(rec).maxHold());
            withSource = std::max(withSource, o.PeakAt(1.0f));
        }
        const float ex = exception(id);
        const float alone = live(id);
        if (ex > 0.0f && ex <= alone * 1.002f) {
            EXPECT_NEAR(alone, ex, 0.002f * ex) << "spell " << id << " (oracle exception)";
            EXPECT_LE(withSource, bound * 1.002f) << "spell " << id;
        } else {
            EXPECT_NEAR(withSource, bound, 0.002f * bound)
                << "spell " << id << " at pool " << pool() << ": landing " << alone << " + held Source";
        }
    }
}

// The hand-set exceptions are the landings the oracle fills with FEWER sprites
// than the pool bound -- each with its measured reason -- and the bound-limited
// landings (record 0 = 93/38, 253's field) are NOT exceptions: they re-flow
// with the pool.  RED on the B1123 config (no landingPopulation block).
TEST(SpellPoolExceptions, OnlyTheOracleLimitedLandingsAreExceptions) {
    std::ifstream f(repoRoot() + "/config/spell_effects.json");
    Json::Value root;
    Json::CharReaderBuilder b;
    std::string errs;
    ASSERT_TRUE(Json::parseFromStream(b, f, &root, &errs)) << errs;
    const Json::Value& ex = root["landingPopulation"]["exceptions"];
    ASSERT_TRUE(ex.isObject()) << "config/spell_effects.json has no landingPopulation.exceptions";
    for (const char* id : {"41", "54", "656", "458"}) {
        EXPECT_GT(ex[id]["peakLive"].asFloat(), 0.0f) << id;
        EXPECT_GT(ex[id]["why"].asString().size(), 20u) << id << ": an exception says why";
    }
    for (const char* id : {"0", "93", "38", "253"}) EXPECT_FALSE(ex.isMember(id)) << id;
}

// The rule scales a landing's rates, never its schedule: the peak:sustain and
// impact:spellComplete ratios are the file's.
TEST_P(SpellPoolRule, TheRuleKeepsEachLandingsSchedule) {
    for (uint32_t id : {0u, 41u, 54u, 253u}) {
        const EmitterConfig i = impact(id), c = complete(id);
        if (i.sustainRate > 0.0f) {
            EXPECT_NEAR(i.spawnRate / i.sustainRate, c.spawnRate / c.sustainRate, 0.02f) << id;
        }
        EXPECT_GT(i.spawnRate, 0.0f) << id;
    }
}

// Whirling Wind is a continuous field, not a 0.6 s burst -- and since B1120
// a FRAME-FILLING one (B1118: oracle 253 Landing coverage 7.28 caster
// heights^2; T422's 25-36 trickle measured max 0.033).  Its steady population
// is bounded by the pool: one 253 field plus one record-0 landing must fit.
TEST_P(SpellPoolRule, WhirlingWindIsASteadyFrameFillingField) {
    const float total = steady(impact(253)) + steady(complete(253));
    const float largest = live(93);
    EXPECT_GE(total, 1500.0f) << "a field, not a trickle; fable holds " << total;
    EXPECT_LE(total + largest, pool() * 1.002f)
        << "253's field (" << total << ") plus a record-0 landing (" << largest
        << ") must fit the pool (" << pool() << ")";
}

// And it lasts as long as the oracle's: B1118 measured its landing END at
// Act+6.52 s (T422's 5.6 s total was an llvmpipe-era read).  The sim's last
// death (delay + emitterLifetime + lifetimeMax) must not fall before it; the
// measured end is the lane-5 capture's (refactor/B1121/README.md).
TEST_F(SpellStyleOverrides, WhirlingWindLastsAsLongAsTheOracles) {
    const glm::vec4 col(1.0f);
    for (const auto& def : {SpellPresets::Impact(col, 253), SpellPresets::SpellComplete(col, 253)}) {
        const auto& ed = def.emitters.at(0);
        const float lastDeath = ed.triggerDelay + ed.config.emitterLifetime + ed.config.lifetimeMax;
        EXPECT_GE(lastDeath, 6.52f) << "last death " << lastDeath << " s";
        EXPECT_LE(lastDeath, 7.32f) << "last death " << lastDeath << " s";
        EXPECT_FLOAT_EQ(ed.config.sustainRate, 0.0f) << "a continuous field, not a peak + sustain";
        EXPECT_FLOAT_EQ(ed.config.spawnWindow, 0.0f);
    }
}

// An UNMEASURED spell still resolves to the plain style -- no override may be
// invented for a spell whose oracle numbers nobody has taken. 285, 1 and 2
// are the worked cases. (656 Shock of Ice was one until B1120 measured its
// landing SHAPE -- the record-23 swirl + glints -- and gave it an override.)
// (54 Frost Bolt used to be the example; B1118 measured it and B1119 gave it a
// Landing size override.)
TEST_F(SpellStyleOverrides, AnUnmeasuredSpellStillResolvesToTheStyle) {
    const EmitterConfig style = impact(0);
    for (uint32_t spellId : {285u, 1u, 2u}) {
        const EmitterConfig c = impact(spellId);
        EXPECT_FLOAT_EQ(c.spawnRate, style.spawnRate) << "spell " << spellId;
        EXPECT_FLOAT_EQ(c.emitterLifetime, style.emitterLifetime) << "spell " << spellId;
        EXPECT_FLOAT_EQ(c.sizeStartMin, style.sizeStartMin) << "spell " << spellId;
        EXPECT_FLOAT_EQ(c.lifetimeMax, style.lifetimeMax) << "spell " << spellId;
    }
}

// An override is PARTIAL: 38 sets only its B1119 sizes, so everything else it
// does not name must still come from the style. That is the three-tier
// hierarchy's contract and the reason a size override does not silently reset
// the measured population, lifetimes and shape.  (93 was the example until
// B1123 deleted its population override.)
TEST_F(SpellStyleOverrides, AnOverrideOnlyMovesWhatItNames) {
    const EmitterConfig style = impact(0);
    const EmitterConfig c = impact(38);
    EXPECT_NE(c.sizeStartMin, style.sizeStartMin);
    EXPECT_FLOAT_EQ(c.spawnRate, style.spawnRate);
    EXPECT_FLOAT_EQ(c.sustainRate, style.sustainRate);
    EXPECT_FLOAT_EQ(c.emitterLifetime, style.emitterLifetime);
    EXPECT_FLOAT_EQ(c.lifetimeMin, style.lifetimeMin);
    EXPECT_FLOAT_EQ(c.lifetimeMax, style.lifetimeMax);
    EXPECT_FLOAT_EQ(c.expandSpeed, style.expandSpeed);
    EXPECT_EQ(c.textureRegionCount, style.textureRegionCount);
}

// The pool must still hold two of the LARGEST fed landing. B395 wired
// `global.maxParticles`; since B1123 93 and 41 both sit at the pool bound.
TEST_P(SpellPoolRule, ThePoolStillHoldsTwoOfTheLargestLanding) {
    float largest = 0.0f;
    for (uint32_t id : {0u, 93u, 38u, 54u, 656u, 41u, 458u}) largest = std::max(largest, live(id));
    EXPECT_GE(pool() * 1.002f, 2.0f * largest)
        << "largest landing " << largest << ", pool "
        << SpellEffectsConfig::instance().getGlobal().maxParticles;
}

}  // namespace

// ---------------------------------------------------------------------------
// B1123 SIZES-FIRST (#443 note 7118) -- sprite SIZE per stage.  Will: "sizes
// match the oracle first, coverage follows from the pool".  Every row is
// pinned to the oracle's caster-ROI p90 (review/B1119/scripts/fx_sprite_roi.py;
// review/B1120/measure_oracle/roi_oracle.txt; band +-15 %), read DENSE on the
// lane-5 8192 capture (refactor/B1123/README.md "Sizes-first leg", review/
// B1123/leg3/gate_table.tsv).  Over-band rows were SHRUNK; nothing was held
// for coverage.  start-range midpoint, oracle ROI, cal c2 / gate reading:
//   Landing  impact  spellComplete  oracle  band          cal    gate
//     93     0.198   0.244 (style)  0.075   0.064-0.086   0.075  0.074
//     38     0.1785  0.219          0.069   0.059-0.079   0.075  0.073
//     54     0.1525  0.187          0.044   0.037-0.051   0.047  0.046
//     656    0.115   0.141          0.035   0.030-0.040   0.033  0.033
//     41     0.2645  0.325          0.101   0.086-0.116   0.102  0.102
//     253    0.351   0.4315         0.093   0.079-0.107   0.096  0.095
//     458    0.3615  0.4455         0.101   0.086-0.116   0.101  0.100
//   Source   castGlow 0.659 (style: 93, 38), 54 0.216, 656 0.225, 253 1.03,
//            458 0.4895; castSpray 41 1.0035 (style 1.356 untouched: 41 is
//            its only measured spell).  Oracle 93 0.084, 38 0.074, 54 0.027,
//            656 0.028, 41 0.113, 253 0.178, 458 0.043; cal c2 0.080 0.079
//            0.027 0.028 0.112 0.150 0.045; gate 0.078 0.077
//            0.027 0.028 0.108 0.164 0.046 -- all 14 in band.  Coverage at the gate
//            (landing 3.01 2.55 0.98 0.62 2.25 3.67 1.50 vs the oracle 5.85 3.89
//            2.46 2.49 4.15 7.28 1.24) is reported, not asserted.
// ---------------------------------------------------------------------------

namespace {
EmitterConfig castGlowFor(uint32_t spellId) {
    return SpellPresets::CastGlow(glm::vec4(1.0f), spellId).emitters.at(0).config;
}
EmitterConfig castSprayFor(uint32_t spellId) {
    return SpellPresets::CastSpray(glm::vec4(1.0f), spellId).emitters.at(0).config;
}
// Midpoint of the start range: what the factor tables above are written in.
float startMid(const EmitterConfig& c) { return 0.5f * (c.sizeStartMin + c.sizeStartMax); }
}  // namespace

// B1122 (#442): the Source cloud's GEOMETRY follows the spells.eff record, the
// same rule B1120 applied to the landing: 93/38 (record 0) are the style, 54
// and 656 (record 23) share one swirl, 41 (record 20) and 253/458 (records
// 35/33) carry their own measured shape, and an unmeasured spell is the style
// (an invented override is the guess Will's ruling forbids).
TEST_F(SpellStyleOverrides, B1122SourceShapesFollowTheSpellsEffRecord) {
    using EQT::Graphics::Environment::MotionType;
    using EQT::Graphics::Environment::SpawnShape;
    const EmitterConfig style = castGlowFor(0);
    EXPECT_EQ(style.spawnShape, SpawnShape::BOX);
    for (uint32_t id : {93u, 38u, 285u, 1u, 2u}) {
        const EmitterConfig c = castGlowFor(id);
        EXPECT_EQ(c.spawnShape, style.spawnShape) << id;
        EXPECT_EQ(c.spawnExtents, style.spawnExtents) << id;
        EXPECT_FLOAT_EQ(c.spawnRate, style.spawnRate) << id;
    }
    for (uint32_t id : {285u, 1u, 2u}) {
        EXPECT_EQ(castSprayFor(id).spawnExtents, castSprayFor(0).spawnExtents) << id;
        EXPECT_FLOAT_EQ(castSprayFor(id).spawnRate, castSprayFor(0).spawnRate) << id;
    }
    const EmitterConfig s54 = castGlowFor(54), s656 = castGlowFor(656);
    EXPECT_EQ(s54.motionType, MotionType::ORBITAL);
    EXPECT_FLOAT_EQ(s54.orbitalRadius, s656.orbitalRadius);
    EXPECT_FLOAT_EQ(s54.spawnRate, s656.spawnRate);
    EXPECT_NE(castSprayFor(41).spawnExtents, castSprayFor(0).spawnExtents);
    EXPECT_NE(castGlowFor(253).spawnExtents, style.spawnExtents);
    EXPECT_NE(castGlowFor(458).spawnExtents, style.spawnExtents);
    // Sizes are B1123SizesAreTheOraclesRoi's: 253 keeps the size that read in
    // band before the sizes-first shrink of the style.
    EXPECT_NE(startMid(castGlowFor(253)), startMid(style));
}

// RED on the completion-leg config (fable 52b810e2): every row but 253/458
// Landing and 253/458 Source moved. B1127 (#447): 54/656/253/458 draw their
// field-145 (spellsnew.edd) sprites, whose drawn footprint differs from the old
// ones', so their sizes were re-fitted to the SAME oracle ROI p90 bands on lane 5
// (review/B1127/README.md sec. 5; 253's Source stays short: its sprites reach
// the point clamp, and 458's largest end size is held at 2.49 under it).
TEST_F(SpellStyleOverrides, B1123SizesAreTheOraclesRoi) {
    struct Row { uint32_t id; float impact, complete; };
    for (const Row& r : {Row{0, 0.198f, 0.244f}, Row{93, 0.198f, 0.244f}, Row{38, 0.1785f, 0.219f},
                         Row{54, 0.314f, 0.3855f}, Row{656, 0.2015f, 0.2465f}, Row{41, 0.2645f, 0.325f},
                         Row{253, 0.496f, 0.6105f}, Row{458, 1.0545f, 1.2995f}}) {
        EXPECT_NEAR(startMid(impact(r.id)), r.impact, 0.0006f) << r.id;
        EXPECT_NEAR(startMid(complete(r.id)), r.complete, 0.0006f) << r.id;
    }
    struct Src { uint32_t id; float mid; };
    for (const Src& r : {Src{0, 0.659f}, Src{93, 0.659f}, Src{38, 0.659f}, Src{54, 0.3435f},
                         Src{656, 0.3715f}, Src{253, 1.8335f}, Src{458, 1.3165f}}) {
        EXPECT_NEAR(startMid(castGlowFor(r.id)), r.mid, 0.0006f) << r.id;
    }
    EXPECT_NEAR(startMid(castSprayFor(41)), 1.0035f, 0.0006f);
    EXPECT_NEAR(startMid(castSprayFor(0)), 1.356f, 0.0006f) << "the castSpray style is unmeasured";
    // 93 IS record 0: no size override of its own.
    EXPECT_FLOAT_EQ(startMid(impact(93)), startMid(impact(0)));
    EXPECT_FLOAT_EQ(startMid(castGlowFor(93)), startMid(castGlowFor(0)));
}

// The point-sprite clamp (POINT_SPRITE_VS: 1..100 px, size * screenH / clip.w)
// must not bind at the B1118 chase camera (clip.w >= ~20 world units) at
// 1280x800, nor at the Orange Pi's 800x600: the largest B1119 size there.
TEST_F(SpellStyleOverrides, B1119SizesDoNotHitThePointClampAtTheChaseCamera) {
    float largest = 0.0f;
    for (uint32_t id : {0u, 93u, 38u, 54u, 656u, 41u, 253u, 458u}) {
        for (const auto& c : {impact(id), complete(id), castGlowFor(id), castSprayFor(id)}) {
            largest = std::max({largest, c.sizeStartMax, c.sizeEndMax});
        }
    }
    EXPECT_LT(largest * 800.0f / 20.0f, 100.0f) << "largest size " << largest;
}
