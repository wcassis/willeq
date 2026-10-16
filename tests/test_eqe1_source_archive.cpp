// B1083 (GitLab #403): two EQE1 packs of one zone family hold the same
// texture name with DIFFERENT content, and the name-keyed cache collapsed
// them into one entry.
//
// The measured collision (scripts/refactor/eqe1_family_census.py: 361
// names across 172 families in data/eqe1_client, table in
// refactor/B1083/README.md; qeytoqrg below): qeytoqrg.s3d
// holds tree70 opaque (no alpha companion) and qeytoqrg_obj.s3d holds tree70
// WITH its alpha companion. The willow in qeytoqrg drew its foliage as a
// solid rectangle because the OBJECT's material asked the cache for "tree70"
// by name only, and findCompressedEntry() scanned the OPEN packs in
// configured order -- {zone} first, {zone}_obj second -- so the zone pack's
// opaque copy won the lookup for the object too. The cache key (name) had
// no axis to distinguish the two copies, so the second request found the
// first pack's entry and returned it.
//
// The fix keys the cache on (sourceArchiveStem, name, applyPaletteMask) —
// cacheKey(x, "", false) == x keeps every unscoped call site byte-identical —
// and findCompressedEntry() tries the request's own pack FIRST before the
// configured-order scan. This test drives both halves against two synthetic
// packs that mirror the measured collision:
//
//   zone_eqe1.eqe1 : "tree70" opaque ETC1
//   obj_eqe1.eqe1  : "tree70" with its "tree70_alpha" companion
//
// RED on origin/fable: getOrLoad()/uploadTexture()/eqe1ServedByStem() do not
// take (and this test does not compile against) an archive stem, and the
// resolution is name-only, so the object's request is served by the zone
// pack no matter what. GREEN here: the object's request is served by obj_eqe1
// and the zone's by zone_eqe1, from two distinct (archive, name) entries;
// the unscoped request keeps today's configured-order answer; and a request
// naming a pack that holds no copy of the name falls through to the scan.

#include "client/graphics/constrained_texture_cache.h"
#include "client/graphics/constrained_renderer_config.h"
#include "client/graphics/eqe1_loader.h"
#include "client/graphics/eqe1_format.h"

#include <gtest/gtest.h>

#include <cctype>
#include <cstring>
#include <cstdio>
#include <fstream>
#include <string>
#include <vector>
#include <sys/stat.h>
#include <sys/types.h>

#ifdef EQT_HAS_GRAPHICS
#include <irrlicht.h>
#endif

using namespace EQT::Graphics;

namespace {

constexpr uint8_t kFillZone = 0x5A;  // zone pack payload fill (opaque)
constexpr uint8_t kFillObj = 0xA5;   // object pack payload fill (alpha)

// Writes a v1 pack holding `name` (plus its `name`_alpha` companion when
// hasAlpha) with the given fill byte in every payload. Mirrors the shape of
// a real builder output: one 48-byte index entry per texture, entries
// 16-byte aligned, ETC1 payload of the exact 4x4/3-mip chain size.
bool writeSyntheticPack(const std::string& path, const std::string& name,
                        bool hasAlpha, uint8_t fill) {
    const std::vector<std::pair<std::string, bool>> entries = hasAlpha
        ? std::vector<std::pair<std::string, bool>>{{name, true}, {name + "_alpha", false}}
        : std::vector<std::pair<std::string, bool>>{{name, false}};

    const uint32_t szEtc1 = etc1MipChainSize(4, 4, 3);
    const uint32_t indexEnd = static_cast<uint32_t>(sizeof(EQE1Header) +
        static_cast<size_t>(entries.size()) * EQE1_INDEX_ENTRY_SIZE);
    const uint32_t dataOffset = alignTo16(indexEnd);

    std::ofstream out(path, std::ios::binary);
    if (!out.is_open()) return false;

    EQE1Header hdr = {};
    hdr.magic = EQE1_MAGIC;
    hdr.version = EQE1_VERSION;
    hdr.numEntries = static_cast<uint16_t>(entries.size());
    out.write(reinterpret_cast<const char*>(&hdr), sizeof(hdr));

    for (const auto& [entryName, entryAlpha] : entries) {
        EQE1IndexEntry e = {};
        std::strncpy(e.name, entryName.c_str(), EQE1_NAME_SIZE - 1);
        e.width = 4;
        e.height = 4;
        e.mipLevels = 3;
        e.hasAlpha = entryAlpha ? 1 : 0;
        e.entryType = EQE1_TYPE_ETC1_RGB;
        e.dataOffset = dataOffset;
        e.dataSize = szEtc1;
        out.write(reinterpret_cast<const char*>(&e), sizeof(e));
    }

    const uint32_t totalData = szEtc1 * static_cast<uint32_t>(entries.size());
    std::vector<char> payload(dataOffset + totalData - indexEnd, static_cast<char>(fill));
    out.write(payload.data(), static_cast<std::streamsize>(payload.size()));
    return static_cast<bool>(out);
}

bool fileExists(const char* path) {
    std::ifstream f(path);
    return f.good();
}

#ifdef EQT_HAS_GRAPHICS

// The device must be surfaceless EGL: uploadCompressedETC1() needs a live GL
// context (glGenTextures/glCompressedTexImage2D), which the EDT_NULL device
// used by the P50 tests in test_constrained_texture_cache.cpp does not
// provide. Same idiom as that file's ConstrainedTextureCacheETC1Test.
class Eqe1SourceArchiveTest : public ::testing::Test {
protected:
    irr::IrrlichtDevice* device_ = nullptr;
    irr::video::IVideoDriver* driver_ = nullptr;
    std::unique_ptr<ConstrainedTextureCache> cache_;
    EQE1Loader zoneLoader_;
    EQE1Loader objLoader_;
    std::string zonePath_;
    std::string objPath_;

    void SetUp() override {
        // The packs go in a subdirectory so their base names are exactly
        // zone_eqe1 / obj_eqe1 — the stems the fix keys on. (Naming the file
        // test_..._obj_eqe1.eqe1 flat in TempDir() would make the stem the
        // whole filename, which is a different question than this fix asks.)
        const std::string dir = (std::string(testing::TempDir()) + "b1083_packs");
        ::mkdir(dir.c_str(), 0755);
        zonePath_ = dir + "/zone_eqe1.eqe1";
        objPath_ = dir + "/obj_eqe1.eqe1";
        if (!writeSyntheticPack(zonePath_, "tree70", /*hasAlpha=*/false, kFillZone) ||
            !writeSyntheticPack(objPath_, "tree70", /*hasAlpha=*/true, kFillObj)) {
            GTEST_SKIP() << "could not write synthetic packs under " << testing::TempDir();
        }
        if (!zoneLoader_.open(zonePath_) || !objLoader_.open(objPath_)) {
            GTEST_SKIP() << "synthetic pack did not open";
        }

        irr::SIrrlichtCreationParameters params;
        params.DriverType = irr::video::EDT_OGLES2;
        params.WindowSize = irr::core::dimension2d<irr::u32>(64, 64);
        device_ = irr::createDeviceEx(params);
        if (!device_) {
            GTEST_SKIP() << "Failed to create EDT_OGLES2 device (surfaceless EGL "
                            "unavailable in this environment) — skipping.";
        }
        driver_ = device_->getVideoDriver();

        auto config = ConstrainedRendererConfig::fromPreset(ConstrainedRenderingPreset::TNT);
        cache_ = std::make_unique<ConstrainedTextureCache>(config);
        cache_->setDriver(driver_);
        // The configured order is {zone} first, {zone}_obj second — the
        // order that hid the defect: the zone pack wins a name-only scan.
        cache_->addEQE1Loader(&zoneLoader_);
        cache_->addEQE1Loader(&objLoader_);
    }

    void TearDown() override {
        cache_.reset();
        std::remove(zonePath_.c_str());
        std::remove(objPath_.c_str());
        std::remove((std::string(testing::TempDir()) + "b1083_packs").c_str());
        if (device_) {
            device_->drop();
            device_ = nullptr;
        }
    }
};

// The defect itself, in miniature. The zone's request (its own pack first)
// and the object's request (its own pack first) resolve to DISTINCT
// (archive, name) entries: the object keeps its alpha copy, the zone keeps
// its opaque one. On origin/fable the object's request has no stem to name
// and the scan hands it the zone pack's copy — the willow's solid rectangle.
TEST_F(Eqe1SourceArchiveTest, ObjectKeepsItsOwnCopyWhenNamesCollide) {
    std::vector<char> emptyData;  // the EQE1 pack supplies the bytes

    // The zone region asks for tree70 from its own pack (zone_eqe1).
    cache_->getOrLoad("tree70", emptyData, "test", "ObjectKeepsItsOwnCopyWhenNamesCollide",
                      /*applyPaletteMask=*/false, /*origin=*/nullptr,
                      /*sourceArchiveStem=*/"zone_eqe1");
    irr::video::ITexture* zoneTex =
        cache_->uploadTexture("tree70", /*applyPaletteMask=*/false, "zone_eqe1");
    ASSERT_NE(zoneTex, nullptr) << "the zone's own copy must upload";

    // The object WLD asks for the SAME name from its own pack (obj_eqe1).
    // Before B1083 this is a plain name request that the open-order scan
    // answers with the zone pack's entry.
    cache_->getOrLoad("tree70", emptyData, "test", "ObjectKeepsItsOwnCopyWhenNamesCollide",
                      /*applyPaletteMask=*/false, /*origin=*/nullptr,
                      /*sourceArchiveStem=*/"obj_eqe1");
    irr::video::ITexture* objTex =
        cache_->uploadTexture("tree70", /*applyPaletteMask=*/false, "obj_eqe1");
    ASSERT_NE(objTex, nullptr) << "the object's own copy must upload";

    // Two distinct GPU textures: the entries no longer collapse.
    EXPECT_NE(zoneTex, objTex)
        << "same name from two packs must be two (archive, name) entries";

    // The zone's entry: ETC1, opaque — hasAlpha false, no companion.
    EXPECT_TRUE(cache_->isETC1Texture("tree70", /*applyPaletteMask=*/false, "zone_eqe1"));
    EXPECT_FALSE(cache_->hasAlpha("tree70", /*applyPaletteMask=*/false, "zone_eqe1"))
        << "the zone pack's tree70 has no alpha companion";
    EXPECT_EQ(cache_->getAlphaTexture("tree70", /*applyPaletteMask=*/false, "zone_eqe1"),
              nullptr);

    // The object's entry: ETC1 WITH its companion — the willow's foliage
    // keeps the alpha that the name-only lookup used to discard.
    EXPECT_TRUE(cache_->isETC1Texture("tree70", /*applyPaletteMask=*/false, "obj_eqe1"));
    EXPECT_TRUE(cache_->hasAlpha("tree70", /*applyPaletteMask=*/false, "obj_eqe1"))
        << "the _obj pack's tree70 ships with an alpha companion";
    EXPECT_NE(cache_->getAlphaTexture("tree70", /*applyPaletteMask=*/false, "obj_eqe1"),
              nullptr)
        << "the object material must bind the _obj pack's companion";

    // The EQE1 request log says which pack served each name ("first
    // resolution wins" per name): the ZONE's request was answered by
    // zone_eqe1 — the pack it named, tried first, not the scan's order.
    EXPECT_EQ(cache_->eqe1ServedByStem("tree70"), "zone_eqe1");
}

// An unscoped request is EXACTLY today's behaviour: the configured-order
// scan of the open packs, {zone} first. Nothing in this fix may change what
// a caller that names no source gets — every character-model, equipment,
// composite and UI call site relies on that identity.
TEST_F(Eqe1SourceArchiveTest, UnscopedRequestKeepsConfiguredOrder) {
    std::vector<char> emptyData;
    cache_->getOrLoad("tree70", emptyData, "test", "UnscopedRequestKeepsConfiguredOrder",
                      /*applyPaletteMask=*/false, /*origin=*/nullptr,
                      /*sourceArchiveStem=*/"");
    irr::video::ITexture* tex =
        cache_->uploadTexture("tree70", /*applyPaletteMask=*/false, "");
    ASSERT_NE(tex, nullptr);
    // The zone pack is opened first and holds the name: the scan serves it,
    // exactly as the pre-B1083 open-order scan did.
    EXPECT_EQ(cache_->eqe1ServedByStem("tree70"), "zone_eqe1");
}

// A request may name a pack that does NOT hold the copy it asks for (a WLD
// referencing a texture its sibling archive carries, or a renamed pack).
// Source-first means "try mine first", not "only mine": the miss falls
// through to the configured-order scan and the name still resolves.
TEST_F(Eqe1SourceArchiveTest, SourceMissFallsThroughToTheScan) {
    std::vector<char> emptyData;
    // "no_such_pack" holds no tree70; the scan must still find it in
    // zone_eqe1.
    cache_->getOrLoad("tree70", emptyData, "test", "SourceMissFallsThroughToTheScan",
                      /*applyPaletteMask=*/false, /*origin=*/nullptr,
                      /*sourceArchiveStem=*/"no_such_pack");
    irr::video::ITexture* tex =
        cache_->uploadTexture("tree70", /*applyPaletteMask=*/false, "no_such_pack");
    ASSERT_NE(tex, nullptr)
        << "a source pack that holds no copy must not make the name unresolvable";
    EXPECT_EQ(cache_->eqe1ServedByStem("tree70"), "zone_eqe1");
}

// The stem a loader passes is a real PATH (eqe1ArchiveStem normalizes it:
// lowercase, .eqe1 stripped, directory dropped). TextureInfo::archiveStem()
// hands down a bare stem, but the request may also carry the full path the
// S3D loader stamped (sourceArchive) — both must match the open loader.
TEST_F(Eqe1SourceArchiveTest, FullPathSourceStemNormalizesLikeTheLoaderPath) {
    std::vector<char> emptyData;
    cache_->getOrLoad("tree70", emptyData, "test", "FullPathSourceStemNormalizesLikeTheLoaderPath",
                      /*applyPaletteMask=*/false, /*origin=*/nullptr,
                      /*sourceArchiveStem=*/objPath_);  // e.g. /tmp/.../obj_eqe1.eqe1
    irr::video::ITexture* tex =
        cache_->uploadTexture("tree70", /*applyPaletteMask=*/false, objPath_);
    ASSERT_NE(tex, nullptr);
    EXPECT_EQ(cache_->eqe1ServedByStem("tree70"), "obj_eqe1")
        << "a full-path stem must normalize to the open loader's stem";
    EXPECT_TRUE(cache_->hasAlpha("tree70", /*applyPaletteMask=*/false, objPath_))
        << "the same normalization must apply to the (archive, name) queries";
}

#endif  // EQT_HAS_GRAPHICS
}  // namespace
