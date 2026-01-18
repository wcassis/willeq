#include "client/graphics/sky_config.h"
#include "common/logging.h"
#include <fstream>
#include <sstream>
#include <algorithm>
#include <cctype>

namespace EQT {
namespace Graphics {

bool SkyConfig::loadFromFile(const std::string& path) {
    zoneConfigs_.clear();
    weatherTypes_.clear();
    loaded_ = false;
    error_.clear();

    std::ifstream file(path);
    if (!file.is_open()) {
        error_ = "Failed to open sky.ini: " + path;
        LOG_ERROR(MOD_GRAPHICS, "{}", error_);
        return false;
    }

    // Set up default config
    defaultConfig_.zoneName = "default";
    defaultConfig_.weatherType = "DefaultClear";
    defaultConfig_.opaqueAngle = 0.0f;
    defaultConfig_.transparentAngle = 0.2f;
    defaultConfig_.minAngle = 0.0f;
    defaultConfig_.maxAngle = -0.1f;
    defaultConfig_.minWidth = 0.1f;
    defaultConfig_.maxWidth = 0.1f;
    defaultConfig_.minCameraZ = 0.0f;
    defaultConfig_.maxCameraZ = 100.0f;
    defaultConfig_.skyEnabled = true;

    std::string line;
    std::string currentSection;

    while (std::getline(file, line)) {
        parseLine(line, currentSection);
    }

    file.close();

    // Build weather type mappings
    buildWeatherTypeMappings();

    loaded_ = true;
    LOG_INFO(MOD_GRAPHICS, "Loaded sky.ini: {} zone configurations", zoneConfigs_.size());

    return true;
}

void SkyConfig::parseLine(const std::string& line, std::string& currentSection) {
    // Trim whitespace
    std::string trimmed = line;
    trimmed.erase(0, trimmed.find_first_not_of(" \t\r\n"));
    trimmed.erase(trimmed.find_last_not_of(" \t\r\n") + 1);

    // Skip empty lines and comments
    if (trimmed.empty() || trimmed[0] == '#' || trimmed[0] == ';') {
        return;
    }

    // Check for section header
    if (trimmed[0] == '[' && trimmed.back() == ']') {
        currentSection = trimmed.substr(1, trimmed.size() - 2);

        // Check if this is a zone settings section
        const std::string prefix = "SkySetting-";
        if (currentSection.compare(0, prefix.size(), prefix) == 0) {
            std::string zoneName = currentSection.substr(prefix.size());
            zoneName = normalizeZoneName(zoneName);

            // Create config entry if it doesn't exist
            if (zoneConfigs_.find(zoneName) == zoneConfigs_.end()) {
                ZoneSkyConfig config;
                config.zoneName = zoneName;
                // Start with default values
                config.weatherType = "DefaultClear";
                config.opaqueAngle = 0.0f;
                config.transparentAngle = 0.2f;
                config.minAngle = 0.0f;
                config.maxAngle = -0.1f;
                config.minWidth = 0.1f;
                config.maxWidth = 0.1f;
                config.minCameraZ = 0.0f;
                config.maxCameraZ = 100.0f;
                config.skyEnabled = true;
                zoneConfigs_[zoneName] = config;
            }
        }
        return;
    }

    // Parse key=value
    size_t eqPos = trimmed.find('=');
    if (eqPos == std::string::npos) {
        return;
    }

    std::string key = trimmed.substr(0, eqPos);
    std::string value = trimmed.substr(eqPos + 1);

    // Trim key and value
    key.erase(0, key.find_first_not_of(" \t"));
    key.erase(key.find_last_not_of(" \t") + 1);
    value.erase(0, value.find_first_not_of(" \t"));
    value.erase(value.find_last_not_of(" \t") + 1);

    // Handle [SkySettings] section (zone list)
    if (currentSection == "SkySettings") {
        // Zone names are the keys, values are empty
        // We'll create configs when we encounter [SkySetting-zonename]
        return;
    }

    // Handle [SkySetting-zonename] sections
    const std::string prefix = "SkySetting-";
    if (currentSection.compare(0, prefix.size(), prefix) == 0) {
        std::string zoneName = currentSection.substr(prefix.size());
        zoneName = normalizeZoneName(zoneName);
        parseZoneSetting(zoneName, key, value);
        return;
    }

    // Handle [default] sections (usually just Enabled=1, can ignore)
    if (currentSection == "default") {
        return;
    }
}

void SkyConfig::parseZoneSetting(const std::string& zoneName, const std::string& key, const std::string& value) {
    auto it = zoneConfigs_.find(zoneName);
    if (it == zoneConfigs_.end()) {
        // Create new config
        ZoneSkyConfig config;
        config.zoneName = zoneName;
        zoneConfigs_[zoneName] = config;
        it = zoneConfigs_.find(zoneName);
    }

    ZoneSkyConfig& config = it->second;

    try {
        if (key == "DefaultWeather") {
            config.weatherType = value;
            // Check if sky is disabled (NULL weather)
            if (value == "NULL" || value == "null") {
                config.skyEnabled = false;
            }
        } else if (key == "OpaqueAngle") {
            config.opaqueAngle = std::stof(value);
        } else if (key == "TransparentAngle") {
            config.transparentAngle = std::stof(value);
        } else if (key == "MinAngle") {
            config.minAngle = std::stof(value);
        } else if (key == "MaxAngle") {
            config.maxAngle = std::stof(value);
        } else if (key == "MinWidth") {
            config.minWidth = std::stof(value);
        } else if (key == "MaxWidth") {
            config.maxWidth = std::stof(value);
        } else if (key == "MinCameraZ") {
            config.minCameraZ = std::stof(value);
        } else if (key == "MaxCameraZ") {
            config.maxCameraZ = std::stof(value);
        }
        // Ignore unknown keys
    } catch (const std::exception& e) {
        LOG_WARN(MOD_GRAPHICS, "Failed to parse sky.ini value for {}/{}: {}", zoneName, key, e.what());
    }

    // Update default config if this is the "default" zone
    if (zoneName == "default") {
        defaultConfig_ = config;
    }
}

void SkyConfig::buildWeatherTypeMappings() {
    // Map weather type names to sky type IDs
    // These IDs correspond to the SkyLoader sky types

    // Classic Norrath skies
    weatherTypes_["DefaultClear"] = {"DefaultClear", 0, true, "Default"};
    weatherTypes_["default"] = {"default", 0, true, "Default"};

    // Luclin skies (type 6)
    weatherTypes_["LuclinDay-0"] = {"LuclinDay-0", 6, true, "Luclin"};
    weatherTypes_["LuclinNight-0"] = {"LuclinNight-0", 6, true, "Luclin"};
    weatherTypes_["LuclinDawn"] = {"LuclinDawn", 6, true, "Luclin"};
    weatherTypes_["LuclinDusk-0"] = {"LuclinDusk-0", 6, true, "Luclin"};

    // Planes of Power skies
    weatherTypes_["PoAir-0"] = {"PoAir-0", 17, false, "PoAir"};        // Layer 171/173
    weatherTypes_["PoFire-0"] = {"PoFire-0", 11, false, "PoFire"};     // Layer 111/113
    weatherTypes_["PoStorms-0"] = {"PoStorms-0", 12, false, "PoStorms"}; // Layer 121/123
    weatherTypes_["PoFear-0"] = {"PoFear-0", 5, false, "PoFear"};      // Layer 51/53 (red/fear)
    weatherTypes_["PoNightmare-0"] = {"PoNightmare-0", 10, false, "PoNightmare"}; // The Grey
    weatherTypes_["PoDisease-0"] = {"PoDisease-0", 10, false, "PoDisease"}; // Similar to The Grey
    weatherTypes_["PoWar-0"] = {"PoWar-0", 14, false, "PoWar"};        // Layer 141/143

    // Omens of War skies
    weatherTypes_["OmensClear"] = {"OmensClear", 0, false, "Omens"};
    weatherTypes_["Omens-0"] = {"Omens-0", 0, false, "Omens"};

    // Special skies
    weatherTypes_["NULL"] = {"NULL", -1, false, "NULL"};              // No sky
    weatherTypes_["lava"] = {"lava", 11, false, "lava"};              // Use PoFire-like
    weatherTypes_["clz-0"] = {"clz-0", 0, true, "clz"};               // Guild Lobby etc.
    weatherTypes_["darkhollow-0"] = {"darkhollow-0", 10, false, "darkhollow"}; // Dark/grey
    weatherTypes_["illsalin-0"] = {"illsalin-0", 10, false, "illsalin"};
    weatherTypes_["creep-0"] = {"creep-0", 10, false, "creep"};
    weatherTypes_["scarlet"] = {"scarlet", 5, false, "scarlet"};      // Red sky
    weatherTypes_["nektulos-0"] = {"nektulos-0", 0, true, "nektulos"};

    LOG_DEBUG(MOD_GRAPHICS, "Built {} weather type mappings", weatherTypes_.size());
}

std::string SkyConfig::normalizeZoneName(const std::string& name) {
    std::string normalized = name;
    // Convert to lowercase
    std::transform(normalized.begin(), normalized.end(), normalized.begin(),
                  [](unsigned char c) { return std::tolower(c); });
    // Trim whitespace
    normalized.erase(0, normalized.find_first_not_of(" \t\r\n"));
    normalized.erase(normalized.find_last_not_of(" \t\r\n") + 1);
    return normalized;
}

ZoneSkyConfig SkyConfig::getConfigForZone(const std::string& zoneName) const {
    std::string normalized = normalizeZoneName(zoneName);

    auto it = zoneConfigs_.find(normalized);
    if (it != zoneConfigs_.end()) {
        return it->second;
    }

    // Return default config
    return defaultConfig_;
}

std::string SkyConfig::getWeatherTypeForZone(const std::string& zoneName) const {
    std::string normalized = normalizeZoneName(zoneName);

    auto it = zoneConfigs_.find(normalized);
    if (it != zoneConfigs_.end()) {
        return it->second.weatherType;
    }

    return defaultConfig_.weatherType;
}

int SkyConfig::getSkyTypeIdForWeather(const std::string& weatherType) const {
    auto it = weatherTypes_.find(weatherType);
    if (it != weatherTypes_.end()) {
        return it->second.skyTypeId;
    }

    // Try to parse weather type patterns
    // Format: BaseName-N or BaseName or BaseNameVariant-N
    std::string base = weatherType;

    // Remove trailing -N suffix
    size_t dashPos = base.rfind('-');
    if (dashPos != std::string::npos && dashPos < base.size() - 1) {
        bool isNumber = true;
        for (size_t i = dashPos + 1; i < base.size(); ++i) {
            if (!std::isdigit(base[i])) {
                isNumber = false;
                break;
            }
        }
        if (isNumber) {
            base = base.substr(0, dashPos);
        }
    }

    // Check for Luclin variants
    if (base.find("Luclin") != std::string::npos) {
        return 6; // Luclin sky type
    }

    // Check for PoP variants
    if (base.find("Po") == 0 || base.find("po") == 0) {
        if (base.find("Fire") != std::string::npos || base.find("fire") != std::string::npos) {
            return 11;
        }
        if (base.find("Storm") != std::string::npos || base.find("storm") != std::string::npos) {
            return 12;
        }
        if (base.find("Air") != std::string::npos || base.find("air") != std::string::npos) {
            return 17;
        }
        if (base.find("War") != std::string::npos || base.find("war") != std::string::npos) {
            return 14;
        }
        if (base.find("Tranq") != std::string::npos || base.find("tranq") != std::string::npos) {
            return 16;
        }
    }

    // Check for fear/red variants
    if (base.find("Fear") != std::string::npos || base.find("fear") != std::string::npos ||
        base.find("red") != std::string::npos || base.find("scarlet") != std::string::npos) {
        return 5;
    }

    // Check for grey/dark variants
    if (base.find("Grey") != std::string::npos || base.find("grey") != std::string::npos ||
        base.find("dark") != std::string::npos || base.find("Nightmare") != std::string::npos ||
        base.find("Disease") != std::string::npos) {
        return 10;
    }

    // Default to type 0
    return 0;
}

bool SkyConfig::isSkyEnabledForZone(const std::string& zoneName) const {
    std::string normalized = normalizeZoneName(zoneName);

    auto it = zoneConfigs_.find(normalized);
    if (it != zoneConfigs_.end()) {
        return it->second.skyEnabled;
    }

    return defaultConfig_.skyEnabled;
}

std::vector<std::string> SkyConfig::getConfiguredZones() const {
    std::vector<std::string> zones;
    zones.reserve(zoneConfigs_.size());

    for (const auto& [name, config] : zoneConfigs_) {
        zones.push_back(name);
    }

    return zones;
}

} // namespace Graphics
} // namespace EQT
