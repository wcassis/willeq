// B1068 (#388): walked zone-line crossings.
//
// Pins the three pieces whose failure left a walked crossing silent:
//  1. the 0x29 region-type string is the fragment BODY's string, and the
//     decoder reads the zone files' own DRNTP/WTNTP strings correctly
//     (samples copied from qeynos.wld / qeynos2.wld, printed by
//     scripts/tools/wld_zonelines.py);
//  2. a movement segment is SWEPT -- a 0.5-unit zone line between two
//     position samples 10 units apart is found, and a warp is not swept;
//  3. precedence: a zone whose BSP carries zone-line regions uses them, a
//     zone whose BSP carries none falls back to the curated boxes.
// The real-zone case at the bottom skips when the EQ client files are absent.

#include <gtest/gtest.h>

#include "client/zone_line_crossing.h"
#include "client/zone_lines.h"
#include "client/zone_bsp.h"
#include "client/graphics/eq/wld_loader.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <memory>
#include <string>
#include <sys/stat.h>
#include <unistd.h>

using EQT::ZoneLineCrossing;
using EQT::ZoneLineCrossingFinder;
using EQT::ZoneLines;
using EQT::Graphics::BspNode;
using EQT::Graphics::BspRegion;
using EQT::Graphics::BspTree;
using EQT::Graphics::RegionType;
using EQT::Graphics::WldLoader;
using EQT::Graphics::ZoneLineInfo;
using EQT::Graphics::ZoneLineType;

namespace {

// ── 1. the decoder, on the zone files' own strings ─────────────────────────
// parseFragment29 lowercases the string before decoding.

TEST(ZoneLineDecode, AbsoluteKeepZAndHeading_QeynosToQeynos2) {
    auto info = WldLoader::decodeZoneLineString("drntp00002-00155-00007999999999___000000000000");
    ASSERT_TRUE(info.has_value());
    EXPECT_EQ(info->type, ZoneLineType::Absolute);
    EXPECT_EQ(info->zoneId, 2);
    EXPECT_FLOAT_EQ(info->x, -155.0f);
    EXPECT_FLOAT_EQ(info->y, -7.0f);
    EXPECT_FLOAT_EQ(info->z, 999999.0f);
    EXPECT_FLOAT_EQ(info->heading, 999.0f);
}

TEST(ZoneLineDecode, AbsolutePositiveY_QeynosSecondGate) {
    auto info = WldLoader::decodeZoneLineString("drntp00002-00030000357999999999___000000000000");
    ASSERT_TRUE(info.has_value());
    EXPECT_EQ(info->type, ZoneLineType::Absolute);
    EXPECT_EQ(info->zoneId, 2);
    EXPECT_FLOAT_EQ(info->x, -30.0f);
    EXPECT_FLOAT_EQ(info->y, 357.0f);
}

TEST(ZoneLineDecode, AbsoluteToQeynosAndKeepY_Qeynos2) {
    auto toQeynos = WldLoader::decodeZoneLineString("drntp00001000464-00440999999999___000000000000");
    ASSERT_TRUE(toQeynos.has_value());
    EXPECT_EQ(toQeynos->type, ZoneLineType::Absolute);
    EXPECT_EQ(toQeynos->zoneId, 1);
    EXPECT_FLOAT_EQ(toQeynos->x, 464.0f);
    EXPECT_FLOAT_EQ(toQeynos->y, -440.0f);

    auto toHills = WldLoader::decodeZoneLineString("drntp00004-00350999999999999999___000000000000");
    ASSERT_TRUE(toHills.has_value());
    EXPECT_EQ(toHills->zoneId, 4);
    EXPECT_FLOAT_EQ(toHills->x, -350.0f);
    EXPECT_FLOAT_EQ(toHills->y, 999999.0f);
    EXPECT_FLOAT_EQ(toHills->z, 999999.0f);
}

TEST(ZoneLineDecode, ReferenceNamesTheZonePointNumber) {
    auto dr = WldLoader::decodeZoneLineString("drntp00255000033000000000000000___000000000000");
    ASSERT_TRUE(dr.has_value());
    EXPECT_EQ(dr->type, ZoneLineType::Reference);
    EXPECT_EQ(dr->zonePointIndex, 33u);

    auto wt = WldLoader::decodeZoneLineString("wtntp00255000099000000000000000___000000000000");
    ASSERT_TRUE(wt.has_value());
    EXPECT_EQ(wt->type, ZoneLineType::Reference);
    EXPECT_EQ(wt->zonePointIndex, 99u);
}

TEST(ZoneLineDecode, DocExampleString) {
    // docs/reference/wld_fragments.md 0x29: DRNTP00025-02698-645.6-00020999_ZONE
    auto info = WldLoader::decodeZoneLineString("drntp00025-02698-645.6-00020999_zone");
    ASSERT_TRUE(info.has_value());
    EXPECT_EQ(info->type, ZoneLineType::Absolute);
    EXPECT_EQ(info->zoneId, 25);
    EXPECT_FLOAT_EQ(info->x, -2698.0f);
    EXPECT_FLOAT_EQ(info->y, -645.0f);
    EXPECT_FLOAT_EQ(info->z, -20.0f);
    EXPECT_FLOAT_EQ(info->heading, 999.0f);
}

TEST(ZoneLineDecode, BodyStringIsXorDecoded) {
    static const uint8_t kKey[8] = {0x95, 0x3A, 0xC5, 0x2A, 0x95, 0x7A, 0x95, 0x6A};
    const std::string plain = "DRNTP00002-00155-00007999999999___000000000000";
    std::string body(4, '\0');
    const int32_t size = static_cast<int32_t>(plain.size() + 2);  // NUL padded
    std::memcpy(body.data(), &size, 4);
    std::string enc = plain + std::string(2, '\0');
    for (size_t i = 0; i < enc.size(); ++i) enc[i] = static_cast<char>(enc[i] ^ kKey[i % 8]);
    body += enc;
    EXPECT_EQ(WldLoader::regionTypeStringFromBody(body.data(), body.size()), plain);
    // Absent / truncated bodies give "", so the parser falls back to the name.
    EXPECT_EQ(WldLoader::regionTypeStringFromBody(body.data(), 3), "");
    EXPECT_EQ(WldLoader::regionTypeStringFromBody(body.data(), 10), "");
}

// ── 2. the sweep, over a hand-built tree ───────────────────────────────────
// Region 0 (regionId 1): ordinary. Region 1 (regionId 2): the slab
// 10 <= x < 10.5, a zone line. Plane test: dot = x*nx + d, >= 0 -> left.
std::shared_ptr<BspTree> slabTree(const ZoneLineInfo& info) {
    auto tree = std::make_shared<BspTree>();
    auto normal = std::make_shared<BspRegion>();
    normal->regionTypes = {RegionType::Normal};
    auto line = std::make_shared<BspRegion>();
    line->regionTypes = {RegionType::Zoneline};
    line->zoneLineInfo = info;
    tree->addRegion(normal);
    tree->addRegion(line);
    BspNode n0; n0.normalX = 1.0f; n0.splitDistance = -10.0f; n0.left = 1; n0.right = 2;
    BspNode n1; n1.normalX = 1.0f; n1.splitDistance = -10.5f; n1.left = 3; n1.right = 4;
    BspNode leafNormal; leafNormal.regionId = 1;
    BspNode leafLine; leafLine.regionId = 2;
    tree->nodes = {n0, n1, leafNormal, leafNormal, leafLine};
    return tree;
}

ZoneLineInfo absoluteTo(uint16_t zone, float x, float y, float z) {
    ZoneLineInfo i;
    i.type = ZoneLineType::Absolute;
    i.zoneId = zone; i.x = x; i.y = y; i.z = z; i.heading = 999.0f;
    return i;
}

TEST(ZoneLineSweep, ThinRegionBetweenTwoSamplesIsFound) {
    auto tree = slabTree(absoluteTo(2, -155.0f, -7.0f, 999999.0f));
    const size_t n = ZoneLineCrossingFinder::CountBspZoneLineRegions(*tree);
    ASSERT_EQ(n, 1u);
    ZoneLines none;
    // The renderer's ~250 ms samples: x = 5 then x = 15. Neither is inside.
    auto pointOnly = ZoneLineCrossingFinder::FindCrossing(tree.get(), n, &none, 5, 0, 3, 15, 0, 3, 0, false);
    EXPECT_FALSE(pointOnly.has_value()) << "the end point alone steps over the line (the B1068 miss)";
    auto swept = ZoneLineCrossingFinder::FindCrossing(tree.get(), n, &none, 5, 0, 3, 15, 0, 3, 0, true);
    ASSERT_TRUE(swept.has_value());
    EXPECT_EQ(swept->source, ZoneLineCrossing::Source::Bsp);
    EXPECT_EQ(swept->regionIndex, 1u);
    EXPECT_GE(swept->hitX, 10.0f);
    EXPECT_LT(swept->hitX, 10.5f);
    EXPECT_EQ(swept->result.targetZoneId, 2);
    EXPECT_FLOAT_EQ(swept->result.targetX, -155.0f);
    EXPECT_FLOAT_EQ(swept->result.targetY, -7.0f);
    EXPECT_FLOAT_EQ(swept->result.targetZ, 3.0f) << "999999 keeps the player's own z";
}

TEST(ZoneLineSweep, AWarpIsNotSwept) {
    auto tree = slabTree(absoluteTo(2, 0, 0, 0));
    ZoneLines none;
    auto r = ZoneLineCrossingFinder::FindCrossing(tree.get(), 1, &none, 0, 0, 3,
                                          ZoneLineCrossingFinder::kTeleportDistance + 20.0f, 0, 3, 0, true);
    EXPECT_FALSE(r.has_value());
}

TEST(ZoneLineSweep, StandingInsideIsFoundWithoutMovementHistory) {
    auto tree = slabTree(absoluteTo(2, 0, 0, 0));
    ZoneLines none;
    auto r = ZoneLineCrossingFinder::FindCrossing(tree.get(), 1, &none, 10.2f, 0, 3, 10.2f, 0, 3, 0, false);
    ASSERT_TRUE(r.has_value());
}

// ── Reference resolution through the server's zone points ──────────────────

TEST(ZoneLineResolve, ReferenceUsesTheServerRowInClientOrder) {
    ZoneLines lines;
    EQT::ZonePoint zp;
    zp.number = 33;
    zp.targetX = -5.0f;    // server x (OP_SendZonepoints, stored as-is)
    zp.targetY = -151.0f;  // server y
    zp.targetZ = 1.5f;
    zp.targetZoneId = 2;
    lines.setServerZonePoints({zp});
    ZoneLineInfo ref;
    ref.type = ZoneLineType::Reference;
    ref.zonePointIndex = 33;
    ZoneLineCrossing c = ZoneLineCrossingFinder::ResolveBsp(ref, 7, &lines, 1, 2, 3, 0);
    EXPECT_TRUE(c.isReference);
    EXPECT_EQ(c.zonePointNumber, 33u);
    EXPECT_FALSE(c.result.needsServerLookup);
    EXPECT_EQ(c.result.targetZoneId, 2);
    EXPECT_FLOAT_EQ(c.result.targetX, -151.0f) << "client x = server y";
    EXPECT_FLOAT_EQ(c.result.targetY, -5.0f);
}

TEST(ZoneLineResolve, ReferenceWithoutAServerRowLetsTheServerDecide) {
    ZoneLines lines;
    ZoneLineInfo ref;
    ref.type = ZoneLineType::Reference;
    ref.zonePointIndex = 4;
    ZoneLineCrossing c = ZoneLineCrossingFinder::ResolveBsp(ref, 0, &lines, 1, 2, 3, 0);
    EXPECT_TRUE(c.result.needsServerLookup);
    EXPECT_EQ(c.result.targetZoneId, 0) << "zone 0 = eqemu picks the closest zone point";
}

TEST(ZoneLineResolve, ServerKeepMarkerKeepsThePlayersCoordinate) {
    ZoneLines lines;
    EQT::ZonePoint zp;
    zp.number = 9;
    zp.targetX = 999999.0f;  // qeynos2 #9 -> qeytoqrg: target_x 999999
    zp.targetY = -220.0f;
    zp.targetZ = 4.0f;
    zp.targetZoneId = 4;
    lines.setServerZonePoints({zp});
    ZoneLineInfo ref;
    ref.type = ZoneLineType::Reference;
    ref.zonePointIndex = 9;
    ZoneLineCrossing c = ZoneLineCrossingFinder::ResolveBsp(ref, 0, &lines, 1700, 40, 3, 0);
    EXPECT_FLOAT_EQ(c.result.targetX, -220.0f);
    EXPECT_FLOAT_EQ(c.result.targetY, 40.0f) << "server x 999999 keeps client y";
}

// ── 3. precedence ──────────────────────────────────────────────────────────

std::string writeCuratedBox(const std::string& dir) {
    // A curated box far from the BSP slab: x 100..101.
    const std::string path = dir + "/qeynos2.json";
    std::ofstream f(path);
    f << R"({"zone":"qeynos2","zone_lines":[{"target_zone":"qeynos","landing":"fixed",)"
      << R"("bounds":{"min_x":100,"max_x":101,"min_y":-50,"max_y":50,"min_z":-50,"max_z":50}}]})";
    return path;
}

class ZoneLinePrecedence : public ::testing::Test {
protected:
    void SetUp() override {
        char tmpl[] = "/tmp/b1068_zlXXXXXX";
        ASSERT_NE(mkdtemp(tmpl), nullptr);
        dir_ = tmpl;
        ASSERT_TRUE(lines_.loadFromZoneSpecificJson("qeynos2", writeCuratedBox(dir_)));
    }
    void TearDown() override {
        std::remove((dir_ + "/qeynos2.json").c_str());
        rmdir(dir_.c_str());
    }
    std::string dir_;
    ZoneLines lines_;
};

TEST_F(ZoneLinePrecedence, BspPresentMeansBspUsed) {
    auto tree = slabTree(absoluteTo(1, 464, -440, 999999));
    const size_t n = ZoneLineCrossingFinder::CountBspZoneLineRegions(*tree);
    auto viaBsp = ZoneLineCrossingFinder::FindCrossing(tree.get(), n, &lines_, 5, 0, 3, 15, 0, 3, 0, true);
    ASSERT_TRUE(viaBsp.has_value());
    EXPECT_EQ(viaBsp->source, ZoneLineCrossing::Source::Bsp);
    // The curated box is NOT consulted while the BSP has zone lines.
    auto curatedSpot = ZoneLineCrossingFinder::FindCrossing(tree.get(), n, &lines_, 95, 0, 3, 105, 0, 3, 0, true);
    EXPECT_FALSE(curatedSpot.has_value());
}

TEST_F(ZoneLinePrecedence, NoBspZoneLinesMeansCuratedUsedAndSwept) {
    auto tree = slabTree(absoluteTo(1, 0, 0, 0));
    tree->regions[1]->regionTypes = {RegionType::Normal};
    tree->regions[1]->zoneLineInfo.reset();
    ASSERT_EQ(ZoneLineCrossingFinder::CountBspZoneLineRegions(*tree), 0u);
    auto r = ZoneLineCrossingFinder::FindCrossing(tree.get(), 0, &lines_, 95, 0, 3, 105, 0, 3, 0, true);
    ASSERT_TRUE(r.has_value());
    EXPECT_EQ(r->source, ZoneLineCrossing::Source::Curated);
    EXPECT_EQ(r->result.targetZoneId, 1);
    EXPECT_FALSE(ZoneLineCrossingFinder::FindCrossing(tree.get(), 0, &lines_, 95, 0, 3, 105, 0, 3, 0, false).has_value())
        << "the end point alone misses the 1-unit box";
}

// ── real zone data (skips without the EQ client files) ─────────────────────

std::string eqClientPath() {
    const char* env = std::getenv("EQ_CLIENT_PATH");
    return env ? env : "/mnt/projects/EverQuestP1999";
}

bool fileExists(const std::string& p) {
    struct stat st;
    return stat(p.c_str(), &st) == 0;
}

struct RegionCensus { size_t zoneLines = 0; size_t water = 0; };

RegionCensus census(const std::string& zone) {
    RegionCensus c;
    WldLoader loader;
    if (!loader.parseFromArchive(eqClientPath() + "/" + zone + ".s3d", zone + ".wld")) return c;
    auto tree = loader.getBspTree();
    if (!tree) return c;
    c.zoneLines = ZoneLineCrossingFinder::CountBspZoneLineRegions(*tree);
    // Where the lines are (client coords): review/B1068/README.md quotes this.
    const EQT::Graphics::BspBounds world(-6000, -6000, -2000, 6000, 6000, 2000);
    for (size_t i = 0; i < tree->regions.size(); ++i) {
        const auto& r = tree->regions[i];
        if (!r->zoneLineInfo) continue;
        auto b = tree->computeRegionBounds(i, world);
        const auto& zi = *r->zoneLineInfo;
        std::cout << "  " << zone << " region " << i
                  << (zi.type == ZoneLineType::Absolute ? " abs zone=" : " ref zp=")
                  << (zi.type == ZoneLineType::Absolute ? zi.zoneId : zi.zonePointIndex)
                  << " bounds x[" << b.minX << "," << b.maxX << "] y[" << b.minY << "," << b.maxY
                  << "] z[" << b.minZ << "," << b.maxZ << "]\n";
    }
    for (const auto& r : tree->regions) {
        for (auto t : r->regionTypes) {
            if (t == RegionType::Water) { ++c.water; break; }
        }
    }
    return c;
}

TEST(ZoneLineRealZones, QeynosPairCarryTheirBspZoneLines) {
    if (!fileExists(eqClientPath() + "/qeynos.s3d") || !fileExists(eqClientPath() + "/qeynos2.s3d")) {
        GTEST_SKIP() << "EQ client files not found at " << eqClientPath();
    }
    // Region counts per scripts/tools/wld_zonelines.py (review/B1068/README.md):
    // qeynos 6 zone-line 0x29 fragments over 8 regions, qeynos2 29 over 123.
    // Before B1068 the parser read the fragment NAME (Z####_ZONE) and typed
    // every 0x29 region a zone line (qeynos: all 144 fragments) with no water.
    RegionCensus q = census("qeynos");
    EXPECT_EQ(q.zoneLines, 8u);
    EXPECT_GT(q.water, 0u) << "WTN__ bodies are water, not zone lines";
    RegionCensus q2 = census("qeynos2");
    EXPECT_EQ(q2.zoneLines, 123u);
    EXPECT_GT(q2.water, 0u);
}

} // namespace
