#ifdef WITH_AUDIO

#include "client/audio/music_player.h"
#include "common/logging.h"

#ifdef WITH_FLUIDSYNTH
#include "client/audio/xmi_decoder.h"
#endif

#include <sndfile.h>
#include <filesystem>
#include <algorithm>
#include <cstring>
#include <iostream>
#include <thread>
#include <chrono>

// Simple MP3 decoder (minimp3)
#define MINIMP3_IMPLEMENTATION
#define MINIMP3_ONLY_MP3
#include "minimp3.h"
#include "minimp3_ex.h"

namespace EQT {
namespace Audio {

MusicPlayer::MusicPlayer() = default;

MusicPlayer::~MusicPlayer() {
    shutdown();
}

bool MusicPlayer::initialize(const std::string& soundFontPath) {
    if (initialized_) {
        return true;
    }

    // Create OpenAL source for streaming
    alGenSources(1, &source_);
    if (alGetError() != AL_NO_ERROR) {
        LOG_ERROR(MOD_AUDIO, "Failed to create music source");
        return false;
    }

    // Create streaming buffers
    alGenBuffers(NUM_BUFFERS, buffers_);
    if (alGetError() != AL_NO_ERROR) {
        LOG_ERROR(MOD_AUDIO, "Failed to create music buffers");
        alDeleteSources(1, &source_);
        source_ = 0;
        return false;
    }

    // Configure source for streaming
    alSourcef(source_, AL_PITCH, 1.0f);
    alSourcef(source_, AL_GAIN, 1.0f);
    alSource3f(source_, AL_POSITION, 0.0f, 0.0f, 0.0f);
    alSource3f(source_, AL_VELOCITY, 0.0f, 0.0f, 0.0f);
    alSourcei(source_, AL_LOOPING, AL_FALSE);  // We handle looping manually
    alSourcei(source_, AL_SOURCE_RELATIVE, AL_TRUE);  // Music is non-positional

#ifdef WITH_FLUIDSYNTH
    // Initialize FluidSynth for MIDI/XMI playback - use PulseAudio for background music
    std::cerr << "[AUDIO] FluidSynth: initializing settings..." << std::endl;
    fluidSettings_ = new_fluid_settings();
    if (fluidSettings_) {
        // Use PulseAudio for real-time background music playback
        fluid_settings_setstr(fluidSettings_, "audio.driver", "pulseaudio");
        fluid_settings_setnum(fluidSettings_, "synth.sample-rate", 44100.0);
        fluid_settings_setint(fluidSettings_, "synth.reverb.active", 1);
        fluid_settings_setint(fluidSettings_, "synth.chorus.active", 1);
        fluid_settings_setint(fluidSettings_, "synth.polyphony", 256);

        std::cerr << "[AUDIO] FluidSynth: creating synth..." << std::endl;
        fluidSynth_ = new_fluid_synth(fluidSettings_);
        if (fluidSynth_) {
            std::cerr << "[AUDIO] FluidSynth: synth created (ptr=" << fluidSynth_ << ")" << std::endl;
            // Load SoundFont if provided
            if (!soundFontPath.empty() && std::filesystem::exists(soundFontPath)) {
                std::cerr << "[AUDIO] FluidSynth: loading soundfont: " << soundFontPath << std::endl;
                soundFontId_ = fluid_synth_sfload(fluidSynth_, soundFontPath.c_str(), 1);
                if (soundFontId_ >= 0) {
                    std::cerr << "[AUDIO] FluidSynth: soundfont loaded (id=" << soundFontId_ << ")" << std::endl;
                    LOG_INFO(MOD_AUDIO, "Loaded SoundFont: {}", soundFontPath);

                    // Create audio driver to connect synth to PulseAudio
                    std::cerr << "[AUDIO] FluidSynth: creating audio driver..." << std::endl;
                    fluidAudioDriver_ = new_fluid_audio_driver(fluidSettings_, fluidSynth_);
                    if (fluidAudioDriver_) {
                        std::cerr << "[AUDIO] FluidSynth: audio driver created" << std::endl;
                    } else {
                        std::cerr << "[AUDIO] FluidSynth: audio driver FAILED - trying ALSA" << std::endl;
                        // Try ALSA as fallback
                        fluid_settings_setstr(fluidSettings_, "audio.driver", "alsa");
                        fluidAudioDriver_ = new_fluid_audio_driver(fluidSettings_, fluidSynth_);
                        if (fluidAudioDriver_) {
                            std::cerr << "[AUDIO] FluidSynth: ALSA audio driver created" << std::endl;
                        } else {
                            std::cerr << "[AUDIO] FluidSynth: ALSA audio driver also FAILED" << std::endl;
                        }
                    }
                } else {
                    std::cerr << "[AUDIO] FluidSynth: soundfont FAILED to load" << std::endl;
                    LOG_WARN(MOD_AUDIO, "Failed to load SoundFont: {}", soundFontPath);
                }
            } else {
                std::cerr << "[AUDIO] FluidSynth: no soundfont path or file not found" << std::endl;
            }
        } else {
            std::cerr << "[AUDIO] FluidSynth: synth creation FAILED" << std::endl;
        }
    } else {
        std::cerr << "[AUDIO] FluidSynth: settings creation FAILED" << std::endl;
    }
#endif

    initialized_ = true;
    LOG_INFO(MOD_AUDIO, "Music player initialized");
    return true;
}

void MusicPlayer::shutdown() {
    if (!initialized_) {
        return;
    }

    stop();
    stopThread();

#ifdef WITH_FLUIDSYNTH
    if (fluidPlayer_) {
        delete_fluid_player(fluidPlayer_);
        fluidPlayer_ = nullptr;
    }
    if (fluidAudioDriver_) {
        delete_fluid_audio_driver(fluidAudioDriver_);
        fluidAudioDriver_ = nullptr;
    }
    if (fluidSynth_) {
        if (soundFontId_ >= 0) {
            fluid_synth_sfunload(fluidSynth_, soundFontId_, 1);
        }
        delete_fluid_synth(fluidSynth_);
        fluidSynth_ = nullptr;
    }
    if (fluidSettings_) {
        delete_fluid_settings(fluidSettings_);
        fluidSettings_ = nullptr;
    }
#endif

    if (source_) {
        alSourceStop(source_);
        alSourcei(source_, AL_BUFFER, 0);
        alDeleteSources(1, &source_);
        source_ = 0;
    }

    for (ALuint& buf : buffers_) {
        if (buf) {
            alDeleteBuffers(1, &buf);
            buf = 0;
        }
    }

    initialized_ = false;
}

bool MusicPlayer::play(const std::string& filepath, bool loop) {
    if (!initialized_) {
        return false;
    }

    // Stop current playback
    stop();

    currentFile_ = filepath;
    looping_ = loop;

    // Determine file type and load
    std::string ext = std::filesystem::path(filepath).extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

    bool loaded = false;
    std::cerr << "[AUDIO] MusicPlayer::play: loading " << ext << " file: " << filepath << std::endl;
    std::cerr.flush();
    if (ext == ".mp3") {
        loaded = loadMP3(filepath);
    } else if (ext == ".xmi") {
        std::cerr << "[AUDIO] MusicPlayer::play: calling loadXMI..." << std::endl;
        std::cerr.flush();
        loaded = loadXMI(filepath);
        std::cerr << "[AUDIO] MusicPlayer::play: loadXMI returned " << (loaded ? "true" : "false") << std::endl;
        std::cerr.flush();
    } else if (ext == ".wav") {
        loaded = loadWAV(filepath);
    } else {
        LOG_WARN(MOD_AUDIO, "Unsupported music format: {}", ext);
        return false;
    }

    if (!loaded) {
        return false;
    }

    // For XMI files, FluidSynth handles playback directly via PulseAudio
    // No need for OpenAL streaming
    if (ext == ".xmi") {
        LOG_INFO(MOD_AUDIO, "Playing MIDI music: {}", filepath);
        return true;
    }

    // For MP3/WAV, use OpenAL streaming
    playing_ = true;
    paused_ = false;
    stopRequested_ = false;
    playbackPosition_ = 0;

    // Fill initial buffers
    for (size_t i = 0; i < NUM_BUFFERS; ++i) {
        fillBuffer(buffers_[i]);
    }

    // Queue buffers and start playback
    alSourceQueueBuffers(source_, NUM_BUFFERS, buffers_);
    alSourcef(source_, AL_GAIN, volume_ * fadeVolume_);
    alSourcePlay(source_);

    // Start streaming thread
    streamThread_ = std::thread(&MusicPlayer::streamingThread, this);

    LOG_INFO(MOD_AUDIO, "Playing music: {}", filepath);
    return true;
}

void MusicPlayer::stop(float fadeSeconds) {
    if (!playing_) {
        return;
    }

    if (fadeSeconds > 0.0f) {
        fadeTarget_ = 0.0f;
        fadeRate_ = 1.0f / fadeSeconds;
    } else {
        stopRequested_ = true;
        stopThread();

        // Stop FluidSynth player if active
#ifdef WITH_FLUIDSYNTH
        if (fluidPlayer_) {
            fluid_player_stop(fluidPlayer_);
        }
#endif

        alSourceStop(source_);

        // Unqueue all buffers
        ALint queued;
        alGetSourcei(source_, AL_BUFFERS_QUEUED, &queued);
        while (queued-- > 0) {
            ALuint buffer;
            alSourceUnqueueBuffers(source_, 1, &buffer);
        }

        playing_ = false;
        decodedData_.clear();
        currentFile_.clear();
    }
}

void MusicPlayer::pause() {
    if (playing_ && !paused_) {
        alSourcePause(source_);
        paused_ = true;
    }
}

void MusicPlayer::resume() {
    if (playing_ && paused_) {
        alSourcePlay(source_);
        paused_ = false;
    }
}

float MusicPlayer::getPosition() const {
    if (!playing_ || sampleRate_ == 0) {
        return 0.0f;
    }

    ALfloat offset;
    alGetSourcef(source_, AL_SEC_OFFSET, &offset);
    return offset + (static_cast<float>(playbackPosition_) / channels_ / sampleRate_);
}

void MusicPlayer::setVolume(float volume) {
    volume_ = std::clamp(volume, 0.0f, 1.0f);

    if (source_) {
        alSourcef(source_, AL_GAIN, volume_ * fadeVolume_);
    }
}

void MusicPlayer::setOutputCallback(MusicOutputCallback callback) {
    outputCallback_ = std::move(callback);
}

void MusicPlayer::streamingThread() {
    while (playing_ && !stopRequested_) {
        // Handle fading
        float currentFadeRate = fadeRate_.load();
        if (currentFadeRate > 0.0f) {
            float currentFade = fadeVolume_.load();
            float targetFade = fadeTarget_.load();
            currentFade -= currentFadeRate * 0.033f;  // ~30fps

            if (currentFade <= targetFade) {
                currentFade = targetFade;
                fadeRate_.store(0.0f);

                if (targetFade <= 0.0f) {
                    stopRequested_ = true;
                    break;
                }
            }
            fadeVolume_.store(currentFade);
            alSourcef(source_, AL_GAIN, volume_.load() * currentFade);
        }

        // Check for processed buffers
        ALint processed;
        alGetSourcei(source_, AL_BUFFERS_PROCESSED, &processed);

        while (processed-- > 0 && !stopRequested_) {
            ALuint buffer;
            alSourceUnqueueBuffers(source_, 1, &buffer);

            // Refill and requeue
            if (streamMoreData()) {
                fillBuffer(buffer);
                alSourceQueueBuffers(source_, 1, &buffer);
            } else if (looping_) {
                // Loop: restart from beginning
                playbackPosition_ = 0;
                fillBuffer(buffer);
                alSourceQueueBuffers(source_, 1, &buffer);
            }
            // If not looping and no more data, the buffer isn't requeued
        }

        // Check if playback stopped
        ALint state;
        alGetSourcei(source_, AL_SOURCE_STATE, &state);
        if (state != AL_PLAYING && !paused_) {
            ALint queued;
            alGetSourcei(source_, AL_BUFFERS_QUEUED, &queued);
            if (queued == 0) {
                // Playback finished
                break;
            } else {
                // Restart playback (buffer underrun)
                alSourcePlay(source_);
            }
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    playing_ = false;
}

void MusicPlayer::stopThread() {
    stopRequested_ = true;

    if (streamThread_.joinable()) {
        streamThread_.join();
    }
}

bool MusicPlayer::loadMP3(const std::string& filepath) {
    mp3dec_t mp3d;
    mp3dec_file_info_t info;

    if (mp3dec_load(&mp3d, filepath.c_str(), &info, nullptr, nullptr)) {
        LOG_ERROR(MOD_AUDIO, "Failed to decode MP3: {}", filepath);
        return false;
    }

    // Copy decoded data
    decodedData_.resize(info.samples);
    std::memcpy(decodedData_.data(), info.buffer, info.samples * sizeof(int16_t));
    free(info.buffer);

    sampleRate_ = info.hz;
    channels_ = info.channels;
    format_ = (channels_ == 1) ? AL_FORMAT_MONO16 : AL_FORMAT_STEREO16;

    LOG_DEBUG(MOD_AUDIO, "Loaded MP3: {}Hz, {}ch, {} samples",
              sampleRate_, channels_, decodedData_.size());
    return true;
}

bool MusicPlayer::loadXMI(const std::string& filepath) {
#ifdef WITH_FLUIDSYNTH
    std::cerr << "[AUDIO] loadXMI: " << filepath << std::endl;

    if (!fluidSynth_) {
        std::cerr << "[AUDIO] loadXMI: FAILED - no synth" << std::endl;
        return false;
    }
    if (soundFontId_ < 0) {
        std::cerr << "[AUDIO] loadXMI: FAILED - no soundfont" << std::endl;
        return false;
    }

    // Decode XMI to MIDI
    XmiDecoder decoder;
    std::vector<uint8_t> midiData = decoder.decodeFile(filepath);
    if (midiData.empty()) {
        std::cerr << "[AUDIO] loadXMI: FAILED - " << decoder.getError() << std::endl;
        return false;
    }

    // Reset synth and create new player
    fluid_synth_system_reset(fluidSynth_);

    if (fluidPlayer_) {
        delete_fluid_player(fluidPlayer_);
    }
    fluidPlayer_ = new_fluid_player(fluidSynth_);
    if (!fluidPlayer_) {
        std::cerr << "[AUDIO] loadXMI: FAILED - couldn't create player" << std::endl;
        return false;
    }

    // Load MIDI data directly into player
    if (fluid_player_add_mem(fluidPlayer_, midiData.data(), midiData.size()) != FLUID_OK) {
        std::cerr << "[AUDIO] loadXMI: FAILED - couldn't load MIDI data" << std::endl;
        delete_fluid_player(fluidPlayer_);
        fluidPlayer_ = nullptr;
        return false;
    }

    // Set loop mode if needed (handled by MusicPlayer::play)
    fluid_player_set_loop(fluidPlayer_, looping_ ? -1 : 0);

    // Start playback - FluidSynth handles audio output via PulseAudio
    fluid_player_play(fluidPlayer_);
    std::cerr << "[AUDIO] loadXMI: playing via FluidSynth" << std::endl;

    // For XMI, we don't buffer - just let FluidSynth play directly
    // Set minimal state so isPlaying() works
    sampleRate_ = 44100;
    channels_ = 2;
    format_ = AL_FORMAT_STEREO16;
    playing_ = true;

    return true;
#else
    std::cerr << "[AUDIO] loadXMI: FAILED - FluidSynth not compiled in" << std::endl;
    return false;
#endif
}

bool MusicPlayer::loadWAV(const std::string& filepath) {
    SF_INFO sfInfo;
    std::memset(&sfInfo, 0, sizeof(sfInfo));

    SNDFILE* file = sf_open(filepath.c_str(), SFM_READ, &sfInfo);
    if (!file) {
        LOG_ERROR(MOD_AUDIO, "Failed to open WAV: {}", filepath);
        return false;
    }

    decodedData_.resize(sfInfo.frames * sfInfo.channels);
    sf_readf_short(file, decodedData_.data(), sfInfo.frames);
    sf_close(file);

    sampleRate_ = sfInfo.samplerate;
    channels_ = sfInfo.channels;
    format_ = (channels_ == 1) ? AL_FORMAT_MONO16 : AL_FORMAT_STEREO16;

    LOG_DEBUG(MOD_AUDIO, "Loaded WAV: {}Hz, {}ch, {} samples",
              sampleRate_, channels_, decodedData_.size());
    return true;
}

void MusicPlayer::fillBuffer(ALuint buffer) {
    size_t samplesToFill = BUFFER_SIZE;
    size_t available = decodedData_.size() - playbackPosition_;

    if (available == 0) {
        return;
    }

    size_t toFill = std::min(samplesToFill, available);

    alBufferData(buffer, format_,
                 decodedData_.data() + playbackPosition_,
                 toFill * sizeof(int16_t),
                 sampleRate_);

    // Send to RDP if callback is set
    if (outputCallback_) {
        outputCallback_(decodedData_.data() + playbackPosition_, toFill);
    }

    playbackPosition_ += toFill;
}

bool MusicPlayer::streamMoreData() {
    return playbackPosition_ < decodedData_.size();
}

} // namespace Audio
} // namespace EQT

#endif // WITH_AUDIO
