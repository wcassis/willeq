#include "client/graphics/door_manager.h"
#include "client/graphics/frustum_culler.h"
#include "client/graphics/constrained_texture_cache.h"
#include "client/graphics/eq/s3d_loader.h"
#include "client/graphics/eq/wld_loader.h"
#include "client/graphics/eq/zone_geometry.h"
#include "client/eq.h"
#include "common/logging.h"

#include <algorithm>
#include <cctype>
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

irr::scene::IMesh* DoorManager::findDoorMesh(const std::string& doorName) const
{
    if (!currentZone_ || !smgr_) {
        return nullptr;
    }

    // Convert door name to uppercase for comparison and cache key
    std::string upperDoorName = doorName;
    std::transform(upperDoorName.begin(), upperDoorName.end(), upperDoorName.begin(), ::toupper);

    // Check mesh cache first — avoids rebuilding identical meshes (e.g. 25 DOOR1 instances)
    auto cacheIt = doorMeshCache_.find(upperDoorName);
    if (cacheIt != doorMeshCache_.end()) {
        LOG_DEBUG(MOD_GRAPHICS, "Door mesh cache hit for '{}'", upperDoorName);
        return cacheIt->second;  // Cache holds the reference, scene node will grab() on use
    }

    irr::scene::IMesh* mesh = nullptr;

    // First, search objectGeometries map (contains all object models from _obj.wld)
    // This is the primary lookup for doors since they're dynamically placed
    auto geomIt = currentZone_->objectGeometries.find(upperDoorName);
    if (geomIt != currentZone_->objectGeometries.end() && geomIt->second) {
        LOG_DEBUG(MOD_GRAPHICS, "Found door mesh '{}' in objectGeometries ({} verts, {} tris)",
            upperDoorName, geomIt->second->vertices.size(), geomIt->second->triangles.size());
        ZoneMeshBuilder builder(smgr_, driver_, nullptr);
        if (constrainedCache_) builder.setConstrainedTextureCache(constrainedCache_);
        // No custom shader — doors use built-in EMT_SOLID/EMT_TRANSPARENT_ALPHA_CHANNEL_REF
        // which route through the cheap Solid3D/AlphaTest3D programs (no per-node callback)
        if (!currentZone_->objectTextures.empty() && !geomIt->second->textureNames().empty()) {
            mesh = builder.buildTexturedMesh(*geomIt->second, currentZone_->objectTextures);
        } else {
            mesh = builder.buildColoredMesh(*geomIt->second);
        }
        if (!mesh) {
            LOG_WARN(MOD_GRAPHICS, "Failed to build mesh for door '{}' (verts={}, tris={})",
                upperDoorName, geomIt->second->vertices.size(), geomIt->second->triangles.size());
        }
        if (mesh) {
            const_cast<DoorManager*>(this)->doorMeshCache_[upperDoorName] = mesh;
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
            if (constrainedCache_) builder.setConstrainedTextureCache(constrainedCache_);
            if (!currentZone_->objectTextures.empty() && !geom->textureNames().empty()) {
                mesh = builder.buildTexturedMesh(*geom, currentZone_->objectTextures);
            } else {
                mesh = builder.buildColoredMesh(*geom);
            }
            if (mesh) {
                const_cast<DoorManager*>(this)->doorMeshCache_[upperDoorName] = mesh;
            }
            return mesh;
        }
    }

    // Fallback: Search pre-placed zone objects for matching geometry
    for (const auto& objInstance : currentZone_->objects) {
        if (!objInstance.geometry || !objInstance.placeable) {
            continue;
        }

        std::string objName = objInstance.placeable->getName();
        std::transform(objName.begin(), objName.end(), objName.begin(), ::toupper);

        // Try exact match first, then partial match
        bool matched = (objName == upperDoorName) ||
                       (objName.find(upperDoorName) != std::string::npos) ||
                       (upperDoorName.find(objName) != std::string::npos);

        if (matched) {
            ZoneMeshBuilder builder(smgr_, driver_, nullptr);
            if (constrainedCache_) builder.setConstrainedTextureCache(constrainedCache_);
            if (!currentZone_->objectTextures.empty() && !objInstance.geometry->textureNames().empty()) {
                mesh = builder.buildTexturedMesh(*objInstance.geometry, currentZone_->objectTextures);
            } else {
                mesh = builder.buildColoredMesh(*objInstance.geometry);
            }
            if (mesh) {
                const_cast<DoorManager*>(this)->doorMeshCache_[upperDoorName] = mesh;
            }
            return mesh;
        }
    }

    return nullptr;
}

// Generate a unique color from a door name using a simple hash
static irr::video::SColor colorFromName(const std::string& name)
{
    // FNV-1a hash of the full name
    uint32_t hash = 2166136261u;
    for (char c : name) {
        hash ^= static_cast<uint32_t>(c);
        hash *= 16777619u;
    }

    // Convert hash to HSV with high saturation and medium-high value for visibility
    float hue = (hash % 360) / 360.0f;
    float sat = 0.6f + (((hash >> 12) % 30) / 100.0f);  // 0.6-0.9
    float val = 0.65f + (((hash >> 20) % 25) / 100.0f);  // 0.65-0.9

    // HSV to RGB
    int hi = static_cast<int>(hue * 6.0f) % 6;
    float f = hue * 6.0f - hi;
    float p = val * (1.0f - sat);
    float q = val * (1.0f - f * sat);
    float t = val * (1.0f - (1.0f - f) * sat);

    float r, g, b;
    switch (hi) {
        case 0: r = val; g = t;   b = p;   break;
        case 1: r = q;   g = val; b = p;   break;
        case 2: r = p;   g = val; b = t;   break;
        case 3: r = p;   g = q;   b = val; break;
        case 4: r = t;   g = p;   b = val; break;
        default:r = val; g = p;   b = q;   break;
    }

    return irr::video::SColor(255,
        static_cast<uint8_t>(r * 255),
        static_cast<uint8_t>(g * 255),
        static_cast<uint8_t>(b * 255));
}

// Build an octagonal prism mesh (barrel shape) manually
static irr::scene::IMesh* createOctagonalPrism(irr::scene::ISceneManager* smgr,
                                                 float radius, float height,
                                                 const irr::video::SColor& color)
{
    const int SIDES = 8;
    // Vertices: 2 center + 2*SIDES rim = 18 verts
    // Triangles: 2*SIDES (caps) + 2*SIDES (sides) = 32 tris
    irr::scene::SMeshBuffer* buf = new irr::scene::SMeshBuffer();
    buf->Vertices.set_used(2 + 2 * SIDES + 2 * SIDES);  // centers + rim tops + rim bottoms
    buf->Indices.set_used(SIDES * 3 * 2 + SIDES * 6);   // cap tris + side quads

    float halfH = height * 0.5f;

    // Center vertices for top and bottom caps
    auto& topCenter = buf->Vertices[0];
    topCenter.Pos = irr::core::vector3df(0, halfH, 0);
    topCenter.Normal = irr::core::vector3df(0, 1, 0);
    topCenter.Color = color;
    topCenter.TCoords = irr::core::vector2df(0.5f, 0.5f);

    auto& botCenter = buf->Vertices[1];
    botCenter.Pos = irr::core::vector3df(0, -halfH, 0);
    botCenter.Normal = irr::core::vector3df(0, -1, 0);
    botCenter.Color = color;
    botCenter.TCoords = irr::core::vector2df(0.5f, 0.5f);

    // Rim vertices
    for (int i = 0; i < SIDES; ++i) {
        float angle = static_cast<float>(i) * 2.0f * static_cast<float>(M_PI) / SIDES;
        float cx = radius * std::cos(angle);
        float cz = radius * std::sin(angle);
        irr::core::vector3df outNormal(std::cos(angle), 0, std::sin(angle));

        // Top rim
        auto& vt = buf->Vertices[2 + i];
        vt.Pos = irr::core::vector3df(cx, halfH, cz);
        vt.Normal = irr::core::vector3df(0, 1, 0);
        vt.Color = color;
        vt.TCoords = irr::core::vector2df(0, 0);

        // Bottom rim
        auto& vb = buf->Vertices[2 + SIDES + i];
        vb.Pos = irr::core::vector3df(cx, -halfH, cz);
        vb.Normal = irr::core::vector3df(0, -1, 0);
        // Darken bottom vertices slightly for depth
        vb.Color = irr::video::SColor(255,
            static_cast<uint8_t>(color.getRed() * 0.7f),
            static_cast<uint8_t>(color.getGreen() * 0.7f),
            static_cast<uint8_t>(color.getBlue() * 0.7f));
        vb.TCoords = irr::core::vector2df(0, 0);
    }

    irr::u32 idx = 0;
    // Top cap triangles (fan from center)
    for (int i = 0; i < SIDES; ++i) {
        buf->Indices[idx++] = 0;  // top center
        buf->Indices[idx++] = static_cast<irr::u16>(2 + i);
        buf->Indices[idx++] = static_cast<irr::u16>(2 + (i + 1) % SIDES);
    }
    // Bottom cap triangles (reverse winding)
    for (int i = 0; i < SIDES; ++i) {
        buf->Indices[idx++] = 1;  // bottom center
        buf->Indices[idx++] = static_cast<irr::u16>(2 + SIDES + (i + 1) % SIDES);
        buf->Indices[idx++] = static_cast<irr::u16>(2 + SIDES + i);
    }
    // Side quads (two triangles each)
    for (int i = 0; i < SIDES; ++i) {
        int next = (i + 1) % SIDES;
        irr::u16 tl = static_cast<irr::u16>(2 + i);
        irr::u16 tr = static_cast<irr::u16>(2 + next);
        irr::u16 bl = static_cast<irr::u16>(2 + SIDES + i);
        irr::u16 br = static_cast<irr::u16>(2 + SIDES + next);
        buf->Indices[idx++] = tl; buf->Indices[idx++] = bl; buf->Indices[idx++] = tr;
        buf->Indices[idx++] = tr; buf->Indices[idx++] = bl; buf->Indices[idx++] = br;
    }

    buf->recalculateBoundingBox();
    buf->setHardwareMappingHint(irr::scene::EHM_STATIC);
    buf->Material.Lighting = false;
    buf->Material.BackfaceCulling = false;
    buf->Material.MaterialType = irr::video::EMT_SOLID;

    irr::scene::SMesh* mesh = new irr::scene::SMesh();
    mesh->addMeshBuffer(buf);
    buf->drop();
    mesh->recalculateBoundingBox();
    return mesh;
}

irr::scene::IMesh* DoorManager::createPlaceholderMesh(const std::string& doorName) const
{
    if (!smgr_) {
        return nullptr;
    }

    // Uppercase the name for category matching
    std::string upper = doorName;
    for (auto& c : upper) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));

    // Determine shape category from name prefix
    irr::core::vector3df dims;
    bool useOctagon = false;

    if (upper.find("BARREL") != std::string::npos || upper.find("KEG") != std::string::npos) {
        // Barrel/keg: octagonal prism
        useOctagon = true;
        dims = irr::core::vector3df(1.0f, 2.5f, 0);  // radius, height (unused z)
    } else if (upper.find("CRATE") != std::string::npos || upper.find("BOX") != std::string::npos) {
        // Crates: cube-ish, small height variation per variant
        float h = 2.0f + (doorName.empty() ? 0.0f : (doorName.back() % 5) * 0.3f);
        dims = irr::core::vector3df(2.0f, h, 2.0f);
    } else if (upper.find("DOOR") != std::string::npos || upper.find("GATE") != std::string::npos) {
        // Doors/gates: tall thin slab
        dims = irr::core::vector3df(2.0f, 6.0f, 0.5f);
    } else if (upper.find("CHEST") != std::string::npos) {
        // Chest: low wide box
        dims = irr::core::vector3df(2.0f, 1.2f, 1.5f);
    } else if (upper.find("SACK") != std::string::npos || upper.find("BAG") != std::string::npos) {
        // Sack/bag: small octagonal
        useOctagon = true;
        dims = irr::core::vector3df(0.8f, 1.5f, 0);
    } else if (upper.find("BENCH") != std::string::npos || upper.find("TABLE") != std::string::npos) {
        // Furniture: wide low slab
        dims = irr::core::vector3df(3.0f, 1.5f, 1.5f);
    } else {
        // Unknown: default cube, slightly varied by name
        dims = irr::core::vector3df(2.0f, 3.0f, 2.0f);
    }

    // Get unique color for this specific name
    irr::video::SColor color = doorName.empty()
        ? irr::video::SColor(255, 100, 150, 200)  // fallback blue-ish
        : colorFromName(doorName);

    irr::scene::IMesh* mesh = nullptr;

    if (useOctagon) {
        mesh = createOctagonalPrism(smgr_, dims.X, dims.Y, color);
    } else {
        mesh = smgr_->getGeometryCreator()->createCubeMesh(dims);
        if (mesh) {
            // Bake color into vertex colors — GLES2 shaders read aColor
            // from vertices, not DiffuseColor/AmbientColor from the material
            for (irr::u32 b = 0; b < mesh->getMeshBufferCount(); ++b) {
                irr::scene::IMeshBuffer* mb = mesh->getMeshBuffer(b);
                irr::video::S3DVertex* verts = static_cast<irr::video::S3DVertex*>(mb->getVertices());
                for (irr::u32 v = 0; v < mb->getVertexCount(); ++v) {
                    // Darken bottom vertices for depth
                    float yFactor = (verts[v].Pos.Y < 0) ? 0.7f : 1.0f;
                    verts[v].Color = irr::video::SColor(255,
                        static_cast<uint8_t>(color.getRed() * yFactor),
                        static_cast<uint8_t>(color.getGreen() * yFactor),
                        static_cast<uint8_t>(color.getBlue() * yFactor));
                }
            }
        }
    }

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

    LOG_DEBUG(MOD_GRAPHICS, "Created door {} '{}' at ({:.1f}, {:.1f}, {:.1f}) heading={:.1f} opentype={}{} state={}",
              doorId, name, x, y, z, heading, opentype,
              visual.isSpinning ? " [spinning]" : "",
              initiallyOpen ? "open" : "closed");

    return true;
}

bool DoorManager::registerDoor(uint8_t doorId, const std::string& name,
                                float x, float y, float z, float heading,
                                uint32_t incline, uint16_t size, uint8_t opentype,
                                bool initiallyOpen)
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

    return true;
}

bool DoorManager::isDoorMeshBuilt(uint8_t doorId) const {
    auto it = doors_.find(doorId);
    return it != doors_.end() && it->second.meshBuilt;
}

void DoorManager::getDoorsInRegions(const std::unordered_set<size_t>& regions, std::vector<uint8_t>& out) const {
    out.clear();
    for (const auto& [id, visual] : doors_) {
        if (!visual.meshBuilt && regions.count(visual.bspRegion) > 0) {
            out.push_back(id);
        }
    }
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

        bool doorVisible = isRegionPvsVisible(visual.bspRegion);

        // Occlusion culling: region-level stencil portal occlusion
        if (doorVisible && visual.bspRegion != SIZE_MAX && occlusionCulledRegions_
            && occlusionCulledRegions_->count(visual.bspRegion)) {
            doorVisible = false;
        }

        if (!doorVisible) {
            if (visual.inSceneGraph && visual.pivotNode) {
                visual.pivotNode->remove();
                visual.inSceneGraph = false;
            }
            continue;
        } else {
            if (!visual.inSceneGraph && visual.pivotNode) {
                smgr_->getRootSceneNode()->addChild(visual.pivotNode);
                visual.inSceneGraph = true;
                visual.pivotNode->updateAbsolutePosition();
                if (visual.sceneNode) visual.sceneNode->updateAbsolutePosition();
            }
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

void DoorManager::recomputeAllBspRegions() {
    if (!bspTree_) return;

    size_t updated = 0, nowInGraph = 0, pvsHidden = 0;

    for (auto& [id, visual] : doors_) {
        if (visual.bspRegion != SIZE_MAX) continue;  // Already has a region
        if (!visual.pivotNode) continue;

        visual.bspRegion = bspTree_->findRegionIndexForPoint(visual.x, visual.y, visual.z);
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

    // Swap the mesh on the existing scene node
    visual.sceneNode->setMesh(mesh);

    // Update materials for the new textured mesh
    for (irr::u32 i = 0; i < visual.sceneNode->getMaterialCount(); ++i) {
        visual.sceneNode->getMaterial(i).Lighting = false;
        visual.sceneNode->getMaterial(i).BackfaceCulling = false;
    }

    visual.usePlaceholder = false;
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

        // Swap the mesh on the existing scene node
        visual.sceneNode->setMesh(mesh);

        // Update materials for the new textured mesh
        for (irr::u32 i = 0; i < visual.sceneNode->getMaterialCount(); ++i) {
            visual.sceneNode->getMaterial(i).Lighting = false;
            visual.sceneNode->getMaterial(i).BackfaceCulling = false;
        }

        visual.usePlaceholder = false;
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
        if (visual.sceneNode && visual.inSceneGraph) {
            nodes.push_back(visual.sceneNode);
        }
    }
    return nodes;
}

} // namespace Graphics
} // namespace EQT
