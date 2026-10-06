// renderer_frame_render.cpp - IrrlichtRenderer frame phase 4 (render).
//
// Holds processFrameRender() alone. Phases 1-3 live in
// renderer_frame_input.cpp; the orchestrator processFrame() stays in
// renderer_frame.cpp.
//
// G05 (refactor/G05_process_frame_render.md): processFrameRender was a
// single 544-line body. It is now the ORDERED LIST of the sixteen private
// render passes defined below it, each a verbatim slice of that body
// INCLUDING its measureSection() bracket -- so every frameTimings_ category
// still spans exactly the code it spanned before, and the timing buckets are
// unchanged. Nothing was reordered, nothing crosses a cut: not one local of
// the original body outlived the pass it was declared in, so there is no
// cross-pass state object and no lifetime changed.
//
// THE ORDER IS LOAD-BEARING (depth, blending, overlay-on-top, and the swap
// last of all). It is pinned by tests/test_frame_render_order.cpp against
// tests/goldens/frame_render_order_g05.txt, a golden extracted from the
// pre-G05 body. That test composes processFrameRender by expanding each pass
// call recursively, so moving a statement from one pass to another, swapping
// two pass calls, or dropping one, all fail it.
//
// All nine of this file's conditional regions (__linux__, EQT_HAS_GLES2,
// EQT_HAS_DRM, WITH_RDP) still live wholly inside a single pass; none
// straddles a cut.
//
// WARNING: tests/test_ui_hide_command.cpp reads THIS FILE as text and
// requires that the static-UI draw block's guard, which ands the new-UI flag
// and the UI renderer against the negated all-UI-hidden flag, appear here
// verbatim. The literal condition is spelled out in that test, deliberately
// not here: a comment naming it would satisfy the guard on its own. Since
// G05 that block lives in renderStaticUI(), and the same test also requires
// that renderStaticUI() be CALLED from processFrameRender() -- the guard
// would otherwise pass over a pass nothing invokes.
// Split per refactor/J31_renderer_frame_split.md (move-only).
#include "client/graphics/irrlicht_renderer.h"
#include "client/graphics/frame/frame_component.h"
#include "client/graphics/world_line_batch.h"  // T335b: the shared post-drawAll() 3D-line guard
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
// The ordered list. See the pass definitions below.
bool FrameComponent::processFrameRender(float deltaTime) {
    // If loading screen is visible, show it instead of rendering zone
    if (ctx_.loadingScreenVisible) {
        ctx_.Overlays().drawLoadingScreen(loadingProgress_, loadingText_);
        return false;  // Signal loading screen was shown
    }

    recomposeSceneAmbientIfDirty();
    runSceneProfileIfScheduled();
    beginSceneAndDrawWorld(deltaTime);
    collectScenePassTimings();
    renderFootprints();
    checkFrameBudgetsAndLogStats();
    renderTargetIndicator();
    renderParticleEffects();
    renderFireGlowAndBoids(deltaTime);
    renderWeatherEffects();
    renderDebugOverlays(deltaTime);
    renderEntityCastingBars();
    renderLegacyGui();
    renderStaticUI();
    renderZoneLineOverlays();
    renderFrameTail();
    endSceneAndSwap();

    LOG_TRACE(MOD_GRAPHICS, "POST-DRAW checkpoint 12: frame complete");
    FlushThreadLog();
    return true;
}

// T423 (board B362): the ONE consumer of the scene-ambient dirty mark.
//
// Until this, IrrlichtRenderer::updateTimeOfDay() -- the only production site
// that calls composeSceneAmbient() and smgr_->setAmbientLight() -- ran only
// when an OP_TimeOfDay packet arrived or a vision write forced it. eqemu sends
// that packet at zone-in and on `#set time`, not on a timer, so a weather
// change, a zone-light toggle or an ambient-multiplier change reached the
// world's lighting at an arbitrary later moment or never. T418's first live
// session ran a whole day block of forced rain at four intensities and got
// zero recomposes out of the client.
//
// It is FIRST in the pass list, before beginScene(): the ambient it sets is
// read by smgr_->drawAll() and by the zone shader's uniforms in the same
// frame, so a recompute after the draw would land a frame late. Costs one
// bool test per frame when nothing has changed.
void FrameComponent::recomposeSceneAmbientIfDirty() {
    if (owner_.isSceneAmbientDirty()) owner_.updateTimeOfDay(ctx_.currentHour, ctx_.currentMinute);
}

void FrameComponent::runSceneProfileIfScheduled() {
    // Run scene breakdown profile if scheduled
    if (ctx_.Diagnostics().sceneProfileEnabled()) {
        if (ctx_.Diagnostics().sceneProfileFrameCount() < 0) {
            ctx_.Diagnostics().sceneProfileFrameCount()++;
        } else {
            ctx_.Diagnostics().profileSceneBreakdown();
        }
    }
}

void FrameComponent::beginSceneAndDrawWorld(float deltaTime) {
    // Render - use sky renderer's clear color for day/night effect
    irr::video::SColor clearColor(255, 50, 50, 80);
    if (ctx_.skyRenderer && ctx_.skyRenderer->isEnabled() && ctx_.skyRenderer->isInitialized()) {
        clearColor = ctx_.skyRenderer->getCurrentClearColor();
    }
    LOG_TRACE(MOD_GRAPHICS, "PRE-DRAW checkpoint 0: beginScene");
    ctx_.driver->beginScene(true, true, clearColor);
    sectionStart_ = std::chrono::steady_clock::now();
    if (ctx_.renderPassTimer) ctx_.renderPassTimer->reset();
    if (ctx_.zoneShader) {
        // Update wind shader uniforms each frame
        windTime_ += deltaTime;
        ctx_.zoneShader->setWindTime(windTime_);
        // Wind params from tree wind config (or defaults) with weather multiplier.
        // P33: gate the actual displacement magnitude on the tree-wind-animation
        // setting (treeWindController_.isEnabled(), backed by
        // display_settings.json "animatedTrees") — when disabled, force the
        // multiplier to 0 so uWindParams collapses strength = baseStr + gustStr*gust
        // to exactly 0 in the vertex shader (zone_shader.cpp), i.e. zero
        // displacement, not merely a frozen nonzero pose. windTime_ keeps
        // advancing either way (cheap, avoids a discontinuity if re-enabled
        // mid-session); it has zero visual effect while strength is 0.
        float weatherMult = ctx_.treeWindController.isEnabled()
            ? ctx_.treeWindController.getWeatherMultiplier()
            : 0.0f;
        // Default config values scaled by weather
        ctx_.zoneShader->setWindParams(
            0.3f * weatherMult,   // baseStrength
            0.4f,                 // baseFrequency
            0.5f * weatherMult,   // gustStrength
            0.1f);                // gustFrequency

        ctx_.zoneShader->beginFrame();
    }
    // T300: the down-pitch clamp that hides the player's own body. Re-checked
    // every frame because the clamp tracks the FOV and because entering first
    // person from a steeply-pitched third-person view has to be corrected.
    // A no-op outside first person, and a no-op in first person unless the
    // pitch is out of range. Must run BEFORE the draw.
    ctx_.PlayerCamera().applyFirstPersonPitchClamp();

    ctx_.smgr->drawAll();
    frameTimings_.sceneDrawAll = measureSection();
}

void FrameComponent::collectScenePassTimings() {
    // 3D camera transforms are captured during the SOLID render pass in
    // OnRenderPassPreRender — see captured3DView_/captured3DProj_.
    if (ctx_.renderPassTimer) {
        frameTimings_.sceneAnimate = ctx_.renderPassTimer->getAnimateTime();
        frameTimings_.sceneSolid = ctx_.renderPassTimer->solidTime;
        frameTimings_.sceneTransparent = ctx_.renderPassTimer->transparentTime;
        frameTimings_.sceneSkybox = ctx_.renderPassTimer->skyboxTime;
        frameTimings_.sceneOther = ctx_.renderPassTimer->otherTime;
        frameTimings_.sceneNodeCount = ctx_.renderPassTimer->nodeCount;
    }
    if (frameTimings_.sceneDrawAll > 50000) {
        LOG_WARN(MOD_GRAPHICS, "PERF: smgr->drawAll() took {} ms", frameTimings_.sceneDrawAll / 1000);
    }
}

void FrameComponent::renderFootprints() {
    LOG_TRACE(MOD_GRAPHICS, "POST-DRAW checkpoint 1: getPrimitiveCountDrawn");
    // Track polygon count for constrained mode budget
    lastPolygonCount_ = ctx_.driver->getPrimitiveCountDrawn();

    LOG_TRACE(MOD_GRAPHICS, "POST-DRAW checkpoint 2: footprints (detail={})", ctx_.detailManager != nullptr);
    // Render footprints (after terrain, before UI)
    if (ctx_.detailManager && ctx_.detailManager->isFootprintEnabled()) {
        ctx_.detailManager->renderFootprints();
    }
    frameTimings_.footprintRender = measureSection();
}

void FrameComponent::checkFrameBudgetsAndLogStats() {
    if (lastPolygonCount_ > static_cast<uint32_t>(ctx_.config.constrainedConfig.maxPolygonsPerFrame)) {
        if (++polygonBudgetExceededFrames_ >= 60) {
            LOG_WARN(MOD_GRAPHICS, "Polygon budget exceeded: {} > {} (limit)",
                     lastPolygonCount_, ctx_.config.constrainedConfig.maxPolygonsPerFrame);
            polygonBudgetExceededFrames_ = 0;
        }
    } else {
        polygonBudgetExceededFrames_ = 0;
    }

    if (++constrainedStatsLogCounter_ >= 150) {
        constrainedStatsLogCounter_ = 0;
        int visibleEntities = ctx_.entityRenderer ? ctx_.entityRenderer->getVisibleEntityCount() : 0;
        int totalEntities = ctx_.entityRenderer ? static_cast<int>(ctx_.entityRenderer->getEntityCount()) : 0;
        size_t tmuUsed = 0, tmuLimit = 0;
        float hitRate = 0.0f;
        size_t evictions = 0;
        if (ctx_.constrainedTextureCache) {
            tmuUsed = ctx_.constrainedTextureCache->getCurrentUsage();
            tmuLimit = ctx_.constrainedTextureCache->getMemoryLimit();
            hitRate = ctx_.constrainedTextureCache->getHitRate();
            evictions = ctx_.constrainedTextureCache->getEvictionCount();
        }
        size_t fbiUsed = ctx_.config.constrainedConfig.calculateFramebufferUsage(ctx_.config.width, ctx_.config.height);
        size_t fbiLimit = ctx_.config.constrainedConfig.framebufferMemoryBytes;
        LOG_INFO(MOD_GRAPHICS, "=== RENDERER STATS [{}] ===",
                 ConstrainedRendererConfig::presetName(ctx_.config.constrainedPreset));
        LOG_INFO(MOD_GRAPHICS, "  Resolution: {}x{} @ {}-bit (FBI: {:.1f}MB/{:.1f}MB)",
                 ctx_.config.width, ctx_.config.height, ctx_.config.constrainedConfig.colorDepthBits,
                 fbiUsed / (1024.0f * 1024.0f), fbiLimit / (1024.0f * 1024.0f));
        LOG_INFO(MOD_GRAPHICS, "  Textures: TMU {:.1f}MB/{:.1f}MB | Hit: {:.0f}% | Evictions: {}",
                 tmuUsed / (1024.0f * 1024.0f), tmuLimit / (1024.0f * 1024.0f), hitRate, evictions);
        LOG_INFO(MOD_GRAPHICS, "  Geometry: Polys {}/{} | Entities {}/{} (max {}) | Clip {:.0f}",
                 lastPolygonCount_, ctx_.config.constrainedConfig.maxPolygonsPerFrame,
                 visibleEntities, totalEntities, ctx_.config.constrainedConfig.maxVisibleEntities,
                 ctx_.config.constrainedConfig.clipDistance);
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
}

void FrameComponent::renderTargetIndicator() {
    LOG_TRACE(MOD_GRAPHICS, "POST-DRAW checkpoint 3: target outline");
    // Draw selection indicator around targeted entity
#ifdef EQT_HAS_GLES2
    ctx_.Overlays().drawTargetOutline();
#else
    ctx_.Overlays().drawTargetSelectionBox();
#endif
    frameTimings_.targetBox = measureSection();
}

void FrameComponent::renderParticleEffects() {
    LOG_TRACE(MOD_GRAPHICS, "POST-DRAW checkpoint 4: particles (mgr={}, enabled={}, zoneReady={})",
              ctx_.particleManager != nullptr,
              ctx_.particleManager ? ctx_.particleManager->isEnabled() : false,
              ctx_.zoneReady);
    // P52: GPU-sync characterization -- particles+UI phase begins here and
    // ends after drawZoneLineBoxLabels() below (checkpoint 10), bracketing
    // everything from particle rendering through 2D/UI draw calls. This is
    // a coarser bucket than "particles" and "UI" individually (it also
    // covers the target box/casting bar/debug overlays that run in
    // between), which is an intentional simplification -- see
    // refactor/P52_gpu_sync_characterization.md. No-op when the probe is
    // disabled or unsupported.
    eqtGPUPhaseBegin(2 /* GPUSyncPhase::ParticlesUI */);

    // Render environmental particles (render every frame, update at Tier 3)
    if (ctx_.particleManager && ctx_.particleManager->isEnabled() && ctx_.zoneReady) ctx_.particleManager->render();

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
    if (ctx_.particleManager && ctx_.zoneReady && have3DTransforms_) {
        // Pass View and Projection separately — captured during ESNRP_SOLID pass
        // before any 2D drawing overwrites the driver's transform state.
        // The shader multiplies them in GLSL (same as built-in COGLES2 shaders).
        float fogStart = ctx_.zoneShader ? ctx_.zoneShader->fogStart() : 999999.0f;
        float fogEnd = ctx_.zoneShader ? ctx_.zoneShader->fogEnd() : 999999.0f;
        const float* fogCol = ctx_.zoneShader ? ctx_.zoneShader->fogColor() : nullptr;
        float screenH = static_cast<float>(ctx_.driver->getScreenSize().Height);
        ctx_.particleManager->renderUnified(ctx_.captured3DView, ctx_.captured3DProj,
                                        ctx_.camera ? ctx_.camera->getAbsolutePosition() : irr::core::vector3df(0, 0, 0),
                                        fogStart, fogEnd, fogCol, screenH,
                                        ctx_.particleRenderBuffer);
    }
#endif
    frameTimings_.particles = measureSection();
}

void FrameComponent::renderFireGlowAndBoids(float deltaTime) {
    // Fire glow: update per-vertex lighting uniforms + render icospheres
#ifdef EQT_HAS_DRM
    if (ctx_.fireEffectsEnabled && !ctx_.fireGlowLights.empty() && ctx_.zoneReady && have3DTransforms_) {
        fireGlowTime_ += deltaTime;
        if (ctx_.fireGlowLightingEnabled) {
            ctx_.Eqe1().updateFireGlowLighting();
        }
        if (ctx_.fireGlowIcospheresEnabled && ctx_.icosphereProgram != 0) {
            ctx_.Eqe1().renderFireGlowIcospheres();
        }
    }
#endif

    LOG_TRACE(MOD_GRAPHICS, "POST-DRAW checkpoint 6: boids");
    // Render ambient creatures (render every frame, update at Tier 3)
    if (ctx_.boidsManager && ctx_.boidsManager->isEnabled() && ctx_.zoneReady) ctx_.boidsManager->render();
    frameTimings_.boids = measureSection();
}

void FrameComponent::renderWeatherEffects() {
    LOG_TRACE(MOD_GRAPHICS, "POST-DRAW checkpoint 7: weather");
    // Render weather effects
    if (ctx_.weatherEffects && ctx_.weatherEffects->isEnabled()) ctx_.weatherEffects->render();
    frameTimings_.weatherRender = measureSection();
}

void FrameComponent::renderDebugOverlays(float deltaTime) {
    // Draw collision debug lines
    if (ctx_.PlayerCamera().playerConfig().collisionDebug) ctx_.PlayerCamera().drawCollisionDebugLines(deltaTime);

    // Culling debug: draw region bounding boxes as wireframe
    // Green = rendered (passes PVS + distance + frustum)
    // Blue = PVS-culled (occluded by walls/geometry)
    // Yellow = distance-culled (beyond render distance)
    // Red = frustum-culled (outside camera view cone)
    if (frustumDebugDraw_ && ctx_.frustumCuller) {
        // Disable depth test so debug lines are always visible on top of geometry
        irr::video::SMaterial debugMat;
        debugMat.Lighting = false;
        debugMat.ZBuffer = irr::video::ECFN_ALWAYS;
        debugMat.ZWriteEnable = false;
        debugMat.AntiAliasing = false;
        debugMat.Thickness = 2.0f;
        // T335b (B054): this batch runs after smgr->drawAll() with the GLES2
        // driver latched in 2D mode, so a bare setMaterial() + world-space
        // draw3DLine() drew nothing AND left back-face culling on, wiping the
        // 2D UI for the rest of the frame. WorldLineBatch installs the
        // captured 3D view/projection and hands the 2D-safe state back in its
        // destructor, which runs at this if-block's closing brace -- after the
        // last draw3DLine() below and before the map overlay. The statement it
        // replaces is golden entry 127 (see tests/test_frame_render_order.cpp).
        WorldLineBatch batch(ctx_, debugMat);
        ctx_.driver->setTransform(irr::video::ETS_WORLD, irr::core::matrix4());

        // Get current PVS region for occlusion check
        EQT::Graphics::PvsBits pvsRegion;
        if (ctx_.usePvsCulling && ctx_.zoneBspTree && ctx_.currentPvsRegion != SIZE_MAX
            && ctx_.currentPvsRegion < ctx_.zoneBspTree->regions.size() && ctx_.zoneBspTree->regions[ctx_.currentPvsRegion]) {
            pvsRegion = ctx_.zoneBspTree->regions[ctx_.currentPvsRegion]->pvs();
        }

        float camX = ctx_.playerX, camY = ctx_.playerY, camZ = ctx_.playerZ;

        for (auto& [regionIdx, eqBbox] : ctx_.regionBoundingBoxes) {
            // Determine culling state using the same logic as the rendering pipeline
            irr::video::SColor color;

            // 1. PVS check
            bool pvsVisible = true;
            if (!pvsRegion.empty()) {
                if (regionIdx == ctx_.currentPvsRegion) {
                    pvsVisible = true;
                } else if (regionIdx < pvsRegion.size()) {
                    pvsVisible = pvsRegion[regionIdx];
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

                if (dist > ctx_.renderDistance) {
                    color = irr::video::SColor(255, 255, 255, 0);  // Yellow = distance-culled
                } else {
                    // 3. Frustum check
                    bool inFrustum = ctx_.frustumCuller->testAABB(
                        eqBbox.MinEdge.X, eqBbox.MinEdge.Y, eqBbox.MinEdge.Z,
                        eqBbox.MaxEdge.X, eqBbox.MaxEdge.Y, eqBbox.MaxEdge.Z);

                    if (!inFrustum) {
                        color = irr::video::SColor(255, 255, 0, 0);  // Red = frustum-culled
                    } else if (ctx_.occlusionCulledRegions.count(regionIdx)) {
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
            ctx_.driver->draw3DLine(corners[0], corners[1], color);
            ctx_.driver->draw3DLine(corners[1], corners[2], color);
            ctx_.driver->draw3DLine(corners[2], corners[3], color);
            ctx_.driver->draw3DLine(corners[3], corners[0], color);
            // Top face
            ctx_.driver->draw3DLine(corners[4], corners[5], color);
            ctx_.driver->draw3DLine(corners[5], corners[6], color);
            ctx_.driver->draw3DLine(corners[6], corners[7], color);
            ctx_.driver->draw3DLine(corners[7], corners[4], color);
            // Vertical edges
            ctx_.driver->draw3DLine(corners[0], corners[4], color);
            ctx_.driver->draw3DLine(corners[1], corners[5], color);
            ctx_.driver->draw3DLine(corners[2], corners[6], color);
            ctx_.driver->draw3DLine(corners[3], corners[7], color);
        }
    }

    // Map overlay
    if (ctx_.Overlays().showMapOverlay()) {
        ctx_.Overlays().updateMapOverlay(glm::vec3(ctx_.playerX, ctx_.playerZ, ctx_.playerY));
        ctx_.Overlays().drawMapOverlay();
    }

    // Navmesh overlay
    if (ctx_.Overlays().showNavmeshOverlay()) {
        ctx_.Overlays().updateNavmeshOverlay(glm::vec3(ctx_.playerX, ctx_.playerZ, ctx_.playerY));
        ctx_.Overlays().drawNavmeshOverlay();
    }

    // Portal wireframe debug overlay — batched into a single GL_LINES draw call.
    // Draws all portals within distance of camera (not limited to current region).
    if (ctx_.Diagnostics().portalDebugDraw() && ctx_.portalSystem && ctx_.portalSystem->hasPortals() && have3DTransforms_) {
#ifdef EQT_HAS_GLES2
        const auto& portalData = ctx_.portalSystem->getData();

        // Camera position in EQ coords
        float camEqX = ctx_.playerX;
        float camEqY = ctx_.playerZ;  // Irrlicht Z -> EQ Y
        float camEqZ = ctx_.playerY;  // Irrlicht Y -> EQ Z
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

            bool isAdjacentToCamera = (portal.regionA == ctx_.currentPvsRegion ||
                                       portal.regionB == ctx_.currentPvsRegion);
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
            ctx_.driver->setTransform(irr::video::ETS_VIEW, ctx_.captured3DView);
            ctx_.driver->setTransform(irr::video::ETS_PROJECTION, ctx_.captured3DProj);
            ctx_.driver->setTransform(irr::video::ETS_WORLD, irr::core::matrix4());

            gles2Draw3DLinesBatch(ctx_.driver, lineBuf.data(), static_cast<unsigned int>(lineCount));
        }
#endif
    }

    // Stencil buffer debug overlay (show stencil levels as colored fullscreen rects)
    // Only available on GLES2 with portal occlusion active
#ifdef EQT_HAS_GLES2
    if (ctx_.Diagnostics().stencilDebugDraw() && ctx_.portalOcclusionEnabled) {
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
                static_cast<irr::s32>(ctx_.driver->getScreenSize().Width),
                static_cast<irr::s32>(ctx_.driver->getScreenSize().Height));
            ctx_.driver->draw2DRectangle(levelColors[level - 1], fullScreen);
        }
        glDisable(GL_STENCIL_TEST);
    }
#endif

    frameTimings_.debugOverlays = measureSection();
}

void FrameComponent::renderEntityCastingBars() {
    LOG_TRACE(MOD_GRAPHICS, "POST-DRAW checkpoint 8: casting bars (entityRenderer={})", ctx_.entityRenderer != nullptr);
    // Draw entity casting bars
    if (!allUIHidden_ && ctx_.entityRenderer) ctx_.entityRenderer->renderEntityCastingBars(ctx_.driver, ctx_.Overlays().guienv(), ctx_.camera);
    frameTimings_.castingBars = measureSection();
}

void FrameComponent::renderLegacyGui() {
    LOG_TRACE(MOD_GRAPHICS, "POST-DRAW checkpoint 9: GUI drawAll");
    if (!allUIHidden_ && !newUIEnabled_) {
        ctx_.Overlays().guienv()->drawAll();
    }
    if (!allUIHidden_) {
        ctx_.Overlays().drawFPSCounter();
    }
    frameTimings_.guiDrawAll = measureSection();
}

void FrameComponent::renderStaticUI() {
    // U06j: Process new UI input before rendering
    if (newUIEnabled_ && staticUIInput_ && ctx_.eventReceiver) {
        // B442 (#151): the right click, the modifiers and the cursor, for the
        // item windows' click-to-cursor gesture. The QuantityWnd is above
        // every other window, so a click it takes reaches nothing below it.
        bool rightClick = ctx_.eventReceiver->wasRightButtonClicked();
        {
            eqt::inventory::ClickModifiers mods;
            mods.shift = ctx_.eventReceiver->isKeyDown(irr::KEY_SHIFT);
            mods.ctrl = ctx_.eventReceiver->isKeyDown(irr::KEY_LCONTROL) ||
                        ctx_.eventReceiver->isKeyDown(irr::KEY_RCONTROL);
            mods.alt = ctx_.eventReceiver->isKeyDown(irr::KEY_LMENU) || ctx_.eventReceiver->isKeyDown(irr::KEY_RMENU);
            if (staticUIInput_->processDialogInput(
                    ctx_.eventReceiver->getMouseX(), ctx_.eventReceiver->getMouseY(),
                    pendingUIClick_, uiLayout_)) {
                pendingUIClick_ = false;
                rightClick = false;
            }
            staticUIInput_->setFrameClickState(rightClick, mods, &cursorState_);
        }
        // B834: the tip window draws above every window but the Quantity
        // window, so it is asked before them and keeps the click it takes.
        if (processTipWindowInput()) pendingUIClick_ = false;
        // B896 (#212): the trade / give window is above every window from
        // here to the containers, and the inventory is open beside it, so it
        // takes its click before them (renderer_frame_trade.cpp).
        processTradeWindowInput();
        bool uiTook = staticUIInput_->processInput(
            ctx_.eventReceiver->getMouseX(), ctx_.eventReceiver->getMouseY(), pendingUIClick_,
            uiLayout_, hotbarState_, spellGemState_, inventoryState_,
            spellbookState_, skillsPopupState_, buffBarState_, lootState_, ctx_.eventReceiver->isKeyDown(irr::KEY_SHIFT));
        uiTook |= staticUIInput_->processPanelInput(
            ctx_.eventReceiver->getMouseX(), ctx_.eventReceiver->getMouseY(),
            pendingUIClick_, uiLayout_, tradeskillState_, combatAbilityState_);
        // T436 (board B429): the merchant and bank windows, and the mouse
        // position the cursor attachment is drawn at. The bank is hit-tested
        // above the centre popups and the merchant is one of them, so this
        // hop sits between processPanelInput() and the two windows that are
        // higher still.
        cursorState_.mouseX = ctx_.eventReceiver->getMouseX();
        cursorState_.mouseY = ctx_.eventReceiver->getMouseY();
        uiTook |= staticUIInput_->processCommerceInput(
            ctx_.eventReceiver->getMouseX(), ctx_.eventReceiver->getMouseY(),
            pendingUIClick_, uiLayout_, merchantState_, bankState_, cursorState_);
        // B898 (#214): the trainer window draws above the bank, so it is
        // asked after it and before the item display / Options windows.
        uiTook |= staticUIInput_->processTrainerInput(
            ctx_.eventReceiver->getMouseX(), ctx_.eventReceiver->getMouseY(),
            pendingUIClick_, uiLayout_, trainerState_);
        // B895: the Tracking Window -- above the trainer, below the item display.
        uiTook |= processTrackingInput(ctx_.bridge, ctx_.eventReceiver->getMouseX(), ctx_.eventReceiver->getMouseY(), pendingUIClick_, uiLayout_, trackingState_);
        // B899 (#215): the container windows -- their own placement, above the
        // centre popups, the bank, the trainer and the tracking window, below
        // the item display and the Options window.
        uiTook |= staticUIInput_->processContainerInput(
            ctx_.eventReceiver->getMouseX(), ctx_.eventReceiver->getMouseY(),
            pendingUIClick_, uiLayout_, containerState_);
        // T421 (board B298): the item display window is hit-tested LAST and
        // drawn LAST -- it sits on top of every centre popup.
        uiTook |= staticUIInput_->processItemDisplayInput(
            ctx_.eventReceiver->getMouseX(), ctx_.eventReceiver->getMouseY(),
            pendingUIClick_, uiLayout_, itemDisplayState_);
        // T434: the Options window is hit-tested LAST and drawn LAST -- it
        // is on top of everything, including the item display. A moved
        // slider is reported as a Setting and applied in the one place that
        // knows how (FrameComponent::applyOptionsSetting).
        // B897: the Friends window, just below Options in both orders.
        processFriendsWindowInput();
        OptionsLayout::Setting optionsChanged = OptionsLayout::Setting::None;
        uiTook |= staticUIInput_->processOptionsInput(
            ctx_.eventReceiver->getMouseX(), ctx_.eventReceiver->getMouseY(),
            pendingUIClick_, uiLayout_, optionsState_, optionsChanged);
        applyOptionsSetting(optionsChanged);
        // B442b (#151): the world drop click and the item display's lifetime.
        processCursorGestureFrame(uiTook);
        pendingUIClick_ = false;
        staticUIInput_->setFrameClickState(false, {}, &cursorState_);
    }

    // T60: the paperdoll only exists/renders while the inventory popup is
    // open — lazy construction on first open, RTT re-render gated by its
    // own markDirty()/dirty_ flag (never a per-frame re-render). Nothing
    // runs here when the popup is closed.
    if (newUIEnabled_ && inventoryState_.activePopup == PopupType::Inventory) {
        ctx_.Overlays().updatePaperdollForFrame();
    }
    inventoryState_.paperdollTexture = ctx_.Overlays().characterModelView() ? ctx_.Overlays().characterModelView()->getTexture() : nullptr;

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
        // T415 (B321): the tradeskill container window, the last centre
        // popup, drawn after the loot popup for the same reason -- the
        // server opens it and nothing else may cover it.
        renderTradeskillPopup(*uiRenderer_, uiLayout_, tradeskillState_);
        // T436 (B429): the merchant window, the last centre popup. It is one
        // of them because the oracle's MerchantWnd is Style_Sizable true and
        // 505px tall from its own <Location>, which does not fit 640x480 --
        // T415's condition for scaling a window onto centerPopup.
        renderMerchantPopup(*uiRenderer_, uiLayout_, merchantState_, cursorState_);
        // T415 (B320): the combat abilities window is a HUD panel, not a
        // centre popup, so it draws above the popups like the buff bar does.
        renderCombatAbilityPanel(*uiRenderer_, uiLayout_, combatAbilityState_);
        // T436 (B429): the bank window. NOT a centre popup -- the oracle's
        // BankWnd is Style_Sizable false at 108x347 and keeps its own size --
        // so it draws above them, mirroring the hit-test order.
        renderBankWindow(*uiRenderer_, uiLayout_, bankState_);
        // B898 (#214): the trainer window -- Style_Sizable false at 580x280,
        // NOT a centre popup, so it draws above them and above the bank.
        renderTrainerWindow(*uiRenderer_, uiLayout_, trainerState_, xpBarState_.progress);
        // B895: the Tracking Window (own placement, not a centre popup).
        renderTrackingWindow(*uiRenderer_, uiLayout_, trackingState_);
        // B899 (#215): the container windows (EQUI_Container.xml), drawn
        // after the tracking window, mirroring the hit-test order above.
        renderContainerWindows(*uiRenderer_, uiLayout_, containerState_);
        // B896 (#212): the trade / give window, at the oracle's own (0,25),
        // above the containers and every window it took the click from.
        renderTradeWindow(*uiRenderer_, uiLayout_, tradeState_);
        // T421 (B298 item half): the item display window. Drawn after every
        // popup because the oracle's ItemDisplayWindow has its own absolute
        // placement (0,80) and overlaps whatever is beneath it.
        renderItemDisplayPopup(*uiRenderer_, uiLayout_, itemDisplayState_);
        // T434: the Options window is drawn LAST of all. The oracle's
        // OptionsWindow is `RelativePosition false` at its own <Location>
        // and overlaps whatever is beneath it; the draw order mirrors the
        // hit-test order above.
        renderFriendsWindow(*uiRenderer_, uiLayout_, friendsState_);  // B897
        renderOptionsWindow(*uiRenderer_, uiLayout_, optionsState_);
        renderTipWindow(*uiRenderer_, uiLayout_, tipState_);  // B834
        renderXPBar(*uiRenderer_, uiLayout_, xpBarState_);
        // B442: the QuantityWnd, above every window (hit-tested first).
        if (staticUIInput_) {
            renderQuantityWindow(*uiRenderer_, uiLayout_, staticUIInput_->quantityWindow());
            renderConfirmationDialog(*uiRenderer_, uiLayout_, staticUIInput_->confirmationDialog());
        }
        // T436 (B429): the cursor attachment is drawn LAST of everything.
        // It is the pointer: nothing may cover it, including the Options
        // window and the XP bar.
        renderCursorAttachment(*uiRenderer_, uiLayout_, cursorState_);
        uiRenderer_->endFrame();
    }
    frameTimings_.staticUI = measureSection();
}

void FrameComponent::renderZoneLineOverlays() {
    // T354 (B042): drawZoneLineOverlay() was called here every frame and
    // deleted -- its gate member had no writer left, so it drew nothing.
    ctx_.Overlays().drawZoneLineBoxLabels();
    frameTimings_.zoneLineOverlay = measureSection();
}

void FrameComponent::renderFrameTail() {
    eqtGPUPhaseEnd(2 /* GPUSyncPhase::ParticlesUI */);

#ifdef WITH_RDP
    ctx_.Rdp().captureFrameForRDP();
#endif

    // Draw software mouse cursor for DRM/KMS mode (no hardware cursor available)
    if (owner_.softwareCursorTexture_) {
        auto* cursor = ctx_.device->getCursorControl();
        if (cursor && cursor->isVisible()) {
            auto pos = cursor->getPosition();
            irr::core::position2di destPos(pos.X, pos.Y);
            irr::core::rect<irr::s32> srcRect(0, 0, 8, 10);
            ctx_.driver->draw2DImage(owner_.softwareCursorTexture_, destPos, srcRect,
                nullptr, irr::video::SColor(255, 255, 255, 255), true);
        }
    }

    // Take deferred screenshot after all rendering, before swap
    if (pendingScreenshot_) {
        pendingScreenshot_ = false;
        owner_.saveScreenshot("screenshot.png");
    }
    frameTimings_.postRender = measureSection();
}

void FrameComponent::endSceneAndSwap() {
    LOG_TRACE(MOD_GRAPHICS, "POST-DRAW checkpoint 11: endScene");
    // P52: Swap phase. endScene() -> Device->present() -> eglSwapBuffers() /
    // drmPageFlip() (docker/irrlicht-drm/COpenGLES2Driver.cpp,
    // CIrrDeviceFB.cpp). The GPU timer query wrapped around this span is
    // expected to read near-zero (a swap submits little/no new GPU command
    // work) -- the meaningful measurement for this phase is the CPU-side
    // sync-point attribution recorded inside present()/drmPageFlip() itself
    // (eqtGetLastSwapBuffersMs()/eqtGetLastFlipWaitMs()), not the GPU query.
    eqtGPUPhaseBegin(3 /* GPUSyncPhase::Swap */);
    ctx_.driver->endScene();
    eqtGPUPhaseEnd(3 /* GPUSyncPhase::Swap */);
    // Collect any GPU query results that became available (double-buffered,
    // non-blocking) and flip the write buffer for next frame. Must be
    // called exactly once per frame, after the last eqtGPUPhaseEnd() above.
    eqtGPUPhaseCollectResults();
    frameTimings_.endScene = measureSection();
}

} // namespace Graphics
} // namespace EQT
