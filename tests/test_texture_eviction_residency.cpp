// B944 (GitLab #262) -- ConstrainedTextureCache eviction under its ceiling
// with LIVE consumers.
//
// Before B944 an LRU eviction DESTROYED the evicted ITexture. Every raw
// pointer a consumer held -- an SMaterial slot, ZoneMeshBuilder's name map,
// a cached mesh -- dangled unless an eviction-listener guard reached it:
// gles2_zone_benchmark's qeynos2 golden run at textureMemoryBytes = 20 MiB
// read a freed texture's name in ZoneBench::textureGroupId (SIGSEGV), and the
// ASan client read one in RaceModelLoader::createAnimatedNodeWithEquipment
// (a cached EQAnimatedMesh's material; review/B944/README.md).
//
// Now eviction releases the GL storage and keeps the object; the driver's
// first bind restores it. These tests hold textures in materials the cache
// knows nothing about (exactly the benchmark's shape), drive the cache past
// its ceiling, and assert: the objects stay valid and registered with the
// driver, the accounting counts only resident storage, a draw through the
// driver's own bind path restores the SAME pixels, and the one path that still
// destroys (no restore source) scrubs the holders the cache itself filled.
//
// Real EDT_OGLES2 surfaceless device (the backend willeq and the gate use);
// each fixture self-skips when EGL or the EQE1 asset is unavailable.

#include <gtest/gtest.h>

#ifdef EQT_HAS_GRAPHICS
#include "client/graphics/constrained_texture_cache.h"
#include "client/graphics/constrained_renderer_config.h"
#include "client/graphics/eqe1_loader.h"
#include "client/graphics/eq/zone_geometry.h"
#include <irrlicht.h>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <fstream>
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
    int reads = 0;
    bool hasSource(const std::string& name) override { return bytes.count(name) != 0; }
    bool readSource(const std::string& name, std::vector<char>& out) override {
        auto it = bytes.find(name);
        if (it == bytes.end()) return false;
        out = it->second;
        ++reads;
        return true;
    }
};

class CountingListener : public TextureEvictionListener {
public:
    int calls = 0;
    void onTextureEvicted(const std::string&, irr::video::ITexture*) override { ++calls; }
};

class ResidencyFixture : public ::testing::Test {
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
        cache_->getOrLoad(name, bytes, "test", "B944");
        return cache_->uploadTexture(name);
    }

    // Draw `tex` through the driver's own 2D bind path and read back the
    // framebuffer's centre pixel.
    irr::video::SColor drawAndSample(irr::video::ITexture* tex) {
        driver_->beginScene(true, true, irr::video::SColor(255, 0, 0, 0));
        // Full-screen quad, so the sampled pixel is inside it whichever way
        // up the screenshot is.
        driver_->draw2DImage(tex, irr::core::rect<irr::s32>(0, 0, 64, 64),
                             irr::core::rect<irr::s32>(0, 0, kSide, kSide), nullptr, nullptr, false);
        irr::video::IImage* shot = driver_->createScreenShot();
        irr::video::SColor c(0, 0, 0, 0);
        if (shot) {
            c = shot->getPixel(32, 32);
            shot->drop();
        }
        driver_->endScene();
        return c;
    }
};

}  // namespace

// The benchmark's shape: materials the cache does not know about hold every
// texture; the ceiling fits three of five.
TEST_F(ResidencyFixture, EvictionKeepsTheObjectAndABindRestoresTheSamePixels) {
    makeDevice();
    if (!device_) GTEST_SKIP() << "EDT_OGLES2 (surfaceless EGL) unavailable";
    makeCache(3 * kBytes);

    const Rgb colors[5] = {{250, 10, 10}, {10, 250, 10}, {10, 10, 250}, {250, 250, 10}, {10, 250, 250}};
    MapSource source;
    CountingListener listener;
    cache_->addSourceProvider(&source);
    cache_->addEvictionListener(&listener);
    const size_t driverBaseline = gles2GetGpuTextureMemoryUsage(driver_);

    std::vector<irr::video::ITexture*> textures;
    std::vector<irr::video::SMaterial> materials(5);  // live consumers
    for (int i = 0; i < 5; ++i) {
        const std::string name = "b944_tex" + std::to_string(i) + ".bmp";
        source.bytes[name] = makeBmp(colors[i]);
        irr::video::ITexture* tex = upload(name, source.bytes[name]);
        ASSERT_NE(tex, nullptr) << name;
        textures.push_back(tex);
        materials[i].setTexture(0, tex);
    }

    // The ceiling held: two evictions, three resident, five live objects.
    EXPECT_EQ(cache_->getEvictionCount(), 2u);
    EXPECT_EQ(cache_->getResidentTextureCount(), 3u);
    EXPECT_EQ(cache_->getTextureCount(), 5u);
    EXPECT_LE(cache_->getCurrentUsage(), cache_->getMemoryLimit());
    EXPECT_EQ(cache_->getCurrentUsage(), 3 * kBytes);
    // Honest driver accounting: the two evicted textures hold no storage.
    EXPECT_EQ(gles2GetGpuTextureMemoryUsage(driver_) - driverBaseline, 3 * kBytes);
    // An eviction that can restore does not tell the guards to swap holders
    // to the placeholder.
    EXPECT_EQ(listener.calls, 0);

    for (int i = 0; i < 2; ++i) {
        const std::string name = "b944_tex" + std::to_string(i) + ".bmp";
        EXPECT_FALSE(cache_->isResident(name)) << name;
        // The object the material holds is the one the cache and the driver
        // still hold (pre-B944: removed from the driver and freed).
        EXPECT_EQ(cache_->getTexture(name), textures[i]);
        EXPECT_EQ(driver_->findTexture(name.c_str()), textures[i]);
        EXPECT_EQ(materials[i].getTexture(0), textures[i]);
        EXPECT_EQ(std::string(textures[i]->getName().getPath().c_str()), name);  // the benchmark's read
        EXPECT_TRUE(cache_->isLiveTexture(textures[i]));
    }

    // A draw of the evicted texture through the driver restores it -- the
    // same pixels -- and evicts the oldest resident one to stay under.
    const irr::video::SColor drawn = drawAndSample(materials[0].getTexture(0));
    EXPECT_EQ(drawn.getRed(), colors[0].r);
    EXPECT_EQ(drawn.getGreen(), colors[0].g);
    EXPECT_EQ(drawn.getBlue(), colors[0].b);
    EXPECT_TRUE(cache_->isResident("b944_tex0.bmp"));
    EXPECT_EQ(cache_->getRestoreCount(), 1u);
    EXPECT_EQ(cache_->getRestoreFailureCount(), 0u);
    EXPECT_EQ(source.reads, 1);
    EXPECT_EQ(cache_->getResidentTextureCount(), 3u);
    EXPECT_EQ(cache_->getCurrentUsage(), 3 * kBytes);
    EXPECT_EQ(gles2GetGpuTextureMemoryUsage(driver_) - driverBaseline, 3 * kBytes);
    EXPECT_EQ(listener.calls, 0);

    // Every texture draws its own colour, whatever is resident when it is
    // bound -- five restores/evictions round the ring, no dangling use.
    for (int round = 0; round < 2; ++round) {
        for (int i = 0; i < 5; ++i) {
            const irr::video::SColor c = drawAndSample(materials[i].getTexture(0));
            EXPECT_EQ(c.getRed(), colors[i].r) << "texture " << i << " round " << round;
            EXPECT_EQ(c.getGreen(), colors[i].g) << "texture " << i << " round " << round;
            EXPECT_EQ(c.getBlue(), colors[i].b) << "texture " << i << " round " << round;
            EXPECT_LE(cache_->getCurrentUsage(), cache_->getMemoryLimit());
        }
    }
    EXPECT_EQ(cache_->getRestoreFailureCount(), 0u);

    cache_->removeEvictionListener(&listener);
    cache_->removeSourceProvider(&source);
}

// With no restore source the cache still destroys the texture (pre-B944
// behaviour, guards swap holders to the placeholder) -- and it now also drops
// the texture from ZoneMeshBuilder's name map, which the cache fills itself
// (registerUploadedTexture) and never scrubbed.
TEST_F(ResidencyFixture, WithoutASourceTheTextureIsDestroyedAndTheMeshBuilderForgetsIt) {
    makeDevice();
    if (!device_) GTEST_SKIP() << "EDT_OGLES2 (surfaceless EGL) unavailable";
    makeCache(2 * kBytes);
    ZoneMeshBuilder builder(device_->getSceneManager(), driver_, device_->getFileSystem());
    cache_->setMeshBuilder(&builder);
    CountingListener listener;
    cache_->addEvictionListener(&listener);

    irr::video::ITexture* first = upload("b944_first.bmp", makeBmp({200, 0, 0}));
    ASSERT_NE(first, nullptr);
    EXPECT_TRUE(builder.hasTexture("b944_first.bmp"));
    ASSERT_NE(upload("b944_second.bmp", makeBmp({0, 200, 0})), nullptr);
    ASSERT_NE(upload("b944_third.bmp", makeBmp({0, 0, 200})), nullptr);

    EXPECT_EQ(cache_->getEvictionCount(), 1u);
    EXPECT_FALSE(cache_->hasTexture("b944_first.bmp"));
    EXPECT_EQ(driver_->findTexture("b944_first.bmp"), nullptr);
    EXPECT_FALSE(builder.hasTexture("b944_first.bmp"))
        << "ZoneMeshBuilder kept a pointer to the destroyed texture";
    EXPECT_EQ(listener.calls, 1);
    EXPECT_LE(cache_->getCurrentUsage(), cache_->getMemoryLimit());

    cache_->removeEvictionListener(&listener);
    cache_->setMeshBuilder(nullptr);
}

// ---------------------------------------------------------------------------
// ETC1 (the shipped Pi path): the pack is the restore source, and the alpha
// companion is released and restored with its main texture.
// ---------------------------------------------------------------------------

namespace {
const char* kObjPack = "/mnt/projects/EverQuest_ETC1/qeynos2_obj.eqe1";
}

class ResidencyEtc1Fixture : public ResidencyFixture {
protected:
    EQE1Loader loader_;
    bool ready_ = false;

    void prepare(size_t ceiling) {
        std::ifstream f(kObjPack);
        if (!f.good() || !loader_.open(kObjPack)) return;
        makeDevice();
        if (!device_) return;
        makeCache(ceiling);
        cache_->addEQE1Loader(&loader_);
        ready_ = true;
    }

    irr::video::ITexture* uploadPacked(const std::string& name) {
        std::vector<char> none;  // the pack supplies the bytes
        cache_->getOrLoad(name, none, "test", "B944");
        return cache_->uploadTexture(name);
    }
};

TEST_F(ResidencyEtc1Fixture, EvictionReleasesAndABindRestoresTheCompanionToo) {
    prepare(64u * 1024u * 1024u);
    if (!ready_) GTEST_SKIP() << kObjPack << " or EDT_OGLES2 unavailable";

    // fire1 carries an alpha companion (test_eqe1_loader.cpp). Measure it and
    // one other entry under a roomy ceiling, then rebuild the cache with a
    // ceiling that holds either alone but not both.
    std::string other;
    for (const auto& n : loader_.getTextureNames()) {
        if (n != "fire1" && n.find("_alpha") == std::string::npos) { other = n; break; }
    }
    ASSERT_FALSE(other.empty());
    ASSERT_NE(uploadPacked("fire1"), nullptr);
    const size_t fireBytes = cache_->getCurrentUsage();
    ASSERT_NE(uploadPacked(other), nullptr);
    const size_t otherBytes = cache_->getCurrentUsage() - fireBytes;

    makeCache(std::max(fireBytes, otherBytes) + std::min(fireBytes, otherBytes) - 1);
    cache_->addEQE1Loader(&loader_);
    const size_t base2 = gles2GetGpuTextureMemoryUsage(driver_);
    irr::video::ITexture* fire = uploadPacked("fire1");
    irr::video::ITexture* companion = cache_->getAlphaTexture("fire1");
    ASSERT_NE(fire, nullptr);
    ASSERT_NE(companion, nullptr);
    const size_t fireDriverBytes = gles2GetGpuTextureMemoryUsage(driver_) - base2;
    ASSERT_GT(fireDriverBytes, 0u);
    ASSERT_NE(uploadPacked(other), nullptr);

    EXPECT_FALSE(cache_->isResident("fire1"));
    EXPECT_EQ(cache_->getTexture("fire1"), fire);
    EXPECT_EQ(cache_->getAlphaTexture("fire1"), companion) << "the companion object survives too";
    EXPECT_EQ(driver_->findTexture("fire1_alpha"), companion);
    EXPECT_EQ(fire->getDriverTextureHandle() != 0, true)
        << "getDriverTextureHandle() restores (the raw-GL consumers' path)";
    EXPECT_TRUE(cache_->isResident("fire1"));
    EXPECT_FALSE(cache_->isResident(other)) << "restoring fire1 had to evict the other one";
    EXPECT_EQ(companion->getDriverTextureHandle() != 0, true) << "restored with its main texture";
    EXPECT_EQ(gles2GetGpuTextureMemoryUsage(driver_) - base2, fireDriverBytes)
        << "driver bytes after the round trip are fire1's own, nothing leaked";
    EXPECT_EQ(cache_->getRestoreCount(), 1u);
}

TEST_F(ResidencyEtc1Fixture, ClearDestroysTheAlphaCompanionWithItsTexture) {
    prepare(64u * 1024u * 1024u);
    if (!ready_) GTEST_SKIP() << kObjPack << " or EDT_OGLES2 unavailable";
    const size_t baseline = gles2GetGpuTextureMemoryUsage(driver_);
    ASSERT_NE(uploadPacked("fire1"), nullptr);
    ASSERT_NE(driver_->findTexture("fire1_alpha"), nullptr);

    cache_->clear();

    EXPECT_EQ(driver_->findTexture("fire1"), nullptr);
    EXPECT_EQ(driver_->findTexture("fire1_alpha"), nullptr)
        << "clear() left the ETC1 alpha companion in the driver (pre-B944 leak)";
    EXPECT_EQ(gles2GetGpuTextureMemoryUsage(driver_), baseline);
}

#else
TEST(TextureEvictionResidency, SkippedNoGraphics) { GTEST_SKIP() << "EQT_HAS_GRAPHICS not compiled in"; }
#endif
