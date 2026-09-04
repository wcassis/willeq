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
int64_t IrrlichtRenderer::measureSection() {
    auto now = std::chrono::steady_clock::now();
    auto us = std::chrono::duration_cast<std::chrono::microseconds>(now - sectionStart_).count();
    sectionStart_ = now;
    return us;
}

bool FrameComponent::processFrame(float deltaTime) {
    auto frameStart = std::chrono::steady_clock::now();
    owner_.frameStart_ = frameStart;
    owner_.sectionStart_ = frameStart;

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
    owner_.frameCount_++;
    if (currentTime - owner_.lastFpsTime_ >= 1000) {
        owner_.currentFps_ = owner_.frameCount_;
        owner_.frameCount_ = 0;
        owner_.lastFpsTime_ = currentTime;
    }

    // Tiered update: increment frame counter and accumulate delta for Tier 3
    owner_.frameNumber_++;
    tier3DeltaAccum_ += deltaTime;
    runTier2_ = (owner_.frameNumber_ % kTier2Interval == 0);
    runTier3_ = (owner_.frameNumber_ % kTier3Interval == 0);

    // Safety: force Tier 2 at least once per second (~30 frames at 30fps)
    if (owner_.frameNumber_ % 30 == 0) runTier2_ = true;

    // Phase 1: Input
    processFrameInput(deltaTime);

    // ─── Always: drain queues (cheap data moves, no GL) ───
    owner_.sectionStart_ = std::chrono::steady_clock::now();
#ifdef EQT_HAS_GLES2
    if (ctx_.progressiveLoadingActive) {
        // Loading screen: batch process freely (§4.2: no governor restriction)
        owner_.ZoneLifecycle().processCompletedUploads();
    } else {
        owner_.ZoneLifecycle().drainGPUResults();  // move to pendingGPUResults_ (no GL)
    }
#endif

    // T04: processUploadQueue/drainDecodedQueue removed. Textures uploaded via uploadTexture().
    owner_.frameTimings_.drainQueues = owner_.measureSection();

    // SimulationWorker: apply results from previous frame, then post new input
    owner_.SimBridge().applySimulationResults();
    owner_.frameTimings_.simWorkerApply = owner_.measureSection();

    // Phase 2: Visibility
    owner_.sectionStart_ = std::chrono::steady_clock::now();
    processFrameVisibility();
    owner_.frameTimings_.visibility = owner_.measureSection();

    // SimulationWorker: post new input after frustum planes are updated
    owner_.SimBridge().postSimulationInput(deltaTime);
    owner_.frameTimings_.postSimInput = owner_.measureSection();

    // ─── Progressive loading (loading screen showing, no governor restriction §4.2) ───
    if (ctx_.progressiveLoadingActive) {
        owner_.ZoneStream().processFrameProgressiveLoad();
        owner_.frameTimings_.meshLoading = owner_.measureSection();
    }

    // ─── Post-loading: all work GREEN-gated, 1 unit per GREEN frame ───
    owner_.sectionStart_ = std::chrono::steady_clock::now();
    if (!ctx_.progressiveLoadingActive) {
        if (!ctx_.governor || ctx_.governor->getState() == BudgetState::Green) {
            bool didWork = false;

            // Priority 1: Complete in-flight GPU work (VBO/texture registration)
#ifdef EQT_HAS_GLES2
            if (!didWork) didWork = owner_.ZoneLifecycle().processOneGPUResult();
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
            if (!didWork && !owner_.doorTextureRebuildQueue_.empty() && ctx_.doorManager) {
                uint8_t doorId = owner_.doorTextureRebuildQueue_.front();
                owner_.doorTextureRebuildQueue_.erase(owner_.doorTextureRebuildQueue_.begin());
                ctx_.doorManager->rebuildSingleDoor(doorId);
                didWork = true;
                LOG_INFO(MOD_GRAPHICS, "Post-progressive: rebuilt door {} with textures (queue: {})",
                         doorId, owner_.doorTextureRebuildQueue_.size());
            }

            // Priority 9: Object texture arrival
            if (!didWork && !owner_.objectTextureRebuildQueue_.empty()) {
                size_t objIdx = owner_.objectTextureRebuildQueue_.front();
                owner_.objectTextureRebuildQueue_.erase(owner_.objectTextureRebuildQueue_.begin());
                if (objIdx < owner_.deferredObjects_.size()) {
                    size_t nodeIdx = owner_.deferredObjects_[objIdx].nodeIndex;
                    if (nodeIdx != SIZE_MAX && nodeIdx < ctx_.objectNodes.size() && ctx_.objectNodes[nodeIdx]) {
                        auto* oldNode = ctx_.objectNodes[nodeIdx];
                        if (ctx_.animatedTextureManager)
                            ctx_.animatedTextureManager->removeSceneNode(oldNode);
                        if (oldNode->getParent()) oldNode->remove(); else oldNode->drop();
                        ctx_.objectNodes[nodeIdx] = nullptr;
                        if (nodeIdx < ctx_.objectInSceneGraph.size())
                            ctx_.objectInSceneGraph[nodeIdx] = false;
                    }
                    owner_.deferredObjects_[objIdx].meshBuilt = false;
                    owner_.ZoneStream().buildDeferredObject(objIdx);
                    didWork = true;
                    LOG_INFO(MOD_GRAPHICS, "Post-progressive: rebuilt object {} with textures (queue: {})",
                             objIdx, owner_.objectTextureRebuildQueue_.size());
                }
            }

            // Priority 10 (submit new background mesh build) removed T67 —
            // see the Priority 3/4 removal note above.

            // Priority 11: Lazy icon loading (disabled — WindowManager removed)

            if (didWork) {
                owner_.frameTimings_.meshLoading = owner_.measureSection();
            }
        }

        // Lightweight entity housekeeping (always runs, not didWork-gated):
        if (ctx_.entityPrepReady && ctx_.entityPrepWorker && ctx_.entityRenderer) {
            ctx_.entityRenderer->getRaceModelLoader()->promotePreparedModels();
            if (++owner_.entityPrepScanCounter_ % 10 == 0) owner_.ZoneStream().queueEntityPrepRequests();
            if (ctx_.entityPrepWorker->isIdle()) ctx_.entityPrepWorker->dispatchOne();
        }
        owner_.frameTimings_.entityHousekeeping = owner_.measureSection();
    }
    owner_.frameTimings_.postLoadWork = owner_.measureSection();

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
        owner_.frameTimings_.totalFrame = std::chrono::duration_cast<std::chrono::microseconds>(
            frameEnd - frameStart).count();

        // Update render cost EMA (alpha = 0.1 for smooth tracking)
        int64_t renderUs = owner_.frameTimings_.sceneDrawAll + owner_.frameTimings_.targetBox +
                           owner_.frameTimings_.particles + owner_.frameTimings_.boids +
                           owner_.frameTimings_.weatherRender + owner_.frameTimings_.debugOverlays +
                           owner_.frameTimings_.castingBars + owner_.frameTimings_.guiDrawAll +
                           owner_.frameTimings_.staticUI + owner_.frameTimings_.zoneLineOverlay +
                           owner_.frameTimings_.endScene + owner_.frameTimings_.footprintRender +
                           owner_.frameTimings_.postRender;
        owner_.renderCostAvgUs_ = owner_.renderCostAvgUs_ * 0.9f + static_cast<float>(renderUs) * 0.1f;

        // Update essential simulation cost EMA
        int64_t essSimUs = owner_.frameTimings_.entityUpdate + owner_.frameTimings_.doorUpdate +
                           owner_.frameTimings_.spellVfxUpdate + owner_.frameTimings_.animatedTextures +
                           owner_.frameTimings_.vertexAnimations + owner_.frameTimings_.hudUpdate;
        owner_.essentialSimCostAvgUs_ = owner_.essentialSimCostAvgUs_ * 0.9f + static_cast<float>(essSimUs) * 0.1f;

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
            ctx_.governor->endFrame(static_cast<float>(owner_.frameTimings_.endScene), hasPendingWork);
        }

        float totalFrameMs = owner_.frameTimings_.totalFrame / 1000.0f;
        float budgetMs = ctx_.governor ? ctx_.governor->getTargetFrameTimeMs() : 33.3f;

        // ===== SPIKE DETECTOR: unconditional, fires on any frame >50ms =====
        // Logs every section >1ms sorted by cost. Independent of governor state.
        if (totalFrameMs > 50.0f) {
            struct SectionCost { const char* name; int64_t us; };
            SectionCost spikeSections[] = {
                {"inputHandling",      owner_.frameTimings_.inputHandling},
                {"playerMovement",     owner_.frameTimings_.playerMovement},
                {"cameraUpdate",       owner_.frameTimings_.cameraUpdate},
                {"drainQueues",        owner_.frameTimings_.drainQueues},
                {"simWorkerApply",     owner_.frameTimings_.simWorkerApply},
                {"visibility",         owner_.frameTimings_.visibility},
                {"postSimInput",       owner_.frameTimings_.postSimInput},
                {"meshLoading",        owner_.frameTimings_.meshLoading},
                {"postLoadWork",       owner_.frameTimings_.postLoadWork},
                {"entityHousekeeping", owner_.frameTimings_.entityHousekeeping},
                {"entityUpdate",       owner_.frameTimings_.entityUpdate},
                {"doorUpdate",         owner_.frameTimings_.doorUpdate},
                {"spellVfxUpdate",     owner_.frameTimings_.spellVfxUpdate},
                {"animatedTextures",   owner_.frameTimings_.animatedTextures},
                {"vertexAnimations",   owner_.frameTimings_.vertexAnimations},
                {"tier2Update",        owner_.frameTimings_.tier2Update},
                {"tier3Update",        owner_.frameTimings_.tier3Update},
                {"hudUpdate",          owner_.frameTimings_.hudUpdate},
                {"sceneDrawAll",       owner_.frameTimings_.sceneDrawAll},
                {"footprintRender",    owner_.frameTimings_.footprintRender},
                {"targetBox",          owner_.frameTimings_.targetBox},
                {"particles",          owner_.frameTimings_.particles},
                {"boids",              owner_.frameTimings_.boids},
                {"weatherRender",      owner_.frameTimings_.weatherRender},
                {"debugOverlays",      owner_.frameTimings_.debugOverlays},
                {"castingBars",        owner_.frameTimings_.castingBars},
                {"guiDrawAll",         owner_.frameTimings_.guiDrawAll},
                {"staticUI",           owner_.frameTimings_.staticUI},
                {"zoneLineOverlay",    owner_.frameTimings_.zoneLineOverlay},
                {"endScene",           owner_.frameTimings_.endScene},
                {"postRender",         owner_.frameTimings_.postRender},
            };
            std::sort(std::begin(spikeSections), std::end(spikeSections),
                      [](const auto& a, const auto& b) { return a.us > b.us; });

            // Sum all measured sections to find unaccounted time
            int64_t accountedUs = 0;
            for (const auto& s : spikeSections) accountedUs += s.us;
            int64_t unaccountedUs = owner_.frameTimings_.totalFrame - accountedUs;

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
                {"inputHandling",    owner_.frameTimings_.inputHandling},
                {"playerMovement",   owner_.frameTimings_.playerMovement},
                {"cameraUpdate",     owner_.frameTimings_.cameraUpdate},
                {"pvsVisibility",    owner_.frameTimings_.pvsVisibility},
                {"occlusionCulling", owner_.frameTimings_.occlusionCulling},
                {"zoneLightVis",     owner_.frameTimings_.zoneLightVisibility},
                {"objectVisibility", owner_.frameTimings_.objectVisibility},
                {"meshLoading",      owner_.frameTimings_.meshLoading},
                {"objectLights",     owner_.frameTimings_.objectLights},
                {"entityUpdate",     owner_.frameTimings_.entityUpdate},
                {"doorUpdate",       owner_.frameTimings_.doorUpdate},
                {"spellVfxUpdate",   owner_.frameTimings_.spellVfxUpdate},
                {"animatedTextures", owner_.frameTimings_.animatedTextures},
                {"vertexAnimations", owner_.frameTimings_.vertexAnimations},
                {"fireFlicker",      owner_.frameTimings_.fireFlicker},
                {"tier2Update",      owner_.frameTimings_.tier2Update},
                {"tier3Update",      owner_.frameTimings_.tier3Update},
                {"hudUpdate",        owner_.frameTimings_.hudUpdate},
                {"sceneDrawAll",     owner_.frameTimings_.sceneDrawAll},
                {"sceneAnimate",     owner_.frameTimings_.sceneAnimate},
                {"sceneSolid",       owner_.frameTimings_.sceneSolid},
                {"sceneTransparent", owner_.frameTimings_.sceneTransparent},
                {"manualZoneDraw",   owner_.frameTimings_.manualZoneDraw},
                {"footprintRender",  owner_.frameTimings_.footprintRender},
                {"targetBox",        owner_.frameTimings_.targetBox},
                {"particles",        owner_.frameTimings_.particles},
                {"weatherRender",    owner_.frameTimings_.weatherRender},
                {"castingBars",      owner_.frameTimings_.castingBars},
                {"guiDrawAll",       owner_.frameTimings_.guiDrawAll},
                {"staticUI",         owner_.frameTimings_.staticUI},
                {"glFinish",         owner_.frameTimings_.glFinish},
                {"endScene",         owner_.frameTimings_.endScene},
                {"postRender",       owner_.frameTimings_.postRender},
                {"simWorkerApply",   owner_.frameTimings_.simWorkerApply},
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
                         owner_.frameTimings_.sceneNodeCount, owner_.lastPolygonCount_,
                         owner_.frameTimings_.manualZoneRegionsDrawn, owner_.frameTimings_.manualZoneDrawCalls,
                         owner_.sortedZoneDrawList_.size());
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
                    {"playerMovement", owner_.frameTimings_.playerMovement},
                    {"pvsVisibility", owner_.frameTimings_.pvsVisibility},
                    {"occlusionCulling", owner_.frameTimings_.occlusionCulling},
                    {"meshLoading", owner_.frameTimings_.meshLoading},
                    {"entityUpdate", owner_.frameTimings_.entityUpdate},
                    {"tier2Update", owner_.frameTimings_.tier2Update},
                    {"tier3Update", owner_.frameTimings_.tier3Update},
                    {"sceneDrawAll", owner_.frameTimings_.sceneDrawAll},
                    {"staticUI", owner_.frameTimings_.staticUI},
                    {"endScene", owner_.frameTimings_.endScene},
                    {"particles", owner_.frameTimings_.particles},
                    {"simWorkerApply", owner_.frameTimings_.simWorkerApply},
                    {"targetBox", owner_.frameTimings_.targetBox},
                    {"guiDrawAll", owner_.frameTimings_.guiDrawAll},
                    {"footprintRender", owner_.frameTimings_.footprintRender},
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
    if (owner_.frameTimingEnabled_) {
        owner_.frameTimingsAccum_.inputHandling += owner_.frameTimings_.inputHandling;
        owner_.frameTimingsAccum_.cameraUpdate += owner_.frameTimings_.cameraUpdate;
        owner_.frameTimingsAccum_.entityUpdate += owner_.frameTimings_.entityUpdate;
        owner_.frameTimingsAccum_.doorUpdate += owner_.frameTimings_.doorUpdate;
        owner_.frameTimingsAccum_.spellVfxUpdate += owner_.frameTimings_.spellVfxUpdate;
        owner_.frameTimingsAccum_.animatedTextures += owner_.frameTimings_.animatedTextures;
        owner_.frameTimingsAccum_.vertexAnimations += owner_.frameTimings_.vertexAnimations;
        owner_.frameTimingsAccum_.fireFlicker += owner_.frameTimings_.fireFlicker;
        owner_.frameTimingsAccum_.objectVisibility += owner_.frameTimings_.objectVisibility;
        owner_.frameTimingsAccum_.pvsVisibility += owner_.frameTimings_.pvsVisibility;
        owner_.frameTimingsAccum_.meshLoading += owner_.frameTimings_.meshLoading;
        owner_.frameTimingsAccum_.objectLights += owner_.frameTimings_.objectLights;
        owner_.frameTimingsAccum_.tier2Update += owner_.frameTimings_.tier2Update;
        owner_.frameTimingsAccum_.tier3Update += owner_.frameTimings_.tier3Update;
        owner_.frameTimingsAccum_.hudUpdate += owner_.frameTimings_.hudUpdate;
        owner_.frameTimingsAccum_.sceneDrawAll += owner_.frameTimings_.sceneDrawAll;
        owner_.frameTimingsAccum_.sceneAnimate += owner_.frameTimings_.sceneAnimate;
        owner_.frameTimingsAccum_.sceneSolid += owner_.frameTimings_.sceneSolid;
        owner_.frameTimingsAccum_.sceneTransparent += owner_.frameTimings_.sceneTransparent;
        owner_.frameTimingsAccum_.sceneSkybox += owner_.frameTimings_.sceneSkybox;
        owner_.frameTimingsAccum_.sceneOther += owner_.frameTimings_.sceneOther;
        owner_.frameTimingsAccum_.sceneNodeCount += owner_.frameTimings_.sceneNodeCount;
        owner_.frameTimingsAccum_.manualZoneDraw += owner_.frameTimings_.manualZoneDraw;
        owner_.frameTimingsAccum_.targetBox += owner_.frameTimings_.targetBox;
        owner_.frameTimingsAccum_.particles += owner_.frameTimings_.particles;
        owner_.frameTimingsAccum_.boids += owner_.frameTimings_.boids;
        owner_.frameTimingsAccum_.weatherRender += owner_.frameTimings_.weatherRender;
        owner_.frameTimingsAccum_.debugOverlays += owner_.frameTimings_.debugOverlays;
        owner_.frameTimingsAccum_.castingBars += owner_.frameTimings_.castingBars;
        owner_.frameTimingsAccum_.guiDrawAll += owner_.frameTimings_.guiDrawAll;
        owner_.frameTimingsAccum_.staticUI += owner_.frameTimings_.staticUI;
        owner_.frameTimingsAccum_.zoneLineOverlay += owner_.frameTimings_.zoneLineOverlay;
        owner_.frameTimingsAccum_.glFinish += owner_.frameTimings_.glFinish;
        owner_.frameTimingsAccum_.endScene += owner_.frameTimings_.endScene;
        owner_.frameTimingsAccum_.manualZoneRegionsDrawn += owner_.frameTimings_.manualZoneRegionsDrawn;
        owner_.frameTimingsAccum_.manualZoneDrawCalls += owner_.frameTimings_.manualZoneDrawCalls;
        owner_.frameTimingsAccum_.totalFrame += owner_.frameTimings_.totalFrame;
        // New fine-grained fields
        owner_.frameTimingsAccum_.playerMovement += owner_.frameTimings_.playerMovement;
        owner_.frameTimingsAccum_.occlusionCulling += owner_.frameTimings_.occlusionCulling;
        owner_.frameTimingsAccum_.zoneLightVisibility += owner_.frameTimings_.zoneLightVisibility;
        owner_.frameTimingsAccum_.weatherSystemUpdate += owner_.frameTimings_.weatherSystemUpdate;
        owner_.frameTimingsAccum_.footprintRender += owner_.frameTimings_.footprintRender;
        owner_.frameTimingsAccum_.postRender += owner_.frameTimings_.postRender;
        owner_.frameTimingsAccum_.simWorkerApply += owner_.frameTimings_.simWorkerApply;
        owner_.frameTimingsAccum_.drainQueues += owner_.frameTimings_.drainQueues;
        owner_.frameTimingsAccum_.visibility += owner_.frameTimings_.visibility;
        owner_.frameTimingsAccum_.postSimInput += owner_.frameTimings_.postSimInput;
        owner_.frameTimingsAccum_.postLoadWork += owner_.frameTimings_.postLoadWork;
        owner_.frameTimingsAccum_.entityHousekeeping += owner_.frameTimings_.entityHousekeeping;
        owner_.frameTimingsSampleCount_++;

        // Log every 60 frames (~2 seconds at 30fps)
        if (owner_.frameTimingsSampleCount_ >= 60) {
            owner_.Diagnostics().logFrameTimings();
            // Reset accumulators
            owner_.frameTimingsAccum_ = IrrlichtRenderer::FrameTimings();
            owner_.frameTimingsSampleCount_ = 0;
        }
    }

    return true;
}

} // namespace Graphics
} // namespace EQT
