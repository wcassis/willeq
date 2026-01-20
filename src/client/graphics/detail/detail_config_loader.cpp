#include "client/graphics/detail/detail_config_loader.h"
#include "common/logging.h"
#include <json/json.h>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <cctype>

namespace EQT {
namespace Graphics {
namespace Detail {

std::unique_ptr<ZoneDetailConfig> DetailConfigLoader::loadConfigForZone(
    const std::string& zoneName,
    const std::string& dataPath) const
{
    // Convert zone name to lowercase for file lookup
    std::string lowerZone = zoneName;
    std::transform(lowerZone.begin(), lowerZone.end(), lowerZone.begin(),
                   [](unsigned char c) { return std::tolower(c); });

    // Try zone-specific config first
    std::string zoneConfigPath = dataPath + "/detail/zones/" + lowerZone + ".json";
    auto config = loadConfigFile(zoneConfigPath);
    if (config) {
        config->zoneName = zoneName;
        LOG_INFO(MOD_GRAPHICS, "DetailConfigLoader: Loaded zone config from {}", zoneConfigPath);
        return config;
    }

    // Fall back to default config
    config = loadDefaultConfig(dataPath);
    if (config) {
        config->zoneName = zoneName;
        return config;
    }

    // Return hardcoded default
    auto hardcoded = std::make_unique<ZoneDetailConfig>(getHardcodedDefault(zoneName));
    LOG_INFO(MOD_GRAPHICS, "DetailConfigLoader: Using hardcoded default for zone '{}'", zoneName);
    return hardcoded;
}

std::unique_ptr<ZoneDetailConfig> DetailConfigLoader::loadDefaultConfig(const std::string& dataPath) const {
    std::string defaultPath = dataPath + "/detail/default_config.json";
    auto config = loadConfigFile(defaultPath);
    if (config) {
        LOG_INFO(MOD_GRAPHICS, "DetailConfigLoader: Loaded default config from {}", defaultPath);
    }
    return config;
}

std::unique_ptr<ZoneDetailConfig> DetailConfigLoader::loadConfigFile(const std::string& filepath) const {
    std::ifstream file(filepath);
    if (!file.is_open()) {
        return nullptr;
    }

    std::stringstream buffer;
    buffer << file.rdbuf();
    std::string json = buffer.str();

    auto config = std::make_unique<ZoneDetailConfig>();
    if (parseConfigJson(json, *config)) {
        return config;
    }

    LOG_ERROR(MOD_GRAPHICS, "DetailConfigLoader: Failed to parse {}", filepath);
    return nullptr;
}

bool DetailConfigLoader::parseConfigJson(const std::string& json, ZoneDetailConfig& config) const {
    Json::Value root;
    Json::CharReaderBuilder builder;
    std::string errors;
    std::istringstream stream(json);

    if (!Json::parseFromStream(builder, stream, &root, &errors)) {
        LOG_ERROR(MOD_GRAPHICS, "DetailConfigLoader: JSON parse error: {}", errors);
        return false;
    }

    // Parse basic settings
    if (root.isMember("zoneName")) {
        config.zoneName = root["zoneName"].asString();
    }
    if (root.isMember("isOutdoor")) {
        config.isOutdoor = root["isOutdoor"].asBool();
    }
    if (root.isMember("chunkSize")) {
        config.chunkSize = root["chunkSize"].asFloat();
    }
    if (root.isMember("viewDistance")) {
        config.viewDistance = root["viewDistance"].asFloat();
    }
    if (root.isMember("densityMultiplier")) {
        config.densityMultiplier = root["densityMultiplier"].asFloat();
    }

    // Parse wind settings
    if (root.isMember("wind")) {
        const Json::Value& wind = root["wind"];
        if (wind.isMember("strength")) {
            config.windStrength = wind["strength"].asFloat();
        }
        if (wind.isMember("frequency")) {
            config.windFrequency = wind["frequency"].asFloat();
        }
    }

    // Parse detail types
    if (root.isMember("detailTypes") && root["detailTypes"].isArray()) {
        for (const auto& typeJson : root["detailTypes"]) {
            DetailType type;

            if (typeJson.isMember("name")) {
                type.name = typeJson["name"].asString();
            }
            if (typeJson.isMember("category")) {
                type.category = parseCategoryString(typeJson["category"].asString());
            }
            if (typeJson.isMember("orientation")) {
                type.orientation = parseOrientationString(typeJson["orientation"].asString());
            }

            // Parse atlas coordinates
            if (typeJson.isMember("atlas")) {
                const Json::Value& atlas = typeJson["atlas"];
                if (atlas.isMember("x")) type.atlasX = atlas["x"].asInt();
                if (atlas.isMember("y")) type.atlasY = atlas["y"].asInt();
                if (atlas.isMember("width")) type.atlasWidth = atlas["width"].asInt();
                if (atlas.isMember("height")) type.atlasHeight = atlas["height"].asInt();
            }

            // Parse size range
            if (typeJson.isMember("sizeRange") && typeJson["sizeRange"].isArray()) {
                const Json::Value& sizes = typeJson["sizeRange"];
                if (sizes.size() >= 2) {
                    type.minSize = sizes[0].asFloat();
                    type.maxSize = sizes[1].asFloat();
                }
            }

            // Parse placement rules
            if (typeJson.isMember("minSlope")) {
                type.minSlope = typeJson["minSlope"].asFloat();
            }
            if (typeJson.isMember("maxSlope")) {
                type.maxSlope = typeJson["maxSlope"].asFloat();
            }
            if (typeJson.isMember("baseDensity")) {
                type.baseDensity = typeJson["baseDensity"].asFloat();
            }

            // Parse wind settings
            if (typeJson.isMember("windResponse")) {
                type.windResponse = typeJson["windResponse"].asFloat();
            }
            if (typeJson.isMember("windHeightBias")) {
                type.windHeightBias = typeJson["windHeightBias"].asFloat();
            }

            // Parse test color (RGBA)
            if (typeJson.isMember("testColor") && typeJson["testColor"].isArray()) {
                const Json::Value& color = typeJson["testColor"];
                if (color.size() >= 3) {
                    int r = color[0].asInt();
                    int g = color[1].asInt();
                    int b = color[2].asInt();
                    int a = color.size() >= 4 ? color[3].asInt() : 255;
                    type.testColor = irr::video::SColor(a, r, g, b);
                }
            }

            config.detailTypes.push_back(type);
        }
    }

    return true;
}

DetailCategory DetailConfigLoader::parseCategoryString(const std::string& str) {
    std::string lower = str;
    std::transform(lower.begin(), lower.end(), lower.begin(),
                   [](unsigned char c) { return std::tolower(c); });

    if (lower == "grass") return DetailCategory::Grass;
    if (lower == "plants") return DetailCategory::Plants;
    if (lower == "rocks") return DetailCategory::Rocks;
    if (lower == "debris") return DetailCategory::Debris;
    if (lower == "mushrooms") return DetailCategory::Mushrooms;
    if (lower == "all") return DetailCategory::All;

    return DetailCategory::Grass;  // Default
}

DetailOrientation DetailConfigLoader::parseOrientationString(const std::string& str) {
    std::string lower = str;
    std::transform(lower.begin(), lower.end(), lower.begin(),
                   [](unsigned char c) { return std::tolower(c); });

    if (lower == "crossed_quads" || lower == "crossedquads") {
        return DetailOrientation::CrossedQuads;
    }
    if (lower == "flat_ground" || lower == "flatground" || lower == "flat") {
        return DetailOrientation::FlatGround;
    }
    if (lower == "single_quad" || lower == "singlequad" || lower == "single") {
        return DetailOrientation::SingleQuad;
    }

    return DetailOrientation::CrossedQuads;  // Default
}

ZoneDetailConfig DetailConfigLoader::getHardcodedDefault(const std::string& zoneName) {
    ZoneDetailConfig config;
    config.zoneName = zoneName;
    config.isOutdoor = true;
    config.chunkSize = 50.0f;
    config.viewDistance = 150.0f;
    config.densityMultiplier = 1.0f;
    config.windStrength = 1.0f;
    config.windFrequency = 0.5f;

    // Create test detail types with different colors
    DetailType grass;
    grass.name = "grass";
    grass.category = DetailCategory::Grass;
    grass.orientation = DetailOrientation::CrossedQuads;
    grass.minSize = 0.5f;
    grass.maxSize = 1.2f;
    grass.baseDensity = 1.0f;
    grass.maxSlope = 0.5f;
    grass.windResponse = 1.0f;
    grass.testColor = irr::video::SColor(255, 80, 160, 80);  // Green
    config.detailTypes.push_back(grass);

    DetailType tallGrass;
    tallGrass.name = "tall_grass";
    tallGrass.category = DetailCategory::Grass;
    tallGrass.orientation = DetailOrientation::CrossedQuads;
    tallGrass.minSize = 1.0f;
    tallGrass.maxSize = 2.0f;
    tallGrass.baseDensity = 0.3f;
    tallGrass.maxSlope = 0.4f;
    tallGrass.windResponse = 1.0f;
    tallGrass.testColor = irr::video::SColor(255, 60, 140, 60);  // Darker green
    config.detailTypes.push_back(tallGrass);

    DetailType flower;
    flower.name = "flower";
    flower.category = DetailCategory::Plants;
    flower.orientation = DetailOrientation::CrossedQuads;
    flower.minSize = 0.4f;
    flower.maxSize = 0.8f;
    flower.baseDensity = 0.15f;
    flower.maxSlope = 0.35f;
    flower.windResponse = 0.7f;
    flower.testColor = irr::video::SColor(255, 200, 100, 200);  // Purple/pink
    config.detailTypes.push_back(flower);

    DetailType rock;
    rock.name = "rock";
    rock.category = DetailCategory::Rocks;
    rock.orientation = DetailOrientation::FlatGround;
    rock.minSize = 0.2f;
    rock.maxSize = 0.5f;
    rock.baseDensity = 0.1f;
    rock.maxSlope = 0.7f;
    rock.windResponse = 0.0f;
    rock.testColor = irr::video::SColor(255, 120, 110, 100);  // Gray/brown
    config.detailTypes.push_back(rock);

    return config;
}

} // namespace Detail
} // namespace Graphics
} // namespace EQT
