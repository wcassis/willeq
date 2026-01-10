# WillEQ Mode Separation Refactoring Plan

## Overview

This plan outlines the refactoring of WillEQ to cleanly support three operational modes and multiple graphical output types while maintaining a single source of truth for all game state.

### Target Modes
1. **Automated Mode** - No user input; all actions scripted/dynamic (bots, automation)
2. **Headless Interactive Mode** - Console-based input (stdin) and output (stdout/stderr)
3. **Graphical Interactive Mode** - Visual rendering with keyboard/mouse input

### Target Graphical Output Types
1. **Software Renderer** (current default - Irrlicht software)
2. **GPU Accelerated** (Irrlicht OpenGL/Direct3D)
3. **Low-Res Mode** (reduced resolution/detail)
4. **Overhead/Top-Down View** (2D map-like view)
5. **Rogue-like ASCII** (terminal-based ASCII art)
6. **MUD Interface** (Zork-like text adventure)

---

## Current Architecture Analysis

### Strengths
- Network layer (`DaybreakConnection`) is already decoupled
- Callback system exists between EverQuest and IrrlichtRenderer
- Command system (`ProcessCommand`) handles text-based actions
- Optional graphics compile flag (`EQT_GRAPHICS`)

### Problems to Address
1. **EverQuest class is monolithic** (~12,595 lines) - mixes state, logic, networking, and graphics coordination
2. **Input handling is fragmented** - split between main.cpp console handling and RendererEventReceiver
3. **Graphics tightly coupled** - IrrlichtRenderer is directly instantiated and managed in EverQuest
4. **No abstraction for output** - console output scattered throughout codebase
5. **Movement logic duplicated** - separate paths for console vs graphics input

---

## Proposed Architecture

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                              Application Layer                               │
│  ┌─────────────────┐  ┌────────────────────┐  ┌────────────────────────┐    │
│  │ AutomatedMode   │  │ HeadlessMode       │  │ GraphicalMode          │    │
│  │ (ScriptEngine)  │  │ (ConsoleInterface) │  │ (IOutputRenderer)      │    │
│  └────────┬────────┘  └─────────┬──────────┘  └───────────┬────────────┘    │
│           │                     │                         │                  │
│           └─────────────────────┼─────────────────────────┘                  │
│                                 │                                            │
│                    ┌────────────▼────────────┐                               │
│                    │    IInputHandler        │                               │
│                    │    (Abstract Input)     │                               │
│                    └────────────┬────────────┘                               │
└─────────────────────────────────┼────────────────────────────────────────────┘
                                  │
┌─────────────────────────────────▼────────────────────────────────────────────┐
│                              Core Layer                                       │
│  ┌──────────────────────────────────────────────────────────────────────┐    │
│  │                        GameStateManager                               │    │
│  │  ┌─────────────┐ ┌─────────────┐ ┌─────────────┐ ┌────────────────┐  │    │
│  │  │ PlayerState │ │ EntityState │ │ WorldState  │ │ CombatState    │  │    │
│  │  └─────────────┘ └─────────────┘ └─────────────┘ └────────────────┘  │    │
│  │  ┌─────────────┐ ┌─────────────┐ ┌─────────────┐ ┌────────────────┐  │    │
│  │  │ SpellState  │ │ SkillState  │ │ GroupState  │ │ InventoryState │  │    │
│  │  └─────────────┘ └─────────────┘ └─────────────┘ └────────────────┘  │    │
│  └──────────────────────────────────────────────────────────────────────┘    │
│                                                                              │
│  ┌──────────────────────────────────────────────────────────────────────┐    │
│  │                        ActionDispatcher                               │    │
│  │  (Converts user intents to game actions, validates, executes)        │    │
│  └──────────────────────────────────────────────────────────────────────┘    │
│                                                                              │
│  ┌──────────────────────────────────────────────────────────────────────┐    │
│  │                        EventBus / Observer                            │    │
│  │  (State change notifications for UI/rendering updates)               │    │
│  └──────────────────────────────────────────────────────────────────────┘    │
└──────────────────────────────────────────────────────────────────────────────┘
                                  │
┌─────────────────────────────────▼────────────────────────────────────────────┐
│                              Network Layer                                    │
│  ┌────────────────┐  ┌────────────────┐  ┌────────────────┐                  │
│  │ LoginConnection│  │ WorldConnection│  │ ZoneConnection │                  │
│  └────────────────┘  └────────────────┘  └────────────────┘                  │
└──────────────────────────────────────────────────────────────────────────────┘
```

### Output Renderer Hierarchy

```
IOutputRenderer (abstract)
├── NullRenderer (automated mode - no output)
├── ConsoleRenderer (headless mode - text output)
│   └── Uses: GameStateManager events → formatted text to stdout
└── GraphicalRenderer (abstract base for visual renderers)
    ├── IrrlichtSoftwareRenderer (current implementation)
    ├── IrrlichtGPURenderer (OpenGL/D3D)
    ├── LowResRenderer (simplified 3D)
    ├── TopDownRenderer (2D overhead view)
    ├── ASCIIRenderer (curses/terminal graphics)
    └── MUDRenderer (pure text descriptions)
```

---

## Implementation Phases

### Phase 1: Extract Game State (Foundation) - COMPLETED

**Status:** ✅ COMPLETE (2026-01-03)

**Goal:** Create a clean separation between game state and the EverQuest class.

**What was implemented:**

1. **EventBus System** (`include/client/state/event_bus.h`, `src/client/state/event_bus.cpp`)
   - Thread-safe event bus with subscribe/unsubscribe/publish
   - 15+ event types: PlayerMoved, EntitySpawned, DoorStateChanged, etc.
   - Typed event data structs for each event type
   - Listener handles for safe unsubscription

2. **PlayerState** (`include/client/state/player_state.h`, `src/client/state/player_state.cpp`)
   - Position, heading, velocity, animation
   - Stats: HP, mana, endurance, level, class, race
   - Attributes: STR, STA, CHA, DEX, INT, AGI, WIS
   - Currency: platinum, gold, silver, copper
   - Movement state: mode, target, follow, keyboard state
   - Flags: sneaking, AFK, anonymous, camping
   - Fires PlayerMoved and PlayerStatsChanged events

3. **EntityManager** (`include/client/state/entity_manager.h`, `src/client/state/entity_manager.cpp`)
   - Entity struct with all appearance/equipment data
   - Add/remove/update entity operations
   - Spatial queries: getEntitiesInRange, getNearestNPC, etc.
   - Name search (case-insensitive partial match)
   - Fires EntitySpawned, EntityDespawned, EntityMoved events

4. **WorldState** (`include/client/state/world_state.h`, `src/client/state/world_state.cpp`)
   - Zone name and ID
   - Time of day (hour, minute, day, month, year)
   - Weather type and intensity
   - Zone transition state (pending zone, zone line detection)
   - Connection state (zone connected, client ready)
   - Fires ZoneChanged, TimeOfDayChanged events

5. **CombatState** (`include/client/state/combat_state.h`, `src/client/state/combat_state.cpp`)
   - Current target (ID, name, HP%, level)
   - Auto-attack state
   - Combat movement state
   - Last slain entity name

6. **GroupState** (`include/client/state/group_state.h`, `src/client/state/group_state.cpp`)
   - Group membership and leader status
   - GroupMember struct with HP/mana/level/class
   - Pending invite handling
   - Fires GroupChanged, GroupMemberUpdated events

7. **DoorState** (`include/client/state/door_state.h`, `src/client/state/door_state.cpp`)
   - Door struct with position, heading, state
   - Add/remove/update door operations
   - Spatial queries: getDoorsInRange, getNearestDoor
   - Pending click tracking
   - Fires DoorSpawned, DoorStateChanged events

8. **GameState Container** (`include/client/state/game_state.h`, `src/client/state/game_state.cpp`)
   - Aggregates all state managers
   - Owns the EventBus and connects it to all managers
   - Convenience methods: isFullyZonedIn(), playerPosition()
   - Reset methods for zone changes

9. **EverQuest Integration**
   - Added `#include "client/state/game_state.h"` to eq.h
   - Added `eqt::state::GameState m_game_state` member
   - Added public accessors: `GetGameState()`, `const GetGameState()`
   - Legacy member variables retained for backward compatibility

10. **Build System**
    - Updated CMakeLists.txt with 8 new source files
    - All code compiles successfully
    - 99% of tests pass (4 pre-existing failures unrelated to changes)

**Files Created:**
```
include/client/state/
├── event_bus.h
├── player_state.h
├── entity_manager.h
├── world_state.h
├── combat_state.h
├── group_state.h
├── door_state.h
└── game_state.h

src/client/state/
├── event_bus.cpp
├── player_state.cpp
├── entity_manager.cpp
├── world_state.cpp
├── combat_state.cpp
├── group_state.cpp
├── door_state.cpp
└── game_state.cpp
```

**Next Steps (Phase 2):**
- Existing code continues to use legacy member variables in EverQuest
- New code can use `GetGameState()` to access the new state system
- Gradual migration of existing code to use GameState
- Renderer can subscribe to EventBus for state change notifications

#### 1.1 Create State Container Classes

Create `include/client/state/` directory with:

```cpp
// game_state.h - Central state container
class GameState {
public:
    PlayerState& player();
    EntityManager& entities();
    WorldState& world();
    CombatState& combat();
    SpellState& spells();
    SkillState& skills();
    GroupState& group();
    InventoryState& inventory();
    DoorState& doors();

    // Event system for state change notifications
    EventBus& events();
};

// player_state.h
class PlayerState {
    float m_x, m_y, m_z, m_heading;
    uint8_t m_level;
    uint32_t m_class, m_race, m_gender;
    uint32_t m_cur_hp, m_max_hp, m_mana, m_max_mana;
    // ... all player-specific state from EverQuest class

public:
    // Getters (const)
    float x() const;
    float y() const;
    // ...

    // State modification (fires events)
    void setPosition(float x, float y, float z);
    void setHeading(float heading);
    void setHP(uint32_t current, uint32_t max);
};

// entity_manager.h
class EntityManager {
    std::map<uint16_t, Entity> m_entities;
    uint16_t m_player_spawn_id;

public:
    void addEntity(const Entity& entity);
    void removeEntity(uint16_t spawnId);
    void updateEntity(uint16_t spawnId, const EntityUpdate& update);
    const Entity* getEntity(uint16_t spawnId) const;
    std::vector<uint16_t> getEntitiesInRange(float x, float y, float z, float range) const;
};
```

#### 1.2 Create Event Bus System

```cpp
// event_bus.h
enum class GameEvent {
    PlayerMoved,
    PlayerStatsChanged,
    EntitySpawned,
    EntityDespawned,
    EntityMoved,
    DoorStateChanged,
    ZoneChanged,
    ChatMessage,
    CombatEvent,
    SpellCast,
    SkillUsed,
    InventoryChanged,
    GroupChanged,
    // ...
};

struct EventData {
    GameEvent type;
    std::variant<PlayerMovedData, EntitySpawnedData, ChatMessageData, ...> data;
};

class EventBus {
    std::vector<std::function<void(const EventData&)>> m_listeners;

public:
    void subscribe(std::function<void(const EventData&)> listener);
    void publish(const EventData& event);
};
```

#### 1.3 Migrate State from EverQuest Class

Systematically move state variables from `eq.h` to appropriate state classes:

| EverQuest Member | Target State Class |
|------------------|-------------------|
| m_x, m_y, m_z, m_heading | PlayerState |
| m_cur_hp, m_max_hp, m_mana | PlayerState |
| m_entities | EntityManager |
| m_doors | DoorState |
| m_current_zone_* | WorldState |
| m_combat_* | CombatState |
| m_group_* | GroupState |
| m_inventory_* | InventoryState |

---

### Phase 2: Abstract Input Handling - COMPLETED

**Status:** ✅ COMPLETE (2026-01-03)

**Goal:** Create a unified input abstraction that works for all modes.

**What was implemented:**

1. **IInputHandler Interface** (`include/client/input/input_handler.h`)
   - InputAction enum with 27 discrete actions (Quit, Screenshot, ToggleAutorun, Jump, etc.)
   - InputState struct for continuous state (movement keys, mouse state)
   - SpellCastRequest, HotbarRequest, TargetRequest, LootRequest structs
   - ChatMessage, MoveCommand structs for console input
   - KeyEvent struct for text input handling
   - Full interface with discrete actions, continuous state, and debug adjustments

2. **NullInputHandler** (`include/client/input/null_input_handler.h`, `src/client/input/null_input_handler.cpp`)
   - For automated mode (no user input)
   - All queries return false/empty by default
   - Programmatic injection methods for scripted actions:
     - `injectAction()`, `injectSpellCast()`, `injectTarget()`, etc.
     - `injectChatMessage()`, `injectMoveCommand()`, `injectRawCommand()`
   - Direct movement state setters for scripted movement

3. **ConsoleInputHandler** (`include/client/input/console_input_handler.h`, `src/client/input/console_input_handler.cpp`)
   - For headless interactive mode (stdin commands)
   - Two modes: keyboard mode (raw terminal, WASD movement) and command mode
   - Press Enter to toggle between modes
   - Keyboard controls: WASD/Q/E movement, Space=Jump, `=AutoAttack, R=Autorun, X=Stop
   - Command parsing for: say, tell, shout, ooc, auction, move, moveto, face, target, cast, etc.
   - Background input thread with thread-safe queues
   - Platform-specific terminal handling (Unix termios, Windows _kbhit/_getch)

4. **GraphicsInputHandler** (`include/client/input/graphics_input_handler.h`, `src/client/input/graphics_input_handler.cpp`)
   - For graphical mode (wraps RendererEventReceiver)
   - Translates Irrlicht key/mouse events to InputAction enum
   - Continuous movement state from held keys (WASD, arrows)
   - Mouse state tracking (position, deltas, button clicks)
   - All debug adjustment deltas passthrough from RendererEventReceiver
   - Injection methods for target/loot requests (called by IrrlichtRenderer)

5. **Build System**
   - Updated CMakeLists.txt with 3 new source files:
     - `src/client/input/null_input_handler.cpp`
     - `src/client/input/console_input_handler.cpp`
     - `src/client/input/graphics_input_handler.cpp` (in GRAPHICS_SOURCES)
   - All code compiles successfully
   - 99% of tests pass (same 4 pre-existing failures)

**Files Created:**
```
include/client/input/
├── input_handler.h          # Interface and types
├── null_input_handler.h     # Automated mode
├── console_input_handler.h  # Headless mode
└── graphics_input_handler.h # Graphical mode

src/client/input/
├── null_input_handler.cpp
├── console_input_handler.cpp
└── graphics_input_handler.cpp
```

**Next Steps (Phase 3):**
- Create IOutputRenderer interface
- Implement NullRenderer, ConsoleRenderer
- Refactor IrrlichtRenderer to implement GraphicalRenderer interface

#### 2.1 Define Input Interface (Reference Design)

```cpp
// input_handler.h
enum class InputAction {
    // Movement
    MoveForward, MoveBackward, TurnLeft, TurnRight,
    StrafeLeft, StrafeRight, Jump,
    ToggleAutoRun, ToggleSit, ToggleSneak,

    // Combat
    Attack, StopAttack, TargetNearest, ClearTarget,
    CastSpell1, CastSpell2, /* ... */ CastSpell8,
    UseSkill,

    // Interaction
    Interact, Hail, Loot, Trade,

    // UI
    OpenInventory, OpenSkills, OpenGroup, OpenSpellbook,
    OpenChat, ToggleMap,

    // System
    Quit, ToggleDebug,
};

class IInputHandler {
public:
    virtual ~IInputHandler() = default;

    // Called each frame to process input
    virtual void update(float deltaTime) = 0;

    // Check if action is currently active (for held keys)
    virtual bool isActionActive(InputAction action) const = 0;

    // Check if action was just triggered (for one-shot actions)
    virtual bool wasActionTriggered(InputAction action) = 0;

    // Get mouse/cursor position (for graphical modes)
    virtual std::optional<std::pair<int, int>> getCursorPosition() const { return std::nullopt; }

    // Get text input (for chat/commands)
    virtual std::optional<std::string> getTextInput() { return std::nullopt; }

    // Get command input (for slash commands)
    virtual std::optional<std::string> getCommandInput() { return std::nullopt; }
};
```

#### 2.2 Implement Input Handlers

```cpp
// null_input_handler.h - For automated mode
class NullInputHandler : public IInputHandler {
    // Returns false for all actions - automation uses ActionDispatcher directly
};

// console_input_handler.h - For headless mode
class ConsoleInputHandler : public IInputHandler {
    // Reads from stdin, parses commands, maps to InputActions
    // Handles keyboard state for Unix raw mode
};

// graphics_input_handler.h - For graphical mode
class GraphicsInputHandler : public IInputHandler {
    // Wraps RendererEventReceiver, translates to InputActions
    // Handles mouse input for targeting, camera control
};
```

---

### Phase 3: Abstract Output/Rendering - COMPLETED

**Status:** ✅ COMPLETE (2026-01-03)

**Goal:** Create a renderer abstraction that supports all output types.

**What was implemented:**

1. **IOutputRenderer Interface** (`include/client/output/output_renderer.h`)
   - Lifecycle methods: initialize(), shutdown(), isRunning(), update()
   - Event bus integration: connectToGameState(), disconnectFromGameState()
   - Zone management: loadZone(), unloadZone(), setLoadingProgress(), setZoneReady()
   - Player display: setPlayerSpawnId(), setPlayerPosition(), setCharacterInfo(), updateCharacterStats()
   - Target display: setCurrentTarget(), updateCurrentTargetHP(), clearCurrentTarget()
   - Chat/message output: displayChatMessage(), displaySystemMessage(), displayCombatMessage()
   - Optional graphical features: setCameraMode(), toggleWireframe(), saveScreenshot()
   - RendererConfig struct with display/path/rendering options
   - RendererType enum and createRenderer() factory function

2. **NullRenderer** (`include/client/output/null_renderer.h`, `src/client/output/null_renderer.cpp`)
   - For automated mode (no output)
   - All methods are no-ops
   - Maintains minimal state (zone name, running flag)

3. **ConsoleRenderer** (`include/client/output/console_renderer.h`, `src/client/output/console_renderer.cpp`)
   - For headless interactive mode (stdout output)
   - ANSI color support with channel-specific colors
   - Timestamp display (configurable)
   - Verbose mode for entity spawn/despawn notifications
   - Periodic status line updates (HP/Mana/Location/Zone/Target)
   - Event bus integration for chat, combat, zone, and entity events
   - Thread-safe output with mutex protection

4. **GraphicalRenderer Base Class** (`include/client/output/graphical_renderer.h`, `src/client/output/graphical_renderer.cpp`)
   - Abstract base for all visual renderers (Irrlicht, ASCII, TopDown, etc.)
   - Camera mode management with cycle support
   - Render quality settings
   - EntityAppearanceData struct for model customization
   - Entity management methods: createEntity(), updateEntity(), removeEntity()
   - Door management methods: createDoor(), setDoorState(), clearDoors()
   - Callback types: DoorInteract, SpellGemCast, Target, Movement, ChatSubmit
   - Input handler ownership

5. **Renderer Factory** (`src/client/output/output_renderer.cpp`)
   - createRenderer() function supporting all RendererType values
   - Fallback to ConsoleRenderer when graphics not available

6. **Build System**
   - Updated CMakeLists.txt with 4 new source files:
     - `src/client/output/output_renderer.cpp`
     - `src/client/output/null_renderer.cpp`
     - `src/client/output/console_renderer.cpp`
     - `src/client/output/graphical_renderer.cpp`
   - All code compiles successfully
   - 99% of tests pass (same 4 pre-existing failures)

**Files Created:**
```
include/client/output/
├── output_renderer.h     # IOutputRenderer interface, RendererConfig, RendererType
├── null_renderer.h       # Automated mode (no output)
├── console_renderer.h    # Headless mode (stdout)
└── graphical_renderer.h  # Base class for visual renderers

src/client/output/
├── output_renderer.cpp   # Factory function
├── null_renderer.cpp
├── console_renderer.cpp
└── graphical_renderer.cpp
```

**Next Steps (Phase 4):**
- Create IGameMode interface
- Implement AutomatedMode, HeadlessMode, GraphicalMode
- Combine input handlers with renderers in mode classes

#### 3.1 Define Renderer Interface (Reference Design)

```cpp
// output_renderer.h
class IOutputRenderer {
public:
    virtual ~IOutputRenderer() = default;

    // Lifecycle
    virtual bool initialize(const RendererConfig& config) = 0;
    virtual void shutdown() = 0;
    virtual bool update(float deltaTime) = 0;  // Returns false if should quit

    // Zone management
    virtual void loadZone(const std::string& zoneName) = 0;
    virtual void unloadZone() = 0;

    // Entity rendering
    virtual void onEntitySpawned(const Entity& entity) = 0;
    virtual void onEntityDespawned(uint16_t spawnId) = 0;
    virtual void onEntityMoved(uint16_t spawnId, const EntityPosition& pos) = 0;

    // Player rendering
    virtual void onPlayerMoved(const PlayerPosition& pos) = 0;
    virtual void onPlayerStatsChanged(const PlayerStats& stats) = 0;

    // Door rendering
    virtual void onDoorSpawned(const Door& door) = 0;
    virtual void onDoorStateChanged(uint8_t doorId, bool open) = 0;

    // UI/Output
    virtual void displayChatMessage(const ChatMessage& msg) = 0;
    virtual void displaySystemMessage(const std::string& msg) = 0;
    virtual void displayCombatMessage(const CombatMessage& msg) = 0;

    // Input handler (renderer may provide its own)
    virtual IInputHandler* getInputHandler() { return nullptr; }
};
```

#### 3.2 Implement Null Renderer (Automated Mode)

```cpp
// null_renderer.h
class NullRenderer : public IOutputRenderer {
    // All methods are no-ops or minimal logging
    // Used for automated/headless operation without any output
};
```

#### 3.3 Implement Console Renderer (Headless Mode)

```cpp
// console_renderer.h
class ConsoleRenderer : public IOutputRenderer {
    // Outputs game state changes to stdout in formatted text
    // Provides real-time status updates
    // Handles chat display with timestamps and channel filtering

public:
    void displayChatMessage(const ChatMessage& msg) override {
        std::cout << "[" << msg.channel << "] " << msg.sender << ": " << msg.text << std::endl;
    }

    void onEntitySpawned(const Entity& entity) override {
        if (m_verbose) {
            std::cout << "* " << entity.name << " has entered the area." << std::endl;
        }
    }

    // Periodic status display
    void displayStatus(const GameState& state) {
        std::cout << "\rHP: " << state.player().curHP() << "/" << state.player().maxHP()
                  << " | Mana: " << state.player().mana() << "/" << state.player().maxMana()
                  << " | Loc: " << state.player().x() << ", " << state.player().y()
                  << std::flush;
    }
};
```

#### 3.4 Create Graphical Renderer Base Class

```cpp
// graphical_renderer.h
class GraphicalRenderer : public IOutputRenderer {
protected:
    std::unique_ptr<IInputHandler> m_inputHandler;

public:
    IInputHandler* getInputHandler() override { return m_inputHandler.get(); }

    // Common graphical functionality
    virtual void setCameraMode(CameraMode mode) = 0;
    virtual void setRenderQuality(RenderQuality quality) = 0;
};
```

#### 3.5 Refactor IrrlichtRenderer

Adapt existing `IrrlichtRenderer` to implement `GraphicalRenderer`:

```cpp
// irrlicht_renderer.h
class IrrlichtRenderer : public GraphicalRenderer {
    // Existing implementation with interface conformance

    RendererMode m_rendererMode;  // Software vs GPU

public:
    bool initialize(const RendererConfig& config) override;
    // ... implement all interface methods
};
```

#### 3.6 Plan Future Renderers

```cpp
// ascii_renderer.h (future)
class ASCIIRenderer : public IOutputRenderer {
    // Uses ncurses or similar for terminal graphics
    // Renders entities as ASCII characters on a grid
    // Mini-map style view of immediate surroundings
};

// mud_renderer.h (future)
class MUDRenderer : public IOutputRenderer {
    // Pure text descriptions of environment
    // "You are in the North Qeynos gate area.
    //  Exits: north, south, east, west
    //  You see: Guard Fippy (NPC), a merchant (NPC), PlayerName (Player)"
};

// topdown_renderer.h (future)
class TopDownRenderer : public GraphicalRenderer {
    // 2D overhead view using Irrlicht or SDL
    // Shows zone map with entity dots
    // Simpler rendering, lower resource usage
};
```

---

### Phase 4: Create Mode Controller - COMPLETED

**Status:** ✅ COMPLETE (2026-01-03)

**Goal:** Unify mode initialization and lifecycle management.

**What was implemented:**

1. **IGameMode Interface** (`include/client/mode/game_mode.h`)
   - OperatingMode enum: Automated, HeadlessInteractive, GraphicalInteractive
   - ModeConfig struct with display, path, and mode-specific options
   - Core interface: getMode(), getInputHandler(), getRenderer(), initialize(), shutdown(), update()
   - Quit handling: requestQuit(), isQuitRequested()
   - Factory function: createMode(OperatingMode)
   - String parser: parseModeString() for CLI argument parsing

2. **AutomatedMode** (`include/client/mode/automated_mode.h`, `src/client/mode/automated_mode.cpp`)
   - Uses NullInputHandler and NullRenderer (no user I/O)
   - AutomationCallback type for registering periodic automation functions
   - EventCallback type for subscribing to game state events
   - Command queue with thread-safe injection via queueCommand()
   - Methods: registerAutomation(), registerEventCallback(), queueCommand()
   - Access to GameState for programmatic queries

3. **HeadlessMode** (`include/client/mode/headless_mode.h`, `src/client/mode/headless_mode.cpp`)
   - Uses ConsoleInputHandler and ConsoleRenderer
   - Configurable: setVerbose(), setShowTimestamps(), setColorOutput()
   - Processes chat messages and move commands from console input
   - Displays command confirmations back to user
   - Event bus connected for real-time state updates

4. **GraphicalMode** (`include/client/mode/graphical_mode.h`, `src/client/mode/graphical_mode.cpp`)
   - GraphicalRendererType enum: IrrlichtSoftware, IrrlichtGPU, ASCII, TopDown, LowRes
   - Falls back to ConsoleRenderer when Irrlicht adapter not yet implemented
   - Input handling: processInput() for toggle actions (wireframe, HUD, name tags, camera mode, screenshot)
   - Camera control: setCameraMode(), cycleCameraMode(), getCameraMode()
   - Rendering options: toggleWireframe(), toggleHUD(), toggleNameTags(), saveScreenshot()
   - Zone management: loadZone(), unloadZone(), setLoadingProgress(), setZoneReady()
   - Input handler preference: renderer's handler if available, otherwise own handler

5. **Factory and Parser** (`src/client/mode/game_mode.cpp`)
   - createMode(): Creates appropriate mode instance for OperatingMode enum
   - parseModeString(): Parses CLI strings (automated/auto/bot, headless/console/text, graphical/gui)

6. **Build System**
   - Updated CMakeLists.txt with 4 new source files:
     - `src/client/mode/game_mode.cpp`
     - `src/client/mode/automated_mode.cpp`
     - `src/client/mode/headless_mode.cpp`
     - `src/client/mode/graphical_mode.cpp`
   - All code compiles successfully
   - 99% of tests pass (same 4 pre-existing failures)

**Files Created:**
```
include/client/mode/
├── game_mode.h        # IGameMode interface, OperatingMode, ModeConfig, factory
├── automated_mode.h   # Scripted/bot mode with callbacks
├── headless_mode.h    # Console interactive mode
└── graphical_mode.h   # Visual mode with renderer type selection

src/client/mode/
├── game_mode.cpp      # Factory and string parser
├── automated_mode.cpp
├── headless_mode.cpp
└── graphical_mode.cpp
```

**Next Steps (Phase 5):**
- Create ActionDispatcher to unify action handling
- Create InputActionBridge to connect input handlers to action dispatcher
- Migrate action handling from EverQuest class

#### 4.1 Define Mode Interface (Reference Design)

```cpp
// game_mode.h
enum class OperatingMode {
    Automated,
    HeadlessInteractive,
    GraphicalInteractive
};

class IGameMode {
public:
    virtual ~IGameMode() = default;

    virtual OperatingMode getMode() const = 0;
    virtual IInputHandler* getInputHandler() = 0;
    virtual IOutputRenderer* getRenderer() = 0;

    virtual bool initialize(GameState& state, const ModeConfig& config) = 0;
    virtual void shutdown() = 0;
    virtual bool update(float deltaTime) = 0;
};
```

#### 4.2 Implement Mode Classes (Reference Design)

```cpp
// automated_mode.h
class AutomatedMode : public IGameMode {
    NullInputHandler m_input;
    NullRenderer m_renderer;
    std::unique_ptr<ScriptEngine> m_scriptEngine;

public:
    OperatingMode getMode() const override { return OperatingMode::Automated; }

    // Scripting interface
    void loadScript(const std::string& scriptPath);
    void executeCommand(const std::string& command);
};

// headless_mode.h
class HeadlessMode : public IGameMode {
    ConsoleInputHandler m_input;
    ConsoleRenderer m_renderer;

public:
    OperatingMode getMode() const override { return OperatingMode::HeadlessInteractive; }
};

// graphical_mode.h
class GraphicalMode : public IGameMode {
    std::unique_ptr<GraphicalRenderer> m_renderer;

public:
    OperatingMode getMode() const override { return OperatingMode::GraphicalInteractive; }

    // Factory method for different graphical renderers
    static std::unique_ptr<GraphicalRenderer> createRenderer(GraphicalRendererType type);
};
```

---

### Phase 5: Refactor Action System - COMPLETED

**Status:** ✅ COMPLETE (2026-01-03)

**Goal:** Create a unified action dispatch system usable by all modes.

**What was implemented:**

1. **IActionHandler Interface** (`include/client/action/action_dispatcher.h`)
   - Abstract interface for objects that execute game actions
   - Methods for movement, combat, interaction, chat, group, character state, inventory, spellbook, trade, and zone actions
   - Designed to be implemented by EverQuest class or an adapter

2. **ActionDispatcher** (`include/client/action/action_dispatcher.h`, `src/client/action/action_dispatcher.cpp`)
   - Central dispatcher for all game actions
   - Validates actions before execution (zone connection, handler availability)
   - Returns ActionResult with success/failure and optional message
   - Direction enum for movement directions
   - ChatChannel enum for chat message types
   - 60+ action methods covering all game functionality
   - Helper methods: checkHandler(), checkZoneConnection(), calculateHeadingTo()

3. **InputActionBridge** (`include/client/action/input_action_bridge.h`, `src/client/action/input_action_bridge.cpp`)
   - Connects input handlers to the action dispatcher
   - Processes discrete actions (one-shot events)
   - Processes continuous input (movement, turning)
   - Processes mouse input (camera rotation)
   - Processes console commands via CommandProcessor
   - Processes chat messages, move commands, spell casts, target requests, loot requests, hotbar requests
   - Movement state tracking for start/stop transitions
   - Configurable: mouse sensitivity, invert Y, turn speed
   - Action callback for logging/UI feedback

4. **CommandProcessor** (`include/client/action/command_processor.h`, `src/client/action/command_processor.cpp`)
   - Processes text commands (slash commands)
   - 40+ built-in commands across categories: Chat, Movement, Combat, Group, Spells, Utility
   - Command registration with aliases, usage, description, category
   - Command completion for auto-complete support
   - Output to renderer for command feedback
   - Extensible via registerCommand() for custom commands

5. **Build System**
   - Updated CMakeLists.txt with 3 new source files:
     - `src/client/action/action_dispatcher.cpp`
     - `src/client/action/input_action_bridge.cpp`
     - `src/client/action/command_processor.cpp`
   - All code compiles successfully
   - 99% of tests pass (same 4 pre-existing failures)

6. **Interface Updates**
   - Added setShowTimestamps(), getShowTimestamps(), setVerbose(), getVerbose(), setColorOutput(), getColorOutput() to IOutputRenderer
   - Updated ConsoleRenderer with override specifiers

**Files Created:**
```
include/client/action/
├── action_dispatcher.h    # IActionHandler interface, ActionDispatcher, Direction, ChatChannel, ActionResult
├── input_action_bridge.h  # InputActionBridge for translating input to actions
└── command_processor.h    # CommandProcessor, CommandInfo for slash commands

src/client/action/
├── action_dispatcher.cpp
├── input_action_bridge.cpp
└── command_processor.cpp
```

**Built-in Commands:**
- Chat: /say, /shout, /ooc, /auction, /tell, /reply, /gsay, /gu, /emote
- Movement: /loc, /sit, /stand, /camp, /move, /moveto, /follow, /stopfollow, /face
- Combat: /target, /attack, /stopattack, /aa, /cast, /interrupt, /consider, /hail
- Group: /invite, /disband, /decline
- Spells: /skills, /gems, /mem, /forget
- Utility: /who, /help, /quit, /q, /debug, /timestamp, /door, /afk, /anon, /filter

**Next Steps (Phase 6):**
- Create Application class to orchestrate all components
- Refactor main.cpp to use the new architecture
- Connect ActionDispatcher to EverQuest via IActionHandler adapter

#### 5.1 Define Action Dispatcher (Reference Design)

```cpp
// action_dispatcher.h
class ActionDispatcher {
    GameState& m_state;
    NetworkClient& m_network;

public:
    // Movement actions
    void startMoving(Direction dir);
    void stopMoving(Direction dir);
    void setHeading(float heading);
    void jump();
    void sit();
    void stand();

    // Combat actions
    void attack(uint16_t targetId);
    void stopAttack();
    void castSpell(uint8_t gemSlot);
    void useSkill(SkillId skill);

    // Interaction actions
    void targetEntity(uint16_t spawnId);
    void hail();
    void openDoor(uint8_t doorId);
    void lootCorpse(uint16_t corpseId);

    // Chat actions
    void say(const std::string& message);
    void shout(const std::string& message);
    void tell(const std::string& target, const std::string& message);

    // Group actions
    void inviteToGroup(const std::string& playerName);
    void acceptGroupInvite();
    void leaveGroup();

    // This replaces the scattered action handling in EverQuest class
};
```

#### 5.2 Input-to-Action Bridge (Reference Design)

```cpp
// input_action_bridge.h
class InputActionBridge {
    IInputHandler& m_input;
    ActionDispatcher& m_dispatcher;

public:
    void update(float deltaTime) {
        m_input.update(deltaTime);

        // Continuous actions
        if (m_input.isActionActive(InputAction::MoveForward))
            m_dispatcher.startMoving(Direction::Forward);
        else
            m_dispatcher.stopMoving(Direction::Forward);

        // One-shot actions
        if (m_input.wasActionTriggered(InputAction::Attack))
            m_dispatcher.attack(m_state.combat().currentTarget());

        // Command input
        if (auto cmd = m_input.getCommandInput())
            m_commandProcessor.process(*cmd);
    }
};
```

---

### Phase 6: Refactor Main Application - COMPLETED

**Status:** ✅ COMPLETE (2026-01-03)

**Goal:** Create a clean application entry point that supports all modes.

**What was implemented:**

1. **ApplicationConfig** (`include/client/application.h`)
   - Configuration struct containing all application settings
   - Connection settings: host, port, user, pass, server, character
   - Path settings: configFile, navmeshPath, mapsPath, eqClientPath
   - Display settings: displayWidth, displayHeight, fullscreen
   - Mode settings: operatingMode, graphicalRendererType
   - Feature flags: pathfindingEnabled, graphicsEnabled
   - Logging: debugLevel

2. **Application Class** (`include/client/application.h`, `src/client/application.cpp`)
   - Central orchestration of all components
   - Owns: GameState, IGameMode, ActionDispatcher, InputActionBridge, CommandProcessor
   - Owns: EverQuest client and EqActionHandler adapter
   - Lifecycle: initialize(), run(), shutdown(), requestQuit()
   - Main loop stages: processNetworkEvents(), processInput(), updateGameState(), render()
   - State synchronization between EverQuest client and GameState
   - Static helpers: parseArguments(), loadConfigFile()

3. **EqActionHandler Adapter** (`include/client/eq_action_handler.h`, `src/client/eq_action_handler.cpp`)
   - Implements IActionHandler interface
   - Delegates all action calls to EverQuest class methods
   - Bridges ActionDispatcher to existing EverQuest functionality
   - Movement: direction mapping, pathfinding, follow
   - Combat: targeting via CombatManager, auto-attack, spell casting
   - Interaction: hail, doors, looting, consider
   - Chat: channel mapping, tells
   - Group: invites, accept/decline, leave
   - Character state: AFK, anonymous, camp
   - Stubs for features not yet exposed: sneak toggle, inventory moves, spellbook

4. **Build System**
   - Updated CMakeLists.txt with 2 new source files:
     - `src/client/application.cpp`
     - `src/client/eq_action_handler.cpp`
   - All code compiles successfully
   - 99% of tests pass (same 4 pre-existing failures)

**Files Created:**
```
include/client/
├── application.h      # Application class, ApplicationConfig
└── eq_action_handler.h # EqActionHandler adapter

src/client/
├── application.cpp
└── eq_action_handler.cpp
```

**Notes:**
- The Application class is designed to be a drop-in replacement for the current main.cpp
- The existing main.cpp remains functional and unchanged for backward compatibility
- Migration to Application-based main can be done incrementally
- Some IActionHandler methods are stubbed pending public exposure of EverQuest internals

**Next Steps (Phase 7):**
- Implement additional graphical renderers (ASCII, MUD, TopDown)
- Or: Migrate main.cpp to use Application class
- Or: Add public accessors to EverQuest for remaining action handler stubs

#### 6.1 New Application Class (Reference Design)

```cpp
// application.h
class Application {
    std::unique_ptr<GameState> m_state;
    std::unique_ptr<NetworkClient> m_network;
    std::unique_ptr<IGameMode> m_mode;
    std::unique_ptr<ActionDispatcher> m_dispatcher;
    std::unique_ptr<InputActionBridge> m_inputBridge;

    bool m_running = false;

public:
    bool initialize(const ApplicationConfig& config);
    void run();
    void shutdown();

private:
    void mainLoop();
    void processNetworkEvents();
    void processInput();
    void updateGameState(float deltaTime);
    void render(float deltaTime);
};
```

#### 6.2 Simplified main.cpp (Reference Design)

```cpp
int main(int argc, char* argv[]) {
    ApplicationConfig config = parseArguments(argc, argv);

    Application app;
    if (!app.initialize(config)) {
        return 1;
    }

    app.run();
    app.shutdown();

    return 0;
}
```

---

### Phase 7: Implement Additional Graphical Renderers (Future)

Once the architecture is in place, additional renderers can be implemented:

#### 7.1 ASCII Renderer

```cpp
class ASCIIRenderer : public IOutputRenderer {
    // Terminal-based rendering using ncurses
    // Character grid representing map
    // Status bars for HP/Mana
    // Chat window area
    // Entity list sidebar

    void render() {
        // Clear and redraw
        // @ = player
        // M = mob/NPC
        // P = other player
        // C = corpse
        // D = door
        // . = empty floor
        // # = wall
    }
};
```

#### 7.2 MUD Renderer

```cpp
class MUDRenderer : public IOutputRenderer {
    // Pure text adventure style
    // Describe room on enter
    // List exits
    // List visible entities
    // Show combat as text narrative

    void onZoneChanged() {
        std::cout << "\n=== " << m_zoneName << " ===\n";
        std::cout << getZoneDescription() << "\n\n";
        std::cout << "Obvious exits: " << getExits() << "\n";
        std::cout << "You see: " << getVisibleEntities() << "\n";
    }
};
```

#### 7.3 Top-Down Renderer

```cpp
class TopDownRenderer : public GraphicalRenderer {
    // 2D overhead view
    // Zone map as background
    // Colored dots for entities
    // Click to target
    // Simple UI panels
};
```

---

## File Structure

```
include/client/
├── state/
│   ├── game_state.h
│   ├── player_state.h
│   ├── entity_manager.h
│   ├── world_state.h
│   ├── combat_state.h
│   ├── spell_state.h
│   ├── skill_state.h
│   ├── group_state.h
│   ├── inventory_state.h
│   ├── door_state.h
│   └── event_bus.h
├── input/
│   ├── input_handler.h
│   ├── null_input_handler.h
│   ├── console_input_handler.h
│   └── graphics_input_handler.h
├── output/
│   ├── output_renderer.h
│   ├── null_renderer.h
│   ├── console_renderer.h
│   ├── graphical_renderer.h
│   └── renderer_config.h
├── mode/
│   ├── game_mode.h
│   ├── automated_mode.h
│   ├── headless_mode.h
│   └── graphical_mode.h
├── action/
│   ├── action_dispatcher.h
│   ├── input_action_bridge.h
│   └── command_processor.h
├── graphics/
│   ├── irrlicht_renderer.h (refactored)
│   ├── ascii_renderer.h (future)
│   ├── mud_renderer.h (future)
│   └── topdown_renderer.h (future)
└── application.h

src/client/
├── state/
│   ├── game_state.cpp
│   ├── player_state.cpp
│   ├── entity_manager.cpp
│   └── ... (implementations)
├── input/
│   ├── console_input_handler.cpp
│   └── graphics_input_handler.cpp
├── output/
│   ├── console_renderer.cpp
│   └── ... (implementations)
├── mode/
│   ├── automated_mode.cpp
│   ├── headless_mode.cpp
│   └── graphical_mode.cpp
├── action/
│   ├── action_dispatcher.cpp
│   ├── input_action_bridge.cpp
│   └── command_processor.cpp
├── graphics/
│   └── irrlicht_renderer.cpp (refactored)
├── application.cpp
└── main.cpp (simplified)
```

---

## Migration Strategy

### Step 1: Create State Classes (Non-Breaking)
- Create new state classes alongside existing code
- Add forwarding getters/setters in EverQuest that delegate to state classes
- Existing code continues to work

### Step 2: Create Abstractions (Non-Breaking)
- Define interfaces (IInputHandler, IOutputRenderer, IGameMode)
- Create adapters that wrap existing implementations
- No changes to existing behavior

### Step 3: Migrate Incrementally
- Move functionality from EverQuest to appropriate state/action classes
- Update call sites one at a time
- Run tests after each migration

### Step 4: Refactor Entry Point
- Create Application class that uses new architecture
- Keep old main.cpp as fallback during transition
- Switch over when confident

### Step 5: Add New Renderers
- Implement ASCII renderer as proof of concept
- Add MUD renderer for text-only mode
- Add top-down renderer for lightweight graphics

---

## Testing Strategy

1. **Unit Tests**: Test state classes in isolation
2. **Integration Tests**: Test input→action→state→output flow
3. **Mode Tests**: Verify each mode initializes and runs correctly
4. **Regression Tests**: Ensure existing functionality preserved
5. **Renderer Tests**: Visual verification for each renderer type

---

## Configuration

```json
{
    "mode": "graphical",  // "automated", "headless", "graphical"
    "renderer": {
        "type": "irrlicht",  // "irrlicht", "irrlicht_gpu", "ascii", "mud", "topdown"
        "width": 800,
        "height": 600,
        "fullscreen": false
    },
    "automation": {
        "script": "scripts/bot.lua",
        "enabled": false
    },
    "eq_client_path": "/path/to/EverQuest"
}
```

---

## Summary

This refactoring plan separates WillEQ into clean layers:

1. **State Layer** - Single source of truth for all game data
2. **Input Layer** - Abstract input handling for all modes
3. **Action Layer** - Unified action dispatch system
4. **Output Layer** - Pluggable renderers for different output types
5. **Mode Layer** - Combines input/output for each operating mode

The architecture enables:
- Running the same game logic in any mode
- Adding new renderers without changing core code
- Scripting/automation using the same action system as manual play
- Testing game logic without graphics dependencies
