# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

Additional context is in subdirectory CLAUDE.md files:
- `src/client/graphics/CLAUDE.md` - Rendering, shaders, GLES2, zone loading, performance
- `include/client/graphics/ui/CLAUDE.md` - UI windows, key bindings, slash commands
- `src/client/audio/CLAUDE.md` - Audio system, sound categories, audio tests
- `docs/CLAUDE.md` - Coordinate systems, heading conventions, docs layout

## Project Overview

WillEQ is a standalone EverQuest client targeting the Titanium client version with optional 3D graphics rendering. It was migrated from the akk-stack headless client to remove all eqemu dependencies. All opcodes are hard-coded for Titanium specifically.

## Build Commands

**IMPORTANT: `-j$(nproc)` causes problems when executed directly in a Bash tool command (the shell expansion doesn't work reliably in Claude Code interactive sessions). Use a hardcoded value instead, e.g. `cmake --build build -j24` for this 24-core host. If building from a script (e.g. `scripts/build-arm-noble.sh`), `$(nproc)` works fine.**

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

**GLES2 rendering backend**: The Orange Pi uses a custom native GLES2 driver (`COpenGLES2Driver`) rather than Irrlicht's desktop OpenGL driver. This eliminates all desktop GL → GLES translation complexity, enables native ETC1 compressed textures (real 6:1 memory savings via `GL_ETC1_RGB8_OES`), and provides a shared rendering path for the planned Android 4.4 port. The driver uses 6 built-in GLSL ES 1.0 shader programs (no fixed-function pipeline). See `src/client/graphics/CLAUDE.md` for details.

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

### Packet Structures

Titanium-specific packet structures are defined in `include/common/packet_structs.h`. These are binary-compatible with the Titanium client protocol.

### Logging

**IMPORTANT**: All new logging code MUST follow the standards in `docs/standards/debug_logging_standards.md`.

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

Audio tests and graphics integration tests are documented in their respective CLAUDE.md files.

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

**Spell effects configuration** (optional):
- `config/spell_effects.json` - JSON-configurable spell particle presets with hot-reload
- 3-tier hierarchy: global settings → per-style defaults → per-spell overrides
- Configures resist colors, spawn rates, lifetimes, motion types, blend modes

**User settings** stored in `config/` directory:
- `config/chat_settings.json` - Chat window position, size, channel filters

Audio configuration is documented in `src/client/audio/CLAUDE.md`.

## Graphics Integration

Graphics is enabled by default (`EQT_GRAPHICS=ON` in CMake). Requires EQ Titanium client files for zone geometry and character models. See `src/client/graphics/CLAUDE.md` for rendering details, and `include/client/graphics/ui/CLAUDE.md` for UI and key bindings.
