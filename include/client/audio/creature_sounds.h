#pragma once

#ifdef WITH_AUDIO

#include <cstdint>
#include <string>
#include <vector>

namespace EQT {
namespace Audio {

// Sound types that creatures can produce
enum class CreatureSoundType {
    Attack,     // Attack/swing sound (prefix_atk.wav)
    Damage,     // Taking damage sound (prefix_dam.wav)
    Death,      // Death sound (prefix_dth.wav or prefix_die.wav)
    Idle,       // Idle/ambient sound (prefix_idl.wav)
    Special,    // Special attack/spell sound (prefix_spl.wav)
    Run,        // Running footsteps (prefix_run.wav)
    Walk        // Walking footsteps (prefix_wlk.wav)
};

// Utility class for creature/NPC sound file lookup
// This class maps EverQuest race IDs to sound file prefixes and generates
// the appropriate sound filenames for different actions.
class CreatureSounds {
public:
    // Get the sound filename for a given sound type and race ID
    // Returns empty string if the race doesn't have that sound type
    // Example: getSoundFile(CreatureSoundType::Attack, 35) -> "rat_atk.wav"
    static std::string getSoundFile(CreatureSoundType type, uint16_t raceId);

    // Get all variant sound files for a given sound type and race ID
    // Some creatures have multiple variants (e.g., rat_atk1.wav, rat_atk2.wav)
    // Returns empty vector if no sounds exist for this type/race
    static std::vector<std::string> getSoundFileVariants(CreatureSoundType type, uint16_t raceId);

    // Check if a race has a sound file for the given type
    static bool hasSoundFile(CreatureSoundType type, uint16_t raceId);

    // Get the sound prefix for a race ID (e.g., "rat" for race 35)
    // Returns empty string if the race has no known sound prefix
    static std::string getRacePrefix(uint16_t raceId);

    // Get a list of all known race IDs that have sound files
    static std::vector<uint16_t> getRacesWithSounds();

    // Get the sound type suffix (e.g., "atk" for Attack)
    static std::string getSoundTypeSuffix(CreatureSoundType type);

private:
    // Internal helper to build filename from prefix and type
    static std::string buildFilename(const std::string& prefix, CreatureSoundType type, int variant = 0);
};

} // namespace Audio
} // namespace EQT

#endif // WITH_AUDIO
