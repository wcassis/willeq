// RaceModelLoader core functionality
// Most methods are defined in separate files:
// - model_loading.cpp - S3D file loading methods
// - mesh_building.cpp - Irrlicht mesh construction
// - animated_mesh_creation.cpp - Animated mesh/node creation
// - variant_loading.cpp - Head/body variant loading

#include "client/graphics/eq/race_model_loader.h"
#include "client/graphics/eq/race_codes.h"
#include "client/graphics/eq/geometry_combiner.h"
#include "client/graphics/eq/animation_mapping.h"
#include "client/graphics/eq/dds_decoder.h"
#include "client/graphics/constrained_texture_cache.h"
#include "common/logging.h"
#include "client/core/memory_ledger.h"
#include "common/performance_metrics.h"
#include <algorithm>
#include <cctype>
#include <iostream>
#include <chrono>

namespace EQT {
namespace Graphics {

RaceModelLoader::RaceModelLoader(irr::scene::ISceneManager* smgr,
                                  irr::video::IVideoDriver* driver,
                                  irr::io::IFileSystem* fileSystem)
    : smgr_(smgr), driver_(driver), fileSystem_(fileSystem) {
    meshBuilder_ = std::make_unique<ZoneMeshBuilder>(smgr, driver, fileSystem);
    // P13b: deliberately did NOT tag this builder "entity_perpart" (tried it, reverted —
    // see the plan's Review "Deviations"). This ZoneMeshBuilder's loadTextureFromBMP()
    // is shared by TWO different call patterns: (1) equipment-texture-VARIANT overrides
    // (buildMeshFromGeometry's bodyTextureVariant>0 branch, createAnimatedNodeWithEquipment's
    // texture-swap loop) — pre-existing, unconditional, composite-independent, already
    // landing in "uncategorized" in P13's own baseline ledger; (2) the NEW on-demand
    // per-part load this unit's T05 deferral relies on. Tagging this "entity_perpart"
    // would have reclassified (1) too, making entity_perpart's before/after ledger count
    // look WORSE (confirmed empirically: 268->323 uploads) despite the fix genuinely
    // working — a measurement artifact, not a regression. Leaving the default
    // ("uncategorized", unchanged from P13) keeps the entity_perpart bucket an
    // apples-to-apples comparison against P13's own numbers: it now contains ONLY the
    // three callsites P13's own audit named (T05 eager, loadZoneStep8c_VariantTextures,
    // loadZoneStep8d_EquipmentPrep), so a drop there is unambiguously attributable to
    // fewer eager decodes, not a category relabel.
}

RaceModelLoader::~RaceModelLoader() {
    // Drop cached meshes
    for (auto& [key, mesh] : meshCache_) {
        if (mesh) {
            mesh->drop();
        }
    }
    meshCache_.clear();

    for (auto& [key, mesh] : variantMeshCache_) {
        if (mesh) {
            mesh->drop();
        }
    }
    variantMeshCache_.clear();

    // Drop animated mesh caches
    for (auto& [key, mesh] : animatedMeshCache_) {
        if (mesh) {
            mesh->drop();
        }
    }
    animatedMeshCache_.clear();

    for (auto& [key, mesh] : variantAnimatedMeshCache_) {
        if (mesh) {
            mesh->drop();
        }
    }
    variantAnimatedMeshCache_.clear();

    // P08d: composite mesh caches
    for (auto& [key, mesh] : compositeAnimatedMeshCache_) {
        if (mesh) {
            mesh->drop();
        }
    }
    compositeAnimatedMeshCache_.clear();

    for (auto& [key, mesh] : compositeVariantAnimatedMeshCache_) {
        if (mesh) {
            mesh->drop();
        }
    }
    compositeVariantAnimatedMeshCache_.clear();
}

void RaceModelLoader::setClientPath(const std::string& path) {
    clientPath_ = path;
    if (!clientPath_.empty() && clientPath_.back() != '/' && clientPath_.back() != '\\') {
        clientPath_ += '/';
    }
}

bool RaceModelLoader::hasRaceModel(uint16_t raceId, uint8_t gender) const {
    uint32_t key = makeCacheKey(raceId, gender);
    std::lock_guard<std::mutex> lock(loadedModelsMutex_);
    return loadedModels_.find(key) != loadedModels_.end();
}

std::shared_ptr<RaceModelData> RaceModelLoader::getRaceModelData(uint16_t raceId, uint8_t gender) {
    // T113: called from EntityPrepWorker's background thread
    // (prepVariantTextures()) as well as the main/loading thread —
    // loadedModelsMutex_ makes the lookup safe against concurrent inserts.
    uint32_t key = makeCacheKey(raceId, gender);
    std::lock_guard<std::mutex> lock(loadedModelsMutex_);
    auto it = loadedModels_.find(key);
    if (it != loadedModels_.end()) {
        return it->second;
    }
    return nullptr;
}

void RaceModelLoader::setCurrentZone(const std::string& zoneName) {
    if (zoneName != currentZoneName_) {
        loadZoneModels(zoneName);
    }
}

void RaceModelLoader::setUseOldModels(bool useOld) {
    if (useOldModels_ != useOld) {
        useOldModels_ = useOld;
        LOG_DEBUG(MOD_GRAPHICS, "RaceModelLoader: Switched to {} models", (useOld ? "OLD" : "NEW"));
    }
}

void RaceModelLoader::clearCache() {
    // Drop all cached meshes
    for (auto& [key, mesh] : meshCache_) {
        if (mesh) {
            mesh->drop();
        }
    }
    meshCache_.clear();
    {
        std::lock_guard<std::mutex> lock(loadedModelsMutex_);
        loadedModels_.clear();
    }

    // Also clear variant caches
    for (auto& [key, mesh] : variantMeshCache_) {
        if (mesh) {
            mesh->drop();
        }
    }
    variantMeshCache_.clear();
    {
        std::lock_guard<std::mutex> lock(variantModelsMutex_);
        variantModels_.clear();
    }

    // Clear animated mesh caches (properly drop references)
    for (auto& [key, mesh] : animatedMeshCache_) {
        if (mesh) {
            mesh->drop();
        }
    }
    animatedMeshCache_.clear();
    for (auto& [key, mesh] : variantAnimatedMeshCache_) {
        if (mesh) {
            mesh->drop();
        }
    }
    variantAnimatedMeshCache_.clear();

    // P08d: composite mesh caches
    for (auto& [key, mesh] : compositeAnimatedMeshCache_) {
        if (mesh) {
            mesh->drop();
        }
    }
    compositeAnimatedMeshCache_.clear();
    for (auto& [key, mesh] : compositeVariantAnimatedMeshCache_) {
        if (mesh) {
            mesh->drop();
        }
    }
    compositeVariantAnimatedMeshCache_.clear();

    // Clear cached _chr.s3d files from other zones
    otherChrCaches_.clear();
    chrCacheLruOrder_.clear();

    LOG_DEBUG(MOD_GRAPHICS, "RaceModelLoader: Cache cleared");
}

void RaceModelLoader::clearMeshCaches() {
    // Drop static mesh caches
    for (auto& [key, mesh] : meshCache_) {
        if (mesh) {
            mesh->drop();
        }
    }
    meshCache_.clear();

    for (auto& [key, mesh] : variantMeshCache_) {
        if (mesh) {
            mesh->drop();
        }
    }
    variantMeshCache_.clear();

    // Drop animated mesh caches (EQAnimatedMesh inherits IReferenceCounted)
    for (auto& [key, mesh] : animatedMeshCache_) {
        if (mesh) {
            mesh->drop();
        }
    }
    animatedMeshCache_.clear();

    for (auto& [key, mesh] : variantAnimatedMeshCache_) {
        if (mesh) {
            mesh->drop();
        }
    }
    variantAnimatedMeshCache_.clear();

    // P08d: composite mesh caches
    for (auto& [key, mesh] : compositeAnimatedMeshCache_) {
        if (mesh) {
            mesh->drop();
        }
    }
    compositeAnimatedMeshCache_.clear();

    for (auto& [key, mesh] : compositeVariantAnimatedMeshCache_) {
        if (mesh) {
            mesh->drop();
        }
    }
    compositeVariantAnimatedMeshCache_.clear();

    // P17: this comment used to justify KEEPING meshBuilder_->textureCache_
    // intact ("character textures are never removed during zone
    // transitions") -- that assumption is now FALSE. It was only ever true
    // because ConstrainedTextureCache::clear() (called right after this, by
    // IrrlichtRenderer::unloadZone()) was starved out by a bridge-event
    // ordering race and essentially never fired during a live zone
    // transition (see refactor/P17_eviction_hygiene.md Review) -- so the
    // driver-level textures this map's raw ITexture* pointers reference
    // never actually went away, in practice, before now. With that race
    // fixed, clear() reliably calls driver_->removeTexture() for every
    // cached texture on every zone unload, which invalidates any pointer
    // this map is still holding for a race reused across zone visits.
    // Reproduced live: a race's cached ITexture* read here (via
    // getOrLoadTexture()/hasTexture()) survived the wipe as a dangling
    // pointer, crashing createAnimatedNodeWithEquipment() on
    // currentTex->getName() on the SECOND zone visit that needed the same
    // race (befallen -> qeynos2 -> befallen, 0 evictions logged -- the
    // full-clear path, not eviction pressure). clearTextureCache() already
    // existed for exactly this (textureCache_/pendingTextures_/
    // texturesWithAlpha_) but had no caller anywhere in the codebase before
    // this fix -- calling it here re-derives from meshBuilder_'s own
    // pendingTextures_/raw S3D data or, for names it doesn't have pending
    // data for, forces a clean re-upload through
    // ConstrainedTextureCache::getOrLoad() the next time this race is
    // built, which is always safe (just re-does upload work), unlike
    // leaving a stale pointer in place (which crashes).
    meshBuilder_->clearTextureCache();

    // Keep loadedModels_, variantModels_, global data, S3D archives, skeletons, etc.
    // These are expensive to load and their data is still valid

    LOG_INFO(MOD_GRAPHICS, "RaceModelLoader: Mesh caches cleared for zone transition");
}

std::shared_ptr<RaceModelData> RaceModelLoader::ensureModelDataLoaded(uint16_t raceId, uint8_t gender) {
    eqt::memory::MemoryScope memScope(eqt::memory::Category::EntityModels);  // T491
    uint32_t key = makeCacheKey(raceId, gender);

    // Check if model data is loaded
    auto modelIt = loadedModels_.find(key);
    if (modelIt == loadedModels_.end()) {
        // Try to load the model from various sources
        // Search order depends on useOldModels_ flag

        if (useOldModels_) {
            // Old models mode: Use JSON-configured S3D files for pre-Luclin models
            // Search order:
            // 1. JSON-specified S3D file (from race_models.json)
            // 2. Current zone's _chr.s3d file
            // 3. global_chr.s3d
            // 4. Numbered globals (global2-7_chr.s3d)

            // 1. Try JSON-specified S3D file first
            std::string jsonS3dFile = getRaceS3DFile(raceId);
            if (!jsonS3dFile.empty() && !clientPath_.empty()) {
                // Try to load from the JSON-specified zone chr file
                // Extract zone name from s3d filename (e.g., "qeynos_chr.s3d" -> "qeynos")
                std::string zoneName = jsonS3dFile;
                auto chrPos = zoneName.find("_chr.s3d");
                if (chrPos != std::string::npos) {
                    zoneName = zoneName.substr(0, chrPos);
                    // Check if this is a global file (different loading path)
                    if (zoneName.find("global") == 0) {
                        // It's a global file - try global_chr.s3d or numbered global
                        if (zoneName == "global") {
                            if (loadModelFromGlobalChr(raceId, gender)) {
                                modelIt = loadedModels_.find(key);
                            }
                        } else {
                            // Numbered global (e.g., global4_chr.s3d for Iksar) --
                            // only when the stem is all digits. T267: names like
                            // "globalpcfroglok" (race 330) used to hit stoi() here,
                            // get globalNum 0 and load NOTHING; they are plain
                            // archives and go through the cached-chr path below.
                            std::string stem = zoneName.substr(6);
                            bool numeric = !stem.empty() &&
                                std::all_of(stem.begin(), stem.end(),
                                            [](unsigned char c) { return std::isdigit(c); });
                            int globalNum = numeric ? std::stoi(stem) : 0;
                            if (numeric) {
                                if (globalNum >= 2 && globalNum <= 7) {
                                    if (!numberedGlobalsLoaded_) {
                                        loadNumberedGlobalModels();
                                    }
                                    if (loadModelFromNumberedGlobal(globalNum, raceId, gender)) {
                                        modelIt = loadedModels_.find(key);
                                    }
                                }
                            } else if (loadModelFromCachedChr(jsonS3dFile, raceId, gender)) {
                                modelIt = loadedModels_.find(key);
                            }
                        }
                    } else {
                        // It's a zone chr file - use cached loading to preserve otherChrCaches_
                        if (loadModelFromCachedChr(jsonS3dFile, raceId, gender)) {
                            modelIt = loadedModels_.find(key);
                        }
                    }
                }
            }

            // 2. If not found, try archive index (targeted per-archive load)
            if (modelIt == loadedModels_.end() && graphicsArchiveIndex_) {
                if (loadModelFromArchiveIndex(raceId, gender)) {
                    modelIt = loadedModels_.find(key);
                }
            }

            // 3. If not found, try current zone's _chr.s3d file
            if (modelIt == loadedModels_.end() && !currentZoneName_.empty()) {
                if (loadModelFromZoneChr(currentZoneName_, raceId, gender)) {
                    modelIt = loadedModels_.find(key);
                }
            }

            // 4. If not found in zone, try global_chr.s3d (classic models)
            if (modelIt == loadedModels_.end()) {
                if (loadModelFromGlobalChr(raceId, gender)) {
                    modelIt = loadedModels_.find(key);
                }
            }

            // 5. If still not found, try numbered globals (global2-7_chr.s3d)
            if (modelIt == loadedModels_.end()) {
                if (!numberedGlobalsLoaded_) {
                    loadNumberedGlobalModels();
                }
                for (int num = 2; num <= 7 && modelIt == loadedModels_.end(); ++num) {
                    if (loadModelFromNumberedGlobal(num, raceId, gender)) {
                        modelIt = loadedModels_.find(key);
                    }
                }
            }

            // For old models mode, don't search other zone _chr.s3d files.
            // Classic models should be in JSON-specified files, global_chr.s3d,
            // global2-7_chr.s3d, or the current zone's _chr.s3d.
        } else {
            // New models mode: prefer race-specific S3D files (Luclin+ models)

            // First, try race-specific S3D file (e.g., globalhum_chr.s3d)
            std::string raceFilename = getRaceModelFilename(raceId, gender);
            if (!raceFilename.empty()) {
                std::string racePath = clientPath_ + raceFilename;
                if (loadModelFromS3D(racePath, raceId, gender)) {
                    modelIt = loadedModels_.find(key);
                }
            }

            // If not found, try archive index (targeted per-archive load)
            if (modelIt == loadedModels_.end() && graphicsArchiveIndex_) {
                if (loadModelFromArchiveIndex(raceId, gender)) {
                    modelIt = loadedModels_.find(key);
                }
            }

            // If not found, try zone-specific _chr.s3d file
            if (modelIt == loadedModels_.end() && !currentZoneName_.empty()) {
                if (loadModelFromZoneChr(currentZoneName_, raceId, gender)) {
                    modelIt = loadedModels_.find(key);
                }
            }

            // If not found, search all global archives (global_chr.s3d + global2-7_chr.s3d)
            if (modelIt == loadedModels_.end()) {
                if (searchAllGlobalsForModel(raceId, gender)) {
                    modelIt = loadedModels_.find(key);
                }
            }

            // If still not found, search all other zone _chr.s3d files
            if (modelIt == loadedModels_.end()) {
                if (searchZoneChrFilesForModel(raceId, gender)) {
                    modelIt = loadedModels_.find(key);
                }
            }
        }

        // Still not found
        if (modelIt == loadedModels_.end()) {
            return nullptr;
        }
    }

    return modelIt->second;
}

irr::scene::IMesh* RaceModelLoader::getMeshForRace(uint16_t raceId, uint8_t gender) {
    eqt::memory::MemoryScope memScope(eqt::memory::Category::EntityModels);  // T491
    auto funcStart = std::chrono::steady_clock::now();
    uint32_t key = makeCacheKey(raceId, gender);

    // Check mesh cache first
    auto meshIt = meshCache_.find(key);
    if (meshIt != meshCache_.end()) {
        return meshIt->second;
    }

    // Not cached - this will involve actual loading
    auto loadStart = std::chrono::steady_clock::now();

    // P13b: model-data search/load extracted to ensureModelDataLoaded() so
    // getAnimatedMeshForRace() (animated_mesh_creation.cpp) can ensure a race's
    // model data is loaded WITHOUT also paying for buildTexturedMesh()'s eager,
    // unconditional per-part texture decode/upload below — that call previously
    // ran as an unwanted side effect of getMeshForRace()'s null-check-only usage
    // there, silently defeating the T05 composite-hit deferral (preloadModelData
    // would skip decoding a composite-hit race's per-part set, but this static
    // mesh build — called first thing inside getAnimatedMeshForRace, for EVERY
    // entity, eligible or not — would immediately decode+upload the very same
    // set anyway, just tagged differently). Found and fixed while validating
    // this unit's own ledger acceptance evidence; see the plan Review.
    auto modelData = ensureModelDataLoaded(raceId, gender);

    // Build mesh from model data
    if (!modelData || !modelData->combinedGeometry) {
        meshCache_[key] = nullptr;
        return nullptr;
    }

    irr::scene::IMesh* mesh = nullptr;
    if (!modelData->textures.empty() && !modelData->combinedGeometry->textureNames().empty()) {
        mesh = meshBuilder_->buildTexturedMesh(*modelData->combinedGeometry, modelData->textures, true);
    } else {
        mesh = meshBuilder_->buildColoredMesh(*modelData->combinedGeometry);
    }

    // Log slow model loads
    auto loadEnd = std::chrono::steady_clock::now();
    auto loadMs = std::chrono::duration_cast<std::chrono::milliseconds>(loadEnd - loadStart).count();
    if (loadMs > 50) {
        LOG_WARN(MOD_GRAPHICS, "PERF: getMeshForRace race={} took {} ms (not cached)", raceId, loadMs);
        EQT::PerformanceMetrics::instance().recordSample("Slow Model Load", loadMs);
    }

    meshCache_[key] = mesh;
    return mesh;
}

bool RaceModelLoader::isModelDataCached(uint32_t cacheKey) const {
    // Check main cache — T113: this is called from EntityPrepWorker's
    // background thread (processRequest()), so loadedModels_ needs
    // loadedModelsMutex_ here even though most writers are main-thread.
    {
        std::lock_guard<std::mutex> lock(loadedModelsMutex_);
        if (loadedModels_.count(cacheKey) > 0) return true;
    }
    // Check staging cache (shared with background thread)
    std::lock_guard<std::mutex> lock(preparedDataMutex_);
    return preparedModelData_.count(cacheKey) > 0;
}

bool RaceModelLoader::isRaceModelKnownMissing(uint32_t cacheKey) const {
    std::lock_guard<std::mutex> lock(preparedDataMutex_);
    return knownMissingRaceGenders_.count(cacheKey) > 0;
}

void RaceModelLoader::promotePreparedModels() {
    eqt::memory::MemoryScope memScope(eqt::memory::Category::EntityModels);  // T491
    std::lock_guard<std::mutex> lock(preparedDataMutex_);
    if (preparedModelData_.empty()) return;
    {
        // T113: loadedModelsMutex_ nested inside preparedDataMutex_ — always
        // acquired in this order, never the reverse, so no deadlock risk.
        std::lock_guard<std::mutex> loadedLock(loadedModelsMutex_);
        for (auto& [key, data] : preparedModelData_) {
            if (loadedModels_.count(key) == 0) {
                loadedModels_[key] = std::move(data);
                LOG_DEBUG(MOD_GRAPHICS, "RaceModelLoader: promoted preloaded model key={} to main cache", key);
            }
        }
    }
    preparedModelData_.clear();
}

std::shared_ptr<RaceModelData> RaceModelLoader::buildModelDataFromCharacters(
        const std::vector<std::shared_ptr<CharacterModel>>& characters,
        const std::map<std::string, std::shared_ptr<TextureInfo>>& textures,
        uint16_t raceId, uint8_t gender) const {

    std::string baseRaceCode = getRaceCode(raceId);
    if (baseRaceCode.empty()) {
        LOG_DEBUG(MOD_GRAPHICS, "buildModelDataFromCharacters: race={} has empty raceCode, returning nullptr", raceId);
        return nullptr;
    }

    std::string raceCode = getGenderedRaceCode(baseRaceCode, gender);
    std::transform(raceCode.begin(), raceCode.end(), raceCode.begin(),
                   [](unsigned char c) { return std::toupper(c); });

    // Build fallback codes list
    std::vector<std::string> codesToTry = { raceCode };
    std::string fallbackCode = getFallbackRaceCode(raceId, gender);
    if (!fallbackCode.empty()) {
        std::transform(fallbackCode.begin(), fallbackCode.end(), fallbackCode.begin(),
                       [](unsigned char c) { return std::toupper(c); });
        if (fallbackCode != raceCode) {
            codesToTry.push_back(fallbackCode);
        }
    }

    LOG_DEBUG(MOD_GRAPHICS, "buildModelDataFromCharacters: race={} gender={} raceCode='{}' codesToTry={} characters={}",
              raceId, gender, raceCode, codesToTry.size(), characters.size());

    for (const auto& codeToTry : codesToTry) {
        for (const auto& character : characters) {
            if (!character || character->parts.empty()) continue;

            std::string charName = character->name;
            std::transform(charName.begin(), charName.end(), charName.begin(),
                           [](unsigned char c) { return std::toupper(c); });

            if (charName.find(codeToTry) == std::string::npos) continue;

            LOG_DEBUG(MOD_GRAPHICS, "buildModelDataFromCharacters: race={} MATCHED character='{}' code='{}' parts={} partsWithTransforms={} rawParts={}",
                      raceId, character->name, codeToTry, character->parts.size(),
                      character->partsWithTransforms.size(), character->rawParts.size());

            // Found a match — select default body and head parts
            std::string defaultBodyName = codeToTry + "_DMSPRITEDEF";
            std::string defaultHeadName = codeToTry + "HE00_DMSPRITEDEF";
            LOG_DEBUG(MOD_GRAPHICS, "buildModelDataFromCharacters: looking for body='{}' head='{}'", defaultBodyName, defaultHeadName);

            std::vector<CharacterPart> selectedSkinnedParts;
            for (const auto& part : character->partsWithTransforms) {
                if (!part.geometry) continue;
                std::string partName = part.geometry->name;
                std::transform(partName.begin(), partName.end(), partName.begin(),
                               [](unsigned char c) { return std::toupper(c); });
                LOG_TRACE(MOD_GRAPHICS, "  skinned candidate: '{}' materialData={} textureNames={}",
                          partName, (bool)part.geometry->materialData, part.geometry->textureNames().size());
                if (partName == defaultBodyName || partName == defaultHeadName) {
                    selectedSkinnedParts.push_back(part);
                    LOG_DEBUG(MOD_GRAPHICS, "  SELECTED skinned: '{}' materialData={} textureNames={}",
                              partName, (bool)part.geometry->materialData, part.geometry->textureNames().size());
                }
            }

            std::vector<CharacterPart> selectedRawParts;
            for (const auto& part : character->rawParts) {
                if (!part.geometry) continue;
                std::string partName = part.geometry->name;
                std::transform(partName.begin(), partName.end(), partName.begin(),
                               [](unsigned char c) { return std::toupper(c); });
                if (partName == defaultBodyName || partName == defaultHeadName) {
                    selectedRawParts.push_back(part);
                    LOG_DEBUG(MOD_GRAPHICS, "  SELECTED raw: '{}' materialData={} textureNames={}",
                              partName, (bool)part.geometry->materialData, part.geometry->textureNames().size());
                }
            }

            if (selectedSkinnedParts.empty()) {
                LOG_DEBUG(MOD_GRAPHICS, "buildModelDataFromCharacters: no body/head match, using ALL {} partsWithTransforms and {} rawParts",
                          character->partsWithTransforms.size(), character->rawParts.size());
                selectedSkinnedParts = character->partsWithTransforms;
                selectedRawParts = character->rawParts;
                // Log what we're using as fallback
                for (size_t pi = 0; pi < selectedSkinnedParts.size(); ++pi) {
                    if (selectedSkinnedParts[pi].geometry) {
                        LOG_DEBUG(MOD_GRAPHICS, "  fallback skinned[{}]: '{}' materialData={} textureNames={}",
                                  pi, selectedSkinnedParts[pi].geometry->name,
                                  (bool)selectedSkinnedParts[pi].geometry->materialData,
                                  selectedSkinnedParts[pi].geometry->textureNames().size());
                    }
                }
            } else {
                LOG_DEBUG(MOD_GRAPHICS, "buildModelDataFromCharacters: selected {} skinned parts, {} raw parts",
                          selectedSkinnedParts.size(), selectedRawParts.size());
            }

            auto combinedGeom = combineCharacterPartsWithTransforms(selectedSkinnedParts);
            auto rawGeom = combineCharacterPartsRaw(selectedRawParts);

            LOG_DEBUG(MOD_GRAPHICS, "buildModelDataFromCharacters: combinedGeom={} (textureNames={}) rawGeom={}",
                      (bool)combinedGeom,
                      combinedGeom ? combinedGeom->textureNames().size() : 0,
                      (bool)rawGeom);

            if (!combinedGeom) {
                LOG_DEBUG(MOD_GRAPHICS, "buildModelDataFromCharacters: combinedGeom is null, continuing search");
                continue;
            }

            auto modelData = std::make_shared<RaceModelData>();
            modelData->combinedGeometry = combinedGeom;
            modelData->rawGeometry = rawGeom;
            modelData->textures = textures;
            modelData->raceName = character->name;
            modelData->raceId = raceId;
            modelData->gender = gender;
            modelData->scale = getRaceScale(raceId);

            if (character->animatedSkeleton) {
                modelData->skeleton = character->animatedSkeleton;
            }
            if (rawGeom && !rawGeom->vertexPieces.empty()) {
                modelData->vertexPieces = rawGeom->vertexPieces;
            }

            LOG_DEBUG(MOD_GRAPHICS, "RaceModelLoader::buildModelDataFromCharacters: race={} code={} vertices={} anims={} vpieces={}",
                      raceId, codeToTry, combinedGeom->vertices.size(),
                      modelData->skeleton ? modelData->skeleton->animations.size() : 0,
                      rawGeom ? rawGeom->vertexPieces.size() : 0);
            return modelData;
        }
    }
    return nullptr;
}

RaceModelLoader::CharacterList RaceModelLoader::snapshotZoneCharacters() const {
    std::lock_guard<std::mutex> lock(sourceStoresMutex_);
    return zoneCharacters_;
}
RaceModelLoader::CharacterList RaceModelLoader::snapshotGlobalCharacters() const {
    std::lock_guard<std::mutex> lock(sourceStoresMutex_);
    return globalCharacters_;
}
RaceModelLoader::CharacterList RaceModelLoader::snapshotNumberedGlobalCharacters(int num) const {
    std::lock_guard<std::mutex> lock(sourceStoresMutex_);
    auto it = numberedGlobalCharacters_.find(num);
    return it == numberedGlobalCharacters_.end() ? CharacterList{} : it->second;
}

bool RaceModelLoader::adoptGlobalChrIntoOtherCacheLocked() {
    if (otherChrCaches_.find("global_chr.s3d") != otherChrCaches_.end()) return true;
    if (!globalModelsLoaded_.load(std::memory_order_acquire)) return false;
    const CharacterList globals = snapshotGlobalCharacters();   // T504b Leg 2: under sourceStoresMutex_
    if (globals.empty()) return false;
    storeOtherChrCacheLocked("global_chr.s3d", globals, globalTextures_);
    LOG_INFO(MOD_GRAPHICS, "RaceModelLoader: other-chr cache adopted {} global_chr.s3d characters from the global store (no second parse)",
             globals.size());
    return otherChrCaches_.find("global_chr.s3d") != otherChrCaches_.end();
}

void RaceModelLoader::storeOtherChrCacheLocked(const std::string& lowerFilename,
                                                std::vector<std::shared_ptr<CharacterModel>> characters,
                                                std::map<std::string, std::shared_ptr<TextureInfo>> textures) {
    OtherChrCache& cache = otherChrCaches_[lowerFilename];
    cache.characters = std::move(characters);
    cache.textures = std::move(textures);
    releaseBytesIfEqe1Authoritative(cache.textures);  // T504: see the header
    mergedTexturesCacheValid_ = false;

    if (maxChrCacheEntries_ > 0) {
        chrCacheLruOrder_.push_front(lowerFilename);
        while (otherChrCaches_.size() > maxChrCacheEntries_ &&
               chrCacheLruOrder_.size() > 1) {
            const std::string& oldest = chrCacheLruOrder_.back();
            otherChrCaches_.erase(oldest);
            chrCacheLruOrder_.pop_back();
            mergedTexturesCacheValid_ = false;
        }
    }
}

RaceModelLoader::ChrCacheAccounting RaceModelLoader::getChrCacheAccountingForTest() const {
    ChrCacheAccounting out;
    std::lock_guard<std::mutex> lock(otherChrCacheMutex_);
    out.entries = otherChrCaches_.size();
    out.lruOrder = chrCacheLruOrder_.size();
    out.maxEntries = maxChrCacheEntries_;
    for (const auto& [filename, cache] : otherChrCaches_) {
        (void)cache;
        if (std::find(chrCacheLruOrder_.begin(), chrCacheLruOrder_.end(), filename) ==
            chrCacheLruOrder_.end()) {
            ++out.untracked;
        }
    }
    return out;
}

bool RaceModelLoader::preloadModelData(uint16_t raceId, uint8_t gender, int textureVariant) {
    eqt::memory::MemoryScope memScope(eqt::memory::Category::EntityModels);  // T491
    uint32_t key = makeCacheKey(raceId, gender);

    // Check staging cache (thread-safe). If already staged or will be promoted,
    // no need to redo the work. Note: we do NOT read loadedModels_ here because
    // that map is main-thread-only (no lock). The main thread's
    // promotePreparedModels() flow ensures no redundant work.
    {
        std::lock_guard<std::mutex> lock(preparedDataMutex_);
        if (preparedModelData_.count(key) > 0) return true;

        // T94: negative-result memo. A prior call for this exact (raceId,
        // gender) already walked the full search chain below (steps 1-5)
        // and found nothing — the S3D archives are static for the process
        // lifetime, so that result is permanent and the search does not
        // need to be repeated. Just bump the hit counter (for the
        // once-per-race summary log) and return immediately, skipping the
        // O(archives x characters-per-archive) string search entirely.
        auto missIt = knownMissingRaceGenders_.find(key);
        if (missIt != knownMissingRaceGenders_.end()) {
            ++missIt->second;
            return false;
        }
    }

    // Collect textures from safe sources for model building.
    // zoneTextures_ is immutable after zone init (safe to read from bg thread).
    std::map<std::string, std::shared_ptr<TextureInfo>> mergedTextures = zoneTextures_;

    std::shared_ptr<RaceModelData> modelData;

    // Helper lambda: load an S3D archive into otherChrCaches_ (or use cached copy)
    // and search for the race model. Respects maxChrCacheEntries_ LRU eviction.
    // Returns model data if found, nullptr otherwise. Merges textures into mergedTextures.
    auto tryLoadFromArchive = [&](const std::string& s3dPath) -> std::shared_ptr<RaceModelData> {
        // Extract filename for cache key
        std::string filename = s3dPath;
        auto lastSlash = filename.rfind('/');
        if (lastSlash != std::string::npos) {
            filename = filename.substr(lastSlash + 1);
        }
        std::string lowerFilename = filename;
        std::transform(lowerFilename.begin(), lowerFilename.end(), lowerFilename.begin(),
                       [](unsigned char c) { return std::tolower(c); });

        std::lock_guard<std::mutex> chrLock(otherChrCacheMutex_);

        auto cacheIt = otherChrCaches_.find(lowerFilename);
        if (cacheIt == otherChrCaches_.end() && lowerFilename == "global_chr.s3d"
            && adoptGlobalChrIntoOtherCacheLocked())
            cacheIt = otherChrCaches_.find(lowerFilename);   // T504b Leg 1 (5th site)
        if (cacheIt != otherChrCaches_.end()) {
            LOG_INFO(MOD_GRAPHICS, "RaceModelLoader::preloadModelData: archive cache HIT for {} (race={} gender={})",
                     lowerFilename, raceId, gender);
        } else {
            // Not cached — load from disk and store
            LOG_INFO(MOD_GRAPHICS, "RaceModelLoader::preloadModelData: archive cache MISS for {} — loading from disk (race={} gender={})",
                     lowerFilename, raceId, gender);
            S3DLoader loader;
            if (!loader.loadZone(s3dPath)) {
                // T91: route the "poisoned" empty entry through the same
                // LRU-tracked path as a real load, so a permanently-failing
                // filename occupies (and can eventually give up) a counted
                // cache slot instead of squatting outside maxChrCacheEntries_
                // forever (it used to bypass registration entirely here).
                storeOtherChrCacheLocked(lowerFilename, {}, {});
                return nullptr;
            }
            auto zone = loader.getZone();
            if (!zone || zone->characters.empty()) {
                storeOtherChrCacheLocked(lowerFilename, {}, {});
                return nullptr;
            }

            storeOtherChrCacheLocked(lowerFilename, zone->characters, zone->characterTextures);
            cacheIt = otherChrCaches_.find(lowerFilename);

            LOG_DEBUG(MOD_GRAPHICS, "RaceModelLoader::preloadModelData: cached {} characters from {} (total cached: {})",
                      cacheIt != otherChrCaches_.end() ? cacheIt->second.characters.size() : 0,
                      lowerFilename, otherChrCaches_.size());
        }

        if (cacheIt == otherChrCaches_.end() || cacheIt->second.characters.empty()) {
            return nullptr;
        }

        // Merge archive textures with existing textures (existing take priority)
        auto diskTextures = cacheIt->second.textures;
        for (const auto& [name, tex] : mergedTextures) {
            diskTextures[name] = tex;  // Existing textures override
        }
        auto result = buildModelDataFromCharacters(cacheIt->second.characters, diskTextures, raceId, gender);
        if (result) {
            // Add new textures from this archive to our merged set
            for (const auto& [name, tex] : cacheIt->second.textures) {
                if (mergedTextures.find(name) == mergedTextures.end()) {
                    mergedTextures[name] = tex;
                }
            }
        }
        return result;
    };

    // 1. Search the zone characters (T504b Leg 2: a snapshot, the store may be pruned)
    const CharacterList zoneSnap = snapshotZoneCharacters();
    if (!modelData && !zoneSnap.empty()) {
        modelData = buildModelDataFromCharacters(zoneSnap, mergedTextures, raceId, gender);
    }

    // 2. Search otherChrCaches_ (mutex-protected — safe)
    if (!modelData) {
        std::lock_guard<std::mutex> chrLock(otherChrCacheMutex_);
        for (const auto& [filename, cache] : otherChrCaches_) {
            // Merge archive textures with existing textures (existing take priority)
            // — same merge logic as tryLoadFromArchive
            auto diskTextures = cache.textures;
            for (const auto& [name, tex] : mergedTextures) {
                diskTextures[name] = tex;
            }
            modelData = buildModelDataFromCharacters(cache.characters, diskTextures, raceId, gender);
            if (modelData) {
                LOG_DEBUG(MOD_GRAPHICS, "RaceModelLoader::preloadModelData: found race={} in cached archive '{}' (mergedTextures={} archiveTextures={} diskTextures={})",
                          raceId, filename, mergedTextures.size(), cache.textures.size(), diskTextures.size());
                // Add new textures from this archive to our merged set
                for (const auto& [name, tex] : cache.textures) {
                    if (mergedTextures.find(name) == mergedTextures.end()) {
                        mergedTextures[name] = tex;
                    }
                }
                break;
            }
        }
    }

    // 3. Try JSON-specified S3D file (cached in otherChrCaches_)
    if (!modelData && !clientPath_.empty()) {
        std::string jsonS3dFile = getRaceS3DFile(raceId);
        if (!jsonS3dFile.empty()) {
            modelData = tryLoadFromArchive(clientPath_ + jsonS3dFile);
        }
    }

    // 4. Try global_chr.s3d (cached in otherChrCaches_)
    if (!modelData && !clientPath_.empty()) {
        modelData = tryLoadFromArchive(clientPath_ + "global_chr.s3d");
    }

    // 5. Try global2-7_chr.s3d (cached in otherChrCaches_)
    if (!modelData && !clientPath_.empty()) {
        for (int num = 2; num <= 7 && !modelData; ++num) {
            modelData = tryLoadFromArchive(clientPath_ + "global" + std::to_string(num) + "_chr.s3d");
        }
    }

    if (!modelData) {
        // T94: record the negative result permanently and log it exactly
        // once per race/gender — this is the search's first (and only)
        // failure for this key; every later entity of this race/gender
        // takes the early-out above instead of reaching here. Replaces
        // what used to be per-entity noise (one LOG_INFO from every call
        // site, for every entity, proportional to entity count — T91
        // measured 130 such lines for Nektulos) with a single line stating
        // how many entities are affected once the count is known (the
        // zone-load summary in renderer_zone_load.cpp already aggregates
        // this via skippedRaceGenderPairs/entitiesSkipped; this WARN is the
        // "how do I even know race=457-class gaps exist" signal the plan
        // asked for, visible without cross-referencing that summary).
        {
            std::lock_guard<std::mutex> lock(preparedDataMutex_);
            knownMissingRaceGenders_.emplace(key, 1);
        }
        LOG_WARN(MOD_GRAPHICS, "RaceModelLoader::preloadModelData: race={} gender={} has no model in any S3D archive "
                 "(zoneCharacters_, otherChrCaches_, JSON-specified S3D, global_chr.s3d, global2-7_chr.s3d all searched) — "
                 "caching this negative result permanently (S3D archives are static for the process lifetime); "
                 "further entities of this race/gender skip the search and render as placeholder cubes",
                 raceId, gender);
        return false;
    }

    // Animation merge: if this race shares animations with another (e.g., DWF uses HUM anims),
    // search the cached global_chr.s3d archive for the animation source character.
    if (modelData->skeleton) {
        std::string raceCode = getRaceCode(raceId);
        raceCode = getGenderedRaceCode(raceCode, gender);
        std::string animSourceCode = getAnimationSourceCode(raceCode);

        if (!animSourceCode.empty() && animSourceCode != raceCode) {
            // Search otherChrCaches_ for global_chr.s3d (already loaded by tryLoadFromArchive above).
            // If not cached (e.g., model came from zone_chr.s3d), load it now.
            std::lock_guard<std::mutex> chrLock(otherChrCacheMutex_);
            std::string globalKey = "global_chr.s3d";
            auto cacheIt = otherChrCaches_.find(globalKey);
            if (cacheIt == otherChrCaches_.end() && adoptGlobalChrIntoOtherCacheLocked())
                cacheIt = otherChrCaches_.find(globalKey);   // T504b Leg 1
            if (cacheIt != otherChrCaches_.end()) {
                LOG_INFO(MOD_GRAPHICS, "RaceModelLoader: animation merge using cached global_chr.s3d for race={}", raceId);
            } else if (!clientPath_.empty()) {
                LOG_INFO(MOD_GRAPHICS, "RaceModelLoader: animation merge loading global_chr.s3d from disk for race={}", raceId);
                // Load and cache global_chr.s3d for animation data
                S3DLoader animLoader;
                if (animLoader.loadZone(clientPath_ + globalKey)) {
                    auto animZone = animLoader.getZone();
                    if (animZone && !animZone->characters.empty()) {
                        storeOtherChrCacheLocked(globalKey, animZone->characters, animZone->characterTextures);
                    }
                }
                cacheIt = otherChrCaches_.find(globalKey);
            }

            if (cacheIt != otherChrCaches_.end() && !cacheIt->second.characters.empty()) {
                for (const auto& sourceChar : cacheIt->second.characters) {
                    if (!sourceChar) continue;
                    std::string sourceName = sourceChar->name;
                    std::transform(sourceName.begin(), sourceName.end(), sourceName.begin(),
                                   [](unsigned char c) { return std::toupper(c); });

                    if (sourceName.find(animSourceCode) == std::string::npos) continue;
                    if (!sourceChar->animatedSkeleton || sourceChar->animatedSkeleton->animations.empty()) continue;

                    auto& ourSkel = modelData->skeleton;
                    auto& sourceSkel = sourceChar->animatedSkeleton;

                    std::string lowerCode = raceCode;
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

                    LOG_DEBUG(MOD_GRAPHICS, "RaceModelLoader::preloadModelData: merged {} animations from {} for race={}",
                              addedAnimations, animSourceCode, raceId);
                    break;
                }
            }
        }
    }

    // Composite texture lookup: check if a composite JSON exists for this race/gender.
    // The matching EQE1 file is already loaded as an EQE1 loader (opened during zone load
    // from the composite directory). The constrained cache will find the composite texture
    // via findCompressedEntry() using the EQE1 entry name.
    if (!compositeEntityPath_.empty() && modelData->combinedGeometry) {
        std::string modelName = modelData->raceName;
        std::transform(modelName.begin(), modelName.end(), modelName.begin(),
                       [](unsigned char c) { return std::tolower(c); });
        // entity_composite_builder output: {modelname}_{gender}_{variant}.json
        // The model name already encodes gender (e.g., HUM vs HUF), so the
        // composite builder always writes gender=0 in the filename. Use _0_ here.
        std::string compositeBaseName = modelName + "_0_" + std::to_string(textureVariant);
        std::string jsonPath = compositeEntityPath_ + "/" + compositeBaseName + ".json";
        auto compositeData = std::make_shared<CompositeTextureData>();
        if (compositeData->loadFromJson(jsonPath)) {
            // P37: composite sheets originally carried NO alpha data in either
            // deployed form (entity_composite_builder.cpp hardcoded hasAlpha=0
            // for the ETC1 sheet and wrote a 24bpp-only BMP) — a WLD-masked
            // source texture baked into a composite would silently render fully
            // opaque with no way to alpha-test it. P37's fix excluded any
            // race/gender/variant whose composite included a WLD-masked source
            // part from composite eligibility entirely, falling through to the
            // per-part buildMeshFromGeometry() path (T05 decode loop), which
            // honors textureMasked().
            //
            // P48: replaces that BLANKET exclusion with a sheet-CAPABILITY gate.
            // entity_composite_builder.cpp can now emit a real ETC1 alpha
            // companion page for a sheet whenever it contains WLD-masked source
            // parts (mirrors the zone ETC1 companion convention); the JSON's
            // "hasAlpha" key (CompositeTextureData::hasAlpha) records whether
            // THIS specific sheet actually carries one. A masked composite is
            // excluded only when the sheet does NOT have that companion —
            // i.e. it's a stale sheet built by a pre-P48 builder (or a global
            // sheet not yet regenerated) — same safety net P37 established,
            // now scoped to the sheets that actually need it instead of every
            // masked race unconditionally. When compositeData->hasAlpha is
            // true, mesh_building.cpp's buildCompositeMeshFromGeometry() marks
            // the composite buffer EMT_TRANSPARENT_ALPHA_CHANNEL_REF, which
            // entity_renderer.cpp's existing production promotion path (the
            // same one the per-part path already uses — P46b: never fork it)
            // picks up automatically, binding the companion on unit 1.
            bool compositeHasMaskedTexture = false;
            std::string maskedTexName;
            if (modelData->combinedGeometry) {
                const auto& texNames = modelData->combinedGeometry->textureNames();
                const auto& masked = modelData->combinedGeometry->textureMasked();
                for (size_t i = 0; i < texNames.size(); ++i) {
                    if (i < masked.size() && masked[i] && compositeData->lookup(texNames[i])) {
                        compositeHasMaskedTexture = true;
                        maskedTexName = texNames[i];
                        break;
                    }
                }
            }
            if (compositeHasMaskedTexture && !compositeData->hasAlpha) {
                LOG_INFO(MOD_GRAPHICS, "RaceModelLoader::preloadModelData: composite EXCLUDED for race={} gender={} ('{}') — "
                         "source includes WLD-masked texture '{}' and this sheet has no alpha companion (P37/P48 gate); "
                         "falling back to the per-part path",
                         raceId, gender, compositeBaseName, maskedTexName);
            } else {
                if (compositeHasMaskedTexture) {
                    LOG_INFO(MOD_GRAPHICS, "RaceModelLoader::preloadModelData: composite ADMITTED for race={} gender={} ('{}') — "
                             "source includes WLD-masked texture '{}' but this sheet carries an alpha companion (P48); "
                             "composite eligible with alpha-test",
                             raceId, gender, compositeBaseName, maskedTexName);
                }
                modelData->compositeData = compositeData;
                // Store the EQE1 texture name (matches the entry name inside the .eqe1 file)
                modelData->compositeBmpPath = compositeBaseName;
                LOG_INFO(MOD_GRAPHICS, "RaceModelLoader::preloadModelData: composite found for race={} gender={}: '{}' ({} textures, {}x{})",
                         raceId, gender, compositeBaseName,
                         compositeData->textures.size(), compositeData->sheetWidth, compositeData->sheetHeight);

                // Trigger EQE1 lookup so the composite texture gets queued for GPU upload.
                // getOrLoad() will find it via findCompressedEntry() in the composite EQE1 loader.
                // T357 (B014): the EQE1 path is the ONLY thing that can supply
                // bytes for this name -- the buffer below is deliberately
                // empty -- so with no EQE1 tree configured the call is
                // guaranteed to fail, once per composite name, and lands in
                // ConstrainedTextureCache's "empty input data" warn (the T92
                // diagnostic that named this exact caller). Gate it on the
                // same predicate every other EQE1 site in the tree uses:
                // renderer_zone_load.cpp's "Step 0: EQE1 ... (if configured)",
                // renderer_init.cpp and eqe1_archive_component.cpp all read
                // constrainedConfig.eqe1Path. This is the cache's own copy of
                // that config, so no new plumbing and no second predicate.
                //
                // NOT gated on the loader set being non-empty: T284/T207 open
                // packs lazily, and a composite sheet's pack is deliberately
                // still closed here -- the miss is what drives
                // eqe1ArchiveResolver_ -> openArchiveContaining(). "Configured"
                // is the question; "already open" is not.
                if (constrainedTextureCache_ &&
                    !constrainedTextureCache_->getConfig().eqe1Path.empty()) {
                    std::vector<char> emptyData;  // No raw data — EQE1 lookup provides the data
                    // P13: tagged "composite_sheet" — see the T05 loop just below, which
                    // (confirmed during the P13 audit) unconditionally ALSO queues every
                    // individual per-part texture for this race regardless of whether this
                    // composite lookup just succeeded — the double-load the P13 plan asked
                    // to test for. Not fixed here (P13 is report-only); flagged in its Review.
                    constrainedTextureCache_->getOrLoad(compositeBaseName, emptyData,
                                                         "composite_sheet", "preloadModelData:compositeLookup");
                }
            }
        } else {
            LOG_DEBUG(MOD_GRAPHICS, "RaceModelLoader::preloadModelData: no composite at {}", jsonPath);
        }
    }

    // T05: Decode textures via constrained cache (single decoder path).
    // getOrLoad() decodes raw bytes to RGBA and stores as DecodedEntry.
    // uploadTexture() promotes to GPU later (on the loading thread or render thread).
    //
    // P13b: on a composite-sheet HIT (modelData->compositeData just got set above),
    // DEFER this decode/upload instead of running it unconditionally — this is the
    // fix for the double-load the P13 audit confirmed (entity_perpart upload count
    // was identical, 268, whether composites were on or off: every composite-eligible
    // race's per-part textures were being decoded+uploaded ANYWAY, on top of the
    // sheet). Per-ENTITY eligibility (P08d: players and per-slot-override NPCs are
    // ALWAYS ineligible and need the individual per-part textures for their
    // multi-buffer mesh) means we cannot just skip these forever — record the
    // texture names in deferredPerPartTextureNames_ so a later ineligible entity of
    // this race can still load them ON DEMAND. No GPU touch, no decode here at all.
    if (modelData->compositeData) {
        size_t newlyDeferred = 0;
        size_t alreadyKnown = 0;
        if (modelData->combinedGeometry) {
            std::lock_guard<std::mutex> lock(preparedDataMutex_);
            for (const auto& texName : modelData->combinedGeometry->textureNames()) {
                std::string lowerName = texName;
                std::transform(lowerName.begin(), lowerName.end(), lowerName.begin(),
                               [](unsigned char c) { return std::tolower(c); });
                if (modelData->textures.count(lowerName) == 0) {
                    continue;  // Not actually decodable (matches T05's own notFound skip)
                }
                if (deferredPerPartTextureNames_.insert(lowerName).second) {
                    ++newlyDeferred;
                } else {
                    ++alreadyKnown;
                }
            }
        }
        LOG_INFO(MOD_GRAPHICS, "RaceModelLoader::preloadModelData: composite sheet hit for race={} gender={} — "
                 "deferred {} per-part texture(s) ({} already known from a prior race/gender) instead of eager "
                 "decode/upload; loaded on demand only if an ineligible entity of this race needs the multi-buffer path",
                 raceId, gender, newlyDeferred, alreadyKnown);
    } else if (modelData->combinedGeometry && !modelData->textures.empty() && constrainedTextureCache_) {
        LOG_DEBUG(MOD_GRAPHICS, "RaceModelLoader::preloadModelData: T05 decode loop for race={} gender={} — combinedGeometry textureNames={} textures map size={}",
                  raceId, gender, modelData->combinedGeometry->textureNames().size(), modelData->textures.size());
        size_t decoded = 0;
        size_t notFound = 0;
        size_t nullTex = 0;
        size_t emptyData = 0;
        const auto& combinedTextureNames = modelData->combinedGeometry->textureNames();
        const auto& combinedTextureMasked = modelData->combinedGeometry->textureMasked();
        for (size_t texIdx = 0; texIdx < combinedTextureNames.size(); ++texIdx) {
            const std::string& texName = combinedTextureNames[texIdx];
            std::string lowerName = texName;
            std::transform(lowerName.begin(), lowerName.end(), lowerName.begin(),
                           [](unsigned char c) { return std::tolower(c); });
            auto texIt = modelData->textures.find(lowerName);
            if (texIt == modelData->textures.end()) {
                LOG_DEBUG(MOD_GRAPHICS, "  T05: texName='{}' NOT FOUND in textures map (size={})", lowerName, modelData->textures.size());
                notFound++;
                continue;
            }
            if (!texIt->second) {
                LOG_DEBUG(MOD_GRAPHICS, "  T05: texName='{}' found but TextureInfo is null", lowerName);
                nullTex++;
                continue;
            }
            const auto& rawData = texIt->second->data;
            // T504: bytes released early (EQE1 serves the name) is not "empty".
            if (!texIt->second->present()) {
                LOG_DEBUG(MOD_GRAPHICS, "  T05: texName='{}' found but data is empty", lowerName);
                emptyData++;
                continue;
            }

            LOG_TRACE(MOD_GRAPHICS, "  T05: decoding texName='{}' ({} bytes)", lowerName, rawData.size());
            // P13/P13b: tagged "entity_perpart". Only reached when this race/gender did
            // NOT get a composite-sheet hit this preload call (see the
            // modelData->compositeData branch above) — composite MISS or composites off,
            // so this is the same eager, unconditional-per-race decode P13's audit
            // measured (behavior byte-identical to before P13b for every race that
            // reaches this branch).
            // P46: applyPaletteMask gated on this exact texture's WLD-derived
            // textureMasked() flag (same parallel-array signal
            // mesh_building.cpp's buildMeshFromGeometry() and this class's
            // own compositeHasMaskedTexture check above already use) — only
            // WLD-masked per-part entity textures get palette-index-0
            // punched transparent; every unmasked sibling texture decodes
            // exactly as before.
            bool isMaskedMaterial = texIdx < combinedTextureMasked.size() && combinedTextureMasked[texIdx];
            constrainedTextureCache_->getOrLoad(lowerName, rawData, "entity_perpart", "preloadModelData:T05",
                                                 isMaskedMaterial);
            ++decoded;
        }
        LOG_DEBUG(MOD_GRAPHICS, "RaceModelLoader::preloadModelData: decoded {} textures for race={} gender={} (notFound={} null={} empty={})",
                  decoded, raceId, gender, notFound, nullTex, emptyData);
    } else if (modelData->combinedGeometry && !modelData->textures.empty() && !constrainedTextureCache_) {
        LOG_WARN(MOD_GRAPHICS, "RaceModelLoader::preloadModelData: no constrained cache, skipping {} textures for race={} gender={}",
                 modelData->textures.size(), raceId, gender);
    } else {
        LOG_DEBUG(MOD_GRAPHICS, "RaceModelLoader::preloadModelData: no textures to decode (geometry={}, textures={}, cache={}) for race={} gender={}",
                  (bool)modelData->combinedGeometry, modelData->textures.size(), (bool)constrainedTextureCache_, raceId, gender);
    }

    // Store in staging cache
    {
        std::lock_guard<std::mutex> lock(preparedDataMutex_);
        preparedModelData_[key] = std::move(modelData);
    }

    LOG_DEBUG(MOD_GRAPHICS, "RaceModelLoader::preloadModelData: staged model for race={} gender={}", raceId, gender);
    return true;
}

} // namespace Graphics
} // namespace EQT
