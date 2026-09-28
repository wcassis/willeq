// Spatial budget overrun (B813 follow-up, 2026-09-28).
//
// The orangepi preset's `spatial` ledger budget is 4 MiB. The CI gate measured
// iceclad at 8,680,304 B and timorous at 11,601,824 B, and 99.9 % of both was
// the software-occlusion occluder table: one unordered_map node plus one heap
// vector per BSP region (iceclad: 17,783 regions, 35,568 blocks, 8.67 MB for
// 65,880 triangles = 2.64 MB of payload), with the benchmark's copy of the
// extraction loop also keeping every vector's pre-truncation capacity.
//
// These tests pin the fix so it cannot silently return:
//   * RegionOccluderTable holds the zone in three flat arrays, and after
//     shrinkToFit() reserves EXACTLY 40 B per triangle + 8 B per region + 4 B;
//   * SelectRegionOccluders() -- the one selection both loaders call -- keeps
//     the maxPerRegion largest triangles and returns capacity == size;
//   * both loaders really do call it (a source census), so the gate's
//     benchmark cannot drift from the client again.

#include "client/graphics/region_occluders.h"
#include "client/graphics/software_occlusion_culler.h"

#include <gtest/gtest.h>

#include <fstream>
#include <sstream>
#include <string>
#include <vector>

using EQT::Graphics::OccluderSpan;
using EQT::Graphics::OccluderTriangle;
using EQT::Graphics::RegionOccluderTable;
using EQT::Graphics::SelectRegionOccluders;
using EQT::Graphics::SoftwareOcclusionCuller;

namespace {

// Minimal stand-in for ZoneGeometry: the template needs only these members.
struct V { float x, y, z; };
struct T { uint32_t v1, v2, v3; };
struct FakeGeom {
    std::vector<V> vertices;
    std::vector<T> triangles;
    float centerX = 0, centerY = 0, centerZ = 0;
};

// A right triangle in the z=0 plane with legs `leg`: area leg*leg/2.
void addRightTriangle(FakeGeom& g, float ox, float leg) {
    const uint32_t base = static_cast<uint32_t>(g.vertices.size());
    g.vertices.push_back({ox, 0, 0});
    g.vertices.push_back({ox + leg, 0, 0});
    g.vertices.push_back({ox, leg, 0});
    g.triangles.push_back({base, base + 1, base + 2});
}

std::vector<OccluderTriangle> makeSet(int n, float tag) {
    std::vector<OccluderTriangle> v(static_cast<size_t>(n));
    for (int i = 0; i < n; ++i) {
        v[static_cast<size_t>(i)] = OccluderTriangle{{tag, float(i), 0}, {0, 0, 0}, {0, 0, 0}, tag + i};
    }
    return v;
}

std::string readSource(const std::string& rel) {
    std::ifstream f(std::string(WILLEQ_REPO_ROOT) + "/" + rel);
    std::stringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

}  // namespace

// ---- SelectRegionOccluders --------------------------------------------------

TEST(RegionOccluders, SelectKeepsTheLargestAndIsRightSized) {
    FakeGeom g;
    g.centerX = 100.0f;
    // 20 qualifying triangles of growing area (leg 7..26 -> area 24.5..338),
    // plus two below minArea and one with an out-of-range index.
    for (int i = 0; i < 20; ++i) addRightTriangle(g, float(i) * 50.0f, 7.0f + float(i));
    addRightTriangle(g, 5000.0f, 1.0f);   // area 0.5  -> dropped
    addRightTriangle(g, 6000.0f, 6.0f);   // area 18   -> dropped (< 20)
    g.triangles.push_back({0, 1, 999});   // malformed -> skipped

    const auto occ = SelectRegionOccluders(g, 20.0f, 12);
    ASSERT_EQ(occ.size(), 12u);
    EXPECT_EQ(occ.capacity(), occ.size()) << "the per-region vector must not keep its growth slack";
    // Largest first: legs 26..15.
    for (size_t k = 0; k < occ.size(); ++k) {
        const float leg = 26.0f - float(k);
        EXPECT_FLOAT_EQ(occ[k].area, 0.5f * leg * leg);
    }
    // Region centre applied (world space).
    EXPECT_FLOAT_EQ(occ[0].v0[0], 100.0f + 19.0f * 50.0f);
}

TEST(RegionOccluders, SelectUnderTheCapKeepsSourceOrder) {
    FakeGeom g;
    addRightTriangle(g, 0.0f, 10.0f);   // 50
    addRightTriangle(g, 50.0f, 30.0f);  // 450
    addRightTriangle(g, 90.0f, 20.0f);  // 200
    const auto occ = SelectRegionOccluders(g, 20.0f, 12);
    ASSERT_EQ(occ.size(), 3u);
    EXPECT_EQ(occ.capacity(), 3u);
    EXPECT_FLOAT_EQ(occ[0].area, 50.0f);
    EXPECT_FLOAT_EQ(occ[1].area, 450.0f);
    EXPECT_FLOAT_EQ(occ[2].area, 200.0f);
}

// ---- RegionOccluderTable ----------------------------------------------------

TEST(RegionOccluders, TableFindsEveryRegionInAscendingLoad) {
    RegionOccluderTable t;
    t.set(3, makeSet(2, 3.0f));
    t.set(7, makeSet(5, 7.0f));
    t.set(8, {});                  // empty: ignored, like the old map
    t.set(40000, makeSet(1, 9.0f));
    t.shrinkToFit();

    EXPECT_EQ(t.regionCount(), 3u);
    EXPECT_EQ(t.triangleCount(), 8u);
    EXPECT_TRUE(t.find(0).empty());
    EXPECT_TRUE(t.find(8).empty());
    EXPECT_TRUE(t.find(40001).empty());
    const OccluderSpan s7 = t.find(7);
    ASSERT_EQ(s7.size(), 5u);
    for (size_t i = 0; i < s7.size(); ++i) {
        EXPECT_FLOAT_EQ(s7[i].v0[0], 7.0f);
        EXPECT_FLOAT_EQ(s7[i].v0[1], float(i));
    }
    size_t n = 0;
    for (const auto& tri : t.find(40000)) { EXPECT_FLOAT_EQ(tri.v0[0], 9.0f); ++n; }
    EXPECT_EQ(n, 1u);
}

TEST(RegionOccluders, TableOutOfOrderInsertAndReplace) {
    RegionOccluderTable t;
    t.set(10, makeSet(3, 10.0f));
    t.set(2, makeSet(2, 2.0f));    // before
    t.set(5, makeSet(4, 5.0f));    // middle
    t.set(10, makeSet(1, 11.0f));  // replace, shrinking
    t.set(2, makeSet(6, 12.0f));   // replace, growing
    EXPECT_EQ(t.regionCount(), 3u);
    EXPECT_EQ(t.triangleCount(), 11u);
    ASSERT_EQ(t.find(2).size(), 6u);
    EXPECT_FLOAT_EQ(t.find(2)[5].v0[0], 12.0f);
    ASSERT_EQ(t.find(5).size(), 4u);
    EXPECT_FLOAT_EQ(t.find(5)[3].v0[1], 3.0f);
    ASSERT_EQ(t.find(10).size(), 1u);
    EXPECT_FLOAT_EQ(t.find(10)[0].v0[0], 11.0f);
}

// THE regression net for the overrun: bytes held per triangle and per region.
TEST(RegionOccluders, TableHoldsFortyBytesPerTriangleAndEightPerRegion) {
    static_assert(sizeof(OccluderTriangle) == 40, "OccluderTriangle layout changed; revisit the bound");
    RegionOccluderTable t;
    size_t tris = 0;
    const size_t regions = 5000;
    for (size_t r = 0; r < regions; ++r) {
        const int n = 1 + static_cast<int>(r % 12);  // 1..12, like maxTrianglesPerRegion
        t.set(r * 2, makeSet(n, float(r)));
        tris += static_cast<size_t>(n);
    }
    t.shrinkToFit();
    EXPECT_EQ(t.triangleCount(), tris);
    EXPECT_EQ(t.reservedBytes(), tris * 40 + regions * 8 + 4)
        << "the occluder table must be flat: 40 B/triangle + 4 B region id + 4 B offset, no per-region blocks";
}

TEST(RegionOccluders, CullerHandsTheTableOverWhole) {
    SoftwareOcclusionCuller renderSide;
    renderSide.setRegionOccluders(1, makeSet(2, 1.0f));
    renderSide.setRegionOccluders(4, makeSet(3, 4.0f));
    renderSide.finishOccluders();
    ASSERT_TRUE(renderSide.hasOccluders());

    RegionOccluderTable moved = renderSide.takeRegionOccluders();
    EXPECT_FALSE(renderSide.hasOccluders()) << "the renderer-side culler must not keep a copy (T504)";
    EXPECT_TRUE(renderSide.getRegionOccluders(4).empty());

    SoftwareOcclusionCuller worker;
    worker.setRegionOccluderTable(std::move(moved));
    ASSERT_EQ(worker.getRegionOccluders(4).size(), 3u);
    EXPECT_FLOAT_EQ(worker.getRegionOccluders(4)[2].area, 6.0f);
    worker.clearOccluders();
    EXPECT_FALSE(worker.hasOccluders());
}

// Both loaders select through the one body. The gate measures the benchmark,
// the Pi runs the client; a private copy in either is how the benchmark kept
// the capacity defect after T504 fixed the client.
TEST(RegionOccluders, BothZoneLoadersUseTheSharedSelection) {
    for (const char* rel : {"src/client/graphics/zone_load/renderer_zone_load.cpp",
                            "tools/hwcaps/zone_bench_loader.cpp"}) {
        const std::string src = readSource(rel);
        ASSERT_FALSE(src.empty()) << rel;
        EXPECT_NE(src.find("SelectRegionOccluders("), std::string::npos) << rel;
        EXPECT_NE(src.find("finishOccluders()"), std::string::npos) << rel;
        EXPECT_EQ(src.find("OccluderTriangle ot;"), std::string::npos)
            << rel << " builds occluders by hand again -- call SelectRegionOccluders()";
    }
}
