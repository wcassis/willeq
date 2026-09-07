// S3DLoader -- placeable object geometry and instances (_obj archive).
//
// T451: split out of the 1,336-line src/client/graphics/eq/s3d_loader.cpp.
// Bodies are contiguous line ranges of that file, moved verbatim.

#include "client/graphics/eq/s3d_loader.h"
#include "common/logging.h"
#include <algorithm>
#include <iostream>
#include <limits>
#include <cmath>
#include <cctype>
#include <filesystem>
#include <functional>
#include <map>
#include <set>

namespace EQT {
namespace Graphics {

bool S3DLoader::loadObjects(const std::string& archivePath) {
    // Object loading involves three files:
    // 1. zonename.s3d/objects.wld - contains placement instances (0x15 fragments)
    // 2. zonename_obj.s3d/zonename_obj.wld - contains object definitions (0x14) and geometry (0x36)

    // Build paths for related files
    std::string basePath = archivePath;
    size_t dotPos = basePath.find_last_of('.');
    if (dotPos != std::string::npos) {
        basePath = basePath.substr(0, dotPos);
    }

    // Remove _obj suffix if present (we want the base zone name)
    size_t objSuffix = basePath.find("_obj");
    if (objSuffix != std::string::npos) {
        basePath = basePath.substr(0, objSuffix);
    }

    std::string objArchivePath = basePath + "_obj.s3d";
    std::string objWldName = zoneName_ + "_obj.wld";

    // First, try to load object placement data from objects.wld in main archive
    WldLoader placementLoader;
    PfsArchive mainArchive;
    if (!mainArchive.open(archivePath)) {
        return false;
    }

    // Check for objects.wld in main archive
    std::vector<std::string> wldFiles;
    mainArchive.getFilenames(".wld", wldFiles);

    bool hasObjectsWld = false;
    for (const auto& wld : wldFiles) {
        if (wld == "objects.wld") {
            hasObjectsWld = true;
            break;
        }
    }

    if (!hasObjectsWld) {
        // No objects.wld, no placements to load
        return false;
    }

    // Parse objects.wld to get placement instances
    std::vector<char> placementBuffer;
    if (!mainArchive.get("objects.wld", placementBuffer)) {
        return false;
    }

    // Need to parse this WLD for 0x15 fragments only
    WldLoader objPlacementLoader;
    if (!objPlacementLoader.parseFromArchive(archivePath, "objects.wld")) {
        // No placements found
        return false;
    }

    const auto& placeables = objPlacementLoader.getPlaceables();
    if (placeables.empty()) {
        return false;
    }

    // Now load object definitions and geometry from _obj.s3d
    PfsArchive objArchive;
    if (!objArchive.open(objArchivePath)) {
        // Object archive doesn't exist
        return false;
    }

    // Load textures from _obj.s3d archive for object rendering
    loadObjectTextures(objArchive);
    LOG_DEBUG(MOD_GRAPHICS, "Loaded {} object textures from _obj archive", zone_->objectTextures.size());

    WldLoader objDefLoader;
    if (!objDefLoader.parseFromArchive(objArchivePath, objWldName)) {
        // Try alternative name without zone prefix
        std::vector<std::string> objWldFiles;
        objArchive.getFilenames(".wld", objWldFiles);

        bool loaded = false;
        for (const auto& wld : objWldFiles) {
            if (objDefLoader.parseFromArchive(objArchivePath, wld)) {
                loaded = true;
                break;
            }
        }

        if (!loaded) {
            return false;
        }
    }

    const auto& objectDefs = objDefLoader.getObjectDefs();
    const auto& modelRefs = objDefLoader.getModelRefs();
    const auto& geometries = objDefLoader.getGeometries();

    if (objectDefs.empty()) {
        return false;
    }

    // Store all object geometries by name for dynamic placement (e.g., doors)
    // This allows finding geometries even for objects not pre-placed in the zone
    for (const auto& geom : geometries) {
        if (!geom || geom->vertices.empty()) continue;

        // Extract base name from geometry name (e.g., "DOOR1_DMSPRITEDEF" -> "DOOR1")
        std::string baseName = geom->name;
        std::transform(baseName.begin(), baseName.end(), baseName.begin(), ::toupper);

        size_t suffixPos = baseName.find("_DMSPRITEDEF");
        if (suffixPos != std::string::npos) {
            baseName = baseName.substr(0, suffixPos);
        }

        // Store geometry by base name with center offset applied to vertices
        // This matches eqsage which adds center to vertex positions before export
        if (!baseName.empty()) {
            // Create a copy with center offset applied to vertices
            auto adjustedGeom = std::make_shared<ZoneGeometry>(*geom);

            // Apply center offset to all vertices (in EQ coordinate space)
            // This positions vertices at their actual world locations
            for (auto& v : adjustedGeom->vertices) {
                v.x += geom->centerX;
                v.y += geom->centerY;
                v.z += geom->centerZ;
            }

            // Update bounding box to match adjusted vertices
            adjustedGeom->minX += geom->centerX;
            adjustedGeom->maxX += geom->centerX;
            adjustedGeom->minY += geom->centerY;
            adjustedGeom->maxY += geom->centerY;
            adjustedGeom->minZ += geom->centerZ;
            adjustedGeom->maxZ += geom->centerZ;

            // Clear center since it's now baked into vertices
            adjustedGeom->centerX = 0;
            adjustedGeom->centerY = 0;
            adjustedGeom->centerZ = 0;

            zone_->objectGeometries[baseName] = adjustedGeom;
            LOG_DEBUG(MOD_GRAPHICS, "Stored object geometry '{}' ({} verts, {} tris, center applied: {:.2f},{:.2f},{:.2f})",
                baseName, adjustedGeom->vertices.size(), adjustedGeom->triangles.size(),
                geom->centerX, geom->centerY, geom->centerZ);
        }
    }

    LOG_DEBUG(MOD_GRAPHICS, "Loaded {} object geometries from _obj.wld",
        zone_->objectGeometries.size());

    // Build a map of geometry by fragment index
    // The geometries are stored in order they were parsed
    // We need to map 0x2D refs -> 0x36 geometry

    // Match placeables to their geometry
    for (const auto& placeable : placeables) {
        const std::string& objName = placeable->getName();

        // Find object definition by name
        auto defIt = objectDefs.find(objName);
        if (defIt == objectDefs.end()) {
            continue;  // Object definition not found
        }

        const WldObjectDef& def = defIt->second;

        // Get geometry through the reference chain
        // def.meshRefs contains indices to 0x2D fragments
        for (uint32_t meshRef : def.meshRefs) {
            auto modelIt = modelRefs.find(meshRef);
            if (modelIt == modelRefs.end()) {
                continue;
            }

            uint32_t geomRef = modelIt->second.geometryFragRef;
            if (geomRef == 0) {
                continue;
            }

            // Find geometry with matching index
            // Since geometries are parsed in order, we need to find by fragment index
            // The geometries vector may not directly correspond to fragment indices

            // For simplicity, use the first geometry from the object definition
            // This works for most single-mesh objects
            if (!geometries.empty()) {
                // Find a geometry that might match
                // Use the adjusted geometry from objectGeometries (has center offset applied)
                std::shared_ptr<ZoneGeometry> geom = nullptr;

                // Try to find in already-adjusted objectGeometries first
                auto objGeomIt = zone_->objectGeometries.find(objName);
                if (objGeomIt != zone_->objectGeometries.end()) {
                    geom = objGeomIt->second;
                }

                // If no match found, skip this object
                if (!geom) {
                    continue;
                }

                ObjectInstance instance;
                instance.placeable = placeable;
                instance.geometry = geom;
                zone_->objects.push_back(instance);
            }
        }
    }

    return !zone_->objects.empty();
}


} // namespace Graphics
} // namespace EQT
