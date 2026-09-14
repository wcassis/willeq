// T279: pixel proof for the EQE1 ALPHA COMPANION texture's mip filter.
//
// T275 established that a mipmap MIN_FILTER over an INCOMPLETE ETC1 mip chain
// samples opaque black on GLES2, and fixed the two RGB paths by dropping to
// plain GL_LINEAR. The alpha companion (texture unit 1) was left on
// GL_NEAREST_MIPMAP_NEAREST guarded only by `mipLevels > 1` -- which is not
// the completeness predicate. Packs built before `9189ad69` stop the chain at
// 4x4 (7 levels for 256x256 where GLES2 wants 9): mipLevels > 1, still
// incomplete, still black. For an alpha companion "black" means alpha == 0
// everywhere, i.e. every alpha-tested fragment discarded -- invisible
// foliage/hair/capes. The 3D driver rewrites unit 0's filters on every
// material bind but never touches unit 1's, so nothing masks it there.
//
// These tests build two synthetic EQE1 packs -- one with a COMPLETE chain to
// 1x1, one TRUNCATED at 4x4 exactly the way the pre-`9189ad69` builder wrote
// them -- upload both through the real ConstrainedTextureCache ETC1 path, and
// draw the resulting alpha companion MINIFIED (which forces a reduced mip
// level to be sampled) into a real headless GLES2 framebuffer. Both must come
// back white, because both packs are solid white all the way down.
//
// Red/green, measured (T279): with the guard restored to `mipLevels > 1`,
// TruncatedChainAlphaCompanionStillSamplesWhite reads back (0,0,0) and fails.
//
// Self-skips when a GLES2 device is unavailable, following
// test_ui_atlas_pixels.cpp / test_constrained_texture_cache.cpp precedent.

#include <gtest/gtest.h>

#include "client/graphics/constrained_texture_cache.h"
#include "client/graphics/constrained_renderer_config.h"
#include "client/graphics/eqe1_loader.h"
#include "client/graphics/eqe1_format.h"
#include "scoped_temp_dir.h"

#include <irrlicht.h>

#include <cstdio>
#include <cstring>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

using namespace EQT::Graphics;

namespace {

// One ETC1 "individual mode" block decoding to solid white.
//   bytes 0-2: R1R2 G1G2 B1B2, all nibbles 15 -> base colour (255,255,255)
//   byte 3:    cw1=0, cw2=0, diff=0, flip=0
//   bytes 4-7: all pixel indices 0 -> modifier +2, clamped to 255
constexpr uint8_t kWhiteBlock[8] = {0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0x00};

void appendWhiteLevel(std::vector<uint8_t>& out, uint16_t w, uint16_t h) {
    const uint32_t bytes = etc1DataSize(w, h);
    for (uint32_t i = 0; i < bytes; i += 8) {
        out.insert(out.end(), kWhiteBlock, kWhiteBlock + 8);
    }
}

// Solid-white ETC1 chain of exactly `levels` levels, halving from w/h and
// clamping the way the builder of the era did (min 1 for a complete chain,
// min 4 for the pre-`9189ad69` truncated one -- the clamp only bites once the
// dimension is already below 4, so passing the right level count is enough).
std::vector<uint8_t> whiteChain(uint16_t w, uint16_t h, uint8_t levels, uint16_t minDim) {
    std::vector<uint8_t> out;
    for (uint8_t level = 0; level < levels; ++level) {
        appendWhiteLevel(out, w, h);
        w = static_cast<uint16_t>(w / 2 < minDim ? minDim : w / 2);
        h = static_cast<uint16_t>(h / 2 < minDim ? minDim : h / 2);
    }
    return out;
}

void putEntry(std::vector<uint8_t>& index, const char* name, uint16_t w, uint16_t h,
              uint8_t mips, bool hasAlpha, uint8_t entryType,
              uint32_t dataOffset, uint32_t dataSize) {
    EQE1IndexEntry e{};
    std::snprintf(e.name, sizeof(e.name), "%s", name);
    e.width = w;
    e.height = h;
    e.mipLevels = mips;
    e.hasAlpha = hasAlpha ? 1 : 0;
    e.entryType = entryType;
    e.payloadFormat = EQE1_PAYLOAD_ETC1;
    e.dataOffset = dataOffset;
    e.dataSize = dataSize;
    const uint8_t* p = reinterpret_cast<const uint8_t*>(&e);
    index.insert(index.end(), p, p + sizeof(e));
}

// Write a minimal 2-entry v1 pack: an RGBA ETC1 texture plus its "_alpha"
// companion, both solid white, both with `levels` mip levels.
bool writePack(const std::string& path, const std::string& texName,
               uint16_t dim, uint8_t levels, uint16_t minDim) {
    const std::vector<uint8_t> chain = whiteChain(dim, dim, levels, minDim);

    EQE1Header hdr{};
    hdr.magic = EQE1_MAGIC;
    hdr.version = EQE1_VERSION;
    hdr.numEntries = 2;

    const uint32_t indexEnd = sizeof(EQE1Header) + 2 * EQE1_INDEX_ENTRY_SIZE;
    const uint32_t rgbOffset = alignTo16(indexEnd);
    const uint32_t alphaOffset =
        alignTo16(rgbOffset + static_cast<uint32_t>(chain.size()));

    std::vector<uint8_t> index;
    putEntry(index, texName.c_str(), dim, dim, levels, /*hasAlpha=*/true,
             EQE1_TYPE_ETC1_RGBA, rgbOffset, static_cast<uint32_t>(chain.size()));
    putEntry(index, (texName + "_alpha").c_str(), dim, dim, levels, /*hasAlpha=*/false,
             EQE1_TYPE_ETC1_RGB, alphaOffset, static_cast<uint32_t>(chain.size()));

    std::vector<uint8_t> file;
    const uint8_t* h = reinterpret_cast<const uint8_t*>(&hdr);
    file.insert(file.end(), h, h + sizeof(hdr));
    file.insert(file.end(), index.begin(), index.end());
    file.resize(rgbOffset, 0);
    file.insert(file.end(), chain.begin(), chain.end());
    file.resize(alphaOffset, 0);
    file.insert(file.end(), chain.begin(), chain.end());

    std::ofstream f(path, std::ios::binary);
    if (!f) return false;
    f.write(reinterpret_cast<const char*>(file.data()),
            static_cast<std::streamsize>(file.size()));
    return f.good();
}

}  // namespace

class EQE1AlphaCompanionMipTest : public ::testing::Test {
protected:
    irr::IrrlichtDevice* device_ = nullptr;
    irr::video::IVideoDriver* driver_ = nullptr;
    std::unique_ptr<ConstrainedTextureCache> cache_;
    EQE1Loader loader_;
    std::string packPath_;
    willeq_test::ScopedTempDir tmp_{"t279_alpha_mip"};  // T477 (B571)

    static constexpr irr::u32 kW = 256;
    static constexpr irr::u32 kH = 256;

    void SetUp() override {
        irr::SIrrlichtCreationParameters params;
        params.DriverType = irr::video::EDT_OGLES2;
        params.WindowSize = irr::core::dimension2d<irr::u32>(kW, kH);
        params.Fullscreen = true;  // surfaceless-EGL idiom (test_ui_atlas_pixels)
        device_ = irr::createDeviceEx(params);
        if (!device_) {
            GTEST_SKIP() << "Failed to create EDT_OGLES2 device (surfaceless EGL "
                            "unavailable in this environment) — skipping.";
        }
        driver_ = device_->getVideoDriver();
    }

    void TearDown() override {
        cache_.reset();
        loader_.close();
        if (!packPath_.empty()) std::remove(packPath_.c_str());
        if (device_) {
            device_->drop();
            device_ = nullptr;
        }
    }

    // Build a pack with the given chain length, upload the texture, and draw
    // its ALPHA COMPANION minified 8:1 into the framebuffer. Returns the
    // colour read back from the middle of the drawn quad.
    ::testing::AssertionResult drawAlphaCompanionMinified(const char* tag, uint8_t levels,
                                                         uint16_t minDim,
                                                         irr::video::SColor& out) {
        const std::string name = std::string("t279_") + tag;
        packPath_ = (tmp_ / (name + ".eqe1")).string();
        if (!writePack(packPath_, name, 256, levels, minDim))
            return ::testing::AssertionFailure() << "failed to write " << packPath_;
        if (!loader_.open(packPath_))
            return ::testing::AssertionFailure() << "failed to open " << packPath_;

        const EQE1Loader::TextureEntry* e = loader_.lookup(name);
        if (!e) return ::testing::AssertionFailure() << "entry " << name << " not found";
        if (e->mipLevels != levels)
            return ::testing::AssertionFailure() << "wrote " << int(levels)
                                                 << " levels, read " << int(e->mipLevels);
        if (!e->hasAlpha || !e->alphaData)
            return ::testing::AssertionFailure() << "no alpha companion for " << name;

        auto config = ConstrainedRendererConfig::fromPreset(ConstrainedRenderingPreset::TNT);
        cache_ = std::make_unique<ConstrainedTextureCache>(config);
        cache_->setDriver(driver_);
        cache_->addEQE1Loader(&loader_);

        std::vector<char> empty;  // EQE1 supplies the bytes
        cache_->getOrLoad(name, empty, "test", "T279AlphaCompanionMip");
        irr::video::ITexture* rgb = cache_->uploadTexture(name);
        if (!rgb) return ::testing::AssertionFailure() << "uploadTexture failed";
        irr::video::ITexture* alpha = cache_->getAlphaTexture(name);
        if (!alpha) return ::testing::AssertionFailure() << "no alpha texture cached";

        // Minify 256x256 -> 32x32 so the sampler must reach a reduced mip
        // level. An INCOMPLETE texture returns (0,0,0,1) here; a complete one
        // returns the white we encoded at every level.
        driver_->beginScene(true, true, irr::video::SColor(255, 255, 0, 255));
        driver_->draw2DImage(alpha,
                             irr::core::rect<irr::s32>(112, 112, 144, 144),
                             irr::core::rect<irr::s32>(0, 0, 256, 256),
                             nullptr, nullptr, /*useAlphaChannel=*/false);
        driver_->endScene();

        irr::video::IImage* img = driver_->createScreenShot();
        if (!img) return ::testing::AssertionFailure() << "createScreenShot returned null";
        out = img->getPixel(128, 128);
        img->drop();
        return ::testing::AssertionSuccess();
    }
};

// A COMPLETE chain (9 levels for 256x256) is mipmap-complete, so
// GL_NEAREST_MIPMAP_NEAREST is legal and samples the white we encoded. This is
// the state every EQE1 pack ships in after T279's regeneration.
TEST_F(EQE1AlphaCompanionMipTest, CompleteChainAlphaCompanionSamplesWhite) {
    irr::video::SColor c;
    ASSERT_TRUE(drawAlphaCompanionMinified("complete", computeMipLevels(256, 256), 1, c));
    EXPECT_GT(c.getRed(), 200) << "alpha companion sampled dark at a reduced mip level";
    EXPECT_GT(c.getGreen(), 200);
    EXPECT_GT(c.getBlue(), 200);
}

// A TRUNCATED chain (7 levels, the pre-`9189ad69` shape) has mipLevels > 1 and
// is NOT mipmap-complete. The filter must degrade to plain GL_NEAREST rather
// than sample black. Restore the old `mipLevels > 1` guard and this reads
// (0,0,0) — that is the red half of the red/green.
TEST_F(EQE1AlphaCompanionMipTest, TruncatedChainAlphaCompanionStillSamplesWhite) {
    irr::video::SColor c;
    // 256,128,64,32,16,8,4 == 7 levels, clamped at 4x4.
    ASSERT_TRUE(drawAlphaCompanionMinified("truncated", 7, 4, c));
    EXPECT_GT(c.getRed(), 200) << "incomplete chain sampled black — the T275 defect, "
                                  "on the alpha companion this time";
    EXPECT_GT(c.getGreen(), 200);
    EXPECT_GT(c.getBlue(), 200);
}

// The completeness predicate itself, independent of any GL state: this is what
// the upload path now keys the filter choice on.
TEST(EQE1AlphaCompanionMipPredicate, ComputeMipLevelsReachesOneByOne) {
    EXPECT_EQ(computeMipLevels(256, 256), 9);
    EXPECT_EQ(computeMipLevels(512, 512), 10);
    EXPECT_EQ(computeMipLevels(64, 64), 7);
    EXPECT_EQ(computeMipLevels(8, 10), 4);
    EXPECT_EQ(computeMipLevels(1, 1), 1);
}
