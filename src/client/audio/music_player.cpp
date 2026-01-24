#ifdef WITH_AUDIO

#include "client/audio/music_player.h"
#include "common/logging.h"

#ifdef WITH_FLUIDSYNTH
#include "client/audio/xmi_decoder.h"
#endif

#include <AL/alc.h>
#include <sndfile.h>
#include <filesystem>
#include <algorithm>
#include <cstring>
#include <cstdlib>
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

bool MusicPlayer::initialize(const std::string& eqPath, const std::string& soundFontPath) {
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
    // Initialize FluidSynth for MIDI/XMI playback
    fluidSettings_ = new_fluid_settings();
    if (fluidSettings_) {
        // Use PulseAudio for real-time background music playback
        fluid_settings_setstr(fluidSettings_, "audio.driver", "pulseaudio");

        // Match exact settings from working standalone test
        fluid_settings_setnum(fluidSettings_, "synth.sample-rate", 44100.0);
        fluid_settings_setint(fluidSettings_, "synth.reverb.active", 1);
        fluid_settings_setint(fluidSettings_, "synth.chorus.active", 1);
        fluid_settings_setint(fluidSettings_, "synth.polyphony", 256);
        fluid_settings_setint(fluidSettings_, "audio.period-size", 512);
        fluid_settings_setint(fluidSettings_, "audio.periods", 4);
        fluid_settings_setnum(fluidSettings_, "synth.gain", 0.6);

        fluidSynth_ = new_fluid_synth(fluidSettings_);
        if (fluidSynth_) {
            // Load EQ client soundfonts first (lower priority)
            bool anySoundFontLoaded = false;
            if (!eqPath.empty()) {
                // Load synthus2.sf2 (main instrument bank)
                std::string eqSf2Main = eqPath + "/synthus2.sf2";
                if (std::filesystem::exists(eqSf2Main)) {
                    int sfId = fluid_synth_sfload(fluidSynth_, eqSf2Main.c_str(), 1);
                    if (sfId >= 0) {
                        LOG_INFO(MOD_AUDIO, "Loaded EQ SoundFont: {}", eqSf2Main);
                        soundFontId_ = sfId;
                        anySoundFontLoaded = true;
                    }
                }

                // Load synthusr.sf2 (supplemental bank)
                std::string eqSf2User = eqPath + "/synthusr.sf2";
                if (std::filesystem::exists(eqSf2User)) {
                    int sfId = fluid_synth_sfload(fluidSynth_, eqSf2User.c_str(), 1);
                    if (sfId >= 0) {
                        LOG_INFO(MOD_AUDIO, "Loaded EQ SoundFont: {}", eqSf2User);
                        soundFontId_ = sfId;
                        anySoundFontLoaded = true;
                    }
                }
            }

            // Load user-specified soundfont last (highest priority)
            if (!soundFontPath.empty() && std::filesystem::exists(soundFontPath)) {
                int sfId = fluid_synth_sfload(fluidSynth_, soundFontPath.c_str(), 1);
                if (sfId >= 0) {
                    LOG_INFO(MOD_AUDIO, "Loaded user SoundFont: {}", soundFontPath);
                    soundFontId_ = sfId;
                    anySoundFontLoaded = true;
                } else {
                    LOG_WARN(MOD_AUDIO, "Failed to load user SoundFont: {}", soundFontPath);
                }
            }

            // Create audio driver if any soundfont was loaded
            if (anySoundFontLoaded) {
                fluidAudioDriver_ = new_fluid_audio_driver(fluidSettings_, fluidSynth_);
                if (!fluidAudioDriver_) {
                    // Try ALSA as fallback
                    fluid_settings_setstr(fluidSettings_, "audio.driver", "alsa");
                    fluidAudioDriver_ = new_fluid_audio_driver(fluidSettings_, fluidSynth_);
                    if (!fluidAudioDriver_) {
                        LOG_WARN(MOD_AUDIO, "Failed to create FluidSynth audio driver");
                    }
                }
            } else {
                LOG_WARN(MOD_AUDIO, "No soundfonts loaded - XMI playback disabled");
            }
        } else {
            LOG_ERROR(MOD_AUDIO, "Failed to create FluidSynth synth");
        }
    } else {
        LOG_ERROR(MOD_AUDIO, "Failed to create FluidSynth settings");
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

bool MusicPlayer::play(const std::string& filepath, bool loop, int trackIndex) {
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
    if (ext == ".mp3") {
        loaded = loadMP3(filepath);
    } else if (ext == ".xmi") {
        loaded = loadXMI(filepath, trackIndex);
    } else if (ext == ".wav") {
        loaded = loadWAV(filepath);
    } else {
        LOG_WARN(MOD_AUDIO, "Unsupported music format: {}", ext);
        return false;
    }

    if (!loaded) {
        return false;
    }

    // For XMI files in hardware mode, FluidSynth handles playback directly via PulseAudio
    // No need for OpenAL streaming (but only if audio driver actually exists)
#ifdef WITH_FLUIDSYNTH
    if (ext == ".xmi" && !softwareRendering_ && fluidAudioDriver_ != nullptr) {
        LOG_INFO(MOD_AUDIO, "Playing MIDI music (hardware mode): {}", filepath);
        return true;
    }
#endif

    // For MP3/WAV, or XMI in software rendering mode, use OpenAL streaming
    playing_ = true;
    paused_ = false;
    stopRequested_ = false;
    playbackPosition_ = 0;
    fadeVolume_ = 1.0f;  // Reset fade volume for new playback
    fadeTarget_ = 1.0f;
    fadeRate_ = 0.0f;

    // Verify OpenAL context is current
    ALCcontext* ctx = alcGetCurrentContext();
    if (!ctx) {
        LOG_ERROR(MOD_AUDIO, "No OpenAL context current when starting music");
    }

    // Stop source and clear any previously queued buffers
    alSourceStop(source_);
    ALint queued;
    alGetSourcei(source_, AL_BUFFERS_QUEUED, &queued);
    while (queued-- > 0) {
        ALuint buffer;
        alSourceUnqueueBuffers(source_, 1, &buffer);
    }

    // Fill initial buffers
    for (size_t i = 0; i < NUM_BUFFERS; ++i) {
        fillBuffer(buffers_[i]);
    }

    // Queue buffers and start playback
    alSourceQueueBuffers(source_, NUM_BUFFERS, buffers_);
    ALenum err = alGetError();
    if (err != AL_NO_ERROR) {
        LOG_ERROR(MOD_AUDIO, "alSourceQueueBuffers failed: 0x{:x}", err);
    }

    float gain = volume_ * fadeVolume_;
    alSourcef(source_, AL_GAIN, gain);
    alSourcePlay(source_);
    err = alGetError();
    if (err != AL_NO_ERROR) {
        LOG_ERROR(MOD_AUDIO, "alSourcePlay failed: 0x{:x}", err);
    }

    // Start streaming thread
    LOG_DEBUG(MOD_AUDIO, "play: about to create streaming thread, streamThread_.joinable()={}", streamThread_.joinable());
    if (streamThread_.joinable()) {
        LOG_ERROR(MOD_AUDIO, "play: ERROR - streamThread_ is still joinable! This will crash!");
    }
    streamThread_ = std::thread(&MusicPlayer::streamingThread, this);

    LOG_INFO(MOD_AUDIO, "Playing music: {}", filepath);
    return true;
}

void MusicPlayer::stop(float fadeSeconds) {
    LOG_DEBUG(MOD_AUDIO, "MusicPlayer::stop called, playing_={}, fadeSeconds={}", playing_.load(), fadeSeconds);

    // Always join the thread if it's joinable, even if playing_ is false
    // (the streaming thread may have finished and set playing_=false but not been joined)
    if (!playing_) {
        if (streamThread_.joinable()) {
            LOG_DEBUG(MOD_AUDIO, "MusicPlayer::stop - not playing but thread joinable, joining...");
            streamThread_.join();
            LOG_DEBUG(MOD_AUDIO, "MusicPlayer::stop - thread joined");
        }
        LOG_DEBUG(MOD_AUDIO, "MusicPlayer::stop - not playing, returning early");
        return;
    }

    if (fadeSeconds > 0.0f) {
        fadeTarget_ = 0.0f;
        fadeRate_ = 1.0f / fadeSeconds;
    } else {
        LOG_DEBUG(MOD_AUDIO, "MusicPlayer::stop - stopping immediately");
        stopRequested_ = true;
        stopThread();
        LOG_DEBUG(MOD_AUDIO, "MusicPlayer::stop - thread stopped");

        // Stop FluidSynth player if active
#ifdef WITH_FLUIDSYNTH
        fluidSynthStreaming_ = false;
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
        LOG_DEBUG(MOD_AUDIO, "MusicPlayer::stop - done");
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

void MusicPlayer::enableSoftwareRendering() {
    if (softwareRendering_) {
        return;  // Already enabled
    }

    LOG_INFO(MOD_AUDIO, "Enabling software rendering for music (FluidSynth -> OpenAL)");

#ifdef WITH_FLUIDSYNTH
    // In software rendering mode, we manually parse and sequence MIDI events
    // instead of using fluid_player (which relies on audio driver callbacks).
    // Delete the audio driver since we'll render manually.
    if (fluidAudioDriver_) {
        delete_fluid_audio_driver(fluidAudioDriver_);
        fluidAudioDriver_ = nullptr;
        LOG_DEBUG(MOD_AUDIO, "FluidSynth audio driver removed for manual MIDI sequencing");
    }
#endif

    softwareRendering_ = true;
}

void MusicPlayer::reinitializeOpenAL() {
    LOG_DEBUG(MOD_AUDIO, "Reinitializing music player OpenAL resources");

    // Stop the streaming thread if running
    stopRequested_ = true;
    stopThread();

    // Stop FluidSynth player if active
#ifdef WITH_FLUIDSYNTH
    fluidSynthStreaming_ = false;
    if (fluidPlayer_) {
        fluid_player_stop(fluidPlayer_);
    }
#endif

    // Reset state
    playing_ = false;
    paused_ = false;
    decodedData_.clear();
    fadeVolume_ = 1.0f;  // Reset fade volume
    fadeTarget_ = 1.0f;
    fadeRate_ = 0.0f;

    // Old OpenAL resources are invalid (old context is destroyed)
    // Just reset handles - don't try to delete them
    source_ = 0;
    for (ALuint& buf : buffers_) {
        buf = 0;
    }

    // Create new OpenAL resources with current context
    alGetError();  // Clear any pending errors
    alGenSources(1, &source_);
    if (alGetError() != AL_NO_ERROR) {
        LOG_ERROR(MOD_AUDIO, "Failed to create music source after context change");
        return;
    }

    alGenBuffers(NUM_BUFFERS, buffers_);
    if (alGetError() != AL_NO_ERROR) {
        LOG_ERROR(MOD_AUDIO, "Failed to create music buffers after context change");
        alDeleteSources(1, &source_);
        source_ = 0;
        return;
    }

    // Configure source for streaming (same as in initialize())
    alSourcef(source_, AL_PITCH, 1.0f);
    alSourcef(source_, AL_GAIN, 1.0f);
    alSource3f(source_, AL_POSITION, 0.0f, 0.0f, 0.0f);
    alSource3f(source_, AL_VELOCITY, 0.0f, 0.0f, 0.0f);
    alSourcei(source_, AL_LOOPING, AL_FALSE);
    alSourcei(source_, AL_SOURCE_RELATIVE, AL_TRUE);

    LOG_INFO(MOD_AUDIO, "Music player OpenAL resources reinitialized");
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
    LOG_DEBUG(MOD_AUDIO, "stopThread: stopRequested_=true, joinable={}", streamThread_.joinable());
    stopRequested_ = true;

    if (streamThread_.joinable()) {
        LOG_DEBUG(MOD_AUDIO, "stopThread: joining thread...");
        streamThread_.join();
        LOG_DEBUG(MOD_AUDIO, "stopThread: thread joined");
    } else {
        LOG_DEBUG(MOD_AUDIO, "stopThread: thread not joinable");
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

bool MusicPlayer::loadXMI(const std::string& filepath, int trackIndex) {
#ifdef WITH_FLUIDSYNTH
    if (!fluidSynth_) {
        LOG_ERROR(MOD_AUDIO, "loadXMI: no synth available");
        return false;
    }
    if (soundFontId_ < 0) {
        LOG_ERROR(MOD_AUDIO, "loadXMI: no soundfont loaded");
        return false;
    }

    // Decode XMI to MIDI, selecting specific track/sequence
    XmiDecoder decoder;
    std::vector<uint8_t> midiData = decoder.decodeFile(filepath, trackIndex);
    if (midiData.empty()) {
        LOG_ERROR(MOD_AUDIO, "loadXMI: failed to decode - {}", decoder.getError());
        return false;
    }

    LOG_DEBUG(MOD_AUDIO, "loadXMI: decoded track {} of {} from {}",
              trackIndex, decoder.getNumSequences(), filepath);

    // Reset synth
    fluid_synth_system_reset(fluidSynth_);

    sampleRate_ = 44100;  // Match FluidSynth's sample rate (and loopback rate)
    channels_ = 2;
    format_ = AL_FORMAT_STEREO16;

    // Use software rendering if explicitly enabled OR if hardware audio driver failed
    bool useSoftwareRendering = softwareRendering_ || (fluidAudioDriver_ == nullptr);
    if (useSoftwareRendering) {
        // Software rendering mode: manually parse MIDI and sequence events
        // We can't use fluid_player because it relies on audio driver callbacks for timing
        if (!parseMidiEvents(midiData)) {
            LOG_ERROR(MOD_AUDIO, "loadXMI: failed to parse MIDI data");
            return false;
        }

        // Reset sequencer state
        midiEventIndex_ = 0;
        midiTickPosition_ = 0;
        samplePosition_ = 0;

        LOG_DEBUG(MOD_AUDIO, "loadXMI: parsed {} MIDI events for software rendering", midiEvents_.size());
        fluidSynthStreaming_ = true;
    } else {
        // Hardware mode: use fluid_player (audio driver handles timing)
        if (fluidPlayer_) {
            delete_fluid_player(fluidPlayer_);
        }
        fluidPlayer_ = new_fluid_player(fluidSynth_);
        if (!fluidPlayer_) {
            LOG_ERROR(MOD_AUDIO, "loadXMI: failed to create FluidSynth player");
            return false;
        }

        if (fluid_player_add_mem(fluidPlayer_, midiData.data(), midiData.size()) != FLUID_OK) {
            LOG_ERROR(MOD_AUDIO, "loadXMI: failed to load MIDI data into player");
            delete_fluid_player(fluidPlayer_);
            fluidPlayer_ = nullptr;
            return false;
        }

        fluid_player_set_loop(fluidPlayer_, looping_ ? -1 : 0);
        fluid_player_play(fluidPlayer_);
        LOG_DEBUG(MOD_AUDIO, "FluidSynth player started");
    }

    return true;
#else
    LOG_ERROR(MOD_AUDIO, "loadXMI: FluidSynth not compiled in");
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
#ifdef WITH_FLUIDSYNTH
    // FluidSynth streaming mode
    if (fluidSynthStreaming_.load() && fluidSynth_) {
        // Render audio from FluidSynth
        // BUFFER_SIZE is total samples, we need frames (samples / channels)
        size_t frames = BUFFER_SIZE / 2;  // stereo
        std::vector<int16_t> renderBuffer(BUFFER_SIZE);

        // In software rendering mode, process MIDI events before rendering
        if (softwareRendering_) {
            processMidiEvents(frames);
        }

        int result = fluid_synth_write_s16(fluidSynth_, frames,
                                           renderBuffer.data(), 0, 2,  // left channel
                                           renderBuffer.data(), 1, 2); // right channel (interleaved)

        if (result == FLUID_OK) {
            alBufferData(buffer, AL_FORMAT_STEREO16,
                         renderBuffer.data(),
                         BUFFER_SIZE * sizeof(int16_t),
                         sampleRate_);
        } else {
            LOG_ERROR(MOD_AUDIO, "fillBuffer: FluidSynth render failed, result={}", result);
        }
        return;
    }
#endif

    // Regular PCM streaming (MP3/WAV)
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
    // Note: Don't call outputCallback_ here - audio goes through OpenAL loopback

    playbackPosition_ += toFill;
}

bool MusicPlayer::streamMoreData() {
#ifdef WITH_FLUIDSYNTH
    // For FluidSynth streaming with manual sequencing
    if (fluidSynthStreaming_.load()) {
        if (softwareRendering_) {
            // Manual sequencing mode - check if we have more events or looping
            bool hasMoreEvents = midiEventIndex_ < midiEvents_.size();
            if (!hasMoreEvents && looping_ && !midiEvents_.empty()) {
                // Loop: reset to beginning
                midiEventIndex_ = 0;
                midiTickPosition_ = 0;
                samplePosition_ = 0;
                fluid_synth_system_reset(fluidSynth_);
                return true;
            }
            return hasMoreEvents;
        } else if (fluidPlayer_) {
            // Hardware mode with fluid_player
            int status = fluid_player_get_status(fluidPlayer_);
            return status == FLUID_PLAYER_PLAYING;
        }
    }
#endif
    return playbackPosition_ < decodedData_.size();
}

#ifdef WITH_FLUIDSYNTH
bool MusicPlayer::parseMidiEvents(const std::vector<uint8_t>& midiData) {
    midiEvents_.clear();
    midiTicksPerBeat_ = 480;  // Default
    midiTempo_ = 500000;      // Default 120 BPM

    if (midiData.size() < 14) {
        LOG_ERROR(MOD_AUDIO, "parseMidiEvents: data too small");
        return false;
    }

    // Check MIDI header
    if (midiData[0] != 'M' || midiData[1] != 'T' || midiData[2] != 'h' || midiData[3] != 'd') {
        LOG_ERROR(MOD_AUDIO, "parseMidiEvents: invalid MIDI header");
        return false;
    }

    // Parse header
    uint32_t headerLen = (midiData[4] << 24) | (midiData[5] << 16) | (midiData[6] << 8) | midiData[7];
    uint16_t format = (midiData[8] << 8) | midiData[9];
    uint16_t numTracks = (midiData[10] << 8) | midiData[11];
    uint16_t ticksPerBeat = (midiData[12] << 8) | midiData[13];

    midiTicksPerBeat_ = ticksPerBeat;
    LOG_DEBUG(MOD_AUDIO, "MIDI: format={}, tracks={}, ppqn={}", format, numTracks, ticksPerBeat);

    size_t pos = 8 + headerLen;

    // Parse each track
    for (uint16_t track = 0; track < numTracks && pos < midiData.size(); track++) {
        if (pos + 8 > midiData.size()) break;

        // Check track header
        if (midiData[pos] != 'M' || midiData[pos+1] != 'T' || midiData[pos+2] != 'r' || midiData[pos+3] != 'k') {
            LOG_WARN(MOD_AUDIO, "parseMidiEvents: invalid track header at pos {}", pos);
            break;
        }

        uint32_t trackLen = (midiData[pos+4] << 24) | (midiData[pos+5] << 16) | (midiData[pos+6] << 8) | midiData[pos+7];
        size_t trackEnd = pos + 8 + trackLen;
        pos += 8;

        uint64_t absoluteTick = 0;
        uint8_t runningStatus = 0;

        while (pos < trackEnd && pos < midiData.size()) {
            // Read variable-length delta time
            uint32_t deltaTime = 0;
            while (pos < midiData.size()) {
                uint8_t byte = midiData[pos++];
                deltaTime = (deltaTime << 7) | (byte & 0x7F);
                if (!(byte & 0x80)) break;
            }
            absoluteTick += deltaTime;

            if (pos >= midiData.size()) break;

            uint8_t status = midiData[pos];

            // Handle running status
            if (status < 0x80) {
                status = runningStatus;
            } else {
                pos++;
                if (status < 0xF0) {
                    runningStatus = status;
                }
            }

            uint8_t type = status & 0xF0;
            uint8_t channel = status & 0x0F;

            if (type == 0x80 || type == 0x90 || type == 0xA0 || type == 0xB0 || type == 0xE0) {
                // Two data bytes
                if (pos + 1 >= midiData.size()) break;
                uint8_t data1 = midiData[pos++];
                uint8_t data2 = midiData[pos++];

                MidiEvent event;
                event.tick = absoluteTick;
                event.type = type;
                event.channel = channel;
                event.data1 = data1;
                event.data2 = data2;
                midiEvents_.push_back(event);
            } else if (type == 0xC0 || type == 0xD0) {
                // One data byte
                if (pos >= midiData.size()) break;
                uint8_t data1 = midiData[pos++];

                MidiEvent event;
                event.tick = absoluteTick;
                event.type = type;
                event.channel = channel;
                event.data1 = data1;
                event.data2 = 0;
                midiEvents_.push_back(event);
            } else if (status == 0xFF) {
                // Meta event
                if (pos + 1 >= midiData.size()) break;
                uint8_t metaType = midiData[pos++];
                uint32_t metaLen = 0;
                while (pos < midiData.size()) {
                    uint8_t byte = midiData[pos++];
                    metaLen = (metaLen << 7) | (byte & 0x7F);
                    if (!(byte & 0x80)) break;
                }

                // Handle tempo change
                if (metaType == 0x51 && metaLen == 3 && pos + 2 < midiData.size()) {
                    midiTempo_ = (midiData[pos] << 16) | (midiData[pos+1] << 8) | midiData[pos+2];
                    LOG_DEBUG(MOD_AUDIO, "MIDI: tempo change to {} us/beat ({} BPM)", midiTempo_, 60000000 / midiTempo_);
                }

                pos += metaLen;
            } else if (status == 0xF0 || status == 0xF7) {
                // SysEx
                uint32_t sysexLen = 0;
                while (pos < midiData.size()) {
                    uint8_t byte = midiData[pos++];
                    sysexLen = (sysexLen << 7) | (byte & 0x7F);
                    if (!(byte & 0x80)) break;
                }
                pos += sysexLen;
            }
        }

        pos = trackEnd;
    }

    // Sort events by tick (for multi-track files)
    std::sort(midiEvents_.begin(), midiEvents_.end(),
              [](const MidiEvent& a, const MidiEvent& b) { return a.tick < b.tick; });

    return !midiEvents_.empty();
}

void MusicPlayer::processMidiEvents(size_t samplesToRender) {
    if (!fluidSynth_ || midiEvents_.empty()) return;

    // Calculate how many ticks correspond to the samples we're about to render
    // ticks = samples * ticksPerBeat * tempo / (sampleRate * 1000000)
    // But we need to track cumulative position to avoid drift

    samplePosition_ += samplesToRender;

    // Convert sample position to tick position
    // tick = sample * ticksPerBeat / (sampleRate * microsecondsPerBeat / 1000000)
    // tick = sample * ticksPerBeat * 1000000 / (sampleRate * microsecondsPerBeat)
    // NOTE: Cast to uint64_t to prevent overflow in denominator (44100 * 500000 > uint32_t max)
    uint64_t targetTick = (samplePosition_ * midiTicksPerBeat_ * 1000000ULL) / ((uint64_t)sampleRate_ * midiTempo_);

    // Process all events up to the target tick
    while (midiEventIndex_ < midiEvents_.size()) {
        const MidiEvent& event = midiEvents_[midiEventIndex_];

        if (event.tick > targetTick) {
            break;  // Not time for this event yet
        }

        // Send event to synth
        switch (event.type) {
            case 0x80:  // Note Off
                fluid_synth_noteoff(fluidSynth_, event.channel, event.data1);
                break;
            case 0x90:  // Note On
                if (event.data2 == 0) {
                    fluid_synth_noteoff(fluidSynth_, event.channel, event.data1);
                } else {
                    fluid_synth_noteon(fluidSynth_, event.channel, event.data1, event.data2);
                }
                break;
            case 0xA0:  // Polyphonic Aftertouch
                fluid_synth_key_pressure(fluidSynth_, event.channel, event.data1, event.data2);
                break;
            case 0xB0:  // Control Change
                fluid_synth_cc(fluidSynth_, event.channel, event.data1, event.data2);
                break;
            case 0xC0:  // Program Change
                fluid_synth_program_change(fluidSynth_, event.channel, event.data1);
                break;
            case 0xD0:  // Channel Aftertouch
                fluid_synth_channel_pressure(fluidSynth_, event.channel, event.data1);
                break;
            case 0xE0:  // Pitch Bend
                {
                    int bend = ((event.data2 << 7) | event.data1) - 8192;
                    fluid_synth_pitch_bend(fluidSynth_, event.channel, bend + 8192);
                }
                break;
        }

        midiEventIndex_++;
    }

    midiTickPosition_ = targetTick;
}
#endif

} // namespace Audio
} // namespace EQT

#endif // WITH_AUDIO
