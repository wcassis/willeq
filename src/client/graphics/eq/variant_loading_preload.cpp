// RaceModelLoader variant loading - the variant cache and its preloader
// These methods remain part of the RaceModelLoader class
//
// Split out of variant_loading.cpp by
// refactor/J35_variant_loading_split.md (move-only, verbatim relocation).
// This TU holds getVariantModelData and preloadVariantModel.
//
// preloadVariantModel is 396 lines long. It is a recorded decomposition
// candidate on the Batch-G follow-up list in
// refactor/J_file_size_reclamation.md (section Optional), and J35
// deliberately did not touch it, for the same reason as its 815-line
// sibling: a verbatim-relocation commit must stay verbatim. This is not a
// ceiling problem either - the TU sits 1,060 lines under the 1,500-line
// warn ceiling.
//
// Siblings: variant_loading.cpp (race and appearance mesh resolution) and
// variant_loading_animation.cpp (the animation-aware variant loader).

#include "client/graphics/eq/race_model_loader.h"
#include "client/graphics/eq/race_codes.h"
#include "client/graphics/eq/animation_mapping.h"
#include "client/graphics/eq/geometry_combiner.h"
#include "client/graphics/eq/s3d_loader.h"
#include "common/logging.h"
#include <algorithm>
#include <cstdio>
#include <iostream>

namespace EQT {
namespace Graphics {
std::shared_ptr<RaceModelData> RaceModelLoader::getVariantModelData(uint16_t raceId, uint8_t gender,
                                                                     uint8_t headVariant, uint8_t bodyVariant) {
    uint64_t key = makeVariantCacheKey(raceId, gender, headVariant, bodyVariant, 0);
    std::lock_guard<std::mutex> lock(variantModelsMutex_);
    auto it = variantModels_.find(key);
    if (it != variantModels_.end()) {
        return it->second;
    }
    return nullptr;
}

bool RaceModelLoader::preloadVariantModel(uint16_t raceId, uint8_t gender,
                                           uint8_t headVariant, uint8_t bodyVariant) {
    uint64_t variantKey = makeVariantCacheKey(raceId, gender, headVariant, bodyVariant, 0);

    // Check if already cached
    {
        std::lock_guard<std::mutex> lock(variantModelsMutex_);
        if (variantModels_.count(variantKey) > 0) return true;
    }

    // Build list of race codes to try (row code, then the row's fallback_code)
    std::vector<std::string> codesToTry;

    std::string baseRaceCode = getRaceCode(raceId);
    if (!baseRaceCode.empty()) {
        std::string raceCode = getGenderedRaceCode(baseRaceCode, gender);
        std::transform(raceCode.begin(), raceCode.end(), raceCode.begin(),
                       [](unsigned char c) { return std::toupper(c); });
        if (std::find(codesToTry.begin(), codesToTry.end(), raceCode) == codesToTry.end()) {
            codesToTry.push_back(raceCode);
        }
    }

    std::string fallbackCode = getFallbackRaceCode(raceId, gender);
    if (!fallbackCode.empty()) {
        std::transform(fallbackCode.begin(), fallbackCode.end(), fallbackCode.begin(),
                       [](unsigned char c) { return std::toupper(c); });
        if (std::find(codesToTry.begin(), codesToTry.end(), fallbackCode) == codesToTry.end()) {
            codesToTry.push_back(fallbackCode);
        }
    }

    // Compute animation source code
    std::string baseCode = codesToTry.empty() ? "" : codesToTry[0];
    std::string animationSourceCode = getAnimationSourceCode(baseCode);
    std::transform(animationSourceCode.begin(), animationSourceCode.end(), animationSourceCode.begin(),
                   [](unsigned char c) { return std::toupper(c); });

    if (codesToTry.empty()) {
        return false;
    }

    // Snapshot merged textures (thread-safe read of immutable cache)
    std::map<std::string, std::shared_ptr<TextureInfo>> mergedTextures;
    if (mergedTexturesCacheValid_) {
        mergedTextures = cachedMergedTextures_;
    } else {
        mergedTextures = globalTextures_;
        LOG_WARN(MOD_GRAPHICS, "RaceModelLoader::preloadVariantModel: merged texture cache not valid, using global textures only");
    }

    for (const std::string& codeToTry : codesToTry) {
        // Build expected mesh names
        std::string bodyMeshName;
        if (bodyVariant == 0) {
            bodyMeshName = codeToTry + "_DMSPRITEDEF";
        } else {
            char buf[32];
            snprintf(buf, sizeof(buf), "%s%02d_DMSPRITEDEF", codeToTry.c_str(), bodyVariant);
            bodyMeshName = buf;
        }

        char headBuf[32];
        snprintf(headBuf, sizeof(headBuf), "%sHE%02d_DMSPRITEDEF", codeToTry.c_str(), headVariant);
        std::string headMeshName = headBuf;

        // Build list of character sources to search
        std::vector<std::pair<const std::vector<std::shared_ptr<CharacterModel>>*, const std::map<std::string, std::shared_ptr<TextureInfo>>*>> characterSources;

        // Check JSON-specified s3d_file
        std::string jsonS3dFile = getRaceS3DFile(raceId);
        if (!jsonS3dFile.empty() && !clientPath_.empty()) {
            auto chrPos = jsonS3dFile.find("_chr.s3d");
            if (chrPos != std::string::npos) {
                std::string jsonZoneName = jsonS3dFile.substr(0, chrPos);
                if (jsonZoneName != currentZoneName_ && jsonZoneName.find("global") != 0) {
                    std::string lowerFilename = jsonS3dFile;
                    std::transform(lowerFilename.begin(), lowerFilename.end(), lowerFilename.begin(),
                                   [](unsigned char c) { return std::tolower(c); });

                    // Check/load otherChrCaches_ with mutex
                    {
                        std::lock_guard<std::mutex> lock(otherChrCacheMutex_);
                        auto cacheIt = otherChrCaches_.find(lowerFilename);
                        if (cacheIt == otherChrCaches_.end()) {
                            std::string fullPath = clientPath_ + jsonS3dFile;
                            S3DLoader loader;
                            if (loader.loadZone(fullPath)) {
                                auto zone = loader.getZone();
                                if (zone && !zone->characters.empty()) {
                                    // T91: was a raw otherChrCaches_[key] = ...
                                    // insert with no LRU registration.
                                    storeOtherChrCacheLocked(lowerFilename, zone->characters, zone->characterTextures);
                                    cacheIt = otherChrCaches_.find(lowerFilename);
                                    if (cacheIt != otherChrCaches_.end()) {
                                        // Add new textures to our local merged copy
                                        for (const auto& [name, tex] : cacheIt->second.textures) {
                                            if (mergedTextures.find(name) == mergedTextures.end()) {
                                                mergedTextures[name] = tex;
                                            }
                                        }
                                        LOG_INFO(MOD_GRAPHICS, "RaceModelLoader::preloadVariantModel: Loaded {} characters from {}",
                                                  cacheIt->second.characters.size(), jsonS3dFile);
                                    }
                                }
                            }
                        }

                        if (cacheIt != otherChrCaches_.end() && !cacheIt->second.characters.empty()) {
                            characterSources.push_back({&cacheIt->second.characters, &cacheIt->second.textures});
                        }
                    }
                }
            }
        }

        // Add zone characters (immutable after zone load — safe to read)
        if (zoneModelsLoaded_ && !zoneCharacters_.empty()) {
            characterSources.push_back({&zoneCharacters_, &zoneTextures_});
        }

        // Helper lambda to search a character list for variant parts
        auto searchCharacters = [&](const std::vector<std::shared_ptr<CharacterModel>>& characters,
                                    const std::map<std::string, std::shared_ptr<TextureInfo>>& textures)
            -> std::shared_ptr<RaceModelData> {

            for (const auto& character : characters) {
                if (!character || character->parts.empty()) continue;

                std::string charName = character->name;
                std::transform(charName.begin(), charName.end(), charName.begin(),
                               [](unsigned char c) { return std::toupper(c); });

                if (charName.find(codeToTry) == std::string::npos) continue;

                // Prepare fallback mesh names
                std::string headMeshFallback;
                std::string bodyMeshFallback;
                if (headVariant > 0) {
                    char fallbackBuf[32];
                    snprintf(fallbackBuf, sizeof(fallbackBuf), "%sHE00_DMSPRITEDEF", codeToTry.c_str());
                    headMeshFallback = fallbackBuf;
                }
                if (bodyVariant > 0) {
                    bodyMeshFallback = codeToTry + "_DMSPRITEDEF";
                }

                // Select SKINNED parts
                std::vector<CharacterPart> selectedSkinnedParts;
                bool foundRequestedHead = false;
                bool foundRequestedBody = false;
                for (const auto& part : character->partsWithTransforms) {
                    if (!part.geometry) continue;
                    std::string partName = part.geometry->name;
                    std::transform(partName.begin(), partName.end(), partName.begin(),
                                   [](unsigned char c) { return std::toupper(c); });
                    if (partName == bodyMeshName) { selectedSkinnedParts.push_back(part); foundRequestedBody = true; }
                    else if (partName == headMeshName) { selectedSkinnedParts.push_back(part); foundRequestedHead = true; }
                }
                if (!foundRequestedHead && !headMeshFallback.empty()) {
                    for (const auto& part : character->partsWithTransforms) {
                        if (!part.geometry) continue;
                        std::string partName = part.geometry->name;
                        std::transform(partName.begin(), partName.end(), partName.begin(),
                                       [](unsigned char c) { return std::toupper(c); });
                        if (partName == headMeshFallback) { selectedSkinnedParts.push_back(part); break; }
                    }
                }
                if (!foundRequestedBody && !bodyMeshFallback.empty()) {
                    for (const auto& part : character->partsWithTransforms) {
                        if (!part.geometry) continue;
                        std::string partName = part.geometry->name;
                        std::transform(partName.begin(), partName.end(), partName.begin(),
                                       [](unsigned char c) { return std::toupper(c); });
                        if (partName == bodyMeshFallback) { selectedSkinnedParts.push_back(part); break; }
                    }
                }

                // Select RAW parts
                std::vector<CharacterPart> selectedRawParts;
                foundRequestedHead = false;
                foundRequestedBody = false;
                for (const auto& part : character->rawParts) {
                    if (!part.geometry) continue;
                    std::string partName = part.geometry->name;
                    std::transform(partName.begin(), partName.end(), partName.begin(),
                                   [](unsigned char c) { return std::toupper(c); });
                    if (partName == bodyMeshName) { selectedRawParts.push_back(part); foundRequestedBody = true; }
                    else if (partName == headMeshName) { selectedRawParts.push_back(part); foundRequestedHead = true; }
                }
                if (!foundRequestedHead && !headMeshFallback.empty()) {
                    for (const auto& part : character->rawParts) {
                        if (!part.geometry) continue;
                        std::string partName = part.geometry->name;
                        std::transform(partName.begin(), partName.end(), partName.begin(),
                                       [](unsigned char c) { return std::toupper(c); });
                        if (partName == headMeshFallback) { selectedRawParts.push_back(part); break; }
                    }
                }
                if (!foundRequestedBody && !bodyMeshFallback.empty()) {
                    for (const auto& part : character->rawParts) {
                        if (!part.geometry) continue;
                        std::string partName = part.geometry->name;
                        std::transform(partName.begin(), partName.end(), partName.begin(),
                                       [](unsigned char c) { return std::toupper(c); });
                        if (partName == bodyMeshFallback) { selectedRawParts.push_back(part); break; }
                    }
                }

                if (selectedSkinnedParts.empty() || selectedRawParts.empty()) continue;

                auto combinedGeom = combineCharacterPartsWithTransforms(selectedSkinnedParts);
                auto rawGeom = combineCharacterPartsRaw(selectedRawParts);
                if (!combinedGeom || !rawGeom) continue;

                auto modelData = std::make_shared<RaceModelData>();
                modelData->combinedGeometry = combinedGeom;
                modelData->rawGeometry = rawGeom;
                modelData->textures = mergedTextures;
                modelData->raceName = character->name;
                modelData->raceId = raceId;
                modelData->gender = gender;
                modelData->scale = getRaceScale(raceId);

                // Copy animation data and merge from source
                if (character->animatedSkeleton) {
                    modelData->skeleton = character->animatedSkeleton;
                    size_t existingAnimCount = character->animatedSkeleton->animations.size();

                    if (!animationSourceCode.empty() && codeToTry != animationSourceCode) {
                        std::string animSourceS3dFile = getAnimationSourceS3DFile(raceId);
                        bool foundSource = false;

                        // Try zone-specific animation source S3D
                        if (!animSourceS3dFile.empty() && !clientPath_.empty()) {
                            std::string lowerFilename = animSourceS3dFile;
                            std::transform(lowerFilename.begin(), lowerFilename.end(), lowerFilename.begin(),
                                           [](unsigned char c) { return std::tolower(c); });

                            std::lock_guard<std::mutex> lock(otherChrCacheMutex_);
                            auto cacheIt = otherChrCaches_.find(lowerFilename);
                            if (cacheIt == otherChrCaches_.end()) {
                                std::string fullPath = clientPath_ + animSourceS3dFile;
                                S3DLoader loader;
                                if (loader.loadZone(fullPath)) {
                                    auto zone = loader.getZone();
                                    if (zone && !zone->characters.empty()) {
                                        // T91: was a raw otherChrCaches_[key] = ...
                                        // insert with no LRU registration.
                                        storeOtherChrCacheLocked(lowerFilename, zone->characters, zone->characterTextures);
                                        cacheIt = otherChrCaches_.find(lowerFilename);
                                    }
                                }
                            }

                            if (cacheIt != otherChrCaches_.end()) {
                                for (const auto& sourceChar : cacheIt->second.characters) {
                                    if (!sourceChar) continue;
                                    std::string sourceName = sourceChar->name;
                                    std::transform(sourceName.begin(), sourceName.end(), sourceName.begin(),
                                                   [](unsigned char c) { return std::toupper(c); });
                                    if (sourceName.find(animationSourceCode) != std::string::npos &&
                                        sourceChar->animatedSkeleton && !sourceChar->animatedSkeleton->animations.empty()) {

                                        auto& ourSkel = character->animatedSkeleton;
                                        auto& sourceSkel = sourceChar->animatedSkeleton;
                                        std::string lowerCode = codeToTry;
                                        std::transform(lowerCode.begin(), lowerCode.end(), lowerCode.begin(),
                                                       [](unsigned char c) { return std::tolower(c); });
                                        std::string lowerSource = animationSourceCode;
                                        std::transform(lowerSource.begin(), lowerSource.end(), lowerSource.begin(),
                                                       [](unsigned char c) { return std::tolower(c); });

                                        int addedAnimations = 0;
                                        for (const auto& [animCode, sourceAnim] : sourceSkel->animations) {
                                            if (ourSkel->animations.find(animCode) == ourSkel->animations.end()) {
                                                ourSkel->animations[animCode] = sourceAnim;
                                                addedAnimations++;
                                            }
                                        }
                                        for (size_t i = 0; i < ourSkel->bones.size(); ++i) {
                                            std::string mappedName = ourSkel->bones[i].name;
                                            size_t pos = mappedName.find(lowerCode);
                                            if (pos != std::string::npos) {
                                                mappedName.replace(pos, lowerCode.length(), lowerSource);
                                            }
                                            int sourceIdx = sourceSkel->getBoneIndex(mappedName);
                                            if (sourceIdx >= 0 && sourceIdx < static_cast<int>(sourceSkel->bones.size())) {
                                                for (const auto& [trackCode, trackDef] : sourceSkel->bones[sourceIdx].animationTracks) {
                                                    if (ourSkel->bones[i].animationTracks.find(trackCode) == ourSkel->bones[i].animationTracks.end()) {
                                                        ourSkel->bones[i].animationTracks[trackCode] = trackDef;
                                                    }
                                                }
                                            }
                                        }
                                        LOG_DEBUG(MOD_GRAPHICS, "RaceModelLoader::preloadVariantModel: Merged {} animations from {} ({}) to {}",
                                                  addedAnimations, animationSourceCode, animSourceS3dFile, codeToTry);
                                        foundSource = true;
                                        break;
                                    }
                                }
                            }
                        }

                        // Fall back to global characters
                        if (!foundSource && !globalCharacters_.empty()) {
                            for (const auto& sourceChar : globalCharacters_) {
                                if (!sourceChar) continue;
                                std::string sourceName = sourceChar->name;
                                std::transform(sourceName.begin(), sourceName.end(), sourceName.begin(),
                                               [](unsigned char c) { return std::toupper(c); });
                                if (sourceName.find(animationSourceCode) == std::string::npos) continue;
                                if (!sourceChar->animatedSkeleton || sourceChar->animatedSkeleton->animations.empty()) continue;

                                auto& ourSkel = character->animatedSkeleton;
                                auto& sourceSkel = sourceChar->animatedSkeleton;
                                std::string lowerCode = codeToTry;
                                std::transform(lowerCode.begin(), lowerCode.end(), lowerCode.begin(),
                                               [](unsigned char c) { return std::tolower(c); });
                                std::string lowerSource = animationSourceCode;
                                std::transform(lowerSource.begin(), lowerSource.end(), lowerSource.begin(),
                                               [](unsigned char c) { return std::tolower(c); });

                                int addedAnimations = 0;
                                for (const auto& [animCode, sourceAnim] : sourceSkel->animations) {
                                    if (ourSkel->animations.find(animCode) == ourSkel->animations.end()) {
                                        ourSkel->animations[animCode] = sourceAnim;
                                        addedAnimations++;
                                    }
                                }
                                for (size_t i = 0; i < ourSkel->bones.size(); ++i) {
                                    std::string mappedName = ourSkel->bones[i].name;
                                    size_t pos = mappedName.find(lowerCode);
                                    if (pos != std::string::npos) {
                                        mappedName.replace(pos, lowerCode.length(), lowerSource);
                                    }
                                    int sourceIdx = sourceSkel->getBoneIndex(mappedName);
                                    if (sourceIdx >= 0 && sourceIdx < static_cast<int>(sourceSkel->bones.size())) {
                                        for (const auto& [trackCode, trackDef] : sourceSkel->bones[sourceIdx].animationTracks) {
                                            if (ourSkel->bones[i].animationTracks.find(trackCode) == ourSkel->bones[i].animationTracks.end()) {
                                                ourSkel->bones[i].animationTracks[trackCode] = trackDef;
                                            }
                                        }
                                    }
                                }
                                LOG_DEBUG(MOD_GRAPHICS, "RaceModelLoader::preloadVariantModel: Merged {} animations from {} (global) to {}",
                                          addedAnimations, animationSourceCode, codeToTry);
                                foundSource = true;
                                break;
                            }
                        }

                        if (!foundSource) {
                            LOG_WARN(MOD_GRAPHICS, "RaceModelLoader::preloadVariantModel: Animation source {} not found for race {}",
                                     animationSourceCode, raceId);
                        }
                    }
                }

                if (!rawGeom->vertexPieces.empty()) {
                    modelData->vertexPieces = rawGeom->vertexPieces;
                }

                // Store with mutex
                {
                    std::lock_guard<std::mutex> lock(variantModelsMutex_);
                    variantModels_[variantKey] = modelData;
                }

                LOG_INFO(MOD_GRAPHICS, "RaceModelLoader::preloadVariantModel: race={} ({}) variant (head={}, body={}) loaded ({} verts, {} anims)",
                          raceId, codeToTry, (int)headVariant, (int)bodyVariant,
                          combinedGeom->vertices.size(),
                          modelData->skeleton ? modelData->skeleton->animations.size() : 0);
                return modelData;
            }

            return nullptr;
        };

        // Search JSON-specified and zone character sources first
        for (const auto& [characters, textures] : characterSources) {
            auto result = searchCharacters(*characters, *textures);
            if (result) return true;
        }

        // Then try global characters
        if (!globalCharacters_.empty()) {
            auto result = searchCharacters(globalCharacters_, mergedTextures);
            if (result) return true;
        }
    }

    LOG_DEBUG(MOD_GRAPHICS, "RaceModelLoader::preloadVariantModel: no variant found for race={} head={} body={}",
              raceId, (int)headVariant, (int)bodyVariant);
    return false;
}

} // namespace Graphics
} // namespace EQT
