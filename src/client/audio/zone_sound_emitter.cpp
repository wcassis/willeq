#ifdef WITH_AUDIO

#include "client/audio/zone_sound_emitter.h"
#include "client/audio/audio_manager.h"
#include "client/audio/sound_buffer.h"
#include "common/logging.h"
#include <cstdlib>
#include <algorithm>
#include <iostream>

namespace EQT {
namespace Audio {

ZoneSoundEmitter::ZoneSoundEmitter() = default;

ZoneSoundEmitter::~ZoneSoundEmitter() {
    stop();
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
    , loop_(other.loop_)
    , loopAudio_(other.loopAudio_)
    , loopFile_(std::move(other.loopFile_))
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
    other.loop_ = SfxLoopHandle{};
    other.loopAudio_ = nullptr;
    other.isPlaying_ = false;
}

ZoneSoundEmitter& ZoneSoundEmitter::operator=(ZoneSoundEmitter&& other) noexcept {
    if (this != &other) {
        stop();

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
        loop_ = other.loop_;
        loopAudio_ = other.loopAudio_;
        loopFile_ = std::move(other.loopFile_);
        currentBuffer_ = std::move(other.currentBuffer_);
        cooldownTimer_ = other.cooldownTimer_;
        isPlaying_ = other.isPlaying_;
        wasInRange_ = other.wasInRange_;
        currentIsDay_ = other.currentIsDay_;
        currentVolume_ = other.currentVolume_;
        targetVolume_ = other.targetVolume_;
        fadeTimer_ = other.fadeTimer_;
        isFadingOut_ = other.isFadingOut_;

        other.loop_ = SfxLoopHandle{};
        other.loopAudio_ = nullptr;
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

    // Initialize cooldown with random offset
    if (randomDelay_ > 0) {
        cooldownTimer_ = static_cast<float>(std::rand() % (randomDelay_ + 1));
    }
}

void ZoneSoundEmitter::update(float deltaTime, const glm::vec3& listenerPos, bool isDay,
                               AudioManager* audioManager) {
    if (!audioManager) {
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

    // B1074: a loop is ours until we stop it -- unless something else freed
    // the channel (AudioManager::stopAllSounds on audio disable), or the
    // file for the current day/night state is no longer the one looping
    // (ZoneAudioManager::setDayNight has already moved currentIsDay_, so the
    // block above never sees that switch; a night-only emitter has NO day
    // file at all). Either way: release it, and replay at once if in range.
    if (loop_.valid()) {
        if (!audioManager->isLoopingSoundActive(loop_)) {
            LOG_DEBUG(MOD_AUDIO, "Emitter {} loop channel {} lost; restarting", sequence_, loop_.handle);
            loop_ = SfxLoopHandle{};
            loopAudio_ = nullptr;
            loopFile_.clear();
            isPlaying_ = false;
            cooldownTimer_ = 0;
        } else if (playbackFileName(isDay) != loopFile_) {
            LOG_DEBUG(MOD_AUDIO, "Emitter {} day/night switch: stop loop {}", sequence_, loopFile_);
            stop();
            cooldownTimer_ = 0;
        }
    }

    // Calculate target volume based on type and distance
    if (inRange) {
        targetVolume_ = calculateVolume(distance);
    } else {
        targetVolume_ = 0.0f;
    }

    // Handle fade in/out
    float deltaMs = deltaTime * 1000.0f;
    if (isPlaying_) {
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
    // B1074: a loop that is already silent when the listener leaves (its
    // envelope was 0 in range) has nothing to fade -- it still must stop.
    if (loop_.valid() && !inRange && currentVolume_ <= 0.0f) {
        stop();
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

    // B1074: a loop's channel follows the listener and the fade envelope.
    if (loop_.valid()) {
        audioManager->updateLoopingSound(loop_, currentVolume_);
    }
}

bool ZoneSoundEmitter::loopsFor(bool isDay) const {
    int32_t baseCooldown = isDay ? cooldown1_ : cooldown2_;
    return baseCooldown <= 0 && randomDelay_ <= 0;
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
    // One-shots end by themselves in the mixer. A LOOP never does (B1074):
    // free its channel here, which also drops the buffer's in-use pin
    // (B1015). stopLoopingSound() is a no-op if the channel is no longer
    // ours.
    if (loop_.valid() && loopAudio_) {
        loopAudio_->stopLoopingSound(loop_);
        LOG_DEBUG(MOD_AUDIO, "Emitter {} loop stopped: {}", sequence_, loopFile_);
    }
    loop_ = SfxLoopHandle{};
    loopAudio_ = nullptr;
    loopFile_.clear();
    isPlaying_ = false;
    currentVolume_ = 0.0f;
    isFadingOut_ = false;
}

void ZoneSoundEmitter::play(AudioManager* audioManager, bool isDay) {
    if (!audioManager) {
        return;
    }

    const std::string filename = playbackFileName(isDay);
    if (filename.empty()) {
        return;
    }

    // Ensure minimum cooldown of 1 second to prevent rapid re-triggering
    // This accounts for typical ambient sound durations
    static constexpr int32_t MIN_COOLDOWN_MS = 1000;

    // B1074 (#394): zero cooldown and zero random delay = a LOOP while in
    // range (the official client's continuous ambience), not a one-shot.
    // maxDist is the emitter's own radius (it is audible wherever it is in
    // range; SfxManager's 500 default silenced qeynos2's r=700 emitter);
    // type 0 is constant volume within the radius, so its full-volume
    // distance is the radius; the distance types keep SfxManager's 50.
    if (loopsFor(isDay)) {
        const float fullVolDist = (type_ == EmitterSoundType::DayNightConstant)
            ? radius_ : std::min(50.0f, radius_);
        SfxLoopHandle loop = audioManager->playLoopingSound(filename, position_, radius_, fullVolDist);
        if (!loop.valid()) {
            // Nothing started (no free channel, sound missing, audio off):
            // retry after the minimum cooldown rather than every frame.
            isPlaying_ = false;
            cooldownTimer_ = static_cast<float>(MIN_COOLDOWN_MS);
            return;
        }
        loop_ = loop;
        loopAudio_ = audioManager;
        loopFile_ = filename;
        isPlaying_ = true;
        isFadingOut_ = false;
        currentVolume_ = targetVolume_;
        cooldownTimer_ = 0.0f;
        audioManager->updateLoopingSound(loop_, currentVolume_);
        LOG_DEBUG(MOD_AUDIO, "Emitter {} loop started: {} channel={} radius={}",
                  sequence_, filename, loop_.handle, radius_);
        return;
    }

    // Play through AudioManager (uses SfxManager internally)
    audioManager->playSoundByName(filename, position_);
    isPlaying_ = true;
    currentVolume_ = targetVolume_;

    // Set cooldown for next play (a cooldown > 0 or a random delay: the
    // zero/zero case looped above)
    int32_t baseCooldown = isDay ? cooldown1_ : cooldown2_;
    int32_t randomAdd = randomDelay_ > 0 ? (std::rand() % (randomDelay_ + 1)) : 0;

    int32_t totalCooldown = baseCooldown + randomAdd;
    if (totalCooldown < MIN_COOLDOWN_MS && totalCooldown >= 0) {
        totalCooldown = MIN_COOLDOWN_MS;
    }
    cooldownTimer_ = static_cast<float>(totalCooldown);
}

std::string ZoneSoundEmitter::playbackFileName(bool isDay) const {
    std::string filename = getCurrentSoundFile(isDay);
    // Add .wav extension if not present
    if (!filename.empty() && filename.find('.') == std::string::npos) {
        filename += ".wav";
    }
    return filename;
}

void ZoneSoundEmitter::updateVolume(float distance) {
    // Volume updates are handled by the SfxManager spatial attenuation.
    // This method is kept for API compatibility but no longer directly
    // manipulates OpenAL sources.
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
