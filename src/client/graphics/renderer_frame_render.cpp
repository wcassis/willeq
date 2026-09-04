// renderer_frame_render.cpp - IrrlichtRenderer frame phase 4 (render).
//
// Holds processFrameRender() alone. Phases 1-3 live in
// renderer_frame_input.cpp; the orchestrator processFrame() stays in
// renderer_frame.cpp.
//
// processFrameRender is 546 lines and is a recorded Batch-G decomposition
// candidate that J31 deliberately did NOT touch (move-only). All nine of this
// file's conditional regions (__linux__, EQT_HAS_GLES2, EQT_HAS_DRM,
// WITH_RDP) live wholly inside it.
//
// WARNING: tests/test_ui_hide_command.cpp reads THIS FILE as text and
// requires that the static-UI draw block's guard, which ands the new-UI flag
// and the UI renderer against the negated all-UI-hidden flag, appear here
// verbatim. The literal condition is spelled out in that test, deliberately
// not here: a comment naming it would satisfy the guard on its own.
// Split per refactor/J31_renderer_frame_split.md (move-only).
#include "client/graphics/irrlicht_renderer.h"
#include "client/graphics/entity_prep_worker.h"
#include "client/graphics/sky_renderer.h"
#include "client/graphics/weather_effects_controller.h"
#include "client/graphics/spell_visual_fx.h"
#include "client/graphics/ui/static_ui_input.h"
#include "client/graphics/ui/ui_renderer.h"
#include "client/graphics/ui/character_model_view.h"
#include "client/bridge/game_state_bridge.h"
#include "client/state/event_bus.h"
#include "client/events/renderer_intents.h"
#include "common/logging.h"
#include "common/performance_metrics.h"
#include <fmt/format.h>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#ifdef __linux__
#include <unistd.h>  // sysconf, _SC_PAGESIZE (H01: missing on x86 EQT_GLES2 builds)
#endif

#ifdef EQT_HAS_GLES2
// Declared in irrlicht_renderer.cpp (H01: hit when building EQT_GLES2 on x86;
// pre-existing gap from the Batch F file split).
extern void gles2Draw3DLinesBatch(void* driver, const float* vertices, unsigned int lineCount);
#endif

namespace EQT {
namespace Graphics {
// ===== Phase 4: Render =====
bool IrrlichtRenderer::processFrameRender(float deltaTime) {
    // If loading screen is visible, show it instead of rendering zone
    if (loadingScreenVisible_) {
        overlays_->drawLoadingScreen(loadingProgress_, loadingText_);
        return false;  // Signal loading screen was shown
    }

    // Run scene breakdown profile if scheduled
    if (sceneProfileEnabled_) {
        if (sceneProfileFrameCount_ < 0) {
            sceneProfileFrameCount_++;
        } else {
            diagnostics_->profileSceneBreakdown();
        }
    }

    // Render - use sky renderer's clear color for day/night effect
    irr::video::SColor clearColor(255, 50, 50, 80);
    if (skyRenderer_ && skyRenderer_->isEnabled() && skyRenderer_->isInitialized()) {
        clearColor = skyRenderer_->getCurrentClearColor();
    }
    LOG_TRACE(MOD_GRAPHICS, "PRE-DRAW checkpoint 0: beginScene");
    driver_->beginScene(true, true, clearColor);
    sectionStart_ = std::chrono::steady_clock::now();
    if (renderPassTimer_) renderPassTimer_->reset();
    if (zoneShader_) {
        // Update wind shader uniforms each frame
        windTime_ += deltaTime;
        zoneShader_->setWindTime(windTime_);
        // Wind params from tree wind config (or defaults) with weather multiplier.
        // P33: gate the actual displacement magnitude on the tree-wind-animation
        // setting (treeWindController_.isEnabled(), backed by
        // display_settings.json "animatedTrees") — when disabled, force the
        // multiplier to 0 so uWindParams collapses strength = baseStr + gustStr*gust
        // to exactly 0 in the vertex shader (zone_shader.cpp), i.e. zero
        // displacement, not merely a frozen nonzero pose. windTime_ keeps
        // advancing either way (cheap, avoids a discontinuity if re-enabled
        // mid-session); it has zero visual effect while strength is 0.
        float weatherMult = treeWindController_.isEnabled()
            ? treeWindController_.getWeatherMultiplier()
            : 0.0f;
        // Default config values scaled by weather
        zoneShader_->setWindParams(
            0.3f * weatherMult,   // baseStrength
            0.4f,                 // baseFrequency
            0.5f * weatherMult,   // gustStrength
            0.1f);                // gustFrequency

        zoneShader_->beginFrame();
    }
    // T300: the down-pitch clamp that hides the player's own body. Re-checked
    // every frame because the clamp tracks the FOV and because entering first
    // person from a steeply-pitched third-person view has to be corrected.
    // A no-op outside first person, and a no-op in first person unless the
    // pitch is out of range. Must run BEFORE the draw.
    applyFirstPersonPitchClamp();

    smgr_->drawAll();
    frameTimings_.sceneDrawAll = measureSection();

    // 3D camera transforms are captured during the SOLID render pass in
    // OnRenderPassPreRender — see captured3DView_/captured3DProj_.
    if (renderPassTimer_) {
        frameTimings_.sceneAnimate = renderPassTimer_->getAnimateTime();
        frameTimings_.sceneSolid = renderPassTimer_->solidTime;
        frameTimings_.sceneTransparent = renderPassTimer_->transparentTime;
        frameTimings_.sceneSkybox = renderPassTimer_->skyboxTime;
        frameTimings_.sceneOther = renderPassTimer_->otherTime;
        frameTimings_.sceneNodeCount = renderPassTimer_->nodeCount;
    }
    if (frameTimings_.sceneDrawAll > 50000) {
        LOG_WARN(MOD_GRAPHICS, "PERF: smgr->drawAll() took {} ms", frameTimings_.sceneDrawAll / 1000);
    }

    LOG_TRACE(MOD_GRAPHICS, "POST-DRAW checkpoint 1: getPrimitiveCountDrawn");
    // Track polygon count for constrained mode budget
    lastPolygonCount_ = driver_->getPrimitiveCountDrawn();

    LOG_TRACE(MOD_GRAPHICS, "POST-DRAW checkpoint 2: footprints (detail={})", detailManager_ != nullptr);
    // Render footprints (after terrain, before UI)
    if (detailManager_ && detailManager_->isFootprintEnabled()) {
        detailManager_->renderFootprints();
    }
    frameTimings_.footprintRender = measureSection();

    if (lastPolygonCount_ > static_cast<uint32_t>(config_.constrainedConfig.maxPolygonsPerFrame)) {
        if (++polygonBudgetExceededFrames_ >= 60) {
            LOG_WARN(MOD_GRAPHICS, "Polygon budget exceeded: {} > {} (limit)",
                     lastPolygonCount_, config_.constrainedConfig.maxPolygonsPerFrame);
            polygonBudgetExceededFrames_ = 0;
        }
    } else {
        polygonBudgetExceededFrames_ = 0;
    }

    if (++constrainedStatsLogCounter_ >= 150) {
        constrainedStatsLogCounter_ = 0;
        int visibleEntities = entityRenderer_ ? entityRenderer_->getVisibleEntityCount() : 0;
        int totalEntities = entityRenderer_ ? static_cast<int>(entityRenderer_->getEntityCount()) : 0;
        size_t tmuUsed = 0, tmuLimit = 0;
        float hitRate = 0.0f;
        size_t evictions = 0;
        if (constrainedTextureCache_) {
            tmuUsed = constrainedTextureCache_->getCurrentUsage();
            tmuLimit = constrainedTextureCache_->getMemoryLimit();
            hitRate = constrainedTextureCache_->getHitRate();
            evictions = constrainedTextureCache_->getEvictionCount();
        }
        size_t fbiUsed = config_.constrainedConfig.calculateFramebufferUsage(config_.width, config_.height);
        size_t fbiLimit = config_.constrainedConfig.framebufferMemoryBytes;
        LOG_INFO(MOD_GRAPHICS, "=== RENDERER STATS [{}] ===",
                 ConstrainedRendererConfig::presetName(config_.constrainedPreset));
        LOG_INFO(MOD_GRAPHICS, "  Resolution: {}x{} @ {}-bit (FBI: {:.1f}MB/{:.1f}MB)",
                 config_.width, config_.height, config_.constrainedConfig.colorDepthBits,
                 fbiUsed / (1024.0f * 1024.0f), fbiLimit / (1024.0f * 1024.0f));
        LOG_INFO(MOD_GRAPHICS, "  Textures: TMU {:.1f}MB/{:.1f}MB | Hit: {:.0f}% | Evictions: {}",
                 tmuUsed / (1024.0f * 1024.0f), tmuLimit / (1024.0f * 1024.0f), hitRate, evictions);
        LOG_INFO(MOD_GRAPHICS, "  Geometry: Polys {}/{} | Entities {}/{} (max {}) | Clip {:.0f}",
                 lastPolygonCount_, config_.constrainedConfig.maxPolygonsPerFrame,
                 visibleEntities, totalEntities, config_.constrainedConfig.maxVisibleEntities,
                 config_.constrainedConfig.clipDistance);
        LOG_INFO(MOD_GRAPHICS, "  FPS: {}", currentFps_);
#ifdef __linux__
        {
            // Governor exception: procfs is memory-mapped kernel data, not disk I/O (~1μs)
            FILE* f = fopen("/proc/self/statm", "r");
            if (f) {
                unsigned long vm = 0, rss = 0;
                if (fscanf(f, "%lu %lu", &vm, &rss) == 2) {
                    long ps = sysconf(_SC_PAGESIZE);
                    LOG_INFO(MOD_GRAPHICS, "  Memory: RSS {:.1f}MB",
                             (rss * ps) / (1024.0f * 1024.0f));
                }
                fclose(f);
            }
        }
#endif
    }

    LOG_TRACE(MOD_GRAPHICS, "POST-DRAW checkpoint 3: target outline");
    // Draw selection indicator around targeted entity
#ifdef EQT_HAS_GLES2
    overlays_->drawTargetOutline();
#else
    overlays_->drawTargetSelectionBox();
#endif
    frameTimings_.targetBox = measureSection();

    LOG_TRACE(MOD_GRAPHICS, "POST-DRAW checkpoint 4: particles (mgr={}, enabled={}, zoneReady={})",
              particleManager_ != nullptr,
              particleManager_ ? particleManager_->isEnabled() : false,
              zoneReady_);
    // P52: GPU-sync characterization -- particles+UI phase begins here and
    // ends after drawZoneLineOverlay() below (checkpoint 10), bracketing
    // everything from particle rendering through 2D/UI draw calls. This is
    // a coarser bucket than "particles" and "UI" individually (it also
    // covers the target box/casting bar/debug overlays that run in
    // between), which is an intentional simplification -- see
    // refactor/P52_gpu_sync_characterization.md. No-op when the probe is
    // disabled or unsupported.
    eqtGPUPhaseBegin(2 /* GPUSyncPhase::ParticlesUI */);

    // Render environmental particles (render every frame, update at Tier 3)
    if (particleManager_ && particleManager_->isEnabled() && zoneReady_) particleManager_->render();

    LOG_TRACE(MOD_GRAPHICS, "POST-DRAW checkpoint 5: unified particles (have3D={})", have3DTransforms_);
    // Unified particle system (fire point sprites, GLES2 only)
    //
    // P10b (history): gated this call on particleManager_->isEnabled() as a
    // workaround for a nondeterministic SIGSEGV inside
    // ParticleManager::renderUnified() -- a dangling-pointer-shaped race
    // against the SimulationWorker double-buffered result object
    // (particleRenderBuffer_ points into it). isEnabled() reflects the
    // *atmosphericParticles* setting (weather/dust/pollen billboard
    // emitters), which has no semantic relationship to unified fire/spell
    // particles -- it only "worked" as a workaround because
    // atmosphericParticles is false in the shipped
    // config/display_settings.json, so the gate happened to be a no-op for
    // that default while masking the crash whenever someone actually
    // enabled it.
    //
    // P23: root-caused and fixed for real in SimulationWorker
    // (simulation_worker.cpp workerLoop()/swapAndGetResults() -- see the
    // comments there, and tools/particle_race_stress.cpp for the
    // ThreadSanitizer reproduction: 55 races including two confirmed
    // heap-use-after-free reports pre-fix, zero races across repeated
    // 100k-iteration soaks post-fix). The isEnabled() gate here is now
    // redundant for the race it was covering, so it is REMOVED -- fire and
    // spell-effect particles should render whenever the zone/3D transforms
    // are ready, independent of the atmosphericParticles toggle. The
    // billboard emitter render() call above (checkpoint 4) is intentionally
    // still gated on isEnabled() -- that one IS the atmospheric particle
    // system it's named for.
#ifdef EQT_HAS_GLES2
    if (particleManager_ && zoneReady_ && have3DTransforms_) {
        // Pass View and Projection separately — captured during ESNRP_SOLID pass
        // before any 2D drawing overwrites the driver's transform state.
        // The shader multiplies them in GLSL (same as built-in COGLES2 shaders).
        float fogStart = zoneShader_ ? zoneShader_->fogStart() : 999999.0f;
        float fogEnd = zoneShader_ ? zoneShader_->fogEnd() : 999999.0f;
        const float* fogCol = zoneShader_ ? zoneShader_->fogColor() : nullptr;
        float screenH = static_cast<float>(driver_->getScreenSize().Height);
        particleManager_->renderUnified(captured3DView_, captured3DProj_,
                                        camera_ ? camera_->getAbsolutePosition() : irr::core::vector3df(0, 0, 0),
                                        fogStart, fogEnd, fogCol, screenH,
                                        particleRenderBuffer_);
    }
#endif
    frameTimings_.particles = measureSection();

    // Fire glow: update per-vertex lighting uniforms + render icospheres
#ifdef EQT_HAS_DRM
    if (fireEffectsEnabled_ && !fireGlowLights_.empty() && zoneReady_ && have3DTransforms_) {
        fireGlowTime_ += deltaTime;
        if (fireGlowLightingEnabled_) {
            eqe1_->updateFireGlowLighting();
        }
        if (fireGlowIcospheresEnabled_ && icosphereProgram_ != 0) {
            eqe1_->renderFireGlowIcospheres();
        }
    }
#endif

    LOG_TRACE(MOD_GRAPHICS, "POST-DRAW checkpoint 6: boids");
    // Render ambient creatures (render every frame, update at Tier 3)
    if (boidsManager_ && boidsManager_->isEnabled() && zoneReady_) boidsManager_->render();
    frameTimings_.boids = measureSection();

    LOG_TRACE(MOD_GRAPHICS, "POST-DRAW checkpoint 7: weather");
    // Render weather effects
    if (weatherEffects_ && weatherEffects_->isEnabled()) weatherEffects_->render();
    frameTimings_.weatherRender = measureSection();

    // Draw collision debug lines
    if (playerConfig_.collisionDebug) drawCollisionDebugLines(deltaTime);

    // Culling debug: draw region bounding boxes as wireframe
    // Green = rendered (passes PVS + distance + frustum)
    // Blue = PVS-culled (occluded by walls/geometry)
    // Yellow = distance-culled (beyond render distance)
    // Red = frustum-culled (outside camera view cone)
    if (frustumDebugDraw_ && frustumCuller_) {
        // Disable depth test so debug lines are always visible on top of geometry
        irr::video::SMaterial debugMat;
        debugMat.Lighting = false;
        debugMat.ZBuffer = irr::video::ECFN_ALWAYS;
        debugMat.ZWriteEnable = false;
        debugMat.AntiAliasing = false;
        debugMat.Thickness = 2.0f;
        driver_->setMaterial(debugMat);
        driver_->setTransform(irr::video::ETS_WORLD, irr::core::matrix4());

        // Get current PVS region for occlusion check
        std::shared_ptr<EQT::Graphics::BspRegion> pvsRegion;
        if (usePvsCulling_ && zoneBspTree_ && currentPvsRegion_ != SIZE_MAX
            && currentPvsRegion_ < zoneBspTree_->regions.size()) {
            pvsRegion = zoneBspTree_->regions[currentPvsRegion_];
        }

        float camX = playerX_, camY = playerY_, camZ = playerZ_;

        for (auto& [regionIdx, eqBbox] : regionBoundingBoxes_) {
            // Determine culling state using the same logic as the rendering pipeline
            irr::video::SColor color;

            // 1. PVS check
            bool pvsVisible = true;
            if (pvsRegion && !pvsRegion->visibleRegions.empty()) {
                if (regionIdx == currentPvsRegion_) {
                    pvsVisible = true;
                } else if (regionIdx < pvsRegion->visibleRegions.size()) {
                    pvsVisible = pvsRegion->visibleRegions[regionIdx];
                }
            }

            if (!pvsVisible) {
                color = irr::video::SColor(255, 64, 64, 255);  // Blue = PVS-culled
            } else {
                // 2. Distance check
                float closestX = std::max(eqBbox.MinEdge.X, std::min(camX, eqBbox.MaxEdge.X));
                float closestY = std::max(eqBbox.MinEdge.Y, std::min(camY, eqBbox.MaxEdge.Y));
                float closestZ = std::max(eqBbox.MinEdge.Z, std::min(camZ, eqBbox.MaxEdge.Z));
                float ddx = camX - closestX, ddy = camY - closestY, ddz = camZ - closestZ;
                float dist = std::sqrt(ddx*ddx + ddy*ddy + ddz*ddz);

                if (dist > renderDistance_) {
                    color = irr::video::SColor(255, 255, 255, 0);  // Yellow = distance-culled
                } else {
                    // 3. Frustum check
                    bool inFrustum = frustumCuller_->testAABB(
                        eqBbox.MinEdge.X, eqBbox.MinEdge.Y, eqBbox.MinEdge.Z,
                        eqBbox.MaxEdge.X, eqBbox.MaxEdge.Y, eqBbox.MaxEdge.Z);

                    if (!inFrustum) {
                        color = irr::video::SColor(255, 255, 0, 0);  // Red = frustum-culled
                    } else if (occlusionCulledRegions_.count(regionIdx)) {
                        color = irr::video::SColor(200, 255, 0, 255);  // Magenta = occlusion-culled
                    } else {
                        color = irr::video::SColor(255, 0, 255, 0);  // Green = rendered
                    }
                }
            }

            // Convert EQ Z-up bbox to Irrlicht Y-up corners: EQ(x,y,z) -> Irr(x,z,y)
            irr::core::vector3df corners[8];
            corners[0] = irr::core::vector3df(eqBbox.MinEdge.X, eqBbox.MinEdge.Z, eqBbox.MinEdge.Y);
            corners[1] = irr::core::vector3df(eqBbox.MaxEdge.X, eqBbox.MinEdge.Z, eqBbox.MinEdge.Y);
            corners[2] = irr::core::vector3df(eqBbox.MaxEdge.X, eqBbox.MaxEdge.Z, eqBbox.MinEdge.Y);
            corners[3] = irr::core::vector3df(eqBbox.MinEdge.X, eqBbox.MaxEdge.Z, eqBbox.MinEdge.Y);
            corners[4] = irr::core::vector3df(eqBbox.MinEdge.X, eqBbox.MinEdge.Z, eqBbox.MaxEdge.Y);
            corners[5] = irr::core::vector3df(eqBbox.MaxEdge.X, eqBbox.MinEdge.Z, eqBbox.MaxEdge.Y);
            corners[6] = irr::core::vector3df(eqBbox.MaxEdge.X, eqBbox.MaxEdge.Z, eqBbox.MaxEdge.Y);
            corners[7] = irr::core::vector3df(eqBbox.MinEdge.X, eqBbox.MaxEdge.Z, eqBbox.MaxEdge.Y);

            // Bottom face
            driver_->draw3DLine(corners[0], corners[1], color);
            driver_->draw3DLine(corners[1], corners[2], color);
            driver_->draw3DLine(corners[2], corners[3], color);
            driver_->draw3DLine(corners[3], corners[0], color);
            // Top face
            driver_->draw3DLine(corners[4], corners[5], color);
            driver_->draw3DLine(corners[5], corners[6], color);
            driver_->draw3DLine(corners[6], corners[7], color);
            driver_->draw3DLine(corners[7], corners[4], color);
            // Vertical edges
            driver_->draw3DLine(corners[0], corners[4], color);
            driver_->draw3DLine(corners[1], corners[5], color);
            driver_->draw3DLine(corners[2], corners[6], color);
            driver_->draw3DLine(corners[3], corners[7], color);
        }
    }

    // Map overlay
    if (showMapOverlay_) {
        overlays_->updateMapOverlay(glm::vec3(playerX_, playerZ_, playerY_));
        overlays_->drawMapOverlay();
    }

    // Navmesh overlay
    if (showNavmeshOverlay_) {
        overlays_->updateNavmeshOverlay(glm::vec3(playerX_, playerZ_, playerY_));
        overlays_->drawNavmeshOverlay();
    }

    // Portal wireframe debug overlay — batched into a single GL_LINES draw call.
    // Draws all portals within distance of camera (not limited to current region).
    if (portalDebugDraw_ && portalSystem_ && portalSystem_->hasPortals() && have3DTransforms_) {
#ifdef EQT_HAS_GLES2
        const auto& portalData = portalSystem_->getData();

        // Camera position in EQ coords
        float camEqX = playerX_;
        float camEqY = playerZ_;  // Irrlicht Z -> EQ Y
        float camEqZ = playerY_;  // Irrlicht Y -> EQ Z
        const float maxDistSq = 500.0f * 500.0f;

        // Batch all lines: 2 vertices per line, 7 floats per vertex (xyz + rgba)
        std::vector<float> lineBuf;
        lineBuf.reserve(4096);
        size_t lineCount = 0;

        auto pushLine = [&](float x1, float y1, float z1, float x2, float y2, float z2,
                            float r, float g, float b) {
            // EQ (x,y,z) -> Irrlicht (x,z,y)
            lineBuf.push_back(x1); lineBuf.push_back(z1); lineBuf.push_back(y1);
            lineBuf.push_back(r); lineBuf.push_back(g); lineBuf.push_back(b); lineBuf.push_back(1.0f);
            lineBuf.push_back(x2); lineBuf.push_back(z2); lineBuf.push_back(y2);
            lineBuf.push_back(r); lineBuf.push_back(g); lineBuf.push_back(b); lineBuf.push_back(1.0f);
            lineCount++;
        };

        for (size_t pi = 0; pi < portalData.portals.size(); ++pi) {
            const auto& portal = portalData.portals[pi];
            size_t n = portal.vertexCount();
            if (n < 3) continue;

            // Distance cull by portal center
            float dx = portal.centerX - camEqX;
            float dy = portal.centerY - camEqY;
            float dz = portal.centerZ - camEqZ;
            if (dx * dx + dy * dy + dz * dz > maxDistSq) continue;

            bool isAdjacentToCamera = (portal.regionA == currentPvsRegion_ ||
                                       portal.regionB == currentPvsRegion_);
            float r, g, b;
            if (isAdjacentToCamera) { r = 0.0f; g = 1.0f; b = 1.0f; }  // Cyan
            else { r = 1.0f; g = 0.647f; b = 0.0f; }                    // Orange

            for (size_t vi = 0; vi < n; ++vi) {
                size_t vj = (vi + 1) % n;
                pushLine(portal.vx(vi), portal.vy(vi), portal.vz(vi),
                         portal.vx(vj), portal.vy(vj), portal.vz(vj), r, g, b);
            }
        }

        if (lineCount > 0) {
            // Restore 3D camera transforms (2D draws during drawAll() overwrite them)
            driver_->setTransform(irr::video::ETS_VIEW, captured3DView_);
            driver_->setTransform(irr::video::ETS_PROJECTION, captured3DProj_);
            driver_->setTransform(irr::video::ETS_WORLD, irr::core::matrix4());

            gles2Draw3DLinesBatch(driver_, lineBuf.data(), static_cast<unsigned int>(lineCount));
        }
#endif
    }

    // Stencil buffer debug overlay (show stencil levels as colored fullscreen rects)
    // Only available on GLES2 with portal occlusion active
#ifdef EQT_HAS_GLES2
    if (stencilDebugDraw_ && portalOcclusionEnabled_) {
        irr::video::SColor levelColors[4] = {
            irr::video::SColor(80, 0, 255, 0),     // Level 1: green
            irr::video::SColor(80, 0, 0, 255),     // Level 2: blue
            irr::video::SColor(80, 255, 255, 0),   // Level 3: yellow
            irr::video::SColor(80, 255, 0, 255)    // Level 4: magenta
        };

        for (int level = 1; level <= 4; ++level) {
            glEnable(GL_STENCIL_TEST);
            glStencilFunc(GL_EQUAL, level, 0xFF);
            glStencilOp(GL_KEEP, GL_KEEP, GL_KEEP);

            irr::core::rect<irr::s32> fullScreen(0, 0,
                static_cast<irr::s32>(driver_->getScreenSize().Width),
                static_cast<irr::s32>(driver_->getScreenSize().Height));
            driver_->draw2DRectangle(levelColors[level - 1], fullScreen);
        }
        glDisable(GL_STENCIL_TEST);
    }
#endif

    frameTimings_.debugOverlays = measureSection();

    LOG_TRACE(MOD_GRAPHICS, "POST-DRAW checkpoint 8: casting bars (entityRenderer={})", entityRenderer_ != nullptr);
    // Draw entity casting bars
    if (!allUIHidden_ && entityRenderer_) entityRenderer_->renderEntityCastingBars(driver_, guienv_, camera_);
    frameTimings_.castingBars = measureSection();

    LOG_TRACE(MOD_GRAPHICS, "POST-DRAW checkpoint 9: GUI drawAll");
    if (!allUIHidden_ && !newUIEnabled_) {
        guienv_->drawAll();
    }
    if (!allUIHidden_) {
        overlays_->drawFPSCounter();
    }
    frameTimings_.guiDrawAll = measureSection();

    // U06j: Process new UI input before rendering
    if (newUIEnabled_ && staticUIInput_ && eventReceiver_) {
        staticUIInput_->processInput(
            eventReceiver_->getMouseX(), eventReceiver_->getMouseY(), pendingUIClick_,
            uiLayout_, hotbarState_, spellGemState_, inventoryState_,
            spellbookState_, skillsPopupState_, buffBarState_, lootState_);
        pendingUIClick_ = false;
    }

    // T60: the paperdoll only exists/renders while the inventory popup is
    // open — lazy construction on first open, RTT re-render gated by its
    // own markDirty()/dirty_ flag (never a per-frame re-render). Nothing
    // runs here when the popup is closed.
    if (newUIEnabled_ && inventoryState_.activePopup == PopupType::Inventory) {
        overlays_->updatePaperdollForFrame();
    }
    inventoryState_.paperdollTexture = characterModelView_ ? characterModelView_->getTexture() : nullptr;

    // U03c: Render new static UI when enabled.
    // T301 Leg D: ...and NOT when the UI is hidden. `allUIHidden_` (F11 /
    // `/hideui`) predates the static UI and gated only the three things that
    // existed when it was written -- the casting bars, the old guienv_ draw
    // and the FPS counter -- so with the static UI on (the shipped default)
    // pressing F11 left the chat panel, the hotbar and every other panel
    // painted on top of the frame. That is why T300 Leg C could not rule out
    // a weapon sliver hidden under the chat box.
    if (newUIEnabled_ && uiRenderer_ && !allUIHidden_) {
        uiRenderer_->beginFrame();
        renderPlayerStatus(*uiRenderer_, uiLayout_, cachedPlayerStats_);
        renderTargetInfo(*uiRenderer_, uiLayout_, cachedTargetInfo_);
        renderChatPanel(*uiRenderer_, uiLayout_, chatPanelState_);
        renderHotbar(*uiRenderer_, uiLayout_, hotbarState_);
        renderSpellGemPanel(*uiRenderer_, uiLayout_, spellGemState_);
        renderBuffBar(*uiRenderer_, uiLayout_, buffBarState_);
        renderCastingBar(*uiRenderer_, uiLayout_, castingBarState_);
        renderGroupPanel(*uiRenderer_, uiLayout_, groupPanelState_);
        renderPetPanel(*uiRenderer_, uiLayout_, petPanelState_);
        renderInventoryPopup(*uiRenderer_, uiLayout_, inventoryState_);
        renderSpellbookPopup(*uiRenderer_, uiLayout_, spellbookState_);
        renderSkillsPopup(*uiRenderer_, uiLayout_, skillsPopupState_);
        // T290: last of the center popups, so it is on top. It only draws
        // while the game says a corpse is open, and opening one closes the
        // other three (IrrlichtBridge's LootWindowOpened arm).
        renderLootPopup(*uiRenderer_, uiLayout_, lootState_);
        renderXPBar(*uiRenderer_, uiLayout_, xpBarState_);
        uiRenderer_->endFrame();
    }
    frameTimings_.staticUI = measureSection();

    // Draw zone line overlay
    overlays_->drawZoneLineOverlay();
    overlays_->drawZoneLineBoxLabels();
    frameTimings_.zoneLineOverlay = measureSection();
    eqtGPUPhaseEnd(2 /* GPUSyncPhase::ParticlesUI */);

#ifdef WITH_RDP
    rdp_->captureFrameForRDP();
#endif

    // Draw software mouse cursor for DRM/KMS mode (no hardware cursor available)
    if (softwareCursorTexture_) {
        auto* cursor = device_->getCursorControl();
        if (cursor && cursor->isVisible()) {
            auto pos = cursor->getPosition();
            irr::core::position2di destPos(pos.X, pos.Y);
            irr::core::rect<irr::s32> srcRect(0, 0, 8, 10);
            driver_->draw2DImage(softwareCursorTexture_, destPos, srcRect,
                nullptr, irr::video::SColor(255, 255, 255, 255), true);
        }
    }

    // Take deferred screenshot after all rendering, before swap
    if (pendingScreenshot_) {
        pendingScreenshot_ = false;
        saveScreenshot("screenshot.png");
    }
    frameTimings_.postRender = measureSection();

    LOG_TRACE(MOD_GRAPHICS, "POST-DRAW checkpoint 11: endScene");
    // P52: Swap phase. endScene() -> Device->present() -> eglSwapBuffers() /
    // drmPageFlip() (docker/irrlicht-drm/COpenGLES2Driver.cpp,
    // CIrrDeviceFB.cpp). The GPU timer query wrapped around this span is
    // expected to read near-zero (a swap submits little/no new GPU command
    // work) -- the meaningful measurement for this phase is the CPU-side
    // sync-point attribution recorded inside present()/drmPageFlip() itself
    // (eqtGetLastSwapBuffersMs()/eqtGetLastFlipWaitMs()), not the GPU query.
    eqtGPUPhaseBegin(3 /* GPUSyncPhase::Swap */);
    driver_->endScene();
    eqtGPUPhaseEnd(3 /* GPUSyncPhase::Swap */);
    // Collect any GPU query results that became available (double-buffered,
    // non-blocking) and flip the write buffer for next frame. Must be
    // called exactly once per frame, after the last eqtGPUPhaseEnd() above.
    eqtGPUPhaseCollectResults();
    frameTimings_.endScene = measureSection();

    LOG_TRACE(MOD_GRAPHICS, "POST-DRAW checkpoint 12: frame complete");
    FlushThreadLog();
    return true;
}

} // namespace Graphics
} // namespace EQT
