/**
 * T267: race 330 (the playable Froglok, FRM/FRF) lives in exactly one
 * archive, globalpcfroglok_chr.s3d. T265 fixed the data row; this file pins
 * the two loader facts that still blocked it:
 *
 *  1. getRaceModelFilename() hardcoded 330 -> globalfroglok_chr.s3d (FRO/FRG,
 *     the monster frogloks) instead of honouring the row's s3d_file.
 *  2. RaceModelLoader's s3d_file branch parsed every "global*" name as a
 *     numbered global (stoi("pcfroglok") -> 0 -> loads nothing).
 *
 * Offline: opens the shipped data/willeq.db as the global race source and the
 * real client tree (data/eq_client -> EverQuestP1999); skips if either is
 * absent. EDT_NULL Irrlicht device, no server, no DISPLAY.
 */

#include <gtest/gtest.h>
#include <fstream>
#include <string>

#include "client/willeq_database.h"

#ifdef EQT_HAS_GRAPHICS
#include "client/graphics/eq/race_codes.h"
#include "client/graphics/eq/race_model_loader.h"
#include "client/graphics/constrained_texture_cache.h"
#include "client/graphics/constrained_renderer_config.h"
#endif

namespace {

bool fileExists(const std::string& p) {
    std::ifstream f(p);
    return f.good();
}

std::string clientPath() {
    const char* envPath = std::getenv("EQ_CLIENT_PATH");
    std::string p = envPath ? envPath : EQ_CLIENT_DATA_PATH;
    if (!p.empty() && p.back() != '/') p += '/';
    return p;
}

class Race330Test : public ::testing::Test {
protected:
    EQT::WilleqDatabase db_;

    void SetUp() override {
        const std::string dbPath = WILLEQ_DB_REAL_PATH;
        if (!fileExists(dbPath)) GTEST_SKIP() << "no shipped db at " << dbPath;
        ASSERT_TRUE(db_.open(dbPath)) << dbPath;
        EQT::WilleqDatabase::setGlobalInstance(&db_);
    }
    void TearDown() override {
        EQT::WilleqDatabase::setGlobalInstance(nullptr);
    }
};

}  // namespace

#ifdef EQT_HAS_GRAPHICS

// The row is authoritative: 330's archive is the one that holds FRM/FRF.
TEST_F(Race330Test, ModelFilenameFollowsTheRow) {
    EXPECT_EQ(EQT::Graphics::getRaceModelFilename(330, 0), "globalpcfroglok_chr.s3d");
    EXPECT_EQ(EQT::Graphics::getRaceModelFilename(330, 1), "globalpcfroglok_chr.s3d");
}

// Rows whose s3d_file is global_chr / global{N}_chr keep the Luclin-style
// per-race name (that is what the archive index and new-models path key on).
TEST_F(Race330Test, NumberedGlobalRowsKeepTheConventionName) {
    EXPECT_EQ(EQT::Graphics::getRaceModelFilename(128, 0), "globalikm_chr.s3d");  // row: global4_chr.s3d
    EXPECT_EQ(EQT::Graphics::getRaceModelFilename(128, 1), "globalikf_chr.s3d");
    EXPECT_EQ(EQT::Graphics::getRaceModelFilename(1, 0), "globalhum_chr.s3d");    // row: global_chr.s3d
}

class Race330LoaderTest : public Race330Test {
protected:
    irr::IrrlichtDevice* device_ = nullptr;
    std::unique_ptr<EQT::Graphics::ConstrainedTextureCache> textureCache_;
    std::unique_ptr<EQT::Graphics::RaceModelLoader> loader_;

    void SetUp() override {
        Race330Test::SetUp();
        if (IsSkipped()) return;
        const std::string eq = clientPath();
        if (!fileExists(eq + "globalpcfroglok_chr.s3d") || !fileExists(eq + "global4_chr.s3d")) {
            GTEST_SKIP() << "EQ client tree not at " << eq;
        }
        irr::SIrrlichtCreationParameters params;
        params.DriverType = irr::video::EDT_NULL;
        params.WindowSize = irr::core::dimension2d<irr::u32>(64, 64);
        device_ = irr::createDeviceEx(params);
        if (!device_) GTEST_SKIP() << "no EDT_NULL device";

        loader_ = std::make_unique<EQT::Graphics::RaceModelLoader>(
            device_->getSceneManager(), device_->getVideoDriver(), device_->getFileSystem());
        loader_->setClientPath(eq);
        auto config = EQT::Graphics::ConstrainedRendererConfig::fromPreset(
            EQT::Graphics::ConstrainedRenderingPreset::TNT);
        textureCache_ = std::make_unique<EQT::Graphics::ConstrainedTextureCache>(config);
        textureCache_->setDriver(device_->getVideoDriver());
        loader_->setConstrainedTextureCache(textureCache_.get());
        loader_->getMeshBuilder()->setConstrainedTextureCache(textureCache_.get());
    }
    void TearDown() override {
        loader_.reset();
        textureCache_.reset();
        if (device_) { device_->drop(); device_ = nullptr; }
        Race330Test::TearDown();
    }
};

// The loader's s3d_file branch reaches a non-numeric global archive and
// finds the gendered playable froglok in it.
TEST_F(Race330LoaderTest, LoadsFrmAndFrfFromGlobalPcFroglok) {
    ASSERT_TRUE(loader_->isUsingOldModels());
    auto male = loader_->ensureModelDataLoaded(330, 0);
    ASSERT_NE(male, nullptr) << "race 330 male loaded nothing";
    EXPECT_NE(male->raceName.find("FRM"), std::string::npos) << male->raceName;
    ASSERT_NE(male->combinedGeometry, nullptr);
    EXPECT_GT(male->combinedGeometry->vertices.size(), 0u);

    auto female = loader_->ensureModelDataLoaded(330, 1);
    ASSERT_NE(female, nullptr) << "race 330 female loaded nothing";
    EXPECT_NE(female->raceName.find("FRF"), std::string::npos) << female->raceName;
    ASSERT_NE(female->combinedGeometry, nullptr);
    EXPECT_GT(female->combinedGeometry->vertices.size(), 0u);
}

// A row naming a numbered global (128 Iksar -> global4_chr.s3d) still loads
// through the numeric path.
TEST_F(Race330LoaderTest, NumberedGlobalRowStillLoads) {
    auto iksar = loader_->ensureModelDataLoaded(128, 0);
    ASSERT_NE(iksar, nullptr);
    EXPECT_NE(iksar->raceName.find("IKM"), std::string::npos) << iksar->raceName;
    ASSERT_NE(iksar->combinedGeometry, nullptr);
    EXPECT_GT(iksar->combinedGeometry->vertices.size(), 0u);
}

#endif  // EQT_HAS_GRAPHICS
