#include "client/graphics/eq/sky_loader.h"
#include "client/graphics/eq/pfs.h"
#include "client/graphics/eq/wld_loader.h"
#include "client/graphics/eq/s3d_loader.h"
#include "common/logging.h"
#include <algorithm>
#include <cctype>
#include <filesystem>
#include <regex>

namespace EQT {
namespace Graphics {

bool SkyLoader::load(const std::string& eqClientPath) {
    skyData_.reset();
    error_.clear();
    geometries_.clear();
    materialTextures_.clear();

    // Build path to sky.s3d
    std::filesystem::path skyPath = std::filesystem::path(eqClientPath) / "sky.s3d";

    if (!std::filesystem::exists(skyPath)) {
        error_ = "sky.s3d not found at: " + skyPath.string();
        LOG_ERROR(MOD_GRAPHICS, "{}", error_);
        return false;
    }

    std::string archivePath = skyPath.string();

    // Load textures first
    if (!loadTextures(archivePath)) {
        return false;
    }

    // Parse WLD for geometry and animation data
    if (!parseWld(archivePath)) {
        return false;
    }

    // Build pre-defined sky types
    buildSkyTypes();

    LOG_INFO(MOD_GRAPHICS, "Sky loaded: {} layers, {} celestial bodies, {} textures",
             skyData_->layers.size(),
             skyData_->celestialBodies.size(),
             skyData_->textures.size());

    return true;
}

bool SkyLoader::loadTextures(const std::string& archivePath) {
    PfsArchive archive;
    if (!archive.open(archivePath)) {
        error_ = "Failed to open sky.s3d archive";
        LOG_ERROR(MOD_GRAPHICS, "{}", error_);
        return false;
    }

    skyData_ = std::make_shared<SkyData>();

    // Load BMP textures
    std::vector<std::string> bmpFiles;
    archive.getFilenames(".bmp", bmpFiles);

    for (const auto& filename : bmpFiles) {
        std::vector<char> data;
        if (archive.get(filename, data)) {
            auto tex = std::make_shared<TextureInfo>();
            tex->name = filename;
            tex->data = std::move(data);

            // Store with lowercase key for case-insensitive lookup
            std::string lowerName = filename;
            std::transform(lowerName.begin(), lowerName.end(), lowerName.begin(),
                          [](unsigned char c) { return std::tolower(c); });

            skyData_->textures[lowerName] = tex;
            LOG_DEBUG(MOD_GRAPHICS, "Loaded sky texture: {}", filename);
        }
    }

    // Load TGA textures
    std::vector<std::string> tgaFiles;
    archive.getFilenames(".tga", tgaFiles);

    for (const auto& filename : tgaFiles) {
        std::vector<char> data;
        if (archive.get(filename, data)) {
            auto tex = std::make_shared<TextureInfo>();
            tex->name = filename;
            tex->data = std::move(data);

            std::string lowerName = filename;
            std::transform(lowerName.begin(), lowerName.end(), lowerName.begin(),
                          [](unsigned char c) { return std::tolower(c); });

            skyData_->textures[lowerName] = tex;
            LOG_DEBUG(MOD_GRAPHICS, "Loaded sky texture: {}", filename);
        }
    }

    // Load DDS textures
    std::vector<std::string> ddsFiles;
    archive.getFilenames(".dds", ddsFiles);

    for (const auto& filename : ddsFiles) {
        std::vector<char> data;
        if (archive.get(filename, data)) {
            auto tex = std::make_shared<TextureInfo>();
            tex->name = filename;
            tex->data = std::move(data);

            std::string lowerName = filename;
            std::transform(lowerName.begin(), lowerName.end(), lowerName.begin(),
                          [](unsigned char c) { return std::tolower(c); });

            skyData_->textures[lowerName] = tex;
            LOG_DEBUG(MOD_GRAPHICS, "Loaded sky texture: {}", filename);
        }
    }

    LOG_INFO(MOD_GRAPHICS, "Loaded {} sky textures", skyData_->textures.size());
    return true;
}

bool SkyLoader::parseWld(const std::string& archivePath) {
    // Use WldLoader to parse sky.wld
    WldLoader wldLoader;
    if (!wldLoader.parseFromArchive(archivePath, "sky.wld")) {
        error_ = "Failed to parse sky.wld";
        LOG_ERROR(MOD_GRAPHICS, "{}", error_);
        return false;
    }

    // Extract geometries
    const auto& geometries = wldLoader.getGeometries();
    for (const auto& geom : geometries) {
        if (!geom->name.empty()) {
            // Store geometry by uppercase name for matching
            std::string upperName = geom->name;
            std::transform(upperName.begin(), upperName.end(), upperName.begin(),
                          [](unsigned char c) { return std::toupper(c); });
            geometries_[upperName] = geom;
        }
    }

    // Extract texture names from geometries to build material->texture mapping
    for (const auto& geom : geometries) {
        if (!geom->textureNames.empty() && !geom->name.empty()) {
            std::string upperName = geom->name;
            std::transform(upperName.begin(), upperName.end(), upperName.begin(),
                          [](unsigned char c) { return std::toupper(c); });

            // First texture is the primary texture for this layer
            std::string texName = geom->textureNames[0];
            std::transform(texName.begin(), texName.end(), texName.begin(),
                          [](unsigned char c) { return std::tolower(c); });
            materialTextures_[upperName] = texName;
        }
    }

    // Extract tracks from WldLoader first (needed for celestial body linking)
    const auto& trackDefs = wldLoader.getTrackDefs();
    const auto& trackRefs = wldLoader.getTrackRefs();

    for (const auto& [fragIndex, trackDef] : trackDefs) {
        if (trackDef && !trackDef->name.empty()) {
            auto skyTrack = std::make_shared<SkyTrack>();

            // Store without _TRACKDEF suffix for easier lookup
            std::string trackName = trackDef->name;
            if (trackName.size() > 9 && trackName.substr(trackName.size() - 9) == "_TRACKDEF") {
                trackName = trackName.substr(0, trackName.size() - 9) + "_TRACK";
            }
            skyTrack->name = trackName;

            // Convert BoneTransform frames to SkyTrackKeyframe
            for (const auto& frame : trackDef->frames) {
                SkyTrackKeyframe keyframe;
                keyframe.rotation = glm::quat(frame.quatW, frame.quatX, frame.quatY, frame.quatZ);
                keyframe.translation = glm::vec3(frame.shiftX, frame.shiftY, frame.shiftZ);
                keyframe.scale = frame.scale;
                skyTrack->keyframes.push_back(keyframe);
            }

            // Get frame delay from corresponding track ref if available
            for (const auto& [refIndex, trackRef] : trackRefs) {
                if (trackRef && trackRef->trackDefRef == fragIndex) {
                    skyTrack->frameDelayMs = trackRef->frameMs;
                    break;
                }
            }

            skyData_->tracks[skyTrack->name] = skyTrack;
            LOG_DEBUG(MOD_GRAPHICS, "Loaded sky track: {} with {} keyframes",
                     skyTrack->name, skyTrack->keyframes.size());
        }
    }

    // Parse layer meshes
    parseLayers();

    // Parse celestial body meshes (after tracks are loaded so they can link)
    parseCelestialBodies();

    // Additional track processing
    parseTracks();

    return true;
}

void SkyLoader::parseLayers() {
    // Pattern: LAYERXX_DMSPRITEDEF where XX is the layer number
    std::regex layerPattern(R"(LAYER(\d+)_DMSPRITEDEF)", std::regex::icase);

    for (const auto& [name, geom] : geometries_) {
        std::smatch match;
        if (std::regex_match(name, match, layerPattern)) {
            int layerNum = std::stoi(match[1].str());

            auto layer = std::make_shared<SkyLayer>();
            layer->name = name;
            layer->layerNumber = layerNum;
            layer->geometry = geom;
            layer->type = determineLayerType(name, layerNum);
            layer->isCloud = (layer->type == SkyLayerType::Cloud);

            // Find texture for this layer
            auto texIt = materialTextures_.find(name);
            if (texIt != materialTextures_.end()) {
                layer->textureName = texIt->second;
            }

            skyData_->layers[layerNum] = layer;
            LOG_DEBUG(MOD_GRAPHICS, "Loaded sky layer {}: type={}, texture={}",
                     layerNum,
                     static_cast<int>(layer->type),
                     layer->textureName);
        }
    }
}

void SkyLoader::parseCelestialBodies() {
    // List of known celestial body names
    const std::vector<std::string> celestialNames = {
        "SUN", "MOON", "MOON31", "MOON32", "MOON33", "MOON34", "MOON35",
        "MOON62", "SUN62", "SATURN", "CRESCENT", "EARTHRISE"
    };

    for (const auto& celestialName : celestialNames) {
        std::string meshName = celestialName + "_DMSPRITEDEF";

        auto geomIt = geometries_.find(meshName);
        if (geomIt != geometries_.end()) {
            auto celestial = std::make_shared<CelestialBody>();
            celestial->name = celestialName;
            celestial->geometry = geomIt->second;

            // Determine type
            if (celestialName.find("SUN") != std::string::npos) {
                celestial->isSun = true;
            } else if (celestialName.find("MOON") != std::string::npos ||
                       celestialName == "CRESCENT") {
                celestial->isMoon = true;
            }

            // Find texture - prioritize name-based lookup for celestial bodies
            // The WLD often has shared materials that can cause incorrect texture mappings
            std::string baseName = celestialName;
            // Strip numeric suffix (e.g., "SUN62" -> "SUN", "MOON31" -> "MOON")
            size_t digitPos = baseName.find_first_of("0123456789");
            if (digitPos != std::string::npos) {
                baseName = baseName.substr(0, digitPos);
            }
            std::transform(baseName.begin(), baseName.end(), baseName.begin(),
                          [](unsigned char c) { return std::tolower(c); });

            // Check if a texture with this base name exists
            std::string expectedTexture = baseName + ".bmp";
            if (skyData_->textures.find(expectedTexture) != skyData_->textures.end()) {
                celestial->textureName = expectedTexture;
            } else {
                // Fall back to material texture mapping
                auto texIt = materialTextures_.find(meshName);
                if (texIt != materialTextures_.end()) {
                    celestial->textureName = texIt->second;
                } else {
                    celestial->textureName = expectedTexture;  // Use expected even if not found
                }
            }

            // Find animation track
            std::string trackName = celestialName + "_TRACK";
            auto trackIt = skyData_->tracks.find(trackName);
            if (trackIt != skyData_->tracks.end()) {
                celestial->track = trackIt->second;
            }

            skyData_->celestialBodies[celestialName] = celestial;
            LOG_DEBUG(MOD_GRAPHICS, "Loaded celestial body: {} (sun={}, moon={}, texture={})",
                     celestialName, celestial->isSun, celestial->isMoon, celestial->textureName);
        }
    }
}

void SkyLoader::parseTracks() {
    // Tracks are already parsed by WldLoader and added to skyData_->tracks
    // in parseWld(). This method is for any additional track processing.

    // Link tracks to celestial bodies
    for (auto& [name, celestial] : skyData_->celestialBodies) {
        if (!celestial->track) {
            std::string trackName = name + "_TRACK";
            auto trackIt = skyData_->tracks.find(trackName);
            if (trackIt != skyData_->tracks.end()) {
                celestial->track = trackIt->second;
                LOG_DEBUG(MOD_GRAPHICS, "Linked track {} to celestial body {}",
                         trackName, name);
            }
        }
    }
}

void SkyLoader::buildSkyTypes() {
    // Pre-define sky type configurations based on layer numbering patterns
    // Layer numbering: X1=background, X2=celestials, X3=clouds
    // Type 1 (Normal): layers 11, 12, 13
    // Type 2 (Desert): layers 21, 22, 23
    // Type 3 (Air): layers 31, 32, 33
    // Type 4 (Cottony): layers 41, 42, 43
    // Type 5 (Red/Fear): layers 51, 52, 53
    // Type 6 (Luclin): layers 61, 62, 63
    // Type 7-9 (Luclin variants): layers 71-93
    // Type 10 (The Grey): layers 101, 102, 103
    // Type 11 (PoFire): layers 111, 112, 113
    // Type 12 (PoStorms): layers 121, 122, 123
    // Type 13 (Bottom sky): layers 131, 132, 133
    // Type 14 (PoWar): layers 141, 142, 143
    // Type 15-17 (PoAir/PoTranq): layers 151-173

    struct SkyTypeDef {
        int id;
        std::string name;
        std::vector<int> backgrounds;
        std::vector<int> clouds;
        std::vector<std::string> celestials;
    };

    std::vector<SkyTypeDef> typeDefs = {
        {0, "DEFAULT", {11}, {13}, {"SUN", "MOON"}},
        {1, "NORMAL", {11}, {13}, {"SUN", "MOON"}},
        {2, "DESERT", {21}, {23}, {"SUN", "MOON"}},
        {3, "AIR", {31}, {33}, {"SUN", "MOON"}},
        {4, "COTTONY", {41}, {43}, {"SUN", "MOON"}},
        {5, "FEAR", {51}, {53}, {"SUN", "MOON"}},
        {6, "LUCLIN", {61}, {63}, {"EARTHRISE", "MOON62", "SUN62"}},
        {7, "LUCLIN2", {71}, {73}, {"EARTHRISE", "MOON62", "SUN62"}},
        {8, "LUCLIN3", {81}, {83}, {"EARTHRISE", "MOON62", "SUN62"}},
        {9, "LUCLIN4", {91}, {93}, {"EARTHRISE", "MOON62", "SUN62"}},
        {10, "THEGREY", {101}, {103}, {}},  // The Grey has no celestials
        {11, "POFIRE", {111}, {113}, {}},
        {12, "POSTORMS", {121}, {123}, {}},
        {13, "BOTTOM", {131}, {133}, {}},
        {14, "POWAR", {141}, {143}, {}},
        {15, "POAIR1", {151}, {153}, {}},
        {16, "POTRANQ", {161}, {163}, {}},
        {17, "POAIR2", {171}, {173}, {}},
    };

    for (const auto& def : typeDefs) {
        SkyType skyType;
        skyType.name = def.name;
        skyType.typeId = def.id;
        skyType.backgroundLayers = def.backgrounds;
        skyType.cloudLayers = def.clouds;
        skyType.celestialBodies = def.celestials;

        skyData_->skyTypes[def.id] = skyType;
    }

    LOG_INFO(MOD_GRAPHICS, "Built {} sky type configurations", skyData_->skyTypes.size());
}

int SkyLoader::extractLayerNumber(const std::string& name) {
    std::regex pattern(R"(LAYER(\d+))", std::regex::icase);
    std::smatch match;
    if (std::regex_search(name, match, pattern)) {
        return std::stoi(match[1].str());
    }
    return -1;
}

SkyLayerType SkyLoader::determineLayerType(const std::string& name, int layerNumber) {
    // Check name for hints
    std::string upperName = name;
    std::transform(upperName.begin(), upperName.end(), upperName.begin(),
                  [](unsigned char c) { return std::toupper(c); });

    if (upperName.find("CLOUD") != std::string::npos) {
        return SkyLayerType::Cloud;
    }
    if (upperName.find("BOT") != std::string::npos) {
        return SkyLayerType::Bottom;
    }

    // Use layer number convention: X1=background, X3=cloud
    int lastDigit = layerNumber % 10;
    if (lastDigit == 1) {
        return SkyLayerType::Background;
    } else if (lastDigit == 3) {
        return SkyLayerType::Cloud;
    } else if (lastDigit == 2) {
        // Layer X2 is typically the celestial bodies layer
        return SkyLayerType::CelestialBody;
    }

    return SkyLayerType::Background;
}

bool SkyLoader::isCelestialBodyName(const std::string& name) {
    std::string upperName = name;
    std::transform(upperName.begin(), upperName.end(), upperName.begin(),
                  [](unsigned char c) { return std::toupper(c); });

    return upperName.find("SUN") != std::string::npos ||
           upperName.find("MOON") != std::string::npos ||
           upperName.find("SATURN") != std::string::npos ||
           upperName.find("CRESCENT") != std::string::npos ||
           upperName.find("EARTHRISE") != std::string::npos;
}

std::shared_ptr<SkyLayer> SkyLoader::getLayer(int layerNumber) const {
    if (!skyData_) return nullptr;
    auto it = skyData_->layers.find(layerNumber);
    return (it != skyData_->layers.end()) ? it->second : nullptr;
}

std::shared_ptr<CelestialBody> SkyLoader::getCelestialBody(const std::string& name) const {
    if (!skyData_) return nullptr;

    std::string upperName = name;
    std::transform(upperName.begin(), upperName.end(), upperName.begin(),
                  [](unsigned char c) { return std::toupper(c); });

    auto it = skyData_->celestialBodies.find(upperName);
    return (it != skyData_->celestialBodies.end()) ? it->second : nullptr;
}

std::shared_ptr<TextureInfo> SkyLoader::getTexture(const std::string& name) const {
    if (!skyData_) return nullptr;

    std::string lowerName = name;
    std::transform(lowerName.begin(), lowerName.end(), lowerName.begin(),
                  [](unsigned char c) { return std::tolower(c); });

    auto it = skyData_->textures.find(lowerName);
    return (it != skyData_->textures.end()) ? it->second : nullptr;
}

std::vector<std::shared_ptr<SkyLayer>> SkyLoader::getLayersForSkyType(int skyTypeId) const {
    std::vector<std::shared_ptr<SkyLayer>> result;
    if (!skyData_) return result;

    auto typeIt = skyData_->skyTypes.find(skyTypeId);
    if (typeIt == skyData_->skyTypes.end()) {
        // Fall back to default (type 0)
        typeIt = skyData_->skyTypes.find(0);
        if (typeIt == skyData_->skyTypes.end()) return result;
    }

    const auto& skyType = typeIt->second;

    // Add background layers
    for (int layerNum : skyType.backgroundLayers) {
        auto layer = getLayer(layerNum);
        if (layer) {
            result.push_back(layer);
        }
    }

    // Add cloud layers
    for (int layerNum : skyType.cloudLayers) {
        auto layer = getLayer(layerNum);
        if (layer) {
            result.push_back(layer);
        }
    }

    return result;
}

std::vector<std::shared_ptr<CelestialBody>> SkyLoader::getCelestialBodiesForSkyType(int skyTypeId) const {
    std::vector<std::shared_ptr<CelestialBody>> result;
    if (!skyData_) return result;

    auto typeIt = skyData_->skyTypes.find(skyTypeId);
    if (typeIt == skyData_->skyTypes.end()) {
        // Fall back to default (type 0)
        typeIt = skyData_->skyTypes.find(0);
        if (typeIt == skyData_->skyTypes.end()) return result;
    }

    const auto& skyType = typeIt->second;

    for (const auto& name : skyType.celestialBodies) {
        auto celestial = getCelestialBody(name);
        if (celestial) {
            result.push_back(celestial);
        }
    }

    return result;
}

} // namespace Graphics
} // namespace EQT
