#ifndef EQT_GRAPHICS_DETAIL_CONFIG_LOADER_H
#define EQT_GRAPHICS_DETAIL_CONFIG_LOADER_H

#include <string>
#include <memory>
#include "client/graphics/detail/detail_types.h"
#include "client/graphics/detail/detail_texture_atlas.h"
#include "client/graphics/detail/seasonal_controller.h"

namespace EQT {
namespace Graphics {
namespace Detail {

// Loads detail configuration from JSON files
class DetailConfigLoader {
public:
    DetailConfigLoader() = default;
    ~DetailConfigLoader() = default;

    // Load configuration for a zone
    // Tries zone-specific config first, falls back to default
    // Returns nullptr if no config could be loaded
    std::unique_ptr<ZoneDetailConfig> loadConfigForZone(const std::string& zoneName,
                                                         const std::string& dataPath) const;

    // Load the default configuration
    std::unique_ptr<ZoneDetailConfig> loadDefaultConfig(const std::string& dataPath) const;

    // Load a specific config file
    std::unique_ptr<ZoneDetailConfig> loadConfigFile(const std::string& filepath) const;

    // Get the default hardcoded config (fallback when no files exist)
    static ZoneDetailConfig getHardcodedDefault(const std::string& zoneName);

private:
    // Parse JSON into config structure
    bool parseConfigJson(const std::string& json, ZoneDetailConfig& config) const;

    // Parse a single detail type from JSON
    bool parseDetailType(const std::string& typeJson, DetailType& type) const;

    // Convert category string to enum
    static DetailCategory parseCategoryString(const std::string& str);

    // Convert orientation string to enum
    static DetailOrientation parseOrientationString(const std::string& str);

    // Convert surface type string to enum
    static SurfaceType parseSurfaceTypeString(const std::string& str);

    // Convert atlas tile string to enum
    static AtlasTile parseAtlasTileString(const std::string& str);
};

} // namespace Detail
} // namespace Graphics
} // namespace EQT

#endif // EQT_GRAPHICS_DETAIL_CONFIG_LOADER_H
