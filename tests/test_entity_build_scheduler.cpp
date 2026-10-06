/**
 * B973 (GitLab #291) -- post-load entity builds make progress in every
 * governor state, at a bounded per-frame cost.
 *
 * Before B973 a post-load entity was built only on a Green frame, one unit per
 * Green frame. A 200-NPC crowd kept the Orange Pi's frame Red, no Green frame
 * came, and 0 of 200 were built in 4 minutes (refactor/B973/README.md).
 *
 * Part 1 drives EntityBuildScheduler with fake units of known cost (no
 * device). Part 2 drives the real staged build (EntityRenderer::
 * processOneEntityBuildStep) under a Red scheduler on an EDT_NULL scene graph,
 * and pins B966 (#284): the create unit promotes the node's materials in the
 * same unit, so the node is never drawn with its raw materials.
 */
#include <gtest/gtest.h>

#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <string>
#include <vector>

#include <irrlicht.h>

#include "client/graphics/constrained_renderer_config.h"
#include "client/graphics/constrained_texture_cache.h"
#include "client/graphics/entity_build_scheduler.h"
#include "client/graphics/entity_renderer.h"
#include "client/willeq_database.h"

using EQT::Graphics::BudgetState;
using EQT::Graphics::EntityBuildBacklog;
using EQT::Graphics::EntityBuildScheduler;
using EQT::Graphics::EntityBuildUnitKind;

namespace {

constexpr float kTarget = 1000.0f / 30.0f;   // the orangepi governor's 30 fps target

// One fake entity: its remaining units, in order.
struct FakeEntity {
    std::vector<std::pair<EntityBuildUnitKind, float>> units;
    size_t next = 0;
    bool built() const { return next >= units.size(); }
};

// Run one frame the way FrameComponent::runEntityBuildSlice() does: ask, run
// (the unit "costs" its fixed ms), record, until the scheduler declines or no
// work is left. Returns the frame's units.
int RunFrame(EntityBuildScheduler& s, BudgetState state, std::vector<FakeEntity>& work) {
    s.beginFrame(state, kTarget);
    int units = 0;
    for (auto& e : work) {
        while (!e.built()) {
            const auto [kind, ms] = e.units[e.next];
            if (!s.mayStartUnit(kind)) return units;
            s.recordUnit(kind, ms);
            e.next++;
            units++;
        }
    }
    return units;
}

std::vector<FakeEntity> Crowd(int n, float createMs, float finalizeMs) {
    std::vector<FakeEntity> v(n);
    for (auto& e : v) {
        e.units = {{EntityBuildUnitKind::Create, createMs}, {EntityBuildUnitKind::Finalize, finalizeMs}};
    }
    return v;
}

bool AllBuilt(const std::vector<FakeEntity>& v) {
    return std::all_of(v.begin(), v.end(), [](const FakeEntity& e) { return e.built(); });
}

}  // namespace

// The defect, in one line: the old rule never runs a unit in Red.
TEST(EntityBuildScheduler, LegacyRuleStarvesInRed) {
    EntityBuildScheduler s;
    s.setLegacy(true);
    auto crowd = Crowd(200, 12.0f, 0.2f);
    int units = 0;
    for (int f = 0; f < 10000; ++f) units += RunFrame(s, BudgetState::Red, crowd);
    EXPECT_FALSE(AllBuilt(crowd));
    EXPECT_EQ(units, 0);
}

TEST(EntityBuildScheduler, LegacyRuleRunsExactlyOneUnitPerGreenFrame) {
    EntityBuildScheduler s;
    s.setLegacy(true);
    auto crowd = Crowd(10, 0.01f, 0.01f);
    EXPECT_EQ(RunFrame(s, BudgetState::Green, crowd), 1);
    EXPECT_EQ(RunFrame(s, BudgetState::Yellow, crowd), 0);
}

// Guaranteed progress: at least one unit in every frame, every state, while
// work is pending -- even a unit far bigger than the slice.
TEST(EntityBuildScheduler, AtLeastOneUnitPerFrameInEveryState) {
    for (BudgetState state : {BudgetState::Green, BudgetState::Yellow, BudgetState::Red}) {
        EntityBuildScheduler s;
        auto crowd = Crowd(50, 40.0f, 0.2f);   // a create unit > a whole frame
        int frames = 0;
        while (!AllBuilt(crowd)) {
            ASSERT_GE(RunFrame(s, state, crowd), 1) << "frame " << frames << " state "
                                                     << static_cast<int>(state);
            ASSERT_LT(++frames, 10000);
        }
    }
}

// The slice: past the first unit, a frame's build work stays within the
// state's slice when the units cost what their kind measured.
TEST(EntityBuildScheduler, WorkAfterTheFirstUnitStaysInsideTheSlice) {
    struct Case { BudgetState state; float sliceMs; };
    for (const Case c : {Case{BudgetState::Green, kTarget * 0.20f},
                         Case{BudgetState::Yellow, kTarget * 0.12f},
                         Case{BudgetState::Red, kTarget * 0.06f}}) {
        EntityBuildScheduler s;
        auto crowd = Crowd(400, 0.7f, 0.3f);
        int frames = 0, multiUnitFrames = 0;
        while (!AllBuilt(crowd)) {
            const int units = RunFrame(s, c.state, crowd);
            ASSERT_GE(units, 1);
            EXPECT_NEAR(s.sliceMs(), c.sliceMs, 1e-3f);
            // Bound: the slice, or the one unit that always runs.
            EXPECT_LE(s.spentMs(), std::max(c.sliceMs, 0.7f) + 1e-3f)
                << "state " << static_cast<int>(c.state) << " frame " << frames;
            if (units > 1) multiUnitFrames++;
            ASSERT_LT(++frames, 10000);
        }
        EXPECT_GT(multiUnitFrames, 0) << "cheap units should share a frame";
    }
    // Red is 2 ms at 30 fps: the dispatcher's bound.
    EntityBuildScheduler red;
    red.beginFrame(BudgetState::Red, kTarget);
    EXPECT_NEAR(red.sliceMs(), 2.0f, 0.01f);
}

// The predictor is per kind: an expensive create does not start a second time
// in a frame it does not fit, while cheap finalize units still fill the slice.
TEST(EntityBuildScheduler, AnExpensiveKindDoesNotRepeatInsideARedFrame) {
    EntityBuildScheduler s;
    s.beginFrame(BudgetState::Red, kTarget);
    ASSERT_TRUE(s.mayStartUnit(EntityBuildUnitKind::Create));
    s.recordUnit(EntityBuildUnitKind::Create, 1.5f);
    // 1.5 + predicted 1.5 > 2.0
    EXPECT_FALSE(s.mayStartUnit(EntityBuildUnitKind::Create));
    // a finalize never measured is predicted 0: it fits the remaining 0.5 ms
    EXPECT_TRUE(s.mayStartUnit(EntityBuildUnitKind::Finalize));
    s.recordUnit(EntityBuildUnitKind::Finalize, 0.1f);
    EXPECT_TRUE(s.mayStartUnit(EntityBuildUnitKind::Finalize));
    EXPECT_NEAR(s.predictedMs(EntityBuildUnitKind::Create), 1.5f, 1e-4f);
}

// The live case in numbers: 200 entities, Pi-sized units, every frame Red.
// The old rule builds none; the scheduler builds all of them in a bounded
// number of frames (two units per entity, at least one unit per frame).
TEST(EntityBuildScheduler, A200EntityCrowdInPermanentRedIsBuiltInBoundedFrames) {
    EntityBuildScheduler s;
    auto crowd = Crowd(200, 12.0f, 0.2f);
    int frames = 0, units = 0;
    while (!AllBuilt(crowd)) {
        const int u = RunFrame(s, BudgetState::Red, crowd);
        ASSERT_GE(u, 1);
        units += u;
        ASSERT_LE(++frames, 400) << "a 200-entity crowd must be built within 2 units per entity";
    }
    EXPECT_EQ(units, 400);
}

// B979: a capture is a unit of the B973 scheduler (entity_impostors.cpp
// gates its loop with mayStartCaptureUnit / recordUnit, kind Impostor). The
// defect: a slow FIRST capture (30-62 ms on the Pi, B964) was unbounded --
// the per-pass budget checked the time already spent BEFORE the unit, so a
// slow capture landed whole in a frame, and a second one could follow it.
// With the kind accounted to the scheduler, a second capture starts only
// when the pass's work so far plus the kind's PREDICTED cost fits the
// slice. So a 46 ms capture can never be joined by another capture in the
// same frame, and a capture never starts beside build work it does not fit.
TEST(EntityBuildScheduler, ASlowImpostorCaptureIsDeferredSoTheFrameStaysInsideItsBudget) {
    // Frame 1: the first capture starts (guaranteed progress, every state)
    // and costs 46 ms -- the Pi's first-draw cost. A second capture must not
    // follow it in the same frame.
    EntityBuildScheduler s;
    s.beginFrame(BudgetState::Green, kTarget);
    ASSERT_TRUE(s.mayStartCaptureUnit(0.0f, 0)) << "the first capture of a frame always runs";
    s.recordUnit(EntityBuildUnitKind::Impostor, 46.0f);
    // 46 spent + 46 predicted > the 6.7 ms Green slice, far over one frame
    // budget. Deferred to the next frame -- the bar B964 left unmet.
    EXPECT_FALSE(s.mayStartCaptureUnit(46.0f, 1))
        << "a second capture must not join a 46 ms one in the same frame";

    // Frame 2: the build slice spent 5 ms of the 6.7 ms slice. The first
    // capture still starts (guaranteed progress), costs 46 ms, and the
    // second is deferred: frame work (5 + 46) + prediction does not fit the
    // slice -- the capture never lands beside build work it does not fit in.
    s.beginFrame(BudgetState::Green, kTarget);
    s.recordUnit(EntityBuildUnitKind::Create, 5.0f);
    ASSERT_TRUE(s.mayStartCaptureUnit(0.0f, 0)) << "the first capture of a frame always runs";
    s.recordUnit(EntityBuildUnitKind::Impostor, 46.0f);
    EXPECT_FALSE(s.mayStartCaptureUnit(46.0f, 1))
        << "a 46 ms capture does not join a 5 ms build slice in a 6.7 ms slice";

    // Frames 3+: in Red (a 2 ms slice at 30 fps) the one guaranteed capture
    // IS the frame's unit work; nothing else lands beside it.
    for (int f = 0; f < 4; ++f) {
        s.beginFrame(BudgetState::Red, kTarget);
        ASSERT_TRUE(s.mayStartCaptureUnit(0.0f, 0)) << "guaranteed progress, frame " << f;
        s.recordUnit(EntityBuildUnitKind::Impostor, 46.0f);   // the Pi's first-draw cost
        EXPECT_FALSE(s.mayStartCaptureUnit(46.0f, 1))
            << "the second capture of a frame is deferred, frame " << f;
    }
    EXPECT_GE(s.predictedMs(EntityBuildUnitKind::Impostor), 30.0f)
        << "the kind's prediction must carry the capture's real cost";

    // Legacy A/B mode: the gate is off, the pass behaves as pre-B979.
    EntityBuildScheduler legacy;
    legacy.setLegacy(true);
    legacy.beginFrame(BudgetState::Red, kTarget);
    legacy.recordUnit(EntityBuildUnitKind::Impostor, 46.0f);
    EXPECT_TRUE(legacy.mayStartCaptureUnit(46.0f, 1)) << "legacy mode never defers";
}

// The window line carries the new kind's count and predictor (appended at the
// end, so the B973-era fields do not move).
TEST(EntityBuildScheduler, TheWindowLineCarriesTheImpostorKind) {
    EntityBuildScheduler s;
    EntityBuildBacklog b;
    b.built = 1;
    b.pendingUnits = 1;
    double t = 100.0;
    for (int i = 0; i < 6; ++i) {
        s.beginFrame(BudgetState::Green, kTarget);
        s.recordUnit(EntityBuildUnitKind::Impostor, 5.0f);
        s.recordUnit(EntityBuildUnitKind::Create, 1.0f);
        b.built++;
        if (s.endFrame(30.0f, b, t + 0.25 * i)) break;
    }
    const std::string& line = s.windowLine();
    EXPECT_NE(line.find("impostor="), std::string::npos) << line;
    EXPECT_NE(line.find("pred_ms upload="), std::string::npos) << line;
    EXPECT_NE(line.find("impostor=5.00 kind_ms"), std::string::npos) << line;
}

// The accounting line: one per window, only when there is something to say.
TEST(EntityBuildScheduler, TheWindowLineCountsUnitsStatesAndTheBacklog) {
    EntityBuildScheduler s;
    EntityBuildBacklog b;
    b.built = 10;
    b.unbuiltVisible = 5;
    b.pendingUnits = 10;
    double t = 100.0;
    s.beginFrame(BudgetState::Red, kTarget);
    EXPECT_FALSE(s.endFrame(30.0f, b, t));   // opens the window (a Red frame, no unit)
    for (int i = 0; i < 5; ++i) {
        s.beginFrame(BudgetState::Red, kTarget);
        s.recordUnit(EntityBuildUnitKind::Create, 3.0f);
        t += 0.25;
        b.built++;
        const bool closed = s.endFrame(40.0f, b, t);
        if (i < 3) EXPECT_FALSE(closed);
        else if (closed) break;
    }
    const std::string& line = s.windowLine();
    EXPECT_NE(line.find("[entity-build] mode=sliced"), std::string::npos) << line;
    EXPECT_NE(line.find("frames=5 gov=G0/Y0/R5"), std::string::npos) << line;
    EXPECT_NE(line.find("units=4 (upload=0 create=4"), std::string::npos) << line;
    EXPECT_NE(line.find("over_slice=4"), std::string::npos) << line;
    EXPECT_NE(line.find("built_delta=+4"), std::string::npos) << line;

    // Nothing pending, nothing done: no line.
    EntityBuildScheduler quiet;
    EntityBuildBacklog none;
    EXPECT_FALSE(quiet.endFrame(16.0f, none, 0.0));
    EXPECT_FALSE(quiet.endFrame(16.0f, none, 2.0));
}

// ---------------------------------------------------------------------------
// Part 2 -- the real staged build under a Red scheduler (EDT_NULL device; skips
// without the shipped db or the client tree, like test_corpse_death_pose).
// ---------------------------------------------------------------------------

class StagedBuildUnderRed : public ::testing::Test {
protected:
    static bool fileExists(const std::string& p) { std::ifstream f(p); return f.good(); }

    void SetUp() override {
        const std::string dbPath = WILLEQ_DB_REAL_PATH;
        if (!fileExists(dbPath)) GTEST_SKIP() << "no shipped db at " << dbPath;
        ASSERT_TRUE(db_.open(dbPath)) << dbPath;
        EQT::WilleqDatabase::setGlobalInstance(&db_);
        const char* envPath = std::getenv("EQ_CLIENT_PATH");
        std::string eq = envPath ? envPath : EQ_CLIENT_DATA_PATH;
        if (!eq.empty() && eq.back() != '/') eq += '/';
        if (!fileExists(eq + "global_chr.s3d")) GTEST_SKIP() << "no client data at " << eq;

        irr::SIrrlichtCreationParameters params;
        params.DriverType = irr::video::EDT_NULL;
        params.WindowSize = irr::core::dimension2d<irr::u32>(64, 64);
        device_ = irr::createDeviceEx(params);
        if (!device_) GTEST_SKIP() << "no EDT_NULL device";
        entities_ = std::make_unique<EQT::Graphics::EntityRenderer>(
            device_->getSceneManager(), device_->getVideoDriver(), device_->getFileSystem());
        entities_->setClientPath(eq);
        auto config = EQT::Graphics::ConstrainedRendererConfig::fromPreset(
            EQT::Graphics::ConstrainedRenderingPreset::TNT);
        cache_ = std::make_unique<EQT::Graphics::ConstrainedTextureCache>(config);
        cache_->setDriver(device_->getVideoDriver());
        entities_->setConstrainedTextureCache(cache_.get());
        if (!entities_->loadGlobalCharacters()) GTEST_SKIP() << "global_chr.s3d would not load";
    }
    void TearDown() override {
        entities_.reset();
        cache_.reset();
        if (device_) { device_->drop(); device_ = nullptr; }
        EQT::WilleqDatabase::setGlobalInstance(nullptr);
    }

    // A Human male whose model is ready (what the prep worker does in
    // production), made the build's first choice through the player priority
    // -- the one selection lever reachable without a BSP region.
    const EQT::Graphics::EntityVisual* registerReady(uint16_t spawn) {
        if (!entities_->registerEntity(spawn, 1, "Crowd001", 10.f, 20.f, 3.f, 0.f, false, 0,
                                       EQT::Graphics::EntityAppearance(), true, false, 6.0f, 10))
            return nullptr;
        entities_->getRaceModelLoader()->preloadModelData(1, 0);
        entities_->setPlayerSpawnId(spawn);
        return find(spawn);
    }
    const EQT::Graphics::EntityVisual* find(uint16_t spawn) {
        auto it = entities_->getEntities().find(spawn);
        return it == entities_->getEntities().end() ? nullptr : &it->second;
    }

    EQT::WilleqDatabase db_;
    irr::IrrlichtDevice* device_ = nullptr;
    std::unique_ptr<EQT::Graphics::ConstrainedTextureCache> cache_;
    std::unique_ptr<EQT::Graphics::EntityRenderer> entities_;
};

TEST_F(StagedBuildUnderRed, TheLegacyRuleBuildsNothingInRed) {
    ASSERT_NE(registerReady(501), nullptr);
    EntityBuildScheduler legacy;
    legacy.setLegacy(true);
    for (int f = 0; f < 50; ++f) {
        legacy.beginFrame(BudgetState::Red, kTarget);
        EXPECT_FALSE(entities_->processOneEntityBuildStep(SIZE_MAX, 0, &legacy));
    }
    EXPECT_EQ(find(501)->buildPhase, EQT::Graphics::EntityBuildPhase::Placeholder);
}

TEST_F(StagedBuildUnderRed, ARedFrameStillAdvancesTheBuildToBuilt) {
    ASSERT_NE(registerReady(502), nullptr);
    EntityBuildScheduler& s = entities_->buildScheduler();
    ASSERT_FALSE(s.legacy());
    int frames = 0;
    while (find(502)->buildPhase != EQT::Graphics::EntityBuildPhase::Built) {
        s.beginFrame(BudgetState::Red, kTarget);
        ASSERT_TRUE(entities_->processOneEntityBuildStep(SIZE_MAX, 0, &s)) << "frame " << frames;
        ASSERT_EQ(s.unitsThisFrame(), 1);
        ASSERT_LT(++frames, 10);
        if (frames == 1) {
            // B966: the create unit ran NodeSetup too -- the node never sits in
            // the scene graph with its raw materials across a frame boundary.
            EXPECT_EQ(find(502)->buildPhase, EQT::Graphics::EntityBuildPhase::MeshFinalize);
            ASSERT_NE(find(502)->sceneNode, nullptr);
            for (irr::u32 i = 0; i < find(502)->sceneNode->getMaterialCount(); ++i)
                EXPECT_FALSE(find(502)->sceneNode->getMaterial(i).BackfaceCulling)
                    << "material " << i << " was not set up in the create unit";
        }
    }
    EXPECT_LE(frames, 2) << "create (+setup), finalize";
    EXPECT_GT(s.predictedMs(EntityBuildUnitKind::Create), 0.0f);
}
