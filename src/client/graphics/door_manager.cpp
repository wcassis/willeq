#include "client/graphics/door_manager.h"
#include "client/graphics/eq/s3d_loader.h"
#include "client/graphics/eq/zone_geometry.h"
#include "client/eq.h"
#include "common/logging.h"

#include <algorithm>
#include <cstring>
#include <set>

namespace EQT {
namespace Graphics {

// OpenType values for invisible doors that should not be rendered
static const std::set<uint8_t> INVISIBLE_OPENTYPES = {50, 53, 54};

DoorManager::DoorManager(irr::scene::ISceneManager* smgr, irr::video::IVideoDriver* driver)
    : smgr_(smgr)
    , driver_(driver)
{
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

irr::scene::IMesh* DoorManager::findDoorMesh(const std::string& doorName) const
{
    if (!currentZone_ || !smgr_) {
        return nullptr;
    }

    // Convert door name to uppercase for comparison
    std::string upperDoorName = doorName;
    std::transform(upperDoorName.begin(), upperDoorName.end(), upperDoorName.begin(), ::toupper);

    // First, search objectGeometries map (contains all object models from _obj.wld)
    // This is the primary lookup for doors since they're dynamically placed
    auto geomIt = currentZone_->objectGeometries.find(upperDoorName);
    if (geomIt != currentZone_->objectGeometries.end() && geomIt->second) {
        LOG_DEBUG(MOD_GRAPHICS, "Found door mesh '{}' in objectGeometries ({} verts, {} tris)",
            upperDoorName, geomIt->second->vertices.size(), geomIt->second->triangles.size());
        ZoneMeshBuilder builder(smgr_, driver_, nullptr);
        irr::scene::IMesh* mesh = nullptr;
        if (!currentZone_->objectTextures.empty() && !geomIt->second->textureNames.empty()) {
            mesh = builder.buildTexturedMesh(*geomIt->second, currentZone_->objectTextures);
        } else {
            mesh = builder.buildColoredMesh(*geomIt->second);
        }
        if (!mesh) {
            LOG_WARN(MOD_GRAPHICS, "Failed to build mesh for door '{}' (verts={}, tris={})",
                upperDoorName, geomIt->second->vertices.size(), geomIt->second->triangles.size());
        }
        return mesh;
    }

    // Try partial match in objectGeometries (for names like "DOOR_QEY01")
    for (const auto& [name, geom] : currentZone_->objectGeometries) {
        if (!geom) continue;

        // Try partial match (door name contains object name or vice versa)
        if (name.find(upperDoorName) != std::string::npos ||
            upperDoorName.find(name) != std::string::npos) {
            LOG_DEBUG(MOD_GRAPHICS, "Found door mesh via partial match: '{}' -> '{}' ({} verts)",
                upperDoorName, name, geom->vertices.size());
            ZoneMeshBuilder builder(smgr_, driver_, nullptr);
            if (!currentZone_->objectTextures.empty() && !geom->textureNames.empty()) {
                return builder.buildTexturedMesh(*geom, currentZone_->objectTextures);
            } else {
                return builder.buildColoredMesh(*geom);
            }
        }
    }

    // Fallback: Search pre-placed zone objects for matching geometry
    for (const auto& objInstance : currentZone_->objects) {
        if (!objInstance.geometry || !objInstance.placeable) {
            continue;
        }

        std::string objName = objInstance.placeable->getName();
        std::transform(objName.begin(), objName.end(), objName.begin(), ::toupper);

        // Try exact match first
        if (objName == upperDoorName) {
            ZoneMeshBuilder builder(smgr_, driver_, nullptr);
            if (!currentZone_->objectTextures.empty() && !objInstance.geometry->textureNames.empty()) {
                return builder.buildTexturedMesh(*objInstance.geometry, currentZone_->objectTextures);
            } else {
                return builder.buildColoredMesh(*objInstance.geometry);
            }
        }

        // Try partial match (door name contains object name or vice versa)
        if (objName.find(upperDoorName) != std::string::npos ||
            upperDoorName.find(objName) != std::string::npos) {
            ZoneMeshBuilder builder(smgr_, driver_, nullptr);
            if (!currentZone_->objectTextures.empty() && !objInstance.geometry->textureNames.empty()) {
                return builder.buildTexturedMesh(*objInstance.geometry, currentZone_->objectTextures);
            } else {
                return builder.buildColoredMesh(*objInstance.geometry);
            }
        }
    }

    return nullptr;
}

irr::scene::IMesh* DoorManager::createPlaceholderMesh() const
{
    if (!smgr_) {
        return nullptr;
    }

    // Create a simple box mesh as placeholder for doors without models
    irr::scene::IMesh* mesh = smgr_->getGeometryCreator()->createCubeMesh(
        irr::core::vector3df(2.0f, 6.0f, 0.5f));  // Door-shaped: 2 wide, 6 tall, 0.5 thick

    return mesh;
}

bool DoorManager::createDoor(uint8_t doorId, const std::string& name,
                              float x, float y, float z, float heading,
                              uint32_t incline, uint16_t size, uint8_t opentype,
                              bool initiallyOpen)
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
        mesh = createPlaceholderMesh();
        usePlaceholder = true;
    }

    if (!mesh) {
        LOG_WARN(MOD_GRAPHICS, "Failed to create mesh for door {}", doorId);
        return false;
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
    // Set initial door state
    visual.isOpen = initiallyOpen;
    visual.animProgress = initiallyOpen ? 1.0f : 0.0f;
    visual.isAnimating = false;
    // Spinning objects: opentype 100 = Y-spin (lamps, gems), 105 = Z-spin (blades)
    visual.isSpinning = (opentype == 100 || opentype == 105);
    visual.spinAngle = 0.0f;

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

    doors_[doorId] = visual;

    LOG_DEBUG(MOD_GRAPHICS, "Created door {} '{}' at ({:.1f}, {:.1f}, {:.1f}) heading={:.1f} opentype={}{} state={}",
              doorId, name, x, y, z, heading, opentype,
              visual.isSpinning ? " [spinning]" : "",
              initiallyOpen ? "open" : "closed");

    return true;
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

    // Only start animation if state actually changed
    if (visual.isOpen != open) {
        visual.isOpen = open;
        visual.isAnimating = true;
        // Log at debug level 2+ for all doors, or level 1+ for user-initiated
        int debugLevel = EverQuest::GetDebugLevel();
        if (debugLevel >= 2 || (userInitiated && debugLevel >= 1)) {
            LOG_DEBUG(MOD_GRAPHICS, "Door {} {} animation started", doorId, open ? "opening" : "closing");
        }
    }
}

void DoorManager::update(float deltaTime)
{
    for (auto& [id, visual] : doors_) {
        if (!visual.sceneNode) {
            continue;
        }

        // Handle spinning objects (opentype 100 = Y-spin, 105 = Z-spin)
        if (visual.isSpinning) {
            visual.spinAngle += SPIN_SPEED * deltaTime;
            if (visual.spinAngle >= 360.0f) {
                visual.spinAngle -= 360.0f;
            }

            // Spinning objects rotate the pivot node
            irr::scene::ISceneNode* rotNode = visual.pivotNode ? visual.pivotNode : visual.sceneNode;
            irr::core::vector3df rot = rotNode->getRotation();
            if (visual.opentype == 100) {
                // Y-axis spin (lamps, gems, etc.) - add spin to base heading rotation
                // Heading is in 512 format, convert to degrees, add 90 for alignment
                rot.Y = -visual.closedHeading * 360.0f / 512.0f + 90.0f + visual.spinAngle;
            } else if (visual.opentype == 105) {
                // Z-axis spin (blades, etc.) - spin around local Z
                rot.Z = visual.spinAngle;
            }
            rotNode->setRotation(rot);
            continue;  // Spinning objects don't use open/close animations
        }

        // Handle door open/close animations
        if (!visual.isAnimating) {
            continue;
        }

        // Update animation progress
        float targetProgress = visual.isOpen ? 1.0f : 0.0f;
        float direction = visual.isOpen ? 1.0f : -1.0f;

        visual.animProgress += direction * ANIM_SPEED * deltaTime;
        visual.animProgress = std::max(0.0f, std::min(1.0f, visual.animProgress));

        // Interpolate heading between closed and open (both in 512 format)
        // When closed (progress=0): openHeading (spawn + incline, appears closed)
        // When open (progress=1): closedHeading (spawn heading, appears open)
        float currentHeading = visual.openHeading +
            (visual.closedHeading - visual.openHeading) * visual.animProgress;

        // Apply rotation to pivot node (rotates door around hinge)
        // Heading is in 512 format, convert to degrees, add 90 for alignment
        float irrRotation = -currentHeading * 360.0f / 512.0f + 90.0f;
        irr::scene::ISceneNode* rotNode = visual.pivotNode ? visual.pivotNode : visual.sceneNode;
        rotNode->setRotation(irr::core::vector3df(0, irrRotation, 0));

        // Update bounding box for interaction
        visual.boundingBox = visual.sceneNode->getTransformedBoundingBox();

        // Check if animation complete
        if (visual.animProgress == targetProgress) {
            visual.isAnimating = false;
            LOG_DEBUG(MOD_GRAPHICS, "Door {} animation complete ({})",
                      id, visual.isOpen ? "open" : "closed");
        }
    }
}

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
        // Remove pivot node (which also removes child scene node)
        if (visual.pivotNode) {
            visual.pivotNode->remove();
        } else if (visual.sceneNode) {
            // Fallback for doors without pivot node
            visual.sceneNode->remove();
        }
    }
    doors_.clear();
    invisibleDoors_.clear();
    LOG_DEBUG(MOD_GRAPHICS, "Cleared all doors");
}

void DoorManager::setAllDoorsVisible(bool visible)
{
    for (auto& [id, visual] : doors_) {
        if (visual.sceneNode) {
            visual.sceneNode->setVisible(visible);
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

} // namespace Graphics
} // namespace EQT
