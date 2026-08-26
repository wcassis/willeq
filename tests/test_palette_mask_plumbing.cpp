/**
 * T285: pins the two applyPaletteMask plumbing sites P46d Part B flagged and
 * P50 fixed:
 *
 *   Gap 1  mesh_building.cpp's buildMeshFromGeometry() on-demand texture
 *          fallback (getOrLoadTexture()/loadTextureFromBMP()) — a masked
 *          per-part texture resolved by a cache that has never seen it must
 *          still decode with the EQ palette-index-0 punch.
 *   Gap 2  animated_mesh_creation.cpp's per-slot equipment texture-variant
 *          swap (materialId != 0) — the swapped variant must inherit the
 *          slot's masked status instead of decoding opaque.
 *
 * Both sites were unpinned: the fix was verified once by a hand-run tool
 * (tools/hwcaps/p50_mask_plumbing_probe.cpp) and nothing would have caught a
 * silent regression. This is that pin. Deleting either `isMaskedMaterial`
 * argument turns these tests red.
 *
 * Ground truth is P37's: race 60 (SKE) carries WLD-masked forearm/shin/chest
 * textures. Offline — shipped data/willeq.db as the race source plus the real
 * client tree (data/eq_client -> EverQuestP1999); skips if either is absent.
 * EDT_NULL Irrlicht device, no server, no DISPLAY.
 */

#include <gtest/gtest.h>

#include <algorithm>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

#include "client/willeq_database.h"

#ifdef EQT_HAS_GRAPHICS
#include "client/graphics/constrained_renderer_config.h"
#include "client/graphics/constrained_texture_cache.h"
#include "client/graphics/entity_renderer.h"  // EntityAppearance
#include "client/graphics/eq/race_model_loader.h"
#include "client/graphics/eq/zone_geometry.h"
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

std::string toLower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return std::tolower(c); });
    return s;
}

constexpr uint16_t kMaskedRace = 60;  // SKE — P37 ground truth
constexpr uint8_t kGender = 0;

}  // namespace

#ifdef EQT_HAS_GRAPHICS

using namespace EQT::Graphics;

class PaletteMaskPlumbingTest : public ::testing::Test {
protected:
    EQT::WilleqDatabase db_;
    irr::IrrlichtDevice* device_ = nullptr;

    // Loader A: the production preload (T05's own masked decode). Source of
    // the WLD ground truth, and Gap 2's subject — its shared cached mesh
    // buffers carry the MaterialType that the equipment swap reads back.
    std::unique_ptr<ConstrainedTextureCache> cacheA_;
    std::unique_ptr<RaceModelLoader> loaderA_;

    void SetUp() override {
        const std::string dbPath = WILLEQ_DB_REAL_PATH;
        if (!fileExists(dbPath)) GTEST_SKIP() << "no shipped db at " << dbPath;
        const std::string eq = clientPath();
        if (!fileExists(eq + "global_chr.s3d")) GTEST_SKIP() << "EQ client tree not at " << eq;
        ASSERT_TRUE(db_.open(dbPath)) << dbPath;
        EQT::WilleqDatabase::setGlobalInstance(&db_);

        irr::SIrrlichtCreationParameters params;
        params.DriverType = irr::video::EDT_NULL;
        params.WindowSize = irr::core::dimension2d<irr::u32>(64, 64);
        device_ = irr::createDeviceEx(params);
        if (!device_) GTEST_SKIP() << "no EDT_NULL device";

        loaderA_ = makeLoader(cacheA_);
        loaderA_->loadGlobalModels();
        loaderA_->loadNumberedGlobalModels();
        if (!loaderA_->preloadModelData(kMaskedRace, kGender)) {
            GTEST_SKIP() << "race " << kMaskedRace << " did not preload from " << eq;
        }
        loaderA_->promotePreparedModels();
        cacheA_->uploadAllPending();
    }

    void TearDown() override {
        // Deliberate: RaceModelLoader/ConstrainedTextureCache pairs sharing
        // one Irrlicht device segfault on destruction ordering (same reason
        // p50_mask_plumbing_probe.cpp and zone_bench_loader.cpp skip their
        // own teardown). One short-lived test process; let the OS reclaim.
        (void)loaderA_.release();
        (void)cacheA_.release();
        EQT::WilleqDatabase::setGlobalInstance(nullptr);
        device_ = nullptr;
    }

    std::unique_ptr<RaceModelLoader> makeLoader(std::unique_ptr<ConstrainedTextureCache>& cacheOut) {
        auto config = ConstrainedRendererConfig::fromPreset(ConstrainedRenderingPreset::TNT);
        cacheOut = std::make_unique<ConstrainedTextureCache>(config);
        cacheOut->setDriver(device_->getVideoDriver());
        auto loader = std::make_unique<RaceModelLoader>(
            device_->getSceneManager(), device_->getVideoDriver(), device_->getFileSystem());
        loader->setClientPath(clientPath());
        loader->setConstrainedTextureCache(cacheOut.get());
        if (auto* mb = loader->getMeshBuilder()) {
            mb->setConstrainedTextureCache(cacheOut.get());
            cacheOut->setMeshBuilder(mb);
        }
        return loader;
    }

    // texIdx -> triangle count in the race's raw geometry. A masked texIdx
    // with zero triangles is an orphan entry in the shared male/female
    // texture table: nothing ever loads it, so it proves nothing either way.
    std::vector<size_t> triangleCounts(const std::shared_ptr<RaceModelData>& md) const {
        std::vector<size_t> counts(md->rawGeometry->textureNames().size(), 0);
        for (const auto& tri : md->rawGeometry->triangles) {
            if (tri.textureIndex < counts.size()) ++counts[tri.textureIndex];
        }
        return counts;
    }
};

// Gap 1. A freshly-built loader/cache pair that has never seen these textures
// resolves them through buildMeshFromGeometry()'s fallback — the exact state
// P13b's per-part deferral leaves a texture in. Every masked texture that is
// actually referenced by geometry must come out of that fallback carrying
// mask alpha.
TEST_F(PaletteMaskPlumbingTest, OnDemandFallbackDecodesMaskedTexturesMasked) {
    auto ground = loaderA_->getRaceModelData(kMaskedRace, kGender);
    ASSERT_NE(ground, nullptr);
    ASSERT_NE(ground->rawGeometry, nullptr);

    std::unique_ptr<ConstrainedTextureCache> cacheB;
    auto loaderB = makeLoader(cacheB);
    ASSERT_NE(loaderB->createAnimatedNode(kMaskedRace, kGender, nullptr, -1), nullptr);

    // ensureModelDataLoaded() never sets compositeData, so the composite
    // branch is skipped and the fallback is what resolved these textures.
    auto fresh = loaderB->getRaceModelData(kMaskedRace, kGender);
    ASSERT_NE(fresh, nullptr);
    EXPECT_EQ(fresh->compositeData, nullptr) << "composite hit invalidates this scenario";

    const auto& names = ground->rawGeometry->textureNames();
    const auto& masked = ground->rawGeometry->textureMasked();
    const auto counts = triangleCounts(ground);

    int checked = 0;
    for (size_t i = 0; i < names.size(); ++i) {
        if (i >= masked.size() || !masked[i] || counts[i] == 0) continue;
        const std::string name = toLower(names[i]);
        ++checked;
        EXPECT_TRUE(cacheB->hasAlpha(name, /*applyPaletteMask=*/true))
            << "masked texture '" << name << "' (texIdx " << i << ", " << counts[i]
            << " triangles) decoded opaque through the on-demand fallback";
    }
    EXPECT_GT(checked, 0) << "race " << kMaskedRace << " exposed no referenced masked texture";

    (void)loaderB.release();
    (void)cacheB.release();
}

// The same fallback must leave UNMASKED textures exactly as before: the flag
// is the WLD's, not a blanket "mask everything on this path".
TEST_F(PaletteMaskPlumbingTest, OnDemandFallbackLeavesUnmaskedTexturesOpaque) {
    auto ground = loaderA_->getRaceModelData(kMaskedRace, kGender);
    ASSERT_NE(ground, nullptr);

    std::unique_ptr<ConstrainedTextureCache> cacheB;
    auto loaderB = makeLoader(cacheB);
    ASSERT_NE(loaderB->createAnimatedNode(kMaskedRace, kGender, nullptr, -1), nullptr);

    const auto& names = ground->rawGeometry->textureNames();
    const auto& masked = ground->rawGeometry->textureMasked();

    int checked = 0;
    for (size_t i = 0; i < names.size(); ++i) {
        if (i < masked.size() && masked[i]) continue;
        const std::string name = toLower(names[i]);
        if (!cacheB->getTexture(name)) continue;  // not resolved through this path
        ++checked;
        EXPECT_FALSE(cacheB->hasAlpha(name)) << "unmasked texture '" << name << "' gained mask alpha";
    }
    EXPECT_GT(checked, 0) << "no unmasked texture was resolved through the fallback";

    (void)loaderB.release();
    (void)cacheB.release();
}

// Gap 2. An equipped, non-default material on a WLD-masked slot swaps the
// buffer's texture for a variant; that variant must be decoded masked too.
// Slot 3 (Wrist/forearms) is one of SKE's masked slots (P37).
TEST_F(PaletteMaskPlumbingTest, EquipmentVariantSwapKeepsTheMask) {
    EntityAppearance appearance;
    appearance.equipment[3] = 1;  // Wrist — non-default variant
    auto* node = loaderA_->createAnimatedNodeWithEquipment(kMaskedRace, kGender, appearance,
                                                          nullptr, -1, /*compositeAllowed=*/true);
    ASSERT_NE(node, nullptr);

    // ConstrainedTextureCache uploads under its internal (name, mask) key and
    // Irrlicht registers the GPU texture under that same string, so a masked
    // variant's texture name IS the mangled key — no guessing which buffer.
    int variants = 0;
    for (irr::u32 m = 0; m < node->getMaterialCount(); ++m) {
        auto* tex = node->getMaterial(m).getTexture(0);
        if (!tex) continue;
        const std::string texName = tex->getName().getPath().c_str();
        // {race}{part}{variant:02d}{page}.bmp — variant digits at 5..6.
        const bool isVariant01 = texName.size() > 6 && texName.substr(5, 2) == "01";
        if (!isVariant01) continue;
        ++variants;
        const std::string mangledSuffix = std::string("\x01") + "masked";
        const size_t sep = texName.find(mangledSuffix);
        ASSERT_NE(sep, std::string::npos)
            << "equipped variant '" << texName << "' was not uploaded as a masked variant";
        const std::string realName = texName.substr(0, sep);
        EXPECT_TRUE(cacheA_->hasAlpha(realName, /*applyPaletteMask=*/true))
            << "equipped variant '" << realName << "' decoded opaque";
    }
    EXPECT_EQ(variants, 1) << "expected exactly one swapped variant buffer on the node";
}

#endif  // EQT_HAS_GRAPHICS
