#ifdef WITH_AUDIO

#include "client/audio/audio_manager.h"
#include "client/audio/sound_buffer.h"
#include "client/audio/music_player.h"
#include "client/audio/sound_assets.h"
#include "client/graphics/eq/pfs.h"
#include "common/logging.h"

#include <filesystem>
#include <algorithm>
#include <cctype>
#include <iostream>
#include <unordered_map>

namespace EQT {
namespace Audio {

AudioManager::AudioManager() = default;

AudioManager::~AudioManager() {
    shutdown();
}

bool AudioManager::initialize(const std::string& eqPath, bool forceLoopback,
                              const std::string& soundFontPath) {
    if (initialized_) {
        return true;
    }

    eqPath_ = eqPath;
    soundFontPath_ = soundFontPath;

    // Try to initialize device
    bool deviceReady = false;

    if (forceLoopback) {
        // User explicitly requested loopback mode (e.g., RDP-only server)
        LOG_INFO(MOD_AUDIO, "Loopback mode requested, skipping hardware device");
        deviceReady = initializeLoopbackDevice();
    } else {
        // Try hardware device first
        deviceReady = initializeHardwareDevice();

        if (!deviceReady) {
            // Fall back to loopback device
            LOG_INFO(MOD_AUDIO, "No hardware audio device, trying loopback mode");
            deviceReady = initializeLoopbackDevice();
        }
    }

    if (!deviceReady) {
        LOG_ERROR(MOD_AUDIO, "Failed to initialize any audio device");
        return false;
    }

    // Configure 3D audio distance model
    // AL_INVERSE_DISTANCE_CLAMPED: gain = ref_dist / (ref_dist + rolloff * (distance - ref_dist))
    // clamped to [0, max_dist]
    alDistanceModel(AL_INVERSE_DISTANCE_CLAMPED);
    alSpeedOfSound(343.3f);  // Speed of sound in m/s (for Doppler, if enabled)
    alDopplerFactor(0.0f);   // Disable Doppler effect for now (can cause issues)

    LOG_DEBUG(MOD_AUDIO, "3D audio distance model configured (inverse distance clamped)");

    // Create source pool
    availableSources_.resize(MAX_SOURCES);
    alGenSources(MAX_SOURCES, availableSources_.data());
    ALenum error = alGetError();
    if (error != AL_NO_ERROR) {
        LOG_WARN(MOD_AUDIO, "Could only create partial source pool: {}", alGetString(error));
        // Remove invalid sources
        availableSources_.erase(
            std::remove(availableSources_.begin(), availableSources_.end(), 0),
            availableSources_.end()
        );
    }

    LOG_INFO(MOD_AUDIO, "Audio source pool: {} sources", availableSources_.size());

    // Initialize music player
    musicPlayer_ = std::make_unique<MusicPlayer>();
    std::cout << "[AUDIO] Initializing music player with eqPath: " << eqPath_
              << ", soundfont: " << (soundFontPath_.empty() ? "(none)" : soundFontPath_) << std::endl;
    if (!musicPlayer_->initialize(eqPath_, soundFontPath_)) {
        LOG_WARN(MOD_AUDIO, "Music player initialization failed, music will be disabled");
    }

    // Load sound asset mapping
    loadSoundAssets();

    // Scan PFS archives for sound files
    scanPfsArchives();

    // Set initial listener position
    ALfloat listenerPos[] = { 0.0f, 0.0f, 0.0f };
    ALfloat listenerVel[] = { 0.0f, 0.0f, 0.0f };
    ALfloat listenerOri[] = { 0.0f, 0.0f, -1.0f, 0.0f, 1.0f, 0.0f };
    alListenerfv(AL_POSITION, listenerPos);
    alListenerfv(AL_VELOCITY, listenerVel);
    alListenerfv(AL_ORIENTATION, listenerOri);

    initialized_ = true;
    LOG_INFO(MOD_AUDIO, "Audio system initialized");
    return true;
}

void AudioManager::shutdown() {
    if (!initialized_) {
        return;
    }

    LOG_INFO(MOD_AUDIO, "Shutting down audio system");

    // Stop loopback thread first
    if (loopbackMode_ && loopbackRunning_.load()) {
        loopbackRunning_ = false;
        if (loopbackThread_.joinable()) {
            loopbackThread_.join();
        }
    }

    // Stop all sounds
    stopAllSounds();

    // Stop music
    if (musicPlayer_) {
        musicPlayer_->stop();
        musicPlayer_->shutdown();
        musicPlayer_.reset();
    }

    // Clear buffer cache
    {
        std::lock_guard<std::mutex> lock(bufferMutex_);
        bufferCache_.clear();
    }

    // Delete sources
    {
        std::lock_guard<std::mutex> lock(sourceMutex_);
        for (ALuint source : availableSources_) {
            if (source != 0) {
                alDeleteSources(1, &source);
            }
        }
        for (ALuint source : activeSources_) {
            if (source != 0) {
                alSourceStop(source);
                alDeleteSources(1, &source);
            }
        }
        availableSources_.clear();
        activeSources_.clear();
    }

    // Destroy context
    alcMakeContextCurrent(nullptr);
    if (context_) {
        alcDestroyContext(context_);
        context_ = nullptr;
    }
    if (device_) {
        alcCloseDevice(device_);
        device_ = nullptr;
    }

    initialized_ = false;
}

void AudioManager::playSound(uint32_t soundId) {
    playSound(soundId, glm::vec3(0.0f));
}

void AudioManager::playSound(uint32_t soundId, const glm::vec3& position) {
    if (!initialized_ || !audioEnabled_) {
        return;
    }

    // Get filename for debug output
    std::string filename = "unknown";
    auto it = soundIdMap_.find(soundId);
    if (it != soundIdMap_.end()) {
        filename = it->second;
    }

    auto buffer = getSoundById(soundId);
    if (!buffer || !buffer->isValid()) {
        std::cout << "[AUDIO] SOUND FAILED: id=" << soundId << " file=" << filename << " (not found)" << std::endl;
        LOG_DEBUG(MOD_AUDIO, "Sound ID {} not found", soundId);
        return;
    }

    ALuint source = acquireSource();
    if (source == 0) {
        std::cout << "[AUDIO] SOUND FAILED: id=" << soundId << " file=" << filename << " (no source)" << std::endl;
        LOG_DEBUG(MOD_AUDIO, "No available audio sources");
        return;
    }

    std::cout << "[AUDIO] SOUND PLAY: id=" << soundId << " file=" << filename
              << " pos=(" << position.x << "," << position.y << "," << position.z << ")" << std::endl;

    alSourcei(source, AL_BUFFER, buffer->getBuffer());
    alSource3f(source, AL_POSITION, position.x, position.y, position.z);
    alSourcef(source, AL_GAIN, masterVolume_ * effectsVolume_);
    alSourcei(source, AL_SOURCE_RELATIVE, AL_FALSE);

    // Configure 3D audio distance attenuation
    // EQ units are roughly feet, so:
    // - Reference distance: Sound is at full volume within 50 units
    // - Max distance: Sound becomes inaudible beyond 500 units
    // - Rolloff factor: 1.0 for standard inverse distance attenuation
    alSourcef(source, AL_REFERENCE_DISTANCE, 50.0f);
    alSourcef(source, AL_MAX_DISTANCE, 500.0f);
    alSourcef(source, AL_ROLLOFF_FACTOR, 1.0f);

    alSourcePlay(source);
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
        std::cout << "[AUDIO] SOUND FAILED: file=" << filename << " (not found)" << std::endl;
        return;
    }

    ALuint source = acquireSource();
    if (source == 0) {
        std::cout << "[AUDIO] SOUND FAILED: file=" << filename << " (no source)" << std::endl;
        return;
    }

    std::cout << "[AUDIO] SOUND PLAY: file=" << filename
              << " pos=(" << position.x << "," << position.y << "," << position.z << ")" << std::endl;

    alSourcei(source, AL_BUFFER, buffer->getBuffer());
    alSource3f(source, AL_POSITION, position.x, position.y, position.z);
    alSourcef(source, AL_GAIN, masterVolume_ * effectsVolume_);
    alSourcei(source, AL_SOURCE_RELATIVE, AL_FALSE);

    // Configure 3D audio distance attenuation
    alSourcef(source, AL_REFERENCE_DISTANCE, 50.0f);
    alSourcef(source, AL_MAX_DISTANCE, 500.0f);
    alSourcef(source, AL_ROLLOFF_FACTOR, 1.0f);

    alSourcePlay(source);
}

void AudioManager::stopAllSounds() {
    std::lock_guard<std::mutex> lock(sourceMutex_);

    for (ALuint source : activeSources_) {
        alSourceStop(source);
        alSourcei(source, AL_BUFFER, 0);
        availableSources_.push_back(source);
    }
    activeSources_.clear();
}

void AudioManager::playMusic(const std::string& filename, bool loop) {
    if (!initialized_ || !audioEnabled_ || !musicPlayer_) {
        std::cout << "[AUDIO] MUSIC FAILED: file=" << filename << " (not initialized or disabled)" << std::endl;
        return;
    }

    std::string fullPath = filename;
    if (!std::filesystem::path(filename).is_absolute()) {
        fullPath = eqPath_ + "/" + filename;
    }

    if (!std::filesystem::exists(fullPath)) {
        std::cout << "[AUDIO] MUSIC FAILED: file=" << fullPath << " (not found)" << std::endl;
        LOG_WARN(MOD_AUDIO, "Music file not found: {}", fullPath);
        return;
    }

    // Skip if the same file is already playing
    if (musicPlayer_->isPlaying() && musicPlayer_->getCurrentFile() == fullPath) {
        std::cout << "[AUDIO] MUSIC SKIP: file=" << fullPath << " (already playing)" << std::endl;
        return;
    }

    std::cout << "[AUDIO] MUSIC PLAY: file=" << fullPath << " loop=" << (loop ? "yes" : "no") << std::endl;

    musicPlayer_->setVolume(masterVolume_ * musicVolume_);
    if (!musicPlayer_->play(fullPath, loop)) {
        std::cout << "[AUDIO] MUSIC FAILED: file=" << fullPath << " (playback error)" << std::endl;
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
    std::cout << "[AUDIO] ZONE CHANGE: " << currentZone_ << " -> " << zoneName << std::endl;

    if (!initialized_ || !audioEnabled_) {
        std::cout << "[AUDIO] ZONE CHANGE IGNORED: not initialized or disabled" << std::endl;
        return;
    }

    if (zoneName == currentZone_) {
        std::cout << "[AUDIO] ZONE CHANGE IGNORED: same zone" << std::endl;
        return;
    }

    LOG_INFO(MOD_AUDIO, "Zone change: {} -> {}", currentZone_, zoneName);
    currentZone_ = zoneName;

    // Find and play zone music
    std::string musicFile = findZoneMusic(zoneName);
    std::cout << "[AUDIO] ZONE MUSIC SEARCH: zone=" << zoneName << " found=" << (musicFile.empty() ? "none" : musicFile) << std::endl;

    if (!musicFile.empty()) {
        // Fade out current, then start new
        if (musicPlayer_ && musicPlayer_->isPlaying()) {
            std::cout << "[AUDIO] STOPPING CURRENT MUSIC (fade 2s)" << std::endl;
            musicPlayer_->stop(2.0f);
        }
        // Start new music after fade (handled by music player)
        playMusic(musicFile, true);
    } else {
        // No music for this zone, just stop
        std::cout << "[AUDIO] NO MUSIC FOR ZONE: " << zoneName << std::endl;
        stopMusic(2.0f);
    }
}

void AudioManager::restartZoneMusic() {
    if (!initialized_ || !audioEnabled_ || currentZone_.empty()) {
        return;
    }

    std::string musicFile = findZoneMusic(currentZone_);
    if (!musicFile.empty()) {
        playMusic(musicFile, true);
    }
}

void AudioManager::setMasterVolume(float volume) {
    masterVolume_ = std::clamp(volume, 0.0f, 1.0f);
    alListenerf(AL_GAIN, masterVolume_);

    if (musicPlayer_) {
        musicPlayer_->setVolume(masterVolume_ * musicVolume_);
    }
}

void AudioManager::setMusicVolume(float volume) {
    musicVolume_ = std::clamp(volume, 0.0f, 1.0f);

    if (musicPlayer_) {
        musicPlayer_->setVolume(masterVolume_ * musicVolume_);
    }
}

void AudioManager::setEffectsVolume(float volume) {
    effectsVolume_ = std::clamp(volume, 0.0f, 1.0f);
}

void AudioManager::setListenerPosition(const glm::vec3& position,
                                        const glm::vec3& forward,
                                        const glm::vec3& up) {
    if (!initialized_) {
        return;
    }

    ALfloat listenerPos[] = { position.x, position.y, position.z };
    ALfloat listenerOri[] = { forward.x, forward.y, forward.z, up.x, up.y, up.z };

    alListenerfv(AL_POSITION, listenerPos);
    alListenerfv(AL_ORIENTATION, listenerOri);
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

    if (musicPlayer_) {
        musicPlayer_->setOutputCallback([this](const int16_t* samples, size_t count) {
            if (audioOutputCallback_) {
                audioOutputCallback_(samples, count, 44100, 2);
            }
        });
    }
}

void AudioManager::onSoundFinished(ALuint source) {
    releaseSource(source);
}

std::shared_ptr<SoundBuffer> AudioManager::loadSound(const std::string& filename) {
    std::lock_guard<std::mutex> lock(bufferMutex_);

    // Check cache
    auto it = bufferCache_.find(filename);
    if (it != bufferCache_.end()) {
        return it->second;
    }

    // Build full path for filesystem check
    std::string fullPath = filename;
    if (!std::filesystem::path(filename).is_absolute()) {
        fullPath = eqPath_ + "/sounds/" + filename;
    }

    // Try loading from filesystem first
    auto buffer = std::make_shared<SoundBuffer>();
    if (std::filesystem::exists(fullPath) && buffer->loadFromFile(fullPath)) {
        bufferCache_[filename] = buffer;
        return buffer;
    }

    // Try loading from PFS archives
    buffer = loadSoundFromPfs(filename);
    if (buffer) {
        bufferCache_[filename] = buffer;
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

    // Zone music mappings for zones that share music with other zones
    static const std::unordered_map<std::string, std::string> zoneMusicMap = {
        // Desert zones use North Ro music
        {"oasis", "nro"},
        {"sro", "nro"},
        {"scarlet", "nro"},
        // Common outdoor zones
        {"ecommons", "nektulos"},
        {"commons", "nektulos"},
        {"wcommons", "nektulos"},
        // Faydwer outdoor
        {"lfaydark", "gfaydark"},
        {"steamfont", "gfaydark"},
        // Antonica outdoor
        {"qeytoqrg", "qeynos"},
        {"qey2hh1", "qeynos"},
        {"westkarana", "southkarana"},
    };

    // Check for zone mapping first
    auto mapIt = zoneMusicMap.find(lowerZone);
    if (mapIt != zoneMusicMap.end()) {
        lowerZone = mapIt->second;
    }

    // Try XMI first (original MIDI format)
    std::string xmiPath = eqPath_ + "/" + lowerZone + ".xmi";
    if (std::filesystem::exists(xmiPath)) {
        return xmiPath;
    }

    // Try MP3 (later expansions)
    std::string mp3Path = eqPath_ + "/" + lowerZone + ".mp3";
    if (std::filesystem::exists(mp3Path)) {
        return mp3Path;
    }

    return "";
}

ALuint AudioManager::acquireSource() {
    std::lock_guard<std::mutex> lock(sourceMutex_);

    // First, check for finished sources and reclaim them
    auto it = activeSources_.begin();
    while (it != activeSources_.end()) {
        ALint state;
        alGetSourcei(*it, AL_SOURCE_STATE, &state);
        if (state != AL_PLAYING) {
            alSourcei(*it, AL_BUFFER, 0);
            availableSources_.push_back(*it);
            it = activeSources_.erase(it);
        } else {
            ++it;
        }
    }

    // Get an available source
    if (availableSources_.empty()) {
        return 0;
    }

    ALuint source = availableSources_.back();
    availableSources_.pop_back();
    activeSources_.push_back(source);
    return source;
}

void AudioManager::releaseSource(ALuint source) {
    std::lock_guard<std::mutex> lock(sourceMutex_);

    auto it = std::find(activeSources_.begin(), activeSources_.end(), source);
    if (it != activeSources_.end()) {
        alSourceStop(source);
        alSourcei(source, AL_BUFFER, 0);
        activeSources_.erase(it);
        availableSources_.push_back(source);
    }
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

    // Build our lookup map from sound ID to filename
    soundIdMap_.clear();
    assets.forEach([this](uint32_t id, const std::string& filename, float volume) {
        soundIdMap_[id] = filename;
    });

    LOG_INFO(MOD_AUDIO, "Loaded {} sound asset mappings", soundIdMap_.size());
}

void AudioManager::preloadSound(uint32_t soundId) {
    if (!initialized_) {
        return;
    }

    // This will load the sound into the buffer cache
    auto buffer = getSoundById(soundId);
    if (buffer && buffer->isValid()) {
        LOG_DEBUG(MOD_AUDIO, "Preloaded sound ID {}", soundId);
    }
}

void AudioManager::preloadCommonSounds() {
    if (!initialized_) {
        return;
    }

    LOG_INFO(MOD_AUDIO, "Preloading common sound effects...");

    // Common sound IDs from SoundAssets.txt
    // Combat sounds
    static const uint32_t commonSounds[] = {
        118,  // Swing.WAV
        119,  // SwingBig.WAV
        126,  // Kick1.WAV
        127,  // KickHit.WAV
        128,  // RndKick.WAV
        130,  // Punch1.WAV
        131,  // PunchHit.WAV

        // Spell sounds
        103,  // Spell_1.wav
        104,  // Spell_2.wav
        105,  // Spell_3.wav
        106,  // Spell_4.wav
        107,  // Spell_5.wav
        108,  // SpelCast.WAV

        // Player sounds
        139,  // LevelUp.WAV

        // Environment
        100,  // WaterIn.WAV
        101,  // WatTrd_1.WAV
        102,  // WatTrd_2.WAV
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

bool AudioManager::initializeHardwareDevice() {
    // Open default audio device
    device_ = alcOpenDevice(nullptr);
    if (!device_) {
        LOG_DEBUG(MOD_AUDIO, "No hardware audio device available");
        return false;
    }

    // Create context
    context_ = alcCreateContext(device_, nullptr);
    if (!context_) {
        LOG_ERROR(MOD_AUDIO, "Failed to create audio context");
        alcCloseDevice(device_);
        device_ = nullptr;
        return false;
    }

    if (!alcMakeContextCurrent(context_)) {
        LOG_ERROR(MOD_AUDIO, "Failed to make audio context current");
        alcDestroyContext(context_);
        alcCloseDevice(device_);
        context_ = nullptr;
        device_ = nullptr;
        return false;
    }

    loopbackMode_ = false;
    LOG_INFO(MOD_AUDIO, "Hardware audio device initialized");
    return true;
}

bool AudioManager::initializeLoopbackDevice() {
    // Check if OpenAL Soft loopback extension is available
    if (!alcIsExtensionPresent(nullptr, "ALC_SOFT_loopback")) {
        LOG_ERROR(MOD_AUDIO, "OpenAL Soft loopback extension not available");
        return false;
    }

    // Get loopback function pointers
    alcLoopbackOpenDeviceSOFT_ = reinterpret_cast<LPALCLOOPBACKOPENDEVICESOFT>(
        alcGetProcAddress(nullptr, "alcLoopbackOpenDeviceSOFT"));
    alcIsRenderFormatSupportedSOFT_ = reinterpret_cast<LPALCISRENDERFORMATSUPPORTEDSOFT>(
        alcGetProcAddress(nullptr, "alcIsRenderFormatSupportedSOFT"));
    alcRenderSamplesSOFT_ = reinterpret_cast<LPALCRENDERSAMPLESSOFT>(
        alcGetProcAddress(nullptr, "alcRenderSamplesSOFT"));

    if (!alcLoopbackOpenDeviceSOFT_ || !alcIsRenderFormatSupportedSOFT_ || !alcRenderSamplesSOFT_) {
        LOG_ERROR(MOD_AUDIO, "Failed to get loopback function pointers");
        return false;
    }

    // Open loopback device
    device_ = alcLoopbackOpenDeviceSOFT_(nullptr);
    if (!device_) {
        LOG_ERROR(MOD_AUDIO, "Failed to open loopback device");
        return false;
    }

    // Check if our desired format is supported
    ALCint formatType = ALC_SHORT_SOFT;
    ALCint formatChannels = ALC_STEREO_SOFT;
    if (!alcIsRenderFormatSupportedSOFT_(device_, LOOPBACK_SAMPLE_RATE, formatChannels, formatType)) {
        LOG_ERROR(MOD_AUDIO, "Loopback format not supported");
        alcCloseDevice(device_);
        device_ = nullptr;
        return false;
    }

    // Create context with specific format
    ALCint attrs[] = {
        ALC_FORMAT_TYPE_SOFT, formatType,
        ALC_FORMAT_CHANNELS_SOFT, formatChannels,
        ALC_FREQUENCY, static_cast<ALCint>(LOOPBACK_SAMPLE_RATE),
        0
    };

    context_ = alcCreateContext(device_, attrs);
    if (!context_) {
        LOG_ERROR(MOD_AUDIO, "Failed to create loopback context");
        alcCloseDevice(device_);
        device_ = nullptr;
        return false;
    }

    if (!alcMakeContextCurrent(context_)) {
        LOG_ERROR(MOD_AUDIO, "Failed to make loopback context current");
        alcDestroyContext(context_);
        alcCloseDevice(device_);
        context_ = nullptr;
        device_ = nullptr;
        return false;
    }

    loopbackMode_ = true;

    // Start loopback render thread
    loopbackRunning_ = true;
    loopbackThread_ = std::thread([this]() {
        // Buffer for rendered audio (stereo 16-bit)
        std::vector<int16_t> buffer(LOOPBACK_BUFFER_FRAMES * LOOPBACK_CHANNELS);

        LOG_DEBUG(MOD_AUDIO, "Loopback render thread started");

        while (loopbackRunning_.load()) {
            // Render audio to buffer
            {
                std::lock_guard<std::mutex> lock(loopbackMutex_);
                if (device_ && alcRenderSamplesSOFT_) {
                    alcRenderSamplesSOFT_(device_, buffer.data(), LOOPBACK_BUFFER_FRAMES);
                }
            }

            // Send to callback if set
            if (audioOutputCallback_) {
                audioOutputCallback_(buffer.data(), LOOPBACK_BUFFER_FRAMES,
                                     LOOPBACK_SAMPLE_RATE, LOOPBACK_CHANNELS);
            }

            // Sleep to maintain ~44100 Hz sample rate
            // 1024 frames at 44100 Hz = ~23.2ms
            std::this_thread::sleep_for(std::chrono::microseconds(
                (LOOPBACK_BUFFER_FRAMES * 1000000) / LOOPBACK_SAMPLE_RATE));
        }

        LOG_DEBUG(MOD_AUDIO, "Loopback render thread stopped");
    });

    LOG_INFO(MOD_AUDIO, "Loopback audio device initialized ({}Hz, {} channels)",
             LOOPBACK_SAMPLE_RATE, LOOPBACK_CHANNELS);
    return true;
}

bool AudioManager::enableLoopbackMode() {
    if (loopbackMode_) {
        LOG_DEBUG(MOD_AUDIO, "Already in loopback mode");
        // Still ensure music player has software rendering enabled
        // (may not have been set if loopback was enabled at startup before RDP connected)
        if (musicPlayer_) {
            musicPlayer_->enableSoftwareRendering();
        }
        return true;
    }

    if (!initialized_) {
        LOG_ERROR(MOD_AUDIO, "Cannot enable loopback mode - audio not initialized");
        return false;
    }

    LOG_INFO(MOD_AUDIO, "Switching to loopback mode for RDP audio streaming");

    // Stop any playing sounds
    stopAllSounds();

    // Clean up current hardware device
    if (context_) {
        alcMakeContextCurrent(nullptr);
        alcDestroyContext(context_);
        context_ = nullptr;
    }
    if (device_) {
        alcCloseDevice(device_);
        device_ = nullptr;
    }

    // Initialize loopback device
    if (!initializeLoopbackDevice()) {
        LOG_ERROR(MOD_AUDIO, "Failed to switch to loopback mode");
        return false;
    }

    // Reinitialize OpenAL state (listener, distance model)
    alListenerf(AL_GAIN, masterVolume_ * effectsVolume_);
    alDistanceModel(AL_INVERSE_DISTANCE_CLAMPED);
    LOG_DEBUG(MOD_AUDIO, "3D audio distance model reconfigured (inverse distance clamped)");

    // Recreate source pool
    {
        std::lock_guard<std::mutex> lock(sourceMutex_);
        availableSources_.clear();
        activeSources_.clear();
        availableSources_.resize(MAX_SOURCES);
        alGenSources(MAX_SOURCES, availableSources_.data());
        LOG_INFO(MOD_AUDIO, "Audio source pool recreated: {} sources", MAX_SOURCES);
    }

    // Reinitialize music player's OpenAL resources (old ones are invalid after context change)
    // Then enable software rendering so FluidSynth routes through OpenAL
    if (musicPlayer_) {
        musicPlayer_->reinitializeOpenAL();
        musicPlayer_->enableSoftwareRendering();
    }

    LOG_INFO(MOD_AUDIO, "Switched to loopback mode for RDP audio streaming");
    return true;
}

void AudioManager::renderLoopbackAudio() {
    // This is called from update() for manual rendering control if needed
    // The loopback thread handles automatic rendering
}

void AudioManager::update() {
    // Called periodically from the main game loop
    // For now, loopback rendering is handled by the dedicated thread
    // This method can be used for any per-frame audio updates

    // Clean up finished sound sources
    std::lock_guard<std::mutex> lock(sourceMutex_);
    auto it = activeSources_.begin();
    while (it != activeSources_.end()) {
        ALint state;
        alGetSourcei(*it, AL_SOURCE_STATE, &state);
        if (state != AL_PLAYING) {
            alSourcei(*it, AL_BUFFER, 0);
            availableSources_.push_back(*it);
            it = activeSources_.erase(it);
        } else {
            ++it;
        }
    }
}

void AudioManager::scanPfsArchives() {
    pfsFileIndex_.clear();

    // Check if EQ path exists
    if (!std::filesystem::exists(eqPath_) || !std::filesystem::is_directory(eqPath_)) {
        LOG_DEBUG(MOD_AUDIO, "EQ path does not exist or is not a directory: {}", eqPath_);
        return;
    }

    // Find all snd*.pfs files in EQ path
    std::vector<std::string> archivePaths;
    for (const auto& entry : std::filesystem::directory_iterator(eqPath_)) {
        if (!entry.is_regular_file()) continue;

        std::string filename = entry.path().filename().string();
        std::string lowerFilename = filename;
        std::transform(lowerFilename.begin(), lowerFilename.end(),
                       lowerFilename.begin(), ::tolower);

        // Match snd*.pfs pattern
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

    LOG_INFO(MOD_AUDIO, "Scanning {} sound archives...", archivePaths.size());

    // Scan each archive and build index
    size_t totalFiles = 0;
    for (const auto& archivePath : archivePaths) {
        auto archive = std::make_unique<Graphics::PfsArchive>();
        if (!archive->open(archivePath)) {
            LOG_WARN(MOD_AUDIO, "Failed to open archive: {}", archivePath);
            continue;
        }

        // Get list of WAV files in this archive
        std::vector<std::string> wavFiles;
        archive->getFilenames(".wav", wavFiles);

        for (const auto& wavFile : wavFiles) {
            // Store lowercase filename -> archive path mapping
            std::string lowerFile = wavFile;
            std::transform(lowerFile.begin(), lowerFile.end(),
                           lowerFile.begin(), ::tolower);
            pfsFileIndex_[lowerFile] = archivePath;
            totalFiles++;
        }

        // Keep archive open for later use
        pfsArchives_[archivePath] = std::move(archive);
    }

    LOG_INFO(MOD_AUDIO, "Indexed {} sound files from {} archives",
             totalFiles, pfsArchives_.size());
}

bool AudioManager::loadSoundDataFromPfs(const std::string& filename,
                                         std::vector<char>& outData) {
    // Normalize filename to lowercase
    std::string lowerFilename = filename;
    std::transform(lowerFilename.begin(), lowerFilename.end(),
                   lowerFilename.begin(), ::tolower);

    // Look up in index
    auto indexIt = pfsFileIndex_.find(lowerFilename);
    if (indexIt == pfsFileIndex_.end()) {
        return false;
    }

    const std::string& archivePath = indexIt->second;

    // Get or open archive
    auto archiveIt = pfsArchives_.find(archivePath);
    if (archiveIt == pfsArchives_.end()) {
        // Archive not cached, try to open it
        auto archive = std::make_unique<Graphics::PfsArchive>();
        if (!archive->open(archivePath)) {
            LOG_WARN(MOD_AUDIO, "Failed to reopen archive: {}", archivePath);
            return false;
        }
        archiveIt = pfsArchives_.emplace(archivePath, std::move(archive)).first;
    }

    // Extract file data
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

} // namespace Audio
} // namespace EQT

#endif // WITH_AUDIO
