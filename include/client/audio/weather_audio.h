#pragma once

#ifdef WITH_AUDIO

#include <AL/al.h>
#include <string>
#include <memory>
#include <random>
#include <cstdint>

namespace EQT {
namespace Audio {

class AudioManager;
class SoundBuffer;

// Weather types matching EQ's OP_Weather packet
enum class WeatherType : uint8_t {
    None = 0,      // Clear weather
    Raining = 1,   // Rain (type 0 with intensity > 0, or type 2 with rain sounds)
    Snowing = 2    // Snow (uses wind sounds instead of rain)
};

// Weather packet structure from OP_Weather (8 bytes)
// type: 0=rain off, 1=snow off, 2=snow on (type 0 + intensity > 0 = rain on)
// intensity: 1-10 (weather intensity)
struct Weather_Struct {
    uint8_t type;
    uint8_t pad1[3];
    uint8_t intensity;
    uint8_t pad2[3];
};

// Manages weather-related ambient sounds (rain, thunder, wind)
// Handles smooth transitions between weather states
class WeatherAudio {
public:
    WeatherAudio();
    ~WeatherAudio();

    // Prevent copying (owns OpenAL sources)
    WeatherAudio(const WeatherAudio&) = delete;
    WeatherAudio& operator=(const WeatherAudio&) = delete;

    // Set reference to AudioManager for loading sounds
    void setAudioManager(AudioManager* audioManager);

    // Set weather state from OP_Weather packet
    // type: raw packet type (0=rain off, 1=snow off, 2=snow on)
    // intensity: 1-10, controls volume
    void setWeather(uint8_t type, uint8_t intensity);

    // Convenience method using WeatherType enum
    void setWeatherType(WeatherType weather, uint8_t intensity);

    // Get current weather state
    WeatherType getWeatherType() const { return currentWeather_; }
    uint8_t getIntensity() const { return intensity_; }
    bool isRaining() const { return currentWeather_ == WeatherType::Raining; }
    bool isSnowing() const { return currentWeather_ == WeatherType::Snowing; }

    // Update weather audio (call each frame)
    // deltaTime: time since last update in seconds
    void update(float deltaTime);

    // Stop all weather sounds immediately
    void stop();

    // Pause/resume (for menus, zone transitions)
    void pause();
    void resume();
    bool isPaused() const { return isPaused_; }

    // Volume control (0.0 - 1.0, multiplied with intensity-based volume)
    void setVolume(float volume);
    float getVolume() const { return volume_; }

    // Thunder timing configuration
    void setThunderEnabled(bool enabled) { thunderEnabled_ = enabled; }
    bool isThunderEnabled() const { return thunderEnabled_; }

    // Get time until next thunder (for testing/debugging)
    float getThunderTimer() const { return thunderTimer_; }

    // Thunder intensity threshold (minimum intensity to trigger thunder)
    static constexpr uint8_t THUNDER_MIN_INTENSITY = 3;

    // Thunder timing range in seconds
    static constexpr float THUNDER_MIN_DELAY = 15.0f;
    static constexpr float THUNDER_MAX_DELAY = 45.0f;

    // Fade duration in seconds
    static constexpr float FADE_DURATION = 2.0f;

private:
    // Start/stop rain ambient loop
    void startRain();
    void stopRain();

    // Start/stop wind ambient loop
    void startWind();
    void stopWind();

    // Play random thunder strike
    void playThunder();

    // Schedule next thunder strike
    void scheduleThunder();

    // Calculate volume based on intensity (intensity/10.0)
    float calculateIntensityVolume() const;

    // Load weather sounds
    void loadSounds();

private:
    AudioManager* audioManager_ = nullptr;

    // Current weather state
    WeatherType currentWeather_ = WeatherType::None;
    WeatherType targetWeather_ = WeatherType::None;
    uint8_t intensity_ = 0;

    // Volume and fade
    float volume_ = 1.0f;
    float currentVolume_ = 0.0f;
    float targetVolume_ = 0.0f;
    float fadeTimer_ = 0.0f;
    bool isFading_ = false;

    // Pause state
    bool isPaused_ = false;

    // OpenAL sources for looping sounds
    ALuint rainSource_ = 0;
    ALuint windSource_ = 0;

    // Sound buffers
    std::shared_ptr<SoundBuffer> rainLoopBuffer_;
    std::shared_ptr<SoundBuffer> windLoopBuffer_;
    std::shared_ptr<SoundBuffer> thunder1Buffer_;
    std::shared_ptr<SoundBuffer> thunder2Buffer_;

    // Thunder timing
    bool thunderEnabled_ = true;
    float thunderTimer_ = 0.0f;
    std::mt19937 rng_;

    // Track if sounds are loaded
    bool soundsLoaded_ = false;
};

} // namespace Audio
} // namespace EQT

#endif // WITH_AUDIO
