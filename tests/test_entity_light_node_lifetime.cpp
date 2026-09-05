/**
 * T391 (board B202) -- the two-client render-thread SIGSEGV.
 *
 * THE DEFECT. `EntityVisual::lightNode` is an `ILightSceneNode` created by
 * `EntityRenderer::setEntityLight()` with `addLightSceneNode(nullptr, ...)`.
 * A null parent means Irrlicht parents the node to the ROOT scene node, and
 * `addLightSceneNode()` drops its construction reference before returning, so
 * the node comes back with a reference count of ONE, held by its parent and
 * by nobody else. `EntityRenderer` then keeps a raw pointer to it.
 *
 * `SimBridgeComponent::applySimulationOutput()` detaches and re-attaches that
 * node every time the entity leaves and re-enters the worker's visible-entity
 * set:
 *
 *     } else if (!er.shouldBeVisible && visual.inSceneGraph) {
 *         visual.sceneNode->remove();
 *         visual.inSceneGraph = false;
 *         if (visual.lightNode) visual.lightNode->remove();   // <-- FREES IT
 *     }
 *
 * `ISceneNode::remove()` is `Parent->removeChild(this)`, and `removeChild()`
 * drops. With a reference count of one the node is DELETED there, while
 * `visual.lightNode` keeps pointing at the freed object -- so the next frame
 * that re-admits the entity runs
 * `smgr->getRootSceneNode()->addChild(visual.lightNode)` on freed memory, on
 * the render thread, inside `applySimulationOutput()`. That is B202's
 * backtrace exactly.
 *
 * WHY IT LOOKED LIKE "a second player kills the client". In the T386 session
 * that produced the backtrace, the whole 206-entity qeynos2 population built
 * exactly ONE entity light node, and it belonged to the other player:
 *   [ENTITY] Created light for entity 286 (Testbuddy): level=11, radius=92.0
 * (Testbuddy carries a Gloomingdeep Lantern, items.light = 11). No NPC in
 * that zone carries a light, so a single-client session never has a lightNode
 * to free and runs clean forever.
 *
 * THE INVARIANT PINNED HERE. A scene node the renderer keeps a raw pointer to
 * across a `remove()`/`addChild()` cycle must be owned by an explicit
 * `grab()` -- the node must survive with no parent. This is exactly what the
 * object-light path already does (`renderer_zone_load_objects.cpp`:
 * `lightNode->grab(); lightNode->remove();`) and what the entity's own
 * `sceneNode` already does (`entity_lifecycle.cpp` drops it on removal). The
 * entity's lightNode was the one node that did neither.
 *
 * Each test takes its OWN grab() first, so nothing here dereferences freed
 * memory even against the pre-fix code: the assertions fail on the reference
 * COUNT, deterministically, in a plain (non-sanitizer) build.
 */

#include <gtest/gtest.h>

#include <cstdlib>
#include <fstream>
#include <memory>
#include <string>

#include <irrlicht.h>

#include "client/graphics/constrained_renderer_config.h"
#include "client/graphics/constrained_texture_cache.h"
#include "client/graphics/entity_renderer.h"
#include "client/graphics/irrlicht_renderer.h"
#include "client/graphics/sim/sim_bridge_component.h"
#include "client/graphics/simulation_worker.h"
#include "client/willeq_database.h"

namespace {

constexpr uint16_t kSpawn = 4242;
constexpr uint16_t kHumanMale = 1;
constexpr uint8_t kLanternLightLevel = 11;   // items.light of the Gloomingdeep Lantern

class EntityLightNodeLifetimeTest : public ::testing::Test {
protected:
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
            GTEST_SKIP() << "no client data at " << eqClientPath_;

        irr::SIrrlichtCreationParameters params;
        params.DriverType = irr::video::EDT_NULL;
        params.WindowSize = irr::core::dimension2d<irr::u32>(64, 64);
        device_ = irr::createDeviceEx(params);
        if (!device_) GTEST_SKIP() << "no EDT_NULL device";
        smgr_ = device_->getSceneManager();

        auto entities = std::make_unique<EQT::Graphics::EntityRenderer>(
            smgr_, device_->getVideoDriver(), device_->getFileSystem());
        entities->setClientPath(eqClientPath_);
        auto config = EQT::Graphics::ConstrainedRendererConfig::fromPreset(
            EQT::Graphics::ConstrainedRenderingPreset::TNT);
        textureCache_ = std::make_unique<EQT::Graphics::ConstrainedTextureCache>(config);
        textureCache_->setDriver(device_->getVideoDriver());
        entities->setConstrainedTextureCache(textureCache_.get());
        if (!entities->loadGlobalCharacters()) GTEST_SKIP() << "global_chr.s3d would not load";

        // The apply seam runs on a real (never Init()'d) IrrlichtRenderer, the
        // same idiom test_sim_bridge_component.cpp uses; only the two context
        // members applySimulationOutput()'s entity block touches are wired.
        renderer_ = std::make_unique<EQT::Graphics::IrrlichtRenderer>();
        renderer_->Context().smgr = smgr_;
        renderer_->Context().entityRenderer = std::move(entities);
        entities_ = renderer_->Context().entityRenderer.get();
    }

    void TearDown() override {
        renderer_.reset();
        entities_ = nullptr;
        textureCache_.reset();
        if (device_) { device_->drop(); device_ = nullptr; }
        EQT::WilleqDatabase::setGlobalInstance(nullptr);
    }

    // Build the lit entity the way production does and hand back its light
    // node with a reference of THIS TEST's own, so that no assertion below
    // can touch freed memory even when the production code under test leaks
    // its only reference away.
    irr::scene::ILightSceneNode* makeLitEntity() {
        if (!entities_->createEntity(kSpawn, kHumanMale, "Lanterner", 10.f, 20.f, 3.f, 0.f,
                                     /*isPlayer=*/true, /*gender=*/0,
                                     EQT::Graphics::EntityAppearance(), /*isNPC=*/false,
                                     /*isCorpse=*/false)) {
            return nullptr;
        }
        entities_->setEntityLight(kSpawn, kLanternLightLevel);
        auto it = entities_->getEntities().find(kSpawn);
        if (it == entities_->getEntities().end()) return nullptr;
        irr::scene::ILightSceneNode* light = it->second.lightNode;
        if (light) light->grab();     // the test's own reference
        return light;
    }

    // One frame of the production apply seam: the worker says this entity is
    // (in)visible, and SimBridgeComponent::applySimulationOutput() does the
    // scene-graph add/remove. This is the exact code B202 dies in.
    void applyVisibility(bool visible) {
        EQT::Graphics::SimulationOutput out;
        EQT::Graphics::SimulationOutput::EntityResult er{};
        er.spawnId = kSpawn;
        er.shouldBeVisible = visible;
        er.wasInterpolated = false;
        er.nameTagVisible = visible;
        er.cachedBspRegion = 0;
        er.bspRegionDirty = false;
        out.entityResults.push_back(er);
        out.entityVisibleCount = visible ? 1 : 0;
        renderer_->SimBridge().applySimulationOutput(out);
    }

    std::string eqClientPath_;
    irr::IrrlichtDevice* device_ = nullptr;
    irr::scene::ISceneManager* smgr_ = nullptr;
    std::unique_ptr<EQT::Graphics::ConstrainedTextureCache> textureCache_;
    std::unique_ptr<EQT::Graphics::IrrlichtRenderer> renderer_;
    EQT::Graphics::EntityRenderer* entities_ = nullptr;
    EQT::WilleqDatabase willeqDb_;
};

// The ownership invariant itself, stated once: EntityRenderer holds a
// reference of its own to the light node it stores a raw pointer to.
TEST_F(EntityLightNodeLifetimeTest, EntityRendererOwnsAReferenceToTheLightNode) {
    irr::scene::ILightSceneNode* light = makeLitEntity();
    ASSERT_NE(light, nullptr) << "no light node built for a lit entity";

    // parent (root) + EntityRenderer + this test.
    EXPECT_EQ(light->getReferenceCount(), 3)
        << "EntityRenderer keeps a raw ILightSceneNode* but does not own a "
           "reference to it; the first remove() will free it (B202)";

    light->drop();
}

// The cycle B202 actually dies on, driven through the production apply seam.
TEST_F(EntityLightNodeLifetimeTest, LightNodeSurvivesACullAndUnCullThroughTheApplySeam) {
    irr::scene::ILightSceneNode* light = makeLitEntity();
    ASSERT_NE(light, nullptr);
    ASSERT_TRUE(entities_->getEntities().at(kSpawn).inSceneGraph);
    ASSERT_EQ(light->getParent(), smgr_->getRootSceneNode());

    // The worker culls the entity: applySimulationOutput() detaches both nodes.
    applyVisibility(false);
    EXPECT_FALSE(entities_->getEntities().at(kSpawn).inSceneGraph);
    EXPECT_EQ(light->getParent(), nullptr) << "the light node was not detached";
    EXPECT_EQ(light->getReferenceCount(), 2)
        << "detached light node must still be owned by EntityRenderer (+ this "
           "test); a count of 1 means production's only reference went with "
           "the parent and visual.lightNode is now dangling (B202)";

    // The worker re-admits it: applySimulationOutput() re-attaches the same
    // pointer. Pre-fix this is a use-after-free on the render thread.
    applyVisibility(true);
    EXPECT_TRUE(entities_->getEntities().at(kSpawn).inSceneGraph);
    EXPECT_EQ(light->getParent(), smgr_->getRootSceneNode());
    EXPECT_EQ(light->getReferenceCount(), 3);

    // And it survives repeated cycles, which is what a 5-minute session is.
    for (int i = 0; i < 20; ++i) { applyVisibility(false); applyVisibility(true); }
    EXPECT_EQ(light->getParent(), smgr_->getRootSceneNode());
    EXPECT_EQ(light->getReferenceCount(), 3);
    EXPECT_EQ(entities_->getEntities().at(kSpawn).lightNode, light)
        << "the entity must still point at the same, live, light node";

    light->drop();
}

// The other half of the invariant: owning a reference must not leak it. Both
// teardown paths (per-entity despawn and whole-zone clear) release it.
TEST_F(EntityLightNodeLifetimeTest, RemoveEntityReleasesTheLightNodeReference) {
    irr::scene::ILightSceneNode* light = makeLitEntity();
    ASSERT_NE(light, nullptr);

    entities_->removeEntity(kSpawn);
    EXPECT_EQ(entities_->getEntities().count(kSpawn), 0u);
    EXPECT_EQ(light->getReferenceCount(), 1)
        << "removeEntity() left a reference behind: the light node leaks";

    light->drop();
}

TEST_F(EntityLightNodeLifetimeTest, ClearEntitiesReleasesTheLightNodeReference) {
    irr::scene::ILightSceneNode* light = makeLitEntity();
    ASSERT_NE(light, nullptr);

    // Cull it first, so clearEntities() takes the detached path -- the one a
    // zone change hits for every entity the worker had culled.
    applyVisibility(false);
    entities_->clearEntities();
    EXPECT_TRUE(entities_->getEntities().empty());
    EXPECT_EQ(light->getReferenceCount(), 1)
        << "clearEntities() left a reference behind: the light node leaks";

    light->drop();
}

// Turning the light off is a third destruction path (an unequipped lantern).
TEST_F(EntityLightNodeLifetimeTest, SettingLightLevelZeroReleasesTheLightNodeReference) {
    irr::scene::ILightSceneNode* light = makeLitEntity();
    ASSERT_NE(light, nullptr);

    entities_->setEntityLight(kSpawn, 0);
    EXPECT_EQ(entities_->getEntities().at(kSpawn).lightNode, nullptr);
    EXPECT_EQ(light->getReferenceCount(), 1)
        << "setEntityLight(0) left a reference behind: the light node leaks";

    light->drop();
}

}  // namespace
