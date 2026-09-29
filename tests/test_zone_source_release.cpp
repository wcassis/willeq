// B914 (2026-09-29): the gate's benchmark releases the zone source the client
// releases, through the client's own function.
//
// The client ends every zone load with loadZoneStep13_Cleanup(), which frees
// the raw texture pixels (orangepi preset), the zone's character models, the
// WLD loader's post-load tables and the per-region source geometry (T504).
// The CI gate measures tools/hwcaps/zone_bench_loader.cpp instead, which
// never did any of that, so its zone_parse ledger category and
// s3dZoneSourceBytes charged it with ~20 MB (iceclad, timorous) the client
// does not hold. Both now call S3DZone::releaseSourceAfterLoad().
//
// These tests pin:
//   * the shared function really drops every region's source geometry and
//     keeps what the frames still read (the BSP tree, the zone's lights);
//   * the texture-pixel half follows the preset flag;
//   * BOTH loaders call it, neither keeps a private copy of the release, and
//     the benchmark calls it after its last source-geometry read (a source
//     census: remove the benchmark's call and this goes red).

#include "client/graphics/eq/s3d_loader.h"
#include "client/graphics/eq/wld_loader.h"

#include "asset_requirement.h"

#include <gtest/gtest.h>

#include <fstream>
#include <sstream>
#include <string>

using EQT::Graphics::S3DLoader;
using EQT::Graphics::S3DLoadOptions;

namespace {

std::string readSource(const std::string& rel) {
    std::ifstream f(std::string(WILLEQ_REPO_ROOT) + "/" + rel);
    std::stringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

// The benchmark's own load options (zone_bench_loader.cpp, Step 2).
S3DLoadOptions benchOptions() {
    S3DLoadOptions o;
    o.loadObjects = false;
    o.loadCharacters = false;
    o.computeCombinedGeometry = false;
    return o;
}

class ZoneSourceRelease : public ::testing::Test {
protected:
    std::string archive_;
    void SetUp() override {
        const char* env = std::getenv("EQ_CLIENT_PATH");
        const std::string root = env ? env : EQ_CLIENT_DATA_PATH;
        eqt::test::requireEqClientAssets(root);
        archive_ = root + "/befallen.s3d";  // small, and one of the gate's zones
        std::ifstream probe(archive_);
        if (!probe.good()) GTEST_SKIP() << "no " << archive_;
    }
};

}  // namespace

TEST_F(ZoneSourceRelease, DropsEveryRegionsSourceGeometryAndKeepsTheBsp) {
    S3DLoader loader;
    ASSERT_TRUE(loader.loadZone(archive_, benchOptions())) << loader.getError();
    auto zone = loader.getZone();
    ASSERT_TRUE(zone && zone->wldLoader);
    auto bsp = zone->wldLoader->getBspTree();
    ASSERT_TRUE(bsp && !bsp->regions.empty());
    const size_t regions = bsp->regions.size();
    const size_t lights = zone->lights.size();

    size_t withGeometry = 0;
    for (size_t i = 0; i < regions; ++i) {
        if (zone->wldLoader->getGeometryForRegion(i)) ++withGeometry;
    }
    ASSERT_GT(withGeometry, 0u);
    const size_t before = zone->getMemoryUsage();

    const auto rel = zone->releaseSourceAfterLoad(/*releaseTexturePixels=*/true);

    EXPECT_TRUE(rel.wldReleased);
    EXPECT_TRUE(rel.texturePixelsReleased);
    EXPECT_GT(rel.regionGeometryBytes, 0u);
    for (size_t i = 0; i < regions; ++i) {
        EXPECT_EQ(zone->wldLoader->getGeometryForRegion(i), nullptr) << "region " << i;
    }
    EXPECT_TRUE(zone->wldLoader->getGeometries().empty());
    EXPECT_LT(zone->getMemoryUsage(), before);
    // What the benchmark's frames and the client's PVS still read survives.
    ASSERT_TRUE(zone->wldLoader->getBspTree());
    EXPECT_EQ(zone->wldLoader->getBspTree()->regions.size(), regions);
    EXPECT_EQ(zone->lights.size(), lights);
}

TEST_F(ZoneSourceRelease, TexturePixelsFollowThePresetFlag) {
    S3DLoader loader;
    ASSERT_TRUE(loader.loadZone(archive_, benchOptions())) << loader.getError();
    auto zone = loader.getZone();
    ASSERT_TRUE(zone);
    const auto rel = zone->releaseSourceAfterLoad(/*releaseTexturePixels=*/false);
    EXPECT_FALSE(rel.texturePixelsReleased);
    EXPECT_EQ(rel.texturePixelBytes, 0u);
    EXPECT_TRUE(rel.wldReleased);  // the geometry half is not preset-gated
}

TEST(ZoneSourceReleaseCensus, BothZoneLoadersCallTheSharedRelease) {
    for (const char* rel : {"src/client/graphics/zone_load/renderer_zone_load_env.cpp",
                            "tools/hwcaps/zone_bench_loader.cpp"}) {
        const std::string src = readSource(rel);
        ASSERT_FALSE(src.empty()) << rel;
        EXPECT_NE(src.find("->releaseSourceAfterLoad("), std::string::npos) << rel;
        // No second copy of the release: its parts are called only from
        // S3DZone::releaseSourceAfterLoad (src/client/graphics/eq/s3d_loader.cpp).
        for (const char* part : {"releaseRegionGeometry(", "releasePostLoadData(",
                                 "releaseTexturePixelData(", "clearCharacterData("}) {
            EXPECT_EQ(src.find(part), std::string::npos) << rel << " calls " << part;
        }
    }
}

TEST(ZoneSourceReleaseCensus, TheBenchmarkReleasesAfterItsLastSourceRead) {
    const std::string src = readSource("tools/hwcaps/zone_bench_loader.cpp");
    const size_t call = src.find("->releaseSourceAfterLoad(");
    ASSERT_NE(call, std::string::npos);
    const size_t lastRead = src.rfind("getGeometryForRegion(");
    ASSERT_NE(lastRead, std::string::npos);
    EXPECT_LT(lastRead, call) << "the benchmark reads region source geometry after releasing it";
    const size_t complete = src.find("ledgerPoint(\"zone_load_complete\")");
    ASSERT_NE(complete, std::string::npos);
    EXPECT_LT(call, complete) << "the release belongs before the zone_load_complete point, as in the client";
}
