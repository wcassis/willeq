#pragma once

#ifdef WITH_AUDIO

#include <string>
#include <vector>
#include <atomic>
#include <mutex>
#include <functional>
#include <memory>

namespace EQT {
namespace Audio {

// Forward declarations
class MidiPlayer;
class AudioMixer;

// Callback for streaming audio output (for RDP)
using MusicOutputCallback = std::function<void(const int16_t* samples, size_t count)>;

class MusicPlayer {
public:
    MusicPlayer();
    ~MusicPlayer();

    // Prevent copying
    MusicPlayer(const MusicPlayer&) = delete;
    MusicPlayer& operator=(const MusicPlayer&) = delete;

    // Initialization
    // eqPath: path to EQ client directory (for auto-loading EQ soundfonts)
    // soundFontPath: optional user-specified soundfont
    bool initialize(const std::string& eqPath = "", const std::string& soundFontPath = "");
    void shutdown();

    // Connect to external MidiPlayer and AudioMixer (owned by AudioManager)
    void setMidiPlayer(MidiPlayer* player);
    void setMixer(AudioMixer* mixer);

    // Playback control
    // trackIndex: for XMI files, selects which sequence to play (0 = first, 1 = second, etc.)
    //             Use -1 to play all sequences combined. Ignored for non-XMI files.
    bool play(const std::string& filepath, bool loop = true, int trackIndex = 0, double startTimeMs = 0.0);
    void stop(float fadeSeconds = 0.0f);
    void pause();
    void resume();

    // State queries
    bool isPlaying() const { return playing_; }
    bool isPaused() const { return paused_; }
    float getPosition() const;  // Current playback position in seconds
    const std::string& getCurrentFile() const { return currentFile_; }
    int getCurrentTrackIndex() const { return currentTrackIndex_; }

    // Volume (0.0 - 1.0)
    void setVolume(float volume);
    float getVolume() const { return volume_; }

    // Output callback for RDP streaming (legacy — handled by AudioBackend now)
    void setOutputCallback(MusicOutputCallback callback);

    // Legacy methods (no-ops — always software rendering now)
    void enableSoftwareRendering();
    bool isSoftwareRendering() const { return true; }
    void reinitializeOpenAL();

    // Render callback for mixer (called from audio thread)
    static void mixerRenderCallback(void* userData, float* buffer, int frameCount);

private:
    // File format handling
    bool loadMP3(const std::string& filepath);
    bool loadXMI(const std::string& filepath, int trackIndex = 0, double startTimeMs = 0.0);
    bool loadWAV(const std::string& filepath);

    // Start playback on mixer channel
    void startMixerPlayback();

private:
    bool initialized_ = false;
    std::atomic<bool> playing_{false};
    std::atomic<bool> paused_{false};
    std::atomic<bool> looping_{false};
    std::atomic<float> volume_{1.0f};
    std::atomic<float> fadeVolume_{1.0f};
    std::atomic<float> fadeTarget_{1.0f};
    std::atomic<float> fadeRate_{0.0f};

    // External components (not owned)
    MidiPlayer* midiPlayer_ = nullptr;
    AudioMixer* mixer_ = nullptr;

    // Internal MidiPlayer (owned, created if no external one provided)
    std::unique_ptr<MidiPlayer> ownedMidiPlayer_;

    // Mixer channel handle
    int musicChannelHandle_ = -1;

    // Audio format info
    uint32_t sampleRate_ = 22050;
    uint8_t channels_ = 2;

    // Decoded audio data (for MP3/WAV — PCM float interleaved)
    std::vector<float> decodedData_;
    std::atomic<size_t> playbackPosition_{0};

    // Current file info
    std::string currentFile_;
    int currentTrackIndex_ = 0;

    // Is this playing MIDI (via MidiPlayer)?
    bool playingMidi_ = false;

    // EQ path for file loading
    std::string eqPath_;

    // RDP output callback (legacy)
    MusicOutputCallback outputCallback_;
};

} // namespace Audio
} // namespace EQT

#endif // WITH_AUDIO
