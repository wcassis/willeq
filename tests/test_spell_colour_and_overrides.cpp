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
// exception, `landingPopulation.exceptions`, capped the landing alone --
// retired by B1149, tests/spell_fx_population_oracle.h).  The
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
#include "client/spell/spell_fx_timing.h"
#include "client/spell/spells_eff_reader.h"
#include "client/spell/spells_new_eff_reader.h"
#include "spell_landing_population.h"
#include "spell_fx_population_oracle.h"
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
        // i.e. sixteen grid frames' worth of time. B1188: a frame occupies
        // its STORED frame (span cells on a side) -- a native 64x64 picture,
        // not one 32x32 cell -- so the colour averages over the whole
        // stored frame, uniform per frame.
        const int frames = SpellSpriteAtlas::framesFor(sprite);
        const int span = SpellSpriteAtlas::storedSpan(sprite);
        const int px = span * SpellSpriteAtlas::kCellSize;
        for (int frame = 0; frame < frames; ++frame) {
            int cx = 0, cy = 0;
            SpellSpriteAtlas::cellOrigin(SpellSpriteAtlas::cellFor(sprite, frame), cx, cy);
            for (int y = cy; y < cy + px; ++y) {
                for (int x = cx; x < cx + px; ++x) {
                    int r = 0, g = 0, b = 0, a = 0;
                    sheet.texel(x, y, r, g, b, a);
                    const double w = a / 255.0;
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
// 253's field beside one, fit (B1149: the oracle exceptions are retired; and a
// spell with a spellsnew.eff record draws its slots' file counts, not this).  Computed through the shipped presets (what the
// worker is handed), not the config structs.  RED on the pre-rule tree
// (populations were hand-typed for one pool).
TEST_P(SpellPoolRule, EveryLandingPeaksAtTheRuleForThisPool) {
    namespace sp = EQT::Graphics::Environment::spell_population;
    const float bound = 0.5f * pool();
    const auto& cfg = EQT::Graphics::Environment::SpellEffectsConfig::instance();
    for (uint32_t id : {0u, 93u, 38u, 54u, 656u, 41u, 253u, 458u, 285u}) {
        // B1153 (#473): the per-record rows and `sourceRecords.overrideRecords`
        // are retired -- the default row's hold and the style's held-Source
        // factor (recordSourceScale_[-1]) for every spell.
        const int rec = -1;
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
        const float alone = live(id);
        EXPECT_NEAR(withSource, bound, 0.002f * bound)
            << "spell " << id << " at pool " << pool() << ": landing " << alone << " + held Source";
    }
}

// B1149 (#469): the hand-set exceptions (B1123's fitted peaks for the four
// landings the oracle fills with FEWER sprites) are retired with the rest of
// the per-record populations: every spellsnew.eff record draws its slots'
// FILE counts. Their values are bands in tests/spell_fx_population_oracle.h.
TEST(SpellPoolExceptions, TheFittedExceptionsAreRetiredToBands) {
    std::ifstream f(repoRoot() + "/config/spell_effects.json");
    Json::Value root;
    Json::CharReaderBuilder b;
    std::string errs;
    ASSERT_TRUE(Json::parseFromStream(b, f, &root, &errs)) << errs;
    EXPECT_FALSE(root["landingPopulation"].isMember("exceptions"));
    EXPECT_FALSE(root["sourceRecords"].isMember("population"));
    EXPECT_FALSE(root["targetRecords"].isMember("referenceCoverage"));
    std::vector<int> ids;
    for (const auto& e : spell_fx_population_oracle::kRetiredExceptions) ids.push_back(e.spell);
    EXPECT_EQ(ids, (std::vector<int>{41, 54, 656, 458}));
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

// B1161 (#480): the pool rule RE-DERIVED against the record-driven field.
// A spell with a spellsnew.eff record draws its slots' FILE populations
// (B1149, applySlotEmitter) and the rule scales none of them: the oracle's
// pool (EQGraphicsDX9, per-emitter rings, ring full drops the rest of the
// frame's emission, 0x10061180) is a CAP, and fable applies that rule at its
// one pool -- new particles dropped while it is full, no count scaled
// (refactor/B1149/README.md sec. 4; the drop itself is exercised through the
// real worker by SpellLandingPool.TheLargestRecordFieldIsCappedNotScaled,
// tests/test_spell_landing_volume.cpp).  Here each record's field is
// re-derived from its EDD words, slot by slot -- burst + perTick x tps x
// min(window, L) (spell_fx_timing.h peakLive), Source with the caller duration
// cast + the cast-animation term, Target with none, every slot a level-60
// caster draws, a stage's slots summed (an upper bound: they need not peak
// together) -- and must reproduce B1149's census, which decoded the same
// words with its own reader (review/B1149/scripts/pool_census.py, the cast it
// uses beside each record): records 104 / 113 / 91 are the three largest
// fields of the 40 B1130 records, 80 (93/38) a small one.
TEST_P(SpellPoolRule, TheRecordFieldsAreTheFileCountsAndOnlyRecord104ExceedsAPool) {
    const std::string dir = eqClientPath();
    if (!std::ifstream(dir + "/spellsnew.edd").good()) GTEST_SKIP() << "spellsnew.edd not under " << dir;
    EQ::SpellsNewEffReader reader;
    ASSERT_TRUE(reader.load(dir)) << reader.loadError();
    auto stagePeak = [&](int rec, int stage, int callerMs) {
        float sum = 0.0f;
        for (const auto& d : reader.drawnSlots(rec, stage, 60)) {
            const EQ::SpellsNewEddTiming* w = reader.edd().timing(d.slot.emitter);
            if (w) sum += EQ::SpellFxSlotTimingFor(*w, callerMs).peakLive();
        }
        return sum;
    };
    const struct { int record; int castMs; float source, target; } kCensus[] = {
        {104, 1750, 3746.0f, 1168.5f},  // 253 Whirling Wind
        {113, 1500, 107.0f, 2417.12f},  // 290 Color Flux
        { 91, 3300, 1554.0f, 796.0f},   // 115 Dismiss Summoned
        { 80, 2700, 304.5f, 1302.5f},   // 38 Lightning Bolt / 93 Burst of Flame
    };
    for (const auto& c : kCensus) {
        const float src = stagePeak(c.record, EQ::kSpellsNewEffSource,
                                    EQ::SpellFxCallerDurationMs(EQ::SpellFxStage::Source, c.castMs, 0));
        const float tgt = stagePeak(c.record, EQ::kSpellsNewEffTarget,
                                    EQ::SpellFxCallerDurationMs(EQ::SpellFxStage::Target, c.castMs, 0));
        EXPECT_NEAR(src, c.source, 0.5f) << "record " << c.record << " Source";
        EXPECT_NEAR(tgt, c.target, 0.5f) << "record " << c.record << " Target";
        const float field = src + tgt;
        if (c.record == 104) {
            EXPECT_GT(field, 4096.0f) << "record 104's field exceeds the sbc pool (by " << field - 4096.0f << ")";
            EXPECT_EQ(field <= pool(), pool() >= 8192.0f)
                << "record 104's field (" << field << ") fits the 8192 pool, not the " << pool()
                << " one, which fills and drops";
        } else {
            EXPECT_LE(field, pool()) << "record " << c.record << "'s field (" << field << ") fits the " << pool()
                                     << " pool at the file's counts";
        }
    }
}

// B1161 (#480): record 0 is NO record (spells_us.txt field 145 = 0,
// spell_visual_vocabulary.h; SpellsNewEffReader::getRecord(0) is nullptr), and
// a record adds nothing to any style definition: the cast-animation term is
// the Source stage's caller duration (spell_fx_timing.h), the client passes
// the Target stage none (eqgame 0x47f5e1), and a record's landing is its
// slots.  So record 0, -1 and the spell's real record give the same settings
// and the same landing definitions -- the 210 field-145 = 0 spells (1106 Sear
// among them) land exactly as an unrecorded spell does.
TEST_F(SpellStyleOverrides, RecordZeroIsTheNoRecordPath) {
    const auto& cfg = SpellEffectsConfig::instance();
    const struct { uint32_t spell; int record; } kSpells[] = {
        {0, 104}, {1106, 104}, {253, 104}, {93, 80}, {290, 113}, {54, 82}};
    for (const auto& sp : kSpells) {
        for (const char* style : {"impact", "spellComplete", "castGlow", "castSpray"}) {
            const auto none = cfg.getStyleSettings(style, sp.spell, -1);
            for (int rec : {0, sp.record}) {
                const auto s = cfg.getStyleSettings(style, sp.spell, rec);
                EXPECT_FLOAT_EQ(s.emitterLifetime, none.emitterLifetime) << style << " " << sp.spell << " rec " << rec;
                EXPECT_EQ(s.hasEmitterLifetime, none.hasEmitterLifetime) << style << " " << sp.spell << " rec " << rec;
                EXPECT_FLOAT_EQ(s.spawnRate, none.spawnRate) << style << " " << sp.spell << " rec " << rec;
                EXPECT_FLOAT_EQ(s.sustainRate, none.sustainRate) << style << " " << sp.spell << " rec " << rec;
                EXPECT_EQ(s.burstCount, none.burstCount) << style << " " << sp.spell << " rec " << rec;
            }
        }
        const glm::vec4 col(1.0f);
        auto lastDeath = [](const SpellEffectDef& d) {
            const auto& ed = d.emitters.at(0);
            return ed.triggerDelay + ed.config.emitterLifetime + ed.config.lifetimeMax;
        };
        EXPECT_FLOAT_EQ(lastDeath(SpellPresets::Impact(col, sp.spell, 0)), lastDeath(SpellPresets::Impact(col, sp.spell, -1)))
            << sp.spell;
        EXPECT_FLOAT_EQ(lastDeath(SpellPresets::SpellComplete(col, sp.spell, 0)),
                        lastDeath(SpellPresets::SpellComplete(col, sp.spell, -1))) << sp.spell;
    }
    // The no-record landing ends where the style does: 1106's definition is
    // the style's (no override), triggerDelay + emitterLifetime + lifetimeMax.
    const auto style = cfg.getStyleSettings("impact", 0, -1);
    EXPECT_FLOAT_EQ(SpellPresets::Impact(glm::vec4(1.0f), 1106, 0).emitters.at(0).config.emitterLifetime,
                    style.emitterLifetime);
}

// Whirling Wind on the RECORD path (B1161 #480: its override is retired; the
// spell's record is spellsnew.eff 104 = survey row 35, tests/spell_anim_rows.h).
// The cast-animation term (B1145's 4.35 s, EQ::kSpellFxCastAnimationSeconds;
// B1148 reads it per caster from the CastingAnim track) is the client's
// SOURCE duration: eqgame passes a Source stage cast_ms + the track
// (0x49b977 / 0x47ddd4) and a Target stage none (0x47f5e1).  Computed from
// record 104's EDD words: the Source emits to OP_BeginCast + 1.75 + 4.35 and
// its last particle dies Act+6.10 -- the oracle's caster-box Source end for
// this row is Act+6.073 (B1145, tests/spell_fx_timing_oracle.h row 35), inside
// B1145's 0.5 s.  The Target stage is the file's fixed 5.0 s run with lives
// up to 3.5 s: last death 8.5 s after the stage starts (B1145's
// crosscheck.tsv target_end_l60).  The landing's VISIBLE end on the harness
// is the EDD fade field's (refactor/B1161/README.md sec. 3 reports it against
// the B1118 oracle's 6.52); no style word reaches the record path.
TEST_F(SpellStyleOverrides, WhirlingWindsRecordPathCarriesTheTermOnItsSource) {
    const std::string dir = eqClientPath();
    if (!std::ifstream(dir + "/spellsnew.edd").good()) GTEST_SKIP() << "spellsnew.edd not under " << dir;
    EQ::SpellsNewEffReader reader;
    ASSERT_TRUE(reader.load(dir)) << reader.loadError();
    constexpr int kRecord = 104, kCastMs = 1750;
    auto stageEnd = [&](int stage, int callerMs, int* n) {
        float end = -1.0f;
        *n = 0;
        for (const auto& d : reader.drawnSlots(kRecord, stage, 60)) {
            const EQ::SpellsNewEddTiming* w = reader.edd().timing(d.slot.emitter);
            if (!w) continue;
            end = std::max(end, EQ::SpellFxSlotTimingFor(*w, callerMs).end());
            ++*n;
        }
        return end;
    };
    const int32_t srcCaller = EQ::SpellFxCallerDurationMs(EQ::SpellFxStage::Source, kCastMs, 0);
    EXPECT_EQ(srcCaller, kCastMs + static_cast<int32_t>(EQ::kSpellFxCastAnimationSeconds * 1000.0f + 0.5f));
    int n = 0;
    const float sourcePastAction = stageEnd(EQ::kSpellsNewEffSource, srcCaller, &n) - kCastMs * 0.001f;
    EXPECT_EQ(n, 4);
    EXPECT_NEAR(sourcePastAction, 6.10f, 1e-4f) << "1.75 + 4.35 + the longest Source life 1.75, less the cast";
    EXPECT_NEAR(sourcePastAction, 6.073f, 0.5f) << "the oracle's Source end for row 35 (B1145)";
    EXPECT_EQ(EQ::SpellFxCallerDurationMs(EQ::SpellFxStage::Target, kCastMs, 0), 0) << "no duration for a Target";
    EXPECT_NEAR(stageEnd(EQ::kSpellsNewEffTarget, 0, &n), 8.5f, 1e-4f) << "B1145 crosscheck.tsv row 35";
    EXPECT_EQ(n, 3);
    // The style definition holds no record word: 253's landing halves are the
    // style's (its override is gone), whatever record is passed.
    const auto& cfg = SpellEffectsConfig::instance();
    for (const char* half : {"impact", "spellComplete"})
        EXPECT_FLOAT_EQ(cfg.getStyleSettings(half, 253, kRecord).emitterLifetime,
                        cfg.getStyleSettings(half, 0, -1).emitterLifetime) << half;
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
//     253    0.351   0.4315         0.093   0.079-0.107   0.096  0.095  (retired B1161)
//     458    0.3615  0.4455         0.101   0.086-0.116   0.101  0.100
//   Source   castGlow 0.659 (style: 93, 38), 54 0.216, 656 0.225, 253 1.03,
//            458 0.4895; castSpray 41 1.0035 (style 1.356 untouched: 41 is
//            its only measured spell).  Oracle 93 0.084, 38 0.074, 54 0.027,
//            656 0.028, 41 0.113, 253 0.178, 458 0.043; cal c2 0.080 0.079
//            0.027 0.028 0.112 0.150 0.045; gate 0.078 0.077
//            0.027 0.028 0.108 0.164 0.046 -- all 14 in band (the 253 row
//            retired with its override by B1161: the record 104 EDD field
//            draws it at the file's sprite sizes now).  Coverage at the gate
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
// and 656 (record 23) share one swirl, an unmeasured spell is the style
// (an invented override is the guess Will's ruling forbids). B1150 (#470):
// the measured per-spell SHAPES (41/253/458 and the 54/656 cloud boxes) are
// retired with the rows (tests/spell_fx_shape_oracle.h kOverrideShape) -- the
// records' spellsnew.edd +0xA8 shapes draw them (spell_spawn_shape.h) -- so
// every override falls back to its style's shape; the motions and sizes the
// rows still carry are asserted as before.
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
    // B1150: the retired rows fall back to the style's shape (their measured
    // values are the kOverrideShape bands); the motions/sizes they kept stay.
    EXPECT_EQ(castSprayFor(41).spawnShape, castSprayFor(0).spawnShape);
    EXPECT_EQ(castSprayFor(41).spawnExtents, castSprayFor(0).spawnExtents);
    EXPECT_EQ(castGlowFor(458).spawnShape, style.spawnShape);
    EXPECT_EQ(castGlowFor(458).spawnExtents, style.spawnExtents);
    // B1161 (#480): the 253 override is retired, so 253's Source is the style
    // too (its cloud was B1122's last non-record Source; its EDD slots draw
    // the record 104 field at the file's counts, B1149).
    EXPECT_EQ(castGlowFor(253).spawnShape, style.spawnShape);
    EXPECT_EQ(castGlowFor(253).spawnExtents, style.spawnExtents);
    EXPECT_FLOAT_EQ(startMid(castGlowFor(253)), startMid(style))
        << "no 253 size override left";
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
                         Row{458, 1.0545f, 1.2995f}}) {
        EXPECT_NEAR(startMid(impact(r.id)), r.impact, 0.0006f) << r.id;
        EXPECT_NEAR(startMid(complete(r.id)), r.complete, 0.0006f) << r.id;
    }
    struct Src { uint32_t id; float mid; };
    for (const Src& r : {Src{0, 0.659f}, Src{93, 0.659f}, Src{38, 0.659f}, Src{54, 0.3435f},
                         Src{656, 0.3715f}, Src{458, 1.3165f}}) {
        EXPECT_NEAR(startMid(castGlowFor(r.id)), r.mid, 0.0006f) << r.id;
    }
    EXPECT_NEAR(startMid(castSprayFor(41)), 1.0035f, 0.0006f);
    EXPECT_NEAR(startMid(castSprayFor(0)), 1.356f, 0.0006f) << "the castSpray style is unmeasured";
    // 93 IS record 0: no size override of its own.
    EXPECT_FLOAT_EQ(startMid(impact(93)), startMid(impact(0)));
    EXPECT_FLOAT_EQ(startMid(castGlowFor(93)), startMid(castGlowFor(0)));
    // B1161 (#480): the 253 override (landing 0.496/0.6105, Source 1.8335,
    // B1123/B1127's ROI fits) is retired: 253's style words are the style's
    // own again, and its record 104 field draws the file's EDD sprite sizes.
    EXPECT_FLOAT_EQ(startMid(impact(253)), startMid(impact(0)));
    EXPECT_FLOAT_EQ(startMid(complete(253)), startMid(complete(0)));
    EXPECT_FLOAT_EQ(startMid(castGlowFor(253)), startMid(castGlowFor(0)));
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
