/**
 * B1083 Step 7 (#403, ruling 2026-10-17 option A): the driver-facing name of
 * a ConstrainedTextureCache entry is decided ONCE per entry (family-first)
 * and stored -- an evict->restore re-creates the entry under the SAME name.
 *
 * The defect this pins: B1083's (archive, name, mask) keying made the S3D
 * upload path hand the driver the mangled internalKey (name\x01@<stem>\x01),
 * where the base name-keyed run handed name. The name is textureGroupId()'s
 * FNV input -> materialKey -> the per-pose draw-order sort, so every
 * single-archive gate zone re-sorted its draw order (197 px on qeynos2 vs
 * the committed golden, all five gate zones red --
 * refactor/B1083/gate_ledger_diff.md). The fix: the FIRST stored member of a
 * (name, mask) family keeps the pre-B1083 name; a later, genuinely different
 * cross-archive twin (the qeytoqrg tree70 willow) takes the full internalKey,
 * so the driver's name-dedup cannot fold the two images.
 *
 * RED BEFORE the fix: this file compiles on the pre-fix tree too -- only
 * the assertions guarded by B1083_STEP7_DRIVER_NAME_STORED read the new
 * post-fix state (the stored driverName field). There, the public-API
 * assertion in SingleArchiveEntryKeepsThePreB1083Name FAILS (the driver name
 * carries the \x01@<stem>\x01 suffix) -- that is the red. GREEN AFTER:
 * every test passes, stored-name included.
 *
 * Part 1 (EDT_NULL, like ConstrainedTextureCacheTest): the family-first name
 * decision, single-archive and cross-archive, plus the P46d distinctness
 * invariant (mask suffix always distinct) and store-order stability.
 * Part 2 (real EDT_OGLES2 surfaceless device, like
 * test_texture_eviction_residency): evict -> bind -> restore round-trip --
 * the restored object keeps its name (the ruling's "decide ONCE and store").
 * Self-skips without EGL.
 */

#include <gtest/gtest.h>
#include <string>
#include <vector>

#ifdef EQT_HAS_GRAPHICS
#include "client/graphics/constrained_texture_cache.h"
#include "client/graphics/constrained_renderer_config.h"
#include "common/logging.h"
#include <irrlicht.h>

// The post-fix-only assertions read the stored name on the entry (private).
// Defined for THIS tree by tests/cmake/renderer_guards.cmake; absent on the
// pre-fix tree, where those assertions compile out and the red is the
// public-API one (the texture's own driver name).
#ifdef B1083_STEP7_DRIVER_NAME_STORED
#define private public
#define protected public
#include "client/graphics/constrained_texture_cache.h"
#undef private
#undef protected
#endif

namespace {

// Mirrors ConstrainedTextureCache::cacheKey() (private) -- the documented
// format, name + [\x01@<stem>\x01] + [\x01masked].
std::string keyForTest(const std::string& name, const std::string& stem, bool masked) {
    std::string key = name;
    if (!stem.empty()) key += "\x01@" + stem + "\x01";
    if (masked) key += "\x01masked";
    return key;
}

// Builds a minimal valid 24bpp BMP (uncompressed, BITMAPINFOHEADER) so
// decodeBMP() succeeds -- the same device as makeTestBmp() in
// test_constrained_texture_cache.cpp (different fixture, no link).
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

std::string nameOf(const irr::video::ITexture* tex) {
    return std::string(tex ? tex->getName().getPath().c_str() : "");
}

class DriverNameFixture : public ::testing::Test {
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

    // Store + promote one decoded entry and return its (texture, key).
    irr::video::ITexture* promote(const std::string& name, const std::vector<char>& bmp,
                                  const std::string& stem) {
        cache_->getOrLoad(name, bmp, "test", "B1083-Step7",
                          /*applyPaletteMask=*/false, /*origin=*/nullptr, stem);
        return cache_->uploadTexture(name, /*applyPaletteMask=*/false, stem);
    }

    const ConstrainedTextureCache::CachedTexture* entry(const std::string& key) const {
        auto it = cache_->cache_.find(key);
        return it == cache_->cache_.end() ? nullptr : &it->second;
    }
};

}  // namespace

// ---------------------------------------------------------------------------
// Part 1: the family-first name decision (EDT_NULL)
// ---------------------------------------------------------------------------

// The defect's own shape: a SINGLE archive (the five gate zones). The entry
// is family-first, so the driver sees the pre-B1083 name (the plain name --
// the unscoped key is the identity key) and the group-ID feed for the draw
// sort is byte-identical to base. Pre-fix, the driver name carried the
// \x01@<stem>\x01 suffix and every group ID (hence the whole sorted draw
// order) moved -- the 197 px regression.
TEST_F(DriverNameFixture, SingleArchiveEntryKeepsThePreB1083Name) {
    const std::string name = "b1083_step7_single";
    const auto bmp = makeTestBmp(4, 4, 10, 20, 30);

    irr::video::ITexture* tex = promote(name, bmp, /*stem=*/"qeynos2");
    ASSERT_NE(tex, nullptr) << "the single-archive entry must upload";

    // The driver-facing name is the pre-B1083 plain name, not internalKey.
    EXPECT_EQ(nameOf(tex), name);
    // ...and it is STORED on the entry (decided once, not re-derived).
    const auto* e = entry(keyForTest(name, "qeynos2", /*masked=*/false));
    ASSERT_NE(e, nullptr);
    EXPECT_EQ(e->driverName, name);

    // The cache-map key is still the full (archive, name, mask) key -- the
    // B1083 keying ruling is untouched by the fix.
    EXPECT_EQ(e->texture, tex);
}

// The willow (qeytoqrg tree70): the SAME name, DIFFERENT bytes, two
// archives. The first member (loaded first, configured order) is
// family-first and keeps the base plain name; the second member takes the
// full internalKey (stem included), so the driver's findTexture name-dedup
// cannot fold the two images and both copies keep their own pixels. Pre-fix
// BOTH members already carried the stem-suffixed name (the second one's
// name was right, the first one's was wrong); the test pins the split.
TEST_F(DriverNameFixture, CrossArchiveTwinTakesTheFullKey) {
    const std::string name = "b1083_step7_twin";
    const auto zoneBmp = makeTestBmp(4, 4, 200, 10, 10);
    const auto objBmp = makeTestBmp(4, 4, 10, 10, 200);

    irr::video::ITexture* zoneTex = promote(name, zoneBmp, "qeytoqrg");
    ASSERT_NE(zoneTex, nullptr);
    irr::video::ITexture* objTex = promote(name, objBmp, "qeytoqrg_obj");
    ASSERT_NE(objTex, nullptr);

    EXPECT_NE(zoneTex, objTex) << "different bytes keep two entries (the willow fix)";
    EXPECT_EQ(nameOf(zoneTex), name) << "the family-first member keeps base's plain name";
    EXPECT_EQ(nameOf(objTex), keyForTest(name, "qeytoqrg_obj", /*masked=*/false))
        << "the second member takes the full internalKey (no driver fold)";

    const auto* zoneEntry = entry(keyForTest(name, "qeytoqrg", false));
    const auto* objEntry = entry(keyForTest(name, "qeytoqrg_obj", false));
    ASSERT_NE(zoneEntry, nullptr);
    ASSERT_NE(objEntry, nullptr);
    EXPECT_EQ(zoneEntry->driverName, name);
    EXPECT_EQ(objEntry->driverName, keyForTest(name, "qeytoqrg_obj", false));

    // Each request keeps ITS OWN pixels (the scoping the willow relies on).
    EXPECT_EQ(cache_->getTexture(name, false, "qeytoqrg"), zoneTex);
    EXPECT_EQ(cache_->getTexture(name, false, "qeytoqrg_obj"), objTex);
}

// The P46d distinctness invariant: the masked variant of a name never
// shares a driver name with the unmasked one (pre-fix this held by the
// \x01masked suffix on internalKey; post-fix it holds by the family-first
// decision -- mask is a family axis, both variants family-first in an
// empty cache, names `name` and `name\x01masked`).
TEST_F(DriverNameFixture, MaskedAndUnmaskedKeepDistinctDriverNames) {
    const std::string name = "b1083_step7_mask";
    const auto bmpA = makeTestBmp(4, 4, 1, 2, 3);
    const auto bmpB = makeTestBmp(4, 4, 4, 5, 6);

    cache_->getOrLoad(name, bmpA, "test", "B1083-Step7", /*applyPaletteMask=*/false,
                      /*origin=*/nullptr, /*sourceArchiveStem=*/"arch");
    irr::video::ITexture* plain = cache_->uploadTexture(name, false, "arch");
    ASSERT_NE(plain, nullptr);

    cache_->getOrLoad(name, bmpB, "test", "B1083-Step7", /*applyPaletteMask=*/true,
                      /*origin=*/nullptr, /*sourceArchiveStem=*/"arch");
    irr::video::ITexture* masked = cache_->uploadTexture(name, true, "arch");
    ASSERT_NE(masked, nullptr);

    EXPECT_NE(plain, masked) << "masked and unmasked variants are distinct entries";
    EXPECT_NE(nameOf(plain), nameOf(masked));
    EXPECT_EQ(nameOf(plain), name) << "unmasked family-first: the plain name";
    EXPECT_EQ(nameOf(masked), name + "\x01masked") << "masked family-first: the P46d name";

    const auto* maskedEntry = entry(keyForTest(name, "arch", /*masked=*/true));
    ASSERT_NE(maskedEntry, nullptr);
    EXPECT_EQ(maskedEntry->driverName, name + "\x01masked");
}

// A PENDING sibling (decoded, not yet promoted) already counts: the zone
// load promotes in decode order, so whichever of the two stored first is
// family-first -- and the second must take the full key from its own
// promote onward, whatever the actual promotion order turns out to be.
TEST_F(DriverNameFixture, PendingSiblingDecidesTheFamilyOrder) {
    const std::string name = "b1083_step7_pending";
    const auto bmpA = makeTestBmp(4, 4, 1, 1, 1);
    const auto bmpB = makeTestBmp(4, 4, 2, 2, 2);

    // Store both, promote the SECOND one first (a non-load-order schedule).
    cache_->getOrLoad(name, bmpA, "test", "B1083-Step7", false, nullptr, "arch_a");
    cache_->getOrLoad(name, bmpB, "test", "B1083-Step7", false, nullptr, "arch_b");

    irr::video::ITexture* second = cache_->uploadTexture(name, false, "arch_b");
    ASSERT_NE(second, nullptr);
    irr::video::ITexture* first = cache_->uploadTexture(name, false, "arch_a");
    ASSERT_NE(first, nullptr);

    // arch_a stored first: it is the family's first member and must have
    // the plain name even though it promoted second -- the name is decided
    // per STORE, family-first, not per promote-time arrival.
    EXPECT_EQ(nameOf(first), name);
    EXPECT_EQ(nameOf(second), keyForTest(name, "arch_b", false));
}

// ---------------------------------------------------------------------------
// Part 2: evict -> restore keeps the name (real GT, no)
// ---------------------------------------------------------------------------

// The ruling's additional requirement: the driver name is decided ONCE per
// entry and stored -- an evict->restore re-creates the entry under the SAME
// name. B944's restore keeps the ITexture OBJECT (the storage is released
// and re-adopted into it), so the name (and every holder of the pointer) is
// unchanged by construction; this test exercises the real round-trip on the
// shipped driver and pins it. Self-skips without EGL.
TEST_F(DriverNameFixture, EvictRestoreRecreatesUnderTheSameName) {
    // Swap the EDT_NULL device for a real surfaceless one (the restore
    // callback path is GLES2-only) and build a cache on it.
    cache_.reset();
    device_->drop();
    irr::SIrrlichtCreationParameters params;
    params.DriverType = irr::video::EDT_OGLES2;
    params.WindowSize = irr::core::dimension2d<irr::u32>(64, 64);
    device_ = irr::createDeviceEx(params);
    if (!device_) {
        GTEST_SKIP() << "EDT_OGLES2 (surfaceless EGL) unavailable";
    }
    driver_ = device_->getVideoDriver();

    constexpr int kSide = 32;
    const std::string name = "b1083_step7_evict";
    const auto bmp = makeTestBmp(kSide, kSide, 10, 250, 10);
    const size_t kBytes = size_t(kSide) * kSide * 4;

    // The restore source (a registered provider, like a zone's .s3d).
    class Provider : public TextureSourceProvider {
    public:
        const std::string kName = "b1083_step7_evict";
        std::vector<char> bytes;
        bool hasSource(const std::string& n) override { return n == kName; }
        bool readSource(const std::string& n, std::vector<char>& out) override {
            if (n != kName) return false;
            out = bytes;
            return true;
        }
    } source;
    source.bytes = bmp;

    // A ceiling that fits ONE texture (the residency test's per-texture
    // figure): the second upload evicts the first.
    const auto config = ConstrainedRendererConfig::fromPreset(ConstrainedRenderingPreset::TNT);
    config.textureMemoryBytes = kBytes;
    config.enableMipmaps = false;
    config.maxTextureDimension = 256;
    cache_ = std::make_unique<ConstrainedTextureCache>(config);
    cache_->setDriver(driver_);
    cache_->addSourceProvider(&source);

    irr::video::ITexture* tex = promote(name, bmp, /*stem=*/"");
    ASSERT_NE(tex, nullptr);
    const std::string originalName = nameOf(tex);
    ASSERT_EQ(originalName, name) << "family-first: the plain name before eviction";
    irr::video::SMaterial mat;  // a live consumer the cache does not know
    mat.setTexture(0, tex);

    // Evict it (the second upload busts the one-texture ceiling).
    const auto filler = makeTestBmp(kSide, kSide, 250, 10, 10);
    irr::video::ITexture* fillerTex = promote("b1083_step7_filler", filler, "");
    ASSERT_NE(fillerTex, nullptr);
    ASSERT_FALSE(cache_->isResident(name)) << "the first entry must have been evicted";

    // Bind through the driver: the restore re-creates the storage.
    driver_->beginScene(true, true, irr::video::SColor(255, 0, 0, 0));
    driver_->draw2DImage(tex, irr::core::rect<irr::s32>(0, 0, 64, 64),
                         irr::core::rect<irr::s32>(0, 0, kSide, kSide), nullptr, nullptr, false);
    driver_->endScene();

    ASSERT_TRUE(cache_->isResident(name)) << "the bind must restore the evicted entry";
    EXPECT_EQ(nameOf(tex), originalName)
        << "the restored entry re-creates under the SAME stored driver name";
    // The object (and the material's pointer to it) is unchanged: B944's
    // adopt-into-the-same-object restore is what makes "stored once" work.
    EXPECT_EQ(mat.getTexture(0), tex);
    EXPECT_EQ(cache_->getRestoreFailureCount(), 0u);

    cache_->removeSourceProvider(&source);
}

#else  // !EQT_HAS_GRAPHICS

TEST(DriverNameFixture, SkippedNoGraphics) {
    GTEST_SKIP() << "EQT_HAS_GRAPHICS not compiled in";
}

#endif
