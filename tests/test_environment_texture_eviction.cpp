// T463 (boards B522, B523, B524) -- the three environment effects that load a
// texture ONCE into ConstrainedTextureCache and keep drawing it for the life
// of the renderer must still draw a real, live texture after the cache's two
// destruction paths:
//   * zone unload -- IrrlichtRenderer::unloadZone() -> ConstrainedTextureCache::
//     clear(), which notifies the listeners of, scrubs the scene graph of, and
//     driver_->removeTexture()s every texture that is not protectTexture()'d;
//   * LRU pressure -- evictUntilAvailable() does the same to the oldest
//     unprotected entry, and a texture loaded at renderer init is always among
//     the oldest.
//
// Same seam as T460's tests/test_software_cursor_eviction.cpp: the REAL cache
// on an EDT_NULL device, the REAL owner classes with their REAL init(), the
// REAL guard the renderer registers for boids, and the same call order the
// renderer uses (onZoneLeave() -> clear() -> onZoneEnter()).
//
// "Drawable" is checked without dereferencing: a texture is drawable when it
// is non-null, is not the cache's placeholder, is still a cache entry
// (isLiveTexture) AND is still in the driver's own texture list. The last
// condition is the direct proof of a dangling pointer: removeTexture() drops
// the driver's only reference, so a pointer the driver no longer lists points
// at freed memory.

#include <gtest/gtest.h>

#ifdef EQT_HAS_GRAPHICS
#include "client/graphics/constrained_texture_cache.h"
#include "client/graphics/constrained_renderer_config.h"
#include "client/graphics/renderer_zone_merge.h"
#include "client/graphics/simulation_worker.h"
#include "client/graphics/environment/boids_manager.h"
#include "client/graphics/environment/storm_cloud_layer.h"
#include "client/graphics/environment/tumbleweed_manager.h"
#include <irrlicht.h>

#include <memory>
#include <set>
#include <string>

using namespace EQT::Graphics;
using Environment::ZoneBiome;

namespace {

class EnvironmentTextureEvictionTest : public ::testing::Test {
protected:
    irr::IrrlichtDevice* device_ = nullptr;
    irr::video::IVideoDriver* driver_ = nullptr;
    irr::scene::ISceneManager* smgr_ = nullptr;
    std::unique_ptr<ConstrainedTextureCache> cache_;
    irr::video::ITexture* placeholder_ = nullptr;

    void makeCache(size_t textureBudgetBytes) {
        irr::SIrrlichtCreationParameters params;
        params.DriverType = irr::video::EDT_NULL;
        params.WindowSize = irr::core::dimension2d<irr::u32>(64, 64);
        device_ = irr::createDeviceEx(params);
        ASSERT_NE(device_, nullptr) << "EDT_NULL device";
        driver_ = device_->getVideoDriver();
        smgr_ = device_->getSceneManager();

        auto config = ConstrainedRendererConfig::fromPreset(ConstrainedRenderingPreset::OrangePi);
        if (textureBudgetBytes) config.textureMemoryBytes = textureBudgetBytes;
        cache_ = std::make_unique<ConstrainedTextureCache>(config);
        cache_->setDriver(driver_);
        cache_->setSceneManager(smgr_);
        // Created up front so it can never be allocated at the address of a
        // texture a test is about to prove freed.
        placeholder_ = cache_->getPlaceholderTexture();
    }

    bool driverHolds(irr::video::ITexture* t) const {
        for (irr::u32 i = 0; i < driver_->getTextureCount(); ++i) {
            if (driver_->getTextureByIndex(i) == t) return true;
        }
        return false;
    }

    ::testing::AssertionResult drawable(irr::video::ITexture* t) const {
        if (!t) return ::testing::AssertionFailure() << "texture is NULL";
        if (t == placeholder_) return ::testing::AssertionFailure() << "texture is the placeholder";
        if (!driverHolds(t))
            return ::testing::AssertionFailure()
                   << "texture " << static_cast<void*>(t)
                   << " is no longer in the driver's texture list: DANGLING (freed)";
        if (!cache_->isLiveTexture(t))
            return ::testing::AssertionFailure() << "texture is not a live cache entry";
        return ::testing::AssertionSuccess();
    }

    // Sixteen zone textures of a quarter of the budget each: four budgets'
    // worth, so the cache must evict repeatedly (T460's pressure recipe).
    void applyBudgetPressure(size_t budget) {
        for (int i = 0; i < 16; ++i) {
            const std::string name = "t463_filler_" + std::to_string(i);
            irr::video::ITexture* t =
                driver_->addTexture(irr::core::dimension2d<irr::u32>(4, 4), name.c_str());
            ASSERT_NE(t, nullptr);
            ASSERT_TRUE(cache_->registerTexture(name, t, budget / 4)) << name;
        }
        ASSERT_GT(cache_->getEvictionCount(), 0u) << "the fixture did not create budget pressure";
    }

    irr::scene::IMeshSceneNode* firstMeshNode() const {
        for (auto* child : smgr_->getRootSceneNode()->getChildren()) {
            if (child->getType() == irr::scene::ESNT_MESH)
                return static_cast<irr::scene::IMeshSceneNode*>(child);
        }
        return nullptr;
    }

    void TearDown() override {
        cache_.reset();
        if (device_) device_->drop();
    }
};

// ---------------------------------------------------------------------------
// B524 -- BoidsManager's creature_atlas
// ---------------------------------------------------------------------------

// Pre-fix RED: clear() fired the renderer's ForwardingTextureGuard, which
// swapped atlasTexture_ and creatureMaterial_ to the placeholder, and nothing
// ever loaded the atlas again -- render() binds creatureMaterial_ every frame.
TEST_F(EnvironmentTextureEvictionTest, BoidsDrawTheCreatureAtlasAfterEveryZoneUnload) {
    makeCache(0);
    Environment::BoidsManager boids(smgr_, driver_);
    boids.setConstrainedTextureCache(cache_.get());
    ASSERT_TRUE(boids.init(""));
    ASSERT_TRUE(drawable(boids.getCreatureMaterial().getTexture(0))) << "init() did not load the atlas";

    // Exactly the guard ZoneStreamComponent::ensureBoidsTextureGuard() registers.
    ForwardingTextureGuard guard(
        [&boids](irr::video::ITexture* from, irr::video::ITexture* to) { boids.swapEvictedTexture(from, to); },
        placeholder_);
    cache_->addEvictionListener(&guard);

    for (int zone = 0; zone < 2; ++zone) {
        boids.onZoneLeave();
        cache_->clear();
        boids.onZoneEnter("gfaydark", ZoneBiome::Forest);
        EXPECT_TRUE(drawable(boids.getCreatureMaterial().getTexture(0)))
            << "zone unload #" << (zone + 1) << " left boids drawing without the creature atlas";
    }

    cache_->removeEvictionListener(&guard);
}

// Pre-fix RED: the init-time atlas is an LRU victim like any zone texture.
TEST_F(EnvironmentTextureEvictionTest, BoidsDrawTheCreatureAtlasUnderBudgetPressure) {
    const size_t kBudget = 256 * 1024;
    makeCache(kBudget);
    Environment::BoidsManager boids(smgr_, driver_);
    boids.setConstrainedTextureCache(cache_.get());
    ASSERT_TRUE(boids.init(""));

    ForwardingTextureGuard guard(
        [&boids](irr::video::ITexture* from, irr::video::ITexture* to) { boids.swapEvictedTexture(from, to); },
        placeholder_);
    cache_->addEvictionListener(&guard);

    applyBudgetPressure(kBudget);
    EXPECT_TRUE(drawable(boids.getCreatureMaterial().getTexture(0)))
        << "LRU eviction left boids drawing without the creature atlas";

    cache_->removeEvictionListener(&guard);
}

// ---------------------------------------------------------------------------
// B523 -- TumbleweedManager's tumbleweed texture
// ---------------------------------------------------------------------------

namespace {
void spawnTumbleweed(Environment::TumbleweedManager& tw, int poolIndex) {
    SimulationOutput::TumbleweedOutput out;
    out.spawns.push_back({poolIndex, 1.0f});
    out.activeCount = 1;
    out.valid = true;
    tw.applyWorkerResults(out);
}
}  // namespace

// Pre-fix RED, and a use-after-free: no tumbleweed spawned in the first zone,
// so no scene node held tumbleweedMesh_ when clear() ran, and
// clearTextureReferences()'s scene walk could not reach the mesh buffer's
// material. The texture was destroyed under it; the next zone's first spawn
// copies that freed pointer into a new node's material and draws it.
TEST_F(EnvironmentTextureEvictionTest, TumbleweedsNeverDrawAFreedTextureAfterAZoneUnload) {
    makeCache(0);
    Environment::TumbleweedManager tw(smgr_, driver_);
    tw.setConstrainedTextureCache(cache_.get());
    ASSERT_TRUE(tw.init());

    tw.onZoneLeave();
    cache_->clear();
    tw.onZoneEnter("nro", ZoneBiome::Desert);
    spawnTumbleweed(tw, 0);

    irr::scene::IMeshSceneNode* node = firstMeshNode();
    ASSERT_NE(node, nullptr) << "the spawn created no scene node";
    EXPECT_TRUE(drawable(node->getMesh()->getMeshBuffer(0)->getMaterial().getTexture(0)))
        << "tumbleweedMesh_'s buffer material after zone unload";
    EXPECT_TRUE(drawable(node->getMaterial(0).getTexture(0)))
        << "the spawned node's material after zone unload";

    // A second unload, this time with a (hidden) node alive -- the path the
    // scene walk does reach, which swapped it to the placeholder pre-fix.
    tw.onZoneLeave();
    cache_->clear();
    tw.onZoneEnter("sro", ZoneBiome::Desert);
    spawnTumbleweed(tw, 0);
    EXPECT_TRUE(drawable(node->getMaterial(0).getTexture(0)))
        << "the node's material after the second zone unload";
}

// Pre-fix RED: the LRU path evicts the init-time texture and the scene walk
// swaps the live node to the placeholder, for good.
TEST_F(EnvironmentTextureEvictionTest, TumbleweedsDrawTheirTextureUnderBudgetPressure) {
    const size_t kBudget = 256 * 1024;
    makeCache(kBudget);
    Environment::TumbleweedManager tw(smgr_, driver_);
    tw.setConstrainedTextureCache(cache_.get());
    ASSERT_TRUE(tw.init());
    tw.onZoneEnter("nro", ZoneBiome::Desert);
    spawnTumbleweed(tw, 0);
    irr::scene::IMeshSceneNode* node = firstMeshNode();
    ASSERT_NE(node, nullptr);

    applyBudgetPressure(kBudget);
    spawnTumbleweed(tw, 1);
    for (auto* child : smgr_->getRootSceneNode()->getChildren()) {
        if (child->getType() != irr::scene::ESNT_MESH) continue;
        auto* n = static_cast<irr::scene::IMeshSceneNode*>(child);
        EXPECT_TRUE(drawable(n->getMaterial(0).getTexture(0))) << "a tumbleweed node under LRU pressure";
    }
}

// ---------------------------------------------------------------------------
// B522 -- StormCloudLayer's 32 pre-blended frames
// ---------------------------------------------------------------------------

namespace {
void stormStep(Environment::StormCloudLayer& clouds) {
    // Intensity 10 is above the default threshold (3); 0.25 s per step is
    // above timePerFrame (3.0 s / 32), so each step advances exactly one frame.
    clouds.update(0.25f, glm::vec3(0.0f), glm::vec3(1.0f, 0.0f, 0.0f), 0.5f, 10, 12.0f);
}

irr::scene::ISceneNode* domeOf(const Environment::StormCloudLayer& clouds) {
    std::set<irr::scene::ISceneNode*> nodes;
    clouds.collectSceneNodes(nodes);
    return nodes.empty() ? nullptr : *nodes.begin();
}
}  // namespace

// Pre-fix RED: clear() nulled every cloudFrames_ entry through the layer's own
// listener and the scene walk swapped the (hidden) dome to the placeholder.
// updateBlendedTexture() returns on a null frame, so a storm in any zone after
// the first showed a static placeholder dome and never animated.
TEST_F(EnvironmentTextureEvictionTest, StormCloudsAnimateRealFramesAfterEveryZoneUnload) {
    makeCache(0);
    Environment::StormCloudLayer clouds;
    clouds.setConstrainedTextureCache(cache_.get());
    ASSERT_TRUE(clouds.initialize(smgr_, driver_));
    irr::scene::ISceneNode* dome = domeOf(clouds);
    ASSERT_NE(dome, nullptr);

    for (int zone = 0; zone < 2; ++zone) {
        clouds.onZoneLeave();
        cache_->clear();
        clouds.onZoneEnter("qeynos2", false);

        for (int i = 0; i < 4; ++i) stormStep(clouds);  // fade in
        ASSERT_TRUE(dome->isVisible()) << "the storm did not fade the dome in";

        std::set<irr::video::ITexture*> seen;
        for (int i = 0; i < 32; ++i) {
            stormStep(clouds);
            irr::video::ITexture* t = dome->getMaterial(0).getTexture(0);
            EXPECT_TRUE(drawable(t)) << "zone unload #" << (zone + 1) << ", storm step " << i;
            seen.insert(t);
        }
        EXPECT_EQ(seen.size(), 32u)
            << "zone unload #" << (zone + 1) << ": one full cycle should show all 32 frames";
    }
}

// Pre-fix RED: the frames are 32 x ~350 KB of accounted budget; LRU pressure
// evicts them, the listener nulls them, and they never come back.
TEST_F(EnvironmentTextureEvictionTest, StormCloudsAnimateRealFramesUnderBudgetPressure) {
    const size_t kBudget = 16 * 1024 * 1024;
    makeCache(kBudget);
    Environment::StormCloudLayer clouds;
    clouds.setConstrainedTextureCache(cache_.get());
    ASSERT_TRUE(clouds.initialize(smgr_, driver_));
    irr::scene::ISceneNode* dome = domeOf(clouds);
    ASSERT_NE(dome, nullptr);
    clouds.onZoneEnter("qeynos2", false);

    applyBudgetPressure(kBudget);

    for (int i = 0; i < 4; ++i) stormStep(clouds);
    std::set<irr::video::ITexture*> seen;
    for (int i = 0; i < 32; ++i) {
        stormStep(clouds);
        irr::video::ITexture* t = dome->getMaterial(0).getTexture(0);
        EXPECT_TRUE(drawable(t)) << "storm step " << i << " under LRU pressure";
        seen.insert(t);
    }
    EXPECT_EQ(seen.size(), 32u) << "one full cycle under LRU pressure should show all 32 frames";
}

}  // namespace

#endif  // EQT_HAS_GRAPHICS
