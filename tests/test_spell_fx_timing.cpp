/*
 * B1145 (#465): spell FX timing from spellsnew.edd.
 *
 * The rules (include/client/spell/spell_fx_timing.h, read from the client's
 * code by B1141: review/B1141/edd_fields_from_code.tsv +0x78/+0x7C/+0x80..
 * +0x8C, eff_fields_from_code.tsv "call args"), the EDD reader's decode of
 * those words on the shipped file, and the CROSS-CHECK against the official
 * client's measured timing (tests/spell_fx_timing_oracle.h, generated):
 *   - the computed Target end of the 39 B1130 records against their measured
 *     landing end reproduces B1141's crosscheck.tsv (median -0.11 s,
 *     Spearman 0.834, 25 within 0.5 s, 29 within 1.0 s) through this code;
 *   - the two outliers are explained from the file plus the capture: record
 *     90 (f123 3) is a rain whose second wave lands 2.501 s after the first
 *     (pcap), record 100 (f123 41) was measured in the target ROI while its
 *     whole frame runs to the file's end;
 *   - 93 at level 1 draws only its level-0 Target slots, which end where the
 *     B1144 observer leg measured the landing (Act+2.61 / +2.65);
 *   - every measured Source hold (B1131's "still lit at Act+x" lower bounds)
 *     is inside the computed Source end.
 * RED before B1145: none of the timing words was decoded.
 */

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <numeric>
#include <string>
#include <vector>

#include "client/spell/spell_fx_timing.h"
#include "client/spell/spells_new_edd_reader.h"
#include "client/spell/spells_new_eff_reader.h"
#include "spell_fx_timing_oracle.h"

using namespace EQ;
namespace oracle = spell_fx_timing_oracle;

namespace {

std::string clientDir() {
    const char* env = std::getenv("EQ_CLIENT_PATH");
    return env ? env : "/mnt/projects/EverQuestP1999";
}

SpellsNewEddTiming words(float duration, float lifetime, int burst, int perTick, float rate, float delay) {
    SpellsNewEddTiming w;
    w.duration = duration;
    w.lifetime = lifetime;
    w.burst = burst;
    w.perTick = perTick;
    w.ticksPerSecond = rate;
    w.delay = delay;
    return w;
}

// Spearman's rho with average ranks (B1141's crosscheck.py breaks ties by order;
// the 39 rows have ties, so both are printed).
double spearman(const std::vector<double>& a, const std::vector<double>& b) {
    auto rank = [](const std::vector<double>& v) {
        std::vector<size_t> idx(v.size());
        std::iota(idx.begin(), idx.end(), 0);
        std::stable_sort(idx.begin(), idx.end(), [&](size_t x, size_t y) { return v[x] < v[y]; });
        std::vector<double> r(v.size());
        for (size_t n = 0; n < idx.size(); ++n) r[idx[n]] = static_cast<double>(n);
        return r;
    };
    const auto ra = rank(a), rb = rank(b);
    const double n = static_cast<double>(a.size());
    double d2 = 0.0;
    for (size_t i = 0; i < a.size(); ++i) d2 += (ra[i] - rb[i]) * (ra[i] - rb[i]);
    return 1.0 - 6.0 * d2 / (n * (n * n - 1.0));
}

double median(std::vector<double> v) {
    std::sort(v.begin(), v.end());
    const size_t n = v.size();
    return n % 2 ? v[n / 2] : 0.5 * (v[n / 2 - 1] + v[n / 2]);
}

}  // namespace

// ---------------------------------------------------------------------------
// The rules, on words chosen to exercise each branch.
// ---------------------------------------------------------------------------

TEST(SpellFxTimingRules, ANegativeDurationIsAFixedRunWhateverTheCallerPasses) {
    const auto t = SpellFxSlotTimingFor(words(-5.0f, 1.5f, 2, 2, 28.0f, 0.25f), 9000);
    EXPECT_TRUE(t.fixed);
    EXPECT_FLOAT_EQ(t.run, 5.0f);
    EXPECT_FLOAT_EQ(t.start, 0.25f);
    EXPECT_FLOAT_EQ(t.window(), 4.75f);
    EXPECT_FLOAT_EQ(t.lifetime, 1.5f);
    EXPECT_FLOAT_EQ(t.end(), 6.5f) << "the last particle dies T + L after the stage starts";
    EXPECT_FLOAT_EQ(t.streamRate(), 56.0f);
    EXPECT_FLOAT_EQ(t.peakLive(), 2.0f + 56.0f * 1.5f);
}

TEST(SpellFxTimingRules, ANonNegativeDurationTakesTheCallersWhenItGivesOne) {
    const auto caller = SpellFxSlotTimingFor(words(0.0f, 1.0f, 2, 1, 32.0f, 0.0f), 5850);
    EXPECT_FALSE(caller.fixed);
    EXPECT_FLOAT_EQ(caller.run, 5.85f);
    const auto none = SpellFxSlotTimingFor(words(2.0f, 1.0f, 2, 1, 32.0f, 0.0f), 0);
    EXPECT_FLOAT_EQ(none.run, 2.0f) << "no caller duration: the file's own +0x78";
    // 281's +0x78 is 9.86e-32: no caller (Target) -> a zero run, the burst only.
    const auto burstOnly = SpellFxSlotTimingFor(words(9.86076e-32f, 1.0f, 1, 0, 1.0f, 0.0f), 0);
    EXPECT_TRUE(burstOnly.emits());
    EXPECT_NEAR(burstOnly.end(), 1.0f, 1e-6f);
    EXPECT_FLOAT_EQ(burstOnly.peakLive(), 1.0f);
}

TEST(SpellFxTimingRules, ANegativeLifetimeLivesTheRunAndADelayPastTheRunEmitsNothing) {
    const auto life = SpellFxSlotTimingFor(words(-3.0f, -1.0f, 4, 0, 0.0f, 0.0f), 0);
    EXPECT_FLOAT_EQ(life.lifetime, 3.0f);
    const auto late = SpellFxSlotTimingFor(words(-1.0f, 1.0f, 4, 4, 10.0f, 2.0f), 0);
    EXPECT_LT(late.window(), 0.0f);
    EXPECT_FALSE(late.emits());
    EXPECT_FLOAT_EQ(late.peakLive(), 0.0f);
}

TEST(SpellFxTimingRules, TheStageDurationsAreTheOnesEqgamePasses) {
    EXPECT_EQ(SpellFxCallerDurationMs(SpellFxStage::Source, 1500, 0), 1500 + 4350)
        << "cast time + the measured cast-animation term";
    EXPECT_EQ(SpellFxCallerDurationMs(SpellFxStage::Middle, 1500, 7500), 7500) << "spell field 18";
    EXPECT_EQ(SpellFxCallerDurationMs(SpellFxStage::Target, 1500, 7500), 0);
}

// ---------------------------------------------------------------------------
// The shipped files.
// ---------------------------------------------------------------------------

class SpellFxTimingFiles : public ::testing::Test {
protected:
    void SetUp() override {
        if (!std::ifstream(clientDir() + "/spellsnew.edd").good())
            GTEST_SKIP() << "spellsnew.edd not under " << clientDir();
        ASSERT_TRUE(reader.load(clientDir())) << reader.loadError();
    }
    // The computed end of stage `stage` of record `rec` for a caster of gate
    // level `level`, the latest slot's (s after the stage starts).
    float stageEnd(int rec, int stage, int level, int callerMs, int* slots = nullptr) const {
        float end = -1.0f;
        int n = 0;
        for (const auto& d : reader.drawnSlots(rec, stage, level)) {
            const SpellsNewEddTiming* w = reader.edd().timing(d.slot.emitter);
            if (!w) continue;
            end = std::max(end, SpellFxSlotTimingFor(*w, callerMs).end());
            ++n;
        }
        if (slots) *slots = n;
        return end;
    }
    SpellsNewEffReader reader;
};

TEST_F(SpellFxTimingFiles, TheReaderDecodesSpell93sEmitters) {
    const auto& edd = reader.edd();
    ASSERT_GT(edd.emitterCount(), 600u);
    const SpellsNewEddTiming* t280 = edd.timing(280);  // record 80 Target slot 0, spelcc
    ASSERT_NE(t280, nullptr);
    EXPECT_FLOAT_EQ(t280->duration, -1.0f);
    EXPECT_FLOAT_EQ(t280->lifetime, 1.5f);
    EXPECT_EQ(t280->burst, 50);
    EXPECT_EQ(t280->perTick, 50);
    EXPECT_FLOAT_EQ(t280->ticksPerSecond, 30.0f);
    EXPECT_FLOAT_EQ(t280->delay, 0.25f);
    const SpellsNewEddTiming* t282 = edd.timing(282);  // level-39 slot, the 5 s one
    ASSERT_NE(t282, nullptr);
    EXPECT_FLOAT_EQ(t282->duration, -5.0f);
    EXPECT_EQ(edd.texture(282), "spelcf.png");
    const SpellsNewEddTiming* t276 = edd.timing(276);  // the Source hand flame
    ASSERT_NE(t276, nullptr);
    EXPECT_FLOAT_EQ(t276->duration, 0.0f) << "a Source emitter takes the caller's duration";
    EXPECT_FLOAT_EQ(t276->lifetime, 1.0f);
    EXPECT_EQ(edd.timing(0), nullptr);
}

TEST_F(SpellFxTimingFiles, TheTargetEndReproducesB1141sCrossCheck) {
    std::vector<double> pred, meas, diff;
    int within05 = 0, within10 = 0;
    for (const auto& r : oracle::Rows()) {
        if (r.row == 40) continue;  // the port: B1141 had no landing end for it (B1134's 0.73 is the caster leaving)
        ASSERT_GT(r.landingEnd, oracle::kNone) << r.row;
        int n = 0;
        const float p = stageEnd(r.f145, kSpellsNewEffTarget, 60, 0, &n);
        ASSERT_GT(n, 0) << "record " << r.f145 << " has no Target slot";
        pred.push_back(p);
        meas.push_back(r.landingEnd);
        diff.push_back(r.landingEnd - p);
        within05 += std::fabs(r.landingEnd - p) <= 0.5f;
        within10 += std::fabs(r.landingEnd - p) <= 1.0f;
    }
    ASSERT_EQ(pred.size(), 39u);
    // WILLEQ_FX_TIMING_TSV=<file>: the cross-check table (review/B1145/crosscheck.tsv).
    if (const char* out = std::getenv("WILLEQ_FX_TIMING_TSV")) {
        std::ofstream f(out);
        f << "f145\trow\tspell\tcast_ms\ttarget_end_l60\ttarget_end_l1\toracle_landing_end\toracle_minus_l60"
             "\tsource_end_past_act\toracle_source_end\tcast_s\toracle_minus_source\n";
        for (const auto& r : oracle::Rows()) {
            const float l60 = stageEnd(r.f145, kSpellsNewEffTarget, 60, 0);
            const float l1 = stageEnd(r.f145, kSpellsNewEffTarget, 1, 0);
            const float cast = r.castS > oracle::kNone ? r.castS : r.castMs * 0.001f;
            const int callerMs = SpellFxCallerDurationMs(SpellFxStage::Source, static_cast<int>(cast * 1000.0f + 0.5f), 0);
            const float src = stageEnd(r.f145, kSpellsNewEffSource, 60, callerMs) - cast;
            auto num = [](float v) { return v > oracle::kNone ? std::to_string(v) : std::string("-"); };
            f << r.f145 << '\t' << r.row << '\t' << r.spell << '\t' << r.castMs << '\t' << l60 << '\t'
              << (l1 >= 0.0f ? std::to_string(l1) : std::string("-")) << '\t' << num(r.landingEnd) << '\t'
              << (r.landingEnd > oracle::kNone ? std::to_string(r.landingEnd - l60) : std::string("-")) << '\t'
              << src << '\t' << num(r.sourceEnd) << '\t' << num(r.castS) << '\t'
              << (r.sourceEnd > oracle::kNone ? std::to_string(r.sourceEnd - src) : std::string("-")) << '\n';
        }
    }
    EXPECT_NEAR(median(diff), -0.11, 0.006);
    EXPECT_NEAR(std::accumulate(diff.begin(), diff.end(), 0.0) / diff.size(), -0.12, 0.01);
    EXPECT_EQ(within05, 25);
    EXPECT_EQ(within10, 29);
    EXPECT_NEAR(spearman(pred, meas), 0.834, 0.01);
}

TEST_F(SpellFxTimingFiles, TheTwoOutliersAreTheCapturesNotTheFile) {
    // Record 90 (f123 3, 377 Icestrike): a rain (spell field 18 = 7500 ms).
    // The pcap (review/B1130/oracle/e3/all-opcodes.tsv) has a second wave of
    // OP_Action 2.501 s after the first; each plays the Target stage again.
    const float rain = stageEnd(90, kSpellsNewEffTarget, 60, 0);
    EXPECT_NEAR(rain, 6.4f, 1e-4f);
    EXPECT_NEAR(2.501f + rain, 9.00f, 0.15f) << "the second wave's landing ends where the capture did";
    // Record 100 (f123 41, 703 Chords of Dissonance, a PB-AE song): the B1130
    // RUN line (2.75) is the TARGET ROI; the capture's whole crop
    // (review/B1130/measure/e41_fixed_npc/lit80.tsv) stays lit until Act+5.67
    // (2703 px) and is dark at Act+5.75 (83 px): the file's T=5 slots.
    const float song = stageEnd(100, kSpellsNewEffTarget, 60, 0);
    EXPECT_NEAR(song, 5.5f, 1e-4f);
    EXPECT_NEAR(5.71f - song, -0.11f, 0.35f) << "within the 39 records' own spread of the median";
}

// B1179 (#498): the five b1130 rows that end "later" than the oracle in
// refactor/B1171/tables/b1130_table.tsv. Each decodes (the verified EDD/EFF
// indexing, a level-60 caster, all Target slots level-gated <= 60) to the
// file's nominal Target death -- the UPPER BOUND on the measured visible end,
// because the alpha fades to 0 over the fade-out before the hard death, so the
// instrument's threshold crossing is earlier. No lifetime word is mis-applied:
// fable draws the full file lifetime and its measured end is the nominal death
// minus the fade, as the client's. refactor/B1179/README.md has the per-record
// words; the r41 ROI and the r40 port are pinned by the surrounding tests.
TEST_F(SpellFxTimingFiles, TheFiveB1130LandingRowsDecodeToTheOracleSpread) {
    auto rowById = [](int id) -> const oracle::Row* {
        for (const auto& r : oracle::Rows())
            if (r.row == id) return &r;
        return nullptr;
    };
    // (row, the file's nominal Target death, the oracle's measured visible end,
    // the max allowed gap = the latest slot's life + fade).
    struct Case { int row; float end; float oracle; float maxGap; };
    const std::vector<Case> withinSpread = {
        {6,  6.75f, 6.260f, 3.35f},  // e6  Jaxan: T=5 slots' 1.75 life + 1.60 fade
        {10, 7.00f, 5.720f, 2.75f},  // r43 Illusion: the T=5 slot's 2.00 life
        {29, 8.00f, 7.340f, 2.75f},  // e29 Shallow Breath: the T=6 slots' 2.00 life
        {43, 5.75f, 4.430f, 3.20f},  // e43 Lyssa: the long slot's 3.00 life
    };
    for (const auto& c : withinSpread) {
        const auto* r = rowById(c.row);
        ASSERT_NE(r, nullptr) << "row " << c.row;
        const float p = stageEnd(r->f145, kSpellsNewEffTarget, 60, 0);
        EXPECT_FLOAT_EQ(p, c.end) << "row " << c.row << ": the file's nominal Target death";
        EXPECT_LT(r->landingEnd, p) << "row " << c.row
                                    << ": the measured visible end is below the nominal death (the fade)";
        EXPECT_LE(p - r->landingEnd, c.maxGap + 0.01f) << "row " << c.row
                                                       << ": within the latest slot's life + fade";
    }
    // r41 (row 41, 703 Chords): the B1130 RUN line (2.75) is the TARGET ROI; the
    // whole crop runs to the file's 5.5 (the T=5 slots). The +2.8 in the table is
    // that ROI measurement, not a lifetime defect.
    const auto* r41 = rowById(41);
    ASSERT_NE(r41, nullptr);
    EXPECT_FLOAT_EQ(stageEnd(r41->f145, kSpellsNewEffTarget, 60, 0), 5.5f);
    EXPECT_NEAR(5.71f - 5.5f, -0.11f, 0.35f) << "the whole crop (5.71) is inside the spread";
    EXPECT_LT(r41->landingEnd, 5.5f) << "the TARGET-ROI line (2.75) is below the file's end";
    // r40 (row 40, 602 Evacuate, the port): the file's nominal death is 6.00, but
    // the measured 0.73 is the caster leaving (model gone by 0.79-0.81), not the
    // emitters running out. --caster-despawn at the model-gone time ends fable's
    // landing where the oracle's does; the mechanism is pinned by
    // SlotRules.ADeletedSpawnTakesItsAttachedEffectsWithIt.
    const auto* r40 = rowById(40);
    ASSERT_NE(r40, nullptr);
    EXPECT_FLOAT_EQ(stageEnd(r40->f145, kSpellsNewEffTarget, 60, 0), 6.0f);
    EXPECT_NEAR(r40->landingEnd, 0.73f, 0.01f) << "the port's measured end is the caster leaving";
}

TEST_F(SpellFxTimingFiles, Spell93AtLevelOneEndsWhereTheObserverSawIt) {
    // Record 80 at level 1: Target slots 0 (280: 1 s fixed, 0.25 s delay,
    // 1.5 s life) and 1 (281: a one-particle burst living 1 s).
    int n = 0;
    const float l1 = stageEnd(80, kSpellsNewEffTarget, 1, 0, &n);
    EXPECT_EQ(n, 2);
    EXPECT_FLOAT_EQ(l1, 2.5f);
    // B1144's lane leg (review/B1144/oracle/l1_l60_93, the observer's ROI):
    // the level-1 landing ran to Act+2.65 and Act+2.61.
    for (float measured : {2.65f, 2.61f}) EXPECT_NEAR(measured - l1, 0.13f, 0.05f);
    // At level 60 the 5 s level-39 slot (282) ends it: the B1130 capture's 6.67.
    EXPECT_FLOAT_EQ(stageEnd(80, kSpellsNewEffTarget, 60, 0), 6.5f);
}

TEST_F(SpellFxTimingFiles, EveryMeasuredSourceHoldIsInsideTheComputedSourceEnd) {
    // A Source slot with +0x78 >= 0 runs cast + the cast-animation term from
    // OP_BeginCast, so it emits until ~OP_Action + 4.35 s and its particles
    // live L more. B1131's holdOther is the last frame the caster's Source
    // was still lit with another target -- a lower bound.
    int checked = 0;
    for (const auto& r : oracle::Rows()) {
        if (r.rowHoldOther <= 0.33f + 1e-3f) continue;  // self-only rows: no separable hold
        const int callerMs = SpellFxCallerDurationMs(SpellFxStage::Source, r.castMs, 0);
        const float end = stageEnd(r.f145, kSpellsNewEffSource, 60, callerMs);
        // Times from OP_BeginCast; the measured hold is from OP_Action.
        EXPECT_GE(end - r.castMs * 0.001f + 0.1f, r.rowHoldOther) << "row " << r.row;
        ++checked;
    }
    EXPECT_EQ(checked, 23) << "the 23 rows B1131 measured an other-target hold on";
}

TEST_F(SpellFxTimingFiles, TheSourceEndsAFixedTermAfterTheActionWhateverTheCastTime) {
    // The B1145 caster-box ends (review/B1145/cast_anim/source_end.tsv): casts
    // of 1.0 .. 5.9 s, yet the Source stops a cast-animation term after
    // OP_Action -- the duration is cast + that term, not a constant from
    // OP_BeginCast. 93 (1.51 s) and 378 (3.01 s) and 73 (5.93 s):
    for (int row : {0, 18, 28}) {
        const auto it = std::find_if(oracle::Rows().begin(), oracle::Rows().end(),
                                     [&](const oracle::Row& r) { return r.row == row; });
        ASSERT_NE(it, oracle::Rows().end());
        ASSERT_GT(it->sourceEnd, oracle::kNone);
        EXPECT_GT(it->sourceEnd, kSpellFxCastAnimationSeconds) << row;
        EXPECT_LT(it->sourceEnd, kSpellFxCastAnimationSeconds + 2.1f) << row;
    }
}
