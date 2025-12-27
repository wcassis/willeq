#ifndef EQT_GRAPHICS_EQUIPMENT_TEXTURES_H
#define EQT_GRAPHICS_EQUIPMENT_TEXTURES_H

#include <cstdint>
#include <string>

namespace EQT {
namespace Graphics {

// Equipment texture slots (matches TextureProfile order)
enum class EquipSlot : uint8_t {
    Head = 0,
    Chest = 1,
    Arms = 2,
    Wrist = 3,
    Hands = 4,
    Legs = 5,
    Feet = 6,
    Primary = 7,
    Secondary = 8
};

// Texture type IDs for player races and NPCs
// Player races use IDs 0-4, 11-23, 240
// NPC races use IDs 5-10
enum class TextureType : uint8_t {
    // Player Race Textures (0-4)
    Naked = 0,           // Default skin texture
    Cloth = 1,           // Cloth armor
    Chain = 2,           // Chain armor
    Plate = 3,           // Plate armor
    Leather = 4,         // Leather armor

    // NPC Race Only Textures (5-10)
    NPCOnly5 = 5,
    NPCOnly6 = 6,
    NPCOnly7 = 7,
    NPCOnly8 = 8,
    NPCOnly9 = 9,
    NPCOnly10 = 10,

    // Robe Textures (11-16) - Robe number = texture ID - 10
    Robe1 = 11,          // Robe texture 1
    Robe2 = 12,          // Robe texture 2
    Robe3 = 13,          // Robe texture 3
    Robe4 = 14,          // Robe texture 4
    Robe5 = 15,          // Robe texture 5
    Robe6 = 16,          // Robe texture 6

    // Velious Armor Textures (17-23)
    VeliousLeather1 = 17,
    VeliousChain1 = 18,
    VeliousPlate1 = 19,
    VeliousLeather2 = 20,
    VeliousChain2 = 21,
    VeliousPlate2 = 22,
    VeliousMonk = 23,

    // Special Textures
    VeliousHelmets = 240
};

// Helper functions for texture type classification
inline bool isPlayerRaceTexture(uint8_t textureId) {
    return textureId <= 4 || (textureId >= 11 && textureId <= 23) || textureId == 240;
}

inline bool isNPCOnlyTexture(uint8_t textureId) {
    return textureId >= 5 && textureId <= 10;
}

inline bool isRobeTexture(uint8_t textureId) {
    return textureId >= 11 && textureId <= 16;
}

inline uint8_t getRobeIndex(uint8_t textureId) {
    // Robe index 1-6 for texture IDs 11-16
    return isRobeTexture(textureId) ? (textureId - 10) : 0;
}

inline uint8_t getRobeClkTextureNumber(uint8_t textureId) {
    // Maps robe texture ID to clk texture number
    // Robe 1 (ID 11) -> clk04, Robe 2 (ID 12) -> clk05, etc.
    // Returns 0 if not a robe texture
    if (!isRobeTexture(textureId)) return 0;
    uint8_t robeIndex = textureId - 10;  // 1-6
    // Lookup table handles any potential gaps in texture numbering
    static const uint8_t robeToClk[] = {0, 4, 5, 6, 7, 8, 9};
    return (robeIndex <= 6) ? robeToClk[robeIndex] : 0;
}

inline bool isVeliousTexture(uint8_t textureId) {
    return textureId >= 17 && textureId <= 23;
}

// Legacy alias for backwards compatibility
using ArmorMaterial = TextureType;

// Get equipment texture name for a body part based on material ID
// Returns empty string if no equipment texture (use default body texture)
std::string getEquipmentTextureName(const std::string& raceCode, EquipSlot slot, uint32_t materialId);

// Body part codes for texture naming
std::string getBodyPartCode(EquipSlot slot);

// Transform a base texture name to an equipment variant texture name
// Pattern: {race}{part}00{page}.bmp -> {race}{part}{variant:02d}{page}.bmp
// e.g., humch0001.bmp with variant 1 -> humch0101.bmp
// Returns original name if pattern doesn't match or variant is 0
std::string getVariantTextureName(const std::string& baseTexName, uint8_t variant);

} // namespace Graphics
} // namespace EQT

#endif // EQT_GRAPHICS_EQUIPMENT_TEXTURES_H
