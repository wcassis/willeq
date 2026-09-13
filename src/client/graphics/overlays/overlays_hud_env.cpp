// HUD, fog, loading screen, environment display settings and the inventory
// paperdoll -- EQT::Graphics::OverlaysComponent (second TU of the family).
//
// L03 (Batch L, refactor/L_renderer_component_extraction.md): moved from
// src/client/graphics/renderer_hud_env.cpp to here, and the bodies rebased
// from IrrlichtRenderer onto EQT::Graphics::OverlaysComponent. Verbatim --
// the only edits inside a body are the four substitutions the mechanism
// allows (ctx_.X, owner_.X_, owner_.Foo(), OverlaysComponent::).

#include "client/graphics/overlays/overlays_component.h"
#include "client/graphics/irrlicht_renderer.h"
#include "client/graphics/renderer_zone_merge.h"  // ZoneMergeBuffers complete type (unique_ptr member destruction)
#include "client/willeq_database.h"
#include "client/graphics/loading_thread.h"
#include "client/graphics/entity_prep_worker.h"
#include "client/graphics/eqe1_loader.h"
#include <filesystem>
#include "client/graphics/eq/dds_decoder.h"
#include "client/graphics/eq/equipment_textures.h"
#include "client/graphics/frame_budget_governor.h"
#include "client/graphics/constrained_texture_cache.h"
#include "client/graphics/light_source.h"
#include "client/graphics/weather_effects_controller.h"
#include "client/graphics/weather_config_loader.h"
#include "client/graphics/weather_quality_preset.h"
#include "client/graphics/environment/spell_effects_config.h"
#include "client/graphics/environment/zone_biome.h"
#include "client/graphics/eq/zone_geometry.h"
#include "client/graphics/eq/race_model_loader.h"
#include "client/graphics/eq/race_codes.h"
#include "client/inventory/inventory_manager.h"
#include "client/graphics/spell_visual_fx.h"
#include "client/graphics/text_batch.h"
#include "client/graphics/ui/ui_atlas.h"
#include "client/graphics/ui/ui_renderer.h"
#include "client/graphics/ui/static_ui_input.h"
#include "client/graphics/ui/character_model_view.h"
#include "client/graphics/ui/inventory_layout.h"
#include "client/chat/chat_message_buffer.h"
#include "client/graphics/sky_renderer.h"
#include "client/graphics/eq/sky_loader.h"
#include "client/graphics/sky_config.h"
#include "client/zone_lines.h"
#include "client/hc_map.h"
#ifdef EQT_HAS_NAVMESH
#include "client/pathfinder_nav_mesh.h"
#endif
#include "client/bridge/game_state_bridge.h"
#include "client/state/event_bus.h"
#include "client/events/renderer_intents.h"
#include "client/graphics/detail/detail_manager.h"
#include "client/graphics/detail/detail_types.h"
#include "client/graphics/detail/seasonal_controller.h"
#include "common/logging.h"
#include <fmt/format.h>
#include "common/name_utils.h"
#include "common/performance_metrics.h"
#include <cstdio>
#ifdef __linux__
#include <unistd.h>
#include <sched.h>
#include <pthread.h>
#endif

#ifdef WITH_RDP
#include "client/graphics/rdp/rdp_server.h"
#include "client/graphics/rdp/rdp_input_handler.h"
#endif
#ifdef EQT_HAS_DRM
#ifdef EQT_HAS_GLES2
#include <GLES2/gl2.h>
#include <EGL/eglext.h>
#else
#include <GL/gl.h>
#endif
#include <EGL/egl.h>
#endif

// Bridge functions for GLES2 static VBO management.
// Defined in COpenGLES2Driver.cpp (compiled into Irrlicht library), declared here
// to avoid including the full COpenGLES2Driver.h (which depends on Irrlicht-internal headers).
#ifdef EQT_HAS_GLES2
extern void gles2DeleteAllStaticHWBuffers(void* driver);
extern size_t gles2GetHWBufferMemoryUsage(void* driver);
extern size_t gles2GetHWBufferCount(void* driver);
extern size_t gles2GetGpuTextureMemoryUsage(void* driver);
extern void gles2Draw3DLinesBatch(void* driver, const float* vertices, unsigned int lineCount);
extern void* gles2WrapTexture(void* driver, const char* name, unsigned int glTexName,
                               unsigned int width, unsigned int height,
                               unsigned int gpuBytes = 0);
extern void gles2RegisterExternalHWBuffer(void* driver, const void* meshBuffer,
                                           unsigned int vbo, unsigned int ebo,
                                           unsigned int vertexCount, unsigned int indexCount,
                                           int vertexType);
#endif
#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <json/json.h>
#include <sstream>
#include <cmath>
#include <limits>
#include <set>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// Stub definition for WindowManager — class has been deleted but unique_ptr member
// remains in the header (forward-declared). This allows the unique_ptr destructor to compile.
namespace eqt { namespace ui { class WindowManager { public: virtual ~WindowManager() = default; }; } }

namespace EQT {
namespace Graphics {

void OverlaysComponent::setupFog() {
    if (!ctx_.driver) {
        return;
    }

    // Unified fog system:
    // - fogEnd = renderDistance_ (absolute visibility limit)
    // - fogStart = renderDistance_ - fogThickness_ (where fade begins)
    // - Everything beyond renderDistance_ is culled, nothing renders there
    // - Fog creates a smooth fade zone at the edge
    float fogEnd = ctx_.renderDistance;
    float fogStart = ctx_.renderDistance - ctx_.fogThickness;
    fogStart = std::max(0.0f, fogStart);  // Ensure non-negative

    // Get fog color from sky renderer if available, otherwise use default
    irr::video::SColor fogColor(255, 128, 128, 160);  // Default: light gray-blue
    if (ctx_.skyRenderer && ctx_.skyRenderer->isInitialized()) {
        fogColor = ctx_.skyRenderer->getRecommendedFogColor();
    }

    ctx_.driver->setFog(
        fogColor,
        irr::video::EFT_FOG_LINEAR,
        fogStart,
        fogEnd,
        0.01f,
        true,   // Pixel fog
        false   // Range fog
    );

    // Update shader fog uniforms
    if (ctx_.zoneShader && ctx_.zoneShader->isAvailable()) {
        ctx_.zoneShader->setFog(fogStart, fogEnd,
                            fogColor.getRed() / 255.0f,
                            fogColor.getGreen() / 255.0f,
                            fogColor.getBlue() / 255.0f,
                            fogColor.getAlpha() / 255.0f);
    }

    LOG_INFO(MOD_GRAPHICS, "Fog: start={:.0f}, end={:.0f} (renderDistance={:.0f}, fogThickness={:.0f})",
        fogStart, fogEnd, ctx_.renderDistance, ctx_.fogThickness);
}

void OverlaysComponent::drawLoadingScreen(float progress, const std::wstring& stageText) {
    if (!ctx_.driver || !ctx_.device) return;

    // Debug log loading progress (when debug level > 0)
    float clampedProgressForLog = std::max(0.0f, std::min(1.0f, progress));
    int percentComplete = static_cast<int>(clampedProgressForLog * 100);

    // Convert wide strings to narrow strings for logging
    std::string stageTextNarrow(stageText.begin(), stageText.end());
    std::string titleNarrow(loadingTitle_.begin(), loadingTitle_.end());

    LOG_DEBUG(MOD_GRAPHICS_LOAD, "[Loading] {} - {} ({}%)", titleNarrow, stageTextNarrow, percentComplete);

    // Immediately render a frame showing loading progress
    ctx_.driver->beginScene(true, true, irr::video::SColor(255, 20, 20, 40));

    irr::core::dimension2d<irr::u32> screenSize = ctx_.driver->getScreenSize();

    // Progress bar dimensions
    const int barWidth = 400;
    const int barHeight = 30;
    const int barX = (screenSize.Width - barWidth) / 2;
    const int barY = (screenSize.Height / 2) + 20;

    // Colors
    irr::video::SColor bgColor(255, 40, 40, 60);
    irr::video::SColor borderColor(255, 100, 100, 120);
    irr::video::SColor fillColor(255, 80, 120, 200);

    // Draw border
    ctx_.driver->draw2DRectangle(borderColor,
        irr::core::recti(barX - 2, barY - 2, barX + barWidth + 2, barY + barHeight + 2));

    // Draw background
    ctx_.driver->draw2DRectangle(bgColor,
        irr::core::recti(barX, barY, barX + barWidth, barY + barHeight));

    // Draw progress fill
    float clampedProgress = std::max(0.0f, std::min(1.0f, progress));
    int fillWidth = static_cast<int>(barWidth * clampedProgress);
    if (fillWidth > 0) {
        ctx_.driver->draw2DRectangle(fillColor,
            irr::core::recti(barX, barY, barX + fillWidth, barY + barHeight));
    }

    // Draw text using built-in font
    irr::gui::IGUIFont* font = guienv_ ? guienv_->getBuiltInFont() : nullptr;
    if (font) {
        // Dynamic title (e.g., "EverQuest", "Connecting...", "Loading Zone...")
        irr::core::dimension2d<irr::u32> titleSize = font->getDimension(loadingTitle_.c_str());
        int titleX = (screenSize.Width - titleSize.Width) / 2;
        int titleY = barY - 40;
        font->draw(loadingTitle_.c_str(),
            irr::core::recti(titleX, titleY, titleX + titleSize.Width, titleY + titleSize.Height),
            irr::video::SColor(255, 255, 255, 255));

        // Stage text below progress bar
        irr::core::dimension2d<irr::u32> stageSize = font->getDimension(stageText.c_str());
        int stageX = (screenSize.Width - stageSize.Width) / 2;
        int stageY = barY + barHeight + 10;
        font->draw(stageText.c_str(),
            irr::core::recti(stageX, stageY, stageX + stageSize.Width, stageY + stageSize.Height),
            irr::video::SColor(255, 200, 200, 200));

        // Percentage text centered in progress bar
        std::wstring pctText = std::to_wstring(static_cast<int>(clampedProgress * 100)) + L"%";
        irr::core::dimension2d<irr::u32> pctSize = font->getDimension(pctText.c_str());
        int pctX = (screenSize.Width - pctSize.Width) / 2;
        int pctY = barY + (barHeight - pctSize.Height) / 2;
        font->draw(pctText.c_str(),
            irr::core::recti(pctX, pctY, pctX + pctSize.Width, pctY + pctSize.Height),
            irr::video::SColor(255, 255, 255, 255));
    }

    ctx_.driver->endScene();
}

void OverlaysComponent::applyEnvironmentalDisplaySettings() {
    const auto& settings = owner_.cachedDisplaySettings_;
    bool zoneLoaded = !ctx_.currentZoneName.empty();

    // --- Particle Manager: toggle atmospheric billboard particles ---
    // ParticleManager is always created (in loadGlobalAssets) for unified fire support.
    // This toggle controls only the atmospheric billboard emitters (dust, pollen, etc.)
    if (ctx_.particleManager) {
        ctx_.particleManager->setEnabled(settings.atmosphericParticles);
    }

    // Lazily create weather effects if not yet created and particles are available
    if (!ctx_.weatherEffects && ctx_.particleManager) {
        LOG_TRACE(MOD_GRAPHICS, "Lazy-creating weather effects controller...");
        ctx_.weatherEffects = std::make_unique<WeatherEffectsController>(
            ctx_.smgr, ctx_.driver, ctx_.particleManager.get(), ctx_.skyRenderer.get());
        if (ctx_.constrainedTextureCache) {
            ctx_.weatherEffects->setConstrainedTextureCache(ctx_.constrainedTextureCache);
        }
        LOG_TRACE(MOD_GRAPHICS, "Lazy-initializing weather effects (loading textures)...");
        if (!ctx_.weatherEffects->initialize(ctx_.config.eqClientPath)) {
            LOG_WARN(MOD_GRAPHICS, "Failed to initialize weather effects controller (lazy)");
        }
        if (ctx_.weatherSystem) {
            ctx_.weatherSystem->addListener(ctx_.weatherEffects.get());
        }
        LOG_INFO(MOD_GRAPHICS, "Weather effects created (toggled on via settings)");
    }

    if (ctx_.particleManager) {
        Environment::EffectQuality quality;
        switch (settings.environmentQuality) {
            case eqt::ui::EffectQuality::Off:
                quality = Environment::EffectQuality::Off;
                break;
            case eqt::ui::EffectQuality::Low:
                quality = Environment::EffectQuality::Low;
                break;
            case eqt::ui::EffectQuality::Medium:
                quality = Environment::EffectQuality::Medium;
                break;
            case eqt::ui::EffectQuality::High:
                quality = Environment::EffectQuality::High;
                break;
            default:
                quality = Environment::EffectQuality::Medium;
                break;
        }

        ctx_.particleManager->setQuality(quality);
        ctx_.particleManager->setEnabled(settings.atmosphericParticles);
        ctx_.particleManager->setDensity(settings.environmentDensity);
        ctx_.particleManager->setTypeEnabled(Environment::ParticleType::Ember, settings.fireEffects);
        ctx_.particleManager->setTypeEnabled(Environment::ParticleType::Smoke, settings.fireEffects);

        LOG_DEBUG(MOD_GRAPHICS, "Applied particle settings: quality={}, enabled={}, density={}, fire={}",
                 static_cast<int>(quality), settings.atmosphericParticles, settings.environmentDensity, settings.fireEffects);
    }

    // Fire effects (light flickering)
    ctx_.fireEffectsEnabled = settings.fireEffects;

    // --- Boids Manager: lazy create if toggled on ---
    if (settings.ambientCreatures && !ctx_.boidsManager && ctx_.smgr && ctx_.driver) {
        ctx_.boidsManager = std::make_unique<Environment::BoidsManager>(ctx_.smgr, ctx_.driver);
        if (ctx_.constrainedTextureCache) ctx_.boidsManager->setConstrainedTextureCache(ctx_.constrainedTextureCache);  // T124
        if (!ctx_.boidsManager->init(ctx_.config.eqClientPath)) {
            LOG_WARN(MOD_GRAPHICS, "Failed to initialize boids manager (lazy)");
        } else {
            LOG_INFO(MOD_GRAPHICS, "Boids manager created (toggled on via settings)");
        }
        ctx_.ZoneStream().ensureBoidsTextureGuard();
        if (zoneLoaded) {
            Environment::ZoneBiome biome = Environment::ZoneBiomeDetector::instance().getBiome(ctx_.currentZoneName);
            if (ctx_.zoneBoundsValid) {
                ctx_.boidsManager->onZoneEnter(ctx_.currentZoneName, biome,
                    glm::vec3(ctx_.zoneBoundsMinX, ctx_.zoneBoundsMinY, -1000.0f),
                    glm::vec3(ctx_.zoneBoundsMaxX, ctx_.zoneBoundsMaxY, 1000.0f));
            } else {
                ctx_.boidsManager->onZoneEnter(ctx_.currentZoneName, biome);
            }
        }
    }

    if (ctx_.boidsManager) {
        int quality = static_cast<int>(settings.environmentQuality);
        ctx_.boidsManager->setQuality(quality);
        ctx_.boidsManager->setEnabled(settings.ambientCreatures);
        ctx_.boidsManager->setDensity(settings.environmentDensity);

        LOG_DEBUG(MOD_GRAPHICS, "Applied boids settings: quality={}, enabled={}, density={}",
                 quality, settings.ambientCreatures, settings.environmentDensity);
    }

    // --- Detail Manager: lazy create if toggled on ---
    if (settings.detailObjectsEnabled && !ctx_.detailManager && ctx_.smgr && ctx_.driver) {
        ctx_.detailManager = std::make_unique<Detail::DetailManager>(ctx_.smgr, ctx_.driver);
        ctx_.detailManager->setSurfaceMapsPath("data/detail/zones");
        if (ctx_.constrainedTextureCache) {
            ctx_.detailManager->setConstrainedTextureCache(ctx_.constrainedTextureCache);
        }
        LOG_INFO(MOD_GRAPHICS, "Detail manager created (toggled on via settings)");

    }

    if (ctx_.detailManager) {
        ctx_.detailManager->setEnabled(settings.detailObjectsEnabled);
        ctx_.detailManager->setDensity(settings.detailDensity);
        ctx_.detailManager->setCategoryEnabled(Detail::DetailCategory::Grass, settings.detailGrass);
        ctx_.detailManager->setCategoryEnabled(Detail::DetailCategory::Plants, settings.detailPlants);
        ctx_.detailManager->setCategoryEnabled(Detail::DetailCategory::Rocks, settings.detailRocks);
        ctx_.detailManager->setCategoryEnabled(Detail::DetailCategory::Debris, settings.detailDebris);

        auto foliageConfig = ctx_.detailManager->getFoliageDisturbanceConfig();
        if (foliageConfig.enabled != settings.reactiveFoliage) {
            foliageConfig.enabled = settings.reactiveFoliage;
            ctx_.detailManager->setFoliageDisturbanceConfig(foliageConfig);
        }

        LOG_DEBUG(MOD_GRAPHICS, "Applied detail settings: enabled={}, density={:.2f}, grass={}, plants={}, rocks={}, debris={}, reactiveFoliage={}",
                 settings.detailObjectsEnabled, settings.detailDensity,
                 settings.detailGrass, settings.detailPlants, settings.detailRocks, settings.detailDebris,
                 settings.reactiveFoliage);
    }

    // --- Tumbleweed Manager: lazy create if toggled on ---
    if (settings.rollingObjects && !ctx_.tumbleweedManager && ctx_.smgr && ctx_.driver) {
        ctx_.tumbleweedManager = std::make_unique<Environment::TumbleweedManager>(ctx_.smgr, ctx_.driver);
        if (!ctx_.tumbleweedManager->init()) {
            LOG_WARN(MOD_GRAPHICS, "Failed to initialize tumbleweed manager (lazy)");
        } else {
            LOG_INFO(MOD_GRAPHICS, "Tumbleweed manager created (toggled on via settings)");
        }
        if (zoneLoaded) {
            Environment::ZoneBiome biome = Environment::ZoneBiomeDetector::instance().getBiome(ctx_.currentZoneName);
            ctx_.tumbleweedManager->onZoneEnter(ctx_.currentZoneName, biome);
        }
    }

    if (ctx_.tumbleweedManager) {
        ctx_.tumbleweedManager->setEnabled(settings.rollingObjects);

        LOG_DEBUG(MOD_GRAPHICS, "Applied tumbleweed settings: enabled={}",
                 settings.rollingObjects);
    }

    // --- Sky Renderer: toggle based on setting + indoor zone state ---
    if (ctx_.skyRenderer) {
        bool skyAllowed = !ctx_.isIndoorZone && settings.skyEnabled;
        ctx_.skyRenderer->setEnabled(skyAllowed);
        LOG_DEBUG(MOD_GRAPHICS, "Applied sky settings: enabled={} (setting={}, indoor={})",
                 skyAllowed, settings.skyEnabled, ctx_.isIndoorZone);
    }

    // --- Tree Wind Controller: toggle wind animation ---
    ctx_.treeWindController.setEnabled(settings.animatedTrees);
    LOG_DEBUG(MOD_GRAPHICS, "Applied tree animation settings: enabled={}", settings.animatedTrees);

    // --- Fire Glow Lighting + Icospheres ---
    // fireEffects is the master gate for all fire-based effects
    ctx_.fireGlowLightingEnabled = settings.fireEffects && settings.enableFireGlowLighting;
    ctx_.fireGlowIcospheresEnabled = settings.fireEffects && settings.enableFireGlowIcospheres;
    ctx_.maxFireGlowLights = settings.maxFireGlowLights;
    if (!ctx_.fireGlowLightingEnabled && ctx_.zoneShader) {
        ctx_.zoneShader->clearFireGlowLights();
    }
}

void OverlaysComponent::setupHUD() {
    if (!guienv_) return;

    // Main HUD in upper left - made taller to show player + target info
    hudText_ = guienv_->addStaticText(
        L"",
        irr::core::rect<irr::s32>(10, 10, 450, 500),
        false,
        true,
        nullptr,
        -1,
        false
    );

    if (hudText_) {
        hudText_->setOverrideColor(irr::video::SColor(255, 255, 255, 255));
    }

    // Hotkey hints in upper right
    int screenWidth = ctx_.config.width;
    hotkeysText_ = guienv_->addStaticText(
        L"",
        irr::core::rect<irr::s32>(screenWidth - 400, 10, screenWidth - 10, 80),
        false,
        true,
        nullptr,
        -1,
        false
    );

    if (hotkeysText_) {
        hotkeysText_->setOverrideColor(irr::video::SColor(255, 200, 200, 200));  // Slightly dimmer
        hotkeysText_->setTextAlignment(irr::gui::EGUIA_LOWERRIGHT, irr::gui::EGUIA_UPPERLEFT);
    }

    // Heading debug info centered at top (for Player mode)
    int centerX = screenWidth / 2;
    headingDebugText_ = guienv_->addStaticText(
        L"",
        irr::core::rect<irr::s32>(centerX - 175, 10, centerX + 175, 150),
        false,
        true,
        nullptr,
        -1,
        false
    );

    if (headingDebugText_) {
        headingDebugText_->setOverrideColor(irr::video::SColor(255, 255, 255, 0));  // Yellow for visibility
        headingDebugText_->setTextAlignment(irr::gui::EGUIA_CENTER, irr::gui::EGUIA_UPPERLEFT);
    }
}

void OverlaysComponent::updateHUD() {
    if (!hudText_ || !ctx_.Diagnostics().hudEnabled()) return;

    // Performance optimization: check if HUD state has changed
    // Build current state snapshot for comparison
    HudCachedState currentState;
    currentState.fps = ctx_.Frame().currentFps();
    currentState.playerX = static_cast<int>(ctx_.playerX);
    currentState.playerY = static_cast<int>(ctx_.playerY);
    currentState.playerZ = static_cast<int>(ctx_.playerZ);
    if (ctx_.entityRenderer) {
        currentState.entityCount = ctx_.entityRenderer->getEntityCount();
        currentState.modeledEntityCount = ctx_.entityRenderer->getModeledEntityCount();
    }
    currentState.targetId = currentTargetId_;
    currentState.targetHpPercent = currentTargetHpPercent_;
    currentState.wireframeMode = ctx_.wireframeMode;
    currentState.oldModels = ctx_.Diagnostics().isUsingOldModels();
    currentState.cameraMode = owner_.getCameraModeString();
    currentState.zoneName = ctx_.currentZoneName;

    // Check if state has changed
    bool stateChanged = (currentState.fps != hudCachedState_.fps ||
                         currentState.playerX != hudCachedState_.playerX ||
                         currentState.playerY != hudCachedState_.playerY ||
                         currentState.playerZ != hudCachedState_.playerZ ||
                         currentState.entityCount != hudCachedState_.entityCount ||
                         currentState.modeledEntityCount != hudCachedState_.modeledEntityCount ||
                         currentState.targetId != hudCachedState_.targetId ||
                         currentState.targetHpPercent != hudCachedState_.targetHpPercent ||
                         currentState.wireframeMode != hudCachedState_.wireframeMode ||
                         currentState.oldModels != hudCachedState_.oldModels ||
                         currentState.cameraMode != hudCachedState_.cameraMode ||
                         currentState.zoneName != hudCachedState_.zoneName);

    // Skip rebuild if nothing changed
    if (!stateChanged) {
        return;
    }

    // Update cached state
    hudCachedState_ = currentState;

    std::wstringstream text;
    std::wstringstream hotkeys;

    // Heading debug text (right side, used in Player mode)
    std::wstringstream headingDebug;

    {
        // === PLAYER MODE HUD ===
        // Show heading debug info for current target (on right side)
        if (currentTargetId_ != 0 && ctx_.entityRenderer) {
            const auto& entities = ctx_.entityRenderer->getEntities();
            auto it = entities.find(currentTargetId_);
            if (it != entities.end()) {
                const EntityVisual& visual = it->second;
                headingDebug << L"--- TARGET HEADING DEBUG ---\n";
                // Entity position (EQ coords: x, y, z)
                headingDebug << L"Pos: (" << static_cast<int>(visual.serverX)
                     << L", " << static_cast<int>(visual.serverY)
                     << L", " << static_cast<int>(visual.serverZ) << L")\n";
                // Server heading (from entity data, degrees 0-360)
                wchar_t hdgBuf[64];
                swprintf(hdgBuf, 64, L"Server Heading: %.1f deg\n", visual.serverHeading);
                headingDebug << hdgBuf;
                // Model rotation (from Irrlicht scene node)
                if (visual.sceneNode) {
                    irr::core::vector3df rot = visual.sceneNode->getRotation();
                    swprintf(hdgBuf, 64, L"Model Rotation: (%.1f, %.1f, %.1f)\n",
                             rot.X, rot.Y, rot.Z);
                    headingDebug << hdgBuf;
                }
                // Interpolated heading (visual.lastHeading)
                swprintf(hdgBuf, 64, L"Interp Heading: %.1f deg\n", visual.lastHeading);
                headingDebug << hdgBuf;
            }
        }
    }

    hudText_->setText(text.str().c_str());
    if (hotkeysText_) {
        hotkeysText_->setText(hotkeys.str().c_str());
    }
    if (headingDebugText_) {
        headingDebugText_->setText(headingDebug.str().c_str());
    }
}

// ============================================================================
// T60: Inventory paperdoll (CharacterModelView)
// ============================================================================

void OverlaysComponent::markPaperdollDirty() {
    if (characterModelView_) characterModelView_->markDirty();
}

// T457 (board B482): see the header. Called from
// ZoneLifecycleComponent::unloadZone() before either owner of the doll's
// resources destroys them.
void OverlaysComponent::onZoneUnload() {
    if (characterModelView_) {
        characterModelView_->releaseCharacterModel();
    }

    // Arm the rebuild. updatePaperdollForFrame() only calls setCharacter() when
    // the player's appearance differs from the one it last used -- and a zone
    // change does not change the player's appearance, so without this reset the
    // doll would be released and never rebuilt, leaving an empty panel for the
    // rest of the session. paperdollLastRaceId_/Gender_/Appearance_ are left as
    // they are: paperdollHasAppearance_ is the gate the `changed` expression
    // reads first, and it short-circuits the rest.
    paperdollHasAppearance_ = false;

    // NOTE: characterModelView_ itself is deliberately NOT destroyed, and
    // paperdollInitAttempted_ is deliberately NOT reset. The view owns its
    // render target and its scene manager, neither of which is a zone asset;
    // rebuilding them every zone change would re-run addRenderTargetTexture()
    // for no reason. Only the MODEL is zone-scoped.
}

void OverlaysComponent::ensurePaperdollInitialized() {
    if (characterModelView_ || paperdollInitAttempted_) {
        return;
    }
    paperdollInitAttempted_ = true;

    if (!ctx_.smgr || !ctx_.driver) {
        LOG_WARN(MOD_UI, "IrrlichtRenderer: paperdoll init skipped, no scene manager/driver yet");
        return;
    }

    // Size and proportion come from the rect the doll is blitted into —
    // InventoryLayout::paperdollRenderTargetSize(), which is
    // paperdollRect() supersampled. Constant regardless of screen
    // resolution, since centerPopup is a fixed-size box whose ORIGIN, not
    // size, varies with resolution (see inventory_layout.h). T409 narrowed
    // the rect to the oracle's IW_CharacterView proportion (board B219),
    // so the old hardcoded 304x256 would now squash the model.
    const auto rt = InventoryLayout::paperdollRenderTargetSize();

    auto doll = std::make_unique<eqt::ui::CharacterModelView>();
    doll->init(ctx_.smgr, ctx_.driver, static_cast<int>(rt.Width),
               static_cast<int>(rt.Height));
    if (ctx_.entityRenderer) {
        doll->setRaceModelLoader(ctx_.entityRenderer->getRaceModelLoader());
        doll->setEquipmentModelLoader(ctx_.entityRenderer->getEquipmentModelLoader());
    }
    // No InventoryManager is wired here (S2 decoupled the renderer from it
    // entirely — IrrlichtRenderer holds no InventoryManager pointer to
    // pass on; inventory UI state arrives only via bridge events). This is
    // fine: the doll's appearance comes from the player's live
    // EntityAppearance (updatePaperdollForFrame(), below), matching Step
    // 4 of the plan, not from inventory item materials.
    characterModelView_ = std::move(doll);
    LOG_INFO(MOD_UI, "IrrlichtRenderer: paperdoll (CharacterModelView) constructed lazily on first popup open");
}

namespace {
bool appearanceEquals(const EQT::Graphics::EntityAppearance& a, const EQT::Graphics::EntityAppearance& b) {
    if (a.face != b.face || a.haircolor != b.haircolor || a.hairstyle != b.hairstyle ||
        a.beardcolor != b.beardcolor || a.beard != b.beard || a.texture != b.texture ||
        a.helm != b.helm) {
        return false;
    }
    for (int i = 0; i < 9; ++i) {
        if (a.equipment[i] != b.equipment[i] || a.equipment_tint[i] != b.equipment_tint[i]) {
            return false;
        }
    }
    return true;
}

// T462: the paperdoll's own share of the Static UI section, as a permanent
// frame-timing sub-section (T459 measured it with a temporary INFO timer).
// renderStaticUI() is pinned statement-for-statement by the G05 frame-render
// golden, so the time is taken here, in the callee, and handed over by
// takePaperdollUpdateUs(). Render thread only.
struct PaperdollUpdateTimer {
    int64_t& sink;
    std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
    explicit PaperdollUpdateTimer(int64_t& s) : sink(s) {}
    ~PaperdollUpdateTimer() {
        sink += std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now() - start).count();
    }
};
} // namespace

int64_t OverlaysComponent::takePaperdollUpdateUs() {
    const int64_t us = paperdollUpdateUs_;
    paperdollUpdateUs_ = 0;
    return us;
}

void OverlaysComponent::updatePaperdollForFrame() {
    PaperdollUpdateTimer updateTimer(paperdollUpdateUs_);
    ensurePaperdollInitialized();
    if (!characterModelView_ || !ctx_.entityRenderer) {
        return;
    }

    // T60 (Step 4): find the player's own in-world entity and feed the
    // doll the SAME EntityAppearance it renders with in 3rd person — not a
    // separately-reconstructed one — so the doll can never visually
    // disagree with what the player actually sees.
    const EQT::Graphics::EntityVisual* playerVisual = nullptr;
    for (const auto& [spawnId, visual] : ctx_.entityRenderer->getEntities()) {
        if (visual.isPlayer) {
            playerVisual = &visual;
            break;
        }
    }
    if (!playerVisual) {
        return;  // Not zoned in / player entity not built yet — nothing to show.
    }

    const bool changed = !paperdollHasAppearance_ ||
        playerVisual->raceId != paperdollLastRaceId_ ||
        playerVisual->gender != paperdollLastGender_ ||
        !appearanceEquals(playerVisual->appearance, paperdollLastAppearance_);

    if (changed) {
        paperdollLastRaceId_ = playerVisual->raceId;
        paperdollLastGender_ = playerVisual->gender;
        paperdollLastAppearance_ = playerVisual->appearance;
        paperdollHasAppearance_ = true;
        // setCharacter() rebuilds the model and marks the doll dirty
        // internally — only call it when something actually changed.
        characterModelView_->setCharacter(playerVisual->raceId, playerVisual->gender,
                                           playerVisual->appearance);
    }

    // update()/renderToTexture() are cheap no-ops when not dirty — safe to
    // call every frame the popup is open; the RTT itself only re-renders
    // on markDirty() triggers (appearance change, popup open, drag-rotate).
    characterModelView_->update(16.0f);  // Approximate frame delta for idle-anim throttling
    characterModelView_->renderToTexture();
}

} // namespace Graphics
} // namespace EQT
