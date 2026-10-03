// B948 (GitLab #266) -- getGpuTextureMemoryUsage() counts each ETC1 alpha
// chain exactly once.
//
// Pre-B948 the pair's MAIN texture was wrapped with mainChain + alphaChain
// while its `_alpha` companion carried alphaChain AGAIN, so the driver's sum
// (gpuTextureBytes, the gate's budget-asserted and baseline-guarded figure)
// counted the alpha chain twice for every ETC1 texture with alpha. The cache
// entry's own sizeBytes (and therefore textureCacheBytesUsed) always counted
// the pair once, which is why on EQE1 runs gpuTextureBytes exceeded
// textureCacheBytesUsed (B927 census: qeynos2 16,797,104 vs 16,250,840).
//
// These tests upload one ETC1 texture WITH an alpha companion through the
// real ConstrainedTextureCache ETC1 path (a synthetic 3-entry pack, the T279
// device) and assert the driver's figure is exactly main + alpha, each chain
// counted ONCE. The second case drives the B944 eviction/restore round trip
// and asserts the restore re-adopts the same corrected figures (a restore
// that handed the pair back to the main texture would put the alpha chain on
// it a second time). The third is the no-alpha control: a companion-less
// ETC1 texture counts its own chain, unchanged by the fix.
//
// Red half, measured: pre-fix the first test read mainChain + 2*alphaChain
// and failed; the restore test read mainChain + 2*alphaChain again after the
// round trip; the control passed both ways (that is what makes the others
// discriminating).
//
// Real EDT_OGLES2 surfaceless device; self-skips without EGL, exactly like
// test_eqe1_alpha_companion_mip / test_texture_eviction_residency.

#include <gtest/gtest.h>

#include "client/graphics/constrained_texture_cache.h"
#include "client/graphics/constrained_renderer_config.h"
#include "client/graphics/eqe1_format.h"
#include "client/graphics/eqe1_loader.h"
#include "scoped_temp_dir.h"

#include <irrlicht.h>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

extern size_t gles2GetGpuTextureMemoryUsage(void* driver);

using namespace EQT::Graphics;

namespace {

// One ETC1 "individual mode" block decoding to solid white (T279's block).
constexpr uint8_t kWhiteBlock[8] = {0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0x00};

void appendWhiteLevel(std::vector<uint8_t>& out, uint16_t w, uint16_t h) {
    const uint32_t bytes = etc1DataSize(w, h);
    for (uint32_t i = 0; i < bytes; i += 8)
        out.insert(out.end(), kWhiteBlock, kWhiteBlock + 8);
}

// Solid-white ETC1 mip chain down to 1x1 (`levels` levels).
std::vector<uint8_t> whiteChain(uint16_t w, uint16_t h, uint8_t levels) {
    std::vector<uint8_t> out;
    for (uint8_t level = 0; level < levels; ++level) {
        appendWhiteLevel(out, w, h);
        w = static_cast<uint16_t>(std::max<uint16_t>(1, w / 2));
        h = static_cast<uint16_t>(std::max<uint16_t>(1, h / 2));
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

// Three-entry v1 pack: "b948texa" WITH its "_alpha" companion (both ETC1, 256x256,
// complete 9-level chains) and "b948texn" WITHOUT one.
bool writePack(const std::string& path) {
    const uint8_t levels = computeMipLevels(256, 256);
    const std::vector<uint8_t> chain = whiteChain(256, 256, levels);

    EQE1Header hdr{};
    hdr.magic = EQE1_MAGIC;
    hdr.version = EQE1_VERSION;
    hdr.numEntries = 3;

    const uint32_t indexEnd = sizeof(EQE1Header) + 3 * EQE1_INDEX_ENTRY_SIZE;
    const uint32_t aOffset = alignTo16(indexEnd);
    const uint32_t aAlphaOffset = alignTo16(aOffset + static_cast<uint32_t>(chain.size()));
    const uint32_t nOffset = alignTo16(aAlphaOffset + static_cast<uint32_t>(chain.size()));

    std::vector<uint8_t> index;
    putEntry(index, "b948texa", 256, 256, levels, /*hasAlpha=*/true,
             EQE1_TYPE_ETC1_RGBA, aOffset, static_cast<uint32_t>(chain.size()));
    putEntry(index, "b948texa_alpha", 256, 256, levels, /*hasAlpha=*/false,
             EQE1_TYPE_ETC1_RGB, aAlphaOffset, static_cast<uint32_t>(chain.size()));
    putEntry(index, "b948texn", 256, 256, levels, /*hasAlpha=*/false,
             EQE1_TYPE_ETC1_RGB, nOffset, static_cast<uint32_t>(chain.size()));

    std::vector<uint8_t> file;
    const uint8_t* h = reinterpret_cast<const uint8_t*>(&hdr);
    file.insert(file.end(), h, h + sizeof(hdr));
    file.insert(file.end(), index.begin(), index.end());
    file.resize(aOffset, 0);
    file.insert(file.end(), chain.begin(), chain.end());
    file.resize(aAlphaOffset, 0);
    file.insert(file.end(), chain.begin(), chain.end());
    file.resize(nOffset, 0);
    file.insert(file.end(), chain.begin(), chain.end());

    std::ofstream f(path, std::ios::binary);
    if (!f) return false;
    f.write(reinterpret_cast<const char*>(file.data()),
            static_cast<std::streamsize>(file.size()));
    return f.good();
}

class AlphaCountFixture : public ::testing::Test {
protected:
    irr::IrrlichtDevice* device_ = nullptr;
    irr::video::IVideoDriver* driver_ = nullptr;
    std::unique_ptr<ConstrainedTextureCache> cache_;
    EQE1Loader loader_;
    std::string packPath_;
    willeq_test::ScopedTempDir tmp_{"b948_alpha_count"};

    // The wrapped figures (the cache entry's own accounting) for this pack.
    const size_t mainChain_ = etc1MipChainSize(256, 256, computeMipLevels(256, 256));
    const size_t alphaChain_ = etc1MipChainSize(256, 256, computeMipLevels(256, 256));
    const size_t pairBytes_ = mainChain_ + alphaChain_;  // b948texa's cache entry

    void SetUp() override {
        irr::SIrrlichtCreationParameters params;
        params.DriverType = irr::video::EDT_OGLES2;
        params.WindowSize = irr::core::dimension2d<irr::u32>(64, 64);
        params.Fullscreen = true;  // surfaceless-EGL idiom
        device_ = irr::createDeviceEx(params);
        if (!device_) {
            GTEST_SKIP() << "Failed to create EDT_OGLES2 device (surfaceless EGL "
                           "unavailable in this environment) -- skipping.";
        }
        driver_ = device_->getVideoDriver();
    }

    void TearDown() override {
        cache_.reset();
        loader_.close();
        if (!packPath_.empty()) std::remove(packPath_.c_str());
        if (device_) device_->drop();
    }

    bool prepare(size_t ceiling) {
        packPath_ = (tmp_ / "b948.eqe1").string();
        if (!writePack(packPath_)) return false;
        if (!loader_.open(packPath_)) return false;
        auto config = ConstrainedRendererConfig::fromPreset(ConstrainedRenderingPreset::TNT);
        config.textureMemoryBytes = ceiling;
        cache_ = std::make_unique<ConstrainedTextureCache>(config);
        cache_->setDriver(driver_);
        cache_->addEQE1Loader(&loader_);
        return true;
    }

    irr::video::ITexture* upload(const std::string& name) {
        std::vector<char> empty;  // the pack supplies the bytes
        cache_->getOrLoad(name, empty, "test", "B948");
        return cache_->uploadTexture(name);
    }
};

}  // namespace

// THE acceptance: one ETC1 texture with alpha, uploaded once. The driver's
// figure must be main chain + alpha chain, each chain counted exactly ONCE.
TEST_F(AlphaCountFixture, Etc1WithAlphaCountsEachChainOnce) {
    ASSERT_TRUE(prepare(64u * 1024u * 1024u));

    const size_t baseline = gles2GetGpuTextureMemoryUsage(driver_);
    irr::video::ITexture* rgb = upload("b948texa");
    ASSERT_NE(rgb, nullptr);
    irr::video::ITexture* alpha = cache_->getAlphaTexture("b948texa");
    ASSERT_NE(alpha, nullptr);

    const size_t driverBytes = gles2GetGpuTextureMemoryUsage(driver_) - baseline;
    EXPECT_EQ(driverBytes, pairBytes_)
        << "the driver must count the pair exactly once (pre-B948 it read "
        << pairBytes_ + alphaChain_ << " -- the alpha chain twice)";
    // The cache entry owns both storages: its usage is the same pair.
    EXPECT_EQ(cache_->getCurrentUsage(), pairBytes_);
    // And the two agree, the census invariant (B927's gpuTextureBytes >
    // textureCacheBytesUsed gap).
    EXPECT_EQ(driverBytes, cache_->getCurrentUsage());
}

// B944's restore path: evict, then re-bind. The re-adopted byte counts must
// be the corrected ones (main chain on the main, alpha chain on the
// companion) -- a restore that handed the pair back to the main texture
// would put the alpha chain on it a second time.
TEST_F(AlphaCountFixture, RestoreReAdoptsTheCorrectedByteCounts) {
    // The ceiling holds b948texa's pair but not pair + b948texn: the second upload
    // must evict b948texa (the B944 round trip).
    ASSERT_TRUE(prepare(pairBytes_));

    const size_t baseline = gles2GetGpuTextureMemoryUsage(driver_);
    irr::video::ITexture* rgb = upload("b948texa");
    ASSERT_NE(rgb, nullptr);
    irr::video::ITexture* alpha = cache_->getAlphaTexture("b948texa");
    ASSERT_NE(alpha, nullptr);
    ASSERT_EQ(gles2GetGpuTextureMemoryUsage(driver_) - baseline, pairBytes_);
    ASSERT_TRUE(cache_->isResident("b948texa"));

    irr::video::ITexture* other = upload("b948texn");
    ASSERT_NE(other, nullptr);
    ASSERT_FALSE(cache_->isResident("b948texa")) << "b948texn's upload had to evict b948texa";

    // Re-bind through the driver's own path (B944's restore).
    const uint32_t restored = rgb->getDriverTextureHandle();
    ASSERT_NE(restored, 0u) << "the bind must restore the evicted storage";
    ASSERT_TRUE(cache_->isResident("b948texa"));
    ASSERT_FALSE(cache_->isResident("b948texn")) << "restoring b948texa evicted b948texn in turn";
    EXPECT_EQ(gles2GetGpuTextureMemoryUsage(driver_) - baseline, pairBytes_)
        << "after the B944 round trip the driver must still count each chain "
           "exactly once (main on the main, alpha on the companion)";
    EXPECT_EQ(cache_->getRestoreCount(), 1u);
    EXPECT_EQ(cache_->getRestoreFailureCount(), 0u);
}

// The no-alpha control: a companion-less ETC1 texture counts its own chain
// only -- unchanged by the fix, and what makes the alpha case discriminating.
TEST_F(AlphaCountFixture, Etc1WithoutAlphaCountsItsChainOnce) {
    ASSERT_TRUE(prepare(64u * 1024u * 1024u));

    const size_t baseline = gles2GetGpuTextureMemoryUsage(driver_);
    irr::video::ITexture* rgb = upload("b948texn");
    ASSERT_NE(rgb, nullptr);
    ASSERT_EQ(cache_->getAlphaTexture("b948texn"), nullptr);

    EXPECT_EQ(gles2GetGpuTextureMemoryUsage(driver_) - baseline, mainChain_);
    EXPECT_EQ(cache_->getCurrentUsage(), mainChain_);
}
