# Sound Implementation Plan for WillEQ

## Overview

This plan details the implementation of sound support in WillEQ, using original EverQuest sound files. The system will support both sound effects (WAV) and music (XMI/MIDI and MP3), working seamlessly with local display and RDP remote access.

**Audio Formats to Support:**
- **WAV** - Sound effects (combat, spells, ambient, UI)
- **XMI** - Extended MIDI music (original EQ format)
- **MP3** - Compressed music (later expansions)

**Integration Requirements:**
- Local playback via audio library
- RDP streaming via RDPSND virtual channel
- Headless operation (audio disabled gracefully)

## Current State

### Existing Code
- Opcodes defined in `include/client/eq.h`:
  ```cpp
  HC_OP_PlayMP3 = 0x26ab,   // S->C: Play music
  HC_OP_Sound = 0x541e,     // S->C: Play sound effect
  ```
- Stub handlers in `src/client/eq.cpp`:
  - `ZoneProcessPlayMP3()` - Currently logs only
  - `ZoneProcessSound()` - Currently logs only
- Logging module `MOD_AUDIO` already defined

### EQ Sound Files Structure
```
/EverQuestP1999/
├── sounds/                    # WAV effects (~1000+ files)
│   ├── {race}_atk.wav        # Attack sounds by race
│   ├── {race}_dam.wav        # Damage sounds
│   ├── {race}_dth.wav        # Death sounds
│   ├── Swing.WAV, Kick1.WAV  # Combat
│   ├── SpelCast.WAV          # Spell casting
│   └── ...
├── *.xmi                      # MIDI zone music (~60 files)
├── *.mp3                      # MP3 zone music (~80 files)
├── SoundAssets.txt           # Sound ID -> filename mapping
├── {zone}_sounds.eff         # Zone spatial sound metadata
└── eqtheme.mp3               # UI theme music
```

## Implementation Phases

### Phase 1: Audio Library Integration and Core Architecture ✅ COMPLETED

**Goal:** Set up audio subsystem with library that supports all required formats.

**Library Selection: OpenAL-Soft + libsndfile + FluidSynth + minimp3**
- **OpenAL-Soft** - Cross-platform 3D audio, good for positional sound effects
- **libsndfile** - WAV file loading
- **FluidSynth** - MIDI/XMI playback with SoundFont support (optional)
- **minimp3** - Lightweight MP3 decoding (header-only)

**Files created:**
```
include/client/audio/
├── audio_manager.h           # Main audio system class
├── sound_buffer.h            # OpenAL buffer wrapper
├── music_player.h            # Music playback (XMI/MP3/WAV)
├── sound_assets.h            # SoundAssets.txt parser
└── xmi_decoder.h             # XMI to MIDI conversion (with FluidSynth)

src/client/audio/
├── audio_manager.cpp         # Core audio management, source pooling
├── sound_buffer.cpp          # WAV loading via libsndfile
├── music_player.cpp          # Streaming playback with fading
├── sound_assets.cpp          # Sound ID to filename mapping
└── xmi_decoder.cpp           # XMI format conversion (with FluidSynth)

third_party/minimp3/
├── minimp3.h                 # Public domain MP3 decoder
└── minimp3_ex.h              # Extended API
```

**CMake additions (in CMakeLists.txt):**
- Audio library detection via pkg-config (openal, sndfile, fluidsynth)
- `WITH_AUDIO` option (defaults ON if OpenAL+libsndfile found)
- `WITH_FLUIDSYNTH` definition when FluidSynth available
- AUDIO_SOURCES list conditionally compiled
- Include directory for third_party/minimp3
- Link libraries for audio

**AudioManager features implemented:**
- OpenAL device/context initialization
- Source pool (32 sources) for concurrent sounds
- Sound buffer caching by filename
- Music player with streaming and fading
- 3D listener position support
- Volume controls (master, music, effects)
- RDP audio output callback hook

**Verified:**
- Build succeeds with `WITH_AUDIO=ON` (OpenAL + libsndfile detected)
- Build succeeds with `WITH_AUDIO=OFF` (no regressions)
- FluidSynth optional (MIDI/XMI disabled if not found)

---

### Phase 2: Sound Asset Loading ✅ COMPLETED

**Goal:** Load and parse EQ sound files and mappings.

**SoundAssets.txt Parser:**
- Format: `ID^{volume}^{filename}` or `ID^{filename}`
- Build lookup table: sound_id -> filename
- Handle special cases (Unknown, references)

**WAV Loading:**
- Load from `sounds/` directory
- Cache frequently used effects (combat, spells)
- Support lazy loading for less common sounds

**Sound effect categories:**
```cpp
enum class SoundCategory {
    Combat,      // Swings, hits, blocks
    Spell,       // Casting, effects
    Creature,    // NPC sounds by race
    Environment, // Ambient, weather
    UI,          // Button clicks, notifications
    Movement     // Footsteps, jumping
};
```

**Files created:**
```
include/client/audio/
└── sound_assets.h            # SoundAssets.txt parser (enhanced with forEach/getAllSoundIds)

src/client/audio/
└── sound_assets.cpp

tests/
└── test_sound_assets.cpp     # Unit tests for parsing and loading
```

**Implementation details:**
- SoundAssets.txt format parsed: `ID^{filename}` (2165 entries loaded)
- Sound ID to filename mapping integrated into AudioManager
- AudioManager::loadSoundAssets() populates soundIdMap_ from SoundAssets.txt
- AudioManager::preloadSound() and preloadCommonSounds() for eager loading
- Common combat/spell/player sound IDs preloaded (Swing, Kick, Punch, Spell, LevelUp, etc.)
- SoundBuffer properly handles WAV loading via libsndfile
- Tests verify:
  - SoundAssets.txt parsing (7 tests pass)
  - Known sound ID lookup (Swing=118, LevelUp=139, SpelCast=108)
  - "Unknown" entries excluded (Sound ID 1)
  - forEach iteration and getAllSoundIds()
  - WAV loading (skipped if no audio device)
  - AudioManager integration (skipped if no audio device)

---

### Phase 3: XMI/MIDI Music Support ✅ COMPLETED

**Goal:** Play original EQ MIDI music files.

**XMI Format:**
- Extended MIDI format used by EQ
- Contains tempo, instrument mappings
- Needs conversion to standard MIDI for playback

**Implementation details:**
- XmiDecoder class converts XMI to standard MIDI format
- Decoder works independently of FluidSynth (format conversion only)
- FluidSynth integration for MIDI synthesis (renders MIDI to PCM audio)
- MusicPlayer::loadXMI() decodes XMI and renders via FluidSynth

**Files modified:**
```
include/client/audio/xmi_decoder.h    # Removed WITH_FLUIDSYNTH guard
src/client/audio/xmi_decoder.cpp      # Removed WITH_FLUIDSYNTH guard
src/client/audio/music_player.cpp     # Improved error messages
CMakeLists.txt                        # Always include xmi_decoder.cpp with audio
```

**Files created:**
```
tests/test_xmi_decoder.cpp            # XMI decoder unit tests
```

**Test results:**
- 11 tests pass
- 100% XMI decode success rate (82/82 EQ music files)
- All XMI files produce valid MIDI output with proper headers

**FluidSynth requirement:**
- XMI decoding works without FluidSynth
- Actual MIDI playback requires FluidSynth + SoundFont
- Install: `sudo apt-get install libfluidsynth-dev`
- SoundFont options: FluidR3_GM.sf2, GeneralUser_GS.sf2

---

### Phase 4: Zone Music System ✅ COMPLETED

**Goal:** Automatically play zone-appropriate music on zone changes.

**Zone Music Mapping:**
- Primary: Check for `{zonename}.xmi`
- Fallback: Check for `{zonename}.mp3`
- Default: Silence or generic ambient

**Implementation details:**
- Integrated AudioManager into EverQuest class
- Audio initialization happens after graphics init (uses EQ client path)
- onZoneChange() called automatically when entering a new zone
- Zone music lookup is case-insensitive
- 82 XMI files + 78 MP3 files detected in EQ client directory

**Files modified:**
```
include/client/eq.h           # Added AudioManager forward decl and member
src/client/eq.cpp             # Added InitializeAudio, ShutdownAudio,
                              # zone change hook, audio include
```

**Files created:**
```
tests/test_zone_music.cpp     # Zone music system tests
```

**Integration points:**
- `InitGraphics()` calls `InitializeAudio()` after graphics init
- `~EverQuest()` calls `ShutdownAudio()` during cleanup
- Zone entry (line ~2850) calls `m_audio_manager->onZoneChange()`

**Test results:**
- 8 tests (4 pass, 4 skipped on headless)
- Zone mapping tests verify classic/dungeon zones have music
- Case-insensitive zone lookup verified
- Music file discovery: 82 XMI + 78 MP3

---

### Phase 5: Sound Effect Playback ✅ COMPLETED

**Goal:** Play sound effects triggered by game events.

**Server-triggered sounds:**
- `ZoneProcessSound()` handler parses sound ID and optional position
- `ZoneProcessPlayMP3()` handler for MP3 playback triggers
- Sound IDs mapped to audio files via SoundAssets.txt

**Client-triggered sounds:**
- Combat: Hit/miss sounds in `ZoneProcessDamage()`
- Spells: Cast sounds in `ZoneProcessBeginCast()`
- Death: Death sounds in `ZoneProcessDeath()`
- Helper methods: `PlaySound()`, `PlaySoundAt()`, `PlayCombatSound()`, `PlaySpellSound()`, `PlayUISound()`

**Implementation details:**
- Sound effects integrated into packet handlers with position support
- 3D positional audio using entity coordinates
- Sound ID constants defined in `sound_assets.h`:
  - MELEE_SWING, MELEE_HIT, MELEE_MISS, KICK, PUNCH
  - SPELL_CAST, SPELL_FIZZLE
  - LEVEL_UP, DEATH, TELEPORT
  - WATER_IN, WATER_OUT
  - BUTTON_CLICK, OPEN_WINDOW, CLOSE_WINDOW

**Files modified:**
```
src/client/eq.cpp        # ZoneProcessDamage, ZoneProcessBeginCast,
                         # ZoneProcessDeath, ZoneProcessSound,
                         # ZoneProcessPlayMP3 with audio integration
```

**Files created:**
```
tests/test_sound_effects.cpp  # Sound effect unit tests (15 tests)
```

**Test results:**
- 8 tests pass, 7 skip on headless (require audio device)
- Sound ID constants validated
- Creature sound file detection verified

---

### Phase 6: Spatial/3D Audio ✅ COMPLETED

**Goal:** Position sounds in 3D space for immersion.

**OpenAL 3D Audio Configuration:**
- Distance model: `AL_INVERSE_DISTANCE_CLAMPED`
- Reference distance: 50 units (full volume within this range)
- Max distance: 500 units (sound inaudible beyond)
- Rolloff factor: 1.0 (standard attenuation)
- Doppler effect: Disabled (to avoid artifacts)

**Listener Position Updates:**
- `getCameraTransform()` method added to IrrlichtRenderer
- Listener position updated every frame from camera transform
- Updates position, forward direction, and up vector

**Files modified:**
```
src/client/audio/audio_manager.cpp      # Distance model config, source attenuation
include/client/graphics/irrlicht_renderer.h   # getCameraTransform() declaration
src/client/graphics/irrlicht_renderer.cpp     # getCameraTransform() implementation
src/client/eq.cpp                        # Listener update in UpdateGraphics()
```

**Files created:**
```
tests/test_spatial_audio.cpp             # Spatial audio unit tests (10 tests)
```

**Distance Attenuation Formula:**
```
gain = ref_dist / (ref_dist + rolloff * (distance - ref_dist))
```
- At 50 units: gain = 1.0 (full volume)
- At 100 units: gain = 0.5 (half volume)
- At 500 units: gain = 0.1 (nearly silent)

**Integration with Renderer:**
- Camera position/orientation retrieved via `getCameraTransform()`
- Listener position updated after each `processFrame()` call
- Sound sources positioned at entity world coordinates

**Test results:**
- 2 tests pass (distance model math verification)
- 8 tests skip on headless (require audio device)

---

### Phase 7: RDP Audio Streaming (RDPSND) ✅ COMPLETED

**Goal:** Stream audio to RDP clients via RDPSND virtual channel.

**RDPSND Channel Implementation:**
- FreeRDP RDPSND server context per peer
- PCM audio format negotiation with clients
- Supports 44.1kHz/22.05kHz, stereo/mono, 16-bit
- Automatic format selection (prefers stereo 44.1kHz)

**RDPServer Audio API:**
```cpp
void sendAudioSamples(const int16_t* samples, size_t frameCount,
                      uint32_t sampleRate, uint8_t channels);
bool isAudioReady() const;
void setAudioEnabled(bool enabled);
```

**OpenAL Soft Loopback Device Support:**
For headless servers without audio hardware, the AudioManager uses OpenAL Soft's
loopback device to render audio to a buffer that can be streamed to RDP clients.

- `alcLoopbackOpenDeviceSOFT()` - Creates virtual audio device (no hardware needed)
- `alcRenderSamplesSOFT()` - Renders mixed audio to PCM buffer
- Automatic fallback: tries hardware device first, falls back to loopback
- Force loopback mode: `AudioManager::initialize(eqPath, true)`

**Loopback Mode Features:**
- No audio hardware required
- Dedicated render thread captures mixed audio at ~23ms intervals
- 44.1kHz stereo 16-bit PCM output
- All OpenAL audio (music + sound effects) mixed and captured
- Seamless integration with RDP streaming callback

**Files modified:**
```
include/client/audio/audio_manager.h          # Loopback mode, AL/alext.h, render thread
src/client/audio/audio_manager.cpp            # initializeLoopbackDevice(), loopback thread
include/client/graphics/rdp/rdp_server.h      # Audio methods, RDPSND includes
include/client/graphics/rdp/rdp_peer_context.h # RdpsndServerContext per peer
src/client/graphics/rdp/rdp_server.cpp        # RDPSND init, format negotiation, sendAudioSamples
src/client/graphics/rdp/rdp_peer_context.cpp  # Audio context cleanup
include/client/graphics/irrlicht_renderer.h   # getRDPServer() accessor
src/client/eq.cpp                             # Audio callback integration
CMakeLists.txt                                # Link freerdp-server3 library
tests/CMakeLists.txt                          # Link freerdp-server3 for tests
tests/test_spatial_audio.cpp                  # Loopback mode tests (9 new tests)
```

**Audio Format Support:**
- Server-supported formats:
  - 44.1kHz stereo 16-bit (preferred)
  - 22.05kHz stereo 16-bit
  - 44.1kHz mono 16-bit
  - 22.05kHz mono 16-bit
- Format negotiation selects best match from client capabilities

**Integration with AudioManager:**
- AudioOutputCallback connects to RDPServer::sendAudioSamples()
- Callback configured during InitializeAudio() when RDP is active
- Audio data routed to all connected RDP clients
- In loopback mode, render thread continuously captures mixed audio

**Test Results:**
- 9 loopback-specific tests added to test_spatial_audio.cpp
- All loopback tests pass (extension detection, device creation, format support,
  context creation, sample rendering, AudioManager loopback mode, callback
  verification, sound playback in loopback mode)

---

### Phase 7b: PFS Archive Sound Loading ✅ COMPLETED

**Goal:** Load sound effects from EQ's snd*.pfs archive files.

**Background:**
EverQuest stores most sound effects inside PFS archives (snd1.pfs through snd17.pfs),
not as loose files. The common combat and spell sounds are in snd2.pfs:
- swing.wav, swingbig.wav (melee attacks)
- kick1.wav, kickhit.wav, rndkick.wav (kicks)
- punch1.wav, punchhit.wav (punches)
- spell_1.wav through spell_5.wav (spell casting)
- levelup.wav (level up fanfare)

**Implementation:**

1. **PFS Archive Index:**
   - On initialization, scans EQ path for snd*.pfs files
   - Builds lowercase filename -> archive path mapping
   - Caches opened archives for efficiency

2. **Sound Loading from Archives:**
   - `loadSound()` tries filesystem first, then PFS archives
   - `loadSoundFromPfs()` extracts WAV data from archives
   - `loadSoundDataFromPfs()` handles archive access and extraction

3. **Integration with Existing PFS Loader:**
   - Reuses `EQT::Graphics::PfsArchive` class
   - Extracts WAV data to memory buffer
   - Passes to `SoundBuffer::loadFromMemory()` (libsndfile virtual I/O)

**Files modified:**
```
include/client/audio/audio_manager.h      # PFS archive index members, new methods
src/client/audio/audio_manager.cpp        # scanPfsArchives(), loadSoundFromPfs(), loadSoundDataFromPfs()
tests/CMakeLists.txt                      # Added pfs.cpp and ZLIB to audio test targets
```

**Note:** `SoundBuffer::loadFromMemory()` already existed with full libsndfile
virtual I/O support - no changes needed.

**Test Results:**
- All 805 tests pass (100%)
- `PreloadCommonSounds` test now passes (was failing before)
- Sound files successfully loaded from snd*.pfs archives

---

### Phase 8: Configuration System ✅ COMPLETED

**Goal:** User-configurable audio settings.

**Implementation:**

1. **Command line options** (in `src/client/main.cpp`):
   - `--no-audio` or `-na` - Disable all audio
   - `--audio-volume <0-100>` - Master volume (default: 100)
   - `--music-volume <0-100>` - Music volume (default: 70)
   - `--effects-volume <0-100>` - Sound effects volume (default: 100)
   - `--soundfont <path>` - Path to SoundFont for MIDI

2. **JSON configuration** (parsed from config file):
   ```json
   {
       "audio": {
           "enabled": true,
           "master_volume": 80,
           "music_volume": 60,
           "effects_volume": 100,
           "soundfont": "/path/to/soundfont.sf2"
       }
   }
   ```

3. **In-game slash commands** (in `src/client/eq.cpp` RegisterCommands()):
   - `/music [on|off]` - Toggle or set music playback
   - `/sound [on|off]` - Toggle or set sound effects
   - `/volume <0-100>` (alias: `/vol`) - Show or set master volume
   - `/musicvolume <0-100>` (alias: `/mvol`) - Show or set music volume
   - `/effectsvolume <0-100>` (aliases: `/evol`, `/sfxvol`) - Show or set effects volume

**Files modified:**
```
include/client/eq.h              # Audio configuration members and setters
src/client/eq.cpp                # InitializeAudio() with config, slash commands
src/client/main.cpp              # Command line parsing, JSON config parsing
```

**Configuration flow:**
1. Default values set in EverQuest class members
2. JSON config overrides defaults (if present)
3. Command line options override JSON config
4. Settings applied when InitializeAudio() is called
5. In-game commands allow runtime adjustment

**Test Results:**
- All 805 tests pass (100%)
- Build succeeds with audio enabled
- Configuration options correctly parsed and applied

---

### Phase 9: Testing ✅ COMPLETED

**Test files (84 audio-related tests):**
```
tests/
├── test_sound_assets.cpp       # SoundAssets parsing, SoundBuffer, AudioManager (22 tests)
├── test_xmi_decoder.cpp        # XMI to MIDI conversion (11 tests)
├── test_zone_music.cpp         # Zone music, MusicPlayer, audio config (19 tests)
├── test_sound_effects.cpp      # Sound ID constants, sound effects (15 tests)
└── test_spatial_audio.cpp      # 3D audio, loopback mode, distance model (17 tests)
```

**Test coverage implemented:**

1. **Audio Initialization/Shutdown:**
   - AudioManagerTest.InitializeLoadsAssets
   - AudioManagerTest.ShutdownAndReinitialize
   - AudioManagerTest.MultipleShutdownsSafe
   - AudioManagerTest.InitializeWithInvalidPath
   - MusicPlayerTest.InitializeWithoutSoundFont
   - MusicPlayerTest.Shutdown

2. **Sound Asset Loading and Caching:**
   - SoundAssetsTest (7 tests): Parsing, finding sounds, iteration
   - SoundBufferTest (4 tests): WAV loading, error handling, move semantics
   - AudioManagerTest.PreloadCommonSounds
   - AudioManagerTest.PlaySoundByName
   - AudioManagerTest.StopAllSounds

3. **XMI Decoding Accuracy:**
   - XmiDecoderTest (3 tests): Empty data, invalid magic, small data
   - XmiFileTest (8 tests): Real files, specific zones, all XMI files, MIDI validation

4. **Volume Controls:**
   - AudioManagerTest.VolumeControls (clamping to 0.0-1.0)
   - AudioManagerTest.AudioEnableDisable
   - MusicPlayerTest.VolumeControls
   - SoundEffectAudioTest.VolumeControlsAffectSounds
   - ZoneMusicAudioTest.VolumeControlsDuringZoneChange
   - AudioConfigTest (4 tests): Percent/float conversion, clamping

5. **Zone Music Transitions:**
   - ZoneMusicMappingTest (4 tests): Classic zones, dungeon zones, case-insensitive
   - ZoneMusicAudioTest (4 tests): Zone change, volume, stop, disable

6. **3D Spatial Audio:**
   - DistanceModelTest (2 tests): Constants, inverse distance formula
   - SpatialAudioTest (8 tests): Listener position, orientation, positions, distance model
   - SoundEffectAudioTest.SetListenerPosition

7. **RDP Audio (Loopback Mode):**
   - LoopbackModeTest (6 tests): Extension, function pointers, device, format, context, render
   - LoopbackAudioManagerTest (3 tests): Force loopback, callback, playback

8. **MusicPlayer Specific:**
   - MusicPlayerTest (7 tests): Initialize, volume, state, stop, pause/resume, nonexistent file, shutdown

**Test Results:**
- All 821 tests pass (100%)
- 84 audio-specific tests covering all major components
- Tests gracefully skip when audio device unavailable (CI/headless environments)

**Bug Fix During Testing:**
- Fixed `AudioManager::scanPfsArchives()` to handle non-existent paths gracefully
  (was throwing filesystem exception, now returns early with debug log)

---

### Phase 10: Documentation ✅ COMPLETED

**Files updated:**

1. **CLAUDE.md** - Added audio documentation:
   - Dependencies section: `libopenal-dev`, `libsndfile1-dev`, `libfluidsynth-dev`
   - Architecture section: Audio System components
   - Configuration section: Audio JSON config and command line options
   - Slash Commands section: Audio commands (`/music`, `/sound`, `/volume`, etc.)
   - Test Organization section: Audio test files

2. **docs/audio_implementation_guide.md** - Comprehensive technical guide:
   - Overview and architecture diagrams
   - Dependency installation (Ubuntu/Fedora)
   - SoundFont setup for MIDI/XMI playback
   - Configuration options (CLI, JSON, in-game commands)
   - 3D spatial audio implementation
   - RDP audio streaming (loopback mode)
   - Troubleshooting guide
   - API reference
   - Build configuration

**Documentation coverage:**
- ✅ Audio library installation instructions
- ✅ SoundFont setup for MIDI/XMI music
- ✅ All configuration options documented
- ✅ Troubleshooting guide with common issues
- ✅ API reference for AudioManager
- ✅ Test coverage documentation

---

## Implementation Order

1. **Phase 1**: Audio library integration (foundation) ✅
2. **Phase 2**: Sound asset loading ✅
3. **Phase 3**: XMI/MIDI music support ✅
4. **Phase 4**: Zone music system ✅
5. **Phase 5**: Sound effect playback ✅
6. **Phase 6**: 3D spatial audio ✅
7. **Phase 7**: RDP audio streaming (with loopback support) ✅
8. **Phase 7b**: PFS archive sound loading ✅
9. **Phase 8**: Configuration system ✅
10. **Phase 9**: Testing ✅
11. **Phase 10**: Documentation ✅

**🎉 SOUND IMPLEMENTATION COMPLETE 🎉**

---

## Dependencies

**Required packages (Ubuntu):**
```bash
# OpenAL for audio playback
sudo apt-get install libopenal-dev

# libsndfile for WAV loading
sudo apt-get install libsndfile1-dev

# FluidSynth for MIDI playback
sudo apt-get install libfluidsynth-dev

# Optional: SDL2_mixer as alternative
sudo apt-get install libsdl2-mixer-dev
```

**SoundFont for MIDI:**
- FluidR3_GM.sf2 (free, ~140MB)
- GeneralUser_GS.sf2 (free, ~30MB)
- Or custom EQ-style SoundFont

---

## File Summary

**New files (estimated 15):**
```
include/client/audio/
├── audio_manager.h
├── sound_effect.h
├── music_player.h
├── audio_buffer.h
├── sound_assets.h
└── xmi_decoder.h

src/client/audio/
├── audio_manager.cpp
├── sound_effect.cpp
├── music_player.cpp
├── audio_buffer.cpp
├── sound_assets.cpp
└── xmi_decoder.cpp

tests/
├── test_audio_manager.cpp
├── test_sound_assets.cpp
└── test_xmi_decoder.cpp
```

**Modified files (estimated 8):**
- `CMakeLists.txt` - Audio library detection
- `src/client/main.cpp` - Audio config options
- `src/client/eq.cpp` - Implement sound handlers
- `include/client/eq.h` - AudioManager integration
- `src/client/graphics/irrlicht_renderer.cpp` - Listener position updates
- `src/client/graphics/rdp/rdp_server.cpp` - RDPSND channel
- `include/client/graphics/rdp/rdp_server.h` - Audio methods
- `CLAUDE.md` - Documentation

---

## Risk Considerations

1. **XMI Format Complexity**: May need third-party XMI decoder or custom implementation
2. **SoundFont Quality**: MIDI quality depends heavily on SoundFont choice
3. **Performance**: Audio mixing and encoding for RDP adds CPU overhead
4. **Thread Safety**: Audio often runs on separate thread, needs synchronization
5. **Platform Differences**: OpenAL behavior may vary across systems

---

## Success Criteria

1. WAV sound effects play correctly
2. XMI music plays with acceptable quality
3. MP3 music plays as fallback
4. Zone music changes on zone transitions
5. 3D positional audio works for nearby sounds
6. Audio streams to RDP clients via RDPSND
7. Volume controls work correctly
8. Audio can be disabled without crashes
9. No audio when running headless (--no-audio)
10. Performance impact is minimal (<5% CPU)

---

## Future Enhancements (Out of Scope)

1. Environmental audio effects (reverb, echo)
2. Dynamic music system (combat vs exploration)
3. Custom sound packs
4. Voice chat integration
5. Audio recording/streaming
