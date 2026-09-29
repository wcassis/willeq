// B909 (GitLab #229): the zone's collision triangles are held ONCE.
//
// Before B909 a pooled zone's collision set was a COPY of the zone draw
// pools' triangles (36 B per triangle, plus a vector and a map node per
// region): 8.1 MB of the client's `spatial` in gfaydark, against a 4 MiB
// budget (refactor/B909/README.md). Now the set reads the pools in place.
//
// These tests pin
//   * identical collision results: every ray against the pool view gives the
//     same hit/miss, hit point, hit triangle and distance, bit for bit, as the
//     same ray against the pre-B909 copy (built here with the old code);
//   * the same region set as the copy, including a region with no triangle;
//   * held once: a pooled set owns no triangle bytes;
//   * the player-region overlap: the pools' triangles answer, as the copy's
//     overwrite made them before;
//   * the production call site takes the view and makes no copy (census).
#include <gtest/gtest.h>

#include <cstring>
#include <fstream>
#include <map>
#include <random>
#include <sstream>
#include <string>
#include <vector>

#include <irrlicht.h>

#include "client/graphics/door_collision.h"
#include "client/graphics/region_world_triangles.h"
#include "client/graphics/renderer_zone_merge.h"

using namespace EQT::Graphics;
using irr::core::triangle3df;
using irr::core::vector3df;

namespace {

// A mesh of `tris` random triangles in two buffers, around the origin.
irr::scene::SMesh* makeRegionMesh(std::mt19937& rng, int tris) {
    std::uniform_real_distribution<float> d(-40.0f, 40.0f);
    auto* mesh = new irr::scene::SMesh();
    for (int b = 0; b < 2; ++b) {
        auto* buf = new irr::scene::SMeshBuffer();
        buf->Material.MaterialType = b ? irr::video::EMT_TRANSPARENT_ALPHA_CHANNEL_REF : irr::video::EMT_SOLID;
        const int n = (b == 0) ? tris / 2 : tris - tris / 2;
        for (int t = 0; t < n; ++t) {
            for (int k = 0; k < 3; ++k) {
                irr::video::S3DVertex v;
                v.Pos = vector3df(d(rng), d(rng) * 0.25f, d(rng));
                buf->Indices.push_back(static_cast<irr::u16>(buf->Vertices.size()));
                buf->Vertices.push_back(v);
            }
        }
        buf->recalculateBoundingBox();
        mesh->addMeshBuffer(buf);
        buf->drop();
    }
    mesh->recalculateBoundingBox();
    return mesh;
}

// The pre-B909 copy, verbatim in substance (renderer_player_collision.cpp's
// pooled branch before this unit).
std::map<size_t, std::vector<triangle3df>> oldCopy(const ZoneMergeBuffers& pools,
                                                    const std::vector<size_t>& regions) {
    std::map<size_t, std::vector<triangle3df>> out;
    for (size_t regionIdx : regions) {
        std::vector<triangle3df> triangles;
        pools.forEachRegionTriangle(regionIdx, [&](const float* a, const float* b, const float* c) {
            triangles.emplace_back(vector3df(a[0], a[1], a[2]), vector3df(b[0], b[1], b[2]),
                                   vector3df(c[0], c[1], c[2]));
        });
        if (!triangles.empty()) {
            triangles.shrink_to_fit();
            out[regionIdx] = std::move(triangles);
        }
    }
    return out;
}

bool sameBits(const vector3df& a, const vector3df& b) {
    return std::memcmp(&a.X, &b.X, sizeof(float) * 3) == 0;
}

struct Fixture {
    std::mt19937 rng{909};
    ZoneMergeBuffers pools;
    std::vector<size_t> allRegions{3, 7, 11, 12, 40};
    Fixture() {
        pools.beginPooled();
        const float pos[][3] = {{0, 0, 0}, {100, 5, 0}, {0, 0, 100}, {-100, -5, 0}, {50, 0, -50}};
        for (size_t i = 0; i < allRegions.size(); ++i) {
            if (allRegions[i] == 12) continue;   // a region with no geometry at all
            auto* mesh = makeRegionMesh(rng, 60 + 17 * static_cast<int>(i));
            pools.appendRegion(allRegions[i], mesh, pos[i][0], pos[i][1], pos[i][2]);
            mesh->drop();
        }
        pools.finishPooled();
    }
};

}  // namespace

TEST(RegionWorldTriangles, PoolViewGivesBitIdenticalRayResultsToTheOldCopy) {
    Fixture f;
    ASSERT_TRUE(f.pools.isPooled());
    const auto copy = oldCopy(f.pools, f.allRegions);
    ASSERT_EQ(copy.size(), 4u);

    RegionWorldTriangles set;
    set.setSource(&f.pools);

    std::uniform_real_distribution<float> o(-160.0f, 160.0f);
    std::uniform_real_distribution<float> dir(-1.0f, 1.0f);
    size_t hits = 0, rays = 0;
    for (const auto& [region, tris] : copy) {
        const RegionTrianglesView view = set.find(region);
        ASSERT_TRUE(static_cast<bool>(view)) << region;
        for (int r = 0; r < 3000; ++r) {
            vector3df origin(o(f.rng), o(f.rng) * 0.2f, o(f.rng));
            vector3df d(dir(f.rng), dir(f.rng), dir(f.rng));
            if (d.getLength() < 1e-3f) continue;
            d.normalize();
            // Half the rays straight down, as the ground query casts them.
            if (r % 2) { origin.Y = 60.0f; d = vector3df(0, -1, 0); }
            const float len = 400.0f;
            vector3df hpA(1, 2, 3), hpB(1, 2, 3);
            triangle3df htA, htB;
            float dA = -1.0f, dB = -1.0f;
            const bool a = RayIntersectTriangles(origin, d, len, tris, hpA, htA, dA);
            const bool b = RayIntersectTriangles(origin, d, len, view, hpB, htB, dB);
            ++rays;
            ASSERT_EQ(a, b) << "region " << region << " ray " << r;
            if (!a) continue;
            ++hits;
            EXPECT_TRUE(sameBits(hpA, hpB));
            EXPECT_TRUE(sameBits(htA.pointA, htB.pointA) && sameBits(htA.pointB, htB.pointB) &&
                        sameBits(htA.pointC, htB.pointC));
            EXPECT_EQ(0, std::memcmp(&dA, &dB, sizeof(float)));
        }
    }
    EXPECT_GT(hits, 200u) << "the ray set must actually hit something to prove anything (" << rays << " rays)";
}

TEST(RegionWorldTriangles, SameRegionSetAsTheCopyAndNoTriangleBytesHeld) {
    Fixture f;
    const auto copy = oldCopy(f.pools, f.allRegions);
    RegionWorldTriangles set;
    EXPECT_TRUE(set.empty());
    set.setSource(&f.pools);
    EXPECT_FALSE(set.empty());
    EXPECT_EQ(set.size(), copy.size());
    for (size_t r = 0; r < 64; ++r) {
        EXPECT_EQ(static_cast<bool>(set.find(r)), copy.count(r) == 1) << "region " << r;
    }
    size_t copyBytes = 0, copyTris = 0;
    for (const auto& [r, t] : copy) { copyBytes += t.capacity() * sizeof(triangle3df); copyTris += t.size(); }
    EXPECT_EQ(copyBytes, copyTris * 36u) << "the copy this replaced: 36 B per triangle";
    EXPECT_EQ(set.ownedTriangleBytes(), 0u) << "a pooled zone's collision set must not copy the pools";
    set.clear();
    EXPECT_TRUE(set.empty());
    EXPECT_FALSE(static_cast<bool>(set.find(3)));
}

TEST(RegionWorldTriangles, ThePoolsAnswerForAPlayerRegionAlsoBuiltAsANode) {
    Fixture f;
    RegionWorldTriangles set;
    // The node path runs first (the player's region is built synchronously).
    set.setOwned(7, {triangle3df(vector3df(0, 0, 0), vector3df(1, 0, 0), vector3df(0, 0, 1))});
    set.setOwned(99, {triangle3df(vector3df(0, 0, 0), vector3df(1, 0, 0), vector3df(0, 0, 1))});
    set.setSource(&f.pools);
    size_t viewTris = 0, poolTris = 0;
    set.find(7).forEachTriangle([&](const vector3df&, const vector3df&, const vector3df&) { ++viewTris; });
    f.pools.forEachRegionTriangle(7, [&](const float*, const float*, const float*) { ++poolTris; });
    EXPECT_EQ(viewTris, poolTris);
    EXPECT_GT(poolTris, 1u);
    EXPECT_TRUE(static_cast<bool>(set.find(99))) << "a region the pools do not cover stays owned";
}

// The hot path (regionRuns, batches of 16 runs) visits exactly the triangles
// forEachRegionTriangle visits, in the same order, with the same floats --
// including a region whose run count crosses the batch boundary.
TEST(RegionWorldTriangles, RunBatchesVisitTheSameTrianglesInTheSameOrder) {
    std::mt19937 rng(9091);
    std::uniform_real_distribution<float> d(-20.0f, 20.0f);
    ZoneMergeBuffers pools;
    pools.beginPooled();
    auto* mesh = new irr::scene::SMesh();
    for (int b = 0; b < 37; ++b) {   // 37 distinct materials -> 37 runs: 16 + 16 + 5
        auto* buf = new irr::scene::SMeshBuffer();
        buf->Material.MaterialTypeParam = static_cast<float>(b);
        for (int t = 0; t < 5 + b; ++t) {
            for (int k = 0; k < 3; ++k) {
                irr::video::S3DVertex v;
                v.Pos = vector3df(d(rng), d(rng), d(rng));
                buf->Indices.push_back(static_cast<irr::u16>(buf->Vertices.size()));
                buf->Vertices.push_back(v);
            }
        }
        mesh->addMeshBuffer(buf);
        buf->drop();
    }
    pools.appendRegion(5, mesh, 1.0f, 2.0f, 3.0f);
    mesh->drop();
    pools.finishPooled();

    TriangleRun probe[64];
    const size_t runs = pools.regionRuns(5, 0, probe, 64);
    ASSERT_GT(runs, 32u) << "the fixture must cross two batch boundaries";

    std::vector<float> want, got;
    pools.forEachRegionTriangle(5, [&](const float* a, const float* b, const float* c) {
        want.insert(want.end(), {a[0], a[1], a[2], b[0], b[1], b[2], c[0], c[1], c[2]});
    });
    RegionWorldTriangles set;
    set.setSource(&pools);
    set.find(5).forEachTriangle([&](const vector3df& a, const vector3df& b, const vector3df& c) {
        got.insert(got.end(), {a.X, a.Y, a.Z, b.X, b.Y, b.Z, c.X, c.Y, c.Z});
    });
    ASSERT_EQ(got.size(), want.size());
    EXPECT_EQ(0, std::memcmp(got.data(), want.data(), got.size() * sizeof(float)));
}

// The production call site: the pooled branch of setupMinimalZoneCollision()
// hands the pools over and copies nothing.
TEST(RegionWorldTrianglesCensus, PooledCollisionSetupTakesTheViewAndMakesNoCopy) {
    std::ifstream in(std::string(WILLEQ_REPO_ROOT) + "/src/client/graphics/player/renderer_player_collision.cpp");
    ASSERT_TRUE(in.good());
    std::stringstream ss;
    ss << in.rdbuf();
    const std::string src = ss.str();
    const size_t a = src.find("pools && pools->isPooled()) {");
    ASSERT_NE(a, std::string::npos);
    const size_t b = src.find("\"Collision setup:", a);
    ASSERT_NE(b, std::string::npos);
    const std::string branch = src.substr(a, b - a);
    EXPECT_NE(branch.find("regionWorldTriangles.setSource("), std::string::npos) << branch;
    EXPECT_EQ(branch.find("emplace_back"), std::string::npos) << "the pooled branch copies triangles:\n" << branch;
    EXPECT_EQ(branch.find("setOwned("), std::string::npos) << "the pooled branch copies triangles:\n" << branch;
}
