#pragma once

#ifdef WITH_AUDIO

#include <AL/al.h>
#include <AL/alc.h>
#include <AL/alext.h>
#include <glm/glm.hpp>

#include <memory>
#include <string>
#include <unordered_map>
#include <functional>
#include <atomic>
#include <mutex>
#include <thread>
#include <condition_variable>

namespace EQT {
namespace Graphics {
class PfsArchive;  // Forward declaration
}

namespace Audio {

// Forward declarations
class SoundBuffer;
class MusicPlayer;

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
    // forceLoopback: true = use loopback device (no hardware needed)
    //                false = try hardware first, fall back to loopback
    // soundFontPath: path to SoundFont file for MIDI/XMI music playback
    bool initialize(const std::string& eqPath, bool forceLoopback = false,
                    const std::string& soundFontPath = "");
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
    void playMusic(const std::string& filename, bool loop = true);
    void stopMusic(float fadeOutSeconds = 1.0f);
    void pauseMusic();
    void resumeMusic();
    bool isMusicPlaying() const;

    // Zone transitions
    void onZoneChange(const std::string& zoneName);
    void restartZoneMusic();  // Restart current zone's music

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

    // Internal: called by sound sources when finished
    void onSoundFinished(ALuint source);

    // Loopback mode control
    void update();  // Must be called periodically to render loopback audio

    // Find zone music file with zone name mapping (e.g., oasis -> nro)
    std::string findZoneMusic(const std::string& zoneName);

private:
    // Loopback device initialization
    bool initializeLoopbackDevice();
    bool initializeHardwareDevice();
    void renderLoopbackAudio();
    // Sound loading
    std::shared_ptr<SoundBuffer> loadSound(const std::string& filename);
    std::shared_ptr<SoundBuffer> loadSoundFromPfs(const std::string& filename);
    std::shared_ptr<SoundBuffer> getSoundById(uint32_t soundId);

    // PFS archive management
    void scanPfsArchives();
    bool loadSoundDataFromPfs(const std::string& filename, std::vector<char>& outData);

    // Source management
    ALuint acquireSource();
    void releaseSource(ALuint source);

    // Sound asset mapping
    void loadSoundAssets();

private:
    bool initialized_ = false;
    bool audioEnabled_ = true;
    std::string eqPath_;
    std::string soundFontPath_;

    // OpenAL context
    ALCdevice* device_ = nullptr;
    ALCcontext* context_ = nullptr;

    // Volume levels
    float masterVolume_ = 1.0f;
    float musicVolume_ = 0.7f;
    float effectsVolume_ = 1.0f;

    // Sound buffer cache
    mutable std::mutex bufferMutex_;
    std::unordered_map<std::string, std::shared_ptr<SoundBuffer>> bufferCache_;

    // Sound ID to filename mapping (from SoundAssets.txt)
    std::unordered_map<uint32_t, std::string> soundIdMap_;

    // PFS archive index: lowercase filename -> archive path
    std::unordered_map<std::string, std::string> pfsFileIndex_;
    // Cached open PFS archives
    std::unordered_map<std::string, std::unique_ptr<Graphics::PfsArchive>> pfsArchives_;

    // Source pool for sound effects
    static constexpr size_t MAX_SOURCES = 32;
    std::mutex sourceMutex_;
    std::vector<ALuint> availableSources_;
    std::vector<ALuint> activeSources_;

    // Music player
    std::unique_ptr<MusicPlayer> musicPlayer_;

    // Current zone (for music)
    std::string currentZone_;

    // RDP audio streaming
    AudioOutputCallback audioOutputCallback_;

    // Loopback mode for headless/RDP operation
    bool loopbackMode_ = false;
    std::atomic<bool> loopbackRunning_{false};
    std::thread loopbackThread_;
    std::mutex loopbackMutex_;

    // Loopback audio format
    static constexpr uint32_t LOOPBACK_SAMPLE_RATE = 44100;
    static constexpr uint8_t LOOPBACK_CHANNELS = 2;
    static constexpr size_t LOOPBACK_BUFFER_FRAMES = 1024;  // Frames per render call

    // OpenAL Soft loopback function pointers
    LPALCLOOPBACKOPENDEVICESOFT alcLoopbackOpenDeviceSOFT_ = nullptr;
    LPALCISRENDERFORMATSUPPORTEDSOFT alcIsRenderFormatSupportedSOFT_ = nullptr;
    LPALCRENDERSAMPLESSOFT alcRenderSamplesSOFT_ = nullptr;
};

} // namespace Audio
} // namespace EQT

#endif // WITH_AUDIO
