#ifdef WITH_AUDIO

#include "client/audio/water_sounds.h"

namespace EQT {
namespace Audio {

// Sound file names from SoundAssets.txt
static constexpr const char* WATER_IN_FILE = "waterin.wav";
static constexpr const char* WATER_TREAD_1_FILE = "wattrd_1.wav";
static constexpr const char* WATER_TREAD_2_FILE = "wattrd_2.wav";
static constexpr const char* UNDERWATER_LOOP_FILE = "watundlp.wav";

const char* WaterSounds::getEntrySound() {
    return WATER_IN_FILE;
}

const char* WaterSounds::getTreadSound(int variant) {
    // Wrap around for any variant value
    return (variant % 2 == 0) ? WATER_TREAD_1_FILE : WATER_TREAD_2_FILE;
}

const char* WaterSounds::getUnderwaterLoop() {
    return UNDERWATER_LOOP_FILE;
}

uint32_t WaterSounds::getTreadSoundId(int variant) {
    return (variant % 2 == 0) ? WaterSoundIds::WaterTread1 : WaterSoundIds::WaterTread2;
}

} // namespace Audio
} // namespace EQT

#endif // WITH_AUDIO
