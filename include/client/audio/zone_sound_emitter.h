#pragma once

#ifdef WITH_AUDIO

#include <AL/al.h>
#include <glm/glm.hpp>
#include <string>
#include <cstdint>
#include <memory>

namespace EQT {
namespace Audio {

class AudioManager;
class SoundBuffer;

// Sound types from EFF format
enum class EmitterSoundType : uint8_t {
    DayNightConstant = 0,   // Day/night sounds with constant volume within radius
    BackgroundMusic = 1,    // Zone music (XMI or MP3), different tracks for day/night
    StaticEffect = 2,       // Single sound, volume based on distance
    DayNightDistance = 3    // Day/night sounds with distance-based volume
};

// Individual sound emitter loaded from zone _sounds.eff file
// Represents a positioned sound source in the zone
class ZoneSoundEmitter {
public:
    ZoneSoundEmitter();
    ~ZoneSoundEmitter();

    // Prevent copying (owns OpenAL source)
    ZoneSoundEmitter(const ZoneSoundEmitter&) = delete;
    ZoneSoundEmitter& operator=(const ZoneSoundEmitter&) = delete;

    // Allow moving
    ZoneSoundEmitter(ZoneSoundEmitter&& other) noexcept;
    ZoneSoundEmitter& operator=(ZoneSoundEmitter&& other) noexcept;

    // Initialize from EFF data
    void initialize(
        int32_t sequence,
        const glm::vec3& position,
        float radius,
        EmitterSoundType type,
        const std::string& soundFile1,  // Day/primary sound
        const std::string& soundFile2,  // Night/secondary sound
        int32_t cooldown1,              // Cooldown for sound 1 (ms)
        int32_t cooldown2,              // Cooldown for sound 2 (ms)
        int32_t randomDelay,            // Random delay addition (ms)
        int32_t asDistance,             // Volume distance parameter
        int32_t fadeOutMs,              // Fade out time (ms)
        int32_t fullVolRange,           // Full volume range
        int32_t xmiIndex1 = 0,          // XMI subsong index for music type
        int32_t xmiIndex2 = 0           // XMI subsong index for night music
    );

    // Update emitter state
    // Returns true if still active, false if should be removed
    void update(float deltaTime, const glm::vec3& listenerPos, bool isDay,
                AudioManager* audioManager);

    // Check if listener is within activation radius
    bool isInRange(const glm::vec3& pos) const;

    // Calculate volume based on distance and type
    float calculateVolume(float distance) const;

    // Getters
    int32_t getSequence() const { return sequence_; }
    const glm::vec3& getPosition() const { return position_; }
    float getRadius() const { return radius_; }
    EmitterSoundType getType() const { return type_; }
    bool isPlaying() const { return isPlaying_; }
    bool isMusic() const { return type_ == EmitterSoundType::BackgroundMusic; }

    // Day/night state
    void setDayNight(bool isDay);

    // Crossfade transition to new day/night state
    // fadeMs: time to crossfade between current and new sound
    void transitionTo(bool isDay, int32_t fadeMs);

    // Check if emitter has separate day/night sounds
    bool hasDayNightVariants() const;

    // Stop playback
    void stop();

private:
    // Start playing the appropriate sound
    void play(AudioManager* audioManager, bool isDay);

    // Update volume based on distance
    void updateVolume(float distance);

    // Get current sound file based on day/night
    const std::string& getCurrentSoundFile(bool isDay) const;

private:
    int32_t sequence_ = 0;
    glm::vec3 position_{0.0f};
    float radius_ = 0.0f;
    EmitterSoundType type_ = EmitterSoundType::StaticEffect;

    std::string soundFile1_;  // Day/primary
    std::string soundFile2_;  // Night/secondary

    int32_t cooldown1_ = 0;     // ms
    int32_t cooldown2_ = 0;     // ms
    int32_t randomDelay_ = 0;   // ms
    int32_t asDistance_ = 0;    // Volume distance parameter
    int32_t fadeOutMs_ = 0;     // ms
    int32_t fullVolRange_ = 0;

    int32_t xmiIndex1_ = 0;     // XMI subsong for day
    int32_t xmiIndex2_ = 0;     // XMI subsong for night

    // Runtime state
    ALuint source_ = 0;
    std::shared_ptr<SoundBuffer> currentBuffer_;
    float cooldownTimer_ = 0.0f;  // Remaining cooldown in ms
    bool isPlaying_ = false;
    bool wasInRange_ = false;
    bool currentIsDay_ = true;
    float currentVolume_ = 0.0f;
    float targetVolume_ = 0.0f;

    // Fade state
    float fadeTimer_ = 0.0f;
    bool isFadingOut_ = false;
};

} // namespace Audio
} // namespace EQT

#endif // WITH_AUDIO
