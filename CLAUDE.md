# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

WillEQ is a standalone EverQuest client targeting the Titanium client version with optional 3D graphics rendering. It was migrated from the akk-stack headless client to remove all eqemu dependencies. All opcodes are hard-coded for Titanium specifically.

## Build Commands

**IMPORTANT: Do NOT use the `-j` flag when building. Always use `cmake --build .` without parallel jobs.**

```bash
# Configure (from project root)
mkdir -p build && cd build && cmake ..

# Build (no -j flag!)
cmake --build build

# Run tests
cd build && ctest --output-on-failure

# Run a single test
cd build && ctest -R TestName --output-on-failure
# Or directly:
./build/bin/test_packet

# Build output
./build/bin/willeq
```

## Running

```bash
# With graphics (requires display or VNC)
./build/bin/willeq -c willeq.json

# Headless (no graphics)
./build/bin/willeq -c willeq.json --no-graphics

# With VNC (for headless servers)
./scripts/start-with-vnc.sh -c willeq.json
# Connect via: vnc://localhost:5999

# With VNC + Audio streaming (for headless servers with sound)
./scripts/start-with-vnc-audio.sh -c willeq.json
# Connect VNC: vnc://localhost:5999
# Connect Audio: vlc http://localhost:8080

# With native RDP (alternative to VNC)
./build/bin/willeq -c willeq.json --rdp
# Connect via: mstsc.exe /v:hostname:3389 (Windows)
# Or: xfreerdp /v:hostname:3389 /cert:ignore (Linux)

# RDP with custom port
./build/bin/willeq -c willeq.json --rdp --rdp-port 13389
```

### Display Options Comparison

| Method | Use Case | Audio | Client |
|--------|----------|-------|--------|
| Direct X11 | Local display with GPU/software rendering | Local | Native |
| Xvfb + VNC | Remote access, cross-platform | None | VNC client |
| Xvfb + VNC + Audio | Remote with sound | HTTP stream | VNC + VLC |
| Native RDP | Windows users, better compression | RDP audio | mstsc.exe, xfreerdp |

RDP and VNC can run simultaneously if both X11 and RDP are enabled.

### Linux Capabilities

The client requires specific Linux capabilities for optimal network performance:

```bash
# Set capabilities on the binary (required for reliable zone loading)
sudo setcap cap_net_raw,cap_net_admin+ep ./build/bin/willeq
```

**Why this is needed:**
- `CAP_NET_RAW`: Required for raw packet capture (diagnostic feature)
- `CAP_NET_ADMIN`: Required for `SO_RCVBUFFORCE` to set UDP socket receive buffer above kernel limit

**Background:** During zone loading, the EQ server sends a burst of 40+ packets at nearly the same instant. The default Linux socket buffer (~425KB) can overflow, causing packet loss and zone loading failures (typically hanging at 40%). The client uses `SO_RCVBUFFORCE` to request a 1MB buffer, which requires `CAP_NET_ADMIN`.

Without `CAP_NET_ADMIN`, the client falls back to the kernel maximum (`net.core.rmem_max`, typically 212KB), which may cause intermittent zone loading failures on busy servers.

### Orange Pi (ARM) Cross-Compilation

The client cross-compiles for Orange Pi One (Allwinner H3, ARMv7-A Cortex-A7, Mali 400 GPU).

**Current target platform**: Ubuntu Noble (24.04) with mainline kernel and Lima open-source driver.
- **GPU rendering**: Native OpenGL ES 2.0 via custom `COpenGLES2Driver` (extends Irrlicht's `CNullDriver`)
- **Display**: DRM/KMS direct rendering without X11 (`--drm` flag)
- **Build**: `docker/Dockerfile.arm-noble` + `scripts/build-arm-noble.sh` (sets `-DEQT_GLES2=ON`)
- **Output**: `build-arm-noble/bin/willeq` (ELF 32-bit ARM, ~10MB stripped)

```bash
# Cross-compile
bash scripts/build-arm-noble.sh

# Run on Orange Pi (DRM/KMS, GLES2 — default for ARM builds)
./willeq -c config.json --drm --gles2 --constrained orangepi -r 800 600

# Run on Orange Pi (with X11, if Xorg is running)
DISPLAY=:0 ./willeq -c config.json --gles2 --constrained orangepi -r 800 600
```

**GLES2 rendering backend**: The Orange Pi uses a custom native GLES2 driver (`COpenGLES2Driver`) rather than Irrlicht's desktop OpenGL driver. This eliminates all desktop GL → GLES translation complexity, enables native ETC1 compressed textures (real 6:1 memory savings via `GL_ETC1_RGB8_OES`), and provides a shared rendering path for the planned Android 4.4 port. The driver uses 6 built-in GLSL ES 1.0 shader programs (no fixed-function pipeline). See "GLES2 Rendering Backend" section under Architecture for details.

**Legacy platforms**: (1) Noble with desktop GL 2.1 (Lima/Mesa) — superseded by GLES2 backend. (2) Debian Jessie (kernel 3.4) with proprietary Mali blob and gl4es translation — fully superseded.

**DRM input handling**: The DRM device (`docker/irrlicht-drm/CIrrDeviceFB.cpp`) reads keyboard/mouse via Linux evdev (`/dev/input/event*`). VT keyboard processing is disabled via `KDSKBMODE(K_OFF)` to prevent the console layer from intercepting Ctrl+key combinations.

## Dependencies

Required: OpenSSL, zlib, fmt, GLM (header-only), jsoncpp
Optional:
- Boost (waypoint pathfinding)
- librecast-dev (navmesh pathfinding via Recast/Detour)
- libirrlicht-dev, libxxf86vm-dev (3D graphics rendering)
- freerdp3-dev, libwinpr3-dev (native RDP streaming)
- Audio uses header-only libraries: miniaudio.h (output), tsf.h + tml.h (MIDI synthesis), dr_wav.h (WAV loading)

## Architecture

### Connection Flow

The client connects through three stages, each with its own connection manager:
1. **Login Server** (`m_login_connection_manager`) - Authenticates and gets server list
2. **World Server** (`m_world_connection_manager`) - Character selection and zone info
3. **Zone Server** (`m_zone_connection_manager`) - Actual gameplay

### Core Components

**EverQuest class** (`include/client/eq.h`, `src/client/eq.cpp`)
- Main client class (~9000 lines)
- Manages all three connection stages
- Handles entity tracking, movement, chat, combat, and doors
- All Titanium opcodes defined as enums: `TitaniumLoginOpcodes`, `TitaniumWorldOpcodes`, `TitaniumZoneOpcodes`

**Network Stack** (`include/common/net/`, `src/common/net/`)
- `DaybreakConnection` - UDP reliable protocol with sequencing, fragmentation, CRC, and compression
- `Packet` - Base class with `StaticPacket` (fixed buffer) and `DynamicPacket` (resizable)
- `DaybreakConnectionManager` - Handles connection lifecycle via UdpTransport
- Socket receive buffer set to 1MB to handle server packet bursts during zone loading

**Pathfinding** (`include/client/pathfinder_*.h`)
- `IPathfinder` - Abstract interface
- `PathfinderNull` - Fallback (no pathfinding)
- `PathfinderNavMesh` - Detour navmesh (optional, requires EQT_HAS_NAVMESH)
- `PathfinderWaypoint` - Boost A* waypoint graph (optional, requires EQT_HAS_WAYPOINT)

**Maps** (`include/client/hc_map.h`, `include/client/raycast_mesh.h`)
- `HCMap` - Zone map loading (V1/V2 formats)
- `RaycastMesh` - AABB tree for collision/LOS checks

**Combat** (`include/client/combat.h`)
- `CombatManager` - Auto-attack, targeting, looting, combat abilities

**Trading** (`src/client/trade_manager.cpp`)
- `TradeManager` - Player-to-player trading, item/money exchange, trade state machine

**Spells** (`src/client/spell/`)
- `SpellManager` - Spell casting, gem state, cooldowns, memorization
- `SpellEffects` - Effect processing (damage, healing, buffs)
- `SpellTypeProcessor` - Target type validation and selection

**Skills** (`include/client/skill/`)
- `SkillManager` - Tracks player skills, handles activation, cooldowns
- `SkillData` - Skill properties (value, category, cooldown, requirements)
- `skill_constants.h` - Skill IDs, names, categories, animation mappings

**Graphics Rendering** (`include/client/graphics/`, `src/client/graphics/`)
- `IrrlichtRenderer` - Main renderer (~7000 lines), manages scene, visibility, lighting, UI
- `CameraController` - FPS-style camera with free/follow/first-person modes
- `EntityRenderer` - Character/NPC 3D model rendering with GLSL shaders
- `RaceModelLoader` - Loads character models from S3D archives by race ID
- `DoorManager` - Door object rendering and state management
- `ZoneShader` - GLSL shader source and callbacks (GL 2.1 and GLES2 variants)
- `TextureAtlas` - Atlas tile layout (256px slots, 248px inner, 4px padding)
- `ConstrainedRendererConfig` - Hardware-tier presets (OrangePi, low, medium, high)
- `PortalSystem` - AABB-derived portal extraction and stencil-based portal occlusion
- `SpellVisualFX` - Spell casting effects: glow, projectile, impact, aura, rain, ground circle
- `ParticleManager` - Unified point-sprite particle system for weather and spell effects (GLES2)
- `SpellEffectsConfig` - JSON-configurable spell particle presets (`config/spell_effects.json`)

**GLES2 Driver** (`docker/irrlicht-drm/`)
- `COpenGLES2Driver.h/.cpp` - Native GLES2 `IVideoDriver` (extends `CNullDriver`)
- `COGLES2Texture.h/.cpp` - Texture management, native ETC1, FBO render targets
- `COGLES2Shaders.h/.cpp` - 6 built-in GLSL ES 1.0 shader programs, uniform cache
- `COGLES2MaterialRenderer.h/.cpp` - Material type renderers (blend, depth, cull state)

**UI Components** (`include/client/graphics/ui/`)
- `WindowManager` - Manages all UI windows, handles input routing
- `WindowBase` - Base class for draggable, resizable windows
- `ChatWindow` - Scrollable chat with input field, channel filtering, clickable links
- `InventoryWindow` - Player inventory grid with equipment slots
- `BagWindow` - Container bag contents display
- `LootWindow` - Corpse loot interface
- `GroupWindow` - Group member display with HP/mana bars
- `PetWindow` - Pet status display with command buttons
- `VendorWindow` - Merchant buy/sell with sorting and pricing
- `BankWindow` - Bank slots, shared bank, currency conversion
- `TradeWindow` - Player trading interface with item/money slots
- `TradeskillContainerWindow` - Tradeskill combines
- `SkillsWindow` - Player skills list with activation and cooldown indicators
- `SkillTooltip` - Skill details on hover (category, value, cooldown, requirements)
- `ItemTooltip` - Item stat display on hover
- `ItemIconLoader` - Loads item icons from EQ client files
- `CommandRegistry` - Slash command registration and dispatch
- `ChatMessageBuffer` - Ring buffer for chat history with channel support

**EQ File Loaders** (`include/client/graphics/eq/`)
- `pfs.cpp/h` - PFS/S3D archive reader
- `wld_loader.cpp/h` - WLD fragment parser (0x03-0x36 fragments)
- `s3d_loader.cpp/h` - High-level zone/model/character loader
- `dds_decoder.cpp/h` - DXT1/DXT3/DXT5 texture decompression
- `zone_geometry.cpp/h` - Irrlicht mesh builder with texture support

**EverQuest Client Files**
- `/home/user/projects/claude/EverQuestP1999` - Official Titanium Edition EverQuest Client

**Audio System** (`include/client/audio/`, `src/client/audio/`)

Core Components:
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

Sound files are loaded from `snd*.pfs` archives and the `sounds/` directory in the EQ client

### Coordinate Systems

This codebase uses multiple coordinate systems. Understanding them is critical for correct operation.

#### 1. EQ Server Coordinates (Z-up, INVERSEXY)

The EQ server sends positions with X and Y swapped relative to zone geometry. This is the "INVERSEXY" convention.

- **Axis orientation**: Z-up (vertical), X/Y horizontal
- **Used by**: Network packets, `/loc` command output
- **Heading**: 0-512 units = 0-360 degrees, 0=North(+Y), increases clockwise

When receiving spawn packets, the client **immediately swaps X and Y**:
```cpp
// In eq.cpp - server sends (server_x, server_y, z)
entity.x = server_y;  // Client X = Server Y
entity.y = server_x;  // Client Y = Server X
entity.z = z;         // Z unchanged
```

#### 2. Client Internal Coordinates (Z-up, swapped)

After the X/Y swap from server packets, client stores positions in "zone geometry" format.

- **Axis orientation**: Z-up (vertical), X/Y horizontal (swapped from server)
- **Used by**: `m_x`, `m_y`, `m_z` player position, `Entity.x/y/z`, movement calculations
- **Heading**: 0-512 units = 0-360 degrees (same as server)

This matches the zone geometry coordinate system, so client positions align with loaded S3D zones.

#### 3. S3D/WLD File Coordinates (Z-up)

Zone geometry in S3D files uses Z-up, matching client internal coordinates.

- **Axis orientation**: Z-up (vertical), X/Y horizontal
- **Used by**: Zone meshes, placeables, object positions in WLD files
- **Rotation in WLD**: 512 units = 360 degrees, converted via `rotModifier = 360.0f / 512.0f`

No coordinate transformation needed between client internal and S3D geometry.

#### 4. Irrlicht Rendering Coordinates (Y-up)

Irrlicht uses Y-up for rendering. All positions must be transformed.

- **Axis orientation**: Y-up (vertical), X/Z horizontal
- **Used by**: Camera position, rendered meshes, scene nodes
- **Transform from client**: `EQ(x, y, z) → Irrlicht(x, z, y)` (swap Y and Z)

Example from `zone_geometry.cpp`:
```cpp
vertex.Pos.X = v.x;
vertex.Pos.Y = v.z;  // EQ Z (vertical) → Irrlicht Y (vertical)
vertex.Pos.Z = v.y;  // EQ Y (horizontal) → Irrlicht Z (horizontal)
```

Example from `irrlicht_renderer.cpp`:
```cpp
irr::core::vector3df camPos(x, camZ, y);  // x, z, y order
```

#### 5. HCMap Mesh Coordinates (Y-up, for raycast)

HCMap loads map files and stores geometry in Y-up format for internal raycast queries.

- **File format**: V2 files have INVERSEXY applied (X/Y swapped), V1 files use Z-up directly
- **Internal storage**: Y-up (after Y↔Z swap during loading)
- **Used by**: `RaycastMesh` collision queries, `GetTrianglesInRadius()` for debug overlays

**Important**: `FindBestZ()` and `CheckLOS()` **accept EQ coordinates** (Z-up) as input/output and convert internally:
```cpp
// In FindBestZ - converts EQ input to mesh coords
glm::vec3 from(start.x, start.z + 10.0f, start.y);  // EQ(x,y,z) → Mesh(x,z,y)
// Result converted back to EQ coords
result->x = mesh_result.x;
result->y = mesh_result.z;  // Mesh Z → EQ Y
result->z = mesh_result.y;  // Mesh Y → EQ Z
```

#### 6. Navmesh Coordinates (Permuted X↔Z)

The navmesh files have X and Z axes swapped relative to zone geometry.

- **Axis orientation**: Different permutation than other systems
- **Transform from EQ**: `EQ(x, y, z) → Navmesh(y, z, x)`
- **Transform to EQ**: `Navmesh(x, y, z) → EQ(z, x, y)`

```cpp
// In pathfinder_nav_mesh.cpp
// Query input
glm::vec3 current_location(start.y, start.z, start.x);

// Result output
node.x = straight_path[i * 3 + 2];  // EQ X = Navmesh Z
node.y = straight_path[i * 3];      // EQ Y = Navmesh X
node.z = straight_path[i * 3 + 1];  // EQ Z = Navmesh Y
```

#### 7. Audio Coordinates (Z-up, same as client)

Audio system uses client internal coordinates directly (no transformation).

- **Axis orientation**: Z-up, matches client internal coordinates
- **Used by**: `ZoneSoundEmitter` positions, spatial audio

#### Coordinate System Summary Table

| System | Up Axis | Transform from Client | Notes |
|--------|---------|----------------------|-------|
| **EQ Server** | Z | Swap X↔Y | Packets use INVERSEXY |
| **Client Internal** | Z | (identity) | Reference coordinate system |
| **S3D/WLD Files** | Z | (identity) | Matches client after loading |
| **Irrlicht Render** | Y | Swap Y↔Z | `(x,y,z) → (x,z,y)` |
| **HCMap Mesh** | Y | Swap Y↔Z | Internal only; API uses client coords |
| **Navmesh** | - | Permute `(x,y,z) → (y,z,x)` | Different axis convention |
| **Audio** | Z | (identity) | Uses client coords directly |

#### Heading/Rotation Conventions

- **EQ Heading**: 0-512 units = 0-360 degrees
  - 0 = North (+Y in client coords)
  - 128 = West (-X)
  - 256 = South (-Y)
  - 384 = East (+X)
  - Increases clockwise when viewed from above

- **Packet formats vary**:
  - 11-bit: `raw * 360.0 / 2048.0` = degrees
  - 12-bit: `raw * 360.0 / 2048.0` = degrees
  - Position updates: `degrees * 512.0 / 360.0` = EQ units

- **WLD rotation**: 512 units = 360 degrees, with axis remapping:
  ```cpp
  finalRotX = 0.0f;                           // Always 0
  finalRotY = rawRotX * (360/512) * -1.0f;    // Primary yaw, negated
  finalRotZ = rawRotY * (360/512);            // Secondary rotation
  ```

### Renderer Tiers

WillEQ has three rendering tiers:

| Tier | Driver | GPU | Use Case |
|------|--------|-----|----------|
| Software | Irrlicht `EDT_BURNINGSVIDEO` | None | Headless, VNC, RDP |
| Desktop GL | Irrlicht `EDT_OPENGL` (GL 2.1) | Desktop GPU | PC/laptop with X11 |
| GLES2 | Custom `EDT_OGLES2` (`COpenGLES2Driver`) | Mali 400, mobile | Orange Pi, Android |

The GLES2 tier uses an all-shader pipeline with 6 built-in programs:
- `Solid3D` / `AlphaTest3D` — Non-atlas zone geometry, entities, doors (per-vertex lighting + fog)
- `AtlasSolid3D` / `AtlasAlpha3D` — Atlas zone geometry (precomputed UV in texcoord1)
- `UI2D` — All 2D UI (textured quads with color modulation)
- `Color2D` — 2D rectangles, lines, debug overlays

All VS use `precision highp float` (FP32 on Mali 400 vertex processor). All FS use `precision mediump float` (FP16 on Mali 400 fragment cores).

Custom shader materials from `zone_shader.cpp` use GLES ES 1.0 variants when `EQT_HAS_GLES2` is defined (`attribute`/`varying` instead of `gl_Vertex`/`gl_TexCoord[]`, `precision` qualifiers).

**Per-vertex lighting with wrap lighting**: All 8 point lights computed per-vertex. Player light (index 0, highest priority) uses wrap lighting formula `(NdotL+0.5)/1.5` for softer transitions on large zone triangles. Zone lights (indices 1-7: torches, campfires) use standard Lambertian `max(NdotL, 0.0)`. All lights use quadratic attenuation `1/(x + y*d + z*d² + ε)`. Point light contributions are additive — NOT multiplied by `uTintColor` (night darkening) or `aColor` (EQ baked vertex colors), which would suppress them to invisibility at night.

### Zone Rendering Optimizations

**Static VBOs and material-sorted draw** (GLES2): Zone geometry is uploaded once at zone load via `GL_STATIC_DRAW` VBOs, eliminating per-frame CPU→GPU DMA. Draw calls are sorted by material key `(MaterialType << 16) | TextureID` to minimize `glUseProgram` + `glBindTexture` state changes, with front-to-back order preserved within each material group.

**Front-to-back sorting** (`manualZoneDrawEnabled_`): Zone region meshes are removed from Irrlicht's scene graph rendering and drawn manually in front-to-back order for early-Z rejection on tile-based GPUs. The injection point is `RenderPassTimer::OnRenderPassPreRender(ESNRP_SOLID)` — fires after CAMERA pass sets up matrices. Toggle at runtime with `/sort`.

- Sorted by nearest-AABB-edge squared distance from camera
- Enabled automatically for PVS zones on GLES2 (also works on desktop GL)
- Zone nodes are set invisible so `drawAll()` skips them; manual draw uses `driver_->setMaterial()` + `driver_->drawMeshBuffer()`
- **Critical**: Must use `node->getPosition()` not `node->getAbsolutePosition()` — Irrlicht's `OnAnimate()` skips invisible nodes, so `AbsoluteTransformation` is never computed for hidden nodes

**Stencil portal occlusion** (`portalOcclusionEnabled_`): In indoor/dungeon zones, uses the stencil buffer to mask rendering to only what's visible through doorway portals. Off by default — toggle with `/portal`.

- `PortalSystem` (`portal_system.h/.cpp`) extracts portal quads from adjacent BSP region AABBs at zone load
- Recursive stencil masking: INCR on portal quad → draw room → recurse → DECR (max depth 3)
- Uses raw GL calls (`glStencilFunc`/`glStencilOp`/`glColorMask`) not driver wrapper, restores to defaults after (keeps `SOGLES2State` cache in sync)
- D24S8 depth-stencil format configured in EGL (`CIrrDeviceFB.cpp`)
- Debug: `/stencil` overlays colored rects per stencil level, `/portal` shows portal wireframes

### Packet Structures

Titanium-specific packet structures are defined in `include/common/packet_structs.h`. These are binary-compatible with the Titanium client protocol.

### Logging

**IMPORTANT**: All new logging code MUST follow the standards in `docs/debug_logging_standards.md`.

Key points:
- Default (level 0/NONE): Quiet mode - errors still reported, no verbose output
- Seven levels: NONE, FATAL, ERROR, WARN, INFO, DEBUG, TRACE
- Module-based filtering (NET, ENTITY, GRAPHICS, etc.)
- Runtime configurable via CLI, config file, or signals
- Output format: `[TIMESTAMP] [LEVEL] [MODULE] message`

Legacy logging in `include/common/logging.h` does not follow these standards and should be migrated when touching existing code.

## Test Organization

Tests are in `tests/` with one executable per test suite. Each test links only the minimal sources it needs:
- `test_packet.cpp` - Packet serialization
- `test_titanium_opcodes.cpp` - Opcode value verification
- `test_packet_structs.cpp` - Binary struct layout validation
- `test_integration_network.cpp` - Network protocol integration
- `test_formatted_message.cpp` - FormattedMessage parsing for NPC dialogue

**Audio tests** (require `WITH_AUDIO`, skip if no audio device):
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

### Graphics Integration Tests

Graphics integration tests require a running EQEmu server and X display. They are NOT auto-discovered by ctest because they need external dependencies.

**Requirements:**
- Running EQEmu server (login + world + zone servers)
- X display (use `DISPLAY=:99` with Xvfb for headless testing)
- EQ Titanium client files at configured `eq_client_path`
- Test config file: `/home/user/projects/claude/casterella.json`

**Running graphics integration tests:**
```bash
# Run from build directory
DISPLAY=:99 ./bin/test_zoning_graphics_integration
DISPLAY=:99 ./bin/test_inventory_model_view

# Run specific test
DISPLAY=:99 ./bin/test_inventory_model_view --gtest_filter="*ModelHasTextures*"
```

**Available graphics integration tests:**
- `test_zoning_graphics_integration.cpp` - Zone transitions with graphics enabled, LoadingPhase verification, camera collision safety
- `test_inventory_model_view.cpp` - Inventory window paperdoll model loading, textures, animation, weapons, render-to-texture

**Test config format** (`casterella.json`):
```json
{
  "clients": [{
    "character": "Casterella",
    "eq_client_path": "/path/to/EverQuestP1999",
    "host": "10.0.30.13",
    "port": 5998,
    "user": "test_user",
    "pass": "test_pass",
    "server": "ServerName",
    "maps_path": "/path/to/maps/base",
    "navmesh_path": "/path/to/maps/nav"
  }]
}
```

**Writing new graphics integration tests:**

1. Use `EQT_HAS_GRAPHICS` preprocessor guards
2. Skip if DISPLAY not set: `if (!std::getenv("DISPLAY")) GTEST_SKIP()`
3. Create client with `createClientWithGraphics()` helper
4. Wait for zone-in with `waitForZoneIn()` and `waitForZoneReady()`
5. Access renderer via `eq_->GetRenderer()`
6. Process frames with `processFrames(count)` or `waitForWithGraphics(predicate)`

**Debugging crashes during integration tests:**

Use the GDB helper script to capture crash backtraces:
```bash
# Script sends keystrokes to trigger movement/zoning
./scripts/gdb_zone_crash.sh
# Output saved to gdb_crash_output.log
```

## Key Patterns

- Zone connection requires specific packet sequencing - see `CheckZoneRequestPhaseComplete()` and the various `m_*_sent` / `m_*_received` flags
- Movement updates require `SendPositionUpdate()` to sync with server
- Entities tracked in `m_entities` map keyed by spawn_id
- Doors tracked in `m_doors` map keyed by door_id
- Friend class pattern: `CombatManager` is friend of `EverQuest` for internal access

## Configuration

**Main config** (willeq.json):
```json
{
    "eq_client_path": "/path/to/EverQuest"
}
```

**RDP configuration** (optional):
```json
{
    "eq_client_path": "/path/to/EverQuest",
    "rdp": {
        "enabled": true,
        "port": 3389
    }
}
```

**Audio configuration** (optional):
```json
{
    "eq_client_path": "/path/to/EverQuest",
    "audio": {
        "enabled": true,
        "master_volume": 100,
        "music_volume": 70,
        "effects_volume": 100,
        "soundfont": "/path/to/soundfont.sf2"
    }
}
```

**Audio command line options:**
```bash
# Disable audio
./build/bin/willeq -c willeq.json --no-audio

# Set volumes (0-100)
./build/bin/willeq -c willeq.json --audio-volume 80 --music-volume 50 --effects-volume 100

# Specify SoundFont for MIDI/XMI music
./build/bin/willeq -c willeq.json --soundfont /usr/share/sounds/sf2/FluidR3_GM.sf2
```

**Spell effects configuration** (optional):
- `config/spell_effects.json` - JSON-configurable spell particle presets with hot-reload
- 3-tier hierarchy: global settings → per-style defaults → per-spell overrides
- Configures resist colors, spawn rates, lifetimes, motion types, blend modes

**User settings** stored in `config/` directory:
- `config/chat_settings.json` - Chat window position, size, channel filters

## Graphics Integration

Graphics is enabled by default (`EQT_GRAPHICS=ON` in CMake). Requires EQ Titanium client files for zone geometry and character models.

### Key Bindings

**Global:**
| Key | Action |
|-----|--------|
| F12 | Screenshot |
| LMB+drag | Look around (camera) |
| RMB+drag | Look around (camera) |
| Ctrl+LMB+drag | Look around (single-button mouse) |
| Shift+ESC | Quit |
| Ctrl+F1 | Toggle wireframe |
| Ctrl+F2 | Toggle HUD |
| Ctrl+F3 | Toggle name tags |
| Ctrl+F4 | Toggle zone lights |
| Ctrl+F5 | Cycle camera mode |
| Ctrl+F6 | Toggle Classic/Luclin models |

**Player Mode:**
| Key | Action |
|-----|--------|
| WASD/Arrows | Move (with collision) |
| Ctrl+A / End | Strafe left |
| Ctrl+D / PageDown | Strafe right |
| Q | Toggle auto-attack |
| Ctrl+Q | Attack (initiate combat) |
| ` / NumLock / Numpad+ | Toggle autorun |
| Space | Jump |
| 1-8 | Hotbar slots 1-8 |
| 9-0 | Hotbar slots 9-10 |
| Alt+1-8 | Cast spell from gem 1-8 |
| F1 | Target self |
| F2-F6 | Target group member 1-5 |
| F7 | Target nearest PC |
| F8 | Target nearest NPC |
| Tab | Cycle targets |
| Shift+Tab | Cycle targets reverse |
| C | Consider target |
| R | Reply to last tell |
| I | Toggle inventory |
| K | Toggle skills window |
| G / Alt+P | Toggle group window |
| P | Toggle pet window |
| Ctrl+B | Toggle spellbook |
| Alt+B | Toggle buff window |
| U | Interact (nearest door/object) |
| H | Hail (say "Hail" or "Hail, <target>") |
| L | Cycle object lights |
| ESC | Clear target |
| +/- | Camera zoom in/out |
| Ctrl+Alt+C | Toggle collision |
| Ctrl+Z | Toggle zone line visualization |
| Enter | Open chat input |
| / | Open chat with slash |

### Slash Commands

**Chat:**
- `/say`, `/shout`, `/ooc`, `/auction`, `/gsay`, `/gu` - Channel messages
- `/tell <name> <msg>` - Private messages
- `/emote <text>` - Emotes
- `/filter [channel]` - Toggle channel display (say, tell, group, guild, shout, auction, ooc, emote, combat, exp, loot, npc, all)

**Movement:**
- `/loc` - Show current location
- `/sit`, `/stand` - Sit/stand
- `/camp` - Sit down and logout after 30 seconds (stand to cancel)
- `/move <x> <y> <z>` - Move to coordinates
- `/moveto <name>` - Move to entity
- `/follow <name>`, `/stopfollow` - Follow entity

**Combat:**
- `/target <name>` - Target entity
- `/attack`, `/stopattack` - Toggle attack
- `/aa` - Toggle auto-attack

**Group:**
- `/invite [name]` - Invite target or named player to group
- `/follow [name]` - Accept group invite from player
- `/disband` - Leave current group
- `/decline` - Decline pending group invite

**Spells:**
- `/cast <gem#>` - Cast spell from gem slot (1-8)
- `/mem <gem#> <spell_name>` - Memorize spell to gem slot
- `/forget <gem#>` - Forget spell from gem slot
- `/gems` - Show memorized spells
- `/interrupt` - Interrupt current cast
- `/spellbook` - Open spellbook window

**Skills:**
- `/skills` - Toggle skills window

**Pet:**
- `/pet <command>` - Issue commands to your pet (attack, back, follow, guard, sit, taunt, hold, focus, health, dismiss)

**Trading:**
- `/trade` - Request trade with target
- Trade window opens when accepting trade requests

**Audio:**
- `/music [on|off]` - Toggle or set music playback
- `/sound [on|off]` - Toggle or set sound effects
- `/volume [0-100]` - Show or set master volume (alias: `/vol`)
- `/musicvolume [0-100]` - Show or set music volume (alias: `/mvol`)
- `/effectsvolume [0-100]` - Show or set effects volume (aliases: `/evol`, `/sfxvol`)

**Utility:**
- `/help [command]` - Show help
- `/who` - List nearby entities
- `/quit` - Show exit options
- `/q` - Exit client immediately
- `/debug <level>` - Set debug level (0-6)
- `/timestamp` - Toggle chat timestamps

**Graphics Debug:**
- `/sort` - Toggle front-to-back zone sorting (manual draw vs Irrlicht scene graph)
- `/portal` - Toggle stencil-based portal occlusion (indoor zones)
- `/stencil` - Toggle stencil buffer debug overlay (colored rects per level)
- `/plight` - Toggle per-pixel player light
- `/olight` - Toggle object/torch point lights
- `/zlight` - Toggle directional sun/ambient lighting
- `/fire` - Toggle fire particle effects
- `/frametiming` - Toggle frame timing profiler
- `/sky` - Toggle sky rendering
- `/togglegrass`, `/toggleplants`, `/togglerocks`, `/toggledebris` - Toggle detail objects
- `/season [spring|summer|fall|winter]` - Change seasonal details
- `/detail [low|medium|high|custom]` - Set rendering quality tier

### Tools

- `model_viewer` - Character model viewer with spell effect test mode (category/class/level filtering, cast animations, particle previews)
- `zone_atlas_builder` - Offline ETC1-compressed texture atlas generator for GLES2 (`--zone <name>` or `--all`)
- `s3d_dump` / `s3d_extract` - S3D archive analysis and extraction
- `wld_dump` - WLD file content analyzer
- `merge_sf2.py` - SoundFont merger utility for combining multiple SF2 files
- `generate_textures` - Offline procedural texture generator
- GPU capability tools: `gpu_texture_formats`, `gles2_etc1_benchmark`, `egl_image_sharing_test`

### Model Loading Order

`RaceModelLoader::getMeshForRace`:
1. Race-specific S3D file (e.g., `globalhum_chr.s3d`)
2. Zone-specific `_chr.s3d` file (e.g., `qeynos2_chr.s3d`)
3. Main `global_chr.s3d`
4. Numbered `global2-7_chr.s3d` files
5. Fallback: colored placeholder cubes
