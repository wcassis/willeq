/**
 * T452 (board B468) -- RaceModelLoader::getArmorTexture() resolves the
 * Kunark/Velious armor tier.
 *
 * Before this unit the lookup was
 *     SELECT archive, ... FROM s3d_assets WHERE name=?
 * against a table the shipped data/willeq.db has never contained: `s3d_assets`
 * is created only by `willeq_db_builder --s3d-index`, and no script, CMake rule
 * or test has ever passed that flag. The query therefore failed at prepare()
 * and every call returned nullptr -- from a MISSING TABLE, not from a miss --
 * from the day the route was written (2026-03-21, ca267770).
 *
 * That was not harmless. Character body textures are named
 * {race}{part}{variant:02d}{page:02d}.bmp, and the variant partitions the
 * archives: 00-04 are in global_chr.s3d (merged into globalTextures_), the robe
 * range 10-16 is rewritten to clk<nn><page>.bmp which is also in
 * global_chr.s3d, and 17-23 live ONLY in global17_amr.s3d .. global23_amr.s3d
 * -- which nothing else in the client opens. So mesh_building.cpp's
 * isTextureAvailable() asked for e.g. hufch1701.bmp, got nullptr, and declined
 * the variant override: the whole high-tier armor set rendered as the base
 * skin.
 *
 * These tests drive the real shipped archives through the loader's own public
 * seam (setClientPath() + getArmorTexture()). Offline: no server, no DISPLAY,
 * EDT_NULL Irrlicht device. Skips when the EQ client tree is absent.
 *
 * PRE-FIX these fail with a null texture; POST-FIX they return the archive's
 * bytes. The pure-derivation cases below are new code and have no pre-fix
 * counterpart -- the general net that would have caught B468 six months ago is
 * WilleqDatabaseShippedContent.EveryTableTheClientQueriesExists in
 * tests/test_willeq_database.cpp, which fails on the old tree.
 */

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <utility>
#include <memory>
#include <string>
#include <vector>

#include "client/willeq_database.h"

#ifdef EQT_HAS_GRAPHICS
#include "client/graphics/constrained_renderer_config.h"
#include "client/graphics/constrained_texture_cache.h"
#include "client/graphics/eq/race_model_loader.h"
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

}  // namespace

#ifdef EQT_HAS_GRAPHICS

using EQT::Graphics::ArmorArchivesForTextureName;
using EQT::Graphics::RaceModelLoader;

// ---------------------------------------------------------------------------
// The derivation, with no archives and no loader.
// ---------------------------------------------------------------------------

TEST(ArmorArchiveDerivation, VariantSeventeenPrefersItsOwnArchive) {
    const auto archives = ArmorArchivesForTextureName("hufch1701.bmp");
    ASSERT_EQ(archives.size(), 7u) << "all seven armor archives must remain reachable";
    EXPECT_EQ(archives.front(), "global17_amr.s3d");
}

TEST(ArmorArchiveDerivation, EveryTierResolvesToItsOwnArchiveFirst) {
    for (int v = 17; v <= 23; ++v) {
        char name[32];
        snprintf(name, sizeof(name), "humch%02d01.bmp", v);
        const auto archives = ArmorArchivesForTextureName(name);
        ASSERT_FALSE(archives.empty()) << name;
        EXPECT_EQ(archives.front(), "global" + std::to_string(v) + "_amr.s3d") << name;
    }
}

TEST(ArmorArchiveDerivation, TheOtherSixAreStillProbed) {
    // haft2202.bmp is the ONE measured exception to "archive N holds variant N"
    // -- it lives in global20_amr.s3d, not global22_amr.s3d. A strict
    // derivation would miss exactly this file, which is why the fallback list
    // exists at all. If this ever becomes a one-element list, that texture
    // silently stops resolving.
    const auto archives = ArmorArchivesForTextureName("haft2202.bmp");
    ASSERT_EQ(archives.size(), 7u);
    EXPECT_EQ(archives.front(), "global22_amr.s3d");
    EXPECT_NE(std::find(archives.begin(), archives.end(), "global20_amr.s3d"), archives.end());
}

TEST(ArmorArchiveDerivation, NonArmorNamesOpenNothing) {
    // The common case, and the reason this is cheaper than the SQL query it
    // replaces: a name that is not a 17-23 armor texture must not cause a
    // single archive to be opened.
    for (const char* name : {
             "humch0001.bmp",   // base tier, already in global_chr.s3d
             "humch0401.bmp",   // top of the global_chr range
             "clk0401.bmp",     // a robe page
             "humch1601.bmp",   // the robe range, never an _amr archive
             "humch2401.bmp",   // past the last tier
             "10008head.dds",   // an equipment texture; digits, but not a variant
             "palette.bmp",     // really in the armor archives, but not addressable
             "x.bmp",           // too short to carry {variant}{page}
             "hufch1701",       // no extension
         }) {
        EXPECT_TRUE(ArmorArchivesForTextureName(name).empty()) << name;
    }
}

// ---------------------------------------------------------------------------
// The real archives, through the loader.
// ---------------------------------------------------------------------------

class ArmorTextureArchiveTest : public ::testing::Test {
protected:
    irr::IrrlichtDevice* device_ = nullptr;
    std::unique_ptr<EQT::Graphics::ConstrainedTextureCache> textureCache_;
    std::unique_ptr<RaceModelLoader> loader_;

    void SetUp() override {
        const std::string eq = clientPath();
        for (int v = 17; v <= 23; ++v) {
            const std::string a = eq + "global" + std::to_string(v) + "_amr.s3d";
            if (!fileExists(a)) GTEST_SKIP() << "EQ client tree not at " << eq << " (missing " << a << ")";
        }

        irr::SIrrlichtCreationParameters params;
        params.DriverType = irr::video::EDT_NULL;
        params.WindowSize = irr::core::dimension2d<irr::u32>(64, 64);
        device_ = irr::createDeviceEx(params);
        if (!device_) GTEST_SKIP() << "no EDT_NULL device";

        loader_ = std::make_unique<RaceModelLoader>(
            device_->getSceneManager(), device_->getVideoDriver(), device_->getFileSystem());
        auto config = EQT::Graphics::ConstrainedRendererConfig::fromPreset(
            EQT::Graphics::ConstrainedRenderingPreset::TNT);
        textureCache_ = std::make_unique<EQT::Graphics::ConstrainedTextureCache>(config);
        textureCache_->setDriver(device_->getVideoDriver());
        loader_->setConstrainedTextureCache(textureCache_.get());
        loader_->setClientPath(clientPath());
    }

    void TearDown() override {
        loader_.reset();
        textureCache_.reset();
        if (device_) device_->drop();
    }
};

TEST_F(ArmorTextureArchiveTest, HighTierArmorTextureLoads) {
    // The exact call mesh_building.cpp's isTextureAvailable() makes for a
    // human female wearing tier-17 armor. PRE-FIX: nullptr, because
    // getAssetLocation() SELECTed from a table that is not in the database.
    auto tex = loader_->getArmorTexture("hufch1701.bmp");
    ASSERT_NE(tex, nullptr) << "hufch1701.bmp is in global17_amr.s3d and must resolve";
    EXPECT_FALSE(tex->data.empty());
    EXPECT_EQ(tex->name, "hufch1701.bmp");
}

TEST_F(ArmorTextureArchiveTest, EveryTierLoadsAtLeastOneTexture) {
    // One measured name per archive, so a single archive silently dropping out
    // of the probe list is a red test.
    const std::pair<const char*, int> kOnePerArchive[] = {
        {"hufch1701.bmp", 17}, {"hufch1801.bmp", 18}, {"hufch1901.bmp", 19},
        {"hufch2001.bmp", 20}, {"hufch2101.bmp", 21}, {"hufch2201.bmp", 22},
        {"humch2301.bmp", 23},
    };
    for (const auto& [name, tier] : kOnePerArchive) {
        auto tex = loader_->getArmorTexture(name);
        ASSERT_NE(tex, nullptr) << name << " (tier " << tier << ")";
        EXPECT_FALSE(tex->data.empty()) << name;
    }
}

TEST_F(ArmorTextureArchiveTest, TheMisplacedTextureIsFoundInTheWrongArchive) {
    // haft2202.bmp is in global20_amr.s3d. Only the fallback probe finds it.
    auto tex = loader_->getArmorTexture("haft2202.bmp");
    ASSERT_NE(tex, nullptr) << "haft2202.bmp lives in global20_amr.s3d, not global22_amr.s3d";
    EXPECT_FALSE(tex->data.empty());
}

TEST_F(ArmorTextureArchiveTest, AMissIsRememberedAndNotReprobed) {
    // A 17-23 name no archive has costs seven archive opens the first time and
    // must cost none afterwards; without the negative cache every mesh build
    // re-probes ~21 MB of S3D for a texture that does not exist.
    const char* absent = "zzzch1701.bmp";
    EXPECT_EQ(loader_->getArmorTexture(absent), nullptr);
    EXPECT_EQ(loader_->getArmorTexture(absent), nullptr);
}

TEST_F(ArmorTextureArchiveTest, BaseTierIsNotServedFromAnArmorArchive) {
    // The base tier belongs to globalTextures_, not here. Answering for it
    // would mask a real gap in the global_chr load.
    EXPECT_EQ(loader_->getArmorTexture("hufch0001.bmp"), nullptr);
}

#endif  // EQT_HAS_GRAPHICS
