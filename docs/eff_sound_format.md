# EverQuest EFF Zone Sound Format Specification

## Overview

EverQuest zones use three files to define ambient sounds and music:
- `{zone}_sounds.eff` - Binary file containing 84-byte sound emitter records
- `{zone}_sndbnk.eff` - Text file mapping sound IDs to WAV filenames
- `{zone}.xmi` - Zone background music (MIDI format)

This document describes the binary format of Classic, Kunark, and Velious era zones.

## References

- **Eff2Emt**: Command-line converter by Shendare (CC0 license)
  - https://github.com/Shendare/Eff2Emt
- **EQEmulator Forums**: Original format documentation
  - https://www.eqemulator.org/forums/showthread.php?t=39747

---

## _sndbnk.eff Format (Text File)

The sound bank file is a text file with CRLF line endings containing sound name references organized into sections.

### Structure

```
EMIT
sound_name_1
sound_name_2
...
LOOP
ambient_sound_1
ambient_sound_2
...
```

### Sections

| Keyword | Description |
|---------|-------------|
| `EMIT` | Point-source sounds (torches, waterfalls, etc.) |
| `LOOP` | Ambient background loops (wind, night sounds, etc.) |
| `RAND` | Rarely used, treated as LOOP section |

### Example (gfaydark_sndbnk.eff)

```
EMIT
fire_lp
fire_lp
fire_lp
fire_lp
fire_lp
fire_lp
fire_lp
fire_lp
LOOP
wind_lp2
wind_lp4
darkwds1
darkwds2
night
```

Sound names reference WAV files in `snd*.pfs` archives (without extension).

---

## _sounds.eff Format (Binary File)

Contains 84-byte records with no header. File size is always a multiple of 84.

### Record Structure (84 bytes)

| Offset | Size | Type | Field Name | Description |
|--------|------|------|------------|-------------|
| 0 | 4 | int32 | UnkRef00 | Runtime reference (ignore) |
| 4 | 4 | int32 | UnkRef04 | Runtime reference (ignore) |
| 8 | 4 | int32 | Reserved | Reserved (always 0) |
| 12 | 4 | int32 | Sequence | Entry ID (unique per zone) |
| 16 | 4 | float | X | X position in zone |
| 20 | 4 | float | Y | Y position in zone |
| 24 | 4 | float | Z | Z position in zone |
| 28 | 4 | float | Radius | Activation/audible radius |
| 32 | 4 | int32 | Cooldown1 | Cooldown for sound 1 (ms) |
| 36 | 4 | int32 | Cooldown2 | Cooldown for sound 2 (ms) |
| 40 | 4 | int32 | RandomDelay | Random delay addition (ms) |
| 44 | 4 | int32 | Unk44 | Unknown |
| 48 | 4 | int32 | SoundID1 | Primary/day sound ID |
| 52 | 4 | int32 | SoundID2 | Secondary/night sound ID |
| 56 | 1 | byte | SoundType | Sound type (0-3) |
| 57 | 1 | byte | UnkPad57 | Padding |
| 58 | 1 | byte | UnkPad58 | Padding |
| 59 | 1 | byte | UnkPad59 | Padding |
| 60 | 4 | int32 | AsDistance | Volume as distance (type 2,3) |
| 64 | 4 | int32 | UnkRange64 | Unknown range parameter |
| 68 | 4 | int32 | FadeOutMS | Fade out time (milliseconds) |
| 72 | 4 | int32 | UnkRange72 | Unknown range parameter |
| 76 | 4 | int32 | FullVolRange | Full volume range |
| 80 | 4 | int32 | UnkRange80 | Unknown range parameter |

### Sound Types

| Type | Name | Description |
|------|------|-------------|
| 0 | Day/Night Constant | Sound effect with constant volume, different sounds for day/night |
| 1 | Background Music | Zone music (XMI or MP3), different tracks for day/night |
| 2 | Static Effect | Single sound effect, volume based on distance |
| 3 | Day/Night Distance | Sound effect with distance-based volume, different for day/night |

### Sound ID Mapping

Sound IDs in the binary file map to actual sound files through this system:

| ID Range | Source | Description |
|----------|--------|-------------|
| 0 | None | No sound |
| < 0 | mp3index.txt | MP3 music (abs value = line number in mp3index.txt) |
| 1-31 | sndbnk EMIT | EMIT section, 1-indexed (ID 1 = first EMIT entry) |
| 32-161 | Hardcoded | Global hardcoded sounds (see table below) |
| 162+ | sndbnk LOOP | LOOP section, offset by 161 (ID 162 = LOOP[0], 163 = LOOP[1]) |

### Hardcoded Sound IDs (32-161)

| ID | Sound File | Description |
|----|------------|-------------|
| 39 | death_me | Player death sound |
| 143 | thunder1 | Thunder variant 1 |
| 144 | thunder2 | Thunder variant 2 |
| 158 | wind_lp1 | Wind loop |
| 159 | rainloop | Rain loop |
| 160 | torch_lp | Torch crackling |
| 161 | watundlp | Underwater ambience |

Most IDs in the 32-161 range are undefined/unused.

---

## Sound Behavior by Type

### Type 0: Day/Night Constant Volume

- Uses both SoundID1 (day) and SoundID2 (night)
- Constant volume within activation radius
- Cooldown1/Cooldown2 control repeat timing
- RandomDelay adds randomness to repeats

### Type 1: Background Music

- SoundID1 = day music, SoundID2 = night music
- Positive IDs (1-31) reference XMI subsongs in `{zone}.xmi`
- Negative IDs reference MP3 files via mp3index.txt
- If SoundID1 == SoundID2, same music plays day and night
- Radius defines music transition zone

### Type 2: Static Effect

- Single sound from SoundID1 only
- Volume calculated from AsDistance field
- Volume = (3000 - AsDistance) / 3000
- Typical for positioned ambient sounds (waterfalls, etc.)

### Type 3: Day/Night Distance-Based

- Combines day/night variants with distance-based volume
- Uses both SoundID1 and SoundID2
- Volume calculated same as Type 2

---

## Volume Calculation

For types 2 and 3:
```
if (AsDistance < 0 || AsDistance > 3000):
    Volume = 0.0
else:
    Volume = (3000.0 - AsDistance) / 3000.0
```

For types 0 and 1: Constant volume (1.0) within radius.

---

## Fade Timing

- **FadeOutMS**: Time to fade out when leaving radius (milliseconds)
- **FadeInMS**: Not stored; derived as FadeOutMS / 2 (capped at 5000ms)

---

## Statistics (Classic/Kunark/Velious)

| Metric | Count |
|--------|-------|
| Zones with _sounds.eff | 163 |
| Total sound emitter records | ~10,850 |
| Unique sound names in sndbnk | 379 |
| Matched to snd*.pfs WAVs | 377 |

### Sound Type Distribution

| Type | Count | Percentage |
|------|-------|------------|
| 0 (Day/Night Constant) | 3,431 | 32% |
| 1 (Background Music) | 421 | 4% |
| 2 (Static Effect) | 6,486 | 60% |
| 3 (Day/Night Distance) | 511 | 5% |

---

## Implementation Notes for WillEQ

### Loading Zone Sounds

1. Load `{zone}_sndbnk.eff` and parse EMIT/LOOP sections
2. Load `{zone}_sounds.eff` and read 84-byte records
3. For each record:
   - Map SoundID1/SoundID2 to WAV filenames
   - Create positioned audio source at (X, Y, Z)
   - Set radius, volume, and timing parameters

### Coordinate System

EFF files use EQ coordinates (Z-up):
- X, Y, Z match EQ world coordinates
- Convert to Irrlicht (Y-up): (X, Z, Y)

### Sound Files

All referenced sounds are in `snd1.pfs` through `snd17.pfs`:
- Load WAV data from PFS archives
- Create OpenAL buffers for playback
- Attach to positional sources

### Day/Night Transitions

- Monitor game time for day/night state
- Switch between SoundID1/SoundID2 as appropriate
- Apply FadeOutMS for smooth transitions

---

## C++ Structure Definition

```cpp
#pragma pack(push, 1)
struct EffSoundEntry {
    int32_t  UnkRef00;      // 0: Runtime reference
    int32_t  UnkRef04;      // 4: Runtime reference
    int32_t  Reserved;      // 8: Reserved (0)
    int32_t  Sequence;      // 12: Entry ID
    float    X;             // 16: X position
    float    Y;             // 20: Y position
    float    Z;             // 24: Z position
    float    Radius;        // 28: Activation radius
    int32_t  Cooldown1;     // 32: Sound 1 cooldown (ms)
    int32_t  Cooldown2;     // 36: Sound 2 cooldown (ms)
    int32_t  RandomDelay;   // 40: Random delay (ms)
    int32_t  Unk44;         // 44: Unknown
    int32_t  SoundID1;      // 48: Day/primary sound
    int32_t  SoundID2;      // 52: Night/secondary sound
    uint8_t  SoundType;     // 56: Type (0-3)
    uint8_t  UnkPad57;      // 57: Padding
    uint8_t  UnkPad58;      // 58: Padding
    uint8_t  UnkPad59;      // 59: Padding
    int32_t  AsDistance;    // 60: Volume distance
    int32_t  UnkRange64;    // 64: Unknown
    int32_t  FadeOutMS;     // 68: Fade out time
    int32_t  UnkRange72;    // 72: Unknown
    int32_t  FullVolRange;  // 76: Full volume range
    int32_t  UnkRange80;    // 80: Unknown
};
#pragma pack(pop)

static_assert(sizeof(EffSoundEntry) == 84, "EffSoundEntry must be 84 bytes");
```

---

## Related Files

- `docs/eq_audio_assets_catalog.md` - Complete audio asset inventory
- `include/client/audio/` - WillEQ audio system headers
- `src/client/audio/` - WillEQ audio implementation

---

*Document created for WillEQ audio implementation*
*Based on Shendare's Eff2Emt converter (CC0 license)*
