/**
 * T412 Leg A (board B291) -- the skipEntityTextureUpload knob must actually
 * skip.
 *
 * `IrrlichtRenderer::initDeferredGraphics()` has always called
 * `entityRenderer_->setSkipTextureUpload(config_.constrainedConfig.skipEntityTextureUpload)`
 * with the value parsed out of the shipped `config/debug.json`. The member was
 * stored and never read, so entity textures were uploaded regardless and the
 * documented debug flag was inert -- T407's write-only field census found it.
 *
 * These tests drive the real production seam,
 * `EntityRenderer::uploadPendingEntityTextures()`, which
 * `processOneEntityBuildStep()`'s SceneNodeCreation phase now calls in place of
 * its bare `constrainedTextureCache_->uploadAllPending()`. No EQ client data,
 * no server: an EDT_NULL device, a real `ConstrainedTextureCache`, and two
 * synthetic BMPs decoded into it via the cache's own `getOrLoad()`.
 *
 * Pre-fix (with the `if (skipTextureUpload_)` arm removed) SkipTrueUploadsNothing
 * FAILS: both textures are promoted to the GPU with the flag set.
 */

#include <gtest/gtest.h>

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "client/graphics/constrained_renderer_config.h"
#include "client/graphics/constrained_texture_cache.h"
#include "client/graphics/entity_renderer.h"

#include <irrlicht.h>

namespace EQT {
namespace Graphics {
namespace {

// Minimal valid 24bpp uncompressed BMP so ConstrainedTextureCache::decodeBMP()
// succeeds. Same construction as tests/test_constrained_texture_cache.cpp.
std::vector<char> makeTestBmp(int width, int height, uint8_t r, uint8_t g, uint8_t b) {
    int rowSize = ((width * 3 + 3) / 4) * 4;
    uint32_t pixelDataSize = static_cast<uint32_t>(rowSize) * height;
    uint32_t fileSize = 54 + pixelDataSize;

    std::vector<char> bmp(fileSize, 0);
    uint8_t* p = reinterpret_cast<uint8_t*>(bmp.data());

    p[0] = 'B'; p[1] = 'M';
    *reinterpret_cast<uint32_t*>(p + 2) = fileSize;
    *reinterpret_cast<uint32_t*>(p + 10) = 54;
    *reinterpret_cast<uint32_t*>(p + 14) = 40;
    *reinterpret_cast<int32_t*>(p + 18) = width;
    *reinterpret_cast<int32_t*>(p + 22) = height;
    *reinterpret_cast<uint16_t*>(p + 26) = 1;
    *reinterpret_cast<uint16_t*>(p + 28) = 24;
    *reinterpret_cast<uint32_t*>(p + 34) = pixelDataSize;

    for (int y = 0; y < height; ++y) {
        uint8_t* row = p + 54 + static_cast<size_t>(y) * rowSize;
        for (int x = 0; x < width; ++x) {
            row[x * 3 + 0] = b;
            row[x * 3 + 1] = g;
            row[x * 3 + 2] = r;
        }
    }
    return bmp;
}

class EntityTextureUploadSkipTest : public ::testing::Test {
protected:
    irr::IrrlichtDevice* device_ = nullptr;
    std::unique_ptr<ConstrainedTextureCache> cache_;
    std::unique_ptr<EntityRenderer> entities_;

    void SetUp() override {
        irr::SIrrlichtCreationParameters params;
        params.DriverType = irr::video::EDT_NULL;
        params.WindowSize = irr::core::dimension2d<irr::u32>(64, 64);
        device_ = irr::createDeviceEx(params);
        if (!device_) GTEST_SKIP() << "Failed to create EDT_NULL Irrlicht device";

        auto config = ConstrainedRendererConfig::fromPreset(ConstrainedRenderingPreset::TNT);
        cache_ = std::make_unique<ConstrainedTextureCache>(config);
        cache_->setDriver(device_->getVideoDriver());

        entities_ = std::make_unique<EntityRenderer>(
            device_->getSceneManager(), device_->getVideoDriver(), device_->getFileSystem());
        entities_->setConstrainedTextureCache(cache_.get());
    }

    void TearDown() override {
        entities_.reset();
        cache_.reset();
        if (device_) { device_->drop(); device_ = nullptr; }
    }

    // Decode two entity-ish textures into the cache without uploading them.
    // getOrLoad() returns nullptr for a fresh decode -- the DecodedEntry is
    // pending until something promotes it.
    void decodeTwoPendingTextures() {
        auto a = makeTestBmp(4, 4, 200, 10, 10);
        auto b = makeTestBmp(4, 4, 10, 200, 10);
        ASSERT_EQ(nullptr, cache_->getOrLoad("t412_entity_a", a, "entity_perpart"));
        ASSERT_EQ(nullptr, cache_->getOrLoad("t412_entity_b", b, "entity_perpart"));
        ASSERT_EQ(0u, cache_->getTextureCount()) << "nothing may be on the GPU yet";
    }
};

// The knob's whole contract: set it, and the entity build path performs no
// texture upload at all. This is the clause that fails without the fix.
TEST_F(EntityTextureUploadSkipTest, SkipTrueUploadsNothing) {
    decodeTwoPendingTextures();

    entities_->setSkipTextureUpload(true);
    EXPECT_TRUE(entities_->skipTextureUpload());

    EXPECT_EQ(0, entities_->uploadPendingEntityTextures());
    EXPECT_EQ(0u, cache_->getTextureCount());
    EXPECT_FALSE(cache_->hasTexture("t412_entity_a"));
    EXPECT_FALSE(cache_->hasTexture("t412_entity_b"));
}

// The control: with the knob clear the same call uploads both, so the skip
// above is the flag's doing and not a broken fixture.
TEST_F(EntityTextureUploadSkipTest, SkipFalseUploadsEverythingPending) {
    decodeTwoPendingTextures();

    entities_->setSkipTextureUpload(false);
    EXPECT_FALSE(entities_->skipTextureUpload());

    EXPECT_EQ(2, entities_->uploadPendingEntityTextures());
    EXPECT_EQ(2u, cache_->getTextureCount());
    EXPECT_TRUE(cache_->hasTexture("t412_entity_a"));
    EXPECT_TRUE(cache_->hasTexture("t412_entity_b"));
}

// Skipping must not DISCARD the decoded bytes -- a later frame with the flag
// cleared has to be able to upload them. (A "skip" that dropped the entries
// would make the flag destructive rather than diagnostic.)
TEST_F(EntityTextureUploadSkipTest, SkippedTexturesStayPendingAndUploadLater) {
    decodeTwoPendingTextures();

    entities_->setSkipTextureUpload(true);
    EXPECT_EQ(0, entities_->uploadPendingEntityTextures());

    entities_->setSkipTextureUpload(false);
    EXPECT_EQ(2, entities_->uploadPendingEntityTextures());
    EXPECT_EQ(2u, cache_->getTextureCount());
}

// The default is "upload", matching config/debug.json's shipped false and the
// struct default of ConstrainedRendererConfig::skipEntityTextureUpload.
TEST_F(EntityTextureUploadSkipTest, DefaultIsUpload) {
    EXPECT_FALSE(entities_->skipTextureUpload());
    EXPECT_FALSE(ConstrainedRendererConfig::fromPreset(
        ConstrainedRenderingPreset::OrangePi).skipEntityTextureUpload)
        << "the orangepi preset must not silently disable entity textures";
}

}  // namespace
}  // namespace Graphics
}  // namespace EQT
