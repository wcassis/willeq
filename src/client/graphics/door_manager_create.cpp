// door_manager_create.cpp -- DoorManager's door construction: createDoor,
// registerDoor (the deferred form) and buildDoorMesh.
//
// Split out of door_manager.cpp by T453 (contiguous range 471..928 of the
// 1,365-line original, moved verbatim). The INVISIBLE_OPENTYPES table moved
// with it -- both of its readers are createDoor and registerDoor, so it stays
// a file-static and gained no external linkage.
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

// OpenType values for invisible doors that should not be rendered
static const std::set<uint8_t> INVISIBLE_OPENTYPES = {50, 53, 54};

bool DoorManager::createDoor(uint8_t doorId, const std::string& name,
                              float x, float y, float z, float heading,
                              uint32_t incline, uint16_t size, uint8_t opentype,
                              bool initiallyOpen, uint32_t doorParam)
{
    if (!smgr_) {
        return false;
    }

    // Skip invisible doors (opentypes 50, 53, 54) but track them to suppress warnings
    if (INVISIBLE_OPENTYPES.count(opentype) > 0) {
        LOG_DEBUG(MOD_GRAPHICS, "Skipping invisible door {} '{}' (opentype={})",
                  doorId, name, opentype);
        invisibleDoors_.insert(doorId);  // Track so we don't warn on state updates
        return true;  // Not an error, just skip rendering
    }

    // Check if door already exists
    if (doors_.find(doorId) != doors_.end()) {
        LOG_DEBUG(MOD_GRAPHICS, "Door {} already exists, skipping", doorId);
        return true;
    }

    // Find or create mesh for this door
    irr::scene::IMesh* mesh = findDoorMesh(name);
    bool usePlaceholder = false;

    if (!mesh) {
        LOG_DEBUG(MOD_GRAPHICS, "No mesh found for door '{}', using placeholder", name);
        mesh = createPlaceholderMesh(name);
        usePlaceholder = true;
    }

    if (!mesh) {
        LOG_WARN(MOD_GRAPHICS, "Failed to create mesh for door {}", doorId);
        return false;
    }

    DoorVisual visual;
    visual.doorId = doorId;
    visual.modelName = name;
    visual.usePlaceholder = usePlaceholder;
    visual.x = x;
    visual.y = y;
    visual.z = z;
    visual.closedHeading = heading;
    visual.openHeading = calculateOpenHeading(heading, incline, opentype);
    visual.size = size;
    visual.opentype = opentype;
    // Set initial door state
    visual.isOpen = initiallyOpen;
    visual.animProgress = initiallyOpen ? 1.0f : 0.0f;
    visual.isAnimating = false;
    // Spinning objects: opentype 100 = Y-spin (lamps, gems), 105 = Z-spin (blades)
    visual.isSpinning = (opentype == 100 || opentype == 105);
    visual.spinAngle = 0.0f;
    // T438 (board B422): opentype 59 with door_param > 0 TRANSLATES.
    visual.doorParam = doorParam;
    visual.isLift = DoorTravels(opentype, doorParam);
    if (visual.isLift) {
        visual.lift.init(static_cast<float>(doorParam));
    }

    // Calculate scale and bounding box info first
    float scale = static_cast<float>(size) / 100.0f;
    irr::core::aabbox3df bbox = mesh->getBoundingBox();

    // With center baked into mesh vertices (matching eqsage), the mesh origin
    // is already at the "hinge edge" (one edge of the door). No height offset needed.
    float heightOffset = 0.0f;

    // Calculate mesh dimensions - determine which axis is the door's width
    // Door meshes are typically thin (small depth) and wide (large width)
    float extentX = (bbox.MaxEdge.X - bbox.MinEdge.X) * scale;
    float extentZ = (bbox.MaxEdge.Z - bbox.MinEdge.Z) * scale;

    LOG_DEBUG(MOD_GRAPHICS, "Door {} '{}' bbox: X=[{:.2f},{:.2f}] Y=[{:.2f},{:.2f}] Z=[{:.2f},{:.2f}] scaled extents: X={:.2f} Z={:.2f}",
              doorId, name, bbox.MinEdge.X, bbox.MaxEdge.X, bbox.MinEdge.Y, bbox.MaxEdge.Y,
              bbox.MinEdge.Z, bbox.MaxEdge.Z, extentX, extentZ);

    // The door's width is the larger of X or Z (depth is the smaller one)
    // With center baked, one edge (hinge) is already near the origin
    // We need to offset to put that edge exactly at the pivot point
    bool widthIsX = (extentX >= extentZ);
    // Use the max edge value (should be ~0 after center baking) as the offset
    // This puts the hinge edge at the pivot point
    float hingeOffset = widthIsX ? bbox.MaxEdge.X * scale : bbox.MaxEdge.Z * scale;

    // Create pivot node at door's world position (this is the hinge point)
    // EQ Z-up to Irrlicht Y-up: (x, y, z) -> (x, z, y)
    visual.pivotNode = smgr_->addEmptySceneNode();
    if (!visual.pivotNode) {
        if (usePlaceholder) {
            mesh->drop();
        }
        LOG_WARN(MOD_GRAPHICS, "Failed to create pivot node for door {}", doorId);
        return false;
    }
    visual.pivotNode->setPosition(irr::core::vector3df(x, z + heightOffset, y));

    // Create door mesh as child of pivot, offset so hinge edge is at pivot
    visual.sceneNode = smgr_->addMeshSceneNode(mesh, visual.pivotNode);
    if (!visual.sceneNode) {
        visual.pivotNode->remove();
        visual.pivotNode = nullptr;
        if (usePlaceholder) {
            mesh->drop();
        }
        LOG_WARN(MOD_GRAPHICS, "Failed to create scene node for door {}", doorId);
        return false;
    }

    // Set scale on mesh node
    visual.sceneNode->setScale(irr::core::vector3df(scale, scale, scale));

    // Offset mesh so hinge edge is at pivot origin
    // Offset along the width axis (whichever is larger between X and Z)
    // Use negative offset to put the correct edge at the hinge point
    irr::core::vector3df meshOffset;
    if (widthIsX) {
        meshOffset = irr::core::vector3df(-hingeOffset, 0, 0);
    } else {
        meshOffset = irr::core::vector3df(0, 0, -hingeOffset);
    }
    visual.sceneNode->setPosition(meshOffset);

    // Apply rotation to pivot node (this rotates the door around the hinge)
    // Door heading is in EQ 512 format (0-512), convert to degrees
    // Add 90 degrees to align door correctly in frame (doors face perpendicular to heading)
    // When closed: door is at openHeading (spawn + incline, appears closed)
    // When open: door is at closedHeading (spawn heading, appears open/rotated away)
    float currentHeading = initiallyOpen ? visual.closedHeading : visual.openHeading;
    float irrRotation = -currentHeading * 360.0f / 512.0f + 90.0f;
    visual.pivotNode->setRotation(irr::core::vector3df(0, irrRotation, 0));

    LOG_DEBUG(MOD_GRAPHICS, "Door {} rotation: heading={:.1f} (512 fmt) -> irrRotation={:.1f} deg, hingeOffset={:.2f} ({})",
              doorId, currentHeading, irrRotation, hingeOffset, widthIsX ? "X-axis" : "Z-axis");

    // Update absolute positions so bounding box calculation is correct
    visual.pivotNode->updateAbsolutePosition();
    visual.sceneNode->updateAbsolutePosition();

    // Configure materials
    for (irr::u32 i = 0; i < visual.sceneNode->getMaterialCount(); ++i) {
        visual.sceneNode->getMaterial(i).Lighting = false;
        visual.sceneNode->getMaterial(i).BackfaceCulling = false;

        // Color placeholder doors blue-ish so they stand out
        if (usePlaceholder) {
            visual.sceneNode->getMaterial(i).DiffuseColor = irr::video::SColor(255, 100, 150, 200);
            visual.sceneNode->getMaterial(i).AmbientColor = irr::video::SColor(255, 100, 150, 200);
        }
    }

    // Store bounding box for interaction (in world coordinates)
    visual.boundingBox = visual.sceneNode->getTransformedBoundingBox();

    // Keep pivot alive outside scene graph (grab/remove pattern like entities)
    visual.pivotNode->grab();
    visual.inSceneGraph = true;

    // Compute BSP region for occlusion culling
    if (bspTree_) {
        visual.bspRegion = bspTree_->findRegionIndexForPoint(x, y, z);
    }

    LOG_DEBUG(MOD_GRAPHICS, "createDoor #{}: pos=({:.1f},{:.1f},{:.1f}) bspRegion={} "
              "PVS state: bspTree_={}, currentPvsRegion_={}, regionNeighbors_={}",
              doorId, x, y, z, visual.bspRegion,
              bspTree_ != nullptr, currentPvsRegion_,
              regionNeighbors_ ? std::to_string(regionNeighbors_->size()) : "null");

    // PVS check at insertion — remove from scene graph if not visible
    bool pvsVis = isRegionPvsVisibleDebug(visual.bspRegion, doorId);
    if (!pvsVis) {
        if (visual.pivotNode) {
            visual.pivotNode->remove();
            visual.inSceneGraph = false;
        }
        LOG_DEBUG(MOD_GRAPHICS, "createDoor #{}: PVS-HIDDEN at insertion (region={})", doorId, visual.bspRegion);
    }

    visual.meshBuilt = true;
    doors_[doorId] = visual;
    markCollisionDirty(doorId);  // T435 (B425): the door now has geometry

    LOG_DEBUG(MOD_GRAPHICS, "Created door {} '{}' at ({:.1f}, {:.1f}, {:.1f}) heading={:.1f} opentype={}{} state={}",
              doorId, name, x, y, z, heading, opentype,
              visual.isSpinning ? " [spinning]" : "",
              initiallyOpen ? "open" : "closed");

    return true;
}

bool DoorManager::registerDoor(uint8_t doorId, const std::string& name,
                                float x, float y, float z, float heading,
                                uint32_t incline, uint16_t size, uint8_t opentype,
                                bool initiallyOpen, uint32_t doorParam)
{
    if (!smgr_) {
        return false;
    }

    // Skip invisible doors
    if (INVISIBLE_OPENTYPES.count(opentype) > 0) {
        invisibleDoors_.insert(doorId);
        return true;
    }

    if (doors_.find(doorId) != doors_.end()) {
        return true;
    }

    DoorVisual visual;
    visual.doorId = doorId;
    visual.modelName = name;
    visual.x = x;
    visual.y = y;
    visual.z = z;
    visual.closedHeading = heading;
    visual.openHeading = calculateOpenHeading(heading, incline, opentype);
    visual.size = size;
    visual.opentype = opentype;
    visual.isOpen = initiallyOpen;
    visual.animProgress = initiallyOpen ? 1.0f : 0.0f;
    visual.isAnimating = false;
    visual.isSpinning = (opentype == 100 || opentype == 105);
    visual.spinAngle = 0.0f;
    // T438 (board B422): opentype 59 with door_param > 0 TRANSLATES.
    visual.doorParam = doorParam;
    visual.isLift = DoorTravels(opentype, doorParam);
    if (visual.isLift) {
        visual.lift.init(static_cast<float>(doorParam));
    }
    visual.meshBuilt = false;

    // Store raw parameters for deferred building
    visual.name_raw = name;
    visual.heading_raw = heading;
    visual.incline_raw = incline;
    visual.initiallyOpen_raw = initiallyOpen;

    // Create placeholder scene node for immediate visibility
    irr::scene::IMesh* mesh = createPlaceholderMesh(name);
    if (mesh) {
        float scale = static_cast<float>(size) / 100.0f;
        irr::core::aabbox3df bbox = mesh->getBoundingBox();

        // Hinge at max X edge (works for all placeholder shapes)
        float hingeOffset = bbox.MaxEdge.X * scale;

        // Create pivot node at door's world position (EQ Z-up -> Irrlicht Y-up)
        visual.pivotNode = smgr_->addEmptySceneNode();
        if (visual.pivotNode) {
            visual.pivotNode->setPosition(irr::core::vector3df(x, z, y));

            // Create mesh scene node as child of pivot
            visual.sceneNode = smgr_->addMeshSceneNode(mesh, visual.pivotNode);
            if (visual.sceneNode) {
                visual.sceneNode->setScale(irr::core::vector3df(scale, scale, scale));

                // Offset mesh so hinge edge is at pivot origin (X axis for placeholder)
                visual.sceneNode->setPosition(irr::core::vector3df(-hingeOffset, 0, 0));

                // Apply rotation
                float currentHeading = initiallyOpen ? visual.closedHeading : visual.openHeading;
                float irrRotation = -currentHeading * 360.0f / 512.0f + 90.0f;
                visual.pivotNode->setRotation(irr::core::vector3df(0, irrRotation, 0));

                visual.pivotNode->updateAbsolutePosition();
                visual.sceneNode->updateAbsolutePosition();

                // Configure materials: unlit, no backface cull
                // (vertex colors already baked by createPlaceholderMesh)
                for (irr::u32 i = 0; i < visual.sceneNode->getMaterialCount(); ++i) {
                    visual.sceneNode->getMaterial(i).Lighting = false;
                    visual.sceneNode->getMaterial(i).BackfaceCulling = false;
                }

                visual.usePlaceholder = true;
                visual.boundingBox = visual.sceneNode->getTransformedBoundingBox();

                // Keep pivot alive outside scene graph (grab/remove pattern like entities)
                visual.pivotNode->grab();
                visual.inSceneGraph = true;

                LOG_DEBUG(MOD_GRAPHICS, "Created placeholder door {} '{}' at ({:.1f}, {:.1f}, {:.1f})",
                          doorId, name, x, y, z);
            } else {
                visual.pivotNode->remove();
                visual.pivotNode = nullptr;
                mesh->drop();
            }
        } else {
            mesh->drop();
        }
    }

    // Compute BSP region
    if (bspTree_) {
        visual.bspRegion = bspTree_->findRegionIndexForPoint(x, y, z);
    }

    LOG_DEBUG(MOD_GRAPHICS, "registerDoor #{}: pos=({:.1f},{:.1f},{:.1f}) bspRegion={} "
              "PVS state: bspTree_={}, currentPvsRegion_={}, regionNeighbors_={}",
              doorId, x, y, z, visual.bspRegion,
              bspTree_ != nullptr, currentPvsRegion_,
              regionNeighbors_ ? std::to_string(regionNeighbors_->size()) : "null");

    // PVS check at insertion — remove from scene graph if not visible.
    // When no BSP tree, bspRegion stays SIZE_MAX and isRegionPvsVisible()
    // falls through to "assume visible" — so ALL doors end up in the graph.
    // Instead, start doors OUT of graph when no BSP; recomputeAllBspRegions()
    // will retroactively add visible ones once BSP arrives.
    if (!bspTree_) {
        if (visual.pivotNode) {
            visual.pivotNode->remove();
            visual.inSceneGraph = false;
        }
        LOG_DEBUG(MOD_GRAPHICS, "registerDoor #{}: OUT-OF-GRAPH (no BSP yet)", doorId);
    } else {
        bool pvsVis = isRegionPvsVisibleDebug(visual.bspRegion, doorId);
        if (!pvsVis) {
            if (visual.pivotNode) {
                visual.pivotNode->remove();
                visual.inSceneGraph = false;
            }
            LOG_DEBUG(MOD_GRAPHICS, "registerDoor #{}: PVS-HIDDEN at insertion (region={})", doorId, visual.bspRegion);
        }
    }

    doors_[doorId] = visual;
    // T435 (B425): a registered door already carries a placeholder mesh, and a
    // placeholder is what the player actually walks into until the real mesh
    // arrives -- so it collides from here, not from buildDoorMesh().
    markCollisionDirty(doorId);
    return true;
}

bool DoorManager::buildDoorMesh(uint8_t doorId)
{
    auto it = doors_.find(doorId);
    if (it == doors_.end() || !smgr_) {
        return false;
    }

    DoorVisual& visual = it->second;
    if (visual.meshBuilt) {
        return true;
    }

    // Find or create mesh
    irr::scene::IMesh* mesh = findDoorMesh(visual.name_raw);
    bool usePlaceholder = false;

    if (!mesh) {
        mesh = createPlaceholderMesh(visual.name_raw);
        usePlaceholder = true;
    }

    if (!mesh) {
        return false;
    }

    visual.usePlaceholder = usePlaceholder;

    float x = visual.x, y = visual.y, z = visual.z;

    // Recompute BSP region if it was unknown at registration time
    // (doors registered during instant scene don't have BSP data yet)
    if (visual.bspRegion == SIZE_MAX && bspTree_) {
        visual.bspRegion = bspTree_->findRegionIndexForPoint(x, y, z);
    }

    float scale = static_cast<float>(visual.size) / 100.0f;
    irr::core::aabbox3df bbox = mesh->getBoundingBox();
    float heightOffset = 0.0f;

    float extentX = (bbox.MaxEdge.X - bbox.MinEdge.X) * scale;
    float extentZ = (bbox.MaxEdge.Z - bbox.MinEdge.Z) * scale;
    bool widthIsX = (extentX >= extentZ);
    float hingeOffset = widthIsX ? bbox.MaxEdge.X * scale : bbox.MaxEdge.Z * scale;

    // Create pivot node
    visual.pivotNode = smgr_->addEmptySceneNode();
    if (!visual.pivotNode) {
        if (usePlaceholder) mesh->drop();
        return false;
    }
    visual.pivotNode->setPosition(irr::core::vector3df(x, z + heightOffset, y));

    // Create door mesh as child of pivot
    visual.sceneNode = smgr_->addMeshSceneNode(mesh, visual.pivotNode);
    if (!visual.sceneNode) {
        visual.pivotNode->remove();
        visual.pivotNode = nullptr;
        if (usePlaceholder) mesh->drop();
        return false;
    }

    visual.sceneNode->setScale(irr::core::vector3df(scale, scale, scale));

    // Offset mesh so hinge edge is at pivot
    irr::core::vector3df meshOffset;
    if (widthIsX) {
        meshOffset = irr::core::vector3df(-hingeOffset, 0, 0);
    } else {
        meshOffset = irr::core::vector3df(0, 0, -hingeOffset);
    }
    visual.sceneNode->setPosition(meshOffset);

    // Apply rotation
    float currentHeading = visual.isOpen ? visual.closedHeading : visual.openHeading;
    float irrRotation = -currentHeading * 360.0f / 512.0f + 90.0f;
    visual.pivotNode->setRotation(irr::core::vector3df(0, irrRotation, 0));

    visual.pivotNode->updateAbsolutePosition();
    visual.sceneNode->updateAbsolutePosition();

    // Configure materials
    for (irr::u32 i = 0; i < visual.sceneNode->getMaterialCount(); ++i) {
        visual.sceneNode->getMaterial(i).Lighting = false;
        visual.sceneNode->getMaterial(i).BackfaceCulling = false;
        if (usePlaceholder) {
            visual.sceneNode->getMaterial(i).DiffuseColor = irr::video::SColor(255, 100, 150, 200);
            visual.sceneNode->getMaterial(i).AmbientColor = irr::video::SColor(255, 100, 150, 200);
        }
    }

    visual.boundingBox = visual.sceneNode->getTransformedBoundingBox();
    visual.meshBuilt = true;

    // Keep pivot alive outside scene graph (grab/remove pattern like entities)
    visual.pivotNode->grab();
    visual.inSceneGraph = true;

    // PVS check at insertion — remove from scene graph if not visible
    LOG_DEBUG(MOD_GRAPHICS, "buildDoorMesh #{}: bspRegion={} PVS state: bspTree_={}, currentPvsRegion_={}, regionNeighbors_={}",
              doorId, visual.bspRegion, bspTree_ != nullptr, currentPvsRegion_,
              regionNeighbors_ ? std::to_string(regionNeighbors_->size()) : "null");
    bool pvsVis = isRegionPvsVisibleDebug(visual.bspRegion, doorId);
    if (!pvsVis) {
        if (visual.pivotNode) {
            visual.pivotNode->remove();
            visual.inSceneGraph = false;
        }
        LOG_DEBUG(MOD_GRAPHICS, "buildDoorMesh #{}: PVS-HIDDEN at insertion (region={})", doorId, visual.bspRegion);
    } else {
        LOG_DEBUG(MOD_GRAPHICS, "buildDoorMesh #{}: PVS-VISIBLE at insertion (region={})", doorId, visual.bspRegion);
    }

    LOG_DEBUG(MOD_GRAPHICS, "Built door mesh {} '{}' at ({:.1f}, {:.1f}, {:.1f})",
              doorId, visual.name_raw, x, y, z);

    markCollisionDirty(doorId);  // T435 (B425): real mesh replaces the placeholder
    return true;
}

} // namespace Graphics
} // namespace EQT
