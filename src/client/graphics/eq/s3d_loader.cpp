#include "client/graphics/eq/s3d_loader.h"
#include "common/logging.h"
#include <algorithm>
#include <iostream>
#include <limits>
#include <cmath>
#include <functional>
#include <map>
#include <set>

namespace EQT {
namespace Graphics {

// Static empty vectors
const std::vector<ObjectInstance> S3DLoader::emptyObjects_;
const std::vector<std::shared_ptr<CharacterModel>> S3DLoader::emptyCharacters_;
const std::vector<std::shared_ptr<ZoneLight>> S3DLoader::emptyLights_;

// 4x4 matrix structure for bone transforms (column-major order)
struct Mat4 {
    float m[16];

    Mat4() {
        // Identity matrix
        for (int i = 0; i < 16; i++) m[i] = 0.0f;
        m[0] = m[5] = m[10] = m[15] = 1.0f;
    }

    // Matrix multiplication: result = this * other
    Mat4 operator*(const Mat4& other) const {
        Mat4 result;
        for (int i = 0; i < 4; i++) {
            for (int j = 0; j < 4; j++) {
                result.m[j * 4 + i] = 0.0f;
                for (int k = 0; k < 4; k++) {
                    result.m[j * 4 + i] += m[k * 4 + i] * other.m[j * 4 + k];
                }
            }
        }
        return result;
    }

    // Transform a point (x, y, z) by this matrix
    void transformPoint(float& x, float& y, float& z) const {
        float nx = m[0] * x + m[4] * y + m[8] * z + m[12];
        float ny = m[1] * x + m[5] * y + m[9] * z + m[13];
        float nz = m[2] * x + m[6] * y + m[10] * z + m[14];
        x = nx; y = ny; z = nz;
    }

    // Transform a normal (rotation only, no translation)
    void transformNormal(float& nx, float& ny, float& nz) const {
        float nnx = m[0] * nx + m[4] * ny + m[8] * nz;
        float nny = m[1] * nx + m[5] * ny + m[9] * nz;
        float nnz = m[2] * nx + m[6] * ny + m[10] * nz;
        nx = nnx; ny = nny; nz = nnz;
    }

    // Create translation matrix
    static Mat4 translate(float x, float y, float z) {
        Mat4 result;
        result.m[12] = x;
        result.m[13] = y;
        result.m[14] = z;
        return result;
    }

    // Create scale matrix
    static Mat4 scale(float s) {
        Mat4 result;
        result.m[0] = result.m[5] = result.m[10] = s;
        return result;
    }

    // Create rotation matrix from normalized quaternion
    static Mat4 fromQuaternion(float qx, float qy, float qz, float qw) {
        Mat4 result;

        float xx = qx * qx, yy = qy * qy, zz = qz * qz;
        float xy = qx * qy, xz = qx * qz, yz = qy * qz;
        float wx = qw * qx, wy = qw * qy, wz = qw * qz;

        result.m[0]  = 1.0f - 2.0f * (yy + zz);
        result.m[1]  = 2.0f * (xy + wz);
        result.m[2]  = 2.0f * (xz - wy);
        result.m[3]  = 0.0f;

        result.m[4]  = 2.0f * (xy - wz);
        result.m[5]  = 1.0f - 2.0f * (xx + zz);
        result.m[6]  = 2.0f * (yz + wx);
        result.m[7]  = 0.0f;

        result.m[8]  = 2.0f * (xz + wy);
        result.m[9]  = 2.0f * (yz - wx);
        result.m[10] = 1.0f - 2.0f * (xx + yy);
        result.m[11] = 0.0f;

        result.m[12] = 0.0f;
        result.m[13] = 0.0f;
        result.m[14] = 0.0f;
        result.m[15] = 1.0f;

        return result;
    }
};

bool S3DLoader::loadZone(const std::string& archivePath) {
    zone_ = nullptr;
    error_.clear();
    zoneName_ = extractZoneName(archivePath);

    PfsArchive archive;
    if (!archive.open(archivePath)) {
        error_ = "Failed to open archive: " + archivePath;
        return false;
    }

    // Find the WLD file - typically named like zonename.wld
    std::vector<std::string> wldFiles;
    archive.getFilenames(".wld", wldFiles);

    if (wldFiles.empty()) {
        error_ = "No WLD file found in archive";
        return false;
    }

    // Use the first WLD file (main zone geometry)
    // Typically the zone.wld file, not objects.wld, lights.wld, etc.
    std::string mainWld;
    for (const auto& wld : wldFiles) {
        // Prefer the main zone wld (not objects, characters, lights, etc.)
        if (wld.find("_obj") == std::string::npos &&
            wld.find("_chr") == std::string::npos &&
            wld != "objects.wld" &&
            wld != "lights.wld") {
            mainWld = wld;
            break;
        }
    }

    if (mainWld.empty()) {
        mainWld = wldFiles[0];
    }

    // Parse WLD file
    auto wldLoader = std::make_shared<WldLoader>();
    if (!wldLoader->parseFromArchive(archivePath, mainWld)) {
        error_ = "Failed to parse WLD file: " + mainWld;
        return false;
    }

    zone_ = std::make_shared<S3DZone>();
    zone_->zoneName = zoneName_;
    zone_->wldLoader = wldLoader;  // Store for PVS access
    zone_->geometry = wldLoader->getCombinedGeometry();

    if (!zone_->geometry) {
        error_ = "No geometry extracted from WLD";
        return false;
    }

    // Load textures (optional - rendering can work with vertex colors)
    loadTextures(archive);

    // Load placeable objects (optional)
    loadObjects(archivePath);

    // Load character models (optional)
    loadCharacters(archivePath);

    // Load light sources (optional)
    loadLights(archivePath);

    return true;
}

bool S3DLoader::loadTextures(PfsArchive& archive) {
    std::vector<std::string> texFiles;

    // Get BMP files
    archive.getFilenames(".bmp", texFiles);

    // Get DDS files
    std::vector<std::string> ddsFiles;
    if (archive.getFilenames(".dds", ddsFiles)) {
        texFiles.insert(texFiles.end(), ddsFiles.begin(), ddsFiles.end());
    }

    for (const auto& texName : texFiles) {
        std::vector<char> data;
        if (archive.get(texName, data)) {
            auto tex = std::make_shared<TextureInfo>();
            tex->name = texName;
            tex->data = std::move(data);
            tex->width = 0;
            tex->height = 0;

            // Store with lowercase key for case-insensitive lookup
            std::string lowerName = texName;
            std::transform(lowerName.begin(), lowerName.end(), lowerName.begin(),
                          [](unsigned char c) { return std::tolower(c); });
            zone_->textures[lowerName] = tex;
        }
    }

    return !zone_->textures.empty();
}

bool S3DLoader::loadObjectTextures(PfsArchive& archive) {
    if (!zone_) {
        return false;
    }

    std::vector<std::string> texFiles;

    // Get BMP files
    archive.getFilenames(".bmp", texFiles);

    // Get DDS files
    std::vector<std::string> ddsFiles;
    if (archive.getFilenames(".dds", ddsFiles)) {
        texFiles.insert(texFiles.end(), ddsFiles.begin(), ddsFiles.end());
    }

    for (const auto& texName : texFiles) {
        std::vector<char> data;
        if (archive.get(texName, data)) {
            auto tex = std::make_shared<TextureInfo>();
            tex->name = texName;
            tex->data = std::move(data);
            tex->width = 0;
            tex->height = 0;

            // Store with lowercase key for case-insensitive lookup
            std::string lowerName = texName;
            std::transform(lowerName.begin(), lowerName.end(), lowerName.begin(),
                          [](unsigned char c) { return std::tolower(c); });
            zone_->objectTextures[lowerName] = tex;
        }
    }

    return !zone_->objectTextures.empty();
}

std::string S3DLoader::extractZoneName(const std::string& path) {
    // Find filename from path
    size_t lastSlash = path.find_last_of("/\\");
    std::string filename = (lastSlash != std::string::npos) ?
                           path.substr(lastSlash + 1) : path;

    // Remove .s3d extension
    size_t dot = filename.find_last_of('.');
    if (dot != std::string::npos) {
        filename = filename.substr(0, dot);
    }

    // Remove _obj, _chr suffixes
    size_t underscore = filename.find('_');
    if (underscore != std::string::npos) {
        std::string suffix = filename.substr(underscore);
        if (suffix == "_obj" || suffix == "_chr" || suffix == "_chr2") {
            filename = filename.substr(0, underscore);
        }
    }

    return filename;
}

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

bool S3DLoader::loadCharacterTextures(PfsArchive& archive) {
    if (!zone_) {
        return false;
    }

    std::vector<std::string> texFiles;

    // Get BMP files
    archive.getFilenames(".bmp", texFiles);

    // Get DDS files
    std::vector<std::string> ddsFiles;
    if (archive.getFilenames(".dds", ddsFiles)) {
        texFiles.insert(texFiles.end(), ddsFiles.begin(), ddsFiles.end());
    }

    for (const auto& texName : texFiles) {
        std::vector<char> data;
        if (archive.get(texName, data)) {
            auto tex = std::make_shared<TextureInfo>();
            tex->name = texName;
            tex->data = std::move(data);
            tex->width = 0;
            tex->height = 0;

            // Store with lowercase key for case-insensitive lookup
            std::string lowerName = texName;
            std::transform(lowerName.begin(), lowerName.end(), lowerName.begin(),
                          [](unsigned char c) { return std::tolower(c); });
            zone_->characterTextures[lowerName] = tex;
        }
    }

    return !zone_->characterTextures.empty();
}

void S3DLoader::flattenSkeleton(const std::shared_ptr<SkeletonBone>& bone,
                                 std::shared_ptr<CharacterModel>& model,
                                 WldLoader& wldLoader,
                                 float parentShiftX, float parentShiftY, float parentShiftZ,
                                 float parentRotX, float parentRotY, float parentRotZ) {
    if (!bone) return;

    // For flattenSkeleton, we just collect geometry parts
    // applySkinning handles the actual bone transforms for skinned meshes
    // Legacy parameters kept for signature compatibility but not used for transform

    // Debug bone info (disabled for now)
    // if (bone->orientation) {
    //     std::cout << "  Bone '" << bone->name << "': trans=(" << bone->orientation->shiftX
    //               << "," << bone->orientation->shiftY << "," << bone->orientation->shiftZ
    //               << ") quat=(" << bone->orientation->quatX << "," << bone->orientation->quatY
    //               << "," << bone->orientation->quatZ << "," << bone->orientation->quatW
    //               << ") modelRef=" << bone->modelRef
    //               << " children=" << bone->children.size() << std::endl;
    // }

    // If this bone has a model reference, try to find its geometry
    if (bone->modelRef > 0) {
        std::shared_ptr<ZoneGeometry> geom = nullptr;

        const auto& modelRefs = wldLoader.getModelRefs();
        auto modelIt = modelRefs.find(bone->modelRef);
        if (modelIt != modelRefs.end()) {
            uint32_t geomFragRef = modelIt->second.geometryFragRef;
            LOG_DEBUG(MOD_GRAPHICS, "    -> Found modelRef {} -> geomFragRef {}", bone->modelRef, geomFragRef);
            if (geomFragRef > 0) {
                geom = wldLoader.getGeometryByFragmentIndex(geomFragRef);
                if (geom) {
                    LOG_DEBUG(MOD_GRAPHICS, "    -> Found geometry: {} verts={}", geom->name, geom->vertices.size());
                } else {
                    LOG_DEBUG(MOD_GRAPHICS, "    -> Geometry NOT found for fragRef {}", geomFragRef);
                }
            }
        } else {
            LOG_DEBUG(MOD_GRAPHICS, "    -> modelRef {} not in modelRefs, trying direct geometry lookup", bone->modelRef);
            // modelRef might directly reference a geometry (0x36) fragment
            geom = wldLoader.getGeometryByFragmentIndex(bone->modelRef);
            if (geom) {
                LOG_DEBUG(MOD_GRAPHICS, "    -> Direct lookup found: {} verts={}", geom->name, geom->vertices.size());
            } else {
                LOG_DEBUG(MOD_GRAPHICS, "    -> Direct lookup failed");
            }
        }

        if (geom && !geom->vertices.empty()) {
            // Check if we already have this geometry (in legacy parts list)
            bool alreadyAdded = false;
            for (const auto& existing : model->parts) {
                if (existing.get() == geom.get()) {
                    alreadyAdded = true;
                    break;
                }
            }
            if (!alreadyAdded) {
                // Add to legacy parts list
                model->parts.push_back(geom);

                // Also add to new parts with transforms
                CharacterPart part;
                part.geometry = geom;

                // applySkinning handles transforms for skinned meshes (those with vertex pieces)
                // Use identity transform here - transform is applied during skinning
                part.shiftX = 0;
                part.shiftY = 0;
                part.shiftZ = 0;
                part.rotateX = 0;
                part.rotateY = 0;
                part.rotateZ = 0;

                if (!geom->vertexPieces.empty()) {
                    LOG_DEBUG(MOD_GRAPHICS, "    -> Added skinned mesh (has {} vertex pieces)", geom->vertexPieces.size());
                } else {
                    LOG_DEBUG(MOD_GRAPHICS, "    -> Added static mesh");
                }
                model->partsWithTransforms.push_back(part);
            }
        }
    }

    // Recursively process children
    for (const auto& child : bone->children) {
        flattenSkeleton(child, model, wldLoader, 0, 0, 0, 0, 0, 0);
    }
}

bool S3DLoader::loadCharacters(const std::string& archivePath) {
    // Character loading from _chr.s3d files
    // These contain skeleton tracks (0x10) with bone hierarchies

    // Build path for character file
    std::string basePath = archivePath;
    size_t dotPos = basePath.find_last_of('.');
    if (dotPos != std::string::npos) {
        basePath = basePath.substr(0, dotPos);
    }

    // Remove _chr suffix if present (we want the base zone name)
    size_t chrSuffix = basePath.find("_chr");
    if (chrSuffix != std::string::npos) {
        basePath = basePath.substr(0, chrSuffix);
    }

    std::string chrArchivePath = basePath + "_chr.s3d";

    // Open the character archive
    PfsArchive chrArchive;
    if (!chrArchive.open(chrArchivePath)) {
        // No character archive, that's okay
        return false;
    }

    // Load character textures
    loadCharacterTextures(chrArchive);

    // Find WLD files in the character archive
    std::vector<std::string> wldFiles;
    chrArchive.getFilenames(".wld", wldFiles);

    if (wldFiles.empty()) {
        return false;
    }

    // Parse each WLD file for character data
    for (const auto& wld : wldFiles) {
        WldLoader chrLoader;
        if (!chrLoader.parseFromArchive(chrArchivePath, wld)) {
            continue;
        }

        // Check if this WLD has skeleton data
        if (!chrLoader.hasCharacterData()) {
            continue;
        }

        const auto& skeletonTracks = chrLoader.getSkeletonTracks();
        const auto& geometries = chrLoader.getGeometries();

        // Create character models from skeleton tracks
        for (const auto& [fragIdx, skeleton] : skeletonTracks) {
            auto character = std::make_shared<CharacterModel>();
            character->name = skeleton->name;
            character->skeleton = skeleton;

            // Flatten the skeleton into geometry parts
            for (const auto& rootBone : skeleton->bones) {
                flattenSkeleton(rootBone, character, chrLoader);
            }

            // If no parts found through skeleton, filter geometries by race code prefix
            // In global_chr.s3d, meshes are named like "HUM_DMSPRITEDEF", "HUMHE00_DMSPRITEDEF"
            // The skeleton name is like "HUM_HS_DEF" - extract the race code prefix
            if (character->parts.empty() && !geometries.empty()) {
                // Extract race code from skeleton name (e.g., "HUM" from "HUM_HS_DEF")
                std::string skelName = skeleton->name;
                std::transform(skelName.begin(), skelName.end(), skelName.begin(),
                              [](unsigned char c) { return std::toupper(c); });

                std::string raceCode;
                size_t hsDefPos = skelName.find("_HS_DEF");
                if (hsDefPos != std::string::npos) {
                    raceCode = skelName.substr(0, hsDefPos);
                } else {
                    // Fallback: use first 3 characters
                    raceCode = skelName.length() >= 3 ? skelName.substr(0, 3) : skelName;
                }

                // Find geometries that match this race code
                for (const auto& geom : geometries) {
                    if (!geom) continue;

                    std::string geomName = geom->name;
                    std::transform(geomName.begin(), geomName.end(), geomName.begin(),
                                  [](unsigned char c) { return std::toupper(c); });

                    // Check if geometry name starts with the race code
                    // e.g., "HUM_DMSPRITEDEF" starts with "HUM"
                    // e.g., "HUMHE00_DMSPRITEDEF" starts with "HUM"
                    if (geomName.find(raceCode) == 0) {
                        character->parts.push_back(geom);

                        // Also add to partsWithTransforms with identity transform
                        CharacterPart part;
                        part.geometry = geom;
                        part.shiftX = part.shiftY = part.shiftZ = 0;
                        part.rotateX = part.rotateY = part.rotateZ = 0;
                        character->partsWithTransforms.push_back(part);

                        // Deep copy geometry for rawParts BEFORE skinning is applied
                        // This is critical for animation - we need the original vertex positions
                        auto rawGeom = std::make_shared<ZoneGeometry>();
                        rawGeom->name = geom->name;
                        rawGeom->vertices = geom->vertices;  // Copy vertices
                        rawGeom->triangles = geom->triangles;
                        rawGeom->textureNames = geom->textureNames;
                        rawGeom->textureInvisible = geom->textureInvisible;
                        rawGeom->vertexPieces = geom->vertexPieces;
                        rawGeom->minX = geom->minX; rawGeom->minY = geom->minY; rawGeom->minZ = geom->minZ;
                        rawGeom->maxX = geom->maxX; rawGeom->maxY = geom->maxY; rawGeom->maxZ = geom->maxZ;
                        rawGeom->centerX = geom->centerX;
                        rawGeom->centerY = geom->centerY;
                        rawGeom->centerZ = geom->centerZ;

                        CharacterPart rawPart;
                        rawPart.geometry = rawGeom;
                        rawPart.shiftX = rawPart.shiftY = rawPart.shiftZ = 0;
                        rawPart.rotateX = rawPart.rotateY = rawPart.rotateZ = 0;
                        character->rawParts.push_back(rawPart);
                    }
                }

                if (!character->parts.empty()) {
                    LOG_TRACE(MOD_GRAPHICS, "S3DLoader: Filtered {} meshes for race code '{}'",
                        character->parts.size(), raceCode);
                }
            }

            // Apply skinning to meshes that have vertex piece data
            // NOTE: This modifies character->parts in place. rawParts contains the unskinned copies.
            for (auto& geom : character->parts) {
                if (geom && !geom->vertexPieces.empty() && skeleton) {
                    // std::cout << "Applying skinning to mesh " << geom->name
                    //           << " with " << geom->vertexPieces.size() << " vertex pieces" << std::endl;
                    applySkinning(geom, skeleton);
                }
            }

            // Build animated skeleton with animation data
            // Extract model code from skeleton name (e.g., "HUM" from "HUM_HS_DEF")
            std::string skelName = skeleton->name;
            std::transform(skelName.begin(), skelName.end(), skelName.begin(),
                          [](unsigned char c) { return std::toupper(c); });
            std::string modelCode;
            size_t hsDefPos = skelName.find("_HS_DEF");
            if (hsDefPos != std::string::npos) {
                modelCode = skelName.substr(0, hsDefPos);
            } else {
                modelCode = skelName.length() >= 3 ? skelName.substr(0, 3) : skelName;
            }
            character->animatedSkeleton = buildAnimatedSkeleton(modelCode, skeleton, chrLoader);

            if (!character->parts.empty()) {
                zone_->characters.push_back(character);
            }
        }

        // If no skeletons but we have geometry, create a simple character model
        if (skeletonTracks.empty() && !geometries.empty()) {
            auto character = std::make_shared<CharacterModel>();
            character->name = wld;
            for (const auto& geom : geometries) {
                character->parts.push_back(geom);
            }
            zone_->characters.push_back(character);
        }
    }

    return !zone_->characters.empty();
}

bool S3DLoader::loadLights(const std::string& archivePath) {
    // Light sources are stored in lights.wld within the main zone archive
    if (!zone_) {
        return false;
    }

    PfsArchive archive;
    if (!archive.open(archivePath)) {
        return false;
    }

    // Check for lights.wld in the archive
    std::vector<std::string> wldFiles;
    archive.getFilenames(".wld", wldFiles);

    bool hasLightsWld = false;
    for (const auto& wld : wldFiles) {
        if (wld == "lights.wld") {
            hasLightsWld = true;
            break;
        }
    }

    if (!hasLightsWld) {
        // No lights.wld, no lights to load
        return false;
    }

    // Parse lights.wld
    WldLoader lightsLoader;
    if (!lightsLoader.parseFromArchive(archivePath, "lights.wld")) {
        return false;
    }

    // Get the parsed lights
    const auto& lights = lightsLoader.getLights();
    if (lights.empty()) {
        return false;
    }

    // Copy lights to zone
    zone_->lights = lights;

    return !zone_->lights.empty();
}

// Note: buildBoneTransforms is no longer used - applySkinning handles bone transforms
// using proper quaternion-based matrix multiplication. Keeping declaration for ABI compatibility.
void S3DLoader::buildBoneTransforms(const std::shared_ptr<SkeletonBone>& bone,
                                     std::vector<CharacterPart>& transforms,
                                     int boneIndex,
                                     float parentShiftX, float parentShiftY, float parentShiftZ,
                                     float parentRotX, float parentRotY, float parentRotZ) {
    // Deprecated - bone transforms now handled by applySkinning with Mat4 matrices
    (void)bone; (void)transforms; (void)boneIndex;
    (void)parentShiftX; (void)parentShiftY; (void)parentShiftZ;
    (void)parentRotX; (void)parentRotY; (void)parentRotZ;
}

void S3DLoader::applySkinning(std::shared_ptr<ZoneGeometry>& mesh,
                               const std::shared_ptr<SkeletonTrack>& skeleton) {
    if (!mesh || !skeleton || mesh->vertexPieces.empty()) {
        return;
    }

    // Use allBones from skeleton which preserves original file order
    // This is the order that vertex piece bone indices reference
    const auto& allBones = skeleton->allBones;
    const auto& parentIndices = skeleton->parentIndices;

    if (allBones.empty()) {
        LOG_TRACE(MOD_GRAPHICS, "applySkinning: No bones in skeleton");
        return;
    }

    LOG_TRACE(MOD_GRAPHICS, "applySkinning: Skeleton has {} bones total", allBones.size());

    // Build bone world matrices indexed by original bone order
    std::vector<Mat4> boneMatrices(allBones.size());

    // Compute world transforms for each bone using proper matrix multiplication
    // Process in order (0 to N-1) - parents are guaranteed to come before children
    for (size_t i = 0; i < allBones.size(); ++i) {
        const auto& bone = allBones[i];
        int parentIdx = (i < parentIndices.size()) ? parentIndices[i] : -1;

        // Get local transform components
        float qx = 0, qy = 0, qz = 0, qw = 1;  // Identity quaternion
        float tx = 0, ty = 0, tz = 0;          // No translation
        float scale = 1.0f;                     // Unit scale

        if (bone && bone->orientation) {
            qx = bone->orientation->quatX;
            qy = bone->orientation->quatY;
            qz = bone->orientation->quatZ;
            qw = bone->orientation->quatW;
            tx = bone->orientation->shiftX;
            ty = bone->orientation->shiftY;
            tz = bone->orientation->shiftZ;
            scale = bone->orientation->scale;
        }

        // Build local transform matrix: T * R * S (Translation * Rotation * Scale)
        Mat4 scaleMat = Mat4::scale(scale);
        Mat4 rotMat = Mat4::fromQuaternion(qx, qy, qz, qw);
        Mat4 transMat = Mat4::translate(tx, ty, tz);

        // Local matrix = T * R * S
        Mat4 localMat = transMat * rotMat * scaleMat;

        // World matrix = parent world matrix * local matrix
        if (parentIdx >= 0 && static_cast<size_t>(parentIdx) < boneMatrices.size()) {
            boneMatrices[i] = boneMatrices[parentIdx] * localMat;
        } else {
            boneMatrices[i] = localMat;
        }

        if (i < 10 || (i >= 40 && i <= 55)) {
            std::string boneName = bone ? bone->name : "(null)";
            LOG_TRACE(MOD_GRAPHICS, "  Bone[{}] '{}' parent={} trans=({},{},{}) quat=({},{},{},{}) scale={}",
                i, boneName, parentIdx, tx, ty, tz, qx, qy, qz, qw, scale);
        }
    }

    // Find max bone index from vertex pieces to validate
    int maxBoneIndex = 0;
    for (const auto& piece : mesh->vertexPieces) {
        maxBoneIndex = std::max(maxBoneIndex, static_cast<int>(piece.boneIndex));
    }

    LOG_TRACE(MOD_GRAPHICS, "applySkinning: {} vertex pieces, max bone index {}, {} bone matrices",
        mesh->vertexPieces.size(), maxBoneIndex, boneMatrices.size());

    // Apply bone transforms to vertices based on vertex pieces
    size_t vertexStart = 0;
    for (const auto& piece : mesh->vertexPieces) {
        int boneIdx = piece.boneIndex;
        if (boneIdx < 0 || boneIdx >= static_cast<int>(boneMatrices.size())) {
            vertexStart += piece.count;
            continue;
        }

        const Mat4& boneMat = boneMatrices[boneIdx];

        // Apply transform to each vertex in this piece
        for (int i = 0; i < piece.count; ++i) {
            size_t vIdx = vertexStart + i;
            if (vIdx >= mesh->vertices.size()) break;

            Vertex3D& v = mesh->vertices[vIdx];

            // Transform position by full bone matrix
            boneMat.transformPoint(v.x, v.y, v.z);

            // Transform normal (rotation only, no translation)
            boneMat.transformNormal(v.nx, v.ny, v.nz);

            // Renormalize normal
            float nlen = std::sqrt(v.nx*v.nx + v.ny*v.ny + v.nz*v.nz);
            if (nlen > 0.0001f) {
                v.nx /= nlen;
                v.ny /= nlen;
                v.nz /= nlen;
            }
        }

        vertexStart += piece.count;
    }

    // Recalculate bounding box
    mesh->minX = mesh->minY = mesh->minZ = std::numeric_limits<float>::max();
    mesh->maxX = mesh->maxY = mesh->maxZ = std::numeric_limits<float>::lowest();
    for (const auto& v : mesh->vertices) {
        mesh->minX = std::min(mesh->minX, v.x);
        mesh->minY = std::min(mesh->minY, v.y);
        mesh->minZ = std::min(mesh->minZ, v.z);
        mesh->maxX = std::max(mesh->maxX, v.x);
        mesh->maxY = std::max(mesh->maxY, v.y);
        mesh->maxZ = std::max(mesh->maxZ, v.z);
    }

    LOG_TRACE(MOD_GRAPHICS, "applySkinning: bounds after skinning X[{},{}] Y[{},{}] Z[{},{}]",
        mesh->minX, mesh->maxX, mesh->minY, mesh->maxY, mesh->minZ, mesh->maxZ);
}

std::shared_ptr<CharacterSkeleton> S3DLoader::buildAnimatedSkeleton(
    const std::string& modelCode,
    const std::shared_ptr<SkeletonTrack>& skeleton,
    WldLoader& wldLoader) {

    if (!skeleton || skeleton->allBones.empty()) {
        return nullptr;
    }

    auto animSkel = std::make_shared<CharacterSkeleton>();
    animSkel->modelCode = modelCode;

    // Build bone list from skeleton
    animSkel->bones.resize(skeleton->allBones.size());
    for (size_t i = 0; i < skeleton->allBones.size(); ++i) {
        const auto& srcBone = skeleton->allBones[i];
        AnimatedBone& dstBone = animSkel->bones[i];

        dstBone.name = srcBone ? srcBone->name : "";
        dstBone.parentIndex = (i < skeleton->parentIndices.size()) ? skeleton->parentIndices[i] : -1;

        // Copy the bone's orientation as the default pose track
        // This is critical - the pose data comes from the skeleton hierarchy (Fragment 0x10),
        // not from track refs. Without this, bones use identity transforms.
        if (srcBone && srcBone->orientation) {
            auto poseTrack = std::make_shared<TrackDef>();
            poseTrack->name = dstBone.name + "_pose";
            poseTrack->frames.push_back(*srcBone->orientation);  // Single-frame pose
            dstBone.poseTrack = poseTrack;
        }

        // Convert bone name to lowercase for matching
        std::string lowerName = dstBone.name;
        std::transform(lowerName.begin(), lowerName.end(), lowerName.begin(),
                      [](unsigned char c) { return std::tolower(c); });

        // Remove common suffixes for cleaner matching
        size_t suffixPos = lowerName.find("_dag");
        if (suffixPos != std::string::npos) {
            lowerName = lowerName.substr(0, suffixPos);
        }
        dstBone.name = lowerName;

        // Fill in child indices
        for (size_t j = 0; j < skeleton->allBones.size(); ++j) {
            if (j < skeleton->parentIndices.size() && skeleton->parentIndices[j] == static_cast<int>(i)) {
                dstBone.childIndices.push_back(static_cast<int>(j));
            }
        }
    }

    // Get track references and definitions from WLD loader
    const auto& trackRefs = wldLoader.getTrackRefs();
    const auto& trackDefs = wldLoader.getTrackDefs();

    // Lower-case model code for matching
    std::string lowerModelCode = modelCode;
    std::transform(lowerModelCode.begin(), lowerModelCode.end(), lowerModelCode.begin(),
                  [](unsigned char c) { return std::tolower(c); });

    // Collect animations by matching model code
    std::map<std::string, std::shared_ptr<Animation>> animations;
    int poseTracksFound = 0;
    int animTracksFound = 0;

    for (const auto& [fragIdx, trackRef] : trackRefs) {
        if (!trackRef || !trackRef->isNameParsed) continue;

        // Check if this track belongs to our model
        if (trackRef->modelCode != lowerModelCode) continue;

        // Get the track definition (keyframe data)
        auto trackDef = wldLoader.getTrackDef(trackRef->trackDefRef);
        if (!trackDef) continue;

        // Find the bone this track applies to
        std::string boneName = trackRef->boneName;
        int boneIdx = animSkel->getBoneIndex(boneName);

        // Try alternate name formats if not found
        if (boneIdx < 0 && !boneName.empty() && boneName[0] == '_') {
            boneIdx = animSkel->getBoneIndex(boneName.substr(1));
        }
        if (boneIdx < 0) {
            // Try with lowercase conversion
            std::string lowerBone = boneName;
            std::transform(lowerBone.begin(), lowerBone.end(), lowerBone.begin(),
                          [](unsigned char c) { return std::tolower(c); });
            boneIdx = animSkel->getBoneIndex(lowerBone);
        }
        // Try with model prefix (e.g., "pe" -> "elepe" for model "ele")
        if (boneIdx < 0 && !boneName.empty()) {
            std::string prefixedName = lowerModelCode + boneName;
            boneIdx = animSkel->getBoneIndex(prefixedName);
        }
        // Root bone special case
        if (boneIdx < 0 && (boneName == "root" || boneName.empty())) {
            boneIdx = animSkel->getBoneIndex(lowerModelCode);  // e.g., "ele" for ELE model
        }

        if (boneIdx < 0) {
            // Bone not found - skip this track
            continue;
        }

        AnimatedBone& bone = animSkel->bones[boneIdx];

        if (trackRef->isPoseAnimation || trackRef->animCode == "pos") {
            // This is the default pose
            bone.poseTrack = trackDef;
            poseTracksFound++;
        } else {
            // This is an action animation
            bone.animationTracks[trackRef->animCode] = trackDef;
            animTracksFound++;

            // Create or update animation entry
            auto& anim = animations[trackRef->animCode];
            if (!anim) {
                anim = std::make_shared<Animation>();
                anim->name = trackRef->animCode;
                anim->modelCode = lowerModelCode;
                anim->frameCount = 0;
                anim->animationTimeMs = 0;

                // Determine if this animation should loop
                static const std::set<std::string> loopedAnims = {
                    "pos", "p01", "p03", "p06", "p07", "p08",  // Standing/idle poses
                    "l01", "l02",                               // Walk/run
                    "l05", "l06", "l07", "l09",                 // Falling, crouch walk, climbing, swim
                    "o01", "o02", "o03"                         // Idle animations
                };
                anim->isLooped = (loopedAnims.find(trackRef->animCode) != loopedAnims.end());
            }

            // Update frame count
            if (static_cast<int>(trackDef->frames.size()) > anim->frameCount) {
                anim->frameCount = static_cast<int>(trackDef->frames.size());
            }

            // Update animation duration
            int trackTimeMs = (trackRef->frameMs > 0)
                ? trackRef->frameMs * static_cast<int>(trackDef->frames.size())
                : 100 * static_cast<int>(trackDef->frames.size());  // Default 100ms per frame
            if (trackTimeMs > anim->animationTimeMs) {
                anim->animationTimeMs = trackTimeMs;
            }

            // Store track reference in animation
            anim->tracks[boneName] = trackRef;
        }
    }

    animSkel->animations = animations;

    if (poseTracksFound > 0 || animTracksFound > 0) {
        LOG_TRACE(MOD_GRAPHICS, "S3DLoader: Built animated skeleton for '{}' with {} bones, {} pose tracks, {} animation tracks, {} unique animations",
            modelCode, animSkel->bones.size(), poseTracksFound, animTracksFound, animations.size());

        // List animations found
        for (const auto& [animCode, anim] : animations) {
            LOG_TRACE(MOD_GRAPHICS, "  Animation '{}': {} frames, {}ms{}",
                animCode, anim->frameCount, anim->animationTimeMs, (anim->isLooped ? " (looped)" : ""));
        }
    }

    return animSkel;
}

} // namespace Graphics
} // namespace EQT
