#pragma once

#ifdef WITH_AUDIO

#include <cstdint>
#include <string>

namespace EQT {
namespace Audio {

// Player character sound types
enum class PlayerSoundType {
    Death,      // Death cry
    Drown,      // Drowning sound
    Jump,       // Jump/landing sound
    GetHit1,    // Hit sound variant 1
    GetHit2,    // Hit sound variant 2
    GetHit3,    // Hit sound variant 3
    GetHit4,    // Hit sound variant 4
    Gasp1,      // Gasp/breathing sound 1
    Gasp2       // Gasp/breathing sound 2
};

// Race categories for player sounds
// Different races share sound files based on their voice type
enum class RaceCategory {
    Standard,   // Human, Erudite, Dark Elf, Troll, Ogre, Iksar, Dwarf, Vah Shir, Froglok
    Barbarian,  // Barbarian only (deeper voice)
    Light       // Wood Elf, High Elf, Half Elf, Halfling, Gnome (lighter voice)
};

// Player sound utility class
// Provides race/gender-specific sound file names for player character sounds
class PlayerSounds {
public:
    // Get the sound filename for a specific sound type, race, and gender
    // Returns the filename (e.g., "Death_M.WAV" for standard male death)
    // Returns empty string if race or gender is invalid
    static std::string getSoundFile(PlayerSoundType type, uint16_t raceId, uint8_t gender);

    // Get the race category for sound selection
    static RaceCategory getRaceCategory(uint16_t raceId);

    // Get the sound suffix based on race and gender
    // Returns one of: "_M", "_F", "_MB", "_FB", "_ML", "_FL"
    static std::string getSuffix(uint16_t raceId, uint8_t gender);

    // Check if a race is valid (playable race)
    static bool isValidRace(uint16_t raceId);

    // Check if a gender is valid (male=0, female=1)
    static bool isValidGender(uint8_t gender);

    // Get sound ID for a player sound type, race, and gender
    // This maps to the IDs in SoundAssets.txt
    // Returns 0 if not found
    static uint32_t getSoundId(PlayerSoundType type, uint16_t raceId, uint8_t gender);

private:
    // Base sound IDs for standard male (from SoundAssets.txt)
    // Other variants are at fixed offsets from these
    static constexpr uint32_t SOUND_ID_GETHIT1_M = 33;
    static constexpr uint32_t SOUND_ID_GETHIT2_M = 34;
    static constexpr uint32_t SOUND_ID_GETHIT3_M = 35;
    static constexpr uint32_t SOUND_ID_GETHIT4_M = 36;
    static constexpr uint32_t SOUND_ID_GASP1_M = 37;
    static constexpr uint32_t SOUND_ID_GASP2_M = 38;
    static constexpr uint32_t SOUND_ID_DEATH_M = 39;
    static constexpr uint32_t SOUND_ID_DROWN_M = 40;
    static constexpr uint32_t SOUND_ID_JUMP_M = 32;

    // Offsets for different race/gender categories
    // Female standard: +10 from male standard
    // Male light: +20 from male standard
    // Female light: +30 from male standard
    // Male barbarian: +40 from male standard
    // Female barbarian: +50 from male standard
    static constexpr int32_t OFFSET_FEMALE_STANDARD = 10;
    static constexpr int32_t OFFSET_MALE_LIGHT = 20;
    static constexpr int32_t OFFSET_FEMALE_LIGHT = 30;
    static constexpr int32_t OFFSET_MALE_BARBARIAN = 40;
    static constexpr int32_t OFFSET_FEMALE_BARBARIAN = 50;
};

} // namespace Audio
} // namespace EQT

#endif // WITH_AUDIO
