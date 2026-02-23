# WillEQ Audio Implementation Guide

This document describes the audio system implementation in WillEQ, including architecture, dependencies, configuration, and troubleshooting.

## Overview

WillEQ implements a complete audio system for EverQuest Titanium client compatibility:

- **Sound Effects**: Combat sounds, UI feedback, environmental audio
- **Zone Music**: XMI/MIDI and MP3 playback with zone-based selection
- **3D Spatial Audio**: Positional sound sources with distance attenuation
- **RDP Audio Streaming**: Loopback rendering for remote desktop clients

## Dependencies

### Required Libraries

```bash
# Ubuntu/Debian
sudo apt-get install libopenal-dev libsndfile1-dev

# Fedora/RHEL
sudo dnf install openal-soft-devel libsndfile-devel
```

- **OpenAL Soft** - 3D audio rendering and spatial positioning
- **libsndfile** - WAV file loading and format conversion

### Optional Libraries

```bash
# Ubuntu/Debian
sudo apt-get install libfluidsynth-dev

# Fedora/RHEL
sudo dnf install fluidsynth-devel
```

- **FluidSynth** - MIDI/XMI music playback via SoundFont synthesis

### SoundFont Setup (for MIDI/XMI Music)

EverQuest's zone music uses XMI format (MIDI variant). FluidSynth requires a SoundFont file for synthesis.

**Installing a SoundFont:**

```bash
# Ubuntu - install FluidR3 GM SoundFont
sudo apt-get install fluid-soundfont-gm

# The SoundFont is typically installed to:
# /usr/share/sounds/sf2/FluidR3_GM.sf2
```

**Alternative SoundFonts:**

- [GeneralUser GS](https://schristiancollins.com/generaluser.php) - Free, high quality
- [Arachno SoundFont](https://www.arachnosoft.com/main/soundfont.php) - Free, good GM compatibility
- [SGM-V2.01](https://musical-artifacts.com/artifacts/855) - Large, excellent quality

**Configuring the SoundFont:**

```bash
# Via command line
./build/bin/willeq -c willeq.json --soundfont /usr/share/sounds/sf2/FluidR3_GM.sf2

# Via JSON config
{
    "audio": {
        "soundfont": "/usr/share/sounds/sf2/FluidR3_GM.sf2"
    }
}
```

## Architecture

### Component Overview

```
AudioManager
├── SoundAssets          # Sound ID -> filename mapping (SoundAssets.txt)
├── SoundBuffer          # OpenAL buffer management
├── MusicPlayer          # XMI/MIDI/MP3 streaming
│   ├── XmiDecoder       # XMI -> MIDI conversion
│   └── FluidSynth       # MIDI synthesis (optional)
└── OpenAL Context       # 3D audio rendering
    └── Loopback Mode    # For RDP streaming
```

### Key Classes

**AudioManager** (`include/client/audio/audio_manager.h`)
- Central audio system controller
- Manages sound loading, caching, and playback
- Handles listener (camera) position for 3D audio
- Supports loopback mode for RDP audio streaming

**SoundAssets** (`include/client/audio/sound_assets.h`)
- Parses EQ's `SoundAssets.txt` file
- Maps sound IDs to filenames with volume multipliers
- Provides lookup by ID or iteration over all sounds

**SoundBuffer** (`include/client/audio/sound_buffer.h`)
- Wraps OpenAL buffer objects
- Loads WAV files from filesystem or PFS archives
- Supports virtual I/O for memory-based loading

**MusicPlayer** (`include/client/audio/music_player.h`)
- Streaming playback for music files
- Supports XMI (via FluidSynth), MP3, and WAV formats
- Handles looping, fading, pause/resume

**XmiDecoder** (`include/client/audio/xmi_decoder.h`)
- Converts EQ's XMI format to standard MIDI
- Handles timing conversion and track merging
- Required for zone music playback

### Sound Loading Pipeline

1. **Sound ID Request** → `AudioManager::playSound(soundId)`
2. **Asset Lookup** → `SoundAssets::getFilename(soundId)`
3. **Cache Check** → Check `bufferCache_` for loaded buffer
4. **File Loading**:
   - Try filesystem: `{eqPath}/sounds/{filename}`
   - Try PFS archives: `snd*.pfs` files
5. **Buffer Creation** → OpenAL buffer from WAV data
6. **Source Playback** → Positional or non-positional source

### PFS Archive Sound Loading

Sound files are stored in PFS archives (`snd1.pfs` through `snd17.pfs`):

```cpp
// AudioManager scans archives on initialization
void AudioManager::scanPfsArchives() {
    // Builds index: lowercase filename -> archive path
    // e.g., "swing.wav" -> "/path/to/EQ/snd2.pfs"
}

// Loading tries filesystem first, then PFS
std::shared_ptr<SoundBuffer> AudioManager::loadSound(const std::string& filename) {
    // 1. Try filesystem
    // 2. Try PFS archives via pfsFileIndex_
}
```

## Configuration

### Command Line Options

| Option | Description | Default |
|--------|-------------|---------|
| `--no-audio`, `-na` | Disable audio system | Enabled |
| `--audio-volume <0-100>` | Master volume | 100 |
| `--music-volume <0-100>` | Music volume | 70 |
| `--effects-volume <0-100>` | Sound effects volume | 100 |
| `--soundfont <path>` | SoundFont for MIDI | None |

### JSON Configuration

```json
{
    "audio": {
        "enabled": true,
        "master_volume": 100,
        "music_volume": 70,
        "effects_volume": 100,
        "soundfont": "/path/to/soundfont.sf2"
    }
}
```

### In-Game Commands

| Command | Description |
|---------|-------------|
| `/music [on\|off]` | Toggle music playback |
| `/sound [on\|off]` | Toggle sound effects |
| `/volume [0-100]` | Show/set master volume |
| `/musicvolume [0-100]` | Show/set music volume |
| `/effectsvolume [0-100]` | Show/set effects volume |

Aliases: `/vol`, `/mvol`, `/evol`, `/sfxvol`

## 3D Spatial Audio

### Distance Model

WillEQ uses OpenAL's inverse distance clamped model:

```
gain = reference_distance / (reference_distance + rolloff * (distance - reference_distance))
```

Where:
- `reference_distance` = 50.0 (full volume within this range)
- `max_distance` = 500.0 (inaudible beyond this range)
- `rolloff` = 1.0 (linear attenuation)

### Listener Updates

The listener position (camera/player) must be updated each frame:

```cpp
// In EverQuest render loop
m_audio_manager->setListenerPosition(
    cameraPosition,    // glm::vec3 position
    cameraForward,     // glm::vec3 forward direction
    cameraUp           // glm::vec3 up direction
);
```

### Coordinate Conversion

EQ uses Z-up coordinates, OpenAL uses Y-up:
- EQ: (x, y, z) where Z is up
- OpenAL: (x, y, z) where Y is up
- Conversion: `openal_pos = glm::vec3(eq_x, eq_z, eq_y)`

## RDP Audio Streaming

### Loopback Mode

For RDP clients without local audio devices, AudioManager supports OpenAL Soft's loopback rendering:

```cpp
// Force loopback mode for RDP
m_audio_manager->setForceLoopbackMode(true);
m_audio_manager->setLoopbackCallback([](const int16_t* samples, size_t count) {
    // Send samples to RDP audio channel
    rdp_server->sendAudioData(samples, count);
});
```

### Audio Format

- Sample Rate: 44100 Hz
- Channels: Stereo (2)
- Format: 16-bit signed PCM
- Buffer Size: 4096 samples per callback

## Troubleshooting

### No Sound Output

1. **Check audio device availability:**
   ```bash
   aplay -l  # List ALSA devices
   ```

2. **Verify OpenAL configuration:**
   ```bash
   # Check ~/.config/alsoft.conf or /etc/openal/alsoft.conf
   [general]
   drivers = pulse,alsa,oss
   ```

3. **Test with simple OpenAL app:**
   ```bash
   sudo apt-get install openal-info
   openal-info
   ```

### No Music (XMI files)

1. **Verify FluidSynth is installed:**
   ```bash
   pkg-config --exists fluidsynth && echo "FluidSynth found"
   ```

2. **Check SoundFont path:**
   ```bash
   ls -la /usr/share/sounds/sf2/
   ```

3. **Verify XMI files exist:**
   ```bash
   ls /path/to/EverQuest/*.xmi | head -5
   ```

### Sounds Not Loading

1. **Check SoundAssets.txt exists:**
   ```bash
   ls -la /path/to/EverQuest/SoundAssets.txt
   ```

2. **Verify PFS archives:**
   ```bash
   ls /path/to/EverQuest/snd*.pfs
   ```

3. **Enable debug logging:**
   ```bash
   ./build/bin/willeq -c willeq.json --log-level=DEBUG --log-module=AUDIO
   ```

### RDP Audio Issues

1. **Verify loopback extension:**
   ```bash
   ./build/bin/test_spatial_audio --gtest_filter="*Loopback*"
   ```

2. **Check RDP client audio settings:**
   - Windows: Remote Desktop Connection → Local Resources → Remote Audio
   - xfreerdp: `/sound:sys:alsa` or `/sound:sys:pulse`

### Performance Issues

1. **Reduce concurrent sounds:**
   - AudioManager limits to 32 simultaneous sources
   - Oldest sounds are stopped when limit reached

2. **Preload common sounds:**
   ```cpp
   m_audio_manager->preloadCommonSounds();
   ```

3. **Use appropriate distance model:**
   - Sounds beyond `max_distance` are culled automatically

## Testing

### Unit Tests

```bash
cd build
ctest -R "audio|sound|xmi|music|spatial" --output-on-failure
```

### Test Coverage

- **test_sound_assets.cpp**: Asset loading, caching, volume controls
- **test_xmi_decoder.cpp**: XMI to MIDI conversion accuracy
- **test_zone_music.cpp**: Zone-based music transitions
- **test_sound_effects.cpp**: Sound playback, positioning
- **test_spatial_audio.cpp**: 3D audio, loopback mode

### Manual Testing

```bash
# Test with debug output
./build/bin/willeq -c willeq.json --log-level=DEBUG --log-module=AUDIO

# Test specific volumes
./build/bin/willeq -c willeq.json --audio-volume 50 --music-volume 100

# Test without audio (verify no crashes)
./build/bin/willeq -c willeq.json --no-audio
```

## API Reference

### AudioManager

```cpp
class AudioManager {
public:
    // Initialization
    bool initialize(const std::string& eqPath);
    void shutdown();
    bool isInitialized() const;

    // Sound playback
    void playSound(uint32_t soundId);
    void playSound(uint32_t soundId, const glm::vec3& position);
    void playSoundByName(const std::string& filename);
    void stopAllSounds();

    // Music control
    void onZoneChange(const std::string& zoneName);
    void playMusic(const std::string& filepath, bool loop = true);
    void stopMusic(float fadeSeconds = 0.0f);
    void pauseMusic();
    void resumeMusic();
    bool isMusicPlaying() const;

    // Volume control (0.0 - 1.0)
    void setMasterVolume(float volume);
    void setMusicVolume(float volume);
    void setEffectsVolume(float volume);
    float getMasterVolume() const;
    float getMusicVolume() const;
    float getEffectsVolume() const;

    // Audio enable/disable
    void setAudioEnabled(bool enabled);
    bool isAudioEnabled() const;

    // 3D audio
    void setListenerPosition(const glm::vec3& pos, const glm::vec3& forward, const glm::vec3& up);

    // RDP loopback
    void setForceLoopbackMode(bool enabled);
    void setLoopbackCallback(std::function<void(const int16_t*, size_t)> callback);
};
```

### Sound IDs

Common sound IDs from `SoundAssets.txt`:

| ID | Sound | Usage |
|----|-------|-------|
| 118 | Swing.WAV | Melee swing |
| 119 | Kick1.WAV | Kick attack |
| 120 | Punch1.WAV | Punch attack |
| 108 | SpelCast.WAV | Spell casting |
| 139 | LevelUp.WAV | Level gained |
| 140 | Death.WAV | Player death |

See `include/client/audio/sound_assets.h` for `SoundId` namespace constants.

## Build Configuration

### CMake Options

| Option | Description | Default |
|--------|-------------|---------|
| `WITH_AUDIO` | Enable audio system | ON |
| `WITH_FLUIDSYNTH` | Enable MIDI via FluidSynth | Auto-detect |

### Conditional Compilation

```cpp
#ifdef WITH_AUDIO
    // Audio code
#endif

#ifdef WITH_FLUIDSYNTH
    // FluidSynth-specific code
#endif
```

## Version History

- **Phase 1-2**: Audio library integration, sound asset loading
- **Phase 3-4**: XMI/MIDI music, zone music system
- **Phase 5-6**: Sound effect playback, 3D spatial audio
- **Phase 7**: RDP audio streaming with loopback support
- **Phase 7b**: PFS archive sound loading
- **Phase 8**: Configuration system (CLI, JSON, slash commands)
- **Phase 9**: Comprehensive test coverage (84 tests)
- **Phase 10**: Documentation (this guide)
