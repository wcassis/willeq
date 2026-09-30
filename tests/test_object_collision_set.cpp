// B909b (GitLab #229): placed objects' collision holds each distinct mesh ONCE.
//
// Before B909b, ZoneLoadComponent::loadZoneStep9_Collision() ran a triangle
// selector over every object node and appended the WORLD-space result to a
// per-region vector: 36 B per triangle PER INSTANCE, so a mesh placed a
// thousand times was copied a thousand times (growthplane: 40.2 MB of the
// Pi's `spatial`, refactor/B909b/README.md). ObjectCollisionSet reads each
// distinct mesh in place and keeps a matrix + box per instance.
//
// These tests pin
//   * identical collision results: every ray against the set gives the same
//     hit/miss, hit point, hit triangle and distance, bit for bit, as the same
//     ray against the pre-B909b copy -- built here with the production
//     PlayerCameraComponent::extractWorldTriangles() over real scene nodes,
//     the exact old code path -- over every ray shape the client casts;
//   * held once: the set's own bytes do not grow with triangles x instances;
//   * a vertex-animated mesh is snapshotted (later vertex writes do not move
//     its collision, as they did not move the old copy);
//   * the production call sites build and read the set, not a copy (census).
#include <gtest/gtest.h>

#include <cmath>
#include <cstring>
#include <fstream>
#include <map>
#include <random>
#include <sstream>
#include <string>
#include <vector>

#include <irrlicht.h>

#include "client/graphics/door_collision.h"
#include "client/graphics/object_collision_set.h"
#include "client/graphics/player/player_camera_component.h"

using namespace EQT::Graphics;
using irr::core::triangle3df;
using irr::core::vector3df;

namespace {

// A mesh of `tris` random triangles over `buffers` buffers. `lightmap` uses
// S3DVertex2TCoords buffers (44 B vertices), as the atlased object builder does.
irr::scene::SMesh* makeObjectMesh(std::mt19937& rng, int tris, int buffers, bool lightmap, float size) {
    std::uniform_real_distribution<float> d(-size, size);
    auto* mesh = new irr::scene::SMesh();
    for (int b = 0; b < buffers; ++b) {
        irr::scene::IMeshBuffer* out = nullptr;
        const int n = tris / buffers + (b < tris % buffers ? 1 : 0);
        if (lightmap) {
            auto* buf = new irr::scene::SMeshBufferLightMap();
            // Shared vertices: a small vertex pool indexed repeatedly.
            for (int v = 0; v < n + 2; ++v) {
                irr::video::S3DVertex2TCoords vx;
                vx.Pos = vector3df(d(rng), d(rng) + size, d(rng));
                buf->Vertices.push_back(vx);
            }
            for (int t = 0; t < n; ++t) {
                buf->Indices.push_back(static_cast<irr::u16>(t));
                buf->Indices.push_back(static_cast<irr::u16>(t + 1));
                buf->Indices.push_back(static_cast<irr::u16>(t + 2));
            }
            out = buf;
        } else {
            auto* buf = new irr::scene::SMeshBuffer();
            for (int t = 0; t < n; ++t) {
                for (int k = 0; k < 3; ++k) {
                    irr::video::S3DVertex v;
                    v.Pos = vector3df(d(rng), d(rng) + size, d(rng));
                    buf->Indices.push_back(static_cast<irr::u16>(buf->Vertices.size()));
                    buf->Vertices.push_back(v);
                }
            }
            out = buf;
        }
        out->recalculateBoundingBox();
        mesh->addMeshBuffer(out);
        out->drop();
    }
    mesh->recalculateBoundingBox();
    return mesh;
}

// A flat, exactly horizontal table top (two triangles at y = h): the worst
// case for grazing, in-plane movement rays.
irr::scene::SMesh* makeTableTop(float half, float h) {
    auto* mesh = new irr::scene::SMesh();
    auto* buf = new irr::scene::SMeshBuffer();
    const vector3df p[4] = {{-half, h, -half}, {half, h, -half}, {half, h, half}, {-half, h, half}};
    for (const auto& q : p) {
        irr::video::S3DVertex v;
        v.Pos = q;
        buf->Vertices.push_back(v);
    }
    const irr::u16 idx[6] = {0, 1, 2, 0, 2, 3};
    for (irr::u16 i : idx) buf->Indices.push_back(i);
    buf->recalculateBoundingBox();
    mesh->addMeshBuffer(buf);
    buf->drop();
    mesh->recalculateBoundingBox();
    return mesh;
}

bool sameBits(const vector3df& a, const vector3df& b) {
    return std::memcmp(&a.X, &b.X, sizeof(float) * 3) == 0;
}

class ObjectCollisionSetTest : public ::testing::Test {
protected:
    void SetUp() override {
        irr::SIrrlichtCreationParameters params;
        params.DriverType = irr::video::EDT_NULL;
        params.LoggingLevel = irr::ELL_NONE;
        device_ = irr::createDeviceEx(params);
        if (!device_) GTEST_SKIP() << "no EDT_NULL device";
        smgr_ = device_->getSceneManager();

        // Five distinct meshes, shared by many instances (as T504 shares
        // them), plus one private "vertex-animated" mesh per animated instance.
        meshes_.push_back(makeObjectMesh(rng_, 40, 2, false, 6.0f));   // tree-ish
        meshes_.push_back(makeObjectMesh(rng_, 12, 1, false, 2.0f));   // plant
        meshes_.push_back(makeObjectMesh(rng_, 30, 3, true, 4.0f));    // atlased rock
        meshes_.push_back(makeObjectMesh(rng_, 7, 1, false, 1.0f));    // debris
        meshes_.push_back(makeTableTop(5.0f, 3.0f));                   // table top

        std::uniform_real_distribution<float> pos(-100.0f, 100.0f);
        std::uniform_real_distribution<float> rot(0.0f, 360.0f);
        std::uniform_real_distribution<float> sc(0.5f, 2.0f);
        std::uniform_int_distribution<int> which(0, static_cast<int>(meshes_.size()) - 1);
        for (int i = 0; i < kInstances; ++i) {
            const bool animated = (i % 37) == 5;
            irr::scene::IMesh* mesh = nullptr;
            if (animated) {
                auto* m = makeObjectMesh(rng_, 8, 1, false, 3.0f);
                animatedMeshes_.push_back(m);
                mesh = m;
            } else {
                mesh = meshes_[which(rng_)];
            }
            auto* node = smgr_->addMeshSceneNode(mesh);
            node->setPosition(vector3df(pos(rng_), pos(rng_) * 0.05f, pos(rng_)));
            // Table tops (and some others) rotate about Y only, like most
            // placeables; the rest take all three axes and non-uniform scale.
            if (mesh == meshes_[4] || i % 3 == 0) {
                node->setRotation(vector3df(0.0f, rot(rng_), 0.0f));
                const float s = sc(rng_);
                node->setScale(vector3df(s, s, s));
            } else {
                node->setRotation(vector3df(rot(rng_), rot(rng_), rot(rng_)));
                node->setScale(vector3df(sc(rng_), sc(rng_), sc(rng_)));
            }
            node->updateAbsolutePosition();
            // Regions: a coarse 3x3 grid of the placement area (9 regions),
            // plus SIZE_MAX (unknown) for a few, as the loader records them.
            const vector3df p = node->getPosition();
            size_t region = static_cast<size_t>((p.X + 100.0f) / 67.0f) * 3 +
                            static_cast<size_t>((p.Z + 100.0f) / 67.0f) + 10;
            if (i % 53 == 7) region = SIZE_MAX;
            nodes_.push_back(node);
            regions_.push_back(region);
            animated_.push_back(animated);
        }
        buildOld();
        buildNew();
    }
    void TearDown() override {
        set_.clear();
        for (auto* m : meshes_) m->drop();
        for (auto* m : animatedMeshes_) m->drop();
        if (device_) device_->drop();
    }

    // The pre-B909b copy: loadZoneStep9_Collision()'s old loop, verbatim in
    // substance.
    void buildOld() {
        for (size_t oi = 0; oi < nodes_.size(); ++oi) {
            auto triangles = PlayerCameraComponent::extractWorldTriangles(smgr_, nodes_[oi]);
            if (triangles.empty()) continue;
            auto& regionTris = old_[regions_[oi]];
            oldTriangles_ += triangles.size();
            regionTris.insert(regionTris.end(), triangles.begin(), triangles.end());
        }
    }
    // The new build, as the production step 9 does it.
    void buildNew() {
        for (size_t oi = 0; oi < nodes_.size(); ++oi) {
            set_.addInstance(regions_[oi], nodes_[oi]->getMesh(),
                             SelectorWorldMatrix(nodes_[oi]->getAbsoluteTransformation()), animated_[oi]);
        }
        set_.finish();
    }

    // Returns the number of rays whose results differ in any bit.
    size_t compare(size_t region, const vector3df& origin, const vector3df& dir, float len, size_t& hits) {
        vector3df hpA(1, 2, 3), hpB(1, 2, 3);
        triangle3df htA, htB;
        float dA = -1.0f, dB = -1.0f;
        bool a = false;
        auto it = old_.find(region);
        if (it != old_.end()) a = RayIntersectTriangles(origin, dir, len, it->second, hpA, htA, dA);
        const bool b = set_.rayIntersect(region, origin, dir, len, hpB, htB, dB);
        auto show = [&](const char* why) {
            ADD_FAILURE() << why << ": region " << region << " origin (" << origin.X << "," << origin.Y << ","
                          << origin.Z << ") dir (" << dir.X << "," << dir.Y << "," << dir.Z << ") len " << len
                          << "\n  old hit=" << a << " p=(" << hpA.X << "," << hpA.Y << "," << hpA.Z << ") d2=" << dA
                          << " tri A=(" << htA.pointA.X << "," << htA.pointA.Y << "," << htA.pointA.Z << ")"
                          << "\n  new hit=" << b << " p=(" << hpB.X << "," << hpB.Y << "," << hpB.Z << ") d2=" << dB
                          << " tri A=(" << htB.pointA.X << "," << htB.pointA.Y << "," << htB.pointA.Z << ")";
        };
        if (a != b) { show("hit/miss differs"); return 1; }
        if (!a) return 0;
        ++hits;
        const bool same = sameBits(hpA, hpB) && sameBits(htA.pointA, htB.pointA) &&
                          sameBits(htA.pointB, htB.pointB) && sameBits(htA.pointC, htB.pointC) &&
                          std::memcmp(&dA, &dB, sizeof(float)) == 0;
        if (!same) show("hit differs");
        return same ? 0 : 1;
    }

    static constexpr int kInstances = 900;
    std::mt19937 rng_{9092};
    irr::IrrlichtDevice* device_ = nullptr;
    irr::scene::ISceneManager* smgr_ = nullptr;
    std::vector<irr::scene::SMesh*> meshes_;
    std::vector<irr::scene::SMesh*> animatedMeshes_;
    std::vector<irr::scene::IMeshSceneNode*> nodes_;
    std::vector<size_t> regions_;
    std::vector<bool> animated_;
    std::map<size_t, std::vector<triangle3df>> old_;
    size_t oldTriangles_ = 0;
    ObjectCollisionSet set_;
};

}  // namespace

TEST_F(ObjectCollisionSetTest, SameRegionsAndTrianglesAsTheOldCopy) {
    EXPECT_EQ(set_.instanceCount(), nodes_.size());
    EXPECT_EQ(set_.worldTriangleCount(), oldTriangles_);
    EXPECT_EQ(set_.regionCount(), old_.size());
    for (const auto& [region, tris] : old_) EXPECT_TRUE(set_.hasRegion(region)) << region;
    EXPECT_FALSE(set_.hasRegion(3));
    // 5 shared meshes + one private snapshot per animated instance.
    EXPECT_EQ(set_.meshCount(), meshes_.size() + animatedMeshes_.size());
}

// The premise: transforming a mesh's object-space vertices with
// SelectorWorldMatrix() at query time gives the selector's world triangles,
// bit for bit, in the selector's order.
TEST_F(ObjectCollisionSetTest, QueryTimeTransformReproducesTheSelectorBitForBit) {
    size_t compared = 0, differing = 0;
    for (auto* node : nodes_) {
        const auto want = PlayerCameraComponent::extractWorldTriangles(smgr_, node);
        const irr::core::matrix4 mat = SelectorWorldMatrix(node->getAbsoluteTransformation());
        std::vector<triangle3df> got;
        irr::scene::IMesh* mesh = node->getMesh();
        for (irr::u32 b = 0; b < mesh->getMeshBufferCount(); ++b) {
            const irr::scene::IMeshBuffer* buf = mesh->getMeshBuffer(b);
            for (irr::u32 i = 0; i + 2 < buf->getIndexCount(); i += 3) {
                triangle3df t;
                mat.transformVect(t.pointA, buf->getPosition(buf->getIndices()[i]));
                mat.transformVect(t.pointB, buf->getPosition(buf->getIndices()[i + 1]));
                mat.transformVect(t.pointC, buf->getPosition(buf->getIndices()[i + 2]));
                got.push_back(t);
            }
        }
        ASSERT_EQ(got.size(), want.size());
        for (size_t k = 0; k < got.size(); ++k) {
            ++compared;
            if (!sameBits(got[k].pointA, want[k].pointA) || !sameBits(got[k].pointB, want[k].pointB) ||
                !sameBits(got[k].pointC, want[k].pointC))
                ++differing;
        }
    }
    EXPECT_GT(compared, 10000u);
    EXPECT_EQ(differing, 0u) << "of " << compared << " triangles";
}

// The ray shapes the client casts, 13,000+ in all: random rays, the ground
// query's straight-down rays over instances, movement rays aimed through
// instances at every height, and in-plane rays along exactly horizontal table
// tops (the grazing case where Moller-Trumbore's determinant is rounding noise).
TEST_F(ObjectCollisionSetTest, EveryRayGivesBitIdenticalResultsToTheOldCopy) {
    std::uniform_real_distribution<float> o(-130.0f, 130.0f);
    std::uniform_real_distribution<float> u(-1.0f, 1.0f);
    std::uniform_real_distribution<float> h(-2.0f, 16.0f);
    std::uniform_int_distribution<size_t> pick(0, nodes_.size() - 1);
    size_t rays = 0, hits = 0, diffs = 0, grazingHits = 0;
    std::map<std::string, size_t> diffsByKind;
    auto run = [&](const char* kind, size_t region, const vector3df& origin, vector3df dir, float len) {
        if (dir.getLength() < 1e-3f) return;
        dir.normalize();
        ++rays;
        const size_t before = hits;
        const size_t d = compare(region, origin, dir, len, hits);
        diffs += d;
        diffsByKind[kind] += d;
        if (std::string(kind) == "grazing" && hits > before) ++grazingHits;
    };
    for (int r = 0; r < 4000; ++r) {   // random rays in every region
        const size_t i = pick(rng_);
        run("random", regions_[i], vector3df(o(rng_), h(rng_), o(rng_)), vector3df(u(rng_), u(rng_), u(rng_)), 400.0f);
    }
    for (int r = 0; r < 4000; ++r) {   // ground query: straight down over an instance
        const size_t i = pick(rng_);
        const vector3df p = nodes_[i]->getPosition();
        const vector3df jitter(u(rng_) * 6.0f, 0.0f, u(rng_) * 6.0f);
        const float len = (r % 2) ? 12.0f : 520.0f;   // phase 1 short, phase 2 full
        run("down", regions_[i], p + jitter + vector3df(0.0f, 30.0f, 0.0f), vector3df(0, -1, 0), len);
    }
    for (int r = 0; r < 4000; ++r) {   // movement: short horizontal-ish rays through an instance
        const size_t i = pick(rng_);
        const vector3df p = nodes_[i]->getPosition() + vector3df(0.0f, h(rng_), 0.0f);
        const vector3df d(u(rng_), u(rng_) * 0.1f, u(rng_));
        run("move", regions_[i], p - d * 8.0f, d, 16.0f);
    }
    for (int r = 0; r < 1200; ++r) {   // grazing: in the plane of a table top
        size_t i = pick(rng_);
        for (int tries = 0; tries < 50 && nodes_[i]->getMesh() != meshes_[4]; ++tries) i = pick(rng_);
        const auto& box = nodes_[i]->getTransformedBoundingBox();
        const float y = box.MinEdge.Y;   // the top's world height (it is flat)
        const vector3df c = nodes_[i]->getPosition();
        const vector3df d(u(rng_), 0.0f, u(rng_));
        const vector3df off(u(rng_) * 60.0f, 0.0f, u(rng_) * 60.0f);
        run("grazing", regions_[i], vector3df(c.X + off.X, y, c.Z + off.Z), d, 200.0f);
    }
    EXPECT_GE(rays, 13000u);
    EXPECT_GT(hits, 3000u) << "the ray set must actually hit something to prove anything";
    for (const auto& [kind, n] : diffsByKind) EXPECT_EQ(n, 0u) << kind << " rays differ from the old copy";
    EXPECT_EQ(diffs, 0u) << rays << " rays, " << hits << " hits, " << grazingHits << " grazing hits";
}

// Held once: the set's own bytes are per instance and per distinct mesh, not
// per triangle per instance. The shared meshes' arrays belong to `objects`.
TEST_F(ObjectCollisionSetTest, HoldsEachMeshOnceNotACopyPerInstance) {
    const size_t oldBytes = oldTriangles_ * sizeof(triangle3df);
    EXPECT_EQ(sizeof(triangle3df), 36u);
    size_t snapshotTris = 0;
    for (auto* m : animatedMeshes_)
        for (irr::u32 b = 0; b < m->getMeshBufferCount(); ++b) snapshotTris += m->getMeshBuffer(b)->getIndexCount() / 3;
    // Per instance: a matrix, a box, a region and a slot -- bounded; plus each
    // snapshot's own triangles (12 B per vertex) and a small table per mesh.
    const size_t bound = set_.instanceCount() * 128 + snapshotTris * 36 + set_.meshCount() * 128 +
                         set_.regionCount() * 16 + 4096;
    EXPECT_LE(set_.ownedBytes(), bound) << "the set must not hold a world-space copy per instance";
    EXPECT_LT(set_.ownedBytes() * 4, oldBytes) << "old copy " << oldBytes << " B, set " << set_.ownedBytes() << " B";
    EXPECT_GT(set_.sharedMeshBytes(), 0u);
    EXPECT_EQ(set_.meshTriangleCount() < set_.worldTriangleCount(), true);
}

// A vertex-animated mesh's collision is the load-time pose, as the old copy's
// was: rewriting its vertices after the build must not move it.
TEST_F(ObjectCollisionSetTest, AnimatedMeshIsSnapshottedAtBuild) {
    ASSERT_FALSE(animatedMeshes_.empty());
    size_t idx = 0;
    while (idx < nodes_.size() && !animated_[idx]) ++idx;
    ASSERT_LT(idx, nodes_.size());
    const vector3df p = nodes_[idx]->getPosition();
    // Find a straight-down ray that hits this instance's triangles.
    auto* mesh = static_cast<irr::scene::SMesh*>(nodes_[idx]->getMesh());
    auto* buf = static_cast<irr::scene::SMeshBuffer*>(mesh->getMeshBuffer(0));
    vector3df hpBefore, hpAfter;
    triangle3df ht;
    float dsq = 0.0f;
    bool found = false;
    vector3df origin;
    std::uniform_real_distribution<float> u(-4.0f, 4.0f);
    for (int t = 0; t < 2000 && !found; ++t) {
        origin = p + vector3df(u(rng_), 40.0f, u(rng_));
        found = set_.rayIntersect(regions_[idx], origin, vector3df(0, -1, 0), 100.0f, hpBefore, ht, dsq);
    }
    ASSERT_TRUE(found);
    for (irr::u32 v = 0; v < buf->Vertices.size(); ++v) buf->Vertices[v].Pos.Y += 1000.0f;   // "animate"
    ASSERT_TRUE(set_.rayIntersect(regions_[idx], origin, vector3df(0, -1, 0), 100.0f, hpAfter, ht, dsq));
    EXPECT_TRUE(sameBits(hpBefore, hpAfter));
}

TEST(ObjectCollisionSetBasics, EmptyAndClear) {
    ObjectCollisionSet set;
    EXPECT_TRUE(set.empty());
    vector3df hp;
    triangle3df ht;
    float d = 0.0f;
    EXPECT_FALSE(set.rayIntersect(1, vector3df(0, 10, 0), vector3df(0, -1, 0), 20.0f, hp, ht, d));
    auto* mesh = makeTableTop(1.0f, 0.0f);
    EXPECT_TRUE(set.addInstance(1, mesh, irr::core::matrix4(), false));
    auto* empty = new irr::scene::SMesh();
    EXPECT_FALSE(set.addInstance(1, empty, irr::core::matrix4(), false)) << "a mesh with no triangle adds nothing";
    set.finish();
    EXPECT_EQ(set.meshCount(), 1u);
    EXPECT_TRUE(set.rayIntersect(1, vector3df(0.2f, 10, 0.3f), vector3df(0, -1, 0), 20.0f, hp, ht, d));
    EXPECT_FALSE(set.rayIntersect(2, vector3df(0.2f, 10, 0.3f), vector3df(0, -1, 0), 20.0f, hp, ht, d));
    set.clear();
    EXPECT_TRUE(set.empty());
    EXPECT_FALSE(set.rayIntersect(1, vector3df(0.2f, 10, 0.3f), vector3df(0, -1, 0), 20.0f, hp, ht, d));
    empty->drop();
    mesh->drop();
}

// A tie (two instances' faces at exactly the same t) goes to the instance
// placed FIRST, as it did in the old copy, whose single Moller-Trumbore loop
// ran over the region's triangles in placement order with a strict `<`.
// Small-integer coordinates keep every step exact, so the two t are equal.
TEST(ObjectCollisionSetBasics, ATieGoesToTheInstancePlacedFirst) {
    auto* small = makeTableTop(1.0f, 3.0f);   // quad (-1..1) at y = 3
    auto* large = makeTableTop(2.0f, 3.0f);   // quad (-2..2) at y = 3
    const vector3df origin(0.25f, 10.0f, 0.5f), down(0, -1, 0);
    for (int order = 0; order < 2; ++order) {
        irr::scene::SMesh* first = order ? large : small;
        irr::scene::SMesh* second = order ? small : large;
        ObjectCollisionSet set;
        ASSERT_TRUE(set.addInstance(4, first, irr::core::matrix4(), false));
        ASSERT_TRUE(set.addInstance(4, second, irr::core::matrix4(), false));
        set.finish();
        std::vector<triangle3df> old;
        AppendMeshWorldTriangles(first, irr::core::matrix4(), old);
        AppendMeshWorldTriangles(second, irr::core::matrix4(), old);
        vector3df hpA, hpB;
        triangle3df htA, htB;
        float dA = 0.0f, dB = 0.0f;
        ASSERT_TRUE(RayIntersectTriangles(origin, down, 20.0f, old, hpA, htA, dA));
        ASSERT_TRUE(set.rayIntersect(4, origin, down, 20.0f, hpB, htB, dB));
        EXPECT_TRUE(sameBits(htA.pointA, htB.pointA) && sameBits(htA.pointB, htB.pointB) &&
                    sameBits(htA.pointC, htB.pointC)) << "order " << order;
        // The winner really is the first-placed quad's triangle.
        const float extent = order ? 2.0f : 1.0f;
        EXPECT_EQ(std::fabs(htB.pointA.X), extent) << "order " << order;
    }
    small->drop();
    large->drop();
}

namespace {
std::string readRepoFile(const char* rel) {
    std::ifstream in(std::string(WILLEQ_REPO_ROOT) + "/" + rel);
    std::stringstream ss;
    ss << in.rdbuf();
    return ss.str();
}
}  // namespace

// The production sites: step 9 builds the set (no per-instance world-space
// extraction) and the one reader asks the set.
TEST(ObjectCollisionSetCensus, Step9BuildsTheSetAndMakesNoPerInstanceCopy) {
    const std::string src = readRepoFile("src/client/graphics/zone_load/renderer_zone_load_objects.cpp");
    const size_t a = src.find("void ZoneLoadComponent::loadZoneStep9_Collision(");
    ASSERT_NE(a, std::string::npos);
    const std::string step9 = src.substr(a);
    EXPECT_NE(step9.find("objectCollision.addInstance("), std::string::npos);
    EXPECT_NE(step9.find("objectCollision.finish()"), std::string::npos);
    EXPECT_EQ(step9.find("extractWorldTriangles("), std::string::npos)
        << "step 9 copies world-space triangles per object instance again";
    EXPECT_EQ(src.find("objectWorldTriangles"), std::string::npos);

    const std::string reader = readRepoFile("src/client/graphics/player/renderer_player_collision.cpp");
    const size_t r = reader.find("bool PlayerCameraComponent::rayTestDoorsAndObjects(");
    ASSERT_NE(r, std::string::npos);
    EXPECT_NE(reader.find("ctx_.objectCollision.rayIntersect(", r), std::string::npos);
    EXPECT_EQ(reader.find("objectWorldTriangles"), std::string::npos);
}
