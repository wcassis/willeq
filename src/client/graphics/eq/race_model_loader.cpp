// RaceModelLoader core functionality
// Most methods are defined in separate files:
// - model_loading.cpp - S3D file loading methods
// - mesh_building.cpp - Irrlicht mesh construction
// - animated_mesh_creation.cpp - Animated mesh/node creation
// - variant_loading.cpp - Head/body variant loading

#include "client/graphics/eq/race_model_loader.h"
#include "client/graphics/eq/race_codes.h"
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

    // Clear animated mesh caches
    animatedMeshCache_.clear();
    variantAnimatedMeshCache_.clear();

    // Clear cached _chr.s3d files from other zones
    otherChrCaches_.clear();

    LOG_DEBUG(MOD_GRAPHICS, "RaceModelLoader: Cache cleared");
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
    if (!modelData->textures.empty() && !modelData->combinedGeometry->textureNames.empty()) {
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

} // namespace Graphics
} // namespace EQT
