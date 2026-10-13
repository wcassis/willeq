#include "client/graphics/simulation_worker.h"
#include "common/thread_name.h"  // T504 thread names; B1039: Linux + macOS forms
#include "client/graphics/portal_system.h"
#include "client/graphics/eq/wld_loader.h"
#include "client/graphics/environment/unified_particle.h"
#include "client/graphics/environment/spell_particle_types.h"
#include "client/hc_map.h"
#include "common/logging.h"
#include "client/core/memory_ledger.h"
#include <algorithm>
#include <cmath>
#include <random>

namespace EQT {
namespace Graphics {

// ============================================================================
// Constructor / Destructor
// ============================================================================

SimulationWorker::SimulationWorker() = default;

SimulationWorker::~SimulationWorker() {
    stop();
}

// ============================================================================
// Thread Lifecycle
// ============================================================================

void SimulationWorker::start() {
    if (running_.load(std::memory_order_relaxed)) return;

    running_.store(true, std::memory_order_relaxed);
    workReady_.store(false, std::memory_order_relaxed);
    resultReady_.store(false, std::memory_order_relaxed);
    frontIdx_ = 0;
    backIdx_ = 1;

    // Reset debug stats
    {
        std::lock_guard<std::mutex> lock(debugMutex_);
        framesComputed_ = 0;
        framesSkipped_ = 0;
        lastComputeTimeMs_ = 0;
        avgComputeTimeMs_ = 0;
    }

    thread_ = std::make_unique<std::thread>(&SimulationWorker::workerLoop, this);
    LOG_INFO(MOD_GRAPHICS, "SimulationWorker started");
}

void SimulationWorker::stop() {
    if (!running_.load(std::memory_order_relaxed)) return;

    running_.store(false, std::memory_order_relaxed);

    // Wake the worker so it can exit
    {
        std::lock_guard<std::mutex> lock(mutex_);
        workReady_.store(true, std::memory_order_release);
    }
    cv_.notify_one();

    if (thread_ && thread_->joinable()) {
        thread_->join();
    }
    thread_.reset();

    LOG_INFO(MOD_GRAPHICS, "SimulationWorker stopped (computed={}, skipped={})",
             framesComputed_, framesSkipped_);
}

// ============================================================================
// Zone Data Registration
// ============================================================================

void SimulationWorker::setZoneData(const SimulationZoneData& data) {
    setZoneData(SimulationZoneData(data));  // one copy, then the move path
}

void SimulationWorker::setZoneData(SimulationZoneData&& data) {
    // Must be called while worker is stopped
    zoneData_ = std::move(data);
    zoneDataValid_ = true;

    // Pre-allocate output buffers based on zone data sizes
    for (int i = 0; i < 2; ++i) {
        output_[i].regionVisible.resize(zoneData_.regionBounds.size(), 0);
        output_[i].objectVisible.resize(zoneData_.objects.size(), 0);
        output_[i].lightVisible.resize(zoneData_.zoneLights.size(), 0);
        output_[i].objectLightColors.resize(zoneData_.objectLights.size());
        output_[i].valid = false;
    }

    // Capture original base colors for vision/weather recomputation
    zoneLightBaseColors_.resize(zoneData_.zoneLightNodes.size());
    for (size_t i = 0; i < zoneData_.zoneLightNodes.size(); ++i) {
        zoneLightBaseColors_[i] = zoneData_.zoneLightNodes[i].diffuseColor;
    }
    // Reset cached vision/weather to force initial application
    cachedVisionType_ = 255;
    cachedWeatherAmbientModifier_ = -1.0f;

    // Init flicker phases
    flickerPhases_.resize(zoneData_.objectLights.size(), 0.0f);
    for (size_t i = 0; i < flickerPhases_.size(); ++i) {
        // Randomize initial phase so lights don't flicker in sync
        flickerPhases_[i] = static_cast<float>(i) * 1.37f;
    }

    // Initialize worker-owned occlusion culler from zone data
    if (!zoneData_.regionOccluders.empty()) {
        workerOcclusionCuller_ = std::make_unique<SoftwareOcclusionCuller>(zoneData_.occlusionConfig);
        const size_t occluderSets = zoneData_.regionOccluders.regionCount();
        // T504: moved, not copied -- the worker culler owns them now.
        workerOcclusionCuller_->setRegionOccluderTable(std::move(zoneData_.regionOccluders));
        zoneData_.regionOccluders.clear();
        LOG_INFO(MOD_GRAPHICS, "SimulationWorker: occlusion culler initialized with {} region occluder sets",
                 occluderSets);
    }

    LOG_INFO(MOD_GRAPHICS, "SimulationWorker zone data set: {} regions, {} objects, {} zone lights, {} object lights",
             zoneData_.regionBounds.size(), zoneData_.objects.size(),
             zoneData_.zoneLights.size(), zoneData_.objectLights.size());
}

void SimulationWorker::updateTreeData(std::vector<SimulationZoneData::AnimatedTreeData>&& trees) {
    // Called between frames when worker is sleeping (after swapAndGetResults, before postInput)
    zoneData_.trees = std::move(trees);

    // Resize tree shadow buffers in both output buffers
    for (int i = 0; i < 2; ++i) {
        output_[i].treeShadows.resize(zoneData_.trees.size());
        for (size_t t = 0; t < zoneData_.trees.size(); ++t) {
            output_[i].treeShadows[t].resize(zoneData_.trees[t].buffers.size());
            for (size_t b = 0; b < zoneData_.trees[t].buffers.size(); ++b) {
                output_[i].treeShadows[t][b].positions.resize(
                    zoneData_.trees[t].buffers[b].basePositions.size());
                output_[i].treeShadows[t][b].dirty = false;
            }
        }
    }

    LOG_INFO(MOD_GRAPHICS, "SimulationWorker: updated tree data ({} trees)", zoneData_.trees.size());
}

void SimulationWorker::updateVertexAnimData(std::vector<SimulationZoneData::VertexAnimData>&& vertAnims) {
    // Called between frames when worker is sleeping (after swapAndGetResults, before postInput)
    zoneData_.vertexAnims = std::move(vertAnims);

    // Resize vertex anim output buffers and reset state
    vertAnimStates_.resize(zoneData_.vertexAnims.size());
    for (auto& s : vertAnimStates_) {
        s.elapsedMs = 0;
        s.currentFrame = 0;
    }
    for (int i = 0; i < 2; ++i) {
        output_[i].vertexAnims.resize(zoneData_.vertexAnims.size());
    }

    LOG_INFO(MOD_GRAPHICS, "SimulationWorker: updated vertex anim data ({} anims)", zoneData_.vertexAnims.size());
}

void SimulationWorker::updateZoneLightData(std::vector<SimulationZoneData::ZoneLightData>&& lights,
                                            std::vector<SimulationZoneData::ZoneLightNodeData>&& nodes,
                                            std::vector<SimulationZoneData::ZoneLightAnimData>&& anims) {
    // Called between frames when worker is sleeping (after swapAndGetResults, before postInput)
    zoneData_.zoneLights = std::move(lights);
    zoneData_.zoneLightNodes = std::move(nodes);
    zoneData_.zoneLightAnims = std::move(anims);

    // Resize light visibility output buffers
    for (int i = 0; i < 2; ++i) {
        output_[i].lightVisible.resize(zoneData_.zoneLights.size(), 0);
    }

    // Resize light animation state and capture base colors
    lightAnimStates_.resize(zoneData_.zoneLightAnims.size());
    for (auto& s : lightAnimStates_) {
        s.elapsedMs = 0;
        s.currentFrame = 0;
    }
    zoneLightBaseColors_.resize(zoneData_.zoneLightNodes.size());
    for (size_t i = 0; i < zoneData_.zoneLightNodes.size(); ++i) {
        zoneLightBaseColors_[i] = zoneData_.zoneLightNodes[i].diffuseColor;
    }
    // Force vision/weather reapply
    cachedVisionType_ = 255;
    cachedWeatherAmbientModifier_ = -1.0f;

    LOG_INFO(MOD_GRAPHICS, "SimulationWorker: updated zone light data ({} lights, {} nodes, {} anims)",
             zoneData_.zoneLights.size(), zoneData_.zoneLightNodes.size(), zoneData_.zoneLightAnims.size());
}

void SimulationWorker::updateObjectLightData(std::vector<SimulationZoneData::ObjectLightData>&& lights) {
    // Called between frames when worker is sleeping (after swapAndGetResults, before postInput)
    zoneData_.objectLights = std::move(lights);

    // Resize object light output buffers
    for (int i = 0; i < 2; ++i) {
        output_[i].objectLightColors.resize(zoneData_.objectLights.size());
        output_[i].objectLightVisible.resize(zoneData_.objectLights.size(), 0);
    }

    // Resize flicker phases
    flickerPhases_.resize(zoneData_.objectLights.size());
    for (size_t i = 0; i < zoneData_.objectLights.size(); ++i) {
        flickerPhases_[i] = static_cast<float>(i) * 1.3f;  // Staggered phases
    }

    LOG_INFO(MOD_GRAPHICS, "SimulationWorker: updated object light data ({} lights)",
             zoneData_.objectLights.size());
}

void SimulationWorker::clearZoneData() {
    zoneDataValid_ = false;
    zoneData_ = SimulationZoneData();
    zoneLightBaseColors_.clear();
    cachedVisionType_ = 255;
    cachedWeatherAmbientModifier_ = -1.0f;
    cachedDepthMapRegion_ = SIZE_MAX;
    cachedDepthMap_.clear();
    flickerPhases_.clear();
    workerOcclusionCuller_.reset();
    workerEntities_.clear();
    for (int i = 0; i < 2; ++i) {
        output_[i] = SimulationOutput();
    }

    // Reset spell VFX state
    spellVfxEffects_.clear();

    // Reset particle state
    particlePool_.clear();
    particleFreeList_.clear();
    particleActiveCount_ = 0;
    particlePoolInitialized_ = false;
    particleEmitters_.clear();
    particleFireEnabled_ = true;
    particleWeatherEmitterID_ = 0;
    particleSpellEffects_.clear();
    particleNextSpellEffectID_ = 1;
    particleNextEmitterID_ = 1;

    // Reset boids state
    boidsFlocks_.clear();
    boidsSpawnTimer_ = 0.0f;

    // Reset tumbleweed state
    twInstances_.clear();
    twSpawnTimer_ = 0.0f;
    twNextPoolIndex_ = 0;

    // Reset detail wind/disturbance state
    detailChunks_.clear();
    detailWindTime_ = 0;
    detailResiduals_.clear();

    // Reset weather state
    weatherCurrentWeather_ = 1;  // Normal
    weatherTargetWeather_ = 1;
    weatherTransitionProgress_ = 1.0f;
    weatherTimeSinceLastCheck_ = 0.0f;
    weatherCurrentDuration_ = 0.0f;
    weatherCurrentElapsed_ = 0.0f;
    weatherWindIntensity_ = 0.6f;
    weatherSimulationEnabled_ = true;
    weatherZoneConfig_ = ZoneWeatherConfig();
}

void SimulationWorker::computeOnce(const SimulationInput& input, SimulationOutput& output) {
    output = SimulationOutput();
    computeAll(input, output);
}

// ============================================================================
// Per-Frame Protocol
// ============================================================================

void SimulationWorker::postInput(const SimulationInput& input) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        input_ = input;
        workReady_.store(true, std::memory_order_release);
    }
    cv_.notify_one();
}

const SimulationOutput* SimulationWorker::swapAndGetResults() {
    if (resultReady_.load(std::memory_order_acquire)) {
        // Worker finished — swap buffers.
        // P23: frontIdx_/backIdx_ are plain ints (not atomics); this swap
        // (main thread) and workerLoop()'s read of backIdx_ (worker thread)
        // now share mutex_ — see workerLoop() for the fuller writeup of why
        // that read needed a lock at all (it's not just about frontIdx_/
        // backIdx_ visibility — see the notify_one() below for the actual
        // race this closes).
        std::lock_guard<std::mutex> lock(mutex_);
        std::swap(frontIdx_, backIdx_);
        resultReady_.store(false, std::memory_order_release);
        // P23: wake a worker that may already be parked waiting for
        // !resultReady_ (see workerLoop()). Without this, a worker that
        // finished computing into backIdx_ before this swap ran (i.e. it
        // saw workReady_ already true from an earlier postInput() that
        // happened while it was still busy) would sit correctly parked
        // here — but only because the wait predicate now includes
        // !resultReady_. This notify is what lets it wake up promptly once
        // that becomes true, instead of waiting for some unrelated future
        // notify_one() from postInput().
        cv_.notify_one();
        return &output_[frontIdx_];
    }

    // Worker hasn't finished yet — use stale data (graceful degradation).
    // P23: this branch also touches frontIdx_/output_[frontIdx_] and must
    // take the same mutex_ as the swap branch above and workerLoop()'s
    // snapshot — TSAN reproduction (tools/particle_race_stress.cpp) caught
    // a real race here between this read and the worker's writes into
    // output_[backIdx_] during computeAll() when this branch ran unlocked.
    {
        std::lock_guard<std::mutex> lock(debugMutex_);
        framesSkipped_++;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    // Return front buffer if it has valid data from a previous frame
    if (output_[frontIdx_].valid) {
        return &output_[frontIdx_];
    }
    return nullptr;
}

SimulationWorker::DebugInfo SimulationWorker::getDebugInfo() const {
    std::lock_guard<std::mutex> lock(debugMutex_);
    DebugInfo info;
    info.framesComputed = framesComputed_;
    info.framesSkipped = framesSkipped_;
    info.lastComputeTimeMs = lastComputeTimeMs_;
    info.avgComputeTimeMs = avgComputeTimeMs_;
    info.workerBusy = workReady_.load(std::memory_order_relaxed) &&
                      !resultReady_.load(std::memory_order_relaxed);
    return info;
}

// ============================================================================
// Worker Thread Loop
// ============================================================================

void SimulationWorker::workerLoop() {
    eqt::memory::MemoryScope memScope(eqt::memory::Category::IrrlichtScene);  // T491
    eqt::setCurrentThreadName("eq-sim");  // T504
    LOG_DEBUG(MOD_GRAPHICS, "SimulationWorker thread started");

    while (running_.load(std::memory_order_relaxed)) {
        // Wait for work, then snapshot input_ and backIdx_ while still
        // holding mutex_.
        //
        // P23 root-cause has two parts, both stemming from the same
        // assumption: that "one postInput() call always pairs with exactly
        // one prior swapAndGetResults() call" is enough to keep the
        // double-buffered handoff safe. It isn't, because postInput() is
        // called unconditionally once per frame — including frames where
        // the worker hasn't finished (or even started consuming) the
        // previous one — and swapAndGetResults() explicitly degrades
        // gracefully (serves stale front-buffer data) instead of blocking
        // when that happens. Under sustained load where the worker can't
        // keep up with the frame rate (exactly what framesSkipped_ is
        // already tracking as a normal condition, not a bug), both of the
        // following become reachable:
        //
        // (1) input_ read unsynchronized. This used to release the lock as
        //     soon as the cv predicate was satisfied and read
        //     input_/backIdx_ afterwards, unsynchronized. A postInput()
        //     call arriving while this thread was still mid-computeAll()
        //     would reassign input_ (destroying/reallocating its
        //     std::vector/std::unordered_map members) while this thread
        //     iterated those same containers with no lock held — vectors
        //     reallocated out from under an in-progress iterator.
        //
        // (2) backIdx_'s buffer overwritten before being consumed. Because
        //     workReady_ can already be true (from a postInput() that
        //     arrived while this thread was busy) by the time this thread
        //     finishes a compute and loops back here, it could immediately
        //     start a *second* compute into output_[backIdx_] before the
        //     main thread's swapAndGetResults() had actually consumed the
        //     first one (i.e. before resultReady_ was reset to false and
        //     frontIdx_/backIdx_ were swapped). If swapAndGetResults() then
        //     ran concurrently with that second compute, it would swap
        //     frontIdx_ to the very index this thread was mid-write on,
        //     handing the render path a buffer being actively mutated —
        //     the "dangling-pointer-shaped" result-buffer race reported
        //     against ParticleManager::renderUnified() (its renderBuffer
        //     comes straight out of output_[backIdx_].particleOutput).
        //     Waiting for !resultReady_ too (not just workReady_) closes
        //     this: the worker won't start a new compute until the
        //     previous one has actually been swapped out from under it.
        //
        // Both reproduce reliably in tools/particle_race_stress.cpp under
        // ThreadSanitizer (unthrottled postInput()/swapAndGetResults()
        // calls, no frame-rate cap — the condition needed to hit either
        // race on nearly every iteration instead of rarely).
        //
        // Fix: (1) copy input_ into a local, private snapshot under mutex_
        // (the same lock postInput() uses to publish it), then run
        // computeAll() against the stable local copy with no lock held.
        // (2) don't start that compute at all until !resultReady_ — i.e.
        // until the main thread has actually swapped the previous result
        // out. Neither adds a lock around the (expensive) compute itself
        // or around the render path — only around the handoff, mirroring
        // the file's existing double-buffered-output discipline (worker
        // computes privately, publishes atomically) and extending it to
        // cover input_ and the compute-start gate that output_'s
        // double-buffering alone didn't cover.
        SimulationInput localInput;
        int myBackIdx;
        {
            std::unique_lock<std::mutex> lock(mutex_);
            cv_.wait(lock, [this] {
                return (workReady_.load(std::memory_order_relaxed) &&
                        !resultReady_.load(std::memory_order_relaxed)) ||
                       !running_.load(std::memory_order_relaxed);
            });

            if (!running_.load(std::memory_order_relaxed)) break;
            workReady_.store(false, std::memory_order_relaxed);

            localInput = input_;
            myBackIdx = backIdx_;
        }

        if (!zoneDataValid_) continue;

        // Compute
        auto start = std::chrono::steady_clock::now();

        computeAll(localInput, output_[myBackIdx]);
        output_[myBackIdx].valid = true;

        auto end = std::chrono::steady_clock::now();
        float computeMs = std::chrono::duration<float, std::milli>(end - start).count();

        // Update debug stats
        {
            std::lock_guard<std::mutex> lock(debugMutex_);
            framesComputed_++;
            lastComputeTimeMs_ = computeMs;
            // EMA with alpha = 0.1
            avgComputeTimeMs_ = avgComputeTimeMs_ * 0.9f + computeMs * 0.1f;
        }

        // Signal results ready
        resultReady_.store(true, std::memory_order_release);
        FlushThreadLog();
    }

    LOG_DEBUG(MOD_GRAPHICS, "SimulationWorker thread exiting");
    FlushThreadLog();
}

// ============================================================================
// Compute Functions
// ============================================================================

void SimulationWorker::computeAll(const SimulationInput& input, SimulationOutput& output) {
    workerFrameCount_++;

    // Apply vision/weather modifiers to zone light colors when they change
    if (input.visionType != cachedVisionType_ ||
        std::abs(input.weatherAmbientModifier - cachedWeatherAmbientModifier_) > 0.005f) {
        applyVisionWeatherToZoneLights(input.visionType, input.weatherAmbientModifier);
    }

    // Prep layer — always runs (region lookup, mesh load queue, depth map)
    calculateRegions(input, output);

    // Entity state — always runs
    computeEntitySync(input);
    computeEntityPendingUpdates(input);
    computeEntityInterpolation(input, output);

    if (input.loadingActive) {
        // During loading: default all visibility to "visible" so applySimulationResults
        // doesn't hide pre-built nodes
        size_t regionCount = zoneData_.regionBounds.size();
        if (output.regionVisible.size() != regionCount)
            output.regionVisible.resize(regionCount, 1);
        else
            std::fill(output.regionVisible.begin(), output.regionVisible.end(), 1);

        size_t objectCount = zoneData_.objects.size();
        if (output.objectVisible.size() != objectCount)
            output.objectVisible.resize(objectCount, 1);
        else
            std::fill(output.objectVisible.begin(), output.objectVisible.end(), 1);

        size_t lightCount = zoneData_.zoneLights.size();
        if (output.lightVisible.size() != lightCount)
            output.lightVisible.resize(lightCount, 1);
        else
            std::fill(output.lightVisible.begin(), output.lightVisible.end(), 1);

        size_t objLightCount = zoneData_.objectLights.size();
        if (output.objectLightVisible.size() != objLightCount)
            output.objectLightVisible.resize(objLightCount, 1);
        else
            std::fill(output.objectLightVisible.begin(), output.objectLightVisible.end(), 1);

        output.sortedRegions.clear();
        output.portalVisibleRegions.clear();
        output.occlusionCulledRegions.clear();

        for (auto& er : output.entityResults) {
            er.shouldBeVisible = true;
            er.nameTagVisible = false;

            // Compute BSP regions during loading so entities have valid regions
            // for PVS depth gating in queueEntityPrepRequests()
            if (zoneData_.bspTree && (er.bspRegionDirty || er.cachedBspRegion == SIZE_MAX)) {
                er.cachedBspRegion = zoneData_.bspTree->findRegionIndexForPoint(
                    er.posX, er.posY, er.posZ);
                er.bspRegionDirty = false;
            }
        }
        output.entityVisibleCount = static_cast<int>(output.entityResults.size());
    } else {
        // Render layer — culling
        computeVisibility(input, output);
        computeObjectVisibility(input, output);
        computeLightVisibility(input, output);
        computePortalVisibility(input, output);
        computeObjectLightVisibility(input, output);
        computeSoftwareOcclusion(input, output);
        computeEntityVisibility(input, output);
        computeNameTagVisibility(input, output);
    }

    // Light selection — always runs
    computeLightSelection(input, output);

    // Normal tier — every frame: fire flicker, spell VFX, trees, vertex anims, detail wind, particles, boids, tumbleweeds
    computeFireFlicker(input, output);
    computeSpellVFX(input, output);
    computeTreeAnimation(input, output);
    computeVertexAnimations(input, output);
    computeDetailAnimation(input, output);
    computeParticles(input, output);
    computeBoids(input, output);
    computeTumbleweeds(input, output);

    // Background tier — every kBackgroundInterval frames
    if (workerFrameCount_ % kBackgroundInterval == 0) {
        computeWeather(input, output);
        computeLightAnimations(input, output);

        // Apply animation results to zoneData_ so light selection reads animated colors
        for (const auto& alc : output.zoneLightAnimColors) {
            if (alc.updated && alc.lightIndex < zoneData_.zoneLightNodes.size()) {
                zoneData_.zoneLightNodes[alc.lightIndex].diffuseColor =
                    irr::video::SColorf(alc.r, alc.g, alc.b, 1.0f);
            }
        }

        computeSkyState(input, output);
        computeWeatherEffectsState(input, output);
    }
}

} // namespace Graphics
} // namespace EQT
