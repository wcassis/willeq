#pragma once

#ifdef WITH_AUDIO

#include <AL/al.h>
#include <string>
#include <vector>
#include <thread>
#include <atomic>
#include <mutex>
#include <condition_variable>
#include <functional>

#ifdef WITH_FLUIDSYNTH
#include <fluidsynth.h>
#endif

namespace EQT {
namespace Audio {

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
    // soundFontPath: optional user-specified soundfont (loaded last, highest priority)
    bool initialize(const std::string& eqPath = "", const std::string& soundFontPath = "");
    void shutdown();

    // Playback control
    bool play(const std::string& filepath, bool loop = true);
    void stop(float fadeSeconds = 0.0f);
    void pause();
    void resume();

    // State queries
    bool isPlaying() const { return playing_; }
    bool isPaused() const { return paused_; }
    float getPosition() const;  // Current playback position in seconds
    const std::string& getCurrentFile() const { return currentFile_; }

    // Volume (0.0 - 1.0)
    void setVolume(float volume);
    float getVolume() const { return volume_; }

    // Output callback for RDP streaming
    void setOutputCallback(MusicOutputCallback callback);

    // Enable software rendering mode (for loopback/RDP)
    // When enabled, FluidSynth renders through OpenAL instead of its own audio driver
    void enableSoftwareRendering();
    bool isSoftwareRendering() const { return softwareRendering_; }

    // Reinitialize OpenAL resources (call after context change)
    void reinitializeOpenAL();

private:
    // Streaming thread
    void streamingThread();
    void stopThread();

    // File format handling
    bool loadMP3(const std::string& filepath);
    bool loadXMI(const std::string& filepath);
    bool loadWAV(const std::string& filepath);

    // Buffer management
    void fillBuffer(ALuint buffer);
    bool streamMoreData();

private:
    bool initialized_ = false;
    std::atomic<bool> playing_{false};
    std::atomic<bool> paused_{false};
    std::atomic<bool> looping_{false};
    std::atomic<bool> stopRequested_{false};
    std::atomic<float> volume_{1.0f};
    std::atomic<float> fadeVolume_{1.0f};
    std::atomic<float> fadeTarget_{1.0f};
    std::atomic<float> fadeRate_{0.0f};

    // OpenAL streaming
    static constexpr size_t NUM_BUFFERS = 4;
    static constexpr size_t BUFFER_SIZE = 16384;  // samples per buffer
    ALuint source_ = 0;
    ALuint buffers_[NUM_BUFFERS] = {0};

    // Audio format info
    uint32_t sampleRate_ = 44100;
    uint8_t channels_ = 2;
    ALenum format_ = AL_FORMAT_STEREO16;

    // Decoded audio data (for simple formats)
    std::vector<int16_t> decodedData_;
    size_t playbackPosition_ = 0;

    // Streaming thread
    std::thread streamThread_;
    std::mutex streamMutex_;
    std::condition_variable streamCond_;

    // Current file info
    std::string currentFile_;

#ifdef WITH_FLUIDSYNTH
    // FluidSynth for MIDI/XMI playback
    fluid_settings_t* fluidSettings_ = nullptr;
    fluid_synth_t* fluidSynth_ = nullptr;
    fluid_audio_driver_t* fluidAudioDriver_ = nullptr;
    fluid_player_t* fluidPlayer_ = nullptr;
    int soundFontId_ = -1;

    // Manual MIDI sequencer for software rendering mode
    // (fluid_player doesn't work without audio driver callbacks)
    struct MidiEvent {
        uint64_t tick;      // Time in MIDI ticks
        uint8_t type;       // Event type (0x80-0xF0)
        uint8_t channel;    // MIDI channel (0-15)
        uint8_t data1;      // First data byte
        uint8_t data2;      // Second data byte (if applicable)
    };
    std::vector<MidiEvent> midiEvents_;
    size_t midiEventIndex_ = 0;
    uint64_t midiTickPosition_ = 0;
    uint32_t midiTicksPerBeat_ = 480;  // Default PPQN
    uint32_t midiTempo_ = 500000;      // Default tempo (microseconds per beat) = 120 BPM
    uint64_t samplePosition_ = 0;      // Current sample position for timing

    // Parse MIDI data for software sequencing
    bool parseMidiEvents(const std::vector<uint8_t>& midiData);
    void processMidiEvents(size_t samplesToRender);
#endif

    // RDP output callback
    MusicOutputCallback outputCallback_;

    // Software rendering mode (for loopback/RDP)
    bool softwareRendering_ = false;
    std::atomic<bool> fluidSynthStreaming_{false};  // true when streaming FluidSynth output
};

} // namespace Audio
} // namespace EQT

#endif // WITH_AUDIO
