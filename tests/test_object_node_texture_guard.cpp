/**
 * T123: regression coverage for ObjectNodeTextureGuard
 * (renderer_zone_merge.h/.cpp), the fix for the confirmed use-after-free
 * root-caused by this unit.
 *
 * Root cause (see refactor/T123_texture_holder_audit.md Review for the full
 * forensic chain): IrrlichtRenderer::applySimulationOutput()
 * (renderer_sim_bridge.cpp) calls objectNodes_[i]->remove() whenever an
 * object becomes invisible, detaching it from the scene graph while the
 * renderer keeps the raw irr::scene::IMeshSceneNode* alive in objectNodes_.
 * Irrlicht's own CMeshSceneNode (what addMeshSceneNode() actually
 * constructs, with the default ReadOnlyMaterials=false) copies the mesh's
 * buffer materials into its OWN core::array<SMaterial> Materials member at
 * set-mesh time, and render() reads THAT copy — via node->getMaterial(i) —
 * not the mesh buffer's own material (see
 * third_party/irrlicht-gles2-x86/source/Irrlicht/CMeshSceneNode.cpp:162).
 * ConstrainedTextureCache::clearTextureReferences() only reaches nodes
 * reachable from smgr_->getRootSceneNode(); a detached object node's
 * Materials array is invisible to it. A texture destroyed by clear() while
 * the object happens to be detached leaves that Materials array holding a
 * dangling ITexture* forever — until the object is made visible again and
 * rendered, at which point COpenGLES2Driver::applyMaterialRenderState()
 * dereferences it (cacheTexParams() UAF).
 *
 * This test exercises the REAL destruction path
 * (ConstrainedTextureCache::clear(), the exact zone-unload hook
 * IrrlichtRenderer::unloadZone() calls) against a detached node with a real
 * Irrlicht-populated Materials array (via addMeshSceneNode(), not a hand-
 * built fake), and asserts the dangling reference is gone afterward — not a
 * direct/synthetic call to the listener interface.
 *
 * EDT_NULL device (no window, no GPU, no DISPLAY) — same idiom
 * test_constrained_texture_cache.cpp / test_inventory_model_view.cpp use.
 */

#include <gtest/gtest.h>
#include <memory>

#ifdef EQT_HAS_GRAPHICS
#include "client/graphics/constrained_texture_cache.h"
#include "client/graphics/constrained_renderer_config.h"
#include "client/graphics/placed_object_meshes.h"
#include "client/graphics/renderer_zone_merge.h"
#include <irrlicht.h>
#endif

#ifdef EQT_HAS_GRAPHICS

using namespace EQT::Graphics;

namespace {

class ObjectNodeTextureGuardTest : public ::testing::Test {
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

    // Builds a real one-triangle mesh with the given material texture set on
    // its (sole) buffer.
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
        buffer->drop();  // mesh now owns it
        mesh->recalculateBoundingBox();
        return mesh;
    }
};

}  // namespace

// The core regression: a DETACHED object placement's node-level Materials
// copy (populated by Irrlicht's own addMeshSceneNode()/copyMaterials(), the
// exact array the scene-graph pass reads) must not be left holding a
// dangling texture pointer after ConstrainedTextureCache::clear() — the real
// zone-unload path — destroys that texture, PROVIDED ObjectNodeTextureGuard
// is registered as an eviction listener the way irrlicht_renderer.cpp wires
// it up in production. B938 (#256): the guard now scans a
// PlacedObjectRecord (record.node covers this path; record.mesh covers the
// PVS manual-draw path, asserted in the second test).
TEST_F(ObjectNodeTextureGuardTest, ClearSwapsDetachedNodeMaterialToPlaceholder) {
    // Real texture, registered into the cache like any zone/object texture.
    irr::video::ITexture* tex = driver_->addTexture(irr::core::dimension2d<irr::u32>(2, 2), "t123_obj_tex");
    ASSERT_NE(tex, nullptr);
    ASSERT_TRUE(cache_->registerTexture("t123_obj_tex", tex, 16));

    irr::scene::SMesh* mesh = buildTestMesh(tex);
    irr::scene::IMeshSceneNode* node = smgr_->addMeshSceneNode(mesh);
    mesh->drop();  // node now owns it
    ASSERT_NE(node, nullptr);
    // Mirror production's non-PVS record (renderer_zone_load_objects.cpp
    // step 6: the record grabs its mesh ref where needsOwnRef, and grabs the
    // node unconditionally -- `objNode->grab(); record.node = objNode;`):
    // grab() both BEFORE any remove(), since remove() drops the scene
    // graph's own reference and would otherwise destroy the node outright
    // (matching zone teardown's corresponding drops).
    mesh->grab();  // the record's mesh reference (needsOwnRef false in
                   // non-PVS -- see below: here we model the record holding
                   // it so the guard's mesh-buffer scan is also exercised)
    node->grab();  // the record's node reference (step 6 grab)

    // Sanity: Irrlicht's default ReadOnlyMaterials=false means the node's
    // OWN Materials copy — not the mesh buffer's material — is what the
    // scene-graph pass actually reads. Confirm the copy exists and holds our
    // texture before testing the fix.
    ASSERT_GT(node->getMaterialCount(), 0u);
    ASSERT_EQ(node->getMaterial(0).getTexture(0), tex);

    // Simulate applySimulationOutput()'s detach-on-invisible path exactly:
    // node->remove() detaches from the scene graph but the renderer (here,
    // the record standing in for objectRecords_[i]) keeps the pointer alive
    // via the grab() above.
    node->remove();
    ASSERT_EQ(node->getParent(), nullptr);

    // Wire up the fix exactly like irrlicht_renderer.cpp does: a records
    // vector standing in for objectRecords_, a guard bound to it, registered
    // as an eviction listener.
    PlacedObjectRecord rec;
    rec.mesh = mesh;
    rec.node = node;
    rec.inSceneGraph = false;
    std::vector<PlacedObjectRecord> objectRecords{rec};
    irr::video::ITexture* placeholder = cache_->getPlaceholderTexture();
    ASSERT_NE(placeholder, nullptr);
    ASSERT_NE(placeholder, tex);
    ObjectNodeTextureGuard guard(objectRecords, placeholder);
    cache_->addEvictionListener(&guard);

    // The REAL destruction path — not a direct/synthetic call to the
    // listener. This is exactly what IrrlichtRenderer::unloadZone() calls.
    cache_->clear();

    // The detached node's Materials copy must now point at the placeholder,
    // not the destroyed texture — proving the guard covers node-level
    // override materials for nodes unreachable from the scene-graph root.
    EXPECT_EQ(node->getMaterial(0).getTexture(0), placeholder);
    EXPECT_NE(node->getMaterial(0).getTexture(0), tex);
    // B938: the shared mesh's buffer material (what the PVS manual draw path
    // binds) is covered by the same scan.
    EXPECT_EQ(mesh->getMeshBuffer(0)->getMaterial().getTexture(0), placeholder);

    cache_->removeEvictionListener(&guard);
    node->drop();
    mesh->drop();  // release the record's reference
}

// B938 (#256): the PVS shape — the guard's primary audience. In every PVS
// zone (all gate zones) the record carries NO node; the live draw path binds
// the shared mesh's buffer materials, so an evicted texture dangling in a
// mesh buffer material of a zone whose placements are all node-less must be
// swapped to the placeholder. Pre-B938 the guard only scanned nodes and
// would have missed this entirely.
TEST_F(ObjectNodeTextureGuardTest, ClearSwapsNodelessRecordMeshMaterialToPlaceholder) {
    irr::video::ITexture* tex = driver_->addTexture(irr::core::dimension2d<irr::u32>(2, 2), "t123_obj_tex_pvs");
    ASSERT_NE(tex, nullptr);
    ASSERT_TRUE(cache_->registerTexture("t123_obj_tex_pvs", tex, 16));

    irr::scene::SMesh* mesh = buildTestMesh(tex);  // the record holds its ref
    ASSERT_NE(mesh, nullptr);
    ASSERT_EQ(mesh->getMeshBuffer(0)->getMaterial().getTexture(0), tex);

    PlacedObjectRecord rec;
    rec.mesh = mesh;
    rec.node = nullptr;  // PVS zone: no per-placement node
    std::vector<PlacedObjectRecord> objectRecords{rec};
    irr::video::ITexture* placeholder = cache_->getPlaceholderTexture();
    ASSERT_NE(placeholder, nullptr);
    ObjectNodeTextureGuard guard(objectRecords, placeholder);
    cache_->addEvictionListener(&guard);

    cache_->clear();

    EXPECT_EQ(mesh->getMeshBuffer(0)->getMaterial().getTexture(0), placeholder);

    cache_->removeEvictionListener(&guard);
    mesh->drop();
}

// Control: an ATTACHED object node is already covered by
// ConstrainedTextureCache::clearTextureReferences()'s scene-graph walk
// (pre-existing behavior) — confirms the guard doesn't change that, and
// that the fixture itself is sound (same setup, without the detach step).
TEST_F(ObjectNodeTextureGuardTest, ClearSwapsAttachedNodeMaterialToPlaceholderToo) {
    irr::video::ITexture* tex = driver_->addTexture(irr::core::dimension2d<irr::u32>(2, 2), "t123_obj_tex_attached");
    ASSERT_NE(tex, nullptr);
    ASSERT_TRUE(cache_->registerTexture("t123_obj_tex_attached", tex, 16));

    irr::scene::SMesh* mesh = buildTestMesh(tex);
    irr::scene::IMeshSceneNode* node = smgr_->addMeshSceneNode(mesh);
    mesh->drop();  // node now owns it
    ASSERT_NE(node, nullptr);
    ASSERT_NE(node->getParent(), nullptr);  // still attached
    mesh->grab();  // the record's mesh reference (modeled held, as in PVS)
    node->grab();  // the record's node reference (step 6 grab)

    PlacedObjectRecord rec;
    rec.mesh = mesh;
    rec.node = node;
    rec.inSceneGraph = true;
    std::vector<PlacedObjectRecord> objectRecords{rec};
    irr::video::ITexture* placeholder = cache_->getPlaceholderTexture();
    ObjectNodeTextureGuard guard(objectRecords, placeholder);
    cache_->addEvictionListener(&guard);

    cache_->clear();

    EXPECT_EQ(node->getMaterial(0).getTexture(0), placeholder);

    cache_->removeEvictionListener(&guard);
    node->remove();
    node->drop();
    mesh->drop();
}

#else

TEST(ObjectNodeTextureGuardTest, SkippedNoGraphics) {
    GTEST_SKIP() << "EQT_HAS_GRAPHICS not compiled in";
}

#endif
