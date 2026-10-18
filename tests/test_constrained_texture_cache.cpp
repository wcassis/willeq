/**
 * T92: coverage for two things found while diagnosing the "decode failed
 * (0 bytes)" WARNs T85's qeynos2 capture logged:
 *
 *  - Part 1: ConstrainedTextureCache::getOrLoad() must report an empty
 *    input buffer distinctly from a genuine decoder rejection of non-empty
 *    bytes. Before this unit both cases logged the identical "decode
 *    failed" message, which sent an earlier investigation upstream into
 *    decodeBMP()/DDSDecoder when the actual defect was a caller
 *    (RaceModelLoader::preloadModelData()'s composite-sheet lookup)
 *    supplying an intentionally empty buffer on the assumption an EQE1
 *    flat pack would supply the bytes -- which never happens when no
 *    --eqe1-path is configured for the run. See this unit's Review.
 *
 *  - Part 3 (P50): hasAlpha(name)/getTexture(name)/getAlphaTexture(name)/
 *    isETC1Texture(name) looked up cache_ by plain name, but
 *    uploadTexture(name, applyPaletteMask=true) stores masked variants
 *    under the mangled cacheKey(name, true) = name + "\x01masked" -- a
 *    plain-name query for a masked-variant entry always missed. This
 *    exercises the new applyPaletteMask parameter added to all four query
 *    methods.
 *
 * EDT_NULL device (no window, no GPU, no DISPLAY) -- same idiom as
 * test_inventory_model_view.cpp's RaceModelAnimationHeadlessTest.
 */

#include <gtest/gtest.h>
#include <algorithm>
#include <chrono>
#include <list>
#include <thread>
#include <vector>
#include <cstring>
#include <functional>
#include <string>

#ifdef EQT_HAS_GRAPHICS
// T486 (item 1): ProtectedEntriesSurviveClearWithValidLruIterators reads the
// LRU list and each entry's iterator into it, which no public method exposes.
#define private public
#define protected public
#include "client/graphics/constrained_texture_cache.h"
#undef private
#undef protected
#include "client/graphics/constrained_renderer_config.h"
#include "client/graphics/eqe1_loader.h"
#include "common/logging.h"
#include <irrlicht.h>
#include <fstream>
#endif

#ifdef EQT_HAS_GRAPHICS

using namespace EQT::Graphics;

namespace {

// Mirrors ConstrainedTextureCache::cacheKey() (private) — documented format,
// name + "\x01masked" for the masked variant. Used only to plant/verify
// entries at the exact key uploadTexture(applyPaletteMask=true) would use,
// via the public registerTexture()/hasTexture() API (no private access).
std::string maskedKeyForTest(const std::string& name) {
    return name + "\x01masked";
}

// Builds a minimal valid 24bpp BMP (uncompressed, BITMAPINFOHEADER) so
// decodeBMP() succeeds. width/height small and fixed; fillByte lets two
// calls produce texture data that hashes/differs if ever needed.
std::vector<char> makeTestBmp(int width, int height, uint8_t r, uint8_t g, uint8_t b) {
    int rowSize = ((width * 3 + 3) / 4) * 4;
    uint32_t pixelDataSize = static_cast<uint32_t>(rowSize) * height;
    uint32_t fileSize = 54 + pixelDataSize;

    std::vector<char> bmp(fileSize, 0);
    uint8_t* p = reinterpret_cast<uint8_t*>(bmp.data());

    p[0] = 'B'; p[1] = 'M';
    *reinterpret_cast<uint32_t*>(p + 2) = fileSize;
    *reinterpret_cast<uint32_t*>(p + 10) = 54;  // data offset
    *reinterpret_cast<uint32_t*>(p + 14) = 40;  // DIB header size (BITMAPINFOHEADER)
    *reinterpret_cast<int32_t*>(p + 18) = width;
    *reinterpret_cast<int32_t*>(p + 22) = height;  // bottom-up
    *reinterpret_cast<uint16_t*>(p + 26) = 1;   // planes
    *reinterpret_cast<uint16_t*>(p + 28) = 24;  // bits per pixel
    *reinterpret_cast<uint32_t*>(p + 34) = pixelDataSize;

    for (int y = 0; y < height; ++y) {
        uint8_t* row = p + 54 + static_cast<size_t>(y) * rowSize;
        for (int x = 0; x < width; ++x) {
            row[x * 3 + 0] = b;  // BMP stores BGR
            row[x * 3 + 1] = g;
            row[x * 3 + 2] = r;
        }
    }
    return bmp;
}

class ConstrainedTextureCacheTest : public ::testing::Test {
protected:
    irr::IrrlichtDevice* device_ = nullptr;
    irr::video::IVideoDriver* driver_ = nullptr;
    std::unique_ptr<ConstrainedTextureCache> cache_;

    void SetUp() override {
        irr::SIrrlichtCreationParameters params;
        params.DriverType = irr::video::EDT_NULL;
        params.WindowSize = irr::core::dimension2d<irr::u32>(64, 64);
        device_ = irr::createDeviceEx(params);
        if (!device_) {
            GTEST_SKIP() << "Failed to create EDT_NULL Irrlicht device";
        }
        driver_ = device_->getVideoDriver();

        auto config = ConstrainedRendererConfig::fromPreset(ConstrainedRenderingPreset::TNT);
        cache_ = std::make_unique<ConstrainedTextureCache>(config);
        cache_->setDriver(driver_);
    }

    void TearDown() override {
        cache_.reset();
        if (device_) {
            device_->drop();
            device_ = nullptr;
        }
    }
};

}  // namespace

// ---------------------------------------------------------------------------
// Part 3 (P50): masked-variant query methods
// ---------------------------------------------------------------------------

// getTexture(name)/getTexture(name, true) must resolve to the DISTINCT
// unmasked/masked uploads of the same base name -- proving the new
// parameter reaches the mangled cacheKey() and that omitting it still finds
// only the unmasked entry (byte-identical to pre-T92 behavior).
TEST_F(ConstrainedTextureCacheTest, GetTextureRespectsApplyPaletteMask) {
    const std::string name = "p50_get_texture_test";

    auto unmaskedBmp = makeTestBmp(4, 4, 200, 10, 10);
    auto maskedBmp = makeTestBmp(4, 4, 10, 10, 200);

    // Unmasked variant.
    cache_->getOrLoad(name, unmaskedBmp, "test", "GetTextureRespectsApplyPaletteMask",
                       /*applyPaletteMask=*/false);
    irr::video::ITexture* unmaskedTex = cache_->uploadTexture(name, /*applyPaletteMask=*/false);
    ASSERT_NE(unmaskedTex, nullptr);

    // Masked variant, SAME name, distinct internal key.
    cache_->getOrLoad(name, maskedBmp, "test", "GetTextureRespectsApplyPaletteMask",
                       /*applyPaletteMask=*/true);
    irr::video::ITexture* maskedTex = cache_->uploadTexture(name, /*applyPaletteMask=*/true);
    ASSERT_NE(maskedTex, nullptr);

    // Different GPU textures for the two variants.
    EXPECT_NE(unmaskedTex, maskedTex);

    // Findable through the new parameter.
    EXPECT_EQ(cache_->getTexture(name, /*applyPaletteMask=*/false), unmaskedTex);
    EXPECT_EQ(cache_->getTexture(name, /*applyPaletteMask=*/true), maskedTex);

    // Default (no argument) matches the unmasked-only pre-T92 behavior.
    EXPECT_EQ(cache_->getTexture(name), unmaskedTex);
}

// A masked-only upload (no unmasked variant ever uploaded for this name)
// must NOT be findable by a plain-name query -- this is the exact P50 bug:
// before the parameter existed, hasAlpha(name)/getTexture(name)/etc. always
// missed a masked-only entry.
TEST_F(ConstrainedTextureCacheTest, MaskedOnlyUploadNotFoundWithoutParameter) {
    const std::string name = "p50_masked_only_test";
    auto bmp = makeTestBmp(4, 4, 5, 5, 5);

    cache_->getOrLoad(name, bmp, "test", "MaskedOnlyUploadNotFoundWithoutParameter",
                       /*applyPaletteMask=*/true);
    irr::video::ITexture* maskedTex = cache_->uploadTexture(name, /*applyPaletteMask=*/true);
    ASSERT_NE(maskedTex, nullptr);

    // Found WITH the parameter.
    EXPECT_EQ(cache_->getTexture(name, /*applyPaletteMask=*/true), maskedTex);
    EXPECT_TRUE(cache_->hasTexture(maskedKeyForTest(name)));

    // NOT found without it (the P50 bug, reproduced and now fixable by
    // callers that pass the parameter).
    EXPECT_EQ(cache_->getTexture(name), nullptr);
    EXPECT_EQ(cache_->getTexture(name, /*applyPaletteMask=*/false), nullptr);
}

// hasAlpha() must key on applyPaletteMask the same way. Give the masked and
// unmasked variants opposite alpha outcomes (via registerTexture(), which
// stores directly under whatever key it's given and takes an explicit
// hasAlphaFlag) so the two query results can only match if the lookup is
// actually keyed by the mangled name -- a name-only lookup would either
// always find the unmasked entry (masked query silently wrong) or collide.
TEST_F(ConstrainedTextureCacheTest, HasAlphaRespectsApplyPaletteMask) {
    const std::string name = "p50_has_alpha_test";

    // Fabricate two distinct GPU textures via addTexture (EDT_NULL still
    // creates real ITexture* handles -- see RaceModelAnimationHeadlessTest).
    std::vector<uint32_t> pixels(4 * 4, 0xFFFFFFFF);
    irr::video::IImage* img1 = driver_->createImageFromData(
        irr::video::ECF_A8R8G8B8, irr::core::dimension2d<irr::u32>(4, 4), pixels.data(), false);
    irr::video::ITexture* unmaskedTex = driver_->addTexture("p50_alpha_unmasked", img1);
    img1->drop();
    irr::video::IImage* img2 = driver_->createImageFromData(
        irr::video::ECF_A8R8G8B8, irr::core::dimension2d<irr::u32>(4, 4), pixels.data(), false);
    irr::video::ITexture* maskedTex = driver_->addTexture("p50_alpha_masked", img2);
    img2->drop();
    ASSERT_NE(unmaskedTex, nullptr);
    ASSERT_NE(maskedTex, nullptr);

    // Plant them at the exact keys uploadTexture(applyPaletteMask=false/true)
    // would have used, with opposite hasAlpha flags.
    ASSERT_TRUE(cache_->registerTexture(name, unmaskedTex, 64, /*hasAlphaFlag=*/false, "test"));
    ASSERT_TRUE(cache_->registerTexture(maskedKeyForTest(name), maskedTex,
                                         64, /*hasAlphaFlag=*/true, "test"));

    EXPECT_FALSE(cache_->hasAlpha(name));                       // default = unmasked = false
    EXPECT_FALSE(cache_->hasAlpha(name, /*applyPaletteMask=*/false));
    EXPECT_TRUE(cache_->hasAlpha(name, /*applyPaletteMask=*/true));  // masked = true, only reachable with the parameter
}

// ---------------------------------------------------------------------------
// isETC1Texture()/getAlphaTexture(): same P50 gap, but these two fields
// (CachedTexture::isETC1, CachedTexture::alphaTexture) are only ever set by
// the real ETC1/EQE1 upload path (uploadCompressedETC1(), which needs a
// live GL context — glGenTextures/glCompressedTexImage2D). registerTexture()
// (used above for hasAlpha) cannot set them. Exercise the real path instead,
// against the same real .eqe1 archive test_eqe1_loader.cpp already depends
// on, under a real EDT_OGLES2 surfaceless-EGL device (same backend
// ./build/bin/willeq uses headlessly by default — see CLAUDE.md). Skips if
// either is unavailable in this environment rather than failing the suite.
// ---------------------------------------------------------------------------

namespace {
const char* kTestObjEqe1 = "/mnt/projects/EverQuest_ETC1/qeynos2_obj.eqe1";
bool fileExists(const char* path) {
    std::ifstream f(path);
    return f.good();
}
}  // namespace

class ConstrainedTextureCacheETC1Test : public ::testing::Test {
protected:
    irr::IrrlichtDevice* device_ = nullptr;
    irr::video::IVideoDriver* driver_ = nullptr;
    std::unique_ptr<ConstrainedTextureCache> cache_;
    EQE1Loader loader_;

    void SetUp() override {
        if (!fileExists(kTestObjEqe1)) {
            GTEST_SKIP() << "Real EQE1 archive not present at " << kTestObjEqe1
                         << " (same asset test_eqe1_loader.cpp depends on) — skipping.";
        }
        if (!loader_.open(kTestObjEqe1)) {
            GTEST_SKIP() << "Failed to open " << kTestObjEqe1;
        }

        irr::SIrrlichtCreationParameters params;
        params.DriverType = irr::video::EDT_OGLES2;
        params.WindowSize = irr::core::dimension2d<irr::u32>(64, 64);
        device_ = irr::createDeviceEx(params);
        if (!device_) {
            GTEST_SKIP() << "Failed to create EDT_OGLES2 device (surfaceless EGL unavailable "
                            "in this environment) — skipping.";
        }
        driver_ = device_->getVideoDriver();

        auto config = ConstrainedRendererConfig::fromPreset(ConstrainedRenderingPreset::TNT);
        cache_ = std::make_unique<ConstrainedTextureCache>(config);
        cache_->setDriver(driver_);
        cache_->addEQE1Loader(&loader_);
    }

    void TearDown() override {
        cache_.reset();
        if (device_) {
            device_->drop();
            device_ = nullptr;
        }
    }
};

// "fire1" is a known-alpha entry in qeynos2_obj.eqe1 (test_eqe1_loader.cpp's
// testAlphaCompanion). Upload it as the MASKED variant only (applyPaletteMask
// is irrelevant to the ETC1 path itself — see getOrLoad()'s comment — the
// point here is purely that it's stored under the mangled key) and confirm
// isETC1Texture()/getAlphaTexture() find it with applyPaletteMask=true and
// correctly miss it without.
TEST_F(ConstrainedTextureCacheETC1Test, ETC1QueriesRespectApplyPaletteMask) {
    std::vector<char> emptyData;  // EQE1 lookup supplies the real bytes
    cache_->getOrLoad("fire1", emptyData, "test", "ETC1QueriesRespectApplyPaletteMask",
                       /*applyPaletteMask=*/true);
    irr::video::ITexture* tex = cache_->uploadTexture("fire1", /*applyPaletteMask=*/true);
    ASSERT_NE(tex, nullptr) << "fire1 should upload via the real ETC1/EQE1 path";

    EXPECT_TRUE(cache_->isETC1Texture("fire1", /*applyPaletteMask=*/true));
    EXPECT_NE(cache_->getAlphaTexture("fire1", /*applyPaletteMask=*/true), nullptr)
        << "fire1 has a known alpha companion (test_eqe1_loader.cpp)";

    // Not found without the parameter — this variant was never uploaded
    // unmasked.
    EXPECT_FALSE(cache_->isETC1Texture("fire1"));
    EXPECT_FALSE(cache_->isETC1Texture("fire1", /*applyPaletteMask=*/false));
    EXPECT_EQ(cache_->getAlphaTexture("fire1"), nullptr);
    EXPECT_EQ(cache_->getAlphaTexture("fire1", /*applyPaletteMask=*/false), nullptr);
}

// ---------------------------------------------------------------------------
// Part 1: empty-input vs genuine decode failure must be distinguishable
// ---------------------------------------------------------------------------

namespace {
// Polls up to ~1s for the background log-drain thread (50ms cadence,
// include/common/logging.h) to flush this thread's buffered lines to
// stdout after FlushThreadLog() flips the active/inactive side.
std::string captureLoggedOutput(const std::function<void()>& action) {
    SetLogLevel(LOG_WARN);
    testing::internal::CaptureStdout();
    action();
    FlushThreadLog();
    for (int i = 0; i < 20; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(60));
    }
    return testing::internal::GetCapturedStdout();
}
}  // namespace

TEST_F(ConstrainedTextureCacheTest, EmptyInputReportedDistinctlyFromDecodeFailure) {
    // Case 1: caller supplies a genuinely empty buffer (T92 Part 1 -- the
    // shape of RaceModelLoader::preloadModelData()'s composite-sheet
    // lookup call when no EQE1 archive is configured).
    std::string emptyOutput = captureLoggedOutput([&]() {
        std::vector<char> emptyData;
        irr::video::ITexture* tex = cache_->getOrLoad(
            "t92_empty_case", emptyData, "test", "EmptyInputReportedDistinctlyFromDecodeFailure");
        EXPECT_EQ(tex, nullptr);
    });
    EXPECT_NE(emptyOutput.find("t92_empty_case"), std::string::npos)
        << "expected the texture name in the log output; full capture:\n" << emptyOutput;
    EXPECT_NE(emptyOutput.find("empty input"), std::string::npos)
        << "expected an empty-input-specific message; full capture:\n" << emptyOutput;
    EXPECT_EQ(emptyOutput.find("decode failed"), std::string::npos)
        << "empty input must NOT be reported as a decode failure; full capture:\n" << emptyOutput;

    // Case 2: caller supplies real, non-empty bytes that are not a valid
    // BMP or DDS -- a genuine decoder rejection.
    std::string garbageOutput = captureLoggedOutput([&]() {
        std::vector<char> garbage(64, 'x');  // not "BM", not a DDS magic
        irr::video::ITexture* tex = cache_->getOrLoad(
            "t92_garbage_case", garbage, "test", "EmptyInputReportedDistinctlyFromDecodeFailure");
        EXPECT_EQ(tex, nullptr);
    });
    EXPECT_NE(garbageOutput.find("t92_garbage_case"), std::string::npos)
        << "expected the texture name in the log output; full capture:\n" << garbageOutput;
    EXPECT_NE(garbageOutput.find("decode failed"), std::string::npos)
        << "non-empty, undecodable input must be reported as a decode failure; full capture:\n"
        << garbageOutput;
    EXPECT_EQ(garbageOutput.find("empty input"), std::string::npos)
        << "a genuine decode failure must NOT be reported as empty input; full capture:\n"
        << garbageOutput;
}

// ---------------------------------------------------------------------------
// T357 (B014): the caller the test above DIAGNOSED is now GATED.
//
// T92 pinned that an empty buffer is reported distinctly from a decode
// failure. It did not stop the call being made: RaceModelLoader::
// preloadModelData() fired getOrLoad() with a deliberately empty buffer --
// EQE1 is the only thing that can supply the bytes -- once per composite name
// on every run with a JSON manifest and no EQE1 tree, and each one was a
// guaranteed failure plus a warn.
//
// This is a source-reader pin rather than a call count because reaching the
// composite branch of preloadModelData() needs a real S3D model, a real
// composite manifest and an EDT_NULL device (tests/test_palette_mask_
// plumbing.cpp does exactly that and GTEST_SKIPs without the shipped EQ
// tree), so a counted assertion would not run on this host. What is
// deterministic, and what actually regressed, is whether the call sits
// inside the gate.
//
// Red before the fix: the guard read `if (constrainedTextureCache_) {` and
// nothing else.
TEST(Eqe1PreloadGate, CompositeSheetLookupIsGatedOnEqe1BeingConfigured) {
    const std::string path =
        std::string(WILLEQ_REPO_ROOT) + "/src/client/graphics/eq/race_model_loader.cpp";
    std::ifstream in(path);
    ASSERT_TRUE(in.is_open()) << "cannot open: " << path;
    std::vector<std::string> lines;
    for (std::string line; std::getline(in, line);) lines.push_back(line);

    // Locate the one composite-sheet lookup.
    size_t call = lines.size();
    for (size_t i = 0; i < lines.size(); ++i) {
        if (lines[i].find("getOrLoad(compositeBaseName") != std::string::npos) {
            EXPECT_EQ(call, lines.size()) << "more than one composite-sheet lookup; "
                                             "this pin only guards the first";
            call = i;
        }
    }
    ASSERT_LT(call, lines.size())
        << "the composite-sheet getOrLoad() call was not found -- if it was renamed or "
           "removed, update this pin rather than deleting it";

    // Walk back to its enclosing `if (constrainedTextureCache_` and require
    // that the SAME condition also tests the EQE1 configuration. eqe1Path is
    // the predicate every other EQE1 gate in the tree reads
    // (renderer_zone_load.cpp, renderer_init.cpp, eqe1_archive_component.cpp);
    // a second, invented predicate here would be the defect B014 warned about.
    bool gated = false;
    for (size_t i = call; i-- > 0;) {
        if (lines[i].find("if (constrainedTextureCache_") == std::string::npos) continue;
        std::string condition;
        for (size_t j = i; j <= call; ++j) {
            condition += lines[j];
            if (lines[j].find(") {") != std::string::npos) break;
        }
        gated = condition.find("eqe1Path.empty()") != std::string::npos;
        break;
    }
    EXPECT_TRUE(gated)
        << "RaceModelLoader::preloadModelData() calls getOrLoad() with an EMPTY buffer "
           "outside any EQE1 check -- with no EQE1 tree configured that is one "
           "guaranteed-to-fail call and one warn per composite name (B014).";
}


// ---------------------------------------------------------------------------
// T486 (2026-09-14 audit, item 1): clear() preserves the protected entries
// (T205) but never refreshed their CachedTexture::lruIterator after
// lruOrder_.clear() -- every preserved entry kept an iterator into a list
// whose nodes had been freed. The next touch of a protected name (getOrLoad
// hit, uploadTexture hit, registerTexture, getTexture, touch()) did
// lruOrder_.erase(<dangling>) -- undefined behaviour on freed memory. The UI
// atlas is protected for the life of the process and is touched on every
// zone after the first, so this was reached on every second zone load.
//
// The check is the invariant itself: every cached entry's iterator must point
// INTO lruOrder_ at the node carrying its own name. Three names are planted in
// ascending order and clear()'s preserve loop walks a std::map (ascending),
// while list::clear() frees the nodes in list order -- so with glibc's LIFO
// free-list reuse the stale iterators land on the WRONG live nodes and the
// name check fails; with any other allocator they point at no live node and
// the address check fails. Either way the pre-fix cache fails here without
// ever dereferencing freed memory.
// ---------------------------------------------------------------------------
TEST_F(ConstrainedTextureCacheTest, ProtectedEntriesSurviveClearWithValidLruIterators) {
    const char* names[] = {"t486_protected_a", "t486_protected_b", "t486_protected_c"};
    for (const char* name : names) {
        auto bmp = makeTestBmp(4, 4, 30, 60, 90);
        cache_->getOrLoad(name, bmp, "test", "ProtectedEntriesSurviveClearWithValidLruIterators");
        ASSERT_NE(cache_->uploadTexture(name), nullptr) << name;
        cache_->protectTexture(name);
    }
    ASSERT_EQ(cache_->getTextureCount(), 3u);

    cache_->clear();
    ASSERT_EQ(cache_->getTextureCount(), 3u) << "the protected entries did not survive clear()";
    ASSERT_EQ(cache_->lruOrder_.size(), 3u);

    auto iteratorIsValidFor = [&](const std::string& name,
                                  std::list<std::string>::iterator it) -> ::testing::AssertionResult {
        for (auto live = cache_->lruOrder_.begin(); live != cache_->lruOrder_.end(); ++live) {
            if (live == it) {
                if (*live == name) return ::testing::AssertionSuccess();
                return ::testing::AssertionFailure()
                       << "'" << name << "' holds an iterator to the node carrying '" << *live
                       << "' -- a stale iterator into the list clear() emptied";
            }
        }
        return ::testing::AssertionFailure()
               << "'" << name << "' holds an iterator to no live node of lruOrder_";
    };
    for (const auto& [name, entry] : cache_->cache_) {
        EXPECT_TRUE(iteratorIsValidFor(name, entry.lruIterator));
    }

    // And the user-reachable consequence: touching every protected name after
    // the zone-unload wipe leaves one LRU node per entry, none lost, none
    // doubled.
    for (const char* name : names) {
        cache_->touch(name);
        EXPECT_NE(cache_->getTexture(name), nullptr) << name;
    }
    EXPECT_EQ(cache_->lruOrder_.size(), 3u);
    for (const char* name : names) {
        EXPECT_EQ(std::count(cache_->lruOrder_.begin(), cache_->lruOrder_.end(), name), 1)
            << name << " is not exactly once in the LRU order";
    }
    for (const auto& [name, entry] : cache_->cache_) {
        EXPECT_TRUE(iteratorIsValidFor(name, entry.lruIterator));
    }
}

// ---------------------------------------------------------------------------
// B1083 (GitLab #403, ruling 2026-10-16): dedupe by content.
//
// The (archive, name) keying splits every name two of a zone's open archives
// hold into two entries -- even when the copies are byte-identical -- and
// those extra bytes bust the texture budget on zones the name-keyed run
// stayed under (the x86 gate: qeynos2 at 99.6% of its 20 MiB ceiling,
// evicted textures drawing black). The ruling: same (name, mask) with
// content-identical payload -> ONE entry (the second request becomes an
// alias of the first); genuinely different content (qeytoqrg tree70's
// DXT1-vs-DXT5) -> two entries, the willow fix itself.
//
// The decode path (BMP bytes in, no EQE1) is the x86 gate's path, and it
// runs under the EDT_NULL device this suite already uses -- no GL context
// needed.
// ---------------------------------------------------------------------------

// The ruling's first clause: the SAME image bytes under two archives are ONE
// cache entry. The second request's upload returns the FIRST request's
// texture (the pre-B1083 name-keyed cache's first-upload-wins answer), and
// every accessor answers the alias like its target. On the pre-dedupe branch
// (the gate regression) the second upload creates a second entry and the
// textures differ -- the test is RED there and GREEN here.
TEST_F(ConstrainedTextureCacheTest, SameBytesUnderTwoArchivesIsOneEntry) {
    const std::string name = "b1083_same_bytes_test";
    const auto bmp = makeTestBmp(4, 4, 20, 40, 60);

    // The zone's archive uploads first (configured order: {zone} first).
    cache_->getOrLoad(name, bmp, "test", "SameBytesUnderTwoArchivesIsOneEntry",
                      /*applyPaletteMask=*/false, /*origin=*/nullptr,
                      /*sourceArchiveStem=*/"zone_s3d");
    irr::video::ITexture* zoneTex =
        cache_->uploadTexture(name, /*applyPaletteMask=*/false, "zone_s3d");
    ASSERT_NE(zoneTex, nullptr) << "the zone's own copy must upload";

    // The sibling archive (a .s3d of the same zone family holding the same
    // name) asks for the same name with the same bytes.
    cache_->getOrLoad(name, bmp, "test", "SameBytesUnderTwoArchivesIsOneEntry",
                      /*applyPaletteMask=*/false, /*origin=*/nullptr,
                      /*sourceArchiveStem=*/"obj_s3d");
    irr::video::ITexture* objTex =
        cache_->uploadTexture(name, /*applyPaletteMask=*/false, "obj_s3d");
    ASSERT_NE(objTex, nullptr) << "the sibling request must still be served";

    // ONE GPU entry: the alias answers the twin's texture.
    EXPECT_EQ(objTex, zoneTex)
        << "content-identical same-(name, mask) requests must share one entry";
    EXPECT_EQ(cache_->getTextureCount(), 1u)
        << "the dedupe must not create a second (archive, name) entry";
    EXPECT_EQ(cache_->getDedupedEntryCount(), 1u)
        << "the alias is the bookkeeping the entry count is allowed to hide";

    // The scoped accessor answers the alias like its target: the object's
    // request resolves to the zone entry's texture through the alias remap
    // in findEntryForKey. (hasTexture()/isResident() key on the plain
    // name/mask, which has no entry here -- both copies are archive-scoped --
    // so they are not in this family's accessor set.)
    EXPECT_EQ(cache_->getTexture(name, /*applyPaletteMask=*/false, "obj_s3d"), zoneTex);
    EXPECT_EQ(cache_->getTexture(name, /*applyPaletteMask=*/false, "zone_s3d"), zoneTex);

    // An unscoped request keeps the configured-order answer (the zone's
    // entry -- the first in family key order).
    EXPECT_EQ(cache_->getTexture(name), zoneTex);
}

// The ruling's second clause: GENUINELY different content under the same
// (name, mask) keeps two entries. This is the willow fix itself (qeytoqrg
// tree70: the zone copy opaque, the object copy with its alpha) and must
// not be folded by the dedupe -- the digest is over the decoded pixels, not
// the file bytes.
TEST_F(ConstrainedTextureCacheTest, DifferentBytesUnderTwoArchivesKeepTwoEntries) {
    const std::string name = "b1083_diff_bytes_test";
    const auto zoneBmp = makeTestBmp(4, 4, 200, 10, 10);
    const auto objBmp = makeTestBmp(4, 4, 10, 10, 200);

    cache_->getOrLoad(name, zoneBmp, "test", "DifferentBytesUnderTwoArchivesKeepTwoEntries",
                      /*applyPaletteMask=*/false, /*origin=*/nullptr,
                      /*sourceArchiveStem=*/"zone_s3d");
    irr::video::ITexture* zoneTex =
        cache_->uploadTexture(name, /*applyPaletteMask=*/false, "zone_s3d");
    ASSERT_NE(zoneTex, nullptr);

    cache_->getOrLoad(name, objBmp, "test", "DifferentBytesUnderTwoArchivesKeepTwoEntries",
                      /*applyPaletteMask=*/false, /*origin=*/nullptr,
                      /*sourceArchiveStem=*/"obj_s3d");
    irr::video::ITexture* objTex =
        cache_->uploadTexture(name, /*applyPaletteMask=*/false, "obj_s3d");
    ASSERT_NE(objTex, nullptr);

    // Two distinct GPU textures: the dedupe must NOT touch the willow pair.
    EXPECT_NE(zoneTex, objTex)
        << "different-content same-(name, mask) requests must keep two entries";
    EXPECT_EQ(cache_->getTextureCount(), 2u);
    EXPECT_EQ(cache_->getDedupedEntryCount(), 0u)
        << "a content-miss must not record an alias";

    // Each request keeps ITS OWN copy (the B1083 scoping the willow relies
    // on): the object's material must not be answered with the zone's
    // pixels or vice versa.
    EXPECT_EQ(cache_->getTexture(name, /*applyPaletteMask=*/false, "zone_s3d"), zoneTex);
    EXPECT_EQ(cache_->getTexture(name, /*applyPaletteMask=*/false, "obj_s3d"), objTex);
}

// An already-aliased request re-arriving (a second getOrLoad + upload for
// the same key, e.g. a re-draw asking again) must keep the same one-entry
// answer: the alias is stable, and the target's texture is what every
// re-arrival gets.
TEST_F(ConstrainedTextureCacheTest, AliasRearrivalKeepsTheTwinEntry) {
    const std::string name = "b1083_rearrival_test";
    const auto bmp = makeTestBmp(4, 4, 80, 80, 80);

    cache_->getOrLoad(name, bmp, "test", "AliasRearrivalKeepsTheTwinEntry",
                      /*applyPaletteMask=*/false, /*origin=*/nullptr,
                      /*sourceArchiveStem=*/"zone_s3d");
    irr::video::ITexture* zoneTex =
        cache_->uploadTexture(name, /*applyPaletteMask=*/false, "zone_s3d");
    ASSERT_NE(zoneTex, nullptr);

    cache_->getOrLoad(name, bmp, "test", "AliasRearrivalKeepsTheTwinEntry",
                      /*applyPaletteMask=*/false, /*origin=*/nullptr,
                      /*sourceArchiveStem=*/"obj_s3d");
    irr::video::ITexture* first =
        cache_->uploadTexture(name, /*applyPaletteMask=*/false, "obj_s3d");
    ASSERT_EQ(first, zoneTex);

    // Re-arrival for the ALIASED key: the alias (not a fresh fold, and not
    // a promotion) answers the same twin.
    cache_->getOrLoad(name, bmp, "test", "AliasRearrivalKeepsTheTwinEntry",
                      /*applyPaletteMask=*/false, /*origin=*/nullptr,
                      /*sourceArchiveStem=*/"obj_s3d");
    irr::video::ITexture* second =
        cache_->uploadTexture(name, /*applyPaletteMask=*/false, "obj_s3d");
    ASSERT_EQ(second, zoneTex)
        << "a re-arrival must keep answering the twin, not promote a second entry";
    EXPECT_EQ(cache_->getTextureCount(), 1u);
    EXPECT_EQ(cache_->getDedupedEntryCount(), 1u);
}

#else  // !EQT_HAS_GRAPHICS

TEST(ConstrainedTextureCacheTest, SkippedNoGraphics) {
    GTEST_SKIP() << "EQT_HAS_GRAPHICS not compiled in";
}

#endif
