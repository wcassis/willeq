/*
 * B1185 (#504): the Source stage's particle onset (the client's createCastGlow
 * Source branch: sourceLatency + the slot's +0x8C start, spell_visual_fx.cpp:684).
 *
 * The b1130 sweep's source_onset_s was the pixel measure's first-lit frame
 * (anchor.py's SOURCE onset). B1157's CastingAnim (the raised-arm swing)
 * lights the ROI at BC+0.100, so the pixel measure reports 0.100 instead of
 * the Source stage's 0.433 (the B1153-era's first visible particle frame).
 *
 * This test pins the committed B1153-era reference (refactor/B1178/tables/
 * gate_c_before.tsv, harness_source_onset_s = 0.433 s for record 0 (spell 93)
 * and the fixed_self records 6/42) and pins the client's rule that produces
 * it: the client's Source stage particle onset (sourceStageLatency + the
 * +0x8C start) sits inside the committed reference's band. The 0.433 s is
 * the client's 0.41 s rule plus the build-up time the particles need to
 * reach full alpha (0.023 s) -- a rendering property, not a tuned delay.
 */

#include <gtest/gtest.h>

#include <cmath>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

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

// The committed B1153-era harness measurement for one b1130 record
// (refactor/B1178/tables/gate_c_before.tsv, harness_source_onset_s column,
// 19th tab-separated field). -1 when the record has no row.
float committedHarnessOnset(int record) {
    std::ifstream in(WILLEQ_REPO_PATH("refactor/B1178/tables/gate_c_before.tsv"));
    std::string line;
    if (!std::getline(in, line)) return -1.0f;  // header
    while (std::getline(in, line)) {
        std::vector<std::string> c;
        std::string cell;
        std::istringstream ss(line);
        while (std::getline(ss, cell, '\t')) c.push_back(cell);
        if (c.size() < 19) continue;
        if (std::atoi(c[0].c_str()) == record)
            return std::atof(c[18].c_str());
    }
    return -1.0f;
}

}  // namespace

// record 80 = spell 93 (Burst of Flame), the b1130 sweep's record 0: the
// committed B1153-era reference is 0.433 s (oracle 0.460 s, PASS), and the
// client's rule (sourceLatency + spell 93's +0x8C start, 0 for emitter 276)
// = 0.41 s sits inside its band -- the quantity the sweep's source_onset_s
// reports after the fix.
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
    // The committed reference this card restores: 0.433 s.
    const float committed = committedHarnessOnset(0);
    ASSERT_GT(committed, 0.0f) << "record 0's row in gate_c_before.tsv";
    EXPECT_NEAR(committed, 0.433f, 1e-3f)
        << "the committed B1153-era reference onset";
    EXPECT_NEAR(onset, committed, 0.10f)
        << "the client's rule is inside the committed reference's band";
}

// The fixed_self records (6: spell 722, 42: spell 700): the committed
// reference stays 0.433 s, and a spell with no EDD +0x8C delay stays at the
// sourceLatency -- inside that band.
TEST(B1185SourceOnset, FixedSelfStays) {
    const float latency = sourceStageLatency();
    // A spell with no EDD entry: the particle onset = the sourceLatency.
    const float onset = sourceParticleOnset(99999, latency);
    EXPECT_NEAR(onset, latency, 1e-3f) << "no EDD entry -> the sourceLatency";
    for (const int rec : {6, 42}) {
        const float committed = committedHarnessOnset(rec);
        ASSERT_GT(committed, 0.0f) << "record " << rec << "'s row in gate_c_before.tsv";
        EXPECT_NEAR(committed, 0.433f, 1e-3f)
            << "record " << rec << "'s committed 0.433 s reference stays";
        EXPECT_NEAR(onset, committed, 0.10f)
            << "record " << rec << "'s client onset is inside its band";
    }
}
