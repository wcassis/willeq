// T441 -- board B398 (where a spell's colour comes from) and B397 (the
// per-spell style overrides).
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
//   253 keeps the pool-capped field; its landing sprites are the style x1.12,
//       toward the oracle's ROI p90 0.093 (B1120 sec. 7) but held inside
//       B1119's band (0.059 +-15 %, ROI <= 0.068).

#include <gtest/gtest.h>

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

// Alpha-weighted mean RGB over every frame of every sprite an emitter's
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
        for (int frame = 0; frame < SpellSpriteAtlas::kFramesPerSprite; ++frame) {
            int cx = 0, cy = 0;
            SpellSpriteAtlas::cellOrigin(SpellSpriteAtlas::cellFor(sprite, frame), cx, cy);
            for (int y = cy; y < cy + SpellSpriteAtlas::kCellSize; ++y) {
                for (int x = cx; x < cx + SpellSpriteAtlas::kCellSize; ++x) {
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
    void SetUp() override {
        if (!SpellEffectsConfig::instance().load(repoRoot() + "/config/spell_effects.json")) {
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

// B1123: the pool bound the landings are held to -- the largest live peak that
// fits beside one 253 field (the steady field is the largest single landing).
static float PoolBoundBesideAField() {
    const float pool = static_cast<float>(SpellEffectsConfig::instance().getGlobal().maxParticles);
    const EmitterConfig i = SpellPresets::Impact(glm::vec4(1.0f), 253).emitters.at(0).config;
    const EmitterConfig c = SpellPresets::SpellComplete(glm::vec4(1.0f), 253).emitters.at(0).config;
    return pool - (i.spawnRate * 0.5f * (i.lifetimeMin + i.lifetimeMax) +
                   c.spawnRate * 0.5f * (c.lifetimeMin + c.lifetimeMax));
}

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

// B1123 (RED on the B1122 config: the style peaked at 1747, 41 at 1562).  The
// oracle's landings need more sprites than the pool holds (the B1123 sweep), so
// every measured fed landing sits AT the pool bound: within 2 % of
// 4096 - (253's field), never over it.
TEST_F(SpellStyleOverrides, EveryFedLandingTakesThePoolBoundBesideAField) {
    const float bound = PoolBoundBesideAField();
    for (uint32_t id : {0u, 93u, 41u}) {
        const float l = live(id);
        EXPECT_LE(l, bound) << "spell " << id << " peaks at " << l << " live; one 253 field leaves " << bound;
        EXPECT_GE(l, 0.98f * bound) << "spell " << id << " peaks at " << l
                                    << " live; the oracle needs more than the pool holds, so the bound ("
                                    << bound << ") is the target";
    }
}

// Whirling Wind is a continuous field, not a 0.6 s burst -- and since B1120
// a FRAME-FILLING one (B1118: oracle 253 Landing coverage 7.28 caster
// heights^2; T422's 25-36 trickle measured max 0.033).  Its steady population
// is bounded by the pool: one 253 field plus one record-0 landing must fit.
TEST_F(SpellStyleOverrides, WhirlingWindIsASteadyFrameFillingField) {
    const float total = steady(impact(253)) + steady(complete(253));
    const float largest = live(93);
    const float pool = static_cast<float>(SpellEffectsConfig::instance().getGlobal().maxParticles);
    EXPECT_GE(total, 1500.0f) << "a field, not a trickle; fable holds " << total;
    EXPECT_LE(total + largest, pool)
        << "253's field (" << total << ") plus a record-0 landing (" << largest
        << ") must fit the pool (" << pool << ")";
}

// B1119 (#439) retired T422's 253 size override: B1118's whole-frame oracle
// Landing p90 (0.059 caster heights) landed on the B1119 style.  B1123: on the
// ROI instrument the oracle's 253 Landing sprite is 0.093 (B1120 sec. 7) and
// fable's style read 0.054-0.057, so 253's landing sprites are raised -- but only
// to B1119's band top (0.068): x1.12 of the style (lane 5: x1.12 read 0.067,
// x1.2 read 0.072).  RED on the B1122 config (x1.0).
TEST_F(SpellStyleOverrides, WhirlingWindSpritesAreTheStyleRaisedToTheB1119BandTop) {
    for (const auto& pair : {std::make_pair(impact(253), impact(0)),
                             std::make_pair(complete(253), complete(0))}) {
        EXPECT_NEAR(pair.first.sizeStartMin / pair.second.sizeStartMin, 1.12f, 0.005f);
        EXPECT_NEAR(pair.first.sizeStartMax / pair.second.sizeStartMax, 1.12f, 0.005f);
        EXPECT_NEAR(pair.first.sizeEndMin / pair.second.sizeEndMin, 1.12f, 0.005f);
        EXPECT_NEAR(pair.first.sizeEndMax / pair.second.sizeEndMax, 1.12f, 0.005f);
    }
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
TEST_F(SpellStyleOverrides, ThePoolStillHoldsTwoOfTheLargestLanding) {
    float largest = 0.0f;
    for (uint32_t id : {0u, 93u, 38u, 54u, 656u, 41u, 458u}) largest = std::max(largest, live(id));
    EXPECT_GE(static_cast<float>(SpellEffectsConfig::instance().getGlobal().maxParticles),
              2.0f * largest)
        << "largest landing " << largest << ", pool "
        << SpellEffectsConfig::instance().getGlobal().maxParticles;
}

}  // namespace

// ---------------------------------------------------------------------------
// B1119 (#439) -- sprite SIZE per stage, against B1118's real-frame-rate oracle
// p90 sprite diameters (caster heights).  Calibrated on a sprite-separable
// lane-5 capture (refactor/B1119/README.md): an isolated sprite's lit p90
// diameter is linear in its size, ~0.028 caster heights per x1 of the T422
// landing sizes and ~0.0071 per x1 of the pre-B1119 castGlow sizes.
//   Landing  style x2.8 (93 656 458 253); 38 x2.07, 54 x3.3, 41 x4.23
//   Source   castGlow x10.3 (93 38 253); 54/656 x4.5, 458 x5.5; castSpray x22.6 (41)
// Every per-spell size override below is forced by the oracle table: the
// style cannot hold 0.058 (38) and 0.074 (93) on the same record-0 sprites,
// nor 0.031 (656) and 0.075 (253) at the Source.
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
    // The B1119 Source SIZES are untouched (B1119SourceSizesFollowTheOracleTable).
    EXPECT_FLOAT_EQ(startMid(castGlowFor(253)), startMid(style));
}

TEST_F(SpellStyleOverrides, B1119LandingStylesAreTwoPointEightTimesT422) {
    EXPECT_NEAR(startMid(impact(0)) / 0.1025f, 2.8f, 0.03f);    // T422 impact 0.08-0.125
    EXPECT_NEAR(startMid(complete(0)) / 0.126f, 2.8f, 0.03f);   // T422 spellComplete 0.095-0.157
}

TEST_F(SpellStyleOverrides, B1119LandingOverridesFollowTheOracleTable) {
    // 38 0.058 < 93 0.074 < 54 0.064-at-a-smaller-texture-response < 41 0.110.
    const float style = startMid(impact(0));
    EXPECT_NEAR(startMid(impact(38)) / style, 2.07f / 2.8f, 0.02f);
    EXPECT_NEAR(startMid(impact(54)) / style, 3.3f / 2.8f, 0.02f);
    EXPECT_NEAR(startMid(impact(41)) / style, 4.23f / 2.8f, 0.02f);
    EXPECT_NEAR(startMid(complete(41)) / startMid(complete(0)), 4.23f / 2.8f, 0.02f);
    EXPECT_FLOAT_EQ(startMid(impact(93)), style) << "93 is the style's own band";
    EXPECT_FLOAT_EQ(startMid(impact(458)), style) << "458 is the style's own band";
}

TEST_F(SpellStyleOverrides, B1119SourceSizesFollowTheOracleTable) {
    const float glow = startMid(castGlowFor(0));
    EXPECT_NEAR(glow / 0.10f, 10.3f, 0.1f);                     // pre-B1119 castGlow 0.08-0.12
    EXPECT_NEAR(startMid(castSprayFor(0)) / 0.06f, 22.6f, 0.2f); // pre-B1119 castSpray 0.04-0.08
    EXPECT_FLOAT_EQ(startMid(castGlowFor(93)), glow);
    EXPECT_FLOAT_EQ(startMid(castGlowFor(38)), glow);
    EXPECT_FLOAT_EQ(startMid(castGlowFor(253)), glow);
    EXPECT_NEAR(startMid(castGlowFor(54)) / glow, 4.5f / 10.3f, 0.01f);
    EXPECT_NEAR(startMid(castGlowFor(656)) / glow, 4.5f / 10.3f, 0.01f);
    EXPECT_NEAR(startMid(castGlowFor(458)) / glow, 5.5f / 10.3f, 0.01f);
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
