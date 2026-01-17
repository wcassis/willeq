// RaceModelLoader variant loading methods - split from race_model_loader.cpp
// These methods remain part of the RaceModelLoader class

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

irr::scene::IMesh* RaceModelLoader::getMeshForRaceWithAppearance(uint16_t raceId, uint8_t gender,
                                                                  uint8_t headVariant, uint8_t bodyVariant) {
    // For now, only old models support variant selection
    if (!useOldModels_) {
        // New models don't have the same variant system - just use default
        return getMeshForRace(raceId, gender);
    }

    uint64_t key = makeVariantCacheKey(raceId, gender, headVariant, bodyVariant);

    // Check variant mesh cache first
    auto meshIt = variantMeshCache_.find(key);
    if (meshIt != variantMeshCache_.end()) {
        return meshIt->second;
    }

    // Check if variant model data is loaded
    auto modelIt = variantModels_.find(key);
    if (modelIt == variantModels_.end()) {
        // Try to load the model with specific variants
        if (loadModelFromGlobalChrWithVariants(raceId, gender, headVariant, bodyVariant)) {
            modelIt = variantModels_.find(key);
        }

        if (modelIt == variantModels_.end()) {
            // Fall back to default model
            variantMeshCache_[key] = getMeshForRace(raceId, gender);
            return variantMeshCache_[key];
        }
    }

    // Build mesh from model data
    auto modelData = modelIt->second;
    if (!modelData || !modelData->combinedGeometry) {
        variantMeshCache_[key] = nullptr;
        return nullptr;
    }

    irr::scene::IMesh* mesh = nullptr;
    if (!modelData->textures.empty() && !modelData->combinedGeometry->textureNames.empty()) {
        mesh = meshBuilder_->buildTexturedMesh(*modelData->combinedGeometry, modelData->textures, true);  // flipV for character models
    } else {
        mesh = meshBuilder_->buildColoredMesh(*modelData->combinedGeometry);
    }

    variantMeshCache_[key] = mesh;
    return mesh;
}

bool RaceModelLoader::loadModelFromGlobalChrWithVariants(uint16_t raceId, uint8_t gender,
                                                          uint8_t headVariant, uint8_t bodyVariant) {
    if (!globalModelsLoaded_) {
        if (!loadGlobalModels()) {
            return false;
        }
    }

    // Build list of race codes to try (primary first, then fallback)
    std::vector<std::string> codesToTry;

    std::string baseRaceCode = getRaceCode(raceId);
    if (!baseRaceCode.empty()) {
        // Get gender-specific code (e.g., HUM -> HUF for female)
        std::string raceCode = getGenderedRaceCode(baseRaceCode, gender);
        std::transform(raceCode.begin(), raceCode.end(), raceCode.begin(),
                       [](unsigned char c) { return std::toupper(c); });
        codesToTry.push_back(raceCode);
    }

    // Add fallback code for citizen races (e.g., QCM -> HUM)
    std::string fallbackCode = getFallbackRaceCode(raceId, gender);
    if (!fallbackCode.empty()) {
        std::transform(fallbackCode.begin(), fallbackCode.end(), fallbackCode.begin(),
                       [](unsigned char c) { return std::toupper(c); });
        if (std::find(codesToTry.begin(), codesToTry.end(), fallbackCode) == codesToTry.end()) {
            codesToTry.push_back(fallbackCode);
        }
    }

    if (codesToTry.empty()) {
        return false;
    }

    // Try each race code in order
    for (const std::string& codeToTry : codesToTry) {
        // Build expected mesh names for body and head
        // Body: "{RACE}_DMSPRITEDEF" for variant 0, "{RACE}01_DMSPRITEDEF" for variant 1, etc.
        // Head: "{RACE}HE00_DMSPRITEDEF" for variant 0, "{RACE}HE01_DMSPRITEDEF" for variant 1, etc.
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

        // Search for a character model that matches this race code
        for (const auto& character : globalCharacters_) {
            if (!character || character->parts.empty()) {
                continue;
            }

            std::string charName = character->name;
            std::transform(charName.begin(), charName.end(), charName.begin(),
                           [](unsigned char c) { return std::toupper(c); });

            // Look for race code in character name
            if (charName.find(codeToTry) != std::string::npos) {
                // Found a matching character - now find specific body and head parts
                std::vector<CharacterPart> selectedParts;

                for (const auto& part : character->partsWithTransforms) {
                    if (!part.geometry) continue;

                    std::string partName = part.geometry->name;
                    std::transform(partName.begin(), partName.end(), partName.begin(),
                                   [](unsigned char c) { return std::toupper(c); });

                    // Check if this is the body or head we want
                    if (partName == bodyMeshName || partName == headMeshName) {
                        selectedParts.push_back(part);
                    }
                }

                // If we didn't find variant parts, continue to next code to try
                if (selectedParts.empty()) {
                    LOG_DEBUG(MOD_GRAPHICS, "RaceModelLoader: Could not find variant meshes '{}' or '{}' for {}", bodyMeshName, headMeshName, codeToTry);
                    continue;
                }

                // For body variant > 0 (robes), we MUST find the body mesh.
                // If we only found the head, that's not a valid robe model - return false
                // so the fallback logic can use the default body with robe textures.
                if (bodyVariant > 0) {
                    bool foundBody = false;
                    for (const auto& part : selectedParts) {
                        if (part.geometry) {
                            std::string partName = part.geometry->name;
                            std::transform(partName.begin(), partName.end(), partName.begin(),
                                           [](unsigned char c) { return std::toupper(c); });
                            if (partName == bodyMeshName) {
                                foundBody = true;
                                break;
                            }
                        }
                    }
                    if (!foundBody) {
                        LOG_DEBUG(MOD_GRAPHICS, "RaceModelLoader: Robe body mesh '{}' not found for {}, will fall back to default body",
                                  bodyMeshName, codeToTry);
                        continue;  // Skip to next race code or fall back entirely
                    }
                }

                auto combinedGeom = combineCharacterPartsWithTransforms(selectedParts);
                if (!combinedGeom) {
                    continue;
                }

                auto modelData = std::make_shared<RaceModelData>();
                modelData->combinedGeometry = combinedGeom;
                // Use merged textures from all sources (global + numbered globals + zone)
                modelData->textures = getMergedTextures();
                modelData->raceName = character->name;
                modelData->raceId = raceId;
                modelData->gender = gender;
                modelData->scale = getRaceScale(raceId);

                uint64_t key = makeVariantCacheKey(raceId, gender, headVariant, bodyVariant);
                variantModels_[key] = modelData;

                LOG_DEBUG(MOD_GRAPHICS, "RaceModelLoader: Loaded race {} ({}) with variants (head={}, body={}) from global_chr.s3d ({} parts, {} vertices)",
                          raceId, codeToTry, (int)headVariant, (int)bodyVariant, selectedParts.size(), combinedGeom->vertices.size());
                return true;
            }
        }
    }

    return false;
}

EQAnimatedMesh* RaceModelLoader::getAnimatedMeshWithAppearance(uint16_t raceId, uint8_t gender,
                                                                uint8_t headVariant, uint8_t bodyVariant,
                                                                uint8_t textureVariant) {
    // For non-default variants, use variant cache (includes texture variant now)
    if (headVariant == 0 && bodyVariant == 0 && textureVariant == 0) {
        return getAnimatedMeshForRace(raceId, gender);
    }

    // Include texture variant in cache key
    uint64_t key = makeVariantCacheKey(raceId, gender, headVariant, bodyVariant, textureVariant);

    // Check variant animated mesh cache first
    auto cacheIt = variantAnimatedMeshCache_.find(key);
    if (cacheIt != variantAnimatedMeshCache_.end()) {
        return cacheIt->second;
    }

    // For model loading, we need the base variant key (without texture variant)
    // because model geometry is the same regardless of texture
    uint64_t modelKey = makeVariantCacheKey(raceId, gender, headVariant, bodyVariant, 0);

    // Try to load the model with specific variants for animation
    auto modelIt = variantModels_.find(modelKey);
    if (modelIt == variantModels_.end()) {
        if (!loadModelFromGlobalChrWithVariantsForAnimation(raceId, gender, headVariant, bodyVariant)) {
            // Fall back to default
            variantAnimatedMeshCache_[key] = nullptr;
            return nullptr;
        }
        modelIt = variantModels_.find(modelKey);
    }

    if (modelIt == variantModels_.end()) {
        variantAnimatedMeshCache_[key] = nullptr;
        return nullptr;
    }

    auto modelData = modelIt->second;
    if (!modelData) {
        variantAnimatedMeshCache_[key] = nullptr;
        return nullptr;
    }

    // Check if we have animation data AND raw geometry
    if (!modelData->skeleton || modelData->skeleton->animations.empty()) {
        LOG_DEBUG(MOD_GRAPHICS, "RaceModelLoader: No animation data for race {} variant (head={}, body={}) - falling back to default",
                  raceId, (int)headVariant, (int)bodyVariant);
        variantAnimatedMeshCache_[key] = nullptr;
        return nullptr;
    }

    if (!modelData->rawGeometry) {
        LOG_DEBUG(MOD_GRAPHICS, "RaceModelLoader: No raw geometry for race {} variant - falling back to default", raceId);
        variantAnimatedMeshCache_[key] = nullptr;
        return nullptr;
    }

    // Build mesh from RAW (unskinned) geometry for animation
    // Pass texture variant and race code for equipment texture override
    irr::scene::IMesh* rawMesh = buildMeshFromGeometry(modelData->rawGeometry, modelData->textures,
                                                        textureVariant, modelData->raceName);
    if (!rawMesh) {
        LOG_DEBUG(MOD_GRAPHICS, "RaceModelLoader: Failed to build raw mesh for race {} variant", raceId);
        variantAnimatedMeshCache_[key] = nullptr;
        return nullptr;
    }

    // Create animated mesh using the RAW (unskinned) mesh
    EQAnimatedMesh* animMesh = new EQAnimatedMesh();
    animMesh->setBaseMesh(rawMesh);
    animMesh->setSkeleton(modelData->skeleton);
    animMesh->setVertexPieces(modelData->vertexPieces);

    // Set vertex mapping data for multi-buffer animation support
    animMesh->setOriginalVertices(originalVerticesForAnimation_);
    animMesh->setVertexMapping(vertexMappingForAnimation_);

    // Apply initial pose
    animMesh->applyAnimation();

    variantAnimatedMeshCache_[key] = animMesh;

    LOG_DEBUG(MOD_GRAPHICS, "RaceModelLoader: Created variant animated mesh for race {} (head={}, body={}, texture={}) with {} animations",
              raceId, (int)headVariant, (int)bodyVariant, (int)textureVariant, modelData->skeleton->animations.size());

    return animMesh;
}

bool RaceModelLoader::loadModelFromGlobalChrWithVariantsForAnimation(uint16_t raceId, uint8_t gender,
                                                                      uint8_t headVariant, uint8_t bodyVariant) {
    // NOTE: We defer loading global models until we confirm zone models don't have what we need.
    // This prevents the expensive loading of ALL race skeletons when only a zone-specific race is needed.

    // Build list of race codes to try for mesh/textures (zone-specific first, then base, then fallback)
    std::vector<std::string> codesToTry;

    // 1. Try zone-specific code first (e.g., QCM for Qeynos Citizen in qeynos zones)
    if (zoneModelsLoaded_ && !currentZoneName_.empty()) {
        std::string zoneCode = getZoneSpecificRaceCode(raceId, gender, currentZoneName_);
        if (!zoneCode.empty()) {
            std::transform(zoneCode.begin(), zoneCode.end(), zoneCode.begin(),
                           [](unsigned char c) { return std::toupper(c); });
            codesToTry.push_back(zoneCode);
        }
    }

    // 2. Then try base race code with gender
    std::string baseRaceCode = getRaceCode(raceId);
    if (!baseRaceCode.empty()) {
        // Get gender-specific code (e.g., HUM -> HUF for female)
        std::string raceCode = getGenderedRaceCode(baseRaceCode, gender);
        std::transform(raceCode.begin(), raceCode.end(), raceCode.begin(),
                       [](unsigned char c) { return std::toupper(c); });
        if (std::find(codesToTry.begin(), codesToTry.end(), raceCode) == codesToTry.end()) {
            codesToTry.push_back(raceCode);
        }
    }

    // 3. Add fallback code for citizen races (e.g., QCM -> HUM)
    std::string fallbackCode = getFallbackRaceCode(raceId, gender);
    if (!fallbackCode.empty()) {
        std::transform(fallbackCode.begin(), fallbackCode.end(), fallbackCode.begin(),
                       [](unsigned char c) { return std::toupper(c); });
        if (std::find(codesToTry.begin(), codesToTry.end(), fallbackCode) == codesToTry.end()) {
            codesToTry.push_back(fallbackCode);
        }
    }

    // Compute animation source code (based on EQSage's approach - ELM/ELF have 40+ animations)
    std::string baseCode = codesToTry.empty() ? "" : codesToTry[0];
    std::string animationSourceCode = getAnimationSourceCode(baseCode);
    std::transform(animationSourceCode.begin(), animationSourceCode.end(), animationSourceCode.begin(),
                   [](unsigned char c) { return std::toupper(c); });

    if (codesToTry.empty()) {
        return false;
    }

    // Try each race code in order
    for (const std::string& codeToTry : codesToTry) {
        // Build expected mesh names for body and head
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

        // Build list of character sources to search (JSON-specified first, then zone, then global)
        std::vector<std::pair<const std::vector<std::shared_ptr<CharacterModel>>*, const std::map<std::string, std::shared_ptr<TextureInfo>>*>> characterSources;
        bool needGlobalFallback = true;

        // First, check if this race has a JSON-specified s3d_file
        std::string jsonS3dFile = getRaceS3DFile(raceId);
        if (!jsonS3dFile.empty() && !clientPath_.empty()) {
            // Extract zone name from s3d filename (e.g., "freporte_chr.s3d" -> "freporte")
            auto chrPos = jsonS3dFile.find("_chr.s3d");
            if (chrPos != std::string::npos) {
                std::string jsonZoneName = jsonS3dFile.substr(0, chrPos);
                // Check if this is different from the current zone
                if (jsonZoneName != currentZoneName_ && jsonZoneName.find("global") != 0) {
                    // Try to load from this zone's chr file
                    std::string lowerFilename = jsonS3dFile;
                    std::transform(lowerFilename.begin(), lowerFilename.end(), lowerFilename.begin(),
                                   [](unsigned char c) { return std::tolower(c); });

                    // Check if already in cache
                    auto cacheIt = otherChrCaches_.find(lowerFilename);
                    if (cacheIt == otherChrCaches_.end()) {
                        // Load the chr file
                        std::string fullPath = clientPath_ + jsonS3dFile;
                        S3DLoader loader;
                        if (loader.loadZone(fullPath)) {
                            auto zone = loader.getZone();
                            if (zone && !zone->characters.empty()) {
                                OtherChrCache& cache = otherChrCaches_[lowerFilename];
                                cache.characters = zone->characters;
                                cache.textures = zone->characterTextures;
                                // Invalidate merged textures cache since we added new textures
                                mergedTexturesCacheValid_ = false;
                                LOG_DEBUG(MOD_GRAPHICS, "RaceModelLoader: Loaded {} characters from JSON-specified {} for race {}",
                                          cache.characters.size(), jsonS3dFile, raceId);
                                cacheIt = otherChrCaches_.find(lowerFilename);
                            }
                        }
                    }

                    // Add JSON-specified chr file to search sources
                    if (cacheIt != otherChrCaches_.end() && !cacheIt->second.characters.empty()) {
                        characterSources.push_back({&cacheIt->second.characters, &cacheIt->second.textures});
                        LOG_DEBUG(MOD_GRAPHICS, "RaceModelLoader: Searching JSON-specified {} ({} models) for {}",
                                  jsonS3dFile, cacheIt->second.characters.size(), codeToTry);
                    }
                }
            }
        }

        // Then add current zone characters
        if (zoneModelsLoaded_ && !zoneCharacters_.empty()) {
            characterSources.push_back({&zoneCharacters_, &zoneTextures_});
            LOG_DEBUG(MOD_GRAPHICS, "RaceModelLoader: Searching zone characters ({} models) for {}", zoneCharacters_.size(), codeToTry);
        } else {
            LOG_DEBUG(MOD_GRAPHICS, "RaceModelLoader: No zone characters loaded (zoneModelsLoaded_={})", zoneModelsLoaded_);
        }
        // Note: We'll add globalCharacters_ later only if zone search fails
        // Try JSON-specified, then zone, then global if needed
        bool foundInZone = false;

        // Search for a character model that matches this race code
        for (const auto& [characters, textures] : characterSources) {
        for (const auto& character : *characters) {
            if (!character || character->parts.empty()) {
                continue;
            }

            std::string charName = character->name;
            std::transform(charName.begin(), charName.end(), charName.begin(),
                           [](unsigned char c) { return std::toupper(c); });

            // Look for race code in character name
            if (charName.find(codeToTry) != std::string::npos) {
                // Found a matching character - now find specific body and head parts

                // If head variant > 0, also prepare fallback to head variant 0
                std::string headMeshFallback;
                if (headVariant > 0) {
                    char fallbackBuf[32];
                    snprintf(fallbackBuf, sizeof(fallbackBuf), "%sHE00_DMSPRITEDEF", codeToTry.c_str());
                    headMeshFallback = fallbackBuf;
                }

                // Select SKINNED parts (partsWithTransforms)
                std::vector<CharacterPart> selectedSkinnedParts;
                bool foundRequestedHead = false;
                for (const auto& part : character->partsWithTransforms) {
                    if (!part.geometry) continue;

                    std::string partName = part.geometry->name;
                    std::transform(partName.begin(), partName.end(), partName.begin(),
                                   [](unsigned char c) { return std::toupper(c); });

                    if (partName == bodyMeshName || partName == headMeshName) {
                        selectedSkinnedParts.push_back(part);
                        if (partName == headMeshName) foundRequestedHead = true;
                    }
                }

                // If requested head variant wasn't found, try fallback to head variant 0
                if (!foundRequestedHead && !headMeshFallback.empty()) {
                    for (const auto& part : character->partsWithTransforms) {
                        if (!part.geometry) continue;
                        std::string partName = part.geometry->name;
                        std::transform(partName.begin(), partName.end(), partName.begin(),
                                       [](unsigned char c) { return std::toupper(c); });
                        if (partName == headMeshFallback) {
                            LOG_DEBUG(MOD_GRAPHICS, "RaceModelLoader: Head variant {} not found, using fallback head variant 0 ({})", (int)headVariant, headMeshFallback);
                            selectedSkinnedParts.push_back(part);
                            break;
                        }
                    }
                }

                // Select RAW parts (rawParts) for animation
                std::vector<CharacterPart> selectedRawParts;
                foundRequestedHead = false;
                for (const auto& part : character->rawParts) {
                    if (!part.geometry) continue;

                    std::string partName = part.geometry->name;
                    std::transform(partName.begin(), partName.end(), partName.begin(),
                                   [](unsigned char c) { return std::toupper(c); });

                    if (partName == bodyMeshName || partName == headMeshName) {
                        selectedRawParts.push_back(part);
                        if (partName == headMeshName) foundRequestedHead = true;
                    }
                }

                // If requested head variant wasn't found in raw parts, try fallback to head variant 0
                if (!foundRequestedHead && !headMeshFallback.empty()) {
                    for (const auto& part : character->rawParts) {
                        if (!part.geometry) continue;
                        std::string partName = part.geometry->name;
                        std::transform(partName.begin(), partName.end(), partName.begin(),
                                       [](unsigned char c) { return std::toupper(c); });
                        if (partName == headMeshFallback) {
                            selectedRawParts.push_back(part);
                            break;
                        }
                    }
                }

                // If we didn't find variant parts, continue to next code to try
                if (selectedSkinnedParts.empty() || selectedRawParts.empty()) {
                    LOG_DEBUG(MOD_GRAPHICS, "RaceModelLoader: Could not find variant meshes '{}' or '{}' for animation in {}", bodyMeshName, headMeshName, codeToTry);
                    continue;
                }

                auto combinedGeom = combineCharacterPartsWithTransforms(selectedSkinnedParts);
                auto rawGeom = combineCharacterPartsRaw(selectedRawParts);

                if (!combinedGeom || !rawGeom) {
                    continue;
                }

                auto modelData = std::make_shared<RaceModelData>();
                modelData->combinedGeometry = combinedGeom;
                modelData->rawGeometry = rawGeom;
                // Use merged textures from all sources (global + numbered globals + zone)
                modelData->textures = getMergedTextures();
                modelData->raceName = character->name;
                modelData->raceId = raceId;
                modelData->gender = gender;
                modelData->scale = getRaceScale(raceId);

                // Copy animation data from the character's skeleton and merge missing animations from source
                // Key behavior from EQSage: model-specific animations are NEVER overwritten - only missing animations are added
                if (character->animatedSkeleton) {
                    modelData->skeleton = character->animatedSkeleton;
                    size_t existingAnimCount = character->animatedSkeleton->animations.size();

                    LOG_DEBUG(MOD_GRAPHICS, "RaceModelLoader: Using skeleton from {} with {} existing animations, animSource={}",
                              codeToTry, existingAnimCount, animationSourceCode);

                    // Merge missing animations from animation source (ELM/ELF/etc)
                    if (!animationSourceCode.empty() && codeToTry != animationSourceCode) {
                        LOG_DEBUG(MOD_GRAPHICS, "RaceModelLoader: Attempting animation merge from {} to {}", animationSourceCode, codeToTry);

                        // Check if animation source is in a zone-specific S3D file
                        std::string animSourceS3dFile = getAnimationSourceS3DFile(raceId);
                        bool foundSource = false;

                        // First, try to load from zone-specific animation source S3D if configured
                        if (!animSourceS3dFile.empty() && !clientPath_.empty()) {
                            std::string lowerFilename = animSourceS3dFile;
                            std::transform(lowerFilename.begin(), lowerFilename.end(), lowerFilename.begin(),
                                           [](unsigned char c) { return std::tolower(c); });

                            // Check if already in cache
                            auto cacheIt = otherChrCaches_.find(lowerFilename);
                            if (cacheIt == otherChrCaches_.end()) {
                                // Load the animation source S3D file
                                std::string fullPath = clientPath_ + animSourceS3dFile;
                                S3DLoader loader;
                                if (loader.loadZone(fullPath)) {
                                    auto zone = loader.getZone();
                                    if (zone && !zone->characters.empty()) {
                                        OtherChrCache& cache = otherChrCaches_[lowerFilename];
                                        cache.characters = zone->characters;
                                        cache.textures = zone->characterTextures;
                                        mergedTexturesCacheValid_ = false;
                                        LOG_DEBUG(MOD_GRAPHICS, "RaceModelLoader: Loaded animation source S3D {} ({} characters)",
                                                  animSourceS3dFile, cache.characters.size());
                                        cacheIt = otherChrCaches_.find(lowerFilename);
                                    }
                                }
                            }

                            // Search for animation source in the loaded cache
                            if (cacheIt != otherChrCaches_.end()) {
                                for (const auto& sourceChar : cacheIt->second.characters) {
                                    if (!sourceChar) continue;
                                    std::string sourceName = sourceChar->name;
                                    std::transform(sourceName.begin(), sourceName.end(), sourceName.begin(),
                                                   [](unsigned char c) { return std::toupper(c); });
                                    if (sourceName.find(animationSourceCode) != std::string::npos) {
                                        if (sourceChar->animatedSkeleton && !sourceChar->animatedSkeleton->animations.empty()) {
                                            auto& ourSkel = character->animatedSkeleton;
                                            auto& sourceSkel = sourceChar->animatedSkeleton;

                                            std::string lowerCode = codeToTry;
                                            std::transform(lowerCode.begin(), lowerCode.end(), lowerCode.begin(),
                                                           [](unsigned char c) { return std::tolower(c); });
                                            std::string lowerSource = animationSourceCode;
                                            std::transform(lowerSource.begin(), lowerSource.end(), lowerSource.begin(),
                                                           [](unsigned char c) { return std::tolower(c); });

                                            // Add missing animations (never overwrite existing)
                                            int addedAnimations = 0;
                                            for (const auto& [animCode, sourceAnim] : sourceSkel->animations) {
                                                if (ourSkel->animations.find(animCode) == ourSkel->animations.end()) {
                                                    ourSkel->animations[animCode] = sourceAnim;
                                                    addedAnimations++;
                                                }
                                            }

                                            // Merge animation tracks for each bone
                                            int mappedBones = 0;
                                            for (size_t i = 0; i < ourSkel->bones.size(); ++i) {
                                                std::string ourBoneName = ourSkel->bones[i].name;
                                                std::string mappedName = ourBoneName;
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
                                                    mappedBones++;
                                                }
                                            }

                                            LOG_DEBUG(MOD_GRAPHICS, "RaceModelLoader: Merged animations from {} ({}) to {} - added {} animations (had {}, now {}), mapped {}/{} bones",
                                                      animationSourceCode, animSourceS3dFile, codeToTry, addedAnimations, existingAnimCount, ourSkel->animations.size(), mappedBones, ourSkel->bones.size());
                                            foundSource = true;
                                            break;
                                        }
                                    }
                                }
                            }

                            if (!foundSource) {
                                LOG_WARN(MOD_GRAPHICS, "RaceModelLoader: Animation source {} not found in {} for race {}",
                                         animationSourceCode, animSourceS3dFile, raceId);
                            }
                        }

                        // Fall back to global characters if not found in zone-specific S3D
                        if (!foundSource) {
                            // Ensure global models are loaded so we can get animations from the source
                            if (!globalModelsLoaded_) {
                                LOG_DEBUG(MOD_GRAPHICS, "RaceModelLoader: Loading global models for animation source {}", animationSourceCode);
                                loadGlobalModels();
                            }
                            // Search for animation source skeleton in global characters
                            for (const auto& sourceChar : globalCharacters_) {
                                if (!sourceChar) continue;
                                std::string sourceName = sourceChar->name;
                                std::transform(sourceName.begin(), sourceName.end(), sourceName.begin(),
                                               [](unsigned char c) { return std::toupper(c); });
                                if (sourceName.find(animationSourceCode) != std::string::npos) {
                                    if (sourceChar->animatedSkeleton && !sourceChar->animatedSkeleton->animations.empty()) {
                                        // Merge animations from source to our skeleton
                                        // e.g., QCM bones (qcmpe, qcmch, etc.) <- ELM animations (elmpe, elmch, etc.)
                                        // Model-specific animations are NEVER overwritten
                                        auto& ourSkel = character->animatedSkeleton;
                                        auto& sourceSkel = sourceChar->animatedSkeleton;

                                        std::string lowerCode = codeToTry;
                                        std::transform(lowerCode.begin(), lowerCode.end(), lowerCode.begin(),
                                                       [](unsigned char c) { return std::tolower(c); });
                                        std::string lowerSource = animationSourceCode;
                                        std::transform(lowerSource.begin(), lowerSource.end(), lowerSource.begin(),
                                                       [](unsigned char c) { return std::tolower(c); });

                                        // Add missing animations (never overwrite existing)
                                        int addedAnimations = 0;
                                        for (const auto& [animCode, sourceAnim] : sourceSkel->animations) {
                                            if (ourSkel->animations.find(animCode) == ourSkel->animations.end()) {
                                                ourSkel->animations[animCode] = sourceAnim;
                                                addedAnimations++;
                                            }
                                        }

                                        // Merge animation tracks for each bone (only add missing track entries)
                                        int mappedBones = 0;
                                        for (size_t i = 0; i < ourSkel->bones.size(); ++i) {
                                            std::string ourBoneName = ourSkel->bones[i].name;
                                            std::string mappedName = ourBoneName;
                                            size_t pos = mappedName.find(lowerCode);
                                            if (pos != std::string::npos) {
                                                mappedName.replace(pos, lowerCode.length(), lowerSource);
                                            }

                                            int sourceIdx = sourceSkel->getBoneIndex(mappedName);
                                            if (sourceIdx >= 0 && sourceIdx < static_cast<int>(sourceSkel->bones.size())) {
                                                // Merge animation tracks - only add missing ones
                                                for (const auto& [trackCode, trackDef] : sourceSkel->bones[sourceIdx].animationTracks) {
                                                    if (ourSkel->bones[i].animationTracks.find(trackCode) == ourSkel->bones[i].animationTracks.end()) {
                                                        ourSkel->bones[i].animationTracks[trackCode] = trackDef;
                                                    }
                                                }
                                                mappedBones++;
                                            }
                                        }

                                        LOG_DEBUG(MOD_GRAPHICS, "RaceModelLoader: Merged animations from {} to {} - added {} animations (had {}, now {}), mapped {}/{} bones",
                                                  animationSourceCode, codeToTry, addedAnimations, existingAnimCount, ourSkel->animations.size(), mappedBones, ourSkel->bones.size());
                                        foundSource = true;
                                        break;
                                    }
                                }
                            }
                        }

                        if (!foundSource) {
                            LOG_WARN(MOD_GRAPHICS, "RaceModelLoader: Animation source {} not found for race {} (code {})",
                                     animationSourceCode, raceId, codeToTry);
                        }
                    } else if (animationSourceCode.empty()) {
                        LOG_DEBUG(MOD_GRAPHICS, "RaceModelLoader: No animation source for {} - keeping {} animations",
                                  codeToTry, character->animatedSkeleton->animations.size());
                    } else {
                        LOG_DEBUG(MOD_GRAPHICS, "RaceModelLoader: Animation source {} same as {} - skipping merge",
                                  animationSourceCode, codeToTry);
                    }

                    // Log final animation count after merging
                    LOG_DEBUG(MOD_GRAPHICS, "RaceModelLoader: Variant {} final animation count: {}",
                              codeToTry, modelData->skeleton->animations.size());
                }

                if (!rawGeom->vertexPieces.empty()) {
                    modelData->vertexPieces = rawGeom->vertexPieces;
                }

                uint64_t key = makeVariantCacheKey(raceId, gender, headVariant, bodyVariant);
                variantModels_[key] = modelData;

                LOG_DEBUG(MOD_GRAPHICS, "RaceModelLoader: Loaded race {} ({}) variant (head={}, body={}) for animation ({} parts, {} vertices)",
                          raceId, codeToTry, (int)headVariant, (int)bodyVariant, selectedSkinnedParts.size(), combinedGeom->vertices.size());
                return true;
            }
        }
        }

        // Zone search didn't find it - try global models
        // Load global models if not already loaded
        if (!globalModelsLoaded_) {
            LOG_DEBUG(MOD_GRAPHICS, "RaceModelLoader: Zone search failed for {}, loading global models...", codeToTry);
            if (!loadGlobalModels()) {
                continue;  // Failed to load globals, try next code
            }
        }

        // Search global characters for the variant
        LOG_DEBUG(MOD_GRAPHICS, "RaceModelLoader: Searching global characters ({} models) for {} variant (body={}, head={})",
                  globalCharacters_.size(), codeToTry, bodyMeshName, headMeshName);

        for (const auto& character : globalCharacters_) {
            if (!character || character->parts.empty()) {
                continue;
            }

            std::string charName = character->name;
            std::transform(charName.begin(), charName.end(), charName.begin(),
                           [](unsigned char c) { return std::toupper(c); });

            if (charName.find(codeToTry) != std::string::npos) {
                // If head variant > 0, also prepare fallback to head variant 0
                std::string headMeshFallback;
                if (headVariant > 0) {
                    char fallbackBuf[32];
                    snprintf(fallbackBuf, sizeof(fallbackBuf), "%sHE00_DMSPRITEDEF", codeToTry.c_str());
                    headMeshFallback = fallbackBuf;
                }

                std::vector<CharacterPart> selectedSkinnedParts;
                bool foundRequestedHead = false;
                for (const auto& part : character->partsWithTransforms) {
                    if (!part.geometry) continue;
                    std::string partName = part.geometry->name;
                    std::transform(partName.begin(), partName.end(), partName.begin(),
                                   [](unsigned char c) { return std::toupper(c); });
                    if (partName == bodyMeshName || partName == headMeshName) {
                        selectedSkinnedParts.push_back(part);
                        if (partName == headMeshName) foundRequestedHead = true;
                    }
                }

                // If requested head variant wasn't found, try fallback to head variant 0
                if (!foundRequestedHead && !headMeshFallback.empty()) {
                    for (const auto& part : character->partsWithTransforms) {
                        if (!part.geometry) continue;
                        std::string partName = part.geometry->name;
                        std::transform(partName.begin(), partName.end(), partName.begin(),
                                       [](unsigned char c) { return std::toupper(c); });
                        if (partName == headMeshFallback) {
                            LOG_DEBUG(MOD_GRAPHICS, "RaceModelLoader: Head variant {} not found, using fallback head variant 0 ({})", (int)headVariant, headMeshFallback);
                            selectedSkinnedParts.push_back(part);
                            break;
                        }
                    }
                }

                std::vector<CharacterPart> selectedRawParts;
                foundRequestedHead = false;
                for (const auto& part : character->rawParts) {
                    if (!part.geometry) continue;
                    std::string partName = part.geometry->name;
                    std::transform(partName.begin(), partName.end(), partName.begin(),
                                   [](unsigned char c) { return std::toupper(c); });
                    if (partName == bodyMeshName || partName == headMeshName) {
                        selectedRawParts.push_back(part);
                        if (partName == headMeshName) foundRequestedHead = true;
                    }
                }

                // If requested head variant wasn't found in raw parts, try fallback to head variant 0
                if (!foundRequestedHead && !headMeshFallback.empty()) {
                    for (const auto& part : character->rawParts) {
                        if (!part.geometry) continue;
                        std::string partName = part.geometry->name;
                        std::transform(partName.begin(), partName.end(), partName.begin(),
                                       [](unsigned char c) { return std::toupper(c); });
                        if (partName == headMeshFallback) {
                            selectedRawParts.push_back(part);
                            break;
                        }
                    }
                }

                if (selectedSkinnedParts.empty() || selectedRawParts.empty()) {
                    LOG_DEBUG(MOD_GRAPHICS, "RaceModelLoader: Found {} but missing variant parts (skinned={}, raw={})",
                              charName, selectedSkinnedParts.size(), selectedRawParts.size());
                    continue;
                }

                auto combinedGeom = combineCharacterPartsWithTransforms(selectedSkinnedParts);
                auto rawGeom = combineCharacterPartsRaw(selectedRawParts);
                if (!combinedGeom || !rawGeom) {
                    continue;
                }

                auto modelData = std::make_shared<RaceModelData>();
                modelData->combinedGeometry = combinedGeom;
                modelData->rawGeometry = rawGeom;
                modelData->textures = getMergedTextures();
                modelData->raceName = character->name;
                modelData->raceId = raceId;
                modelData->gender = gender;
                modelData->scale = getRaceScale(raceId);

                if (character->animatedSkeleton) {
                    modelData->skeleton = character->animatedSkeleton;
                    size_t existingAnimCount = character->animatedSkeleton->animations.size();

                    LOG_DEBUG(MOD_GRAPHICS, "RaceModelLoader: Global fallback - skeleton from {} with {} animations, animSource={}",
                              codeToTry, existingAnimCount, animationSourceCode);

                    // Merge missing animations from animation source (same as main path)
                    if (!animationSourceCode.empty() && codeToTry != animationSourceCode) {
                        LOG_DEBUG(MOD_GRAPHICS, "RaceModelLoader: Global fallback - merging animations from {} to {}", animationSourceCode, codeToTry);

                        // Check if animation source is in a zone-specific S3D file
                        std::string animSourceS3dFile = getAnimationSourceS3DFile(raceId);
                        bool foundSource = false;

                        // First, try to load from zone-specific animation source S3D if configured
                        if (!animSourceS3dFile.empty() && !clientPath_.empty()) {
                            std::string lowerFilename = animSourceS3dFile;
                            std::transform(lowerFilename.begin(), lowerFilename.end(), lowerFilename.begin(),
                                           [](unsigned char c) { return std::tolower(c); });

                            // Check if already in cache
                            auto cacheIt = otherChrCaches_.find(lowerFilename);
                            if (cacheIt == otherChrCaches_.end()) {
                                // Load the animation source S3D file
                                std::string fullPath = clientPath_ + animSourceS3dFile;
                                S3DLoader loader;
                                if (loader.loadZone(fullPath)) {
                                    auto zone = loader.getZone();
                                    if (zone && !zone->characters.empty()) {
                                        OtherChrCache& cache = otherChrCaches_[lowerFilename];
                                        cache.characters = zone->characters;
                                        cache.textures = zone->characterTextures;
                                        mergedTexturesCacheValid_ = false;
                                        LOG_DEBUG(MOD_GRAPHICS, "RaceModelLoader: Global fallback - loaded animation source S3D {} ({} characters)",
                                                  animSourceS3dFile, cache.characters.size());
                                        cacheIt = otherChrCaches_.find(lowerFilename);
                                    }
                                }
                            }

                            // Search for animation source in the loaded cache
                            if (cacheIt != otherChrCaches_.end()) {
                                for (const auto& sourceChar : cacheIt->second.characters) {
                                    if (!sourceChar) continue;
                                    std::string sourceName = sourceChar->name;
                                    std::transform(sourceName.begin(), sourceName.end(), sourceName.begin(),
                                                   [](unsigned char c) { return std::toupper(c); });
                                    if (sourceName.find(animationSourceCode) != std::string::npos) {
                                        if (sourceChar->animatedSkeleton && !sourceChar->animatedSkeleton->animations.empty()) {
                                            auto& ourSkel = character->animatedSkeleton;
                                            auto& sourceSkel = sourceChar->animatedSkeleton;

                                            std::string lowerCode = codeToTry;
                                            std::transform(lowerCode.begin(), lowerCode.end(), lowerCode.begin(),
                                                           [](unsigned char c) { return std::tolower(c); });
                                            std::string lowerSource = animationSourceCode;
                                            std::transform(lowerSource.begin(), lowerSource.end(), lowerSource.begin(),
                                                           [](unsigned char c) { return std::tolower(c); });

                                            // Add missing animations (never overwrite existing)
                                            int addedAnimations = 0;
                                            for (const auto& [animCode, sourceAnim] : sourceSkel->animations) {
                                                if (ourSkel->animations.find(animCode) == ourSkel->animations.end()) {
                                                    ourSkel->animations[animCode] = sourceAnim;
                                                    addedAnimations++;
                                                }
                                            }

                                            // Merge animation tracks for each bone
                                            int mappedBones = 0;
                                            for (size_t i = 0; i < ourSkel->bones.size(); ++i) {
                                                std::string ourBoneName = ourSkel->bones[i].name;
                                                std::string mappedName = ourBoneName;
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
                                                    mappedBones++;
                                                }
                                            }

                                            LOG_DEBUG(MOD_GRAPHICS, "RaceModelLoader: Global fallback - merged from {} ({}) - added {} animations (had {}, now {}), mapped {}/{} bones",
                                                      animationSourceCode, animSourceS3dFile, addedAnimations, existingAnimCount, ourSkel->animations.size(), mappedBones, ourSkel->bones.size());
                                            foundSource = true;
                                            break;
                                        }
                                    }
                                }
                            }
                        }

                        // Fall back to global characters if not found in zone-specific S3D
                        if (!foundSource) {
                            // Search for animation source skeleton in global characters
                            for (const auto& sourceChar : globalCharacters_) {
                                if (!sourceChar) continue;
                                std::string sourceName = sourceChar->name;
                                std::transform(sourceName.begin(), sourceName.end(), sourceName.begin(),
                                               [](unsigned char c) { return std::toupper(c); });
                                if (sourceName.find(animationSourceCode) != std::string::npos) {
                                    if (sourceChar->animatedSkeleton && !sourceChar->animatedSkeleton->animations.empty()) {
                                        auto& ourSkel = character->animatedSkeleton;
                                        auto& sourceSkel = sourceChar->animatedSkeleton;

                                        std::string lowerCode = codeToTry;
                                        std::transform(lowerCode.begin(), lowerCode.end(), lowerCode.begin(),
                                                       [](unsigned char c) { return std::tolower(c); });
                                        std::string lowerSource = animationSourceCode;
                                        std::transform(lowerSource.begin(), lowerSource.end(), lowerSource.begin(),
                                                       [](unsigned char c) { return std::tolower(c); });

                                        // Add missing animations (never overwrite existing)
                                        int addedAnimations = 0;
                                        for (const auto& [animCode, sourceAnim] : sourceSkel->animations) {
                                            if (ourSkel->animations.find(animCode) == ourSkel->animations.end()) {
                                                ourSkel->animations[animCode] = sourceAnim;
                                                addedAnimations++;
                                            }
                                        }

                                        // Merge animation tracks for each bone
                                        int mappedBones = 0;
                                        for (size_t i = 0; i < ourSkel->bones.size(); ++i) {
                                            std::string ourBoneName = ourSkel->bones[i].name;
                                            std::string mappedName = ourBoneName;
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
                                                mappedBones++;
                                            }
                                        }

                                        LOG_DEBUG(MOD_GRAPHICS, "RaceModelLoader: Global fallback - merged {} animations (had {}, now {}), mapped {}/{} bones",
                                                  addedAnimations, existingAnimCount, ourSkel->animations.size(), mappedBones, ourSkel->bones.size());
                                        foundSource = true;
                                        break;
                                    }
                                }
                            }
                        }

                        if (!foundSource) {
                            LOG_WARN(MOD_GRAPHICS, "RaceModelLoader: Global fallback - animation source {} not found for race {}",
                                     animationSourceCode, raceId);
                        }
                    }

                    LOG_DEBUG(MOD_GRAPHICS, "RaceModelLoader: Global fallback - {} final animation count: {}",
                              codeToTry, modelData->skeleton->animations.size());
                }

                if (!rawGeom->vertexPieces.empty()) {
                    modelData->vertexPieces = rawGeom->vertexPieces;
                }

                uint64_t key = makeVariantCacheKey(raceId, gender, headVariant, bodyVariant);
                variantModels_[key] = modelData;

                LOG_DEBUG(MOD_GRAPHICS, "RaceModelLoader: Loaded race {} ({}) variant from global ({} parts)",
                          raceId, codeToTry, selectedSkinnedParts.size());
                return true;
            }
        }
    }

    return false;
}

} // namespace Graphics
} // namespace EQT
