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

#include <fstream>
#include <sstream>
#include <string>

#if defined(EQT_HAS_GRAPHICS) && defined(EQT_HAS_GLES2)
#include "client/graphics/environment/spell_effects_config.h"
#include "client/graphics/environment/spell_particle_types.h"
#include "client/graphics/environment/unified_particle.h"
#include "client/graphics/simulation_worker.h"
#include <glm/glm.hpp>
#include <algorithm>
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
    void SetUp() override {
        const std::string cfg = std::string(WILLEQ_REPO_ROOT) + "/config/spell_effects.json";
        if (!SpellEffectsConfig::instance().load(cfg)) {
            GTEST_SKIP() << "no " << cfg;
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
// dome of balls that spreads >= 1.5 caster heights and comes back down.
TEST_F(SpellLandingVolume, WeakenIsAPillarAndASettlingDome) {
    const Shape pillar = Measure(Run(Impact(41), 1.9f));
    EXPECT_LE(pillar.radius, 0.5f) << "the oracle's 41 pillar is ~0.7 caster heights wide";
    EXPECT_GE(pillar.top, 1.6f) << "and ~2 caster heights tall (f6448)";
    const Shape dome = Measure(Run(Complete(41), 1.9f));
    EXPECT_GE(dome.radius, 1.5f) << "the oracle's 41 dome is >= 3.9 caster heights across";
    EXPECT_LE(dome.top, 2.0f);
    EXPECT_LE(dome.bottom, 0.3f) << "the balls settle towards the ground";
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
        EXPECT_GE(s.top, 1.2f) << id;
        const Shape g = Measure(Run(Impact(id), 1.0f));
        EXPECT_GE(g.radius, 1.6f) << id << ": the oracle's glints span ~4 caster heights (f10670)";
        EXPECT_LE(g.top, 1.0f) << id << ": and stay low";
    }
}

// 253 Whirling Wind: both halves fill a frame-sized box round the caster.
TEST_F(SpellLandingVolume, WhirlingWindFillsTheFrame) {
    for (const auto& def : {Complete(253), Impact(253)}) {
        const Shape s = Measure(Run(def, 2.0f));
        EXPECT_GE(s.radius, 1.8f);
        EXPECT_GE(s.top, 2.2f);
        EXPECT_LE(s.bottom, 0.3f);
    }
}

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

// A style with no triggerDelay keeps the IMMEDIATE trigger (the cast glow is
// the worked case: it is created at BeginCast and must not wait).
TEST_F(SpellLandingVolume, AStyleWithoutADelayStaysImmediate) {
    const auto glow = SpellPresets::CastGlow(glm::vec4(1.0f), 285);
    EXPECT_EQ(glow.emitters.at(0).trigger, SpellTrigger::IMMEDIATE);
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

}  // namespace
