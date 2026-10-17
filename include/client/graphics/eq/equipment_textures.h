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
// Player races use IDs 0-4, 10-23, 240
// NPC-only textures are 5-9
enum class TextureType : uint8_t {
    // Player Race Textures (0-4)
    Naked = 0,           // Default skin texture
    Cloth = 1,           // Cloth armor
    Chain = 2,           // Chain armor
    Plate = 3,           // Plate armor
    Leather = 4,         // Leather armor

    // NPC Race Only Textures (5-9)
    NPCOnly5 = 5,
    NPCOnly6 = 6,
    NPCOnly7 = 7,
    NPCOnly8 = 8,
    NPCOnly9 = 9,

    // Robe Textures (10-16) - seven robe materials, clk04..clk10
    Robe1 = 10,          // Robe texture 1 (clk04)
    Robe2 = 11,          // Robe texture 2 (clk05)
    Robe3 = 12,          // Robe texture 3 (clk06)
    Robe4 = 13,          // Robe texture 4 (clk07)
    Robe5 = 14,          // Robe texture 5 (clk08)
    Robe6 = 15,          // Robe texture 6 (clk09)
    Robe7 = 16,          // Robe texture 7 (clk10)

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

// B1084: the robe range is ONE MEASURED fact (official-client oracle, lane 1,
// 2026-10-16; evidence in refactor/B1084/README.md): a worn robe of material
// 10 (item 11641 Crimson Robe of Alendine) draws the robe body mesh and the
// clk04 texture in the official client, and the shipped archives carry robe
// art for exactly materials 10..16 (global_chr.s3d: clk04..clk10,
// globaldaf_chr.s3d: dafch10xx..dafch16xx -- no 05..09). Everything that
// classifies a texture as robe goes through isRobeTexture(); a second spelling
// of the range is a regression (test_robe_texture_range).
constexpr uint8_t kRobeTextureMin = 10;
constexpr uint8_t kRobeTextureMax = 16;

// The ONE robe predicate. Every consumer of the robe range uses this.
inline bool isRobeTexture(uint8_t textureId) {
    return textureId >= kRobeTextureMin && textureId <= kRobeTextureMax;
}

inline bool isPlayerRaceTexture(uint8_t textureId) {
    return textureId <= 4 || (textureId >= 10 && textureId <= 23) || textureId == 240;
}

inline bool isNPCOnlyTexture(uint8_t textureId) {
    // 5-9 are NPC-only; 10 is a robe (isRobeTexture), not an NPC texture.
    return textureId >= 5 && textureId < kRobeTextureMin;
}

inline uint8_t getRobeIndex(uint8_t textureId) {
    // Robe index 1-7 for texture IDs 10-16 (texture 10 is the plain robe).
    return isRobeTexture(textureId) ? (textureId - kRobeTextureMin + 1) : 0;
}

inline uint8_t getRobeClkTextureNumber(uint8_t textureId) {
    // Maps a robe texture ID to its clk texture number (global_chr.s3d):
    // 10 -> clk04, 11 -> clk05, ..., 16 -> clk10.
    // Returns 0 if not a robe texture.
    if (!isRobeTexture(textureId)) return 0;
    static const uint8_t robeToClk[] = {4, 5, 6, 7, 8, 9, 10};
    return robeToClk[textureId - kRobeTextureMin];
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
