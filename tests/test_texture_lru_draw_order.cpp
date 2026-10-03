// B949 (GitLab #267) -- the texture cache's LRU follows DRAW order, not load
// order.
//
// Before B949 the LRU list was touched on lookup only, so its order was load
// order: the texture uploaded FIRST (the floor, the sky -- the ones drawn
// every frame) sat at the FRONT and was evicted first, then restored on its
// very next bind, evicting the next-oldest, which may also have been on
// screen. B944 measured 85 evictions / 21 restores on the gate's three static
// qeynos2 poses at a 20 MiB ceiling. Correct pixels (B944's net), but thrash.
//
// Now the driver stamps every texture with the frame it was last bound in
// (COGLES2Texture::markDrawn, on the getOpenGLTextureName() bind accessor),
// and ConstrainedTextureCache::endFrame() -- called once per rendered frame,
// after the frame's binds -- reorders the resident list by those stamps, so
// eviction falls off the least-recently-DRAWN end. These tests drive a real
// EDT_OGLES2 surfaceless device (the backend willeq and the gate use) through
// beginScene -> draw -> endScene -> endFrame "frames": a texture drawn every
// frame must survive eviction pressure from loads, and the control (the same
// sequence with no draws) must evict the oldest-loaded one, which is exactly
// the pre-B949 behaviour.
//
// Real EDT_OGLES2 surfaceless device; the fixture self-skips without EGL,
// exactly like test_texture_eviction_residency.cpp.

#include <gtest/gtest.h>

#ifdef EQT_HAS_GRAPHICS
#include "client/graphics/constrained_texture_cache.h"
#include "client/graphics/constrained_renderer_config.h"
#include <irrlicht.h>

#include <cstdint>
#include <cstring>
#include <map>
#include <memory>
#include <string>
#include <vector>

extern size_t gles2GetGpuTextureMemoryUsage(void* driver);

using namespace EQT::Graphics;

namespace {

constexpr int kSide = 32;
constexpr size_t kBytes = kSide * kSide * 4;  // RGBA8888, no mip inflation (enableMipmaps=false)

struct Rgb { uint8_t r, g, b; };

std::vector<char> makeBmp(Rgb c) {
    const int rowSize = ((kSide * 3 + 3) / 4) * 4;
    const uint32_t pixelBytes = static_cast<uint32_t>(rowSize) * kSide;
    std::vector<char> bmp(54 + pixelBytes, 0);
    auto* p = reinterpret_cast<uint8_t*>(bmp.data());
    p[0] = 'B'; p[1] = 'M';
    const uint32_t fileSize = static_cast<uint32_t>(bmp.size());
    std::memcpy(p + 2, &fileSize, 4);
    const uint32_t dataOffset = 54, dibSize = 40;
    std::memcpy(p + 10, &dataOffset, 4);
    std::memcpy(p + 14, &dibSize, 4);
    const int32_t side = kSide;
    std::memcpy(p + 18, &side, 4);
    std::memcpy(p + 22, &side, 4);
    const uint16_t planes = 1, bpp = 24;
    std::memcpy(p + 26, &planes, 2);
    std::memcpy(p + 28, &bpp, 2);
    for (int y = 0; y < kSide; ++y) {
        uint8_t* row = p + 54 + static_cast<size_t>(y) * rowSize;
        for (int x = 0; x < kSide; ++x) {
            row[x * 3 + 0] = c.b;
            row[x * 3 + 1] = c.g;
            row[x * 3 + 2] = c.r;
        }
    }
    return bmp;
}

// The restore source a zone load registers (ArchiveTextureSource), in memory.
class MapSource : public TextureSourceProvider {
public:
    std::map<std::string, std::vector<char>> bytes;
    bool hasSource(const std::string& name) override { return bytes.count(name) != 0; }
    bool readSource(const std::string& name, std::vector<char>& out) override {
        auto it = bytes.find(name);
        if (it == bytes.end()) return false;
        out = it->second;
        return true;
    }
};

class DrawOrderFixture : public ::testing::Test {
protected:
    irr::IrrlichtDevice* device_ = nullptr;
    irr::video::IVideoDriver* driver_ = nullptr;
    std::unique_ptr<ConstrainedTextureCache> cache_;

    void makeDevice() {
        irr::SIrrlichtCreationParameters params;
        params.DriverType = irr::video::EDT_OGLES2;
        params.WindowSize = irr::core::dimension2d<irr::u32>(64, 64);
        device_ = irr::createDeviceEx(params);
        if (device_) driver_ = device_->getVideoDriver();
    }

    void makeCache(size_t ceiling) {
        auto config = ConstrainedRendererConfig::fromPreset(ConstrainedRenderingPreset::TNT);
        config.textureMemoryBytes = ceiling;
        config.enableMipmaps = false;
        config.maxTextureDimension = 256;
        cache_ = std::make_unique<ConstrainedTextureCache>(config);
        cache_->setDriver(driver_);
    }

    void TearDown() override {
        cache_.reset();
        if (device_) device_->drop();
    }

    irr::video::ITexture* upload(const std::string& name, const std::vector<char>& bytes) {
        cache_->getOrLoad(name, bytes, "test", "B949");
        return cache_->uploadTexture(name);
    }

    // One "frame": beginScene (the driver's frame counter bumps), bind `tex`
    // through the driver's own 2D draw path (which stamps the texture with
    // this frame's number), endScene, then the cache's endFrame() reorder --
    // the same order application_frame.cpp and the benchmark run.
    void frame(irr::video::ITexture* tex) {
        driver_->beginScene(true, true, irr::video::SColor(255, 0, 0, 0));
        driver_->draw2DImage(tex, irr::core::rect<irr::s32>(0, 0, 64, 64),
                             irr::core::rect<irr::s32>(0, 0, kSide, kSide), nullptr, nullptr, false);
        driver_->endScene();
        cache_->endFrame();
    }

    // A frame that binds nothing (the counter still bumps, so stamps made
    // last frame become stale relative to the current frame number).
    void emptyFrame() {
        driver_->beginScene(true, true, irr::video::SColor(255, 0, 0, 0));
        driver_->endScene();
        cache_->endFrame();
    }
};

}  // namespace

// The card's acceptance: a texture drawn every frame survives eviction
// pressure from loads, while the ceiling is enforced by evicting the
// textures that are NOT on screen.
TEST_F(DrawOrderFixture, EveryFrameDrawnTextureSurvivesLoadPressure) {
    makeDevice();
    if (!device_) GTEST_SKIP() << "EDT_OGLES2 (surfaceless EGL) unavailable";
    makeCache(2 * kBytes);  // room for exactly two resident textures

    MapSource source;
    cache_->addSourceProvider(&source);

    const Rgb colors[10] = {{250, 10, 10}, {10, 250, 10}, {10, 10, 250}, {250, 250, 10},
                            {10, 250, 250}, {250, 10, 250}, {10, 250, 10}, {250, 250, 250},
                            {10, 10, 10}, {128, 128, 128}};
    // "a" is the floor/sky stand-in: loaded FIRST (worst position in load
    // order) and drawn every frame. "b<i>" are the loads pressing the
    // ceiling; they are never drawn.
    irr::video::ITexture* a = upload("b949_a.bmp", source.bytes["b949_a.bmp"] = makeBmp(colors[0]));
    irr::video::ITexture* b1 = upload("b949_b1.bmp", source.bytes["b949_b1.bmp"] = makeBmp(colors[1]));
    ASSERT_NE(a, nullptr);
    ASSERT_NE(b1, nullptr);

    for (int i = 2; i <= 9; ++i) {
        frame(a);  // a is on screen this frame
        const std::string name = "b949_b" + std::to_string(i) + ".bmp";
        irr::video::ITexture* b = upload(name, source.bytes[name] = makeBmp(colors[i]));
        ASSERT_NE(b, nullptr) << name;
        // Pre-B949 this failed at i == 2: a was the oldest-LOADED resident,
        // so the first load evicted it. Now a is the most recently DRAWN and
        // it is b<i-1> (the texture not on screen) that goes.
        EXPECT_TRUE(cache_->isResident("b949_a.bmp")) << "pressure round " << i;
        EXPECT_FALSE(cache_->isResident("b949_b" + std::to_string(i - 1) + ".bmp"))
            << "the off-screen texture " << i - 1 << " must be the eviction, not the floor";
        EXPECT_EQ(cache_->getEvictionCount(), static_cast<size_t>(i - 1));
        EXPECT_LE(cache_->getCurrentUsage(), cache_->getMemoryLimit());
        // a's pointer is still the one the material would hold (B944's net).
        EXPECT_EQ(cache_->getTexture("b949_a.bmp"), a);
    }

    // End state: a + the last load resident, everything off-screen evicted.
    EXPECT_TRUE(cache_->isResident("b949_a.bmp"));
    EXPECT_TRUE(cache_->isResident("b949_b9.bmp"));
    for (int i = 1; i <= 8; ++i) {
        EXPECT_FALSE(cache_->isResident("b949_b" + std::to_string(i) + ".bmp")) << i;
    }
    EXPECT_EQ(cache_->getRestoreCount(), 0u) << "a was never evicted, so nothing was restored";
    EXPECT_EQ(cache_->getRestoreFailureCount(), 0u);

    cache_->removeSourceProvider(&source);
}

// Control: the SAME kind of sequence with no draws at all. Every texture's
// draw stamp stays 0, so endFrame()'s stable sort keeps load order -- and
// the oldest-LOADED texture (a) is evicted by the first load that presses
// the ceiling. This is the pre-B949 behaviour, and it is what makes the
// test above discriminating: with the draw-order LRU off, a would be gone
// at the first pressure round.
TEST_F(DrawOrderFixture, WithoutDrawsEvictionStaysInLoadOrder) {
    makeDevice();
    if (!device_) GTEST_SKIP() << "EDT_OGLES2 (surfaceless EGL) unavailable";
    makeCache(kBytes);  // room for exactly ONE resident texture, so every
                        // load evicts something
    MapSource source;
    cache_->addSourceProvider(&source);
    irr::video::ITexture* a = upload("b949_a.bmp", source.bytes["b949_a.bmp"] = makeBmp({250, 10, 10}));
    ASSERT_NE(a, nullptr);
    // b1's own upload evicts a: nothing was drawn, so load order rules and
    // a (oldest loaded) is first out.
    irr::video::ITexture* b1 = upload("b949_b1.bmp", source.bytes["b949_b1.bmp"] = makeBmp({10, 250, 10}));
    ASSERT_NE(b1, nullptr);
    EXPECT_FALSE(cache_->isResident("b949_a.bmp"));
    EXPECT_EQ(cache_->getEvictionCount(), 1u);

    emptyFrame();
    irr::video::ITexture* b2 = upload("b949_b2.bmp", source.bytes["b949_b2.bmp"] = makeBmp({10, 10, 250}));
    ASSERT_NE(b2, nullptr);
    // b1 was never restored (nothing ever bound it), so load order evicts it
    // next -- the pre-B949 behaviour, frame after frame.
    EXPECT_FALSE(cache_->isResident("b949_b1.bmp"));
    EXPECT_TRUE(cache_->isResident("b949_b2.bmp"));
    EXPECT_EQ(cache_->getEvictionCount(), 2u);

    // a is not destroyed (B944): its next draw restores it.
    frame(a);
    EXPECT_TRUE(cache_->isResident("b949_a.bmp"));
    EXPECT_EQ(cache_->getRestoreCount(), 1u);

    cache_->removeSourceProvider(&source);
}

// Within one frame two textures draw with the SAME stamp (the stamp is a
// frame number, not a bind order): the tie keeps load order, so the
// oldest-loaded of the tied pair is the eviction candidate.
TEST_F(DrawOrderFixture, SameFrameTieKeepsLoadOrder) {
    makeDevice();
    if (!device_) GTEST_SKIP() << "EDT_OGLES2 (surfaceless EGL) unavailable";
    makeCache(2 * kBytes);

    MapSource source;
    cache_->addSourceProvider(&source);
    irr::video::ITexture* a = upload("b949_a.bmp", source.bytes["b949_a.bmp"] = makeBmp({200, 0, 0}));
    irr::video::ITexture* b = upload("b949_b.bmp", source.bytes["b949_b.bmp"] = makeBmp({0, 200, 0}));
    ASSERT_NE(a, nullptr);
    ASSERT_NE(b, nullptr);

    // Frame 1: both are on screen (same stamp).
    driver_->beginScene(true, true, irr::video::SColor(255, 0, 0, 0));
    driver_->draw2DImage(a, irr::core::rect<irr::s32>(0, 0, 64, 64),
                         irr::core::rect<irr::s32>(0, 0, kSide, kSide), nullptr, nullptr, false);
    driver_->draw2DImage(b, irr::core::rect<irr::s32>(0, 0, 64, 64),
                         irr::core::rect<irr::s32>(0, 0, kSide, kSide), nullptr, nullptr, false);
    driver_->endScene();
    cache_->endFrame();

    // The next load evicts a (oldest loaded of the tie), not b.
    irr::video::ITexture* c = upload("b949_c.bmp", source.bytes["b949_c.bmp"] = makeBmp({0, 0, 200}));
    ASSERT_NE(c, nullptr);
    EXPECT_FALSE(cache_->isResident("b949_a.bmp"));
    EXPECT_TRUE(cache_->isResident("b949_b.bmp"));

    // Frame 2: only c is on screen now. The next load evicts b, not c.
    frame(c);
    irr::video::ITexture* d = upload("b949_d.bmp", source.bytes["b949_d.bmp"] = makeBmp({128, 128, 0}));
    ASSERT_NE(d, nullptr);
    EXPECT_FALSE(cache_->isResident("b949_b.bmp"));
    EXPECT_TRUE(cache_->isResident("b949_c.bmp"));
    EXPECT_TRUE(cache_->isResident("b949_d.bmp"));
    EXPECT_EQ(cache_->getEvictionCount(), 2u);

    cache_->removeSourceProvider(&source);
}

// A texture that was evicted off-screen and comes back on a frame is
// restored by its own bind and immediately protected by the new stamp: the
// next load evicts an off-screen texture, not the one just restored.
TEST_F(DrawOrderFixture, RestoredTextureIsProtectedOnTheNextLoad) {
    makeDevice();
    if (!device_) GTEST_SKIP() << "EDT_OGLES2 (surfaceless EGL) unavailable";
    makeCache(2 * kBytes);

    MapSource source;
    cache_->addSourceProvider(&source);
    irr::video::ITexture* a = upload("b949_a.bmp", source.bytes["b949_a.bmp"] = makeBmp({200, 0, 0}));
    irr::video::ITexture* b1 = upload("b949_b1.bmp", source.bytes["b949_b1.bmp"] = makeBmp({0, 200, 0}));
    ASSERT_NE(a, nullptr);
    ASSERT_NE(b1, nullptr);

    // Frame 1: only b1 is on screen -> a is the eviction for the next load.
    frame(b1);
    irr::video::ITexture* b2 = upload("b949_b2.bmp", source.bytes["b949_b2.bmp"] = makeBmp({0, 0, 200}));
    ASSERT_NE(b2, nullptr);
    EXPECT_FALSE(cache_->isResident("b949_a.bmp"));
    EXPECT_TRUE(cache_->isResident("b949_b1.bmp"));

    // Frame 2: a comes back on screen. Its bind restores it (the restore
    // evicts the least-recently-drawn b1, not the just-restored a) and its
    // stamp now beats b2's.
    frame(a);
    EXPECT_TRUE(cache_->isResident("b949_a.bmp"));
    EXPECT_EQ(cache_->getRestoreCount(), 1u);
    EXPECT_EQ(cache_->getRestoreFailureCount(), 0u);

    // The next load evicts b2 (off screen), not a.
    irr::video::ITexture* b3 = upload("b949_b3.bmp", source.bytes["b949_b3.bmp"] = makeBmp({128, 128, 0}));
    ASSERT_NE(b3, nullptr);
    EXPECT_FALSE(cache_->isResident("b949_b2.bmp"));
    EXPECT_TRUE(cache_->isResident("b949_a.bmp"));
    EXPECT_TRUE(cache_->isResident("b949_b3.bmp"));
    EXPECT_EQ(cache_->getEvictionCount(), 3u);  // a, b1 (the restore), b2

    cache_->removeSourceProvider(&source);
}

#else
TEST(TextureLruDrawOrder, SkippedNoGraphics) { GTEST_SKIP() << "EQT_HAS_GRAPHICS not compiled in"; }
#endif
