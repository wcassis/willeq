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

void IrrlichtRenderer::setupFog() {
    if (!driver_) {
        return;
    }

    // Unified fog system:
    // - fogEnd = renderDistance_ (absolute visibility limit)
    // - fogStart = renderDistance_ - fogThickness_ (where fade begins)
    // - Everything beyond renderDistance_ is culled, nothing renders there
    // - Fog creates a smooth fade zone at the edge
    float fogEnd = renderDistance_;
    float fogStart = renderDistance_ - fogThickness_;
    fogStart = std::max(0.0f, fogStart);  // Ensure non-negative

    // Get fog color from sky renderer if available, otherwise use default
    irr::video::SColor fogColor(255, 128, 128, 160);  // Default: light gray-blue
    if (skyRenderer_ && skyRenderer_->isInitialized()) {
        fogColor = skyRenderer_->getRecommendedFogColor();
    }

    driver_->setFog(
        fogColor,
        irr::video::EFT_FOG_LINEAR,
        fogStart,
        fogEnd,
        0.01f,
        true,   // Pixel fog
        false   // Range fog
    );

    // Update shader fog uniforms
    if (zoneShader_ && zoneShader_->isAvailable()) {
        zoneShader_->setFog(fogStart, fogEnd,
                            fogColor.getRed() / 255.0f,
                            fogColor.getGreen() / 255.0f,
                            fogColor.getBlue() / 255.0f,
                            fogColor.getAlpha() / 255.0f);
    }

    LOG_INFO(MOD_GRAPHICS, "Fog: start={:.0f}, end={:.0f} (renderDistance={:.0f}, fogThickness={:.0f})",
        fogStart, fogEnd, renderDistance_, fogThickness_);
}

void IrrlichtRenderer::drawLoadingScreen(float progress, const std::wstring& stageText) {
    if (!driver_ || !device_) return;

    // Debug log loading progress (when debug level > 0)
    float clampedProgressForLog = std::max(0.0f, std::min(1.0f, progress));
    int percentComplete = static_cast<int>(clampedProgressForLog * 100);

    // Convert wide strings to narrow strings for logging
    std::string stageTextNarrow(stageText.begin(), stageText.end());
    std::string titleNarrow(loadingTitle_.begin(), loadingTitle_.end());

    LOG_DEBUG(MOD_GRAPHICS_LOAD, "[Loading] {} - {} ({}%)", titleNarrow, stageTextNarrow, percentComplete);

    // Immediately render a frame showing loading progress
    driver_->beginScene(true, true, irr::video::SColor(255, 20, 20, 40));

    irr::core::dimension2d<irr::u32> screenSize = driver_->getScreenSize();

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
    driver_->draw2DRectangle(borderColor,
        irr::core::recti(barX - 2, barY - 2, barX + barWidth + 2, barY + barHeight + 2));

    // Draw background
    driver_->draw2DRectangle(bgColor,
        irr::core::recti(barX, barY, barX + barWidth, barY + barHeight));

    // Draw progress fill
    float clampedProgress = std::max(0.0f, std::min(1.0f, progress));
    int fillWidth = static_cast<int>(barWidth * clampedProgress);
    if (fillWidth > 0) {
        driver_->draw2DRectangle(fillColor,
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

    driver_->endScene();
}

void IrrlichtRenderer::applyEnvironmentalDisplaySettings() {
    const auto& settings = cachedDisplaySettings_;
    bool zoneLoaded = !currentZoneName_.empty();

    // --- Particle Manager: toggle atmospheric billboard particles ---
    // ParticleManager is always created (in loadGlobalAssets) for unified fire support.
    // This toggle controls only the atmospheric billboard emitters (dust, pollen, etc.)
    if (particleManager_) {
        particleManager_->setEnabled(settings.atmosphericParticles);
    }

    // Lazily create weather effects if not yet created and particles are available
    if (!weatherEffects_ && particleManager_) {
        LOG_TRACE(MOD_GRAPHICS, "Lazy-creating weather effects controller...");
        weatherEffects_ = std::make_unique<WeatherEffectsController>(
            smgr_, driver_, particleManager_.get(), skyRenderer_.get());
        if (constrainedTextureCache_) {
            weatherEffects_->setConstrainedTextureCache(constrainedTextureCache_);
        }
        LOG_TRACE(MOD_GRAPHICS, "Lazy-initializing weather effects (loading textures)...");
        if (!weatherEffects_->initialize(config_.eqClientPath)) {
            LOG_WARN(MOD_GRAPHICS, "Failed to initialize weather effects controller (lazy)");
        }
        if (weatherSystem_) {
            weatherSystem_->addListener(weatherEffects_.get());
        }
        LOG_INFO(MOD_GRAPHICS, "Weather effects created (toggled on via settings)");
    }

    if (particleManager_) {
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

        particleManager_->setQuality(quality);
        particleManager_->setEnabled(settings.atmosphericParticles);
        particleManager_->setDensity(settings.environmentDensity);
        particleManager_->setTypeEnabled(Environment::ParticleType::Ember, settings.fireEffects);
        particleManager_->setTypeEnabled(Environment::ParticleType::Smoke, settings.fireEffects);

        LOG_DEBUG(MOD_GRAPHICS, "Applied particle settings: quality={}, enabled={}, density={}, fire={}",
                 static_cast<int>(quality), settings.atmosphericParticles, settings.environmentDensity, settings.fireEffects);
    }

    // Fire effects (light flickering)
    fireEffectsEnabled_ = settings.fireEffects;

    // --- Boids Manager: lazy create if toggled on ---
    if (settings.ambientCreatures && !boidsManager_ && smgr_ && driver_) {
        boidsManager_ = std::make_unique<Environment::BoidsManager>(smgr_, driver_);
        if (constrainedTextureCache_) boidsManager_->setConstrainedTextureCache(constrainedTextureCache_);  // T124
        if (!boidsManager_->init(config_.eqClientPath)) {
            LOG_WARN(MOD_GRAPHICS, "Failed to initialize boids manager (lazy)");
        } else {
            LOG_INFO(MOD_GRAPHICS, "Boids manager created (toggled on via settings)");
        }
        ensureBoidsTextureGuard();
        if (zoneLoaded) {
            Environment::ZoneBiome biome = Environment::ZoneBiomeDetector::instance().getBiome(currentZoneName_);
            if (zoneBoundsValid_) {
                boidsManager_->onZoneEnter(currentZoneName_, biome,
                    glm::vec3(zoneBoundsMinX_, zoneBoundsMinY_, -1000.0f),
                    glm::vec3(zoneBoundsMaxX_, zoneBoundsMaxY_, 1000.0f));
            } else {
                boidsManager_->onZoneEnter(currentZoneName_, biome);
            }
        }
    }

    if (boidsManager_) {
        int quality = static_cast<int>(settings.environmentQuality);
        boidsManager_->setQuality(quality);
        boidsManager_->setEnabled(settings.ambientCreatures);
        boidsManager_->setDensity(settings.environmentDensity);

        LOG_DEBUG(MOD_GRAPHICS, "Applied boids settings: quality={}, enabled={}, density={}",
                 quality, settings.ambientCreatures, settings.environmentDensity);
    }

    // --- Detail Manager: lazy create if toggled on ---
    if (settings.detailObjectsEnabled && !detailManager_ && smgr_ && driver_) {
        detailManager_ = std::make_unique<Detail::DetailManager>(smgr_, driver_);
        detailManager_->setSurfaceMapsPath("data/detail/zones");
        if (constrainedTextureCache_) {
            detailManager_->setConstrainedTextureCache(constrainedTextureCache_);
        }
        LOG_INFO(MOD_GRAPHICS, "Detail manager created (toggled on via settings)");

    }

    if (detailManager_) {
        detailManager_->setEnabled(settings.detailObjectsEnabled);
        detailManager_->setDensity(settings.detailDensity);
        detailManager_->setCategoryEnabled(Detail::DetailCategory::Grass, settings.detailGrass);
        detailManager_->setCategoryEnabled(Detail::DetailCategory::Plants, settings.detailPlants);
        detailManager_->setCategoryEnabled(Detail::DetailCategory::Rocks, settings.detailRocks);
        detailManager_->setCategoryEnabled(Detail::DetailCategory::Debris, settings.detailDebris);

        auto foliageConfig = detailManager_->getFoliageDisturbanceConfig();
        if (foliageConfig.enabled != settings.reactiveFoliage) {
            foliageConfig.enabled = settings.reactiveFoliage;
            detailManager_->setFoliageDisturbanceConfig(foliageConfig);
        }

        LOG_DEBUG(MOD_GRAPHICS, "Applied detail settings: enabled={}, density={:.2f}, grass={}, plants={}, rocks={}, debris={}, reactiveFoliage={}",
                 settings.detailObjectsEnabled, settings.detailDensity,
                 settings.detailGrass, settings.detailPlants, settings.detailRocks, settings.detailDebris,
                 settings.reactiveFoliage);
    }

    // --- Tumbleweed Manager: lazy create if toggled on ---
    if (settings.rollingObjects && !tumbleweedManager_ && smgr_ && driver_) {
        tumbleweedManager_ = std::make_unique<Environment::TumbleweedManager>(smgr_, driver_);
        if (!tumbleweedManager_->init()) {
            LOG_WARN(MOD_GRAPHICS, "Failed to initialize tumbleweed manager (lazy)");
        } else {
            LOG_INFO(MOD_GRAPHICS, "Tumbleweed manager created (toggled on via settings)");
        }
        if (zoneLoaded) {
            Environment::ZoneBiome biome = Environment::ZoneBiomeDetector::instance().getBiome(currentZoneName_);
            tumbleweedManager_->onZoneEnter(currentZoneName_, biome);
        }
    }

    if (tumbleweedManager_) {
        tumbleweedManager_->setEnabled(settings.rollingObjects);

        LOG_DEBUG(MOD_GRAPHICS, "Applied tumbleweed settings: enabled={}",
                 settings.rollingObjects);
    }

    // --- Sky Renderer: toggle based on setting + indoor zone state ---
    if (skyRenderer_) {
        bool skyAllowed = !isIndoorZone_ && settings.skyEnabled;
        skyRenderer_->setEnabled(skyAllowed);
        LOG_DEBUG(MOD_GRAPHICS, "Applied sky settings: enabled={} (setting={}, indoor={})",
                 skyAllowed, settings.skyEnabled, isIndoorZone_);
    }

    // --- Tree Wind Controller: toggle wind animation ---
    treeWindController_.setEnabled(settings.animatedTrees);
    LOG_DEBUG(MOD_GRAPHICS, "Applied tree animation settings: enabled={}", settings.animatedTrees);

    // --- Fire Glow Lighting + Icospheres ---
    // fireEffects is the master gate for all fire-based effects
    fireGlowLightingEnabled_ = settings.fireEffects && settings.enableFireGlowLighting;
    fireGlowIcospheresEnabled_ = settings.fireEffects && settings.enableFireGlowIcospheres;
    maxFireGlowLights_ = settings.maxFireGlowLights;
    if (!fireGlowLightingEnabled_ && zoneShader_) {
        zoneShader_->clearFireGlowLights();
    }
}

void IrrlichtRenderer::setupHUD() {
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
    int screenWidth = config_.width;
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

void IrrlichtRenderer::updateHUD() {
    if (!hudText_ || !hudEnabled_) return;

    // Performance optimization: check if HUD state has changed
    // Build current state snapshot for comparison
    HudCachedState currentState;
    currentState.fps = currentFps_;
    currentState.playerX = static_cast<int>(playerX_);
    currentState.playerY = static_cast<int>(playerY_);
    currentState.playerZ = static_cast<int>(playerZ_);
    if (entityRenderer_) {
        currentState.entityCount = entityRenderer_->getEntityCount();
        currentState.modeledEntityCount = entityRenderer_->getModeledEntityCount();
    }
    currentState.targetId = currentTargetId_;
    currentState.targetHpPercent = currentTargetHpPercent_;
    currentState.wireframeMode = wireframeMode_;
    currentState.oldModels = isUsingOldModels();
    currentState.cameraMode = getCameraModeString();
    currentState.zoneName = currentZoneName_;

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
        if (currentTargetId_ != 0 && entityRenderer_) {
            const auto& entities = entityRenderer_->getEntities();
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

void IrrlichtRenderer::markPaperdollDirty() {
    if (characterModelView_) characterModelView_->markDirty();
}

void IrrlichtRenderer::ensurePaperdollInitialized() {
    if (characterModelView_ || paperdollInitAttempted_) {
        return;
    }
    paperdollInitAttempted_ = true;

    if (!smgr_ || !driver_) {
        LOG_WARN(MOD_UI, "IrrlichtRenderer: paperdoll init skipped, no scene manager/driver yet");
        return;
    }

    // Fixed size matching InventoryLayout::paperdollRect()'s dimensions
    // (304x256 — constant regardless of screen resolution, since
    // centerPopup is a fixed-size box whose ORIGIN, not size, varies with
    // resolution — see inventory_layout.h).
    constexpr int kPaperdollWidth = 304;
    constexpr int kPaperdollHeight = 256;

    auto doll = std::make_unique<eqt::ui::CharacterModelView>();
    doll->init(smgr_, driver_, kPaperdollWidth, kPaperdollHeight);
    if (entityRenderer_) {
        doll->setRaceModelLoader(entityRenderer_->getRaceModelLoader());
        doll->setEquipmentModelLoader(entityRenderer_->getEquipmentModelLoader());
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
} // namespace

void IrrlichtRenderer::updatePaperdollForFrame() {
    ensurePaperdollInitialized();
    if (!characterModelView_ || !entityRenderer_) {
        return;
    }

    // T60 (Step 4): find the player's own in-world entity and feed the
    // doll the SAME EntityAppearance it renders with in 3rd person — not a
    // separately-reconstructed one — so the doll can never visually
    // disagree with what the player actually sees.
    const EQT::Graphics::EntityVisual* playerVisual = nullptr;
    for (const auto& [spawnId, visual] : entityRenderer_->getEntities()) {
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
