# Audio System

## Components

Core:
- `AudioManager` - Main audio manager, owns mixer/backend/sfx/midi subsystems
- `AudioMixer` - Software stereo PCM mixer (16 SFX + 2 music channels)
- `AudioBackend` - Output abstraction: MiniaudioBackend (direct) or RDPAudioBackend (streaming)
- `MidiPlayer` - TSF + TML MIDI/XMI synthesis (SoundFont-based)
- `SfxManager` - WAV loading (dr_wav) and spatial playback with LRU cache
- `SoundBuffer` - Decoded float PCM data (loaded via dr_wav from files or PFS archives)
- `SoundAssets` - Parses SoundAssets.txt for sound ID to filename mapping
- `MusicPlayer` - Streaming music playback for XMI/MIDI and MP3 files
- `XmiDecoder` - Converts EQ's XMI format to standard MIDI at runtime

Zone Audio:
- `EffLoader` - Parses zone_sounds.eff and zone_sndbnk.eff files for emitter data
- `ZoneSoundEmitter` - Positioned sound sources with day/night variants, cooldowns
- `ZoneAudioManager` - Manages all zone emitters, handles day/night transitions

Sound Categories:
- `PlayerSounds` - Race/gender-specific player sounds (death, hit, jump, drown)
- `CreatureSounds` - NPC race-based sounds (attack, damage, death, idle)
- `DoorSounds` - Door type sounds (metal, stone, wood, secret, mechanisms)
- `WeatherAudio` - Rain/wind loops, thunder, intensity-based volume
- `WaterSounds` - Water entry/exit, swimming, underwater ambient
- `UISounds` - Level up, UI interactions, notifications
- `CombatMusic` - Combat stinger XMI files (damage1.xmi, damage2.xmi)

Sound files are loaded from `snd*.pfs` archives and the `sounds/` directory in the EQ client.

## Dependencies

Header-only libraries: miniaudio.h (output), tsf.h + tml.h (MIDI synthesis), dr_wav.h (WAV loading)

## Configuration

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

Command line options:
```bash
--no-audio                    # Disable audio
--audio-volume 80             # Master volume (0-100)
--music-volume 50             # Music volume (0-100)
--effects-volume 100          # Effects volume (0-100)
--soundfont /path/to/sf2      # SoundFont for MIDI/XMI music
```

## Tests

Audio tests require `WITH_AUDIO` and skip if no audio device:
- `test_sound_assets.cpp` - SoundAssets parsing, SoundBuffer, AudioManager
- `test_xmi_decoder.cpp` - XMI to MIDI conversion
- `test_zone_music.cpp` - Zone music transitions, MusicPlayer
- `test_sound_effects.cpp` - Sound ID constants, sound effect playback
- `test_spatial_audio.cpp` - 3D spatial audio, loopback mode for RDP
- `test_eff_loader.cpp` - EFF file parsing (zone sound emitter config)
- `test_zone_sound_emitters.cpp` - Zone sound emitter system
- `test_day_night_audio.cpp` - Day/night audio transitions
- `test_player_sounds.cpp` - Player race/gender sound mapping
- `test_creature_sounds.cpp` - Creature/NPC race sound mapping
- `test_door_sounds.cpp` - Door and object sound types
- `test_weather_audio.cpp` - Weather and water sounds
- `test_ui_sounds.cpp` - UI sound mappings
- `test_combat_music.cpp` - Combat music stingers

## Tools

- `merge_sf2.py` - SoundFont merger utility for combining multiple SF2 files
