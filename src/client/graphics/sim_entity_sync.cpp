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
    int entNoState = 0, entMaxCap = 0, entDistCulled = 0, entFrustumCulled = 0;
    int entPvsCulled = 0, entOcclusionCulled = 0;

    for (const auto& ed : distances) {
        auto& er = output.entityResults[ed.resultIdx];
        auto wit = workerEntities_.find(er.spawnId);
        if (wit == workerEntities_.end()) {
            er.shouldBeVisible = false;
            entNoState++;
            continue;
        }
        const auto& state = wit->second;

        // Always show player
        if (state.isPlayer) {
            er.shouldBeVisible = true;
            visibleCount++;
            continue;
        }

        // Track culling reason for TRACE logging
        const char* cullReason = nullptr;

        bool visible = true;
        if (visibleCount >= maxEntities) {
            visible = false;
            cullReason = "maxCap";
            entMaxCap++;
        } else if (ed.distanceSq > maxDistSq) {
            visible = false;
            cullReason = "distance";
            entDistCulled++;
        }

        // Frustum culling
        int frustumFailPlane = -1;
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
                if (dot < -radius) {
                    insideFrustum = false;
                    frustumFailPlane = p;
                }
            }
            if (!insideFrustum) {
                visible = false;
                cullReason = "frustum";
                entFrustumCulled++;
            }
        }

        // PVS culling — check if entity's BSP region is PVS-visible from camera region
        // (Portal BFS walk is too aggressive for entities — entities should follow the same
        // PVS visibility as their containing region's geometry, matching 1999-era best practice)
        if (visible && zoneData_.usePvsCulling && zoneData_.bspTree &&
            output.currentPvsRegion != SIZE_MAX) {
            // Re-lookup BSP region if dirty
            if (state.bspRegionDirty || state.cachedBspRegion == SIZE_MAX) {
                size_t entityRegion = zoneData_.bspTree->findRegionIndexForPoint(
                    er.posX, er.posY, er.posZ);
                er.cachedBspRegion = entityRegion;
                er.bspRegionDirty = false;
            }
            if (er.cachedBspRegion != SIZE_MAX) {
                const auto& regions = zoneData_.bspTree->regions;
                if (output.currentPvsRegion < regions.size() && regions[output.currentPvsRegion]) {
                    const auto& pvs = regions[output.currentPvsRegion]->visibleRegions;
                    if (!pvs.empty() && er.cachedBspRegion < pvs.size() && !pvs[er.cachedBspRegion]) {
                        visible = false;
                        cullReason = "pvs";
                        entPvsCulled++;
                    }
                }
            }
        }

        // Occlusion test
        if (visible && workerOcclusionCuller_ && workerOcclusionCuller_->isEnabled()) {
            if (workerOcclusionCuller_->testPoint(er.posX, er.posY, er.posZ + 3.0f)) {
                visible = false;
                cullReason = "occlusion";
                entOcclusionCulled++;
            }
        }

        // Log visibility transitions (entity was visible last frame but not now, or vice versa)
        bool wasVisible = (prevEntityVisibility_.count(er.spawnId) && prevEntityVisibility_[er.spawnId]);
        if (wasVisible && !visible) {
            LOG_DEBUG(MOD_GRAPHICS, "EntityVis: spawn={} HIDDEN by {} pos=({:.1f},{:.1f},{:.1f}) dist={:.0f}{}",
                      er.spawnId, cullReason ? cullReason : "unknown",
                      er.posX, er.posY, er.posZ, std::sqrt(ed.distanceSq),
                      frustumFailPlane >= 0 ? fmt::format(" plane={}", frustumFailPlane) : "");
        } else if (!wasVisible && visible) {
            LOG_DEBUG(MOD_GRAPHICS, "EntityVis: spawn={} SHOWN pos=({:.1f},{:.1f},{:.1f}) dist={:.0f}",
                      er.spawnId, er.posX, er.posY, er.posZ, std::sqrt(ed.distanceSq));
        }

        // Per-entity TRACE logging
        if (!visible) {
            LOG_TRACE(MOD_GRAPHICS, "EntityVis: spawn={} culled={} pos=({:.1f},{:.1f},{:.1f}) dist={:.0f}{}",
                      er.spawnId, cullReason ? cullReason : "unknown",
                      er.posX, er.posY, er.posZ, std::sqrt(ed.distanceSq),
                      frustumFailPlane >= 0 ? fmt::format(" plane={}", frustumFailPlane) : "");
        }

        er.shouldBeVisible = visible;
        if (visible) visibleCount++;
    }

    // Update previous visibility map for transition detection
    prevEntityVisibility_.clear();
    for (const auto& er : output.entityResults) {
        prevEntityVisibility_[er.spawnId] = er.shouldBeVisible;
    }

    output.entityVisibleCount = visibleCount;

    LOG_DEBUG(MOD_GRAPHICS, "EntityVis: {}/{} visible (noState={}, maxCap={}, dist={}, frustum={}, pvs={}, occl={})",
              visibleCount, output.entityResults.size(), entNoState, entMaxCap,
              entDistCulled, entFrustumCulled, entPvsCulled, entOcclusionCulled);
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

        // Occlusion test
        if (visible && workerOcclusionCuller_ && workerOcclusionCuller_->isEnabled()) {
            if (workerOcclusionCuller_->testPoint(er.posX, er.posY, er.posZ + 3.0f)) {
                visible = false;
            }
        }

        er.nameTagVisible = visible;
    }
}

} // namespace Graphics
} // namespace EQT
