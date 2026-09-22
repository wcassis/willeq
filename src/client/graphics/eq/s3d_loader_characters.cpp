// S3DLoader -- character models, skeletons and the _chr archive.
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

void S3DLoader::flattenSkeleton(const std::shared_ptr<SkeletonBone>& bone,
                                 std::shared_ptr<CharacterModel>& model,
                                 WldLoader& wldLoader,
                                 float parentShiftX, float parentShiftY, float parentShiftZ,
                                 float parentRotX, float parentRotY, float parentRotZ) {
    if (!bone) return;

    LOG_TRACE(MOD_GRAPHICS, "flattenSkeleton: bone='{}' modelRef={} children={}",
              bone->name, bone->modelRef, bone->children.size());

    // If this bone has a model reference, try to find its geometry
    if (bone->modelRef > 0) {
        std::shared_ptr<ZoneGeometry> geom = nullptr;

        const auto& modelRefs = wldLoader.getModelRefs();
        LOG_TRACE(MOD_GRAPHICS, "  flattenSkeleton: bone='{}' modelRef={} modelRefs_.size={}",
                  bone->name, bone->modelRef, modelRefs.size());
        auto modelIt = modelRefs.find(bone->modelRef);
        if (modelIt != modelRefs.end()) {
            uint32_t geomFragRef = modelIt->second.geometryFragRef;
            LOG_TRACE(MOD_GRAPHICS, "  flattenSkeleton: bone='{}' modelRef={} -> geomFragRef={}",
                      bone->name, bone->modelRef, geomFragRef);
            if (geomFragRef > 0) {
                geom = wldLoader.getGeometryByFragmentIndex(geomFragRef);
                if (geom) {
                    LOG_TRACE(MOD_GRAPHICS, "  flattenSkeleton: bone='{}' -> geometry='{}' verts={} materialData={} textureNames={}",
                              bone->name, geom->name, geom->vertices.size(),
                              (bool)geom->materialData, geom->textureNames().size());
                } else {
                    LOG_TRACE(MOD_GRAPHICS, "  flattenSkeleton: bone='{}' geomFragRef={} NOT FOUND in geometryByFragIndex_",
                              bone->name, geomFragRef);
                }
            } else {
                LOG_TRACE(MOD_GRAPHICS, "  flattenSkeleton: bone='{}' geomFragRef=0, skipping", bone->name);
            }
        } else {
            LOG_TRACE(MOD_GRAPHICS, "  flattenSkeleton: bone='{}' modelRef={} not in modelRefs, trying direct geometry lookup",
                      bone->name, bone->modelRef);
            geom = wldLoader.getGeometryByFragmentIndex(bone->modelRef);
            if (geom) {
                LOG_TRACE(MOD_GRAPHICS, "  flattenSkeleton: bone='{}' direct lookup -> geometry='{}' verts={} materialData={} textureNames={}",
                          bone->name, geom->name, geom->vertices.size(),
                          (bool)geom->materialData, geom->textureNames().size());
            } else {
                LOG_TRACE(MOD_GRAPHICS, "  flattenSkeleton: bone='{}' direct lookup FAILED for modelRef={}",
                          bone->name, bone->modelRef);
            }
        }

        if (geom && !geom->vertices.empty()) {
            bool alreadyAdded = false;
            for (const auto& existing : model->parts) {
                if (existing.get() == geom.get()) {
                    alreadyAdded = true;
                    break;
                }
            }
            if (!alreadyAdded) {
                model->parts.push_back(geom);

                CharacterPart part;
                part.geometry = geom;
                part.shiftX = 0;
                part.shiftY = 0;
                part.shiftZ = 0;
                part.rotateX = 0;
                part.rotateY = 0;
                part.rotateZ = 0;

                LOG_TRACE(MOD_GRAPHICS, "  flattenSkeleton: bone='{}' ADDED geometry='{}' verts={} vpieces={} materialData={} textureNames={} (partsWithTransforms now={})",
                          bone->name, geom->name, geom->vertices.size(), geom->vertexPieces.size(),
                          (bool)geom->materialData, geom->textureNames().size(),
                          model->partsWithTransforms.size() + 1);
                model->partsWithTransforms.push_back(part);
            } else {
                LOG_TRACE(MOD_GRAPHICS, "  flattenSkeleton: bone='{}' geometry='{}' already added, skipping",
                          bone->name, geom->name);
            }
        } else if (geom) {
            LOG_TRACE(MOD_GRAPHICS, "  flattenSkeleton: bone='{}' geometry='{}' has 0 vertices, skipping",
                      bone->name, geom->name);
        }
    }

    // Recursively process children
    for (const auto& child : bone->children) {
        flattenSkeleton(child, model, wldLoader, 0, 0, 0, 0, 0, 0);
    }
}

// T369 (B105). See the declaration in s3d_loader.h for why this operates on
// the filename component only.
std::string S3DLoader::deriveChrArchivePath(const std::string& archivePath) {
    namespace fs = std::filesystem;

    const fs::path input(archivePath);
    std::string stem = input.stem().string();  // filename minus the extension

    // Strip a TRAILING "_chr" (optionally with a numeric variant: "_chr2").
    // Anything earlier in the stem, and every directory component, is left
    // alone -- that is the whole of the fix.
    size_t end = stem.size();
    while (end > 0 && std::isdigit(static_cast<unsigned char>(stem[end - 1]))) {
        --end;
    }
    const std::string kChr = "_chr";
    if (end >= kChr.size() && stem.compare(end - kChr.size(), kChr.size(), kChr) == 0) {
        stem.erase(end - kChr.size());
    }

    const fs::path parent = input.parent_path();
    const std::string chrName = stem + "_chr.s3d";
    return parent.empty() ? chrName : (parent / chrName).string();
}

bool S3DLoader::loadCharacters(const std::string& archivePath) {
    // Character loading from _chr.s3d files
    // These contain skeleton tracks (0x10) with bone hierarchies

    std::string chrArchivePath = deriveChrArchivePath(archivePath);
    LOG_DEBUG(MOD_GRAPHICS, "S3DLoader::loadCharacters: archivePath='{}' -> chrArchivePath='{}'", archivePath, chrArchivePath);

    // Open the character archive
    PfsArchive chrArchive;
    if (!chrArchive.open(chrArchivePath)) {
        LOG_DEBUG(MOD_GRAPHICS, "S3DLoader::loadCharacters: failed to open '{}'", chrArchivePath);
        return false;
    }

    // Load character textures
    loadCharacterTextures(chrArchive);
    LOG_DEBUG(MOD_GRAPHICS, "S3DLoader::loadCharacters: loaded {} character textures", zone_->characterTextures.size());

    // Find WLD files in the character archive
    std::vector<std::string> wldFiles;
    chrArchive.getFilenames(".wld", wldFiles);
    LOG_DEBUG(MOD_GRAPHICS, "S3DLoader::loadCharacters: found {} WLD files in '{}'", wldFiles.size(), chrArchivePath);
    for (const auto& wf : wldFiles) {
        LOG_TRACE(MOD_GRAPHICS, "  WLD file: '{}'", wf);
    }

    if (wldFiles.empty()) {
        LOG_WARN(MOD_GRAPHICS, "S3DLoader::loadCharacters: no WLD files in '{}'", chrArchivePath);
        return false;
    }

    // Parse each WLD file for character data
    for (const auto& wld : wldFiles) {
        LOG_DEBUG(MOD_GRAPHICS, "S3DLoader::loadCharacters: parsing WLD '{}'", wld);
        WldLoader chrLoader;
        if (!chrLoader.parseFromOpenArchive(chrArchive, wld)) {  // T504: no re-open per WLD
            LOG_WARN(MOD_GRAPHICS, "S3DLoader::loadCharacters: parseFromArchive failed for '{}'", wld);
            continue;
        }

        const auto& skeletonTracks = chrLoader.getSkeletonTracks();
        const auto& geometries = chrLoader.getGeometries();
        LOG_DEBUG(MOD_GRAPHICS, "S3DLoader::loadCharacters: WLD '{}' has {} skeletonTracks, {} geometries",
                  wld, skeletonTracks.size(), geometries.size());

        // Log materialData status of all parsed geometries
        size_t geomsWithMat = 0, geomsWithoutMat = 0;
        for (const auto& g : geometries) {
            if (g && g->materialData && !g->materialData->textureNames.empty()) geomsWithMat++;
            else geomsWithoutMat++;
        }
        LOG_DEBUG(MOD_GRAPHICS, "S3DLoader::loadCharacters: geometries with materialData+names={} without={}",
                  geomsWithMat, geomsWithoutMat);

        // Create character models from skeleton tracks
        for (const auto& [fragIdx, skeleton] : skeletonTracks) {
            auto character = std::make_shared<CharacterModel>();
            character->name = skeleton->name;
            character->skeleton = skeleton;

            LOG_DEBUG(MOD_GRAPHICS, "S3DLoader::loadCharacters: processing skeleton '{}' (fragIdx={}) with {} root bones",
                      skeleton->name, fragIdx, skeleton->bones.size());

            // Flatten the skeleton into geometry parts
            for (const auto& rootBone : skeleton->bones) {
                flattenSkeleton(rootBone, character, chrLoader);
            }

            LOG_DEBUG(MOD_GRAPHICS, "S3DLoader::loadCharacters: skeleton '{}' after flattenSkeleton: parts={} partsWithTransforms={} rawParts={}",
                      skeleton->name, character->parts.size(), character->partsWithTransforms.size(), character->rawParts.size());

            // Log materialData of parts found via flattenSkeleton
            for (size_t pi = 0; pi < character->partsWithTransforms.size(); ++pi) {
                const auto& pt = character->partsWithTransforms[pi];
                if (pt.geometry) {
                    LOG_TRACE(MOD_GRAPHICS, "  partsWithTransforms[{}]: '{}' verts={} materialData={} textureNames={}",
                              pi, pt.geometry->name, pt.geometry->vertices.size(),
                              (bool)pt.geometry->materialData, pt.geometry->textureNames().size());
                }
            }

            // If no parts found through skeleton, filter geometries by race code prefix
            if (character->parts.empty() && !geometries.empty()) {
                std::string skelName = skeleton->name;
                std::transform(skelName.begin(), skelName.end(), skelName.begin(),
                              [](unsigned char c) { return std::toupper(c); });

                std::string raceCode;
                size_t hsDefPos = skelName.find("_HS_DEF");
                if (hsDefPos != std::string::npos) {
                    raceCode = skelName.substr(0, hsDefPos);
                } else {
                    raceCode = skelName.length() >= 3 ? skelName.substr(0, 3) : skelName;
                }

                LOG_DEBUG(MOD_GRAPHICS, "S3DLoader::loadCharacters: skeleton '{}' no parts from flattenSkeleton, trying prefix match with raceCode='{}'",
                          skeleton->name, raceCode);

                // Find geometries that match this race code
                for (const auto& geom : geometries) {
                    if (!geom) continue;

                    std::string geomName = geom->name;
                    std::transform(geomName.begin(), geomName.end(), geomName.begin(),
                                  [](unsigned char c) { return std::toupper(c); });

                    if (geomName.find(raceCode) == 0) {
                        LOG_TRACE(MOD_GRAPHICS, "  prefix match: '{}' verts={} materialData={} textureNames={}",
                                  geom->name, geom->vertices.size(),
                                  (bool)geom->materialData, geom->textureNames().size());

                        character->parts.push_back(geom);

                        CharacterPart part;
                        part.geometry = geom;
                        part.shiftX = part.shiftY = part.shiftZ = 0;
                        part.rotateX = part.rotateY = part.rotateZ = 0;
                        character->partsWithTransforms.push_back(part);

                        // Deep copy geometry for rawParts BEFORE skinning is applied
                        auto rawGeom = std::make_shared<ZoneGeometry>();
                        rawGeom->name = geom->name;
                        rawGeom->vertices = geom->vertices;
                        rawGeom->triangles = geom->triangles;
                        rawGeom->materialData = geom->materialData;
                        rawGeom->vertexPieces = geom->vertexPieces;
                        rawGeom->minX = geom->minX; rawGeom->minY = geom->minY; rawGeom->minZ = geom->minZ;
                        rawGeom->maxX = geom->maxX; rawGeom->maxY = geom->maxY; rawGeom->maxZ = geom->maxZ;
                        rawGeom->centerX = geom->centerX;
                        rawGeom->centerY = geom->centerY;
                        rawGeom->centerZ = geom->centerZ;

                        LOG_TRACE(MOD_GRAPHICS, "  rawGeom copy: '{}' materialData={} (same ptr: {})",
                                  rawGeom->name, (bool)rawGeom->materialData,
                                  rawGeom->materialData.get() == geom->materialData.get());

                        CharacterPart rawPart;
                        rawPart.geometry = rawGeom;
                        rawPart.shiftX = rawPart.shiftY = rawPart.shiftZ = 0;
                        rawPart.rotateX = rawPart.rotateY = rawPart.rotateZ = 0;
                        character->rawParts.push_back(rawPart);
                    }
                }

                LOG_DEBUG(MOD_GRAPHICS, "S3DLoader::loadCharacters: skeleton '{}' prefix match found {} parts, {} rawParts",
                          skeleton->name, character->parts.size(), character->rawParts.size());
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
                LOG_DEBUG(MOD_GRAPHICS, "S3DLoader::loadCharacters: storing character '{}' with parts={} partsWithTransforms={} rawParts={} animSkeleton={}",
                          character->name, character->parts.size(), character->partsWithTransforms.size(),
                          character->rawParts.size(), (bool)character->animatedSkeleton);
                // Log final materialData status of all partsWithTransforms
                for (size_t pi = 0; pi < character->partsWithTransforms.size(); ++pi) {
                    const auto& pt = character->partsWithTransforms[pi];
                    if (pt.geometry) {
                        LOG_TRACE(MOD_GRAPHICS, "  FINAL partsWithTransforms[{}]: '{}' materialData={} textureNames={}",
                                  pi, pt.geometry->name, (bool)pt.geometry->materialData, pt.geometry->textureNames().size());
                    }
                }
                // Also log rawParts materialData status
                for (size_t pi = 0; pi < character->rawParts.size(); ++pi) {
                    const auto& rp = character->rawParts[pi];
                    if (rp.geometry) {
                        LOG_TRACE(MOD_GRAPHICS, "  FINAL rawParts[{}]: '{}' materialData={} textureNames={}",
                                  pi, rp.geometry->name, (bool)rp.geometry->materialData, rp.geometry->textureNames().size());
                    }
                }
                zone_->characters.push_back(character);
            } else {
                LOG_TRACE(MOD_GRAPHICS, "S3DLoader::loadCharacters: skeleton '{}' has no parts, not stored", character->name);
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

        // Also check for static actors (like boats) that have no skeleton
        // These are defined by Actor fragments (0x14) with mesh references but no skeleton
        const auto& objectDefs = chrLoader.getObjectDefs();
        const auto& modelRefs = chrLoader.getModelRefs();

        LOG_DEBUG(MOD_GRAPHICS, "S3DLoader: Checking for static actors - {} objectDefs, {} modelRefs",
            objectDefs.size(), modelRefs.size());

        // Track which names we've already added as skeletal characters
        std::set<std::string> existingCharNames;
        for (const auto& chr : zone_->characters) {
            if (chr) {
                std::string upperName = chr->name;
                std::transform(upperName.begin(), upperName.end(), upperName.begin(),
                              [](unsigned char c) { return std::toupper(c); });
                // Extract base name (before _HS_DEF suffix)
                size_t hsDefPos = upperName.find("_HS_DEF");
                if (hsDefPos != std::string::npos) {
                    existingCharNames.insert(upperName.substr(0, hsDefPos));
                } else {
                    existingCharNames.insert(upperName);
                }
            }
        }

        for (const auto& [objName, objDef] : objectDefs) {
            // Skip if we already have this as a skeletal character
            if (existingCharNames.find(objName) != existingCharNames.end()) {
                LOG_TRACE(MOD_GRAPHICS, "S3DLoader: Skipping '{}' - already exists as skeletal character", objName);
                continue;
            }

            // Skip if no mesh references
            if (objDef.meshRefs.empty()) {
                LOG_TRACE(MOD_GRAPHICS, "S3DLoader: Skipping '{}' - no mesh references", objName);
                continue;
            }

            LOG_DEBUG(MOD_GRAPHICS, "S3DLoader: Processing static actor '{}' with {} mesh refs", objName, objDef.meshRefs.size());

            // Find geometry through mesh references
            std::vector<std::shared_ptr<ZoneGeometry>> staticMeshes;
            for (uint32_t meshRef : objDef.meshRefs) {
                LOG_TRACE(MOD_GRAPHICS, "S3DLoader:   meshRef {} -> checking modelRefs", meshRef);
                // meshRef points to a 0x2D fragment (ModelRef)
                auto modelIt = modelRefs.find(meshRef);
                if (modelIt != modelRefs.end()) {
                    uint32_t geomRef = modelIt->second.geometryFragRef;
                    LOG_TRACE(MOD_GRAPHICS, "S3DLoader:   found modelRef -> geomRef {}", geomRef);
                    if (geomRef > 0) {
                        auto geom = chrLoader.getGeometryByFragmentIndex(geomRef);
                        if (geom && !geom->vertices.empty()) {
                            staticMeshes.push_back(geom);
                            LOG_DEBUG(MOD_GRAPHICS, "S3DLoader:   found geometry '{}' with {} vertices", geom->name, geom->vertices.size());
                        } else {
                            LOG_DEBUG(MOD_GRAPHICS, "S3DLoader:   geomRef {} -> no geometry found", geomRef);
                        }
                    }
                } else {
                    LOG_TRACE(MOD_GRAPHICS, "S3DLoader:   meshRef {} not in modelRefs", meshRef);
                }
            }

            // If we found geometry, create a static character model
            if (!staticMeshes.empty()) {
                auto character = std::make_shared<CharacterModel>();
                character->name = objName + "_ACTORDEF";  // Mark as static actor

                for (const auto& geom : staticMeshes) {
                    character->parts.push_back(geom);

                    // Also add to partsWithTransforms with identity transform
                    CharacterPart part;
                    part.geometry = geom;
                    part.shiftX = part.shiftY = part.shiftZ = 0;
                    part.rotateX = part.rotateY = part.rotateZ = 0;
                    character->partsWithTransforms.push_back(part);
                }

                zone_->characters.push_back(character);
                LOG_DEBUG(MOD_GRAPHICS, "S3DLoader: Loaded static actor '{}' with {} meshes, {} vertices",
                    objName, staticMeshes.size(),
                    staticMeshes.empty() ? 0 : staticMeshes[0]->vertices.size());
            }
        }
    }

    return !zone_->characters.empty();
}


} // namespace Graphics
} // namespace EQT
