// T422 (board B226 / B356 / B370): the impact and spellComplete burst
// parameters, pinned against the OFFICIAL CLIENT measured live.
//
// THE ORACLE, and how it was taken.  `review/T422/scripts/t422_oracle.sh` ran
// the official Titanium client (Oraclealpha, qeynos2, `#goto -74 428 3.751
// 160`, `#weather 0` with `zone.rain_chance*` zeroed, `#set time 11 0`,
// third-person, nameplates and corpses off) and self-cast Frost Bolt (54),
// Burst of Flame (93), Weaken (41) and Whirling Wind (253) with a 20 s quiet
// window before each.  The x11grab capture ran at 30 fps -- five to six times
// the client's own render rate, so every distinct render is captured exactly
// once instead of aliased, which is what T389's 10 fps capture could not do.
// `review/T422/scripts/renderrate.py` MEASURED that render rate from
// consecutive-frame diffs: **4.4 to 5.8 renders/s**, median gap 6 grab frames.
// `review/T422/scripts/burst.py` then background-subtracted a temporal median
// of the pre-cast renders and counted 8-connected components of the residual,
// colour-gated to the spell's dominant channel so that nameplate text and a
// shifted character silhouette cannot be counted as particles.
//
// The numbers, over the crop 880x510+5+25 of a 1024x768 frame in which the
// caster stands about 135 px tall (`review/T422/logs/oracle_burst_*.tsv`):
//
//   spell                    peak sprites   median px   duration   extent px
//   93 Burst of Flame            715         4.8-6.9      2.33 s    830x435
//   41 Weaken                    561         4.4-6.7      2.6  s    880x505
//   253 Whirling Wind         25-36 steady   8.0-10.5     5.6  s    840x360
//   54 Frost Bolt          ribbon, merged    3.4-9.9      2.7  s    540x250
//
// WHAT THAT SETTLES, PARAMETER BY PARAMETER.
//
//  * COUNT.  fable shipped `spellComplete` burstCount 45 + `impact`
//    burstCount 36 = 81 sprites for one landing, against 561-715.  The two
//    fable styles are the caster stage and the target stage of one landing,
//    which for the oracle's self-cast are superimposed, so they are compared
//    as a sum.  Scaling 45:36 up to ~648 gives 360 and 288.
//
//  * EMISSION SHAPE.  The oracle does NOT fire a one-shot burst.  Its
//    population rises MONOTONICALLY with no deaths for 1.96 s (spell 93:
//    11 -> 22 -> 188 -> 324 -> 502 -> 578 -> 637 -> 705) and for 1.46 s
//    (spell 41: 204 -> 294 -> 355 -> 461 -> 520 -> 561), then collapses.  So
//    it emits continuously over most of the effect.  fable's 0.1 s
//    `emitterLifetime` with a `burstCount` is the wrong shape, and the fix is
//    a `spawnRate` over an `emitterLifetime` -- both already in the config.
//
//  * LIFETIME.  A population that rises for 1.46-1.96 s with NO deaths bounds
//    the particle lifetime from below by that rise: L >= 1.96 s (93),
//    L >= 1.46 s (41).  Whirling Wind gives it from the other side: a steady
//    population of ~30 fed at ~22 sprites/s is L ~ 1.4 s.  1.6-2.0 s is the
//    band all three admit.  fable shipped 0.4-0.8 s.
//
//  * DURATION.  [Superseded by B1121 (#441).]  T422 read a 2.33-2.6 s total
//    and set a 0.6 s window.  B1118 measured the landing at real frame rate:
//    it ENDS at Act+4.85-6.52 s, and its base stays lit to the end (FED, not
//    long-lived sprites; refactor/B1121/README.md sec. 2).  So the 1.6-2.0 s
//    band is a per-PARTICLE life only; the 0.6 s window is now the PEAK phase
//    (spawnWindow) and a SUSTAIN phase feeds the landing to its measured end.
//
//  * ANIMATION RATE (B370).  T420 tied one 16-frame sprite playthrough to a
//    particle's lifetime, so the rate is 16/lifetime and is NOT a free knob.
//    0.4-0.8 s was 20-40 cells/s; 1.6-2.0 s is 8.0-10.0 cells/s.
//    ** This does NOT reach T420's speculative "below ~3 cells/s" bound, and
//    that is deliberate. **  T420 could not measure a rate at all (its 10 fps
//    capture held ~5 renders/s, Nyquist 2.5 cells/s) and inferred the bound
//    from the MODAL orientation class of a population of MIXED-AGE stars
//    holding across four renders -- a statistic over a mixed-age population is
//    not a clock, and T420 itself rated it low confidence and refuted the
//    effect-wide clock the argument needs.  T422's 4.4-5.8 renders/s is still
//    too low to measure cells/s directly (Nyquist 2.2-2.9), so the rate here
//    is DERIVED from a directly measured lifetime rather than measured.  Said
//    out loud so the next unit does not read it as a measurement.
//
//  * SIZE.  [Superseded by B1119 (#439): the numbers below are T422's, kept as
//    history; B1118 re-measured at real frame rate and B1119 re-sized both
//    styles x2.8 -- see SpriteSizeAndExpandSpeedAreTheMeasuredOnes.]
//    Probed live, not argued.  With the count and lifetime already
//    fixed, fable's Weaken burst measured a MEDIAN SPRITE DIAMETER of
//    3.2-3.7 px through the same pipeline that read 4.4-6.7 px off the
//    oracle's -- 1.57x too small -- so every size in both styles is scaled by
//    1.57 (impact 0.05/0.08/0.10/0.15 -> 0.08/0.125/0.157/0.235,
//    spellComplete 0.06/0.10/0.12/0.18 -> 0.095/0.157/0.19/0.28).  A desk
//    estimate off POINT_SPRITE_VS's `worldSize * screenHeight / dist` had
//    predicted 4-6 px and would have left this alone; the measurement is
//    what moved it.
//
//  * EXTENT.  [Superseded by B1120 (#440): B1118 measured the oracle's
//    landing as a VOLUME -- columns, domes, swirls, a star field -- and the
//    830 px below was crop-limited.  B1120 reshaped both styles; the shapes are
//    pinned in tests/test_spell_landing_volume.cpp.  The T422 text is history.]
//    EXTENT was NOT changed by T422, and had its probe.  The oracle's burst spreads
//    830 px across at ~135 px per character height, i.e. ~8.3 world units in
//    ~2 s = ~4.2 units/s, against fable's shipped `expandSpeed` 4.0.  It was
//    left alone BECAUSE it measured right: the whole extent error was the
//    0.4-0.8 s lifetime cutting the travel short, not the speed.  B226's
//    "two orders of magnitude smaller" conflated three different things --
//    the per-sprite size (1.6x out), the population (8x out) and the
//    whole-burst extent (an effect of the lifetime, not a knob of its own).
//
// WHAT IS STILL NOT MATCHED, recorded rather than hidden: the oracle's
// population RAMPS (its emission rate itself rises) and then is cut off in
// 0.37 s; fable's rectangular emission plus a lifetime tail cannot express
// that.  Boarded as B376.

#include <gtest/gtest.h>

#include <string>

#if defined(EQT_HAS_GRAPHICS) && defined(EQT_HAS_GLES2)
#include "client/graphics/environment/spell_effects_config.h"
#include "client/graphics/environment/spell_particle_types.h"
#include "client/graphics/environment/unified_particle.h"
#include "client/graphics/simulation_worker.h"
#include <glm/glm.hpp>
#include <unordered_set>
#include "spell_landing_population.h"
#include "spell_pool.h"
#endif

namespace {

#if defined(EQT_HAS_GRAPHICS) && defined(EQT_HAS_GLES2)

using EQT::Graphics::Environment::SpellEffectsConfig;
namespace SpellPresets = EQT::Graphics::Environment::SpellPresets;

class SpellBurstOracleMatch : public ::testing::Test {
protected:
    // B1123: at this build's platform pool (spell_pool.h); the pool-accounting
    // cases below run at every platform's pool (SpellBurstPool).
    void SetUp() override {
        const int pool = spell_pool::Shipped(spell_pool::ThisBuildPlatform());
        ASSERT_GT(pool, 0) << "config/constrained_presets.json names no spellParticlePool for this platform";
        if (!spell_pool::LoadAt(pool)) {
            GTEST_SKIP() << "no config/spell_effects.json";
        }
    }

    // The EmitterConfig the shipped config produces for a style, read through
    // the same preset function production calls.
    static EQT::Graphics::Environment::EmitterConfig Impact() {
        auto def = SpellPresets::Impact(glm::vec4(1.0f, 0.47f, 0.2f, 1.0f), 93);
        EXPECT_EQ(def.emitters.size(), 1u);
        return def.emitters.at(0).config;
    }
    static EQT::Graphics::Environment::EmitterConfig Complete() {
        auto def = SpellPresets::SpellComplete(glm::vec4(1.0f, 0.47f, 0.2f, 1.0f), 93);
        EXPECT_EQ(def.emitters.size(), 1u);
        return def.emitters.at(0).config;
    }

    // B1121: one landing's expected peak LIVE count (both halves, peak +
    // sustain phases; tests/spell_landing_population.h), and the peak
    // phase's cohort -- what T422 compared with its blob count.
    static float PeakLive() { return spell_landing_population::PeakLive({Impact(), Complete()}); }
    static float PeakCohort(const EQT::Graphics::Environment::EmitterConfig& c) {
        return spell_landing_population::PeakCohort(c);
    }
    static float MeanLifetime(const EQT::Graphics::Environment::EmitterConfig& c) {
        return 0.5f * (c.lifetimeMin + c.lifetimeMax);
    }
};

// The oracle emits for ~1.5-2.0 s.  A one-shot burst puts every sprite on
// screen in one frame and is a different picture entirely.
TEST_F(SpellBurstOracleMatch, ImpactEmitsOverAWindowRatherThanInOneFrame) {
    const auto c = Impact();
    EXPECT_EQ(c.burstCount, 0)
        << "the oracle's population rises over 1.96 s with no deaths; a "
           "burstCount puts the whole effect on screen in a single frame";
    EXPECT_GT(c.spawnRate, 0.0f);
    // B1121: T422's 0.5-0.8 s window is the PEAK phase now (spawnWindow).
    EXPECT_GE(c.spawnWindow, 0.5f);
    EXPECT_LE(c.spawnWindow, 0.8f);
}

TEST_F(SpellBurstOracleMatch, SpellCompleteEmitsOverAWindowRatherThanInOneFrame) {
    const auto c = Complete();
    EXPECT_EQ(c.burstCount, 0);
    EXPECT_GT(c.spawnRate, 0.0f);
    EXPECT_GE(c.spawnWindow, 0.5f);
    EXPECT_LE(c.spawnWindow, 0.8f);
}

// The headline B226 number: one landing must put the oracle's order of
// sprites on screen, not 81.  T422 measured 561 (Weaken) to 715 (Burst of
// Flame) 8-connected blobs -- a LOWER bound (B1118 sec. 3: overlapping
// sprites merge).  B1120 (#440) filled the oracle's volume (a column >= 2
// caster heights + a spark shell ~3.4 wide, B1118 coverage 5.85 caster
// heights^2 for 93) and the population rose with it; the ceiling is the pool:
// two overlapping record-0 landings must fit `global.maxParticles` (below).
// B1123: run at every platform's shipped pool (spell_pool.h).
class SpellBurstPool : public SpellBurstOracleMatch,
                       public ::testing::WithParamInterface<const char*> {
protected:
    void SetUp() override {
        const int pool = spell_pool::Shipped(GetParam());
        ASSERT_GT(pool, 0) << "config/constrained_presets.json names no spellParticlePool for this platform";
        if (!spell_pool::LoadAt(pool)) {
            GTEST_SKIP() << "no config/spell_effects.json";
        }
    }
    static float Pool() {
        return static_cast<float>(SpellEffectsConfig::instance().getGlobal().maxParticles);
    }
};
INSTANTIATE_TEST_SUITE_P(Platforms, SpellBurstPool, ::testing::ValuesIn(spell_pool::kPlatforms));

TEST_P(SpellBurstPool, OneLandingReachesTheOraclesPeakSpriteCount) {
    const float total = PeakCohort(Impact()) + PeakCohort(Complete());
    EXPECT_GE(total, 715.0f)
        << "one landing paints " << total << " sprites; T422 counted at least "
           "715 blobs on the official client (Burst of Flame)";
    // B1123: the pool rule puts the record-0 landing plus its own held Source
    // AT half the pool (the oracle needs more sprites, B1123 sweep), so the
    // landing is pinned just under the bound. B1131: record 0's Source holds
    // 4 s past OP_Action with another target (B1130), so its ~80 live sprites
    // (80/s x <= 1.2 s) sit beside the landing's peak and the landing yields
    // them (the exact rule is EveryLandingPeaksAtTheRuleForThisPool).
    const float live = PeakLive();
    EXPECT_LE(live, 0.5f * Pool() * 1.001f)
        << "one landing holds up to " << live << " live sprites; two must fit the pool " << Pool();
    EXPECT_GE(live, (0.5f * Pool() - 96.0f) * 0.99f)
        << "the record-0 landing (" << live << ") is not at half the pool " << Pool()
        << ": the pool rule (SpellEffectsConfig::applyPool) did not run";
}

// The lifetime is bounded BELOW by the oracle's death-free rise (1.46 s for
// Weaken, 1.96 s for Burst of Flame).  It is a per-PARTICLE life (the sheet
// plays once over it, below), NOT the landing's duration (B1121).
TEST_F(SpellBurstOracleMatch, ParticleLifetimeMatchesTheOraclesDeathFreeRise) {
    for (const auto& c : {Impact(), Complete()}) {
        EXPECT_GE(c.lifetimeMin, 1.4f)
            << "the oracle's population rises for 1.46-1.96 s without a single "
               "death, so no particle may die before 1.4 s";
        EXPECT_LE(c.lifetimeMax, 2.4f)
            << "B1121 lengthened the landing by FEEDING it; a longer life would "
               "slow the sheet animation instead (16/lifetime)";
    }
}

// B1121 (#441).  The landing's END after OP_Action, per measured spell, is the
// official client's (B1118 sec. 4, fx_measure.py `LANDING ... end`).  The end is
// MEASURED on a lane-5 capture, never summed: `review/B1121/scripts/
// b1120_measure.sh <leg> <out>` (refactor/B1121/README.md).  What a config can
// pin is the sim's last possible death, triggerDelay + emitterLifetime +
// lifetimeMax: the last lit frame falls 0.2-0.6 s before it once the sustain
// thins below the visibility threshold, so it must sit in [end, end + 0.8].
// B1161 (#480): each spell is built with the record the CLIENT gives it
// (spells_us.txt field 145: 93/38 -> 80, 54/656 -> 82, 41 -> 89, 458 -> 121),
// not -1: a style definition carries no per-record word (the cast-animation
// term is the Source stage's caller duration, spell_fx_timing.h, and eqgame
// passes a Target stage none, 0x47f5e1), so the record changes nothing here
// and these are the definitions the landing draws when no spellsnew.eff slot
// is drawn. 253 (record 104) left this table with its override (B1161): its
// definition is now the style's (last death 0.33 + 2.85 + 2.0 = 5.18 s), and
// its landing is drawn from record 104's slots, whose timing is the file's
// (SpellStyleOverrides.WhirlingWindsRecordPathCarriesTheTermOnItsSource pins that path;
// refactor/B1161/README.md sec. 3 reports its end against the oracle's 6.52).
TEST_F(SpellBurstOracleMatch, EveryMeasuredLandingEndsWhereTheOraclesDoes) {
    const struct { uint32_t id; float oracleEnd; int record; } kEnds[] = {
        {93, 4.87f, 80}, {38, 4.85f, 80}, {54, 5.10f, 82}, {656, 5.00f, 82},
        {41, 6.22f, 89}, {458, 4.94f, 121}};
    const glm::vec4 col(1.0f);
    for (const auto& e : kEnds) {
        for (const auto& def : {SpellPresets::Impact(col, e.id, e.record), SpellPresets::SpellComplete(col, e.id, e.record)}) {
            const auto& ed = def.emitters.at(0);
            const float lastDeath = ed.triggerDelay + ed.config.emitterLifetime + ed.config.lifetimeMax;
            EXPECT_GE(lastDeath, e.oracleEnd) << "spell " << e.id << " record " << e.record << " " << def.name;
            EXPECT_LE(lastDeath, e.oracleEnd + 0.8f) << "spell " << e.id << " record " << e.record << " " << def.name;
        }
    }
}

// The landing is FED to its end: the sustain phase runs past the peak window
// up to emitterLifetime, so the column's base is still being spawned late.
TEST_F(SpellBurstOracleMatch, TheLandingIsFedPastItsPeakWindow) {
    for (const auto& c : {Impact(), Complete()}) {
        EXPECT_GT(c.sustainRate, 0.0f);
        EXPECT_GE(c.sustainFrom, c.spawnWindow);
        EXPECT_GT(c.emitterLifetime, c.sustainFrom + 1.0f);
    }
}

// B370.  The sprite sheet plays through exactly once over a particle's
// lifetime (T420), so the rate is 16/lifetime and follows from the lifetime
// above -- there is no second knob, and this test exists so that a future
// lifetime edit cannot silently move the animation rate without being read.
TEST_F(SpellBurstOracleMatch, SheetPlaybackRateFollowsFromTheMeasuredLifetime) {
    for (const auto& c : {Impact(), Complete()}) {
        const float fastest = 16.0f / c.lifetimeMin;
        const float slowest = 16.0f / c.lifetimeMax;
        EXPECT_LE(fastest, 11.5f)
            << "16/lifetimeMin = " << fastest << " cells/s; the shipped "
               "0.4 s lifetime made this 40 cells/s";
        EXPECT_GE(slowest, 6.0f)
            << "16/lifetimeMax = " << slowest << " cells/s";
    }
}

// The sizes are B1119's (#439): T422's 1.57x-scaled sizes (0.08/0.125/0.157/
// 0.235 here) were probed on an llvmpipe-era 4-6 fps capture and B1118 measured
// them 2-4x too small at real frame rate (fable Landing p90 sprite diameter
// 0.022-0.040 caster heights against the oracle's 0.056-0.110).  B1119 scaled
// both landing styles x2.8, calibrated on a sprite-separable ThinkPad lane-5
// capture (refactor/B1119/README.md).  The expand speed is the one that
// MEASURED RIGHT and was deliberately not touched.  Pin both so a later unit
// does not move either off a desk argument.  (B1120 replaced the expand
// speed with the measured spark-shell speed; see the last assertion.)
// B1123 sizes-first (Will, #443 note 7118): every sprite size matches the
// oracle's caster-ROI p90 (review/B1119/scripts/fx_sprite_roi.py against
// review/B1120/measure_oracle/roi_oracle.txt, band +-15 %) FIRST, and
// coverage follows from the pool.  Record 0 is 93's own: the B1119 sizes read
// 0.094 on the lane-5 8192 gate (band 0.064-0.086), so the landing style is
// x0.69 of them (cal 0.075 vs the oracle's 0.075; refactor/B1123/README.md
// "Sizes-first leg").  Coverage is reported there, never bought with size.
TEST_F(SpellBurstOracleMatch, SpriteSizeAndExpandSpeedAreTheMeasuredOnes) {
    const auto c = Impact();
    EXPECT_FLOAT_EQ(c.sizeStartMin, 0.155f);
    EXPECT_FLOAT_EQ(c.sizeStartMax, 0.241f);
    EXPECT_FLOAT_EQ(c.sizeEndMin, 0.304f);
    EXPECT_FLOAT_EQ(c.sizeEndMax, 0.454f);
    const auto sc = Complete();
    EXPECT_FLOAT_EQ(sc.sizeStartMin, 0.184f);
    EXPECT_FLOAT_EQ(sc.sizeStartMax, 0.304f);
    EXPECT_FLOAT_EQ(sc.sizeEndMin, 0.367f);
    EXPECT_FLOAT_EQ(sc.sizeEndMax, 0.541f);
    // B1120 (#440): the impact style is no longer RADIAL_EXPAND at 4.0; it is
    // the oracle's 93/38 spark shell, a BURST at 1.6 caster heights/s
    // horizontal (6.5-unit caster, refactor/B1120/README.md sec. 3-4).
    EXPECT_EQ(c.motionType, EQT::Graphics::Environment::MotionType::BURST);
    EXPECT_NEAR(c.velocityBase.x, 1.6f * 6.5f, 0.05f);
}

// B1122 (#442): the Source cloud's POPULATION. B1118 measured the official
// client's Source peak coverage (caster heights^2) at 93 0.38, 38 0.38, 54 0.48,
// 656 0.63, 41 1.23, 253 3.92, 458 0.08; the rates below are the ones whose
// live count measured >= 80 % of each on the lane-5 gate
// (`review/B1122/scripts/source_all.py fable <measure_root> <out>`, the
// PEAK line; refactor/B1122/README.md sec. 5). They are per-record, like the
// landing's (54/656 share record 23), and they run from the Source delay
// (BeginCast + 0.25 s) to OP_Action -- removeCastGlow() then frees them.
TEST_F(SpellBurstOracleMatch, TheSourceCloudRatesAreTheCalibratedOnes) {
    auto glow = [](uint32_t id) { return SpellPresets::CastGlow(glm::vec4(1.0f), id).emitters.at(0); };
    auto spray = [](uint32_t id) { return SpellPresets::CastSpray(glm::vec4(1.0f), id).emitters.at(0); };
    EXPECT_FLOAT_EQ(glow(93).config.spawnRate, 80.0f);    // record 0 = the style
    EXPECT_FLOAT_EQ(glow(38).config.spawnRate, 80.0f);
    EXPECT_FLOAT_EQ(glow(54).config.spawnRate, 1050.0f);  // record 23
    EXPECT_FLOAT_EQ(glow(656).config.spawnRate, 1050.0f);
    EXPECT_FLOAT_EQ(spray(41).config.spawnRate, 330.0f);  // record 20
    // B1161 (#480): the 253 Source cloud (1950/s, B1122's record-35 fit) is
    // retired with the 253 override: 253's castGlow is the style again, and
    // its record 104 EDD slots draw the Source at the file's counts (B1149).
    EXPECT_FLOAT_EQ(glow(253).config.spawnRate, 80.0f);   // style (no override left)
    EXPECT_FLOAT_EQ(glow(458).config.spawnRate, 65.0f);   // record 33
    // B1131 put each RECORD's measured onset here (93 = record 0 BC+0.46, 54 =
    // 23 0.42, 253 = 35 0.42, 458 = 33 0.38). B1145: the Source starts at the
    // stage's measured latency (the default row's 0.41, the median of the 40)
    // plus each slot's +0x8C, which is 0 on the first slot of every measured
    // record; the per-record values are tests/spell_fx_timing_oracle.h's band.
    const auto& cfg = SpellEffectsConfig::instance();
    const float latency = cfg.sourceStageLatency();
    EXPECT_FLOAT_EQ(latency, 0.41f);
    const std::pair<uint32_t, float> kOnset[] = {{93u, latency}, {54u, latency}, {253u, latency}, {458u, latency}};
    for (const auto& [id, onset] : kOnset) {
        // B1153 (#473): the per-record rows are retired -- the default row's
        // onset (the median) is every record's no-file Source onset.
        const auto src = SpellPresets::SourceForSpell(glm::vec4(1.0f), id, -1, false, 3.6f, 1.0f);
        EXPECT_FLOAT_EQ(src.emitters.at(0).triggerDelay, onset) << id;
        EXPECT_EQ(glow(id).trigger, EQT::Graphics::Environment::SpellTrigger::IMMEDIATE)
            << id << ": the style carries no onset of its own";
        // The Source keeps its B1119 per-particle life: the sheet plays once
        // per life, so a denser cloud is more sprites, never longer ones.
        EXPECT_FLOAT_EQ(glow(id).config.lifetimeMin, 0.8f) << id;
        EXPECT_FLOAT_EQ(glow(id).config.lifetimeMax, 1.2f) << id;
    }
}

// B375.  `global.maxParticles` had no consumer at all: the renderer sent
// poolSize 0 and the worker's own arm was a hardcoded 1024 that happened to
// equal the shipped value.  With one landing now ~650 sprites, two landings
// overlapping exceed 1024 and would be silently clipped.
TEST_P(SpellBurstPool, TheConfiguredPoolHoldsTwoOverlappingLandings) {
    const float perLanding = PeakLive();
    EXPECT_GE(Pool() * 1.001f, 2.0f * perLanding)
        << "one landing is " << perLanding << " sprites and two can overlap; "
           "the pool is "
        << SpellEffectsConfig::instance().getGlobal().maxParticles;
}

// And the knob has to be load-bearing, not decorative: drive the real worker
// with the shipped impact preset at the OLD hardcoded pool and at the
// configured one, and show the population differs.  This is what makes the
// one-line wiring in renderer_sim_bridge.cpp a fact rather than a comment.
class SpellBurstPoolSizing : public ::testing::TestWithParam<const char*> {
protected:
    void SetUp() override {
        const int pool = spell_pool::Shipped(GetParam());
        ASSERT_GT(pool, 0) << "config/constrained_presets.json names no spellParticlePool for this platform";
        if (!spell_pool::LoadAt(pool)) {
            GTEST_SKIP() << "no config/spell_effects.json";
        }
    }

    static constexpr uint16_t kTarget = 38;
    const glm::vec3 kTargetPos{146.0f, 51.0f, -1009.0f};

    // Run `frames` worker frames at 60 Hz with `pool` particles, obeying the
    // one-frame entity-position protocol (T395), and return the largest
    // render-buffer size seen.
    size_t PeakBuffer(int pool, int frames, int emitters) {
        EQT::Graphics::SimulationWorker worker;
        EQT::Graphics::SimulationOutput output;
        std::unordered_set<uint16_t> requested;
        size_t peak = 0;
        for (int f = 0; f < frames; ++f) {
            EQT::Graphics::SimulationInput input;
            auto& pi = input.particleInput;
            pi.deltaTime = 1.0f / 60.0f;
            pi.unifiedRendererInitialized = true;
            pi.poolSize = pool;
            for (uint16_t id : requested) {
                if (id == kTarget) pi.entityPositions[kTarget] = kTargetPos;
            }
            if (f == 0) {
                for (int e = 0; e < emitters; ++e) {
                    EQT::Graphics::ParticleCommandData cmd;
                    cmd.type = EQT::Graphics::ParticleCommand::CreateSpellEffect;
                    cmd.spellDef = SpellPresets::Impact(
                        glm::vec4(1.0f, 0.47f, 0.2f, 1.0f), 93);
                    cmd.casterID = 0;
                    cmd.targetID = kTarget;
                    cmd.duration = 0.0f;
                    cmd.preAssignedEffectID = static_cast<uint16_t>(e + 1);
                    pi.commands.push_back(cmd);
                }
            }
            worker.computeOnce(input, output);
            requested = output.particleOutput.positionRequestEntities;
            if (output.particleOutput.valid)
                peak = std::max(peak, output.particleOutput.renderBuffer.size());
        }
        return peak;
    }
};

// Four simultaneous impacts are ~1,150 sprites.  At the old hardcoded 1,024
// the worker runs out of pool and clips; at the configured pool it does not.
INSTANTIATE_TEST_SUITE_P(Platforms, SpellBurstPoolSizing, ::testing::ValuesIn(spell_pool::kPlatforms));

TEST_P(SpellBurstPoolSizing, TheOldHardcodedPoolClipsAndTheConfiguredOneDoesNot) {
    const int configured = SpellEffectsConfig::instance().getGlobal().maxParticles;
    // 60 frames = 1.0 s: past the 0.33 s delay and the 0.6 s peak window,
    // before anything dies (the sustain starts later, B1121).
    const size_t atOldDefault = PeakBuffer(1024, 60, 4);
    const size_t atConfigured = PeakBuffer(configured, 60, 4);
    EXPECT_LE(atOldDefault, 1024u);
    EXPECT_GT(atConfigured, 1024u)
        << "four overlapping impacts produced " << atConfigured
        << " sprites at the configured pool of " << configured
        << " and " << atOldDefault << " at the old hardcoded 1024 -- if these "
           "are equal the pool is not the limit and this test proves nothing";
    EXPECT_GT(atConfigured, atOldDefault);
}

#endif  // EQT_HAS_GRAPHICS && EQT_HAS_GLES2

}  // namespace
