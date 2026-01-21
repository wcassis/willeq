#pragma once

#ifdef WITH_AUDIO

#include <memory>
#include <string>
#include <random>

namespace EQT {
namespace Audio {

// Forward declarations
class AudioManager;
class MusicPlayer;

/**
 * CombatMusicManager - Manages combat music stingers
 *
 * Combat stingers are short musical cues (damage1.xmi, damage2.xmi) that play
 * when the player enters combat. They layer over the zone music rather than
 * replacing it.
 *
 * Behavior:
 * - When combat starts, wait COMBAT_MUSIC_DELAY seconds before playing
 * - If combat ends before the delay, no stinger plays (avoids brief encounters)
 * - Stingers play once (no looping)
 * - Random selection between damage1.xmi and damage2.xmi
 * - Stinger fades out when combat ends if still playing
 */
class CombatMusicManager {
public:
    CombatMusicManager();
    ~CombatMusicManager();

    // Prevent copying
    CombatMusicManager(const CombatMusicManager&) = delete;
    CombatMusicManager& operator=(const CombatMusicManager&) = delete;

    /**
     * Initialize the combat music system
     * @param eqPath Path to EverQuest client directory containing XMI files
     * @param soundFontPath Path to SoundFont file for MIDI/XMI playback
     * @return true if initialization succeeded
     */
    bool initialize(const std::string& eqPath, const std::string& soundFontPath = "");

    /**
     * Shutdown and release resources
     */
    void shutdown();

    /**
     * Called when combat starts (player engages enemy or is attacked)
     */
    void onCombatStart();

    /**
     * Called when combat ends (all enemies dead, disengaged, or zoned)
     */
    void onCombatEnd();

    /**
     * Update combat music state (call every frame)
     * @param deltaTime Time since last update in seconds
     */
    void update(float deltaTime);

    // State queries
    bool isInCombat() const { return inCombat_; }
    bool isStingerPlaying() const;
    bool isEnabled() const { return enabled_; }

    // Configuration
    void setEnabled(bool enabled);
    void setVolume(float volume);  // 0.0 - 1.0
    float getVolume() const { return volume_; }

    // Combat music delay before stinger plays (seconds)
    void setCombatDelay(float seconds);
    float getCombatDelay() const { return combatDelay_; }

    // Fade out time when combat ends (seconds)
    void setFadeOutTime(float seconds);
    float getFadeOutTime() const { return fadeOutTime_; }

    // Get stinger filenames for testing
    static std::string getStingerFilename(int index);
    static int getStingerCount() { return STINGER_COUNT; }

private:
    // Select a random stinger to play
    std::string selectRandomStinger();

    // Play the combat stinger
    void playStinger();

    // Stop the current stinger with fade
    void stopStinger(float fadeSeconds);

private:
    bool initialized_ = false;
    bool enabled_ = true;
    std::string eqPath_;
    std::string soundFontPath_;

    // Combat state
    bool inCombat_ = false;
    float combatTimer_ = 0.0f;       // Time spent in combat
    bool stingerTriggered_ = false;   // Whether stinger has been triggered this combat

    // Volume
    float volume_ = 0.8f;  // Slightly lower than zone music by default

    // Timing configuration
    float combatDelay_ = 5.0f;   // Seconds before stinger plays
    float fadeOutTime_ = 2.0f;   // Seconds for fade out

    // Stinger music player (separate from zone music)
    std::unique_ptr<MusicPlayer> stingerPlayer_;

    // Random number generator for stinger selection
    std::mt19937 rng_;

    // Combat stinger files
    static constexpr int STINGER_COUNT = 2;
    static constexpr const char* STINGER_FILES[STINGER_COUNT] = {
        "damage1.xmi",
        "damage2.xmi"
    };
};

} // namespace Audio
} // namespace EQT

#endif // WITH_AUDIO
