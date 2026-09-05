// RaceModelLoader model loading methods - split from race_model_loader.cpp
// These methods remain part of the RaceModelLoader class
// The two .chr load paths, moved verbatim from model_loading.cpp by
// refactor/J29_model_loading_split.md (move-only, no behaviour change).
// loadModelFromCachedChr is 326 lines and loadModelFromZoneChr is 302;
// both are recorded Batch-G decomposition candidates that J29 deliberately
// did not touch.

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
bool RaceModelLoader::loadModelFromZoneChr(const std::string& zoneName, uint16_t raceId, uint8_t gender) {
    // Make sure zone models are loaded
    if (!zoneModelsLoaded_ || currentZoneName_ != zoneName) {
        if (!loadZoneModels(zoneName)) {
            return false;
        }
    }

    // Build list of race codes to try:
    // 1. The row's own code with gender (e.g., HUM/HUF for Human)
    // 2. The row's fallback_code, if it names one (e.g. race 71 QCM -> HUM)
    std::vector<std::string> codesToTry;

    // The row's own code, gendered
    std::string baseRaceCode = getRaceCode(raceId);
    if (!baseRaceCode.empty()) {
        std::string raceCode = getGenderedRaceCode(baseRaceCode, gender);
        if (std::find(codesToTry.begin(), codesToTry.end(), raceCode) == codesToTry.end()) {
            codesToTry.push_back(raceCode);
        }
    }

    // Then the row's fallback_code
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
                    {
                        // T113: loadedModelsMutex_ — see race_model_loader.h loadedModels_ comment.
                        std::lock_guard<std::mutex> lock(loadedModelsMutex_);
                        loadedModels_[key] = modelData;
                    }

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

                    // Merge animations from animation source (e.g., LIM for PUM)
                    std::string animSourceCode = getAnimationSourceCode(upperCode);
                    if (!animSourceCode.empty() && animSourceCode != upperCode &&
                        (modelData->skeleton->animations.empty() || modelData->skeleton->animations.size() < 5)) {

                        std::shared_ptr<CharacterSkeleton> sourceSkel;

                        // 1. Search current zone characters
                        for (const auto& sourceChar : zoneCharacters_) {
                            if (!sourceChar) continue;
                            std::string sourceName = sourceChar->name;
                            std::transform(sourceName.begin(), sourceName.end(), sourceName.begin(),
                                           [](unsigned char c) { return std::toupper(c); });
                            if (sourceName.find(animSourceCode) != std::string::npos) {
                                if (sourceChar->animatedSkeleton && !sourceChar->animatedSkeleton->animations.empty()) {
                                    sourceSkel = sourceChar->animatedSkeleton;
                                    break;
                                }
                            }
                        }

                        // 2. If not found, try to load from animation source's configured s3d file
                        if (!sourceSkel) {
                            // Get the s3d file for the animation source race
                            // We need to find which race uses animSourceCode and get its s3d_file
                            std::string animSourceS3d = getRaceS3DFileByCode(animSourceCode);
                            if (!animSourceS3d.empty()) {
                                std::string lowerFilename = animSourceS3d;
                                std::transform(lowerFilename.begin(), lowerFilename.end(), lowerFilename.begin(),
                                               [](unsigned char c) { return std::tolower(c); });

                                // T114: otherChrCaches_ is shared with the worker
                                // thread (preloadVariantModel) -- lock for the
                                // whole find/load/insert/search sequence, same
                                // discipline T113 applied to loadedModels_/
                                // variantModels_.
                                std::lock_guard<std::mutex> chrCacheLock(otherChrCacheMutex_);

                                // Check if already cached
                                auto cacheIt = otherChrCaches_.find(lowerFilename);
                                if (cacheIt == otherChrCaches_.end()) {
                                    // Load the chr file
                                    std::string fullPath = clientPath_ + animSourceS3d;
                                    S3DLoader loader;
                                    if (loader.loadZone(fullPath)) {
                                        auto zone = loader.getZone();
                                        if (zone && !zone->characters.empty()) {
                                            // T366 (B095): storeOtherChrCacheLocked
                                            // is the only correct insert -- a raw
                                            // otherChrCaches_[key] = ... never enters
                                            // chrCacheLruOrder_, so it can never be
                                            // evicted while still counting against
                                            // maxChrCacheEntries_ (the T91 bypass).
                                            storeOtherChrCacheLocked(lowerFilename, zone->characters,
                                                                     zone->characterTextures);
                                            LOG_DEBUG(MOD_GRAPHICS, "RaceModelLoader: Loaded {} for animation source {}",
                                                      animSourceS3d, animSourceCode);
                                            // Re-find: the store may have evicted.
                                            cacheIt = otherChrCaches_.find(lowerFilename);
                                        }
                                    }
                                }

                                // Search the cached chr file for animation source
                                if (cacheIt != otherChrCaches_.end()) {
                                    for (const auto& sourceChar : cacheIt->second.characters) {
                                        if (!sourceChar) continue;
                                        std::string sourceName = sourceChar->name;
                                        std::transform(sourceName.begin(), sourceName.end(), sourceName.begin(),
                                                       [](unsigned char c) { return std::toupper(c); });
                                        if (sourceName.find(animSourceCode) != std::string::npos) {
                                            if (sourceChar->animatedSkeleton && !sourceChar->animatedSkeleton->animations.empty()) {
                                                sourceSkel = sourceChar->animatedSkeleton;
                                                break;
                                            }
                                        }
                                    }
                                }
                            }
                        }

                        // 3. Search global characters as fallback
                        if (!sourceSkel) {
                            if (!globalModelsLoaded_) {
                                loadGlobalModels();
                            }
                            for (const auto& sourceChar : globalCharacters_) {
                                if (!sourceChar) continue;
                                std::string sourceName = sourceChar->name;
                                std::transform(sourceName.begin(), sourceName.end(), sourceName.begin(),
                                               [](unsigned char c) { return std::toupper(c); });
                                if (sourceName.find(animSourceCode) != std::string::npos) {
                                    if (sourceChar->animatedSkeleton && !sourceChar->animatedSkeleton->animations.empty()) {
                                        sourceSkel = sourceChar->animatedSkeleton;
                                        break;
                                    }
                                }
                            }
                        }

                        // Merge animations if source found
                        if (sourceSkel) {
                            auto& ourSkel = modelData->skeleton;
                            std::string lowerCode = upperCode;
                            std::transform(lowerCode.begin(), lowerCode.end(), lowerCode.begin(),
                                           [](unsigned char c) { return std::tolower(c); });
                            std::string lowerSource = animSourceCode;
                            std::transform(lowerSource.begin(), lowerSource.end(), lowerSource.begin(),
                                           [](unsigned char c) { return std::tolower(c); });

                            int addedAnimations = 0;
                            for (const auto& [animCode, sourceAnim] : sourceSkel->animations) {
                                if (ourSkel->animations.find(animCode) == ourSkel->animations.end()) {
                                    ourSkel->animations[animCode] = sourceAnim;
                                    addedAnimations++;
                                }
                            }

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

                            LOG_DEBUG(MOD_GRAPHICS, "RaceModelLoader: Merged animations from {} to {} - added {} animations, mapped {}/{} bones",
                                      animSourceCode, upperCode, addedAnimations, mappedBones, ourSkel->bones.size());
                        }
                    }
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

bool RaceModelLoader::loadModelFromCachedChr(const std::string& chrFilename, uint16_t raceId, uint8_t gender) {
    if (clientPath_.empty() || chrFilename.empty()) {
        return false;
    }

    // Normalize filename to lowercase for cache lookup
    std::string lowerFilename = chrFilename;
    std::transform(lowerFilename.begin(), lowerFilename.end(), lowerFilename.begin(),
                   [](unsigned char c) { return std::tolower(c); });

    // T114: otherChrCaches_ is shared with the worker thread
    // (preloadVariantModel). Lock for find/load/insert, then copy out the
    // (shared_ptr, so cheap) characters vector and release the lock before
    // the heavy, lock-free-elsewhere model-building work below -- avoids
    // holding otherChrCacheMutex_ across combineCharacterParts()/
    // getMergedTextures() and the nested loadedModelsMutex_ acquisition
    // further down, so no new lock-ordering relationship is introduced.
    std::vector<std::shared_ptr<CharacterModel>> characters;
    {
        std::lock_guard<std::mutex> chrCacheLock(otherChrCacheMutex_);

        // Check if already in cache
        auto cacheIt = otherChrCaches_.find(lowerFilename);
        if (cacheIt == otherChrCaches_.end()) {
            // Load the chr file
            std::string fullPath = clientPath_ + chrFilename;
            S3DLoader loader;
            if (!loader.loadZone(fullPath)) {
                // Cache empty entry to avoid repeated load attempts.
                // T364 (B065): through the store helper -- a raw
                // otherChrCaches_[key] = ... never enters chrCacheLruOrder_,
                // so it can never be evicted while still counting against
                // maxChrCacheEntries_ (the T91 bypass).
                storeOtherChrCacheLocked(lowerFilename, {}, {});
                return false;
            }

            auto zone = loader.getZone();
            if (!zone || zone->characters.empty()) {
                storeOtherChrCacheLocked(lowerFilename, {}, {});
                return false;
            }

            storeOtherChrCacheLocked(lowerFilename, zone->characters, zone->characterTextures);
            cacheIt = otherChrCaches_.find(lowerFilename);

            LOG_DEBUG(MOD_GRAPHICS, "RaceModelLoader: Cached {} characters from {}",
                      cacheIt != otherChrCaches_.end() ? cacheIt->second.characters.size() : 0,
                      chrFilename);
        }

        // Check if cache entry exists and has characters
        if (cacheIt == otherChrCaches_.end() || cacheIt->second.characters.empty()) {
            return false;
        }

        characters = cacheIt->second.characters;
    }

    // Build list of race codes to try
    std::vector<std::string> codesToTry;

    // The row's own code, gendered
    std::string baseRaceCode = getRaceCode(raceId);
    if (!baseRaceCode.empty()) {
        std::string raceCode = getGenderedRaceCode(baseRaceCode, gender);
        if (std::find(codesToTry.begin(), codesToTry.end(), raceCode) == codesToTry.end()) {
            codesToTry.push_back(raceCode);
        }
    }

    // Then the row's fallback_code
    std::string fallbackCode = getFallbackRaceCode(raceId, gender);
    if (!fallbackCode.empty() && std::find(codesToTry.begin(), codesToTry.end(), fallbackCode) == codesToTry.end()) {
        codesToTry.push_back(fallbackCode);
    }

    if (codesToTry.empty()) {
        LOG_DEBUG(MOD_GRAPHICS, "RaceModelLoader: No codes to try for race {} in {}", raceId, chrFilename);
        return false;
    }

    LOG_DEBUG(MOD_GRAPHICS, "RaceModelLoader: Searching {} ({} chars) for race {} with codes: {}",
              chrFilename, characters.size(), raceId,
              [&]() { std::string s; for (const auto& c : codesToTry) s += c + " "; return s; }());

    // Search cached characters for matching race code
    for (const std::string& codeToTry : codesToTry) {
        std::string upperCode = codeToTry;
        std::transform(upperCode.begin(), upperCode.end(), upperCode.begin(),
                       [](unsigned char c) { return std::toupper(c); });

        for (const auto& character : characters) {
            if (!character) {
                LOG_DEBUG(MOD_GRAPHICS, "RaceModelLoader:   Skipping null character");
                continue;
            }
            if (character->parts.empty()) {
                LOG_DEBUG(MOD_GRAPHICS, "RaceModelLoader:   Skipping '{}' - no parts (skinned={}, raw={})",
                          character->name, character->partsWithTransforms.size(), character->rawParts.size());
                continue;
            }

            std::string charName = character->name;
            std::transform(charName.begin(), charName.end(), charName.begin(),
                           [](unsigned char c) { return std::toupper(c); });

            LOG_DEBUG(MOD_GRAPHICS, "RaceModelLoader:   Checking char '{}' for code '{}'", charName, upperCode);

            if (charName.find(upperCode) != std::string::npos) {
                // Found a match - select default body and head parts
                std::string defaultBodyName = upperCode + "_DMSPRITEDEF";
                std::string defaultHeadName = upperCode + "HE00_DMSPRITEDEF";

                // Select skinned parts
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

                // Select raw parts for animation
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

                // Fall back to all parts if defaults not found
                if (selectedSkinnedParts.empty()) {
                    selectedSkinnedParts = character->partsWithTransforms;
                    selectedRawParts = character->rawParts;
                }

                // Fall back to old behavior if no partsWithTransforms
                if (selectedSkinnedParts.empty() && !character->parts.empty()) {
                    auto combinedGeom = combineCharacterParts(character->parts);
                    if (!combinedGeom) continue;

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

                    LOG_DEBUG(MOD_GRAPHICS, "RaceModelLoader: Loaded race {} from cached {} (no animation)",
                        raceId, chrFilename);
                    return true;
                }

                auto combinedGeom = combineCharacterPartsWithTransforms(selectedSkinnedParts);
                auto rawGeom = combineCharacterPartsRaw(selectedRawParts);
                if (!combinedGeom) continue;

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

                    // Merge animations from animation source (e.g., LIM for PUM)
                    std::string animSourceCode = getAnimationSourceCode(upperCode);
                    if (!animSourceCode.empty() && animSourceCode != upperCode) {
                        std::shared_ptr<CharacterSkeleton> sourceSkel;

                        // Load the animation_source_s3d file if specified for this race
                        std::string animSourceS3d = getAnimationSourceS3DFile(raceId);

                        // T114: otherChrCaches_ is shared with the worker thread.
                        // Locked from here through the end of the search loop below
                        // because searchSources holds raw pointers straight into
                        // otherChrCaches_ elements (&cache.characters) that must
                        // stay valid (no concurrent insert/erase) for as long as
                        // they're dereferenced.
                        std::lock_guard<std::mutex> chrCacheLock(otherChrCacheMutex_);

                        if (!animSourceS3d.empty()) {
                            std::string lowerAnimS3d = animSourceS3d;
                            std::transform(lowerAnimS3d.begin(), lowerAnimS3d.end(), lowerAnimS3d.begin(),
                                           [](unsigned char c) { return std::tolower(c); });

                            // Load the file if not already cached
                            if (otherChrCaches_.find(lowerAnimS3d) == otherChrCaches_.end()) {
                                std::string fullPath = clientPath_ + animSourceS3d;
                                S3DLoader loader;
                                if (loader.loadZone(fullPath)) {
                                    auto zone = loader.getZone();
                                    if (zone && !zone->characters.empty()) {
                                        // T364 (B065): via the store helper so
                                        // the entry is LRU-tracked and evictable.
                                        size_t loaded = zone->characters.size();
                                        storeOtherChrCacheLocked(lowerAnimS3d, zone->characters, zone->characterTextures);
                                        LOG_DEBUG(MOD_GRAPHICS, "RaceModelLoader: Loaded animation source file {} with {} characters",
                                                  animSourceS3d, loaded);
                                    }
                                }
                            }
                        }

                        // Build list of character sources to search for animation source
                        std::vector<const std::vector<std::shared_ptr<CharacterModel>>*> searchSources;

                        // 1. Search the current chr cache first (LIM is often in same file as PUM)
                        searchSources.push_back(&characters);

                        // 2. Search all other cached chr files
                        for (const auto& [filename, cache] : otherChrCaches_) {
                            if (&cache.characters != &characters) {
                                searchSources.push_back(&cache.characters);
                            }
                        }

                        // 3. Search global characters
                        if (!globalModelsLoaded_) {
                            loadGlobalModels();
                        }
                        searchSources.push_back(&globalCharacters_);

                        // Search all sources for animation source skeleton
                        for (const auto* charList : searchSources) {
                            for (const auto& sourceChar : *charList) {
                                if (!sourceChar) continue;
                                std::string sourceName = sourceChar->name;
                                std::transform(sourceName.begin(), sourceName.end(), sourceName.begin(),
                                               [](unsigned char c) { return std::toupper(c); });
                                if (sourceName.find(animSourceCode) != std::string::npos) {
                                    if (sourceChar->animatedSkeleton && !sourceChar->animatedSkeleton->animations.empty()) {
                                        sourceSkel = sourceChar->animatedSkeleton;
                                        break;
                                    }
                                }
                            }
                            if (sourceSkel) break;
                        }

                        if (sourceSkel) {
                            auto& ourSkel = modelData->skeleton;

                            std::string lowerCode = upperCode;
                            std::transform(lowerCode.begin(), lowerCode.end(), lowerCode.begin(),
                                           [](unsigned char c) { return std::tolower(c); });
                            std::string lowerSource = animSourceCode;
                            std::transform(lowerSource.begin(), lowerSource.end(), lowerSource.begin(),
                                           [](unsigned char c) { return std::tolower(c); });

                            // Add missing animations
                            int addedAnimations = 0;
                            for (const auto& [animCode, sourceAnim] : sourceSkel->animations) {
                                if (ourSkel->animations.find(animCode) == ourSkel->animations.end()) {
                                    ourSkel->animations[animCode] = sourceAnim;
                                    addedAnimations++;
                                }
                            }

                            // Merge bone animation tracks
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

                            LOG_DEBUG(MOD_GRAPHICS, "RaceModelLoader: Merged animations from {} to {} - added {} animations, mapped {}/{} bones",
                                      animSourceCode, upperCode, addedAnimations, mappedBones, ourSkel->bones.size());
                        } else {
                            LOG_DEBUG(MOD_GRAPHICS, "RaceModelLoader: Animation source {} not found for {}", animSourceCode, upperCode);
                        }
                    }
                }
                if (rawGeom && !rawGeom->vertexPieces.empty()) {
                    modelData->vertexPieces = rawGeom->vertexPieces;
                }

                uint32_t key = makeCacheKey(raceId, gender);
                {
                    // T113: loadedModelsMutex_ — see race_model_loader.h loadedModels_ comment.
                    std::lock_guard<std::mutex> lock(loadedModelsMutex_);
                    loadedModels_[key] = modelData;
                }

                LOG_DEBUG(MOD_GRAPHICS, "RaceModelLoader: Loaded race {} from cached {} ({} parts, {} vertices, {} animations)",
                    raceId, chrFilename, selectedSkinnedParts.size(), combinedGeom->vertices.size(),
                    modelData->skeleton ? modelData->skeleton->animations.size() : 0);
                return true;
            }
        }
    }

    return false;
}

bool RaceModelLoader::loadModelFromArchiveIndex(uint16_t raceId, uint8_t gender) {
    if (!graphicsArchiveIndex_) return false;

    std::string archivePath = graphicsArchiveIndex_->getArchiveForRace(raceId, gender);
    if (archivePath.empty()) return false;

    // Extract just the filename — loadModelFromCachedChr prepends clientPath_
    std::string filename = std::filesystem::path(archivePath).filename().string();
    return loadModelFromCachedChr(filename, raceId, gender);
}

bool RaceModelLoader::searchAllGlobalsForModel(uint16_t raceId, uint8_t gender) {
    if (graphicsArchiveIndex_) return false;  // Don't eager-load; caller should use archive index path

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

} // namespace Graphics
} // namespace EQT
