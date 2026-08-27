/**
 * T296 -- first-person viewmodel pass, stage 1.
 *
 * Headless coverage for the geometry math the viewmodel pass depends on:
 * the arm-filtered instance mesh (T74) and the hand ("R_POINT") bone live in
 * two different frames, and the pass has to put both into ONE viewmodel
 * space in front of the camera. This file pins which frame relationship is
 * the correct one and that the resulting placement puts every arm vertex in
 * front of the viewmodel near plane (the failure T71/T74/T75/T76 all hit:
 * arm geometry straddling the near plane, partly BEHIND the eye).
 *
 * EDT_NULL device, real S3D model data, no server, no display.
 */

#include <gtest/gtest.h>
#include <fstream>
#include <iostream>
#include <cmath>
#include <limits>

#ifdef EQT_HAS_GRAPHICS
#include "client/graphics/eq/race_model_loader.h"
#include "client/graphics/eq/animated_mesh_scene_node.h"
#include "client/graphics/eq/bone_attachment.h"
#include "client/graphics/entity_renderer.h"
#include "client/graphics/viewmodel_transform.h"
#include "client/graphics/constrained_texture_cache.h"
#include "client/graphics/constrained_renderer_config.h"
#include "client/willeq_database.h"

class ViewmodelGeometryTest : public ::testing::Test {
protected:
    std::string eqClientPath_;
    irr::IrrlichtDevice* device_ = nullptr;
    irr::scene::ISceneManager* smgr_ = nullptr;
    irr::video::IVideoDriver* driver_ = nullptr;
    irr::io::IFileSystem* fs_ = nullptr;
    std::unique_ptr<EQT::Graphics::ConstrainedTextureCache> textureCache_;
    std::unique_ptr<EQT::Graphics::RaceModelLoader> loader_;
    EQT::WilleqDatabase willeqDb_;

    static bool fileExists(const std::string& p) { std::ifstream f(p); return f.good(); }

    void SetUp() override {
        const std::string dbPath = WILLEQ_DB_REAL_PATH;
        if (!fileExists(dbPath)) GTEST_SKIP() << "no shipped db at " << dbPath;
        ASSERT_TRUE(willeqDb_.open(dbPath)) << dbPath;
        EQT::WilleqDatabase::setGlobalInstance(&willeqDb_);

        const char* envPath = std::getenv("EQ_CLIENT_PATH");
        eqClientPath_ = envPath ? envPath : EQ_CLIENT_DATA_PATH;
        if (!eqClientPath_.empty() && eqClientPath_.back() != '/') eqClientPath_ += '/';
        if (!fileExists(eqClientPath_ + "global_chr.s3d"))
            GTEST_SKIP() << "global_chr.s3d not found at " << eqClientPath_;

        irr::SIrrlichtCreationParameters params;
        params.DriverType = irr::video::EDT_NULL;
        params.WindowSize = irr::core::dimension2d<irr::u32>(64, 64);
        device_ = irr::createDeviceEx(params);
        if (!device_) GTEST_SKIP() << "Failed to create EDT_NULL Irrlicht device";
        smgr_ = device_->getSceneManager();
        driver_ = device_->getVideoDriver();
        fs_ = device_->getFileSystem();

        loader_ = std::make_unique<EQT::Graphics::RaceModelLoader>(smgr_, driver_, fs_);
        loader_->setClientPath(eqClientPath_);
        auto config = EQT::Graphics::ConstrainedRendererConfig::fromPreset(
            EQT::Graphics::ConstrainedRenderingPreset::TNT);
        textureCache_ = std::make_unique<EQT::Graphics::ConstrainedTextureCache>(config);
        textureCache_->setDriver(driver_);
        loader_->setConstrainedTextureCache(textureCache_.get());
        loader_->getMeshBuilder()->setConstrainedTextureCache(textureCache_.get());
    }

    void TearDown() override {
        loader_.reset();
        textureCache_.reset();
        if (device_) { device_->drop(); device_ = nullptr; }
        EQT::WilleqDatabase::setGlobalInstance(nullptr);
    }

    // Build a live, animated, arm-filtered player node at heading `headingY`.
    EQT::Graphics::EQAnimatedMeshSceneNode* makePlayerNode(uint16_t race, uint8_t gender,
                                                           float headingY) {
        auto* node = loader_->createAnimatedNode(race, gender, smgr_->getRootSceneNode());
        if (!node) return nullptr;
        node->setIsPlayerNode(true);
        node->setRotation(irr::core::vector3df(0, headingY, 0));
        node->playAnimation("l01", true);
        node->OnAnimate(100);
        node->setFirstPersonMode(true);
        node->OnAnimate(200);
        return node;
    }

    static std::vector<irr::core::vector3df> armVertices(
            EQT::Graphics::EQAnimatedMeshSceneNode* node) {
        std::vector<irr::core::vector3df> out;
        auto* mesh = node->getInstanceMesh();
        if (!mesh) return out;
        for (irr::u32 b = 0; b < mesh->getMeshBufferCount(); ++b) {
            irr::scene::IMeshBuffer* mb = mesh->getMeshBuffer(b);
            if (!mb) continue;
            const irr::u16* idx = mb->getIndices();
            const irr::u32 icount = mb->getIndexCount();
            auto* verts = static_cast<irr::video::S3DVertex*>(mb->getVertices());
            for (irr::u32 i = 0; i < icount; ++i) out.push_back(verts[idx[i]].Pos);
        }
        return out;
    }

    // The hand bone position expressed the way updateEquipmentTransforms()
    // does it: EQ(x,y,z) -> Irrlicht(x,z,y), in the NODE's local space.
    static bool handBonePos(EQT::Graphics::EQAnimatedMeshSceneNode* node,
                            uint16_t raceId, irr::core::vector3df& out) {
        auto& animator = node->getAnimator();
        auto skeleton = animator.getSkeleton();
        if (!skeleton) return false;
        const auto& boneStates = animator.getBoneStates();
        if (boneStates.empty()) return false;
        std::string raceCode = EQT::Graphics::RaceModelLoader::getRaceCode(raceId);
        for (auto& c : raceCode) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
        std::vector<std::string> suffixes = {"R_POINT_DAG", "R_POINT", "r_point_dag", "r_point"};
        int bi = EQT::Graphics::findBoneIndex(skeleton, raceCode, suffixes);
        if (bi < 0 || bi >= static_cast<int>(boneStates.size())) return false;
        const auto& m = boneStates[bi].worldTransform;
        out = irr::core::vector3df(m.m[12], m.m[14], m.m[13]);
        return true;
    }
};
#endif  // EQT_HAS_GRAPHICS

#ifdef EQT_HAS_GRAPHICS
using EQT::Graphics::computeViewmodelPlacement;
using EQT::Graphics::measureIndexedBounds;
using EQT::Graphics::ViewmodelPlacement;
using EQT::Graphics::ViewmodelTuning;

namespace {
constexpr float kFovY = 0.897f;   // Irrlicht's default vertical FOV (radians)
constexpr float kAspect = 800.0f / 600.0f;
}  // namespace

// The frame algebra this whole unit rests on, measured rather than assumed:
// the player's per-instance mesh is baked with an extra Y rotation, the hand
// bone is not, and the pass composes them into one space. If the relation
// ever changes, the weapon leaves the hand -- so pin it here.
//
// Derivation (applyBoneTransformsToMeshImpl, animated_mesh_scene_node.cpp):
// the extra rotation is (nodeRotationY + 90) degrees about the EQ Z axis,
// applied before the EQ(x,y,z) -> Irrlicht(x,z,y) swap, which is a
// -(h + 90) degree Y rotation in Irrlicht terms. So RotY(h + 90) takes a
// baked vertex back to the node-local frame the bone lives in.
TEST_F(ViewmodelGeometryTest, BakedFrameIsNodeLocalRotatedByHeadingPlus90) {
    for (float h : {0.0f, 37.0f, 137.0f, 250.0f}) {
        SCOPED_TRACE(h);
        auto* node = makePlayerNode(1, 0, h);
        ASSERT_NE(node, nullptr);
        ASSERT_TRUE(node->hasValidFirstPersonGeometry());
        auto verts = armVertices(node);
        ASSERT_FALSE(verts.empty());
        irr::core::vector3df hand;
        ASSERT_TRUE(handBonePos(node, 1, hand));

        irr::core::matrix4 toNodeLocal;
        toNodeLocal.setRotationDegrees(irr::core::vector3df(0, h + 90.0f, 0));
        float best = std::numeric_limits<float>::max();
        for (const auto& v0 : verts) {
            irr::core::vector3df v = v0;
            toNodeLocal.rotateVect(v);
            best = std::min(best, v.getDistanceFrom(hand));
        }
        // The R_POINT bone sits at the hand; after the mapping it must be
        // INSIDE the arm geometry, not somewhere else on the body. Anything
        // above ~0.2 model units means the frames are misaligned.
        EXPECT_LT(best, 0.20f)
            << "heading=" << h << ": R_POINT is " << best
            << " units from the nearest arm vertex after RotY(h+90) -- the "
               "baked/node-local frame relation is wrong, so equipment will "
               "not land in the hand";
        node->remove();
    }
}

// The T74 filter rewrites INDEX buffers only, so the mesh's own bounding box
// still spans the whole body. measureIndexedBounds() is what the pass uses
// to size the arms; if it ever silently returned the full-body box the
// viewmodel would be framed around a torso that is not drawn.
TEST_F(ViewmodelGeometryTest, IndexedBoundsMeasureArmsNotWholeBody) {
    auto* node = makePlayerNode(1, 0, 0.0f);
    ASSERT_NE(node, nullptr);
    ASSERT_TRUE(node->hasValidFirstPersonGeometry());
    auto* mesh = node->getInstanceMesh();
    ASSERT_NE(mesh, nullptr);

    irr::core::aabbox3d<irr::f32> armBox = measureIndexedBounds(mesh);
    irr::core::aabbox3d<irr::f32> fullBox = mesh->getBoundingBox();
    const float armHeight = armBox.MaxEdge.Y - armBox.MinEdge.Y;
    const float fullHeight = fullBox.MaxEdge.Y - fullBox.MinEdge.Y;
    std::cout << "\narm box   [" << armBox.MinEdge.X << "," << armBox.MinEdge.Y
              << "," << armBox.MinEdge.Z << "]..[" << armBox.MaxEdge.X << ","
              << armBox.MaxEdge.Y << "," << armBox.MaxEdge.Z << "]\n"
              << "full box  [" << fullBox.MinEdge.X << "," << fullBox.MinEdge.Y
              << "," << fullBox.MinEdge.Z << "]..[" << fullBox.MaxEdge.X << ","
              << fullBox.MaxEdge.Y << "," << fullBox.MaxEdge.Z << "]\n";
    ASSERT_GT(armHeight, 0.0f);
    EXPECT_LT(armHeight, fullHeight * 0.75f)
        << "the indexed (arm) bounds are nearly as tall as the whole model -- "
           "the arm filter or the measurement is not doing its job";
}

// THE point of the unit. T76 measured the in-world arms spanning camera-space
// Z=[-1.06, 1.11] (idle) and [-1.02, 2.18] (1H slash) with the eye at Z=0 --
// partly BEHIND the camera, which no camera position can fix. After the
// viewmodel placement every arm vertex must be strictly in front of the
// pass's near plane, in every pose, for every race.
TEST_F(ViewmodelGeometryTest, PlacedArmGeometryIsEntirelyInFrontOfTheNearPlane) {
    struct RaceCase { uint16_t race; uint8_t gender; const char* label; };
    const RaceCase races[] = {
        {1, 0, "Human male"}, {1, 1, "Human female"}, {2, 0, "Barbarian male"},
        {6, 1, "Dark Elf female"}, {9, 0, "Troll male"}, {128, 0, "Iksar male"},
    };
    const char* anims[] = {"l01", "c01", "c02", "d01"};
    ViewmodelTuning tuning;
    int covered = 0;

    std::cout << "\nrace                | anim | placed arm box in viewmodel view space\n";
    for (const auto& rc : races) {
        auto* node = makePlayerNode(rc.race, rc.gender, 41.0f);
        if (!node) continue;
        if (!node->hasValidFirstPersonGeometry()) { node->remove(); continue; }
        for (const char* anim : anims) {
            SCOPED_TRACE(std::string(rc.label) + "/" + anim);
            node->playAnimation(anim, true);
            for (int f = 1; f <= 6; ++f) node->OnAnimate(200 + f * 60);
            irr::core::aabbox3d<irr::f32> box = measureIndexedBounds(node->getInstanceMesh());
            if (box.MaxEdge == box.MinEdge) continue;
            ViewmodelPlacement p = computeViewmodelPlacement(
                box, node->getRotation().Y, kFovY, kAspect, tuning);
            ASSERT_TRUE(p.valid);
            std::cout << "  " << rc.label << " | " << anim << " | Z=["
                      << p.placedArmBox.MinEdge.Z << "," << p.placedArmBox.MaxEdge.Z
                      << "] Y=[" << p.placedArmBox.MinEdge.Y << ","
                      << p.placedArmBox.MaxEdge.Y << "]\n";
            EXPECT_GT(p.placedArmBox.MinEdge.Z, tuning.nearPlane)
                << rc.label << "/" << anim
                << ": arm geometry still straddles the near plane -- this is "
                   "exactly the T71/T74/T75/T76 failure";
            EXPECT_GE(p.placedArmBox.MinEdge.Z,
                      tuning.nearPlane + tuning.minClearance - 1e-3f);
            ++covered;
        }
        node->remove();
    }
    EXPECT_GE(covered, 4) << "no race/pose combination was actually measured";
}

// Stage-1 acceptance, headless half: the placed arms must land inside the
// viewmodel frustum, i.e. actually be ON SCREEN, not merely in front of the
// near plane. Projects the placed box's corners and asserts the box covers a
// real fraction of the frame.
TEST_F(ViewmodelGeometryTest, PlacedArmGeometryProjectsIntoTheFrame) {
    auto* node = makePlayerNode(1, 0, 41.0f);
    ASSERT_NE(node, nullptr);
    ASSERT_TRUE(node->hasValidFirstPersonGeometry());
    irr::core::aabbox3d<irr::f32> box = measureIndexedBounds(node->getInstanceMesh());
    ViewmodelTuning tuning;
    ViewmodelPlacement p = computeViewmodelPlacement(
        box, node->getRotation().Y, kFovY, kAspect, tuning);
    ASSERT_TRUE(p.valid);

    irr::core::vector3df corners[8];
    p.placedArmBox.getEdges(corners);
    float minX = 1e9f, maxX = -1e9f, minY = 1e9f, maxY = -1e9f;
    for (const auto& c : corners) {
        // view is the identity, so the corner IS the view-space point.
        irr::core::vector3df ndc = c;
        p.projection.transformVect(ndc);
        // transformVect drops the perspective divide; do it explicitly.
        float w = p.projection[3] * c.X + p.projection[7] * c.Y +
                  p.projection[11] * c.Z + p.projection[15];
        ASSERT_GT(w, 0.0f) << "corner behind the viewmodel eye";
        minX = std::min(minX, ndc.X / w); maxX = std::max(maxX, ndc.X / w);
        minY = std::min(minY, ndc.Y / w); maxY = std::max(maxY, ndc.Y / w);
    }
    std::cout << "\nplaced arm box in NDC: X=[" << minX << "," << maxX
              << "] Y=[" << minY << "," << maxY << "]\n";
    // Fits INSIDE the frame -- the arms must be seen whole, not spilling
    // off every edge as an unidentifiable wall (the T75/T76 failure mode).
    EXPECT_GE(minX, -1.0f); EXPECT_LE(maxX, 1.0f);
    EXPECT_GE(minY, -1.0f); EXPECT_LE(maxY, 1.0f);
    // ...and covers a real part of it, rather than a speck in a corner.
    const float coverX = std::min(maxX, 1.0f) - std::max(minX, -1.0f);
    const float coverY = std::min(maxY, 1.0f) - std::max(minY, -1.0f);
    EXPECT_GT(coverX * coverY, 0.5f)
        << "the arms cover " << (coverX * coverY)
        << " of the 4.0-unit NDC frame area -- too small to be identifiable";
}

// The pass must emit draw calls when first person is on and none when it is
// off. Exercised through the real draw-list builder (no GPU needed).
TEST_F(ViewmodelGeometryTest, DrawListIsEmptyWithoutValidPlacement) {
    EQT::Graphics::EntityRenderer::ViewmodelParts parts;
    std::vector<EQT::Graphics::ViewmodelDrawItem> items;
    ViewmodelPlacement invalid;
    EXPECT_EQ(EQT::Graphics::buildViewmodelDrawList(parts, invalid, items), 0u);
    EXPECT_TRUE(items.empty());

    auto* node = makePlayerNode(1, 0, 0.0f);
    ASSERT_NE(node, nullptr);
    parts.animatedNode = node;
    // A valid node but an invalid placement (the "not in first person" case)
    // must still draw nothing.
    EXPECT_EQ(EQT::Graphics::buildViewmodelDrawList(parts, invalid, items), 0u);
}

TEST_F(ViewmodelGeometryTest, DrawListCoversEveryArmMeshBuffer) {
    auto* node = makePlayerNode(1, 0, 41.0f);
    ASSERT_NE(node, nullptr);
    ASSERT_TRUE(node->hasValidFirstPersonGeometry());
    irr::core::aabbox3d<irr::f32> box = measureIndexedBounds(node->getInstanceMesh());
    ViewmodelPlacement p = computeViewmodelPlacement(
        box, node->getRotation().Y, kFovY, kAspect, ViewmodelTuning());
    ASSERT_TRUE(p.valid);

    EQT::Graphics::EntityRenderer::ViewmodelParts parts;
    parts.animatedNode = node;
    std::vector<EQT::Graphics::ViewmodelDrawItem> items;
    size_t n = EQT::Graphics::buildViewmodelDrawList(parts, p, items);
    EXPECT_GT(n, 0u) << "the viewmodel pass would emit no draw calls at all";

    size_t nonEmptyBuffers = 0;
    auto* mesh = node->getInstanceMesh();
    for (irr::u32 b = 0; b < mesh->getMeshBufferCount(); ++b)
        if (mesh->getMeshBuffer(b)->getIndexCount() > 0) ++nonEmptyBuffers;
    EXPECT_EQ(n, nonEmptyBuffers);
    for (const auto& item : items) {
        EXPECT_NE(item.buffer, nullptr);
        EXPECT_TRUE(item.material.ZWriteEnable)
            << "viewmodel draws must write depth so a weapon is occluded by "
               "the hand holding it";
    }
}

// The node stays VISIBLE while deferred: Irrlicht skips OnAnimate() on
// invisible nodes, so hiding the player node to keep it out of drawAll()
// would freeze the arms at bind pose -- a subtle way to ship still arms.
TEST_F(ViewmodelGeometryTest, DeferralKeepsTheNodeVisibleSoItKeepsAnimating) {
    auto* node = makePlayerNode(1, 0, 0.0f);
    ASSERT_NE(node, nullptr);
    node->setDeferredToViewmodelPass(true);
    EXPECT_TRUE(node->isDeferredToViewmodelPass());
    EXPECT_TRUE(node->isVisible())
        << "deferral must not hide the node -- an invisible node gets no "
           "OnAnimate() and the arms would stop animating";

    node->playAnimation("l01", true);
    node->OnAnimate(400);
    irr::core::aabbox3d<irr::f32> before = measureIndexedBounds(node->getInstanceMesh());
    for (int f = 1; f <= 10; ++f) node->OnAnimate(400 + f * 80);
    irr::core::aabbox3d<irr::f32> after = measureIndexedBounds(node->getInstanceMesh());
    EXPECT_NE(before.MinEdge, after.MinEdge)
        << "arm geometry did not move across 10 animation steps while deferred";
}
#endif
