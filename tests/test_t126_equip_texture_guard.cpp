/**
 * T126: regression coverage for the equip-node UAF holder named by this
 * unit's write-time return-address probe (refactor/T126_uaf_attribution.md).
 *
 * Root cause: EntityRenderer::attachEquipment() (entity_equipment.cpp)
 * attaches weapon/shield meshes as real irr::scene::IMeshSceneNode children
 * (EntityVisual::primaryEquipNode / secondaryEquipNode), built from
 * EquipmentModelLoader::meshCache_ -- a PERSISTENT cache whose IMesh*
 * objects survive zone unload/reload (refcounted via addMeshRef/
 * removeMeshRef, not rebuilt per zone the way object/region meshes are).
 * Two independent, uncovered gaps existed here, both closed by this unit:
 *
 *  1. EntityMeshTextureGuard (P17) scanned visual.animatedNode's own
 *     materials and its getInstanceMesh() buffers, but NEVER
 *     primaryEquipNode/secondaryEquipNode -- despite T123's holder-inventory
 *     table claiming "equipment" was covered under "Entity/character +
 *     equipment". A currently-attached, normally-rendered equip node's
 *     material was left dangling after eviction forever.
 *  2. EquipmentModelLoader itself was not a TextureEvictionListener at all
 *     -- its cached IMesh's own buffer material was never fixed, so any
 *     FUTURE addMeshSceneNode(mesh, ...) call from that same cache entry
 *     (a re-equip, a different entity wearing the same item, a later zone)
 *     would immediately construct a brand-new node inheriting the dangling
 *     pointer, regardless of what (1) does for already-existing nodes.
 *
 * This unit's write-time return-address probe (8-deep
 * __builtin_return_address chain recorded at every cacheTexParams() call,
 * symbolised offline against the exact core that produced it) found 118 of
 * 118 UAF TEXPARAMS writes across a 10-run acceptance gate resolving to the
 * IDENTICAL call chain applyMaterialRenderState() <- applyCustomShaderState()
 * <- applyMaterialState() <- setMaterial() <- CMeshSceneNode::render()+1122
 * <- CSceneManager::drawAll() -- i.e. a single dominant holder, a live,
 * normally-attached IMeshSceneNode, not any of the detached/hidden-node
 * shapes T123/T124's guards were built for.
 *
 * Both tests below exercise the REAL destruction path
 * (ConstrainedTextureCache::clear(), the exact hook IrrlichtRenderer::
 * unloadZone() calls) against real Irrlicht-populated materials, mirroring
 * test_object_node_texture_guard.cpp's discipline exactly.
 *
 * EDT_NULL device (no window, no GPU, no DISPLAY) -- same idiom
 * test_object_node_texture_guard.cpp / test_constrained_texture_cache.cpp
 * use.
 */

#include <gtest/gtest.h>
#include <map>
#include <memory>

#ifdef EQT_HAS_GRAPHICS
#include "client/graphics/constrained_texture_cache.h"
#include "client/graphics/constrained_renderer_config.h"
#include "client/graphics/renderer_zone_merge.h"
#include "client/graphics/entity_renderer.h"
#include "client/graphics/eq/equipment_model_loader.h"
#include <irrlicht.h>
#endif

#ifdef EQT_HAS_GRAPHICS

using namespace EQT::Graphics;

namespace {

class T126EquipTextureGuardTest : public ::testing::Test {
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

    // Same one-triangle mesh builder test_object_node_texture_guard.cpp uses.
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

// Gap 1: EntityMeshTextureGuard must now swap a LIVE, ATTACHED equip node's
// material -- the exact shape the T126 probe found (drawAll() -> render()
// on a normally-rendered node, not a detached one). Before this unit's fix,
// EntityMeshTextureGuard::onTextureEvicted() never looked at
// primaryEquipNode/secondaryEquipNode at all.
TEST_F(T126EquipTextureGuardTest, EntityMeshGuardSwapsPrimaryEquipNodeMaterial) {
    irr::video::ITexture* tex = driver_->addTexture(irr::core::dimension2d<irr::u32>(2, 2), "t126_equip_primary_tex");
    ASSERT_NE(tex, nullptr);
    ASSERT_TRUE(cache_->registerTexture("t126_equip_primary_tex", tex, 16));

    irr::scene::SMesh* mesh = buildTestMesh(tex);
    irr::scene::ISceneNode* parentNode = smgr_->addEmptySceneNode();
    irr::scene::IMeshSceneNode* equipNode = smgr_->addMeshSceneNode(mesh, parentNode);
    mesh->drop();  // node now owns it
    ASSERT_NE(equipNode, nullptr);
    ASSERT_NE(equipNode->getParent(), nullptr);  // attached, exactly like production (child of the entity node)
    ASSERT_GT(equipNode->getMaterialCount(), 0u);
    ASSERT_EQ(equipNode->getMaterial(0).getTexture(0), tex);

    std::map<uint16_t, EntityVisual> entities;
    EntityVisual visual;
    visual.spawnId = 42;
    visual.primaryEquipNode = equipNode;
    entities[42] = visual;

    irr::video::ITexture* placeholder = cache_->getPlaceholderTexture();
    ASSERT_NE(placeholder, nullptr);
    EntityMeshTextureGuard guard(entities, placeholder);
    cache_->addEvictionListener(&guard);

    // The REAL destruction path -- IrrlichtRenderer::unloadZone()'s own hook.
    cache_->clear();

    EXPECT_EQ(equipNode->getMaterial(0).getTexture(0), placeholder);
    EXPECT_NE(equipNode->getMaterial(0).getTexture(0), tex);

    cache_->removeEvictionListener(&guard);
    parentNode->remove();
}

TEST_F(T126EquipTextureGuardTest, EntityMeshGuardSwapsSecondaryEquipNodeMaterial) {
    irr::video::ITexture* tex = driver_->addTexture(irr::core::dimension2d<irr::u32>(2, 2), "t126_equip_secondary_tex");
    ASSERT_TRUE(cache_->registerTexture("t126_equip_secondary_tex", tex, 16));

    irr::scene::SMesh* mesh = buildTestMesh(tex);
    irr::scene::ISceneNode* parentNode = smgr_->addEmptySceneNode();
    irr::scene::IMeshSceneNode* equipNode = smgr_->addMeshSceneNode(mesh, parentNode);
    mesh->drop();
    ASSERT_NE(equipNode, nullptr);

    std::map<uint16_t, EntityVisual> entities;
    EntityVisual visual;
    visual.spawnId = 7;
    visual.secondaryEquipNode = equipNode;
    entities[7] = visual;

    irr::video::ITexture* placeholder = cache_->getPlaceholderTexture();
    EntityMeshTextureGuard guard(entities, placeholder);
    cache_->addEvictionListener(&guard);

    cache_->clear();

    EXPECT_EQ(equipNode->getMaterial(0).getTexture(0), placeholder);

    cache_->removeEvictionListener(&guard);
    parentNode->remove();
}

// Gap 2: EquipmentModelLoader's own persistent meshCache_ must have its
// cached IMesh's buffer material swapped, so a FUTURE addMeshSceneNode()
// call from this exact cache entry does not immediately reintroduce the
// dangling pointer into a brand-new node's copied Materials array.
TEST_F(T126EquipTextureGuardTest, EquipmentModelLoaderSwapsCachedMeshBufferMaterial) {
    irr::video::ITexture* tex = driver_->addTexture(irr::core::dimension2d<irr::u32>(2, 2), "t126_equip_cache_tex");
    ASSERT_NE(tex, nullptr);
    ASSERT_TRUE(cache_->registerTexture("t126_equip_cache_tex", tex, 16));

    EquipmentModelLoader loader(smgr_, driver_, device_->getFileSystem());
    loader.setConstrainedTextureCache(cache_.get());  // self-registers as an eviction listener

    irr::scene::SMesh* mesh = buildTestMesh(tex);
    ASSERT_EQ(mesh->getMeshBuffer(0)->getMaterial().getTexture(0), tex);
    loader.debugInjectCachedMeshForTest(/*modelId=*/156, mesh);  // meshCache_ now owns the one reference

    // The REAL destruction path.
    cache_->clear();

    irr::scene::IMesh* cached = loader.getEquipmentMeshByModelId(156);
    ASSERT_NE(cached, nullptr);
    irr::video::ITexture* placeholder = cache_->getPlaceholderTexture();
    EXPECT_EQ(cached->getMeshBuffer(0)->getMaterial().getTexture(0), placeholder);
    EXPECT_NE(cached->getMeshBuffer(0)->getMaterial().getTexture(0), tex);

    // A brand-new node built from this SAME cache entry, AFTER the eviction,
    // must NOT inherit the dangling texture pointer -- this is exactly the
    // "future re-equip" scenario the fix exists for.
    irr::scene::IMeshSceneNode* freshNode = smgr_->addMeshSceneNode(cached);
    ASSERT_NE(freshNode, nullptr);
    ASSERT_GT(freshNode->getMaterialCount(), 0u);
    EXPECT_EQ(freshNode->getMaterial(0).getTexture(0), placeholder);
    freshNode->remove();
}

#else

TEST(T126EquipTextureGuardTest, SkippedNoGraphics) {
    GTEST_SKIP() << "EQT_HAS_GRAPHICS not compiled in";
}

#endif
