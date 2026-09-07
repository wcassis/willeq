// RaceModelLoader model loading methods - split from race_model_loader.cpp
// These methods remain part of the RaceModelLoader class
// Archive discovery plus the global and S3D load paths; the remainder was
// moved to model_loading_chr.cpp and model_loading_search.cpp by
// refactor/J29_model_loading_split.md (move-only, no behaviour change).

#include "client/graphics/eq/race_model_loader.h"
#include "client/willeq_database.h"
#include "client/graphics/eq/race_codes.h"
#include "client/graphics/eq/geometry_combiner.h"
#include "client/graphics/eq/animation_mapping.h"
#include "client/graphics/eq/pfs.h"
#include "client/graphics/graphics_archive_index.h"
#include "common/logging.h"
#include <algorithm>
#include <filesystem>
#include <iostream>

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

// T452 (board B468): the armor archive is DERIVED FROM THE TEXTURE NAME.
//
// Until this unit getArmorTexture() ran
//   SELECT archive, ... FROM s3d_assets WHERE name=?
// against a table the shipped data/willeq.db has NEVER contained. `s3d_assets`
// is created only by `willeq_db_builder --s3d-index`, and no script, CMake
// rule or test has ever passed that flag, so the query failed at prepare() and
// every call returned nullptr -- from a MISSING TABLE, not from a miss. That
// has been true since the route was introduced on 2026-03-21 (ca267770), and
// it was not harmless.
//
// MEASURED against the client tree at /mnt/projects/EverQuestP1999. A
// character body texture is named {race}{part}{variant:02d}{page:02d}.bmp, and
// the variant partitions the archives:
//   00-04   global_chr.s3d           -- already merged into globalTextures_
//   10-16   robes; mesh_building.cpp rewrites these to clk<nn><page>.bmp,
//           which is also in global_chr.s3d (42 clk pages)
//   17-23   global17_amr.s3d .. global23_amr.s3d -- loaded by NOTHING ELSE
// The seven *_amr.s3d hold 1,784 textures between them (289/291/289/290/291/
// 289/45) and no other archive has them. So mesh_building.cpp's
// isTextureAvailable() asked for e.g. hufch1701.bmp, got nullptr, and declined
// the variant override: the whole Kunark/Velious armor tier rendered as the
// base skin.
//
// Each archive holds its own variant and, with ONE exception, nothing else --
// haft2202.bmp is in global20_amr.s3d, not global21..23. A strict
// name->archive derivation would miss exactly that one file, so this probes
// the derived archive FIRST and then the other six, which is both correct for
// all 1,784 and cheap: the fallback runs only for a 17-23 name, and only when
// the obvious archive did not have it.
//
// No database, no index file, and nothing new to ship. All 1,784 names are
// already in data/eqe1_texture_index.json and all seven archives have EQE1
// packs, so the bytes returned here resolve to real pixels under --eqe1-path
// as well (checked: 0 of 1,784 missing from the index).
namespace {

constexpr int kArmorVariantFirst = 17;
constexpr int kArmorVariantLast = 23;

// The 2-digit variant a body-texture name encodes, or -1 if it is not of that
// shape. The four characters before the extension are {variant}{page}; the
// race+part prefix is 5 characters for player races but this does not depend
// on its length, only on there being one.
int armorVariantFromTextureName(const std::string& lowerName) {
    size_t dot = lowerName.rfind('.');
    if (dot == std::string::npos || dot < 5) return -1;
    size_t v = dot - 4;
    for (size_t i = v; i < dot; ++i) {
        if (lowerName[i] < '0' || lowerName[i] > '9') return -1;
    }
    return (lowerName[v] - '0') * 10 + (lowerName[v + 1] - '0');
}

}  // namespace

// The archives to probe for `lowerName`, most likely first. Empty means the
// name is not a 17-23 armor texture and nothing should be opened at all --
// which is the common case, and is why this is cheaper than the SQL query it
// replaces even when it finds nothing.
std::vector<std::string> ArmorArchivesForTextureName(const std::string& lowerName) {
    const int variant = armorVariantFromTextureName(lowerName);
    if (variant < kArmorVariantFirst || variant > kArmorVariantLast) return {};

    std::vector<std::string> archives;
    archives.reserve(kArmorVariantLast - kArmorVariantFirst + 1);
    archives.push_back("global" + std::to_string(variant) + "_amr.s3d");
    for (int v = kArmorVariantFirst; v <= kArmorVariantLast; ++v) {
        if (v == variant) continue;
        archives.push_back("global" + std::to_string(v) + "_amr.s3d");
    }
    return archives;
}

std::shared_ptr<TextureInfo> RaceModelLoader::getArmorTexture(const std::string& lowerName) {
    // A cached entry may be null: that is a cached MISS, and it matters --
    // without it every mesh build re-probes seven 3 MB archives for a name no
    // archive has (most races do not have every one of the seven tiers).
    auto cacheIt = armorTextureCache_.find(lowerName);
    if (cacheIt != armorTextureCache_.end()) {
        return cacheIt->second;
    }

    const std::vector<std::string> archives = ArmorArchivesForTextureName(lowerName);
    if (archives.empty()) return nullptr;

    for (const std::string& archiveName : archives) {
        PfsArchive archive;
        if (!archive.open(clientPath_ + archiveName)) continue;

        std::vector<char> data;
        if (!archive.get(lowerName, data)) continue;

        auto tex = std::make_shared<TextureInfo>();
        tex->name = lowerName;
        tex->data = std::move(data);
        armorTextureCache_[lowerName] = tex;

        LOG_DEBUG(MOD_GRAPHICS, "RaceModelLoader: Loaded armor texture on demand: {} ({} bytes from {})",
                  lowerName, tex->data.size(), archiveName);
        return tex;
    }

    armorTextureCache_[lowerName] = nullptr;
    return nullptr;
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

    // Invalidate merged textures cache (zone textures are part of the merge)
    mergedTexturesCacheValid_ = false;

    // Clear model caches - models will be re-loaded on demand with new zone textures
    {
        // T113: loadedModelsMutex_ — see race_model_loader.h loadedModels_ comment.
        std::lock_guard<std::mutex> lock(loadedModelsMutex_);
        loadedModels_.clear();
    }
    meshCache_.clear();
    {
        std::lock_guard<std::mutex> lock(variantModelsMutex_);
        variantModels_.clear();
    }
    variantMeshCache_.clear();
    animatedMeshCache_.clear();
    variantAnimatedMeshCache_.clear();
    {
        // T114: otherChrCaches_ is written by the worker thread
        // (preloadVariantModel) and the render thread -- see race_model_loader.h
        // otherChrCaches_ comment; every touch site below now takes
        // otherChrCacheMutex_.
        std::lock_guard<std::mutex> lock(otherChrCacheMutex_);
        otherChrCaches_.clear();  // Also clear cached _chr.s3d files from other zones
    }

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
        {
            // T113: loadedModelsMutex_ — see race_model_loader.h loadedModels_ comment.
            std::lock_guard<std::mutex> lock(loadedModelsMutex_);
            loadedModels_[key] = modelData;
        }

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
        if (graphicsArchiveIndex_) return false;  // Don't eager-load; caller should use archive index path
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
                {
                    // T113: loadedModelsMutex_ — see race_model_loader.h loadedModels_ comment.
                    std::lock_guard<std::mutex> lock(loadedModelsMutex_);
                    loadedModels_[key] = modelData;
                }

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
                {
                    // T113: loadedModelsMutex_ — see race_model_loader.h loadedModels_ comment.
                    std::lock_guard<std::mutex> lock(loadedModelsMutex_);
                    loadedModels_[key] = modelData;
                }

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
            {
                // T113: loadedModelsMutex_ — see race_model_loader.h loadedModels_ comment.
                std::lock_guard<std::mutex> lock(loadedModelsMutex_);
                loadedModels_[key] = modelData;
            }

            LOG_DEBUG(MOD_GRAPHICS, "RaceModelLoader: Loaded race {} from global{}_chr.s3d ({}, {} parts, {} vertices, {} animations, {} vertex pieces)",
                raceId, globalNum, character->name, selectedSkinnedParts.size(), combinedGeom->vertices.size(),
                modelData->skeleton ? modelData->skeleton->animations.size() : 0,
                rawGeom ? rawGeom->vertexPieces.size() : 0);
            return true;
        }
    }

    return false;
}

} // namespace Graphics
} // namespace EQT
