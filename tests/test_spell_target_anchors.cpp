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
// B1150 (#470): the rows' measured SHAPES (lo/hi/halfWidth/centroid/boneAt)
// are retired -- every record's landing is drawn by its spellsnew.edd +0xA8
// shape (dims +0xAC/+0xB0/+0xB4, offsets +0xB8..+0xC0, tilts +0xC4/+0xC8,
// depth +0xD0, +0x19C scale) where the client file is present. The retired
// values are the bands in tests/spell_fx_shape_oracle.h (the generator:
// refactor/B1150/scripts/); the anchor classes and faceOnset stay as
// evidence and are honored by the no-file fallback.
//
// RED on fable 4a5246c1 (review/B1132/tests/): its config has no
// `targetRecords` and its code has no createLanding(); its bridge drew
// spellComplete on the caster of every cast.

#include <gtest/gtest.h>

#include "client/graphics/environment/spell_effects_config.h"
#include "spell_pool.h"
#include "spell_anim_rows.h"
#include "spell_fx_population_oracle.h"
#include "spell_fx_shape_oracle.h"
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

// All 41 records carry a row, in the survey's class counts. B1150 (#470):
// the row's measured extent is retired -- every record is drawn by its
// spellsnew.edd +0xA8 shape; the extent is the band for the record
// (tests/spell_fx_shape_oracle.h), and the anchor class stays as evidence.
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
        EXPECT_FALSE(row.hasShape) << r << " B1150: the record's +0xA8 word is the shape";
        // B1150: the row's retired measured extent is the band for r --
        // every record except 51 (no Target stage, no extent, B1134).
        const auto band = std::find_if(std::begin(spell_fx_shape_oracle::kTarget),
                                       std::end(spell_fx_shape_oracle::kTarget),
                                       [r](const spell_fx_shape_oracle::TargetBand& b) { return b.record == r; });
        if (r == 51) {
            EXPECT_EQ(band, std::end(spell_fx_shape_oracle::kTarget)) << "record 51 has no extent";
        } else {
            ASSERT_NE(band, std::end(spell_fx_shape_oracle::kTarget)) << r;
            EXPECT_EQ(band->anchor, static_cast<int>(row.anchor)) << r;
        }
        if (row.unmeasured) { ++unmeasured; continue; }
        ++classes[row.anchor];
        if (r == 51) continue;  // no Target stage: no extent (B1134)
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
    // B1150: the observer's measured extent is the band for 40.
    const auto band40 = std::find_if(std::begin(spell_fx_shape_oracle::kTarget),
                                     std::end(spell_fx_shape_oracle::kTarget),
                                     [](const spell_fx_shape_oracle::TargetBand& b) { return b.record == 40; });
    ASSERT_NE(band40, std::end(spell_fx_shape_oracle::kTarget));
    EXPECT_NEAR(band40->lo, 0.07f, 1e-3f);
    EXPECT_NEAR(band40->hi, 1.67f, 1e-3f);
    EXPECT_NEAR(band40->halfWidth, 0.615f, 1e-3f);
    EXPECT_NEAR(band40->centroid, 0.87f, 1e-3f);
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

// B1150 (#470): the oracle's per-row extents are the bands; the rows keep
// their anchors (and record 5's faceOnset).
TEST_F(TargetRecordRows, EachRecordCarriesTheOraclesAnchorAndExtent) {
    const auto& cfg = SpellEffectsConfig::instance();
    for (const auto& o : kRows) {
        const auto& row = cfg.targetRecord(o.record);
        EXPECT_EQ(row.anchor, o.anchor) << o.record;
        EXPECT_FALSE(row.hasShape) << o.record << " B1150: the record's +0xA8 word is the shape";
        // The retired measured extent is the band for the record.
        const auto band = std::find_if(std::begin(spell_fx_shape_oracle::kTarget),
                                       std::end(spell_fx_shape_oracle::kTarget),
                                       [o](const spell_fx_shape_oracle::TargetBand& b) { return b.record == o.record; });
        ASSERT_NE(band, std::end(spell_fx_shape_oracle::kTarget)) << o.record;
        EXPECT_EQ(band->anchor, static_cast<int>(o.anchor)) << o.record;
        EXPECT_NEAR(band->lo, o.lo, 0.005f) << o.record;
        EXPECT_NEAR(band->hi, o.hi, 0.005f) << o.record;
        EXPECT_NEAR(band->halfWidth, o.halfWidth, 0.005f) << o.record;
        EXPECT_NEAR(band->centroid, o.centroid, 0.005f) << o.record;
    }
    EXPECT_NEAR(cfg.targetRecord(5).faceOnset, 0.41f, 1e-3f) << "e5 f8233";
    // B1150: the head row's boneAt is the band's (the head row follows the
    // head bone: the face).
    const auto band5 = std::find_if(std::begin(spell_fx_shape_oracle::kTarget),
                                    std::end(spell_fx_shape_oracle::kTarget),
                                    [](const spell_fx_shape_oracle::TargetBand& b) { return b.record == 5; });
    ASSERT_NE(band5, std::end(spell_fx_shape_oracle::kTarget));
    EXPECT_NEAR(band5->boneAt, 0.90f, 1e-3f);
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

// B1150 (#470): the rows' measured extents are retired -- every record is
// drawn by its spellsnew.edd +0xA8 shape where the client file is present
// (the gate: the model_viewer sweep; the retired values are the bands in
// tests/spell_fx_shape_oracle.h). This seam runs WITHOUT the file, so each
// record's landing is the style's own volume (spellComplete's box +
// impact's shell) placed on the record's ANCHOR -- the anchor class is
// evidence, honored without a shape (TargetAnchorForSpell). What the seam
// pins now is the anchor: every record lands on its own point, none on the
// other entity.
const glm::vec3& AnchorPoint(TAnchor a) {
    static const glm::vec3 kBody{kTargetPos.x, kTargetPos.y, kTargetPos.z};
    switch (a) {
        case TAnchor::Head: return kTargetHead;
        case TAnchor::Caster: return kCasterPos;
        default: return kBody;
    }
}

TEST_F(TargetAnchors, EachRecordLandsAsTheStyleVolumeOnItsAnchor) {
    for (const auto& o : kRows) {
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
        const glm::vec3& pt = AnchorPoint(o.anchor);
        double dx = 0.0, dy = 0.0, dz = 0.0;
        for (const auto& p : pooled) {
            dx += p.position.x - pt.x;
            dy += p.position.y - pt.y;
            dz += p.position.z - pt.z;
        }
        const float n = static_cast<float>(pooled.size());
        // The other entity stands 1.4 caster heights away, so a landing on
        // it would sit well outside these tolerances.
        EXPECT_NEAR(dx / n / kH, 0.0f, 0.4f) << o.record << ": landing not on its anchor's x";
        EXPECT_NEAR(dz / n / kH, 0.0f, 0.4f) << o.record << ": landing not on its anchor's z";
        EXPECT_LE(std::fabs(dy / n) / kH, 1.2f) << o.record << ": landing not on its anchor's height";
        TearDown();
    }
}

// B1150: record 5's landing is centred on the target's head bone (the
// oracle's face-start is the record's EDD shape's, verified by the gate).
TEST_F(TargetAnchors, TheVisionLandingIsOnTheTargetsHeadBone) {
    Note(5, 80);
    fx_->createLanding(kCaster, kTarget, 80);
    size_t young = 0, atHead = 0;
    for (int f = 0; f < 60 * 2; ++f) {
        Pump();
        for (const auto& p : Live()) {
            if (p.age > 0.3f) continue;
            ++young;
            if (glm::length(p.position - kTargetHead) < 1.2f * kH) ++atHead;
        }
    }
    ASSERT_GT(young, 0u);
    EXPECT_GE(static_cast<float>(atHead) / static_cast<float>(young), 0.6f)
        << "record 5's landing sits on the target's head bone";
}

// B1150: records 16/28 (feet/ground) and 3 (the column above) land on the
// target's entity anchor (the rings and the falling sheet are the records'
// EDD shapes', verified by the gate).
TEST_F(TargetAnchors, TheFeetAndColumnRecordsLandOnTheTargetsAnchor) {
    for (int rec : {16, 28, 3}) {
        SetUp();
        if (::testing::Test::IsSkipped()) return;
        Note(rec, rec == 16 ? 242u : (rec == 28 ? 73u : 377u));
        fx_->createLanding(kCaster, kTarget, rec == 16 ? 242u : (rec == 28 ? 73u : 377u));
        PumpFor(1.2f);
        std::vector<EQT::Graphics::Environment::UnifiedParticle> pooled;
        for (int k = 0; k < 5; ++k) {
            if (k) PumpFor(0.2f);
            pooled.insert(pooled.end(), Live().begin(), Live().end());
        }
        ASSERT_GT(pooled.size(), 100u) << rec;
        double dx = 0.0, dy = 0.0, dz = 0.0;
        for (const auto& p : pooled) {
            dx += p.position.x - kTargetPos.x;
            dy += p.position.y - kTargetPos.y;
            dz += p.position.z - kTargetPos.z;
        }
        const float n = static_cast<float>(pooled.size());
        EXPECT_NEAR(dx / n / kH, 0.0f, 0.4f) << rec;
        EXPECT_NEAR(dz / n / kH, 0.0f, 0.4f) << rec;
        EXPECT_LE(std::fabs(dy / n) / kH, 1.2f) << rec;
        TearDown();
    }
}

}  // namespace

#endif  // EQT_HAS_GRAPHICS && EQT_HAS_GLES2
