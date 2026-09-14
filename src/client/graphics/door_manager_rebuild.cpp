// door_manager_rebuild.cpp -- DoorManager's rebuild and bulk-visibility
// bodies: recomputeAllBspRegions, rebuildSingleDoor, rebuildPlaceholderDoors,
// setAllDoorsVisible and getDoorSceneNodes.
//
// Split out of door_manager.cpp by T453 (contiguous range 1153..1363 of the
// 1,365-line original, moved verbatim).
#include "client/graphics/door_manager.h"
#include "client/graphics/frustum_culler.h"
#include "client/graphics/constrained_texture_cache.h"
#include "client/graphics/eq/s3d_loader.h"
#include "client/graphics/eq/wld_loader.h"
#include "client/graphics/eq/zone_geometry.h"
#include "common/logging.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <set>

namespace EQT {
namespace Graphics {

void DoorManager::recomputeAllBspRegions() {
    if (!bspTree_) return;

    size_t updated = 0, nowInGraph = 0, pvsHidden = 0;

    for (auto& [id, visual] : doors_) {
        if (visual.bspRegion != SIZE_MAX) continue;  // Already has a region
        if (!visual.pivotNode) continue;

        visual.bspRegion = bspTree_->findRegionIndexForPoint(visual.x, visual.y, visual.z);
        markCollisionDirty(id);  // T435 (B425): the region is part of the collision entry
        updated++;

        bool pvsVis = isRegionPvsVisible(visual.bspRegion);
        if (pvsVis && !visual.inSceneGraph) {
            smgr_->getRootSceneNode()->addChild(visual.pivotNode);
            visual.inSceneGraph = true;
            visual.pivotNode->updateAbsolutePosition();
            if (visual.sceneNode) visual.sceneNode->updateAbsolutePosition();
            nowInGraph++;
        } else if (!pvsVis && visual.inSceneGraph) {
            visual.pivotNode->remove();
            visual.inSceneGraph = false;
            pvsHidden++;
        } else if (!pvsVis) {
            pvsHidden++;
        } else {
            nowInGraph++;
        }
    }

    if (updated > 0) {
        LOG_INFO(MOD_GRAPHICS, "recomputeAllBspRegions: {} doors updated, {} now in-graph, {} PVS-hidden",
                 updated, nowInGraph, pvsHidden);
    }
}

bool DoorManager::rebuildSingleDoor(uint8_t doorId)
{
    if (!currentZone_ || !smgr_) {
        return false;
    }

    auto it = doors_.find(doorId);
    if (it == doors_.end()) {
        return false;
    }

    DoorVisual& visual = it->second;

    // Path 1: Registered-only doors with placeholder scene nodes
    // These need full mesh building (zone data now available)
    if (!visual.meshBuilt && visual.usePlaceholder && visual.sceneNode) {
        // Remove placeholder nodes — drop our grab() reference
        if (visual.pivotNode) {
            if (!visual.inSceneGraph) {
                smgr_->getRootSceneNode()->addChild(visual.pivotNode);
            }
            visual.pivotNode->remove();
            visual.pivotNode->drop();
            visual.pivotNode = nullptr;
            visual.inSceneGraph = false;
        } else if (visual.sceneNode) {
            visual.sceneNode->remove();
        }
        visual.sceneNode = nullptr;
        visual.usePlaceholder = false;

        // Build real mesh with zone data (buildDoorMesh will grab() the new pivot)
        if (buildDoorMesh(doorId)) {
            LOG_DEBUG(MOD_GRAPHICS, "Rebuilt registered placeholder door {} '{}' with real mesh",
                      doorId, visual.modelName);
            return true;
        }
        return false;
    }

    // Path 2: Doors built via createDoor() with placeholder (mesh wasn't found at build time)
    // These just need a mesh swap
    if (!visual.usePlaceholder || !visual.sceneNode) {
        return false;
    }

    irr::scene::IMesh* mesh = findDoorMesh(visual.modelName);
    if (!mesh) {
        return false;
    }

    // Swap the mesh on the existing scene node. setMesh() grabs the new mesh
    // and drops the placeholder; T470 (B621): release findDoorMesh()'s one.
    visual.sceneNode->setMesh(mesh);
    mesh->drop();

    // Update materials for the new textured mesh
    for (irr::u32 i = 0; i < visual.sceneNode->getMaterialCount(); ++i) {
        visual.sceneNode->getMaterial(i).Lighting = false;
        visual.sceneNode->getMaterial(i).BackfaceCulling = false;
    }

    visual.usePlaceholder = false;
    markCollisionDirty(doorId);  // T435 (B425): the mesh changed under the node
    LOG_DEBUG(MOD_GRAPHICS, "Rebuilt placeholder door {} '{}' with textured mesh",
              doorId, visual.modelName);
    return true;
}

void DoorManager::rebuildPlaceholderDoors()
{
    if (!currentZone_ || !smgr_) {
        return;
    }

    int rebuilt = 0;
    int failed = 0;

    for (auto& [id, visual] : doors_) {
        // Handle registered-only doors with placeholder scene nodes
        // These need full mesh building (zone data now available)
        if (!visual.meshBuilt && visual.usePlaceholder && visual.sceneNode) {
            // Remove placeholder nodes — drop our grab() reference
            if (visual.pivotNode) {
                if (!visual.inSceneGraph) {
                    smgr_->getRootSceneNode()->addChild(visual.pivotNode);
                }
                visual.pivotNode->remove();
                visual.pivotNode->drop();
                visual.pivotNode = nullptr;
                visual.inSceneGraph = false;
            } else if (visual.sceneNode) {
                visual.sceneNode->remove();
            }
            visual.sceneNode = nullptr;
            visual.usePlaceholder = false;

            // Build real mesh with zone data (buildDoorMesh will grab() the new pivot)
            if (buildDoorMesh(id)) {
                ++rebuilt;
                LOG_DEBUG(MOD_GRAPHICS, "Rebuilt registered placeholder door {} '{}' with real mesh",
                          id, visual.modelName);
            } else {
                ++failed;
            }
            continue;
        }

        // Handle doors built via createDoor() with placeholder (mesh wasn't found at build time)
        // These just need a mesh swap
        if (!visual.usePlaceholder || !visual.sceneNode) {
            continue;
        }

        irr::scene::IMesh* mesh = findDoorMesh(visual.modelName);
        if (!mesh) {
            ++failed;
            continue;
        }

        // Swap the mesh on the existing scene node. setMesh() grabs the new
        // mesh; T470 (B621): release findDoorMesh()'s reference.
        visual.sceneNode->setMesh(mesh);
        mesh->drop();

        // Update materials for the new textured mesh
        for (irr::u32 i = 0; i < visual.sceneNode->getMaterialCount(); ++i) {
            visual.sceneNode->getMaterial(i).Lighting = false;
            visual.sceneNode->getMaterial(i).BackfaceCulling = false;
        }

        visual.usePlaceholder = false;
        markCollisionDirty(id);  // T435 (B425): the mesh changed under the node
        ++rebuilt;

        LOG_DEBUG(MOD_GRAPHICS, "Rebuilt placeholder door {} '{}' with textured mesh",
                  id, visual.modelName);
    }

    if (rebuilt > 0 || failed > 0) {
        LOG_INFO(MOD_GRAPHICS, "Door rebuild: {} replaced with textured meshes, {} still placeholder",
                 rebuilt, failed);
    }
}

void DoorManager::setAllDoorsVisible(bool visible)
{
    for (auto& [id, visual] : doors_) {
        if (!visual.pivotNode) continue;
        if (visible && !visual.inSceneGraph) {
            smgr_->getRootSceneNode()->addChild(visual.pivotNode);
            visual.inSceneGraph = true;
            visual.pivotNode->updateAbsolutePosition();
            if (visual.sceneNode) visual.sceneNode->updateAbsolutePosition();
        } else if (!visible && visual.inSceneGraph) {
            visual.pivotNode->remove();
            visual.inSceneGraph = false;
        }
    }
}

std::vector<irr::scene::IMeshSceneNode*> DoorManager::getDoorSceneNodes() const
{
    std::vector<irr::scene::IMeshSceneNode*> nodes;
    nodes.reserve(doors_.size());
    for (const auto& [id, visual] : doors_) {
        if (visual.sceneNode) {
            nodes.push_back(visual.sceneNode);
        }
    }
    return nodes;
}

// The collision writer (T435, board B425) lives in door_manager_collision.cpp:
// markCollisionDirty / markAllCollisionDirty / drainCollisionUpdates /
// computeDoorCollision. Split out rather than appended -- this TU was 1,394
// lines against a 1,500 ceiling.

} // namespace Graphics
} // namespace EQT
