// RaceModelLoader model loading methods - split from race_model_loader.cpp
// These methods remain part of the RaceModelLoader class
// Zone .chr file search plus the mesh-lifetime and memory accounting,
// moved verbatim from model_loading.cpp by
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
                    {
                        // T113: loadedModelsMutex_ — see race_model_loader.h loadedModels_ comment.
                        std::lock_guard<std::mutex> lock(loadedModelsMutex_);
                        loadedModels_[key] = modelData;
                    }

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
                {
                    // T113: loadedModelsMutex_ — see race_model_loader.h loadedModels_ comment.
                    std::lock_guard<std::mutex> lock(loadedModelsMutex_);
                    loadedModels_[key] = modelData;
                }

                LOG_DEBUG(MOD_GRAPHICS, "RaceModelLoader: Loaded race {} from {} ({}, {} parts, {} vertices, {} animations, {} vertex pieces)",
                    raceId, sourceFilename, character->name, selectedSkinnedParts.size(), combinedGeom->vertices.size(),
                    modelData->skeleton ? modelData->skeleton->animations.size() : 0,
                    rawGeom ? rawGeom->vertexPieces.size() : 0);
                return true;
            }
        }
        return false;
    };

    // T114: otherChrCaches_ is shared with the worker thread. Locked across
    // both the cache scan below and the directory-scan/load/insert/evict
    // loop that follows -- coarse, but this whole function is a cold,
    // rarely-hit fallback path (main/loading thread only), not a hot
    // per-frame path, so correctness is worth more here than granularity.
    std::lock_guard<std::mutex> chrCacheLock(otherChrCacheMutex_);

    // First, search already-cached _chr.s3d files
    for (const auto& [filename, cache] : otherChrCaches_) {
        if (searchAndBuildModel(cache.characters, cache.textures, filename)) {
            // LRU: move to front on cache hit
            if (maxChrCacheEntries_ > 0) {
                chrCacheLruOrder_.remove(filename);
                chrCacheLruOrder_.push_front(filename);
            }
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

            // T267: per-race global archives (global[a-z]+_chr.s3d) are scanned
            // like any other -- globalpcfroglok_chr.s3d is the ONLY holder of
            // FRM/FRF and the old skip made it unreachable from here.

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
            // T366 (B095): all three inserts below go through
            // storeOtherChrCacheLocked -- the negative-cache entries as much as
            // the positive one. A raw otherChrCaches_[key] = ... never enters
            // chrCacheLruOrder_, so it can never be evicted while still counting
            // against maxChrCacheEntries_ (the T91 bypass), and the third insert
            // used to open-code the helper's push/evict inline.
            if (!loader.loadZone(fullPath)) {
                // Cache an empty entry so we don't try again
                storeOtherChrCacheLocked(lowerFilename, {}, {});
                continue;
            }

            auto zone = loader.getZone();
            if (!zone || zone->characters.empty()) {
                // Cache an empty entry so we don't try again
                storeOtherChrCacheLocked(lowerFilename, {}, {});
                continue;
            }

            // Cache this file's characters and textures for future searches
            storeOtherChrCacheLocked(lowerFilename, zone->characters, zone->characterTextures);

            // Re-find: the store may have evicted this very entry (a cap of 1
            // with an already-tracked entry ahead of it), so read the cached
            // vectors back through the iterator rather than assuming they are
            // still there.
            auto insertedIt = otherChrCaches_.find(lowerFilename);
            if (insertedIt == otherChrCaches_.end()) {
                continue;
            }
            OtherChrCache& cache = insertedIt->second;

            LOG_DEBUG(MOD_GRAPHICS, "RaceModelLoader: Cached {} characters from {} (total cached: {})",
                      cache.characters.size(), lowerFilename, otherChrCaches_.size());

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

void RaceModelLoader::addMeshRef(uint16_t raceId, uint8_t gender) {
    uint32_t key = makeCacheKey(raceId, gender);
    meshRefCounts_[key]++;
}

void RaceModelLoader::removeMeshRef(uint16_t raceId, uint8_t gender) {
    uint32_t key = makeCacheKey(raceId, gender);
    auto it = meshRefCounts_.find(key);
    if (it == meshRefCounts_.end()) return;

    it->second--;
    if (it->second <= 0) {
        meshRefCounts_.erase(it);

        // Evict meshes from caches
        auto meshIt = meshCache_.find(key);
        if (meshIt != meshCache_.end()) {
            if (meshIt->second) {
                meshIt->second->drop();
            }
            meshCache_.erase(meshIt);
        }

        // Remove model data
        // T113: loadedModelsMutex_ — see race_model_loader.h loadedModels_
        // comment. This was the confirmed race: removeMeshRef() (render
        // thread, entity despawn) erased loadedModels_ unlocked while
        // EntityPrepWorker's background thread read the same map unlocked
        // via getRaceModelData()/isModelDataCached().
        {
            std::lock_guard<std::mutex> lock(loadedModelsMutex_);
            loadedModels_.erase(key);
        }

        // Also evict variant and animated caches for this race/gender
        // Variant keys encode raceId in upper 32 bits, gender in bits 24-31
        auto varIt = variantMeshCache_.begin();
        while (varIt != variantMeshCache_.end()) {
            uint64_t vkey = varIt->first;
            uint16_t vRace = static_cast<uint16_t>(vkey >> 32);
            uint8_t vGender = static_cast<uint8_t>((vkey >> 24) & 0xFF);
            if (vRace == raceId && vGender == gender) {
                if (varIt->second) varIt->second->drop();
                varIt = variantMeshCache_.erase(varIt);
            } else {
                ++varIt;
            }
        }

        // Erase variant models. T113: variantModelsMutex_ — this was the
        // second half of the confirmed race, matching the captured
        // teardown backtrace (~RaceModelLoader -> map<uint64_t,
        // shared_ptr<RaceModelData>>::~map, i.e. variantModels_): this
        // erase loop ran unlocked while preloadVariantModel() (background
        // thread) inserted into the same map under variantModelsMutex_.
        std::lock_guard<std::mutex> variantModelsLock(variantModelsMutex_);
        auto vmIt = variantModels_.begin();
        while (vmIt != variantModels_.end()) {
            uint64_t vkey = vmIt->first;
            uint16_t vRace = static_cast<uint16_t>(vkey >> 32);
            uint8_t vGender = static_cast<uint8_t>((vkey >> 24) & 0xFF);
            if (vRace == raceId && vGender == gender) {
                vmIt = variantModels_.erase(vmIt);
            } else {
                ++vmIt;
            }
        }

        // Erase animated mesh caches
        animatedMeshCache_.erase(key);
        compositeAnimatedMeshCache_.erase(key);  // P08d

        auto vaIt = variantAnimatedMeshCache_.begin();
        while (vaIt != variantAnimatedMeshCache_.end()) {
            uint64_t vkey = vaIt->first;
            uint16_t vRace = static_cast<uint16_t>(vkey >> 32);
            uint8_t vGender = static_cast<uint8_t>((vkey >> 24) & 0xFF);
            if (vRace == raceId && vGender == gender) {
                vaIt = variantAnimatedMeshCache_.erase(vaIt);
            } else {
                ++vaIt;
            }
        }

        // P08d: composite variant animated mesh cache
        auto cvaIt = compositeVariantAnimatedMeshCache_.begin();
        while (cvaIt != compositeVariantAnimatedMeshCache_.end()) {
            uint64_t vkey = cvaIt->first;
            uint16_t vRace = static_cast<uint16_t>(vkey >> 32);
            uint8_t vGender = static_cast<uint8_t>((vkey >> 24) & 0xFF);
            if (vRace == raceId && vGender == gender) {
                cvaIt = compositeVariantAnimatedMeshCache_.erase(cvaIt);
            } else {
                ++cvaIt;
            }
        }

        LOG_DEBUG(MOD_GRAPHICS, "RaceModelLoader: Evicted caches for race {} gender {}", raceId, gender);
    }
}

size_t RaceModelLoader::releaseRawTextureData() {
    size_t freed = 0;

    // P13b: names in deferredPerPartTextureNames_ are per-part textures whose
    // decode/upload was DEFERRED because their race got a composite-sheet hit (see
    // race_model_loader.cpp's T05 loop guard) — no ineligible entity has needed them
    // yet, but one might, in this zone or a future one (races persist in
    // loadedModels_/preparedModelData_ across zone transitions). Their raw
    // TextureInfo::data is the ONLY copy of what an on-demand load
    // (buildMeshFromGeometry / createAnimatedNodeWithEquipment's texture-swap) would
    // decode from — clearing it here (this function only runs when
    // releaseTextureDataAfterUpload is on, e.g. the orangepi preset) would silently
    // break that on-demand load the first time it's actually needed, with no upload
    // ever having happened to make the loss safe. Every OTHER texture this function
    // touches has already been eagerly decoded+uploaded to GPU by the time this runs
    // (T05's eager branch, or the composite sheet itself), so releasing those is safe
    // exactly as before this unit.
    std::lock_guard<std::mutex> deferredLock(preparedDataMutex_);
    const auto& deferredNames = deferredPerPartTextureNames_;

    auto releaseMap = [&freed, &deferredNames](std::map<std::string, std::shared_ptr<TextureInfo>>& texMap) {
        for (auto& [name, tex] : texMap) {
            if (!tex) continue;
            if (deferredNames.count(name)) continue;
            freed += tex->rawDataBytes();
            tex->data.clear();
            tex->data.shrink_to_fit();
            for (auto& frame : tex->frames) {
                frame.data.clear();
                frame.data.shrink_to_fit();
            }
        }
    };

    // Release global textures
    releaseMap(globalTextures_);

    // Release numbered global textures
    for (auto& [num, texMap] : numberedGlobalTextures_) {
        releaseMap(texMap);
    }

    // Release zone textures
    releaseMap(zoneTextures_);

    // Release other chr cache textures
    {
        // T114: otherChrCaches_ is shared with the worker thread.
        std::lock_guard<std::mutex> chrCacheLock(otherChrCacheMutex_);
        for (auto& [filename, cache] : otherChrCaches_) {
            releaseMap(cache.textures);
        }
    }

    // Release cached merged textures (they're copies referencing same shared_ptrs)
    releaseMap(cachedMergedTextures_);

    // Release armor texture cache (can be re-loaded on demand from index)
    releaseMap(armorTextureCache_);

    return freed;
}

RaceModelLoader::MemoryStats RaceModelLoader::getMemoryStats() const {
    MemoryStats stats;

    auto sumMapBytes = [](const std::map<std::string, std::shared_ptr<TextureInfo>>& texMap) -> size_t {
        size_t total = 0;
        for (const auto& [name, tex] : texMap) {
            if (tex) total += tex->rawDataBytes();
        }
        return total;
    };

    stats.globalTextureBytes = sumMapBytes(globalTextures_);

    for (const auto& [num, texMap] : numberedGlobalTextures_) {
        stats.numberedTextureBytes += sumMapBytes(texMap);
    }

    stats.armorTextureBytes = sumMapBytes(armorTextureCache_);
    stats.armorTextureCount = armorTextureIndex_.size();
    stats.zoneTextureBytes = sumMapBytes(zoneTextures_);

    {
        // T114: otherChrCaches_ is shared with the worker thread.
        std::lock_guard<std::mutex> chrCacheLock(otherChrCacheMutex_);
        for (const auto& [filename, cache] : otherChrCaches_) {
            stats.otherChrTextureBytes += sumMapBytes(cache.textures);
        }
    }

    {
        // T113: loadedModelsMutex_ — see race_model_loader.h loadedModels_ comment.
        std::lock_guard<std::mutex> lock(loadedModelsMutex_);
        stats.loadedModelCount = loadedModels_.size();
    }
    stats.meshCacheCount = meshCache_.size();
    stats.variantMeshCacheCount = variantMeshCache_.size();
    stats.animatedMeshCacheCount = animatedMeshCache_.size() + variantAnimatedMeshCache_.size() +
        compositeAnimatedMeshCache_.size() + compositeVariantAnimatedMeshCache_.size();  // P08d

    // --- Geometry and skeleton data in loadedModels_ ---
    auto calcSkeletonBytes = [](const std::shared_ptr<CharacterSkeleton>& skel) -> size_t {
        if (!skel) return 0;
        size_t total = sizeof(CharacterSkeleton) + skel->modelCode.capacity();
        for (const auto& bone : skel->bones) {
            total += sizeof(AnimatedBone) + bone.name.capacity();
            total += bone.childIndices.capacity() * sizeof(int);
            if (bone.poseTrack)
                total += bone.poseTrack->frames.capacity() * sizeof(BoneTransform);
            for (const auto& [code, track] : bone.animationTracks)
                if (track) total += track->frames.capacity() * sizeof(BoneTransform);
        }
        for (const auto& [code, anim] : skel->animations)
            if (anim) total += sizeof(Animation) + anim->tracks.size() * 64;
        return total;
    };

    {
        // T113: loadedModelsMutex_ — see race_model_loader.h loadedModels_ comment.
        std::lock_guard<std::mutex> lock(loadedModelsMutex_);
        for (const auto& [key, model] : loadedModels_) {
            if (!model) continue;
            if (model->combinedGeometry) stats.modelGeometryBytes += model->combinedGeometry->getMemoryUsage();
            if (model->rawGeometry) stats.modelGeometryBytes += model->rawGeometry->getMemoryUsage();
            stats.modelGeometryBytes += model->vertexPieces.capacity() * sizeof(VertexPiece);
            stats.modelSkeletonBytes += calcSkeletonBytes(model->skeleton);
        }
    }
    {
        std::lock_guard<std::mutex> lock(variantModelsMutex_);
        for (const auto& [key, model] : variantModels_) {
            if (!model) continue;
            if (model->combinedGeometry) stats.modelGeometryBytes += model->combinedGeometry->getMemoryUsage();
            if (model->rawGeometry) stats.modelGeometryBytes += model->rawGeometry->getMemoryUsage();
        }
    }

    // --- CharacterModel data (global + numbered + zone + other) ---
    auto calcCharModels = [](const std::vector<std::shared_ptr<CharacterModel>>& chars) -> size_t {
        size_t total = 0;
        for (const auto& cm : chars) {
            if (!cm) continue;
            total += sizeof(CharacterModel) + cm->name.capacity();
            for (const auto& part : cm->parts)
                if (part) total += part->getMemoryUsage();
            total += cm->partsWithTransforms.capacity() * sizeof(CharacterPart);
            total += cm->rawParts.capacity() * sizeof(CharacterPart);
        }
        return total;
    };
    stats.characterModelBytes += calcCharModels(globalCharacters_);
    for (const auto& [num, chars] : numberedGlobalCharacters_)
        stats.characterModelBytes += calcCharModels(chars);
    stats.characterModelBytes += calcCharModels(zoneCharacters_);
    {
        // T114: otherChrCaches_ is shared with the worker thread.
        std::lock_guard<std::mutex> chrCacheLock(otherChrCacheMutex_);
        for (const auto& [name, cache] : otherChrCaches_)
            stats.characterModelBytes += calcCharModels(cache.characters);
    }

    // --- EQAnimatedMesh working data ---
    for (const auto& [key, mesh] : animatedMeshCache_)
        if (mesh) stats.animatedMeshBytes += mesh->estimateMemoryUsage();
    for (const auto& [key, mesh] : variantAnimatedMeshCache_)
        if (mesh) stats.animatedMeshBytes += mesh->estimateMemoryUsage();
    for (const auto& [key, mesh] : compositeAnimatedMeshCache_)  // P08d
        if (mesh) stats.animatedMeshBytes += mesh->estimateMemoryUsage();
    for (const auto& [key, mesh] : compositeVariantAnimatedMeshCache_)  // P08d
        if (mesh) stats.animatedMeshBytes += mesh->estimateMemoryUsage();

    // --- Irrlicht IMesh vertex/index buffers ---
    auto calcMeshBytes = [](irr::scene::IMesh* mesh) -> size_t {
        if (!mesh) return 0;
        size_t total = 0;
        for (irr::u32 i = 0; i < mesh->getMeshBufferCount(); ++i) {
            auto* buf = mesh->getMeshBuffer(i);
            if (buf) {
                total += buf->getVertexCount() * sizeof(irr::video::S3DVertex);
                total += buf->getIndexCount() * sizeof(irr::u16);
            }
        }
        return total;
    };
    for (const auto& [key, mesh] : meshCache_)
        stats.irrlichtMeshBytes += calcMeshBytes(mesh);
    for (const auto& [key, mesh] : variantMeshCache_)
        stats.irrlichtMeshBytes += calcMeshBytes(mesh);

    return stats;
}

} // namespace Graphics
} // namespace EQT
