// B1131 (#451, from the B1130 survey #450): the Source (cast) stage per
// spells.eff record.
//
// B1122 drew every record's Source as ONE body cloud (`anchor: "body"`) from
// BeginCast + 0.25 s, released 0.33 s after OP_Action. The B1130 survey cast a
// representative of every spells.eff record the level-1-60 spells use on the
// official client, with the target standing 1.4 caster heights away
// (review/B1130/anchor_table.tsv, record_table.tsv, strips/record_<N>.png):
//   - anchor class per record: hands 8, feet/ground ring 4, orbit 13, whole
//     body 14, head 1 (record 22's halo disc); record 51 (disciplines) was
//     measured by B1134 (#454) with the OBSERVER rig: feet/ground ring;
//   - onset BC+0.37-0.50 on 38 of 40 records (median 0.41);
//   - with ANOTHER target the caster's Source holds >= 4 s past OP_Action on
//     16 records, 2-4 s on 2, a lower bound <= 1 s on 9, ~0.3 s on record 22.
//
// THE SEAM is the real one: SpellVisualFX (EDT_NULL device) -> ParticleManager
// -> SimulationWorker, with entity positions fed back each frame from the same
// callback production installs (the caster, both hand bones, the head bone).
// The rows asserted here are the oracle's numbers, typed from anchor_table.tsv
// / record_table.tsv (fixed camera, caster heights above the feet), so a
// config edit that drifts from the survey goes red here as well.
//
// RED on fable b8c1719f (review/B1131/tests/): its config has no
// `sourceRecords` and its createCastGlow() draws every record as the body
// cloud from 0.25 s, released after 0.33 s whatever the target.

#include <gtest/gtest.h>

#include "client/graphics/environment/spell_effects_config.h"
#include "client/spell/spell_fx_timing.h"
#include "spell_fx_population_oracle.h"
#include "spell_fx_timing_oracle.h"
#include "spell_pool.h"
#include "spell_anim_rows.h"

#include <algorithm>
#include <iterator>
#include <cmath>
#include <map>
#include <string>

using EQT::Graphics::Environment::SpellEffectsConfig;
using Anchor = SpellEffectsConfig::SourceAnchor;

namespace {

// One measured row (anchor_table.tsv + record_table.tsv, fixed camera).
struct OracleRow {
    int record;
    uint32_t spell;     // the survey's representative cast
    Anchor anchor;
    float onset;        // BC+ s
    float holdOther;    // Act+ s, target = another entity (holdSelf when self-only)
    float lo, hi;       // lit extent, caster heights above the feet
    float halfWidth;    // lateral half-span, caster heights
    float centroid;     // lit centroid, caster heights
};

// The seven gate representatives (#451 note 7134): records 2, 1, 4, 9, 0, 5, 22.
const OracleRow kReps[] = {
    {2, 202, Anchor::Hands, 0.40f, 4.0f, 0.30f, 0.66f, 0.205f, 0.48f},     // r134 Courage
    {0, 93, Anchor::Hands, 0.46f, 4.0f, 0.07f, 1.25f, 0.59f, 0.71f},       // r51 Burst of Flame
    {1, 200, Anchor::FeetRing, 0.37f, 5.5f, 0.06f, 1.17f, 0.64f, 0.48f},   // r131 Minor Healing
    {4, 50, Anchor::Orbit, 0.38f, 0.33f, 0.10f, 1.86f, 1.195f, 0.82f},     // e4 Summon Food (self-only)
    {5, 80, Anchor::Orbit, 0.39f, 4.0f, 0.26f, 1.09f, 0.415f, 0.70f},      // e5 See Invisible
    {9, 375, Anchor::Body, 0.39f, 0.33f, 0.10f, 1.02f, 0.95f, 0.56f},      // r103 Fade (self-only)
    {22, 201, Anchor::Head, 0.46f, 0.31f, 0.03f, 1.09f, 0.375f, 0.68f},    // e22 Flash of Light
};

constexpr float kHoldSelf = 0.33f;  // B1122's merged-stage measurement (target on the caster)

class SourceRecordRows : public ::testing::Test {
protected:
    void SetUp() override {
        const int pool = spell_pool::Shipped(spell_pool::ThisBuildPlatform());
        ASSERT_GT(pool, 0);
        if (!spell_pool::LoadAt(pool)) GTEST_SKIP() << "no config/spell_effects.json";
    }
};

// Every one of the 41 records carries its measured row, in the survey's class
// counts; 51 is B1134's observer measurement (it used to stand in as record 13).
TEST_F(SourceRecordRows, EverySurveyedRecordHasItsMeasuredRow) {
    const auto& cfg = SpellEffectsConfig::instance();
    const int surveyed[] = {4, 2, 7, 51, 1, 9, 10, 12, 20, 40, 25, 18, 16, 0, 14, 5, 17, 6, 19, 23, 43,
                            31, 13, 3, 42, 15, 24, 21, 30, 32, 41, 11, 22, 35, 27, 33, 8, 29, 28, 34, 26};
    ASSERT_EQ(cfg.sourceRecordCount(), 41u) << "config/spell_effects.json sourceRecords.records";
    std::map<Anchor, int> classes;
    for (int r : surveyed) {
        const auto& row = cfg.sourceRecord(r);
        EXPECT_EQ(row.record, r) << "record " << r << " has no row";
        EXPECT_TRUE(row.hasShape) << r;
        ++classes[row.anchor];
        // B1145 (#465): the rows carry no measured timing any more: every
        // row starts at the stage latency (the default row's onset) and holds
        // the computed cast-animation term; the measured onsets (0.37..0.80)
        // and holds are tests/spell_fx_timing_oracle.h's bands.
        EXPECT_NEAR(row.onset, cfg.sourceStageLatency(), 1e-6f) << r;
        EXPECT_NEAR(row.holdSelf, EQ::kSpellFxCastAnimationSeconds, 1e-6f) << r;
        EXPECT_NEAR(row.holdOther, EQ::kSpellFxCastAnimationSeconds, 1e-6f) << r;
        // B1149 (#469): no row population; the measured coverage is a band.
        const auto band = std::find_if(std::begin(spell_fx_population_oracle::kRows),
                                       std::end(spell_fx_population_oracle::kRows),
                                       [r](const spell_fx_population_oracle::RecordRow& o) { return o.row == r; });
        ASSERT_NE(band, std::end(spell_fx_population_oracle::kRows)) << r;
        EXPECT_GT(band->sourceCoverage, 0.0f) << r;
    }
    EXPECT_EQ(classes[Anchor::Hands], 8);
    EXPECT_EQ(classes[Anchor::FeetRing], 5);  // 1, 16, 31, 32 + 51 (B1134)
    EXPECT_EQ(classes[Anchor::Orbit], 13);
    EXPECT_EQ(classes[Anchor::Body], 14);
    EXPECT_EQ(classes[Anchor::Head], 1);
    // B1134 (#454): record 51 (disciplines) is measured, no stand-in: the observer
    // saw a ring of blue-white flames at the feet (h -0.05..0.74, +-0.345) from
    // Act+0.7 after a 0-ms OP_BeginCast, lit to Act+8.04-8.28 on all four
    // discipline records (review/B1134/obs_rows.json); self-only, so no
    // other-target hold of its own.
    const auto& d = cfg.sourceRecord(51);
    EXPECT_EQ(d.measuredAs, -1);
    EXPECT_EQ(d.anchor, Anchor::FeetRing);
    EXPECT_NEAR(d.lo, -0.05f, 1e-3f);
    EXPECT_NEAR(d.hi, 0.74f, 1e-3f);
    EXPECT_NEAR(d.halfWidth, 0.345f, 1e-3f);
    EXPECT_NEAR(d.centroid, 0.21f, 1e-3f);
    // Its measured onset (Act+0.7 after a 0-ms OP_BeginCast) and hold (8.16 s)
    // were B1134's; B1145 times the discipline records from the file too.
    // B1131's measured holds: 15 rows held >= 4 s past OP_Action with another
    // target (record 35 a lower bound of 1.1) -- now the band the computed
    // Source end must cover (SpellFxTimingFiles.EveryMeasuredSourceHoldIs-
    // InsideTheComputedSourceEnd).
    int longHolds = 0;
    for (const auto& o : spell_fx_timing_oracle::Rows())
        if (o.rowHoldOther >= 4.0f) ++longHolds;
    EXPECT_EQ(longHolds, 15);
}

TEST_F(SourceRecordRows, TheRepresentativesCarryTheOraclesNumbers) {
    const auto& cfg = SpellEffectsConfig::instance();
    for (const auto& o : kReps) {
        const auto& row = cfg.sourceRecord(o.record);
        EXPECT_EQ(row.anchor, o.anchor) << o.record;
        // B1145: the measured onset is within 0.06 s of the stage latency the
        // row now starts at, and the computed hold covers the measured one.
        EXPECT_NEAR(row.onset, o.onset, 0.06f) << o.record;
        // (the hold is the emission's end; the measured one is the last lit
        // frame, up to a sprite life later: SpellFxTimingFiles checks it per slot)
        EXPECT_LE(o.holdOther, row.holdOther + 1.5f) << o.record;
        EXPECT_NEAR(row.lo, o.lo, 0.005f) << o.record;
        EXPECT_NEAR(row.hi, o.hi, 0.005f) << o.record;
        EXPECT_NEAR(row.centroid, o.centroid, 0.005f) << o.record;
        if (o.anchor != Anchor::Head) EXPECT_NEAR(row.halfWidth, o.halfWidth, 0.005f) << o.record;
    }
    const auto& halo = cfg.sourceRecord(22);
    EXPECT_NEAR(halo.discLo, 0.88f, 1e-3f);
    EXPECT_NEAR(halo.discHi, 1.12f, 1e-3f);
    EXPECT_NEAR(halo.discHalfWidth, 0.43f, 1e-3f);
}

// A record the survey did not reach (and a spell with no record) keeps the
// style's own body cloud, at the measured median onset and hold.
TEST_F(SourceRecordRows, AnUnsurveyedRecordIsTheMedianBodyCloud) {
    const auto& cfg = SpellEffectsConfig::instance();
    for (int r : {-1, 200, 255}) {
        const auto& row = cfg.sourceRecord(r);
        EXPECT_EQ(row.anchor, Anchor::Body) << r;
        EXPECT_FALSE(row.hasShape) << r;
        EXPECT_NEAR(row.onset, 0.41f, 1e-3f) << r;
        EXPECT_NEAR(row.holdOther, 4.0f, 1e-3f) << r;
        EXPECT_NEAR(row.holdSelf, kHoldSelf, 1e-3f) << r;
    }
}

}  // namespace

#if defined(EQT_HAS_GRAPHICS) && defined(EQT_HAS_GLES2)

#include <glm/glm.hpp>
#include <irrlicht.h>

#include "client/graphics/environment/particle_manager.h"
#include "client/graphics/environment/spell_particle_types.h"
#include "client/graphics/simulation_worker.h"
#include "client/graphics/spell_visual_fx.h"

#include <algorithm>
#include <memory>
#include <unordered_set>
#include <vector>

namespace {

constexpr uint16_t kCaster = 300;
constexpr uint16_t kOther = 301;  // the target standing apart
constexpr float kH = 6.5f, kA = 3.6f;
const glm::vec3 kCenter{10.0f, 5.0f, 20.0f};                  // entity anchor: feet at 1.4
const glm::vec3 kRightHand{11.5f, 4.0f, 20.0f};               // 0.40 caster heights up
const glm::vec3 kLeftHand{8.5f, 4.0f, 20.0f};
const glm::vec3 kHead{10.0f, 1.4f + 0.86f * 6.5f, 20.0f};     // the head joint, ~0.86 H

class SourceAnchors : public ::testing::Test {
protected:
    void SetUp() override {
        const int pool = spell_pool::Shipped(spell_pool::ThisBuildPlatform());
        ASSERT_GT(pool, 0);
        if (!spell_pool::LoadAt(pool)) GTEST_SKIP() << "no config/spell_effects.json";
        irr::SIrrlichtCreationParameters params;
        params.DriverType = irr::video::EDT_NULL;
        params.WindowSize = irr::core::dimension2d<irr::u32>(64, 64);
        device_ = irr::createDeviceEx(params);
        if (!device_) GTEST_SKIP() << "Failed to create EDT_NULL Irrlicht device";
        pm_ = std::make_unique<EQT::Graphics::Environment::ParticleManager>(
            device_->getSceneManager(), device_->getVideoDriver());
        pm_->Test_SetUnifiedRendererInitializedForSeam(true);
        pool_ = pool;
        worker_ = std::make_unique<EQT::Graphics::SimulationWorker>();
        out_ = EQT::Graphics::SimulationOutput{};
        requested_.clear();
        t_ = 0.0f;
        pm_->setEntityPositionCallback([](uint16_t id, glm::vec3& out) -> bool {
            if (id == kCaster || id == kOther) { out = kCenter; return true; }
            if (id == EQ::SpellVisualFX::rightHandEntityId(kCaster)) { out = kRightHand; return true; }
            if (id == EQ::SpellVisualFX::leftHandEntityId(kCaster)) { out = kLeftHand; return true; }
            if (id == EQ::SpellVisualFX::headEntityId(kCaster)) { out = kHead; return true; }
            return false;
        });
        fx_ = std::make_unique<EQ::SpellVisualFX>(device_->getSceneManager(),
                                                  device_->getVideoDriver(), std::string());
        fx_->setParticleManager(pm_.get());
    }
    void TearDown() override {
        fx_.reset();
        pm_.reset();
        worker_.reset();
        if (device_) device_->drop();
        device_ = nullptr;
    }

    void Note(const OracleRow& o) {
        EQ::SpellVisualVocabulary v;
        v.known = true;
        v.school = EQ::SpellSchool::Evocation;  // castGlow: the spray style differs only in size/life
        v.spellAffectIndex = o.record;
        v.spellAnim = static_cast<int16_t>(spell_anim_rows::AnimOfRow(o.record));  // B1137: the row key
        fx_->noteSpellVocabulary(o.spell, v);
    }

    // One worker frame; entity positions are resolved for what the previous
    // frame requested, exactly as renderer_sim_bridge.cpp does.
    void Pump() {
        EQT::Graphics::SimulationInput input;
        auto& pi = input.particleInput;
        pi.deltaTime = 1.0f / 60.0f;
        pi.unifiedRendererInitialized = true;
        pi.poolSize = pool_;
        for (uint16_t id : requested_) {
            glm::vec3 p;
            if (pm_->resolveEntityPosition(id, p)) pi.entityPositions[id] = p;
        }
        pi.commands = pm_->drainCommands();
        worker_->computeOnce(input, out_);
        requested_ = out_.particleOutput.positionRequestEntities;
        t_ += pi.deltaTime;
    }
    void PumpFor(float s) {
        const int n = static_cast<int>(std::lround(s * 60.0f));
        for (int i = 0; i < n; ++i) Pump();
    }
    const std::vector<EQT::Graphics::Environment::UnifiedParticle>& Live() const {
        return out_.particleOutput.renderBuffer;
    }
    size_t Young(float age) const {
        size_t n = 0;
        for (const auto& p : Live()) if (p.age < age) ++n;
        return n;
    }

    irr::IrrlichtDevice* device_ = nullptr;
    std::unique_ptr<EQT::Graphics::Environment::ParticleManager> pm_;
    std::unique_ptr<EQ::SpellVisualFX> fx_;
    std::unique_ptr<EQT::Graphics::SimulationWorker> worker_;
    EQT::Graphics::SimulationOutput out_;
    std::unordered_set<uint16_t> requested_;
    int pool_ = 0;
    float t_ = 0.0f;
};

float Pct(std::vector<float> v, float q) {
    std::sort(v.begin(), v.end());
    return v[static_cast<size_t>(q * static_cast<float>(v.size() - 1))];
}

// ANCHOR CLASS: which point the Source follows. RED on b8c1719f: every record
// was one body cloud on the caster's entity.
TEST_F(SourceAnchors, EachClassFollowsItsOwnAnchor) {
    for (const auto& o : kReps) {
        SetUp();  // a fresh seam per record
        if (::testing::Test::IsSkipped()) return;
        Note(o);
        fx_->createCastGlow(kCaster, o.spell, 3000);
        PumpFor(0.7f);
        const auto& e = out_.particleOutput.positionRequestEntities;
        const bool onCaster = e.count(kCaster) > 0;
        const bool onHands = e.count(EQ::SpellVisualFX::rightHandEntityId(kCaster)) > 0 &&
                             e.count(EQ::SpellVisualFX::leftHandEntityId(kCaster)) > 0;
        const bool onHead = e.count(EQ::SpellVisualFX::headEntityId(kCaster)) > 0;
        switch (o.anchor) {
            case Anchor::Hands:
                EXPECT_TRUE(onHands) << o.record << ": not on the two hand bones";
                EXPECT_FALSE(onCaster) << o.record << ": a hands Source is not a body cloud";
                break;
            case Anchor::Head:
                EXPECT_TRUE(onHead) << o.record << ": not on the head bone";
                EXPECT_FALSE(onCaster) << o.record;
                break;
            default:
                EXPECT_TRUE(onCaster) << o.record;
                EXPECT_FALSE(onHands || onHead) << o.record;
        }
        TearDown();
    }
}

// SHAPE: the live sprites sit where the oracle drew them, in caster heights
// above the feet (lit extent lo..hi, half-width, centroid; the sprite's own
// size is not in a particle position, so the bands are the oracle's).
TEST_F(SourceAnchors, EachRecordIsShapedLikeTheOraclesRow) {
    for (const auto& o : kReps) {
        SetUp();
        if (::testing::Test::IsSkipped()) return;
        Note(o);
        fx_->createCastGlow(kCaster, o.spell, 3000);
        // Pool five samples 0.1 s apart (BC+1.4..1.8): one frame of a 40/s
        // hand spray is too few sprites for a stable percentile.
        PumpFor(1.4f);
        std::vector<EQT::Graphics::Environment::UnifiedParticle> pooled;
        for (int k = 0; k < 5; ++k) {
            if (k) PumpFor(0.1f);
            pooled.insert(pooled.end(), Live().begin(), Live().end());
        }
        ASSERT_GT(pooled.size(), 75u) << o.record;
        std::vector<float> ys, dx, rad;
        float ySum = 0.0f;
        size_t inDisc = 0;
        for (const auto& p : pooled) {
            const float y = (p.position.y - (kCenter.y - kA)) / kH;
            ys.push_back(y);
            ySum += y;
            dx.push_back(std::fabs(p.position.x - kCenter.x) / kH);
            rad.push_back(std::hypot(p.position.x - kCenter.x, p.position.z - kCenter.z) / kH);
            if (y >= 0.85f && y <= 1.15f) ++inDisc;
        }
        const float n = static_cast<float>(pooled.size());
        const float centroid = ySum / n;
        EXPECT_NEAR(centroid, o.centroid, 0.2f) << o.record << ": centroid";
        EXPECT_GE(Pct(ys, 0.05f), o.lo - 0.15f) << o.record << ": bottom";
        // A hand spray is centred on the measured CENTROID (not the box middle),
        // so its top may sit up to |centroid - mid| higher than the box's.
        const float topSlack = 0.2f + (o.anchor == Anchor::Hands
                                           ? std::fabs(o.centroid - 0.5f * (o.lo + o.hi)) + 0.05f : 0.0f);
        EXPECT_LE(Pct(ys, 0.95f), o.hi + topSlack) << o.record << ": top";
        EXPECT_LE(Pct(dx, 0.95f), o.halfWidth + 0.3f) << o.record << ": width";
        switch (o.anchor) {
            case Anchor::FeetRing:  // a ring round the feet: every sprite at the radius
            case Anchor::Orbit:
                EXPECT_NEAR(Pct(rad, 0.5f), o.halfWidth, 0.1f * o.halfWidth + 0.02f) << o.record;
                break;
            case Anchor::Head:      // the halo carries about half the sprites at head height
                EXPECT_GE(inDisc / n, 0.4f) << o.record << ": no halo disc at head height";
                EXPECT_LE(Pct(ys, 0.05f), 0.4f) << o.record << ": no column falling down the body";
                break;
            case Anchor::Hands: {    // born AT the hands
                size_t young = 0, atHand = 0;
                for (const auto& p : Live()) {
                    if (p.age > 0.05f) continue;
                    ++young;
                    const float d = std::min(glm::length(p.position - kRightHand),
                                             glm::length(p.position - kLeftHand));
                    if (d < 0.15f * kH) ++atHand;
                }
                ASSERT_GT(young, 0u) << o.record;
                EXPECT_EQ(atHand, young) << o.record << ": sprites born away from the hands";
                break;
            }
            case Anchor::Body:
                EXPECT_GE(Pct(dx, 0.95f), 0.5f * o.halfWidth) << o.record << ": not body-wide";
                break;
        }
        TearDown();
    }
}

// ONSET (B1145): nothing before the stage latency + the first slot's +0x8C
// (0 on every measured record), sprites within 0.1 s of it, and that start
// within 0.1 s of the record's measured onset (the band; B1131 started each
// record at its own measured onset, RED on b8c1719f: 0.25 for every record).
TEST_F(SourceAnchors, EachRecordStartsAtTheStageLatencyInsideItsMeasuredOnset) {
    const float latency = SpellEffectsConfig::instance().sourceStageLatency();
    for (const auto& o : kReps) {
        SetUp();
        if (::testing::Test::IsSkipped()) return;
        Note(o);
        fx_->createCastGlow(kCaster, o.spell, 3000);
        float first = -1.0f;
        for (int f = 0; f < 60 && first < 0.0f; ++f) {
            Pump();
            if (!Live().empty()) first = t_;
        }
        EXPECT_GE(first, latency - 1.0f / 60.0f) << o.record;
        EXPECT_LE(first, latency + 0.1f) << o.record;
        EXPECT_NEAR(first, o.onset, 0.1f) << o.record << ": outside the measured onset";
        TearDown();
    }
}

// HOLD: this seam draws the rows without spellsnew.eff (no field-145
// vocabulary), the path of a spell with no record: OP_Action releases the
// Source, which keeps FEEDING for its row's hold, then its sprites finish
// their life. B1145 (#465): a row's hold is the computed cast-animation term
// (EQ::kSpellFxCastAnimationSeconds) whatever the target -- the file's
// Source duration does not depend on it; B1131's measured holdOther /
// holdSelf are SpellFxTimingFiles' bands. (A spellsnew.eff Source is not
// released at all: SlotRules.ASpellsNewSourceRunsItsSlotsDurations.)
TEST_F(SourceAnchors, TheSourceHoldsItsRecordsTimePastTheAction) {
    const auto& cfg = SpellEffectsConfig::instance();
    for (const auto& o : kReps) {
        for (bool self : {false, true}) {
            SetUp();
            if (::testing::Test::IsSkipped()) return;
            Note(o);
            ASSERT_FALSE(fx_->stageSlots(o.spell, EQ::kSpellsNewEffSource, 60).fromFile);
            fx_->createCastGlow(kCaster, o.spell, 3000);
            PumpFor(1.5f);
            fx_->releaseCastGlow(kCaster, o.spell, self ? kCaster : kOther);  // OP_Action
            const float hold = cfg.sourceHold(fx_->sourceRecordOf(o.spell), self);
            EXPECT_NEAR(hold, EQ::kSpellFxCastAnimationSeconds, 1e-6f) << o.record;
            PumpFor(hold - 0.1f);
            EXPECT_GT(Young(0.2f), 0u) << o.record << (self ? " self" : " other")
                                       << ": stopped feeding before Act+" << hold;
            PumpFor(0.35f);
            EXPECT_EQ(Young(0.2f), 0u) << o.record << (self ? " self" : " other")
                                       << ": still feeding after Act+" << hold;
            PumpFor(1.3f);  // the longest Source life (castGlow 1.2 s)
            EXPECT_TRUE(Live().empty()) << o.record << ": sprites outlived hold + life";
            TearDown();
        }
    }
}

// An interrupt still frees the Source at once, whatever its anchor.
TEST_F(SourceAnchors, AnInterruptFreesEveryAnchor) {
    for (const auto& o : kReps) {
        SetUp();
        if (::testing::Test::IsSkipped()) return;
        Note(o);
        fx_->createCastGlow(kCaster, o.spell, 3000);
        PumpFor(1.0f);
        ASSERT_FALSE(Live().empty()) << o.record;
        fx_->removeCastGlow(kCaster);
        Pump();
        EXPECT_TRUE(Live().empty()) << o.record << ": " << Live().size() << " sprites outlived the interrupt";
        TearDown();
    }
}

}  // namespace

#endif  // EQT_HAS_GRAPHICS && EQT_HAS_GLES2
