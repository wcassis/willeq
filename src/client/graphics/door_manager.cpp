#include "client/graphics/door_manager.h"
#include "client/graphics/frustum_culler.h"
#include "client/graphics/constrained_texture_cache.h"
#include "client/graphics/eq/s3d_loader.h"
#include "client/graphics/eq/wld_loader.h"
#include "client/graphics/eq/zone_geometry.h"
// K15 (board B006): client/eq.h is GONE from this renderer TU. Its only
// reason to be here was the static EverQuest::GetDebugLevel(); the logging
// module's own level query (ShouldLog, behind LOG_TRACE/LOG_DEBUG) does the
// same job without a game-class dependency.
#include "common/logging.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <set>

namespace EQT {
namespace Graphics {


DoorManager::DoorManager(irr::scene::ISceneManager* smgr, irr::video::IVideoDriver* driver)
    : smgr_(smgr)
    , driver_(driver)
{
}

bool DoorManager::isRegionPvsVisible(size_t regionIdx) const {
    if (!bspTree_ || currentPvsRegion_ == SIZE_MAX || regionIdx == SIZE_MAX
        || currentPvsRegion_ >= bspTree_->regions.size())
        return true;  // No PVS data — assume visible
    auto& camRegion = bspTree_->regions[currentPvsRegion_];
    if (!camRegion || camRegion->visibleRegions.empty()
        || regionIdx >= camRegion->visibleRegions.size())
        return true;
    if (camRegion->visibleRegions[regionIdx])
        return true;  // Directly PVS-visible
    // Check 1-depth portal neighbors
    if (regionNeighbors_) {
        auto it = regionNeighbors_->find(regionIdx);
        if (it != regionNeighbors_->end()) {
            for (size_t neighbor : it->second) {
                if (neighbor < camRegion->visibleRegions.size()
                    && camRegion->visibleRegions[neighbor])
                    return true;
            }
        }
    }
    return false;
}

// Debug version that logs the decision
bool DoorManager::isRegionPvsVisibleDebug(size_t regionIdx, uint8_t doorId) const {
    if (!bspTree_) {
        LOG_DEBUG(MOD_GRAPHICS, "Door-PVS-DBG [door#{}] region={}: VISIBLE (no bspTree_)", doorId, regionIdx);
        return true;
    }
    if (currentPvsRegion_ == SIZE_MAX) {
        LOG_DEBUG(MOD_GRAPHICS, "Door-PVS-DBG [door#{}] region={}: VISIBLE (currentPvsRegion_=SIZE_MAX)", doorId, regionIdx);
        return true;
    }
    if (regionIdx == SIZE_MAX) {
        LOG_DEBUG(MOD_GRAPHICS, "Door-PVS-DBG [door#{}] region=SIZE_MAX: VISIBLE (no region)", doorId);
        return true;
    }
    if (currentPvsRegion_ >= bspTree_->regions.size()) {
        LOG_DEBUG(MOD_GRAPHICS, "Door-PVS-DBG [door#{}] region={}: VISIBLE (camRegion {} >= tree size {})",
                  doorId, regionIdx, currentPvsRegion_, bspTree_->regions.size());
        return true;
    }
    auto& camRegion = bspTree_->regions[currentPvsRegion_];
    if (!camRegion) {
        LOG_DEBUG(MOD_GRAPHICS, "Door-PVS-DBG [door#{}] region={}: VISIBLE (camRegion {} null)",
                  doorId, regionIdx, currentPvsRegion_);
        return true;
    }
    if (camRegion->visibleRegions.empty()) {
        LOG_DEBUG(MOD_GRAPHICS, "Door-PVS-DBG [door#{}] region={}: VISIBLE (camRegion {} bitvec empty)",
                  doorId, regionIdx, currentPvsRegion_);
        return true;
    }
    if (regionIdx >= camRegion->visibleRegions.size()) {
        LOG_DEBUG(MOD_GRAPHICS, "Door-PVS-DBG [door#{}] region={}: VISIBLE (region >= bitvec size {})",
                  doorId, regionIdx, camRegion->visibleRegions.size());
        return true;
    }
    bool directlyVisible = camRegion->visibleRegions[regionIdx];
    if (directlyVisible) {
        LOG_DEBUG(MOD_GRAPHICS, "Door-PVS-DBG [door#{}] region={}: VISIBLE (bitvector[{}]=1, camRegion={})",
                  doorId, regionIdx, regionIdx, currentPvsRegion_);
        return true;
    }
    // Check 1-depth portal neighbors
    if (regionNeighbors_) {
        auto it = regionNeighbors_->find(regionIdx);
        if (it != regionNeighbors_->end()) {
            for (size_t neighbor : it->second) {
                if (neighbor < camRegion->visibleRegions.size()
                    && camRegion->visibleRegions[neighbor]) {
                    LOG_DEBUG(MOD_GRAPHICS, "Door-PVS-DBG [door#{}] region={}: VISIBLE (neighbor {} is PVS-visible)",
                              doorId, regionIdx, neighbor);
                    return true;
                }
            }
        }
    }
    LOG_DEBUG(MOD_GRAPHICS, "Door-PVS-DBG [door#{}] region={}: HIDDEN (bitvec=0, no visible neighbors, camRegion={})",
              doorId, regionIdx, currentPvsRegion_);
    return false;
}

DoorManager::~DoorManager()
{
    clearDoors();
}

void DoorManager::setZone(const std::shared_ptr<S3DZone>& zone)
{
    currentZone_ = zone;
}

float DoorManager::calculateOpenHeading(float closedHeading, uint32_t incline, uint8_t opentype) const
{
    // Both closedHeading and incline are in EQ 512 format
    // Keep everything in 512 format for consistent animation interpolation
    float incline512 = static_cast<float>(incline);

    // If incline is 0, use a default rotation for standard door types
    // Standard doors (opentype 0, 5) typically rotate 90 degrees when opened
    // 90 degrees = 128 in 512 format
    if (incline == 0) {
        // Standard door types that should rotate when opened
        if (opentype == 0 || opentype == 5 || opentype == 56) {
            // Default 90 degree (128 in 512 format) rotation for doors with no explicit incline
            incline512 = 128.0f;
            LOG_DEBUG(MOD_GRAPHICS, "Using default 90-degree rotation for door (opentype={}, incline=0)", opentype);
        }
    }

    return closedHeading + incline512;
}

bool DoorManager::isDoorMeshBuilt(uint8_t doorId) const {
    auto it = doors_.find(doorId);
    return it != doors_.end() && it->second.meshBuilt;
}

void DoorManager::getUnbuiltDoors(std::vector<uint8_t>& out) const {
    out.clear();
    for (const auto& [id, visual] : doors_) {
        if (!visual.meshBuilt) {
            out.push_back(id);
        }
    }
}

void DoorManager::setDoorState(uint8_t doorId, bool open, bool userInitiated)
{
    auto it = doors_.find(doorId);
    if (it == doors_.end()) {
        // Silently ignore invisible doors
        if (invisibleDoors_.count(doorId) > 0) {
            return;
        }
        LOG_DEBUG(MOD_GRAPHICS, "setDoorState: unknown door {}", doorId);
        return;
    }

    DoorVisual& visual = it->second;

    // Spinning objects don't have open/close state
    if (visual.isSpinning) {
        return;
    }

    // T438 (board B422): a lift runs a whole CYCLE per OP_MoveDoor -- it
    // travels door_param units, dwells and comes back -- so the packet is a
    // trigger, not a commanded end state. Measured: eqemu's own close arrives
    // 5 s into lift 69's ride and the official client does not react to it, so
    // trigger() ignores a MoveDoor that lands mid-cycle.
    if (visual.isLift) {
        visual.isOpen = open;
        visual.lift.trigger();
        if (userInitiated) {
            LOG_DEBUG(MOD_GRAPHICS, "Lift {} cycle triggered (travel {} units)", doorId, visual.doorParam);
        } else {
            LOG_TRACE(MOD_GRAPHICS, "Lift {} cycle triggered (travel {} units)", doorId, visual.doorParam);
        }
        return;
    }

    // Only start animation if state actually changed
    if (visual.isOpen != open) {
        visual.isOpen = open;
        visual.isAnimating = true;
        // T438: the official client closes the door BY ITSELF 11.04 s after it
        // opens. eqemu transmits no close at all for an ordinary door
        // (/mnt/projects/akk-stack/code/zone/doors.cpp:138 only sends one for
        // open_type 40 or trigger_type 1), so before this every door a player
        // or an NPC opened stayed open for the rest of the session.
        visual.autoCloseTimer = open ? DoorMotion::kHingedAutoCloseSeconds : -1.0f;
        // K15 (board B006): the old gate read EverQuest::GetDebugLevel() and
        // printed at "level >= 2 for every door, level >= 1 when the player
        // initiated it". The logging module already has that query
        // (ShouldLog, inside the macros): a user-initiated door logs at
        // DEBUG, every other door at the one-step-noisier TRACE.
        if (userInitiated) {
            LOG_DEBUG(MOD_GRAPHICS, "Door {} {} animation started", doorId, open ? "opening" : "closing");
        } else {
            LOG_TRACE(MOD_GRAPHICS, "Door {} {} animation started", doorId, open ? "opening" : "closing");
        }
    }
}

// T438: DoorManager::update() -- the animation -- moved to
// door_manager_animation.cpp when this TU reached the 1,500-line ceiling.
// See docs/module_map.md.


uint8_t DoorManager::getDoorAtScreenPos(int screenX, int screenY,
                                         irr::scene::ICameraSceneNode* camera,
                                         irr::scene::ISceneCollisionManager* collisionMgr) const
{
    if (!camera || !collisionMgr) {
        return 0;
    }

    // Get ray from camera through screen position
    irr::core::line3df ray = collisionMgr->getRayFromScreenCoordinates(
        irr::core::position2di(screenX, screenY), camera);

    uint8_t closestDoorId = 0;
    float closestDist = std::numeric_limits<float>::max();

    for (const auto& [id, visual] : doors_) {
        if (!visual.meshBuilt) continue;  // Skip unbuilt doors
        // Check ray intersection with door's bounding box
        irr::core::aabbox3df expandedBox = visual.boundingBox;
        // Expand box slightly for easier clicking
        expandedBox.MinEdge -= irr::core::vector3df(1.0f, 1.0f, 1.0f);
        expandedBox.MaxEdge += irr::core::vector3df(1.0f, 1.0f, 1.0f);

        if (expandedBox.intersectsWithLine(ray)) {
            // Calculate distance to door center
            irr::core::vector3df doorCenter = expandedBox.getCenter();
            float dist = ray.start.getDistanceFrom(doorCenter);

            if (dist < closestDist) {
                closestDist = dist;
                closestDoorId = id;
            }
        }
    }

    return closestDoorId;
}

uint8_t DoorManager::getNearestDoor(float playerX, float playerY, float playerZ,
                                     float playerHeading, float maxDistance) const
{
    uint8_t nearestId = 0;
    float nearestDistSq = maxDistance * maxDistance;

    LOG_DEBUG(MOD_GRAPHICS, "getNearestDoor: player at ({:.1f}, {:.1f}, {:.1f}) heading={:.1f} maxDist={:.1f}",
        playerX, playerY, playerZ, playerHeading, maxDistance);

    for (const auto& [id, visual] : doors_) {
        // Calculate 2D distance (ignore Z for horizontal proximity)
        float dx = visual.x - playerX;
        float dy = visual.y - playerY;
        float distSq = dx * dx + dy * dy;
        float dist = std::sqrt(distSq);

        if (dist < 30.0f) {  // Log nearby doors for debugging
            LOG_DEBUG(MOD_GRAPHICS, "  Door {} '{}' at ({:.1f}, {:.1f}, {:.1f}) dist={:.1f}",
                id, visual.modelName, visual.x, visual.y, visual.z, dist);
        }

        if (distSq > nearestDistSq) {
            continue;
        }

        // Check if player is facing the door (within 45-degree cone)
        // Convert player heading from EQ format (0-512) to degrees
        float playerAngleDeg = playerHeading * 360.0f / 512.0f;

        // Calculate angle to door
        float angleToDoor = std::atan2(dy, dx) * 180.0f / M_PI;
        // Convert from atan2 convention to EQ heading convention
        angleToDoor = 90.0f - angleToDoor;
        if (angleToDoor < 0) angleToDoor += 360.0f;

        float angleDiff = std::abs(angleToDoor - playerAngleDeg);
        if (angleDiff > 180.0f) {
            angleDiff = 360.0f - angleDiff;
        }

        // Must be facing within 45 degrees of the door
        if (angleDiff > 45.0f) {
            continue;
        }

        nearestDistSq = distSq;
        nearestId = id;
    }

    // If nearest door is unbuilt, demand-load it now (player is interacting)
    if (nearestId != 0) {
        auto it = doors_.find(nearestId);
        if (it != doors_.end() && !it->second.meshBuilt) {
            const_cast<DoorManager*>(this)->buildDoorMesh(nearestId);
        }
    }

    return nearestId;
}

bool DoorManager::hasDoor(uint8_t doorId) const
{
    return doors_.find(doorId) != doors_.end();
}

const DoorVisual* DoorManager::getDoor(uint8_t doorId) const
{
    auto it = doors_.find(doorId);
    if (it != doors_.end()) {
        return &it->second;
    }
    return nullptr;
}

void DoorManager::clearDoors()
{
    for (auto& [id, visual] : doors_) {
        if (visual.pivotNode) {
            // Re-add to scene graph if removed, so remove() detaches from parent
            if (!visual.inSceneGraph) {
                smgr_->getRootSceneNode()->addChild(visual.pivotNode);
            }
            visual.pivotNode->remove();  // Detach from scene graph
            visual.pivotNode->drop();    // Release our grab() reference
            visual.inSceneGraph = false;
        } else if (visual.sceneNode) {
            visual.sceneNode->remove();
        }
    }
    // T435 (B425): tell the renderer which ids to drop from its collision map
    // BEFORE the door records go away.
    collisionDirty_.clear();
    for (const auto& [id, visual] : doors_) {
        collisionRemoved_.insert(id);
    }
    doors_.clear();
    invisibleDoors_.clear();

    // Drop cached meshes — scene nodes hold their own grab() references
    for (auto& [name, mesh] : doorMeshCache_) {
        if (mesh) {
            mesh->drop();
        }
    }
    doorMeshCache_.clear();

    LOG_DEBUG(MOD_GRAPHICS, "Cleared all doors and mesh cache");
}

} // namespace Graphics
} // namespace EQT
