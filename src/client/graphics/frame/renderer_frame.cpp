// renderer_frame.cpp - IrrlichtRenderer's per-frame orchestrator.
//
// Holds processFrame() alone, which sequences the four frame phases.
// Phase 1 (input), Phase 2 (visibility) and Phase 3 (simulation) live in
// renderer_frame_input.cpp; Phase 4 (render) lives in
// renderer_frame_render.cpp.
// Split per refactor/J31_renderer_frame_split.md (move-only).
#include "client/graphics/irrlicht_renderer.h"
#include "client/graphics/frame/frame_component.h"
#include "client/graphics/entity_prep_worker.h"
#include "client/graphics/sky_renderer.h"
#include "client/graphics/weather_effects_controller.h"
#include "client/graphics/spell_visual_fx.h"
#include "client/graphics/ui/static_ui_input.h"
#include "client/graphics/ui/ui_renderer.h"
#include "client/graphics/ui/character_model_view.h"
#include "client/chat/chat_message_buffer.h"  // L11: complete type for FrameComponent's out-of-line dtor
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

// Out-of-line so that uiRenderer_, staticUIInput_ and newUIChatBuffer_ can stay
// forward-declared in frame_component.h (batch correction 35: the
// constructor's unwind path needs the complete type just as the destructor
// does).
FrameComponent::FrameComponent(RendererContext& ctx, IrrlichtRenderer& owner)
    : ctx_(ctx), owner_(owner) {}
FrameComponent::~FrameComponent() = default;

// L04 (refactor/L04_diagnostics_component.md): measureSection() used to sit in
// renderer_diagnostics.cpp, but it is frame-phase state, not diagnostics --
// sectionStart_ is declared under "Frame phase shared state" in
// include/client/graphics/irrlicht_renderer.h and all 35 call sites are the
// frame family's (renderer_frame.cpp, renderer_frame_input.cpp,
// renderer_frame_render.cpp). It stays on IrrlichtRenderer and moves here, to
// the family that owns it; L11 takes it with the rest of the frame family.
//
// L12 (Batch L close-out) finished the move L04 predicted and L11 was told to
// leave: the declaration and sectionStart_ are now FrameComponent's, so this
// is FrameComponent::measureSection(). THE BODY IS UNCHANGED -- sectionStart_
// keeps its spelling because the member kept its name.
int64_t FrameComponent::measureSection() {
    auto now = std::chrono::steady_clock::now();
    auto us = std::chrono::duration_cast<std::chrono::microseconds>(now - sectionStart_).count();
    sectionStart_ = now;
    return us;
}

bool FrameComponent::processFrame(float deltaTime) {
    auto frameStart = std::chrono::steady_clock::now();
    sectionStart_ = frameStart;

    if (ctx_.governor) ctx_.governor->beginFrame();

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

    if (!owner_.isRunning()) {
        LOG_INFO(MOD_GRAPHICS, "processFrame: isRunning() returned false");
        LOG_INFO(MOD_GRAPHICS, "initialized_={} device_={} device_run={} quitRequested={}",
                  owner_.initialized_, (ctx_.device ? "valid" : "null"), (ctx_.device ? ctx_.device->run() : false),
                  (ctx_.eventReceiver ? ctx_.eventReceiver->quitRequested() : false));
        return false;
    }

    // Update FPS
    irr::u32 currentTime = ctx_.device->getTimer()->getTime();
    frameCount_++;
    if (currentTime - lastFpsTime_ >= 1000) {
        currentFps_ = frameCount_;
        frameCount_ = 0;
        lastFpsTime_ = currentTime;
    }

    // Tiered update: increment frame counter and accumulate delta for Tier 3
    ctx_.frameNumber++;
    tier3DeltaAccum_ += deltaTime;
    runTier2_ = (ctx_.frameNumber % kTier2Interval == 0);
    runTier3_ = (ctx_.frameNumber % kTier3Interval == 0);

    // Safety: force Tier 2 at least once per second (~30 frames at 30fps)
    if (ctx_.frameNumber % 30 == 0) runTier2_ = true;

    // Phase 1: Input
    processFrameInput(deltaTime);

    // ─── Always: drain queues (cheap data moves, no GL) ───
    sectionStart_ = std::chrono::steady_clock::now();
#ifdef EQT_HAS_GLES2
    if (ctx_.progressiveLoadingActive) {
        // Loading screen: batch process freely (§4.2: no governor restriction)
        ctx_.ZoneLifecycle().processCompletedUploads();
    } else {
        ctx_.ZoneLifecycle().drainGPUResults();  // move to pendingGPUResults_ (no GL)
    }
#endif

    // T04: processUploadQueue/drainDecodedQueue removed. Textures uploaded via uploadTexture().
    frameTimings_.drainQueues = measureSection();

    // SimulationWorker: apply results from previous frame, then post new input
    ctx_.SimBridge().applySimulationResults();
    frameTimings_.simWorkerApply = measureSection();

    // Phase 2: Visibility
    sectionStart_ = std::chrono::steady_clock::now();
    processFrameVisibility();
    frameTimings_.visibility = measureSection();

    // SimulationWorker: post new input after frustum planes are updated
    ctx_.SimBridge().postSimulationInput(deltaTime);
    frameTimings_.postSimInput = measureSection();

    // ─── Progressive loading (loading screen showing, no governor restriction §4.2) ───
    if (ctx_.progressiveLoadingActive) {
        ctx_.ZoneStream().processFrameProgressiveLoad();
        frameTimings_.meshLoading = measureSection();
    }

    // ─── Post-loading: all work GREEN-gated, 1 unit per GREEN frame ───
    sectionStart_ = std::chrono::steady_clock::now();
    if (!ctx_.progressiveLoadingActive) {
        if (!ctx_.governor || ctx_.governor->getState() == BudgetState::Green) {
            bool didWork = false;

            // Priority 1: Complete in-flight GPU work (VBO/texture registration)
#ifdef EQT_HAS_GLES2
            if (!didWork) didWork = ctx_.ZoneLifecycle().processOneGPUResult();
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
            if (!didWork && ctx_.entityPrepReady && ctx_.entityRenderer)
                didWork = ctx_.entityRenderer->pollAndDistributePrepResults();

            // Priority 6: Entity mesh building
            if (!didWork && ctx_.entityPrepReady && ctx_.entityRenderer && !ctx_.config.constrainedConfig.skipEntityBuild)
                didWork = ctx_.entityRenderer->processOneEntityBuildStep(ctx_.currentPvsRegion);

            // Priority 7 (texture rebuild for regions without a buffer map)
            // removed T67 — gated on `constrainedMeshCache_ &&`, never true.
            // textureRebuildQueue_ is still populated live by the GPU
            // upload thread (renderer_gpu_upload.cpp); with every
            // mesh-cache-gated consumer of it gone, it is now drained only
            // at zone unload, exactly as before this change (this consumer
            // never ran in any shipped build either).

            // Priority 8: Door texture arrival
            if (!didWork && !ctx_.doorTextureRebuildQueue.empty() && ctx_.doorManager) {
                uint8_t doorId = ctx_.doorTextureRebuildQueue.front();
                ctx_.doorTextureRebuildQueue.erase(ctx_.doorTextureRebuildQueue.begin());
                ctx_.doorManager->rebuildSingleDoor(doorId);
                didWork = true;
                LOG_INFO(MOD_GRAPHICS, "Post-progressive: rebuilt door {} with textures (queue: {})",
                         doorId, ctx_.doorTextureRebuildQueue.size());
            }

            // Priority 9: Object texture arrival
            if (!didWork && !ctx_.objectTextureRebuildQueue.empty()) {
                size_t objIdx = ctx_.objectTextureRebuildQueue.front();
                ctx_.objectTextureRebuildQueue.erase(ctx_.objectTextureRebuildQueue.begin());
                if (objIdx < ctx_.deferredObjects.size()) {
                    size_t nodeIdx = ctx_.deferredObjects[objIdx].nodeIndex;
                    if (nodeIdx != SIZE_MAX && nodeIdx < ctx_.objectNodes.size() && ctx_.objectNodes[nodeIdx]) {
                        auto* oldNode = ctx_.objectNodes[nodeIdx];
                        if (ctx_.animatedTextureManager)
                            ctx_.animatedTextureManager->removeSceneNode(oldNode);
                        if (oldNode->getParent()) oldNode->remove(); else oldNode->drop();
                        ctx_.objectNodes[nodeIdx] = nullptr;
                        if (nodeIdx < ctx_.objectInSceneGraph.size())
                            ctx_.objectInSceneGraph[nodeIdx] = false;
                    }
                    ctx_.deferredObjects[objIdx].meshBuilt = false;
                    ctx_.ZoneStream().buildDeferredObject(objIdx);
                    didWork = true;
                    LOG_INFO(MOD_GRAPHICS, "Post-progressive: rebuilt object {} with textures (queue: {})",
                             objIdx, ctx_.objectTextureRebuildQueue.size());
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
        if (ctx_.entityPrepReady && ctx_.entityPrepWorker && ctx_.entityRenderer) {
            ctx_.entityRenderer->getRaceModelLoader()->promotePreparedModels();
            if (++ctx_.entityPrepScanCounter % 10 == 0) ctx_.ZoneStream().queueEntityPrepRequests();
            if (ctx_.entityPrepWorker->isIdle()) ctx_.entityPrepWorker->dispatchOne();
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
        if (ctx_.governor) {
            bool hasPendingWork = ctx_.progressiveLoadingActive;
            // The meshLoadQueue_-derived pending-work check (removed T67)
            // was gated on constrainedMeshCache_, never true (the cache was
            // never constructed), so hasPendingWork here was always exactly
            // progressiveLoadingActive_ in every shipped build.
            ctx_.governor->endFrame(static_cast<float>(frameTimings_.endScene), hasPendingWork);
        }

        float totalFrameMs = frameTimings_.totalFrame / 1000.0f;
        float budgetMs = ctx_.governor ? ctx_.governor->getTargetFrameTimeMs() : 33.3f;

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
        if (ctx_.governor &&
            ctx_.governor->getState() == BudgetState::Red) {
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
                         ctx_.governor->getAverageFrameTimeMs(),
                         ctx_.governor->getStallCounter(),
                         FrameBudgetGovernor::kStallThreshold,
                         redFramesSinceWarn);
                LOG_WARN(MOD_GRAPHICS, "  RED SCENE: {} sceneNodes, {} polys | manualDraw: {} regions, {} drawCalls | sortedList: {}",
                         frameTimings_.sceneNodeCount, lastPolygonCount_,
                         frameTimings_.manualZoneRegionsDrawn, frameTimings_.manualZoneDrawCalls,
                         ctx_.sortedZoneDrawList.size());
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
                    if (ctx_.smgr && ctx_.smgr->getRootSceneNode())
                        countNodes(ctx_.smgr->getRootSceneNode());
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
                         ctx_.governor ? ctx_.governor->getStateName() : "N/A",
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
            ctx_.Diagnostics().logFrameTimings();
            // Reset accumulators
            frameTimingsAccum_ = IrrlichtRenderer::FrameTimings();
            frameTimingsSampleCount_ = 0;
        }
    }

    return true;
}

} // namespace Graphics
} // namespace EQT
