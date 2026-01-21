#ifdef WITH_AUDIO

#include "client/audio/zone_audio_manager.h"
#include "client/audio/zone_sound_emitter.h"
#include "client/audio/eff_loader.h"
#include "client/audio/audio_manager.h"
#include <iostream>
#include <algorithm>

namespace EQT {
namespace Audio {

ZoneAudioManager::ZoneAudioManager()
    : effLoader_(std::make_unique<EffLoader>())
{
}

ZoneAudioManager::~ZoneAudioManager() {
    unloadZone();
}

void ZoneAudioManager::setAudioManager(AudioManager* audioManager) {
    audioManager_ = audioManager;
}

bool ZoneAudioManager::loadZone(const std::string& zoneName, const std::string& eqPath) {
    // Unload previous zone
    unloadZone();

    eqPath_ = eqPath;
    currentZone_ = zoneName;

    // Load EFF files
    if (!effLoader_->loadZone(zoneName, eqPath)) {
        std::cout << "[ZONE_AUDIO] No sound emitters found for zone: " << zoneName << std::endl;
        currentZone_.clear();
        return false;
    }

    // Create emitters from EFF data
    createEmittersFromEffData();

    std::cout << "[ZONE_AUDIO] Loaded zone '" << zoneName << "' with "
              << emitters_.size() << " sound emitters ("
              << getMusicEmitterCount() << " music regions)" << std::endl;

    return !emitters_.empty();
}

void ZoneAudioManager::unloadZone() {
    // Stop all emitters
    stopAllSounds();

    // Clear emitters
    emitters_.clear();
    activeMusicEmitter_ = nullptr;
    currentZone_.clear();

    // Clear EFF data
    effLoader_->clear();
}

void ZoneAudioManager::update(float deltaTime, const glm::vec3& listenerPos, bool isDay) {
    if (isPaused_ || !audioManager_) {
        return;
    }

    // Update day/night state
    if (isDay_ != isDay) {
        setDayNight(isDay);
    }

    // Update all non-music emitters
    for (auto& emitter : emitters_) {
        if (!emitter->isMusic()) {
            emitter->update(deltaTime, listenerPos, isDay, audioManager_);
        }
    }

    // Handle music emitters (location-based zone music)
    ZoneSoundEmitter* newMusicEmitter = findMusicEmitter(listenerPos);
    if (newMusicEmitter != activeMusicEmitter_) {
        // Music region changed
        if (activeMusicEmitter_) {
            // Stop previous music
            activeMusicEmitter_->stop();
        }

        activeMusicEmitter_ = newMusicEmitter;

        if (activeMusicEmitter_) {
            // Start new music
            // For XMI music, we use the zone name + XMI index
            // The AudioManager handles XMI playback
            int xmiIndex = isDay_ ? activeMusicEmitter_->getSequence() : activeMusicEmitter_->getSequence();

            // Get sound file (might be XMI reference or MP3)
            // For positive IDs 1-31, it's an XMI subsong
            // This is handled by playMusic in AudioManager
            std::string musicFile = currentZone_ + ".xmi";
            audioManager_->playMusic(musicFile, true);

            std::cout << "[ZONE_AUDIO] Music region entered at ("
                      << activeMusicEmitter_->getPosition().x << ", "
                      << activeMusicEmitter_->getPosition().y << ", "
                      << activeMusicEmitter_->getPosition().z << ")" << std::endl;
        }
    }
}

void ZoneAudioManager::setDayNight(bool isDay) {
    if (isDay_ == isDay) {
        return;
    }

    isDay_ = isDay;

    // Notify all emitters with crossfade for those with day/night variants
    static constexpr int32_t CROSSFADE_MS = 2000;  // 2 second crossfade
    for (auto& emitter : emitters_) {
        if (emitter->hasDayNightVariants()) {
            emitter->transitionTo(isDay, CROSSFADE_MS);
        } else {
            emitter->setDayNight(isDay);
        }
    }

    std::cout << "[ZONE_AUDIO] Day/night changed to: " << (isDay ? "day" : "night") << std::endl;
}

size_t ZoneAudioManager::getEmitterCount() const {
    return emitters_.size();
}

size_t ZoneAudioManager::getActiveEmitterCount() const {
    size_t count = 0;
    for (const auto& emitter : emitters_) {
        if (emitter->isPlaying()) {
            ++count;
        }
    }
    return count;
}

size_t ZoneAudioManager::getMusicEmitterCount() const {
    size_t count = 0;
    for (const auto& emitter : emitters_) {
        if (emitter->isMusic()) {
            ++count;
        }
    }
    return count;
}

void ZoneAudioManager::stopAllSounds() {
    for (auto& emitter : emitters_) {
        emitter->stop();
    }
    activeMusicEmitter_ = nullptr;

    if (audioManager_) {
        audioManager_->stopMusic(0.5f);
    }
}

void ZoneAudioManager::pause() {
    isPaused_ = true;
    stopAllSounds();
}

void ZoneAudioManager::resume() {
    isPaused_ = false;
}

void ZoneAudioManager::createEmittersFromEffData() {
    const auto& entries = effLoader_->getSoundEntries();

    emitters_.reserve(entries.size());

    for (const auto& entry : entries) {
        auto emitter = std::make_unique<ZoneSoundEmitter>();

        // Convert EFF coordinates to our coordinate system
        // EQ uses Z-up, Irrlicht uses Y-up, but for audio we keep EQ coords
        glm::vec3 position(entry.X, entry.Y, entry.Z);

        // Resolve sound files
        std::string soundFile1 = effLoader_->resolveSoundFile(entry.SoundID1);
        std::string soundFile2 = effLoader_->resolveSoundFile(entry.SoundID2);

        // For music type, handle XMI references
        int32_t xmiIndex1 = 0;
        int32_t xmiIndex2 = 0;
        if (entry.SoundType == 1) {
            // Positive IDs 1-31 are XMI subsong references
            if (entry.SoundID1 > 0 && entry.SoundID1 < 32) {
                xmiIndex1 = entry.SoundID1;
                soundFile1 = currentZone_ + ".xmi";
            }
            if (entry.SoundID2 > 0 && entry.SoundID2 < 32) {
                xmiIndex2 = entry.SoundID2;
                soundFile2 = currentZone_ + ".xmi";
            }
        }

        EmitterSoundType type = static_cast<EmitterSoundType>(entry.SoundType);

        emitter->initialize(
            entry.Sequence,
            position,
            entry.Radius,
            type,
            soundFile1,
            soundFile2,
            entry.Cooldown1,
            entry.Cooldown2,
            entry.RandomDelay,
            entry.AsDistance,
            entry.FadeOutMS,
            entry.FullVolRange,
            xmiIndex1,
            xmiIndex2
        );

        emitters_.push_back(std::move(emitter));
    }
}

ZoneSoundEmitter* ZoneAudioManager::findMusicEmitter(const glm::vec3& listenerPos) {
    // Find the closest music emitter that contains the listener
    ZoneSoundEmitter* closest = nullptr;
    float closestDist = std::numeric_limits<float>::max();

    for (auto& emitter : emitters_) {
        if (emitter->isMusic() && emitter->isInRange(listenerPos)) {
            float dist = glm::length(listenerPos - emitter->getPosition());
            if (dist < closestDist) {
                closestDist = dist;
                closest = emitter.get();
            }
        }
    }

    return closest;
}

} // namespace Audio
} // namespace EQT

#endif // WITH_AUDIO
