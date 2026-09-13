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

// T458: the `WaterSounds` filename/id helper class that lived here had no
// production caller (the one water sound the client plays reaches
// WaterSoundIds::WaterIn directly from movement_state.cpp) and its .cpp was in
// no client target; it was deleted with the tests that only pinned it.

} // namespace Audio
} // namespace EQT

#endif // WITH_AUDIO
