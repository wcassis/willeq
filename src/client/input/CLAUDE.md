# Input & Action System

## Overview

Input flows through a four-layer pipeline: capture → hotkey resolution → action dispatch → game execution. The input and action systems live in separate directories (`input/` and `action/`) to maintain separation of concerns.

## Input Layer (`include/client/input/`)

**IInputHandler** (`input_handler.h`) — Abstract interface defining the input contract:
- `InputAction` enum (79 discrete one-shot actions): Quit, Screenshot, Jump, ToggleAutoAttack, TargetSelf, ToggleInventory, etc.
- `InputState` struct for continuous state (movement direction, mouse position)
- Request queues: `SpellCastRequest`, `HotbarRequest`, `TargetRequest`, `LootRequest`, `ChatMessage`
- Implementations: `GraphicsInputHandler` (Irrlicht), `ConsoleInputHandler` (text), `NullInputHandler` (headless)

**GraphicsInputHandler** (`graphics_input_handler.h`) — Wraps Irrlicht's `RendererEventReceiver`:
- Translates keyboard/mouse events to `IInputHandler` interface
- Internal queues for target/loot requests injected by renderer on entity clicks
- Handles movement keys, mouse input, text input, debug toggles

**HotkeyManager** (`hotkey_manager.h`) — Configurable key binding system:
- `HotkeyAction` enum (150+ actions) covering movement, toggles, targeting, interaction
- `HotkeyBinding` struct: key code + `ModifierFlags` (Ctrl/Shift/Alt) + action
- `HotkeyMode` enum: Global (always active) vs Player (gameplay only)
- Loads/saves from `config/hotkeys.json`, supports runtime overrides from `willeq.json`
- Fast hash lookup with separate movement key map for continuous (held) keys
- Conflict detection between bindings
- String parsing (e.g., "Ctrl+Shift+F1")

## Action Layer (`include/client/action/`)

**CommandProcessor** (`command_processor.h`) — Slash command text processing:
- Maps command name → handler function via `CommandInfo` structs
- Built-in commands: chat (/say, /tell), movement (/loc, /sit), combat (/target, /cast), group (/invite), utility (/who, /help)
- Alias support, tab completion, category browsing
- Returns `ActionResult` (success + message)

**ActionDispatcher** (`action_dispatcher.h`) — Central hub for all game actions:
- 100+ action methods covering movement, combat, interaction, chat, group, inventory, spells, skills, pet, trade
- Validates via `GameState`, delegates to `IActionHandler` (typically `EverQuest`)
- `Direction` enum (Forward/Backward/Left/Right/Up/Down)
- `ChatChannel` enum (Say/Shout/OOC/Tell/Group/Guild/Raid/Emote)

**InputActionBridge** (`input_action_bridge.h`) — Glues input to actions each frame:
- `update()` called per frame, processes all input types:
  - Discrete actions (one-shot keys → action methods)
  - Continuous input (held movement keys → direction changes)
  - Mouse input (camera/targeting)
  - Chat messages (text → CommandProcessor)
  - Spell casts, target requests, loot requests, hotbar presses
- Configurable: mouse sensitivity, Y-axis inversion, turn speed
- Optional `ActionCallback` for logging/UI feedback

## Data Flow

```
OS keyboard/mouse events
        ↓
GraphicsInputHandler (IInputHandler)
        ↓
InputActionBridge.update()
   ↙          ↘
IInputHandler   CommandProcessor
(discrete/      (/command text)
 continuous)          ↓
     ↓          ActionDispatcher
ActionDispatcher  (100+ methods)
     ↓                ↓
IActionHandler    IActionHandler
(EverQuest)       (EverQuest)
     ↓                ↓
Network packets / GameState updates
```
