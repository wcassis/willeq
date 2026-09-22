// RaceModelLoader model loading methods - split from race_model_loader.cpp
// These methods remain part of the RaceModelLoader class
// Zone .chr file search plus the mesh-lifetime and memory accounting,
// moved verbatim from model_loading.cpp by
// refactor/J29_model_loading_split.md (move-only, no behaviour change).

#include "client/graphics/eq/race_model_loader.h"
#include "client/graphics/constrained_texture_cache.h"  // T504: snapshotEQE1Loaders()
#include "client/willeq_database.h"
#include "client/graphics/eq/race_codes.h"
#include "client/graphics/eq/geometry_combiner.h"
#include "client/graphics/eq/animation_mapping.h"
#include "client/graphics/eq/pfs.h"
#include "client/graphics/graphics_archive_index.h"
#include "common/logging.h"
#include "client/core/memory_ledger.h"
#include <fmt/format.h>
#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <set>
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
                            modelData->textures.set(name, tex);  // T504: copy-on-write
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
                        modelData->textures.set(name, tex);  // T504: copy-on-write
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
                // T369 (B105) -- see the sibling warning below.
                LOG_WARN(MOD_GRAPHICS,
                         "RaceModelLoader: '{}' is a _chr.s3d archive that yielded "
                         "NO characters (the archive failed to load: {}) -- caching "
                         "the miss",
                         fullPath, loader.getError());
                // Cache an empty entry so we don't try again
                storeOtherChrCacheLocked(lowerFilename, {}, {});
                continue;
            }

            auto zone = loader.getZone();
            if (!zone || zone->characters.empty()) {
                // T369 (B105): an archive that EXISTS on disk and is named
                // `*_chr.s3d` yet produces no characters is a defect signal,
                // not a routine miss -- it is exactly what the whole-path
                // `_chr` derivation produced for every archive in a client
                // directory whose own name contained `_chr`, and the client
                // printed nothing at all about it. Both negative-cache
                // branches warn. Logged once per path: the negative cache
                // entry below stops the scan ever reaching this line again for
                // the same file. The caching behaviour is deliberately
                // unchanged.
                LOG_WARN(MOD_GRAPHICS,
                         "RaceModelLoader: '{}' is a _chr.s3d archive that yielded "
                         "NO characters (it loaded, but holds none) -- caching the "
                         "miss",
                         fullPath);
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
        forgetReloadMemo(key);   // T504b: its sources may be pruned again; the next request may reload

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
            freed += tex->releaseBytes();  // T504: stays present()
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

size_t RaceModelLoader::releaseBytesIfEqe1Authoritative(
        std::map<std::string, std::shared_ptr<TextureInfo>>& texMap) {
    if (!constrainedTextureCache_ || !constrainedTextureCache_->hasEQE1Loaders()) {
        return 0;  // S3D textures are the source: the bytes are needed
    }
    size_t freed = 0;
    for (auto& [name, tex] : texMap) {
        if (tex && !tex->bytesReleased) freed += tex->releaseBytes();
    }
    return freed;
}

namespace {
// Skeleton sizing, shared by getMemoryStats() ([modelmem]) and
// srcCharLogLines() ([srcchar]) so the two tables measure alike.
size_t skeletonBytesOf(const std::shared_ptr<CharacterSkeleton>& skel) {
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
}

size_t skeletonTrackBytesOf(const std::shared_ptr<SkeletonTrack>& sk) {
    if (!sk) return 0;
    size_t total = sizeof(SkeletonTrack) + sk->name.capacity();
    total += sk->bones.capacity() * sizeof(std::shared_ptr<SkeletonBone>);
    total += sk->allBones.capacity() * sizeof(std::shared_ptr<SkeletonBone>);
    total += sk->parentIndices.capacity() * sizeof(int);
    total += sk->meshRefs.capacity() * sizeof(uint32_t);
    for (const auto& b : sk->allBones) {
        if (!b) continue;
        total += sizeof(SkeletonBone) + b->name.capacity();
        total += b->children.capacity() * sizeof(std::shared_ptr<SkeletonBone>);
        if (b->orientation) total += sizeof(BoneOrientation);
    }
    return total;
}
} // namespace

RaceModelLoader::MemoryStats RaceModelLoader::getMemoryStats() const {
    MemoryStats stats;

    // --- B843: ownership-aware pixel tally. A TextureInfo's PIXELS belong to
    // the ledger category that allocated them (TextureInfo::ownerCategory,
    // stamped by loadCharacterTextures()/getArmorTexture()), and a shared_ptr
    // can hold the same TextureInfo in several of the maps below. So every
    // map is walked through this one deduplicator:
    //   owner == EntityModels  -> its bytes go into the matching tex_* term
    //                             (modelMemTotal() charges them to this
    //                             category, exactly as before);
    //   owner == anything else -> the bytes are LEFT OUT of every tex_* term
    //                             and recorded ONCE in sharedTextureBytes
    //                             with the owner and every claimant named
    //                             (Will's ruling, docs/decisions.md
    //                             2026-09-17 item 3).
    // Before B843 the five tex_* terms counted every pixel in every map, so
    // on the Pi -- where zoneTextures_ is allocated under the render thread's
    // ZoneParse scope and the zone _chr.s3d caches under the entity-prep
    // thread's scope -- the EntityModels tally claimed 27,631,752 B another
    // category owned (tally 49,778,752 against 22,146,989 live requested,
    // +125 %, T500 Leg C).
    std::map<const TextureInfo*, size_t> sharedEntryIndex;
    // B843: node/key/object sizes, shared by the pixel tally (below), the
    // per-model map sum (T500 Leg A (a)) and the loader-map storage sum
    // (T500 (d)). Defined once, here, at the top of the function.
    using MergedTexMap = std::map<std::string, std::shared_ptr<TextureInfo>>;
    constexpr size_t kRbNodeOverhead = 4 * sizeof(void*);
    constexpr size_t kMergedTexNodeBytes =
        kRbNodeOverhead + sizeof(std::pair<const std::string, std::shared_ptr<TextureInfo>>);
    constexpr size_t kStringSsoCapacity = 15;  // libstdc++
    // The TextureInfo object's bytes in the EXACT arithmetic T500 (d) used in
    // noteTexMapStorage (raw name/frame capacity, no SSO threshold, no +1),
    // so the EntityModels-owned figure is byte-identical to the pre-B843
    // baseline. Used both for the owned term and, for foreign-owned blocks,
    // as the object half of the shared block's storageBytes (the control
    // block, like the object itself, lives in the allocator's block and is
    // intentionally outside this tally, matching the baseline).
    auto texObjectBytes = [](const TextureInfo& tex) -> size_t {
        size_t n = sizeof(TextureInfo) + tex.name.capacity()
                 + tex.frames.capacity() * sizeof(TextureFrame);
        for (const auto& f : tex.frames) n += f.name.capacity();
        return n;
    };
    // B843: register `tex` as a shared block if its owner is not EntityModels
    // (deduped by pointer); return the entry's index, or -1 when owned.
    // storageBytes is NOT set here: noteTexMapStorage is the single authority
    // on the node+key+object bytes, and it visits every loader map.
    auto ensureSharedEntry = [&](const char* claimant,
                                 const std::shared_ptr<TextureInfo>& tex) -> int {
        if (tex->ownerCategory
                == static_cast<uint8_t>(eqt::memory::Category::EntityModels))
            return -1;
        auto it = sharedEntryIndex.find(tex.get());
        if (it != sharedEntryIndex.end()) {
            stats.sharedTextureBytes[it->second].claimants +=
                std::string(", ") + claimant;
            return static_cast<int>(it->second);
        }
        size_t idx = stats.sharedTextureBytes.size();
        sharedEntryIndex.emplace(tex.get(), idx);
        const uint8_t owner = tex->ownerCategory;
        const char* ownerName = owner < eqt::memory::kCategoryCount
            ? eqt::memory::kCategoryNames[owner] : "unknown";
        MemoryStats::SharedTextureBytes entry;
        entry.bytes = tex->rawDataBytes();
        // storageBytes is filled in by noteTexMapStorage (the single authority
        // on the node+key+object bytes).
        entry.owner = ownerName;
        entry.claimants = claimant;
        stats.sharedTextureBytes.push_back(std::move(entry));
        return static_cast<int>(idx);
    };
    auto sumMapBytes = [&](const char* claimant,
                           const std::map<std::string, std::shared_ptr<TextureInfo>>& texMap,
                           size_t& ownedTermOut) {
        size_t owned = 0;
        for (const auto& [name, tex] : texMap) {
            if (!tex) continue;
            if (tex->ownerCategory
                    == static_cast<uint8_t>(eqt::memory::Category::EntityModels)) {
                owned += tex->rawDataBytes();
                continue;
            }
            // Foreign-owned: left out of this tex_* term, reported ONCE in
            // sharedTextureBytes (Will's ruling, docs/decisions.md
            // 2026-09-17 item 3).
            ensureSharedEntry(claimant, tex);
        }
        ownedTermOut += owned;
    };
    // Same pixels through another map: add the claimant, count nothing.
    // The per-model `textures` maps are shared views of the loader maps
    // (getMergedTextures() copies the shared_ptrs), so their pixels are
    // already charged -- to the tex_* terms (owner EntityModels) or the
    // shared entries above -- and nothing here may add them a second time.
    // Storage (node + key + object) is charged by noteTexMapStorage, which
    // visits every loader map including cachedMergedTextures_ -- a superset of
    // every per-model map -- so there is no storage to claim here either.
    auto claimSharedMap = [&](const char* claimant,
                              const std::map<std::string, std::shared_ptr<TextureInfo>>& texMap) {
        for (const auto& [name, tex] : texMap) {
            if (!tex) continue;
            if (tex->rawDataBytes() == 0) continue;
            ensureSharedEntry(claimant, tex);
        }
    };

    sumMapBytes("tex_global", globalTextures_, stats.globalTextureBytes);

    for (const auto& [num, texMap] : numberedGlobalTextures_) {
        sumMapBytes("tex_numbered", texMap, stats.numberedTextureBytes);
        // B843 (g): the raw held total the owned term is a subdivision of.
        for (const auto& [name, tex] : texMap)
            if (tex) stats.numberedTextureBytesHeld += tex->rawDataBytes();
    }

    sumMapBytes("tex_armor", armorTextureCache_, stats.armorTextureBytes);
    // T452: was armorTextureIndex_.size(), a map with no writer at all --
    // it always read 0. The cache is what actually holds armor textures.
    stats.armorTextureCount = armorTextureCache_.size();
    sumMapBytes("tex_zone", zoneTextures_, stats.zoneTextureBytes);
    // B843 (g): the raw held total the owned term is a subdivision of.
    for (const auto& [name, tex] : zoneTextures_)
        if (tex) stats.zoneCharacterTexturesHeld += tex->rawDataBytes();

    {
        // T114: otherChrCaches_ is shared with the worker thread.
        std::lock_guard<std::mutex> chrCacheLock(otherChrCacheMutex_);
        for (const auto& [filename, cache] : otherChrCaches_) {
            sumMapBytes("tex_otherchr", cache.textures, stats.otherChrTextureBytes);
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
    const auto calcSkeletonBytes = &skeletonBytesOf;  // hoisted (T504b): shared with srcCharLogLines

    // --- T500 Leg A (d): the four terms the first pass's 48 % residual was
    // made of. Pointer sets, so a structure shared between two owners is
    // charged ONCE and to the first owner that reaches it -- that is what
    // makes these terms disjoint from the ten above rather than a second
    // opinion about the same bytes.
    std::set<const CharacterSkeleton*> chargedSkeletons;
    std::set<const SkeletonTrack*> chargedSkeletonTracks;
    std::set<const ResolvedMaterialList*> chargedMaterialLists;
    std::set<const TextureInfo*> chargedTextureObjects;
    // B843: the foreign-owned TextureInfo objects (deduped by pointer), so the
    // object half of each shared block's storageBytes is charged once, not
    // once per map occurrence (node+key are genuinely per-occurrence).
    std::set<const TextureInfo*> chargedSharedObjects;

    // The original (pre-flatten) skeleton every CharacterModel keeps a
    // shared_ptr to. Nothing has ever charged it.
    const auto calcSkeletonTrackBytes = &skeletonTrackBytesOf;  // hoisted (T504b)

    // T500 (e): what calcSkeletonBytes() does NOT charge. Same traversal, the
    // complement of the same structure, deduplicated by pointer so a TrackDef
    // shared between two bones is charged once. The constants are computed from
    // the types, never written down, so the figure is right on armv7 too.
    constexpr size_t kRbNode = 4 * sizeof(void*);          // _Rb_tree_node_base
    constexpr size_t kCtrlBlock = 2 * sizeof(void*) + 16;  // shared_ptr control block
    constexpr size_t kSso = 15;                            // libstdc++ std::string
    auto strHeap = [](const std::string& x) -> size_t {
        return x.capacity() > kSso ? x.capacity() + 1 : 0;
    };
    std::set<const TrackDef*> chargedTracks;
    auto calcSkeletonExtra = [&](const std::shared_ptr<CharacterSkeleton>& skel) -> size_t {
        if (!skel) return 0;
        size_t extra = 0;
        // The bone vector's own storage beyond the per-bone sizeof() the
        // existing sum already adds.
        if (skel->bones.capacity() > skel->bones.size())
            extra += (skel->bones.capacity() - skel->bones.size()) * sizeof(AnimatedBone);
        extra += strHeap(skel->modelCode);
        auto noteTrack = [&](const std::shared_ptr<TrackDef>& t) {
            if (!t || !chargedTracks.insert(t.get()).second) return;
            extra += sizeof(TrackDef) + kCtrlBlock + strHeap(t->name);
            // frames.capacity() * sizeof(BoneTransform) is already charged.
        };
        for (const auto& bone : skel->bones) {
            extra += strHeap(bone.name);
            noteTrack(bone.poseTrack);
            for (const auto& [code, track] : bone.animationTracks) {
                extra += kRbNode + sizeof(std::pair<const std::string,
                                                    std::shared_ptr<TrackDef>>);
                extra += strHeap(code);
                noteTrack(track);
            }
        }
        for (const auto& [code, anim] : skel->animations) {
            extra += kRbNode + sizeof(std::pair<const std::string,
                                                std::shared_ptr<Animation>>);
            extra += strHeap(code) + kCtrlBlock;
            if (!anim) continue;
            extra += strHeap(anim->name) + strHeap(anim->modelCode);
            // The existing sum charges a flat 64 B per track; charge the rest.
            const size_t perTrack = kRbNode
                + sizeof(std::pair<const std::string, std::shared_ptr<TrackRef>>)
                + sizeof(TrackRef) + kCtrlBlock;
            extra += anim->tracks.size() * (perTrack > 64 ? perTrack - 64 : 0);
            for (const auto& [bname, tref] : anim->tracks) {
                extra += strHeap(bname);
                if (tref) extra += strHeap(tref->name);
            }
        }
        return extra;
    };

    // A geometry's materialData is SHARED by design (one 0x31 fragment serves
    // many meshes), which is exactly why ZoneGeometry::getMemoryUsage()
    // excludes it -- and why every caller that summed getMemoryUsage() left it
    // uncharged. Charge each distinct list once.
    auto noteGeometry = [&](const std::shared_ptr<ZoneGeometry>& g) {
        if (!g || !g->materialData) return;
        if (chargedMaterialLists.insert(g->materialData.get()).second)
            stats.materialListBytes += g->materialData->getMemoryUsage();
    };

    // --- T500 Leg A (a): the per-model COPY of the merged texture map ---
    // RaceModelData::textures is assigned `getMergedTextures()` BY VALUE at ten
    // sites and layered on top of an S3D map at three more, so every model owns
    // a full std::map of its own. The mapped TextureInfo is shared (shared_ptr)
    // and its pixels are already charged by sumMapBytes above; what nothing has
    // ever charged is the map's own storage -- one red-black node per entry
    // plus any key too long for std::string's SSO buffer.
    //
    // The node size is computed, not assumed: libstdc++'s _Rb_tree_node is
    // _Rb_tree_node_base (a colour enum padded to a word, plus three pointers)
    // followed by the value_type. So the figure is right on both x86-64 (80 B)
    // and armv7 (40 B) without a per-arch constant. (MergedTexMap,
    // kRbNodeOverhead, kMergedTexNodeBytes and kStringSsoCapacity are defined
    // once at the top of this function, beside the B843 shared-block
    // accounting that re-uses the same node size.)
    // (entries, sum of the mapped shared_ptr targets): two maps with the same
    // fingerprint hold the same texture set, which is what "duplicate" means
    // here. Cheap, and it needs no lock on cachedMergedTextures_, so no new
    // lock-ordering edge is introduced into getMemoryStats().
    std::set<std::pair<size_t, uintptr_t>> texMapFingerprints;
    auto sumTextureMap = [&](const MergedTexMap& m) {
        stats.mergedTextureMapCount += 1;
        stats.mergedTextureEntryCount += m.size();
        size_t bytes = m.size() * kMergedTexNodeBytes;
        uintptr_t fp = 0;
        for (const auto& [name, tex] : m) {
            if (name.capacity() > kStringSsoCapacity) bytes += name.capacity() + 1;
            fp += reinterpret_cast<uintptr_t>(tex.get());
        }
        stats.mergedTextureMapBytes += bytes;
        texMapFingerprints.insert({m.size(), fp});
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
            if (chargedSkeletons.insert(model->skeleton.get()).second)
                stats.skeletonContainerBytes += calcSkeletonExtra(model->skeleton);  // T500
            noteGeometry(model->combinedGeometry);
            noteGeometry(model->rawGeometry);
            sumTextureMap(model->textures);
            // B843: name this model as a claimant of any shared texture
            // pixels it holds (never a second count of them).
            claimSharedMap(
                ("model:" + std::to_string(model->raceId)
                 + ":" + std::to_string(model->gender)).c_str(),
                model->textures);
        }
    }
    {
        std::lock_guard<std::mutex> lock(variantModelsMutex_);
        for (const auto& [key, model] : variantModels_) {
            if (!model) continue;
            if (model->combinedGeometry) stats.modelGeometryBytes += model->combinedGeometry->getMemoryUsage();
            if (model->rawGeometry) stats.modelGeometryBytes += model->rawGeometry->getMemoryUsage();
            noteGeometry(model->combinedGeometry);
            noteGeometry(model->rawGeometry);
            sumTextureMap(model->textures);
            claimSharedMap(
                ("variant:" + std::to_string(model->raceId)
                 + ":" + std::to_string(model->gender)).c_str(),
                model->textures);
        }
    }
    {
        std::lock_guard<std::mutex> lock(preparedDataMutex_);
        for (const auto& [key, model] : preparedModelData_) {
            if (!model) continue;
            noteGeometry(model->combinedGeometry);
            noteGeometry(model->rawGeometry);
            sumTextureMap(model->textures);
            claimSharedMap(
                ("prepared:" + std::to_string(model->raceId)
                 + ":" + std::to_string(model->gender)).c_str(),
                model->textures);
        }
    }
    stats.distinctTextureMapCount = texMapFingerprints.size();

    // --- CharacterModel data (global + numbered + zone + other) ---
    // T500 Leg A (b)/(c): the arithmetic for characterModelBytes is UNCHANGED
    // (that field may not move); what is new is the per-owner split, which sums
    // to it exactly, and dupGeomOut -- the geometry a CharacterModel holds that
    // this sum has never charged. `parts` is charged in full below;
    // `partsWithTransforms` aliases the same shared_ptr objects, so it is
    // correctly not double counted; `rawParts` is a DEEP COPY made before
    // skinning (s3d_loader_characters.cpp) and is charged only
    // sizeof(CharacterPart) -- its vertices and triangles are counted nowhere.
    // The pointer set below charges each distinct geometry at most once, so the
    // term is the uncharged remainder and nothing else.
    auto calcCharModels = [&](const std::vector<std::shared_ptr<CharacterModel>>& chars,
                              size_t& dupGeomOut, size_t& skeletonOut) -> size_t {
        size_t total = 0;
        for (const auto& cm : chars) {
            if (!cm) continue;
            total += sizeof(CharacterModel) + cm->name.capacity();
            for (const auto& part : cm->parts)
                if (part) total += part->getMemoryUsage();
            total += cm->partsWithTransforms.capacity() * sizeof(CharacterPart);
            total += cm->rawParts.capacity() * sizeof(CharacterPart);

            std::set<const ZoneGeometry*> charged;
            for (const auto& part : cm->parts)
                if (part) charged.insert(part.get());
            for (const auto& p : cm->partsWithTransforms)
                if (p.geometry && charged.insert(p.geometry.get()).second)
                    dupGeomOut += p.geometry->getMemoryUsage();
            for (const auto& p : cm->rawParts)
                if (p.geometry && charged.insert(p.geometry.get()).second)
                    dupGeomOut += p.geometry->getMemoryUsage();

            // T500 (d): the shared material lists behind all of that geometry,
            // and the two skeletons this CharacterModel keeps alive for the
            // whole process whether or not any spawn ever uses its race.
            for (const auto* g : charged)
                if (g && g->materialData
                      && chargedMaterialLists.insert(g->materialData.get()).second)
                    stats.materialListBytes += g->materialData->getMemoryUsage();
            if (cm->animatedSkeleton
                    && chargedSkeletons.insert(cm->animatedSkeleton.get()).second) {
                skeletonOut += calcSkeletonBytes(cm->animatedSkeleton);
                stats.skeletonContainerBytes += calcSkeletonExtra(cm->animatedSkeleton);
            }
            stats.containerCapacityBytes +=
                cm->parts.capacity() * sizeof(std::shared_ptr<ZoneGeometry>);
            if (cm->skeleton && chargedSkeletonTracks.insert(cm->skeleton.get()).second)
                skeletonOut += calcSkeletonTrackBytes(cm->skeleton);
        }
        return total;
    };
    // T504b Leg 2: snapshots (the stores are pruned by another thread).
    std::vector<std::pair<int, CharacterList>> numberedSnap;
    {
        std::lock_guard<std::mutex> storesLock(sourceStoresMutex_);
        for (const auto& [num, chars] : numberedGlobalCharacters_) numberedSnap.emplace_back(num, chars);
    }
    stats.sourceCharacterGlobalBytes +=
        calcCharModels(snapshotGlobalCharacters(), stats.duplicateGeometryBytes,
                       stats.sourceSkeletonGlobalBytes);
    for (const auto& [num, chars] : numberedSnap)
        stats.sourceCharacterNumberedBytes += calcCharModels(chars, stats.duplicateGeometryBytes,
                                                             stats.sourceSkeletonNumberedBytes);
    stats.sourceCharacterZoneBytes +=
        calcCharModels(snapshotZoneCharacters(), stats.duplicateGeometryBytes,
                       stats.sourceSkeletonZoneBytes);
    {
        // T114: otherChrCaches_ is shared with the worker thread.
        std::lock_guard<std::mutex> chrCacheLock(otherChrCacheMutex_);
        for (const auto& [name, cache] : otherChrCaches_)
            stats.sourceCharacterOtherBytes += calcCharModels(cache.characters,
                                                              stats.duplicateGeometryBytes,
                                                              stats.sourceSkeletonOtherBytes);

        // T500 (d): the loader's OWN texture maps -- nodes and keys, plus each
        // distinct TextureInfo object (rawDataBytes() is the pixels only).
        // cachedMergedTextures_ is guarded by this same mutex.
        //
        // B843: ownership follows the block. The map node, the key and the
        // TextureInfo object were allocated in the SAME step and scope that
        // parsed the texture's pixels (the whole loader map is populated by
        // one copy in that scope), so they carry the SAME ledger owner as the
        // pixels. A texture whose pixels another category owns therefore has
        // its node + key + object owned by that category too: they are
        // excluded from loaderTextureMapBytes / textureObjectBytes (the
        // EntityModels-owned remainder) and added to the shared block's
        // storageBytes, reported once with its owner (Will's ruling,
        // docs/decisions.md 2026-09-17 item 3).
        auto noteTexMapStorage = [&](const MergedTexMap& m) {
            for (const auto& [name, tex] : m) {
                if (!tex) continue;
                const size_t keyBytes =
                    name.capacity() > kStringSsoCapacity ? name.capacity() + 1 : 0;
                const bool ownedByEntity = tex->ownerCategory
                    == static_cast<uint8_t>(eqt::memory::Category::EntityModels);
                if (ownedByEntity) {
                    stats.loaderTextureMapBytes += kMergedTexNodeBytes + keyBytes;
                    if (chargedTextureObjects.insert(tex.get()).second)
                        stats.textureObjectBytes += texObjectBytes(*tex);
                } else {
                    // Foreign-owned: attribute the node + key + object to the
                    // shared block, never to the EntityModels-owned terms.
                    // The node + key are one allocation PER map occurrence
                    // (each red-black map holds its own node + key for this
                    // pointer); the TextureInfo object is one allocation per
                    // DISTINCT pointer (deduped), matching the owned arm's
                    // chargedTextureObjects. ensureSharedEntry() has already
                    // registered the block when the loader maps were summed
                    // above; the else arm covers cachedMergedTextures_, which
                    // sumMapBytes does not visit.
                    size_t storageHere = kMergedTexNodeBytes + keyBytes;
                    if (chargedSharedObjects.insert(tex.get()).second)
                        storageHere += texObjectBytes(*tex);
                    auto it = sharedEntryIndex.find(tex.get());
                    if (it != sharedEntryIndex.end()) {
                        stats.sharedTextureBytes[it->second].storageBytes += storageHere;
                    } else {
                        sharedEntryIndex.emplace(tex.get(), stats.sharedTextureBytes.size());
                        const uint8_t owner = tex->ownerCategory;
                        const char* ownerName = owner < eqt::memory::kCategoryCount
                            ? eqt::memory::kCategoryNames[owner] : "unknown";
                        MemoryStats::SharedTextureBytes entry;
                        entry.bytes = tex->rawDataBytes();
                        entry.storageBytes = storageHere;
                        entry.owner = ownerName;
                        entry.claimants = "cached_merged";
                        stats.sharedTextureBytes.push_back(std::move(entry));
                    }
                }
            }
        };
        noteTexMapStorage(globalTextures_);
        for (const auto& [num, m] : numberedGlobalTextures_) noteTexMapStorage(m);
        noteTexMapStorage(armorTextureCache_);
        noteTexMapStorage(zoneTextures_);
        for (const auto& [name, cache] : otherChrCaches_) noteTexMapStorage(cache.textures);
        noteTexMapStorage(cachedMergedTextures_);
    }
    stats.characterModelBytes += stats.sourceCharacterGlobalBytes
                               + stats.sourceCharacterNumberedBytes
                               + stats.sourceCharacterZoneBytes
                               + stats.sourceCharacterOtherBytes;

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

// T500 Leg A: the DISJOINT terms, summed. The ten pre-existing terms partition
// what the loader knew it held; mergedTextureMapBytes and duplicateGeometryBytes
// are the two it did not. The per-owner sourceCharacter* fields are a
// subdivision of characterModelBytes and are deliberately NOT added again.
size_t RaceModelLoader::modelMemTotal(const MemoryStats& s) {
    return s.globalTextureBytes + s.numberedTextureBytes + s.armorTextureBytes
         + s.zoneTextureBytes + s.otherChrTextureBytes
         + s.modelGeometryBytes + s.modelSkeletonBytes + s.characterModelBytes
         + s.animatedMeshBytes + s.irrlichtMeshBytes
         + s.mergedTextureMapBytes + s.duplicateGeometryBytes
         + s.sourceSkeletonGlobalBytes + s.sourceSkeletonNumberedBytes
         + s.sourceSkeletonZoneBytes + s.sourceSkeletonOtherBytes
         + s.materialListBytes + s.textureObjectBytes + s.loaderTextureMapBytes
         + s.skeletonContainerBytes + s.containerCapacityBytes;
}

// T500 Leg A: the split, one line, with the ledger's own EntityModels figures
// read at the SAME instant so `total` vs `ledger_live` is a single comparison
// rather than two greps that may straddle an allocation.
std::string RaceModelLoader::modelMemLogLine(const std::string& point) const {
    const MemoryStats s = getMemoryStats();
    const size_t total = modelMemTotal(s);
    const auto cat = eqt::memory::stats(eqt::memory::Category::EntityModels);
    // T500: the comparison is against liveREQUESTED, not liveBytes. liveBytes
    // is the whole glibc chunk -- caller bytes plus the size word, the 16-byte
    // rounding and the ledger's own 16-byte header -- and a logical tally can
    // never reach it. The per-block overhead is reported beside it rather than
    // hidden in the residual.
    const int64_t residual = static_cast<int64_t>(cat.liveRequestedBytes)
                           - static_cast<int64_t>(total);
    const double residualPct = cat.liveRequestedBytes
                                   ? (100.0 * static_cast<double>(residual)
                                      / static_cast<double>(cat.liveRequestedBytes))
                                   : 0.0;
    return fmt::format(
        "[modelmem] point={} total={} ledger_requested={} ledger_live={} ledger_peak={}"
        " block_overhead={} blocks={} residual={} residual_pct={:.2f}"
        " tex_global={} tex_numbered={} tex_armor={} tex_zone={} tex_otherchr={}"
        " model_geometry={} model_skeleton={} char_models={} anim_mesh={} irr_mesh={}"
        " merged_tex_map={} merged_tex_maps={} merged_tex_entries={} merged_tex_distinct={}"
        " src_char_global={} src_char_numbered={} src_char_zone={} src_char_other={}"
        " dup_geometry={}"
        " src_skel_global={} src_skel_numbered={} src_skel_zone={} src_skel_other={}"
        " material_lists={} tex_objects={} loader_tex_maps={}"
        " skel_containers={} container_capacity={} allocs={} models={}",
        point, total, cat.liveRequestedBytes, cat.liveBytes, cat.peakBytes,
        cat.blockOverheadBytes, cat.liveBlocks, residual, residualPct,
        s.globalTextureBytes, s.numberedTextureBytes, s.armorTextureBytes,
        s.zoneTextureBytes, s.otherChrTextureBytes,
        s.modelGeometryBytes, s.modelSkeletonBytes, s.characterModelBytes,
        s.animatedMeshBytes, s.irrlichtMeshBytes,
        s.mergedTextureMapBytes, s.mergedTextureMapCount, s.mergedTextureEntryCount,
        s.distinctTextureMapCount,
        s.sourceCharacterGlobalBytes, s.sourceCharacterNumberedBytes,
        s.sourceCharacterZoneBytes, s.sourceCharacterOtherBytes,
        s.duplicateGeometryBytes,
        s.sourceSkeletonGlobalBytes, s.sourceSkeletonNumberedBytes,
        s.sourceSkeletonZoneBytes, s.sourceSkeletonOtherBytes,
        s.materialListBytes, s.textureObjectBytes, s.loaderTextureMapBytes,
        s.skeletonContainerBytes, s.containerCapacityBytes,
        cat.allocs, s.loadedModelCount);
}

// B843: the shared lines. One line per distinct TextureInfo block the loader
// holds but the ledger charges to ANOTHER category, in Will's ruling form
// (`X: owner, claimant, ...`) -- reported ONCE, never added into the
// entity_models total. `X` is the WHOLE block: its pixels plus the map node +
// key and the TextureInfo object that hold it (storageBytes; the shared_ptr
// control block, like the object, is outside this tally -- see
// MemoryStats::SharedTextureBytes). Emitted after the [modelmem] line; empty
// when every texture block the loader holds is owned by entity_models (the
// x86 offline path).
std::string RaceModelLoader::modelMemSharedLines(const std::string& point) const {
    const MemoryStats s = getMemoryStats();
    if (s.sharedTextureBytes.empty()) return std::string();
    uint64_t total = 0;
    uint64_t storageTotal = 0;
    for (const auto& e : s.sharedTextureBytes) {
        total += e.bytes + e.storageBytes;
        storageTotal += e.storageBytes;
    }
    std::string out = fmt::format(
        "[modelmem-shared] point={} shared_total={} shared_storage={} distinct_blocks={}",
        point, total, storageTotal, s.sharedTextureBytes.size());
    for (const auto& e : s.sharedTextureBytes) {
        out += fmt::format(" | {} (pixels {} + storage {}): {}, [{}]",
                           e.bytes + e.storageBytes, e.bytes, e.storageBytes,
                           e.owner, e.claimants);
    }
    return out;
}


std::string RaceModelLoader::srcCharLogLines(const std::string& point) const {
    // The used set: skeletons a built model holds, and every Animation those
    // skeletons hold (a donor's animations are shared by pointer after the
    // merge, so a source whose Animation object is in here is a donor).
    std::set<const CharacterSkeleton*> usedSkeletons;
    std::set<const Animation*> usedAnimations;
    auto take = [&](const std::shared_ptr<RaceModelData>& m) {
        if (!m || !m->skeleton) return;
        usedSkeletons.insert(m->skeleton.get());
        for (const auto& [code, anim] : m->skeleton->animations)
            if (anim) usedAnimations.insert(anim.get());
    };
    {
        std::lock_guard<std::mutex> lock(loadedModelsMutex_);
        for (const auto& [key, m] : loadedModels_) take(m);
    }
    {
        std::lock_guard<std::mutex> lock(variantModelsMutex_);
        for (const auto& [key, m] : variantModels_) take(m);
    }
    {
        std::lock_guard<std::mutex> lock(preparedDataMutex_);
        for (const auto& [key, m] : preparedModelData_) take(m);
    }

    // Bytes of one source character: each distinct part geometry once, plus
    // its two skeletons. The same measures modelMemLogLine() uses, so the
    // per-archive sums line up with the src_* rows there.
    auto charBytes = [](const CharacterModel& cm) {
        size_t b = sizeof(CharacterModel) + cm.name.capacity();
        std::set<const ZoneGeometry*> seen;
        for (const auto& part : cm.parts)
            if (part && seen.insert(part.get()).second) b += part->getMemoryUsage();
        for (const auto& p : cm.partsWithTransforms)
            if (p.geometry && seen.insert(p.geometry.get()).second) b += p.geometry->getMemoryUsage();
        for (const auto& p : cm.rawParts)
            if (p.geometry && seen.insert(p.geometry.get()).second) b += p.geometry->getMemoryUsage();
        b += cm.partsWithTransforms.capacity() * sizeof(CharacterPart);
        b += cm.rawParts.capacity() * sizeof(CharacterPart);
        // Container overhead (modelmem's skel_containers) is not included here.
        if (cm.animatedSkeleton) b += skeletonBytesOf(cm.animatedSkeleton);
        if (cm.skeleton) b += skeletonTrackBytesOf(cm.skeleton);
        return b;
    };
    enum Class { Used = 0, Donor = 1, Unused = 2, Static = 3 };
    auto classify = [&](const CharacterModel& cm) -> Class {
        if (!cm.animatedSkeleton) return Static;   // no skeleton: nothing to key on
        if (usedSkeletons.count(cm.animatedSkeleton.get())) return Used;
        for (const auto& [code, anim] : cm.animatedSkeleton->animations)
            if (anim && usedAnimations.count(anim.get())) return Donor;
        return Unused;
    };

    std::string out;
    size_t tChars = 0, tBytes = 0, tUnusedBytes = 0;
    auto walk = [&](const std::string& archive,
                    const std::vector<std::shared_ptr<CharacterModel>>& chars) {
        size_t n[4] = {0, 0, 0, 0};
        size_t bytes[4] = {0, 0, 0, 0};
        std::string unusedNames;
        for (const auto& cm : chars) {
            if (!cm) continue;
            const Class c = classify(*cm);
            const size_t b = charBytes(*cm);
            ++n[c];
            bytes[c] += b;
            if (c == Unused && unusedNames.size() < 200) {
                if (!unusedNames.empty()) unusedNames += ',';
                unusedNames += cm->name;
            }
        }
        const size_t all = n[0] + n[1] + n[2] + n[3];
        const size_t allBytes = bytes[0] + bytes[1] + bytes[2] + bytes[3];
        tChars += all;
        tBytes += allBytes;
        tUnusedBytes += bytes[Unused] + bytes[Static];
        if (!out.empty()) out += '\n';
        out += fmt::format(
            "[srcchar] point={} archive={} chars={} used={} donor={} unused={} static={} "
            "bytes={} bytes_used={} bytes_donor={} bytes_unused={} bytes_static={} unused_names={}",
            point, archive, all, n[Used], n[Donor], n[Unused], n[Static],
            allBytes, bytes[Used], bytes[Donor], bytes[Unused], bytes[Static], unusedNames);
    };
    const CharacterList globalSnap = snapshotGlobalCharacters();   // T504b Leg 2: snapshots
    walk("global_chr.s3d", globalSnap);
    {
        std::vector<std::pair<int, CharacterList>> numberedSnap;
        {
            std::lock_guard<std::mutex> storesLock(sourceStoresMutex_);
            for (const auto& [num, chars] : numberedGlobalCharacters_) numberedSnap.emplace_back(num, chars);
        }
        for (const auto& [num, chars] : numberedSnap) walk(fmt::format("global{}_chr.s3d", num), chars);
    }
    const CharacterList zoneSnap = snapshotZoneCharacters();
    if (!zoneSnap.empty()) walk(currentZoneName_ + "_chr.s3d", zoneSnap);
    {
        std::lock_guard<std::mutex> chrCacheLock(otherChrCacheMutex_);
        for (const auto& [name, cache] : otherChrCaches_) {
            // T504b Leg 1: the global entry holds the SAME objects as
            // globalCharacters_ (adopted, not re-parsed); counting it again
            // would double the total. Report it as shared and skip.
            const bool sharedWithGlobal = name == "global_chr.s3d" && !cache.characters.empty()
                && !globalSnap.empty() && cache.characters.front() == globalSnap.front();
            if (sharedWithGlobal) {
                if (!out.empty()) out += '\n';
                out += fmt::format("[srcchar] point={} archive=other:{} shared_with=global_chr.s3d chars={}",
                                   point, name, cache.characters.size());
                continue;
            }
            walk("other:" + name, cache.characters);
        }
    }
    if (!out.empty()) out += '\n';
    out += fmt::format("[srcchar] point={} archive=TOTAL chars={} bytes={} bytes_droppable={} "
                       "used_skeletons={} used_animations={}",
                       point, tChars, tBytes, tUnusedBytes, usedSkeletons.size(), usedAnimations.size());
    return out;
}

} // namespace Graphics
} // namespace EQT
