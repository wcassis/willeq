#ifdef WITH_AUDIO

#include "client/audio/music_player.h"
#include "client/audio/midi_player.h"
#include "client/audio/audio_mixer.h"
#include "client/audio/xmi_decoder.h"
#include "common/logging.h"

#include <filesystem>
#include <algorithm>
#include <cstring>

// Simple MP3 decoder (minimp3)
#define MINIMP3_IMPLEMENTATION
#define MINIMP3_ONLY_MP3
#include "minimp3.h"
#include "minimp3_ex.h"

// dr_wav for WAV loading (implementation in sfx_manager.cpp)
#include "dr_wav.h"

namespace EQT {
namespace Audio {

MusicPlayer::MusicPlayer() = default;

MusicPlayer::~MusicPlayer() {
    shutdown();
}

bool MusicPlayer::initialize(const std::string& eqPath, const std::string& soundFontPath) {
    if (initialized_) {
        return true;
    }

    eqPath_ = eqPath;

    // If no external MidiPlayer is set, create our own
    if (!midiPlayer_) {
        ownedMidiPlayer_ = std::make_unique<MidiPlayer>();

        // Try to load a SoundFont
        std::string sfPath = soundFontPath;
        if (sfPath.empty() && !eqPath.empty()) {
            std::string eqSf2 = eqPath + "/synthus2.sf2";
            if (std::filesystem::exists(eqSf2)) {
                sfPath = eqSf2;
            } else {
                std::string eqSf2User = eqPath + "/synthusr.sf2";
                if (std::filesystem::exists(eqSf2User)) {
                    sfPath = eqSf2User;
                }
            }
        }

        if (!sfPath.empty()) {
            if (ownedMidiPlayer_->init(sfPath, AudioMixer::SAMPLE_RATE)) {
                LOG_INFO(MOD_AUDIO, "MusicPlayer: internal MIDI player initialized with {}", sfPath);
            } else {
                LOG_WARN(MOD_AUDIO, "MusicPlayer: failed to init internal MIDI player");
            }
        }

        midiPlayer_ = ownedMidiPlayer_.get();
    }

    initialized_ = true;
    LOG_INFO(MOD_AUDIO, "Music player initialized");
    return true;
}

void MusicPlayer::shutdown() {
    if (!initialized_) {
        return;
    }

    stop();

    // Free mixer channel
    if (mixer_ && musicChannelHandle_ >= 0) {
        mixer_->freeChannel(musicChannelHandle_);
        musicChannelHandle_ = -1;
    }

    // Release owned MidiPlayer
    if (ownedMidiPlayer_) {
        ownedMidiPlayer_->stop();
        ownedMidiPlayer_.reset();
    }

    midiPlayer_ = nullptr;
    initialized_ = false;
}

void MusicPlayer::setMidiPlayer(MidiPlayer* player) {
    midiPlayer_ = player;
}

void MusicPlayer::setMixer(AudioMixer* mixer) {
    mixer_ = mixer;
}

bool MusicPlayer::play(const std::string& filepath, bool loop, int trackIndex, double startTimeMs) {
    if (!initialized_) {
        return false;
    }

    // Stop current playback
    stop();

    currentFile_ = filepath;
    currentTrackIndex_ = trackIndex;
    looping_ = loop;
    playingMidi_ = false;

    // Determine file type and load
    std::string ext = std::filesystem::path(filepath).extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

    bool loaded = false;
    if (ext == ".mp3") {
        loaded = loadMP3(filepath);
    } else if (ext == ".xmi") {
        loaded = loadXMI(filepath, trackIndex, startTimeMs);
    } else if (ext == ".wav") {
        loaded = loadWAV(filepath);
    } else {
        LOG_WARN(MOD_AUDIO, "Unsupported music format: {}", ext);
        return false;
    }

    if (!loaded) {
        return false;
    }

    playing_ = true;
    paused_ = false;
    fadeVolume_ = 1.0f;
    fadeTarget_ = 1.0f;
    fadeRate_ = 0.0f;
    playbackPosition_ = 0;

    startMixerPlayback();

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
        // Fade will be handled in the render callback
        return;
    }

    // Immediate stop
    playing_ = false;
    paused_ = false;

    // Free mixer channel FIRST to prevent further render callbacks.
    // Do NOT call midiPlayer_->stop() here — the audio thread may still be
    // inside midiPlayer_->render() accessing TSF state. Resetting TSF from
    // the main thread while the audio thread reads it causes garbled audio.
    // The midi player will be safely reset when midiPlayer_->play() is called
    // next (from loadXMI), or from the fade-out completion in the render callback.
    if (mixer_ && musicChannelHandle_ >= 0) {
        mixer_->freeChannel(musicChannelHandle_);
        musicChannelHandle_ = -1;
    }

    decodedData_.clear();
    currentFile_.clear();
    playingMidi_ = false;
}

void MusicPlayer::pause() {
    if (playing_ && !paused_) {
        paused_ = true;
        // Render callback will output silence when paused
    }
}

void MusicPlayer::resume() {
    if (playing_ && paused_) {
        paused_ = false;
    }
}

float MusicPlayer::getPosition() const {
    if (!playing_ || sampleRate_ == 0) {
        return 0.0f;
    }
    return static_cast<float>(playbackPosition_.load()) / static_cast<float>(sampleRate_);
}

void MusicPlayer::setVolume(float volume) {
    volume_ = std::clamp(volume, 0.0f, 1.0f);

    // Update mixer channel volume
    if (mixer_ && musicChannelHandle_ >= 0) {
        MixChannel* ch = mixer_->getChannel(musicChannelHandle_);
        if (ch) {
            ch->volume = volume_.load() * fadeVolume_.load();
        }
    }
}

void MusicPlayer::setOutputCallback(MusicOutputCallback callback) {
    outputCallback_ = std::move(callback);
}

void MusicPlayer::enableSoftwareRendering() {
    // No-op — always software rendering now
}

void MusicPlayer::reinitializeOpenAL() {
    // No-op — no OpenAL to reinitialize
}

void MusicPlayer::mixerRenderCallback(void* userData, float* buffer, int frameCount) {
    auto* player = static_cast<MusicPlayer*>(userData);
    if (!player) {
        std::memset(buffer, 0, frameCount * 2 * sizeof(float));
        return;
    }

    if (!player->playing_.load() || player->paused_.load()) {
        std::memset(buffer, 0, frameCount * 2 * sizeof(float));
        return;
    }

    // Handle fade
    float fadeRate = player->fadeRate_.load();
    if (fadeRate > 0.0f) {
        float fade = player->fadeVolume_.load();
        float target = player->fadeTarget_.load();
        float dt = static_cast<float>(frameCount) / static_cast<float>(player->sampleRate_);
        fade -= fadeRate * dt;
        if (fade <= target) {
            fade = target;
            player->fadeRate_.store(0.0f);
            if (target <= 0.0f) {
                // Fade complete — stop playback
                player->playing_.store(false);
                if (player->playingMidi_ && player->midiPlayer_) {
                    player->midiPlayer_->stop();
                }
                std::memset(buffer, 0, frameCount * 2 * sizeof(float));
                return;
            }
        }
        player->fadeVolume_.store(fade);

        // Update channel volume to reflect fade
        if (player->mixer_ && player->musicChannelHandle_ >= 0) {
            MixChannel* ch = player->mixer_->getChannel(player->musicChannelHandle_);
            if (ch) {
                ch->volume = player->volume_.load() * fade;
            }
        }
    }

    if (player->playingMidi_ && player->midiPlayer_) {
        // MIDI: delegate to MidiPlayer
        player->midiPlayer_->render(buffer, frameCount);
    } else if (!player->decodedData_.empty()) {
        // PCM (MP3/WAV): read from decoded data
        size_t pos = player->playbackPosition_.load();
        size_t totalFrames = player->decodedData_.size() / player->channels_;
        uint8_t ch = player->channels_;

        for (int i = 0; i < frameCount; ++i) {
            if (pos >= totalFrames) {
                if (player->looping_.load()) {
                    pos = 0;
                } else {
                    // Fill remainder with silence
                    std::memset(buffer + i * 2, 0,
                                (frameCount - i) * 2 * sizeof(float));
                    player->playing_.store(false);
                    break;
                }
            }

            if (ch == 2) {
                buffer[i * 2]     = player->decodedData_[pos * 2];
                buffer[i * 2 + 1] = player->decodedData_[pos * 2 + 1];
            } else {
                // Mono → stereo
                float s = player->decodedData_[pos];
                buffer[i * 2]     = s;
                buffer[i * 2 + 1] = s;
            }
            pos++;
        }
        player->playbackPosition_.store(pos);
    } else {
        std::memset(buffer, 0, frameCount * 2 * sizeof(float));
    }
}

bool MusicPlayer::loadMP3(const std::string& filepath) {
    mp3dec_t mp3d;
    mp3dec_file_info_t info;

    if (mp3dec_load(&mp3d, filepath.c_str(), &info, nullptr, nullptr)) {
        LOG_ERROR(MOD_AUDIO, "Failed to decode MP3: {}", filepath);
        return false;
    }

    // Convert int16_t to float
    size_t totalSamples = info.samples;
    decodedData_.resize(totalSamples);
    for (size_t i = 0; i < totalSamples; ++i) {
        decodedData_[i] = static_cast<float>(info.buffer[i]) / 32768.0f;
    }
    free(info.buffer);

    sampleRate_ = info.hz;
    channels_ = info.channels;

    LOG_DEBUG(MOD_AUDIO, "Loaded MP3: {}Hz, {}ch, {} samples",
              sampleRate_, channels_, totalSamples);
    return true;
}

bool MusicPlayer::loadXMI(const std::string& filepath, int trackIndex, double startTimeMs) {
    if (!midiPlayer_ || !midiPlayer_->isInitialized()) {
        LOG_ERROR(MOD_AUDIO, "loadXMI: no MIDI player available");
        return false;
    }

    // Decode XMI to MIDI
    XmiDecoder decoder;
    std::vector<uint8_t> midiData = decoder.decodeFile(filepath, trackIndex);
    if (midiData.empty()) {
        LOG_ERROR(MOD_AUDIO, "loadXMI: failed to decode - {}", decoder.getError());
        return false;
    }

    LOG_DEBUG(MOD_AUDIO, "loadXMI: decoded track {} of {} from {}",
              trackIndex, decoder.getNumSequences(), filepath);

    // Play via MidiPlayer (with optional seek before playback starts)
    if (!midiPlayer_->play(midiData, looping_.load(), startTimeMs)) {
        LOG_ERROR(MOD_AUDIO, "loadXMI: MidiPlayer failed to play");
        return false;
    }

    playingMidi_ = true;
    sampleRate_ = AudioMixer::SAMPLE_RATE;
    channels_ = 2;

    return true;
}

bool MusicPlayer::loadWAV(const std::string& filepath) {
    unsigned int wavChannels = 0;
    unsigned int wavSampleRate = 0;
    drwav_uint64 wavFrameCount = 0;

    float* samples = drwav_open_file_and_read_pcm_frames_f32(
        filepath.c_str(), &wavChannels, &wavSampleRate, &wavFrameCount, nullptr);
    if (!samples) {
        LOG_ERROR(MOD_AUDIO, "Failed to open WAV: {}", filepath);
        return false;
    }

    size_t totalSamples = static_cast<size_t>(wavFrameCount * wavChannels);
    decodedData_.assign(samples, samples + totalSamples);
    drwav_free(samples, nullptr);

    sampleRate_ = wavSampleRate;
    channels_ = static_cast<uint8_t>(wavChannels);

    LOG_DEBUG(MOD_AUDIO, "Loaded WAV: {}Hz, {}ch, {} samples",
              sampleRate_, channels_, totalSamples);
    return true;
}

void MusicPlayer::startMixerPlayback() {
    if (!mixer_) {
        LOG_WARN(MOD_AUDIO, "No mixer available for music playback");
        return;
    }

    // Free old channel if any
    if (musicChannelHandle_ >= 0) {
        mixer_->freeChannel(musicChannelHandle_);
        musicChannelHandle_ = -1;
    }

    // Allocate a music channel
    musicChannelHandle_ = mixer_->allocMusicChannel();
    if (musicChannelHandle_ < 0) {
        LOG_ERROR(MOD_AUDIO, "Failed to allocate music channel");
        return;
    }

    MixChannel* ch = mixer_->getChannel(musicChannelHandle_);
    if (!ch) return;

    ch->type = MixChannelType::Music;
    ch->volume = volume_.load() * fadeVolume_.load();
    ch->pan = 0.0f;
    ch->sampleRate = sampleRate_;
    ch->channels = 2;  // All music output is stereo
    ch->looping = false;  // Looping handled by render callback

    // Always use render callback for music (handles MIDI, PCM, and fade)
    ch->samples = nullptr;
    ch->totalFrames = 0;
    ch->position = 0;
    ch->renderCallback = &MusicPlayer::mixerRenderCallback;
    ch->renderUserData = this;

    ch->active.store(true);
}

} // namespace Audio
} // namespace EQT

#endif // WITH_AUDIO
