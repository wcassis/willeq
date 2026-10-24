// B1132 (#452, from the B1130 survey #450): the Target (landing) stage per
// spells.eff record.
//
// Until B1132 fable drew every landing the same way (B1120's volume): the
// impact half on the target's entity and the spellComplete half on the
// CASTER's -- so an other-target cast lit the caster to Act+~5 (B1131's gate,
// record 22) while the target got half a landing. The B1130 survey cast every
// record the level-1-60 spells use on the official client with the target
// standing 1.4 caster heights away (review/B1130/anchor_table.tsv
// target_anchor, the landing ROI's RUN line in measure/<capture>_*.txt):
//   - whole body 35 records (27 of them round the CASTER: its PB-AE landing
//     is not drawn on the NPC it hits; 8/9/11/19/43 are self-only);
//   - head/eyes 1: record 5 (See Invisible family) starts at the target's FACE
//     (e5 fixed_npc f8233, Act+0.41) and stays round head and shoulders
//     (run h 0.66..1.04 target heights, centroid 0.87);
//   - feet/ground 2: record 16 (Snare) rings on the ground climbing the legs,
//     record 28 (Gravity Flux) ground rings + a body column;
//   - column above 1: record 3 (Icestrike) a sheet sweeping down over the target;
//   - measured by B1134 (#454) with the OBSERVER rig, a second player
//     character on a second official client: 40 (ports: round the caster plus
//     a column above its head, gone with the caster at Act+0.73) and 51
//     (disciplines: no Target stage, nothing drawn at OP_Action).
// Records 14 (Bind Sight) and 7 (Insight) are NOT head-anchored.
//
// THE SEAM is the real one: SpellVisualFX (EDT_NULL device) -> ParticleManager
// -> SimulationWorker, with the caster, the target and the target's head bone
// at distinct positions fed back each frame.
//
// RED on fable 4a5246c1 (review/B1132/tests/): its config has no
// `targetRecords` and its code has no createLanding(); its bridge drew
// spellComplete on the caster of every cast.

#include <gtest/gtest.h>

#include "client/graphics/environment/spell_effects_config.h"
#include "spell_pool.h"
#include "spell_anim_rows.h"
#include "spell_fx_population_oracle.h"
#include "spell_fx_timing_oracle.h"

#include <algorithm>

#include <cmath>
#include <fstream>
#include <map>
#include <sstream>
#include <string>

using EQT::Graphics::Environment::SpellEffectsConfig;
using TAnchor = SpellEffectsConfig::TargetAnchor;

namespace {

struct LandingRow {
    int record;
    uint32_t spell;      // the survey's representative cast
    TAnchor anchor;
    float lo, hi, halfWidth, centroid;  // target heights above the target's feet
};

// Typed from the survey (gen_target_records.py reads the same RUN lines).
const LandingRow kRows[] = {
    {5, 80, TAnchor::Head, 0.66f, 1.04f, 0.35f, 0.87f},          // e5 fixed_npc RUN f8223-f8362
    {16, 242, TAnchor::FeetGround, 0.21f, 1.03f, 0.565f, 0.53f}, // e16 fixed_npc RUN
    {28, 73, TAnchor::FeetGround, 0.20f, 0.85f, 0.52f, 0.52f},   // e28 fixed_npc RUN
    {3, 377, TAnchor::Column, 0.0f, 1.82f, 1.52f, 0.91f},        // e3: the capture crop (saturated)
    {27, 290, TAnchor::Caster, 0.10f, 1.47f, 0.97f, 0.72f},      // e27 fixed_self RUN (round the caster)
    {12, 208, TAnchor::Body, 0.22f, 1.29f, 0.575f, 0.72f},       // r40 fixed_npc RUN
    {0, 93, TAnchor::Body, 0.25f, 1.46f, 0.46f, 0.80f},          // r51 fixed_npc RUN
    {22, 201, TAnchor::Body, 0.27f, 1.49f, 0.56f, 0.87f},        // e22 fixed_npc RUN
};

class TargetRecordRows : public ::testing::Test {
protected:
    void SetUp() override {
        const int pool = spell_pool::Shipped(spell_pool::ThisBuildPlatform());
        ASSERT_GT(pool, 0);
        if (!spell_pool::LoadAt(pool)) GTEST_SKIP() << "no config/spell_effects.json";
    }
};

// All 41 records carry a row, in the survey's class counts.
TEST_F(TargetRecordRows, EverySurveyedRecordHasItsLandingRow) {
    const auto& cfg = SpellEffectsConfig::instance();
    ASSERT_EQ(cfg.targetRecordCount(), 41u) << "config/spell_effects.json targetRecords.records";
    std::map<TAnchor, int> classes;
    int unmeasured = 0;
    const int all[] = {4, 2, 7, 51, 1, 9, 10, 12, 20, 40, 25, 18, 16, 0, 14, 5, 17, 6, 19, 23, 43,
                       31, 13, 3, 42, 15, 24, 21, 30, 32, 41, 11, 22, 35, 27, 33, 8, 29, 28, 34, 26};
    for (int r : all) {
        const auto& row = cfg.targetRecord(r);
        EXPECT_EQ(row.record, r) << r;
        if (row.unmeasured) { ++unmeasured; continue; }
        ++classes[row.anchor];
        if (r == 51) continue;  // no Target stage: no extent (B1134)
        EXPECT_GT(row.hi, row.lo) << r;
        EXPECT_GT(row.halfWidth, 0.0f) << r;
    }
    EXPECT_EQ(unmeasured, 0) << "B1134 measured 40 and 51";
    EXPECT_EQ(classes[TAnchor::Body] + classes[TAnchor::Caster], 37) << "whole body, B1130 + 40/51";
    EXPECT_EQ(classes[TAnchor::Caster], 8) << "27 + the self-only 8/9/11/19/43 + 40/51";
    EXPECT_EQ(classes[TAnchor::Head], 1);
    EXPECT_EQ(classes[TAnchor::FeetGround], 2);
    EXPECT_EQ(classes[TAnchor::Column], 1);
}

// B1134 (#454): the two records the B1130 survey's own camera could not see,
// measured on the OBSERVER's frames (review/B1134/obs_rows.json). Record 40
// (ports): the landing is round the CASTER and reaches over its head, rises at
// OP_Action and ends with the caster's model (it zones 0.16 s after
// OP_Action; nothing stays at the spot). Record 51 (disciplines): no Target
// stage at all.
TEST_F(TargetRecordRows, ThePortAndDisciplineRowsAreTheObserversNumbers) {
    const auto& cfg = SpellEffectsConfig::instance();
    const auto& port = cfg.targetRecord(40);
    EXPECT_FALSE(port.unmeasured);
    EXPECT_EQ(port.anchor, TAnchor::Caster);
    EXPECT_NEAR(port.lo, 0.07f, 1e-3f);
    EXPECT_NEAR(port.hi, 1.67f, 1e-3f);
    EXPECT_NEAR(port.halfWidth, 0.615f, 1e-3f);
    EXPECT_NEAR(port.centroid, 0.87f, 1e-3f);
    // B1145 (#465): a landing's timing is its spellsnew.edd slots' (record 78's
    // run 6 s); the observer's onset 0.07 / end 0.73 -- the caster zones out
    // 0.16 s after OP_Action and its landing goes with it -- are kept as the
    // measured band (tests/spell_fx_timing_oracle.h), not config keys.
    EXPECT_EQ(port.onset, 0.0f);
    EXPECT_EQ(port.end, 0.0f);
    const auto it = std::find_if(spell_fx_timing_oracle::Rows().begin(), spell_fx_timing_oracle::Rows().end(),
                                 [](const spell_fx_timing_oracle::Row& r) { return r.row == 40; });
    ASSERT_NE(it, spell_fx_timing_oracle::Rows().end());
    EXPECT_NEAR(it->rowTargetOnset, 0.07f, 1e-3f);
    EXPECT_NEAR(it->rowTargetEnd, 0.73f, 1e-3f);
    EXPECT_LT(it->rowTargetEnd, 1.0f) << "the port landing ends with the caster, not ~6 s later";
    const auto& disc = cfg.targetRecord(51);
    EXPECT_FALSE(disc.unmeasured);
    EXPECT_EQ(disc.anchor, TAnchor::Caster);
    const auto band = std::find_if(std::begin(spell_fx_population_oracle::kRows),
                                   std::end(spell_fx_population_oracle::kRows),
                                   [](const spell_fx_population_oracle::RecordRow& o) { return o.row == 51; });
    ASSERT_NE(band, std::end(spell_fx_population_oracle::kRows));
    EXPECT_EQ(band->landingCoverage, 0.0f) << "no Target-stage emitter: nothing at OP_Action";
}

TEST_F(TargetRecordRows, EachRecordCarriesTheOraclesAnchorAndExtent) {
    const auto& cfg = SpellEffectsConfig::instance();
    for (const auto& o : kRows) {
        const auto& row = cfg.targetRecord(o.record);
        EXPECT_EQ(row.anchor, o.anchor) << o.record;
        EXPECT_NEAR(row.lo, o.lo, 0.005f) << o.record;
        EXPECT_NEAR(row.hi, o.hi, 0.005f) << o.record;
        EXPECT_NEAR(row.halfWidth, o.halfWidth, 0.005f) << o.record;
        EXPECT_NEAR(row.centroid, o.centroid, 0.005f) << o.record;
        const bool shaped = o.anchor == TAnchor::Head || o.anchor == TAnchor::FeetGround ||
                            o.anchor == TAnchor::Column;
        EXPECT_EQ(row.hasShape, shaped) << o.record;
    }
    EXPECT_NEAR(cfg.targetRecord(5).faceOnset, 0.41f, 1e-3f) << "e5 f8233";
    EXPECT_NEAR(cfg.targetRecord(5).boneAt, 0.90f, 1e-3f) << "the head row follows the head bone (the face)";
    // The vision records the survey checked and found NOT head-anchored.
    EXPECT_NE(cfg.targetRecord(14).anchor, TAnchor::Head) << "Bind Sight class (r35)";
    EXPECT_NE(cfg.targetRecord(7).anchor, TAnchor::Head) << "Insight (r132)";
    // B1149 (#469): the shaped rows' populations are retired (each record
    // draws its slots' file counts); the measured coverages they came from are
    // bands: record 5's head glow 0.559 of record 0's 3.945.
    namespace pop = spell_fx_population_oracle;
    EXPECT_NEAR(pop::kRetiredTargetReferenceCoverage, 3.945f, 1e-3f);
    for (const auto& o : pop::kRows)
        if (o.row == 5) EXPECT_NEAR(o.populationShare, 0.559f / 3.945f, 0.002f);
}

// The bridge's cast-animation arm draws the whole landing through
// createLanding(). RED on 4a5246c1, whose arm was
// createSpellComplete(d.casterId, ...) + createImpact(d.targetId, ...): half of
// every other-target landing drawn on the caster (B1131 FOLLOW-UP 1).
// B1138: the arm's FX half moved to spell_cast_fx.cpp (shared with the
// model_viewer FX harness); both files are held to the rule.
TEST(TargetLandingBridge, TheCastAnimationArmDrawsTheLandingThroughCreateLanding) {
    auto slurp = [](const char* rel) {
        std::ifstream f(std::string(WILLEQ_REPO_ROOT) + rel);
        std::stringstream ss;
        ss << f.rdbuf();
        return ss.str();
    };
    const std::string bridge = slurp("/src/client/bridge/irrlicht_bridge.cpp");
    const std::string fx = slurp("/src/client/bridge/spell_cast_fx.cpp");
    ASSERT_FALSE(bridge.empty());
    ASSERT_FALSE(fx.empty());
    EXPECT_NE(bridge.find("ApplySpellCastCompleteFx("), std::string::npos);
    // B1144: with the caster's slot-gate level.
    EXPECT_NE(fx.find("createLanding(d.casterId, d.targetId, d.spellId, d.fxGateLevel)"), std::string::npos);
    for (const std::string* src : {&bridge, &fx})
        EXPECT_EQ(src->find("createSpellComplete(d.casterId"), std::string::npos)
            << "the caster-side landing half is back on the caster of an other-target cast";
}

TEST_F(TargetRecordRows, AnUnsurveyedRecordIsTheStyleVolumeOnTheTarget) {
    const auto& cfg = SpellEffectsConfig::instance();
    for (int r : {-1, 200, 255}) {
        EXPECT_EQ(cfg.targetRecord(r).anchor, TAnchor::Body) << r;
        EXPECT_FALSE(cfg.targetRecord(r).hasShape) << r;
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
constexpr uint16_t kTarget = 301;
constexpr float kH = 6.5f, kA = 3.6f;
// Entity anchors (feet at y - kA). The target stands 1.4 caster heights to the
// caster's right, as on the B1130 rig.
const glm::vec3 kCasterPos{10.0f, 5.0f, 20.0f};
const glm::vec3 kTargetPos{10.0f + 1.4f * kH, 5.0f, 20.0f};
const glm::vec3 kTargetHead{kTargetPos.x, kTargetPos.y - kA + 0.90f * kH, kTargetPos.z};

class TargetAnchors : public ::testing::Test {
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
        pm_->setEntityPositionCallback([](uint16_t id, glm::vec3& out) -> bool {
            if (id == kCaster) { out = kCasterPos; return true; }
            if (id == kTarget) { out = kTargetPos; return true; }
            if (id == EQ::SpellVisualFX::headEntityId(kTarget)) { out = kTargetHead; return true; }
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
    void Note(int record, uint32_t spell) {
        EQ::SpellVisualVocabulary v;
        v.known = true;
        v.school = EQ::SpellSchool::Evocation;
        v.spellAffectIndex = record;
        v.spellAnim = static_cast<int16_t>(spell_anim_rows::AnimOfRow(record));  // B1137: the row key
        fx_->noteSpellVocabulary(spell, v);
    }
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
    }
    void PumpFor(float s) {
        const int n = static_cast<int>(std::lround(s * 60.0f));
        for (int i = 0; i < n; ++i) Pump();
    }
    const std::vector<EQT::Graphics::Environment::UnifiedParticle>& Live() const {
        return out_.particleOutput.renderBuffer;
    }

    irr::IrrlichtDevice* device_ = nullptr;
    std::unique_ptr<EQT::Graphics::Environment::ParticleManager> pm_;
    std::unique_ptr<EQ::SpellVisualFX> fx_;
    std::unique_ptr<EQT::Graphics::SimulationWorker> worker_;
    EQT::Graphics::SimulationOutput out_;
    std::unordered_set<uint16_t> requested_;
    int pool_ = 0;
};

float Pct(std::vector<float> v, float q) {
    std::sort(v.begin(), v.end());
    return v[static_cast<size_t>(q * static_cast<float>(v.size() - 1))];
}

// ANCHOR: the point each record's landing follows. RED on 4a5246c1: no
// createLanding(); its bridge put spellComplete on the caster of every cast.
TEST_F(TargetAnchors, EachRecordLandsOnItsOwnAnchor) {
    for (const auto& o : kRows) {
        SetUp();
        if (::testing::Test::IsSkipped()) return;
        Note(o.record, o.spell);
        fx_->createLanding(kCaster, kTarget, o.spell);
        PumpFor(1.0f);
        const auto& e = out_.particleOutput.positionRequestEntities;
        const bool onCaster = e.count(kCaster) > 0;
        const bool onTarget = e.count(kTarget) > 0;
        const bool onHead = e.count(EQ::SpellVisualFX::headEntityId(kTarget)) > 0;
        switch (o.anchor) {
            case TAnchor::Head:
                EXPECT_TRUE(onHead) << o.record << ": not on the target's head bone";
                EXPECT_FALSE(onCaster || onTarget) << o.record;
                break;
            case TAnchor::Caster:
                EXPECT_TRUE(onCaster) << o.record << ": record 27 lands round the caster";
                EXPECT_FALSE(onTarget || onHead) << o.record;
                break;
            default:
                EXPECT_TRUE(onTarget) << o.record;
                EXPECT_FALSE(onCaster || onHead) << o.record;
        }
        TearDown();
    }
}

// B1131 FOLLOW-UP 1: an other-target cast draws NO landing on its caster.
// No emitter follows the caster at any frame, and the landing is centred on
// the target (the style's spark shell is wide -- the oracle's 93 cloud is ~3.4
// caster heights across -- so single sprites may reach past the midpoint;
// what matters is what they follow). RED on 4a5246c1: spellComplete followed
// the caster, centring half the landing 1.4 caster heights away.
TEST_F(TargetAnchors, AnOtherTargetCastDrawsNoLandingOnTheCaster) {
    for (const auto& o : kRows) {
        if (o.anchor == TAnchor::Caster) continue;
        SetUp();
        if (::testing::Test::IsSkipped()) return;
        Note(o.record, o.spell);
        fx_->createLanding(kCaster, kTarget, o.spell);
        size_t total = 0, casterFrames = 0;
        double xSum = 0.0;
        for (int f = 0; f < 60 * 5; ++f) {
            Pump();
            if (out_.particleOutput.positionRequestEntities.count(kCaster)) ++casterFrames;
            for (const auto& p : Live()) {
                ++total;
                xSum += p.position.x;
            }
        }
        ASSERT_GT(total, 0u) << o.record;
        EXPECT_EQ(casterFrames, 0u) << o.record << ": a landing emitter followed the caster";
        const float cx = static_cast<float>(xSum / static_cast<double>(total));
        EXPECT_NEAR((cx - kTargetPos.x) / kH, 0.0f, 0.1f) << o.record << ": landing not centred on the target";
        TearDown();
    }
}

// A self cast (and a cast with no target) still lands on the caster.
TEST_F(TargetAnchors, ASelfCastLandsOnTheCaster) {
    for (uint16_t target : {kCaster, uint16_t(0)}) {
        SetUp();
        if (::testing::Test::IsSkipped()) return;
        Note(0, 93);
        EXPECT_EQ(fx_->createLanding(kCaster, target, 93), kCaster);
        PumpFor(1.0f);
        EXPECT_TRUE(out_.particleOutput.positionRequestEntities.count(kCaster) > 0);
        EXPECT_FALSE(Live().empty());
        TearDown();
    }
}

// SHAPE: the live sprites sit where the oracle drew them, in target heights
// above the target's feet.
TEST_F(TargetAnchors, EachShapedRecordIsShapedLikeTheOraclesRun) {
    for (const auto& o : kRows) {
        if (o.anchor != TAnchor::Head && o.anchor != TAnchor::FeetGround) continue;
        SetUp();
        if (::testing::Test::IsSkipped()) return;
        Note(o.record, o.spell);
        fx_->createLanding(kCaster, kTarget, o.spell);
        PumpFor(1.2f);
        std::vector<EQT::Graphics::Environment::UnifiedParticle> pooled;
        for (int k = 0; k < 5; ++k) {
            if (k) PumpFor(0.2f);
            pooled.insert(pooled.end(), Live().begin(), Live().end());
        }
        ASSERT_GT(pooled.size(), 100u) << o.record;
        std::vector<float> ys, dx;
        float ySum = 0.0f;
        for (const auto& p : pooled) {
            const float y = (p.position.y - (kTargetPos.y - kA)) / kH;
            ys.push_back(y);
            ySum += y;
            dx.push_back(std::fabs(p.position.x - kTargetPos.x) / kH);
        }
        const float centroid = ySum / static_cast<float>(pooled.size());
        EXPECT_NEAR(centroid, o.centroid, 0.15f) << o.record << ": centroid";
        EXPECT_GE(Pct(ys, 0.05f), o.lo - 0.15f) << o.record << ": bottom";
        EXPECT_LE(Pct(ys, 0.95f), o.hi + 0.15f) << o.record << ": top";
        EXPECT_LE(Pct(dx, 0.95f), o.halfWidth + 0.15f) << o.record << ": width";
        if (o.anchor == TAnchor::Head)  // nothing below the waist (bands legs 0.01)
            EXPECT_GE(Pct(ys, 0.05f), 0.5f) << o.record;
        TearDown();
    }
}

// Record 5 starts AT THE FACE: the first sprites are born at the head bone.
TEST_F(TargetAnchors, TheVisionLandingStartsAtTheTargetsFace) {
    Note(5, 80);
    fx_->createLanding(kCaster, kTarget, 80);
    size_t first = 0, atFace = 0;
    for (int f = 0; f < 60 && first == 0; ++f) {
        Pump();
        for (const auto& p : Live()) {
            ++first;
            if (glm::length(p.position - kTargetHead) < 0.2f * kH) ++atFace;
        }
    }
    ASSERT_GT(first, 0u);
    EXPECT_GE(static_cast<float>(atFace) / static_cast<float>(first), 0.3f)
        << "the oracle's record-5 landing lights the target's face first (e5 f8233)";
}

// Record 16/28: rings on the ground round the target's feet (sprites at the
// ring radius at the row's bottom).
TEST_F(TargetAnchors, TheSnareLandingIsRingsAtTheTargetsFeet) {
    for (int rec : {16, 28}) {
        SetUp();
        if (::testing::Test::IsSkipped()) return;
        const auto& row = SpellEffectsConfig::instance().targetRecord(rec);
        Note(rec, rec == 16 ? 242u : 73u);
        fx_->createLanding(kCaster, kTarget, rec == 16 ? 242u : 73u);
        PumpFor(1.0f);
        size_t low = 0, ring = 0;
        for (const auto& p : Live()) {
            const float y = (p.position.y - (kTargetPos.y - kA)) / kH;
            if (std::fabs(y - row.lo) > 0.05f) continue;
            ++low;
            const float r = std::hypot(p.position.x - kTargetPos.x, p.position.z - kTargetPos.z) / kH;
            if (std::fabs(r - row.halfWidth) < 0.05f) ++ring;
        }
        ASSERT_GT(low, 20u) << rec;
        EXPECT_GE(static_cast<float>(ring) / static_cast<float>(low), 0.9f) << rec;
        TearDown();
    }
}

// Record 3: a sheet falling over the target from above its head.
TEST_F(TargetAnchors, TheColumnFallsFromAboveTheTarget) {
    Note(3, 377);
    fx_->createLanding(kCaster, kTarget, 377);
    PumpFor(1.0f);
    size_t young = 0, high = 0;
    for (const auto& p : Live()) {
        if (p.age > 0.1f || p.velocity.y > -0.1f) continue;
        ++young;
        if ((p.position.y - (kTargetPos.y - kA)) / kH > 1.6f) ++high;
    }
    ASSERT_GT(young, 10u);
    EXPECT_EQ(high, young) << "falling sprites are born above the target's head";
}

}  // namespace

#endif  // EQT_HAS_GRAPHICS && EQT_HAS_GLES2
