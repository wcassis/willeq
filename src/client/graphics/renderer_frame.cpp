#include "client/graphics/irrlicht_renderer.h"
#include "client/graphics/entity_prep_worker.h"
#include "client/graphics/constrained_mesh_cache.h"
#include "client/graphics/sky_renderer.h"
#include "client/graphics/weather_effects_controller.h"
#include "client/graphics/spell_visual_fx.h"
#include "client/graphics/ui/static_ui_input.h"
#include "client/graphics/ui/ui_renderer.h"
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
        processCompletedUploads();
    } else {
        drainGPUResults();  // move to pendingGPUResults_ (no GL)
    }
#endif

    // T04: processUploadQueue/drainDecodedQueue removed. Textures uploaded via uploadTexture().
    frameTimings_.drainQueues = measureSection();

    // SimulationWorker: apply results from previous frame, then post new input
    applySimulationResults();
    frameTimings_.simWorkerApply = measureSection();

    // Phase 2: Visibility
    sectionStart_ = std::chrono::steady_clock::now();
    processFrameVisibility();
    frameTimings_.visibility = measureSection();

    // SimulationWorker: post new input after frustum planes are updated
    postSimulationInput(deltaTime);
    frameTimings_.postSimInput = measureSection();

    // ─── Progressive loading (loading screen showing, no governor restriction §4.2) ───
    if (progressiveLoadingActive_) {
        processFrameProgressiveLoad();
        frameTimings_.meshLoading = measureSection();
    }

    // ─── Post-loading: all work GREEN-gated, 1 unit per GREEN frame ───
    sectionStart_ = std::chrono::steady_clock::now();
    if (!progressiveLoadingActive_) {
        // Drain bg mesh results (cheap mutex swap, no GL)
        drainMeshResults();

        if (!governor_ || governor_->getState() == BudgetState::Green) {
            bool didWork = false;

            // Priority 1: Complete in-flight GPU work (VBO/texture registration)
#ifdef EQT_HAS_GLES2
            if (!didWork) didWork = processOneGPUResult();
#endif

            // T04: processOneUpload removed. Textures uploaded via uploadTexture().

            // Priority 3: Finalize background mesh build (add to scene graph)
            if (!didWork) didWork = finalizeOneMeshResult();

            // Priority 4: Material swap (placeholder → real texture)
            if (!didWork) didWork = processOneMaterialSwap();

            // Priority 5: Entity prep results
            if (!didWork && entityPrepReady_ && entityRenderer_)
                didWork = entityRenderer_->pollAndDistributePrepResults();

            // Priority 6: Entity mesh building
            if (!didWork && entityPrepReady_ && entityRenderer_ && !config_.constrainedConfig.skipEntityBuild)
                didWork = entityRenderer_->processOneEntityBuildStep(currentPvsRegion_);

            // Priority 7: Texture rebuild (full rebuild for regions without buffer map)
            if (!didWork && constrainedMeshCache_ && !textureRebuildQueue_.empty()) {
                auto texEntry = textureRebuildQueue_.front();
                textureRebuildQueue_.erase(textureRebuildQueue_.begin());

                auto existingIt = regionMeshNodes_.find(texEntry.regionIdx);
                if (existingIt != regionMeshNodes_.end() && existingIt->second) {
                    deleteMeshHardwareBuffers(existingIt->second);
                    if (animatedTextureManager_)
                        animatedTextureManager_->removeSceneNode(existingIt->second);
                    if (existingIt->second->getParent()) existingIt->second->remove();
                    else existingIt->second->drop();
                    existingIt->second = nullptr;
                }

                constrainedMeshCache_->markForRebuild(texEntry.regionIdx);
                if (rebuildRegionMesh(texEntry.regionIdx)) {
                    addRegionToCollision(texEntry.regionIdx);
                    LOG_INFO(MOD_GRAPHICS, "Post-progressive: rebuilt region {} with textures (queue remaining: {})",
                             texEntry.regionIdx, textureRebuildQueue_.size());
                }
                didWork = true;
            }

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
                    buildDeferredObject(objIdx);
                    didWork = true;
                    LOG_INFO(MOD_GRAPHICS, "Post-progressive: rebuilt object {} with textures (queue: {})",
                             objIdx, objectTextureRebuildQueue_.size());
                }
            }

            // Priority 10: Submit new background mesh build
            if (!didWork && constrainedMeshCache_) didWork = submitOneBgMeshBuild();

            // Priority 11: Lazy icon loading (disabled — WindowManager removed)

            if (didWork) {
                frameTimings_.meshLoading = measureSection();
            }
        }

        // Lightweight entity housekeeping (always runs, not didWork-gated):
        if (entityPrepReady_ && entityPrepWorker_ && entityRenderer_) {
            entityRenderer_->getRaceModelLoader()->promotePreparedModels();
            if (++entityPrepScanCounter_ % 10 == 0) queueEntityPrepRequests();
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
            // meshLoadQueue_ is repopulated every frame by SimulationWorker
            // (it's a "regions that should be loaded" list, not a drain queue).
            // Only count as pending work if entries actually need building.
            if (!hasPendingWork && constrainedMeshCache_) {
                for (const auto& entry : meshLoadQueue_) {
                    if (!constrainedMeshCache_->isLoaded(entry.regionIdx)) {
                        hasPendingWork = true;
                        break;
                    }
                }
            }
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
            logFrameTimings();
            // Reset accumulators
            frameTimingsAccum_ = FrameTimings();
            frameTimingsSampleCount_ = 0;
        }
    }

    return true;
}

// ===== Phase 1: Input =====
void IrrlichtRenderer::processFrameInput(float deltaTime) {
    // chatInputFocused_ is managed by processChatInput() — don't reset it here

    // Drain action queue and dispatch
    auto actions = eventReceiver_->drainActions();
    if (!actions.empty()) {
        LOG_DEBUG(MOD_INPUT, "[INPUT-TRACE] processFrameInput: chatFocused={}, drained {} actions from actionQueue, bridgeQueue size={}",
            chatInputFocused_, actions.size(), eventReceiver_->getBridgeQueueSize());
    }
    processCommonInput(actions);
    processPlayerInput(actions);

    // ===== FRAME TIMING: Input Handling =====
    frameTimings_.inputHandling = measureSection();

    processInputDeltas(deltaTime);

    // ===== FRAME TIMING: Input Handling (accumulated) =====
    frameTimings_.inputHandling += measureSection();

    // Handle mouse state BEFORE camera/movement updates
    bool hadClick = eventReceiver_->wasLeftButtonClicked();
    bool hadRelease = eventReceiver_->wasLeftButtonReleased();
    int clickX = eventReceiver_->getClickMouseX();
    int clickY = eventReceiver_->getClickMouseY();
    pendingUIClick_ = hadClick;  // Preserved for UI input processing in render phase

    // Update chat panel mouse hover state
    {
        int mx = eventReceiver_->getMouseX();
        int my = eventReceiver_->getMouseY();
        chatPanelState_.mouseHover = uiLayout_.chatPanel.isPointInside(
            irr::core::position2d<irr::s32>(mx, my));
    }

    // Update camera and player movement
    updatePlayerMovement(deltaTime);
    frameTimings_.playerMovement = measureSection();
    // Update sky position to follow camera
    if (skyRenderer_ && skyRenderer_->isInitialized() && camera_) {
        skyRenderer_->setCameraPosition(camera_->getPosition());
    }

    // ===== FRAME TIMING: Camera Update =====
    frameTimings_.cameraUpdate = measureSection();

    processChatInput();

    // Handle mouse targeting
    if (hadClick) {
        handleMouseTargeting(clickX, clickY);
    }
}

void IrrlichtRenderer::processCommonInput(const std::vector<RendererEvent>& actions) {
    using RA = RendererAction;
    for (const auto& event : actions) {
        switch (event.action) {
            case RA::Screenshot: pendingScreenshot_ = true; break;
            case RA::ToggleAllUI: toggleAllUI(); break;
            case RA::ToggleZoneLights: toggleZoneLights(); break;
            case RA::CycleObjectLights: cycleObjectLights(); break;
            case RA::ToggleOptions:
                // WindowManager removed — options window not available
                break;
            case RA::ClearTarget:
                if (currentTargetId_ != 0) {
                    LOG_INFO(MOD_GRAPHICS, "[TARGET] Cleared target: {}", currentTargetName_);
                    clearCurrentTarget();
                    SetTrackedTargetId(0);
                }
                break;
            default: break;
        }
    }
}

void IrrlichtRenderer::processPlayerInput(const std::vector<RendererEvent>& actions) {
    using RA = RendererAction;
    for (const auto& event : actions) {
        // Note: Targeting, combat (autorun, autoattack, hail, consider), and clear target
        // actions are now routed through bridgeQueue_ → GraphicsInputHandler → InputActionBridge.
        // They no longer appear in actionQueue_, so only renderer-local actions remain here.
        if (chatInputFocused_) {
            // No renderer-local actions work when chat is focused
            continue;
        }
        switch (event.action) {
            case RA::ToggleVendor:
                if (vendorToggleCallback_) vendorToggleCallback_();
                if (bridge_) bridge_->pushIntent(eqt::events::VendorToggleIntent{});
                break;
            case RA::ToggleTrainer:
                if (trainerToggleCallback_) trainerToggleCallback_();
                if (bridge_) bridge_->pushIntent(eqt::events::TrainerToggleIntent{});
                break;
            case RA::DoorInteract:
                if (doorManager_) {
                    uint8_t doorId = doorManager_->getNearestDoor(playerX_, playerY_, playerZ_, playerHeading_);
                    if (doorId != 0) {
                        LOG_INFO(MOD_GRAPHICS, "Door interaction: ID {}", doorId);
                        if (doorInteractCallback_) doorInteractCallback_(doorId);
                        if (bridge_) bridge_->pushIntent(eqt::events::DoorInteractIntent{doorId});
                    }
                }
                break;
            case RA::WorldObjectInteract: {
                    uint32_t objectId = getNearestWorldObject(playerX_, playerY_, playerZ_);
                    if (objectId != 0) {
                        LOG_INFO(MOD_GRAPHICS, "World object interaction: dropId {}", objectId);
                        if (worldObjectInteractCallback_) worldObjectInteractCallback_(objectId);
                        if (bridge_) bridge_->pushIntent(eqt::events::WorldObjectInteractIntent{objectId});
                    }
                }
                break;

            // Collision
            case RA::ToggleCollision:
                playerConfig_.collisionEnabled = !playerConfig_.collisionEnabled;
                LOG_INFO(MOD_GRAPHICS, "Collision: {}", (playerConfig_.collisionEnabled ? "ENABLED" : "DISABLED"));
                break;
            case RA::ToggleCollisionDebug:
                playerConfig_.collisionDebug = !playerConfig_.collisionDebug;
                LOG_INFO(MOD_GRAPHICS, "Collision Debug: {}", (playerConfig_.collisionDebug ? "ON" : "OFF"));
                if (playerConfig_.collisionDebug) {
                    LOG_INFO(MOD_GRAPHICS, "  Collision Height: {}", playerConfig_.collisionCheckHeight);
                    LOG_INFO(MOD_GRAPHICS, "  Step Height: {}", playerConfig_.collisionStepHeight);
                }
                break;

            // UI Windows
            case RA::ToggleInventory: toggleInventory(); break;
            case RA::ToggleGroup:
                break;  // Group panel visibility is driven by inGroup state
            case RA::ToggleSkills:
                skillsPopupState_.isOpen = !skillsPopupState_.isOpen;
                LOG_INFO(MOD_GRAPHICS, "Skills window {}", skillsPopupState_.isOpen ? "opened" : "closed");
                break;
            case RA::TogglePet:
                break;  // Pet panel visibility is driven by hasPet state
            case RA::ToggleSpellbook:
                spellbookState_.isOpen = !spellbookState_.isOpen;
                LOG_INFO(MOD_GRAPHICS, "Spellbook {}", spellbookState_.isOpen ? "opened" : "closed");
                break;
            case RA::ToggleBuffWindow:
                break;  // Buff bar always visible when buffs exist

            // Debug overlays
            case RA::ToggleZoneLineVisualization: toggleZoneLineVisualization(); break;
            case RA::ToggleMapOverlay: toggleMapOverlay(); break;
            case RA::RotateMapOverlay:
                mapOverlayRotation_ = (mapOverlayRotation_ + 1) % 4;
                LOG_INFO(MOD_GRAPHICS, "Map overlay placeable rotation: {}° around Y axis (terrain unchanged)", mapOverlayRotation_ * 90);
                break;
            case RA::MirrorXMapOverlay:
                mapOverlayMirrorX_ = !mapOverlayMirrorX_;
                LOG_INFO(MOD_GRAPHICS, "Map overlay placeable X mirror: {} (terrain unchanged)", mapOverlayMirrorX_ ? "ON" : "OFF");
                break;
            case RA::ToggleNavmeshOverlay: toggleNavmeshOverlay(); break;
            case RA::RotateNavmeshOverlay:
                navmeshOverlayRotation_ = (navmeshOverlayRotation_ + 1) % 4;
                LOG_INFO(MOD_GRAPHICS, "Navmesh overlay rotation: {}° around Y axis", navmeshOverlayRotation_ * 90);
                break;
            case RA::MirrorXNavmeshOverlay:
                navmeshOverlayMirrorX_ = !navmeshOverlayMirrorX_;
                LOG_INFO(MOD_GRAPHICS, "Navmesh overlay X mirror: {}", navmeshOverlayMirrorX_ ? "ON" : "OFF");
                break;

            case RA::ToggleFrustumCulling:
                if (frustumCuller_) {
                    // Cycle: ON → ON+DEBUG → OFF → ON ...
                    if (frustumCuller_->isEnabled() && !frustumDebugDraw_) {
                        frustumDebugDraw_ = true;
                        LOG_INFO(MOD_GRAPHICS, "Frustum culling: ON + DEBUG DRAW (Ctrl+V to cycle)");
                    } else if (frustumCuller_->isEnabled() && frustumDebugDraw_) {
                        frustumCuller_->setEnabled(false);
                        frustumDebugDraw_ = false;
                        LOG_INFO(MOD_GRAPHICS, "Frustum culling: OFF (Ctrl+V to cycle)");
                    } else {
                        frustumCuller_->setEnabled(true);
                        frustumDebugDraw_ = false;
                        LOG_INFO(MOD_GRAPHICS, "Frustum culling: ON (Ctrl+V to cycle)");
                    }

                    // Diagnostic: log camera direction and nearby region bboxes
                    if (camera_ && cameraController_) {
                        irr::core::vector3df irrFwd = (camera_->getTarget() - camera_->getPosition());
                        float camX, camY, camZ;
                        cameraController_->getPositionEQ(camX, camY, camZ);
                        float eqFwdX = irrFwd.X, eqFwdY = irrFwd.Z, eqFwdZ = irrFwd.Y;
                        float fwdLen = std::sqrt(eqFwdX*eqFwdX + eqFwdY*eqFwdY + eqFwdZ*eqFwdZ);
                        if (fwdLen > 0.001f) { eqFwdX /= fwdLen; eqFwdY /= fwdLen; eqFwdZ /= fwdLen; }
                        LOG_INFO(MOD_GRAPHICS, "FRUSTUM DIAG: cam EQ=({:.1f},{:.1f},{:.1f}) fwd EQ=({:.3f},{:.3f},{:.3f})",
                            camX, camY, camZ, eqFwdX, eqFwdY, eqFwdZ);
                        LOG_INFO(MOD_GRAPHICS, "FRUSTUM DIAG: Irrlicht cam=({:.1f},{:.1f},{:.1f}) target=({:.1f},{:.1f},{:.1f})",
                            camera_->getPosition().X, camera_->getPosition().Y, camera_->getPosition().Z,
                            camera_->getTarget().X, camera_->getTarget().Y, camera_->getTarget().Z);

                        // Log 10 closest region bboxes with sizes and frustum results
                        struct RegionDiag { size_t idx; float dist; float sizeX, sizeY, sizeZ; bool frustum; };
                        std::vector<RegionDiag> diags;
                        for (auto& [regionIdx, bbox] : regionBoundingBoxes_) {
                            float cx = (bbox.MinEdge.X + bbox.MaxEdge.X) * 0.5f;
                            float cy = (bbox.MinEdge.Y + bbox.MaxEdge.Y) * 0.5f;
                            float cz = (bbox.MinEdge.Z + bbox.MaxEdge.Z) * 0.5f;
                            float dx = camX - cx, dy = camY - cy, dz = camZ - cz;
                            float dist = std::sqrt(dx*dx + dy*dy + dz*dz);
                            bool inFrustum = !frustumCuller_->isEnabled() || frustumCuller_->testAABB(
                                bbox.MinEdge.X, bbox.MinEdge.Y, bbox.MinEdge.Z,
                                bbox.MaxEdge.X, bbox.MaxEdge.Y, bbox.MaxEdge.Z);
                            diags.push_back({regionIdx, dist,
                                bbox.MaxEdge.X - bbox.MinEdge.X,
                                bbox.MaxEdge.Y - bbox.MinEdge.Y,
                                bbox.MaxEdge.Z - bbox.MinEdge.Z, inFrustum});
                        }
                        std::sort(diags.begin(), diags.end(), [](const RegionDiag& a, const RegionDiag& b) { return a.dist < b.dist; });
                        for (int i = 0; i < 15 && i < (int)diags.size(); ++i) {
                            auto& d = diags[i];
                            auto& bbox = regionBoundingBoxes_[d.idx];
                            LOG_INFO(MOD_GRAPHICS, "FRUSTUM DIAG: region {} dist={:.0f} size=({:.1f},{:.1f},{:.1f}) "
                                "bbox=({:.1f},{:.1f},{:.1f})->({:.1f},{:.1f},{:.1f}) {}",
                                d.idx, d.dist, d.sizeX, d.sizeY, d.sizeZ,
                                bbox.MinEdge.X, bbox.MinEdge.Y, bbox.MinEdge.Z,
                                bbox.MaxEdge.X, bbox.MaxEdge.Y, bbox.MaxEdge.Z,
                                d.frustum ? "VISIBLE" : "CULLED");
                        }
                    }

                    // Log occlusion culler stats if available
                    if (occlusionCuller_ && occlusionCuller_->isEnabled()) {
                        const auto& occStats = occlusionCuller_->getStats();
                        int fillPct = occStats.depthBufferTotalPixels > 0
                            ? (occStats.depthBufferFilledPixels * 100 / occStats.depthBufferTotalPixels) : 0;
                        LOG_INFO(MOD_GRAPHICS, "OCCLUSION: {} rasterized ({} tris, {} clipped), buf {}/{} px ({}%), "
                            "{} tested, {} culled [reject: {} behind, {} offscreen, {} too-large, {} low-coverage]",
                            occStats.regionsRasterized, occStats.trianglesRasterized, occStats.trianglesClipped,
                            occStats.depthBufferFilledPixels, occStats.depthBufferTotalPixels, fillPct,
                            occStats.regionsTested, occStats.regionsCulled,
                            occStats.rejectedBehindCamera, occStats.rejectedOffScreen,
                            occStats.rejectedScreenTooLarge, occStats.rejectedLowCoverage);
                        occlusionCuller_->dumpDepthBufferPGM("occlusion_depth.pgm");
                        LOG_INFO(MOD_GRAPHICS, "OCCLUSION: depth buffer dumped to occlusion_depth.pgm");

                        // Detailed per-region occluder diagnostic (use camera position)
                        float occDiagX = playerX_, occDiagY = playerY_, occDiagZ = playerZ_;
                        if (cameraController_) {
                            cameraController_->getPositionEQ(occDiagX, occDiagY, occDiagZ);
                        }
                        struct OccDiag { size_t regionIdx; float dist; size_t triCount; };
                        std::vector<OccDiag> occDiags;
                        for (auto& [regionIdx, node] : regionMeshNodes_) {
                            if (!node || !node->isVisible()) continue;
                            auto bboxIt = regionBoundingBoxes_.find(regionIdx);
                            if (bboxIt == regionBoundingBoxes_.end()) continue;
                            const auto& bbox = bboxIt->second;
                            float closestX = std::max(bbox.MinEdge.X, std::min(occDiagX, bbox.MaxEdge.X));
                            float closestY = std::max(bbox.MinEdge.Y, std::min(occDiagY, bbox.MaxEdge.Y));
                            float closestZ = std::max(bbox.MinEdge.Z, std::min(occDiagZ, bbox.MaxEdge.Z));
                            float ddx = occDiagX - closestX, ddy = occDiagY - closestY, ddz = occDiagZ - closestZ;
                            float dist = std::sqrt(ddx*ddx + ddy*ddy + ddz*ddz);
                            const auto& occ = occlusionCuller_->getRegionOccluders(regionIdx);
                            occDiags.push_back({regionIdx, dist, occ.size()});
                        }
                        std::sort(occDiags.begin(), occDiags.end(),
                            [](const OccDiag& a, const OccDiag& b) { return a.dist < b.dist; });
                        LOG_INFO(MOD_GRAPHICS, "OCCL REGIONS (closest 20 visible):");
                        for (int i = 0; i < 20 && i < (int)occDiags.size(); ++i) {
                            auto& d = occDiags[i];
                            const auto& occ = occlusionCuller_->getRegionOccluders(d.regionIdx);
                            if (occ.empty()) {
                                LOG_INFO(MOD_GRAPHICS, "  region {} dist={:.1f} NO occluders", d.regionIdx, d.dist);
                            } else {
                                LOG_INFO(MOD_GRAPHICS, "  region {} dist={:.1f} {} occluder tris:", d.regionIdx, d.dist, occ.size());
                                for (size_t t = 0; t < occ.size() && t < 4; ++t) {
                                    LOG_INFO(MOD_GRAPHICS, "    tri{}: ({:.1f},{:.1f},{:.1f}) ({:.1f},{:.1f},{:.1f}) ({:.1f},{:.1f},{:.1f}) area={:.1f}",
                                        t, occ[t].v0[0], occ[t].v0[1], occ[t].v0[2],
                                        occ[t].v1[0], occ[t].v1[1], occ[t].v1[2],
                                        occ[t].v2[0], occ[t].v2[1], occ[t].v2[2], occ[t].area);
                                }
                            }
                        }
                    }

                    // Force visibility rebuild
                    forcePvsUpdate_ = true;
                }
                break;

            default: break;
        }
    }
}

void IrrlichtRenderer::processInputDeltas(float deltaTime) {
    // Camera zoom with Follow camera
    if (!chatInputFocused_) {
        float zoomDelta = eventReceiver_->getCameraZoomDelta();
        if (zoomDelta != 0.0f) {
            LOG_DEBUG(MOD_INPUT, "[INPUT-TRACE] processInputDeltas: zoomDelta={}, cameraController={}, cameraMode={}(Follow={})",
                zoomDelta, (cameraController_ != nullptr),
                static_cast<int>(cameraMode_), static_cast<int>(CameraMode::Follow));
        }
        if (zoomDelta != 0.0f && cameraController_ && cameraMode_ == CameraMode::Follow) {
            cameraController_->adjustFollowDistance(zoomDelta);
            cameraController_->setFollowPosition(playerX_, playerY_, playerZ_, playerHeading_, deltaTime);
            LOG_DEBUG(MOD_GRAPHICS, "Camera zoom distance: {:.1f}", cameraController_->getFollowDistance());
        }
    }

    // D20c2: Push volume hotkey changes as intents via bridge
    if (bridge_) {
        float musicDelta = eventReceiver_->getMusicVolumeDelta();
        if (musicDelta != 0.0f) {
            bridge_->pushIntent(eqt::events::MusicVolumeChangeIntent{musicDelta});
        }
        float effectsDelta = eventReceiver_->getEffectsVolumeDelta();
        if (effectsDelta != 0.0f) {
            bridge_->pushIntent(eqt::events::EffectsVolumeChangeIntent{effectsDelta});
        }
    }
}

void IrrlichtRenderer::processChatInput() {
    // Handle spell gem shortcuts (1-8 keys)
    int8_t spellGemRequest = eventReceiver_->getSpellGemCastRequest();
    if (spellGemRequest >= 0 && !chatInputFocused_) {
        if (spellGemCastCallback_) {
            LOG_DEBUG(MOD_GRAPHICS, "Spell gem {} pressed", spellGemRequest + 1);
            spellGemCastCallback_(static_cast<uint8_t>(spellGemRequest));
        }
    }

    // Handle hotbar shortcuts (WindowManager removed — hotbar handled by new UI)
    int8_t hotbarRequest = eventReceiver_->getHotbarActivationRequest();
    (void)hotbarRequest;

    // Open chat input on Enter or / key
    if (!chatInputFocused_) {
        if (eventReceiver_->enterKeyPressed()) {
            chatInputFocused_ = true;
            chatPanelState_.inputActive = true;
            chatPanelState_.inputText.clear();
            chatPanelState_.cursorPos = 0;
            eventReceiver_->setChatInputFocused(true);
            eventReceiver_->clearPendingKeyEvents();
            return;
        }
        if (eventReceiver_->slashKeyPressed()) {
            chatInputFocused_ = true;
            chatPanelState_.inputActive = true;
            chatPanelState_.inputText = "/";
            chatPanelState_.cursorPos = 1;
            eventReceiver_->setChatInputFocused(true);
            eventReceiver_->clearPendingKeyEvents();
            return;
        }
    }

    // Process typed characters when chat is focused
    if (chatInputFocused_) {
        while (eventReceiver_->hasPendingKeyEvents()) {
            auto ke = eventReceiver_->popKeyEvent();

            if (ke.key == irr::KEY_RETURN) {
                // Submit chat text
                if (!chatPanelState_.inputText.empty()) {
                    LOG_INFO(MOD_GRAPHICS, "Chat submit: '{}'", chatPanelState_.inputText);
                    if (bridge_) {
                        bridge_->pushIntent(eqt::events::ChatSubmitIntent{chatPanelState_.inputText});
                    }
                }
                chatPanelState_.inputText.clear();
                chatPanelState_.cursorPos = 0;
                chatPanelState_.inputActive = false;
                chatInputFocused_ = false;
                eventReceiver_->setChatInputFocused(false);
                return;
            }

            if (ke.key == irr::KEY_ESCAPE) {
                // Cancel chat input
                chatPanelState_.inputText.clear();
                chatPanelState_.cursorPos = 0;
                chatPanelState_.inputActive = false;
                chatInputFocused_ = false;
                eventReceiver_->setChatInputFocused(false);
                return;
            }

            if (ke.key == irr::KEY_BACK) {
                // Backspace
                if (chatPanelState_.cursorPos > 0 && !chatPanelState_.inputText.empty()) {
                    chatPanelState_.inputText.erase(chatPanelState_.cursorPos - 1, 1);
                    chatPanelState_.cursorPos--;
                }
                continue;
            }

            // Insert printable character
            if (ke.character >= 32 && ke.character < 127) {
                chatPanelState_.inputText.insert(chatPanelState_.cursorPos, 1, static_cast<char>(ke.character));
                chatPanelState_.cursorPos++;
            }
        }
        return;
    }

    // Not in chat — clear pending keys and handle escape for target clearing
    eventReceiver_->clearPendingKeyEvents();
    if (eventReceiver_->escapeKeyPressed()) {
        if (currentTargetId_ != 0) {
            LOG_INFO(MOD_GRAPHICS, "[TARGET] Cleared target: {}", currentTargetName_);
            clearCurrentTarget();
            SetTrackedTargetId(0);
            eventReceiver_->pushBridgeAction({RendererAction::ClearTarget});
        }
    }
}

// ===== Phase 2: Visibility =====
void IrrlichtRenderer::processFrameVisibility() {
    // Update frustum planes every frame from actual Irrlicht camera direction.
    // We derive the direction from camera target - position (not CameraController yaw/pitch,
    // which can be stale or represent player facing rather than camera view in follow mode).
    // Update frustum planes every frame (needed by SimulationWorker for next frame's input)
    if (frustumCuller_ && camera_) {
        irr::core::vector3df irrFwd = (camera_->getTarget() - camera_->getPosition());
        float eqFwdX = irrFwd.X;
        float eqFwdY = irrFwd.Z;
        float eqFwdZ = irrFwd.Y;

        float fovV = camera_->getFOV();
        auto screenSize = driver_->getScreenSize();
        float aspect = (float)screenSize.Width / (float)screenSize.Height;
        float camX, camY, camZ;
        cameraController_->getPositionEQ(camX, camY, camZ);

        frustumCuller_->update(camX, camY, camZ, eqFwdX, eqFwdY, eqFwdZ,
            fovV, aspect, 1.0f, renderDistance_);
    }

    // Visibility/lighting handled by SimulationWorker — results applied in applySimulationResults()

    // Update camera position for atlas per-pixel lighting
    if (zoneShader_ && zoneShader_->isAtlasAvailable() && camera_) {
        irr::core::vector3df camPos = camera_->getPosition();
        zoneShader_->setCameraPos(camPos.X, camPos.Y, camPos.Z);
    }
}

// ===== Phase 3: Simulation =====
void IrrlichtRenderer::processFrameSimulation(float deltaTime) {
    // WindowManager removed — no old UI update
    sectionStart_ = std::chrono::steady_clock::now();

    // Entity update — worker handles interpolation + culling, main thread handles
    // animation side-effects, corpse fading, equipment transforms, casting bars
    if (entityRenderer_) {
        // Pass occlusion-culled regions to entity renderer for door manager etc.
        entityRenderer_->setOcclusionCulledRegions(
            occlusionCulledRegions_.empty() ? nullptr : &occlusionCulledRegions_);

        // Pass camera BSP region to entity renderer
        if (zoneBspTree_ && currentPvsRegion_ != SIZE_MAX
            && currentPvsRegion_ < zoneBspTree_->regions.size()) {
            entityRenderer_->setCameraRegion(currentPvsRegion_, zoneBspTree_->regions[currentPvsRegion_]);
        } else {
            entityRenderer_->setCameraRegion(SIZE_MAX, nullptr);
        }

        // Flush pending updates for animation processing only
        // (position/velocity math was already drained for the worker in postSimulationInput)
        entityRenderer_->flushPendingUpdatesForAnimations();

        // Main-thread entity state: corpse fading, equipment transforms, animation timing
        entityRenderer_->updateMainThreadEntityState(deltaTime);

        entityRenderer_->updateEntityCastingBars(deltaTime, camera_);
        entityRenderer_->processExpiredCombatBuffers();
    }
    frameTimings_.entityUpdate = measureSection();

    // Door update
    if (doorManager_) {
        doorManager_->setOcclusionCulledRegions(
            occlusionCulledRegions_.empty() ? nullptr : &occlusionCulledRegions_);
        doorManager_->setPvsRegion(currentPvsRegion_);
        doorManager_->update(deltaTime);
    }
    frameTimings_.doorUpdate = measureSection();

    // Spell VFX update
    if (spellVisualFX_) spellVisualFX_->update(deltaTime);
    frameTimings_.spellVfxUpdate = measureSection();

    // Animated textures
    if (animatedTextureManager_) animatedTextureManager_->update(deltaTime * 1000.0f);
    frameTimings_.animatedTextures = measureSection();

    // Vertex animations and light animations handled by SimulationWorker
    frameTimings_.vertexAnimations = measureSection();

    // Reset timing section so zone load time isn't attributed to tier2Update
    sectionStart_ = std::chrono::steady_clock::now();

    // Rebuild HCMap placeholder as player moves (GREEN-gated)
    // Only active during placeholder phase — before real geometry replaces it
    if (zonePlaceholderNode_ && collisionMap_ && collisionMap_->IsLoaded()) {
        if (!governor_ || governor_->getState() == BudgetState::Green) {
            irr::core::vector3df currentPos(playerX_, playerZ_, playerY_);  // EQ→Irrlicht
            float movedSq = currentPos.getDistanceFromSQ(lastPlaceholderBuildPos_);
            const float rebuildThresholdSq = 100.0f * 100.0f;  // 100 units
            if (movedSq > rebuildThresholdSq) {
                buildZonePlaceholder(playerX_, playerZ_, playerY_);
                setupHCMapCollision();
            }
        }
    }

    // Tier 2: Detail + Tree (~20Hz)
    // Skip while deferred init is still running — subsystems aren't ready yet
    {
    bool runDetailTreeThisFrame = runTier2_;
    if (runDetailTreeThisFrame) {
        if (detailManager_ && detailManager_->isEnabled()) {
            irr::core::vector3df playerPosIrrlicht(playerX_, playerZ_, playerY_);
            static float lastPlayerX = playerX_, lastPlayerY = playerY_;
            irr::core::vector3df playerVelocity(0, 0, 0);
            if (deltaTime > 0.001f) {
                float velX = (playerX_ - lastPlayerX) / deltaTime;
                float velZ = (playerY_ - lastPlayerY) / deltaTime;
                playerVelocity = irr::core::vector3df(velX, 0, velZ);
            }
            lastPlayerX = playerX_;
            lastPlayerY = playerY_;
            bool playerMoving = playerVelocity.getLengthSQ() > 0.1f;
            detailManager_->update(playerPosIrrlicht, deltaTime * 1000.0f,
                                   playerPosIrrlicht, playerVelocity, playerHeading_, playerMoving);
        }
        // Worker computes tree vertex positions — just advance wind controller time
        if (treeManager_ && treeManager_->isEnabled()
            && treeManager_->getAnimatedTreeCount() > 0) {
            treeManager_->getWindController().update(deltaTime);
        }
    }
    } // tier2 detail+tree block
    frameTimings_.tier2Update = measureSection();

    // Fire light flickering handled by SimulationWorker
    frameTimings_.fireFlicker = measureSection();

    // Player light position set in applySimulationResults() by SimulationWorker

    // Tier 3: Environmental simulation (~10Hz)
    {
        // Weather state machine runs on SimulationWorker, results applied in applySimulationResults()
        frameTimings_.weatherSystemUpdate = measureSection();

        bool runEnvThisFrame = runTier3_;

        if (runEnvThisFrame) {
            float accDelta = tier3DeltaAccum_;
            tier3DeltaAccum_ = 0.0f;

            // Weather effects: render-only update (rain/snow overlays, cloud layer)
            // Timer/darkening/lightning state computed by SimulationWorker, applied in applySimulationResults()
            if (weatherEffects_) weatherEffects_->updateRenderOnly(accDelta);

            // Sky cloud scrolling computed by SimulationWorker, applied in applySimulationResults()

            if (particleManager_ && particleManager_->isEnabled() && zoneReady_) {
                particleManager_->setPlayerPosition(glm::vec3(playerX_, playerY_, playerZ_), playerHeading_);
                float timeOfDay = currentHour_ + currentMinute_ / 60.0f;
                particleManager_->setTimeOfDay(timeOfDay);
                particleManager_->update(accDelta);
            }

            // Unified particles (fire + weather + spell effects):
            // Physics now runs in SimulationWorker. Weather light collection
            // and command posting happen in postSimulationInput().
            // updateUnified() is a no-op — kept for API compatibility.
            if (particleManager_ && zoneReady_) {
                particleManager_->updateUnified(0.0f);
            }

            // Boids and tumbleweeds now run in SimulationWorker —
            // input posted in postSimulationInput(), results applied in applySimulationResults()
        }
    }
    frameTimings_.tier3Update = measureSection();

    // HUD
    hudAnimTimer_ += deltaTime;
    if (hudAnimTimer_ > 10000.0f) hudAnimTimer_ = 0.0f;
    updateHUD();
    frameTimings_.hudUpdate = measureSection();
}

// ===== Phase 4: Render =====
bool IrrlichtRenderer::processFrameRender(float deltaTime) {
    // If loading screen is visible, show it instead of rendering zone
    if (loadingScreenVisible_) {
        drawLoadingScreen(loadingProgress_, loadingText_);
        return false;  // Signal loading screen was shown
    }

    // Run scene breakdown profile if scheduled
    if (sceneProfileEnabled_) {
        if (sceneProfileFrameCount_ < 0) {
            sceneProfileFrameCount_++;
        } else {
            profileSceneBreakdown();
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
        // Wind params from tree wind config (or defaults) with weather multiplier
        float weatherMult = 1.0f;
        if (treeManager_) {
            weatherMult = treeManager_->getWindController().getWeatherMultiplier();
        }
        // Default config values scaled by weather
        zoneShader_->setWindParams(
            0.3f * weatherMult,   // baseStrength
            0.4f,                 // baseFrequency
            0.5f * weatherMult,   // gustStrength
            0.1f);                // gustFrequency

        zoneShader_->beginFrame();
    }
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
    drawTargetOutline();
#else
    drawTargetSelectionBox();
#endif
    frameTimings_.targetBox = measureSection();

    LOG_TRACE(MOD_GRAPHICS, "POST-DRAW checkpoint 4: particles (mgr={}, enabled={}, zoneReady={})",
              particleManager_ != nullptr,
              particleManager_ ? particleManager_->isEnabled() : false,
              zoneReady_);
    // Render environmental particles (render every frame, update at Tier 3)
    if (particleManager_ && particleManager_->isEnabled() && zoneReady_) particleManager_->render();

    LOG_TRACE(MOD_GRAPHICS, "POST-DRAW checkpoint 5: unified particles (have3D={})", have3DTransforms_);
    // Unified particle system (fire point sprites, GLES2 only)
    // Not gated by isEnabled() — fire has its own toggle (unifiedFireEnabled_)
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
            updateFireGlowLighting();
        }
        if (fireGlowIcospheresEnabled_ && icosphereProgram_ != 0) {
            renderFireGlowIcospheres();
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
        updateMapOverlay(glm::vec3(playerX_, playerZ_, playerY_));
        drawMapOverlay();
    }

    // Navmesh overlay
    if (showNavmeshOverlay_) {
        updateNavmeshOverlay(glm::vec3(playerX_, playerZ_, playerY_));
        drawNavmeshOverlay();
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
        drawFPSCounter();
    }
    frameTimings_.guiDrawAll = measureSection();

    // U06j: Process new UI input before rendering
    if (newUIEnabled_ && staticUIInput_ && eventReceiver_) {
        staticUIInput_->processInput(
            eventReceiver_->getMouseX(), eventReceiver_->getMouseY(), pendingUIClick_,
            uiLayout_, hotbarState_, spellGemState_, inventoryState_,
            spellbookState_, skillsPopupState_);
        pendingUIClick_ = false;
    }

    // U03c: Render new static UI when enabled
    if (newUIEnabled_ && uiRenderer_) {
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
        renderXPBar(*uiRenderer_, uiLayout_, xpBarState_);
        uiRenderer_->endFrame();
    }
    frameTimings_.staticUI = measureSection();

    // Draw zone line overlay
    drawZoneLineOverlay();
    drawZoneLineBoxLabels();
    frameTimings_.zoneLineOverlay = measureSection();

#ifdef WITH_RDP
    captureFrameForRDP();
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
    driver_->endScene();
    frameTimings_.endScene = measureSection();

    LOG_TRACE(MOD_GRAPHICS, "POST-DRAW checkpoint 12: frame complete");
    FlushThreadLog();
    return true;
}

} // namespace Graphics
} // namespace EQT
