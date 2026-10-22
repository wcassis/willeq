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
//  * DURATION.  emitterLifetime + lifetime must equal the oracle's 2.33-2.6 s
//    total, which with L = 1.8 s puts the emission window at ~0.6 s.
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
//  * EXTENT is NOT changed, and has its probe.  The oracle's burst spreads
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
#endif

namespace {

#if defined(EQT_HAS_GRAPHICS) && defined(EQT_HAS_GLES2)

using EQT::Graphics::Environment::SpellEffectsConfig;
namespace SpellPresets = EQT::Graphics::Environment::SpellPresets;

class SpellBurstOracleMatch : public ::testing::Test {
protected:
    void SetUp() override {
        const std::string cfg =
            std::string(WILLEQ_REPO_ROOT) + "/config/spell_effects.json";
        if (!SpellEffectsConfig::instance().load(cfg)) {
            GTEST_SKIP() << "no " << cfg;
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

    // Peak population of a rectangular emission of `emitterLifetime` seconds
    // at `spawnRate`/s whose particles outlive the window: everything emitted
    // is still alive when emission stops.
    static float PeakPopulation(const EQT::Graphics::Environment::EmitterConfig& c) {
        return c.spawnRate * c.emitterLifetime + static_cast<float>(c.burstCount);
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
    EXPECT_GE(c.emitterLifetime, 0.5f);
    EXPECT_LE(c.emitterLifetime, 0.8f);
}

TEST_F(SpellBurstOracleMatch, SpellCompleteEmitsOverAWindowRatherThanInOneFrame) {
    const auto c = Complete();
    EXPECT_EQ(c.burstCount, 0);
    EXPECT_GT(c.spawnRate, 0.0f);
    EXPECT_GE(c.emitterLifetime, 0.5f);
    EXPECT_LE(c.emitterLifetime, 0.8f);
}

// The headline B226 number: one landing must put the oracle's order of
// sprites on screen, not 81.  Measured band 561 (Weaken) to 715 (Burst of
// Flame); allow +-25% for the split between the two stages.
TEST_F(SpellBurstOracleMatch, OneLandingReachesTheOraclesPeakSpriteCount) {
    const float total = PeakPopulation(Impact()) + PeakPopulation(Complete());
    EXPECT_GE(total, 420.0f)
        << "one landing paints " << total << " sprites; the oracle paints "
           "561 (Weaken) to 715 (Burst of Flame)";
    EXPECT_LE(total, 900.0f)
        << "one landing paints " << total << " sprites, well over the oracle's "
           "715 -- overshooting is as wrong as undershooting";
}

// The lifetime is bounded BELOW by the oracle's death-free rise (1.46 s for
// Weaken, 1.96 s for Burst of Flame) and ABOVE by the total duration.
TEST_F(SpellBurstOracleMatch, ParticleLifetimeMatchesTheOraclesDeathFreeRise) {
    for (const auto& c : {Impact(), Complete()}) {
        EXPECT_GE(c.lifetimeMin, 1.4f)
            << "the oracle's population rises for 1.46-1.96 s without a single "
               "death, so no particle may die before 1.4 s";
        EXPECT_LE(c.lifetimeMax, 2.4f)
            << "a lifetime past 2.4 s cannot fit the oracle's 2.33-2.6 s total "
               "duration alongside the emission window";
    }
}

// Total on-screen duration = emission window + particle lifetime.  Oracle:
// 2.33 s (Burst of Flame) to 2.6 s (Weaken).
TEST_F(SpellBurstOracleMatch, TheEffectLastsAsLongAsTheOracles) {
    for (const auto& c : {Impact(), Complete()}) {
        const float duration = c.emitterLifetime + c.lifetimeMax;
        EXPECT_GE(duration, 2.1f) << "duration " << duration << " s";
        EXPECT_LE(duration, 3.0f) << "duration " << duration << " s";
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
// does not move either off a desk argument.
TEST_F(SpellBurstOracleMatch, SpriteSizeAndExpandSpeedAreTheMeasuredOnes) {
    const auto c = Impact();
    EXPECT_FLOAT_EQ(c.sizeStartMin, 0.224f);
    EXPECT_FLOAT_EQ(c.sizeStartMax, 0.35f);
    EXPECT_FLOAT_EQ(c.sizeEndMin, 0.44f);
    EXPECT_FLOAT_EQ(c.sizeEndMax, 0.658f);
    const auto sc = Complete();
    EXPECT_FLOAT_EQ(sc.sizeStartMin, 0.266f);
    EXPECT_FLOAT_EQ(sc.sizeStartMax, 0.44f);
    EXPECT_FLOAT_EQ(sc.sizeEndMin, 0.532f);
    EXPECT_FLOAT_EQ(sc.sizeEndMax, 0.784f);
    EXPECT_FLOAT_EQ(c.expandSpeed, 4.0f)
        << "the oracle's burst spreads ~8.3 world units in ~2 s = ~4.2 units/s";
}

// B375.  `global.maxParticles` had no consumer at all: the renderer sent
// poolSize 0 and the worker's own arm was a hardcoded 1024 that happened to
// equal the shipped value.  With one landing now ~650 sprites, two landings
// overlapping exceed 1024 and would be silently clipped.
TEST_F(SpellBurstOracleMatch, TheConfiguredPoolHoldsTwoOverlappingLandings) {
    const float perLanding = PeakPopulation(Impact()) + PeakPopulation(Complete());
    EXPECT_GE(static_cast<float>(SpellEffectsConfig::instance().getGlobal().maxParticles),
              2.0f * perLanding)
        << "one landing is " << perLanding << " sprites and two can overlap; "
           "the pool is "
        << SpellEffectsConfig::instance().getGlobal().maxParticles;
}

// And the knob has to be load-bearing, not decorative: drive the real worker
// with the shipped impact preset at the OLD hardcoded pool and at the
// configured one, and show the population differs.  This is what makes the
// one-line wiring in renderer_sim_bridge.cpp a fact rather than a comment.
class SpellBurstPoolSizing : public ::testing::Test {
protected:
    void SetUp() override {
        const std::string cfg =
            std::string(WILLEQ_REPO_ROOT) + "/config/spell_effects.json";
        if (!SpellEffectsConfig::instance().load(cfg)) {
            GTEST_SKIP() << "no " << cfg;
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
TEST_F(SpellBurstPoolSizing, TheOldHardcodedPoolClipsAndTheConfiguredOneDoesNot) {
    const int configured = SpellEffectsConfig::instance().getGlobal().maxParticles;
    // 60 frames = 1.0 s, past the 0.6 s emission window, before anything dies.
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
