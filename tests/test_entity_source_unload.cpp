// T504b Leg 2b: the unload half of the working set. Red-first: against the
// pre-Leg-2b loader the model of a despawned race was evicted at once and its
// source was never freed, so "still loaded within the window" and "bytes
// return to the pre-B value after the sweep" both fail there.
//
// Offline, like test_variant_loader_candidates: the shipped data/willeq.db,
// the real client tree (data/eq_client), EDT_NULL, no worker (so the loader's
// synchronous chain builds on this thread). Skips without the assets.
#include <gtest/gtest.h>

#include <cstdlib>
#include <fstream>
#include <memory>
#include <string>

#include "client/willeq_database.h"
#include "client/core/memory_ledger.h"
#include "client/graphics/constrained_renderer_config.h"
#include "client/graphics/constrained_texture_cache.h"
#include "client/graphics/eq/race_model_loader.h"
#include <irrlicht.h>

namespace {
bool fileExists(const std::string& p) { std::ifstream f(p); return f.good(); }
std::string clientPath() {
    const char* env = std::getenv("EQ_CLIENT_PATH");
    std::string p = env ? env : EQ_CLIENT_DATA_PATH;
    if (!p.empty() && p.back() != '/') p += '/';
    return p;
}
uint64_t entityModelBytes() { return eqt::memory::stats(eqt::memory::Category::EntityModels).liveBytes; }
}  // namespace

class EntitySourceUnloadTest : public ::testing::Test {
protected:
    EQT::WilleqDatabase db_;
    irr::IrrlichtDevice* device_ = nullptr;
    std::unique_ptr<EQT::Graphics::ConstrainedTextureCache> texCache_;
    std::unique_ptr<EQT::Graphics::RaceModelLoader> loader_;

    void SetUp() override {
        if (!eqt::memory::enabled()) GTEST_SKIP() << "memory ledger off";
        if (!fileExists(WILLEQ_DB_REAL_PATH)) GTEST_SKIP() << "no shipped db";
        const std::string eq = clientPath();
        if (!fileExists(eq + "global_chr.s3d") || !fileExists(eq + "global4_chr.s3d"))
            GTEST_SKIP() << "EQ client tree not at " << eq;
        ASSERT_TRUE(db_.open(WILLEQ_DB_REAL_PATH));
        EQT::WilleqDatabase::setGlobalInstance(&db_);
        irr::SIrrlichtCreationParameters params;
        params.DriverType = irr::video::EDT_NULL;
        params.WindowSize = irr::core::dimension2d<irr::u32>(64, 64);
        device_ = irr::createDeviceEx(params);
        if (!device_) GTEST_SKIP() << "no EDT_NULL device";
        loader_ = std::make_unique<EQT::Graphics::RaceModelLoader>(
            device_->getSceneManager(), device_->getVideoDriver(), device_->getFileSystem());
        auto config = EQT::Graphics::ConstrainedRendererConfig::fromPreset(
            EQT::Graphics::ConstrainedRenderingPreset::TNT);
        texCache_ = std::make_unique<EQT::Graphics::ConstrainedTextureCache>(config);
        texCache_->setDriver(device_->getVideoDriver());
        loader_->setConstrainedTextureCache(texCache_.get());
        loader_->getMeshBuilder()->setConstrainedTextureCache(texCache_.get());
        loader_->setClientPath(eq);
        loader_->loadGlobalModels();
    }
    void TearDown() override {
        loader_.reset();
        texCache_.reset();
        if (device_) device_->drop();
        EQT::WilleqDatabase::setGlobalInstance(nullptr);
    }
};

TEST_F(EntitySourceUnloadTest, DespawnedRaceStaysWithinTheWindowThenLeavesWithItsSource) {
    // Race A (human male, global_chr) is the resident population.
    ASSERT_TRUE(loader_->ensureModelDataLoaded(1, 0));
    loader_->addMeshRef(1, 0);
    loader_->pruneSourceCharacters("test-baseline");
    const uint64_t before = entityModelBytes();

    // Race B (128/1, global4_chr) spawns: its model and source arrive.
    ASSERT_TRUE(loader_->ensureModelDataLoaded(128, 1)) << "race 128 gender 1 must resolve from global4_chr.s3d";
    loader_->addMeshRef(128, 1);
    loader_->pruneSourceCharacters("test-B");
    const uint64_t withB = entityModelBytes();
    EXPECT_GT(withB, before);

    // Its last entity despawns: the model is EVICTABLE, still loaded.
    loader_->removeMeshRef(128, 1);
    EXPECT_EQ(loader_->evictableCountForTest(), 1u) << "eviction must be deferred, not immediate";
    EXPECT_TRUE(loader_->isModelReady(128, 1)) << "still loaded within the hysteresis window";
    loader_->sweepEvictableForTest(1.0);
    EXPECT_TRUE(loader_->isModelReady(128, 1)) << "1 s old: inside the window";

    // A respawn inside the window cancels the eviction: no parse, no rebuild.
    loader_->addMeshRef(128, 1);
    EXPECT_EQ(loader_->evictableCountForTest(), 0u);
    loader_->removeMeshRef(128, 1);

    // Past the window: the model and its retained source both leave.
    loader_->sweepEvictableForTest(3600.0);
    EXPECT_EQ(loader_->evictableCountForTest(), 0u);
    EXPECT_FALSE(loader_->isModelReady(128, 1)) << "evicted after the window";
    const uint64_t after = entityModelBytes();
    // Back to the pre-B bytes within 5 % (allocator rounding, texture bookkeeping).
    EXPECT_LE(after, before + before / 20) << "before=" << before << " withB=" << withB << " after=" << after;
}

TEST_F(EntitySourceUnloadTest, BudgetPressureEvictsAtOnceOldestFirst) {
    ASSERT_TRUE(loader_->ensureModelDataLoaded(1, 0));
    loader_->addMeshRef(1, 0);
    ASSERT_TRUE(loader_->ensureModelDataLoaded(128, 1));
    loader_->addMeshRef(128, 1);
    loader_->removeMeshRef(128, 1);
    ASSERT_EQ(loader_->evictableCountForTest(), 1u);
    loader_->setEntityModelBudgetBytes(1);   // everything is over budget
    loader_->sweepEvictableForTest(0.0);     // age 0: only the budget can evict
    EXPECT_EQ(loader_->evictableCountForTest(), 0u);
    EXPECT_FALSE(loader_->isModelReady(128, 1));
    EXPECT_TRUE(loader_->isModelReady(1, 0)) << "a race with live entities is never evicted";
}
