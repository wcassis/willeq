// overlays_env_managers.cpp - THE one construction of each environment manager
// the renderer builds both at init and lazily (T467, board B550).
//
// IrrlichtRenderer::initLoadingScreen() builds the managers the shipped display
// settings turn on; OverlaysComponent::applyEnvironmentalDisplaySettings()
// (Step 11 of every zone load) builds any that are on by then and missing.
// Until T467 each caller carried its own copy of the construction, and the
// tumbleweed copies drifted: the lazy one never called
// setConstrainedTextureCache(), so TumbleweedManager::init() logged FATAL and
// std::exit(1)ed the process. The exit is correct (EQE1 is all-or-nothing);
// reaching it through a missing wiring call was the defect. Both callers now go
// through the four bodies below, and
// tests/test_environment_manager_construction.cpp fails if a second
// construction of any of them appears under src/.
//
// Every body is idempotent (an existing manager is left untouched) and returns
// whether a manager exists afterwards. Everything a manager needs at
// construction is done HERE: the cache, the manager's own init() (which is
// where T463's protectTexture() calls live) and the renderer-side eviction
// guard (boids). A caller adds only what depends on its moment -- the lazy
// caller's onZoneEnter() for a zone that is already loaded.
//
// ParticleManager is not here: it is built unconditionally at init and has no
// lazy site.

#include "client/graphics/display_settings.h"  // T504: BaselineOnly()
#include "client/graphics/overlays/overlays_component.h"
#include "client/graphics/irrlicht_renderer.h"
#include "client/graphics/renderer_context.h"
#include "client/graphics/constrained_texture_cache.h"
#include "client/graphics/detail/detail_manager.h"
#include "client/graphics/environment/boids_manager.h"
#include "client/graphics/environment/tumbleweed_manager.h"
#include "client/graphics/sky_renderer.h"
#include "client/graphics/weather_effects_controller.h"
#include "client/graphics/weather_system.h"
#include "client/graphics/zone_stream/zone_stream_component.h"
#include "common/logging.h"

namespace EQT {
namespace Graphics {

bool OverlaysComponent::ensureBoidsManager() {
    if (ctx_.boidsManager) return true;
    if (!ctx_.smgr || !ctx_.driver) return false;
    LOG_TRACE(MOD_GRAPHICS, "Creating boids manager (loading atlas)...");
    ctx_.boidsManager = std::make_unique<Environment::BoidsManager>(ctx_.smgr, ctx_.driver);
    if (ctx_.constrainedTextureCache) {
        ctx_.boidsManager->setConstrainedTextureCache(ctx_.constrainedTextureCache);  // T124
    }
    if (!ctx_.boidsManager->init(ctx_.config.eqClientPath)) {
        LOG_WARN(MOD_GRAPHICS, "Failed to initialize boids manager");
    } else {
        LOG_INFO(MOD_GRAPHICS, "Boids manager initialized");
    }
    // T124: the guard that swaps an evicted atlas out of creatureMaterial_ is
    // registered with the construction, never separately from it.
    ctx_.ZoneStream().ensureBoidsTextureGuard();
    return true;
}

bool OverlaysComponent::ensureTumbleweedManager() {
    if (ctx_.tumbleweedManager) return true;
    if (!ctx_.smgr || !ctx_.driver) return false;
    LOG_TRACE(MOD_GRAPHICS, "Creating tumbleweed manager (loading texture)...");
    ctx_.tumbleweedManager = std::make_unique<Environment::TumbleweedManager>(ctx_.smgr, ctx_.driver);
    // T467 (B550): the statement the lazy copy was missing -- init() exits the
    // process without a cache.
    if (ctx_.constrainedTextureCache) {
        ctx_.tumbleweedManager->setConstrainedTextureCache(ctx_.constrainedTextureCache);
    }
    if (!ctx_.tumbleweedManager->init()) {
        LOG_WARN(MOD_GRAPHICS, "Failed to initialize tumbleweed manager");
    } else {
        LOG_INFO(MOD_GRAPHICS, "Tumbleweed manager initialized");
    }
    return true;
}

bool OverlaysComponent::ensureDetailManager() {
    if (ctx_.detailManager) return true;
    if (!ctx_.smgr || !ctx_.driver) return false;
    if (eqt::ui::BaselineOnly()) return false;  // T504: non-original; and it pins the combined zone geometry
    LOG_TRACE(MOD_GRAPHICS, "Creating detail manager...");
    ctx_.detailManager = std::make_unique<Detail::DetailManager>(ctx_.smgr, ctx_.driver);
    ctx_.detailManager->setSurfaceMapsPath("data/detail/zones");
    if (ctx_.constrainedTextureCache) {
        ctx_.detailManager->setConstrainedTextureCache(ctx_.constrainedTextureCache);
    }
    LOG_INFO(MOD_GRAPHICS, "Detail manager initialized");
    return true;
}

bool OverlaysComponent::ensureWeatherEffects() {
    if (ctx_.weatherEffects) return true;
    if (!ctx_.smgr || !ctx_.driver) return false;
    LOG_TRACE(MOD_GRAPHICS, "Creating weather effects controller...");
    ctx_.weatherEffects = std::make_unique<WeatherEffectsController>(
        ctx_.smgr, ctx_.driver, ctx_.particleManager.get(), ctx_.skyRenderer.get());
    if (ctx_.constrainedTextureCache) {
        ctx_.weatherEffects->setConstrainedTextureCache(ctx_.constrainedTextureCache);
    }
    LOG_TRACE(MOD_GRAPHICS, "Initializing weather effects (loading cloud/rain/snow textures)...");
    if (!ctx_.weatherEffects->initialize(ctx_.config.eqClientPath)) {
        LOG_WARN(MOD_GRAPHICS, "Failed to initialize weather effects controller");
    }
    if (ctx_.weatherSystem) {
        ctx_.weatherSystem->addListener(ctx_.weatherEffects.get());
    }
    LOG_INFO(MOD_GRAPHICS, "Weather effects initialized");
    return true;
}

}  // namespace Graphics
}  // namespace EQT
