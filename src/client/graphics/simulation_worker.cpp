#include "client/graphics/simulation_worker.h"
#include "client/graphics/eq/wld_loader.h"
#include "common/logging.h"
#include <algorithm>
#include <cmath>

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
    computeVisibility(input, output);
    computeObjectVisibility(input, output);
    computeLightVisibility(input, output);
    computeLightSelection(input, output);
    computeFireFlicker(input, output);
    computeTreeAnimation(input, output);
    computeVertexAnimations(input, output);
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

        // Track for mesh cache protection
        output.protectedRegions.push_back(regionIdx);
    }

    // Sort front-to-back by distance
    std::sort(output.sortedRegions.begin(), output.sortedRegions.end(),
              [](const auto& a, const auto& b) { return a.distanceSq < b.distanceSq; });
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

} // namespace Graphics
} // namespace EQT
