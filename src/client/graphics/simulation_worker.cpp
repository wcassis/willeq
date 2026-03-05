#include "client/graphics/simulation_worker.h"
#include "client/graphics/portal_system.h"
#include "client/graphics/eq/wld_loader.h"
#include "client/graphics/environment/unified_particle.h"
#include "client/graphics/environment/spell_particle_types.h"
#include "client/hc_map.h"
#include "common/logging.h"
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
    // Must be called while worker is stopped
    zoneData_ = data;
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
        for (const auto& [regionIdx, triangles] : zoneData_.regionOccluders) {
            workerOcclusionCuller_->setRegionOccluders(regionIdx, triangles);
        }
        LOG_INFO(MOD_GRAPHICS, "SimulationWorker: occlusion culler initialized with {} region occluder sets",
                 zoneData_.regionOccluders.size());
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
        // Worker finished — swap buffers
        std::swap(frontIdx_, backIdx_);
        resultReady_.store(false, std::memory_order_release);
        return &output_[frontIdx_];
    }

    // Worker hasn't finished yet — use stale data (graceful degradation)
    {
        std::lock_guard<std::mutex> lock(debugMutex_);
        framesSkipped_++;
    }

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
    LOG_DEBUG(MOD_GRAPHICS, "SimulationWorker thread started");

    while (running_.load(std::memory_order_relaxed)) {
        // Wait for work
        {
            std::unique_lock<std::mutex> lock(mutex_);
            cv_.wait(lock, [this] {
                return workReady_.load(std::memory_order_relaxed) ||
                       !running_.load(std::memory_order_relaxed);
            });

            if (!running_.load(std::memory_order_relaxed)) break;
            workReady_.store(false, std::memory_order_relaxed);
        }

        if (!zoneDataValid_) continue;

        // Compute
        auto start = std::chrono::steady_clock::now();

        computeAll(input_, output_[backIdx_]);
        output_[backIdx_].valid = true;

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
    }

    LOG_DEBUG(MOD_GRAPHICS, "SimulationWorker thread exiting");
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

// ============================================================================
// Region Calculation (prep layer — runs during loading too)
// ============================================================================

void SimulationWorker::calculateRegions(const SimulationInput& input, SimulationOutput& output) {
    if (!zoneData_.bspTree || zoneData_.regionBounds.empty()) return;

    const auto& bsp = *zoneData_.bspTree;
    size_t regionCount = zoneData_.regionBounds.size();

    // BSP lookup — find which region the camera is in (EQ Z-up coords)
    size_t cameraRegion = bsp.findRegionIndexForPoint(input.camEqX, input.camEqY, input.camEqZ);
    output.currentPvsRegion = cameraRegion;

    // Clear prep-layer outputs
    output.meshLoadQueue.clear();
    output.protectedRegions.clear();

    // Get PVS data for current region
    const BspRegion* currentRegion = nullptr;
    if (cameraRegion < bsp.regions.size() && bsp.regions[cameraRegion]) {
        currentRegion = bsp.regions[cameraRegion].get();
    }

    float renderDistSq = input.renderDistance * input.renderDistance;

    for (size_t i = 0; i < regionCount; ++i) {
        const auto& rb = zoneData_.regionBounds[i];
        size_t regionIdx = rb.regionIdx;

        // PVS check
        if (zoneData_.usePvsCulling && currentRegion) {
            if (!currentRegion->visibleRegions.empty() &&
                regionIdx < currentRegion->visibleRegions.size() &&
                !currentRegion->visibleRegions[regionIdx]) {
                continue;
            }
        }

        // Distance culling: nearest point on AABB to camera (EQ coords)
        float nearX = std::max(rb.minX, std::min(input.camEqX, rb.maxX));
        float nearY = std::max(rb.minY, std::min(input.camEqY, rb.maxY));
        float nearZ = std::max(rb.minZ, std::min(input.camEqZ, rb.maxZ));
        float dx = nearX - input.camEqX;
        float dy = nearY - input.camEqY;
        float dz = nearZ - input.camEqZ;
        float distSq = dx*dx + dy*dy + dz*dz;

        if (distSq > renderDistSq) continue;

        // Queue for lazy mesh loading (PVS + distance only — orientation-independent)
        output.meshLoadQueue.push_back({regionIdx, distSq});

        // Track for mesh cache protection (all PVS+distance regions)
        output.protectedRegions.push_back(regionIdx);
    }

    // Sort mesh load queue nearest-first for priority preloading
    std::sort(output.meshLoadQueue.begin(), output.meshLoadQueue.end(),
              [](const auto& a, const auto& b) { return a.distanceSq < b.distanceSq; });

    // Compute PVS depth map (portal adjacency BFS)
    computeRegionDepthMap(input, output);

    // Fallback for outdoor zones / regions not reachable via portals:
    // Assign depth based on Euclidean distance buckets (100 units per depth level)
    for (const auto& sr : output.meshLoadQueue) {
        if (output.regionPvsDepth.count(sr.regionIdx) == 0) {
            float dist = std::sqrt(sr.distanceSq);
            uint8_t depth = static_cast<uint8_t>(std::min(254.0f, dist / 100.0f));
            output.regionPvsDepth[sr.regionIdx] = depth;
        }
    }
}

// ============================================================================
// PVS Visibility Computation (render layer — skipped during loading)
// ============================================================================

void SimulationWorker::computeVisibility(const SimulationInput& input, SimulationOutput& output) {
    if (!zoneData_.bspTree || zoneData_.regionBounds.empty()) return;

    size_t regionCount = zoneData_.regionBounds.size();

    // Ensure output is sized correctly
    if (output.regionVisible.size() != regionCount) {
        output.regionVisible.resize(regionCount, 0);
    }

    // currentPvsRegion already set by calculateRegions()
    output.sortedRegions.clear();

    // Get PVS data for current region
    const auto& bsp = *zoneData_.bspTree;
    const BspRegion* currentRegion = nullptr;
    if (output.currentPvsRegion < bsp.regions.size() && bsp.regions[output.currentPvsRegion]) {
        currentRegion = bsp.regions[output.currentPvsRegion].get();
    }

    float renderDistSq = input.renderDistance * input.renderDistance;

    for (size_t i = 0; i < regionCount; ++i) {
        const auto& rb = zoneData_.regionBounds[i];
        size_t regionIdx = rb.regionIdx;

        // PVS check
        if (zoneData_.usePvsCulling && currentRegion) {
            if (!currentRegion->visibleRegions.empty() &&
                regionIdx < currentRegion->visibleRegions.size() &&
                !currentRegion->visibleRegions[regionIdx]) {
                output.regionVisible[i] = 0;
                continue;
            }
        }

        // Distance culling
        float nearX = std::max(rb.minX, std::min(input.camEqX, rb.maxX));
        float nearY = std::max(rb.minY, std::min(input.camEqY, rb.maxY));
        float nearZ = std::max(rb.minZ, std::min(input.camEqZ, rb.maxZ));
        float dx = nearX - input.camEqX;
        float dy = nearY - input.camEqY;
        float dz = nearZ - input.camEqZ;
        float distSq = dx*dx + dy*dy + dz*dz;

        if (distSq > renderDistSq) {
            output.regionVisible[i] = 0;
            continue;
        }

        // Frustum culling (EQ Z-up coordinates)
        if (input.frustumValid) {
            if (!testFrustumAABB(input.frustumPlanes,
                                  rb.minX, rb.minY, rb.minZ,
                                  rb.maxX, rb.maxY, rb.maxZ)) {
                output.regionVisible[i] = 0;
                continue;
            }
        }

        output.regionVisible[i] = 1;

        // Add to sorted draw list (frustum-visible only)
        output.sortedRegions.push_back({regionIdx, distSq});
    }

    // Sort front-to-back by distance
    std::sort(output.sortedRegions.begin(), output.sortedRegions.end(),
              [](const auto& a, const auto& b) { return a.distanceSq < b.distanceSq; });
}

// ============================================================================
// Portal Visibility Computation (BFS walk from camera room through portals)
// ============================================================================

void SimulationWorker::computePortalVisibility(const SimulationInput& input, SimulationOutput& output) {
    output.portalVisibleRegions.clear();

    const auto* portalSystem = zoneData_.portalSystem;
    if (!portalSystem || !portalSystem->hasPortals() || output.currentPvsRegion == SIZE_MAX) {
        return;
    }

    // Camera's room is always visible
    size_t cameraRegion = output.currentPvsRegion;
    output.portalVisibleRegions.insert(cameraRegion);

    // BFS stack: (regionIdx, depth)
    struct Entry { size_t region; int depth; };
    std::vector<Entry> stack;
    stack.push_back({cameraRegion, 0});

    constexpr int MAX_DEPTH = 1;
    constexpr size_t MAX_VISIBLE_REGIONS = 16;

    float camX = input.camEqX, camY = input.camEqY, camZ = input.camEqZ;

    while (!stack.empty()) {
        auto [fromRegion, depth] = stack.back();
        stack.pop_back();
        if (depth >= MAX_DEPTH) continue;

        const auto& portals = portalSystem->getPortalsForRegion(fromRegion);
        for (size_t portalIdx : portals) {
            size_t toRegion = portalSystem->getOtherRegion(portalIdx, fromRegion);
            if (toRegion == SIZE_MAX) continue;
            if (output.portalVisibleRegions.count(toRegion)) continue;

            const Portal& portal = portalSystem->getData().portals[portalIdx];

            // Skip vertical portals — floor/ceiling AABB overlaps (|normalZ| > 0.7)
            float absNZ = portal.normalZ < 0 ? -portal.normalZ : portal.normalZ;
            if (absNZ > 0.7f) continue;

            // Facing check: is the portal opening facing toward the camera?
            float toPX = portal.centerX - camX;
            float toPY = portal.centerY - camY;
            float toPZ = portal.centerZ - camZ;
            float normalSign = (fromRegion == portal.regionA) ? 1.0f : -1.0f;
            float facingDot = normalSign * (toPX * portal.normalX + toPY * portal.normalY + toPZ * portal.normalZ);
            if (facingDot < 0.0f) continue;

            // Frustum check: is the portal opening visible?
            if (input.frustumValid) {
                float minX = portal.vertices[0][0], maxX = portal.vertices[0][0];
                float minY = portal.vertices[0][1], maxY = portal.vertices[0][1];
                float minZ = portal.vertices[0][2], maxZ = portal.vertices[0][2];
                for (int v = 1; v < 4; ++v) {
                    if (portal.vertices[v][0] < minX) minX = portal.vertices[v][0];
                    if (portal.vertices[v][0] > maxX) maxX = portal.vertices[v][0];
                    if (portal.vertices[v][1] < minY) minY = portal.vertices[v][1];
                    if (portal.vertices[v][1] > maxY) maxY = portal.vertices[v][1];
                    if (portal.vertices[v][2] < minZ) minZ = portal.vertices[v][2];
                    if (portal.vertices[v][2] > maxZ) maxZ = portal.vertices[v][2];
                }
                if (!testFrustumAABB(input.frustumPlanes, minX, minY, minZ, maxX, maxY, maxZ)) {
                    continue;
                }
            }

            output.portalVisibleRegions.insert(toRegion);

            // Bail if too many regions — open area, portal culling won't help
            if (output.portalVisibleRegions.size() > MAX_VISIBLE_REGIONS) {
                output.portalVisibleRegions.clear();
                return;
            }

            stack.push_back({toRegion, depth + 1});
        }
    }
}

// ============================================================================
// PVS Depth Map (pure adjacency BFS, no frustum/facing checks)
// ============================================================================

void SimulationWorker::computeRegionDepthMap(const SimulationInput& input, SimulationOutput& output) {
    output.regionPvsDepth.clear();
    const auto* portalSystem = zoneData_.portalSystem;
    if (!portalSystem || !portalSystem->hasPortals() || output.currentPvsRegion == SIZE_MAX)
        return;

    // Cache: skip BFS if region unchanged from previous frame
    if (output.currentPvsRegion == cachedDepthMapRegion_ && !cachedDepthMap_.empty()) {
        output.regionPvsDepth = cachedDepthMap_;
        return;
    }

    size_t cameraRegion = output.currentPvsRegion;
    output.regionPvsDepth[cameraRegion] = 0;

    struct Entry { size_t region; uint8_t depth; };
    std::vector<Entry> queue;
    queue.push_back({cameraRegion, 0});
    size_t head = 0;

    constexpr uint8_t MAX_DEPTH = 8;

    while (head < queue.size()) {
        auto [fromRegion, depth] = queue[head++];
        if (depth >= MAX_DEPTH) continue;

        const auto& portals = portalSystem->getPortalsForRegion(fromRegion);
        for (size_t portalIdx : portals) {
            size_t toRegion = portalSystem->getOtherRegion(portalIdx, fromRegion);
            if (toRegion == SIZE_MAX) continue;
            if (output.regionPvsDepth.count(toRegion)) continue;

            uint8_t newDepth = depth + 1;
            output.regionPvsDepth[toRegion] = newDepth;
            queue.push_back({toRegion, newDepth});
        }
    }

    // Cache for next frame
    cachedDepthMapRegion_ = cameraRegion;
    cachedDepthMap_ = output.regionPvsDepth;
}

// ============================================================================
// Object Visibility Computation
// ============================================================================

void SimulationWorker::computeObjectVisibility(const SimulationInput& input, SimulationOutput& output) {
    size_t objectCount = zoneData_.objects.size();
    if (objectCount == 0) return;

    if (output.objectVisible.size() != objectCount) {
        output.objectVisible.resize(objectCount, 0);
    }

    float renderDistSq = input.renderDistance * input.renderDistance;

    // Camera position in Irrlicht Y-up for AABB distance checks
    const auto& camPos = input.cameraPos;

    for (size_t i = 0; i < objectCount; ++i) {
        const auto& obj = zoneData_.objects[i];
        if (!obj.hasNode) {
            output.objectVisible[i] = 0;
            continue;
        }

        // Distance culling: nearest point on AABB to camera (Irrlicht Y-up)
        const auto& bb = obj.boundingBox;
        float nearX = std::max(bb.MinEdge.X, std::min(camPos.X, bb.MaxEdge.X));
        float nearY = std::max(bb.MinEdge.Y, std::min(camPos.Y, bb.MaxEdge.Y));
        float nearZ = std::max(bb.MinEdge.Z, std::min(camPos.Z, bb.MaxEdge.Z));
        float dx = nearX - camPos.X;
        float dy = nearY - camPos.Y;
        float dz = nearZ - camPos.Z;
        float distSq = dx*dx + dy*dy + dz*dz;

        if (distSq > renderDistSq) {
            output.objectVisible[i] = 0;
            continue;
        }

        // PVS check: if object has a known BSP region, check PVS visibility
        if (zoneData_.usePvsCulling && zoneData_.bspTree &&
            obj.bspRegion != SIZE_MAX && output.currentPvsRegion != SIZE_MAX) {
            const auto& regions = zoneData_.bspTree->regions;
            if (output.currentPvsRegion < regions.size() && regions[output.currentPvsRegion]) {
                const auto& pvs = regions[output.currentPvsRegion]->visibleRegions;
                if (!pvs.empty() && obj.bspRegion < pvs.size() && !pvs[obj.bspRegion]) {
                    output.objectVisible[i] = 0;
                    continue;
                }
            }
        }

        // Frustum culling (convert AABB to EQ Z-up for frustum test)
        // Irrlicht (X,Y,Z) → EQ (X,Z,Y): swap Y and Z
        if (input.frustumValid) {
            if (!testFrustumAABB(input.frustumPlanes,
                                  bb.MinEdge.X, bb.MinEdge.Z, bb.MinEdge.Y,
                                  bb.MaxEdge.X, bb.MaxEdge.Z, bb.MaxEdge.Y)) {
                output.objectVisible[i] = 0;
                continue;
            }
        }

        output.objectVisible[i] = 1;
    }
}

// ============================================================================
// Zone Light Visibility Computation
// ============================================================================

void SimulationWorker::computeLightVisibility(const SimulationInput& input, SimulationOutput& output) {
    size_t lightCount = zoneData_.zoneLights.size();
    if (lightCount == 0) return;

    if (output.lightVisible.size() != lightCount) {
        output.lightVisible.resize(lightCount, 0);
    }

    float renderDistSq = input.renderDistance * input.renderDistance;
    const auto& camPos = input.cameraPos;

    for (size_t i = 0; i < lightCount; ++i) {
        const auto& light = zoneData_.zoneLights[i];

        // Distance culling (Irrlicht Y-up)
        float dx = light.position.X - camPos.X;
        float dy = light.position.Y - camPos.Y;
        float dz = light.position.Z - camPos.Z;
        float distSq = dx*dx + dy*dy + dz*dz;

        if (distSq > renderDistSq) {
            output.lightVisible[i] = 0;
            continue;
        }

        // PVS check
        if (zoneData_.usePvsCulling && zoneData_.bspTree &&
            light.bspRegion != SIZE_MAX && output.currentPvsRegion != SIZE_MAX) {
            const auto& regions = zoneData_.bspTree->regions;
            if (output.currentPvsRegion < regions.size() && regions[output.currentPvsRegion]) {
                const auto& pvs = regions[output.currentPvsRegion]->visibleRegions;
                if (!pvs.empty() && light.bspRegion < pvs.size() && !pvs[light.bspRegion]) {
                    output.lightVisible[i] = 0;
                    continue;
                }
            }
        }

        // Frustum culling: convert Irrlicht Y-up position to EQ Z-up
        if (input.frustumValid) {
            // Treat light as a small sphere (radius ~30 units) for frustum test
            float eqX = light.position.X;
            float eqY = light.position.Z;  // Irrlicht Z → EQ Y
            float eqZ = light.position.Y;  // Irrlicht Y → EQ Z
            float testRadius = 30.0f;
            if (!testFrustumAABB(input.frustumPlanes,
                                  eqX - testRadius, eqY - testRadius, eqZ - testRadius,
                                  eqX + testRadius, eqY + testRadius, eqZ + testRadius)) {
                output.lightVisible[i] = 0;
                continue;
            }
        }

        output.lightVisible[i] = 1;
    }
}

// ============================================================================
// Object Light Visibility Computation
// ============================================================================

void SimulationWorker::computeObjectLightVisibility(const SimulationInput& input, SimulationOutput& output) {
    size_t lightCount = zoneData_.objectLights.size();
    if (lightCount == 0) return;

    if (output.objectLightVisible.size() != lightCount) {
        output.objectLightVisible.resize(lightCount, 0);
    }

    float renderDistSq = input.renderDistance * input.renderDistance;
    const auto& camPos = input.cameraPos;

    for (size_t i = 0; i < lightCount; ++i) {
        const auto& light = zoneData_.objectLights[i];

        // Distance culling (Irrlicht Y-up)
        float dx = light.position.X - camPos.X;
        float dy = light.position.Y - camPos.Y;
        float dz = light.position.Z - camPos.Z;
        float distSq = dx*dx + dy*dy + dz*dz;

        if (distSq > renderDistSq) {
            output.objectLightVisible[i] = 0;
            continue;
        }

        // PVS check
        if (zoneData_.usePvsCulling && zoneData_.bspTree &&
            light.bspRegion != SIZE_MAX && output.currentPvsRegion != SIZE_MAX) {
            const auto& regions = zoneData_.bspTree->regions;
            if (output.currentPvsRegion < regions.size() && regions[output.currentPvsRegion]) {
                const auto& pvs = regions[output.currentPvsRegion]->visibleRegions;
                if (!pvs.empty() && light.bspRegion < pvs.size() && !pvs[light.bspRegion]) {
                    output.objectLightVisible[i] = 0;
                    continue;
                }
            }
        }

        // Portal culling
        if (!output.portalVisibleRegions.empty() && light.bspRegion != SIZE_MAX) {
            if (output.portalVisibleRegions.find(light.bspRegion) == output.portalVisibleRegions.end()) {
                output.objectLightVisible[i] = 0;
                continue;
            }
        }

        // Frustum culling: convert Irrlicht Y-up position to EQ Z-up
        if (input.frustumValid) {
            float eqX = light.position.X;
            float eqY = light.position.Z;  // Irrlicht Z → EQ Y
            float eqZ = light.position.Y;  // Irrlicht Y → EQ Z
            float testRadius = 30.0f;
            if (!testFrustumAABB(input.frustumPlanes,
                                  eqX - testRadius, eqY - testRadius, eqZ - testRadius,
                                  eqX + testRadius, eqY + testRadius, eqZ + testRadius)) {
                output.objectLightVisible[i] = 0;
                continue;
            }
        }

        output.objectLightVisible[i] = 1;
    }
}

// ============================================================================
// Light Selection (Top 8)
// ============================================================================

void SimulationWorker::computeLightSelection(const SimulationInput& input, SimulationOutput& output) {
    // Reset selection
    output.activeLightCount = 0;
    for (auto& sl : output.selectedLights) {
        sl.valid = false;
    }

    struct LightCandidate {
        float distance;
        size_t index;
        bool isZoneLight;    // true = zone light, false = object light
        bool isPlayerLight;
    };
    std::vector<LightCandidate> candidates;

    // Player position in Irrlicht Y-up for distance calculations
    irr::core::vector3df playerPosIrr(input.playerX, input.playerZ, input.playerY);

    // Player light always gets slot 0 if active
    if (input.playerLightLevel > 0) {
        candidates.push_back({0.0f, 0, false, true});
    }

    // Zone lights — use horizontal distance (ignore Y)
    for (size_t i = 0; i < zoneData_.zoneLightNodes.size(); ++i) {
        // Skip lights that aren't visible
        if (i < output.lightVisible.size() && !output.lightVisible[i]) continue;

        const auto& zl = zoneData_.zoneLightNodes[i];
        float dx = zl.position.X - playerPosIrr.X;
        float dz = zl.position.Z - playerPosIrr.Z;
        float distHoriz = std::sqrt(dx*dx + dz*dz);

        // Zone light range threshold: skip if too far
        if (distHoriz > input.renderDistance) continue;

        candidates.push_back({distHoriz, i, true, false});
    }

    // Object lights
    for (size_t i = 0; i < zoneData_.objectLights.size(); ++i) {
        // Skip lights that aren't PVS-visible
        if (i < output.objectLightVisible.size() && !output.objectLightVisible[i]) continue;

        const auto& ol = zoneData_.objectLights[i];
        float dx = ol.position.X - playerPosIrr.X;
        float dy = ol.position.Y - playerPosIrr.Y;
        float dz = ol.position.Z - playerPosIrr.Z;
        float dist = std::sqrt(dx*dx + dy*dy + dz*dz);

        if (dist > input.renderDistance) continue;

        candidates.push_back({dist, i, false, false});
    }

    // Sort by distance (player light always first at distance 0)
    std::sort(candidates.begin(), candidates.end(),
              [](const auto& a, const auto& b) { return a.distance < b.distance; });

    // Take top 8
    int count = 0;
    for (const auto& c : candidates) {
        if (count >= 8) break;

        auto& sl = output.selectedLights[count];

        if (c.isPlayerLight) {
            // Player light at current player position (elevated slightly)
            sl.position = irr::core::vector3df(input.playerX, input.playerZ + 3.0f, input.playerY);
            sl.isPlayerLight = true;
            sl.isZoneLight = false;
            sl.valid = true;
            sl.sourceIndex = SIZE_MAX;
        } else if (c.isZoneLight) {
            const auto& zl = zoneData_.zoneLightNodes[c.index];
            sl.position = zl.position;
            sl.diffuseColor = zl.diffuseColor;
            sl.originalColor = zl.diffuseColor;
            sl.radius = zl.radius;
            sl.attConstant = zl.attConstant;
            sl.attLinear = zl.attLinear;
            sl.attQuadratic = zl.attQuadratic;
            sl.isZoneLight = true;
            sl.isPlayerLight = false;
            sl.valid = true;
            sl.sourceIndex = c.index;
        } else {
            const auto& ol = zoneData_.objectLights[c.index];
            sl.position = ol.position;
            sl.originalColor = ol.originalColor;
            sl.diffuseColor = ol.originalColor;  // Will be updated by fire flicker
            sl.radius = ol.radius;
            sl.attConstant = ol.attConstant;
            sl.attLinear = ol.attLinear;
            sl.attQuadratic = ol.attQuadratic;
            sl.isZoneLight = false;
            sl.isPlayerLight = false;
            sl.valid = true;
            sl.sourceIndex = c.index;
        }

        count++;
    }
    output.activeLightCount = count;
}

// ============================================================================
// Fire Light Flicker
// ============================================================================

void SimulationWorker::computeFireFlicker(const SimulationInput& input, SimulationOutput& output) {
    size_t lightCount = zoneData_.objectLights.size();
    if (lightCount == 0) return;

    if (output.objectLightColors.size() != lightCount) {
        output.objectLightColors.resize(lightCount);
    }
    if (flickerPhases_.size() != lightCount) {
        flickerPhases_.resize(lightCount, 0.0f);
    }

    for (size_t i = 0; i < lightCount; ++i) {
        const auto& ol = zoneData_.objectLights[i];

        float r = ol.originalColor.r;
        float g = ol.originalColor.g;
        float b = ol.originalColor.b;

        if (ol.isFireSource) {
            // Advance flicker phase
            flickerPhases_[i] += input.deltaTime * ol.flickerSpeed;

            // Fire flicker formula: base + two sine waves at different frequencies
            float phase = flickerPhases_[i];
            float flicker = 0.85f + 0.10f * std::sin(phase * 6.7f) + 0.05f * std::sin(phase * 13.1f);

            r *= flicker;
            g *= flicker;
            b *= flicker;
        }

        output.objectLightColors[i].r = r;
        output.objectLightColors[i].g = g;
        output.objectLightColors[i].b = b;
    }

    // Also update flicker colors in selected lights
    for (int i = 0; i < output.activeLightCount; ++i) {
        auto& sl = output.selectedLights[i];
        if (!sl.valid || sl.isZoneLight || sl.isPlayerLight) continue;
        if (sl.sourceIndex < lightCount) {
            const auto& fc = output.objectLightColors[sl.sourceIndex];
            sl.diffuseColor.r = fc.r;
            sl.diffuseColor.g = fc.g;
            sl.diffuseColor.b = fc.b;
        }
    }
}

// ============================================================================
// Tree Wind Animation
// ============================================================================

irr::core::vector3df SimulationWorker::computeTreeWindDisplacement(
    const irr::core::vector3df& worldPos, float normalizedHeight,
    float meshSeed, const SimulationInput::TreeWindState& wind) const
{
    if (!wind.enabled || wind.weatherMultiplier < 0.001f) {
        return irr::core::vector3df(0, 0, 0);
    }

    // Height-based influence (power curve from influenceStartHeight to 1.0)
    if (normalizedHeight <= wind.influenceStartHeight) {
        return irr::core::vector3df(0, 0, 0);
    }
    float heightRange = 1.0f - wind.influenceStartHeight;
    if (heightRange <= 0.0f) return irr::core::vector3df(0, 0, 0);
    float heightFactor = (std::min(normalizedHeight, 1.0f) - wind.influenceStartHeight) / heightRange;
    float influence = std::pow(heightFactor, wind.influenceExponent);
    if (influence < 0.001f) return irr::core::vector3df(0, 0, 0);

    constexpr float TWO_PI = 6.28318f;

    // Primary sway
    float primaryPhase = wind.time * wind.baseFrequency * TWO_PI + meshSeed;
    float primarySway = std::sin(primaryPhase) * wind.baseStrength;

    // Secondary gust
    float gustPhase = wind.time * wind.gustFrequency * TWO_PI + meshSeed * 1.7f;
    float gustSway = std::sin(gustPhase * 3.0f + std::sin(gustPhase)) * wind.gustStrength;

    // Turbulence
    float turbX = std::sin(wind.time * 5.0f + worldPos.X * 0.1f + meshSeed) * wind.turbulence;
    float turbZ = std::cos(wind.time * 4.3f + worldPos.Z * 0.1f + meshSeed) * wind.turbulence;

    float totalStrength = (primarySway + gustSway) * wind.weatherMultiplier * influence;

    float dispX = totalStrength * wind.windDirX + turbX * influence * wind.weatherMultiplier;
    float dispY = 0.0f;
    float dispZ = totalStrength * 0.7f * wind.windDirY + turbZ * influence * wind.weatherMultiplier;

    return irr::core::vector3df(dispX, dispY, dispZ);
}

void SimulationWorker::computeTreeAnimation(const SimulationInput& input, SimulationOutput& output) {
    if (zoneData_.trees.empty() || !input.treeWind.enabled) return;

    // Ensure shadow buffers are allocated
    if (output.treeShadows.size() != zoneData_.trees.size()) {
        output.treeShadows.resize(zoneData_.trees.size());
        for (size_t t = 0; t < zoneData_.trees.size(); ++t) {
            output.treeShadows[t].resize(zoneData_.trees[t].buffers.size());
            for (size_t b = 0; b < zoneData_.trees[t].buffers.size(); ++b) {
                output.treeShadows[t][b].positions.resize(
                    zoneData_.trees[t].buffers[b].basePositions.size());
            }
        }
    }

    // Distance culling threshold for trees
    float treeRenderDistSq = input.renderDistance * input.renderDistance;

    for (size_t t = 0; t < zoneData_.trees.size(); ++t) {
        const auto& tree = zoneData_.trees[t];

        // Distance cull (Irrlicht Y-up)
        float dx = tree.worldPosition.X - input.cameraPos.X;
        float dy = tree.worldPosition.Y - input.cameraPos.Y;
        float dz = tree.worldPosition.Z - input.cameraPos.Z;
        if (dx*dx + dy*dy + dz*dz > treeRenderDistSq) {
            for (auto& bs : output.treeShadows[t]) bs.dirty = false;
            continue;
        }

        for (size_t b = 0; b < tree.buffers.size(); ++b) {
            const auto& buf = tree.buffers[b];
            auto& shadow = output.treeShadows[t][b];

            for (size_t v = 0; v < buf.basePositions.size(); ++v) {
                const auto& basePos = buf.basePositions[v];
                irr::core::vector3df worldPos = basePos + tree.worldPosition;
                irr::core::vector3df displacement = computeTreeWindDisplacement(
                    worldPos, buf.vertexHeights[v], tree.meshSeed, input.treeWind);
                shadow.positions[v] = basePos + displacement;
            }
            shadow.dirty = true;
        }
    }
}

// ============================================================================
// Vertex Animations (flags, banners)
// ============================================================================

void SimulationWorker::computeVertexAnimations(const SimulationInput& input, SimulationOutput& output) {
    if (zoneData_.vertexAnims.empty()) return;

    // Ensure state and output vectors
    if (vertAnimStates_.size() != zoneData_.vertexAnims.size()) {
        vertAnimStates_.resize(zoneData_.vertexAnims.size());
    }
    if (output.vertexAnims.size() != zoneData_.vertexAnims.size()) {
        output.vertexAnims.resize(zoneData_.vertexAnims.size());
    }

    float deltaMs = input.vertAnimDeltaMs;

    for (size_t i = 0; i < zoneData_.vertexAnims.size(); ++i) {
        const auto& vad = zoneData_.vertexAnims[i];
        auto& state = vertAnimStates_[i];
        auto& result = output.vertexAnims[i];

        if (vad.frameCount == 0 || vad.delayMs <= 0) {
            result.frameChanged = false;
            continue;
        }

        state.elapsedMs += deltaMs;
        if (state.elapsedMs >= static_cast<float>(vad.delayMs)) {
            state.elapsedMs = std::fmod(state.elapsedMs, static_cast<float>(vad.delayMs));
            state.currentFrame = (state.currentFrame + 1) % static_cast<int>(vad.frameCount);
            result.currentFrame = state.currentFrame;
            result.frameChanged = true;
        } else {
            result.currentFrame = state.currentFrame;
            result.frameChanged = false;
        }
    }
}

// ============================================================================
// Detail Wind/Disturbance Animation
// ============================================================================

void SimulationWorker::computeDetailAnimation(const SimulationInput& input, SimulationOutput& output) {
    const auto& di = input.detailInput;
    if (!di.initialized) {
        output.detailOutput.valid = false;
        return;
    }

    // Process commands
    for (const auto& cmd : di.commands) {
        switch (cmd.type) {
            case DetailCommand::AddChunk: {
                // Upsert: find existing chunk by key or add new
                bool found = false;
                for (auto& chunk : detailChunks_) {
                    if (chunk.keyX == cmd.chunkKeyX && chunk.keyZ == cmd.chunkKeyZ) {
                        chunk.basePositions = cmd.basePositions;
                        chunk.windInfluence = cmd.windInfluence;
                        found = true;
                        break;
                    }
                }
                if (!found) {
                    WorkerDetailChunk wdc;
                    wdc.keyX = cmd.chunkKeyX;
                    wdc.keyZ = cmd.chunkKeyZ;
                    wdc.basePositions = cmd.basePositions;
                    wdc.windInfluence = cmd.windInfluence;
                    detailChunks_.push_back(std::move(wdc));
                }
                break;
            }
            case DetailCommand::RemoveChunk: {
                detailChunks_.erase(
                    std::remove_if(detailChunks_.begin(), detailChunks_.end(),
                        [&cmd](const WorkerDetailChunk& c) {
                            return c.keyX == cmd.chunkKeyX && c.keyZ == cmd.chunkKeyZ;
                        }),
                    detailChunks_.end());
                break;
            }
            case DetailCommand::ClearAll:
                detailChunks_.clear();
                detailResiduals_.clear();
                break;
        }
    }

    if (detailChunks_.empty()) {
        output.detailOutput.chunkShadows.clear();
        output.detailOutput.valid = true;
        return;
    }

    // Advance wind time
    detailWindTime_ += di.deltaTime;

    // Update disturbance residuals
    if (di.disturbanceEnabled) {
        // Add player as source if moving
        if (di.playerMoving) {
            // Grid key for player position (Irrlicht Y-up: X, Y=up, Z=horizontal)
            int32_t gx = static_cast<int32_t>(std::floor(di.playerPosX / 1.0f));
            int32_t gz = static_cast<int32_t>(std::floor(di.playerPosZ / 1.0f));
            int64_t key = (static_cast<int64_t>(gx) << 32) | static_cast<uint32_t>(gz);

            // Push direction from velocity
            float velLen = std::sqrt(di.playerVelX * di.playerVelX + di.playerVelZ * di.playerVelZ);
            float pushDirX = 0, pushDirZ = 1.0f;
            if (velLen > 0.01f) {
                pushDirX = di.playerVelX / velLen;
                pushDirZ = di.playerVelZ / velLen;
            }

            auto it = detailResiduals_.find(key);
            if (it != detailResiduals_.end()) {
                it->second.posX = di.playerPosX;
                it->second.posY = di.playerPosY;
                it->second.posZ = di.playerPosZ;
                it->second.dirX = pushDirX;
                it->second.dirZ = pushDirZ;
                it->second.intensity = std::max(it->second.intensity, di.playerStrength);
            } else if (detailResiduals_.size() < 500) {
                WorkerResidualDisturbance r;
                r.posX = di.playerPosX;
                r.posY = di.playerPosY;
                r.posZ = di.playerPosZ;
                r.dirX = pushDirX;
                r.dirZ = pushDirZ;
                r.intensity = di.playerStrength;
                detailResiduals_[key] = r;
            }
        }

        // Fade residuals
        std::vector<int64_t> toRemove;
        for (auto& [key, res] : detailResiduals_) {
            res.intensity -= di.recoveryRate * di.deltaTime;
            if (res.intensity <= 0.0f) {
                toRemove.push_back(key);
            }
        }
        for (int64_t key : toRemove) {
            detailResiduals_.erase(key);
        }
    }

    // Ensure output has correct number of shadow entries
    output.detailOutput.chunkShadows.resize(detailChunks_.size());

    constexpr float TWO_PI = 6.28318f;

    for (size_t ci = 0; ci < detailChunks_.size(); ++ci) {
        const auto& chunk = detailChunks_[ci];
        auto& shadow = output.detailOutput.chunkShadows[ci];
        shadow.keyX = chunk.keyX;
        shadow.keyZ = chunk.keyZ;

        size_t vertCount = chunk.basePositions.size();
        if (vertCount == 0 || chunk.windInfluence.size() != vertCount) {
            shadow.positions.clear();
            shadow.dirty = false;
            continue;
        }

        shadow.positions.resize(vertCount);

        for (size_t v = 0; v < vertCount; ++v) {
            const auto& basePos = chunk.basePositions[v];
            float influence = chunk.windInfluence[v];

            if (influence < 0.001f) {
                shadow.positions[v] = basePos;
                continue;
            }

            // Wind displacement (replicates WindController::getDisplacement)
            irr::core::vector3df windDisp(0, 0, 0);
            if (di.windStrength > 0.001f) {
                float spatialPhase = (basePos.X * 0.1f + basePos.Z * 0.13f);
                float baseWave = std::sin(detailWindTime_ * di.windFrequency * TWO_PI + spatialPhase);
                float gustWave = std::sin(detailWindTime_ * di.gustFrequency * TWO_PI +
                                          spatialPhase * 0.3f) * di.gustStrength;
                float wave = (baseWave + gustWave) * di.windStrength * influence;
                float heightInfluence = influence * influence;
                wave *= heightInfluence;
                windDisp.X = wave * di.windDirX * 0.15f;
                windDisp.Y = -std::abs(wave) * 0.02f;
                windDisp.Z = wave * di.windDirY * 0.15f;
            }

            // Disturbance displacement
            irr::core::vector3df disturbDisp(0, 0, 0);
            if (di.disturbanceEnabled) {
                float heightScale = std::pow(influence, di.heightExponent);

                // Active source (player) push
                if (di.playerMoving) {
                    float dx = basePos.X - di.playerPosX;
                    float dz = basePos.Z - di.playerPosZ;
                    float dist = std::sqrt(dx * dx + dz * dz);
                    if (dist > 0.01f && dist < di.playerRadius) {
                        float falloff = 1.0f - (dist / di.playerRadius);
                        falloff = falloff * falloff;
                        // Push away from player
                        float pushDirX = dx / dist;
                        float pushDirZ = dz / dist;
                        // Blend with velocity direction
                        float velLen = std::sqrt(di.playerVelX * di.playerVelX + di.playerVelZ * di.playerVelZ);
                        if (velLen > 0.01f) {
                            float velDirX = di.playerVelX / velLen;
                            float velDirZ = di.playerVelZ / velLen;
                            pushDirX += velDirX * di.velocityInfluence;
                            pushDirZ += velDirZ * di.velocityInfluence;
                            float pLen = std::sqrt(pushDirX * pushDirX + pushDirZ * pushDirZ);
                            if (pLen > 0.01f) { pushDirX /= pLen; pushDirZ /= pLen; }
                        }
                        float disp = falloff * di.playerStrength * heightScale;
                        disturbDisp.X += pushDirX * disp * di.maxDisplacement;
                        disturbDisp.Z += pushDirZ * disp * di.maxDisplacement;
                        disturbDisp.Y -= disp * di.verticalDipFactor;
                    }
                }

                // Residual push
                float residualRadius = di.playerRadius * 0.8f;
                for (const auto& [rkey, res] : detailResiduals_) {
                    if (res.intensity < 0.01f) continue;
                    float dx = basePos.X - res.posX;
                    float dz = basePos.Z - res.posZ;
                    float dist = std::sqrt(dx * dx + dz * dz);
                    if (dist >= residualRadius) continue;
                    float falloff = 1.0f - (dist / residualRadius);
                    falloff = falloff * falloff;
                    float disp = falloff * res.intensity * heightScale;
                    disturbDisp.X += res.dirX * disp * di.maxDisplacement;
                    disturbDisp.Z += res.dirZ * disp * di.maxDisplacement;
                    disturbDisp.Y -= disp * di.verticalDipFactor;
                }
            }

            shadow.positions[v] = basePos + windDisp + disturbDisp;
        }
        shadow.dirty = true;
    }

    output.detailOutput.valid = true;
}

// ============================================================================
// Frustum Test Helper
// ============================================================================

bool SimulationWorker::testFrustumAABB(const float planes[6][4],
                                        float minX, float minY, float minZ,
                                        float maxX, float maxY, float maxZ) const {
    // For each plane, find the positive vertex (most aligned with plane normal)
    // If positive vertex is behind the plane, the AABB is entirely outside
    for (int i = 0; i < 6; ++i) {
        float nx = planes[i][0], ny = planes[i][1], nz = planes[i][2], d = planes[i][3];

        // Pick the corner of the AABB most in the direction of the plane normal
        float px = (nx >= 0) ? maxX : minX;
        float py = (ny >= 0) ? maxY : minY;
        float pz = (nz >= 0) ? maxZ : minZ;

        if (nx * px + ny * py + nz * pz + d < 0) {
            return false;  // Entirely outside this plane
        }
    }
    return true;  // At least partially inside all planes
}

// ============================================================================
// Vision/Weather Zone Light Color Application
// ============================================================================

void SimulationWorker::applyVisionWeatherToZoneLights(uint8_t visionType, float weatherAmbientModifier) {
    cachedVisionType_ = visionType;
    cachedWeatherAmbientModifier_ = weatherAmbientModifier;

    if (zoneLightBaseColors_.size() != zoneData_.zoneLightNodes.size()) return;

    // Compute vision-based intensity and red shift (same logic as old updateZoneLightColors)
    float intensity = 0.25f;
    float redShift = 0.0f;
    switch (visionType) {
        case 1: // Ultravision
            intensity = 1.0f; break;
        case 2: // Infravision
            intensity = 0.75f; redShift = 0.3f; break;
        default: break;
    }
    intensity *= weatherAmbientModifier;

    // Build set of animated light indices (skip those — computeLightAnimations handles them)
    std::unordered_set<size_t> animatedLights;
    for (const auto& anim : zoneData_.zoneLightAnims) {
        animatedLights.insert(anim.lightIndex);
    }

    // Apply to all non-animated zone lights using original base colors
    for (size_t i = 0; i < zoneData_.zoneLightNodes.size(); ++i) {
        if (animatedLights.count(i)) continue;

        const auto& base = zoneLightBaseColors_[i];
        float r = base.r * intensity;
        float g = base.g * intensity * (1.0f - redShift * 0.5f);
        float b = base.b * intensity * (1.0f - redShift);
        if (redShift > 0.0f) {
            r = std::min(1.0f, r * (1.0f + redShift));
        }
        zoneData_.zoneLightNodes[i].diffuseColor = irr::video::SColorf(r, g, b, 1.0f);
    }
}

// ============================================================================
// Zone Light Animation Computation
// ============================================================================

void SimulationWorker::computeLightAnimations(const SimulationInput& input, SimulationOutput& output) {
    if (zoneData_.zoneLightAnims.empty()) return;

    const size_t animCount = zoneData_.zoneLightAnims.size();

    // Initialize state vectors on first run
    if (lightAnimStates_.size() != animCount) {
        lightAnimStates_.resize(animCount);
    }
    output.zoneLightAnimColors.resize(animCount);

    // Compute vision/weather modifiers
    float intensity = 0.25f;
    float redShift = 0.0f;
    switch (input.visionType) {
        case 1: // Ultravision
            intensity = 1.0f; break;
        case 2: // Infravision
            intensity = 0.75f; redShift = 0.3f; break;
        default: break;
    }
    intensity *= input.weatherAmbientModifier;

    float deltaMs = input.deltaTime * 1000.0f;

    for (size_t i = 0; i < animCount; ++i) {
        const auto& anim = zoneData_.zoneLightAnims[i];
        auto& state = lightAnimStates_[i];
        auto& out = output.zoneLightAnimColors[i];

        out.lightIndex = anim.lightIndex;
        out.updated = false;

        // Advance elapsed time
        state.elapsedMs += deltaMs;
        float sleepMs = static_cast<float>(anim.sleepMs);
        if (sleepMs <= 0.0f) sleepMs = 100.0f;

        if (state.elapsedMs < sleepMs) {
            // No frame change — still output current color
            continue;
        }

        // Advance frame(s), consuming elapsed time
        while (state.elapsedMs >= sleepMs) {
            state.elapsedMs -= sleepMs;
            state.currentFrame = (state.currentFrame + 1) % anim.frameCount;
        }
        out.updated = true;

        uint32_t frame = state.currentFrame;

        // Compute frame color
        float baseR, baseG, baseB;
        if (!anim.frameColors.empty() && frame < anim.frameColors.size()) {
            std::tie(baseR, baseG, baseB) = anim.frameColors[frame];
        } else if (!anim.lightLevels.empty() && frame < anim.lightLevels.size()) {
            float level = anim.lightLevels[frame];
            baseR = anim.baseR * level;
            baseG = anim.baseG * level;
            baseB = anim.baseB * level;
        } else {
            continue;
        }

        // Apply vision/weather modifiers
        float r = baseR * intensity;
        float g = baseG * intensity * (1.0f - redShift * 0.5f);
        float b = baseB * intensity * (1.0f - redShift);
        if (redShift > 0.0f) {
            r = std::min(1.0f, r * (1.0f + redShift));
        }

        out.r = r;
        out.g = g;
        out.b = b;
    }
}

// ============================================================================
// Sky State Computation
// ============================================================================

void SimulationWorker::computeSkyState(const SimulationInput& input, SimulationOutput& output) {
    if (!input.skyEnabled || !input.skyInitialized) return;

    output.skyState.cloudScrollOffset = input.skyCloudScrollOffset + input.deltaTime * 0.01f;
    if (output.skyState.cloudScrollOffset > 1.0f) {
        output.skyState.cloudScrollOffset -= 1.0f;
    }
    output.skyState.valid = true;
}

// ============================================================================
// Weather Effects State Computation
// ============================================================================

void SimulationWorker::computeWeatherEffectsState(const SimulationInput& input, SimulationOutput& output) {
    if (!input.weatherEnabled) return;

    auto& ws = output.weatherEffectsState;

    // Advance transition progress
    ws.transitionProgress = input.weatherTransitionProgress;
    if (ws.transitionProgress < 1.0f) {
        ws.transitionProgress += input.deltaTime / input.weatherTransitionDuration;
        ws.transitionProgress = std::min(1.0f, ws.transitionProgress);
    }

    // Advance storm darkening
    float darkeningSpeed = 0.5f;
    ws.currentDarkening = input.weatherCurrentDarkening;
    if (ws.currentDarkening < input.weatherTargetDarkening) {
        ws.currentDarkening += darkeningSpeed * input.deltaTime;
        ws.currentDarkening = std::min(ws.currentDarkening, input.weatherTargetDarkening);
    } else if (ws.currentDarkening > input.weatherTargetDarkening) {
        ws.currentDarkening -= darkeningSpeed * input.deltaTime;
        ws.currentDarkening = std::max(ws.currentDarkening, input.weatherTargetDarkening);
    }

    // Advance lightning timers
    ws.lightningFlashTimer = input.weatherLightningFlashTimer;
    ws.lightningBoltTimer = input.weatherLightningBoltTimer;
    ws.lightningActive = input.weatherLightningActive;
    ws.triggerLightningFlash = false;

    if (ws.lightningFlashTimer > 0.0f) {
        ws.lightningFlashTimer -= input.deltaTime;
    }
    if (ws.lightningBoltTimer > 0.0f) {
        ws.lightningBoltTimer -= input.deltaTime;
        if (ws.lightningBoltTimer <= 0.0f) {
            ws.lightningActive = false;
        }
    }

    // Check for next lightning strike
    if (input.weatherType == 1 && input.weatherIntensity >= 3 && input.weatherLightningEnabled) {
        float timer = input.weatherLightningTimer - input.deltaTime;
        if (timer <= 0.0f) {
            ws.triggerLightningFlash = true;
        }
    }

    ws.valid = true;
}

// ============================================================================
// Particle System
// ============================================================================

using namespace Environment;

void SimulationWorker::computeParticles(const SimulationInput& input, SimulationOutput& output) {
    output.particleOutput.valid = false;

    // Skip if unified renderer not initialized on main thread
    if (!input.particleInput.unifiedRendererInitialized) return;

    // Lazy-init pool on worker thread
    if (!particlePoolInitialized_) {
        int poolSize = input.particleInput.poolSize;
        if (poolSize <= 0) poolSize = 1024;
        particlePool_.resize(poolSize);
        particleFreeList_.resize(poolSize);
        for (int i = 0; i < poolSize; ++i) {
            particleFreeList_[i] = static_cast<uint16_t>(i);
            particlePool_[i].setAlive(false);
        }
        particleActiveCount_ = 0;
        particleRng_.seed(std::random_device{}());
        particlePoolInitialized_ = true;
        LOG_INFO(MOD_GRAPHICS, "SimulationWorker: Particle pool initialized (size={})", poolSize);
    }

    const auto& pi = input.particleInput;
    float deltaTime = pi.deltaTime;
    if (deltaTime <= 0.0f || deltaTime > 1.0f) {
        output.particleOutput.valid = true;
        output.particleOutput.activeCount = particleActiveCount_;
        return;
    }

    // ---- Command processing ----
    for (const auto& cmd : pi.commands) {
        switch (cmd.type) {
            case ParticleCommand::CreateFireEmitters: {
                // Clear existing emitters first
                for (auto& p : particlePool_) {
                    if (p.isAlive()) {
                        p.setAlive(false);
                    }
                }
                particleFreeList_.resize(particlePool_.size());
                for (uint16_t i = 0; i < static_cast<uint16_t>(particlePool_.size()); ++i) {
                    particleFreeList_[i] = i;
                }
                particleActiveCount_ = 0;
                particleEmitters_.clear();
                particleWeatherEmitterID_ = 0;
                particleSpellEffects_.clear();

                const float campfireRadiusThreshold = 150.0f;
                for (size_t i = 0; i < cmd.firePositions.size(); ++i) {
                    const glm::vec3& eqPos = cmd.firePositions[i];
                    float radius = (i < cmd.fireRadii.size()) ? cmd.fireRadii[i] : 120.0f;
                    glm::vec3 irrPos(eqPos.x, eqPos.z, eqPos.y);

                    if (radius >= campfireRadiusThreshold) {
                        {
                            ActiveEmitter ae;
                            ae.config = FirePresets::CampfireFlame();
                            ae.position = irrPos;
                            ae.emitterID = particleNextEmitterID_++;
                            ae.lightRadius = radius;
                            particleEmitters_[ae.emitterID] = ae;
                        }
                        {
                            ActiveEmitter ae;
                            ae.config = FirePresets::CampfireEmber();
                            ae.position = irrPos;
                            ae.emitterID = particleNextEmitterID_++;
                            ae.lightRadius = radius;
                            particleEmitters_[ae.emitterID] = ae;
                        }
                    } else {
                        ActiveEmitter ae;
                        ae.config = FirePresets::Torch();
                        ae.position = irrPos;
                        ae.emitterID = particleNextEmitterID_++;
                        ae.lightRadius = radius;
                        particleEmitters_[ae.emitterID] = ae;
                    }
                }
                LOG_INFO(MOD_GRAPHICS, "SimWorker: Created {} fire emitters for {} sources",
                         particleEmitters_.size(), cmd.firePositions.size());
                break;
            }
            case ParticleCommand::ClearUnifiedEmitters: {
                for (auto& p : particlePool_) {
                    if (p.isAlive()) p.setAlive(false);
                }
                particleFreeList_.resize(particlePool_.size());
                for (uint16_t i = 0; i < static_cast<uint16_t>(particlePool_.size()); ++i) {
                    particleFreeList_[i] = i;
                }
                particleActiveCount_ = 0;
                particleEmitters_.clear();
                particleWeatherEmitterID_ = 0;
                particleSpellEffects_.clear();
                particleNextSpellEffectID_ = 1;
                break;
            }
            case ParticleCommand::ActivateWeather: {
                // Kill existing weather
                if (particleWeatherEmitterID_ != 0) {
                    auto it = particleEmitters_.find(particleWeatherEmitterID_);
                    if (it != particleEmitters_.end()) {
                        it->second.active = false;
                        for (auto& p : particlePool_) {
                            if (p.isAlive() && p.emitterID == particleWeatherEmitterID_) {
                                int idx = static_cast<int>(&p - particlePool_.data());
                                freeParticle(idx);
                            }
                        }
                        particleEmitters_.erase(it);
                    }
                    particleWeatherEmitterID_ = 0;
                }
                EmitterConfig cfg;
                if (cmd.weatherType == 1) {
                    cfg = WeatherPresets::Rain(cmd.weatherIntensity);
                } else if (cmd.weatherType == 2) {
                    cfg = WeatherPresets::Snow(cmd.weatherIntensity);
                } else break;

                ActiveEmitter ae;
                ae.config = cfg;
                ae.position = glm::vec3(0.0f);
                ae.emitterID = particleNextEmitterID_++;
                ae.transitionAlpha = 0.0f;
                ae.transitionRate = 0.5f;
                particleEmitters_[ae.emitterID] = ae;
                particleWeatherEmitterID_ = ae.emitterID;
                LOG_INFO(MOD_GRAPHICS, "SimWorker: Activated weather type={} intensity={}", cmd.weatherType, cmd.weatherIntensity);
                break;
            }
            case ParticleCommand::DeactivateWeather: {
                if (particleWeatherEmitterID_ != 0) {
                    auto it = particleEmitters_.find(particleWeatherEmitterID_);
                    if (it != particleEmitters_.end()) {
                        it->second.active = false;
                        for (auto& p : particlePool_) {
                            if (p.isAlive() && p.emitterID == particleWeatherEmitterID_) {
                                int idx = static_cast<int>(&p - particlePool_.data());
                                freeParticle(idx);
                            }
                        }
                        particleEmitters_.erase(it);
                    }
                    particleWeatherEmitterID_ = 0;
                }
                break;
            }
            case ParticleCommand::CreateSpellEffect: {
                SpellEffectInstance inst;
                inst.effectID = cmd.preAssignedEffectID;
                inst.spellID = 0;
                inst.casterEntityID = cmd.casterID;
                inst.targetEntityID = cmd.targetID;
                inst.age = 0.0f;
                inst.maxDuration = cmd.duration;
                inst.def = cmd.spellDef;
                inst.useDynamicDirection = cmd.useDynamicDir;
                if (cmd.projectileTravelDuration > 0.0f) {
                    inst.projectileTravelDuration = cmd.projectileTravelDuration;
                }
                for (int i = 0; i < static_cast<int>(cmd.spellDef.emitters.size()); ++i) {
                    SpellEffectInstance::EmitterState es;
                    es.defIndex = i;
                    es.activeEmitterID = 0;
                    es.triggered = false;
                    inst.emitterStates.push_back(es);
                }
                // Seed initial entity positions into the tracking map
                for (const auto& [eid, epos] : cmd.initialEntityPositions) {
                    // No need to store separately — pi.entityPositions should have them
                }
                particleSpellEffects_.push_back(std::move(inst));
                if (particleNextSpellEffectID_ <= cmd.preAssignedEffectID) {
                    particleNextSpellEffectID_ = cmd.preAssignedEffectID + 1;
                }
                LOG_DEBUG(MOD_GRAPHICS, "SimWorker: Created spell effect '{}' id={} caster={} target={}",
                          cmd.spellDef.name, cmd.preAssignedEffectID, cmd.casterID, cmd.targetID);
                break;
            }
            case ParticleCommand::CreateSpellEffectAtPos: {
                SpellEffectInstance inst;
                inst.effectID = cmd.preAssignedEffectID;
                inst.spellID = 0;
                inst.casterEntityID = 0;
                inst.targetEntityID = 0;
                inst.groundTarget = cmd.worldPos;
                inst.age = 0.0f;
                inst.maxDuration = cmd.duration;
                inst.def = cmd.spellDef;
                for (auto& e : inst.def.emitters) {
                    if (e.attach == SpellAttach::CASTER) {
                        e.attach = SpellAttach::GROUND_TARGET;
                    }
                }
                for (int i = 0; i < static_cast<int>(cmd.spellDef.emitters.size()); ++i) {
                    SpellEffectInstance::EmitterState es;
                    es.defIndex = i;
                    es.activeEmitterID = 0;
                    es.triggered = false;
                    inst.emitterStates.push_back(es);
                }
                particleSpellEffects_.push_back(std::move(inst));
                if (particleNextSpellEffectID_ <= cmd.preAssignedEffectID) {
                    particleNextSpellEffectID_ = cmd.preAssignedEffectID + 1;
                }
                break;
            }
            case ParticleCommand::RemoveSpellEffect: {
                for (auto it = particleSpellEffects_.begin(); it != particleSpellEffects_.end(); ++it) {
                    if (it->effectID == cmd.effectID) {
                        for (auto& es : it->emitterStates) {
                            if (es.activeEmitterID != 0) {
                                for (auto& p : particlePool_) {
                                    if (p.isAlive() && p.emitterID == es.activeEmitterID) {
                                        int idx = static_cast<int>(&p - particlePool_.data());
                                        freeParticle(idx);
                                    }
                                }
                                particleEmitters_.erase(es.activeEmitterID);
                            }
                        }
                        particleSpellEffects_.erase(it);
                        break;
                    }
                }
                break;
            }
            case ParticleCommand::RemoveSpellEffectsEntity: {
                for (auto it = particleSpellEffects_.begin(); it != particleSpellEffects_.end(); ) {
                    if (it->casterEntityID == cmd.entityID || it->targetEntityID == cmd.entityID) {
                        for (auto& es : it->emitterStates) {
                            if (es.activeEmitterID != 0) {
                                for (auto& p : particlePool_) {
                                    if (p.isAlive() && p.emitterID == es.activeEmitterID) {
                                        int idx = static_cast<int>(&p - particlePool_.data());
                                        freeParticle(idx);
                                    }
                                }
                                particleEmitters_.erase(es.activeEmitterID);
                            }
                        }
                        it = particleSpellEffects_.erase(it);
                    } else {
                        ++it;
                    }
                }
                break;
            }
            case ParticleCommand::ClearAllSpellEffects: {
                for (auto& effect : particleSpellEffects_) {
                    for (auto& es : effect.emitterStates) {
                        if (es.activeEmitterID != 0) {
                            for (auto& p : particlePool_) {
                                if (p.isAlive() && p.emitterID == es.activeEmitterID) {
                                    int idx = static_cast<int>(&p - particlePool_.data());
                                    freeParticle(idx);
                                }
                            }
                            particleEmitters_.erase(es.activeEmitterID);
                        }
                    }
                }
                particleSpellEffects_.clear();
                break;
            }
            case ParticleCommand::ToggleFire: {
                particleFireEnabled_ = !particleFireEnabled_;
                break;
            }
            case ParticleCommand::SetZoneEnter: {
                // Reset state for new zone
                break;
            }
            case ParticleCommand::ZoneLeave: {
                // Clear everything
                for (auto& p : particlePool_) {
                    if (p.isAlive()) p.setAlive(false);
                }
                particleFreeList_.resize(particlePool_.size());
                for (uint16_t i = 0; i < static_cast<uint16_t>(particlePool_.size()); ++i) {
                    particleFreeList_[i] = i;
                }
                particleActiveCount_ = 0;
                particleEmitters_.clear();
                particleWeatherEmitterID_ = 0;
                particleSpellEffects_.clear();
                particleNextSpellEffectID_ = 1;
                break;
            }
        }
    }

    // ---- Spawn phase ----
    for (auto& [id, emitter] : particleEmitters_) {
        if (!emitter.active) continue;

        bool isWeather = (emitter.config.motionType == MotionType::CAMERA_RELATIVE);
        if (!isWeather && !particleFireEnabled_) continue;

        // Check emitter lifetime
        if (emitter.config.emitterLifetime > 0.0f) {
            emitter.emitterAge += deltaTime;
            if (emitter.emitterAge >= emitter.config.emitterLifetime) {
                emitter.active = false;
                continue;
            }
        }

        // Ramp transition alpha for weather
        if (isWeather && emitter.transitionAlpha < 1.0f) {
            emitter.transitionAlpha += emitter.transitionRate * deltaTime;
            if (emitter.transitionAlpha > 1.0f) emitter.transitionAlpha = 1.0f;
        }

        if (isWeather) {
            // CAMERA_RELATIVE: target-count spawning
            emitter.position = pi.cameraPos;

            int aliveCount = 0;
            for (const auto& p : particlePool_) {
                if (p.isAlive() && p.emitterID == emitter.emitterID) {
                    aliveCount++;
                }
            }

            int effectiveTarget = static_cast<int>(emitter.config.targetCount * emitter.transitionAlpha);
            int deficit = effectiveTarget - aliveCount;
            for (int s = 0; s < deficit; ++s) {
                spawnWeatherParticle(emitter.config, emitter.emitterID, pi.cameraPos,
                                     emitter.transitionAlpha, pi.windDirection, pi.windStrength);
            }
        } else {
            // Non-weather: frustum cull using input frustum planes
            if (input.frustumValid) {
                // Convert emitter position (Irrlicht Y-up) to EQ Z-up for frustum test
                float eqX = emitter.position.x;
                float eqY = emitter.position.z;  // Irrlicht Z → EQ Y
                float eqZ = emitter.position.y;  // Irrlicht Y → EQ Z
                if (!testFrustumAABB(input.frustumPlanes,
                                      eqX - 2.0f, eqY - 2.0f, eqZ - 2.0f,
                                      eqX + 2.0f, eqY + 8.0f, eqZ + 2.0f)) {
                    continue;
                }
            }

            // Resolve dynamic direction from entity directions map
            const glm::vec3* dirPtr = nullptr;
            if (emitter.useDynamicDirection && emitter.attachEntityID != 0) {
                auto dirIt = pi.entityDirections.find(emitter.attachEntityID);
                if (dirIt != pi.entityDirections.end()) {
                    emitter.dynamicDirection = dirIt->second;
                }
                dirPtr = &emitter.dynamicDirection;
            }

            // BURST: one-shot spawn
            if (emitter.config.burstCount > 0 && !emitter.isBurstSpawned) {
                for (int s = 0; s < emitter.config.burstCount; ++s) {
                    spawnSpellParticle(emitter.config, emitter.emitterID,
                                       emitter.position + emitter.attachOffset, dirPtr);
                }
                emitter.isBurstSpawned = true;
            }

            // Spawn-rate spawning
            if (emitter.config.spawnRate > 0.0f) {
                emitter.spawnAccumulator += emitter.config.spawnRate * deltaTime;
                int toSpawn = static_cast<int>(emitter.spawnAccumulator);
                emitter.spawnAccumulator -= static_cast<float>(toSpawn);
                for (int s = 0; s < toSpawn; ++s) {
                    spawnSpellParticle(emitter.config, emitter.emitterID,
                                       emitter.position + emitter.attachOffset, dirPtr);
                }
            }
        }
    }

    // ---- Physics phase ----
    for (auto& p : particlePool_) {
        if (!p.isAlive()) continue;

        p.age += deltaTime;
        if (p.age >= p.maxLifetime) {
            int idx = static_cast<int>(&p - particlePool_.data());
            freeParticle(idx);
            continue;
        }

        float t = p.getNormalizedAge();

        // Interpolate color and size
        p.color = glm::mix(p.colorStart, p.colorEnd, t);
        p.size = glm::mix(p.sizeStart, p.sizeEnd, t);

        if (p.motionType == MotionType::CAMERA_RELATIVE) {
            auto it = particleEmitters_.find(p.emitterID);
            if (it == particleEmitters_.end()) continue;
            const EmitterConfig& cfg = it->second.config;

            // Gravity
            p.velocity += cfg.gravity * deltaTime;

            // Wind
            if (cfg.windResponse > 0.0f && pi.windStrength > 0.0f) {
                glm::vec3 windIrr(pi.windDirection.x, pi.windDirection.z, pi.windDirection.y);
                p.velocity += windIrr * (pi.windStrength * cfg.windResponse * deltaTime * 10.0f);
            }

            // Drag
            if (p.drag > 0.0f) {
                float dampFactor = 1.0f - p.drag * deltaTime;
                if (dampFactor < 0.0f) dampFactor = 0.0f;
                p.velocity *= dampFactor;
            }

            // Snow drift
            if (cfg.driftAmplitude > 0.0f && cfg.driftFrequency > 0.0f) {
                float drift = std::sin(p.age * cfg.driftFrequency * 6.28318f + p.phase) * cfg.driftAmplitude * deltaTime;
                p.position.x += drift;
                p.position.z += drift * 0.5f;
            }

            // Snow twinkle
            if (cfg.twinkleSpeed > 0.0f) {
                float baseAlpha = glm::mix(p.colorStart.a, p.colorEnd.a, t);
                p.color.a = baseAlpha * (0.7f + 0.3f * std::sin(p.age * cfg.twinkleSpeed + p.phase));
            }

            // Update position
            p.position += p.velocity * deltaTime;

            // Per-particle weather lighting
            constexpr float kParticleLightRadius = 15.0f;
            constexpr float kAmbientFactor = 0.1f;
            float lightR = pi.ambientColor.x * kAmbientFactor;
            float lightG = pi.ambientColor.y * kAmbientFactor;
            float lightB = pi.ambientColor.z * kAmbientFactor;
            for (const auto& light : pi.weatherLights) {
                glm::vec3 diff = p.position - light.position;
                float distSq = glm::dot(diff, diff);
                float dist = std::sqrt(distSq);
                if (dist < light.radius) {
                    float tt = 1.0f - dist / light.radius;
                    float atten = tt * tt;
                    atten *= atten;  // quartic
                    if (dist > kParticleLightRadius) atten = 0.0f;
                    lightR += light.color.x * atten;
                    lightG += light.color.y * atten;
                    lightB += light.color.z * atten;
                }
            }
            p.color.r *= std::min(lightR, 1.0f);
            p.color.g *= std::min(lightG, 1.0f);
            p.color.b *= std::min(lightB, 1.0f);

            // Recycle check
            glm::vec3 offset = p.position - pi.cameraPos;
            float hExtX = cfg.spawnVolumeHalfExtents.x * 1.5f;
            float hExtZ = cfg.spawnVolumeHalfExtents.z * 1.5f;
            float hExtY = cfg.spawnVolumeHalfExtents.y;
            if (std::abs(offset.x) > hExtX || std::abs(offset.z) > hExtZ || offset.y < -hExtY) {
                int idx = static_cast<int>(&p - particlePool_.data());
                freeParticle(idx);
            }
        } else if (p.motionType == MotionType::LINEAR ||
                   p.motionType == MotionType::BURST ||
                   p.motionType == MotionType::RADIAL_EXPAND) {
            auto it = particleEmitters_.find(p.emitterID);
            if (it != particleEmitters_.end()) {
                p.velocity += it->second.config.gravity * deltaTime;
            }

            if (p.drag > 0.0f) {
                float dampFactor = 1.0f - p.drag * deltaTime;
                if (dampFactor < 0.0f) dampFactor = 0.0f;
                p.velocity *= dampFactor;
            }

            p.position += p.velocity * deltaTime;

        } else if (p.motionType == MotionType::ORBITAL) {
            auto it = particleEmitters_.find(p.emitterID);
            if (it != particleEmitters_.end()) {
                glm::vec3 center = it->second.position + it->second.attachOffset;
                p.phase += p.angularVelocity * deltaTime;
                p.position.x = center.x + p.radius * std::cos(p.phase);
                p.position.z = center.z + p.radius * std::sin(p.phase);
                p.position.y += p.velocity.y * deltaTime;
            }
        }
    }

    // ---- Spell effect lifecycle ----
    if (!particleSpellEffects_.empty()) {
        for (auto it = particleSpellEffects_.begin(); it != particleSpellEffects_.end(); ) {
            auto& effect = *it;
            effect.age += deltaTime;

            // Check max duration
            if (effect.maxDuration > 0.0f && effect.age >= effect.maxDuration) {
                for (auto& es : effect.emitterStates) {
                    if (es.activeEmitterID != 0) {
                        auto emIt = particleEmitters_.find(es.activeEmitterID);
                        if (emIt != particleEmitters_.end()) {
                            emIt->second.active = false;
                        }
                    }
                }
            }

            bool allDone = true;
            for (auto& es : effect.emitterStates) {
                const auto& emDef = effect.def.emitters[es.defIndex];

                // Check triggers
                if (!es.triggered) {
                    bool shouldTrigger = false;
                    switch (emDef.trigger) {
                        case SpellTrigger::IMMEDIATE: shouldTrigger = true; break;
                        case SpellTrigger::DELAYED: shouldTrigger = (effect.age >= emDef.triggerDelay); break;
                        case SpellTrigger::ON_CAST_COMPLETE: shouldTrigger = effect.castCompleteSignaled; break;
                        case SpellTrigger::ON_HIT: shouldTrigger = effect.hitSignaled; break;
                    }

                    if (shouldTrigger) {
                        es.triggered = true;

                        glm::vec3 attachPos(0.0f);
                        uint16_t attachEntity = 0;

                        switch (emDef.attach) {
                            case SpellAttach::CASTER:
                                attachEntity = effect.casterEntityID;
                                if (attachEntity != 0) {
                                    auto posIt = pi.entityPositions.find(attachEntity);
                                    if (posIt != pi.entityPositions.end()) attachPos = posIt->second;
                                }
                                break;
                            case SpellAttach::TARGET:
                                attachEntity = effect.targetEntityID;
                                if (attachEntity != 0) {
                                    auto posIt = pi.entityPositions.find(attachEntity);
                                    if (posIt != pi.entityPositions.end()) attachPos = posIt->second;
                                }
                                break;
                            case SpellAttach::GROUND_TARGET:
                                attachPos = effect.groundTarget;
                                break;
                            case SpellAttach::PROJECTILE_PATH:
                                if (effect.casterEntityID != 0) {
                                    auto posIt = pi.entityPositions.find(effect.casterEntityID);
                                    if (posIt != pi.entityPositions.end()) attachPos = posIt->second;
                                }
                                break;
                        }

                        ActiveEmitter ae;
                        ae.config = emDef.config;
                        ae.position = attachPos;
                        ae.emitterID = particleNextEmitterID_++;
                        ae.attachEntityID = attachEntity;
                        ae.attachOffset = emDef.positionOffset;
                        ae.useDynamicDirection = effect.useDynamicDirection;

                        if (emDef.attach == SpellAttach::PROJECTILE_PATH) {
                            ae.isProjectile = true;
                            ae.projectileStartPos = attachPos;
                            ae.targetEntityID = effect.targetEntityID;
                            ae.travelDuration = effect.projectileTravelDuration;
                            ae.travelElapsed = 0.0f;
                            if (effect.targetEntityID != 0) {
                                auto posIt = pi.entityPositions.find(effect.targetEntityID);
                                if (posIt != pi.entityPositions.end()) ae.projectileTargetPos = posIt->second;
                            }
                        }

                        particleEmitters_[ae.emitterID] = ae;
                        es.activeEmitterID = ae.emitterID;
                    }
                }

                // Update entity positions for attached emitters
                if (es.activeEmitterID != 0) {
                    auto emIt = particleEmitters_.find(es.activeEmitterID);
                    if (emIt != particleEmitters_.end()) {
                        auto& emitter = emIt->second;

                        if (emitter.isProjectile) {
                            emitter.travelElapsed += deltaTime;
                            if (emitter.targetEntityID != 0) {
                                auto posIt = pi.entityPositions.find(emitter.targetEntityID);
                                if (posIt != pi.entityPositions.end()) {
                                    emitter.projectileTargetPos = posIt->second;
                                }
                            }
                            float tt = (emitter.travelDuration > 0.0f)
                                ? std::min(emitter.travelElapsed / emitter.travelDuration, 1.0f) : 1.0f;
                            emitter.position = glm::mix(emitter.projectileStartPos, emitter.projectileTargetPos, tt);
                            if (tt >= 1.0f) {
                                emitter.active = false;
                                effect.hitSignaled = true;
                            }
                        } else if (emitter.attachEntityID != 0) {
                            auto posIt = pi.entityPositions.find(emitter.attachEntityID);
                            if (posIt != pi.entityPositions.end()) {
                                emitter.position = posIt->second;
                            }
                        }

                        if (emitter.active) {
                            allDone = false;
                        } else {
                            for (const auto& pp : particlePool_) {
                                if (pp.isAlive() && pp.emitterID == es.activeEmitterID) {
                                    allDone = false;
                                    break;
                                }
                            }
                        }
                    }
                } else if (!es.triggered) {
                    allDone = false;
                }
            }

            if (allDone && (effect.maxDuration <= 0.0f || effect.age >= effect.maxDuration)) {
                for (auto& es : effect.emitterStates) {
                    if (es.activeEmitterID != 0) {
                        particleEmitters_.erase(es.activeEmitterID);
                    }
                }
                it = particleSpellEffects_.erase(it);
            } else {
                ++it;
            }
        }
    }

    // ---- Build output ----
    output.particleOutput.renderBuffer.clear();
    output.particleOutput.renderBuffer.reserve(particleActiveCount_);
    for (const auto& p : particlePool_) {
        if (p.isAlive()) {
            output.particleOutput.renderBuffer.push_back(p);
        }
    }
    output.particleOutput.activeCount = particleActiveCount_;

    // Build entity position/direction request sets
    output.particleOutput.positionRequestEntities.clear();
    output.particleOutput.directionRequestEntities.clear();
    for (const auto& effect : particleSpellEffects_) {
        if (effect.casterEntityID != 0) output.particleOutput.positionRequestEntities.insert(effect.casterEntityID);
        if (effect.targetEntityID != 0) output.particleOutput.positionRequestEntities.insert(effect.targetEntityID);
        for (const auto& es : effect.emitterStates) {
            if (es.activeEmitterID != 0) {
                auto emIt = particleEmitters_.find(es.activeEmitterID);
                if (emIt != particleEmitters_.end()) {
                    if (emIt->second.attachEntityID != 0) {
                        output.particleOutput.positionRequestEntities.insert(emIt->second.attachEntityID);
                    }
                    if (emIt->second.useDynamicDirection && emIt->second.attachEntityID != 0) {
                        output.particleOutput.directionRequestEntities.insert(emIt->second.attachEntityID);
                    }
                    if (emIt->second.isProjectile && emIt->second.targetEntityID != 0) {
                        output.particleOutput.positionRequestEntities.insert(emIt->second.targetEntityID);
                    }
                }
            }
        }
    }

    output.particleOutput.valid = true;
}

// Particle allocation helpers
int SimulationWorker::allocateParticle() {
    if (particleFreeList_.empty()) return -1;
    int idx = particleFreeList_.back();
    particleFreeList_.pop_back();
    particleActiveCount_++;
    return idx;
}

void SimulationWorker::freeParticle(int index) {
    if (index < 0 || index >= static_cast<int>(particlePool_.size())) return;
    particlePool_[index].setAlive(false);
    particleFreeList_.push_back(static_cast<uint16_t>(index));
    particleActiveCount_--;
}

float SimulationWorker::particleRandomFloat(float minVal, float maxVal) {
    std::uniform_real_distribution<float> dist(minVal, maxVal);
    return dist(particleRng_);
}

int SimulationWorker::particleRandomInt(int minVal, int maxVal) {
    std::uniform_int_distribution<int> dist(minVal, maxVal);
    return dist(particleRng_);
}

void SimulationWorker::spawnWeatherParticle(const EmitterConfig& cfg, uint16_t emitterID,
                                             const glm::vec3& cameraPos, float transitionAlpha,
                                             const glm::vec3& windDir, float windStrength) {
    int idx = allocateParticle();
    if (idx < 0) return;

    UnifiedParticle& p = particlePool_[idx];

    const glm::vec3& he = cfg.spawnVolumeHalfExtents;
    p.position.x = cameraPos.x + particleRandomFloat(-he.x, he.x);
    p.position.z = cameraPos.z + particleRandomFloat(-he.z, he.z);

    float yRand = particleRandomFloat(0.0f, 1.0f);
    if (yRand < cfg.spawnVolumeTopBias) {
        p.position.y = cameraPos.y + he.y * particleRandomFloat(0.6f, 1.0f);
    } else {
        p.position.y = cameraPos.y + particleRandomFloat(-he.y, he.y * 0.6f);
    }

    p.velocity.x = cfg.velocityBase.x + particleRandomFloat(-cfg.velocitySpread.x, cfg.velocitySpread.x);
    p.velocity.y = cfg.velocityBase.y + particleRandomFloat(-cfg.velocitySpread.y, cfg.velocitySpread.y);
    p.velocity.z = cfg.velocityBase.z + particleRandomFloat(-cfg.velocitySpread.z, cfg.velocitySpread.z);

    p.maxLifetime = particleRandomFloat(cfg.lifetimeMin, cfg.lifetimeMax);
    p.age = 0.0f;
    p.colorStart = cfg.colorStart;
    p.colorEnd = cfg.colorEnd;
    p.color = cfg.colorStart;

    if (cfg.blendMode == UnifiedBlendMode::ADDITIVE) {
        float alphaVar = particleRandomFloat(0.3f, cfg.colorStart.a);
        p.colorStart.a = alphaVar;
        p.color.a = alphaVar;
    }

    p.sizeStart = particleRandomFloat(cfg.sizeStartMin, cfg.sizeStartMax);
    p.sizeEnd = particleRandomFloat(cfg.sizeEndMin, cfg.sizeEndMax);
    p.size = p.sizeStart;

    if (cfg.sizeSpeedCorrelation > 0.0f) {
        float sizeRange = cfg.sizeStartMax - cfg.sizeStartMin;
        float sizeFactor = (sizeRange > 0.0f) ? (p.sizeStart - cfg.sizeStartMin) / sizeRange : 0.0f;
        p.velocity.y *= (1.0f - sizeFactor * cfg.sizeSpeedCorrelation);
    }

    p.drag = cfg.drag;
    p.motionType = cfg.motionType;
    p.phase = particleRandomFloat(0.0f, 6.28318f);

    if (cfg.driftAmplitude == 0.0f && cfg.windResponse > 0.0f) {
        float windAngle = std::atan2(windDir.x, windDir.y);
        p.rotation = windAngle * cfg.windResponse * windStrength * 0.3f;
    } else {
        p.rotation = 0.0f;
    }

    if (cfg.textureRegionCount > 1) {
        p.textureIndex = cfg.textureRegions[particleRandomInt(0, cfg.textureRegionCount - 1)];
    } else {
        p.textureIndex = cfg.textureRegions[0];
    }

    p.emitterID = emitterID;
    p.setAlive(true);
    p.setBlendMode(cfg.blendMode);
}

void SimulationWorker::spawnSpellParticle(const EmitterConfig& cfg, uint16_t emitterID,
                                           const glm::vec3& emitterPos, const glm::vec3* dynamicDir) {
    int idx = allocateParticle();
    if (idx < 0) return;

    UnifiedParticle& p = particlePool_[idx];

    p.position = emitterPos;
    if (cfg.spawnShape == SpawnShape::BOX) {
        p.position.x += particleRandomFloat(-cfg.spawnExtents.x, cfg.spawnExtents.x);
        p.position.y += particleRandomFloat(-cfg.spawnExtents.y, cfg.spawnExtents.y);
        p.position.z += particleRandomFloat(-cfg.spawnExtents.z, cfg.spawnExtents.z);
    } else if (cfg.spawnShape == SpawnShape::SPHERE) {
        float r = particleRandomFloat(0.0f, cfg.spawnExtents.x);
        float theta = particleRandomFloat(0.0f, 6.28318f);
        float phi = particleRandomFloat(-1.5708f, 1.5708f);
        p.position.x += r * std::cos(phi) * std::cos(theta);
        p.position.y += r * std::sin(phi);
        p.position.z += r * std::cos(phi) * std::sin(theta);
    } else if (cfg.spawnShape == SpawnShape::RING) {
        float angle = particleRandomFloat(0.0f, 6.28318f);
        p.position.x += cfg.spawnExtents.x * std::cos(angle);
        p.position.z += cfg.spawnExtents.x * std::sin(angle);
    }

    if (dynamicDir) {
        float speed = glm::length(cfg.velocityBase);
        if (speed < 0.01f) speed = 4.0f;
        p.velocity = (*dynamicDir) * speed;
        p.velocity.x += particleRandomFloat(-cfg.velocitySpread.x, cfg.velocitySpread.x);
        p.velocity.y += particleRandomFloat(-cfg.velocitySpread.y, cfg.velocitySpread.y);
        p.velocity.z += particleRandomFloat(-cfg.velocitySpread.z, cfg.velocitySpread.z);
    } else if (cfg.motionType == MotionType::RADIAL_EXPAND) {
        float angle = particleRandomFloat(0.0f, 6.28318f);
        p.velocity.x = std::cos(angle) * cfg.expandSpeed + particleRandomFloat(-cfg.velocitySpread.x, cfg.velocitySpread.x);
        p.velocity.z = std::sin(angle) * cfg.expandSpeed + particleRandomFloat(-cfg.velocitySpread.z, cfg.velocitySpread.z);
        p.velocity.y = cfg.velocityBase.y + particleRandomFloat(-cfg.velocitySpread.y, cfg.velocitySpread.y);
    } else if (cfg.motionType == MotionType::BURST) {
        float angle = particleRandomFloat(0.0f, 6.28318f);
        p.velocity.x = std::cos(angle) * cfg.velocityBase.x + particleRandomFloat(-cfg.velocitySpread.x, cfg.velocitySpread.x);
        p.velocity.z = std::sin(angle) * cfg.velocityBase.x + particleRandomFloat(-cfg.velocitySpread.z, cfg.velocitySpread.z);
        p.velocity.y = cfg.velocityBase.y + particleRandomFloat(-cfg.velocitySpread.y, cfg.velocitySpread.y);
    } else {
        p.velocity.x = cfg.velocityBase.x + particleRandomFloat(-cfg.velocitySpread.x, cfg.velocitySpread.x);
        p.velocity.y = cfg.velocityBase.y + particleRandomFloat(-cfg.velocitySpread.y, cfg.velocitySpread.y);
        p.velocity.z = cfg.velocityBase.z + particleRandomFloat(-cfg.velocitySpread.z, cfg.velocitySpread.z);
    }

    p.maxLifetime = particleRandomFloat(cfg.lifetimeMin, cfg.lifetimeMax);
    p.age = 0.0f;
    p.colorStart = cfg.colorStart;
    p.colorEnd = cfg.colorEnd;
    p.color = cfg.colorStart;

    p.sizeStart = particleRandomFloat(cfg.sizeStartMin, cfg.sizeStartMax);
    p.sizeEnd = particleRandomFloat(cfg.sizeEndMin, cfg.sizeEndMax);
    p.size = p.sizeStart;

    p.drag = cfg.drag;
    p.motionType = cfg.motionType;
    p.phase = particleRandomFloat(0.0f, 6.28318f);
    p.rotation = 0.0f;

    if (cfg.motionType == MotionType::ORBITAL) {
        p.radius = cfg.orbitalRadius;
        p.angularVelocity = cfg.orbitalAngularVelocity;
        p.position.x = emitterPos.x + p.radius * std::cos(p.phase);
        p.position.z = emitterPos.z + p.radius * std::sin(p.phase);
    }

    if (cfg.textureRegionCount > 1) {
        p.textureIndex = cfg.textureRegions[particleRandomInt(0, cfg.textureRegionCount - 1)];
    } else {
        p.textureIndex = cfg.textureRegions[0];
    }

    p.emitterID = emitterID;
    p.setAlive(true);
    p.setBlendMode(cfg.blendMode);
}

// ============================================================================
// Software Occlusion Culling
// ============================================================================

void SimulationWorker::computeSoftwareOcclusion(const SimulationInput& input, SimulationOutput& output) {
    output.occlusionCulledRegions.clear();

    if (!workerOcclusionCuller_ || !input.occlusionCamera.enabled) return;
    if (output.sortedRegions.empty()) return;

    const auto& cam = input.occlusionCamera;
    workerOcclusionCuller_->setCamera(
        input.camEqX, input.camEqY, input.camEqZ,
        cam.fwdX, cam.fwdY, cam.fwdZ,
        cam.rightX, cam.rightY, cam.rightZ,  // Note: rightZ is always 0 for 2D but we pass full vector
        cam.upX, cam.upY, cam.upZ,
        cam.fovRadV, cam.aspect);
    workerOcclusionCuller_->clear();
    workerOcclusionCuller_->resetStats();

    // Rasterize occluders from N closest PVS-visible regions
    int maxOccluderRegions = workerOcclusionCuller_->getConfig().maxOccluderRegions;
    int rasterizedRegions = 0;

    for (const auto& sr : output.sortedRegions) {
        if (rasterizedRegions >= maxOccluderRegions) break;
        const auto& occluders = workerOcclusionCuller_->getRegionOccluders(sr.regionIdx);
        if (occluders.empty()) continue;
        for (const auto& tri : occluders) {
            workerOcclusionCuller_->rasterizeTriangle(tri.v0, tri.v1, tri.v2);
        }
        rasterizedRegions++;
    }

    if (rasterizedRegions == 0) return;

    // Test PVS-visible region AABBs for occlusion
    for (size_t i = 0; i < zoneData_.regionBounds.size(); ++i) {
        if (output.regionVisible[i] == 0) continue;  // Already culled by PVS/frustum
        const auto& rb = zoneData_.regionBounds[i];
        if (workerOcclusionCuller_->testAABB(rb.minX, rb.minY, rb.minZ,
                                              rb.maxX, rb.maxY, rb.maxZ)) {
            output.occlusionCulledRegions.insert(rb.regionIdx);
            output.regionVisible[i] = 0;
        }
    }

    // Refine object visibility — mark objects in occluded regions as hidden
    for (size_t i = 0; i < zoneData_.objects.size(); ++i) {
        if (output.objectVisible[i] == 0) continue;
        const auto& obj = zoneData_.objects[i];
        if (obj.bspRegion != SIZE_MAX &&
            output.occlusionCulledRegions.count(obj.bspRegion)) {
            output.objectVisible[i] = 0;
        }
    }

    // Refine light visibility — mark lights in occluded regions as hidden
    for (size_t i = 0; i < zoneData_.zoneLights.size(); ++i) {
        if (output.lightVisible[i] == 0) continue;
        const auto& zl = zoneData_.zoneLights[i];
        if (zl.bspRegion != SIZE_MAX &&
            output.occlusionCulledRegions.count(zl.bspRegion)) {
            output.lightVisible[i] = 0;
        }
    }
}

// ============================================================================
// Entity Sync — reconcile worker entity list with main-thread snapshot
// ============================================================================

void SimulationWorker::computeEntitySync(const SimulationInput& input) {
    if (input.entitySnapshots.empty()) {
        workerEntities_.clear();
        return;
    }

    // Build set of current spawn IDs from snapshot
    std::unordered_set<uint16_t> currentIds;
    currentIds.reserve(input.entitySnapshots.size());
    for (const auto& snap : input.entitySnapshots) {
        currentIds.insert(snap.spawnId);
    }

    // Remove entities no longer present
    for (auto it = workerEntities_.begin(); it != workerEntities_.end(); ) {
        if (currentIds.find(it->first) == currentIds.end()) {
            it = workerEntities_.erase(it);
        } else {
            ++it;
        }
    }

    // Create or update entries from snapshot
    for (const auto& snap : input.entitySnapshots) {
        auto it = workerEntities_.find(snap.spawnId);
        if (it == workerEntities_.end()) {
            // New entity — initialize from snapshot
            WorkerEntityState state;
            state.lastX = snap.lastX;
            state.lastY = snap.lastY;
            state.lastZ = snap.lastZ;
            state.velocityX = snap.velocityX;
            state.velocityY = snap.velocityY;
            state.velocityZ = snap.velocityZ;
            state.serverX = snap.serverX;
            state.serverY = snap.serverY;
            state.serverZ = snap.serverZ;
            state.serverHeading = snap.serverHeading;
            state.timeSinceUpdate = snap.timeSinceUpdate;
            state.lastUpdateInterval = snap.lastUpdateInterval;
            state.collisionZOffset = snap.collisionZOffset;
            state.modelYOffset = snap.modelYOffset;
            state.serverAnimation = snap.serverAnimation;
            state.lastNonZeroAnimation = snap.lastNonZeroAnimation;
            state.cachedBspRegion = snap.cachedBspRegion;
            state.bspRegionDirty = snap.bspRegionDirty;
            state.isNPC = snap.isNPC;
            state.isPlayer = snap.isPlayer;
            state.isCorpse = snap.isCorpse;
            state.isFading = snap.isFading;
            state.active = snap.hasVelocity;
            workerEntities_[snap.spawnId] = state;
        } else {
            // Existing entity — sync immutable/server-controlled fields
            auto& state = it->second;
            state.isNPC = snap.isNPC;
            state.isPlayer = snap.isPlayer;
            state.isCorpse = snap.isCorpse;
            state.isFading = snap.isFading;
            state.collisionZOffset = snap.collisionZOffset;
            state.modelYOffset = snap.modelYOffset;
        }
    }
}

// ============================================================================
// Entity Pending Updates — process network updates on worker
// ============================================================================

void SimulationWorker::computeEntityPendingUpdates(const SimulationInput& input) {
    for (const auto& update : input.entityPendingUpdates) {
        auto it = workerEntities_.find(update.spawnId);
        if (it == workerEntities_.end()) continue;

        auto& state = it->second;

        // Check if position changed
        float serverDeltaX = update.x - state.serverX;
        float serverDeltaY = update.y - state.serverY;
        float serverDeltaZ = update.z - state.serverZ;
        bool positionChanged = (std::abs(serverDeltaX) > 0.01f ||
                                std::abs(serverDeltaY) > 0.01f ||
                                std::abs(serverDeltaZ) > 0.01f);

        if (positionChanged) {
            state.bspRegionDirty = true;
        }

        // NPC velocity calculation (same logic as EntityRenderer::processUpdate)
        if (state.isNPC) {
            if (update.animation != 0) {
                float headingRad = update.heading * 3.14159265f / 180.0f;
                float speed = static_cast<float>(std::abs(update.animation)) * 0.58f;
                float direction = (update.animation < 0) ? -1.0f : 1.0f;
                state.velocityX = std::cos(headingRad) * speed * direction;
                state.velocityY = std::sin(headingRad) * speed * direction;
                state.velocityZ = 0;
            } else {
                state.velocityX = 0;
                state.velocityY = 0;
                state.velocityZ = 0;
            }
        } else {
            // Player velocity
            if (std::abs(update.dx) > 0.01f || std::abs(update.dy) > 0.01f || std::abs(update.dz) > 0.01f) {
                state.velocityX = update.dx;
                state.velocityY = update.dy;
                state.velocityZ = update.dz;
            } else if (positionChanged && state.timeSinceUpdate > 0.05f) {
                float invTime = 1.0f / state.timeSinceUpdate;
                state.velocityX = serverDeltaX * invTime;
                state.velocityY = serverDeltaY * invTime;
                state.velocityZ = serverDeltaZ * invTime;
            } else if (!positionChanged) {
                state.velocityX = 0;
                state.velocityY = 0;
                state.velocityZ = 0;
            }
        }

        // Update server position tracking
        state.serverX = update.x;
        state.serverY = update.y;
        state.serverZ = update.z;
        state.serverHeading = update.heading;

        // Snap interpolated position to server position
        state.lastX = update.x;
        state.lastY = update.y;
        state.lastZ = update.z;

        // Track update interval
        if (state.timeSinceUpdate > 0.05f && state.timeSinceUpdate < 2.0f) {
            state.lastUpdateInterval = state.lastUpdateInterval * 0.7f + state.timeSinceUpdate * 0.3f;
        }
        state.timeSinceUpdate = 0;
        state.serverAnimation = update.animation;

        // Mark active if has velocity
        state.active = (std::abs(state.velocityX) > 0.01f ||
                        std::abs(state.velocityY) > 0.01f ||
                        std::abs(state.velocityZ) > 0.01f);

        if (update.animation != 0) {
            state.lastNonZeroAnimation = update.animation;
        }
    }
}

// ============================================================================
// Entity Interpolation — velocity-based position computation on worker
// ============================================================================

void SimulationWorker::computeEntityInterpolation(const SimulationInput& input, SimulationOutput& output) {
    output.entityResults.clear();
    if (workerEntities_.empty()) return;

    float deltaTime = input.deltaTime;
    output.entityResults.reserve(workerEntities_.size());

    for (auto& [spawnId, state] : workerEntities_) {
        state.timeSinceUpdate += deltaTime;

        SimulationOutput::EntityResult result;
        result.spawnId = spawnId;
        result.wasInterpolated = false;
        result.shouldBeVisible = true;  // Default, culling sets this later
        result.shouldDeactivate = false;

        // Skip player/corpse/fading position interpolation
        if (state.isPlayer || state.isCorpse || state.isFading) {
            result.posX = state.lastX;
            result.posY = state.lastY;
            result.posZ = state.lastZ;
            result.cachedBspRegion = state.cachedBspRegion;
            result.bspRegionDirty = state.bspRegionDirty;
            output.entityResults.push_back(result);
            continue;
        }

        // Check if stationary
        if (!state.active) {
            result.posX = state.lastX;
            result.posY = state.lastY;
            result.posZ = state.lastZ;
            result.cachedBspRegion = state.cachedBspRegion;
            result.bspRegionDirty = state.bspRegionDirty;
            result.shouldDeactivate = true;
            output.entityResults.push_back(result);
            continue;
        }

        if (std::abs(state.velocityX) < 0.01f &&
            std::abs(state.velocityY) < 0.01f &&
            std::abs(state.velocityZ) < 0.01f) {
            state.active = false;
            result.posX = state.lastX;
            result.posY = state.lastY;
            result.posZ = state.lastZ;
            result.cachedBspRegion = state.cachedBspRegion;
            result.bspRegionDirty = state.bspRegionDirty;
            result.shouldDeactivate = true;
            output.entityResults.push_back(result);
            continue;
        }

        // Timeout handling
        if (state.timeSinceUpdate > state.lastUpdateInterval * 1.5f) {
            if (state.isNPC && state.serverAnimation != 0) {
                // Recalculate velocity from heading with damping
                float headingRad = state.serverHeading * 3.14159265f / 180.0f;
                float dampingFactor = 0.85f;
                float speed = static_cast<float>(std::abs(state.serverAnimation)) * 0.58f * dampingFactor;
                float direction = (state.serverAnimation < 0) ? -1.0f : 1.0f;
                state.velocityX = std::cos(headingRad) * speed * direction;
                state.velocityY = std::sin(headingRad) * speed * direction;
                state.velocityZ = 0;
            } else {
                // Stop movement
                state.velocityX = 0;
                state.velocityY = 0;
                state.velocityZ = 0;
                state.active = false;
                result.posX = state.lastX;
                result.posY = state.lastY;
                result.posZ = state.lastZ;
                result.cachedBspRegion = state.cachedBspRegion;
                result.bspRegionDirty = state.bspRegionDirty;
                result.shouldDeactivate = true;
                output.entityResults.push_back(result);
                continue;
            }
        }

        // Interpolate position
        float damping = 1.0f;
        if (state.timeSinceUpdate > state.lastUpdateInterval * 3.0f) {
            float overshootTime = state.timeSinceUpdate - state.lastUpdateInterval * 3.0f;
            damping = std::exp(-0.14f * overshootTime);
            damping = std::max(0.3f, damping);
        }
        state.lastX += state.velocityX * deltaTime * damping;
        state.lastY += state.velocityY * deltaTime * damping;
        state.lastZ += state.velocityZ * deltaTime * damping;

        // NPC ground snap via HCMap
        if (state.isNPC && zoneData_.hcMap &&
            (std::abs(state.velocityX) > 0.01f || std::abs(state.velocityY) > 0.01f)) {
            glm::vec3 startPos(state.lastX, state.lastY, state.lastZ + 10.0f);
            glm::vec3 resultPos;
            float groundZ = zoneData_.hcMap->FindBestZ(startPos, &resultPos);
            if (groundZ > -999000.0f) {  // Valid result
                float targetZ = groundZ + state.collisionZOffset;
                float heightDiff = targetZ - state.lastZ;
                if (std::abs(heightDiff) < 20.0f) {
                    state.lastZ = targetZ;
                }
            }
        }

        result.posX = state.lastX;
        result.posY = state.lastY;
        result.posZ = state.lastZ;
        result.cachedBspRegion = state.cachedBspRegion;
        result.bspRegionDirty = state.bspRegionDirty;
        result.wasInterpolated = true;
        output.entityResults.push_back(result);
    }
}

// ============================================================================
// Entity Visibility — distance + frustum + portal + occlusion culling
// ============================================================================

void SimulationWorker::computeEntityVisibility(const SimulationInput& input, SimulationOutput& output) {
    if (!input.entityCullingEnabled || output.entityResults.empty()) {
        output.entityVisibleCount = static_cast<int>(output.entityResults.size());
        return;
    }

    float maxDistSq = input.entityRenderDistance * input.entityRenderDistance;
    int maxEntities = input.maxVisibleEntities;

    // Build distance list
    struct EntityDist {
        size_t resultIdx;
        float distanceSq;
    };
    std::vector<EntityDist> distances;
    distances.reserve(output.entityResults.size());

    for (size_t i = 0; i < output.entityResults.size(); ++i) {
        const auto& er = output.entityResults[i];
        float dx = er.posX - input.camEqX;
        float dy = er.posY - input.camEqY;
        float dz = er.posZ - input.camEqZ;
        distances.push_back({i, dx*dx + dy*dy + dz*dz});
    }

    // Sort by distance
    std::sort(distances.begin(), distances.end(),
              [](const EntityDist& a, const EntityDist& b) {
                  return a.distanceSq < b.distanceSq;
              });

    int visibleCount = 0;

    for (const auto& ed : distances) {
        auto& er = output.entityResults[ed.resultIdx];
        auto wit = workerEntities_.find(er.spawnId);
        if (wit == workerEntities_.end()) {
            er.shouldBeVisible = false;
            continue;
        }
        const auto& state = wit->second;

        // Always show player
        if (state.isPlayer) {
            er.shouldBeVisible = true;
            visibleCount++;
            continue;
        }

        bool visible = (visibleCount < maxEntities) && (ed.distanceSq <= maxDistSq);

        // Frustum culling
        if (visible && input.frustumValid) {
            // Simple sphere test using frustum planes
            float cx = er.posX, cy = er.posY, cz = er.posZ;
            float radius = 5.0f;
            bool insideFrustum = true;
            for (int p = 0; p < 6 && insideFrustum; ++p) {
                float dot = input.frustumPlanes[p][0] * cx +
                            input.frustumPlanes[p][1] * cy +
                            input.frustumPlanes[p][2] * cz +
                            input.frustumPlanes[p][3];
                if (dot < -radius) insideFrustum = false;
            }
            if (!insideFrustum) visible = false;
        }

        // Portal culling — check if entity's BSP region is portal-visible
        if (visible && !output.portalVisibleRegions.empty() && zoneData_.bspTree) {
            // Re-lookup BSP region if dirty
            if (state.bspRegionDirty || state.cachedBspRegion == SIZE_MAX) {
                // Can't write to state here safely since we're iterating,
                // but we can do a local lookup
                size_t entityRegion = zoneData_.bspTree->findRegionIndexForPoint(
                    er.posX, er.posY, er.posZ);
                er.cachedBspRegion = entityRegion;
                er.bspRegionDirty = false;
            }
            if (er.cachedBspRegion != SIZE_MAX &&
                output.portalVisibleRegions.find(er.cachedBspRegion) == output.portalVisibleRegions.end()) {
                visible = false;
            }
        }

        // Occlusion test (when portals not active)
        if (visible && output.portalVisibleRegions.empty() &&
            workerOcclusionCuller_ && workerOcclusionCuller_->isEnabled()) {
            if (workerOcclusionCuller_->testPoint(er.posX, er.posY, er.posZ + 3.0f)) {
                visible = false;
            }
        }

        er.shouldBeVisible = visible;
        if (visible) visibleCount++;
    }

    output.entityVisibleCount = visibleCount;
}

// ============================================================================
// Name Tag Visibility (moved from main thread updateNameTagsWithLOS)
// ============================================================================

void SimulationWorker::computeNameTagVisibility(const SimulationInput& input, SimulationOutput& output) {
    if (output.entityResults.empty()) return;

    float nameTagDistSq = input.nameTagDistance * input.nameTagDistance;

    // Get PVS data for camera's current region
    const BspRegion* cameraRegion = nullptr;
    if (zoneData_.bspTree && output.currentPvsRegion != SIZE_MAX
        && output.currentPvsRegion < zoneData_.bspTree->regions.size()) {
        cameraRegion = zoneData_.bspTree->regions[output.currentPvsRegion].get();
    }

    for (auto& er : output.entityResults) {
        er.nameTagVisible = false;

        // Entity must be visible (not culled by distance/frustum/portal/occlusion)
        if (!er.shouldBeVisible) continue;

        // Global toggle
        if (!input.nameTagsVisible) continue;

        // Distance check from camera (EQ coordinates)
        float dx = er.posX - input.camEqX;
        float dy = er.posY - input.camEqY;
        float dz = er.posZ - input.camEqZ;
        float distSq = dx * dx + dy * dy + dz * dz;
        if (distSq > nameTagDistSq) continue;

        bool visible = true;

        // PVS bitvector check
        if (visible && cameraRegion && !cameraRegion->visibleRegions.empty()) {
            size_t entityRegion = er.cachedBspRegion;
            if (entityRegion != SIZE_MAX && entityRegion < cameraRegion->visibleRegions.size()) {
                if (!cameraRegion->visibleRegions[entityRegion]) {
                    visible = false;
                }
            }
        }

        // Portal culling
        if (visible && !output.portalVisibleRegions.empty()) {
            size_t entityRegion = er.cachedBspRegion;
            if (entityRegion != SIZE_MAX &&
                output.portalVisibleRegions.find(entityRegion) == output.portalVisibleRegions.end()) {
                visible = false;
            }
        }

        // Occlusion test (when portals not active)
        if (visible && output.portalVisibleRegions.empty() &&
            workerOcclusionCuller_ && workerOcclusionCuller_->isEnabled()) {
            if (workerOcclusionCuller_->testPoint(er.posX, er.posY, er.posZ + 3.0f)) {
                visible = false;
            }
        }

        er.nameTagVisible = visible;
    }
}

// ============================================================================
// Boids Simulation (worker thread)
// ============================================================================

float SimulationWorker::boidsRandomFloat(float minVal, float maxVal) {
    std::uniform_real_distribution<float> dist(minVal, maxVal);
    return dist(boidsRng_);
}

glm::vec3 SimulationWorker::boidsGetRandomSpawnPosition(const glm::vec3& playerPos) {
    float angle = boidsRandomFloat(0.0f, 6.28318f);
    float dist = boidsRandomFloat(40.0f, 80.0f);
    glm::vec3 pos = playerPos;
    pos.x += std::cos(angle) * dist;
    pos.y += std::sin(angle) * dist;

    if (boidsHasBounds_) {
        pos.x = glm::clamp(pos.x, boidsBoundsMin_.x + 20.0f, boidsBoundsMax_.x - 20.0f);
        pos.y = glm::clamp(pos.y, boidsBoundsMin_.y + 20.0f, boidsBoundsMax_.y - 20.0f);
    }
    return pos;
}

std::vector<Environment::CreatureType> SimulationWorker::boidsGetTypesForBiome(int biome, bool isDay) {
    using CT = Environment::CreatureType;
    using ZB = Environment::ZoneBiome;
    std::vector<CT> types;
    auto b = static_cast<ZB>(biome);

    switch (b) {
        case ZB::Forest:
            if (isDay) { types.push_back(CT::Bird); types.push_back(CT::Butterfly); }
            else { types.push_back(CT::Bat); types.push_back(CT::Firefly); }
            break;
        case ZB::Swamp:
            if (isDay) { types.push_back(CT::Dragonfly); }
            else { types.push_back(CT::Bat); types.push_back(CT::Firefly); }
            break;
        case ZB::Desert:
            if (isDay) { types.push_back(CT::Crow); }
            else { types.push_back(CT::Bat); }
            break;
        case ZB::Plains:
            if (isDay) { types.push_back(CT::Bird); types.push_back(CT::Butterfly); }
            else { types.push_back(CT::Bat); types.push_back(CT::Firefly); }
            break;
        case ZB::Urban:
            if (isDay) { types.push_back(CT::Crow); types.push_back(CT::Bird); }
            else { types.push_back(CT::Bat); }
            break;
        case ZB::Ocean:
            if (isDay) { types.push_back(CT::Seagull); }
            break;
        case ZB::Dungeon:
        case ZB::Cave:
            types.push_back(CT::Bat);
            break;
        case ZB::Volcanic:
            if (isDay) { types.push_back(CT::Crow); }
            else { types.push_back(CT::Bat); }
            break;
        case ZB::Snow:
            if (isDay) { types.push_back(CT::Crow); }
            break;
        default:
            if (isDay) { types.push_back(CT::Bird); }
            break;
    }

    // Filter disabled types
    types.erase(
        std::remove_if(types.begin(), types.end(),
            [this](CT t) { return !boidsTypeEnabled_[static_cast<size_t>(t)]; }),
        types.end());

    return types;
}

void SimulationWorker::computeBoids(const SimulationInput& input, SimulationOutput& output) {
    const auto& bi = input.boidsInput;
    if (!bi.initialized) {
        output.boidsOutput.valid = false;
        return;
    }

    // Process commands
    for (const auto& cmd : bi.commands) {
        switch (cmd.type) {
            case BoidsCommand::ZoneEnter:
                boidsFlocks_.clear();
                boidsSpawnTimer_ = 5.0f;
                boidsZoneBiome_ = cmd.zoneBiome;
                if (cmd.hasBounds) {
                    boidsBoundsMin_ = cmd.boundsMin;
                    boidsBoundsMax_ = cmd.boundsMax;
                    boidsHasBounds_ = true;
                }
                break;
            case BoidsCommand::ZoneLeave:
                boidsFlocks_.clear();
                boidsZoneBiome_ = 0;
                boidsHasBounds_ = false;
                break;
            case BoidsCommand::SetQuality:
                boidsQuality_ = cmd.quality;
                break;
            case BoidsCommand::SetDensity:
                boidsDensity_ = cmd.density;
                break;
            case BoidsCommand::SetEnabled:
                boidsEnabled_ = cmd.enabled;
                if (!boidsEnabled_) boidsFlocks_.clear();
                break;
            case BoidsCommand::SetTypeEnabled:
                if (cmd.creatureType < static_cast<uint8_t>(Environment::CreatureType::Count))
                    boidsTypeEnabled_[cmd.creatureType] = cmd.typeEnabled;
                break;
        }
    }

    if (!boidsEnabled_ || boidsQuality_ == 0) {
        output.boidsOutput.creatures.clear();
        output.boidsOutput.activeCount = 0;
        output.boidsOutput.valid = true;
        return;
    }

    float dt = bi.deltaTime;
    glm::vec3 playerPos = bi.playerPosition;
    float timeOfDay = bi.timeOfDay;
    bool isDay = (timeOfDay >= 6.0f && timeOfDay < 20.0f);

    auto budget = Environment::BoidsBudget::fromQuality(boidsQuality_);

    // Spawn logic
    boidsSpawnTimer_ -= dt;
    if (boidsSpawnTimer_ <= 0.0f) {
        boidsSpawnTimer_ = boidsSpawnCooldown_;

        if (static_cast<int>(boidsFlocks_.size()) < budget.maxFlocks) {
            // Count total creatures
            int totalCreatures = 0;
            for (const auto& f : boidsFlocks_) totalCreatures += static_cast<int>(f.creatures.size());

            if (totalCreatures < budget.maxCreatures) {
                auto validTypes = boidsGetTypesForBiome(boidsZoneBiome_, isDay);
                if (!validTypes.empty()) {
                    std::uniform_int_distribution<size_t> typeDist(0, validTypes.size() - 1);
                    auto type = validTypes[typeDist(boidsRng_)];
                    auto config = Environment::getDefaultFlockConfig(type);

                    float effectiveDensity = boidsDensity_ * budget.densityMult;
                    config.minSize = std::max(2, static_cast<int>(config.minSize * effectiveDensity));
                    config.maxSize = std::max(config.minSize, static_cast<int>(config.maxSize * effectiveDensity));

                    glm::vec3 spawnPos = boidsGetRandomSpawnPosition(playerPos);
                    float minSpawnZ = playerPos.z + config.heightMin;
                    spawnPos.z = minSpawnZ + (config.heightMax - config.heightMin) * 0.5f;

                    // Create flock
                    WorkerFlockState flock;
                    flock.config = config;
                    flock.anchor = spawnPos;
                    flock.destination = spawnPos;
                    flock.center = spawnPos;
                    if (boidsHasBounds_) {
                        flock.boundsMin = boidsBoundsMin_;
                        flock.boundsMax = boidsBoundsMax_;
                        flock.hasBounds = true;
                    }

                    // Initialize creatures
                    std::uniform_int_distribution<int> sizeDist(config.minSize, config.maxSize);
                    int flockSize = sizeDist(boidsRng_);
                    flock.creatures.reserve(flockSize);
                    for (int i = 0; i < flockSize; ++i) {
                        WorkerCreature c;
                        c.position = spawnPos + glm::vec3(
                            boidsRandomFloat(-5.0f, 5.0f),
                            boidsRandomFloat(-5.0f, 5.0f),
                            boidsRandomFloat(-2.0f, 2.0f));
                        glm::vec3 dir = glm::normalize(glm::vec3(
                            boidsRandomFloat(-1.0f, 1.0f),
                            boidsRandomFloat(-1.0f, 1.0f),
                            boidsRandomFloat(-0.3f, 0.3f)));
                        c.speed = boidsRandomFloat(config.minSpeed, config.maxSpeed);
                        c.velocity = dir * c.speed;
                        c.size = boidsRandomFloat(0.8f, 1.2f);
                        c.textureIndex = Environment::CreatureAtlas::getBaseIndex(config.type);
                        c.animFrame = boidsRandomFloat(0.0f, 2.0f);
                        c.animSpeed = boidsRandomFloat(config.animSpeedMin, config.animSpeedMax);
                        flock.creatures.push_back(c);
                    }

                    // Set destination
                    glm::vec3 dest = boidsGetRandomSpawnPosition(playerPos);
                    dest.z = spawnPos.z;
                    flock.destination = dest;

                    boidsFlocks_.push_back(std::move(flock));
                }
            }
        }
    }

    // Update each flock
    for (auto& flock : boidsFlocks_) {
        flock.timeAlive += dt;

        // Handle scattering
        if (flock.scattering) {
            flock.scatterTimer -= dt;
            if (flock.scatterTimer <= 0.0f) flock.scattering = false;
        }

        // Update destination
        flock.destinationTimer -= dt;
        if (flock.destinationTimer <= 0.0f) {
            if (flock.hasBounds) {
                flock.destination = glm::vec3(
                    boidsRandomFloat(flock.boundsMin.x + 50.0f, flock.boundsMax.x - 50.0f),
                    boidsRandomFloat(flock.boundsMin.y + 50.0f, flock.boundsMax.y - 50.0f),
                    playerPos.z + boidsRandomFloat(flock.config.heightMin, flock.config.heightMax));
            } else {
                flock.destination = flock.anchor + glm::vec3(
                    boidsRandomFloat(-flock.config.patrolRadius, flock.config.patrolRadius),
                    boidsRandomFloat(-flock.config.patrolRadius, flock.config.patrolRadius),
                    boidsRandomFloat(-10.0f, 10.0f));
                flock.destination.z = std::max(flock.destination.z, playerPos.z + flock.config.heightMin);
            }
            float minZ = playerPos.z + flock.config.heightMin;
            float maxZ = playerPos.z + flock.config.heightMax;
            flock.destination.z = glm::clamp(flock.destination.z, minZ, maxZ);
            flock.destinationTimer = flock.destinationInterval;
        }

        // Boids per-creature update
        for (auto& creature : flock.creatures) {
            // Separation
            glm::vec3 separation{0.0f};
            int sepCount = 0;
            for (const auto& other : flock.creatures) {
                if (&other == &creature) continue;
                float d = glm::distance(creature.position, other.position);
                if (d < flock.config.separationRadius && d > 0.001f) {
                    separation += glm::normalize(creature.position - other.position) / d;
                    sepCount++;
                }
            }
            if (sepCount > 0) separation /= static_cast<float>(sepCount);

            // Alignment
            glm::vec3 alignment{0.0f};
            int alignCount = 0;
            for (const auto& other : flock.creatures) {
                if (&other == &creature) continue;
                if (glm::distance(creature.position, other.position) < flock.config.neighborRadius) {
                    alignment += other.velocity;
                    alignCount++;
                }
            }
            if (alignCount > 0) alignment = alignment / static_cast<float>(alignCount) - creature.velocity;

            // Cohesion
            glm::vec3 cohesion{0.0f};
            int cohCount = 0;
            for (const auto& other : flock.creatures) {
                if (&other == &creature) continue;
                if (glm::distance(creature.position, other.position) < flock.config.neighborRadius) {
                    cohesion += other.position;
                    cohCount++;
                }
            }
            if (cohCount > 0) {
                cohesion = cohesion / static_cast<float>(cohCount) - creature.position;
                if (glm::dot(cohesion, cohesion) > 0.01f) cohesion = glm::normalize(cohesion);
            }

            // Destination steer
            glm::vec3 destSteer{0.0f};
            glm::vec3 toDest = flock.destination - creature.position;
            if (glm::length(toDest) > 5.0f) destSteer = glm::normalize(toDest);

            // Bounds avoidance
            glm::vec3 boundsAvoid{0.0f};
            if (flock.hasBounds) {
                const float margin = 30.0f;
                if (creature.position.x < flock.boundsMin.x + margin)
                    boundsAvoid.x = (flock.boundsMin.x + margin - creature.position.x) / margin;
                else if (creature.position.x > flock.boundsMax.x - margin)
                    boundsAvoid.x = (flock.boundsMax.x - margin - creature.position.x) / margin;
                if (creature.position.y < flock.boundsMin.y + margin)
                    boundsAvoid.y = (flock.boundsMin.y + margin - creature.position.y) / margin;
                else if (creature.position.y > flock.boundsMax.y - margin)
                    boundsAvoid.y = (flock.boundsMax.y - margin - creature.position.y) / margin;
            }
            const float heightMargin = 5.0f;
            if (creature.position.z < flock.config.heightMin + heightMargin) {
                float depth = (flock.config.heightMin + heightMargin) - creature.position.z;
                boundsAvoid.z = 2.0f + depth * 0.5f;
            } else if (creature.position.z > flock.config.heightMax - heightMargin) {
                float excess = creature.position.z - (flock.config.heightMax - heightMargin);
                boundsAvoid.z = -1.0f - excess * 0.3f;
            }

            // Player avoidance
            glm::vec3 playerAvoid{0.0f};
            glm::vec3 toPlayer = creature.position - playerPos;
            float playerDist = glm::length(toPlayer);
            if (playerDist < 15.0f && playerDist > 0.001f) {
                playerAvoid = glm::normalize(toPlayer) * (15.0f - playerDist) / 15.0f;
            }

            // Terrain avoidance via HCMap
            glm::vec3 terrainAvoid{0.0f};
            if (zoneData_.hcMap && glm::length(creature.velocity) > 0.1f) {
                glm::vec3 dir = glm::normalize(creature.velocity);
                float lookAhead = 15.0f;
                glm::vec3 futurePos = creature.position + dir * lookAhead;
                glm::vec3 startAbove = futurePos;
                startAbove.z = creature.position.z + 10.0f;
                glm::vec3 groundResult;
                if (zoneData_.hcMap->FindBestZ(startAbove, &groundResult) != BEST_Z_INVALID) {
                    float groundZ = groundResult.z;
                    float clearance = creature.position.z - groundZ;
                    if (clearance < 5.0f) {
                        float urgency = 1.0f - (clearance / 5.0f);
                        terrainAvoid.z += urgency * 3.0f;
                        terrainAvoid -= dir * urgency * 1.5f;
                    }
                }
            }

            // Combine forces
            glm::vec3 accel =
                separation * flock.config.behavior.separation +
                alignment * flock.config.behavior.alignment +
                cohesion * flock.config.behavior.cohesion +
                destSteer * flock.config.behavior.destination +
                boundsAvoid * flock.config.behavior.avoidance +
                playerAvoid * flock.config.behavior.playerAvoidance +
                terrainAvoid * flock.config.behavior.avoidance;

            // Scatter force
            if (flock.scattering) {
                glm::vec3 away = creature.position - flock.scatterSource;
                if (glm::dot(away, away) > 0.01f)
                    accel += glm::normalize(away) * flock.scatterStrength * 20.0f;
            }

            // Apply acceleration and clamp velocity
            creature.velocity += accel * dt;
            float speed = glm::length(creature.velocity);
            if (speed < 0.001f) {
                creature.velocity = glm::normalize(glm::vec3(
                    boidsRandomFloat(-1.0f, 1.0f),
                    boidsRandomFloat(-1.0f, 1.0f),
                    boidsRandomFloat(-0.2f, 0.2f))) * flock.config.minSpeed;
            } else if (speed < flock.config.minSpeed) {
                creature.velocity = glm::normalize(creature.velocity) * flock.config.minSpeed;
            } else if (speed > flock.config.maxSpeed) {
                creature.velocity = glm::normalize(creature.velocity) * flock.config.maxSpeed;
            }
            float maxVert = flock.config.maxSpeed * 0.5f;
            creature.velocity.z = glm::clamp(creature.velocity.z, -maxVert, maxVert);

            // Update position
            creature.position += creature.velocity * dt;

            // Hard clamp Z
            float minZ = playerPos.z + flock.config.heightMin;
            float maxZ = playerPos.z + flock.config.heightMax;
            if (creature.position.z < minZ) {
                creature.position.z = minZ;
                if (creature.velocity.z < 0.0f) creature.velocity.z = 0.0f;
            }
            if (creature.position.z > maxZ) {
                creature.position.z = maxZ;
                if (creature.velocity.z > 0.0f) creature.velocity.z = 0.0f;
            }

            // Animation
            uint8_t baseIndex = Environment::CreatureAtlas::getBaseIndex(flock.config.type);
            if (creature.velocity.z > 1.0f) {
                creature.animFrame += creature.animSpeed * dt;
                creature.textureIndex = baseIndex + (static_cast<int>(creature.animFrame) % 2);
            } else {
                creature.animFrame = 0.0f;
                creature.textureIndex = baseIndex;
            }
        }

        // Update flock center
        if (!flock.creatures.empty()) {
            glm::vec3 sum{0.0f};
            for (const auto& c : flock.creatures) sum += c.position;
            flock.center = sum / static_cast<float>(flock.creatures.size());
        }

        // Check bounds exit
        if (flock.hasBounds) {
            const float margin = 50.0f;
            if (flock.center.x < flock.boundsMin.x - margin || flock.center.x > flock.boundsMax.x + margin ||
                flock.center.y < flock.boundsMin.y - margin || flock.center.y > flock.boundsMax.y + margin) {
                flock.exitedBounds = true;
            }
        }
    }

    // Check player proximity for scatter
    for (auto& flock : boidsFlocks_) {
        float d = glm::distance(flock.center, playerPos);
        if (d < boidsScatterRadius_) {
            float strength = 1.0f - (d / boidsScatterRadius_);
            flock.scattering = true;
            flock.scatterSource = playerPos;
            flock.scatterStrength = glm::clamp(strength, 0.0f, 1.0f);
            flock.scatterTimer = 3.0f;
        }
    }

    // Remove expired flocks
    const float maxLifetime = 300.0f;
    boidsFlocks_.erase(
        std::remove_if(boidsFlocks_.begin(), boidsFlocks_.end(),
            [maxLifetime](const WorkerFlockState& f) {
                return f.exitedBounds || f.timeAlive > maxLifetime;
            }),
        boidsFlocks_.end());

    // Build output
    output.boidsOutput.creatures.clear();
    int totalCreatures = 0;
    for (const auto& flock : boidsFlocks_) {
        if (!boidsTypeEnabled_[static_cast<size_t>(flock.config.type)]) continue;
        for (const auto& c : flock.creatures) {
            // Distance culling
            glm::vec3 diff = c.position - playerPos;
            if (glm::dot(diff, diff) > budget.cullDistance * budget.cullDistance) continue;

            SimulationOutput::BoidsOutput::CreatureRender cr;
            cr.position = c.position;
            cr.size = c.size;
            cr.textureIndex = c.textureIndex;
            cr.alpha = c.alpha;
            output.boidsOutput.creatures.push_back(cr);
            totalCreatures++;
        }
    }
    output.boidsOutput.activeCount = totalCreatures;
    output.boidsOutput.valid = true;
}

// ============================================================================
// Tumbleweed Simulation (worker thread)
// ============================================================================

float SimulationWorker::twRandomFloat(float minVal, float maxVal) {
    std::uniform_real_distribution<float> dist(minVal, maxVal);
    return dist(twRng_);
}

void SimulationWorker::computeTumbleweeds(const SimulationInput& input, SimulationOutput& output) {
    const auto& ti = input.tumbleweedInput;
    if (!ti.initialized) {
        output.tumbleweedOutput.valid = false;
        return;
    }

    // Process commands
    for (const auto& cmd : ti.commands) {
        switch (cmd.type) {
            case TumbleweedCommand::ZoneEnter:
                // Despawn all active
                for (auto& tw : twInstances_) tw.active = false;
                twZoneBiome_ = cmd.zoneBiome;
                twSpawnTimer_ = 0.0f;
                break;
            case TumbleweedCommand::ZoneLeave:
                for (auto& tw : twInstances_) tw.active = false;
                twZoneBiome_ = 0;
                break;
            case TumbleweedCommand::SetEnabled:
                twEnabled_ = cmd.enabled;
                if (!twEnabled_) {
                    for (auto& tw : twInstances_) tw.active = false;
                }
                break;
        }
    }

    output.tumbleweedOutput.spawns.clear();
    output.tumbleweedOutput.despawns.clear();

    if (!twEnabled_) {
        output.tumbleweedOutput.tumbleweeds.clear();
        output.tumbleweedOutput.activeCount = 0;
        output.tumbleweedOutput.valid = true;
        return;
    }

    // Only active in desert/plains with wind
    auto biome = static_cast<Environment::ZoneBiome>(twZoneBiome_);
    if (biome != Environment::ZoneBiome::Desert && biome != Environment::ZoneBiome::Plains) {
        output.tumbleweedOutput.tumbleweeds.clear();
        output.tumbleweedOutput.activeCount = 0;
        output.tumbleweedOutput.valid = true;
        return;
    }

    float dt = ti.deltaTime;
    glm::vec3 playerPos = ti.playerPosition;
    glm::vec3 windDir = ti.windDirection;
    float windStrength = weatherWindIntensity_;

    // Spawn logic (only if wind is blowing)
    if (windStrength >= 0.1f) {
        twSpawnTimer_ += dt;
        if (twSpawnTimer_ >= twSpawnCooldown_) {
            twSpawnTimer_ = 0.0f;

            // Count active
            int activeCount = 0;
            for (const auto& tw : twInstances_) if (tw.active) activeCount++;

            if (activeCount < twMaxActive_) {
                // Select spawn position upwind
                glm::vec3 spawnDir = -windDir;
                if (glm::length(spawnDir) < 0.01f) {
                    float angle = twRandomFloat(0.0f, 6.28f);
                    spawnDir = glm::vec3(std::cos(angle), std::sin(angle), 0.0f);
                }
                float angle = twRandomFloat(-1.05f, 1.05f);
                float cosA = std::cos(angle), sinA = std::sin(angle);
                spawnDir = glm::normalize(glm::vec3(
                    spawnDir.x * cosA - spawnDir.y * sinA,
                    spawnDir.x * sinA + spawnDir.y * cosA, 0.0f));

                glm::vec3 spawnPos = playerPos + spawnDir * twSpawnDistance_;

                // Get ground height via HCMap
                float groundZ = playerPos.z;  // Fallback
                if (zoneData_.hcMap) {
                    glm::vec3 startAbove = spawnPos;
                    startAbove.z = playerPos.z + 50.0f;
                    glm::vec3 groundResult;
                    if (zoneData_.hcMap->FindBestZ(startAbove, &groundResult) != BEST_Z_INVALID) {
                        groundZ = groundResult.z;
                    }
                }
                spawnPos.z = groundZ;

                // Validate (reasonable height)
                if (groundZ > -1000.0f && groundZ < 1000.0f) {
                    WorkerTumbleweedInstance tw;
                    tw.active = true;
                    tw.position = spawnPos;
                    tw.lifetime = 0.0f;
                    tw.bounceCount = 0;
                    tw.size = twRandomFloat(twSizeMin_, twSizeMax_);
                    tw.radius = tw.size * 0.5f;
                    tw.velocity = windDir * twRandomFloat(twMinSpeed_, twMaxSpeed_);
                    tw.velocity.z = 0.0f;
                    tw.rotation = glm::vec3(
                        twRandomFloat(0.0f, 360.0f),
                        twRandomFloat(0.0f, 360.0f),
                        twRandomFloat(0.0f, 360.0f));
                    tw.angularVelocity = glm::vec3(0.0f);
                    tw.poolIndex = twNextPoolIndex_++;

                    twInstances_.push_back(tw);

                    // Emit spawn event
                    output.tumbleweedOutput.spawns.push_back({tw.poolIndex, tw.size});
                }
            }
        }
    }

    // Update each instance
    for (auto& tw : twInstances_) {
        if (!tw.active) continue;

        // Wind force
        glm::vec3 windForce = windDir * windStrength * twWindInfluence_;
        tw.velocity += windForce * dt;

        // Drag
        float dragFactor = 1.0f - 0.3f * dt;
        tw.velocity.x *= dragFactor;
        tw.velocity.y *= dragFactor;

        // Clamp speed
        float speed = glm::length(glm::vec2(tw.velocity.x, tw.velocity.y));
        if (speed > twMaxSpeed_) {
            float scale = twMaxSpeed_ / speed;
            tw.velocity.x *= scale;
            tw.velocity.y *= scale;
            speed = twMaxSpeed_;
        }
        if (speed < twMinSpeed_ && windStrength > 0.2f) {
            if (speed > 0.01f) {
                float scale = twMinSpeed_ / speed;
                tw.velocity.x *= scale;
                tw.velocity.y *= scale;
            } else {
                tw.velocity = windDir * twMinSpeed_;
                tw.velocity.z = 0.0f;
            }
            speed = twMinSpeed_;
        }

        // Predict next position
        glm::vec3 nextPos = tw.position + tw.velocity * dt;

        // Ground following via HCMap
        float groundZ = tw.position.z;
        if (zoneData_.hcMap) {
            glm::vec3 startAbove = nextPos;
            startAbove.z = playerPos.z + 50.0f;
            glm::vec3 groundResult;
            if (zoneData_.hcMap->FindBestZ(startAbove, &groundResult) != BEST_Z_INVALID) {
                groundZ = groundResult.z;
            }
        }
        float targetZ = groundZ + tw.radius + twGroundOffset_;
        float zDiff = targetZ - nextPos.z;
        if (std::abs(zDiff) > 0.1f) {
            if (zDiff < -2.0f) {
                tw.velocity.z -= 10.0f * dt;
            } else if (zDiff > 2.0f) {
                // Hit a rise, bounce
                glm::vec3 moveDir = glm::normalize(glm::vec3(tw.velocity.x, tw.velocity.y, 0.0f));
                glm::vec3 normal = -moveDir;
                normal.z = 0.3f;
                normal = glm::normalize(normal);
                float dot = glm::dot(tw.velocity, normal);
                tw.velocity = tw.velocity - 2.0f * dot * normal;
                tw.velocity *= twBounceDecay_;
                tw.velocity.x += twRandomFloat(-0.5f, 0.5f);
                tw.velocity.y += twRandomFloat(-0.5f, 0.5f);
                tw.bounceCount++;
                continue;  // Skip position update this frame
            }
        }
        nextPos.z = targetZ;

        // Wall collision via HCMap::CheckLOS
        bool wallHit = false;
        if (zoneData_.hcMap) {
            glm::vec3 hitPoint;
            if (!zoneData_.hcMap->CheckLOSWithHit(tw.position, nextPos, &hitPoint)) {
                // LOS blocked (returns false) = wall hit
                wallHit = true;
                glm::vec3 moveDir = glm::normalize(nextPos - tw.position);
                glm::vec3 normal = -moveDir;
                normal.z = std::min(normal.z, 0.3f);
                if (glm::length(normal) > 0.01f) normal = glm::normalize(normal);
                else normal = glm::vec3(1.0f, 0.0f, 0.0f);

                float dot = glm::dot(tw.velocity, normal);
                tw.velocity = tw.velocity - 2.0f * dot * normal;
                tw.velocity *= twBounceDecay_;
                tw.velocity.x += twRandomFloat(-0.5f, 0.5f);
                tw.velocity.y += twRandomFloat(-0.5f, 0.5f);
                tw.bounceCount++;
                tw.position = hitPoint + normal * (tw.radius + 0.2f);
            }
        }

        if (!wallHit) {
            tw.position = nextPos;
        }

        // Update rotation
        if (speed > 0.1f) {
            float rollSpeed = speed / tw.radius * 57.3f;
            glm::vec2 moveDir2 = glm::normalize(glm::vec2(tw.velocity.x, tw.velocity.y));
            tw.angularVelocity = glm::vec3(
                rollSpeed * moveDir2.y,
                -rollSpeed * moveDir2.x,
                twRandomFloat(-10.0f, 10.0f));
        }
        tw.rotation += tw.angularVelocity * dt;
        tw.rotation.x = std::fmod(tw.rotation.x, 360.0f);
        tw.rotation.y = std::fmod(tw.rotation.y, 360.0f);
        tw.rotation.z = std::fmod(tw.rotation.z, 360.0f);

        // Lifetime
        tw.lifetime += dt;

        // Despawn checks
        bool shouldDespawn = false;
        if (glm::distance(tw.position, playerPos) > twDespawnDistance_) shouldDespawn = true;
        if (tw.lifetime > twMaxLifetime_) shouldDespawn = true;
        if (tw.bounceCount > static_cast<uint32_t>(twMaxBounces_)) shouldDespawn = true;

        if (shouldDespawn) {
            tw.active = false;
            output.tumbleweedOutput.despawns.push_back({tw.poolIndex});
        }
    }

    // Remove inactive from vector
    twInstances_.erase(
        std::remove_if(twInstances_.begin(), twInstances_.end(),
            [](const WorkerTumbleweedInstance& tw) { return !tw.active; }),
        twInstances_.end());

    // Build output
    output.tumbleweedOutput.tumbleweeds.clear();
    int activeCount = 0;
    for (const auto& tw : twInstances_) {
        if (!tw.active) continue;
        SimulationOutput::TumbleweedOutput::TumbleweedRender tr;
        tr.position = tw.position;
        tr.rotation = tw.rotation;
        tr.size = tw.size;
        tr.active = true;
        tr.poolIndex = tw.poolIndex;
        output.tumbleweedOutput.tumbleweeds.push_back(tr);
        activeCount++;
    }
    output.tumbleweedOutput.activeCount = activeCount;
    output.tumbleweedOutput.valid = true;
}

// ============================================================================
// Weather System (state machine + transitions, owned by worker)
// ============================================================================

// Alias to disambiguate from Environment::WeatherType (particle weather)
using TreeWeatherType = ::EQT::Graphics::WeatherType;

void SimulationWorker::computeWeather(const SimulationInput& input, SimulationOutput& output) {
    const auto& wi = input.weatherInput;
    if (!wi.initialized) {
        output.weatherOutput.valid = false;
        return;
    }

    // Track weather type at start for change detection
    uint8_t weatherAtStart = weatherCurrentWeather_;

    // Process commands
    for (const auto& cmd : wi.commands) {
        switch (cmd.type) {
            case WeatherCommand::SetZoneConfig: {
                weatherZoneConfig_ = cmd.zoneConfig;
                weatherTimeSinceLastCheck_ = 0.0f;
                weatherCurrentElapsed_ = 0.0f;
                weatherCurrentDuration_ = 0.0f;
                // Set initial weather based on zone default
                if (cmd.zoneConfig.enabled) {
                    uint8_t newType = static_cast<uint8_t>(cmd.zoneConfig.defaultWeather);
                    weatherCurrentWeather_ = newType;
                    weatherTargetWeather_ = newType;
                    weatherTransitionProgress_ = 1.0f;
                    weatherCurrentElapsed_ = 0.0f;
                    weatherCurrentDuration_ = 0.0f;
                }
                break;
            }
            case WeatherCommand::SetWeatherFromZone: {
                ZoneWeatherConfig config;
                if (loadZoneWeatherConfig(cmd.zoneName, config)) {
                    LOG_INFO(MOD_GRAPHICS, "WeatherSystem(worker): Loaded weather config for zone '{}'", cmd.zoneName);
                } else {
                    config.zoneName = cmd.zoneName;
                    config.defaultWeather = TreeWeatherType::Normal;
                    config.enabled = true;
                }
                weatherZoneConfig_ = config;
                weatherTimeSinceLastCheck_ = 0.0f;
                weatherCurrentElapsed_ = 0.0f;
                weatherCurrentDuration_ = 0.0f;
                if (config.enabled) {
                    uint8_t newType = static_cast<uint8_t>(config.defaultWeather);
                    weatherCurrentWeather_ = newType;
                    weatherTargetWeather_ = newType;
                    weatherTransitionProgress_ = 1.0f;
                    weatherCurrentElapsed_ = 0.0f;
                    weatherCurrentDuration_ = 0.0f;
                }
                break;
            }
            case WeatherCommand::SetWeatherImmediate: {
                uint8_t newType = cmd.weatherType;
                if (newType != weatherCurrentWeather_ || weatherTransitionProgress_ < 1.0f) {
                    weatherCurrentWeather_ = newType;
                    weatherTargetWeather_ = newType;
                    weatherTransitionProgress_ = 1.0f;
                    weatherCurrentElapsed_ = 0.0f;
                    weatherCurrentDuration_ = 0.0f;
                }
                break;
            }
            case WeatherCommand::TransitionToWeather: {
                uint8_t newType = cmd.weatherType;
                if (newType != weatherTargetWeather_ || weatherTransitionProgress_ < 1.0f) {
                    weatherTargetWeather_ = newType;
                    weatherTransitionDuration_ = std::max(0.1f, cmd.transitionTime);
                    weatherTransitionProgress_ = 0.0f;
                    weatherCurrentElapsed_ = 0.0f;
                    weatherCurrentDuration_ = 0.0f;
                }
                break;
            }
            case WeatherCommand::SetSimulationEnabled:
                weatherSimulationEnabled_ = cmd.simulationEnabled;
                break;
        }
    }

    float dt = wi.deltaTime;

    // Advance transition if in progress
    if (weatherTransitionProgress_ < 1.0f) {
        weatherTransitionProgress_ += dt / weatherTransitionDuration_;
        if (weatherTransitionProgress_ >= 1.0f) {
            weatherTransitionProgress_ = 1.0f;
            weatherCurrentWeather_ = weatherTargetWeather_;
        }
    }

    // Run simulation timer
    if (weatherSimulationEnabled_ && weatherZoneConfig_.enabled) {
        weatherTimeSinceLastCheck_ += dt;
        weatherCurrentElapsed_ += dt;

        bool shouldCheck = (weatherTimeSinceLastCheck_ >= weatherZoneConfig_.checkIntervalSeconds);
        bool durationExpired = (weatherCurrentDuration_ > 0 &&
                               weatherCurrentElapsed_ >= weatherCurrentDuration_);

        if (shouldCheck || durationExpired) {
            weatherTimeSinceLastCheck_ = 0.0f;
            weatherCheckChange();
        }
    }

    // Compute wind intensity with smoothstep interpolation
    float currentIntensity = weatherGetWindIntensity(weatherCurrentWeather_);
    float targetIntensity = weatherGetWindIntensity(weatherTargetWeather_);
    if (weatherTransitionProgress_ < 1.0f) {
        float t = weatherTransitionProgress_;
        float smooth = t * t * (3.0f - 2.0f * t);
        weatherWindIntensity_ = currentIntensity + (targetIntensity - currentIntensity) * smooth;
    } else {
        weatherWindIntensity_ = currentIntensity;
    }

    // Detect weather changes (from commands or simulation rolls)
    bool changed = (weatherCurrentWeather_ != weatherAtStart) ||
                   (weatherTargetWeather_ != weatherAtStart && weatherTransitionProgress_ < 1.0f);

    // Populate output
    auto& wo = output.weatherOutput;
    wo.currentWeather = weatherCurrentWeather_;
    wo.targetWeather = weatherTargetWeather_;
    wo.transitionProgress = weatherTransitionProgress_;
    wo.windIntensity = weatherWindIntensity_;
    wo.weatherChanged = changed;
    wo.newWeatherType = changed ? weatherTargetWeather_ : weatherCurrentWeather_;
    wo.valid = true;
}

void SimulationWorker::weatherCheckChange() {
    // If we're in non-default weather and duration expired, return to default
    uint8_t defaultType = static_cast<uint8_t>(weatherZoneConfig_.defaultWeather);
    if (weatherCurrentWeather_ != defaultType &&
        weatherCurrentDuration_ > 0 &&
        weatherCurrentElapsed_ >= weatherCurrentDuration_) {

        weatherTargetWeather_ = defaultType;
        weatherTransitionDuration_ = 10.0f;
        weatherTransitionProgress_ = 0.0f;
        weatherCurrentElapsed_ = 0.0f;
        weatherCurrentDuration_ = 0.0f;
        return;
    }

    // Roll for new weather
    uint8_t newWeather = weatherRollForWeather();

    if (newWeather != weatherCurrentWeather_) {
        weatherTargetWeather_ = newWeather;
        weatherTransitionDuration_ = 10.0f;
        weatherTransitionProgress_ = 0.0f;
        weatherCurrentElapsed_ = 0.0f;

        // Set duration for the new weather
        if (newWeather == static_cast<uint8_t>(TreeWeatherType::Rain)) {
            std::uniform_int_distribution<int> slot(0, 3);
            int idx = slot(weatherRng_);
            weatherCurrentDuration_ = weatherZoneConfig_.rainDuration[idx] * 60.0f;
        } else if (newWeather == static_cast<uint8_t>(TreeWeatherType::Storm)) {
            std::uniform_real_distribution<float> duration(60.0f, 300.0f);
            weatherCurrentDuration_ = duration(weatherRng_);
        } else {
            weatherCurrentDuration_ = 0.0f;
        }
    }
}

uint8_t SimulationWorker::weatherRollForWeather() {
    // Check rain chance
    int totalRainChance = 0;
    for (int i = 0; i < 4; i++) {
        totalRainChance += weatherZoneConfig_.rainChance[i];
    }

    if (totalRainChance > 0) {
        std::uniform_int_distribution<int> roll(0, 399);
        int rainRoll = roll(weatherRng_);

        if (rainRoll < totalRainChance) {
            std::uniform_int_distribution<int> intensity(0, 99);
            int intensityRoll = intensity(weatherRng_);

            if (intensityRoll > 80) {
                return static_cast<uint8_t>(TreeWeatherType::Storm);
            } else if (intensityRoll > 40) {
                return static_cast<uint8_t>(TreeWeatherType::Rain);
            } else {
                return static_cast<uint8_t>(TreeWeatherType::Normal);
            }
        }
    }

    // Check for calm conditions
    std::uniform_int_distribution<int> calmRoll(0, 99);
    if (calmRoll(weatherRng_) < 20) {
        return static_cast<uint8_t>(TreeWeatherType::Calm);
    }

    return static_cast<uint8_t>(weatherZoneConfig_.defaultWeather);
}

float SimulationWorker::weatherGetWindIntensity(uint8_t type) {
    switch (static_cast<TreeWeatherType>(type)) {
        case TreeWeatherType::Calm:   return 0.3f;
        case TreeWeatherType::Normal: return 0.6f;
        case TreeWeatherType::Rain:   return 0.8f;
        case TreeWeatherType::Storm:  return 1.0f;
        default:                      return 0.6f;
    }
}

// ============================================================================
// Spell VFX Computation (desktop GL worker-driven path)
// ============================================================================

void SimulationWorker::computeSpellVFX(const SimulationInput& input, SimulationOutput& output) {
    auto& svi = input.spellVfxInput;
    auto& svo = output.spellVfxOutput;

    if (!svi.initialized) {
        svo.valid = false;
        return;
    }

    svo.effectUpdates.clear();
    svo.createEvents.clear();
    svo.removeEvents.clear();
    svo.impactEvents.clear();
    svo.valid = true;

    float dt = svi.deltaTime;

    // SpellFXType values (must match EQ::SpellFXType enum)
    constexpr uint8_t FX_CAST_GLOW = 1;
    constexpr uint8_t FX_PROJECTILE = 2;
    constexpr uint8_t FX_IMPACT = 3;
    constexpr uint8_t FX_AURA = 4;
    constexpr uint8_t FX_RAIN = 5;
    constexpr uint8_t FX_GROUND_CIRCLE = 7;

    // Process commands
    for (const auto& cmd : svi.commands) {
        switch (cmd.type) {
            case SpellVFXCommand::CreateEffect: {
                WorkerSpellEffect wfx;
                wfx.effectId = cmd.effectId;
                wfx.type = cmd.fxType;
                wfx.spellId = cmd.spellId;
                wfx.sourceEntity = cmd.sourceEntity;
                wfx.targetEntity = cmd.targetEntity;
                wfx.lifetime = cmd.lifetime;
                wfx.scale = cmd.scale;
                wfx.colorA = cmd.colorA;
                wfx.colorR = cmd.colorR;
                wfx.colorG = cmd.colorG;
                wfx.colorB = cmd.colorB;
                wfx.posX = cmd.posX;
                wfx.posY = cmd.posY;
                wfx.posZ = cmd.posZ;
                wfx.targetPosX = cmd.targetPosX;
                wfx.targetPosY = cmd.targetPosY;
                wfx.targetPosZ = cmd.targetPosZ;
                wfx.elapsed = 0;
                wfx.active = true;

                // Resolve initial positions from workerEntities_ if entity-attached
                if (wfx.sourceEntity != 0 && wfx.posX == 0 && wfx.posY == 0 && wfx.posZ == 0) {
                    auto it = workerEntities_.find(wfx.sourceEntity);
                    if (it != workerEntities_.end()) {
                        const auto& we = it->second;
                        wfx.posX = we.lastX;
                        wfx.posY = we.lastZ + we.modelYOffset;  // EQ Z → Irrlicht Y
                        wfx.posZ = we.lastY;                     // EQ Y → Irrlicht Z
                    }
                }
                if (wfx.targetEntity != 0 && wfx.targetPosX == 0 && wfx.targetPosY == 0 && wfx.targetPosZ == 0) {
                    auto it = workerEntities_.find(wfx.targetEntity);
                    if (it != workerEntities_.end()) {
                        const auto& we = it->second;
                        wfx.targetPosX = we.lastX;
                        wfx.targetPosY = we.lastZ + we.modelYOffset;
                        wfx.targetPosZ = we.lastY;
                    }
                }

                // Emit create event for main thread
                SimulationOutput::SpellVFXOutput::CreateEvent ce;
                ce.effectId = wfx.effectId;
                ce.fxType = wfx.type;
                ce.spellId = wfx.spellId;
                ce.sourceEntity = wfx.sourceEntity;
                ce.targetEntity = wfx.targetEntity;
                ce.posX = wfx.posX;
                ce.posY = wfx.posY;
                ce.posZ = wfx.posZ;
                ce.targetPosX = wfx.targetPosX;
                ce.targetPosY = wfx.targetPosY;
                ce.targetPosZ = wfx.targetPosZ;
                ce.lifetime = wfx.lifetime;
                ce.scale = wfx.scale;
                ce.colorA = wfx.colorA;
                ce.colorR = wfx.colorR;
                ce.colorG = wfx.colorG;
                ce.colorB = wfx.colorB;
                svo.createEvents.push_back(ce);

                spellVfxEffects_.push_back(wfx);
                break;
            }
            case SpellVFXCommand::RemoveCastGlow: {
                for (size_t i = 0; i < spellVfxEffects_.size(); ) {
                    if (spellVfxEffects_[i].type == FX_CAST_GLOW &&
                        spellVfxEffects_[i].sourceEntity == cmd.sourceEntity) {
                        svo.removeEvents.push_back({spellVfxEffects_[i].effectId});
                        if (i != spellVfxEffects_.size() - 1)
                            spellVfxEffects_[i] = std::move(spellVfxEffects_.back());
                        spellVfxEffects_.pop_back();
                    } else {
                        ++i;
                    }
                }
                break;
            }
            case SpellVFXCommand::RemoveBuffAura: {
                for (size_t i = 0; i < spellVfxEffects_.size(); ) {
                    if (spellVfxEffects_[i].type == FX_AURA &&
                        spellVfxEffects_[i].sourceEntity == cmd.sourceEntity &&
                        spellVfxEffects_[i].spellId == cmd.spellId) {
                        svo.removeEvents.push_back({spellVfxEffects_[i].effectId});
                        if (i != spellVfxEffects_.size() - 1)
                            spellVfxEffects_[i] = std::move(spellVfxEffects_.back());
                        spellVfxEffects_.pop_back();
                    } else {
                        ++i;
                    }
                }
                break;
            }
            case SpellVFXCommand::RemoveAllForEntity: {
                for (size_t i = 0; i < spellVfxEffects_.size(); ) {
                    if (spellVfxEffects_[i].sourceEntity == cmd.sourceEntity ||
                        spellVfxEffects_[i].targetEntity == cmd.sourceEntity) {
                        svo.removeEvents.push_back({spellVfxEffects_[i].effectId});
                        if (i != spellVfxEffects_.size() - 1)
                            spellVfxEffects_[i] = std::move(spellVfxEffects_.back());
                        spellVfxEffects_.pop_back();
                    } else {
                        ++i;
                    }
                }
                break;
            }
            case SpellVFXCommand::ClearAll: {
                for (auto& fx : spellVfxEffects_) {
                    svo.removeEvents.push_back({fx.effectId});
                }
                spellVfxEffects_.clear();
                break;
            }
        }
    }

    // Update active effects
    constexpr float PROJECTILE_SPEED = 500.0f;

    for (size_t i = 0; i < spellVfxEffects_.size(); ) {
        auto& fx = spellVfxEffects_[i];
        if (!fx.active) {
            svo.removeEvents.push_back({fx.effectId});
            if (i != spellVfxEffects_.size() - 1)
                spellVfxEffects_[i] = std::move(spellVfxEffects_.back());
            spellVfxEffects_.pop_back();
            continue;
        }

        fx.elapsed += dt;

        // Check lifetime expiration
        if (fx.lifetime > 0 && fx.elapsed >= fx.lifetime) {
            svo.removeEvents.push_back({fx.effectId});
            if (i != spellVfxEffects_.size() - 1)
                spellVfxEffects_[i] = std::move(spellVfxEffects_.back());
            spellVfxEffects_.pop_back();
            continue;
        }

        switch (fx.type) {
            case FX_PROJECTILE: {
                // Update target position from entity
                if (fx.targetEntity != 0) {
                    auto it = workerEntities_.find(fx.targetEntity);
                    if (it != workerEntities_.end()) {
                        const auto& we = it->second;
                        fx.targetPosX = we.lastX;
                        fx.targetPosY = we.lastZ + we.modelYOffset;
                        fx.targetPosZ = we.lastY;
                    }
                }

                // Move toward target
                float dx = fx.targetPosX - fx.posX;
                float dy = fx.targetPosY - fx.posY;
                float dz = fx.targetPosZ - fx.posZ;
                float dist = std::sqrt(dx * dx + dy * dy + dz * dz);

                if (dist < 3.0f) {
                    // Arrived — emit impact event and deactivate
                    svo.impactEvents.push_back({fx.targetEntity, fx.spellId});
                    fx.active = false;
                    // Will be removed next iteration
                } else {
                    float invDist = 1.0f / dist;
                    float moveAmount = PROJECTILE_SPEED * dt;
                    if (moveAmount > dist) moveAmount = dist;
                    fx.posX += dx * invDist * moveAmount;
                    fx.posY += dy * invDist * moveAmount;
                    fx.posZ += dz * invDist * moveAmount;

                    SimulationOutput::SpellVFXOutput::EffectUpdate eu;
                    eu.effectId = fx.effectId;
                    eu.posX = fx.posX;
                    eu.posY = fx.posY;
                    eu.posZ = fx.posZ;
                    eu.hasBillboardUpdate = true;
                    eu.hasParticleUpdate = false;
                    eu.hasColorUpdate = false;
                    eu.billboardWidth = 0;
                    eu.billboardHeight = 0;
                    svo.effectUpdates.push_back(eu);
                }
                break;
            }
            case FX_CAST_GLOW: {
                // Follow source entity
                if (fx.sourceEntity != 0) {
                    auto it = workerEntities_.find(fx.sourceEntity);
                    if (it != workerEntities_.end()) {
                        const auto& we = it->second;
                        fx.posX = we.lastX;
                        fx.posY = we.lastZ + we.modelYOffset;
                        fx.posZ = we.lastY;
                    }
                }

                // Compute sine pulse
                float pulse = 0.7f + 0.3f * std::sin(fx.elapsed * 6.0f);
                float alpha = 80.0f + 40.0f * std::sin(fx.elapsed * 6.0f);

                SimulationOutput::SpellVFXOutput::EffectUpdate eu;
                eu.effectId = fx.effectId;
                // Billboard at chest height (+3 Irrlicht Y)
                eu.posX = fx.posX;
                eu.posY = fx.posY + 3.0f;
                eu.posZ = fx.posZ;
                eu.billboardWidth = 12.0f * pulse;
                eu.billboardHeight = 12.0f * pulse;
                eu.hasBillboardUpdate = true;
                // Particle system at base height (+1 Irrlicht Y)
                eu.particlePosX = fx.posX;
                eu.particlePosY = fx.posY + 1.0f;
                eu.particlePosZ = fx.posZ;
                eu.hasParticleUpdate = true;
                // Color with pulsing alpha
                eu.colorA = static_cast<uint8_t>(std::max(0.0f, std::min(255.0f, alpha)));
                eu.colorR = fx.colorR;
                eu.colorG = fx.colorG;
                eu.colorB = fx.colorB;
                eu.hasColorUpdate = true;
                svo.effectUpdates.push_back(eu);
                break;
            }
            case FX_AURA: {
                // Follow source entity
                if (fx.sourceEntity != 0) {
                    auto it = workerEntities_.find(fx.sourceEntity);
                    if (it != workerEntities_.end()) {
                        const auto& we = it->second;
                        fx.posX = we.lastX;
                        fx.posY = we.lastZ + we.modelYOffset;
                        fx.posZ = we.lastY;
                    }
                }

                SimulationOutput::SpellVFXOutput::EffectUpdate eu;
                eu.effectId = fx.effectId;
                eu.particlePosX = fx.posX;
                eu.particlePosY = fx.posY;
                eu.particlePosZ = fx.posZ;
                eu.hasBillboardUpdate = false;
                eu.hasParticleUpdate = true;
                eu.hasColorUpdate = false;
                svo.effectUpdates.push_back(eu);
                break;
            }
            default:
                // ImpactBurst, RainEffect, GroundCircle — no per-frame update needed
                // (Irrlicht internal particle systems drive them)
                break;
        }

        ++i;
    }
}

} // namespace Graphics
} // namespace EQT
