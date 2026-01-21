#pragma once

#ifdef WITH_AUDIO

#include <string>
#include <cstdint>

namespace EQT {
namespace Audio {

// Player's state relative to water
enum class WaterState : uint8_t {
    NotInWater = 0,   // On land, not touching water
    OnSurface = 1,    // On water surface (swimming)
    Submerged = 2     // Fully underwater
};

// Sound IDs from SoundAssets.txt for water-related sounds
namespace WaterSoundIds {
    constexpr uint32_t WaterIn = 100;      // waterin.wav - Enter water
    constexpr uint32_t WaterTread1 = 101;  // wattrd_1.wav - Swimming/treading water variant 1
    constexpr uint32_t WaterTread2 = 102;  // wattrd_2.wav - Swimming/treading water variant 2
    constexpr uint32_t Underwater = 161;   // watundlp.wav - Underwater ambient loop
}

// Utility class for water sound file mappings
// Provides static methods to get sound file names for water-related events
class WaterSounds {
public:
    // Get the sound file for entering water
    // Returns: "waterin.wav"
    static const char* getEntrySound();

    // Get a swimming/treading water sound variant
    // variant: 0 or 1 (wraps around)
    // Returns: "wattrd_1.wav" or "wattrd_2.wav"
    static const char* getTreadSound(int variant);

    // Get the underwater ambient loop sound
    // Returns: "watundlp.wav"
    static const char* getUnderwaterLoop();

    // Get sound ID for water entry
    static uint32_t getEntrySoundId() { return WaterSoundIds::WaterIn; }

    // Get sound ID for treading water
    // variant: 0 or 1 (wraps around)
    static uint32_t getTreadSoundId(int variant);

    // Get sound ID for underwater ambient
    static uint32_t getUnderwaterLoopId() { return WaterSoundIds::Underwater; }

    // Utility: Get tread sound count
    static constexpr int getTreadSoundCount() { return 2; }
};

} // namespace Audio
} // namespace EQT

#endif // WITH_AUDIO
