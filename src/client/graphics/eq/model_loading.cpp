// RaceModelLoader model loading methods - split from race_model_loader.cpp
// These methods remain part of the RaceModelLoader class

#include "client/graphics/eq/race_model_loader.h"
#include "client/graphics/eq/race_codes.h"
#include "client/graphics/eq/geometry_combiner.h"
#include "client/graphics/eq/pfs.h"
#include "common/logging.h"
#include <algorithm>
#include <filesystem>
#include <iostream>
#include <regex>

namespace EQT {
namespace Graphics {

bool RaceModelLoader::loadGlobalModels() {
    if (clientPath_.empty()) {
        LOG_ERROR(MOD_GRAPHICS, "RaceModelLoader: No client path set");
        return false;
    }

    if (globalModelsLoaded_) {
        return true;
    }

    std::string globalChrPath = clientPath_ + "global_chr.s3d";

    S3DLoader loader;
    if (!loader.loadZone(globalChrPath)) {
        LOG_ERROR(MOD_GRAPHICS, "RaceModelLoader: Failed to load {}: {}", globalChrPath, loader.getError());
        return false;
    }

    auto zone = loader.getZone();
    if (!zone) {
        return false;
    }

    globalCharacters_ = zone->characters;
    globalTextures_ = zone->characterTextures;
    globalModelsLoaded_ = true;

    LOG_DEBUG(MOD_GRAPHICS, "RaceModelLoader: Loaded {} character models from global_chr.s3d", globalCharacters_.size());

    return !globalCharacters_.empty();
}

bool RaceModelLoader::loadNumberedGlobalModels() {
    if (clientPath_.empty()) {
        LOG_ERROR(MOD_GRAPHICS, "RaceModelLoader: No client path set");
        return false;
    }

    if (numberedGlobalsLoaded_) {
        return true;
    }

    // Load global2_chr.s3d through global7_chr.s3d
    int loadedCount = 0;
    for (int num = 2; num <= 7; ++num) {
        std::string filename = clientPath_ + "global" + std::to_string(num) + "_chr.s3d";

        S3DLoader loader;
        if (!loader.loadZone(filename)) {
            // Not all numbered global files may exist
            continue;
        }

        auto zone = loader.getZone();
        if (!zone || zone->characters.empty()) {
            continue;
        }

        numberedGlobalCharacters_[num] = zone->characters;
        numberedGlobalTextures_[num] = zone->characterTextures;

        LOG_DEBUG(MOD_GRAPHICS, "RaceModelLoader: Loaded {} character models from global{}_chr.s3d", zone->characters.size(), num);

        loadedCount++;
    }

    numberedGlobalsLoaded_ = true;

    return loadedCount > 0;
}

bool RaceModelLoader::loadArmorTextures() {
    if (clientPath_.empty()) {
        LOG_ERROR(MOD_GRAPHICS, "RaceModelLoader: No client path set");
        return false;
    }

    if (armorTexturesLoaded_) {
        return true;
    }

    // Load global17_amr.s3d through global23_amr.s3d
    // These contain armor textures for equipment variants 17-23
    int loadedCount = 0;
    for (int num = 17; num <= 23; ++num) {
        std::string filename = clientPath_ + "global" + std::to_string(num) + "_amr.s3d";

        PfsArchive archive;
        if (!archive.open(filename)) {
            // Not all armor archives may exist
            continue;
        }

        // Load BMP textures
        std::vector<std::string> texFiles;
        archive.getFilenames(".bmp", texFiles);

        int texCount = 0;
        for (const auto& texName : texFiles) {
            std::vector<char> data;
            if (archive.get(texName, data)) {
                auto tex = std::make_shared<TextureInfo>();
                tex->name = texName;
                tex->data.assign(data.begin(), data.end());

                // Store with lowercase name for consistent lookup
                std::string lowerName = texName;
                std::transform(lowerName.begin(), lowerName.end(), lowerName.begin(),
                              [](unsigned char c) { return std::tolower(c); });
                armorTextures_[lowerName] = tex;
                texCount++;
            }
        }

        // Also load DDS textures if present
        std::vector<std::string> ddsFiles;
        if (archive.getFilenames(".dds", ddsFiles)) {
            for (const auto& texName : ddsFiles) {
                std::vector<char> data;
                if (archive.get(texName, data)) {
                    auto tex = std::make_shared<TextureInfo>();
                    tex->name = texName;
                    tex->data.assign(data.begin(), data.end());

                    std::string lowerName = texName;
                    std::transform(lowerName.begin(), lowerName.end(), lowerName.begin(),
                                  [](unsigned char c) { return std::tolower(c); });
                    armorTextures_[lowerName] = tex;
                    texCount++;
                }
            }
        }

        LOG_INFO(MOD_GRAPHICS, "RaceModelLoader: Loaded {} armor textures from global{}_amr.s3d",
                 texCount, num);
        loadedCount++;
    }

    armorTexturesLoaded_ = true;

    LOG_INFO(MOD_GRAPHICS, "RaceModelLoader: Total armor textures loaded: {}", armorTextures_.size());

    return loadedCount > 0;
}

bool RaceModelLoader::loadZoneModels(const std::string& zoneName) {
    if (clientPath_.empty()) {
        LOG_ERROR(MOD_GRAPHICS, "RaceModelLoader: No client path set");
        return false;
    }

    // Don't reload if same zone
    if (zoneModelsLoaded_ && currentZoneName_ == zoneName) {
        return true;
    }

    // Clear previous zone models AND cached models (so they get re-loaded with new zone textures)
    // This ensures zone-specific textures (like armor textures) are properly merged
    zoneCharacters_.clear();
    zoneTextures_.clear();
    zoneModelsLoaded_ = false;

    // Clear model caches - models will be re-loaded on demand with new zone textures
    loadedModels_.clear();
    meshCache_.clear();
    variantModels_.clear();
    variantMeshCache_.clear();
    animatedMeshCache_.clear();
    variantAnimatedMeshCache_.clear();
    otherChrCaches_.clear();  // Also clear cached _chr.s3d files from other zones

    std::string zoneFilename = clientPath_ + zoneName + "_chr.s3d";

    S3DLoader loader;
    if (!loader.loadZone(zoneFilename)) {
        // Zone-specific chr file may not exist for all zones
        return false;
    }

    auto zone = loader.getZone();
    if (!zone || zone->characters.empty()) {
        return false;
    }

    zoneCharacters_ = zone->characters;
    zoneTextures_ = zone->characterTextures;
    currentZoneName_ = zoneName;
    zoneModelsLoaded_ = true;

    return true;
}

bool RaceModelLoader::loadModelFromS3D(const std::string& s3dPath, uint16_t raceId, uint8_t gender) {
    S3DLoader loader;
    if (!loader.loadZone(s3dPath)) {
        return false;
    }

    auto zone = loader.getZone();
    if (!zone || zone->characters.empty()) {
        return false;
    }

    // Use the first character model found
    for (const auto& character : zone->characters) {
        if (!character || character->parts.empty()) {
            continue;
        }

        // Build skinned and raw geometry
        std::shared_ptr<ZoneGeometry> combinedGeom;
        std::shared_ptr<ZoneGeometry> rawGeom;

        if (!character->partsWithTransforms.empty()) {
            LOG_DEBUG(MOD_GRAPHICS, "RaceModelLoader: Using {} parts with bone transforms for {}",
                character->partsWithTransforms.size(), character->name);
            combinedGeom = combineCharacterPartsWithTransforms(character->partsWithTransforms);
            rawGeom = combineCharacterPartsRaw(character->rawParts);
        } else {
            combinedGeom = combineCharacterParts(character->parts);
        }

        if (!combinedGeom) {
            continue;
        }

        auto modelData = std::make_shared<RaceModelData>();
        modelData->combinedGeometry = combinedGeom;
        modelData->rawGeometry = rawGeom;  // Store raw geometry for animation
        // Use merged textures from all sources (global + numbered globals + zone)
        // Start with S3D file textures, then merge with getMergedTextures for additional textures
        modelData->textures = zone->characterTextures;
        auto merged = getMergedTextures();
        for (const auto& [name, tex] : merged) {
            if (modelData->textures.find(name) == modelData->textures.end()) {
                modelData->textures[name] = tex;
            }
        }
        modelData->raceName = character->name;
        modelData->raceId = raceId;
        modelData->gender = gender;
        modelData->scale = getRaceScale(raceId);

        // Copy animation data if available
        if (character->animatedSkeleton) {
            modelData->skeleton = character->animatedSkeleton;
        }
        // Use vertex pieces from raw geometry (not skinned)
        if (rawGeom && !rawGeom->vertexPieces.empty()) {
            modelData->vertexPieces = rawGeom->vertexPieces;
        }

        uint32_t key = makeCacheKey(raceId, gender);
        loadedModels_[key] = modelData;

        LOG_DEBUG(MOD_GRAPHICS, "RaceModelLoader: Loaded race {} from {} ({} vertices, {} animations, {} vertex pieces)",
            raceId, s3dPath, combinedGeom->vertices.size(),
            modelData->skeleton ? modelData->skeleton->animations.size() : 0,
            rawGeom ? rawGeom->vertexPieces.size() : 0);
        return true;
    }

    return false;
}

bool RaceModelLoader::loadModelFromGlobalChr(uint16_t raceId, uint8_t gender) {
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
        // Search for a character model that matches this race code
        for (const auto& character : globalCharacters_) {
            if (!character || character->parts.empty()) {
                continue;
            }

            // Character names in global_chr.s3d typically contain the race code
            std::string charName = character->name;
            std::transform(charName.begin(), charName.end(), charName.begin(),
                           [](unsigned char c) { return std::toupper(c); });

            // Look for race code in character name
            if (charName.find(codeToTry) != std::string::npos) {
                // Found a match - select only the DEFAULT body and head parts
                // Default body: "{RACE}_DMSPRITEDEF" (no number suffix)
                // Default head: "{RACE}HE00_DMSPRITEDEF" (head variant 0)
                std::string defaultBodyName = codeToTry + "_DMSPRITEDEF";
                std::string defaultHeadName = codeToTry + "HE00_DMSPRITEDEF";

                // Select skinned parts (from partsWithTransforms - already has skinning applied)
                std::vector<CharacterPart> selectedSkinnedParts;
                for (const auto& part : character->partsWithTransforms) {
                    if (!part.geometry) continue;

                    std::string partName = part.geometry->name;
                    std::transform(partName.begin(), partName.end(), partName.begin(),
                                   [](unsigned char c) { return std::toupper(c); });

                    // Only include default body and head
                    if (partName == defaultBodyName || partName == defaultHeadName) {
                        selectedSkinnedParts.push_back(part);
                    }
                }

                // Select raw parts (from rawParts - unskinned for animation)
                std::vector<CharacterPart> selectedRawParts;
                for (const auto& part : character->rawParts) {
                    if (!part.geometry) continue;

                    std::string partName = part.geometry->name;
                    std::transform(partName.begin(), partName.end(), partName.begin(),
                                   [](unsigned char c) { return std::toupper(c); });

                    // Only include default body and head
                    if (partName == defaultBodyName || partName == defaultHeadName) {
                        selectedRawParts.push_back(part);
                    }
                }

                if (selectedSkinnedParts.empty()) {
                    // Fall back to using all parts if we couldn't find defaults
                    LOG_DEBUG(MOD_GRAPHICS, "RaceModelLoader: Could not find default meshes for {}, using all {} parts",
                        codeToTry, character->partsWithTransforms.size());
                    selectedSkinnedParts = character->partsWithTransforms;
                    selectedRawParts = character->rawParts;
                }

                std::shared_ptr<ZoneGeometry> combinedGeom = combineCharacterPartsWithTransforms(selectedSkinnedParts);
                std::shared_ptr<ZoneGeometry> rawGeom = combineCharacterPartsRaw(selectedRawParts);

                if (!combinedGeom) {
                    continue;
                }

                auto modelData = std::make_shared<RaceModelData>();
                modelData->combinedGeometry = combinedGeom;
                modelData->rawGeometry = rawGeom;  // Store raw geometry for animation
                // Use merged textures from all sources (global + numbered globals + zone)
                modelData->textures = getMergedTextures();
                modelData->raceName = character->name;
                modelData->raceId = raceId;
                modelData->gender = gender;
                modelData->scale = getRaceScale(raceId);

                // Copy animation data if available
                if (character->animatedSkeleton) {
                    modelData->skeleton = character->animatedSkeleton;
                }
                // Use vertex pieces from raw geometry (not skinned)
                if (rawGeom && !rawGeom->vertexPieces.empty()) {
                    modelData->vertexPieces = rawGeom->vertexPieces;
                }

                uint32_t key = makeCacheKey(raceId, gender);
                loadedModels_[key] = modelData;

                LOG_DEBUG(MOD_GRAPHICS, "RaceModelLoader: Loaded race {} ({}) from global_chr.s3d ({}, {} parts, {} vertices, {} animations, {} vertex pieces)",
                    raceId, codeToTry, character->name, selectedSkinnedParts.size(), combinedGeom->vertices.size(),
                    modelData->skeleton ? modelData->skeleton->animations.size() : 0,
                    rawGeom ? rawGeom->vertexPieces.size() : 0);
                return true;
            }
        }
    }

    return false;
}

bool RaceModelLoader::loadModelFromNumberedGlobal(int globalNum, uint16_t raceId, uint8_t gender) {
    // Check if this numbered global is loaded
    auto charIt = numberedGlobalCharacters_.find(globalNum);
    if (charIt == numberedGlobalCharacters_.end()) {
        return false;
    }

    auto texIt = numberedGlobalTextures_.find(globalNum);
    const auto& characters = charIt->second;
    const auto& textures = (texIt != numberedGlobalTextures_.end()) ? texIt->second :
        std::map<std::string, std::shared_ptr<TextureInfo>>();

    std::string baseRaceCode = getRaceCode(raceId);
    if (baseRaceCode.empty()) {
        return false;
    }

    // Get gender-specific code (e.g., HUM -> HUF for female)
    std::string raceCode = getGenderedRaceCode(baseRaceCode, gender);

    // Transform to uppercase for matching
    std::transform(raceCode.begin(), raceCode.end(), raceCode.begin(),
                   [](unsigned char c) { return std::toupper(c); });

    // Search for a character model that matches this race code
    for (const auto& character : characters) {
        if (!character || character->parts.empty()) {
            continue;
        }

        std::string charName = character->name;
        std::transform(charName.begin(), charName.end(), charName.begin(),
                       [](unsigned char c) { return std::toupper(c); });

        if (charName.find(raceCode) != std::string::npos) {
            // Found a match - select only the DEFAULT body and head parts
            std::string defaultBodyName = raceCode + "_DMSPRITEDEF";
            std::string defaultHeadName = raceCode + "HE00_DMSPRITEDEF";

            // Select skinned parts (from partsWithTransforms - already has skinning applied)
            std::vector<CharacterPart> selectedSkinnedParts;
            for (const auto& part : character->partsWithTransforms) {
                if (!part.geometry) continue;

                std::string partName = part.geometry->name;
                std::transform(partName.begin(), partName.end(), partName.begin(),
                               [](unsigned char c) { return std::toupper(c); });

                if (partName == defaultBodyName || partName == defaultHeadName) {
                    selectedSkinnedParts.push_back(part);
                }
            }

            // Select raw parts (from rawParts - unskinned for animation)
            std::vector<CharacterPart> selectedRawParts;
            for (const auto& part : character->rawParts) {
                if (!part.geometry) continue;

                std::string partName = part.geometry->name;
                std::transform(partName.begin(), partName.end(), partName.begin(),
                               [](unsigned char c) { return std::toupper(c); });

                if (partName == defaultBodyName || partName == defaultHeadName) {
                    selectedRawParts.push_back(part);
                }
            }

            if (selectedSkinnedParts.empty()) {
                // Fall back to using all parts if we couldn't find defaults
                LOG_DEBUG(MOD_GRAPHICS, "RaceModelLoader: Could not find default meshes for {} in global{}, using all {} parts",
                    raceCode, globalNum, character->partsWithTransforms.size());
                selectedSkinnedParts = character->partsWithTransforms;
                selectedRawParts = character->rawParts;
            }

            // If still empty (no partsWithTransforms), fall back to old behavior
            if (selectedSkinnedParts.empty()) {
                auto combinedGeom = combineCharacterParts(character->parts);
                if (!combinedGeom) {
                    continue;
                }

                auto modelData = std::make_shared<RaceModelData>();
                modelData->combinedGeometry = combinedGeom;
                modelData->textures = getMergedTextures();
                modelData->raceName = character->name;
                modelData->raceId = raceId;
                modelData->gender = gender;
                modelData->scale = getRaceScale(raceId);

                uint32_t key = makeCacheKey(raceId, gender);
                loadedModels_[key] = modelData;

                LOG_DEBUG(MOD_GRAPHICS, "RaceModelLoader: Loaded race {} from global{}_chr.s3d ({}, {} vertices, no animation data)",
                    raceId, globalNum, character->name, combinedGeom->vertices.size());
                return true;
            }

            std::shared_ptr<ZoneGeometry> combinedGeom = combineCharacterPartsWithTransforms(selectedSkinnedParts);
            std::shared_ptr<ZoneGeometry> rawGeom = combineCharacterPartsRaw(selectedRawParts);

            if (!combinedGeom) {
                continue;
            }

            auto modelData = std::make_shared<RaceModelData>();
            modelData->combinedGeometry = combinedGeom;
            modelData->rawGeometry = rawGeom;  // Store raw geometry for animation
            // Use merged textures from all sources (global + numbered globals + zone)
            modelData->textures = getMergedTextures();
            modelData->raceName = character->name;
            modelData->raceId = raceId;
            modelData->gender = gender;
            modelData->scale = getRaceScale(raceId);

            // Copy animation data if available
            if (character->animatedSkeleton) {
                modelData->skeleton = character->animatedSkeleton;
            }
            // Use vertex pieces from raw geometry (not skinned)
            if (rawGeom && !rawGeom->vertexPieces.empty()) {
                modelData->vertexPieces = rawGeom->vertexPieces;
            }

            uint32_t key = makeCacheKey(raceId, gender);
            loadedModels_[key] = modelData;

            LOG_DEBUG(MOD_GRAPHICS, "RaceModelLoader: Loaded race {} from global{}_chr.s3d ({}, {} parts, {} vertices, {} animations, {} vertex pieces)",
                raceId, globalNum, character->name, selectedSkinnedParts.size(), combinedGeom->vertices.size(),
                modelData->skeleton ? modelData->skeleton->animations.size() : 0,
                rawGeom ? rawGeom->vertexPieces.size() : 0);
            return true;
        }
    }

    return false;
}

bool RaceModelLoader::loadModelFromZoneChr(const std::string& zoneName, uint16_t raceId, uint8_t gender) {
    // Make sure zone models are loaded
    if (!zoneModelsLoaded_ || currentZoneName_ != zoneName) {
        if (!loadZoneModels(zoneName)) {
            return false;
        }
    }

    // Build list of race codes to try:
    // 1. Zone-specific code (e.g., QCM for Qeynos Citizen in qeynos zones)
    // 2. Generic race code with gender (e.g., HUM/HUF for Human)
    // 3. Fallback code for citizen races (e.g., HUM for Qeynos Citizen if QCM not found)
    std::vector<std::string> codesToTry;

    // Try zone-specific code first
    std::string zoneCode = getZoneSpecificRaceCode(raceId, gender, zoneName);
    if (!zoneCode.empty()) {
        codesToTry.push_back(zoneCode);
    }

    // Then try generic race code with gender
    std::string baseRaceCode = getRaceCode(raceId);
    if (!baseRaceCode.empty()) {
        std::string raceCode = getGenderedRaceCode(baseRaceCode, gender);
        if (std::find(codesToTry.begin(), codesToTry.end(), raceCode) == codesToTry.end()) {
            codesToTry.push_back(raceCode);
        }
    }

    // Finally try fallback code for citizen races
    std::string fallbackCode = getFallbackRaceCode(raceId, gender);
    if (!fallbackCode.empty() && std::find(codesToTry.begin(), codesToTry.end(), fallbackCode) == codesToTry.end()) {
        codesToTry.push_back(fallbackCode);
    }

    if (codesToTry.empty()) {
        return false;
    }

    // Search zone characters for matching race code
    for (const std::string& codeToTry : codesToTry) {
        std::string upperCode = codeToTry;
        std::transform(upperCode.begin(), upperCode.end(), upperCode.begin(),
                       [](unsigned char c) { return std::toupper(c); });

        for (const auto& character : zoneCharacters_) {
            if (!character || character->parts.empty()) {
                continue;
            }

            std::string charName = character->name;
            std::transform(charName.begin(), charName.end(), charName.begin(),
                           [](unsigned char c) { return std::toupper(c); });

            if (charName.find(upperCode) != std::string::npos) {
                // Found a match - select only the DEFAULT body and head parts
                // Default body: "{RACE}_DMSPRITEDEF" (no number suffix)
                // Default head: "{RACE}HE00_DMSPRITEDEF" (head variant 0)
                std::string defaultBodyName = upperCode + "_DMSPRITEDEF";
                std::string defaultHeadName = upperCode + "HE00_DMSPRITEDEF";

                // Select skinned parts (from partsWithTransforms - already has skinning applied)
                std::vector<CharacterPart> selectedSkinnedParts;
                for (const auto& part : character->partsWithTransforms) {
                    if (!part.geometry) continue;

                    std::string partName = part.geometry->name;
                    std::transform(partName.begin(), partName.end(), partName.begin(),
                                   [](unsigned char c) { return std::toupper(c); });

                    // Only include default body and head
                    if (partName == defaultBodyName || partName == defaultHeadName) {
                        selectedSkinnedParts.push_back(part);
                    }
                }

                // Select raw parts (from rawParts - unskinned for animation)
                std::vector<CharacterPart> selectedRawParts;
                for (const auto& part : character->rawParts) {
                    if (!part.geometry) continue;

                    std::string partName = part.geometry->name;
                    std::transform(partName.begin(), partName.end(), partName.begin(),
                                   [](unsigned char c) { return std::toupper(c); });

                    // Only include default body and head
                    if (partName == defaultBodyName || partName == defaultHeadName) {
                        selectedRawParts.push_back(part);
                    }
                }

                if (selectedSkinnedParts.empty()) {
                    // Fall back to using all parts if we couldn't find defaults
                    LOG_DEBUG(MOD_GRAPHICS, "RaceModelLoader: Could not find default meshes for {} in zone, using all {} parts",
                        upperCode, character->partsWithTransforms.size());
                    selectedSkinnedParts = character->partsWithTransforms;
                    selectedRawParts = character->rawParts;
                }

                // If still empty (no partsWithTransforms), fall back to old behavior
                if (selectedSkinnedParts.empty()) {
                    LOG_DEBUG(MOD_GRAPHICS, "RaceModelLoader: No partsWithTransforms for {} in zone, using parts directly", upperCode);
                    auto combinedGeom = combineCharacterParts(character->parts);
                    if (!combinedGeom) {
                        continue;
                    }

                    auto modelData = std::make_shared<RaceModelData>();
                    modelData->combinedGeometry = combinedGeom;
                    modelData->textures = getMergedTextures();
                    modelData->raceName = character->name;
                    modelData->raceId = raceId;
                    modelData->gender = gender;
                    modelData->scale = getRaceScale(raceId);

                    uint32_t key = makeCacheKey(raceId, gender);
                    loadedModels_[key] = modelData;

                    LOG_DEBUG(MOD_GRAPHICS, "RaceModelLoader: Loaded race {} (code {}) from {}_chr.s3d ({}, {} vertices, no animation data)",
                        raceId, upperCode, zoneName, character->name, combinedGeom->vertices.size());
                    return true;
                }

                std::shared_ptr<ZoneGeometry> combinedGeom = combineCharacterPartsWithTransforms(selectedSkinnedParts);
                std::shared_ptr<ZoneGeometry> rawGeom = combineCharacterPartsRaw(selectedRawParts);

                if (!combinedGeom) {
                    continue;
                }

                auto modelData = std::make_shared<RaceModelData>();
                modelData->combinedGeometry = combinedGeom;
                modelData->rawGeometry = rawGeom;  // Store raw geometry for animation
                // Use merged textures from all sources (global + numbered globals + zone)
                modelData->textures = getMergedTextures();
                modelData->raceName = character->name;
                modelData->raceId = raceId;
                modelData->gender = gender;
                modelData->scale = getRaceScale(raceId);

                // Copy animation data if available
                if (character->animatedSkeleton) {
                    modelData->skeleton = character->animatedSkeleton;
                }
                // Use vertex pieces from raw geometry (not skinned)
                if (rawGeom && !rawGeom->vertexPieces.empty()) {
                    modelData->vertexPieces = rawGeom->vertexPieces;
                }

                uint32_t key = makeCacheKey(raceId, gender);
                loadedModels_[key] = modelData;

                LOG_DEBUG(MOD_GRAPHICS, "RaceModelLoader: Loaded race {} (code {}) from {}_chr.s3d ({}, {} parts, {} vertices, {} animations, {} vertex pieces)",
                    raceId, upperCode, zoneName, character->name, selectedSkinnedParts.size(), combinedGeom->vertices.size(),
                    modelData->skeleton ? modelData->skeleton->animations.size() : 0,
                    rawGeom ? rawGeom->vertexPieces.size() : 0);
                return true;
            }
        }
    }

    return false;
}

bool RaceModelLoader::searchAllGlobalsForModel(uint16_t raceId, uint8_t gender) {
    // First try main global_chr.s3d
    if (loadModelFromGlobalChr(raceId, gender)) {
        return true;
    }

    // Load numbered globals if not already loaded
    if (!numberedGlobalsLoaded_) {
        loadNumberedGlobalModels();
    }

    // Try each numbered global
    for (int num = 2; num <= 7; ++num) {
        if (loadModelFromNumberedGlobal(num, raceId, gender)) {
            return true;
        }
    }

    return false;
}

bool RaceModelLoader::searchZoneChrFilesForModel(uint16_t raceId, uint8_t gender) {
    if (clientPath_.empty()) {
        return false;
    }

    std::string baseRaceCode = getRaceCode(raceId);
    if (baseRaceCode.empty()) {
        return false;
    }

    // Get gender-specific code (e.g., HUM -> HUF for female)
    std::string raceCode = getGenderedRaceCode(baseRaceCode, gender);

    // Transform race code to uppercase for matching
    std::string upperRaceCode = raceCode;
    std::transform(upperRaceCode.begin(), upperRaceCode.end(), upperRaceCode.begin(),
                   [](unsigned char c) { return std::toupper(c); });

    namespace fs = std::filesystem;

    // Pattern for race-specific global files to skip (e.g., globalelf_chr.s3d, globalhum_chr.s3d)
    // We want to skip: global[a-z]+_chr.s3d but NOT global_chr.s3d or global[0-9]_chr.s3d
    std::regex raceSpecificGlobalPattern("global[a-z]+_chr\\.s3d", std::regex::icase);

    // Helper lambda to search characters for a matching race and build model data
    auto searchAndBuildModel = [&](const std::vector<std::shared_ptr<CharacterModel>>& characters,
                                   const std::map<std::string, std::shared_ptr<TextureInfo>>& textures,
                                   const std::string& sourceFilename) -> bool {
        for (const auto& character : characters) {
            if (!character || character->parts.empty()) {
                continue;
            }

            std::string charName = character->name;
            std::transform(charName.begin(), charName.end(), charName.begin(),
                           [](unsigned char c) { return std::toupper(c); });

            if (charName.find(upperRaceCode) != std::string::npos) {
                // Found a match - select only the DEFAULT body and head parts
                std::string defaultBodyName = upperRaceCode + "_DMSPRITEDEF";
                std::string defaultHeadName = upperRaceCode + "HE00_DMSPRITEDEF";

                // Select skinned parts (from partsWithTransforms - already has skinning applied)
                std::vector<CharacterPart> selectedSkinnedParts;
                for (const auto& part : character->partsWithTransforms) {
                    if (!part.geometry) continue;

                    std::string partName = part.geometry->name;
                    std::transform(partName.begin(), partName.end(), partName.begin(),
                                   [](unsigned char c) { return std::toupper(c); });

                    if (partName == defaultBodyName || partName == defaultHeadName) {
                        selectedSkinnedParts.push_back(part);
                    }
                }

                // Select raw parts (from rawParts - unskinned for animation)
                std::vector<CharacterPart> selectedRawParts;
                for (const auto& part : character->rawParts) {
                    if (!part.geometry) continue;

                    std::string partName = part.geometry->name;
                    std::transform(partName.begin(), partName.end(), partName.begin(),
                                   [](unsigned char c) { return std::toupper(c); });

                    if (partName == defaultBodyName || partName == defaultHeadName) {
                        selectedRawParts.push_back(part);
                    }
                }

                if (selectedSkinnedParts.empty()) {
                    // Fall back to using all parts if we couldn't find defaults
                    selectedSkinnedParts = character->partsWithTransforms;
                    selectedRawParts = character->rawParts;
                }

                // If still empty (no partsWithTransforms), fall back to old behavior
                if (selectedSkinnedParts.empty()) {
                    auto combinedGeom = combineCharacterParts(character->parts);
                    if (!combinedGeom) {
                        continue;
                    }

                    auto modelData = std::make_shared<RaceModelData>();
                    modelData->combinedGeometry = combinedGeom;
                    modelData->textures = textures;
                    auto merged = getMergedTextures();
                    for (const auto& [name, tex] : merged) {
                        if (modelData->textures.find(name) == modelData->textures.end()) {
                            modelData->textures[name] = tex;
                        }
                    }
                    modelData->raceName = character->name;
                    modelData->raceId = raceId;
                    modelData->gender = gender;
                    modelData->scale = getRaceScale(raceId);

                    uint32_t key = makeCacheKey(raceId, gender);
                    loadedModels_[key] = modelData;

                    LOG_DEBUG(MOD_GRAPHICS, "RaceModelLoader: Loaded race {} from {} ({}, {} vertices, no animation data)",
                        raceId, sourceFilename, character->name, combinedGeom->vertices.size());
                    return true;
                }

                std::shared_ptr<ZoneGeometry> combinedGeom = combineCharacterPartsWithTransforms(selectedSkinnedParts);
                std::shared_ptr<ZoneGeometry> rawGeom = combineCharacterPartsRaw(selectedRawParts);

                if (!combinedGeom) {
                    continue;
                }

                auto modelData = std::make_shared<RaceModelData>();
                modelData->combinedGeometry = combinedGeom;
                modelData->rawGeometry = rawGeom;  // Store raw geometry for animation
                // Start with textures from the S3D file, then merge with getMergedTextures
                modelData->textures = textures;
                auto merged = getMergedTextures();
                for (const auto& [name, tex] : merged) {
                    if (modelData->textures.find(name) == modelData->textures.end()) {
                        modelData->textures[name] = tex;
                    }
                }
                modelData->raceName = character->name;
                modelData->raceId = raceId;
                modelData->gender = gender;
                modelData->scale = getRaceScale(raceId);

                // Copy animation data if available
                if (character->animatedSkeleton) {
                    modelData->skeleton = character->animatedSkeleton;
                }
                // Use vertex pieces from raw geometry (not skinned)
                if (rawGeom && !rawGeom->vertexPieces.empty()) {
                    modelData->vertexPieces = rawGeom->vertexPieces;
                }

                uint32_t key = makeCacheKey(raceId, gender);
                loadedModels_[key] = modelData;

                LOG_DEBUG(MOD_GRAPHICS, "RaceModelLoader: Loaded race {} from {} ({}, {} parts, {} vertices, {} animations, {} vertex pieces)",
                    raceId, sourceFilename, character->name, selectedSkinnedParts.size(), combinedGeom->vertices.size(),
                    modelData->skeleton ? modelData->skeleton->animations.size() : 0,
                    rawGeom ? rawGeom->vertexPieces.size() : 0);
                return true;
            }
        }
        return false;
    };

    // First, search already-cached _chr.s3d files
    for (const auto& [filename, cache] : otherChrCaches_) {
        if (searchAndBuildModel(cache.characters, cache.textures, filename)) {
            return true;
        }
    }

    // If not found in cache, scan directory for new _chr.s3d files
    try {
        for (const auto& entry : fs::directory_iterator(clientPath_)) {
            if (!entry.is_regular_file()) {
                continue;
            }

            std::string filename = entry.path().filename().string();
            std::string lowerFilename = filename;
            std::transform(lowerFilename.begin(), lowerFilename.end(), lowerFilename.begin(),
                           [](unsigned char c) { return std::tolower(c); });

            // Must be a _chr.s3d file
            if (lowerFilename.find("_chr.s3d") == std::string::npos) {
                continue;
            }

            // Skip race-specific global files (globalelf_chr.s3d, globalhum_chr.s3d, etc.)
            if (std::regex_match(lowerFilename, raceSpecificGlobalPattern)) {
                continue;
            }

            // Skip files we've already searched (global_chr.s3d and global[2-7]_chr.s3d)
            if (lowerFilename == "global_chr.s3d") {
                continue;
            }
            if (lowerFilename.length() == 16 && lowerFilename.substr(0, 6) == "global" &&
                lowerFilename[6] >= '2' && lowerFilename[6] <= '7' &&
                lowerFilename.substr(7) == "_chr.s3d") {
                continue;
            }

            // Skip the current zone file if already loaded
            if (!currentZoneName_.empty()) {
                std::string currentZoneFile = currentZoneName_ + "_chr.s3d";
                std::transform(currentZoneFile.begin(), currentZoneFile.end(), currentZoneFile.begin(),
                               [](unsigned char c) { return std::tolower(c); });
                if (lowerFilename == currentZoneFile) {
                    continue;
                }
            }

            // Skip if we already have this file in cache (was searched above)
            if (otherChrCaches_.find(lowerFilename) != otherChrCaches_.end()) {
                continue;
            }

            // Try to load from this _chr.s3d file
            std::string fullPath = entry.path().string();
            S3DLoader loader;
            if (!loader.loadZone(fullPath)) {
                // Cache an empty entry so we don't try again
                otherChrCaches_[lowerFilename] = OtherChrCache{};
                continue;
            }

            auto zone = loader.getZone();
            if (!zone || zone->characters.empty()) {
                // Cache an empty entry so we don't try again
                otherChrCaches_[lowerFilename] = OtherChrCache{};
                continue;
            }

            // Cache this file's characters and textures for future searches
            OtherChrCache& cache = otherChrCaches_[lowerFilename];
            cache.characters = zone->characters;
            cache.textures = zone->characterTextures;

            LOG_DEBUG(MOD_GRAPHICS, "RaceModelLoader: Cached {} characters from {}", cache.characters.size(), lowerFilename);

            // Search the newly loaded file
            if (searchAndBuildModel(cache.characters, cache.textures, lowerFilename)) {
                return true;
            }
        }
    } catch (const std::exception& e) {
        LOG_ERROR(MOD_GRAPHICS, "RaceModelLoader: Error scanning _chr.s3d files: {}", e.what());
    }

    return false;
}

} // namespace Graphics
} // namespace EQT
