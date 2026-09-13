// T467 (board B550) -- every environment manager the renderer can construct
// LAZILY must come out of that construction wired exactly as the init-time one
// is, because both now go through ONE body per manager.
//
// MEASURED before the fix: OverlaysComponent::applyEnvironmentalDisplaySettings()
// ("Tumbleweed Manager: lazy create if toggled on") built a TumbleweedManager
// and called init() WITHOUT setConstrainedTextureCache(). TumbleweedManager::
// init() then logs "Constrained texture cache not set" at FATAL and
// std::exit(1)s, so the first case below killed this test binary. The init-time
// construction in IrrlichtRenderer::initLoadingScreen() passed the cache.
//
// Seam: a bare, never-Init()'d IrrlichtRenderer whose RendererContext is bound
// to an EDT_NULL device and the REAL ConstrainedTextureCache (T463's seam), so
// the call under test is the renderer's own applyEnvironmentalDisplaySettings()
// with the renderer's own default DisplaySettings (every environment toggle on,
// and no manager built yet: exactly the lazy branch).

#include <gtest/gtest.h>

#ifdef EQT_HAS_GRAPHICS
#include "client/graphics/constrained_renderer_config.h"
#include "client/graphics/constrained_texture_cache.h"
#include "client/graphics/display_settings.h"
#include "client/graphics/environment/boids_manager.h"
#include "client/graphics/environment/tumbleweed_manager.h"
#include "client/graphics/irrlicht_renderer.h"
#include "client/graphics/overlays/overlays_component.h"
#include "client/graphics/renderer_context.h"
#include "client/graphics/simulation_worker.h"
#include <irrlicht.h>

#include <filesystem>
#include <fstream>
#include <memory>
#include <set>
#include <sstream>
#include <string>

#ifndef WILLEQ_SOURCE_DIR
#error "WILLEQ_SOURCE_DIR must be defined (tests/cmake/renderer_guards.cmake)"
#endif

using namespace EQT::Graphics;
using Environment::ZoneBiome;
namespace fs = std::filesystem;

namespace {

class EnvironmentManagerConstructionTest : public ::testing::Test {
protected:
    irr::IrrlichtDevice* device_ = nullptr;
    irr::video::IVideoDriver* driver_ = nullptr;
    irr::scene::ISceneManager* smgr_ = nullptr;
    std::unique_ptr<ConstrainedTextureCache> cache_;
    irr::video::ITexture* placeholder_ = nullptr;
    std::unique_ptr<IrrlichtRenderer> renderer_;

    void SetUp() override {
        irr::SIrrlichtCreationParameters params;
        params.DriverType = irr::video::EDT_NULL;
        params.WindowSize = irr::core::dimension2d<irr::u32>(64, 64);
        device_ = irr::createDeviceEx(params);
        ASSERT_NE(device_, nullptr) << "EDT_NULL device";
        driver_ = device_->getVideoDriver();
        smgr_ = device_->getSceneManager();

        cache_ = std::make_unique<ConstrainedTextureCache>(
            ConstrainedRendererConfig::fromPreset(ConstrainedRenderingPreset::OrangePi));
        cache_->setDriver(driver_);
        cache_->setSceneManager(smgr_);
        placeholder_ = cache_->getPlaceholderTexture();

        renderer_ = std::make_unique<IrrlichtRenderer>();
        RendererContext& ctx = renderer_->Context();
        // IrrlichtRenderer::shutdown() drops the device it holds; this test
        // keeps its own reference so the cache can be destroyed after it.
        device_->grab();
        ctx.device = device_;
        ctx.driver = driver_;
        ctx.smgr = smgr_;
        ctx.constrainedTextureCache = cache_.get();
    }

    void TearDown() override {
        renderer_.reset();
        cache_.reset();
        if (device_) device_->drop();
    }

    RendererContext& ctx() { return renderer_->Context(); }

    bool driverHolds(irr::video::ITexture* t) const {
        for (irr::u32 i = 0; i < driver_->getTextureCount(); ++i) {
            if (driver_->getTextureByIndex(i) == t) return true;
        }
        return false;
    }

    // T463's definition: non-null, not the placeholder, still in the driver's
    // own texture list (a dangling pointer is not), and a live cache entry.
    ::testing::AssertionResult drawable(irr::video::ITexture* t) const {
        if (!t) return ::testing::AssertionFailure() << "texture is NULL";
        if (t == placeholder_) return ::testing::AssertionFailure() << "texture is the placeholder";
        if (!driverHolds(t))
            return ::testing::AssertionFailure() << "texture is no longer in the driver's list: DANGLING";
        if (!cache_->isLiveTexture(t))
            return ::testing::AssertionFailure() << "texture is not a live cache entry";
        return ::testing::AssertionSuccess();
    }

    std::set<irr::scene::ISceneNode*> meshNodes() const {
        std::set<irr::scene::ISceneNode*> out;
        for (auto* child : smgr_->getRootSceneNode()->getChildren()) {
            if (child->getType() == irr::scene::ESNT_MESH) out.insert(child);
        }
        return out;
    }

    // One tumbleweed spawned through the worker-result path; returns the node
    // it made.
    irr::scene::IMeshSceneNode* spawnTumbleweed(int poolIndex) {
        const auto before = meshNodes();
        SimulationOutput::TumbleweedOutput out;
        out.spawns.push_back({poolIndex, 1.0f});
        out.activeCount = 1;
        out.valid = true;
        ctx().tumbleweedManager->applyWorkerResults(out);
        for (auto* n : meshNodes()) {
            if (!before.count(n)) return static_cast<irr::scene::IMeshSceneNode*>(n);
        }
        return nullptr;
    }
};

}  // namespace

// Pre-fix RED: the process exited with status 1 inside this call
// ("TumbleweedManager: Constrained texture cache not set").
TEST_F(EnvironmentManagerConstructionTest, TurningRollingObjectsOnAfterInitWiresTheTumbleweedManager) {
    ASSERT_EQ(ctx().tumbleweedManager, nullptr) << "precondition: no manager from init";
    // A never-Init()'d renderer holds a default-constructed DisplaySettings.
    ASSERT_TRUE(eqt::ui::DisplaySettings{}.rollingObjects) << "precondition: the lazy branch is taken";

    renderer_->Overlays().applyEnvironmentalDisplaySettings();

    ASSERT_NE(ctx().tumbleweedManager, nullptr) << "the lazy branch built no manager";
    EXPECT_TRUE(cache_->hasTexture("tumbleweed")) << "init() never loaded its texture through the cache";

    // T463's protection must come along with the cache: a zone unload between
    // construction and the first spawn may not leave the mesh a freed texture.
    ctx().tumbleweedManager->onZoneLeave();
    cache_->clear();
    ctx().tumbleweedManager->onZoneEnter("nro", ZoneBiome::Desert);
    irr::scene::IMeshSceneNode* node = spawnTumbleweed(0);
    ASSERT_NE(node, nullptr) << "the spawn created no scene node";
    EXPECT_TRUE(drawable(node->getMaterial(0).getTexture(0)))
        << "a lazily-built tumbleweed after a zone unload";
}

// The sibling lazy site in the same function: boids must also come out with
// the cache, T463's protected atlas and the renderer's eviction guard.
TEST_F(EnvironmentManagerConstructionTest, TurningAmbientCreaturesOnAfterInitWiresTheBoidsManager) {
    ASSERT_EQ(ctx().boidsManager, nullptr);
    ASSERT_TRUE(eqt::ui::DisplaySettings{}.ambientCreatures);

    renderer_->Overlays().applyEnvironmentalDisplaySettings();

    ASSERT_NE(ctx().boidsManager, nullptr);
    EXPECT_TRUE(drawable(ctx().boidsManager->getCreatureMaterial().getTexture(0)))
        << "the lazily-built boids manager has no creature atlas";
    ctx().boidsManager->onZoneLeave();
    cache_->clear();
    ctx().boidsManager->onZoneEnter("gfaydark", ZoneBiome::Forest);
    EXPECT_TRUE(drawable(ctx().boidsManager->getCreatureMaterial().getTexture(0)))
        << "the lazily-built boids manager after a zone unload";
}

// A second call must not rebuild anything: the ensure bodies are idempotent.
TEST_F(EnvironmentManagerConstructionTest, ApplyingTheSettingsTwiceBuildsEachManagerOnce) {
    renderer_->Overlays().applyEnvironmentalDisplaySettings();
    const auto* tw = ctx().tumbleweedManager.get();
    const auto* boids = ctx().boidsManager.get();
    const auto* detail = ctx().detailManager.get();
    ASSERT_NE(tw, nullptr);
    ASSERT_NE(boids, nullptr);
    ASSERT_NE(detail, nullptr);
    renderer_->Overlays().applyEnvironmentalDisplaySettings();
    EXPECT_EQ(ctx().tumbleweedManager.get(), tw);
    EXPECT_EQ(ctx().boidsManager.get(), boids);
    EXPECT_EQ(ctx().detailManager.get(), detail);
}

// The structural half: each of the four environment managers that has both an
// init-time and a lazy construction is constructed in exactly ONE place under
// src/, so the two paths cannot diverge again. Pre-fix RED: two sites each.
TEST(EnvironmentManagerConstructionSource, EachEnvironmentManagerHasOneConstructionSite) {
    const char* kNeedles[] = {
        "make_unique<Environment::TumbleweedManager>",
        "make_unique<Environment::BoidsManager>",
        "make_unique<Detail::DetailManager>",
        "make_unique<WeatherEffectsController>",
    };
    const fs::path src = fs::path(WILLEQ_SOURCE_DIR) / "src";
    ASSERT_TRUE(fs::is_directory(src)) << src;
    for (const char* needle : kNeedles) {
        std::vector<std::string> sites;
        for (const auto& entry : fs::recursive_directory_iterator(src)) {
            if (!entry.is_regular_file()) continue;
            const auto ext = entry.path().extension();
            if (ext != ".cpp" && ext != ".h") continue;
            std::ifstream in(entry.path());
            std::string line;
            int lineNo = 0;
            while (std::getline(in, line)) {
                ++lineNo;
                if (line.find(needle) != std::string::npos) {
                    sites.push_back(fs::relative(entry.path(), WILLEQ_SOURCE_DIR).string() + ":" +
                                    std::to_string(lineNo));
                }
            }
        }
        std::ostringstream list;
        for (const auto& s : sites) list << "\n  " << s;
        ASSERT_EQ(sites.size(), 1u) << needle << " is constructed at:" << list.str();
        EXPECT_NE(sites[0].find("src/client/graphics/overlays/overlays_env_managers.cpp"), std::string::npos)
            << needle << " is constructed outside the one construction TU:" << list.str();
    }
}

#endif  // EQT_HAS_GRAPHICS
