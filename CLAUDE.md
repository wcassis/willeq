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
```

## Dependencies

Required: libuv, OpenSSL, zlib, fmt, GLM (header-only), jsoncpp
Optional:
- Boost (waypoint pathfinding)
- librecast-dev (navmesh pathfinding via Recast/Detour)
- libirrlicht-dev, libxxf86vm-dev (3D graphics rendering)

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
- `DaybreakConnectionManager` - Handles connection lifecycle via libuv

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

**Skills** (`include/client/skill/`)
- `SkillManager` - Tracks player skills, handles activation, cooldowns
- `SkillData` - Skill properties (value, category, cooldown, requirements)
- `skill_constants.h` - Skill IDs, names, categories, animation mappings

**Graphics Rendering** (`include/client/graphics/`, `src/client/graphics/`)
- `IrrlichtRenderer` - Main renderer using Irrlicht with software rendering (no GPU required)
- `CameraController` - FPS-style camera with free/follow/first-person modes
- `EntityRenderer` - Character/NPC 3D model rendering with texture support
- `RaceModelLoader` - Loads character models from S3D archives by race ID
- `DoorManager` - Door object rendering and state management

**UI Components** (`include/client/graphics/ui/`)
- `WindowManager` - Manages all UI windows, handles input routing
- `WindowBase` - Base class for draggable, resizable windows
- `ChatWindow` - Scrollable chat with input field, channel filtering, clickable links
- `InventoryWindow` - Player inventory grid with equipment slots
- `BagWindow` - Container bag contents display
- `LootWindow` - Corpse loot interface
- `GroupWindow` - Group member display with HP/mana bars
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

### Coordinate Systems

- EQ uses Z-up: (x, y, z)
- Irrlicht uses Y-up: (x, y, z)
- Transform: EQ (x, y, z) -> Irrlicht (x, z, y)

### Packet Structures

Titanium-specific packet structures are defined in `include/common/packet_structs.h`. These are binary-compatible with the Titanium client protocol.

### Logging

**IMPORTANT**: All new logging code MUST follow the standards in `.agent/debug_logging_standards.md`.

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

**User settings** stored in `config/` directory:
- `config/chat_settings.json` - Chat window position, size, channel filters

## Graphics Integration

Graphics is enabled by default (`EQT_GRAPHICS=ON` in CMake). Requires EQ Titanium client files for zone geometry and character models.

### Renderer Modes

The renderer has two modes, toggled with **F9**:

**Player Mode** (default for gameplay):
- First-person or follow camera with collision
- Movement synced to server
- Simplified HUD showing player/target info
- Controls: WASD=Move, Q/E=Strafe, R=Autorun, `=Attack, I=Inventory, U=Door

**Admin Mode** (for debugging):
- Free-fly camera (requires F5 to cycle to Free camera mode)
- No collision, full debug HUD
- Animation speed control, corpse Z offset, helm UV adjustments
- Entity/model inspection tools

### Key Bindings

**Both Modes:**
| Key | Action |
|-----|--------|
| F1 | Toggle wireframe |
| F2 | Toggle HUD |
| F3 | Toggle name tags |
| F5 | Cycle camera mode (Free/Follow/FirstPerson) |
| F9 | Toggle Admin/Player mode |
| F12 | Screenshot |
| LMB+drag | Look around |
| Shift+ESC | Quit |

**Player Mode Only:**
| Key | Action |
|-----|--------|
| WASD/Arrows | Move (with collision) |
| Q/E | Strafe |
| R / NumLock | Toggle autorun |
| 1-8 | Cast spell from gem slot 1-8 |
| ` (backtick) | Toggle auto-attack |
| I | Toggle inventory |
| K | Toggle skills window |
| G | Toggle group window |
| P | Toggle pet window |
| U | Interact with nearest door |
| H | Hail (say "Hail" or "Hail, <target>") |
| ESC | Clear target |
| C | Toggle collision |
| Enter | Open chat input |
| / | Open chat with slash |

**Admin Mode Only:**
| Key | Action |
|-----|--------|
| WASD | Camera movement (Free camera) |
| E/Q | Camera up/down |
| F4 | Toggle zone lights |
| F6 | Toggle Classic/Luclin models |
| F7 | Toggle helm debug mode |
| F10 | Save entities to JSON |
| F11 | Toggle lighting |
| [ / ] | Decrease/increase animation speed |
| P | Adjust corpse Z offset |
| Page Up/Down | Adjust ambient light |
| H/N | Cycle head variant |

### Slash Commands

**Chat:**
- `/say`, `/shout`, `/ooc`, `/auction`, `/gsay`, `/gu` - Channel messages
- `/tell <name> <msg>`, `/reply` - Private messages
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

**Utility:**
- `/help [command]` - Show help
- `/who` - List nearby entities
- `/quit` - Show exit options
- `/q` - Exit client immediately
- `/debug <level>` - Set debug level (0-6)
- `/timestamp` - Toggle chat timestamps

### Model Loading Order

`RaceModelLoader::getMeshForRace`:
1. Race-specific S3D file (e.g., `globalhum_chr.s3d`)
2. Zone-specific `_chr.s3d` file (e.g., `qeynos2_chr.s3d`)
3. Main `global_chr.s3d`
4. Numbered `global2-7_chr.s3d` files
5. Fallback: colored placeholder cubes
