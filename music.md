# WillEQ Audio System Architecture

## Status: Implemented

The audio backend has been replaced from OpenAL + libsndfile + FluidSynth to single-header libraries. All platform audio dependencies are eliminated.

## Dependencies (all header-only)

| Library | Purpose | License | Location |
|---------|---------|---------|----------|
| `miniaudio.h` | Audio device output (WASAPI/CoreAudio/ALSA/OpenSLES) | Public domain | `third_party/miniaudio/` |
| `tsf.h` (TinySoundFont) | SoundFont2 MIDI synthesis | MIT | `third_party/TinySoundFont/` |
| `tml.h` (TinyMIDILoader) | MIDI event parsing | MIT | `third_party/TinySoundFont/` |
| `dr_wav.h` (dr_libs) | WAV file loading/decoding | Public domain | `third_party/dr_libs/` |
| `minimp3.h` | MP3 decoding (pre-existing) | CC0 | `third_party/minimp3/` |

## Architecture

```
┌─────────────────────────────────────────────────────────────┐
│                      Audio Sources                          │
│                                                             │
│  ┌──────────────┐  ┌──────────────┐  ┌───────────────────┐  │
│  │  XmiDecoder   │  │  WAV Loader  │  │  Spatial Emitters │  │
│  │  → MidiPlayer │  │  (dr_wav.h)  │  │  (.eff/.emt)      │  │
│  │  (tsf + tml)  │  │  → SfxManager│  │                   │  │
│  └──────┬───────┘  └──────┬───────┘  └────────┬──────────┘  │
│         │                 │                    │             │
│         ▼                 ▼                    ▼             │
│  ┌─────────────────────────────────────────────────────────┐ │
│  │                   AudioMixer                            │ │
│  │  16 SFX channels + 2 music channels (crossfade)        │ │
│  │  22050 Hz stereo float PCM                              │ │
│  └──────────────────────────┬──────────────────────────────┘ │
│                             │                                │
│                        PCM Buffer                            │
└─────────────────────────┬───────────────────────────────────┘
                          │
              ┌───────────┴────────────┐
              ▼                        ▼
   ┌──────────────────┐    ┌───────────────────┐
   │ MiniaudioBackend │    │  RDPAudioBackend  │
   │ (miniaudio.h)    │    │ (RDPSND channel)  │
   └──────────────────┘    └───────────────────┘
```

## Core Components

### AudioMixer (`audio_mixer.h/.cpp`)
Software stereo PCM mixer — convergence point for all audio.
- 16 SFX channels + 2 music channels (current + crossfade)
- `render(float* output, int frame_count)` — called from audio thread
- Thread-safe: mutex on channel alloc/free, lock-free render
- Master/music/SFX volume controls

### AudioBackend (`audio_backend.h/.cpp`)
Output abstraction with two implementations:
- **MiniaudioBackend**: wraps `ma_device` in callback mode → calls `AudioMixer::render()`
- **RDPAudioBackend**: float PCM → s16 conversion, delivers via `AudioOutputCallback` to RDPSND

### MidiPlayer (`midi_player.h/.cpp`)
TSF + TML MIDI synthesis for XMI/MIDI music.
- `init(soundfont_path, sample_rate)` — loads SoundFont via `tsf_load_filename()`
- `play(midi_data, size, loop)` — parses MIDI via `tml_load_memory()`
- `render(float* buffer, frame_count)` — walks tml events, drives tsf note_on/off/control_change
- Crossfade via `tsf_copy()` (shares sample data, ~50KB voice state)

### SfxManager (`sfx_manager.h/.cpp`)
WAV loading and spatial playback.
- LRU cache: `unordered_map<string, WavData>` with eviction
- dr_wav for decoding (files, memory, PFS archives)
- Spatial attenuation: `play_spatial(filename, pos, max_dist, full_vol_dist)`
- Linear resample for non-22050 Hz WAV files

### AudioManager (`audio_manager.h/.cpp`)
Top-level coordinator — public interface unchanged from OpenAL era.
- Owns: AudioMixer, MidiPlayer, SfxManager, AudioBackend
- `initialize(eqPath, forceLoopback)` — creates all subsystems
- `playSound(id, pos)` → SfxManager spatial playback
- `playMusic(filename, loop, trackIndex)` → XmiDecoder → MidiPlayer or minimp3 → mixer
- `setListenerPosition()` → SfxManager listener update

## XMI Handling

Runtime conversion via `XmiDecoder` (pre-existing):
1. `XmiDecoder::decodeFile()` reads XMI from PFS archive
2. Extracts individual MIDI tracks (XMI files contain multiple sub-songs)
3. Produces standard MIDI bytes in memory
4. Fed to `MidiPlayer::play(midi_data, size)`

## Key Decisions

- **22050 Hz sample rate**: Matches EQ's original WAV files, halves CPU/bandwidth vs 44100
- **Runtime XMI→MIDI**: XmiDecoder already existed, no offline conversion needed
- **miniaudio for output**: Single header, zero dependencies, supports all target platforms
- **Kept minimp3**: Pre-existing MP3 decoder, works well for MP3 music files
- **SoundBuffer stores float PCM**: No more OpenAL ALuint handles

## Memory Budget

| Component | Estimate |
|-----------|----------|
| SF2 soundfont (loaded once) | 2-8 MB |
| TSF voice state | ~50 KB |
| WAV cache (zone effects) | 1-5 MB |
| Mixer buffers | ~16 KB |
| **Total** | **~3-13 MB** |

## Files

| File | Role |
|------|------|
| `include/client/audio/audio_mixer.h` | Mixer interface |
| `src/client/audio/audio_mixer.cpp` | Mixer implementation |
| `include/client/audio/audio_backend.h` | Backend interface + both implementations |
| `src/client/audio/audio_backend.cpp` | Backend implementation (miniaudio + RDP) |
| `include/client/audio/midi_player.h` | MIDI player interface |
| `src/client/audio/midi_player.cpp` | TSF+TML synthesis (`TSF_IMPLEMENTATION`, `TML_IMPLEMENTATION`) |
| `include/client/audio/sfx_manager.h` | SFX manager interface |
| `src/client/audio/sfx_manager.cpp` | WAV loading + spatial playback (`DR_WAV_IMPLEMENTATION`) |
| `include/client/audio/audio_manager.h` | Top-level coordinator |
| `src/client/audio/audio_manager.cpp` | Wires everything together (`MA_IMPLEMENTATION`) |
