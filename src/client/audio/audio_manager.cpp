#ifdef WITH_AUDIO

#include "client/audio/audio_manager.h"
#include "client/audio/sound_buffer.h"
#include "client/audio/music_player.h"
#include "client/audio/audio_mixer.h"
#include "client/audio/audio_backend.h"
#include "client/audio/midi_player.h"
#include "client/audio/sfx_manager.h"
#include "client/audio/sound_assets.h"
#include "client/graphics/eq/pfs.h"
#include "common/logging.h"

#include <filesystem>
#include <algorithm>
#include <cctype>
#include <fstream>
#include <set>
#include <unordered_map>
#include <unordered_set>
#include <json/json.h>

namespace EQT {
namespace Audio {

AudioManager::AudioManager() = default;

AudioManager::~AudioManager() {
    shutdown();
}

bool AudioManager::initialize(const std::string& eqPath, bool forceLoopback,
                              const std::string& soundFontPath,
                              std::function<void()> tickCallback) {
    if (initialized_) {
        return true;
    }

    eqPath_ = eqPath;
    soundFontPath_ = soundFontPath;
    tickCallback_ = std::move(tickCallback);

    // Create AudioMixer
    mixer_ = std::make_unique<AudioMixer>();
    mixer_->setMasterVolume(masterVolume_);
    mixer_->setMusicVolume(musicVolume_);
    mixer_->setSfxVolume(effectsVolume_);

    // Create SfxManager
    sfxManager_ = std::make_unique<SfxManager>();
    sfxManager_->setMixer(mixer_.get());

    // Pump event loop before heavy SoundFont loading
    if (tickCallback_) tickCallback_();

    // Create MidiPlayer (for XMI/MIDI music)
    midiPlayer_ = std::make_unique<MidiPlayer>();

    // Find GM SoundFont: CLI --soundfont > synthusr.sf2 > 1mgm.sf2
    std::string gmPath;
    if (!soundFontPath_.empty() && std::filesystem::exists(soundFontPath_)) {
        gmPath = soundFontPath_;
    } else {
        std::string synthusr = eqPath_ + "/synthusr.sf2";
        std::string onegm = eqPath_ + "/1mgm.sf2";
        if (std::filesystem::exists(synthusr)) {
            gmPath = synthusr;
        } else if (std::filesystem::exists(onegm)) {
            gmPath = onegm;
        }
    }

    // Find custom SoundFont: synthus2.sf2 in EQ dir (optional)
    std::string customPath;
    {
        std::string synthus2 = eqPath_ + "/synthus2.sf2";
        if (std::filesystem::exists(synthus2)) {
            customPath = synthus2;
        }
    }

    if (!gmPath.empty()) {
        if (!customPath.empty()) {
            // Dual SoundFont init
            if (midiPlayer_->init(gmPath, customPath, AudioMixer::SAMPLE_RATE)) {
                LOG_INFO(MOD_AUDIO, "MIDI player initialized with dual SoundFonts: GM={}, Custom={}", gmPath, customPath);
            } else {
                LOG_WARN(MOD_AUDIO, "MIDI player dual initialization failed");
            }
        } else {
            // Single SoundFont init
            if (midiPlayer_->init(gmPath, AudioMixer::SAMPLE_RATE)) {
                LOG_INFO(MOD_AUDIO, "MIDI player initialized with SoundFont: {}", gmPath);
            } else {
                LOG_WARN(MOD_AUDIO, "MIDI player initialization failed");
            }
        }

        // If user specified --soundfont AND we found an EQ GM SF2 (synthusr/1mgm),
        // override the GM slot with the user's choice
        if (!soundFontPath_.empty() && gmPath != soundFontPath_ &&
            std::filesystem::exists(soundFontPath_)) {
            midiPlayer_->loadSoundFont(soundFontPath_);
        }
    } else {
        LOG_WARN(MOD_AUDIO, "No SoundFont found - XMI playback disabled");
    }

    // Pump event loop after MIDI player init
    if (tickCallback_) tickCallback_();

    // Create MusicPlayer (wrapper around MidiPlayer + mixer for MP3/WAV/XMI)
    musicPlayer_ = std::make_unique<MusicPlayer>();
    LOG_INFO(MOD_AUDIO, "Initializing music player with eqPath: {}, soundfont: {}",
             eqPath_, soundFontPath_.empty() ? "(none)" : soundFontPath_);
    if (!musicPlayer_->initialize(eqPath_, soundFontPath_)) {
        LOG_WARN(MOD_AUDIO, "Music player initialization failed, music will be disabled");
    }
    // Give MusicPlayer access to MidiPlayer and mixer
    musicPlayer_->setMidiPlayer(midiPlayer_.get());
    musicPlayer_->setMixer(mixer_.get());

    // Pump event loop after music player init
    if (tickCallback_) tickCallback_();

    // Create and initialize audio backend
    if (forceLoopback) {
        LOG_INFO(MOD_AUDIO, "Loopback mode requested");
        auto rdpBackend = std::make_unique<RDPAudioBackend>();
        if (audioOutputCallback_) {
            rdpBackend->setOutputCallback(audioOutputCallback_);
        }
        rdpBackend->init(mixer_.get(), AudioMixer::SAMPLE_RATE);
        rdpBackend->start();
        backend_ = std::move(rdpBackend);
        loopbackMode_ = true;
    } else {
        auto maBackend = std::make_unique<MiniaudioBackend>();
        if (maBackend->init(mixer_.get(), AudioMixer::SAMPLE_RATE)) {
            maBackend->start();
            backend_ = std::move(maBackend);
            loopbackMode_ = false;
        } else {
            LOG_WARN(MOD_AUDIO, "Miniaudio hardware init failed, trying RDP loopback");
            auto rdpBackend = std::make_unique<RDPAudioBackend>();
            if (audioOutputCallback_) {
                rdpBackend->setOutputCallback(audioOutputCallback_);
            }
            rdpBackend->init(mixer_.get(), AudioMixer::SAMPLE_RATE);
            rdpBackend->start();
            backend_ = std::move(rdpBackend);
            loopbackMode_ = true;
        }
    }

    LOG_DEBUG(MOD_AUDIO, "Audio backend initialized (loopback={})", loopbackMode_);

    // Load sound asset mapping
    loadSoundAssets();

    // Scan PFS archives for sound files (pumps event loop per-archive internally)
    scanPfsArchives();

    // Load music event configuration
    loadMusicEventConfig();

    initialized_ = true;
    LOG_INFO(MOD_AUDIO, "Audio system initialized");
    return true;
}

void AudioManager::shutdown() {
    if (!initialized_) {
        return;
    }

    LOG_INFO(MOD_AUDIO, "Shutting down audio system");

    // Stop backend first
    if (backend_) {
        backend_->stop();
        backend_.reset();
    }

    // Stop music
    if (musicPlayer_) {
        musicPlayer_->stop();
        musicPlayer_->shutdown();
        musicPlayer_.reset();
    }

    // Stop MIDI
    if (midiPlayer_) {
        midiPlayer_->stop();
        midiPlayer_.reset();
    }

    // Clear SFX
    sfxManager_.reset();

    // Clear buffer cache
    {
        std::lock_guard<std::mutex> lock(bufferMutex_);
        bufferCache_.clear();
        bufferLruOrder_.clear();
        soundBufferCacheSizeBytes_ = 0;
    }

    // Clear mixer last
    mixer_.reset();

    initialized_ = false;
}

void AudioManager::playSound(uint32_t soundId) {
    playSound(soundId, glm::vec3(0.0f));
}

void AudioManager::playSound(uint32_t soundId, const glm::vec3& position) {
    if (!initialized_ || !audioEnabled_) {
        return;
    }

    std::string filename = "unknown";
    auto it = soundIdMap_.find(soundId);
    if (it != soundIdMap_.end()) {
        filename = it->second;
    }

    // Ensure sound is loaded into SfxManager cache
    auto buffer = getSoundById(soundId);
    if (!buffer || !buffer->isValid()) {
        LOG_DEBUG(MOD_AUDIO, "Sound failed: id={} file={} (not found)", soundId, filename);
        return;
    }

    LOG_TRACE(MOD_AUDIO, "Sound play: id={} file={} pos=({},{},{})",
              soundId, filename, position.x, position.y, position.z);

    playSoundInternal(filename, position);
}

void AudioManager::playSoundByName(const std::string& filename) {
    playSoundByName(filename, glm::vec3(0.0f));
}

void AudioManager::playSoundByName(const std::string& filename, const glm::vec3& position) {
    if (!initialized_ || !audioEnabled_) {
        return;
    }

    auto buffer = loadSound(filename);
    if (!buffer || !buffer->isValid()) {
        LOG_DEBUG(MOD_AUDIO, "Sound failed: file={} (not found)", filename);
        return;
    }

    LOG_TRACE(MOD_AUDIO, "Sound play: file={} pos=({},{},{})",
              filename, position.x, position.y, position.z);

    playSoundInternal(filename, position);
}

void AudioManager::playSoundInternal(const std::string& filename, const glm::vec3& position) {
    if (!sfxManager_) return;

    // Ensure sound data is in SfxManager cache
    auto buffer = loadSound(filename);
    if (!buffer || !buffer->isValid()) return;

    // Preload into SfxManager if not already
    sfxManager_->preload(filename, buffer->getSamples(),
                         buffer->getFrameCount(), buffer->getSampleRate(),
                         buffer->getChannels());

    // Check if this is a positioned sound
    if (position.x != 0.0f || position.y != 0.0f || position.z != 0.0f) {
        sfxManager_->playSpatial(filename, position);
    } else {
        sfxManager_->play(filename, masterVolume_ * effectsVolume_);
    }
}

void AudioManager::stopAllSounds() {
    // SfxManager channels will naturally complete or can be stopped by mixer
    if (mixer_) {
        for (int i = 0; i < AudioMixer::MAX_SFX_CHANNELS; ++i) {
            mixer_->freeChannel(i);
        }
    }
}

void AudioManager::playMusic(const std::string& filename, bool loop, int trackIndex, double startTimeMs) {
    if (!initialized_ || !audioEnabled_ || !musicPlayer_) {
        LOG_DEBUG(MOD_AUDIO, "Music failed: file={} (not initialized or disabled)", filename);
        return;
    }

    std::string fullPath = filename;
    if (!std::filesystem::path(filename).is_absolute()) {
        fullPath = eqPath_ + "/" + filename;
    }

    if (!std::filesystem::exists(fullPath)) {
        LOG_WARN(MOD_AUDIO, "Music file not found: {}", fullPath);
        return;
    }

    // Skip if the same file and track is already playing
    if (musicPlayer_->isPlaying() && musicPlayer_->getCurrentFile() == fullPath &&
        musicPlayer_->getCurrentTrackIndex() == trackIndex) {
        LOG_DEBUG(MOD_AUDIO, "Music skip: file={} track={} (already playing)", fullPath, trackIndex);
        return;
    }

    LOG_DEBUG(MOD_AUDIO, "Music play: file={} track={} loop={}", fullPath, trackIndex, loop ? "yes" : "no");

    // Select soundfont based on XMI filename
    if (midiPlayer_ && midiPlayer_->isInitialized()) {
        std::string ext = std::filesystem::path(fullPath).extension().string();
        std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
        if (ext == ".xmi") {
            midiPlayer_->selectSoundFont(getSoundFontTypeForFile(fullPath));
        }
    }

    musicPlayer_->setVolume(masterVolume_ * musicVolume_);
    if (!musicPlayer_->play(fullPath, loop, trackIndex, startTimeMs)) {
        LOG_WARN(MOD_AUDIO, "Failed to play music: {}", fullPath);
    }
}

void AudioManager::stopMusic(float fadeOutSeconds) {
    if (musicPlayer_) {
        musicPlayer_->stop(fadeOutSeconds);
    }
}

void AudioManager::pauseMusic() {
    if (musicPlayer_) {
        musicPlayer_->pause();
    }
}

void AudioManager::resumeMusic() {
    if (musicPlayer_) {
        musicPlayer_->resume();
    }
}

bool AudioManager::isMusicPlaying() const {
    return musicPlayer_ && musicPlayer_->isPlaying();
}

void AudioManager::onZoneChange(const std::string& zoneName) {
    LOG_DEBUG(MOD_AUDIO, "Zone change: {} -> {}", currentZone_, zoneName);

    if (!initialized_ || !audioEnabled_) {
        LOG_DEBUG(MOD_AUDIO, "Zone change ignored: not initialized or disabled");
        return;
    }

    if (zoneName == currentZone_) {
        LOG_DEBUG(MOD_AUDIO, "Zone change ignored: same zone");
        return;
    }

    LOG_INFO(MOD_AUDIO, "Zone change: {} -> {}", currentZone_, zoneName);
    currentZone_ = zoneName;

    // Clear saved zone music state from previous zone
    savedZoneMusicFile_.clear();
    savedZoneMusicTrackIndex_ = 0;
    savedZoneMusicTimeMs_ = 0.0;

    if (musicPlayer_ && musicPlayer_->isPlaying()) {
        stopMusic(2.0f);
    }
}

void AudioManager::restartZoneMusic() {
    if (!initialized_ || !audioEnabled_ || currentZone_.empty()) {
        return;
    }

    // Restore saved zone music with position if available
    if (!savedZoneMusicFile_.empty()) {
        double savedTimeMs = savedZoneMusicTimeMs_;
        int savedTrack = savedZoneMusicTrackIndex_;
        LOG_DEBUG(MOD_AUDIO, "Restoring zone music: file={} track={} pos={}ms",
                  savedZoneMusicFile_, savedTrack, savedTimeMs);
        playMusic(savedZoneMusicFile_, true, savedTrack, savedTimeMs);
        return;
    }

    std::string musicFile = findZoneMusic(currentZone_);
    if (!musicFile.empty()) {
        playMusic(musicFile, true);
    }
}

void AudioManager::startAutoAttackMusic() {
    if (!initialized_ || !audioEnabled_) return;
    if (!autoAttackMusicConfig_.enabled) return;

    if (vendorBankMusicActive_) {
        LOG_DEBUG(MOD_AUDIO, "Auto-attack music: skipped (vendor/bank music active)");
        autoAttackMusicActive_ = true;
        return;
    }

    if (autoAttackMusicActive_) return;

    LOG_DEBUG(MOD_AUDIO, "Auto-attack music: starting {} track {}",
              autoAttackMusicConfig_.file, autoAttackMusicConfig_.track);

    // Save zone music state before interrupting
    if (musicPlayer_ && musicPlayer_->isPlaying()) {
        savedZoneMusicFile_ = musicPlayer_->getCurrentFile();
        savedZoneMusicTrackIndex_ = musicPlayer_->getCurrentTrackIndex();
        savedZoneMusicTimeMs_ = midiPlayer_ ? midiPlayer_->getPlaybackTimeMs() : 0.0;
    }

    autoAttackMusicActive_ = true;

    std::string musicFile = eqPath_ + "/" + autoAttackMusicConfig_.file;
    if (std::filesystem::exists(musicFile)) {
        playMusic(musicFile, autoAttackMusicConfig_.loop, autoAttackMusicConfig_.track - 1);
    } else {
        LOG_WARN(MOD_AUDIO, "Auto-attack music: {} not found", autoAttackMusicConfig_.file);
    }
}

void AudioManager::stopAutoAttackMusic() {
    if (!autoAttackMusicActive_) return;

    LOG_DEBUG(MOD_AUDIO, "Auto-attack music: stopping");
    autoAttackMusicActive_ = false;

    if (vendorBankMusicActive_) return;

    restartZoneMusic();
}

void AudioManager::startVendorBankMusic() {
    if (!initialized_ || !audioEnabled_) return;
    if (!vendorBankMusicConfig_.enabled) return;
    if (vendorBankMusicActive_) return;

    LOG_DEBUG(MOD_AUDIO, "Vendor/bank music: starting {} track {}",
              vendorBankMusicConfig_.file, vendorBankMusicConfig_.track);

    // Save zone music state before interrupting (only if not already saved by auto-attack)
    if (!autoAttackMusicActive_ && musicPlayer_ && musicPlayer_->isPlaying()) {
        savedZoneMusicFile_ = musicPlayer_->getCurrentFile();
        savedZoneMusicTrackIndex_ = musicPlayer_->getCurrentTrackIndex();
        savedZoneMusicTimeMs_ = midiPlayer_ ? midiPlayer_->getPlaybackTimeMs() : 0.0;
    }

    vendorBankMusicActive_ = true;

    std::string musicFile = eqPath_ + "/" + vendorBankMusicConfig_.file;
    if (std::filesystem::exists(musicFile)) {
        playMusic(musicFile, vendorBankMusicConfig_.loop, vendorBankMusicConfig_.track - 1);
    } else {
        LOG_WARN(MOD_AUDIO, "Vendor/bank music: {} not found", vendorBankMusicConfig_.file);
    }
}

void AudioManager::stopVendorBankMusic() {
    if (!vendorBankMusicActive_) return;

    LOG_DEBUG(MOD_AUDIO, "Vendor/bank music: stopping");
    vendorBankMusicActive_ = false;

    if (autoAttackMusicActive_ && autoAttackMusicConfig_.enabled) {
        std::string musicFile = eqPath_ + "/" + autoAttackMusicConfig_.file;
        if (std::filesystem::exists(musicFile)) {
            playMusic(musicFile, autoAttackMusicConfig_.loop, autoAttackMusicConfig_.track - 1);
        }
        return;
    }

    restartZoneMusic();
}

void AudioManager::setMasterVolume(float volume) {
    masterVolume_ = std::clamp(volume, 0.0f, 1.0f);
    if (mixer_) {
        mixer_->setMasterVolume(masterVolume_);
    }
    if (musicPlayer_) {
        musicPlayer_->setVolume(masterVolume_ * musicVolume_);
    }
}

void AudioManager::setMusicVolume(float volume) {
    musicVolume_ = std::clamp(volume, 0.0f, 1.0f);
    if (mixer_) {
        mixer_->setMusicVolume(musicVolume_);
    }
    if (musicPlayer_) {
        musicPlayer_->setVolume(masterVolume_ * musicVolume_);
    }
}

void AudioManager::setEffectsVolume(float volume) {
    effectsVolume_ = std::clamp(volume, 0.0f, 1.0f);
    if (mixer_) {
        mixer_->setSfxVolume(effectsVolume_);
    }
}

void AudioManager::setListenerPosition(const glm::vec3& position,
                                        const glm::vec3& forward,
                                        const glm::vec3& /*up*/) {
    if (!initialized_) return;

    if (sfxManager_) {
        sfxManager_->updateListener(position, forward);
    }
}

void AudioManager::setAudioEnabled(bool enabled) {
    audioEnabled_ = enabled;

    if (!enabled) {
        stopAllSounds();
        stopMusic(0.0f);
    }
}

void AudioManager::setAudioOutputCallback(AudioOutputCallback callback) {
    audioOutputCallback_ = std::move(callback);

    // If we have an RDP backend, set the callback on it
    if (loopbackMode_ && backend_) {
        auto* rdp = dynamic_cast<RDPAudioBackend*>(backend_.get());
        if (rdp) {
            rdp->setOutputCallback(audioOutputCallback_);
        }
    }
}

void AudioManager::setMemoryConstraints(size_t soundCacheBytes, bool lazyPfs) {
    soundBufferCacheMaxBytes_ = soundCacheBytes;
    lazyPfsLoading_ = lazyPfs;

    if (sfxManager_) {
        sfxManager_->setCacheMaxBytes(soundCacheBytes);
    }

    if (lazyPfs && !pfsArchives_.empty()) {
        LOG_INFO(MOD_AUDIO, "Lazy PFS mode: releasing {} cached archives", pfsArchives_.size());
        pfsArchives_.clear();
    }
}

std::shared_ptr<SoundBuffer> AudioManager::loadSound(const std::string& filename) {
    std::lock_guard<std::mutex> lock(bufferMutex_);

    // Check cache
    auto it = bufferCache_.find(filename);
    if (it != bufferCache_.end()) {
        if (soundBufferCacheMaxBytes_ > 0) {
            bufferLruOrder_.remove(filename);
            bufferLruOrder_.push_front(filename);
        }
        return it->second;
    }

    // Build full path for filesystem check
    std::string fullPath = filename;
    if (!std::filesystem::path(filename).is_absolute()) {
        fullPath = eqPath_ + "/sounds/" + filename;
    }

    // Try loading from filesystem first
    auto buffer = std::make_shared<SoundBuffer>();
    bool loaded = false;
    if (std::filesystem::exists(fullPath) && buffer->loadFromFile(fullPath)) {
        loaded = true;
    } else {
        // Try loading from PFS archives
        buffer = loadSoundFromPfs(filename);
        if (buffer) {
            loaded = true;
        }
    }

    if (loaded && buffer) {
        bufferCache_[filename] = buffer;

        // LRU tracking
        if (soundBufferCacheMaxBytes_ > 0) {
            bufferLruOrder_.push_front(filename);
            soundBufferCacheSizeBytes_ += buffer->getMemorySize();

            while (soundBufferCacheSizeBytes_ > soundBufferCacheMaxBytes_ &&
                   bufferLruOrder_.size() > 1) {
                const std::string& oldest = bufferLruOrder_.back();
                auto evictIt = bufferCache_.find(oldest);
                if (evictIt != bufferCache_.end()) {
                    soundBufferCacheSizeBytes_ -= evictIt->second->getMemorySize();
                    bufferCache_.erase(evictIt);
                }
                bufferLruOrder_.pop_back();
            }
        }

        return buffer;
    }

    LOG_DEBUG(MOD_AUDIO, "Failed to load sound: {} (not found on disk or in PFS)", filename);
    return nullptr;
}

std::shared_ptr<SoundBuffer> AudioManager::getSoundById(uint32_t soundId) {
    auto it = soundIdMap_.find(soundId);
    if (it == soundIdMap_.end()) {
        return nullptr;
    }

    return loadSound(it->second);
}

std::string AudioManager::findZoneMusic(const std::string& zoneName) {
    std::string lowerZone = zoneName;
    std::transform(lowerZone.begin(), lowerZone.end(), lowerZone.begin(), ::tolower);

    static const std::unordered_map<std::string, std::string> zoneMusicMap = {
        {"oasis", "nro"}, {"sro", "nro"}, {"scarlet", "nro"},
        {"ecommons", "nektulos"}, {"commons", "nektulos"}, {"wcommons", "nektulos"},
        {"lfaydark", "gfaydark"}, {"steamfont", "gfaydark"},
        {"qeytoqrg", "qeynos"}, {"qey2hh1", "qeynos"},
        {"westkarana", "southkarana"},
    };

    auto mapIt = zoneMusicMap.find(lowerZone);
    if (mapIt != zoneMusicMap.end()) {
        lowerZone = mapIt->second;
    }

    std::string xmiPath = eqPath_ + "/" + lowerZone + ".xmi";
    if (std::filesystem::exists(xmiPath)) {
        return xmiPath;
    }

    std::string mp3Path = eqPath_ + "/" + lowerZone + ".mp3";
    if (std::filesystem::exists(mp3Path)) {
        return mp3Path;
    }

    return "";
}

void AudioManager::loadSoundAssets() {
    std::string assetsPath = eqPath_ + "/SoundAssets.txt";
    if (!std::filesystem::exists(assetsPath)) {
        LOG_WARN(MOD_AUDIO, "SoundAssets.txt not found at: {}", assetsPath);
        return;
    }

    SoundAssets assets;
    if (!assets.loadFromFile(assetsPath)) {
        LOG_WARN(MOD_AUDIO, "Failed to parse SoundAssets.txt");
        return;
    }

    soundIdMap_.clear();
    assets.forEach([this](uint32_t id, const std::string& filename, float /*volume*/) {
        soundIdMap_[id] = filename;
    });

    LOG_INFO(MOD_AUDIO, "Loaded {} sound asset mappings", soundIdMap_.size());
}

void AudioManager::preloadSound(uint32_t soundId) {
    if (!initialized_) return;

    auto buffer = getSoundById(soundId);
    if (buffer && buffer->isValid()) {
        LOG_DEBUG(MOD_AUDIO, "Preloaded sound ID {}", soundId);
    }
}

void AudioManager::preloadCommonSounds() {
    if (!initialized_) return;

    LOG_INFO(MOD_AUDIO, "Preloading common sound effects...");

    static const uint32_t commonSounds[] = {
        118, 119, 126, 127, 128, 130, 131,    // Combat
        103, 104, 105, 106, 107, 108,          // Spells
        139,                                     // Level up
        100, 101, 102,                           // Environment
    };

    size_t loaded = 0;
    for (uint32_t soundId : commonSounds) {
        auto buffer = getSoundById(soundId);
        if (buffer && buffer->isValid()) {
            ++loaded;
        }
    }

    LOG_INFO(MOD_AUDIO, "Preloaded {}/{} common sounds", loaded, sizeof(commonSounds)/sizeof(commonSounds[0]));
}

size_t AudioManager::getLoadedSoundCount() const {
    std::lock_guard<std::mutex> lock(bufferMutex_);
    return bufferCache_.size();
}

std::shared_ptr<SoundBuffer> AudioManager::getSoundBuffer(uint32_t soundId) {
    return getSoundById(soundId);
}

std::shared_ptr<SoundBuffer> AudioManager::getSoundBufferByName(const std::string& filename) {
    return loadSound(filename);
}

bool AudioManager::enableLoopbackMode() {
    if (loopbackMode_) {
        LOG_DEBUG(MOD_AUDIO, "Already in loopback mode");
        return true;
    }

    if (!initialized_) {
        LOG_ERROR(MOD_AUDIO, "Cannot enable loopback mode - audio not initialized");
        return false;
    }

    LOG_INFO(MOD_AUDIO, "Switching to loopback mode for RDP audio streaming");

    // Stop current backend
    if (backend_) {
        backend_->stop();
        backend_.reset();
    }

    // Create RDP backend
    auto rdpBackend = std::make_unique<RDPAudioBackend>();
    if (audioOutputCallback_) {
        rdpBackend->setOutputCallback(audioOutputCallback_);
    }
    rdpBackend->init(mixer_.get(), AudioMixer::SAMPLE_RATE);
    rdpBackend->start();
    backend_ = std::move(rdpBackend);
    loopbackMode_ = true;

    LOG_INFO(MOD_AUDIO, "Switched to loopback mode for RDP audio streaming");
    return true;
}

void AudioManager::update() {
    // Per-frame audio updates
    // The mixer and backends handle their own timing
}

bool AudioManager::loadPfsIndexCache() {
    std::string cachePath = "config/snd_index_cache.json";
    std::ifstream cacheFile(cachePath);
    if (!cacheFile.is_open()) {
        return false;
    }

    Json::Value root;
    Json::CharReaderBuilder builder;
    std::string errors;
    if (!Json::parseFromStream(builder, cacheFile, &root, &errors)) {
        LOG_DEBUG(MOD_AUDIO, "PFS index cache parse error: {}", errors);
        return false;
    }

    if (!root.isMember("archives") || !root.isMember("index") || !root.isMember("eqPath")) {
        return false;
    }

    if (root["eqPath"].asString() != eqPath_) {
        LOG_DEBUG(MOD_AUDIO, "PFS index cache stale: eqPath changed");
        return false;
    }

    const Json::Value& archives = root["archives"];
    for (const auto& name : archives.getMemberNames()) {
        std::string archivePath = eqPath_ + "/" + name;
        if (!std::filesystem::exists(archivePath)) {
            LOG_DEBUG(MOD_AUDIO, "PFS index cache stale: {} missing", name);
            return false;
        }
        auto fileSize = std::filesystem::file_size(archivePath);
        if (fileSize != static_cast<uintmax_t>(archives[name].asUInt64())) {
            LOG_DEBUG(MOD_AUDIO, "PFS index cache stale: {} size changed", name);
            return false;
        }
    }

    size_t pfsCount = 0;
    for (const auto& entry : std::filesystem::directory_iterator(eqPath_)) {
        if (!entry.is_regular_file()) continue;
        std::string filename = entry.path().filename().string();
        std::string lower = filename;
        std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
        if (lower.length() > 7 && lower.substr(0, 3) == "snd" &&
            lower.substr(lower.length() - 4) == ".pfs") {
            pfsCount++;
        }
    }
    if (pfsCount != archives.size()) {
        LOG_DEBUG(MOD_AUDIO, "PFS index cache stale: archive count changed ({} vs {})",
                  pfsCount, archives.size());
        return false;
    }

    const Json::Value& index = root["index"];
    for (const auto& filename : index.getMemberNames()) {
        pfsFileIndex_[filename] = eqPath_ + "/" + index[filename].asString();
    }

    LOG_INFO(MOD_AUDIO, "Loaded PFS index cache: {} sound files from {}",
             pfsFileIndex_.size(), cachePath);
    return true;
}

void AudioManager::savePfsIndexCache() {
    Json::Value root;
    root["eqPath"] = eqPath_;

    Json::Value archives(Json::objectValue);
    std::set<std::string> archiveNames;
    for (const auto& [filename, archivePath] : pfsFileIndex_) {
        std::string name = std::filesystem::path(archivePath).filename().string();
        if (archiveNames.insert(name).second) {
            auto fileSize = std::filesystem::file_size(archivePath);
            archives[name] = static_cast<Json::UInt64>(fileSize);
        }
    }
    root["archives"] = archives;

    Json::Value index(Json::objectValue);
    for (const auto& [filename, archivePath] : pfsFileIndex_) {
        index[filename] = std::filesystem::path(archivePath).filename().string();
    }
    root["index"] = index;

    std::string cachePath = "config/snd_index_cache.json";
    std::ofstream out(cachePath);
    if (!out.is_open()) {
        LOG_WARN(MOD_AUDIO, "Failed to save PFS index cache to {}", cachePath);
        return;
    }

    Json::StreamWriterBuilder writerBuilder;
    writerBuilder["indentation"] = "";
    out << Json::writeString(writerBuilder, root);
    LOG_INFO(MOD_AUDIO, "Saved PFS index cache: {} entries to {}", pfsFileIndex_.size(), cachePath);
}

void AudioManager::scanPfsArchives() {
    pfsFileIndex_.clear();

    if (!std::filesystem::exists(eqPath_) || !std::filesystem::is_directory(eqPath_)) {
        LOG_DEBUG(MOD_AUDIO, "EQ path does not exist or is not a directory: {}", eqPath_);
        return;
    }

    if (loadPfsIndexCache()) {
        return;
    }

    std::vector<std::string> archivePaths;
    for (const auto& entry : std::filesystem::directory_iterator(eqPath_)) {
        if (!entry.is_regular_file()) continue;

        std::string filename = entry.path().filename().string();
        std::string lowerFilename = filename;
        std::transform(lowerFilename.begin(), lowerFilename.end(),
                       lowerFilename.begin(), ::tolower);

        if (lowerFilename.length() > 7 &&
            lowerFilename.substr(0, 3) == "snd" &&
            lowerFilename.substr(lowerFilename.length() - 4) == ".pfs") {
            archivePaths.push_back(entry.path().string());
        }
    }

    if (archivePaths.empty()) {
        LOG_DEBUG(MOD_AUDIO, "No snd*.pfs archives found in {}", eqPath_);
        return;
    }

    LOG_INFO(MOD_AUDIO, "Scanning {} sound archives (building index)...", archivePaths.size());

    size_t totalFiles = 0;
    for (const auto& archivePath : archivePaths) {
        auto archive = std::make_unique<Graphics::PfsArchive>();
        if (!archive->open(archivePath)) {
            LOG_WARN(MOD_AUDIO, "Failed to open archive: {}", archivePath);
            continue;
        }

        std::vector<std::string> wavFiles;
        archive->getFilenames(".wav", wavFiles);

        for (const auto& wavFile : wavFiles) {
            std::string lowerFile = wavFile;
            std::transform(lowerFile.begin(), lowerFile.end(),
                           lowerFile.begin(), ::tolower);
            pfsFileIndex_[lowerFile] = archivePath;
            totalFiles++;
        }

        if (!lazyPfsLoading_) {
            pfsArchives_[archivePath] = std::move(archive);
        }

        if (tickCallback_) tickCallback_();
    }

    LOG_INFO(MOD_AUDIO, "Indexed {} sound files from {} archives (cached: {})",
             totalFiles, archivePaths.size(), pfsArchives_.size());

    savePfsIndexCache();
}

bool AudioManager::loadSoundDataFromPfs(const std::string& filename,
                                         std::vector<char>& outData) {
    std::string lowerFilename = filename;
    std::transform(lowerFilename.begin(), lowerFilename.end(),
                   lowerFilename.begin(), ::tolower);

    auto indexIt = pfsFileIndex_.find(lowerFilename);
    if (indexIt == pfsFileIndex_.end()) {
        return false;
    }

    const std::string& archivePath = indexIt->second;

    if (lazyPfsLoading_) {
        Graphics::PfsArchive archive;
        if (!archive.open(archivePath)) {
            LOG_WARN(MOD_AUDIO, "Failed to open archive (lazy): {}", archivePath);
            return false;
        }
        if (!archive.get(lowerFilename, outData)) {
            LOG_DEBUG(MOD_AUDIO, "Failed to extract {} from {} (lazy)", lowerFilename, archivePath);
            return false;
        }
        return true;
    }

    auto archiveIt = pfsArchives_.find(archivePath);
    if (archiveIt == pfsArchives_.end()) {
        auto archive = std::make_unique<Graphics::PfsArchive>();
        if (!archive->open(archivePath)) {
            LOG_WARN(MOD_AUDIO, "Failed to reopen archive: {}", archivePath);
            return false;
        }
        archiveIt = pfsArchives_.emplace(archivePath, std::move(archive)).first;
    }

    if (!archiveIt->second->get(lowerFilename, outData)) {
        LOG_DEBUG(MOD_AUDIO, "Failed to extract {} from {}", lowerFilename, archivePath);
        return false;
    }

    return true;
}

std::shared_ptr<SoundBuffer> AudioManager::loadSoundFromPfs(const std::string& filename) {
    std::vector<char> wavData;
    if (!loadSoundDataFromPfs(filename, wavData)) {
        return nullptr;
    }

    auto buffer = std::make_shared<SoundBuffer>();
    if (!buffer->loadFromMemory(wavData.data(), wavData.size())) {
        LOG_DEBUG(MOD_AUDIO, "Failed to decode WAV from PFS: {}", filename);
        return nullptr;
    }

    LOG_DEBUG(MOD_AUDIO, "Loaded sound from PFS: {}", filename);
    return buffer;
}

void AudioManager::loadMusicEventConfig() {
    autoAttackMusicConfig_.file = "gl.xmi";
    autoAttackMusicConfig_.track = 3;
    autoAttackMusicConfig_.loop = true;
    autoAttackMusicConfig_.enabled = true;

    vendorBankMusicConfig_.file = "gl.xmi";
    vendorBankMusicConfig_.track = 22;
    vendorBankMusicConfig_.loop = true;
    vendorBankMusicConfig_.enabled = true;

    std::string configPath = "config/music_events.json";
    std::ifstream file(configPath);
    if (!file.is_open()) {
        LOG_DEBUG(MOD_AUDIO, "Music events config not found at {}, using defaults", configPath);
        return;
    }

    Json::Value root;
    Json::CharReaderBuilder builder;
    std::string errors;
    if (!Json::parseFromStream(builder, file, &root, &errors)) {
        LOG_WARN(MOD_AUDIO, "Failed to parse music_events.json: {}", errors);
        return;
    }

    if (root.isMember("auto_attack")) {
        const auto& cfg = root["auto_attack"];
        if (cfg.isMember("file")) autoAttackMusicConfig_.file = cfg["file"].asString();
        if (cfg.isMember("track")) autoAttackMusicConfig_.track = cfg["track"].asInt();
        if (cfg.isMember("loop")) autoAttackMusicConfig_.loop = cfg["loop"].asBool();
        if (cfg.isMember("enabled")) autoAttackMusicConfig_.enabled = cfg["enabled"].asBool();
    }

    if (root.isMember("vendor_bank")) {
        const auto& cfg = root["vendor_bank"];
        if (cfg.isMember("file")) vendorBankMusicConfig_.file = cfg["file"].asString();
        if (cfg.isMember("track")) vendorBankMusicConfig_.track = cfg["track"].asInt();
        if (cfg.isMember("loop")) vendorBankMusicConfig_.loop = cfg["loop"].asBool();
        if (cfg.isMember("enabled")) vendorBankMusicConfig_.enabled = cfg["enabled"].asBool();
    }

    LOG_INFO(MOD_AUDIO, "Loaded music events config: auto_attack={} track {} loop={} enabled={}, vendor_bank={} track {} loop={} enabled={}",
             autoAttackMusicConfig_.file, autoAttackMusicConfig_.track,
             autoAttackMusicConfig_.loop, autoAttackMusicConfig_.enabled,
             vendorBankMusicConfig_.file, vendorBankMusicConfig_.track,
             vendorBankMusicConfig_.loop, vendorBankMusicConfig_.enabled);
}

SoundFontType AudioManager::getSoundFontTypeForFile(const std::string& path) {
    // XMI files that require the synthus2 custom orchestral SoundFont
    // (Velious and Luclin/Shadows of Luclin zones)
    static const std::unordered_set<std::string> synthus2Files = {
        // Velious (8)
        "cobaltscar", "crystal", "frozenshadow", "kael", "skyshrine",
        "templeveeshan", "thurgadina", "velketor",
        // Luclin/SoL (23)
        "acrylia", "dawnshroud", "echo", "fungusgrove", "griegsend",
        "grimling", "hollowshade", "katta", "letalis", "maiden", "mseru",
        "netherbian", "nexus", "paludal", "shadeweaver", "shadowhaven",
        "sharvahl", "sseru", "tenebrous", "thedeep", "thegrey", "twilight",
        "vexthal",
    };

    std::string stem = std::filesystem::path(path).stem().string();
    std::transform(stem.begin(), stem.end(), stem.begin(), ::tolower);
    return synthus2Files.count(stem) ? SoundFontType::Custom : SoundFontType::GM;
}

} // namespace Audio
} // namespace EQT

#endif // WITH_AUDIO
