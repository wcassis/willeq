#pragma once

#ifdef WITH_AUDIO

#include <glm/glm.hpp>

#include "client/audio/sound_buffer_cache.h"

#include <memory>
#include <string>
#include <unordered_map>
#include <functional>
#include <atomic>
#include <mutex>
#include <thread>
#include <condition_variable>
#include <list>

namespace EQT {
namespace Graphics {
class PfsArchive;  // Forward declaration
}

namespace Audio {

// T334 (board B045): the audio listener's forward vector, derived from the
// player's heading. EQ world axes are +X east, +Y north, +Z up, and a heading
// of 0 faces north, so forward is (sin h, cos h, 0) -- the same convention as
// PlayerCameraComponent::computeMovementVector() and
// BridgeComponent::HandlePlayerPositionUpdate()'s backward-motion test.
//
// headingDeg is DEGREES (what EverQuest's PlayerInfo facade stores and what
// HeadingDeg() returns), NOT the 0-512 wire units. Before T334 the one caller
// converted degrees with the 0-512 divisor AND negated cos, so the listener
// faced a mirrored direction turning 1.42x too fast; the stereo pan of every
// positioned sound was wrong at every heading but due north.
glm::vec3 eqHeadingToListenerForward(float headingDeg);

// Forward declarations
class SoundBuffer;
class MusicPlayer;
class AudioMixer;
class AudioBackend;
class MidiPlayer;
class SfxManager;
enum class SoundFontType;

// Music event configuration (loaded from config/music_events.json)
struct MusicEventConfig {
    std::string file;
    int track = 0;       // 1-based track number (converted to 0-based internally)
    bool loop = true;
    bool enabled = true;
};

// Callback for RDP audio streaming
using AudioOutputCallback = std::function<void(const int16_t* samples, size_t count,
                                                uint32_t sampleRate, uint8_t channels)>;

class AudioManager {
public:
    AudioManager();
    ~AudioManager();

    // Prevent copying
    AudioManager(const AudioManager&) = delete;
    AudioManager& operator=(const AudioManager&) = delete;

    // Initialization
    // forceLoopback: true = use RDP backend (no hardware needed)
    //                false = use miniaudio hardware backend
    // soundFontPath: path to SoundFont file for MIDI/XMI music playback
    // tickCallback: optional function called between heavy init stages,
    // allowing the caller to pump the network event loop on slow hardware
    bool initialize(const std::string& eqPath, bool forceLoopback = false,
                    const std::string& soundFontPath = "",
                    std::function<void()> tickCallback = nullptr);
    void shutdown();
    bool isInitialized() const { return initialized_; }
    bool isLoopbackMode() const { return loopbackMode_; }

    // Sound effects
    void playSound(uint32_t soundId);
    void playSound(uint32_t soundId, const glm::vec3& position);
    void playSoundByName(const std::string& filename);
    void playSoundByName(const std::string& filename, const glm::vec3& position);
    void stopAllSounds();

    // Preload sounds for faster playback
    void preloadSound(uint32_t soundId);
    void preloadCommonSounds();
    size_t getLoadedSoundCount() const;

    // Get sound buffer for custom playback (e.g., looping ambient sounds)
    // Returns nullptr if sound not found
    std::shared_ptr<SoundBuffer> getSoundBuffer(uint32_t soundId);
    std::shared_ptr<SoundBuffer> getSoundBufferByName(const std::string& filename);

    // Music
    // trackIndex: for XMI files, selects which sequence to play (0 = first, 1 = second, etc.)
    //             Use -1 to play all sequences combined. Ignored for non-XMI files.
    void playMusic(const std::string& filename, bool loop = true, int trackIndex = 0, double startTimeMs = 0.0);
    void stopMusic(float fadeOutSeconds = 1.0f);
    void pauseMusic();
    void resumeMusic();
    bool isMusicPlaying() const;

    // Zone transitions
    void onZoneChange(const std::string& zoneName);
    void restartZoneMusic();  // Restart current zone's music

    // Context-based music (with priority: vendor/bank > auto-attack > zone)
    // These methods handle saving/restoring the previous music state
    void startAutoAttackMusic();   // Plays gl.xmi track 3, loops
    void stopAutoAttackMusic();    // Restores previous music
    void startVendorBankMusic();   // Plays gl.xmi track 22, loops
    void stopVendorBankMusic();    // Restores previous music

    // Volume controls (0.0 - 1.0)
    void setMasterVolume(float volume);
    void setMusicVolume(float volume);
    void setEffectsVolume(float volume);
    float getMasterVolume() const { return masterVolume_; }
    float getMusicVolume() const { return musicVolume_; }
    float getEffectsVolume() const { return effectsVolume_; }

    // Listener position for 3D audio
    void setListenerPosition(const glm::vec3& position,
                             const glm::vec3& forward,
                             const glm::vec3& up);

    // Audio enable/disable
    void setAudioEnabled(bool enabled);
    bool isAudioEnabled() const { return audioEnabled_; }

    // K15 (board B037): 3D positional audio on/off. When off, a positioned
    // sound plays centred at the effects volume instead of going through
    // SfxManager::playSpatial() -- see playSoundInternal(). Set once from
    // config by BridgeComponent::InitializeAudio().
    void setUse3DAudio(bool enabled) { use3DAudio_ = enabled; }

    // RDP audio streaming callback
    void setAudioOutputCallback(AudioOutputCallback callback);

    // Switch to loopback mode for RDP audio streaming
    // This will reinitialize the audio device in loopback mode
    // Returns true if already in loopback mode or switch was successful
    bool enableLoopbackMode();

    // Memory constraints for constrained rendering mode
    void setMemoryConstraints(size_t soundCacheBytes, bool lazyPfs);

    // Loopback mode control
    void update();  // Must be called periodically

    // Find zone music file with zone name mapping (e.g., oasis -> nro)
    std::string findZoneMusic(const std::string& zoneName);

    // Determine which SoundFont to use for a given music file. Public because
    // it is the on-demand-load gate for the custom font (B977's
    // au.soundfont_custom owner scope wraps exactly its call in playMusic)
    // and tests/test_soundfont_release.cpp reaches it to pin the
    // au.soundfont_custom residual's identity (the synthus2 name set).
    // Pure function: reads only its argument plus the static name set.
    SoundFontType getSoundFontTypeForFile(const std::string& path);

    // B1015 (#336): deactivate every SFX channel and wait for the audio
    // thread to quiesce (AudioMixer::stopAll). After this, no channel reads
    // any SoundBuffer, so the one decoded-PCM cache's in-use pin names
    // nothing and the cap can evict freely. Used by the cache-eviction tests,
    // which need "every sound stopped" before they can observe the cap.
    void stopAll();

    // Memory usage queries
    size_t getSoundBufferCacheBytes() const { return bufferCache_.getSizeBytes(); }
    size_t getSoundBufferCacheMaxBytes() const { return bufferCache_.getMaxBytes(); }
    size_t getSoundFontMemoryEstimate() const;
    size_t getMusicDecodedBytes() const;
    size_t getPfsArchiveCacheBytes() const;

    // Get mixer and sfx manager for other audio components
    AudioMixer* getMixer() { return mixer_.get(); }
    SfxManager* getSfxManager() { return sfxManager_.get(); }

private:
    // Sound loading
    std::shared_ptr<SoundBuffer> loadSound(const std::string& filename);
    // B1015 (#336): the decode half of the old loadSound(), bound as the
    // SoundBufferCache's Source. Runs WITHOUT the cache's lock; never calls
    // back into the cache. Charged to `au.sound_buffers` (the owner that
    // decoded the PCM -- it is reported once, CLAUDE.md's memory accounting).
    std::shared_ptr<SoundBuffer> decodeSound(const std::string& filename);

    std::shared_ptr<SoundBuffer> loadSoundFromPfs(const std::string& filename);
    std::shared_ptr<SoundBuffer> getSoundById(uint32_t soundId);

    // Play sound effect using SfxManager
    void playSoundInternal(const std::string& filename, const glm::vec3& position);

    // PFS archive management
    void scanPfsArchives();
    bool loadPfsIndexCache();
    void savePfsIndexCache();
    bool loadSoundDataFromPfs(const std::string& filename, std::vector<char>& outData);

    // Sound asset mapping
    void loadSoundAssets();

    // Music event config
    void loadMusicEventConfig();

private:
    bool initialized_ = false;
    bool audioEnabled_ = true;
    bool use3DAudio_ = true;  // K15 (B037)
    std::string eqPath_;
    std::function<void()> tickCallback_;
    std::string soundFontPath_;

    // New audio pipeline
    std::unique_ptr<AudioMixer> mixer_;
    std::unique_ptr<AudioBackend> backend_;
    std::unique_ptr<MidiPlayer> midiPlayer_;
    std::unique_ptr<SfxManager> sfxManager_;
    std::unique_ptr<MusicPlayer> musicPlayer_;

    // Volume levels
    float masterVolume_ = 1.0f;
    float musicVolume_ = 0.7f;
    float effectsVolume_ = 1.0f;

    // Sound buffer cache
    // B1015 (#336): the ONE decoded-PCM cache of SFX PCM. Before this it was
    // the raw map/LRU members below, and `SfxManager::cache_` was a SECOND
    // LRU with its own cap on top of it (two victims, two caps -- the audit
    // in refactor/B1015). The cache now owns the LRU and its one cap, and
    // pins the entries a live mixer channel reads (the pin walks the
    // SfxManager's channels through AudioMixer). The members it replaced:
    // bufferMutex_, bufferCache_, soundBufferCacheMaxBytes_,
    // soundBufferCacheSizeBytes_, bufferLruOrder_.
    SoundBufferCache bufferCache_;

    // Sound ID to filename mapping (from SoundAssets.txt or SQLite)
    std::unordered_map<uint32_t, std::string> soundIdMap_;

    // PFS archive index: lowercase filename -> archive path
    std::unordered_map<std::string, std::string> pfsFileIndex_;
    // Cached open PFS archives
    std::unordered_map<std::string, std::unique_ptr<Graphics::PfsArchive>> pfsArchives_;
    bool lazyPfsLoading_ = false;

    // Current zone (for music)
    std::string currentZone_;

    // Context-based music state
    bool autoAttackMusicActive_ = false;
    bool vendorBankMusicActive_ = false;
    std::string savedZoneMusicFile_;
    int savedZoneMusicTrackIndex_ = 0;
    double savedZoneMusicTimeMs_ = 0.0;

    // Music event configurations (loaded from config/music_events.json)
    MusicEventConfig autoAttackMusicConfig_;
    MusicEventConfig vendorBankMusicConfig_;

    // RDP audio streaming
    AudioOutputCallback audioOutputCallback_;
    bool loopbackMode_ = false;

    // Loopback audio format
    static constexpr uint32_t LOOPBACK_SAMPLE_RATE = 22050;
    static constexpr uint8_t LOOPBACK_CHANNELS = 2;
};

} // namespace Audio
} // namespace EQT

#endif // WITH_AUDIO
