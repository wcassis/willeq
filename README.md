# WillEQ

A standalone EverQuest client targeting the Titanium client version (2006) with 3D graphics rendering, spatial audio, and support for platforms ranging from x86_64 desktops to ARM single-board computers. Connects to EverQuest servers using the Titanium protocol and renders zones, characters, and UI using the Irrlicht engine with software rendering, OpenGL 2.1, or OpenGL ES 2.0.

This is a mostly vibe-coded project so the codebase is a bit chaotic. Expect major changes as features are implemented.

There's no character creation support yet so you'll have to use an existing account & character.

I'm developing this on a remote Ubuntu 24 server without a GPU. Video output goes to Xvfb and is exported via x11vnc. The ARM builds target an Orange Pi One (Mali 400) running Armbian Noble with the Lima open-source driver.

## Project Status

**What works reliably:**
- Login, server selection, character selection, and zoning
- 3D zone rendering with textures, lighting, fog, sky dome, and day/night cycle
- Weather effects (rain, snow) with particle systems and storm clouds
- Character/NPC models with skeletal animation and equipment display
- WASD movement with collision detection and server sync
- Chat (all channels), targeting (click, Tab cycle, F-keys), auto-attack
- Spell memorization, casting, and gem management
- Inventory with drag-and-drop, equipment slots, bags, item tooltips
- Full vendor buy/sell, banking, and player-to-player trading
- Group system with HP/mana bars, invite/disband/decline
- Pet window with all 10 commands
- Hotbar with 10 slots
- Spatial audio with zone emitters, music, weather sounds, and creature sounds
- Navmesh and waypoint pathfinding (optional)
- Native RDP streaming and VNC support for remote access
- GLES2 rendering on ARM (Orange Pi One with Mali 400)

**What doesn't work or is incomplete:**
- No character creation (requires existing account/character)
- Visual spell effects (particles) not implemented
- Swimming mechanics incomplete (keys exist, physics don't)
- Boats/transport not implemented
- Fall damage not implemented
- Most combat abilities (kick, bash, backstab, etc.) are server-side only
- Ranged combat (bows, thrown) not implemented

**Architecture note:** This is a thin client that delegates game logic to the server. Combat damage, spell effects, skill checks, and item transactions are all server-calculated. The client handles rendering, input, and state tracking.

## Feature Comparison: Classic/Kunark/Velious Era (1999-2000)

This checklist targets the original EverQuest experience through the Velious expansion.

Note: Many features have working UI but rely on server-side logic. The client handles rendering, input, and state tracking while the server calculates damage, spell effects, and validates actions.

Legend: Done | Partial | Not Implemented

### Connection & Authentication
| Feature | Status | Notes |
|---------|--------|-------|
| Login server authentication | Done | |
| Server selection | Done | |
| Character selection | Done | Auto-select from config |
| Character creation | -- | |

### World & Zones
| Feature | Status | Notes |
|---------|--------|-------|
| Zone loading | Done | S3D/WLD parsing |
| Zone geometry | Done | With BSP regions |
| Zone textures | Done | DDS (DXT1/DXT3/DXT5), animated textures |
| Zone lighting | Done | Ambient, static, dynamic, per-pixel (GLSL) |
| Zone fog | Done | Server-driven, distance-based |
| Sky dome | Done | 60+ sky types, day/night cycle, celestial bodies |
| Day/night cycle | Done | Smooth color transitions |
| Weather (rain/snow) | Done | Particle systems, storm clouds, lightning |
| Zone transitions | Done | |
| Zone points | Done | |
| Boats/transport | -- | |

### Graphics
| Feature | Status | Notes |
|---------|--------|-------|
| Zone geometry | Done | PVS culling, frustum culling, portal occlusion |
| Classic character models | Done | All original races |
| NPC/mob models | Done | |
| Equipment display | Done | Weapons, shields, helms with tinting |
| Skeletal animation | Done | 100+ animation codes, interpolation |
| Animated textures | Done | Water, lava, fire, flags |
| Environmental particles | Done | Rain, snow, dust, pollen, mist, fireflies |
| Ambient creatures | Done | Flocking birds, fish, insects (boids system) |
| Spell visual effects | -- | |
| Detail objects | Partial | Grass, plants, rocks with wind animation |

### Movement
| Feature | Status | Notes |
|---------|--------|-------|
| Walking/running | Done | With server sync |
| Strafing | Done | |
| Jumping | Partial | Command exists, no physics |
| Swimming | Partial | Keys exist ([ and ]), no mechanics |
| Drowning | -- | |
| Levitation | -- | Spell effect recognized, not applied |
| Auto-run | Done | |
| Collision | Done | Map-based raycasting |
| Sit/stand/crouch | Done | With animations |

### Camera
| Feature | Status | Notes |
|---------|--------|-------|
| First-person | Done | |
| Third-person follow | Done | With collision, zoom +/- |
| Free camera | Done | Debug/admin mode |
| Mouse look | Done | LMB or RMB drag |

### Combat
| Feature | Status | Notes |
|---------|--------|-------|
| Targeting | Done | Click, Tab cycle, F-keys, /target |
| Auto-attack (melee) | Done | Server-calculated damage |
| Damage messages | Done | |
| HP tracking | Done | |
| Consider | Done | Color-coded con system |
| Death/corpse | Done | |
| Corpse looting | Done | Items and money, auto-loot option |
| Spell casting | Done | 8 gem slots, memorize, cast, interrupt |
| Combat abilities | -- | Kick, bash, etc. are server-validated |
| Ranged combat | -- | |
| Pet commands | Done | 10 commands via pet window |
| Bind wound | -- | |

### Spells
| Feature | Status | Notes |
|---------|--------|-------|
| Spell book | Done | 400-slot book, searchable |
| Memorize spells | Done | /mem, /forget, drag-to-gem |
| Cast spells | Done | Gem slots, casting bar, interrupts |
| Spell effects | -- | Visual effects not implemented |
| Buff window | Done | Duration timers, right-click remove |

### Chat
| Feature | Status | Notes |
|---------|--------|-------|
| Say | Done | |
| Shout | Done | |
| OOC | Done | |
| Auction | Done | |
| Group | Done | |
| Guild | Done | |
| Tell | Done | |
| Emote | Done | /emote + shortcuts (/wave, /dance, /cheer, /laugh) |
| Reply | Done | R key or /reply |
| Chat filtering | Done | Per-channel with /filter |

### UI Windows
| Feature | Status | Notes |
|---------|--------|-------|
| Chat window | Done | Scrollable, filterable, clickable links |
| Inventory | Done | Equipment slots, drag-and-drop |
| Bags | Done | Container contents |
| Loot window | Done | |
| Item tooltips | Done | Full stat display |
| Player HUD | Done | HP/mana/stamina, AC, ATK, stats, resists |
| Target window | Done | |
| Spell book | Done | Searchable, drag-to-gem |
| Spell gem bar | Done | 8 gems with casting bar |
| Hotbar | Done | 10 slots, keys 1-0 |
| Group window | Done | HP/mana bars |
| Pet window | Done | Commands and status |
| Buff window | Done | Duration timers |
| Options window | Done | Display, audio, controls, game tabs |
| Trade window | Done | Player-to-player trading |
| Bank window | Done | 16 slots, shared bank, currency conversion |
| Merchant window | Done | Buy/sell with sorting and pricing |
| Tradeskill container | Partial | Container UI works, combines server-side |
| Skill trainer | Partial | Window exists |
| Skills window | Done | All 75 skills with values |
| Casting bar | Done | Progress and interrupt feedback |

### Inventory
| Feature | Status | Notes |
|---------|--------|-------|
| Item display | Done | Icons and names |
| Item stats | Done | Full tooltip |
| Move items | Done | Drag and drop |
| Container bags | Done | Up to 10-slot |
| Equipment slots | Done | With tint colors |
| Loot items | Done | |
| Currency | Done | All denominations |
| Item linking | -- | |

### Social
| Feature | Status | Notes |
|---------|--------|-------|
| /who | Done | Nearby entity listing |
| Friends list | -- | |
| Ignore list | -- | |
| Group invite | Done | /invite |
| Group management | Done | /disband, /decline |
| Guild management | Partial | MOTD only |
| Trading | Done | Full trade window |
| Inspect | -- | |

### NPC Interaction
| Feature | Status | Notes |
|---------|--------|-------|
| Doors | Done | Open/close sync, door sounds |
| Hail NPC | Done | H key, /hail |
| Merchant buy/sell | Done | Full buy/sell with pricing |
| Banker | Done | Slots, shared bank, currency |
| Quest/NPC text | Done | Displayed in chat |

### Skills
| Feature | Status | Notes |
|---------|--------|-------|
| Skill display | Done | 75 skills with values and categories |
| Skill activation | Partial | Framework exists, server validates |
| Tradeskills | Partial | Container window works, combines server-side |
| Forage | -- | |
| Fishing | -- | |
| Tracking | -- | |

### Audio
| Feature | Status | Notes |
|---------|--------|-------|
| Zone music | Done | XMI/MIDI via FluidSynth, MP3 |
| Sound effects | Done | WAV from PFS archives |
| Spatial audio | Done | 3D positioned zone emitters |
| Day/night audio | Done | Crossfade transitions |
| Player sounds | Done | Race/gender-specific (death, hit, jump, etc.) |
| Creature sounds | Done | Per-race (attack, damage, death, idle) |
| Door sounds | Done | Type-specific (metal, stone, wood, secret) |
| Weather audio | Done | Rain loops, wind, thunder |
| Water sounds | Done | Entry, swimming, underwater ambient |
| UI sounds | Done | Level up, buy/sell, notifications |
| Combat music | Done | Stinger tracks during combat |
| Music volume control | Done | F10/F11, /musicvolume |
| Effects volume control | Done | Shift+F10/F11, /effectsvolume |

### Extras (Not in Original)
| Feature | Status | Notes |
|---------|--------|-------|
| Headless mode | Done | For automation |
| VNC support | Done | Xvfb + x11vnc |
| Native RDP streaming | Done | FreeRDP, with audio |
| Screenshots | Done | F12 |
| Navmesh pathfinding | Done | Recast/Detour |
| Waypoint pathfinding | Done | Boost A* |
| Debug overlays | Done | Map, navmesh, zone line wireframes |
| ARM/GLES2 rendering | Done | Orange Pi One (Mali 400) |
| DRM/KMS output | Done | No X11 required |
| Constrained HW presets | Done | Voodoo1/2, TNT, OrangePi profiles |

## Requirements

### Required Dependencies
- C++17 compiler
- OpenSSL (libssl-dev)
- zlib (zlib1g-dev)
- fmt (libfmt-dev)
- GLM (libglm-dev, header-only)
- jsoncpp (libjsoncpp-dev)

### Optional Dependencies
| Dependency | Feature |
|------------|---------|
| libirrlicht-dev, libxxf86vm-dev | 3D graphics rendering |
| libopenal-dev, libsndfile1-dev | Audio playback |
| libfluidsynth-dev | MIDI/XMI music via SoundFont |
| freerdp3-dev, libwinpr3-dev | Native RDP streaming |
| libboost-graph-dev | Waypoint pathfinding |
| librecast-dev | Navmesh pathfinding (Recast/Detour) |
| libdrm-dev, libgbm-dev, libegl-dev | DRM/KMS output (ARM) |

### EverQuest Client Files
Requires a copy of the EverQuest Titanium Edition client for zone geometry, textures, character models, and sound files.

## Building

```bash
# Configure
mkdir -p build && cd build && cmake ..

# Build (do NOT use -j flag)
cmake --build build

# Run tests
cd build && ctest --output-on-failure
```

The executable is output to `./build/bin/willeq`.

### CMake Options

| Option | Default | Description |
|--------|---------|-------------|
| `EQT_GRAPHICS` | ON | Enable Irrlicht graphics rendering |
| `EQT_DRM` | OFF | DRM/KMS/GBM/EGL output (ARM, no X11) |
| `EQT_GLES2` | OFF | OpenGL ES 2.0 instead of desktop GL |
| `WITH_AUDIO` | auto | Audio (requires OpenAL + libsndfile) |
| `WITH_RDP` | auto | Native RDP streaming (requires FreeRDP) |

Pathfinding libraries (`EQT_HAS_NAVMESH`, `EQT_HAS_WAYPOINT`) and FluidSynth (`WITH_FLUIDSYNTH`) are auto-detected.

### ARM Cross-Compilation (Orange Pi One)

```bash
# Cross-compile for Armbian Noble (Lima GPU, DRM/KMS)
bash scripts/build-arm-noble.sh

# Output: build-arm-noble/bin/willeq
```

Docker-based cross-compilation using `docker/Dockerfile.arm-noble`. Targets ARMv7-A Cortex-A7 with native GLES2 via the Lima open-source driver.

### macOS Cross-Compilation

Docker-based osxcross toolchain targeting macOS 10.15+. See `docker/macos-cross/`.

## Configuration

Create a configuration file (e.g., `willeq.json`):

```json
{
    "login_server": "login.server.com",
    "login_port": 5999,
    "username": "your_username",
    "password": "your_password",
    "server_name": "Server Name",
    "character_name": "CharacterName",
    "eq_client_path": "/path/to/EverQuest"
}
```

Optional audio configuration:

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

Optional RDP configuration:

```json
{
    "rdp": {
        "enabled": true,
        "port": 3389
    }
}
```

## Running

```bash
# With graphics (requires X11 display)
./build/bin/willeq -c willeq.json

# Headless (no graphics)
./build/bin/willeq -c willeq.json --no-graphics

# With VNC (for remote servers)
./scripts/start-with-vnc.sh -c willeq.json
# Connect: vnc://localhost:5950

# With VNC + audio streaming
./scripts/start-with-vnc-audio.sh -c willeq.json
# VNC: vnc://localhost:5910, Audio: vlc http://localhost:8085

# With native RDP
./build/bin/willeq -c willeq.json --rdp
# Connect: mstsc.exe /v:hostname:3389

# On Orange Pi (DRM/KMS, GLES2)
./willeq -c config.json --drm --gles2 --constrained orangepi -r 800 600
```

### Audio Options

```bash
--no-audio                     # Disable audio
--audio-volume 80              # Master volume (0-100)
--music-volume 50              # Music volume (0-100)
--effects-volume 100           # Effects volume (0-100)
--soundfont /path/to/file.sf2  # SoundFont for XMI/MIDI music
```

### Linux Capabilities

For reliable zone loading, set network capabilities on the binary:

```bash
sudo setcap cap_net_raw,cap_net_admin+ep ./build/bin/willeq
```

This allows the client to request a 1MB UDP receive buffer (via `SO_RCVBUFFORCE`) to handle the server's burst of 40+ packets during zone loading. Without it, the default kernel buffer may overflow, causing zone loads to hang.

### Display Options

| Method | Use Case | Audio | Client |
|--------|----------|-------|--------|
| Direct X11 | Local display | Local OpenAL | Native |
| Xvfb + VNC | Remote access | None or HTTP stream | VNC client |
| Native RDP | Windows remote | RDP audio channel | mstsc.exe, xfreerdp |
| DRM/KMS | ARM boards, no X11 | Local OpenAL | Direct framebuffer |

## Controls

### Key Bindings

Bindings are configurable in `config/hotkeys.json`.

**Movement:**
| Key | Action |
|-----|--------|
| W / Up | Move forward |
| S / Down | Move backward |
| A / Left | Turn left |
| D / Right | Turn right |
| Ctrl+A / End | Strafe left |
| Ctrl+D / PageDown | Strafe right |
| Space | Jump |
| ` / Numpad+ / NumLock | Toggle autorun |
| [ | Swim up |
| ] | Swim down |

**Combat & Targeting:**
| Key | Action |
|-----|--------|
| Q | Toggle auto-attack |
| Ctrl+Q | Attack target |
| C | Consider target |
| H | Hail target |
| Tab | Cycle targets |
| Shift+Tab | Cycle targets reverse |
| F1 | Target self |
| F2-F6 | Target group member 1-5 |
| F7 | Target nearest PC |
| F8 | Target nearest NPC |
| Escape | Clear target |

**Spells & Hotbar:**
| Key | Action |
|-----|--------|
| Alt+1-8 | Cast spell from gem 1-8 |
| 1-0 | Hotbar slots 1-10 |

**Windows:**
| Key | Action |
|-----|--------|
| I | Inventory |
| K | Skills |
| G / Alt+P | Group |
| P | Pet |
| V | Vendor |
| O | Options |
| Ctrl+B | Spellbook |
| Alt+B | Buff window |
| Enter | Open chat |
| / | Open chat with slash |
| R | Reply to last tell |

**Camera:**
| Key | Action |
|-----|--------|
| Ctrl+F5 | Cycle camera mode |
| +/- | Camera zoom in/out |
| LMB/RMB drag | Look around |
| F12 | Screenshot |

**Audio:**
| Key | Action |
|-----|--------|
| F10 / F11 | Music volume down/up |
| Shift+F10 / Shift+F11 | Effects volume down/up |

**Debug:**
| Key | Action |
|-----|--------|
| U | Interact with door/object |
| L | Cycle object lights |
| Ctrl+F4 | Toggle zone lights |
| Ctrl+Alt+C | Toggle collision debug |
| Ctrl+Z | Toggle zone line visualization |
| Ctrl+M | Toggle map overlay |
| Ctrl+N | Toggle navmesh overlay |
| Ctrl+V | Toggle frustum culling |
| Ctrl+L | Lock/unlock UI |
| Ctrl+S | Save UI layout |
| Shift+Escape | Quit |

### Slash Commands

**Chat:**
```
/say (/s)                       Say to nearby
/shout (/sh)                    Shout to zone
/ooc                            Out of character
/auction (/auc)                 Auction message
/tell (/t) <name> <msg>         Private message
/reply (/r) <msg>               Reply to last tell
/gsay (/g)                      Group chat
/gu                             Guild chat
/emote (/em, /me) <text>        Emote action
/filter [channel]               Toggle channel display
/timestamp (/ts)                Toggle timestamps
```

**Movement:**
```
/loc                            Show coordinates
/sit, /stand                    Sit/stand
/camp                           Sit and logout (30s)
/crouch (/duck)                 Crouch
/feign (/fd)                    Feign death
/walk, /run, /sneak             Movement speed
/move <x> <y> <z>              Move to coordinates
/moveto <name>                  Move to entity
/follow [name], /stopfollow     Follow entity
/face <name|x y z>             Face direction
/turn <degrees>                 Turn to heading
```

**Combat:**
```
/target (/tar) <name>           Target entity
/attack (/a), /stopattack       Start/stop attack
/aa                             Toggle auto-attack
/consider (/con)                Consider target
/hail                           Hail target
/loot                           Loot nearest corpse
/autoloot [on|off]              Toggle auto-looting
```

**Spells:**
```
/cast <gem#>                    Cast from gem (1-8)
/mem <gem#> <spell>             Memorize spell
/forget <gem#>                  Forget spell
/gems                           Show memorized spells
/interrupt                      Interrupt casting
/skills                         Toggle skills window
```

**Group:**
```
/invite (/inv) [name]           Invite to group
/disband                        Leave group
/decline                        Decline invite
```

**Pet:**
```
/pet <command>                  Pet command
    attack, back, follow, guard, sit, taunt, hold, focus, health, dismiss
```

**Audio:**
```
/music [on|off]                 Toggle music
/sound [on|off]                 Toggle sound effects
/volume (/vol) [0-100]          Master volume
/musicvolume (/mvol) [0-100]    Music volume
/effectsvolume (/evol) [0-100]  Effects volume
```

**Emotes:**
```
/wave, /dance, /cheer, /laugh   Animation emotes
```

**Utility:**
```
/who                            List nearby entities
/help (/?) [command]            Show help
/debug <level>                  Set debug (0-6)
/door (/u)                      Interact with nearest door
/dump <name>                    Dump entity info
/afk                            Toggle AFK
/quit                           Show exit options
/q                              Exit immediately
```

## Architecture

### Connection Flow

The client connects through three stages, each with its own connection manager:
1. **Login Server** - Authentication and server list
2. **World Server** - Character selection and zone info
3. **Zone Server** - Gameplay

### Network Protocol

UDP-based Daybreak protocol (Titanium variant) with:
- Reliable delivery with 16-bit sequence numbers
- Packet fragmentation and reassembly (4 independent streams)
- zlib compression with marker-based detection
- XOR obfuscation
- CRC32 validation
- Exponential backoff retransmission (30ms initial, 5s max)
- 1MB socket receive buffer for zone load packet bursts

### Rendering Pipeline

Three rendering backends:
- **Software (Burnings)** - CPU-based, no GPU required (default)
- **OpenGL 2.1** - Desktop GPU with GLSL 1.20 shaders
- **OpenGL ES 2.0** - Embedded GPU (Mali 400, etc.) with GLSL ES 1.0

Zone visibility uses PVS (Potentially Visible Set) culling, frustum culling, and optional stencil-based portal occlusion. Constrained hardware presets (Voodoo1, Voodoo2, TNT, OrangePi) enforce memory budgets and texture limits.

### Audio Pipeline

OpenAL-based 3D spatial audio with:
- WAV effects from PFS/S3D archives
- XMI/MIDI music rendered via FluidSynth with SoundFont
- Zone sound emitters with day/night variants and distance falloff
- Race/gender-specific player sounds
- Per-race creature sounds
- Weather audio (rain, wind, thunder)
- Combat music stingers
- Loopback mode for RDP audio streaming

### Key Components

- **EverQuest** (`src/client/eq.cpp`) - Main client class, protocol handling, game state
- **IrrlichtRenderer** (`src/client/graphics/irrlicht_renderer.cpp`) - Scene management, visibility, lighting, UI
- **EntityRenderer** - Character/NPC models with skeletal animation
- **RaceModelLoader** - Loads models from S3D archives by race ID
- **WindowManager** - UI window system with drag/resize/persistence
- **CombatManager** - Auto-attack, targeting, looting
- **SpellManager** - Casting, gems, cooldowns, memorization
- **SkillManager** - Skill tracking, activation, cooldowns
- **TradeManager** - Player-to-player trading state machine
- **AudioManager** - Sound effects, spatial audio, volume control
- **MusicPlayer** - Streaming XMI/MIDI/MP3 playback
- **ZoneAudioManager** - Zone emitters, day/night transitions
- **DaybreakConnection** - UDP reliable protocol implementation
- **HCMap** - Zone map collision and LOS raycasting
- **PathfinderNavMesh** / **PathfinderWaypoint** - Optional pathfinding
- **ZoneShader** - GLSL shader programs for GL 2.1 and GLES2
- **COpenGLES2Driver** - Native GLES2 driver for embedded GPUs

## File Structure

```
src/
  client/
    main.cpp                 # Entry point, CLI parsing
    application.cpp          # Application lifecycle
    eq.cpp                   # Main client logic (~9000 lines)
    graphics/
      irrlicht_renderer.cpp  # Renderer (~7000 lines)
      entity_renderer.cpp    # Character/NPC models
      camera_controller.cpp  # Camera modes
      zone_shader.cpp        # GLSL shaders
      door_manager.cpp       # Door rendering
      sky_renderer.cpp       # Sky dome and celestial
      eq/                    # EQ file format loaders
        pfs.cpp              #   PFS/S3D archives
        wld_loader.cpp       #   WLD fragments
        s3d_loader.cpp       #   Zone/model/character loading
        dds_decoder.cpp      #   DXT texture decompression
        zone_geometry.cpp    #   Zone mesh building
      environment/           # Weather, particles, ambient
      ui/                    # UI windows and components
    audio/
      audio_manager.cpp      # Sound effects
      music_player.cpp       # Music streaming
      zone_audio_manager.cpp # Zone emitters
    input/
      hotkey_manager.cpp     # Key bindings
      graphics_input_handler.cpp
    action/
      command_processor.cpp  # Slash commands
    spell/                   # Spell system
    skill/                   # Skill system
  common/
    net/                     # Network protocol
      daybreak_connection.cpp
      packet.cpp
include/
  client/                    # Headers matching src layout
  common/
tests/                       # Unit and integration tests
config/                      # User settings (hotkeys, UI layout)
docker/
  Dockerfile                 # Linux x86_64 build
  Dockerfile.arm-noble       # ARM cross-compilation
  docker-compose.yml         # Multi-service deployment
  irrlicht-drm/              # DRM/KMS/GLES2 Irrlicht drivers
  macos-cross/               # macOS cross-compilation
scripts/
  start-with-vnc.sh          # VNC display
  start-with-vnc-audio.sh    # VNC + audio streaming
  build-arm-noble.sh         # ARM build script
tools/                       # Utilities (model viewer, S3D tools)
```

## Credits

This project builds upon the work of several open-source projects:

- **[EQEmu](https://github.com/EQEmu/Server)** - EverQuest server emulator. Network protocol implementation and packet structures derived from EQEmu's extensive reverse-engineering work.
- **[LanternExtractor](https://github.com/LanternEQ/LanternExtractor)** - EQ asset extraction tool. Referenced for S3D/WLD file format parsing and zone geometry extraction.
- **[eqsage](https://github.com/xackery/eqsage)** - EQ asset viewer. Referenced for model loading and animation handling.
- **[eqrequiem](https://github.com/jamfestemq/eqrequiem)** - EQ client project. Referenced for Titanium protocol details and client behavior.

## License

This project is licensed under the GNU General Public License v3.0. See [LICENSE](LICENSE) for details.

EverQuest is a registered trademark of Daybreak Game Company LLC.
