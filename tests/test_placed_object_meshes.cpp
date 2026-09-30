// B936 (GitLab #254): placed objects hold ONE mesh per geometry, the
// vertex-animated (keyframe) ones included.
//
// Before B936, loadZoneStep6_Objects() shared one IMesh per ZoneGeometry
// (T504) EXCEPT for vertex-animated geometries, which got a private mesh per
// placement: growthplane's 4,314 grass/plant placements of 4 geometries were
// 4,314 meshes, 47.1 MB of `objects` on x86, all rewritten on every keyframe
// change. These tests pin
//   * the acceptance of #254: two placements of one animated geometry own ONE
//     vertex array, and it is registered for animation once;
//   * the pose arithmetic: a keyframe lands through the vertex mapping, with
//     the centre offset, exactly as the pre-B936 per-frame loop wrote it;
//   * why sharing is exact: a private mesh per placement, posed at keyframe k
//     (the pre-B936 layout, still built by WILLEQ_SHARE_OBJECT_MESHES=0), holds
//     the same bytes as the one shared mesh posed at k;
//   * the production call sites (census): step 6 goes through
//     PlacedObjectMeshes with no animated exclusion, the per-frame apply poses
//     through applyVertexAnimFrame(), and the worker keeps every placement in
//     phase (the premise of the sharing).
#include <gtest/gtest.h>

#include <cstring>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include <irrlicht.h>

#include "client/graphics/eq/wld_loader.h"
#include "client/graphics/eq/zone_geometry.h"
#include "client/graphics/placed_object_meshes.h"

using namespace EQT::Graphics;

namespace {

// A small plant: a centred quad fan with shared vertices, `frames` keyframes
// that sway it. Geometry vertices carry the centre (cx, cy, cz) baked in;
// keyframe positions are raw (the fragment 0x37 convention bindVertexAnimation
// corrects with its centre offset).
std::shared_ptr<ZoneGeometry> makePlant(const std::string& name, int frames, float cx, float cy, float cz,
                                        float size) {
    auto g = std::make_shared<ZoneGeometry>();
    g->name = name;
    const float raw[6][3] = {{0, 0, 0}, {size, 0, 0}, {size, size, 0}, {0, size, 0},
                             {0, 0, size}, {size, size, size}};
    for (const auto& p : raw) {
        Vertex3D v{};
        v.x = p[0] + cx; v.y = p[1] + cy; v.z = p[2] + cz;
        v.nz = 1.0f;
        g->vertices.push_back(v);
    }
    const uint32_t tris[4][3] = {{0, 1, 2}, {0, 2, 3}, {0, 1, 4}, {2, 3, 5}};
    for (const auto& t : tris) {
        Triangle tri{};
        tri.v1 = t[0]; tri.v2 = t[1]; tri.v3 = t[2];
        g->triangles.push_back(tri);
    }
    g->minX = cx; g->minY = cy; g->minZ = cz;
    g->maxX = cx + size; g->maxY = cy + size; g->maxZ = cz + size;
    if (frames >= 1) {
        auto anim = std::make_shared<MeshAnimatedVertices>();
        anim->name = name + "_DMTRACK";
        anim->fragIndex = 0;
        anim->delayMs = 150;
        for (int f = 0; f < frames; ++f) {
            VertexAnimFrame kf;
            for (size_t i = 0; i < 6; ++i) {
                const float sway = 0.25f * static_cast<float>(f) * raw[i][2];   // tips move, roots stay
                kf.positions.push_back(raw[i][0] + sway);
                kf.positions.push_back(raw[i][1] - 0.5f * sway);
                kf.positions.push_back(raw[i][2]);
            }
            anim->frames.push_back(std::move(kf));
        }
        g->animatedVertices = anim;
    }
    return g;
}

class PlacedObjectMeshesTest : public ::testing::Test {
protected:
    void SetUp() override {
        irr::SIrrlichtCreationParameters params;
        params.DriverType = irr::video::EDT_NULL;
        params.LoggingLevel = irr::ELL_NONE;
        device_ = irr::createDeviceEx(params);
        if (!device_) GTEST_SKIP() << "no EDT_NULL device";
        builder_ = std::make_unique<ZoneMeshBuilder>(device_->getSceneManager(), device_->getVideoDriver(),
                                                     device_->getFileSystem());
    }
    void TearDown() override {
        builder_.reset();
        if (device_) device_->drop();
    }
    // The production build call (step 6's colored-mesh path), counted.
    std::function<irr::scene::IMesh*()> builderFor(const ZoneGeometry& g) {
        return [this, &g]() -> irr::scene::IMesh* { ++builds_; return builder_->buildColoredMesh(g); };
    }
    // One placement the way step 6 makes it: acquire, a node takes its own
    // reference, the iteration's reference is dropped.
    irr::scene::IMeshSceneNode* place(PlacedObjectMeshes& table, const ZoneGeometry& g,
                                      std::vector<VertexAnimatedMesh>& anims) {
        irr::scene::IMesh* mesh = table.acquire(g, builderFor(g));
        EXPECT_NE(mesh, nullptr);
        auto* node = device_->getSceneManager()->addMeshSceneNode(mesh);
        if (isVertexAnimatedGeometry(g)) table.registerAnimated(g, node->getMesh(), g.name, anims);
        mesh->drop();
        return node;
    }
    irr::IrrlichtDevice* device_ = nullptr;
    std::unique_ptr<ZoneMeshBuilder> builder_;
    int builds_ = 0;
};

std::vector<irr::video::S3DVertex> vertexBytes(irr::scene::IMesh* mesh) {
    std::vector<irr::video::S3DVertex> out;
    for (irr::u32 b = 0; b < mesh->getMeshBufferCount(); ++b) {
        const auto* buf = mesh->getMeshBuffer(b);
        const auto* v = static_cast<const irr::video::S3DVertex*>(buf->getVertices());
        out.insert(out.end(), v, v + buf->getVertexCount());
    }
    return out;
}

}  // namespace

// #254 acceptance 3: fails if two placements of one animated geometry own
// separate vertex arrays.
TEST_F(PlacedObjectMeshesTest, TwoPlacementsOfOneAnimatedGeometryOwnOneVertexArray) {
    auto plant = makePlant("CMPLANT102", 4, 10.0f, 20.0f, 30.0f, 2.0f);
    ASSERT_TRUE(isVertexAnimatedGeometry(*plant));
    PlacedObjectMeshes table(true);
    std::vector<VertexAnimatedMesh> anims;
    std::vector<irr::scene::IMeshSceneNode*> nodes;
    for (int i = 0; i < 50; ++i) nodes.push_back(place(table, *plant, anims));

    EXPECT_EQ(builds_, 1);
    EXPECT_EQ(table.meshesBuilt(), 1u);
    EXPECT_EQ(table.placementsReusingAMesh(), 49u);
    for (auto* n : nodes) {
        ASSERT_EQ(n->getMesh(), nodes[0]->getMesh());
        EXPECT_EQ(n->getMesh()->getMeshBuffer(0)->getVertices(),
                  nodes[0]->getMesh()->getMeshBuffer(0)->getVertices());
    }
    ASSERT_EQ(anims.size(), 1u) << "one keyframe registration per MESH, not per placement";
    EXPECT_EQ(anims[0].mesh, nodes[0]->getMesh());
    EXPECT_EQ(anims[0].placements, 50u);
    EXPECT_EQ(table.animatedPlacements(), 50u);
    for (auto* n : nodes) n->remove();
}

TEST_F(PlacedObjectMeshesTest, EachGeometryGetsItsOwnMeshStaticOrAnimated) {
    auto a = makePlant("CMPLANT101", 3, 0, 0, 0, 1.0f);
    auto b = makePlant("DRGRASS101", 2, 5, 5, 5, 1.5f);
    auto rock = makePlant("ROCK", 0, 1, 1, 1, 3.0f);   // no keyframes: static
    ASSERT_FALSE(isVertexAnimatedGeometry(*rock));
    PlacedObjectMeshes table(true);
    std::vector<VertexAnimatedMesh> anims;
    std::vector<irr::scene::IMeshSceneNode*> nodes;
    for (int i = 0; i < 6; ++i) {
        nodes.push_back(place(table, *a, anims));
        nodes.push_back(place(table, *b, anims));
        nodes.push_back(place(table, *rock, anims));
    }
    EXPECT_EQ(builds_, 3);
    ASSERT_EQ(anims.size(), 2u);
    EXPECT_EQ(anims[0].placements, 6u);
    EXPECT_EQ(anims[1].placements, 6u);
    EXPECT_NE(nodes[0]->getMesh(), nodes[1]->getMesh());
    EXPECT_NE(nodes[0]->getMesh(), nodes[2]->getMesh());
    for (auto* n : nodes) n->remove();
}

// WILLEQ_SHARE_OBJECT_MESHES=0: the comparison layout, a mesh and a
// registration per placement (what fable did for animated geometries).
TEST_F(PlacedObjectMeshesTest, SharingOffBuildsAMeshPerPlacement) {
    auto plant = makePlant("CMPLANT102", 4, 0, 0, 0, 2.0f);
    PlacedObjectMeshes table(false);
    std::vector<VertexAnimatedMesh> anims;
    auto* n1 = place(table, *plant, anims);
    auto* n2 = place(table, *plant, anims);
    EXPECT_EQ(builds_, 2);
    EXPECT_NE(n1->getMesh(), n2->getMesh());
    ASSERT_EQ(anims.size(), 2u);
    EXPECT_EQ(anims[0].placements, 1u);
    n1->remove();
    n2->remove();
}

TEST_F(PlacedObjectMeshesTest, AFailedBuildIsNotCached) {
    ZoneGeometry empty;   // no vertices: buildColoredMesh returns null
    PlacedObjectMeshes table(true);
    EXPECT_EQ(table.acquire(empty, builderFor(empty)), nullptr);
    EXPECT_EQ(table.acquire(empty, builderFor(empty)), nullptr);
    EXPECT_EQ(builds_, 2);
    EXPECT_EQ(table.meshesBuilt(), 0u);
}

// The pose arithmetic, against an independent evaluation of the pre-B936
// per-frame loop: every mapped vertex = keyframe + centre offset, EQ (x,y,z)
// -> Irrlicht (x,z,y); nothing else in the vertex moves.
TEST_F(PlacedObjectMeshesTest, AKeyframeLandsThroughTheMappingWithTheCentreOffset) {
    auto plant = makePlant("CMPLANT102", 4, 10.0f, 20.0f, 30.0f, 2.0f);
    PlacedObjectMeshes table(true);
    std::vector<VertexAnimatedMesh> anims;
    auto* node = place(table, *plant, anims);
    ASSERT_EQ(anims.size(), 1u);
    VertexAnimatedMesh& vam = anims[0];
    EXPECT_FLOAT_EQ(vam.centerOffsetX, 10.0f);
    EXPECT_FLOAT_EQ(vam.centerOffsetY, 20.0f);
    EXPECT_FLOAT_EQ(vam.centerOffsetZ, 30.0f);
    const auto before = vertexBytes(vam.mesh);

    for (int k : {3, 1, 0, 2}) {
        applyVertexAnimFrame(vam, k);
        EXPECT_EQ(vam.currentFrame, k);
        const auto after = vertexBytes(vam.mesh);
        ASSERT_EQ(after.size(), before.size());
        size_t b = 0, v = 0;
        for (size_t i = 0; i < after.size(); ++i) {
            // vertexBytes() concatenates buffers; walk the mapping alongside.
            while (v >= vam.vertexMapping[b].size()) { ++b; v = 0; }
            const size_t gi = vam.vertexMapping[b][v++];
            ASSERT_NE(gi, SIZE_MAX);
            const auto& kf = plant->animatedVertices->frames[static_cast<size_t>(k)].positions;
            EXPECT_EQ(after[i].Pos.X, kf[gi * 3 + 0] + vam.centerOffsetX);
            EXPECT_EQ(after[i].Pos.Y, kf[gi * 3 + 2] + vam.centerOffsetZ);
            EXPECT_EQ(after[i].Pos.Z, kf[gi * 3 + 1] + vam.centerOffsetY);
            EXPECT_EQ(after[i].Normal, before[i].Normal);
            EXPECT_EQ(after[i].TCoords, before[i].TCoords);
            EXPECT_EQ(after[i].Color, before[i].Color);
        }
    }
    // Frame 0 is the geometry's own pose (keyframe 0 + offset == the baked vertex).
    applyVertexAnimFrame(vam, 0);
    const auto frame0 = vertexBytes(vam.mesh);
    for (size_t i = 0; i < frame0.size(); ++i) EXPECT_EQ(frame0[i].Pos, before[i].Pos);
    // An out-of-range keyframe leaves the mesh alone.
    applyVertexAnimFrame(vam, 4);
    applyVertexAnimFrame(vam, -1);
    EXPECT_EQ(vam.currentFrame, 0);
    const auto untouched = vertexBytes(vam.mesh);
    EXPECT_EQ(std::memcmp(untouched.data(), frame0.data(), frame0.size() * sizeof(frame0[0])), 0);
    node->remove();
}

// Why the sharing is exact: the pre-B936 layout (a private mesh per placement,
// each posed at the keyframe every placement is on) holds byte-for-byte the
// same vertices as the one shared mesh posed at that keyframe.
TEST_F(PlacedObjectMeshesTest, PrivateMeshesPosedInPhaseEqualTheSharedMeshByteForByte) {
    auto plant = makePlant("CMPLANT101", 5, -3.0f, 7.0f, 1.5f, 3.0f);
    PlacedObjectMeshes shared(true), priv(false);
    std::vector<VertexAnimatedMesh> sharedAnims, privAnims;
    std::vector<irr::scene::IMeshSceneNode*> nodes;
    for (int i = 0; i < 8; ++i) {
        nodes.push_back(place(shared, *plant, sharedAnims));
        nodes.push_back(place(priv, *plant, privAnims));
    }
    ASSERT_EQ(sharedAnims.size(), 1u);
    ASSERT_EQ(privAnims.size(), 8u);
    for (int k = 0; k < 5; ++k) {
        applyVertexAnimFrame(sharedAnims[0], k);
        const auto want = vertexBytes(sharedAnims[0].mesh);
        for (auto& vam : privAnims) {
            applyVertexAnimFrame(vam, k);
            const auto got = vertexBytes(vam.mesh);
            ASSERT_EQ(got.size(), want.size());
            EXPECT_EQ(std::memcmp(got.data(), want.data(), want.size() * sizeof(want[0])), 0) << "keyframe " << k;
        }
    }
    for (auto* n : nodes) n->remove();
}

namespace {
std::string readRepoFile(const std::string& rel) {
    std::ifstream in(std::string(WILLEQ_REPO_ROOT) + "/" + rel);
    std::stringstream ss;
    ss << in.rdbuf();
    return ss.str();
}
std::string functionBody(const std::string& src, const std::string& signature) {
    const size_t a = src.find(signature);
    if (a == std::string::npos) return {};
    const size_t b = src.find("\n}\n", a);
    return src.substr(a, b == std::string::npos ? std::string::npos : b - a);
}
}  // namespace

// Census: the production call sites use the shared table and the one pose
// routine, and the premise (every placement in phase) still holds.
TEST(PlacedObjectMeshesCensus, ProductionSharesAnimatedMeshesAndPosesThemOnce) {
    const std::string step6 = functionBody(
        readRepoFile("src/client/graphics/zone_load/renderer_zone_load_objects.cpp"),
        "void ZoneLoadComponent::loadZoneStep6_Objects(");
    ASSERT_FALSE(step6.empty());
    EXPECT_NE(step6.find("placedMeshes.acquire("), std::string::npos);
    EXPECT_NE(step6.find("placedMeshes.registerAnimated("), std::string::npos);
    EXPECT_EQ(step6.find("!vertexAnimatedObject"), std::string::npos)
        << "an animated exclusion from the mesh sharing is back (B936)";
    EXPECT_EQ(step6.find("vertexAnimatedMeshes.push_back("), std::string::npos)
        << "a registration per placement is back (B936)";

    const std::string apply = functionBody(readRepoFile("src/client/graphics/sim/renderer_sim_bridge.cpp"),
                                           "void SimBridgeComponent::applySimulationOutput(");
    ASSERT_FALSE(apply.empty());
    EXPECT_NE(apply.find("applyVertexAnimFrame("), std::string::npos);

    // In phase: one delta for every entry, every counter reset to frame 0.
    // A per-placement phase would make one shared mesh per geometry wrong;
    // if this fails, B936's sharing has to be revisited, not this test.
    const std::string worker = readRepoFile("src/client/graphics/sim_animation.cpp");
    const std::string compute = functionBody(worker, "void SimulationWorker::computeVertexAnimations(");
    ASSERT_FALSE(compute.empty());
    EXPECT_NE(compute.find("float deltaMs = input.vertAnimDeltaMs;"), std::string::npos);
    const std::string reset = functionBody(readRepoFile("src/client/graphics/simulation_worker.cpp"),
                                           "void SimulationWorker::updateVertexAnimData(");
    ASSERT_FALSE(reset.empty());
    EXPECT_NE(reset.find("s.elapsedMs = 0;"), std::string::npos);
    EXPECT_NE(reset.find("s.currentFrame = 0;"), std::string::npos);
}
