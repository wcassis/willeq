// S3DLoader -- placeable object geometry and instances (_obj archive).
//
// T451: split out of the 1,336-line src/client/graphics/eq/s3d_loader.cpp.
// Bodies are contiguous line ranges of that file, moved verbatim.

#include "client/graphics/eq/s3d_loader.h"
#include "client/graphics/eq/equipment_actor_geometry.h"
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
    PfsArchive mainArchive;
    if (!mainArchive.open(archivePath)) {
        return false;
    }
    return loadObjectsFromArchive(mainArchive, archivePath);
}

bool S3DLoader::loadObjectsFromArchive(PfsArchive& mainArchive, const std::string& archivePath) {
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

    // Parse objects.wld (0x15 placement fragments) from the open archive
    WldLoader objPlacementLoader;
    if (!objPlacementLoader.parseFromOpenArchive(mainArchive, "objects.wld")) {
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
    if (!objDefLoader.parseFromOpenArchive(objArchive, objWldName)) {
        // Try alternative name without zone prefix
        std::vector<std::string> objWldFiles;
        objArchive.getFilenames(".wld", objWldFiles);

        bool loaded = false;
        for (const auto& wld : objWldFiles) {
            if (objDefLoader.parseFromOpenArchive(objArchive, wld)) {
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

    // B734 (issue #71): a SKINNED object geometry -- vertex pieces naming the
    // bones of the 0x10 skeleton that lists it (flag 0x200, T476) -- is stored
    // in the archive relative to those bones, with a degenerate center and
    // bounds (thedeep's STOR301_DMSPRITEDEF: 189 verts, 1 piece,
    // Center (0,0,0), Bounds (0,0,0)-(0,0,0)). Storing it with only the center
    // offset applied kept every piece at its bone's origin -- the un-posed
    // torch. Pose it with T476's equipment-actor resolver (the same rest-pose
    // path, not a second skinning implementation) and store it under the
    // ACTOR name -- the name the placement (0x15) and DoorManager use, e.g.
    // TDTORCH301, not the MESH name STOR301. The mesh name is aliased to the
    // SAME shared geometry (one copy, per the 100 %-memory rule), so the
    // pre-B734 lookup by mesh name keeps working.
    //
    // Which skeleton owns a skinned mesh: the 0x10 whose meshRefs resolve to
    // its fragment index. Which actors name that skeleton: the 0x14s whose
    // meshRefs resolve to the 0x11 track ref (parseFragment14's
    // objectDefs_/modelRefs_/skeletonRefs_ chain, the same chain
    // resolveEquipmentActorGeometry() walks).
    std::map<size_t, const SkeletonTrack*> skinnedGeomTrack;
    for (const auto& geom : geometries) {
        if (!geom || geom->vertices.empty() || geom->vertexPieces.empty()) continue;
        for (const auto& [trackRef, track] : objDefLoader.getSkeletonTracks()) {
            if (!track) continue;
            for (uint32_t meshRef : track->meshRefs) {
                auto modelIt = objDefLoader.getModelRefs().find(meshRef);
                if (modelIt == objDefLoader.getModelRefs().end()
                    || modelIt->second.geometryFragRef == 0) continue;
                if (objDefLoader.getGeometryByFragmentIndex(modelIt->second.geometryFragRef)
                        .get() == geom.get()) {
                    skinnedGeomTrack[&geom - &geometries[0]] = track.get();
                }
            }
        }
    }
    std::map<uint32_t, std::vector<std::string>> trackToActors;
    for (const auto& [actorName, def] : objectDefs) {
        for (uint32_t meshRef : def.meshRefs) {
            auto skelRef = objDefLoader.getSkeletonRefs().find(meshRef);
            if (skelRef != objDefLoader.getSkeletonRefs().end()) {
                trackToActors[skelRef->second].push_back(actorName);
            }
        }
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

        if (baseName.empty()) continue;

        // Skinned geometry: store the rest-pose geometry under the ACTOR name
        // (B734), aliased under the mesh name.
        auto trackIt = skinnedGeomTrack.find(&geom - &geometries[0]);
        if (trackIt != skinnedGeomTrack.end()) {
            // Prefer the actor whose name prefixes the mesh base name (the
            // skeleton's own DAG, e.g. TDTORCH301 for STOR301); otherwise any
            // actor on this skeleton (std::map: objectDefs' name order).
            std::string actorName;
            for (const auto& [trackRef, names] : trackToActors) {
                auto trackDef = objDefLoader.getSkeletonTracks().find(trackRef);
                if (trackDef == objDefLoader.getSkeletonTracks().end()
                    || trackDef->second.get() != trackIt->second) continue;
                for (const auto& name : names) {
                    if (actorName.empty()
                        || (baseName.compare(0, name.size(), name) == 0)) {
                        actorName = name;
                    }
                }
            }
            if (!actorName.empty()) {
                auto resolved = resolveEquipmentActorGeometry(objDefLoader, actorName);
                if (resolved && !resolved->vertices.empty()) {
                    zone_->objectGeometries[actorName] = resolved;
                    // Alias the mesh name to the SAME shared geometry.
                    if (actorName != baseName) {
                        zone_->objectGeometries[baseName] = resolved;
                    }
                    LOG_DEBUG(MOD_GRAPHICS,
                        "Stored skinned object geometry '{}' -> actor '{}' ({} verts, rest pose, B734)",
                        baseName, actorName, resolved->vertices.size());
                    continue;
                }
                LOG_WARN(MOD_GRAPHICS,
                    "B734: rest-pose resolution of actor '{}' failed; storing '{}' un-posed",
                    actorName, baseName);
            } else {
                LOG_WARN(MOD_GRAPHICS,
                    "B734: skinned geometry '{}' has no naming actor; storing un-posed", baseName);
            }
        }

        // Store geometry by base name with center offset applied to vertices
        // This matches eqsage which adds center to vertex positions before export
        {
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
