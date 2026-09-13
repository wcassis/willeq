// T460 (board B520) -- the DRM/KMS software cursor must survive the texture
// cache's two destruction paths.
//
// THE DEFECT (Will: no cursor on the Pi). createSoftwareCursor() loads the
// cursor into ConstrainedTextureCache and keeps a raw pointer to it in
// IrrlichtRenderer::softwareCursorTexture_, which FrameComponent::
// renderFrameTail() draws while it is non-null. The cursor was never
// protectTexture()'d. So:
//   * zone unload -- unloadZone() -> ConstrainedTextureCache::clear() notifies
//     every unprotected texture's eviction listeners and destroys it;
//   * LRU pressure -- evictUntilAvailable() does the same to the oldest
//     unprotected entry, and a texture created once at startup is ALWAYS the
//     oldest (P10b's own finding: "first eviction in every qeynos2 run").
// Either way P10b's SingleTexturePointerGuard nulls the renderer's pointer --
// no use-after-free -- and nothing ever loads the cursor again. The cursor is
// gone for the rest of the process, from the first zone change on.
//
// Seam: the REAL cache on an EDT_NULL device (T124's idiom), the REAL
// acquisition the renderer calls, the REAL guard the renderer registers, and
// a local pointer standing in for softwareCursorTexture_ (the guard binds a
// reference to exactly such a member).

#include <gtest/gtest.h>

#ifdef EQT_HAS_GRAPHICS
#include "client/graphics/constrained_texture_cache.h"
#include "client/graphics/constrained_renderer_config.h"
#include "client/graphics/renderer_zone_merge.h"
#include "client/graphics/software_cursor_texture.h"
#include <irrlicht.h>

#include <fstream>
#include <iterator>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#ifndef WILLEQ_SOURCE_DIR
#define WILLEQ_SOURCE_DIR "."
#endif

using namespace EQT::Graphics;

namespace {

std::string readText(const std::string& path) {
    std::ifstream in(path);
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

std::vector<char> readBytes(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    return std::vector<char>(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

class SoftwareCursorEvictionTest : public ::testing::Test {
protected:
    irr::IrrlichtDevice* device_ = nullptr;
    irr::video::IVideoDriver* driver_ = nullptr;
    std::unique_ptr<ConstrainedTextureCache> cache_;

    void makeCache(size_t textureBudgetBytes) {
        irr::SIrrlichtCreationParameters params;
        params.DriverType = irr::video::EDT_NULL;
        params.WindowSize = irr::core::dimension2d<irr::u32>(64, 64);
        device_ = irr::createDeviceEx(params);
        ASSERT_NE(device_, nullptr) << "EDT_NULL device";
        driver_ = device_->getVideoDriver();

        auto config = ConstrainedRendererConfig::fromPreset(ConstrainedRenderingPreset::OrangePi);
        if (textureBudgetBytes) config.textureMemoryBytes = textureBudgetBytes;
        cache_ = std::make_unique<ConstrainedTextureCache>(config);
        cache_->setDriver(driver_);
        cache_->setSceneManager(device_->getSceneManager());
    }

    irr::video::ITexture* acquireCursor() {
        const auto bytes = readBytes(std::string(WILLEQ_SOURCE_DIR) +
                                     "/data/textures/software_cursor.bmp");
        EXPECT_FALSE(bytes.empty()) << "data/textures/software_cursor.bmp unreadable";
        return AcquireSoftwareCursorTexture(*cache_, bytes);
    }

    void TearDown() override {
        cache_.reset();
        if (device_) device_->drop();
    }
};

// Pre-fix RED: clear() evicted the cursor, the guard nulled the pointer, and
// the frame tail's `if (owner_.softwareCursorTexture_)` never drew again.
TEST_F(SoftwareCursorEvictionTest, AZoneUnloadLeavesTheRendererADrawableCursor) {
    makeCache(0);
    irr::video::ITexture* const loaded = acquireCursor();
    ASSERT_NE(loaded, nullptr);

    irr::video::ITexture* softwareCursorTexture = loaded;  // the renderer's member
    SingleTexturePointerGuard guard(softwareCursorTexture);
    cache_->addEvictionListener(&guard);

    cache_->clear();  // exactly what IrrlichtRenderer::unloadZone() calls

    EXPECT_NE(softwareCursorTexture, nullptr)
        << "zone unload evicted the software cursor; the renderer will never draw it again";
    EXPECT_EQ(softwareCursorTexture, loaded);
    EXPECT_TRUE(cache_->hasTexture(kSoftwareCursorCacheName))
        << "the cursor texture was destroyed by clear()";

    // And a SECOND zone unload, which is where a reload-on-demand scheme that
    // only ran once would show.
    cache_->clear();
    EXPECT_EQ(softwareCursorTexture, loaded);

    cache_->removeEvictionListener(&guard);
}

// Pre-fix RED: under a budget the cursor, loaded first, is the LRU victim.
TEST_F(SoftwareCursorEvictionTest, BudgetPressureNeverEvictsTheCursor) {
    const size_t kBudget = 64 * 1024;
    makeCache(kBudget);
    irr::video::ITexture* const loaded = acquireCursor();
    ASSERT_NE(loaded, nullptr);

    irr::video::ITexture* softwareCursorTexture = loaded;
    SingleTexturePointerGuard guard(softwareCursorTexture);
    cache_->addEvictionListener(&guard);

    // Sixteen zone textures of a quarter of the budget each: four budgets'
    // worth, so the cache must evict repeatedly.
    for (int i = 0; i < 16; ++i) {
        const std::string name = "t460_filler_" + std::to_string(i);
        irr::video::ITexture* t = driver_->addTexture(irr::core::dimension2d<irr::u32>(4, 4), name.c_str());
        ASSERT_NE(t, nullptr);
        ASSERT_TRUE(cache_->registerTexture(name, t, kBudget / 4)) << name;
    }

    ASSERT_GT(cache_->getEvictionCount(), 0u) << "the fixture did not create budget pressure";
    EXPECT_NE(softwareCursorTexture, nullptr)
        << "LRU eviction took the software cursor";
    EXPECT_TRUE(cache_->hasTexture(kSoftwareCursorCacheName));

    cache_->removeEvictionListener(&guard);
}

// The renderer really goes through the acquisition under test.
TEST(SoftwareCursorEvictionSource, CreateSoftwareCursorUsesTheProtectedAcquisition) {
    const std::string src = readText(std::string(WILLEQ_SOURCE_DIR) +
                                     "/src/client/graphics/renderer/irrlicht_renderer.cpp");
    const size_t fn = src.find("void IrrlichtRenderer::createSoftwareCursor()");
    ASSERT_NE(fn, std::string::npos);
    const size_t end = src.find("\n}\n", fn);
    const std::string body = src.substr(fn, end - fn);
    EXPECT_NE(body.find("AcquireSoftwareCursorTexture("), std::string::npos)
        << "createSoftwareCursor() no longer acquires the cursor through "
           "AcquireSoftwareCursorTexture(), so these tests no longer cover it";
    EXPECT_EQ(body.find("->getOrLoad("), std::string::npos)
        << "createSoftwareCursor() loads the cursor directly again, bypassing the protection";
}

}  // namespace

#endif  // EQT_HAS_GRAPHICS
