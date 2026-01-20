#include "client/graphics/tree_wind_config.h"
#include "common/logging.h"
#include <json/json.h>
#include <fstream>
#include <sstream>

namespace EQT {
namespace Graphics {

bool TreeWindConfigLoader::loadFromFile(const std::string& filepath, TreeWindConfig& config) {
    std::ifstream file(filepath);
    if (!file.is_open()) {
        LOG_DEBUG(MOD_GRAPHICS, "TreeWindConfigLoader: Could not open {}", filepath);
        return false;
    }

    std::stringstream buffer;
    buffer << file.rdbuf();

    Json::Value root;
    Json::CharReaderBuilder builder;
    std::string errors;
    std::istringstream stream(buffer.str());

    if (!Json::parseFromStream(builder, stream, &root, &errors)) {
        LOG_ERROR(MOD_GRAPHICS, "TreeWindConfigLoader: JSON parse error in {}: {}", filepath, errors);
        return false;
    }

    // Get the tree_wind section
    const Json::Value& treeWind = root["tree_wind"];
    if (treeWind.isNull()) {
        LOG_ERROR(MOD_GRAPHICS, "TreeWindConfigLoader: Missing 'tree_wind' section in {}", filepath);
        return false;
    }

    // Parse base wind parameters
    if (treeWind.isMember("base_strength")) {
        config.baseStrength = treeWind["base_strength"].asFloat();
    }
    if (treeWind.isMember("base_frequency")) {
        config.baseFrequency = treeWind["base_frequency"].asFloat();
    }
    if (treeWind.isMember("gust_strength")) {
        config.gustStrength = treeWind["gust_strength"].asFloat();
    }
    if (treeWind.isMember("gust_frequency")) {
        config.gustFrequency = treeWind["gust_frequency"].asFloat();
    }
    if (treeWind.isMember("turbulence")) {
        config.turbulence = treeWind["turbulence"].asFloat();
    }

    // Parse height-based influence curve
    if (treeWind.isMember("influence_start_height")) {
        config.influenceStartHeight = treeWind["influence_start_height"].asFloat();
    }
    if (treeWind.isMember("influence_exponent")) {
        config.influenceExponent = treeWind["influence_exponent"].asFloat();
    }

    // Parse weather multipliers
    const Json::Value& weatherMults = treeWind["weather_multipliers"];
    if (!weatherMults.isNull()) {
        if (weatherMults.isMember("calm")) {
            config.calmMultiplier = weatherMults["calm"].asFloat();
        }
        if (weatherMults.isMember("normal")) {
            config.normalMultiplier = weatherMults["normal"].asFloat();
        }
        if (weatherMults.isMember("rain")) {
            config.rainMultiplier = weatherMults["rain"].asFloat();
        }
        if (weatherMults.isMember("storm")) {
            config.stormMultiplier = weatherMults["storm"].asFloat();
        }
    }

    // Parse performance settings
    if (treeWind.isMember("update_distance")) {
        config.updateDistance = treeWind["update_distance"].asFloat();
    }
    if (treeWind.isMember("lod_distance")) {
        config.lodDistance = treeWind["lod_distance"].asFloat();
    }
    if (treeWind.isMember("enabled")) {
        config.enabled = treeWind["enabled"].asBool();
    }

    LOG_INFO(MOD_GRAPHICS, "TreeWindConfigLoader: Loaded config from {}", filepath);
    LOG_DEBUG(MOD_GRAPHICS, "TreeWindConfigLoader: baseStrength={}, baseFrequency={}, enabled={}",
              config.baseStrength, config.baseFrequency, config.enabled);

    return true;
}

std::string TreeWindConfigLoader::findConfigFile(const std::string& cmdLinePath,
                                                  const std::string& zoneName) {
    // 1. Command line specified path
    if (!cmdLinePath.empty()) {
        std::ifstream test(cmdLinePath);
        if (test.good()) {
            return cmdLinePath;
        }
        LOG_WARN(MOD_GRAPHICS, "TreeWindConfigLoader: Command line config not found: {}", cmdLinePath);
    }

    // 2. Zone-specific config
    if (!zoneName.empty()) {
        std::string zonePath = "data/config/zones/" + zoneName + "/tree_wind.json";
        std::ifstream test(zonePath);
        if (test.good()) {
            return zonePath;
        }
    }

    // 3. Default config
    std::string defaultPath = "data/config/tree_wind.json";
    std::ifstream test(defaultPath);
    if (test.good()) {
        return defaultPath;
    }

    // 4. No config file found - will use built-in defaults
    return "";
}

bool TreeWindConfigLoader::loadConfig(TreeWindConfig& config,
                                      const std::string& cmdLinePath,
                                      const std::string& zoneName) {
    // Start with defaults
    config = TreeWindConfig{};

    // Find and load config file
    std::string configPath = findConfigFile(cmdLinePath, zoneName);
    if (configPath.empty()) {
        LOG_INFO(MOD_GRAPHICS, "TreeWindConfigLoader: No config file found, using built-in defaults");
        return true;  // Using defaults is not an error
    }

    return loadFromFile(configPath, config);
}

bool TreeWindConfigLoader::saveToFile(const std::string& filepath, const TreeWindConfig& config) {
    Json::Value root;
    Json::Value& treeWind = root["tree_wind"];

    // Base wind parameters
    treeWind["base_strength"] = config.baseStrength;
    treeWind["base_frequency"] = config.baseFrequency;
    treeWind["gust_strength"] = config.gustStrength;
    treeWind["gust_frequency"] = config.gustFrequency;
    treeWind["turbulence"] = config.turbulence;

    // Height-based influence curve
    treeWind["influence_start_height"] = config.influenceStartHeight;
    treeWind["influence_exponent"] = config.influenceExponent;

    // Weather multipliers
    Json::Value& weatherMults = treeWind["weather_multipliers"];
    weatherMults["calm"] = config.calmMultiplier;
    weatherMults["normal"] = config.normalMultiplier;
    weatherMults["rain"] = config.rainMultiplier;
    weatherMults["storm"] = config.stormMultiplier;

    // Performance settings
    treeWind["update_distance"] = config.updateDistance;
    treeWind["lod_distance"] = config.lodDistance;
    treeWind["enabled"] = config.enabled;

    // Write to file
    std::ofstream file(filepath);
    if (!file.is_open()) {
        LOG_ERROR(MOD_GRAPHICS, "TreeWindConfigLoader: Could not open {} for writing", filepath);
        return false;
    }

    Json::StreamWriterBuilder builder;
    builder["indentation"] = "    ";
    std::unique_ptr<Json::StreamWriter> writer(builder.newStreamWriter());
    writer->write(root, &file);
    file << std::endl;

    LOG_INFO(MOD_GRAPHICS, "TreeWindConfigLoader: Saved config to {}", filepath);
    return true;
}

} // namespace Graphics
} // namespace EQT
