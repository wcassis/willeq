#ifdef EQT_HAS_GRAPHICS

#include "client/graphics/graphics_archive_index.h"
#include "client/graphics/eq/pfs.h"
#include "client/graphics/eq/race_codes.h"
#include "common/logging.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <set>
#include <json/json.h>

namespace EQT {
namespace Graphics {

bool GraphicsArchiveIndex::buildIndex(const std::string& eqPath, bool lazyMode,
                                       std::function<void()> tickCallback) {
    eqPath_ = eqPath;
    lazyMode_ = lazyMode;

    if (eqPath_.empty() || !std::filesystem::exists(eqPath_) ||
        !std::filesystem::is_directory(eqPath_)) {
        LOG_ERROR(MOD_GRAPHICS_LOAD, "GraphicsArchiveIndex: invalid EQ path: {}", eqPath_);
        return false;
    }

    // Try loading from cache first
    if (loadCache()) {
        LOG_INFO(MOD_GRAPHICS_LOAD, "GraphicsArchiveIndex: loaded from cache ({} race entries, {} archives)",
                 raceIndex_.size(), archiveSizes_.size());
        return true;
    }

    // Cache miss or stale - scan archives
    scanArchives(tickCallback);

    if (!raceIndex_.empty()) {
        saveCache();
    }

    return !raceIndex_.empty();
}

std::string GraphicsArchiveIndex::getArchiveForRace(uint16_t raceId, uint8_t gender) const {
    uint32_t key = makeKey(raceId, gender);
    auto it = raceIndex_.find(key);
    if (it != raceIndex_.end()) {
        return eqPath_ + "/" + it->second;
    }
    return "";
}

PfsArchive* GraphicsArchiveIndex::getCachedArchive(const std::string& archivePath) {
    auto it = cachedArchives_.find(archivePath);
    if (it != cachedArchives_.end()) {
        return it->second.get();
    }
    return nullptr;
}

bool GraphicsArchiveIndex::loadCache() {
    std::string cachePath = "config/gfx_index_cache.json";
    std::ifstream cacheFile(cachePath);
    if (!cacheFile.is_open()) {
        return false;
    }

    Json::Value root;
    Json::CharReaderBuilder builder;
    std::string errors;
    if (!Json::parseFromStream(builder, cacheFile, &root, &errors)) {
        LOG_DEBUG(MOD_GRAPHICS_LOAD, "GFX index cache parse error: {}", errors);
        return false;
    }

    if (!root.isMember("archives") || !root.isMember("raceIndex") || !root.isMember("eqPath")) {
        return false;
    }

    // Validate EQ path matches
    if (root["eqPath"].asString() != eqPath_) {
        LOG_DEBUG(MOD_GRAPHICS_LOAD, "GFX index cache stale: eqPath changed");
        return false;
    }

    // Validate that all cached archives still exist with same size
    const Json::Value& archives = root["archives"];
    for (const auto& name : archives.getMemberNames()) {
        std::string archivePath = eqPath_ + "/" + name;
        if (!std::filesystem::exists(archivePath)) {
            LOG_DEBUG(MOD_GRAPHICS_LOAD, "GFX index cache stale: {} missing", name);
            return false;
        }
        auto fileSize = std::filesystem::file_size(archivePath);
        if (fileSize != static_cast<uintmax_t>(archives[name].asUInt64())) {
            LOG_DEBUG(MOD_GRAPHICS_LOAD, "GFX index cache stale: {} size changed", name);
            return false;
        }
    }

    // Check that no new *_chr.s3d files have appeared
    size_t chrCount = 0;
    for (const auto& entry : std::filesystem::directory_iterator(eqPath_)) {
        if (!entry.is_regular_file()) continue;
        std::string filename = entry.path().filename().string();
        std::string lower = filename;
        std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
        if (lower.length() > 8 && lower.find("_chr.s3d") != std::string::npos) {
            chrCount++;
        }
    }
    if (chrCount != archives.size()) {
        LOG_DEBUG(MOD_GRAPHICS_LOAD, "GFX index cache stale: archive count changed ({} vs {})",
                  chrCount, archives.size());
        return false;
    }

    // Load the index
    archiveSizes_.clear();
    for (const auto& name : archives.getMemberNames()) {
        archiveSizes_[name] = archives[name].asUInt64();
    }

    raceIndex_.clear();
    const Json::Value& index = root["raceIndex"];
    for (const auto& keyStr : index.getMemberNames()) {
        uint32_t key = static_cast<uint32_t>(std::stoul(keyStr));
        raceIndex_[key] = index[keyStr].asString();
    }

    return true;
}

void GraphicsArchiveIndex::saveCache() {
    Json::Value root;
    root["eqPath"] = eqPath_;

    // Store archive filenames and sizes
    Json::Value archives(Json::objectValue);
    for (const auto& [name, size] : archiveSizes_) {
        archives[name] = static_cast<Json::UInt64>(size);
    }
    root["archives"] = archives;

    // Store race index
    Json::Value index(Json::objectValue);
    for (const auto& [key, archiveName] : raceIndex_) {
        index[std::to_string(key)] = archiveName;
    }
    root["raceIndex"] = index;

    std::string cachePath = "config/gfx_index_cache.json";
    std::ofstream out(cachePath);
    if (!out.is_open()) {
        LOG_WARN(MOD_GRAPHICS_LOAD, "Failed to save GFX index cache to {}", cachePath);
        return;
    }

    Json::StreamWriterBuilder writerBuilder;
    writerBuilder["indentation"] = "";  // Compact output
    out << Json::writeString(writerBuilder, root);
    LOG_INFO(MOD_GRAPHICS_LOAD, "Saved GFX index cache: {} race entries to {}", raceIndex_.size(), cachePath);
}

void GraphicsArchiveIndex::scanArchives(std::function<void()> tickCallback) {
    raceIndex_.clear();
    archiveSizes_.clear();
    cachedArchives_.clear();

    // Find all *_chr.s3d files in EQ path (filename + size only, NO archive opening)
    std::set<std::string> archiveFilenames;  // lowercase filenames that exist on disk
    for (const auto& entry : std::filesystem::directory_iterator(eqPath_)) {
        if (!entry.is_regular_file()) continue;

        std::string filename = entry.path().filename().string();
        std::string lowerFilename = filename;
        std::transform(lowerFilename.begin(), lowerFilename.end(),
                       lowerFilename.begin(), ::tolower);

        if (lowerFilename.length() > 8 && lowerFilename.find("_chr.s3d") != std::string::npos) {
            archiveSizes_[filename] = std::filesystem::file_size(entry.path());
            archiveFilenames.insert(lowerFilename);
        }
    }

    if (archiveFilenames.empty()) {
        LOG_DEBUG(MOD_GRAPHICS_LOAD, "No *_chr.s3d archives found in {}", eqPath_);
        return;
    }

    LOG_INFO(MOD_GRAPHICS_LOAD, "Building GFX index from {} archive filenames (no archive parsing)...",
             archiveFilenames.size());

    // Build the index by iterating all known race IDs and computing their expected
    // archive filename via getRaceModelFilename(). This is pure computation — no I/O.
    // The naming convention is: global{code}_chr.s3d (e.g., globalhum_chr.s3d for HUM race)
    size_t totalRaces = 0;
    for (uint16_t raceId = 1; raceId <= 733; ++raceId) {
        // Male/neuter
        std::string maleFile = getRaceModelFilename(raceId, 0);
        if (!maleFile.empty()) {
            std::string lower = maleFile;
            std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
            if (archiveFilenames.count(lower)) {
                uint32_t key = makeKey(raceId, 0);
                if (raceIndex_.find(key) == raceIndex_.end()) {
                    raceIndex_[key] = maleFile;
                    totalRaces++;
                }
            }
        }

        // Female
        std::string femaleFile = getRaceModelFilename(raceId, 1);
        if (!femaleFile.empty() && femaleFile != maleFile) {
            std::string lower = femaleFile;
            std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
            if (archiveFilenames.count(lower)) {
                uint32_t key = makeKey(raceId, 1);
                if (raceIndex_.find(key) == raceIndex_.end()) {
                    raceIndex_[key] = femaleFile;
                    totalRaces++;
                }
            }
        }
    }

    LOG_INFO(MOD_GRAPHICS_LOAD, "Indexed {} race entries from {} archive filenames",
             totalRaces, archiveFilenames.size());
}

} // namespace Graphics
} // namespace EQT

#endif // EQT_HAS_GRAPHICS
