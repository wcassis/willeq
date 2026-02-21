#ifdef WITH_AUDIO

#include "client/audio/weather_audio.h"
#include "client/audio/audio_manager.h"
#include "client/audio/audio_mixer.h"
#include "client/audio/sfx_manager.h"
#include "client/audio/sound_buffer.h"
#include "common/logging.h"

#include <algorithm>
#include <chrono>

namespace EQT {
namespace Audio {

// Sound file names from SoundAssets.txt
static constexpr const char* RAIN_LOOP_FILE = "rainloop.wav";
static constexpr const char* WIND_LOOP_FILE = "wind_lp1.wav";
static constexpr const char* THUNDER1_FILE = "thunder1.wav";
static constexpr const char* THUNDER2_FILE = "thunder2.wav";

WeatherAudio::WeatherAudio()
    : rng_(std::random_device{}()) {
}

WeatherAudio::~WeatherAudio() {
    stop();
}

void WeatherAudio::setAudioManager(AudioManager* audioManager) {
    audioManager_ = audioManager;
}

void WeatherAudio::setWeather(uint8_t type, uint8_t intensity) {
    // Decode packet type:
    // type 0 + intensity > 0 = rain on
    // type 0 + intensity == 0 = rain off (clear)
    // type 1 = snow off (clear)
    // type 2 = snow on

    WeatherType newWeather = WeatherType::None;

    if (type == 0) {
        // Rain type: on if intensity > 0
        if (intensity > 0) {
            newWeather = WeatherType::Raining;
        } else {
            newWeather = WeatherType::None;
        }
    } else if (type == 1) {
        // Snow off
        newWeather = WeatherType::None;
    } else if (type == 2) {
        // Snow on
        newWeather = WeatherType::Snowing;
    }

    setWeatherType(newWeather, intensity);
}

void WeatherAudio::setWeatherType(WeatherType weather, uint8_t intensity) {
    // Clamp intensity to 0-10
    intensity = std::min(intensity, static_cast<uint8_t>(10));

    // If no change, just update intensity
    if (weather == currentWeather_ && intensity == intensity_) {
        return;
    }

    LOG_DEBUG(MOD_AUDIO, "Weather change: {} intensity {} -> {} intensity {}",
              static_cast<int>(currentWeather_), intensity_,
              static_cast<int>(weather), intensity);

    targetWeather_ = weather;
    intensity_ = intensity;

    // Calculate target volume based on intensity
    if (weather == WeatherType::None) {
        targetVolume_ = 0.0f;
    } else {
        targetVolume_ = calculateIntensityVolume();
    }

    // Start fade
    isFading_ = true;
    fadeTimer_ = FADE_DURATION;

    // If starting new weather type, begin playing
    if (weather != currentWeather_) {
        if (weather == WeatherType::Raining) {
            // Load sounds if needed and start rain
            loadSounds();
            startRain();
            // Schedule thunder if intensity high enough
            if (intensity_ >= THUNDER_MIN_INTENSITY && thunderEnabled_) {
                scheduleThunder();
            }
        } else if (weather == WeatherType::Snowing) {
            // Load sounds if needed and start wind
            loadSounds();
            startWind();
        }
    }
}

void WeatherAudio::update(float deltaTime) {
    if (isPaused_) {
        return;
    }

    // Handle fade transition
    if (isFading_) {
        fadeTimer_ -= deltaTime;

        if (fadeTimer_ <= 0.0f) {
            // Fade complete
            fadeTimer_ = 0.0f;
            isFading_ = false;
            currentVolume_ = targetVolume_;

            // If fading to silence, stop the old weather sound
            if (targetVolume_ == 0.0f && currentWeather_ != targetWeather_) {
                if (currentWeather_ == WeatherType::Raining) {
                    stopRain();
                } else if (currentWeather_ == WeatherType::Snowing) {
                    stopWind();
                }
            }

            currentWeather_ = targetWeather_;
        } else {
            // Interpolate volume
            float t = 1.0f - (fadeTimer_ / FADE_DURATION);
            currentVolume_ = currentVolume_ + (targetVolume_ - currentVolume_) * t * deltaTime * 2.0f;
        }

        // Update channel volumes via mixer
        float effectiveVolume = currentVolume_ * volume_;
        AudioMixer* mixer = audioManager_ ? audioManager_->getMixer() : nullptr;
        if (mixer) {
            if (rainHandle_ >= 0) {
                MixChannel* ch = mixer->getChannel(rainHandle_);
                if (ch) ch->volume = effectiveVolume;
            }
            if (windHandle_ >= 0) {
                MixChannel* ch = mixer->getChannel(windHandle_);
                if (ch) ch->volume = effectiveVolume;
            }
        }
    }

    // Handle thunder timing during rain
    if (currentWeather_ == WeatherType::Raining && thunderEnabled_ &&
        intensity_ >= THUNDER_MIN_INTENSITY) {
        thunderTimer_ -= deltaTime;

        if (thunderTimer_ <= 0.0f) {
            playThunder();
            scheduleThunder();
        }
    }
}

void WeatherAudio::stop() {
    stopRain();
    stopWind();

    currentWeather_ = WeatherType::None;
    targetWeather_ = WeatherType::None;
    intensity_ = 0;
    currentVolume_ = 0.0f;
    targetVolume_ = 0.0f;
    isFading_ = false;
    fadeTimer_ = 0.0f;
}

void WeatherAudio::pause() {
    isPaused_ = true;
    // Stop looping channels (will be restarted on resume)
    stopRain();
    stopWind();
}

void WeatherAudio::resume() {
    isPaused_ = false;

    // Restart loops if weather is active
    if (currentWeather_ == WeatherType::Raining) {
        startRain();
    }
    if (currentWeather_ == WeatherType::Snowing) {
        startWind();
    }
}

void WeatherAudio::setVolume(float volume) {
    volume_ = std::clamp(volume, 0.0f, 1.0f);

    // Update channel volumes immediately
    float effectiveVolume = currentVolume_ * volume_;
    AudioMixer* mixer = audioManager_ ? audioManager_->getMixer() : nullptr;
    if (mixer) {
        if (rainHandle_ >= 0) {
            MixChannel* ch = mixer->getChannel(rainHandle_);
            if (ch) ch->volume = effectiveVolume;
        }
        if (windHandle_ >= 0) {
            MixChannel* ch = mixer->getChannel(windHandle_);
            if (ch) ch->volume = effectiveVolume;
        }
    }
}

void WeatherAudio::startRain() {
    if (!audioManager_) {
        LOG_WARN(MOD_AUDIO, "Cannot start rain: no audio manager");
        return;
    }

    if (!rainLoopBuffer_ || !rainLoopBuffer_->isValid()) {
        LOG_WARN(MOD_AUDIO, "Cannot start rain: sound not loaded");
        return;
    }

    // Stop existing rain if playing
    stopRain();

    SfxManager* sfx = audioManager_->getSfxManager();
    if (!sfx) return;

    // Preload into SfxManager cache
    sfx->preload(RAIN_LOOP_FILE, rainLoopBuffer_->getSamples(),
                 rainLoopBuffer_->getFrameCount(),
                 rainLoopBuffer_->getSampleRate(),
                 rainLoopBuffer_->getChannels());

    // Play looping
    float effectiveVolume = currentVolume_ * volume_;
    rainHandle_ = sfx->play(RAIN_LOOP_FILE, effectiveVolume, 0.0f, true);

    LOG_DEBUG(MOD_AUDIO, "Rain ambient started (handle={})", rainHandle_);
}

void WeatherAudio::stopRain() {
    if (rainHandle_ >= 0 && audioManager_) {
        SfxManager* sfx = audioManager_->getSfxManager();
        if (sfx) {
            sfx->stopChannel(rainHandle_);
        }
        rainHandle_ = -1;
        LOG_DEBUG(MOD_AUDIO, "Rain ambient stopped");
    }
}

void WeatherAudio::startWind() {
    if (!audioManager_) {
        LOG_WARN(MOD_AUDIO, "Cannot start wind: no audio manager");
        return;
    }

    if (!windLoopBuffer_ || !windLoopBuffer_->isValid()) {
        LOG_WARN(MOD_AUDIO, "Cannot start wind: sound not loaded");
        return;
    }

    // Stop existing wind if playing
    stopWind();

    SfxManager* sfx = audioManager_->getSfxManager();
    if (!sfx) return;

    // Preload into SfxManager cache
    sfx->preload(WIND_LOOP_FILE, windLoopBuffer_->getSamples(),
                 windLoopBuffer_->getFrameCount(),
                 windLoopBuffer_->getSampleRate(),
                 windLoopBuffer_->getChannels());

    // Play looping
    float effectiveVolume = currentVolume_ * volume_;
    windHandle_ = sfx->play(WIND_LOOP_FILE, effectiveVolume, 0.0f, true);

    LOG_DEBUG(MOD_AUDIO, "Wind ambient started (handle={})", windHandle_);
}

void WeatherAudio::stopWind() {
    if (windHandle_ >= 0 && audioManager_) {
        SfxManager* sfx = audioManager_->getSfxManager();
        if (sfx) {
            sfx->stopChannel(windHandle_);
        }
        windHandle_ = -1;
        LOG_DEBUG(MOD_AUDIO, "Wind ambient stopped");
    }
}

void WeatherAudio::playThunder() {
    if (!audioManager_) {
        return;
    }

    // Choose random thunder sound
    std::uniform_int_distribution<int> dist(0, 1);
    int choice = dist(rng_);

    std::shared_ptr<SoundBuffer> thunderBuffer = (choice == 0) ? thunder1Buffer_ : thunder2Buffer_;

    if (!thunderBuffer || !thunderBuffer->isValid()) {
        LOG_DEBUG(MOD_AUDIO, "Thunder buffer not available");
        return;
    }

    // Play thunder as a one-shot sound through audio manager
    // Using sound ID 143 or 144 (thunder1.wav or thunder2.wav)
    if (choice == 0) {
        audioManager_->playSound(143);  // thunder1.wav
    } else {
        audioManager_->playSound(144);  // thunder2.wav
    }

    LOG_DEBUG(MOD_AUDIO, "Thunder strike played");
}

void WeatherAudio::scheduleThunder() {
    // Random delay between THUNDER_MIN_DELAY and THUNDER_MAX_DELAY
    std::uniform_real_distribution<float> dist(THUNDER_MIN_DELAY, THUNDER_MAX_DELAY);
    thunderTimer_ = dist(rng_);

    // Scale delay inversely with intensity (higher intensity = more frequent thunder)
    // At intensity 10, delay is halved
    float intensityScale = 1.0f - (static_cast<float>(intensity_) - THUNDER_MIN_INTENSITY) /
                                   (10.0f - THUNDER_MIN_INTENSITY) * 0.5f;
    thunderTimer_ *= intensityScale;

    LOG_DEBUG(MOD_AUDIO, "Next thunder in {:.1f} seconds", thunderTimer_);
}

float WeatherAudio::calculateIntensityVolume() const {
    // Volume scales linearly with intensity (1-10)
    return static_cast<float>(intensity_) / 10.0f;
}

void WeatherAudio::loadSounds() {
    if (soundsLoaded_ || !audioManager_) {
        return;
    }

    // Load sound buffers for looping ambient sounds
    // Sound IDs from SoundAssets.txt:
    // 159 = rainloop.wav
    // 158 = wind_lp1.wav
    // 143 = thunder1.wav
    // 144 = thunder2.wav

    rainLoopBuffer_ = audioManager_->getSoundBuffer(159);  // rainloop.wav
    if (!rainLoopBuffer_ || !rainLoopBuffer_->isValid()) {
        LOG_WARN(MOD_AUDIO, "Failed to load rain loop sound");
    }

    windLoopBuffer_ = audioManager_->getSoundBuffer(158);  // wind_lp1.wav
    if (!windLoopBuffer_ || !windLoopBuffer_->isValid()) {
        LOG_WARN(MOD_AUDIO, "Failed to load wind loop sound");
    }

    thunder1Buffer_ = audioManager_->getSoundBuffer(143);  // thunder1.wav
    if (!thunder1Buffer_ || !thunder1Buffer_->isValid()) {
        LOG_WARN(MOD_AUDIO, "Failed to load thunder1 sound");
    }

    thunder2Buffer_ = audioManager_->getSoundBuffer(144);  // thunder2.wav
    if (!thunder2Buffer_ || !thunder2Buffer_->isValid()) {
        LOG_WARN(MOD_AUDIO, "Failed to load thunder2 sound");
    }

    soundsLoaded_ = true;
    LOG_INFO(MOD_AUDIO, "Weather sounds loaded");
}

} // namespace Audio
} // namespace EQT

#endif // WITH_AUDIO
