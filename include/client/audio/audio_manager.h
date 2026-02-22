#pragma once

#ifdef WITH_AUDIO

#include <glm/glm.hpp>

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
    bool isAutoAttackMusicPlaying() const { return autoAttackMusicActive_; }
    bool isVendorBankMusicPlaying() const { return vendorBankMusicActive_; }

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

    // Get mixer and sfx manager for other audio components
    AudioMixer* getMixer() { return mixer_.get(); }
    SfxManager* getSfxManager() { return sfxManager_.get(); }

private:
    // Sound loading
    std::shared_ptr<SoundBuffer> loadSound(const std::string& filename);
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

    // Determine which SoundFont to use for a given music file
    SoundFontType getSoundFontTypeForFile(const std::string& path);

private:
    bool initialized_ = false;
    bool audioEnabled_ = true;
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
    mutable std::mutex bufferMutex_;
    std::unordered_map<std::string, std::shared_ptr<SoundBuffer>> bufferCache_;

    // Sound buffer LRU cache eviction
    size_t soundBufferCacheMaxBytes_ = 0;    // 0 = unlimited
    size_t soundBufferCacheSizeBytes_ = 0;
    std::list<std::string> bufferLruOrder_;

    // Sound ID to filename mapping (from SoundAssets.txt)
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
