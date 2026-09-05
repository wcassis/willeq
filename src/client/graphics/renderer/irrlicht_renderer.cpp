#include "client/graphics/irrlicht_renderer.h"
#include "client/graphics/renderer_zone_merge.h"  // ZoneMergeBuffers complete type (unique_ptr member destruction)
#include "client/willeq_database.h"
#include "client/graphics/loading_thread.h"
#include "client/graphics/entity_prep_worker.h"
#include "client/graphics/eqe1_loader.h"
#include <filesystem>
#include "client/graphics/eq/dds_decoder.h"
#include "client/graphics/eq/equipment_textures.h"
#include "client/graphics/frame/frame_component.h"  // T357: setCommandCompletionProvider forwarder
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

eqt::ui::DisplaySettings IrrlichtRenderer::getDisplaySettings() {
    return cachedDisplaySettings_;
}

void IrrlichtRenderer::shutdown() {
    // P10b: unregister before constrainedTextureCache_ (owned by Application,
    // outlives IrrlichtRenderer in the normal shutdown order, but don't rely
    // on that) can call into a soon-to-be-destroyed regionMeshTextureGuard_.
    if (constrainedTextureCache_ && regionMeshTextureGuard_) {
        constrainedTextureCache_->removeEvictionListener(regionMeshTextureGuard_.get());
    }
    regionMeshTextureGuard_.reset();
    if (constrainedTextureCache_ && softwareCursorTextureGuard_) {
        constrainedTextureCache_->removeEvictionListener(softwareCursorTextureGuard_.get());
    }
    softwareCursorTextureGuard_.reset();
    // P17: same unregister-before-destroy discipline for the entity-mesh guard.
    if (constrainedTextureCache_ && entityMeshTextureGuard_) {
        constrainedTextureCache_->removeEvictionListener(entityMeshTextureGuard_.get());
    }
    entityMeshTextureGuard_.reset();
    zoneStream_->unregisterRemainingT124Guards();  // T124, see Review

    // Stop GPU upload thread before any other cleanup (must stop before EGL context destroyed)
#ifdef EQT_HAS_GLES2
    if (gpuUploadThread_) {
        gpuUploadThread_->stop();
        gpuUploadThread_.reset();
    }
#endif

    // Stop simulation worker before any other cleanup
    simBridge_->stopSimulationWorker();

    // Stop background entity prep worker before any other cleanup
    if (entityPrepWorker_) {
        entityPrepWorker_->stop();
        entityPrepWorker_.reset();
    }

    zoneLifecycle_->unloadZone();

    // Shutdown shared thread pool (all queue users already stopped above)
    if (backgroundThreadPool_) {
        backgroundThreadPool_->shutdown();
        backgroundThreadPool_.reset();
    }

    // Reset all managers that hold Irrlicht resources BEFORE dropping the device
    // Their destructors may try to remove scene nodes which requires a valid device
    entityRenderer_.reset();
    cameraController_.reset();
    doorManager_.reset();
    skyRenderer_.reset();
    animatedTextureManager_.reset();
    eventReceiver_.reset();

    // Weather and environment managers also need the device for cleanup
    weatherEffects_.reset();
    particleManager_.reset();
    boidsManager_.reset();
    tumbleweedManager_.reset();
    weatherSystem_.reset();
    // treeWindController_/treeIdentifier_ are plain members (P32) — no
    // reset needed, they hold no Irrlicht scene-graph resources.
    detailManager_.reset();
    // constrainedTextureCache_ is owned by Application, not reset here
    SimBridge().spellVisualFX().reset();

    if (device_) {
        device_->drop();
        device_ = nullptr;
    }

    driver_ = nullptr;
    smgr_ = nullptr;
    Overlays().guienv() = nullptr;
    camera_ = nullptr;
    Overlays().hudText() = nullptr;
    Overlays().hotkeysText() = nullptr;
    initialized_ = false;
    loadingScreenVisible_ = true;

    LOG_INFO(MOD_GRAPHICS, "IrrlichtRenderer shutdown");
}

bool IrrlichtRenderer::isRunning() const {
    LOG_TRACE(MOD_GRAPHICS, "isRunning: checking initialized_={}", initialized_);
    if (!initialized_) return false;

    LOG_TRACE(MOD_GRAPHICS, "isRunning: checking device_={}", (device_ ? "valid" : "null"));
    if (!device_) return false;

    LOG_TRACE(MOD_GRAPHICS, "isRunning: calling device_->run()...");
    bool deviceRunning = device_->run();
    LOG_TRACE(MOD_GRAPHICS, "isRunning: device_->run() returned {}", deviceRunning);
    if (!deviceRunning) return false;

    LOG_TRACE(MOD_GRAPHICS, "isRunning: checking eventReceiver_...");
    bool quit = eventReceiver_->quitRequested();
    LOG_TRACE(MOD_GRAPHICS, "isRunning: quitRequested={}", quit);

    return !quit;
}

void IrrlichtRenderer::requestQuit() {
    if (eventReceiver_) {
        eventReceiver_->setQuitRequested(true);
    }
}

void IrrlichtRenderer::createSoftwareCursor() {
    if (!driver_ || !config_.constrainedConfig.useDRM) return;

    // Load pre-generated cursor texture via constrained cache
    std::string cursorPath = "data/textures/software_cursor.bmp";
    std::ifstream cursorFile(cursorPath, std::ios::binary | std::ios::ate);
    if (!cursorFile) {
        LOG_FATAL(MOD_GRAPHICS, "Required texture {} not found — run generate_textures first", cursorPath);
        std::exit(1);
    }
    std::streamsize cursorFileSize = cursorFile.tellg();
    cursorFile.seekg(0, std::ios::beg);
    std::vector<char> cursorData(cursorFileSize);
    cursorFile.read(cursorData.data(), cursorFileSize);

    softwareCursorTexture_ = constrainedTextureCache_->getOrLoad("softwareCursor", cursorData);
    if (!softwareCursorTexture_) softwareCursorTexture_ = constrainedTextureCache_->uploadTexture("softwareCursor");
    if (!softwareCursorTexture_) {
        LOG_FATAL(MOD_GRAPHICS, "Failed to load required texture: {}", cursorPath);
        std::exit(1);
    }

    LOG_INFO(MOD_GRAPHICS, "Software cursor texture loaded for DRM mode");
}

void IrrlichtRenderer::setupCamera() {
    camera_ = smgr_->addCameraSceneNode(
        nullptr,
        irr::core::vector3df(0, 100, 0),
        irr::core::vector3df(100, 0, 100),
        -1
    );

    camera_->setFarValue(SKY_FAR_PLANE);
    camera_->setNearValue(1.0f);
    LOG_INFO(MOD_GRAPHICS, "Camera far plane: {}, render distance: {}", SKY_FAR_PLANE, renderDistance_);

    cameraController_ = std::make_unique<CameraController>(camera_);
    cameraController_->setMoveSpeed(500.0f);
    cameraController_->setMouseSensitivity(0.2f);

    frustumCuller_ = std::make_unique<FrustumCuller>();

    // Create software occlusion culler if configured (non-zero buffer dimensions)
    if (config_.constrainedConfig.occlusionBufferWidth > 0 &&
        config_.constrainedConfig.occlusionBufferHeight > 0) {
        OcclusionCullerConfig occConfig;
        occConfig.width = config_.constrainedConfig.occlusionBufferWidth;
        occConfig.height = config_.constrainedConfig.occlusionBufferHeight;
        occConfig.maxOccluderRegions = config_.constrainedConfig.occlusionMaxOccluderRegions;
        occlusionCuller_ = std::make_unique<SoftwareOcclusionCuller>(occConfig);
        LOG_INFO(MOD_GRAPHICS, "Software occlusion culler enabled: {}x{} depth buffer, max {} occluder regions",
            occConfig.width, occConfig.height, occConfig.maxOccluderRegions);
    }
}

void IrrlichtRenderer::setupLighting() {
    // Start in dark mode (lighting ON, zone lights OFF)
    // Only object lights will illuminate the scene
    smgr_->setAmbientLight(irr::video::SColorf(0.005f, 0.005f, 0.008f, 1.0f));

    // Add a directional light (sun) - store reference for time of day updates
    Diagnostics().sunLight() = smgr_->addLightSceneNode(
        nullptr,
        irr::core::vector3df(0, 1000, 0),
        irr::video::SColorf(1.0f, 1.0f, 0.9f, 1.0f),
        10000.0f
    );

    if (Diagnostics().sunLight()) {
        irr::video::SLight& lightData = Diagnostics().sunLight()->getLightData();
        lightData.Type = irr::video::ELT_DIRECTIONAL;
        lightData.Direction = irr::core::vector3df(0.5f, -1.0f, 0.5f);
        // Start with sun hidden (dark mode - only object lights)
        Diagnostics().sunLight()->setVisible(false);
    }
}

void IrrlichtRenderer::updateTimeOfDay(uint8_t hour, uint8_t minute) {
    if (isLoading()) return;
    if (!smgr_) return;

    currentHour_ = hour;
    currentMinute_ = minute;

    // Don't update ambient/sun if we're in dark mode (zone lights OFF)
    // In dark mode, only object lights illuminate the scene
    if (lightingEnabled_ && !Diagnostics().zoneLightsEnabled()) {
        return;
    }

    // Calculate ambient light based on hour
    // EQ time: 0-4 night, 5-6 dawn, 7-17 day, 18-19 dusk, 20-23 night
    float r, g, b;
    float sunIntensity = 1.0f;

    if (hour >= 7 && hour <= 17) {
        // Day - bright white ambient
        r = 0.5f; g = 0.5f; b = 0.5f;
        sunIntensity = 1.0f;
    } else if (hour >= 20 || hour <= 4) {
        // Night - dark blue ambient
        r = 0.08f; g = 0.08f; b = 0.15f;
        sunIntensity = 0.1f;
    } else if (hour >= 5 && hour <= 6) {
        // Dawn - transition from night to day (orange tint)
        float t = (hour - 5) + minute / 60.0f;  // 0-2 range
        t /= 2.0f;  // normalize to 0-1
        r = 0.08f + t * (0.5f - 0.08f);
        g = 0.08f + t * (0.4f - 0.08f);
        b = 0.15f + t * (0.35f - 0.15f);
        sunIntensity = 0.1f + t * 0.9f;
    } else {
        // Dusk (18-19) - transition from day to night (orange tint)
        float t = (hour - 18) + minute / 60.0f;  // 0-2 range
        t /= 2.0f;  // normalize to 0-1
        r = 0.5f - t * (0.5f - 0.08f);
        g = 0.4f - t * (0.4f - 0.08f);
        b = 0.35f - t * (0.35f - 0.15f);
        sunIntensity = 1.0f - t * 0.9f;
    }

    // Apply user-adjustable ambient multiplier (Page Up/Down)
    r = std::min(1.0f, r * ambientMultiplier_);
    g = std::min(1.0f, g * ambientMultiplier_);
    b = std::min(1.0f, b * ambientMultiplier_);

    // Apply weather effects (darkening for storms)
    if (weatherEffects_ && weatherEffects_->isEnabled()) {
        float weatherMod = weatherEffects_->getAmbientLightModifier();
        static float lastLoggedMod = -1.0f;
        if (std::abs(weatherMod - lastLoggedMod) > 0.01f && weatherMod < 0.99f) {
            LOG_DEBUG(MOD_GRAPHICS, "updateTimeOfDay: weatherMod={:.3f}, applying to ambient r={:.3f} g={:.3f} b={:.3f}",
                      weatherMod, r, g, b);
            lastLoggedMod = weatherMod;
        }
        r *= weatherMod;
        g *= weatherMod;
        b *= weatherMod;
        sunIntensity *= weatherMod;
    }

    smgr_->setAmbientLight(irr::video::SColorf(r, g, b, 1.0f));

    // Update sun light intensity
    if (Diagnostics().sunLight()) {
        irr::video::SLight& lightData = Diagnostics().sunLight()->getLightData();
        lightData.DiffuseColor = irr::video::SColorf(sunIntensity, sunIntensity, sunIntensity * 0.9f, 1.0f);
    }

    // Update sky celestial body positions and colors
    if (skyRenderer_ && skyRenderer_->isInitialized()) {
        skyRenderer_->updateTimeOfDay(hour, minute);

        // Update fog color to match sky if fog is enabled
        if (fogEnabled_ && driver_ && skyRenderer_->isEnabled()) {
            irr::video::SColor fogColor = skyRenderer_->getRecommendedFogColor();

            // Get current fog settings to preserve distances
            irr::video::SColor currentFogColor;
            irr::video::E_FOG_TYPE fogType;
            irr::f32 fogStart, fogEnd, fogDensity;
            bool pixelFog, rangeFog;
            driver_->getFog(currentFogColor, fogType, fogStart, fogEnd, fogDensity, pixelFog, rangeFog);

            // Apply weather effects to fog
            if (weatherEffects_ && weatherEffects_->isEnabled()) {
                // Blend fog color with weather fog color
                irr::video::SColor weatherFog = weatherEffects_->getWeatherFogColor();
                float weatherMod = weatherEffects_->getAmbientLightModifier();
                float blendFactor = 1.0f - weatherMod;  // More blending when darker

                fogColor.setRed(static_cast<uint8_t>(
                    fogColor.getRed() * (1.0f - blendFactor) + weatherFog.getRed() * blendFactor));
                fogColor.setGreen(static_cast<uint8_t>(
                    fogColor.getGreen() * (1.0f - blendFactor) + weatherFog.getGreen() * blendFactor));
                fogColor.setBlue(static_cast<uint8_t>(
                    fogColor.getBlue() * (1.0f - blendFactor) + weatherFog.getBlue() * blendFactor));

                // Apply fog density modifier (brings fog closer during storms)
                float densityMod = weatherEffects_->getFogDensityModifier();
                fogEnd /= densityMod;

                // Apply rain overlay fog reduction (original EQ behavior - heavy rain = short fog)
                float rainFogStart, rainFogEnd;
                if (weatherEffects_->getRainFogSettings(rainFogStart, rainFogEnd)) {
                    // Rain fog completely overrides normal fog distances
                    fogStart = rainFogStart;
                    fogEnd = rainFogEnd;
                }
            }

            // Only update fog color if we have valid fog distances
            if (fogEnd > fogStart && fogEnd > 0) {
                driver_->setFog(fogColor, fogType, fogStart, fogEnd, fogDensity, pixelFog, rangeFog);
            }
        }
    }

    // Update zone and object lights with weather modifier (only when weather is active)
    if (weatherEffects_ && weatherEffects_->isEnabled()) {
        float weatherMod = weatherEffects_->getAmbientLightModifier();
        static float lastWeatherMod = 1.0f;
        // Only update light colors when weather modifier changes significantly
        if (std::abs(weatherMod - lastWeatherMod) > 0.005f) {
            // Zone light colors now handled by SimulationWorker (vision/weather applied there)
            updateObjectLightColors();
            lastWeatherMod = weatherMod;
        }

        // Update sky brightness based on rain intensity
        // Intensity 1: 50%, Intensity 5: 25%, Intensity 10: ~10% (like night)
        // Formula: brightness = 0.5 * pow(0.5, (intensity - 1) / 4)
        if (skyRenderer_ && weatherEffects_->isRaining()) {
            uint8_t intensity = weatherEffects_->getCurrentIntensity();
            if (intensity > 0) {
                float skyBrightness = 0.5f * std::pow(0.5f, static_cast<float>(intensity - 1) / 4.0f);
                skyRenderer_->setWeatherBrightness(skyBrightness);
            } else {
                skyRenderer_->setWeatherBrightness(1.0f);
            }
        } else if (skyRenderer_) {
            skyRenderer_->setWeatherBrightness(1.0f);
        }
    } else if (skyRenderer_) {
        // No weather active, restore full sky brightness
        skyRenderer_->setWeatherBrightness(1.0f);
    }

    // Update shader uniforms with current lighting state
    if (zoneShader_ && zoneShader_->isAvailable()) {
        if (!Diagnostics().debugDirectionalLightEnabled()) {
            // Fullbright: override ambient/sun/tint for debug isolation
            zoneShader_->setAmbientColor(1.0f, 1.0f, 1.0f);
            zoneShader_->setSunColor(0.0f, 0.0f, 0.0f);
            zoneShader_->setTintColor(1.0f, 1.0f, 1.0f);
        } else {
            zoneShader_->setAmbientColor(r, g, b);
            zoneShader_->setSunColor(sunIntensity, sunIntensity, sunIntensity * 0.9f);

            // Sun direction matches setupLighting() directional light
            zoneShader_->setSunDirection(0.5f, -1.0f, 0.5f);

            // Compute day/night tint color from ambient values
            // Dawn/dusk: warm orange; night: cool blue; day: neutral white
            float maxAmb = std::max({r, g, b, 0.01f});
            zoneShader_->setTintColor(r / maxAmb, g / maxAmb, b / maxAmb);
        }

        // Also sync fog with any weather-modified values
        irr::video::SColor currentFogColor;
        irr::video::E_FOG_TYPE fogType;
        irr::f32 fogStart, fogEnd, fogDensity;
        bool pixelFog, rangeFog;
        driver_->getFog(currentFogColor, fogType, fogStart, fogEnd, fogDensity, pixelFog, rangeFog);
        zoneShader_->setFog(fogStart, fogEnd,
                            currentFogColor.getRed() / 255.0f,
                            currentFogColor.getGreen() / 255.0f,
                            currentFogColor.getBlue() / 255.0f,
                            currentFogColor.getAlpha() / 255.0f);
    }
}

// L05 (Batch L, refactor/L_renderer_component_extraction.md): this body was
// in renderer_camera.cpp, which L05 moved into PlayerCameraComponent. It did
// NOT go with it. It reads objectLights_, weatherEffects_ and
// fireEffectsEnabled_ and nothing else -- all three are RendererContext core,
// none is player or camera state -- and its ONE caller is updateTimeOfDay()
// directly above. It sat in the camera TU by accident of the J11 split.
// So it moved SIDEWAYS, verbatim, to the TU of its caller, and its
// declaration stayed on IrrlichtRenderer: zero call sites changed. This is
// L04 correction 5 (the measureSection case) for the second time. L09 decides
// whether the lighting family finally takes it.
// updateZoneLightColors() removed -- vision/weather modifiers now applied in SimulationWorker
void IrrlichtRenderer::updateObjectLightColors(float deltaTime) {
    if (objectLights_.empty()) {
        return;
    }

    // Get weather modifier (darker during storms)
    float weatherMod = 1.0f;
    if (weatherEffects_ && weatherEffects_->isEnabled()) {
        weatherMod = weatherEffects_->getAmbientLightModifier();
    }

    // Update all object light colors
    for (auto& objLight : objectLights_) {
        if (objLight.node) {
            // Apply weather modifier to original color
            float r = objLight.originalColor.r * weatherMod;
            float g = objLight.originalColor.g * weatherMod;
            float b = objLight.originalColor.b * weatherMod;

            // Fire flickering: modulate intensity with layered sine waves
            if (objLight.isFireSource && fireEffectsEnabled_) {
                objLight.flickerPhase += objLight.flickerSpeed * deltaTime;
                float flicker = 0.70f + 0.10f * std::sin(objLight.flickerPhase * 6.7f)
                                      + 0.05f * std::sin(objLight.flickerPhase * 13.1f);
                r *= flicker;
                g *= flicker;
                b *= flicker;
            }

            irr::video::SLight& lightData = objLight.node->getLightData();
            lightData.DiffuseColor = irr::video::SColorf(r, g, b, 1.0f);
        }
    }

}

bool IrrlichtRenderer::isRegionPvsVisible(size_t regionIdx) const {
    if (!usePvsCulling_ || regionIdx == SIZE_MAX || currentPvsRegion_ == SIZE_MAX)
        return true;  // No PVS data — assume visible
    if (!zoneBspTree_ || currentPvsRegion_ >= zoneBspTree_->regions.size())
        return true;
    auto& camRegion = zoneBspTree_->regions[currentPvsRegion_];
    if (!camRegion || camRegion->visibleRegions.empty())
        return true;
    if (regionIdx >= camRegion->visibleRegions.size())
        return true;
    return camRegion->visibleRegions[regionIdx];
}

// Dead code removed: updateObjectVisibility(), updateZoneLightVisibility(), updateObjectLights()
// These are now handled exclusively by SimulationWorker.


void IrrlichtRenderer::toggleSky() {
    if (isLoading()) return;
    if (skyRenderer_) {
        bool newState = !skyRenderer_->isEnabled();
        skyRenderer_->setEnabled(newState);
        LOG_INFO(MOD_GRAPHICS, "Sky rendering: {}", newState ? "ON" : "OFF");
    }
}

void IrrlichtRenderer::forceSkyType(uint8_t skyTypeId) {
    if (isLoading()) return;
    if (skyRenderer_ && skyRenderer_->isInitialized()) {
        skyRenderer_->setSkyType(skyTypeId, currentZoneName_);
        LOG_INFO(MOD_GRAPHICS, "Forced sky type to {}", skyTypeId);
    }
}

bool IrrlichtRenderer::isSkyEnabled() const {
    return skyRenderer_ && skyRenderer_->isEnabled();
}

std::string IrrlichtRenderer::getSkyDebugInfo() const {
    if (!skyRenderer_ || !skyRenderer_->isInitialized()) {
        return "Sky: Not initialized";
    }

    std::string info = "Sky: ";
    if (!skyRenderer_->isEnabled()) {
        info += "OFF";
    } else {
        info += fmt::format("Type {} ", skyRenderer_->getCurrentSkyType());

        // Get sky color info
        auto colors = skyRenderer_->getCurrentSkyColors();
        info += fmt::format("Bright:{:.0f}% ", colors.cloudBrightness * 100);
    }

    return info;
}


void IrrlichtRenderer::createEntityRenderer() {
    // S05: Called once from initLoadingScreen(). No lazy-init guard needed.
    entityRenderer_ = std::make_unique<EntityRenderer>(smgr_, driver_, device_->getFileSystem());
    entityRenderer_->setClientPath(config_.eqClientPath);
    entityRenderer_->setNameTagsVisible(config_.constrainedConfig.nameTagsEnabled);
    entityRenderer_->setRenderDistance(renderDistance_);
    if (constrainedTextureCache_) {
        entityRenderer_->setConstrainedTextureCache(constrainedTextureCache_);
        // Wire mesh builder into cache for entity texture registration during uploadTexture
        auto* meshBuilder = entityRenderer_->getMeshBuilder();
        if (meshBuilder)
            constrainedTextureCache_->setMeshBuilder(meshBuilder);
    }
    entityRenderer_->setConstrainedConfig(&config_.constrainedConfig);
    entityRenderer_->setSkipTextureUpload(config_.constrainedConfig.skipEntityTextureUpload);
    if (config_.constrainedConfig.enableCompositeEntities && !config_.constrainedConfig.compositeEntityPath.empty()) {
        entityRenderer_->setCompositeEntityPath(config_.constrainedConfig.compositeEntityPath);
    }
    if (zoneShader_ && zoneShader_->isAvailable()) {
        entityRenderer_->setShaderMaterialTypes(zoneShader_->getActiveSolid(),
                                                zoneShader_->getActiveAlphaTest(),
                                                zoneShader_->getActiveAlphaTestRGBA());
    }
    if (config_.constrainedConfig.chrCacheMaxEntries > 0 && entityRenderer_->getRaceModelLoader()) {
        entityRenderer_->getRaceModelLoader()->setMaxChrCacheEntries(config_.constrainedConfig.chrCacheMaxEntries);
    }
    entityRenderer_->setGroundFinderCallback([this](float x, float y, float currentZ) {
        return playerCamera_->findGroundZ(x, y, currentZ);
    });
    if (zoneBspTree_) {
        entityRenderer_->setBspTree(zoneBspTree_);
    }
    if (frustumCuller_) {
        entityRenderer_->setFrustumCuller(frustumCuller_.get());
    }
}







bool IrrlichtRenderer::createDoor(uint8_t doorId, const std::string& name,
                                   float x, float y, float z, float heading,
                                   uint32_t incline, uint16_t size, uint8_t opentype,
                                   bool initiallyOpen) {
    if (doorManager_) {
        return doorManager_->createDoor(doorId, name, x, y, z, heading, incline, size, opentype, initiallyOpen);
    }
    return false;
}

bool IrrlichtRenderer::registerDoor(uint8_t doorId, const std::string& name,
                                     float x, float y, float z, float heading,
                                     uint32_t incline, uint16_t size, uint8_t opentype,
                                     bool initiallyOpen) {
    if (doorManager_) {
        return doorManager_->registerDoor(doorId, name, x, y, z, heading, incline, size, opentype, initiallyOpen);
    }
    return false;
}

void IrrlichtRenderer::setDoorState(uint8_t doorId, bool open, bool userInitiated) {
    if (isLoading()) return;
    if (doorManager_) {
        doorManager_->setDoorState(doorId, open, userInitiated);
    }
}

void IrrlichtRenderer::clearDoors() {
    if (isLoading()) return;
    if (doorManager_) {
        doorManager_->clearDoors();
    }
}

// ============================================================================
// World Object Management (for tradeskill container click detection)
// ============================================================================

void IrrlichtRenderer::addWorldObject(uint32_t dropId, float x, float y, float z,
                                       uint32_t objectType, const std::string& name) {
    if (isLoading()) return;
    WorldObjectVisual obj;
    obj.dropId = dropId;
    obj.x = x;
    obj.y = y;
    obj.z = z;
    obj.objectType = objectType;
    obj.name = name;

    // Create bounding box for click detection
    // EQ coords: x, y are horizontal, z is vertical
    // Irrlicht coords: x, z are horizontal, y is vertical
    // Transform: EQ (x, y, z) -> Irrlicht (x, z, y)
    float irrX = x;
    float irrY = z;  // EQ z -> Irrlicht y
    float irrZ = y;  // EQ y -> Irrlicht z

    // Default object size - most tradeskill containers are roughly 3x3x3 units
    float halfSize = 3.0f;
    obj.boundingBox = irr::core::aabbox3df(
        irrX - halfSize, irrY - halfSize, irrZ - halfSize,
        irrX + halfSize, irrY + halfSize * 2, irrZ + halfSize  // Extend upward more
    );

    worldObjects_[dropId] = obj;
    LOG_DEBUG(MOD_GRAPHICS, "Added world object: dropId={} type={} name='{}' at ({:.1f}, {:.1f}, {:.1f})",
              dropId, objectType, name, x, y, z);
}

void IrrlichtRenderer::removeWorldObject(uint32_t dropId) {
    auto it = worldObjects_.find(dropId);
    if (it != worldObjects_.end()) {
        LOG_DEBUG(MOD_GRAPHICS, "Removed world object: dropId={}", dropId);
        worldObjects_.erase(it);
    }
}

void IrrlichtRenderer::clearWorldObjects() {
    LOG_DEBUG(MOD_GRAPHICS, "Clearing {} world objects", worldObjects_.size());
    worldObjects_.clear();
}

uint32_t IrrlichtRenderer::getWorldObjectAtScreenPos(int screenX, int screenY) const {
    if (!camera_ || !collisionManager_) {
        return 0;
    }

    // Get ray from camera through screen position
    irr::core::line3df ray = collisionManager_->getRayFromScreenCoordinates(
        irr::core::position2di(screenX, screenY), camera_);

    uint32_t closestObjectId = 0;
    float closestDist = std::numeric_limits<float>::max();

    for (const auto& [id, obj] : worldObjects_) {
        // Expand box slightly for easier clicking
        irr::core::aabbox3df expandedBox = obj.boundingBox;
        expandedBox.MinEdge -= irr::core::vector3df(1.0f, 1.0f, 1.0f);
        expandedBox.MaxEdge += irr::core::vector3df(1.0f, 1.0f, 1.0f);

        if (expandedBox.intersectsWithLine(ray)) {
            // Calculate distance to object center
            irr::core::vector3df objCenter = expandedBox.getCenter();
            float dist = ray.start.getDistanceFrom(objCenter);

            if (dist < closestDist) {
                closestDist = dist;
                closestObjectId = id;
            }
        }
    }

    return closestObjectId;
}

uint32_t IrrlichtRenderer::getNearestWorldObject(float playerX, float playerY, float playerZ,
                                                   float maxDistance) const {
    uint32_t nearestId = 0;
    float nearestDistSq = maxDistance * maxDistance;

    for (const auto& [id, obj] : worldObjects_) {
        // Calculate 3D distance
        float dx = obj.x - playerX;
        float dy = obj.y - playerY;
        float dz = obj.z - playerZ;
        float distSq = dx * dx + dy * dy + dz * dz;

        if (distSq < nearestDistSq) {
            nearestDistSq = distSq;
            nearestId = id;
        }
    }

    if (nearestId != 0) {
        LOG_DEBUG(MOD_GRAPHICS, "getNearestWorldObject: found dropId={} at distance {:.1f}",
                  nearestId, std::sqrt(nearestDistSq));
    }

    return nearestId;
}


// T319: the body of the InputActionBridge action callback that
// Application installs (application.cpp). Runs on the GAME thread, right
// after ActionDispatcher has produced the result, so it must not touch
// chatPanelState_ (render-thread state) directly -- it parks the text and
// processChatInput() picks it up next frame.
//
// ActionDispatcher::openReplyToTell() returns Success("/tell <Name> ") --
// literally the text the chat input is meant to open with (T316) -- or a
// Failure carrying the refusal wording. A failure prefills nothing; the
// user simply gets no chat box, which is what the official client does
// when there is nobody to reply to.
void IrrlichtRenderer::handleActionResult(const std::string& actionName, bool success,
                                          const std::string& message) {
    if (actionName != "ReplyToTell") return;
    if (!success || message.empty()) return;
    requestChatPrefill(message);
}


// T357 (B057): forwarder to the frame component, which owns the chat input
// state the Tab arm edits. Application calls this once, after the renderer is
// constructed and before the render thread starts.
void IrrlichtRenderer::setCommandCompletionProvider(CommandCompletionProvider provider) {
    if (frame_) {
        frame_->setCommandCompletionProvider(std::move(provider));
    }
}

void IrrlichtRenderer::run() {
    if (!initialized_) {
        return;
    }

    irr::u32 lastTime = device_->getTimer()->getTime();

    while (isRunning()) {
        irr::u32 currentTime = device_->getTimer()->getTime();
        irr::u32 frameTimeMs = currentTime - lastTime;
        float deltaTime = frameTimeMs / 1000.0f;
        lastTime = currentTime;

        // Record frame time for gameplay statistics
        if (frameTimeMs > 0) {
            EQT::PerformanceMetrics::instance().recordSample("Frame Time", static_cast<int64_t>(frameTimeMs));
        }

        if (!processFrame(deltaTime)) {
            break;
        }
    }
}

bool IrrlichtRenderer::saveScreenshot(const std::string& filename) {
    if (!driver_) {
        return false;
    }

#if defined(EQT_HAS_DRM) || defined(EQT_HAS_GLES2)
    // DRM/EGL: Irrlicht's createScreenShot() reads GL_FRONT which doesn't exist
    // in EGL (only GLX has separate front/back read targets). Read GL_BACK instead.
    // P10: gated on the GLES2 driver type generally, not just useDRM — this branch
    // is also required for the x86 EGL-surfaceless dev build (H01; useDRM is false
    // there since --drm isn't passed, but it's still the same EQT_HAS_DRM-defined
    // GLES2/EGL device with no real GL_FRONT/window-system default framebuffer).
    // createScreenShot() was found to return garbage (uninitialized-looking noise)
    // under the surfaceless case during P10's in-app A/B screenshot verification —
    // this widened gate is the fix, discovered and applied there.
    // P23: widened again to `EQT_HAS_DRM || EQT_HAS_GLES2`. The x86 GLES2 dev
    // build (build-gles2-x86.sh) configures EQT_DRM=OFF / EQT_GLES2=ON, so
    // EQT_HAS_DRM was never defined there and this whole safe-readback branch
    // was compiled OUT, falling through to driver_->createScreenShot() (which
    // segfaults nondeterministically under llvmpipe — see deferred.md, P20
    // Review deviation 2a). EQT_HAS_GLES2 is defined on both the x86 GLES2
    // build and ARM (which also always defines EQT_HAS_DRM, so ARM's path
    // through this #if is unchanged — this is additive for x86-without-DRM
    // only). The runtime check below (useDRM || EDT_OGLES2) already covers
    // the x86 case correctly once the branch is compiled in at all.
    if (config_.constrainedConfig.useDRM || driver_->getDriverType() == irr::video::EDT_OGLES2) {
        irr::core::dimension2d<irr::u32> screenSize = driver_->getScreenSize();
        uint32_t w = screenSize.Width;
        uint32_t h = screenSize.Height;

        std::vector<uint8_t> pixels(w * h * 4);
#ifndef EQT_HAS_GLES2
        glReadBuffer(GL_BACK);
#endif
        glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());

        // glReadPixels returns bottom-to-top; flip to top-to-bottom
        size_t rowBytes = w * 4;
        std::vector<uint8_t> rowBuf(rowBytes);
        for (uint32_t y = 0; y < h / 2; ++y) {
            uint8_t* top = pixels.data() + y * rowBytes;
            uint8_t* bot = pixels.data() + (h - 1 - y) * rowBytes;
            std::memcpy(rowBuf.data(), top, rowBytes);
            std::memcpy(top, bot, rowBytes);
            std::memcpy(bot, rowBuf.data(), rowBytes);
        }

        // Convert RGBA to ARGB for Irrlicht (swap R and B)
        for (size_t i = 0; i < pixels.size(); i += 4) {
            std::swap(pixels[i], pixels[i + 2]);  // R <-> B
        }

        irr::video::IImage* image = driver_->createImageFromData(
            irr::video::ECF_A8R8G8B8,
            irr::core::dimension2d<irr::u32>(w, h),
            pixels.data(), false);

        if (image) {
            bool result = driver_->writeImageToFile(image, filename.c_str());
            image->drop();
            if (result) {
                LOG_INFO(MOD_GRAPHICS, "Screenshot saved: {}", filename);
            }
            return result;
        }
        return false;
    }
#endif

    irr::video::IImage* screenshot = driver_->createScreenShot();
    if (screenshot) {
        bool result = driver_->writeImageToFile(screenshot, filename.c_str());
        screenshot->drop();
        if (result) {
            LOG_INFO(MOD_GRAPHICS, "Screenshot saved: {}", filename);
        }
        return result;
    }
    return false;
}


void IrrlichtRenderer::setFrameTimingEnabled(bool enabled) {
    if (isLoading()) return;
    Frame().frameTimingEnabled() = enabled;
    if (enabled) {
        // Reset accumulators when starting
        Frame().timings() = FrameTimings();
        Frame().timingsAccum() = FrameTimings();
        Frame().timingsSampleCount() = 0;
        // S05: renderPassTimer_ already created in initLoadingScreen() — just install
        if (smgr_ && renderPassTimer_) {
            smgr_->setLightManager(renderPassTimer_);
        }
        LOG_INFO(MOD_GRAPHICS, "Frame timing profiler ENABLED - timing data will be logged every 60 frames");
    } else {
        // Remove render pass timer (but keep it if manual zone draw needs it)
        if (smgr_ && renderPassTimer_ && !manualZoneDrawEnabled_) {
            smgr_->setLightManager(nullptr);
            renderPassTimer_ = nullptr;  // Irrlicht drops the ref
        }
        LOG_INFO(MOD_GRAPHICS, "Frame timing profiler DISABLED");
    }
}

// --- Renderer Mode Implementation ---

void IrrlichtRenderer::setClipDistance(float distance) {
    if (isLoading()) return;
    // Clamp to reasonable range
    if (distance < 100.0f) distance = 100.0f;
    if (distance > 50000.0f) distance = 50000.0f;

    // Update render distance for fog/object culling
    renderDistance_ = distance;
    config_.constrainedConfig.clipDistance = distance;

    // In constrained mode, set far plane = render distance for Z-precision
    if (camera_) {
        camera_->setFarValue(std::max(SKY_FAR_PLANE, distance));
    }

    // Update fog to match new render distance
    overlays_->setupFog();

    LOG_INFO(MOD_GRAPHICS, "Render distance set to {}, camera far plane: {}",
             distance, camera_ ? camera_->getFarValue() : SKY_FAR_PLANE);
}

float IrrlichtRenderer::getClipDistance() const {
    if (camera_) {
        return camera_->getFarValue();
    }
    return config_.constrainedConfig.clipDistance;
}


} // namespace Graphics
} // namespace EQT
