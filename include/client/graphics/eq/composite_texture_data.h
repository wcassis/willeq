#ifndef EQT_GRAPHICS_COMPOSITE_TEXTURE_DATA_H
#define EQT_GRAPHICS_COMPOSITE_TEXTURE_DATA_H

#include <map>
#include <string>
#include <fstream>
#include <algorithm>
#include <json/json.h>
#include "common/logging.h"

namespace EQT {
namespace Graphics {

// UV remap entry for a single body-part texture within a composite sheet.
// Produced by entity_composite_builder, consumed at runtime to map
// per-texture UVs into composite sheet coordinates.
struct CompositeTextureEntry {
    int x = 0, y = 0;            // Placement in sheet (pixels)
    int w = 0, h = 0;            // Unrolled size in sheet (pixels)
    int tilesU = 1, tilesV = 1;  // Tile counts used for UV wrapping
    float uvScaleU = 0, uvScaleV = 0;   // w/sheetW, h/sheetH
    float uvOffsetU = 0, uvOffsetV = 0; // x/sheetW, y/sheetH
};

// Composite sheet metadata for one race/gender/variant combination.
struct CompositeTextureData {
    int sheetWidth = 0;
    int sheetHeight = 0;
    std::string race;
    int gender = 0;
    int variant = 0;
    // P48: sheet-capability signal — true only if entity_composite_builder
    // wrote a real ETC1 alpha companion page for this sheet (at least one
    // source part was WLD-masked). Defaults false, so sheets built by a
    // pre-P48 builder (no "hasAlpha" JSON key at all) parse as false — the
    // safe "exclude masked races from composite eligibility" behavior for
    // stale/global sheets not yet regenerated (race_model_loader.cpp's
    // eligibility gate).
    bool hasAlpha = false;
    std::map<std::string, CompositeTextureEntry> textures; // keyed by lowercase texture name

    bool loadFromJson(const std::string& jsonPath) {
        std::ifstream file(jsonPath);
        if (!file.is_open()) {
            return false;
        }

        Json::Value root;
        Json::CharReaderBuilder builder;
        std::string errors;
        if (!Json::parseFromStream(builder, file, &root, &errors)) {
            LOG_WARN(MOD_GRAPHICS, "CompositeTextureData: JSON parse error in {}: {}", jsonPath, errors);
            return false;
        }

        sheetWidth = root.get("sheetWidth", 0).asInt();
        sheetHeight = root.get("sheetHeight", 0).asInt();
        race = root.get("race", "").asString();
        gender = root.get("gender", 0).asInt();
        variant = root.get("variant", 0).asInt();
        hasAlpha = root.get("hasAlpha", false).asBool();

        if (sheetWidth <= 0 || sheetHeight <= 0) {
            LOG_WARN(MOD_GRAPHICS, "CompositeTextureData: invalid sheet dimensions {}x{} in {}",
                     sheetWidth, sheetHeight, jsonPath);
            return false;
        }

        const Json::Value& texObj = root["textures"];
        if (!texObj.isObject()) {
            LOG_WARN(MOD_GRAPHICS, "CompositeTextureData: no 'textures' object in {}", jsonPath);
            return false;
        }

        for (const auto& name : texObj.getMemberNames()) {
            const Json::Value& entry = texObj[name];
            CompositeTextureEntry e;
            e.x = entry.get("x", 0).asInt();
            e.y = entry.get("y", 0).asInt();
            e.w = entry.get("w", 0).asInt();
            e.h = entry.get("h", 0).asInt();
            e.tilesU = entry.get("tilesU", 1).asInt();
            e.tilesV = entry.get("tilesV", 1).asInt();
            e.uvScaleU = static_cast<float>(entry.get("uvScaleU", 0.0).asDouble());
            e.uvScaleV = static_cast<float>(entry.get("uvScaleV", 0.0).asDouble());
            e.uvOffsetU = static_cast<float>(entry.get("uvOffsetU", 0.0).asDouble());
            e.uvOffsetV = static_cast<float>(entry.get("uvOffsetV", 0.0).asDouble());

            // Store with lowercase key
            std::string lowerName = name;
            std::transform(lowerName.begin(), lowerName.end(), lowerName.begin(),
                           [](unsigned char c) { return std::tolower(c); });
            textures[lowerName] = e;
        }

        LOG_DEBUG(MOD_GRAPHICS, "CompositeTextureData: loaded {} textures from {} ({}x{})",
                  textures.size(), jsonPath, sheetWidth, sheetHeight);
        return true;
    }

    const CompositeTextureEntry* lookup(const std::string& textureName) const {
        std::string lowerName = textureName;
        std::transform(lowerName.begin(), lowerName.end(), lowerName.begin(),
                       [](unsigned char c) { return std::tolower(c); });
        auto it = textures.find(lowerName);
        return (it != textures.end()) ? &it->second : nullptr;
    }
};

} // namespace Graphics
} // namespace EQT

#endif // EQT_GRAPHICS_COMPOSITE_TEXTURE_DATA_H
