#pragma once

#ifdef WITH_AUDIO

#include <string>
#include <vector>
#include <cstdint>

namespace EQT {
namespace Audio {

// 84-byte binary structure from _sounds.eff files
// Based on Shendare's Eff2Emt converter (CC0 license)
#pragma pack(push, 1)
struct EffSoundEntry {
    int32_t  UnkRef00;      // 0: Runtime reference (ignore)
    int32_t  UnkRef04;      // 4: Runtime reference (ignore)
    int32_t  Reserved;      // 8: Reserved (always 0)
    int32_t  Sequence;      // 12: Entry ID (unique per zone)
    float    X;             // 16: X position in zone
    float    Y;             // 20: Y position in zone
    float    Z;             // 24: Z position in zone
    float    Radius;        // 28: Activation/audible radius
    int32_t  Cooldown1;     // 32: Cooldown for sound 1 (ms)
    int32_t  Cooldown2;     // 36: Cooldown for sound 2 (ms)
    int32_t  RandomDelay;   // 40: Random delay addition (ms)
    int32_t  Unk44;         // 44: Unknown
    int32_t  SoundID1;      // 48: Primary/day sound ID
    int32_t  SoundID2;      // 52: Secondary/night sound ID
    uint8_t  SoundType;     // 56: Sound type (0-3)
    uint8_t  UnkPad57;      // 57: Padding
    uint8_t  UnkPad58;      // 58: Padding
    uint8_t  UnkPad59;      // 59: Padding
    int32_t  AsDistance;    // 60: Volume as distance (type 2,3)
    int32_t  UnkRange64;    // 64: Unknown range parameter
    int32_t  FadeOutMS;     // 68: Fade out time (milliseconds)
    int32_t  UnkRange72;    // 72: Unknown range parameter
    int32_t  FullVolRange;  // 76: Full volume range
    int32_t  UnkRange80;    // 80: Unknown range parameter
};
#pragma pack(pop)

static_assert(sizeof(EffSoundEntry) == 84, "EffSoundEntry must be 84 bytes");

// Sound types from EFF format
enum class EffSoundType : uint8_t {
    DayNightConstant = 0,   // Day/night sounds with constant volume within radius
    BackgroundMusic = 1,    // Zone music (XMI or MP3), different tracks for day/night
    StaticEffect = 2,       // Single sound, volume based on distance
    DayNightDistance = 3    // Day/night sounds with distance-based volume
};

// Loads and parses zone EFF sound configuration files
// Files: {zone}_sounds.eff (binary) and {zone}_sndbnk.eff (text)
class EffLoader {
public:
    EffLoader() = default;

    // Load zone sound files from the EQ client directory
    // Returns true if at least one file was loaded successfully
    bool loadZone(const std::string& zoneName, const std::string& eqPath);

    // Load binary _sounds.eff file
    bool loadSoundsEff(const std::string& filepath);

    // Load text _sndbnk.eff file
    bool loadSndBnkEff(const std::string& filepath);

    // Get loaded sound entries
    const std::vector<EffSoundEntry>& getSoundEntries() const { return soundEntries_; }

    // Get EMIT section sounds (point-source, 1-indexed in EFF)
    const std::vector<std::string>& getEmitSounds() const { return emitSounds_; }

    // Get LOOP section sounds (ambient loops, offset by 161 in EFF)
    const std::vector<std::string>& getLoopSounds() const { return loopSounds_; }

    // Resolve a sound ID to a WAV filename
    // Sound ID mapping:
    //   0         = No sound
    //   < 0       = MP3 from mp3index.txt (abs value = line number)
    //   1-31      = EMIT section (1-indexed, so ID 1 = emitSounds_[0])
    //   32-161    = Hardcoded global sounds
    //   162+      = LOOP section (offset by 161, so ID 162 = loopSounds_[0])
    std::string resolveSoundFile(int32_t soundId) const;

    // Get the zone name that was loaded
    const std::string& getZoneName() const { return zoneName_; }

    // Get count of entries
    size_t getEntryCount() const { return soundEntries_.size(); }

    // Get count of Type 1 (music) entries
    size_t getMusicEntryCount() const;

    // Clear all loaded data
    void clear();

    // Static: Load mp3index.txt for MP3 music references
    static bool loadMp3Index(const std::string& eqPath);
    static std::string getMp3File(int index);
    static const std::vector<std::string>& getMp3Index() { return mp3Index_; }

    // Static: Get hardcoded sound file for IDs 32-161
    static std::string getHardcodedSound(int32_t soundId);

private:
    std::string zoneName_;
    std::vector<EffSoundEntry> soundEntries_;
    std::vector<std::string> emitSounds_;
    std::vector<std::string> loopSounds_;

    // Static mp3index.txt entries (shared across all zones)
    static std::vector<std::string> mp3Index_;
    static bool mp3IndexLoaded_;
};

} // namespace Audio
} // namespace EQT

#endif // WITH_AUDIO
