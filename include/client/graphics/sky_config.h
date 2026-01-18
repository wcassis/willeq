#ifndef EQT_GRAPHICS_SKY_CONFIG_H
#define EQT_GRAPHICS_SKY_CONFIG_H

#include <string>
#include <map>
#include <vector>

namespace EQT {
namespace Graphics {

// Per-zone sky configuration from sky.ini
struct ZoneSkyConfig {
    std::string zoneName;
    std::string weatherType;        // e.g., "DefaultClear", "LuclinDay-0", "PoFire-0"

    // Horizon fade parameters
    float opaqueAngle = 0.0f;       // Angle below which sky is fully opaque
    float transparentAngle = 0.2f;  // Angle above which sky is fully transparent

    // Sky dome parameters
    float minAngle = 0.0f;
    float maxAngle = -0.1f;
    float minWidth = 0.1f;
    float maxWidth = 0.1f;

    // Camera height adjustment
    float minCameraZ = 0.0f;
    float maxCameraZ = 100.0f;

    // Whether sky is enabled (NULL weather type = no sky)
    bool skyEnabled = true;
};

// Weather type to sky layer mapping
struct WeatherTypeInfo {
    std::string name;               // Weather type name (e.g., "DefaultClear")
    int skyTypeId = 0;              // Maps to SkyLoader sky type
    bool hasDayNightCycle = true;   // Whether this weather has day/night variations
    std::string baseName;           // Base name without suffix (e.g., "Luclin" from "LuclinDay-0")
};

// Sky configuration parser for sky.ini
class SkyConfig {
public:
    SkyConfig() = default;
    ~SkyConfig() = default;

    // Load configuration from sky.ini file
    // path: full path to sky.ini
    // Returns true on success
    bool loadFromFile(const std::string& path);

    // Get configuration for a specific zone
    // Returns default config if zone not found
    ZoneSkyConfig getConfigForZone(const std::string& zoneName) const;

    // Get weather type for a zone
    // Returns "DefaultClear" if zone not found
    std::string getWeatherTypeForZone(const std::string& zoneName) const;

    // Get sky type ID for a weather type name
    // Returns 0 (default) if weather type not recognized
    int getSkyTypeIdForWeather(const std::string& weatherType) const;

    // Check if a zone has sky enabled
    bool isSkyEnabledForZone(const std::string& zoneName) const;

    // Check if loaded successfully
    bool isLoaded() const { return loaded_; }

    // Get error message if loading failed
    const std::string& getError() const { return error_; }

    // Get all zone names that have configurations
    std::vector<std::string> getConfiguredZones() const;

    // Get number of configured zones
    size_t getZoneCount() const { return zoneConfigs_.size(); }

    // Get the default configuration
    const ZoneSkyConfig& getDefaultConfig() const { return defaultConfig_; }

private:
    // Parse a single line of the INI file
    void parseLine(const std::string& line, std::string& currentSection);

    // Parse a zone config section
    void parseZoneSetting(const std::string& zoneName, const std::string& key, const std::string& value);

    // Build weather type to sky type mappings
    void buildWeatherTypeMappings();

    // Normalize zone name (lowercase, trim)
    static std::string normalizeZoneName(const std::string& name);

    std::map<std::string, ZoneSkyConfig> zoneConfigs_;
    std::map<std::string, WeatherTypeInfo> weatherTypes_;
    ZoneSkyConfig defaultConfig_;
    bool loaded_ = false;
    std::string error_;
};

} // namespace Graphics
} // namespace EQT

#endif // EQT_GRAPHICS_SKY_CONFIG_H
