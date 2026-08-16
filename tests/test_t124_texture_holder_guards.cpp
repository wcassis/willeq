/**
 * T124: regression coverage for the shared eviction-swap primitives
 * (swapTextureInMaterial/swapTextureInMesh/swapTextureInNodeMaterials/
 * swapTextureInMeshSceneNode, renderer_zone_merge.h) and the new guards
 * built on top of them -- DoorTextureGuard (doors_ sibling of T123's
 * ObjectNodeTextureGuard) and the singleton-subsystem holders
 * (SpellVisualFX::swapEvictedTexture, BoidsManager::swapEvictedTexture).
 *
 * Follows test_object_node_texture_guard.cpp's shape exactly: drives the
 * REAL ConstrainedTextureCache::clear() destruction path (or, for the
 * shared primitives, calls them directly against real Irrlicht-populated
 * materials) rather than a synthetic call to the listener interface.
 *
 * EDT_NULL device (no window, no GPU, no DISPLAY) -- same idiom every
 * other guard test in this suite uses.
 */

#include <gtest/gtest.h>
#include <memory>

#ifdef EQT_HAS_GRAPHICS
#include "client/graphics/constrained_texture_cache.h"
#include "client/graphics/constrained_renderer_config.h"
#include "client/graphics/renderer_zone_merge.h"
#include "client/graphics/door_manager.h"
#include "client/graphics/spell_visual_fx.h"
#include "client/graphics/environment/boids_manager.h"
#include <irrlicht.h>
#endif

#ifdef EQT_HAS_GRAPHICS

using namespace EQT::Graphics;

namespace {

class T124GuardsTest : public ::testing::Test {
protected:
    irr::IrrlichtDevice* device_ = nullptr;
    irr::video::IVideoDriver* driver_ = nullptr;
    irr::scene::ISceneManager* smgr_ = nullptr;
    std::unique_ptr<ConstrainedTextureCache> cache_;

    void SetUp() override {
        irr::SIrrlichtCreationParameters params;
        params.DriverType = irr::video::EDT_NULL;
        params.WindowSize = irr::core::dimension2d<irr::u32>(64, 64);
        device_ = irr::createDeviceEx(params);
        if (!device_) {
            GTEST_SKIP() << "Failed to create EDT_NULL Irrlicht device";
        }
        driver_ = device_->getVideoDriver();
        smgr_ = device_->getSceneManager();

        auto config = ConstrainedRendererConfig::fromPreset(ConstrainedRenderingPreset::TNT);
        cache_ = std::make_unique<ConstrainedTextureCache>(config);
        cache_->setDriver(driver_);
        cache_->setSceneManager(smgr_);
    }

    void TearDown() override {
        cache_.reset();
        if (device_) {
            device_->drop();
            device_ = nullptr;
        }
    }

    irr::scene::SMesh* buildTestMesh(irr::video::ITexture* tex) {
        auto* mesh = new irr::scene::SMesh();
        auto* buffer = new irr::scene::SMeshBuffer();
        buffer->Vertices.set_used(3);
        buffer->Vertices[0] = irr::video::S3DVertex(0, 0, 0, 0, 0, 1, irr::video::SColor(255, 255, 255, 255), 0, 0);
        buffer->Vertices[1] = irr::video::S3DVertex(1, 0, 0, 0, 0, 1, irr::video::SColor(255, 255, 255, 255), 1, 0);
        buffer->Vertices[2] = irr::video::S3DVertex(0, 1, 0, 0, 0, 1, irr::video::SColor(255, 255, 255, 255), 0, 1);
        buffer->Indices.set_used(3);
        buffer->Indices[0] = 0;
        buffer->Indices[1] = 1;
        buffer->Indices[2] = 2;
        buffer->getMaterial().setTexture(0, tex);
        mesh->addMeshBuffer(buffer);
        buffer->drop();
        mesh->recalculateBoundingBox();
        return mesh;
    }
};

}  // namespace

// ---------------------------------------------------------------------------
// Shared primitives -- direct unit coverage (the "one loop" every guard
// above composes).
// ---------------------------------------------------------------------------

TEST_F(T124GuardsTest, SwapTextureInMaterialSwapsMatchingSlotOnly) {
    irr::video::ITexture* texA = driver_->addTexture(irr::core::dimension2d<irr::u32>(2, 2), "t124_a");
    irr::video::ITexture* texB = driver_->addTexture(irr::core::dimension2d<irr::u32>(2, 2), "t124_b");
    irr::video::ITexture* placeholder = driver_->addTexture(irr::core::dimension2d<irr::u32>(2, 2), "t124_ph");
    ASSERT_NE(texA, nullptr);
    ASSERT_NE(texB, nullptr);

    irr::video::SMaterial mat;
    mat.setTexture(0, texA);
    mat.setTexture(1, texB);

    int swapped = swapTextureInMaterial(mat, texA, placeholder);
    EXPECT_EQ(swapped, 1);
    EXPECT_EQ(mat.getTexture(0), placeholder);
    EXPECT_EQ(mat.getTexture(1), texB);  // untouched -- didn't match

    // No-op when nothing matches.
    swapped = swapTextureInMaterial(mat, texA, placeholder);
    EXPECT_EQ(swapped, 0);
}

TEST_F(T124GuardsTest, SwapTextureInMeshWalksAllBuffers) {
    irr::video::ITexture* tex = driver_->addTexture(irr::core::dimension2d<irr::u32>(2, 2), "t124_mesh_tex");
    irr::video::ITexture* placeholder = driver_->addTexture(irr::core::dimension2d<irr::u32>(2, 2), "t124_mesh_ph");
    irr::scene::SMesh* mesh = buildTestMesh(tex);

    int swapped = swapTextureInMesh(mesh, tex, placeholder);
    EXPECT_EQ(swapped, 1);
    EXPECT_EQ(mesh->getMeshBuffer(0)->getMaterial().getTexture(0), placeholder);

    // Null mesh is a documented no-op, not a crash.
    EXPECT_EQ(swapTextureInMesh(nullptr, tex, placeholder), 0);

    mesh->drop();
}

TEST_F(T124GuardsTest, SwapTextureInNodeMaterialsCoversOverrideArrayNotMeshBuffer) {
    irr::video::ITexture* tex = driver_->addTexture(irr::core::dimension2d<irr::u32>(2, 2), "t124_node_tex");
    irr::video::ITexture* placeholder = driver_->addTexture(irr::core::dimension2d<irr::u32>(2, 2), "t124_node_ph");
    irr::scene::SMesh* mesh = buildTestMesh(tex);
    irr::scene::IMeshSceneNode* node = smgr_->addMeshSceneNode(mesh);
    mesh->drop();
    ASSERT_NE(node, nullptr);
    ASSERT_EQ(node->getMaterial(0).getTexture(0), tex);  // Irrlicht copies it in at set-mesh time

    int swapped = swapTextureInNodeMaterials(node, tex, placeholder);
    EXPECT_EQ(swapped, 1);
    EXPECT_EQ(node->getMaterial(0).getTexture(0), placeholder);
    // The mesh buffer's OWN material is untouched -- this function only
    // walks the node's override copy, matching what render() actually
    // reads for ReadOnlyMaterials=false (Irrlicht's default).
    EXPECT_EQ(node->getMesh()->getMeshBuffer(0)->getMaterial().getTexture(0), tex);

    node->remove();
}

TEST_F(T124GuardsTest, SwapTextureInMeshSceneNodeCoversBothMeshAndOverrideMaterials) {
    irr::video::ITexture* tex = driver_->addTexture(irr::core::dimension2d<irr::u32>(2, 2), "t124_both_tex");
    irr::video::ITexture* placeholder = driver_->addTexture(irr::core::dimension2d<irr::u32>(2, 2), "t124_both_ph");
    irr::scene::SMesh* mesh = buildTestMesh(tex);
    irr::scene::IMeshSceneNode* node = smgr_->addMeshSceneNode(mesh);
    mesh->drop();

    int swapped = swapTextureInMeshSceneNode(node, tex, placeholder);
    EXPECT_EQ(swapped, 2);  // one mesh-buffer slot + one node-override slot
    EXPECT_EQ(node->getMesh()->getMeshBuffer(0)->getMaterial().getTexture(0), placeholder);
    EXPECT_EQ(node->getMaterial(0).getTexture(0), placeholder);

    // Null node is a documented no-op, not a crash.
    EXPECT_EQ(swapTextureInMeshSceneNode(nullptr, tex, placeholder), 0);

    node->remove();
}

// ---------------------------------------------------------------------------
// DoorTextureGuard -- T124 doors_ sibling of ObjectNodeTextureGuard.
// ---------------------------------------------------------------------------

TEST_F(T124GuardsTest, DoorTextureGuardSwapsDetachedDoorSceneNodeMaterial) {
    irr::video::ITexture* tex = driver_->addTexture(irr::core::dimension2d<irr::u32>(2, 2), "t124_door_tex");
    ASSERT_TRUE(cache_->registerTexture("t124_door_tex", tex, 16));

    irr::scene::SMesh* mesh = buildTestMesh(tex);
    irr::scene::ISceneNode* pivot = smgr_->addEmptySceneNode();
    irr::scene::IMeshSceneNode* sceneNode = smgr_->addMeshSceneNode(mesh, pivot);
    mesh->drop();
    ASSERT_NE(sceneNode, nullptr);
    ASSERT_EQ(sceneNode->getMaterial(0).getTexture(0), tex);

    // Mirror door_manager.cpp's exact grab()-before-detach pattern
    // (clearDoors()/setDoorState() both grab() the pivotNode before any
    // remove() so the renderer's own bookkeeping keeps it alive).
    pivot->grab();
    pivot->remove();
    ASSERT_EQ(pivot->getParent(), nullptr);

    DoorVisual visual;
    visual.pivotNode = pivot;
    visual.sceneNode = sceneNode;
    std::map<uint8_t, DoorVisual> doors;
    doors[1] = visual;

    irr::video::ITexture* placeholder = cache_->getPlaceholderTexture();
    ASSERT_NE(placeholder, nullptr);
    DoorTextureGuard guard(doors, placeholder);
    cache_->addEvictionListener(&guard);

    cache_->clear();  // the real destruction path, exactly as unloadZone() calls it

    EXPECT_EQ(sceneNode->getMaterial(0).getTexture(0), placeholder);
    EXPECT_NE(sceneNode->getMaterial(0).getTexture(0), tex);

    cache_->removeEvictionListener(&guard);
    pivot->drop();
}

// ---------------------------------------------------------------------------
// SpellVisualFX::swapEvictedTexture -- T124.
// ---------------------------------------------------------------------------

// NOTE on coverage: SpellVisualFX::loadParticleTextures() (the only code
// that ever populates the 8 members swapEvictedTexture() protects) is
// called from the constructor only under `#ifndef EQT_HAS_GLES2` -- this
// build (like every shipped build since P32, the only supported renderer)
// defines EQT_HAS_GLES2, so a plain-constructed SpellVisualFX here has all
// 8 members permanently null, exactly as it does in production (see
// refactor/T124_remaining_texture_holders.md Review for the full finding:
// this makes the guard currently a no-op in production, kept anyway as
// cheap, correct, future-proofing against that dead code being revived).
// That leaves nothing to exercise a genuine positive-match swap without
// reaching into a private, unreachable-from-tests code path -- this test
// therefore only proves getTextureForSpell()'s no-database default (0) and
// swapEvictedTexture()'s no-crash/no-match and null-`from` branches, which
// is exactly what production actually exercises today. The swap logic
// itself (pointer compare over the 8-slot list) is the same shape already
// proven correct by SwapTextureInMaterialSwapsMatchingSlotOnly above.
TEST_F(T124GuardsTest, SpellVisualFXGetTextureForSpellDefaultsNullOnGLES2) {
    EQ::SpellVisualFX fx(smgr_, driver_, "");
    EXPECT_EQ(fx.getTextureForSpell(0), nullptr);

    irr::video::ITexture* someTex = driver_->addTexture(irr::core::dimension2d<irr::u32>(2, 2), "t124_spellfx_tex");
    irr::video::ITexture* placeholder = driver_->addTexture(irr::core::dimension2d<irr::u32>(2, 2), "t124_spellfx_ph");
    fx.swapEvictedTexture(someTex, placeholder);   // no-match path: nothing to swap
    fx.swapEvictedTexture(nullptr, placeholder);   // documented no-op
    SUCCEED();
}

// ---------------------------------------------------------------------------
// BoidsManager::swapEvictedTexture -- T124.
// ---------------------------------------------------------------------------

// Deliberately does NOT call BoidsManager::init() -- init() reads a
// hard-coded relative "data/textures/creature_atlas.bmp" path and, on
// failure, calls std::exit(1) directly (LOG_FATAL + exit, not an
// exception/return-false the test could catch). gtest_discover_tests()
// runs with WORKING_DIRECTORY = <build>/tests (see this file's CMakeLists.txt
// comment), where that relative path does not resolve -- calling init()
// here would kill the entire test BINARY, not just this test case, which
// is a far worse failure mode than a slightly weaker test. This exercises
// swapEvictedTexture()'s no-match path only (the overwhelmingly common
// case: an eviction of a texture this instance never touched); the actual
// swap logic (pointer compare + swapTextureInMaterial) is covered directly
// by SwapTextureInMaterialSwapsMatchingSlotOnly above and BoidsManager's
// own implementation is a two-line composition of exactly that primitive
// plus a bare pointer compare -- see refactor/T124_remaining_texture_holders.md
// Review for this as a flagged, deliberate test-coverage gap, not an
// oversight.
TEST_F(T124GuardsTest, BoidsManagerSwapEvictedTextureNoCrashUninitialized) {
    Environment::BoidsManager boids(smgr_, driver_);
    boids.setConstrainedTextureCache(cache_.get());

    irr::video::ITexture* fakeTex = driver_->addTexture(irr::core::dimension2d<irr::u32>(2, 2), "t124_boids_fake");
    irr::video::ITexture* placeholder = driver_->addTexture(irr::core::dimension2d<irr::u32>(2, 2), "t124_boids_ph");
    boids.swapEvictedTexture(fakeTex, placeholder);
    boids.swapEvictedTexture(nullptr, placeholder);
    SUCCEED();
}

#else

TEST(T124GuardsTest, SkippedNoGraphics) {
    GTEST_SKIP() << "EQT_HAS_GRAPHICS not compiled in";
}

#endif
