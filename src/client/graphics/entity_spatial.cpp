#include "client/graphics/entity_renderer.h"
#include "client/graphics/simulation_worker.h"
#include "client/graphics/entity_prep_worker.h"
#include "client/graphics/constrained_renderer_config.h"
#include "client/graphics/constrained_texture_cache.h"
#include "client/graphics/light_source.h"
#include "client/graphics/eq/zone_geometry.h"
#include "client/graphics/eq/race_model_loader.h"
#include "client/graphics/eq/race_codes.h"
#include "client/graphics/eq/dds_decoder.h"
#include "client/graphics/eq/animated_mesh_scene_node.h"
#include "client/graphics/eq/skeletal_animator.h"
#include "client/animation_constants.h"
#include "common/logging.h"
#include "common/name_utils.h"
#include "common/performance_metrics.h"
#include <algorithm>
#include <iostream>
#include <cmath>
#include <limits>
#include <set>
#include <unordered_set>
#include <chrono>
#include <fstream>
#include <fmt/ranges.h>

namespace EQT {
namespace Graphics {

// ============================================================================
// Spatial Grid Functions - for efficient O(1) visibility queries
// ============================================================================

void EntityRenderer::updateEntityGridPosition(uint16_t spawnId, float x, float y) {
    int64_t newCellKey = positionToGridKey(x, y);

    // Check if entity is already in this cell
    auto cellIt = entityGridCell_.find(spawnId);
    if (cellIt != entityGridCell_.end()) {
        if (cellIt->second == newCellKey) {
            // Already in correct cell, nothing to do
            return;
        }

        // Remove from old cell
        int64_t oldCellKey = cellIt->second;
        auto oldCellIt = spatialGrid_.find(oldCellKey);
        if (oldCellIt != spatialGrid_.end()) {
            oldCellIt->second.erase(spawnId);
            if (oldCellIt->second.empty()) {
                spatialGrid_.erase(oldCellIt);
            }
        }
    }

    // Add to new cell
    spatialGrid_[newCellKey].insert(spawnId);
    entityGridCell_[spawnId] = newCellKey;
}

void EntityRenderer::removeEntityFromGrid(uint16_t spawnId) {
    auto cellIt = entityGridCell_.find(spawnId);
    if (cellIt == entityGridCell_.end()) {
        return;
    }

    // Remove from spatial grid cell
    int64_t cellKey = cellIt->second;
    auto gridIt = spatialGrid_.find(cellKey);
    if (gridIt != spatialGrid_.end()) {
        gridIt->second.erase(spawnId);
        if (gridIt->second.empty()) {
            spatialGrid_.erase(gridIt);
        }
    }

    // Remove from cell tracking
    entityGridCell_.erase(cellIt);
}

void EntityRenderer::getEntitiesInRange(float centerX, float centerY, float range,
                                         std::vector<uint16_t>& outEntities) const {
    outEntities.clear();

    // Calculate which grid cells are within range
    // Need to check all cells that could contain entities within 'range' distance
    int32_t minCellX = static_cast<int32_t>(std::floor((centerX - range) / GRID_CELL_SIZE));
    int32_t maxCellX = static_cast<int32_t>(std::floor((centerX + range) / GRID_CELL_SIZE));
    int32_t minCellY = static_cast<int32_t>(std::floor((centerY - range) / GRID_CELL_SIZE));
    int32_t maxCellY = static_cast<int32_t>(std::floor((centerY + range) / GRID_CELL_SIZE));

    // Iterate over potentially relevant cells
    for (int32_t cellX = minCellX; cellX <= maxCellX; ++cellX) {
        for (int32_t cellY = minCellY; cellY <= maxCellY; ++cellY) {
            int64_t cellKey = (static_cast<int64_t>(cellX) << 32) | static_cast<uint32_t>(cellY);
            auto it = spatialGrid_.find(cellKey);
            if (it != spatialGrid_.end()) {
                // Add all entities in this cell - fine-grained distance check done by caller
                for (uint16_t spawnId : it->second) {
                    outEntities.push_back(spawnId);
                }
            }
        }
    }
}

// ============================================================================
// PVS-based Visibility Culling
// ============================================================================

void EntityRenderer::setBspTree(std::shared_ptr<BspTree> bspTree) {
    bspTree_ = bspTree;
    currentCameraRegionIdx_ = SIZE_MAX;
    currentCameraRegion_ = nullptr;
    LOG_DEBUG(MOD_GRAPHICS, "EntityRenderer: BSP tree set with {} regions",
              bspTree_ ? bspTree_->regions.size() : 0);
}

void EntityRenderer::invalidateOcclusionCache() {
    // Log diagnostic info about entity occlusion state
    LOG_DEBUG(MOD_GRAPHICS, "ENTITY_OCC: frustum={} occ_tested={} occ_hidden={} occ_visible={} (frame {}) entities={}",
        frustumCulledCount_, occlusionTestedCount_, occlusionHiddenCount_,
        occlusionTestedCount_ - occlusionHiddenCount_, occlusionFrameCounter_,
        entities_.size());

    // Detailed debug: test a few entities with testPointDebug when none are hidden
    if (occlusionHiddenCount_ == 0 && occlusionCuller_ && occlusionCuller_->isEnabled()) {
        int debugCount = 0;
        for (const auto& [spawnId, visual] : entities_) {
            if (visual.isPlayer || debugCount >= 5) break;
            auto result = occlusionCuller_->testPointDebug(
                visual.lastX, visual.lastY, visual.lastZ + 3.0f);
            LOG_DEBUG(MOD_GRAPHICS,
                "  ENTITY_OCC_DBG: id={} pos=({:.1f},{:.1f},{:.1f}) viewZ={:.1f} "
                "screen=({:.1f},{:.1f}) px=({},{}) entDepth={:.1f} bufDepth={:.1f} "
                "behind={} offscreen={} occluded={}",
                spawnId, visual.lastX, visual.lastY, visual.lastZ,
                result.viewZ, result.screenX, result.screenY,
                result.pixelX, result.pixelY,
                result.entityDepth, result.bufferDepth,
                result.behindCamera, result.offScreen, result.occluded);
            debugCount++;
        }
    }
    frustumCulledCount_ = 0;
    occlusionTestedCount_ = 0;
    occlusionHiddenCount_ = 0;
    occlusionFrameCounter_++;
}

void EntityRenderer::clearBspTree() {
    bspTree_ = nullptr;
    currentCameraRegionIdx_ = SIZE_MAX;
    currentCameraRegion_ = nullptr;
    LOG_DEBUG(MOD_GRAPHICS, "EntityRenderer: BSP tree cleared");
}

void EntityRenderer::registerBoat(uint16_t spawnId) {
    // Avoid duplicates (e.g., rebuild after appearance change)
    for (uint16_t id : boatSpawnIds_) {
        if (id == spawnId) return;
    }
    boatSpawnIds_.push_back(spawnId);
    LOG_INFO(MOD_GRAPHICS, "EntityRenderer: registered boat spawnId={} (total boats={})",
             spawnId, boatSpawnIds_.size());
}

void EntityRenderer::unregisterBoat(uint16_t spawnId) {
    auto it = std::find(boatSpawnIds_.begin(), boatSpawnIds_.end(), spawnId);
    if (it != boatSpawnIds_.end()) {
        boatSpawnIds_.erase(it);
        LOG_INFO(MOD_GRAPHICS, "EntityRenderer: unregistered boat spawnId={} (total boats={})",
                 spawnId, boatSpawnIds_.size());
    }
}

float EntityRenderer::findBoatDeckZ(float x, float y, float currentZ) const {
    constexpr float BEST_Z_INVALID = -999999.0f;

    // Early-out: no boats in zone (99% of zones)
    if (boatSpawnIds_.empty()) return BEST_Z_INVALID;

    float bestDeckZ = BEST_Z_INVALID;

    for (uint16_t boatId : boatSpawnIds_) {
        auto it = entities_.find(boatId);
        if (it == entities_.end() || !it->second.hasCollision) continue;
        const EntityVisual& boat = it->second;

        // PVS check: skip if boat is not in a PVS-visible region from the camera
        if (currentCameraRegion_ && boat.cachedBspRegion != SIZE_MAX
            && boat.cachedBspRegion < currentCameraRegion_->visibleRegions.size()
            && !currentCameraRegion_->visibleRegions[boat.cachedBspRegion]) {
            continue;
        }

        // Simple XY distance check against boat collision radius
        float dx = x - boat.lastX;
        float dy = y - boat.lastY;
        float distSq = dx * dx + dy * dy;
        float radiusSq = boat.collisionRadius * boat.collisionRadius;
        if (distSq > radiusSq) continue;

        // Z range check: player must be within reasonable height of the deck
        // (not far below or above the boat)
        float deckZ = boat.deckZ;
        if (currentZ < deckZ - boat.collisionHeight || currentZ > deckZ + 20.0f) continue;

        if (deckZ > bestDeckZ) {
            bestDeckZ = deckZ;
        }
    }

    return bestDeckZ;
}

} // namespace Graphics
} // namespace EQT
