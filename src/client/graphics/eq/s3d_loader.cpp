// S3DLoader -- archive entry points, zone name derivation and zone lights.
//
// T451: split out of the 1,336-line src/client/graphics/eq/s3d_loader.cpp.
// Bodies are contiguous line ranges of that file, moved verbatim.

#include "client/graphics/eq/s3d_loader.h"
#include "client/core/memory_ledger.h"
#include <pthread.h>  // T504: thread names
#include "common/logging.h"
#include <algorithm>
#include <iostream>
#include <limits>
#include <cmath>
#include <cctype>
#include <filesystem>
#include <functional>
#include <map>
#include <set>

namespace EQT {
namespace Graphics {

// Static empty vectors
const std::vector<ObjectInstance> S3DLoader::emptyObjects_;
const std::vector<std::shared_ptr<CharacterModel>> S3DLoader::emptyCharacters_;
const std::vector<std::shared_ptr<ZoneLight>> S3DLoader::emptyLights_;

bool S3DLoader::loadZone(const std::string& archivePath) {
    return loadZone(archivePath, S3DLoadOptions{});
}

bool S3DLoader::loadZone(const std::string& archivePath, const S3DLoadOptions& options) {
    zone_ = nullptr;
    error_.clear();
    options_ = options;
    zoneName_ = extractZoneName(archivePath);
    {
        // T504b Leg 0: every whole-archive parse names the thread that ran
        // it, so the census can say whether a parse ever happens on the
        // render thread (the plan's STOP 3).
        char thread[32] = "?";
        pthread_getname_np(pthread_self(), thread, sizeof(thread));
        const size_t slash = archivePath.find_last_of("/\\");
        LOG_INFO(MOD_GRAPHICS, "[srcchar] parse archive={} thread={} filter={} entity_models_live={}",
                 slash == std::string::npos ? archivePath : archivePath.substr(slash + 1), thread,
                 options.characterCodeFilter.size(),
                 eqt::memory::stats(eqt::memory::Category::EntityModels).liveBytes);
    }

    PfsArchive archive;
    if (!archive.open(archivePath)) {
        error_ = "Failed to open archive: " + archivePath;
        return false;
    }

    // Find the WLD file - typically named like zonename.wld
    std::vector<std::string> wldFiles;
    archive.getFilenames(".wld", wldFiles);

    if (wldFiles.empty()) {
        error_ = "No WLD file found in archive";
        return false;
    }

    // Use the first WLD file (main zone geometry)
    // Typically the zone.wld file, not objects.wld, lights.wld, etc.
    std::string mainWld;
    for (const auto& wld : wldFiles) {
        // Prefer the main zone wld (not objects, characters, lights, etc.)
        if (wld.find("_obj") == std::string::npos &&
            wld.find("_chr") == std::string::npos &&
            wld != "objects.wld" &&
            wld != "lights.wld") {
            mainWld = wld;
            break;
        }
    }

    if (mainWld.empty()) {
        mainWld = wldFiles[0];
    }
    mainWldName_ = mainWld;

    // Parse WLD file
    auto wldLoader = std::make_shared<WldLoader>();
    if (!wldLoader->parseFromOpenArchive(archive, mainWld)) {  // T504: archive is already open
        error_ = "Failed to parse WLD file: " + mainWld;
        return false;
    }

    zone_ = std::make_shared<S3DZone>();
    zone_->zoneName = zoneName_;
    zone_->wldLoader = wldLoader;  // Store for PVS access

    if (options.computeCombinedGeometry) {
        zone_->geometry = wldLoader->getCombinedGeometry();

        if (!zone_->geometry) {
            error_ = "No geometry extracted from WLD";
            return false;
        }
    } else {
        // Validate WLD parsed successfully without computing combined geometry
        if (wldLoader->getGeometries().empty()) {
            error_ = "No geometry extracted from WLD";
            return false;
        }
    }

    // Load textures (optional - rendering can work with vertex colors)
    loadTextures(archive);

    // T504: the zone archive is open once, here; objects.wld and lights.wld
    // are read from it (loadObjects/loadLights used to re-open the same file
    // per sub-step -- five opens + 7 MiB block reads each, per zone load).
    if (options.loadObjects) {
        loadObjectsFromArchive(archive, archivePath);
    }

    // Load character models (optional; _chr.s3d is a different file)
    if (options.loadCharacters) {
        loadCharacters(archivePath);
    }

    loadLightsFromArchive(archive);

    {
        const size_t slash = archivePath.find_last_of("/\\");
        LOG_INFO(MOD_GRAPHICS, "[srcchar] parsed archive={} characters={} of {} entity_models_live={}",
                 slash == std::string::npos ? archivePath : archivePath.substr(slash + 1),
                 zone_ ? zone_->characters.size() : 0, zone_ ? zone_->allCharacterNames.size() : 0,
                 eqt::memory::stats(eqt::memory::Category::EntityModels).liveBytes);
    }
    return true;
}

bool S3DLoader::loadZoneFromPreParsed(std::shared_ptr<PfsArchive> archive,
                                       std::shared_ptr<WldLoader> wldLoader,
                                       const std::string& zoneName,
                                       const std::string& archivePath,
                                       const S3DLoadOptions& options) {
    zone_ = nullptr;
    error_.clear();
    options_ = options;
    mainWldName_.clear();
    zoneName_ = zoneName;

    if (!archive || !wldLoader) {
        error_ = "Pre-parsed archive or WLD loader is null";
        return false;
    }

    zone_ = std::make_shared<S3DZone>();
    zone_->zoneName = zoneName_;
    zone_->wldLoader = wldLoader;

    if (options.computeCombinedGeometry) {
        zone_->geometry = wldLoader->getCombinedGeometry();
        if (!zone_->geometry) {
            error_ = "No geometry extracted from pre-parsed WLD";
            return false;
        }
    } else {
        if (wldLoader->getGeometries().empty()) {
            error_ = "No geometry extracted from pre-parsed WLD";
            return false;
        }
    }

    // Load textures from the already-open archive
    loadTextures(*archive);

    // Load placeable objects (objects.wld from the open zone archive, then _obj.s3d)
    if (options.loadObjects) {
        loadObjectsFromArchive(*archive, archivePath);
    }

    // Load light sources (lights.wld is in the zone archive, already open)
    loadLightsFromArchive(*archive);

    return true;
}

std::string S3DLoader::extractZoneName(const std::string& path) {
    // Find filename from path
    size_t lastSlash = path.find_last_of("/\\");
    std::string filename = (lastSlash != std::string::npos) ?
                           path.substr(lastSlash + 1) : path;

    // Remove .s3d extension
    size_t dot = filename.find_last_of('.');
    if (dot != std::string::npos) {
        filename = filename.substr(0, dot);
    }

    // Remove _obj, _chr suffixes
    size_t underscore = filename.find('_');
    if (underscore != std::string::npos) {
        std::string suffix = filename.substr(underscore);
        if (suffix == "_obj" || suffix == "_chr" || suffix == "_chr2") {
            filename = filename.substr(0, underscore);
        }
    }

    return filename;
}

bool S3DLoader::loadLights(const std::string& archivePath) {
    PfsArchive archive;
    if (!archive.open(archivePath)) {
        return false;
    }
    return loadLightsFromArchive(archive);
}

bool S3DLoader::loadLightsFromArchive(PfsArchive& archive) {
    if (!zone_) {
        return false;
    }

    // Check for lights.wld in the already-open archive
    std::vector<std::string> wldFiles;
    archive.getFilenames(".wld", wldFiles);

    bool hasLightsWld = false;
    for (const auto& wld : wldFiles) {
        if (wld == "lights.wld") {
            hasLightsWld = true;
            break;
        }
    }

    if (!hasLightsWld) {
        return false;
    }

    // Parse lights.wld from the already-open archive
    WldLoader lightsLoader;
    if (!lightsLoader.parseFromOpenArchive(archive, "lights.wld")) {
        return false;
    }

    const auto& lights = lightsLoader.getLights();
    if (lights.empty()) {
        return false;
    }

    zone_->lights = lights;
    return !zone_->lights.empty();
}


} // namespace Graphics
} // namespace EQT
