#include "client/graphics/eq/equipment_textures.h"
#include <algorithm>
#include <cstdio>

namespace EQT {
namespace Graphics {

std::string getBodyPartCode(EquipSlot slot) {
    switch (slot) {
        case EquipSlot::Head:   return "he";
        case EquipSlot::Chest:  return "ch";
        case EquipSlot::Arms:   return "ua";  // Upper arms
        case EquipSlot::Wrist:  return "fa";  // Forearms
        case EquipSlot::Hands:  return "hn";
        case EquipSlot::Legs:   return "lg";
        case EquipSlot::Feet:   return "ft";
        default: return "";
    }
}

std::string getEquipmentTextureName(const std::string& raceCode, EquipSlot slot, uint32_t materialId) {
    if (materialId == 0) {
        return "";  // No equipment (Naked), use default body texture
    }

    // Texture Type IDs:
    // 0 = Naked (default skin)
    // 1 = Cloth
    // 2 = Chain
    // 3 = Plate
    // 4 = Leather
    // 5-9 = NPC Race Only
    // 10-16 = Robe 1-7 (10->clk04, 11->clk05, ..., 16->clk10)
    // 17 = Velious Leather 1
    // 18 = Velious Chain 1
    // 19 = Velious Plate 1
    // 20 = Velious Leather 2
    // 21 = Velious Chain 2
    // 22 = Velious Plate 2
    // 23 = Velious Monk
    // 240 = Velious Helmets
    uint8_t textureType = materialId & 0xFF;

    // For weapons (Primary/Secondary), return empty - they're 3D models, not body textures
    if (slot == EquipSlot::Primary || slot == EquipSlot::Secondary) {
        return "";
    }

    // Build equipment texture name based on texture type
    std::string texName;

    // Helper to get race-specific body texture with variant
    // Texture naming: {race}{part}{variant:02d}{page:02d}.bmp
    // e.g., humch0101.bmp = human chest, variant 01, page 01
    auto getRaceBodyTexture = [&](int variant, int page = 1) -> std::string {
        std::string lowerRace = raceCode;
        std::transform(lowerRace.begin(), lowerRace.end(), lowerRace.begin(),
                      [](unsigned char c) { return std::tolower(c); });
        std::string partCode = getBodyPartCode(slot);
        if (!partCode.empty()) {
            char buf[32];
            snprintf(buf, sizeof(buf), "%s%s%02d%02d.bmp", lowerRace.c_str(), partCode.c_str(), variant, page);
            return std::string(buf);
        }
        return "";
    };

    switch (textureType) {
        case 1:  // Cloth
            // Cloth uses race-specific body textures with variant 01
            texName = getRaceBodyTexture(1);
            break;

        case 2:  // Chain
            // Chain textures: chain01.bmp, chain02.bmp, chain04.bmp, chain05.bmp
            {
                int variant = ((materialId >> 8) & 0xFF) % 4;
                int chainNums[] = {1, 2, 4, 5};
                char buf[32];
                snprintf(buf, sizeof(buf), "chain%02d.bmp", chainNums[variant]);
                texName = buf;
            }
            break;

        case 3:  // Plate
            // Plate uses chain textures in classic EQ (same texture set)
            {
                int variant = ((materialId >> 8) & 0xFF) % 4;
                int chainNums[] = {1, 2, 4, 5};
                char buf[32];
                snprintf(buf, sizeof(buf), "chain%02d.bmp", chainNums[variant]);
                texName = buf;
            }
            break;

        case 4:  // Leather
            // Leather uses race-specific body textures with variant 02
            texName = getRaceBodyTexture(2);
            break;

        case 5: case 6: case 7: case 8: case 9:  // NPC Race Only
            // NPC-specific textures - use default body texture for the NPC
            // These are handled by the NPC's race-specific model textures
            break;

        case 10: case 11: case 12: case 13: case 14: case 15: case 16:  // Robe 1-7
            // Robes: texture ID 10 = Robe 1, 11 = Robe 2, etc.
            // Material 10 -> clk04, Material 11 -> clk05, ..., Material 16 -> clk10
            {
                int clkNumber = textureType - 10 + 4;  // 10->4, 11->5, ..., 16->10
                int variant = ((materialId >> 8) & 0xFF) % 6 + 1;  // Variant 1-6
                char buf[32];
                snprintf(buf, sizeof(buf), "clk%02d%02d.bmp", clkNumber, variant);
                texName = buf;
            }
            break;

        case 17:  // Velious Leather 1
            texName = getRaceBodyTexture(2);  // Use leather variant
            break;

        case 18:  // Velious Chain 1
            {
                char buf[32];
                snprintf(buf, sizeof(buf), "chain%02d.bmp", 1);
                texName = buf;
            }
            break;

        case 19:  // Velious Plate 1
            {
                char buf[32];
                snprintf(buf, sizeof(buf), "chain%02d.bmp", 4);
                texName = buf;
            }
            break;

        case 20:  // Velious Leather 2
            texName = getRaceBodyTexture(3);  // Use leather variant 2
            break;

        case 21:  // Velious Chain 2
            {
                char buf[32];
                snprintf(buf, sizeof(buf), "chain%02d.bmp", 2);
                texName = buf;
            }
            break;

        case 22:  // Velious Plate 2
            {
                char buf[32];
                snprintf(buf, sizeof(buf), "chain%02d.bmp", 5);
                texName = buf;
            }
            break;

        case 23:  // Velious Monk
            texName = getRaceBodyTexture(4);  // Use monk variant
            break;

        case 240:  // Velious Helmets
            // Special helmet textures - only applies to head slot
            if (slot == EquipSlot::Head) {
                texName = getRaceBodyTexture(5);  // Velious helmet variant
            }
            break;

        default:
            // Unknown texture type, return empty
            break;
    }

    return texName;
}

std::string getVariantTextureName(const std::string& baseTexName, uint8_t variant) {
    if (variant == 0) {
        return baseTexName;  // No variant, use original texture
    }

    std::string lowerTexName = baseTexName;
    std::transform(lowerTexName.begin(), lowerTexName.end(), lowerTexName.begin(),
                  [](unsigned char c) { return std::tolower(c); });

    // Pattern: {race}{part}00{page}.bmp -> {race}{part}{variant:02d}{page}.bmp
    // e.g., humch0001.bmp with variant 1 -> humch0101.bmp
    // The "00" represents the base variant (naked), we replace with the target variant
    size_t pos = lowerTexName.find("00");
    if (pos != std::string::npos && pos >= 3 && lowerTexName.length() > pos + 4) {
        // Verify this looks like a body texture (has digits after the "00")
        if (pos + 2 < lowerTexName.size() && std::isdigit(lowerTexName[pos + 2])) {
            char variantStr[8];
            snprintf(variantStr, sizeof(variantStr), "%02d", variant);
            return lowerTexName.substr(0, pos) + variantStr + lowerTexName.substr(pos + 2);
        }
    }

    return baseTexName;  // Pattern doesn't match, return original
}

} // namespace Graphics
} // namespace EQT
