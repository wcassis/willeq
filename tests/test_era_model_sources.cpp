// T504: the original client's character-model sources, era-pruned
// (include/client/graphics/eq/era_model_sources.h). Against the REAL client
// tree (data/eq_client) and the shipped data/era_zones.tsv; skips without them.
#include <gtest/gtest.h>

#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <string>

#include "client/graphics/eq/era_model_sources.h"

namespace {
bool fileExists(const std::string& p) { std::ifstream f(p); return f.good(); }
std::string clientPath() {
    const char* env = std::getenv("EQ_CLIENT_PATH");
    std::string p = env ? env : EQ_CLIENT_DATA_PATH;
    if (!p.empty() && p.back() != '/') p += '/';
    return p;
}
bool has(const std::vector<std::string>& v, const std::string& s) { return std::find(v.begin(), v.end(), s) != v.end(); }
}  // namespace

class EraModelSourcesTest : public ::testing::Test {
protected:
    std::string eq_;
    std::set<std::string> zones_;
    void SetUp() override {
        eq_ = clientPath();
        if (!fileExists(eq_ + "Resources/GlobalLoad.txt")) GTEST_SKIP() << "no client tree at " << eq_;
        zones_ = EQT::Graphics::LoadEraZones();
        if (zones_.empty()) GTEST_SKIP() << "no data/era_zones.tsv";
    }
};

TEST_F(EraModelSourcesTest, EraZoneListHasTheThreeExpansionsAndWillsExclusions) {
    EXPECT_EQ(zones_.size(), 123u);
    for (const char* z : {"qeynos2", "gfaydark", "dreadlands", "iceclad", "growthplane", "hole"}) EXPECT_TRUE(zones_.count(z)) << z;
    for (const char* z : {"jaggedpine", "nedaria", "shadowrest", "tutoriala", "tutorialb", "poair"}) EXPECT_FALSE(zones_.count(z)) << z;
}

TEST_F(EraModelSourcesTest, GlobalLoadKeepsTheEraArchivesAndDropsTheRest) {
    auto s = EQT::Graphics::LoadEraModelSources(eq_, "qeynos2", zones_);
    for (const char* a : {"global_chr.s3d", "global2_chr.s3d", "global3_chr.s3d", "global4_chr.s3d", "growthplane_chr.s3d"})
        EXPECT_TRUE(has(s.globalArchives, a)) << a;
    for (const char* a : {"globalfroglok_chr.s3d", "globalpcfroglok_chr.s3d", "globalske_chr2.s3d", "globaldrk_chr.s3d", "rap_chr.s3d", "skt_chr.s3d"})
        EXPECT_FALSE(has(s.globalArchives, a)) << a << " must be dropped (post-Velious)";
    EXPECT_FALSE(s.droppedGlobals.empty());
    // file order is the load order: Global4 precedes Global in this tree
    auto g4 = std::find(s.globalArchives.begin(), s.globalArchives.end(), "global4_chr.s3d");
    auto g1 = std::find(s.globalArchives.begin(), s.globalArchives.end(), "global_chr.s3d");
    EXPECT_LT(g4 - s.globalArchives.begin(), g1 - s.globalArchives.begin());
}

TEST_F(EraModelSourcesTest, ZoneListResolvesCaseInsensitivelyAndRecordsUnresolvedCodes) {
    auto s = EQT::Graphics::LoadEraModelSources(eq_, "qeynos2", zones_);
    EXPECT_EQ(s.zoneArchive, "qeynos2_chr.s3d");
    ASSERT_TRUE(s.zoneListPresent);
    ASSERT_EQ(s.zoneList.size(), 3u);   // cst, rtn, btn
    EXPECT_EQ(s.zoneList[0].code, "CST");
    EXPECT_EQ(s.zoneList[0].archive, "cst_chr.s3d");
    EXPECT_TRUE(s.zoneList[0].resolved);
    EXPECT_EQ(s.zoneList[1].code, "RTN");
    EXPECT_FALSE(s.zoneList[1].resolved) << "rtn names no archive in the tree: it comes from the globals";
}

TEST_F(EraModelSourcesTest, ZoneListPrunesArchivesOfZonesOutsideTheEra) {
    auto s = EQT::Graphics::LoadEraModelSources(eq_, "dreadlands", zones_);
    bool sawPoair = false;
    for (const auto& e : s.zoneList) if (e.archive == "poair_chr.s3d") { sawPoair = true; EXPECT_TRUE(e.pruned); }
    EXPECT_TRUE(sawPoair) << "dreadlands_chr.txt lists efe,poair_chr in this tree";
    const auto order = s.orderedArchives();
    EXPECT_FALSE(has(order, "poair_chr.s3d"));
    EXPECT_TRUE(has(order, "dreadlands_chr.s3d"));
    EXPECT_TRUE(has(order, "cst_chr.s3d"));
    EXPECT_EQ(order.front(), s.globalArchives.front()) << "globals first, in GlobalLoad order";
}

TEST_F(EraModelSourcesTest, AZoneWithoutAListHasOnlyGlobalsAndItsOwnArchive) {
    auto s = EQT::Graphics::LoadEraModelSources(eq_, "kael", zones_);
    EXPECT_FALSE(s.zoneListPresent);
    EXPECT_EQ(s.zoneArchive, "kael_chr.s3d");
    EXPECT_EQ(s.orderedArchives().size(), s.globalArchives.size() + 1);
}
