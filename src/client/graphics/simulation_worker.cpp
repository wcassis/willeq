#include "client/graphics/simulation_worker.h"
#include "client/graphics/portal_system.h"
#include "client/graphics/eq/wld_loader.h"
#include "client/graphics/environment/unified_particle.h"
#include "client/graphics/environment/spell_particle_types.h"
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

    // Init flicker phases
    flickerPhases_.resize(zoneData_.objectLights.size(), 0.0f);
    for (size_t i = 0; i < flickerPhases_.size(); ++i) {
        // Randomize initial phase so lights don't flicker in sync
        flickerPhases_[i] = static_cast<float>(i) * 1.37f;
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
    flickerPhases_.clear();
    for (int i = 0; i < 2; ++i) {
        output_[i] = SimulationOutput();
    }

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

const SimulationOutput* SimulationWorker::getFrontBuffer() const {
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

    // Critical tier — every frame: visibility + light selection
    computeVisibility(input, output);
    computeObjectVisibility(input, output);
    computeLightVisibility(input, output);
    computeLightSelection(input, output);

    // Normal tier — every frame: portals, fire flicker, trees, vertex anims, particles
    computePortalVisibility(input, output);
    computeFireFlicker(input, output);
    computeTreeAnimation(input, output);
    computeVertexAnimations(input, output);
    computeParticles(input, output);

    // Background tier — every kBackgroundInterval frames
    if (workerFrameCount_ % kBackgroundInterval == 0) {
        computeLightAnimations(input, output);
        computeSkyState(input, output);
        computeWeatherEffectsState(input, output);
    }
}

// ============================================================================
// PVS Visibility Computation
// ============================================================================

void SimulationWorker::computeVisibility(const SimulationInput& input, SimulationOutput& output) {
    if (!zoneData_.bspTree || zoneData_.regionBounds.empty()) return;

    const auto& bsp = *zoneData_.bspTree;
    size_t regionCount = zoneData_.regionBounds.size();

    // Ensure output is sized correctly
    if (output.regionVisible.size() != regionCount) {
        output.regionVisible.resize(regionCount, 0);
    }

    // BSP lookup — find which region the camera is in (EQ Z-up coords)
    size_t cameraRegion = bsp.findRegionIndexForPoint(input.camEqX, input.camEqY, input.camEqZ);
    output.currentPvsRegion = cameraRegion;

    // Clear sorted draw list
    output.sortedRegions.clear();
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

        // PVS check: if we have PVS data, only show regions visible from camera's region
        if (zoneData_.usePvsCulling && currentRegion) {
            if (!currentRegion->visibleRegions.empty() &&
                regionIdx < currentRegion->visibleRegions.size() &&
                !currentRegion->visibleRegions[regionIdx]) {
                output.regionVisible[i] = 0;
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

        // Add to sorted draw list
        output.sortedRegions.push_back({regionIdx, distSq});

        // Queue for lazy mesh loading (main thread filters already-loaded via isLoaded())
        output.meshLoadQueue.push_back(regionIdx);

        // Track for mesh cache protection
        output.protectedRegions.push_back(regionIdx);
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

} // namespace Graphics
} // namespace EQT
