// renderer_frame.cpp - IrrlichtRenderer's per-frame orchestrator.
//
// Holds processFrame() alone, which sequences the four frame phases.
// Phase 1 (input), Phase 2 (visibility) and Phase 3 (simulation) live in
// renderer_frame_input.cpp; Phase 4 (render) lives in
// renderer_frame_render.cpp.
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

// L04 (refactor/L04_diagnostics_component.md): measureSection() used to sit in
// renderer_diagnostics.cpp, but it is frame-phase state, not diagnostics --
// sectionStart_ is declared under "Frame phase shared state" in
// include/client/graphics/irrlicht_renderer.h and all 35 call sites are the
// frame family's (renderer_frame.cpp, renderer_frame_input.cpp,
// renderer_frame_render.cpp). It stays on IrrlichtRenderer and moves here, to
// the family that owns it; L11 takes it with the rest of the frame family.
int64_t IrrlichtRenderer::measureSection() {
    auto now = std::chrono::steady_clock::now();
    auto us = std::chrono::duration_cast<std::chrono::microseconds>(now - sectionStart_).count();
    sectionStart_ = now;
    return us;
}

bool IrrlichtRenderer::processFrame(float deltaTime) {
    auto frameStart = std::chrono::steady_clock::now();
    frameStart_ = frameStart;
    sectionStart_ = frameStart;

    if (governor_) governor_->beginFrame();

    LOG_TRACE(MOD_GRAPHICS, "processFrame: entered");

    // Record frame time for gameplay statistics (deltaTime is in seconds)
    int64_t frameTimeMs = static_cast<int64_t>(deltaTime * 1000.0f);
    if (frameTimeMs > 0) {
        EQT::PerformanceMetrics::instance().recordSample("Frame Time", frameTimeMs);
    }

    // Log warning if previous frame was slow (deltaTime > 100ms = <10 FPS)
    if (deltaTime > 0.1f) {
        LOG_WARN(MOD_GRAPHICS, "PERF: Previous frame took {} ms (slow!)", static_cast<int>(deltaTime * 1000.0f));
    }

    if (!isRunning()) {
        LOG_INFO(MOD_GRAPHICS, "processFrame: isRunning() returned false");
        LOG_INFO(MOD_GRAPHICS, "initialized_={} device_={} device_run={} quitRequested={}",
                  initialized_, (device_ ? "valid" : "null"), (device_ ? device_->run() : false),
                  (eventReceiver_ ? eventReceiver_->quitRequested() : false));
        return false;
    }

    // Update FPS
    irr::u32 currentTime = device_->getTimer()->getTime();
    frameCount_++;
    if (currentTime - lastFpsTime_ >= 1000) {
        currentFps_ = frameCount_;
        frameCount_ = 0;
        lastFpsTime_ = currentTime;
    }

    // Tiered update: increment frame counter and accumulate delta for Tier 3
    frameNumber_++;
    tier3DeltaAccum_ += deltaTime;
    runTier2_ = (frameNumber_ % kTier2Interval == 0);
    runTier3_ = (frameNumber_ % kTier3Interval == 0);

    // Safety: force Tier 2 at least once per second (~30 frames at 30fps)
    if (frameNumber_ % 30 == 0) runTier2_ = true;

    // Phase 1: Input
    processFrameInput(deltaTime);

    // ─── Always: drain queues (cheap data moves, no GL) ───
    sectionStart_ = std::chrono::steady_clock::now();
#ifdef EQT_HAS_GLES2
    if (progressiveLoadingActive_) {
        // Loading screen: batch process freely (§4.2: no governor restriction)
        zoneLifecycle_->processCompletedUploads();
    } else {
        zoneLifecycle_->drainGPUResults();  // move to pendingGPUResults_ (no GL)
    }
#endif

    // T04: processUploadQueue/drainDecodedQueue removed. Textures uploaded via uploadTexture().
    frameTimings_.drainQueues = measureSection();

    // SimulationWorker: apply results from previous frame, then post new input
    simBridge_->applySimulationResults();
    frameTimings_.simWorkerApply = measureSection();

    // Phase 2: Visibility
    sectionStart_ = std::chrono::steady_clock::now();
    processFrameVisibility();
    frameTimings_.visibility = measureSection();

    // SimulationWorker: post new input after frustum planes are updated
    simBridge_->postSimulationInput(deltaTime);
    frameTimings_.postSimInput = measureSection();

    // ─── Progressive loading (loading screen showing, no governor restriction §4.2) ───
    if (progressiveLoadingActive_) {
        zoneStream_->processFrameProgressiveLoad();
        frameTimings_.meshLoading = measureSection();
    }

    // ─── Post-loading: all work GREEN-gated, 1 unit per GREEN frame ───
    sectionStart_ = std::chrono::steady_clock::now();
    if (!progressiveLoadingActive_) {
        if (!governor_ || governor_->getState() == BudgetState::Green) {
            bool didWork = false;

            // Priority 1: Complete in-flight GPU work (VBO/texture registration)
#ifdef EQT_HAS_GLES2
            if (!didWork) didWork = zoneLifecycle_->processOneGPUResult();
#endif

            // T04: processOneUpload removed. Textures uploaded via uploadTexture().

            // Priorities 3/4 (finalize background mesh build, material swap)
            // removed T67 — this whole background-mesh-build subsystem
            // (drainMeshResults()/finalizeOneMeshResult()/
            // processOneMaterialSwap()/submitOneBgMeshBuild()) existed only
            // to feed and drain the never-constructed ConstrainedMeshCache's
            // async region loading; submitOneBgMeshBuild() (ex-Priority 10,
            // below) was its sole producer and was itself cache-gated, so
            // the whole chain was permanently a no-op. See
            // src/client/graphics/CLAUDE.md for the reference pointer.

            // Priority 5: Entity prep results
            if (!didWork && entityPrepReady_ && entityRenderer_)
                didWork = entityRenderer_->pollAndDistributePrepResults();

            // Priority 6: Entity mesh building
            if (!didWork && entityPrepReady_ && entityRenderer_ && !config_.constrainedConfig.skipEntityBuild)
                didWork = entityRenderer_->processOneEntityBuildStep(currentPvsRegion_);

            // Priority 7 (texture rebuild for regions without a buffer map)
            // removed T67 — gated on `constrainedMeshCache_ &&`, never true.
            // textureRebuildQueue_ is still populated live by the GPU
            // upload thread (renderer_gpu_upload.cpp); with every
            // mesh-cache-gated consumer of it gone, it is now drained only
            // at zone unload, exactly as before this change (this consumer
            // never ran in any shipped build either).

            // Priority 8: Door texture arrival
            if (!didWork && !doorTextureRebuildQueue_.empty() && doorManager_) {
                uint8_t doorId = doorTextureRebuildQueue_.front();
                doorTextureRebuildQueue_.erase(doorTextureRebuildQueue_.begin());
                doorManager_->rebuildSingleDoor(doorId);
                didWork = true;
                LOG_INFO(MOD_GRAPHICS, "Post-progressive: rebuilt door {} with textures (queue: {})",
                         doorId, doorTextureRebuildQueue_.size());
            }

            // Priority 9: Object texture arrival
            if (!didWork && !objectTextureRebuildQueue_.empty()) {
                size_t objIdx = objectTextureRebuildQueue_.front();
                objectTextureRebuildQueue_.erase(objectTextureRebuildQueue_.begin());
                if (objIdx < deferredObjects_.size()) {
                    size_t nodeIdx = deferredObjects_[objIdx].nodeIndex;
                    if (nodeIdx != SIZE_MAX && nodeIdx < objectNodes_.size() && objectNodes_[nodeIdx]) {
                        auto* oldNode = objectNodes_[nodeIdx];
                        if (animatedTextureManager_)
                            animatedTextureManager_->removeSceneNode(oldNode);
                        if (oldNode->getParent()) oldNode->remove(); else oldNode->drop();
                        objectNodes_[nodeIdx] = nullptr;
                        if (nodeIdx < objectInSceneGraph_.size())
                            objectInSceneGraph_[nodeIdx] = false;
                    }
                    deferredObjects_[objIdx].meshBuilt = false;
                    zoneStream_->buildDeferredObject(objIdx);
                    didWork = true;
                    LOG_INFO(MOD_GRAPHICS, "Post-progressive: rebuilt object {} with textures (queue: {})",
                             objIdx, objectTextureRebuildQueue_.size());
                }
            }

            // Priority 10 (submit new background mesh build) removed T67 —
            // see the Priority 3/4 removal note above.

            // Priority 11: Lazy icon loading (disabled — WindowManager removed)

            if (didWork) {
                frameTimings_.meshLoading = measureSection();
            }
        }

        // Lightweight entity housekeeping (always runs, not didWork-gated):
        if (entityPrepReady_ && entityPrepWorker_ && entityRenderer_) {
            entityRenderer_->getRaceModelLoader()->promotePreparedModels();
            if (++entityPrepScanCounter_ % 10 == 0) zoneStream_->queueEntityPrepRequests();
            if (entityPrepWorker_->isIdle()) entityPrepWorker_->dispatchOne();
        }
        frameTimings_.entityHousekeeping = measureSection();
    }
    frameTimings_.postLoadWork = measureSection();

    // Phase 3: Simulation
    processFrameSimulation(deltaTime);

    // Phase 4: Render
    if (!processFrameRender(deltaTime)) {
        // Loading screen was shown - skip rest of render
        return true;
    }

    // ===== FRAME TIMING: Total Frame (always-on) =====
    {
        auto frameEnd = std::chrono::steady_clock::now();
        frameTimings_.totalFrame = std::chrono::duration_cast<std::chrono::microseconds>(
            frameEnd - frameStart).count();

        // Update render cost EMA (alpha = 0.1 for smooth tracking)
        int64_t renderUs = frameTimings_.sceneDrawAll + frameTimings_.targetBox +
                           frameTimings_.particles + frameTimings_.boids +
                           frameTimings_.weatherRender + frameTimings_.debugOverlays +
                           frameTimings_.castingBars + frameTimings_.guiDrawAll +
                           frameTimings_.staticUI + frameTimings_.zoneLineOverlay +
                           frameTimings_.endScene + frameTimings_.footprintRender +
                           frameTimings_.postRender;
        renderCostAvgUs_ = renderCostAvgUs_ * 0.9f + static_cast<float>(renderUs) * 0.1f;

        // Update essential simulation cost EMA
        int64_t essSimUs = frameTimings_.entityUpdate + frameTimings_.doorUpdate +
                           frameTimings_.spellVfxUpdate + frameTimings_.animatedTextures +
                           frameTimings_.vertexAnimations + frameTimings_.hudUpdate;
        essentialSimCostAvgUs_ = essentialSimCostAvgUs_ * 0.9f + static_cast<float>(essSimUs) * 0.1f;

        // Record frame in governor and log budget violations.
        // endScene is passed for diagnostics but NOT subtracted — full wall-clock
        // frame time is recorded. On shared-bus ARM SoCs, subtracting endScene
        // blinds the governor to GPU pipeline stalls from progressive loading.
        if (governor_) {
            bool hasPendingWork = progressiveLoadingActive_;
            // The meshLoadQueue_-derived pending-work check (removed T67)
            // was gated on constrainedMeshCache_, never true (the cache was
            // never constructed), so hasPendingWork here was always exactly
            // progressiveLoadingActive_ in every shipped build.
            governor_->endFrame(static_cast<float>(frameTimings_.endScene), hasPendingWork);
        }

        float totalFrameMs = frameTimings_.totalFrame / 1000.0f;
        float budgetMs = governor_ ? governor_->getTargetFrameTimeMs() : 33.3f;

        // ===== SPIKE DETECTOR: unconditional, fires on any frame >50ms =====
        // Logs every section >1ms sorted by cost. Independent of governor state.
        if (totalFrameMs > 50.0f) {
            struct SectionCost { const char* name; int64_t us; };
            SectionCost spikeSections[] = {
                {"inputHandling",      frameTimings_.inputHandling},
                {"playerMovement",     frameTimings_.playerMovement},
                {"cameraUpdate",       frameTimings_.cameraUpdate},
                {"drainQueues",        frameTimings_.drainQueues},
                {"simWorkerApply",     frameTimings_.simWorkerApply},
                {"visibility",         frameTimings_.visibility},
                {"postSimInput",       frameTimings_.postSimInput},
                {"meshLoading",        frameTimings_.meshLoading},
                {"postLoadWork",       frameTimings_.postLoadWork},
                {"entityHousekeeping", frameTimings_.entityHousekeeping},
                {"entityUpdate",       frameTimings_.entityUpdate},
                {"doorUpdate",         frameTimings_.doorUpdate},
                {"spellVfxUpdate",     frameTimings_.spellVfxUpdate},
                {"animatedTextures",   frameTimings_.animatedTextures},
                {"vertexAnimations",   frameTimings_.vertexAnimations},
                {"tier2Update",        frameTimings_.tier2Update},
                {"tier3Update",        frameTimings_.tier3Update},
                {"hudUpdate",          frameTimings_.hudUpdate},
                {"sceneDrawAll",       frameTimings_.sceneDrawAll},
                {"footprintRender",    frameTimings_.footprintRender},
                {"targetBox",          frameTimings_.targetBox},
                {"particles",          frameTimings_.particles},
                {"boids",              frameTimings_.boids},
                {"weatherRender",      frameTimings_.weatherRender},
                {"debugOverlays",      frameTimings_.debugOverlays},
                {"castingBars",        frameTimings_.castingBars},
                {"guiDrawAll",         frameTimings_.guiDrawAll},
                {"staticUI",           frameTimings_.staticUI},
                {"zoneLineOverlay",    frameTimings_.zoneLineOverlay},
                {"endScene",           frameTimings_.endScene},
                {"postRender",         frameTimings_.postRender},
            };
            std::sort(std::begin(spikeSections), std::end(spikeSections),
                      [](const auto& a, const auto& b) { return a.us > b.us; });

            // Sum all measured sections to find unaccounted time
            int64_t accountedUs = 0;
            for (const auto& s : spikeSections) accountedUs += s.us;
            int64_t unaccountedUs = frameTimings_.totalFrame - accountedUs;

            LOG_WARN(MOD_GRAPHICS, "SPIKE: {:.1f}ms frame (unaccounted: {:.1f}ms):",
                     totalFrameMs, unaccountedUs / 1000.0f);
            for (const auto& s : spikeSections) {
                if (s.us < 1000) break;  // Skip <1ms
                LOG_WARN(MOD_GRAPHICS, "  SPIKE: {:>20s} = {:>7.1f}ms",
                         s.name, s.us / 1000.0f);
            }
        }

        // RED STATE: Full diagnostic dump — every section, every frame, so we can
        // trace exactly which subsystem is the budget hog and eliminate it.
        if (governor_ &&
            governor_->getState() == BudgetState::Red) {
            struct SectionCost { const char* name; int64_t us; };
            SectionCost sections[] = {
                {"inputHandling",    frameTimings_.inputHandling},
                {"playerMovement",   frameTimings_.playerMovement},
                {"cameraUpdate",     frameTimings_.cameraUpdate},
                {"pvsVisibility",    frameTimings_.pvsVisibility},
                {"occlusionCulling", frameTimings_.occlusionCulling},
                {"zoneLightVis",     frameTimings_.zoneLightVisibility},
                {"objectVisibility", frameTimings_.objectVisibility},
                {"meshLoading",      frameTimings_.meshLoading},
                {"objectLights",     frameTimings_.objectLights},
                {"entityUpdate",     frameTimings_.entityUpdate},
                {"doorUpdate",       frameTimings_.doorUpdate},
                {"spellVfxUpdate",   frameTimings_.spellVfxUpdate},
                {"animatedTextures", frameTimings_.animatedTextures},
                {"vertexAnimations", frameTimings_.vertexAnimations},
                {"fireFlicker",      frameTimings_.fireFlicker},
                {"tier2Update",      frameTimings_.tier2Update},
                {"tier3Update",      frameTimings_.tier3Update},
                {"hudUpdate",        frameTimings_.hudUpdate},
                {"sceneDrawAll",     frameTimings_.sceneDrawAll},
                {"sceneAnimate",     frameTimings_.sceneAnimate},
                {"sceneSolid",       frameTimings_.sceneSolid},
                {"sceneTransparent", frameTimings_.sceneTransparent},
                {"manualZoneDraw",   frameTimings_.manualZoneDraw},
                {"footprintRender",  frameTimings_.footprintRender},
                {"targetBox",        frameTimings_.targetBox},
                {"particles",        frameTimings_.particles},
                {"weatherRender",    frameTimings_.weatherRender},
                {"castingBars",      frameTimings_.castingBars},
                {"guiDrawAll",       frameTimings_.guiDrawAll},
                {"staticUI",         frameTimings_.staticUI},
                {"glFinish",         frameTimings_.glFinish},
                {"endScene",         frameTimings_.endScene},
                {"postRender",       frameTimings_.postRender},
                {"simWorkerApply",   frameTimings_.simWorkerApply},
            };
            std::sort(std::begin(sections), std::end(sections),
                      [](const auto& a, const auto& b) { return a.us > b.us; });

            // Throttle RED warnings to 1 per second
            static auto lastRedWarnTime = std::chrono::steady_clock::now();
            static int redFramesSinceWarn = 0;
            redFramesSinceWarn++;
            auto redNow = std::chrono::steady_clock::now();
            bool emitRedWarn = std::chrono::duration_cast<std::chrono::milliseconds>(redNow - lastRedWarnTime).count() >= 1000;
            if (emitRedWarn) {
                lastRedWarnTime = redNow;
            }

            // Log header with totals + scene composition
            if (emitRedWarn) {
                LOG_WARN(MOD_GRAPHICS, "RED STATE: {:.1f}ms / {:.1f}ms budget ({:.0f}% over) — avg {:.1f}ms, stall {}/{} — {} red frames in last 1s",
                         totalFrameMs, budgetMs,
                         (totalFrameMs / budgetMs - 1.0f) * 100.0f,
                         governor_->getAverageFrameTimeMs(),
                         governor_->getStallCounter(),
                         FrameBudgetGovernor::kStallThreshold,
                         redFramesSinceWarn);
                LOG_WARN(MOD_GRAPHICS, "  RED SCENE: {} sceneNodes, {} polys | manualDraw: {} regions, {} drawCalls | sortedList: {}",
                         frameTimings_.sceneNodeCount, lastPolygonCount_,
                         frameTimings_.manualZoneRegionsDrawn, frameTimings_.manualZoneDrawCalls,
                         sortedZoneDrawList_.size());
                {
                    int totalNodes = 0, visibleNodes = 0;
                    std::function<void(irr::scene::ISceneNode*)> countNodes =
                        [&](irr::scene::ISceneNode* node) {
                            if (!node) return;
                            totalNodes++;
                            if (node->isVisible()) visibleNodes++;
                            for (auto* child : node->getChildren())
                                countNodes(child);
                        };
                    if (smgr_ && smgr_->getRootSceneNode())
                        countNodes(smgr_->getRootSceneNode());
                    LOG_WARN(MOD_GRAPHICS, "  RED GRAPH: {} total scene nodes, {} visible (drawAll traverses all)",
                             totalNodes, visibleNodes);
                }
                for (const auto& s : sections) {
                    if (s.us < 100) continue;
                    LOG_WARN(MOD_GRAPHICS, "  RED: {:>20s} = {:>7.1f}ms ({:>5.1f}%)",
                             s.name, s.us / 1000.0f,
                             totalFrameMs > 0 ? (s.us / 1000.0f / totalFrameMs * 100.0f) : 0.0f);
                }
                redFramesSinceWarn = 0;
            }
        }
        // Yellow/Green budget violation: condensed top-3 warning, throttled to 1/sec
        else if (totalFrameMs > budgetMs * 1.2f) {
            static auto lastBudgetWarnTime = std::chrono::steady_clock::now();
            static int budgetFramesSinceWarn = 0;
            budgetFramesSinceWarn++;
            auto budgetNow = std::chrono::steady_clock::now();
            if (std::chrono::duration_cast<std::chrono::milliseconds>(budgetNow - lastBudgetWarnTime).count() >= 1000) {
                struct SectionCost { const char* name; int64_t us; };
                SectionCost sections[] = {
                    {"playerMovement", frameTimings_.playerMovement},
                    {"pvsVisibility", frameTimings_.pvsVisibility},
                    {"occlusionCulling", frameTimings_.occlusionCulling},
                    {"meshLoading", frameTimings_.meshLoading},
                    {"entityUpdate", frameTimings_.entityUpdate},
                    {"tier2Update", frameTimings_.tier2Update},
                    {"tier3Update", frameTimings_.tier3Update},
                    {"sceneDrawAll", frameTimings_.sceneDrawAll},
                    {"staticUI", frameTimings_.staticUI},
                    {"endScene", frameTimings_.endScene},
                    {"particles", frameTimings_.particles},
                    {"simWorkerApply", frameTimings_.simWorkerApply},
                    {"targetBox", frameTimings_.targetBox},
                    {"guiDrawAll", frameTimings_.guiDrawAll},
                    {"footprintRender", frameTimings_.footprintRender},
                };
                std::sort(std::begin(sections), std::end(sections),
                          [](const auto& a, const auto& b) { return a.us > b.us; });

                LOG_WARN(MOD_GRAPHICS, "BUDGET: {:.1f}ms (budget {:.1f}ms, gov={}) top: {}={:.1f}ms {}={:.1f}ms {}={:.1f}ms — {} frames in last 1s",
                         totalFrameMs, budgetMs,
                         governor_ ? governor_->getStateName() : "N/A",
                         sections[0].name, sections[0].us / 1000.0f,
                         sections[1].name, sections[1].us / 1000.0f,
                         sections[2].name, sections[2].us / 1000.0f,
                         budgetFramesSinceWarn);
                lastBudgetWarnTime = budgetNow;
                budgetFramesSinceWarn = 0;
            }
        }
    }

    // Accumulate and periodically log (gated behind /frametiming command)
    if (frameTimingEnabled_) {
        frameTimingsAccum_.inputHandling += frameTimings_.inputHandling;
        frameTimingsAccum_.cameraUpdate += frameTimings_.cameraUpdate;
        frameTimingsAccum_.entityUpdate += frameTimings_.entityUpdate;
        frameTimingsAccum_.doorUpdate += frameTimings_.doorUpdate;
        frameTimingsAccum_.spellVfxUpdate += frameTimings_.spellVfxUpdate;
        frameTimingsAccum_.animatedTextures += frameTimings_.animatedTextures;
        frameTimingsAccum_.vertexAnimations += frameTimings_.vertexAnimations;
        frameTimingsAccum_.fireFlicker += frameTimings_.fireFlicker;
        frameTimingsAccum_.objectVisibility += frameTimings_.objectVisibility;
        frameTimingsAccum_.pvsVisibility += frameTimings_.pvsVisibility;
        frameTimingsAccum_.meshLoading += frameTimings_.meshLoading;
        frameTimingsAccum_.objectLights += frameTimings_.objectLights;
        frameTimingsAccum_.tier2Update += frameTimings_.tier2Update;
        frameTimingsAccum_.tier3Update += frameTimings_.tier3Update;
        frameTimingsAccum_.hudUpdate += frameTimings_.hudUpdate;
        frameTimingsAccum_.sceneDrawAll += frameTimings_.sceneDrawAll;
        frameTimingsAccum_.sceneAnimate += frameTimings_.sceneAnimate;
        frameTimingsAccum_.sceneSolid += frameTimings_.sceneSolid;
        frameTimingsAccum_.sceneTransparent += frameTimings_.sceneTransparent;
        frameTimingsAccum_.sceneSkybox += frameTimings_.sceneSkybox;
        frameTimingsAccum_.sceneOther += frameTimings_.sceneOther;
        frameTimingsAccum_.sceneNodeCount += frameTimings_.sceneNodeCount;
        frameTimingsAccum_.manualZoneDraw += frameTimings_.manualZoneDraw;
        frameTimingsAccum_.targetBox += frameTimings_.targetBox;
        frameTimingsAccum_.particles += frameTimings_.particles;
        frameTimingsAccum_.boids += frameTimings_.boids;
        frameTimingsAccum_.weatherRender += frameTimings_.weatherRender;
        frameTimingsAccum_.debugOverlays += frameTimings_.debugOverlays;
        frameTimingsAccum_.castingBars += frameTimings_.castingBars;
        frameTimingsAccum_.guiDrawAll += frameTimings_.guiDrawAll;
        frameTimingsAccum_.staticUI += frameTimings_.staticUI;
        frameTimingsAccum_.zoneLineOverlay += frameTimings_.zoneLineOverlay;
        frameTimingsAccum_.glFinish += frameTimings_.glFinish;
        frameTimingsAccum_.endScene += frameTimings_.endScene;
        frameTimingsAccum_.manualZoneRegionsDrawn += frameTimings_.manualZoneRegionsDrawn;
        frameTimingsAccum_.manualZoneDrawCalls += frameTimings_.manualZoneDrawCalls;
        frameTimingsAccum_.totalFrame += frameTimings_.totalFrame;
        // New fine-grained fields
        frameTimingsAccum_.playerMovement += frameTimings_.playerMovement;
        frameTimingsAccum_.occlusionCulling += frameTimings_.occlusionCulling;
        frameTimingsAccum_.zoneLightVisibility += frameTimings_.zoneLightVisibility;
        frameTimingsAccum_.weatherSystemUpdate += frameTimings_.weatherSystemUpdate;
        frameTimingsAccum_.footprintRender += frameTimings_.footprintRender;
        frameTimingsAccum_.postRender += frameTimings_.postRender;
        frameTimingsAccum_.simWorkerApply += frameTimings_.simWorkerApply;
        frameTimingsAccum_.drainQueues += frameTimings_.drainQueues;
        frameTimingsAccum_.visibility += frameTimings_.visibility;
        frameTimingsAccum_.postSimInput += frameTimings_.postSimInput;
        frameTimingsAccum_.postLoadWork += frameTimings_.postLoadWork;
        frameTimingsAccum_.entityHousekeeping += frameTimings_.entityHousekeeping;
        frameTimingsSampleCount_++;

        // Log every 60 frames (~2 seconds at 30fps)
        if (frameTimingsSampleCount_ >= 60) {
            diagnostics_->logFrameTimings();
            // Reset accumulators
            frameTimingsAccum_ = FrameTimings();
            frameTimingsSampleCount_ = 0;
        }
    }

    return true;
}

} // namespace Graphics
} // namespace EQT
