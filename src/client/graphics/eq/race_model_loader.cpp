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
#include "common/logging.h"
#include "common/performance_metrics.h"
#include <algorithm>
#include <iostream>
#include <chrono>

namespace EQT {
namespace Graphics {

RaceModelLoader::RaceModelLoader(irr::scene::ISceneManager* smgr,
                                  irr::video::IVideoDriver* driver,
                                  irr::io::IFileSystem* fileSystem)
    : smgr_(smgr), driver_(driver), fileSystem_(fileSystem) {
    meshBuilder_ = std::make_unique<ZoneMeshBuilder>(smgr, driver, fileSystem);
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
}

void RaceModelLoader::setClientPath(const std::string& path) {
    clientPath_ = path;
    if (!clientPath_.empty() && clientPath_.back() != '/' && clientPath_.back() != '\\') {
        clientPath_ += '/';
    }
}

bool RaceModelLoader::hasRaceModel(uint16_t raceId, uint8_t gender) const {
    uint32_t key = makeCacheKey(raceId, gender);
    return loadedModels_.find(key) != loadedModels_.end();
}

std::shared_ptr<RaceModelData> RaceModelLoader::getRaceModelData(uint16_t raceId, uint8_t gender) {
    uint32_t key = makeCacheKey(raceId, gender);
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
    loadedModels_.clear();

    // Also clear variant caches
    for (auto& [key, mesh] : variantMeshCache_) {
        if (mesh) {
            mesh->drop();
        }
    }
    variantMeshCache_.clear();
    variantModels_.clear();

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

    // Keep meshBuilder_->textureCache_ intact - the ITexture* pointers are still valid
    // in the Irrlicht driver (character textures are never removed during zone transitions).
    // Clearing it would force re-reading raw TextureInfo data which may have been released
    // by releaseRawTextureData() after the first zone's entity loading.

    // Keep loadedModels_, variantModels_, global data, S3D archives, skeletons, etc.
    // These are expensive to load and their data is still valid

    LOG_INFO(MOD_GRAPHICS, "RaceModelLoader: Mesh caches cleared for zone transition");
}

irr::scene::IMesh* RaceModelLoader::getMeshForRace(uint16_t raceId, uint8_t gender) {
    auto funcStart = std::chrono::steady_clock::now();
    uint32_t key = makeCacheKey(raceId, gender);

    // Check mesh cache first
    auto meshIt = meshCache_.find(key);
    if (meshIt != meshCache_.end()) {
        return meshIt->second;
    }

    // Not cached - this will involve actual loading
    auto loadStart = std::chrono::steady_clock::now();

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
                            // Try numbered global (e.g., global4_chr.s3d for Iksar)
                            int globalNum = 0;
                            if (zoneName.length() > 6) {
                                try {
                                    globalNum = std::stoi(zoneName.substr(6));
                                } catch (...) {}
                            }
                            if (globalNum >= 2 && globalNum <= 7) {
                                if (!numberedGlobalsLoaded_) {
                                    loadNumberedGlobalModels();
                                }
                                if (loadModelFromNumberedGlobal(globalNum, raceId, gender)) {
                                    modelIt = loadedModels_.find(key);
                                }
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

            // 2. If not found, try current zone's _chr.s3d file
            if (modelIt == loadedModels_.end() && !currentZoneName_.empty()) {
                if (loadModelFromZoneChr(currentZoneName_, raceId, gender)) {
                    modelIt = loadedModels_.find(key);
                }
            }

            // 3. If not found in zone, try global_chr.s3d (classic models)
            if (modelIt == loadedModels_.end()) {
                if (loadModelFromGlobalChr(raceId, gender)) {
                    modelIt = loadedModels_.find(key);
                }
            }

            // 4. If still not found, try numbered globals (global2-7_chr.s3d)
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

        // Still not found - return nullptr (caller should use placeholder)
        if (modelIt == loadedModels_.end()) {
            meshCache_[key] = nullptr;
            return nullptr;
        }
    }

    // Build mesh from model data
    auto modelData = modelIt->second;
    if (!modelData || !modelData->combinedGeometry) {
        meshCache_[key] = nullptr;
        return nullptr;
    }

    irr::scene::IMesh* mesh = nullptr;
    if (!modelData->textures.empty() && !modelData->combinedGeometry->textureNames().empty()) {
        mesh = meshBuilder_->buildTexturedMesh(*modelData->combinedGeometry, modelData->textures, true);  // flipV for character models
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
    // Check main cache (main-thread only, no lock)
    if (loadedModels_.count(cacheKey) > 0) return true;
    // Check staging cache (shared with background thread)
    std::lock_guard<std::mutex> lock(preparedDataMutex_);
    return preparedModelData_.count(cacheKey) > 0;
}

void RaceModelLoader::promotePreparedModels() {
    std::lock_guard<std::mutex> lock(preparedDataMutex_);
    if (preparedModelData_.empty()) return;
    for (auto& [key, data] : preparedModelData_) {
        if (loadedModels_.count(key) == 0) {
            loadedModels_[key] = std::move(data);
            LOG_DEBUG(MOD_GRAPHICS, "RaceModelLoader: promoted preloaded model key={} to main cache", key);
        }
    }
    preparedModelData_.clear();
}

std::shared_ptr<RaceModelData> RaceModelLoader::buildModelDataFromCharacters(
        const std::vector<std::shared_ptr<CharacterModel>>& characters,
        const std::map<std::string, std::shared_ptr<TextureInfo>>& textures,
        uint16_t raceId, uint8_t gender) const {

    std::string baseRaceCode = getRaceCode(raceId);
    if (baseRaceCode.empty()) return nullptr;

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

    for (const auto& codeToTry : codesToTry) {
        for (const auto& character : characters) {
            if (!character || character->parts.empty()) continue;

            std::string charName = character->name;
            std::transform(charName.begin(), charName.end(), charName.begin(),
                           [](unsigned char c) { return std::toupper(c); });

            if (charName.find(codeToTry) == std::string::npos) continue;

            // Found a match — select default body and head parts
            std::string defaultBodyName = codeToTry + "_DMSPRITEDEF";
            std::string defaultHeadName = codeToTry + "HE00_DMSPRITEDEF";

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
                selectedSkinnedParts = character->partsWithTransforms;
                selectedRawParts = character->rawParts;
            }

            auto combinedGeom = combineCharacterPartsWithTransforms(selectedSkinnedParts);
            auto rawGeom = combineCharacterPartsRaw(selectedRawParts);
            if (!combinedGeom) continue;

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

bool RaceModelLoader::preloadModelData(uint16_t raceId, uint8_t gender) {
    uint32_t key = makeCacheKey(raceId, gender);

    // Check staging cache (thread-safe). If already staged or will be promoted,
    // no need to redo the work. Note: we do NOT read loadedModels_ here because
    // that map is main-thread-only (no lock). The main thread's isPending() check
    // and promotePreparedModels() flow ensures no redundant work.
    {
        std::lock_guard<std::mutex> lock(preparedDataMutex_);
        if (preparedModelData_.count(key) > 0) return true;
    }

    // Try to build model data from already-loaded archives.
    // These archives (globalCharacters_, etc.) are immutable after zone init,
    // so reading them from the background thread is safe.
    // getMergedTextures() is safe here because the cache is always warm by the time
    // progressive loading activates (loadGlobalAssets + loadZone already called it).
    // If somehow the cache is cold, fall back to just globalTextures_ to avoid
    // triggering non-thread-safe lazy loading paths.
    std::map<std::string, std::shared_ptr<TextureInfo>> mergedTextures;
    if (mergedTexturesCacheValid_) {
        mergedTextures = cachedMergedTextures_;
    } else {
        mergedTextures = globalTextures_;
        LOG_WARN(MOD_GRAPHICS, "RaceModelLoader::preloadModelData: merged texture cache not valid, using global textures only");
    }
    std::shared_ptr<RaceModelData> modelData;

    // 1. Search globalCharacters_ (global_chr.s3d, already loaded)
    if (!modelData && !globalCharacters_.empty()) {
        modelData = buildModelDataFromCharacters(globalCharacters_, mergedTextures, raceId, gender);
    }

    // 2. Search numberedGlobalCharacters_ (global2-7_chr.s3d, already loaded)
    if (!modelData) {
        for (const auto& [num, chars] : numberedGlobalCharacters_) {
            modelData = buildModelDataFromCharacters(chars, mergedTextures, raceId, gender);
            if (modelData) break;
        }
    }

    // 3. Search zoneCharacters_ (zone_chr.s3d, already loaded)
    if (!modelData && !zoneCharacters_.empty()) {
        modelData = buildModelDataFromCharacters(zoneCharacters_, mergedTextures, raceId, gender);
    }

    // 4. Search otherChrCaches_ (previously loaded chr files)
    if (!modelData) {
        std::lock_guard<std::mutex> chrLock(otherChrCacheMutex_);
        for (const auto& [filename, cache] : otherChrCaches_) {
            modelData = buildModelDataFromCharacters(cache.characters, mergedTextures, raceId, gender);
            if (modelData) break;
        }
    }

    // 5. Try JSON-specified S3D file from disk (last resort)
    if (!modelData && !clientPath_.empty()) {
        std::string jsonS3dFile = getRaceS3DFile(raceId);
        if (!jsonS3dFile.empty()) {
            std::string s3dPath = clientPath_ + jsonS3dFile;
            S3DLoader loader;
            if (loader.loadZone(s3dPath)) {
                auto zone = loader.getZone();
                if (zone && !zone->characters.empty()) {
                    // Merge S3D textures with global textures
                    auto diskTextures = zone->characterTextures;
                    for (const auto& [name, tex] : mergedTextures) {
                        if (diskTextures.find(name) == diskTextures.end()) {
                            diskTextures[name] = tex;
                        }
                    }
                    modelData = buildModelDataFromCharacters(zone->characters, diskTextures, raceId, gender);
                }
            }
        }
    }

    if (!modelData) {
        LOG_DEBUG(MOD_GRAPHICS, "RaceModelLoader::preloadModelData: no model found for race={} gender={}", raceId, gender);
        return false;
    }

    // Animation merge (same logic as getAnimatedMeshForRace, reading immutable globalCharacters_)
    if (modelData->skeleton) {
        std::string raceCode = getRaceCode(raceId);
        raceCode = getGenderedRaceCode(raceCode, gender);
        std::string animSourceCode = getAnimationSourceCode(raceCode);

        if (!animSourceCode.empty() && animSourceCode != raceCode) {
            for (const auto& sourceChar : globalCharacters_) {
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

    // Decode textures to ARGB on background thread (CPU-only, no GL calls).
    // Main thread can then upload one texture per frame via driver_->addTexture().
    if (modelData->combinedGeometry && !modelData->textures.empty()) {
        for (const auto& texName : modelData->combinedGeometry->textureNames()) {
            std::string lowerName = texName;
            std::transform(lowerName.begin(), lowerName.end(), lowerName.begin(),
                           [](unsigned char c) { return std::tolower(c); });
            auto texIt = modelData->textures.find(lowerName);
            if (texIt == modelData->textures.end() || !texIt->second) continue;
            const auto& rawData = texIt->second->data;
            if (rawData.empty()) continue;

            DecodedTexture decoded;
            decoded.name = texName;

            if (DDSDecoder::isDDS(rawData)) {
                DecodedImage img = DDSDecoder::decode(rawData);
                if (!img.isValid()) continue;
                decoded.width = img.width;
                decoded.height = img.height;
                decoded.argbPixels.resize(img.width * img.height);
                for (uint32_t i = 0; i < img.width * img.height; ++i) {
                    uint8_t r = img.pixels[i * 4 + 0];
                    uint8_t g = img.pixels[i * 4 + 1];
                    uint8_t b = img.pixels[i * 4 + 2];
                    uint8_t a = img.pixels[i * 4 + 3];
                    decoded.argbPixels[i] = (static_cast<uint32_t>(a) << 24) |
                                            (static_cast<uint32_t>(r) << 16) |
                                            (static_cast<uint32_t>(g) << 8) |
                                            static_cast<uint32_t>(b);
                    if (a < 255) decoded.hasAlpha = true;
                }
            } else {
                // BMP textures: try to decode via Irrlicht's BMP reader path
                // These are uncommon for entity textures; leave as raw data for main-thread decode
                continue;
            }

            if (!decoded.argbPixels.empty()) {
                modelData->decodedTextures.push_back(std::move(decoded));
            }
        }
        LOG_DEBUG(MOD_GRAPHICS, "RaceModelLoader::preloadModelData: decoded {} textures for race={} gender={}",
                  modelData->decodedTextures.size(), raceId, gender);
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
