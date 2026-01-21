#ifdef WITH_AUDIO

#include "client/audio/combat_music.h"
#include "client/audio/music_player.h"
#include <iostream>
#include <chrono>
#include <filesystem>

namespace EQT {
namespace Audio {

// Static member definition for stinger files
constexpr const char* CombatMusicManager::STINGER_FILES[STINGER_COUNT];

CombatMusicManager::CombatMusicManager()
    : rng_(static_cast<unsigned int>(
          std::chrono::steady_clock::now().time_since_epoch().count()))
{
}

CombatMusicManager::~CombatMusicManager() {
    shutdown();
}

bool CombatMusicManager::initialize(const std::string& eqPath,
                                     const std::string& soundFontPath) {
    if (initialized_) {
        return true;
    }

    eqPath_ = eqPath;
    soundFontPath_ = soundFontPath;

    // Verify combat stinger files exist
    bool hasStingers = false;
    for (int i = 0; i < STINGER_COUNT; ++i) {
        std::string path = eqPath_ + "/" + STINGER_FILES[i];
        if (std::filesystem::exists(path)) {
            hasStingers = true;
        }
    }

    if (!hasStingers) {
        std::cout << "[COMBAT_MUSIC] Warning: No combat stinger files found in "
                  << eqPath_ << std::endl;
        // Still allow initialization - combat music just won't play
    }

    // Create dedicated music player for stingers
    stingerPlayer_ = std::make_unique<MusicPlayer>();
    if (!stingerPlayer_->initialize(eqPath_, soundFontPath_)) {
        std::cout << "[COMBAT_MUSIC] Failed to initialize stinger player" << std::endl;
        stingerPlayer_.reset();
        return false;
    }

    stingerPlayer_->setVolume(volume_);

    initialized_ = true;
    std::cout << "[COMBAT_MUSIC] Initialized combat music system" << std::endl;
    return true;
}

void CombatMusicManager::shutdown() {
    if (!initialized_) {
        return;
    }

    if (stingerPlayer_) {
        stingerPlayer_->stop(0.0f);
        stingerPlayer_->shutdown();
        stingerPlayer_.reset();
    }

    inCombat_ = false;
    stingerTriggered_ = false;
    combatTimer_ = 0.0f;
    initialized_ = false;
}

void CombatMusicManager::onCombatStart() {
    if (!enabled_ || inCombat_) {
        return;
    }

    inCombat_ = true;
    combatTimer_ = 0.0f;
    stingerTriggered_ = false;

    std::cout << "[COMBAT_MUSIC] Combat started" << std::endl;
}

void CombatMusicManager::onCombatEnd() {
    if (!inCombat_) {
        return;
    }

    std::cout << "[COMBAT_MUSIC] Combat ended (duration: " << combatTimer_ << "s)" << std::endl;

    inCombat_ = false;
    combatTimer_ = 0.0f;

    // Fade out stinger if still playing
    if (isStingerPlaying()) {
        stopStinger(fadeOutTime_);
    }

    stingerTriggered_ = false;
}

void CombatMusicManager::update(float deltaTime) {
    if (!initialized_ || !enabled_ || !inCombat_) {
        return;
    }

    combatTimer_ += deltaTime;

    // Check if we should trigger the stinger
    if (!stingerTriggered_ && combatTimer_ >= combatDelay_) {
        playStinger();
        stingerTriggered_ = true;
    }
}

bool CombatMusicManager::isStingerPlaying() const {
    return stingerPlayer_ && stingerPlayer_->isPlaying();
}

void CombatMusicManager::setEnabled(bool enabled) {
    enabled_ = enabled;

    if (!enabled && isStingerPlaying()) {
        stopStinger(0.5f);
    }
}

void CombatMusicManager::setVolume(float volume) {
    volume_ = std::max(0.0f, std::min(1.0f, volume));

    if (stingerPlayer_) {
        stingerPlayer_->setVolume(volume_);
    }
}

void CombatMusicManager::setCombatDelay(float seconds) {
    combatDelay_ = std::max(0.0f, seconds);
}

void CombatMusicManager::setFadeOutTime(float seconds) {
    fadeOutTime_ = std::max(0.0f, seconds);
}

std::string CombatMusicManager::getStingerFilename(int index) {
    if (index < 0 || index >= STINGER_COUNT) {
        return "";
    }
    return STINGER_FILES[index];
}

std::string CombatMusicManager::selectRandomStinger() {
    std::uniform_int_distribution<int> dist(0, STINGER_COUNT - 1);
    int index = dist(rng_);
    return eqPath_ + "/" + STINGER_FILES[index];
}

void CombatMusicManager::playStinger() {
    if (!stingerPlayer_) {
        return;
    }

    std::string stingerPath = selectRandomStinger();

    // Check if file exists
    if (!std::filesystem::exists(stingerPath)) {
        std::cout << "[COMBAT_MUSIC] Stinger file not found: " << stingerPath << std::endl;
        return;
    }

    // Play stinger (not looped - plays once)
    if (stingerPlayer_->play(stingerPath, false)) {
        std::cout << "[COMBAT_MUSIC] Playing combat stinger: "
                  << std::filesystem::path(stingerPath).filename().string() << std::endl;
    } else {
        std::cout << "[COMBAT_MUSIC] Failed to play stinger: " << stingerPath << std::endl;
    }
}

void CombatMusicManager::stopStinger(float fadeSeconds) {
    if (!stingerPlayer_) {
        return;
    }

    stingerPlayer_->stop(fadeSeconds);
    std::cout << "[COMBAT_MUSIC] Stopping stinger with " << fadeSeconds << "s fade" << std::endl;
}

} // namespace Audio
} // namespace EQT

#endif // WITH_AUDIO
