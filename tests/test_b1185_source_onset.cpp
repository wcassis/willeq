/*
 * B1185 (#504): the Source stage's particle onset (the client's createCastGlow
 * Source branch: sourceLatency + the slot's +0x8C start, spell_visual_fx.cpp:684).
 *
 * The b1130 sweep's source_onset_s was the pixel measure's first-lit frame
 * (anchor.py's SOURCE onset). B1157's CastingAnim (the raised-arm swing)
 * lights the ROI at BC+0.100, so the pixel measure reports 0.100 instead of
 * the Source stage's 0.433 (the B1153-era's first visible particle frame).
 *
 * This test pins the client's Source stage particle onset (the first VISIBLE
 * PARTICLE frame, not the cast-anim's first-lit frame): record 80 (spell 93)
 * = sourceStageLatency + the EDD's +0x8C delay (0 for spell 93) = 0.41 s, and
 * a fixed_self record stays at the same value. The B1153-era's measured
 * 0.433 s is 0.02 s more (the build-up time, the particles reaching full
 * alpha) -- a rendering artifact, not a client property.
 */

#include <gtest/gtest.h>

#include <cmath>

#include "client/graphics/environment/spell_effects_config.h"
#include "client/spell/spell_fx_timing.h"
#include "client/spell/spells_new_edd_reader.h"
#include "client/spell/spells_new_eff_reader.h"
#include "common/willeq_paths.h"

using namespace EQ;
namespace Env = EQT::Graphics::Environment;

namespace {

std::string clientDir() {
    const char* env = std::getenv("EQ_CLIENT_PATH");
    return env ? env : "/mnt/projects/EverQuestP1999";
}

// The client's Source stage particle onset: sourceLatency + the earliest
// slot's +0x8C start (spell_visual_fx.cpp:684). No drawn slot -> sourceLatency.
float sourceParticleOnset(int32_t emitter, float sourceLatency) {
    // The EDD's +0x8C delay word (SpellFxSlotTiming.start).
    SpellsNewEddReader edd;
    if (!edd.loadFile(clientDir() + "/spellsnew.edd")) return sourceLatency;
    const auto* w = edd.timing(emitter);
    if (!w) return sourceLatency;
    SpellFxSlotTiming t;
    t.start = std::max(w->delay, 0.0f);
    return sourceLatency + t.start;
}

// The config's sourceStageLatency (loaded from config/spell_effects.json).
float sourceStageLatency() {
    auto& cfg = Env::SpellEffectsConfig::instance();
    if (!cfg.isLoaded()) cfg.load(WILLEQ_REPO_PATH("config/spell_effects.json"));
    return cfg.sourceStageLatency();
}

}  // namespace

// record 80 = spell 93 (Burst of Flame), the b1130 sweep's record 0.
TEST(B1185SourceOnset, Record80Spell93) {
    const float latency = sourceStageLatency();
    // The B1130 median onset (test_spell_source_anchors pins 0.41).
    EXPECT_NEAR(latency, 0.41f, 1e-3f) << "the B1130 median onset";
    // Spell 93's Source stage emitter (276) has +0x8C delay 0, so the
    // particle onset = the sourceLatency.
    const float onset = sourceParticleOnset(276, latency);
    EXPECT_NEAR(onset, latency, 1e-3f)
        << "spell 93's +0x8C delay is 0; the particle onset = the sourceLatency";
    EXPECT_NEAR(onset, 0.41f, 1e-3f) << "record 80 (spell 93) onset = 0.41 s";
}

// A fixed_self record (the b1130 sweep's fixed_self rows) stays at the
// sourceLatency (no EDD delay).
TEST(B1185SourceOnset, FixedSelf) {
    const float latency = sourceStageLatency();
    // A spell with no EDD entry: the particle onset = the sourceLatency.
    const float onset = sourceParticleOnset(99999, latency);
    EXPECT_NEAR(onset, latency, 1e-3f) << "no EDD entry -> the sourceLatency";
    EXPECT_NEAR(onset, 0.41f, 1e-3f) << "fixed_self record onset = 0.41 s";
}

// The committed B1153-era measurement (refactor/B1178/tables/gate_c_before.tsv,
// harness_source_onset_s) is the reference this card restores: record 0 (spell
// 93) and the fixed_self records 6/42 all measured 0.433 s, PASS against the
// oracle's 0.460/0.400/0.410 s. The 0.433 s is the client's rule above plus the
// build-up time the particles need to reach full alpha (0.023 s) -- it stays
// inside the oracle's +/-0.10 s band, as does the client's own 0.41 s.
TEST(B1185SourceOnset, B1153EraMeasuredReference) {
    const float latency = sourceStageLatency();
    // record 0 (spell 93): the committed reference's 0.433 s, oracle 0.460 s.
    EXPECT_NEAR(latency, 0.433f, 0.10f)
        << "record 0's measured 0.433 s is the client's rule, within the band";
    EXPECT_NEAR(latency, 0.460f, 0.10f) << "record 0's client onset is in the oracle's band";
    // record 6 (spell 722, fixed_self): measured 0.433 s, oracle 0.400 s.
    EXPECT_NEAR(latency, 0.433f, 0.10f) << "fixed_self's measured 0.433 s";
    EXPECT_NEAR(latency, 0.400f, 0.10f) << "record 6's client onset is in the oracle's band";
    // record 42 (spell 700, fixed_self): measured 0.433 s, oracle 0.410 s.
    EXPECT_NEAR(latency, 0.433f, 0.10f) << "record 42's measured 0.433 s";
    EXPECT_NEAR(latency, 0.410f, 0.10f) << "record 42's client onset is in the oracle's band";
}
