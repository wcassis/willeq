// B1120 (#440): the spell LANDING draws the official client's VOLUME, not a
// waist-high disc (B1118 M2).
//
// THE DEFECT.  Until B1120 the two landing styles were a RING r=0.5 with a
// BURST of 3.0 horizontal / 1.5 vertical (spellComplete) and a RADIAL_EXPAND
// of 4.0 / 1.0 at target origin +1 (impact), both under gravity -2.  Every
// particle stayed below ~0.6 caster heights: a flat disc.  B1118 measured the
// official client at real frame rate on ThinkPad lane 5 (refactor/B1118/
// README.md sec. 4) and found columns, domes, swirls and a star field instead:
//   93/38  a fire COLUMN >= 2-2.5 caster heights tall (f7573, f9800) + a spark
//          shell ~3.4 wide
//   41     a narrow PILLAR ~2 tall + a ball DOME ~4 wide settling to the ground
//          (f6448)
//   54/656 a SWIRL round the body + glints flung flat ~4 wide (f10670, f11639)
//   253    a STAR FIELD filling the frame (f6552)
//   458    a BURST of balls up and out, falling back (f7773)
// refactor/B1120/README.md derives each emitter from those videos frame by
// frame; config/spell_effects.json carries them (styles = the record-0 column
// + spark shell, spellOverrides = the other four shapes).
//
// UNITS.  The JSON is in world units; the oracle was measured in CASTER
// HEIGHTS.  B1120 sec. 3 calibrated the Testcaster dark elf on lane 5 at
// kH = 6.5 world units tall with the entity anchor kA = 3.6 above the feet;
// every band below is written in caster heights through those two numbers.
//
// THE ONSET.  A volume emitter that starts at OP_Action crosses the B1118
// visibility threshold ~0.1-0.2 s later, against the oracle's measured
// Act+0.40-0.80 s, so the landing styles carry a `triggerDelay` (the sim's
// SpellTrigger::DELAYED arm) -- pinned here so the onset cannot silently
// regress when a later card touches the emitters.
//
// THE POOL.  `global.maxParticles` (B375) never reached the worker in a live
// run: the zone-load input initialises the pool first and sent the struct
// default 1024.  Pinned at the bottom by a source census.

#include <gtest/gtest.h>

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

#if defined(EQT_HAS_GRAPHICS) && defined(EQT_HAS_GLES2)
#include "client/graphics/environment/spell_effects_config.h"
#include "client/graphics/environment/spell_particle_types.h"
#include "client/graphics/environment/unified_particle.h"
#include "spell_fx_shape_oracle.h"
#include "client/graphics/simulation_worker.h"
#include "client/spell/spell_fx_timing.h"
#include "client/spell/spells_new_eff_reader.h"
#include "spell_pool.h"
#include "particle_test_seed.h"
#include <glm/glm.hpp>
#include <algorithm>
#include <utility>
#include <cmath>
#include <unordered_set>
#include <vector>
#endif

namespace {

std::string ReadFile(const std::string& rel) {
    std::ifstream f(std::string(WILLEQ_REPO_ROOT) + "/" + rel);
    std::stringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

#if defined(EQT_HAS_GRAPHICS) && defined(EQT_HAS_GLES2)

using EQT::Graphics::Environment::EmitterConfig;
using EQT::Graphics::Environment::SpellEffectDef;
using EQT::Graphics::Environment::SpellEffectsConfig;
using EQT::Graphics::Environment::SpellTrigger;
using EQT::Graphics::Environment::UnifiedParticle;
namespace SpellPresets = EQT::Graphics::Environment::SpellPresets;

constexpr float kH = 6.5f;   // caster height, world units (B1120 sec. 3)
constexpr float kA = 3.6f;   // entity anchor above the feet, world units

// Shape statistics of the live particles, in caster heights relative to the
// caster's FEET (y) and its vertical axis (r).
struct Shape {
    size_t n = 0;
    float top = 0.0f;        // 95th percentile height above the feet
    float bottom = 0.0f;     // 5th percentile height above the feet
    float radius = 0.0f;     // 95th percentile horizontal distance from the axis
    float fracAbove1 = 0.0f; // share of particles higher than one caster height
};

class SpellLandingVolume : public ::testing::Test {
protected:
    // B1123: at this build's platform pool; the pool-accounting cases run at
    // every platform's pool (SpellLandingPool, spell_pool.h).
    void SetUp() override {
        const int pool = spell_pool::Shipped(spell_pool::ThisBuildPlatform());
        ASSERT_GT(pool, 0) << "config/constrained_presets.json names no spellParticlePool for this platform";
        if (!spell_pool::LoadAt(pool)) {
            GTEST_SKIP() << "no config/spell_effects.json";
        }
    }

    static constexpr uint16_t kCaster = 41;
    const glm::vec3 kPos{146.0f, 51.0f, -1009.0f};  // the anchor (Irrlicht Y-up)

    // Drive the real worker at 60 Hz with one landing emitter attached to
    // kCaster, obeying the one-frame entity-position protocol (T395), and
    // return the live particles after `seconds`.  `firstSpawn` receives the
    // effect age of the first frame that held any particle.
    std::vector<UnifiedParticle> Run(const SpellEffectDef& def, float seconds,
                                     float* firstSpawn = nullptr) {
        EQT::Graphics::SimulationWorker worker;
        EQT::Graphics::SimulationOutput output;
        std::unordered_set<uint16_t> requested;
        const int frames = static_cast<int>(std::lround(seconds * 60.0f));
        if (firstSpawn) *firstSpawn = -1.0f;
        for (int f = 0; f < frames; ++f) {
            EQT::Graphics::SimulationInput input;
            auto& pi = input.particleInput;
            pi.deltaTime = 1.0f / 60.0f;
            pi.unifiedRendererInitialized = true;
            pi.rngSeed = particle_test::kSeed;  // B1169: fixed draws (tests/particle_test_seed.h)
            pi.poolSize = SpellEffectsConfig::instance().getGlobal().maxParticles;
            for (uint16_t id : requested) {
                if (id == kCaster) pi.entityPositions[kCaster] = kPos;
            }
            if (f == 0) {
                EQT::Graphics::ParticleCommandData cmd;
                cmd.type = EQT::Graphics::ParticleCommand::CreateSpellEffect;
                cmd.spellDef = def;
                cmd.casterID = kCaster;
                cmd.targetID = kCaster;
                cmd.duration = 0.0f;
                cmd.preAssignedEffectID = 1;
                pi.commands.push_back(cmd);
            }
            worker.computeOnce(input, output);
            requested = output.particleOutput.positionRequestEntities;
            if (firstSpawn && *firstSpawn < 0.0f && output.particleOutput.valid &&
                !output.particleOutput.renderBuffer.empty()) {
                *firstSpawn = static_cast<float>(f + 1) / 60.0f;
            }
        }
        return output.particleOutput.renderBuffer;
    }

    Shape Measure(const std::vector<UnifiedParticle>& ps) const {
        Shape s;
        s.n = ps.size();
        if (ps.empty()) return s;
        std::vector<float> ys, rs;
        size_t above = 0;
        for (const auto& p : ps) {
            const float y = (p.position.y - (kPos.y - kA)) / kH;
            const float r = std::hypot(p.position.x - kPos.x, p.position.z - kPos.z) / kH;
            ys.push_back(y);
            rs.push_back(r);
            if (y > 1.0f) ++above;
        }
        auto pct = [](std::vector<float> v, float q) {
            std::sort(v.begin(), v.end());
            return v[static_cast<size_t>(q * static_cast<float>(v.size() - 1))];
        };
        s.top = pct(ys, 0.95f);
        s.bottom = pct(ys, 0.05f);
        s.radius = pct(rs, 0.95f);
        s.fracAbove1 = static_cast<float>(above) / static_cast<float>(ps.size());
        return s;
    }

    static SpellEffectDef Complete(uint32_t id) { return SpellPresets::SpellComplete(glm::vec4(1.0f), id); }
    static SpellEffectDef Impact(uint32_t id) { return SpellPresets::Impact(glm::vec4(1.0f), id); }
};

// RED on the pre-B1120 config: the spellComplete ring + burst peaked 0.56
// world units above the anchor, i.e. (3.6 + 0.56) / 6.5 = 0.64 caster heights.
// 285 has no override, so this is the STYLE: the record-0 fire column.
TEST_F(SpellLandingVolume, TheStyleLandingIsAColumnAtLeastTwoCasterHeightsTall) {
    const Shape s = Measure(Run(Complete(285), 1.9f));
    ASSERT_GT(s.n, 100u);
    EXPECT_GE(s.top, 2.0f) << "the oracle's 93/38 column reaches >= 2-2.5 caster heights (f7573, f9800)";
    EXPECT_LE(s.bottom, 0.5f) << "the column stands on the ground, it is not a floating slab";
    EXPECT_GE(s.bottom, -0.2f);
    EXPECT_LE(s.radius, 1.2f) << "a column, not a disc: the oracle's is ~1.8 caster heights wide";
    EXPECT_GE(s.fracAbove1, 0.3f);
}

// The record-0 spells are the style itself (93 overrides only its population).
TEST_F(SpellLandingVolume, BurstOfFlameAndLightningBoltDrawTheColumn) {
    for (uint32_t id : {93u, 38u}) {
        const Shape s = Measure(Run(Complete(id), 1.9f));
        EXPECT_GE(s.top, 2.0f) << "spell " << id;
        EXPECT_LE(s.radius, 1.2f) << "spell " << id;
    }
}

// The target half of the record-0 landing is the spark shell: wider than the
// column (the oracle's 93 spark cloud is ~3.4 caster heights across at its
// peak) and lifted off the ground, not a waist-high ring.
TEST_F(SpellLandingVolume, TheStyleImpactIsASparkShellWiderThanTheColumn) {
    const Shape s = Measure(Run(Impact(285), 1.9f));
    ASSERT_GT(s.n, 100u);
    EXPECT_GE(s.radius, 1.2f);
    EXPECT_GE(s.top, 1.3f);
}

// 41 Weaken: a narrow pillar at the target (= the caster, self-cast) and a
// dome of balls that spreads >= 1.5 caster heights. B1150 (#470): the dome's
// spawn is record 20's Target shape (ring r 14 at +3, sphere r 12, point,
// ring r 12; refactor/B1150/edd_shapes.tsv), which holds its lower edge
// ~0.36H above the feet -- the retired SPHERE 2.6 dome (kOverrideShape) was
// what reached the ground.
TEST_F(SpellLandingVolume, WeakenIsAPillarAndASettlingDome) {
    const Shape pillar = Measure(Run(Impact(41), 1.9f));
    EXPECT_LE(pillar.radius, 0.5f) << "the oracle's 41 pillar is ~0.7 caster heights wide";
    EXPECT_GE(pillar.top, 1.6f) << "and ~2 caster heights tall (f6448)";
    const Shape dome = Measure(Run(Complete(41), 1.9f));
    EXPECT_GE(dome.radius, 1.5f) << "the oracle's 41 dome is >= 3.9 caster heights across";
    EXPECT_LE(dome.top, 2.0f);
    EXPECT_LE(dome.bottom, 0.45f) << "record 20's T shape keeps the dome off the ground";
}

// 54/656 Frost Bolt / Shock of Ice: the caster half orbits the body; the
// target half is glints flung out flat to ~2 caster heights.
TEST_F(SpellLandingVolume, FrostBoltIsASwirlAndFlatGlints) {
    for (uint32_t id : {54u, 656u}) {
        const auto swirl = Complete(id).emitters.at(0).config;
        EXPECT_EQ(swirl.motionType, EQT::Graphics::Environment::MotionType::ORBITAL) << id;
        EXPECT_NEAR(swirl.orbitalRadius / kH, 0.85f, 0.15f) << id;
        const Shape s = Measure(Run(Complete(id), 1.5f));
        EXPECT_NEAR(s.radius, 0.85f, 0.15f) << id;
        // B1150 (#470): record 23's Target shape (sphere r 4, point, ring r 11,
        // ring r 7 at -3; edd_shapes.tsv) tops the swirl at ~0.94H.
        EXPECT_GE(s.top, 0.85f) << id;
        const Shape g = Measure(Run(Impact(id), 1.0f));
        EXPECT_GE(g.radius, 1.6f) << id << ": the oracle's glints span ~4 caster heights (f10670)";
        EXPECT_LE(g.top, 1.0f) << id << ": and stay low";
    }
}

// 253 Whirling Wind: B1161 (#480) retired the 253 override, so the spell has
// no style-drawn volume of its own any more: Complete(253)/Impact(253) are
// the style itself (the B1120 volume the no-record landings draw), and the
// FRAME-FILLING field it was (B1118: oracle coverage 7.28 caster heights^2)
// is its record 104's EDD field -- the file's counts and shapes (B1149,
// B1150), capped by the pool at runtime, measured by the B1161 sweep against
// the band in tests/spell_fx_population_oracle.h row 35.  The style's own
// volume stays pinned by TheStyleLandingIsAColumnAtLeastTwoCasterHeightsTall
// and TheStyleImpactIsASparkShellWiderThanTheColumn above.

// 458 Fire Spiral: balls burst up and out and fall back; the burst stays
// within ~2.2 caster heights (the oracle's 458 rowtop at its peak, f7773).
TEST_F(SpellLandingVolume, FireSpiralIsABurstThatFallsBack) {
    const Shape s = Measure(Run(Complete(458), 1.9f));
    EXPECT_GE(s.top, 1.2f);
    EXPECT_LE(s.top, 2.3f);
    EXPECT_GE(s.radius, 0.5f);
}

// The onset: no landing particle exists before the style's delay, and the
// delay sits where the oracle's measured onset puts it.
TEST_F(SpellLandingVolume, TheLandingWaitsForTheOraclesOnset) {
    for (const auto& def : {Complete(285), Impact(285)}) {
        const auto& ed = def.emitters.at(0);
        EXPECT_EQ(ed.trigger, SpellTrigger::DELAYED);
        EXPECT_GE(ed.triggerDelay, 0.25f);
        EXPECT_LE(ed.triggerDelay, 0.45f);
        float first = -1.0f;
        Run(def, 1.0f, &first);
        EXPECT_GE(first, ed.triggerDelay) << "a landing particle appeared before the delay";
        EXPECT_LE(first, ed.triggerDelay + 0.1f) << "and must appear right after it";
    }
}

// B1121 (#441): the landing is FED to the official client's measured end.
// B1118's oracle keeps its column BASE lit to the end (93: the 0-0.5 caster
// height band holds 0.74-1.32 caster heights^2 from Act+1.2 to Act+4.55,
// review/B1121/feed/), so the sustain phase must still be spawning at the
// feet just before emitterLifetime, and nothing may outlive the last death.
TEST_F(SpellLandingVolume, TheColumnIsFedAtTheFeetUntilItsWindowCloses) {
    const auto def = Complete(285);
    const auto& ed = def.emitters.at(0);
    const float close = ed.triggerDelay + ed.config.emitterLifetime;
    std::vector<UnifiedParticle> late = Run(def, close - 0.05f);
    ASSERT_GT(late.size(), 100u) << "the column must still be there as emission closes";
    size_t young = 0, atFeet = 0;
    for (const auto& p : late) {
        if (p.age < 0.5f) ++young;
        if (p.age < 0.5f && (p.position.y - (kPos.y - kA)) / kH < 0.6f) ++atFeet;
    }
    EXPECT_GT(young, 50u) << "sprites born in the last 0.5 s: the emitter is still feeding";
    EXPECT_GT(atFeet, 25u) << "and they rise from the feet, as the oracle's base band does";
    const float lastDeath = close + ed.config.lifetimeMax;
    EXPECT_TRUE(Run(def, lastDeath + 0.1f).empty()) << "nothing outlives delay + window + life";
}

// B1123 (#443): the pool-accounting cases, at EVERY platform's shipped pool.
class SpellLandingPool : public SpellLandingVolume,
                         public ::testing::WithParamInterface<const char*> {
protected:
    void SetUp() override {
        const int pool = spell_pool::Shipped(GetParam());
        ASSERT_GT(pool, 0) << "config/constrained_presets.json names no spellParticlePool for this platform";
        if (!spell_pool::LoadAt(pool)) {
            GTEST_SKIP() << "no config/spell_effects.json";
        }
    }
};
INSTANTIATE_TEST_SUITE_P(Platforms, SpellLandingPool, ::testing::ValuesIn(spell_pool::kPlatforms));

// One record-0 landing (93's population, both halves) through the real worker
// never holds more than half the pool: two can overlap (B375).
TEST_P(SpellLandingPool, OneFedLandingFitsHalfThePool) {
    EQT::Graphics::SimulationWorker worker;
    EQT::Graphics::SimulationOutput output;
    std::unordered_set<uint16_t> requested;
    const int pool = SpellEffectsConfig::instance().getGlobal().maxParticles;
    size_t peak = 0;
    for (int f = 0; f < 60 * 6; ++f) {
        EQT::Graphics::SimulationInput input;
        auto& pi = input.particleInput;
        pi.deltaTime = 1.0f / 60.0f;
        pi.unifiedRendererInitialized = true;
        pi.rngSeed = particle_test::kSeed;  // B1169: fixed draws (tests/particle_test_seed.h)
        pi.poolSize = pool;
        for (uint16_t id : requested)
            if (id == kCaster) pi.entityPositions[kCaster] = kPos;
        if (f == 0) {
            uint16_t eid = 1;
            for (const auto& def : {Complete(93), Impact(93)}) {
                EQT::Graphics::ParticleCommandData cmd;
                cmd.type = EQT::Graphics::ParticleCommand::CreateSpellEffect;
                cmd.spellDef = def;
                cmd.casterID = kCaster;
                cmd.targetID = kCaster;
                cmd.duration = 0.0f;
                cmd.preAssignedEffectID = eid++;
                pi.commands.push_back(cmd);
            }
        }
        worker.computeOnce(input, output);
        requested = output.particleOutput.positionRequestEntities;
        if (output.particleOutput.valid) peak = std::max(peak, output.particleOutput.renderBuffer.size());
    }
    EXPECT_GT(peak, 1500u) << "the peak cohort is B1120's (~1920)";
    EXPECT_LE(peak, static_cast<size_t>(pool / 2)) << "one landing peaked at " << peak << " live sprites";
}

// B1161 (#480): the pool rule on the RECORD path is the oracle's ring-full
// rule -- a CAP, not a scaler (EQGraphicsDX9 0x10061180: ring full drops the
// rest of the frame's emission; refactor/B1149/README.md sec. 4).  Drive the
// real worker with the largest record field a class-level 1-60 spell draws --
// record 104 (253 Whirling Wind), built slot by slot from the shipped
// spellsnew.edd the way SpellVisualFX::createCastGlow / createLandingFromSlots
// build it (Source slots at OP_BeginCast with the caller duration cast +
// the cast-animation term, Target slots at OP_Action with none) -- and count
// the live sprites at each platform's pool.  The file's words, uncapped,
// exceed the sbc 4096 (B1149's census: 3746 + 1168 = 4914, an upper bound):
// on the desktop 8192 the field runs at the file's counts (peak above 4096,
// never scaled down), on sbc the pool fills (measured 4071 of 4096; the test asserts
// >= 0.98 x pool) and drops the rest (allocateParticle returns -1, sim_particles.cpp) -- never above.
TEST_P(SpellLandingPool, TheLargestRecordFieldIsCappedNotScaled) {
    const char* env = std::getenv("EQ_CLIENT_PATH");
    const std::string dir = env ? env : "/mnt/projects/EverQuestP1999";
    if (!std::ifstream(dir + "/spellsnew.edd").good()) GTEST_SKIP() << "spellsnew.edd not under " << dir;
    EQ::SpellsNewEffReader reader;
    ASSERT_TRUE(reader.load(dir)) << reader.loadError();
    namespace Presets = EQT::Graphics::Environment::SpellPresets;
    const auto& cfg = SpellEffectsConfig::instance();
    const int pool = cfg.getGlobal().maxParticles;
    constexpr uint32_t kSpell = 253;   // Whirling Wind
    constexpr int kRecord = 104;       // its spells_us.txt field 145
    constexpr int kCastMs = 1750;      // its cast time (spells_us.txt)
    constexpr int kLevel = 60;
    const glm::vec4 col(1.0f);
    struct Made { SpellEffectDef def; float duration; bool atAction; };
    std::vector<Made> made;
    // Source: createCastGlow's caller duration (the cast less the stage
    // latency, + the cast-animation term) and its per-slot cap.
    const float srcLatency = cfg.sourceStageLatency();
    const int32_t srcCaller = EQ::SpellFxCallerDurationMs(
        EQ::SpellFxStage::Source, kCastMs - static_cast<int32_t>(srcLatency * 1000.0f + 0.5f), 0);
    for (const auto& d : reader.drawnSlots(kRecord, EQ::kSpellsNewEffSource, kLevel)) {
        const EQ::SpellsNewEddTiming* w = reader.edd().timing(d.slot.emitter);
        ASSERT_NE(w, nullptr) << "Source slot " << d.index;
        const EQ::SpellFxSlotTiming t = EQ::SpellFxSlotTimingFor(*w, srcCaller);
        auto def = Presets::SourceForSpell(col, kSpell, kRecord, false, kA, 1.0f);
        Presets::applySlotEmitter({&def}, t, reader.edd().sprite(d.slot.emitter), srcLatency, EQ::kSpellFxAttachScale,
                                  reader.edd().shape(d.slot.emitter), reader.edd().motion(d.slot.emitter));
        made.push_back({def, srcLatency + t.end() + 0.1f, false});
    }
    // Target: no caller duration (eqgame 0x47f5e1), the two landing halves
    // sharing each slot, from OP_Action.
    const float tgtLatency = cfg.targetStageLatency();
    for (const auto& d : reader.drawnSlots(kRecord, EQ::kSpellsNewEffTarget, kLevel)) {
        const EQ::SpellsNewEddTiming* w = reader.edd().timing(d.slot.emitter);
        ASSERT_NE(w, nullptr) << "Target slot " << d.index;
        const EQ::SpellFxSlotTiming t = EQ::SpellFxSlotTimingFor(
            *w, EQ::SpellFxCallerDurationMs(EQ::SpellFxStage::Target, kCastMs, 0));
        auto complete = Presets::TargetForSpell(col, kSpell, kRecord, true, kA);
        auto impact = Presets::TargetForSpell(col, kSpell, kRecord, false, kA);
        Presets::applySlotEmitter({&complete, &impact}, t, reader.edd().sprite(d.slot.emitter), tgtLatency,
                                  EQ::kSpellFxAttachScale, reader.edd().shape(d.slot.emitter),
                                  reader.edd().motion(d.slot.emitter));
        made.push_back({complete, 0.0f, true});
        made.push_back({impact, 0.0f, true});
    }
    ASSERT_EQ(made.size(), 4u + 2u * 3u) << "record 104: 4 Source slots + 3 Target slots at level 60";

    const int actFrame = static_cast<int>(std::lround(kCastMs * 0.001f * 60.0f));
    auto peakAt = [&](int poolSize) {
        EQT::Graphics::SimulationWorker worker;
        EQT::Graphics::SimulationOutput output;
        std::unordered_set<uint16_t> requested;
        size_t peak = 0;
        for (int f = 0; f < actFrame + 60 * 9; ++f) {
            EQT::Graphics::SimulationInput input;
            auto& pi = input.particleInput;
            pi.deltaTime = 1.0f / 60.0f;
            pi.unifiedRendererInitialized = true;
            pi.rngSeed = particle_test::kSeed;  // B1169: fixed draws (tests/particle_test_seed.h)
            pi.poolSize = poolSize;
            for (uint16_t id : requested)
                if (id == kCaster) pi.entityPositions[kCaster] = kPos;
            uint32_t eid = 1;
            for (const auto& m : made) {
                if (m.atAction ? f == actFrame : f == 0) {
                    EQT::Graphics::ParticleCommandData cmd;
                    cmd.type = EQT::Graphics::ParticleCommand::CreateSpellEffect;
                    cmd.spellDef = m.def;
                    cmd.casterID = kCaster;
                    cmd.targetID = kCaster;
                    cmd.duration = m.duration;
                    cmd.preAssignedEffectID = eid;
                    pi.commands.push_back(cmd);
                }
                ++eid;
            }
            worker.computeOnce(input, output);
            requested = output.particleOutput.positionRequestEntities;
            if (output.particleOutput.valid) peak = std::max(peak, output.particleOutput.renderBuffer.size());
        }
        return peak;
    };
    // The field at the file's counts: the desktop pool never fills under it.
    const int desktop = spell_pool::Shipped("desktop");
    const size_t field = peakAt(desktop);
    EXPECT_GT(field, 4096u) << "record 104's file field, unscaled, exceeds the sbc pool: it peaked at " << field;
    EXPECT_LT(field, static_cast<size_t>(desktop)) << "and fits the desktop pool";
    const size_t here = peakAt(pool);
    EXPECT_LE(here, static_cast<size_t>(pool)) << "the pool is a cap: " << here << " live at pool " << pool;
    if (static_cast<size_t>(pool) > field) {
        EXPECT_EQ(here, field) << "a pool the field fits leaves it at the file's counts (no scaling)";
    } else {
        // The pool fills (the peak sits at the pool less the sprites that
        // die inside the frame they are counted in) and the rest is dropped.
        EXPECT_GE(here, static_cast<size_t>(0.98f * static_cast<float>(pool)))
            << "the field fills the " << pool << " pool: peak " << here;
        EXPECT_LT(here, field) << "and the sprites over it are dropped, not rescheduled";
    }
    std::printf("[ B1161    ] record 104 field peak %zu live at pool %d, %zu at pool %d\n", field, desktop, here, pool);
}

// A style with no triggerDelay keeps the IMMEDIATE trigger (the projectile
// trail is the worked case since B1138 deleted the buff aura; the cast glow was
// one until B1122 gave the Source its onset).
TEST_F(SpellLandingVolume, AStyleWithoutADelayStaysImmediate) {
    const auto projectile = SpellPresets::Projectile(glm::vec4(1.0f), 285);
    EXPECT_EQ(projectile.emitters.at(0).trigger, SpellTrigger::IMMEDIATE);
}

// ---------------------------------------------------------------------------
// B1122 (#442): the SOURCE (cast) stage is a BODY-WIDE cloud (B1118 M4).
//
// Until B1122 createCastGlow() pinned the Source to the two hand bones with a
// 0.4-unit orbit at 24/s per hand: 5-15 dots of 1-2 px. The official client
// draws a cloud round the whole body from BeginCast + ~0.4 s until OP_Action.
// review/B1122/scripts/source_all.py measures it on the B1118 videos (lit
// mask over the pre-cast second, the oracle's Casting Time window and gem
// tooltip excluded; refactor/B1122/README.md sec. 2), in caster heights:
//   spell        93    38    54    656   41    253   458
//   extent       2.92  2.24  1.60  1.45  2.68  3.70  0.75   (x p2..p98)
//   centroid     0.89  0.95  0.82  0.61  0.87  1.11  0.53   (above the feet)
// The bands below are on the PARTICLE positions (no sprite size), so they sit
// a little inside the lit-pixel extents.
// ---------------------------------------------------------------------------

// B1131 (#451): 93 and 38 are spells.eff record 0, whose Source the B1130
// survey measured at the HANDS (hand flames + spark cloud). B1150 (#470):
// the record's spellsnew.edd +0xA8 shape draws every record's Source from
// the caster's entity anchor, so all seven are body clouds again -- the
// per-record anchor class (record 0: hands) stays in the config as evidence,
// pinned with its retired extents by tests/spell_fx_shape_oracle.h and
// test_spell_source_anchors.cpp. The other five lost their measured
// per-spell spellOverrides shapes in the same retirement.
struct SourceCase {
    uint32_t id;
    bool spray;       // 41 Weaken is Alteration: SpellVisualFX picks castSpray
    float castSec;    // the server-confirmed cast time (B1118 sec. 2)
    float wMin, wMax; // the post-retirement width (the EDD shape + the style's
                      // motion composite, measured in caster heights)
};
// B1150 widths, measured in this test's Run (three unseeded runs each, 2026-10-25,
// the rbuild tree of 216f6621): 93 2.44-2.54, 38 2.46-2.53, 54/656 1.497
// (record 23's S shape is narrow and the style's ORBITAL radius 0.85 bounds
// it), 41 2.43-2.52, 458 2.43-2.53. B1161 (#480): the 253 row (2.52-2.55) is
// retired with the 253 override -- its Source is record 104's EDD slots
// (B1144/B1149), not a style cloud.
const SourceCase kSourceCases[] = {
    {93, false, 1.5f, 2.2f, 2.8f},
    {38, false, 2.5f, 2.2f, 2.8f},
    {54, false, 1.0f, 1.3f, 1.7f},
    {656, false, 2.0f, 1.3f, 1.7f},
    {41, true, 1.5f, 2.2f, 2.8f},
    {458, false, 3.6f, 2.2f, 2.8f},
};

// The Source as SpellVisualFX::createCastGlow() builds it for the caster's
// entity. B1153 (#473): `sourceRecords.overrideRecords` is retired (the rows
// are the bands in tests/spell_fx_row_oracle.h); the spell's spells.eff
// record (spells_us.txt field 123, B1131's table: 93/38 0, 41 20, 54/656 23,
// 253 35, 458 33) names its band.
int RecordOf(uint32_t id) {
    switch (id) {
        case 93: case 38: return 0;
        case 41: return 20;
        case 54: case 656: return 23;
        case 253: return 35;
        case 458: return 33;
        default: return -1;
    }
}
SpellEffectDef SourceDef(const SourceCase& c) {
    return SpellPresets::SourceForSpell(glm::vec4(1.0f), c.id, RecordOf(c.id), c.spray, kA, 1.0f);
}
bool IsBodyCloud(const SourceCase& c) {
    return SpellPresets::SourceAnchorForSpell(c.id, RecordOf(c.id), c.spray) ==
           SpellEffectsConfig::SourceAnchor::Body;
}

// RED on the B1121 config + code: no Source style was body-anchored and the
// hand-bone emitters were a POINT spawn (castSpray) or a 1.5-unit orbit
// that createCastGlow() then squeezed to 0.4 units at each hand.
// B1150 (#470): every record's Source is the style's cloud on the caster's
// entity with the record's +0xA8 spawn shape; the CENTROID is checked against
// the record's retired band (the oracle's measured vertical placement:
// spell_fx_shape_oracle.h kSource's centroid +- halfWidth), the WIDTH against
// the post-retirement composite above.
TEST_F(SpellLandingVolume, TheSourceIsABodyWideCloudShapedLikeTheOracles) {
    for (const auto& c : kSourceCases) {
        EXPECT_TRUE(IsBodyCloud(c)) << c.id << ": B1150: the record's +0xA8 shape draws the Source from the caster's entity anchor (its measured anchor class is evidence in spell_fx_shape_oracle.h)";
        int bandIdx = -1;
        for (int i = 0; i < spell_fx_shape_oracle::kSourceRows; ++i) {
            if (spell_fx_shape_oracle::kSource[i].record == RecordOf(c.id)) bandIdx = i;
        }
        ASSERT_GE(bandIdx, 0) << c.id << ": no retired band for its record";
        const auto& band = spell_fx_shape_oracle::kSource[bandIdx];
        const SpellEffectDef def = SourceDef(c);
        const auto ps = Run(def, std::min(c.castSec, 1.4f) + def.emitters.at(0).triggerDelay);
        ASSERT_GT(ps.size(), 20u) << c.id;
        std::vector<float> xs;
        float ySum = 0.0f;
        for (const auto& p : ps) {
            xs.push_back((p.position.x - kPos.x) / kH);
            ySum += (p.position.y - (kPos.y - kA)) / kH;
        }
        std::sort(xs.begin(), xs.end());
        const float width = xs[xs.size() * 98 / 100] - xs[xs.size() * 2 / 100];
        const float centroid = ySum / static_cast<float>(ps.size());
        EXPECT_GE(width, c.wMin) << c.id;
        EXPECT_LE(width, c.wMax) << c.id;
        EXPECT_GE(centroid, band.centroid - band.halfWidth) << c.id;
        EXPECT_LE(centroid, band.centroid + band.halfWidth) << c.id;
    }
}

// The Source starts at its spells.eff record's onset: BC+0.37-0.50 on 38 of
// the 40 records the B1130 survey measured (median 0.41; B1122's single 0.25
// was the defect). Every one of the seven, castGlow or castSpray, waits for it
// -- and nothing is drawn before it.
TEST_F(SpellLandingVolume, TheSourceWaitsForTheOraclesOnset) {
    for (const auto& c : kSourceCases) {
        const SpellEffectDef def = SourceDef(c);
        const auto& ed = def.emitters.at(0);
        EXPECT_EQ(ed.trigger, SpellTrigger::DELAYED) << c.id;
        EXPECT_FLOAT_EQ(ed.triggerDelay, SpellEffectsConfig::instance().sourceRecord(RecordOf(c.id)).onset) << c.id;
        EXPECT_GE(ed.triggerDelay, 0.37f) << c.id;
        EXPECT_LE(ed.triggerDelay, 0.50f) << c.id;
        float first = -1.0f;
        Run(def, 0.8f, &first);
        EXPECT_GE(first, ed.triggerDelay) << c.id;
        EXPECT_LE(first, ed.triggerDelay + 0.1f) << c.id;
    }
}

// B1122 held the Source past OP_Action; B1131 holds it for its spells.eff
// record's measured time (up to 6 s with another target, B1130), so it
// overlaps its own landing for most of the landing's life. Drive the real
// worker through one whole cast per spell -- Source for the cast time,
// OP_Action = release (the record's longer hold) + the landing (both halves)
// -- and bound the peak of Source + own landing by half the pool (the rule,
// applyPool), so the previous cast's landing (<= half,
// OneFedLandingFitsHalfThePool) still fits.
TEST_P(SpellLandingPool, ASourceAndItsOwnLandingFitHalfThePool) {
    const int pool = SpellEffectsConfig::instance().getGlobal().maxParticles;
    for (const auto& c : kSourceCases) {
        EQT::Graphics::SimulationWorker worker;
        EQT::Graphics::SimulationOutput output;
        std::unordered_set<uint16_t> requested;
        const SpellEffectDef src = SourceDef(c);
        const float hold = SpellEffectsConfig::instance().sourceRecord(RecordOf(c.id)).maxHold();
        const int actFrame = static_cast<int>(std::lround(c.castSec * 60.0f));
        size_t peak = 0;
        for (int f = 0; f < actFrame + 60 * 8; ++f) {
            EQT::Graphics::SimulationInput input;
            auto& pi = input.particleInput;
            pi.deltaTime = 1.0f / 60.0f;
            pi.unifiedRendererInitialized = true;
            pi.rngSeed = particle_test::kSeed;  // B1169: fixed draws (tests/particle_test_seed.h)
            pi.poolSize = pool;
            for (uint16_t id : requested)
                if (id == kCaster) pi.entityPositions[kCaster] = kPos;
            auto push = [&](const SpellEffectDef& def, float dur, uint32_t eid) {
                EQT::Graphics::ParticleCommandData cmd;
                cmd.type = EQT::Graphics::ParticleCommand::CreateSpellEffect;
                cmd.spellDef = def;
                cmd.casterID = kCaster;
                cmd.targetID = kCaster;
                cmd.duration = dur;
                cmd.preAssignedEffectID = eid;
                pi.commands.push_back(cmd);
            };
            if (f == 0) push(src, c.castSec + hold, 1);  // as createCastGlow() makes it
            if (f == actFrame) {
                EQT::Graphics::ParticleCommandData rel;
                rel.type = EQT::Graphics::ParticleCommand::ReleaseSpellEffectsEntity;
                rel.entityID = kCaster;
                rel.styleFilter = {"CastGlow", "CastSpray"};
                rel.duration = hold;
                pi.commands.push_back(rel);
                push(SpellPresets::SpellComplete(glm::vec4(1.0f), c.id, RecordOf(c.id)), 0.0f, 2);
                push(SpellPresets::Impact(glm::vec4(1.0f), c.id, RecordOf(c.id)), 0.0f, 3);
            }
            worker.computeOnce(input, output);
            requested = output.particleOutput.positionRequestEntities;
            if (!output.particleOutput.valid) continue;
            peak = std::max(peak, output.particleOutput.renderBuffer.size());
        }
        EXPECT_GE(hold, 0.3f) << c.id;
        // The rule fits the EXPECTED peak to half the pool; 2 % covers the
        // sampling spread of one run (B1121 measured +-2-3 %).
        const size_t bound = static_cast<size_t>(pool / 2) + static_cast<size_t>(pool / 100);
        EXPECT_LE(peak, bound) << c.id << ": Source + own landing peaked at " << peak;
        EXPECT_GT(peak, 300u) << c.id << ": the landing never filled";  // 458's exception is 373
    }
}

// The pool (4096, not raised: #440) holds a Source cloud beside a landing.
// A Source never overlaps its OWN landing (removeCastGlow() at OP_Action
// frees it; the landing waits triggerDelay), but it can overlap the previous
// cast's landing, which OneFedLandingFitsHalfThePool bounds at half the
// pool -- so every Source cloud must fit the other half.
// The lower bounds are the live counts that measured >= 80 % of the oracle's
// Source coverage on the B1122 gate (refactor/B1122/README.md sec. 5), at the
// pool those were measured at. B1131: where a record's held Source cannot sit
// beside its own landing in half the pool (sbc: 54/656, 41), the pool rule
// scales the Source too (SpellEffectsConfig::sourcePoolScale), and the floor
// scales with it. B1161 (#480): the 253 row (floor 1640, beside the retired
// 2160 field) is retired with the 253 override: its Source is record 104's
// EDD slots at the file's counts (B1149), capped by the pool, not a
// style-drawn cloud beside a style-drawn landing.
TEST_P(SpellLandingPool, EverySourceCloudFitsBesideALanding) {
    const int pool = SpellEffectsConfig::instance().getGlobal().maxParticles;
    const std::pair<uint32_t, size_t> kMinLive[] = {
        {93, 70}, {38, 70}, {54, 670}, {656, 900}, {41, 150}, {458, 54}};
    for (const auto& c : kSourceCases) {
        const SpellEffectDef src = SourceDef(c);
        const auto ps = Run(src, c.castSec + src.emitters.at(0).triggerDelay);
        size_t want = 0;
        for (const auto& [id, n] : kMinLive) if (id == c.id) want = n;
        const float k = SpellEffectsConfig::instance().sourcePoolScale(c.id, RecordOf(c.id));
        EXPECT_GE(static_cast<float>(ps.size()), static_cast<float>(want) * k)
            << c.id << ": a thinner cloud measured below 80 % of the oracle (pool factor " << k << ")";
        EXPECT_LE(ps.size(), static_cast<size_t>(pool / 2)) << c.id;
    }
}

#endif  // EQT_HAS_GRAPHICS && EQT_HAS_GLES2

// Every site that tells the worker the unified renderer is initialised must
// also hand it the configured pool, because the FIRST such input sizes the
// pool for the rest of the session (sim_particles.cpp lazy init).  RED before
// B1120: renderer_zone_load_env.cpp set the flag and left poolSize at the
// struct default, so every live run logged "pool initialized (size=1024)"
// against a configured 4096.
TEST(SpellParticlePoolWiring, EveryInitialisedInputCarriesTheConfiguredPool) {
    for (const char* rel : {"src/client/graphics/zone_load/renderer_zone_load_env.cpp",
                            "src/client/graphics/sim/renderer_sim_bridge.cpp"}) {
        const std::string src = ReadFile(rel);
        ASSERT_FALSE(src.empty()) << rel;
        size_t at = 0;
        int sites = 0;
        while ((at = src.find("pi.unifiedRendererInitialized = true", at)) != std::string::npos) {
            ++sites;
            const std::string after = src.substr(at, 1200);
            EXPECT_NE(after.find("pi.poolSize ="), std::string::npos) << rel;
            EXPECT_NE(after.find("getGlobal().maxParticles"), std::string::npos) << rel;
            at += 10;
        }
        EXPECT_EQ(sites, 1) << rel;
    }
}

// B1169 (#488): every TEST input that runs the particle phase seeds its RNG
// (tests/particle_test_seed.h). An unseeded one draws from std::random_device
// and its banded cloud is a different cloud every run -- this file's Source
// widths failed ~1 run in 300 that way. Literal occurrences (a quote before
// the flag, as in the guards' own search strings) are not sites.
TEST(SpellParticleSeedWiring, EveryTestThatRunsTheParticlePhaseSeedsIt) {
    namespace fs = std::filesystem;
    const fs::path dir = fs::path(WILLEQ_REPO_ROOT) / "tests";
    const std::string flag = "pi.unifiedRendererInitialized = true";
    int sites = 0;
    for (const auto& e : fs::directory_iterator(dir)) {
        const std::string ext = e.path().extension().string();
        if (ext != ".cpp" && ext != ".h") continue;
        const std::string rel = "tests/" + e.path().filename().string();
        const std::string src = ReadFile(rel);
        for (size_t at = src.find(flag); at != std::string::npos; at = src.find(flag, at + flag.size())) {
            if (at > 0 && src[at - 1] == '"') continue;
            ++sites;
            EXPECT_NE(src.substr(at, 600).find("pi.rngSeed ="), std::string::npos)
                << rel << " offset " << at << ": an unseeded particle input (B1169)";
        }
    }
    EXPECT_GE(sites, 17) << "the 2026-10-27 tree had 17 seeded sites; did the scan miss tests/?";
}

}  // namespace
