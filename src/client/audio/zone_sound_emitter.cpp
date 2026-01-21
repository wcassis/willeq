#ifdef WITH_AUDIO

#include "client/audio/zone_sound_emitter.h"
#include "client/audio/audio_manager.h"
#include "client/audio/sound_buffer.h"
#include <cstdlib>
#include <algorithm>
#include <iostream>

namespace EQT {
namespace Audio {

ZoneSoundEmitter::ZoneSoundEmitter() = default;

ZoneSoundEmitter::~ZoneSoundEmitter() {
    stop();
    if (source_ != 0) {
        alDeleteSources(1, &source_);
        source_ = 0;
    }
}

ZoneSoundEmitter::ZoneSoundEmitter(ZoneSoundEmitter&& other) noexcept
    : sequence_(other.sequence_)
    , position_(other.position_)
    , radius_(other.radius_)
    , type_(other.type_)
    , soundFile1_(std::move(other.soundFile1_))
    , soundFile2_(std::move(other.soundFile2_))
    , cooldown1_(other.cooldown1_)
    , cooldown2_(other.cooldown2_)
    , randomDelay_(other.randomDelay_)
    , asDistance_(other.asDistance_)
    , fadeOutMs_(other.fadeOutMs_)
    , fullVolRange_(other.fullVolRange_)
    , xmiIndex1_(other.xmiIndex1_)
    , xmiIndex2_(other.xmiIndex2_)
    , source_(other.source_)
    , currentBuffer_(std::move(other.currentBuffer_))
    , cooldownTimer_(other.cooldownTimer_)
    , isPlaying_(other.isPlaying_)
    , wasInRange_(other.wasInRange_)
    , currentIsDay_(other.currentIsDay_)
    , currentVolume_(other.currentVolume_)
    , targetVolume_(other.targetVolume_)
    , fadeTimer_(other.fadeTimer_)
    , isFadingOut_(other.isFadingOut_)
{
    other.source_ = 0;
    other.isPlaying_ = false;
}

ZoneSoundEmitter& ZoneSoundEmitter::operator=(ZoneSoundEmitter&& other) noexcept {
    if (this != &other) {
        stop();
        if (source_ != 0) {
            alDeleteSources(1, &source_);
        }

        sequence_ = other.sequence_;
        position_ = other.position_;
        radius_ = other.radius_;
        type_ = other.type_;
        soundFile1_ = std::move(other.soundFile1_);
        soundFile2_ = std::move(other.soundFile2_);
        cooldown1_ = other.cooldown1_;
        cooldown2_ = other.cooldown2_;
        randomDelay_ = other.randomDelay_;
        asDistance_ = other.asDistance_;
        fadeOutMs_ = other.fadeOutMs_;
        fullVolRange_ = other.fullVolRange_;
        xmiIndex1_ = other.xmiIndex1_;
        xmiIndex2_ = other.xmiIndex2_;
        source_ = other.source_;
        currentBuffer_ = std::move(other.currentBuffer_);
        cooldownTimer_ = other.cooldownTimer_;
        isPlaying_ = other.isPlaying_;
        wasInRange_ = other.wasInRange_;
        currentIsDay_ = other.currentIsDay_;
        currentVolume_ = other.currentVolume_;
        targetVolume_ = other.targetVolume_;
        fadeTimer_ = other.fadeTimer_;
        isFadingOut_ = other.isFadingOut_;

        other.source_ = 0;
        other.isPlaying_ = false;
    }
    return *this;
}

void ZoneSoundEmitter::initialize(
    int32_t sequence,
    const glm::vec3& position,
    float radius,
    EmitterSoundType type,
    const std::string& soundFile1,
    const std::string& soundFile2,
    int32_t cooldown1,
    int32_t cooldown2,
    int32_t randomDelay,
    int32_t asDistance,
    int32_t fadeOutMs,
    int32_t fullVolRange,
    int32_t xmiIndex1,
    int32_t xmiIndex2)
{
    sequence_ = sequence;
    position_ = position;
    radius_ = radius;
    type_ = type;
    soundFile1_ = soundFile1;
    soundFile2_ = soundFile2;
    cooldown1_ = cooldown1;
    cooldown2_ = cooldown2;
    randomDelay_ = randomDelay;
    asDistance_ = asDistance;
    fadeOutMs_ = fadeOutMs > 0 ? fadeOutMs : 1000;  // Default 1 second
    fullVolRange_ = fullVolRange;
    xmiIndex1_ = xmiIndex1;
    xmiIndex2_ = xmiIndex2;

    // Create OpenAL source
    if (source_ == 0) {
        alGenSources(1, &source_);
        if (alGetError() != AL_NO_ERROR) {
            source_ = 0;
        }
    }

    // Initialize cooldown with random offset
    if (randomDelay_ > 0) {
        cooldownTimer_ = static_cast<float>(std::rand() % (randomDelay_ + 1));
    }
}

void ZoneSoundEmitter::update(float deltaTime, const glm::vec3& listenerPos, bool isDay,
                               AudioManager* audioManager) {
    if (!audioManager || source_ == 0) {
        return;
    }

    // Skip music emitters - they're handled separately by ZoneAudioManager
    if (type_ == EmitterSoundType::BackgroundMusic) {
        return;
    }

    // Check if listener is in range
    float distance = glm::length(listenerPos - position_);
    bool inRange = isInRange(listenerPos);

    // Handle day/night change
    if (currentIsDay_ != isDay && isPlaying_) {
        // Day/night changed while playing - may need to switch sounds
        if (type_ == EmitterSoundType::DayNightConstant ||
            type_ == EmitterSoundType::DayNightDistance) {
            // Stop current and restart with new sound
            stop();
            cooldownTimer_ = 0;  // Play immediately
        }
    }
    currentIsDay_ = isDay;

    // Calculate target volume based on type and distance
    if (inRange) {
        targetVolume_ = calculateVolume(distance);
    } else {
        targetVolume_ = 0.0f;
    }

    // Handle fade in/out
    float deltaMs = deltaTime * 1000.0f;
    if (isPlaying_) {
        // Note: We can't check the actual source state because AudioManager uses
        // its own source pool. Instead, we use the cooldown timer to track when
        // we consider the sound "done playing" for re-trigger purposes.

        // Update volume with fade
        if (currentVolume_ < targetVolume_) {
            float fadeInMs = static_cast<float>(fadeOutMs_) / 2.0f;
            fadeInMs = std::min(fadeInMs, 5000.0f);  // Cap at 5 seconds
            float fadeRate = deltaMs / fadeInMs;
            currentVolume_ = std::min(targetVolume_, currentVolume_ + fadeRate);
        } else if (currentVolume_ > targetVolume_) {
            float fadeRate = deltaMs / static_cast<float>(fadeOutMs_);
            currentVolume_ = std::max(targetVolume_, currentVolume_ - fadeRate);

            // If faded out completely, stop
            if (currentVolume_ <= 0.0f && !inRange) {
                stop();
            }
        }
    }

    // Handle entering/leaving range
    if (inRange && !wasInRange_) {
        // Just entered range - start playing if not already
        if (!isPlaying_ && cooldownTimer_ <= 0) {
            play(audioManager, isDay);
        }
    } else if (!inRange && wasInRange_) {
        // Just left range - start fading out
        isFadingOut_ = true;
    }

    wasInRange_ = inRange;

    // Update cooldown timer (also serves as "play duration" tracking)
    if (cooldownTimer_ > 0) {
        cooldownTimer_ -= deltaMs;

        // When cooldown expires, mark as no longer playing (ready for next trigger)
        if (cooldownTimer_ <= 0 && isPlaying_) {
            isPlaying_ = false;
        }
    }

    // Check if we should start playing
    if (!isPlaying_ && inRange && cooldownTimer_ <= 0) {
        play(audioManager, isDay);
    }
}

bool ZoneSoundEmitter::isInRange(const glm::vec3& pos) const {
    float distance = glm::length(pos - position_);
    return distance <= radius_;
}

float ZoneSoundEmitter::calculateVolume(float distance) const {
    if (distance > radius_) {
        return 0.0f;
    }

    switch (type_) {
        case EmitterSoundType::DayNightConstant:
        case EmitterSoundType::BackgroundMusic:
            // Constant volume within radius
            return 1.0f;

        case EmitterSoundType::StaticEffect:
        case EmitterSoundType::DayNightDistance:
            // Volume based on AsDistance field
            if (asDistance_ < 0 || asDistance_ > 3000) {
                return 0.0f;
            }
            return (3000.0f - static_cast<float>(asDistance_)) / 3000.0f;

        default:
            return 1.0f;
    }
}

void ZoneSoundEmitter::setDayNight(bool isDay) {
    if (currentIsDay_ == isDay) {
        return;
    }

    // Will be handled in update()
    currentIsDay_ = isDay;
}

void ZoneSoundEmitter::transitionTo(bool isDay, int32_t fadeMs) {
    if (currentIsDay_ == isDay) {
        return;
    }

    // If playing, start crossfade
    if (isPlaying_ && hasDayNightVariants()) {
        // Start fading out current sound
        isFadingOut_ = true;
        fadeTimer_ = 0.0f;
        fadeOutMs_ = fadeMs > 0 ? fadeMs : 1000;  // Use specified or default
    }

    currentIsDay_ = isDay;
}

bool ZoneSoundEmitter::hasDayNightVariants() const {
    // Types 0, 1, and 3 support day/night variants
    switch (type_) {
        case EmitterSoundType::DayNightConstant:
        case EmitterSoundType::BackgroundMusic:
        case EmitterSoundType::DayNightDistance:
            return !soundFile1_.empty() && !soundFile2_.empty() &&
                   soundFile1_ != soundFile2_;
        case EmitterSoundType::StaticEffect:
        default:
            return false;
    }
}

void ZoneSoundEmitter::stop() {
    if (source_ != 0 && isPlaying_) {
        alSourceStop(source_);
        alSourcei(source_, AL_BUFFER, 0);
    }
    isPlaying_ = false;
    currentVolume_ = 0.0f;
    isFadingOut_ = false;
}

void ZoneSoundEmitter::play(AudioManager* audioManager, bool isDay) {
    if (!audioManager) {
        return;
    }

    const std::string& soundFile = getCurrentSoundFile(isDay);
    if (soundFile.empty()) {
        return;
    }

    // Add .wav extension if not present
    std::string filename = soundFile;
    if (filename.find('.') == std::string::npos) {
        filename += ".wav";
    }

    // Play through AudioManager's source pool
    audioManager->playSoundByName(filename, position_);
    isPlaying_ = true;
    currentVolume_ = targetVolume_;

    // Set cooldown for next play
    // Note: We use cooldown timer to track play duration since we can't check
    // the actual source state (AudioManager uses its own source pool)
    int32_t baseCooldown = isDay ? cooldown1_ : cooldown2_;
    int32_t randomAdd = randomDelay_ > 0 ? (std::rand() % (randomDelay_ + 1)) : 0;

    // Ensure minimum cooldown of 1 second to prevent rapid re-triggering
    // This accounts for typical ambient sound durations
    static constexpr int32_t MIN_COOLDOWN_MS = 1000;
    int32_t totalCooldown = baseCooldown + randomAdd;
    if (totalCooldown < MIN_COOLDOWN_MS && totalCooldown >= 0) {
        totalCooldown = MIN_COOLDOWN_MS;
    }
    cooldownTimer_ = static_cast<float>(totalCooldown);

    // If no cooldown specified, this is a one-shot trigger
    // The sound will play once and we wait for the player to leave and re-enter
    if (baseCooldown <= 0 && randomDelay_ <= 0) {
        cooldownTimer_ = -1.0f;  // Don't replay automatically
    }
}

void ZoneSoundEmitter::updateVolume(float distance) {
    if (source_ == 0) {
        return;
    }

    // Apply distance attenuation for types 2 and 3
    float baseVolume = currentVolume_;

    // Additional distance-based attenuation within the radius
    if (type_ == EmitterSoundType::StaticEffect ||
        type_ == EmitterSoundType::DayNightDistance) {
        // Linear falloff within radius
        if (radius_ > 0 && fullVolRange_ > 0) {
            float fullVolDist = static_cast<float>(fullVolRange_);
            if (distance > fullVolDist) {
                float falloff = 1.0f - ((distance - fullVolDist) / (radius_ - fullVolDist));
                falloff = std::max(0.0f, std::min(1.0f, falloff));
                baseVolume *= falloff;
            }
        }
    }

    alSourcef(source_, AL_GAIN, baseVolume);
}

const std::string& ZoneSoundEmitter::getCurrentSoundFile(bool isDay) const {
    switch (type_) {
        case EmitterSoundType::DayNightConstant:
        case EmitterSoundType::DayNightDistance:
        case EmitterSoundType::BackgroundMusic:
            // Use day/night variants
            return isDay ? soundFile1_ : soundFile2_;

        case EmitterSoundType::StaticEffect:
        default:
            // Always use primary sound
            return soundFile1_;
    }
}

} // namespace Audio
} // namespace EQT

#endif // WITH_AUDIO
