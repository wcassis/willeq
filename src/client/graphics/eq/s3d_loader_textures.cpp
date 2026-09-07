// S3DLoader -- texture archives (zone, object, character).
//
// T451: split out of the 1,336-line src/client/graphics/eq/s3d_loader.cpp.
// Bodies are contiguous line ranges of that file, moved verbatim.

#include "client/graphics/eq/s3d_loader.h"
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

bool S3DLoader::loadTextures(PfsArchive& archive) {
    std::vector<std::string> texFiles;

    // Get BMP files
    archive.getFilenames(".bmp", texFiles);

    // Get DDS files
    std::vector<std::string> ddsFiles;
    if (archive.getFilenames(".dds", ddsFiles)) {
        texFiles.insert(texFiles.end(), ddsFiles.begin(), ddsFiles.end());
    }

    for (const auto& texName : texFiles) {
        std::vector<char> data;
        if (archive.get(texName, data)) {
            auto tex = std::make_shared<TextureInfo>();
            tex->name = texName;
            tex->data = std::move(data);
            tex->width = 0;
            tex->height = 0;

            // Store with lowercase key for case-insensitive lookup
            std::string lowerName = texName;
            std::transform(lowerName.begin(), lowerName.end(), lowerName.begin(),
                          [](unsigned char c) { return std::tolower(c); });
            zone_->textures[lowerName] = tex;
        }
    }

    return !zone_->textures.empty();
}

bool S3DLoader::loadObjectTextures(PfsArchive& archive) {
    if (!zone_) {
        return false;
    }

    std::vector<std::string> texFiles;

    // Get BMP files
    archive.getFilenames(".bmp", texFiles);

    // Get DDS files
    std::vector<std::string> ddsFiles;
    if (archive.getFilenames(".dds", ddsFiles)) {
        texFiles.insert(texFiles.end(), ddsFiles.begin(), ddsFiles.end());
    }

    for (const auto& texName : texFiles) {
        std::vector<char> data;
        if (archive.get(texName, data)) {
            auto tex = std::make_shared<TextureInfo>();
            tex->name = texName;
            tex->data = std::move(data);
            tex->width = 0;
            tex->height = 0;

            // Store with lowercase key for case-insensitive lookup
            std::string lowerName = texName;
            std::transform(lowerName.begin(), lowerName.end(), lowerName.begin(),
                          [](unsigned char c) { return std::tolower(c); });
            zone_->objectTextures[lowerName] = tex;
        }
    }

    return !zone_->objectTextures.empty();
}

bool S3DLoader::loadCharacterTextures(PfsArchive& archive) {
    if (!zone_) {
        return false;
    }

    std::vector<std::string> texFiles;

    // Get BMP files
    archive.getFilenames(".bmp", texFiles);

    // Get DDS files
    std::vector<std::string> ddsFiles;
    if (archive.getFilenames(".dds", ddsFiles)) {
        texFiles.insert(texFiles.end(), ddsFiles.begin(), ddsFiles.end());
    }

    for (const auto& texName : texFiles) {
        std::vector<char> data;
        if (archive.get(texName, data)) {
            auto tex = std::make_shared<TextureInfo>();
            tex->name = texName;
            tex->data = std::move(data);
            tex->width = 0;
            tex->height = 0;

            // Store with lowercase key for case-insensitive lookup
            std::string lowerName = texName;
            std::transform(lowerName.begin(), lowerName.end(), lowerName.begin(),
                          [](unsigned char c) { return std::tolower(c); });
            zone_->characterTextures[lowerName] = tex;
        }
    }

    return !zone_->characterTextures.empty();
}


} // namespace Graphics
} // namespace EQT
