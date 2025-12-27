#ifndef EQT_GRAPHICS_RACE_CODES_H
#define EQT_GRAPHICS_RACE_CODES_H

#include <cstdint>
#include <string>

namespace EQT {
namespace Graphics {

// EverQuest race IDs (Titanium era)
// Playable races: 1-12, 128, 130, 330
// Many NPC/monster races above these
enum class EQRace : uint16_t {
    Human = 1,
    Barbarian = 2,
    Erudite = 3,
    WoodElf = 4,
    HighElf = 5,
    DarkElf = 6,
    HalfElf = 7,
    Dwarf = 8,
    Troll = 9,
    Ogre = 10,
    Halfling = 11,
    Gnome = 12,
    Iksar = 128,
    VahShir = 130,
    Froglok = 330
};

// Gender codes
enum class EQGender : uint8_t {
    Male = 0,
    Female = 1,
    Neuter = 2
};

// Get a 3-letter race code (HUM, ELF, DWF, etc.)
// Returns base code for male; use getGenderedRaceCode() for female variant
std::string getRaceCode(uint16_t raceId);

// Convert male race code to female (for playable races)
// E.g., "HUM" -> "HUF", "BAM" -> "BAF"
std::string getGenderedRaceCode(const std::string& maleCode, uint8_t gender);

// Get zone-specific race code for races that have different models in zone _chr.s3d files
// Returns the zone-specific code (e.g., "QCM" for Qeynos Citizen Male) or empty string if no zone-specific model
std::string getZoneSpecificRaceCode(uint16_t raceId, uint8_t gender, const std::string& zoneName);

// Get fallback race code for zone-specific races when zone model not found
std::string getFallbackRaceCode(uint16_t raceId, uint8_t gender);

// Get the S3D filename for a race (for loading from zone archives)
std::string getRaceModelFilename(uint16_t raceId, uint8_t gender);

// Get race scale factor (some races are larger/smaller)
float getRaceScale(uint16_t raceId);

// Get human-readable race name
std::string getRaceName(uint16_t raceId);

// Get human-readable class name
std::string getClassName(uint8_t classId);

// Get human-readable gender string
std::string getGenderName(uint8_t gender);

} // namespace Graphics
} // namespace EQT

#endif // EQT_GRAPHICS_RACE_CODES_H
