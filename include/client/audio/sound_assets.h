#pragma once

#ifdef WITH_AUDIO

#include <string>
#include <unordered_map>
#include <vector>
#include <cstdint>

namespace EQT {
namespace Audio {

// Parses the EQ SoundAssets.txt file which maps sound IDs to filenames
// Format: ID^{volume}^{filename} or ID^{filename}
class SoundAssets {
public:
    SoundAssets() = default;

    // Load from SoundAssets.txt file
    bool loadFromFile(const std::string& filepath);

    // Get filename for a sound ID
    // Returns empty string if not found
    std::string getFilename(uint32_t soundId) const;

    // Get volume for a sound ID (normalized 0.0 - 1.0)
    // Returns 1.0 if not specified or not found
    float getVolume(uint32_t soundId) const;

    // Check if a sound ID exists
    bool hasSound(uint32_t soundId) const;

    // Get number of loaded entries
    size_t size() const { return entries_.size(); }

    // Iterate over all entries (for building lookup maps)
    template<typename Func>
    void forEach(Func&& func) const {
        for (const auto& [id, entry] : entries_) {
            func(id, entry.filename, entry.volume);
        }
    }

    // Get all sound IDs
    std::vector<uint32_t> getAllSoundIds() const {
        std::vector<uint32_t> ids;
        ids.reserve(entries_.size());
        for (const auto& [id, _] : entries_) {
            ids.push_back(id);
        }
        return ids;
    }

private:
    struct SoundEntry {
        std::string filename;
        float volume = 1.0f;
    };

    std::unordered_map<uint32_t, SoundEntry> entries_;
};

// Common sound IDs used in the game
namespace SoundId {
    // Combat
    constexpr uint32_t MELEE_SWING = 118;       // Swing.WAV
    constexpr uint32_t MELEE_HIT = 119;         // Hit sound
    constexpr uint32_t MELEE_MISS = 120;        // Miss sound
    constexpr uint32_t KICK = 121;              // Kick1.WAV
    constexpr uint32_t PUNCH = 122;             // Punch1.WAV

    // Spells
    constexpr uint32_t SPELL_CAST = 108;        // SpelCast.WAV
    constexpr uint32_t SPELL_FIZZLE = 109;      // SpelFail.WAV

    // Player
    constexpr uint32_t LEVEL_UP = 139;          // LevelUp.WAV
    constexpr uint32_t DEATH = 140;             // Death sound

    // Environment
    constexpr uint32_t WATER_IN = 3;            // WaterIn.WAV
    constexpr uint32_t WATER_OUT = 4;           // WaterOut.WAV
    constexpr uint32_t TELEPORT = 141;          // Teleport.WAV

    // UI
    constexpr uint32_t BUTTON_CLICK = 142;      // UI click
    constexpr uint32_t OPEN_WINDOW = 143;       // Window open
    constexpr uint32_t CLOSE_WINDOW = 144;      // Window close
    constexpr uint32_t BUY_ITEM = 145;          // BuyItem.WAV
    constexpr uint32_t SELL_ITEM = 146;         // SellItem.WAV
}

} // namespace Audio
} // namespace EQT

#endif // WITH_AUDIO
