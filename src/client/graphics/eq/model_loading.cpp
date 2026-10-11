// RaceModelLoader model loading methods - split from race_model_loader.cpp
// These methods remain part of the RaceModelLoader class
// Archive discovery plus the global and S3D load paths; the remainder was
// moved to model_loading_chr.cpp and model_loading_search.cpp by
// refactor/J29_model_loading_split.md (move-only, no behaviour change).

#include "client/graphics/eq/race_model_loader.h"
#include "client/core/memory_ledger.h"
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

    // T504b Leg 1: if the worker already parsed global_chr.s3d into
    // otherChrCaches_, adopt those objects instead of parsing again.
    {
        std::lock_guard<std::mutex> chrLock(otherChrCacheMutex_);
        auto cacheIt = otherChrCaches_.find("global_chr.s3d");
        if (cacheIt != otherChrCaches_.end() && !cacheIt->second.characters.empty()) {
            {
                std::lock_guard<std::mutex> storesLock(sourceStoresMutex_);
                globalCharacters_ = cacheIt->second.characters;
                recordArchiveNamesLocked("global", globalChrPath, globalCharacters_);   // T504b Leg 2
            }
            globalTextures_ = cacheIt->second.textures;
            globalModelsLoaded_.store(true, std::memory_order_release);
            LOG_INFO(MOD_GRAPHICS, "RaceModelLoader: adopted {} global_chr.s3d characters from the other-chr cache (no second parse)",
                     globalCharacters_.size());
            return !globalCharacters_.empty();
        }
    }

    S3DLoader loader;
    if (!loader.loadZone(globalChrPath, characterLoadOptions())) {
        LOG_ERROR(MOD_GRAPHICS, "RaceModelLoader: Failed to load {}: {}", globalChrPath, loader.getError());
        return false;
    }

    auto zone = loader.getZone();
    if (!zone) {
        return false;
    }

    {
        std::lock_guard<std::mutex> storesLock(sourceStoresMutex_);
        globalCharacters_ = zone->characters;
        recordArchiveNamesLocked("global", globalChrPath, globalCharacters_);   // T504b Leg 2
    }
    globalTextures_ = zone->characterTextures;
    releaseBytesIfEqe1Authoritative(globalTextures_);  // T504
    globalModelsLoaded_.store(true, std::memory_order_release);
    {
        // T504b Leg 1: publish the SAME objects to the worker's store.
        std::lock_guard<std::mutex> chrLock(otherChrCacheMutex_);
        if (otherChrCaches_.find("global_chr.s3d") == otherChrCaches_.end())
            storeOtherChrCacheLocked("global_chr.s3d", globalCharacters_, globalTextures_);
    }

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

    // T500 Leg B: TEXTURES only. `zone` goes out of scope with its characters,
    // their parts, their rawParts deep copies and their animated skeletons
    // still in it -- 15,486,203 B of retention that every caller of this
    // function but one was paying for a texture map. See the header.
    int loadedCount = 0;
    for (int num = 2; num <= 7; ++num) {
        std::string filename = clientPath_ + "global" + std::to_string(num) + "_chr.s3d";

        S3DLoader loader;
        if (!loader.loadZone(filename, characterLoadOptions())) {
            // Not all numbered global files may exist
            continue;
        }

        auto zone = loader.getZone();
        if (!zone || zone->characterTextures.empty()) {
            continue;
        }

        numberedGlobalTextures_[num] = zone->characterTextures;
        releaseBytesIfEqe1Authoritative(numberedGlobalTextures_[num]);  // T504

        LOG_DEBUG(MOD_GRAPHICS, "RaceModelLoader: Loaded {} character textures from global{}_chr.s3d",
                  zone->characterTextures.size(), num);

        loadedCount++;
    }

    numberedGlobalsLoaded_ = true;

    return loadedCount > 0;
}

// T500 Leg B: the character half, per archive and on demand. This is what
// loadNumberedGlobalModels() used to do eagerly for all six.
bool RaceModelLoader::ensureNumberedGlobalCharacters(int num, const std::vector<std::string>* codeFilter) {
    if (clientPath_.empty()) return false;
    const std::string filename = clientPath_ + "global" + std::to_string(num) + "_chr.s3d";
    const std::string key = "global" + std::to_string(num);
    if (numberedGlobalCharacterArchives_.count(num)) {
        // T504c Leg 2: a filtered (or pruned) store reloads for a wanted
        // code the archive is indexed to hold.
        if (codeFilter) {
            std::lock_guard<std::mutex> chrLock(otherChrCacheMutex_);
            std::lock_guard<std::mutex> storesLock(sourceStoresMutex_);
            auto it = numberedGlobalCharacters_.find(num);
            const CharacterList empty;
            const CharacterList& have = it == numberedGlobalCharacters_.end() ? empty : it->second;
            for (const auto& code : *codeFilter)
                if (storeLacksIndexedCodeLocked(key, have, code)) {
                    reloadArchiveForCodesLocked(key, filename, *codeFilter, 0, 0, "ensureNumberedGlobalCharacters");
                    break;
                }
        }
        return numberedGlobalCharacters_.count(num) > 0;
    }
    numberedGlobalCharacterArchives_.insert(num);

    S3DLoader loader;
    const S3DLoadOptions opts = characterLoadOptions(codeFilter);   // T504c Leg 2: filtered when the caller says what it wants
    if (!loader.loadZone(filename, opts)) return false;
    auto zone = loader.getZone();
    if (!zone) return false;
    {
        std::lock_guard<std::mutex> storesLock(sourceStoresMutex_);   // every name, even when filtered
        recordArchiveNamesLocked("global" + std::to_string(num), filename, zone->allCharacterNames);
    }
    if (zone->characters.empty()) return false;

    {
        std::lock_guard<std::mutex> storesLock(sourceStoresMutex_);
        numberedGlobalCharacters_[num] = zone->characters;
        recordArchiveNamesLocked(key, filename, zone->characters);   // T504b Leg 2
        if (codeFilter) archiveIndex_[key].pruned = true;            // incomplete: reload-on-miss applies
    }
    // The textures come with the same parse; if the texture-only pass never
    // ran (or found nothing) this is the same map it would have stored.
    if (!numberedGlobalTextures_.count(num) && !zone->characterTextures.empty()) {
        numberedGlobalTextures_[num] = zone->characterTextures;
        releaseBytesIfEqe1Authoritative(numberedGlobalTextures_[num]);  // T504
        mergedTexturesCacheValid_ = false;
    }
    LOG_DEBUG(MOD_GRAPHICS, "RaceModelLoader: Loaded {} character models from global{}_chr.s3d (on demand)",
              zone->characters.size(), num);
    return true;
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
    // B843: armor pixels are charged to the scope ACTIVE at the call site
    // (EntityModels on the render thread, the entity-prep scope on the worker
    // thread) -- same as before, this only records that owner on the
    // TextureInfo (s3d_loader_textures-style stamping below) so the tally can
    // name it.
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
        tex->sourceArchive = archive.sharedPath();  // B947: restore origin
        // B843: stamp the allocating scope, like loadCharacterTextures() does.
        tex->ownerCategory = static_cast<uint8_t>(eqt::memory::currentCategory());
        armorTextureCache_[lowerName] = tex;

        LOG_DEBUG(MOD_GRAPHICS, "RaceModelLoader: Loaded armor texture on demand: {} ({} bytes from {})",
                  lowerName, tex->data.size(), archiveName);
        return tex;
    }

    armorTextureCache_[lowerName] = nullptr;
    return nullptr;
}

bool RaceModelLoader::loadZoneModels(const std::string& zoneName) {
    // T504 (load-step audit): this is ENTITY model data. It is called from
    // setupInstantScene() on the render thread, whose open scope is ZoneMesh, so
    // the whole <zone>_chr.s3d parse was charged to zone_mesh -- MEASURED iceclad:
    // zone_mesh read 11.4 MiB at zone_load_start, before any zone geometry
    // existed, and the [modelmem] tally (25.95 MiB) was twice the entity_models
    // row (11.41) because the other half was filed here.
    eqt::memory::MemoryScope memScope(eqt::memory::Category::EntityModels);
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
    {
        std::lock_guard<std::mutex> storesLock(sourceStoresMutex_);
        zoneCharacters_.clear();
        archiveIndex_.erase("zone");   // T504b Leg 2: a new zone, a new index
    }
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
    forgetAllReloadMemos();   // T504b
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
    if (!loader.loadZone(zoneFilename, characterLoadOptions())) {
        // Zone-specific chr file may not exist for all zones
        return false;
    }

    auto zone = loader.getZone();
    if (!zone || zone->characters.empty()) {
        return false;
    }

    {
        std::lock_guard<std::mutex> storesLock(sourceStoresMutex_);
        zoneCharacters_ = zone->characters;
        recordArchiveNamesLocked("zone", zoneFilename, zoneCharacters_);   // T504b Leg 2
    }
    zoneTextures_ = zone->characterTextures;
    currentZoneName_ = zoneName;
    clearPrefetchRaces();   // T504c Leg 4: the set belongs to the zone being left
    zoneModelsLoaded_ = true;
    // T504 resolver Leg 0: what the ORIGINAL client would load for this zone.
    if (!eraZonesLoaded_) { eraZones_ = LoadEraZones(); eraZonesLoaded_ = true; }
    eraSources_ = LoadEraModelSources(clientPath_, zoneName, eraZones_);
    eraNameIndex_.clear();
    eraNameIndexBuilt_ = false;
    LOG_INFO(MOD_GRAPHICS, "{}", eraSources_.toLogLine(zoneName));

    // T504 (load-step audit): what this eager whole-archive parse retains.
    size_t rawTextureBytes = 0;
    for (const auto& [name, tex] : zoneTextures_) {
        if (tex) rawTextureBytes += tex->rawDataBytes();
    }
    const size_t releasedNow = releaseBytesIfEqe1Authoritative(zoneTextures_);  // T504
    LOG_INFO(MOD_GRAPHICS, "RaceModelLoader: zone models '{}': {} characters, {} textures with {} raw bytes "
             "({} released at once: EQE1 authoritative)",
             zoneFilename, zoneCharacters_.size(), zoneTextures_.size(), rawTextureBytes, releasedNow);

    return true;
}

bool RaceModelLoader::loadModelFromS3D(const std::string& s3dPath, uint16_t raceId, uint8_t gender) {
    S3DLoader loader;
    if (!loader.loadZone(s3dPath, characterLoadOptions())) {
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
        for (const auto& character : snapshotGlobalCharacters()) {  // T504b Leg 2
            if (!character || character->parts.empty()) {
                continue;
            }

            // Character names in global_chr.s3d typically contain the race code
            std::string charName = character->name;
            std::transform(charName.begin(), charName.end(), charName.begin(),
                           [](unsigned char c) { return std::toupper(c); });

            // Look for race code in character name
            if (charName.find(codeToTry) != std::string::npos) {
                // Found a match. B733 (issue #70): select the parts the
                // skeleton REFERENCES (its 0x200 mesh list), with the
                // race-code default names as the fallback for skeletons that
                // list none -- a part whose mesh carries a name the race code
                // does not predict (race 146 SSN's head "SNNHE00_DMSPRITEDEF")
                // was dropped by the old name-only selection.
                SelectedCharacterParts selected = selectCharacterParts(*character, codeToTry);
                std::vector<CharacterPart> selectedSkinnedParts = std::move(selected.skinned);
                std::vector<CharacterPart> selectedRawParts = std::move(selected.raw);

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
                modelData->textures = getMergedTexturesShared();  // T504: shared, not copied
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
    // T500 Leg B: the characters are no longer pre-loaded by
    // loadNumberedGlobalModels(); this is the one path that wants them, so it
    // is the one that loads them.
    const std::vector<std::string> wantedHere = wantedCodesFor(raceId, gender);   // T504b Leg 3
    ensureNumberedGlobalCharacters(globalNum, &wantedHere);

    // Check if this numbered global is loaded (T504b Leg 2: a snapshot)
    const CharacterList characters = snapshotNumberedGlobalCharacters(globalNum);
    if (characters.empty()) {
        return false;
    }

    auto texIt = numberedGlobalTextures_.find(globalNum);
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
            // Found a match. B733 (issue #70): select the parts the
            // skeleton REFERENCES (its 0x200 mesh list), with the
            // race-code default names as the fallback for skeletons that
            // list none -- a part whose mesh carries a name the race code
            // does not predict (race 146 SSN's head "SNNHE00_DMSPRITEDEF")
            // was dropped by the old name-only selection.
            SelectedCharacterParts selected = selectCharacterParts(*character, raceCode);
            std::vector<CharacterPart> selectedSkinnedParts = std::move(selected.skinned);
            std::vector<CharacterPart> selectedRawParts = std::move(selected.raw);

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
                modelData->textures = getMergedTexturesShared();  // T504: shared, not copied
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
            modelData->textures = getMergedTexturesShared();  // T504: shared, not copied
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
