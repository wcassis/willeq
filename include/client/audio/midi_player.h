#pragma once

#ifdef WITH_AUDIO

#include <cstdint>
#include <string>
#include <vector>
#include <memory>
#include <atomic>

namespace EQT {
namespace Audio {

enum class SoundFontType { GM, Custom };

// TSF + TML MIDI synthesis.
// This is a NEW class — not the existing MusicPlayer.
class MidiPlayer {
public:
    MidiPlayer();
    ~MidiPlayer();

    // Not copyable
    MidiPlayer(const MidiPlayer&) = delete;
    MidiPlayer& operator=(const MidiPlayer&) = delete;

    // Initialize with a single SoundFont file
    bool init(const std::string& soundFontPath, uint32_t sampleRate = 22050);

    // Initialize with dual SoundFonts (GM + custom orchestral)
    bool init(const std::string& gmPath, const std::string& customPath, uint32_t sampleRate);

    // Replace the GM SoundFont slot
    bool loadSoundFont(const std::string& path);

    // Switch between loaded SoundFonts
    void selectSoundFont(SoundFontType type);
    SoundFontType getActiveSoundFont() const { return activeSoundFont_; }

    // Play MIDI data from memory
    // trackIndex: ignored by TML (tml plays all tracks). Caller should provide
    // single-track MIDI data from XmiDecoder if a specific track is wanted.
    bool play(const uint8_t* midiData, size_t size, bool loop = true);
    bool play(const std::vector<uint8_t>& midiData, bool loop = true);

    // Render audio into buffer (called from mixer, audio thread)
    // buffer: interleaved stereo float, frame_count * 2 floats
    void render(float* buffer, int frame_count);

    // Controls
    void stop();
    void setVolume(float vol);
    float getVolume() const { return volume_.load(); }
    bool isPlaying() const { return playing_.load(); }

    bool isInitialized() const { return initialized_; }

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;

    bool initialized_ = false;
    std::atomic<bool> playing_{false};
    std::atomic<float> volume_{1.0f};
    bool looping_ = false;
    uint32_t sampleRate_ = 22050;
    SoundFontType activeSoundFont_ = SoundFontType::GM;
};

} // namespace Audio
} // namespace EQT

#endif // WITH_AUDIO
