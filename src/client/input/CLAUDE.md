# Input & Action System

## Overview

Input flows through a four-layer pipeline: capture → hotkey resolution → action dispatch → game execution. The input and action systems live in separate directories (`input/` and `action/`) to maintain separation of concerns.

## Input Layer (`include/client/input/`)

**IInputHandler** (`input_handler.h`) — Abstract interface defining the input contract:
- `InputAction` enum (40 discrete one-shot actions plus a `Count` sentinel, re-counted J21): Quit, Screenshot, Jump, ToggleAutoAttack, TargetSelf, ToggleInventory, etc.
- Continuous state (movement, mouse position) is polled at the point of
  use, on the thread that uses it -- there is deliberately no snapshot
  struct. T315 deleted `InputState`: it had no reader, and filling it
  stole the render thread's clicks and mouse deltas.
- Request queues: `SpellCastRequest`, `HotbarRequest`, `TargetRequest`, `LootRequest`, `ChatMessage`
- Implementations: `GraphicsInputHandler` (Irrlicht), `ConsoleInputHandler` (text), `NullInputHandler` (headless)

**GraphicsInputHandler** (`graphics_input_handler.h`) — Wraps Irrlicht's `RendererEventReceiver`:
- Translates keyboard/mouse events to `IInputHandler` interface
- Internal queues for target/loot requests injected by renderer on entity clicks
- Handles movement keys, mouse input, text input, debug toggles

**HotkeyManager** (`hotkey_manager.h`) — Configurable key binding system:
- `HotkeyAction` enum (80 actions plus a `Count` sentinel, counted 2026-09-03) covering movement, toggles, targeting, interaction
- `HotkeyBinding` struct: key code + `ModifierFlags` (Ctrl/Shift/Alt) + action
- `HotkeyMode` enum: Global (always active) vs Player (gameplay only)
- Loads/saves from `config/hotkeys.json`, supports runtime overrides from `willeq.json`
- Fast hash lookup with separate movement key map for continuous (held) keys
- Conflict detection between bindings
- String parsing (e.g., "Ctrl+Shift+F1")

## Action Layer (`include/client/action/`)

**CommandProcessor** (`command_processor.h`) — Slash command text processing.
Implementation split across eight TUs in `src/client/action/` (J04, plus
T268/T269/T280): `command_processor.cpp` holds the core (parse, dispatch,
registry queries), `command_processor_registration.cpp` holds
`registerBuiltinCommands()` and its 142 `registerCommand({…})` calls, and the
handlers live in `command_processor_{social,spells_gm,world,bank,loot,train}.cpp`
behind the shared private header `command_processor_internal.h`. See
`docs/module_map.md`.
- Maps command name → handler function via `CommandInfo` structs
- Built-in commands: chat (/say, /tell), movement (/loc, /sit), combat (/target, /cast), group (/invite), utility (/who, /help)
- Alias support, tab completion, category browsing
- Returns `ActionResult` (success + message)

**ActionDispatcher** (`action_dispatcher.h`) — Central hub for all game actions:
- ~182 action methods covering movement, combat, interaction, chat, group, inventory, spells, skills, pet, trade
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

## Action-layer migration (T97–T143) and hotbar dispatch (T174)

Every slash command that reaches the game now goes through
`action::CommandProcessor` → `ActionDispatcher` → `IActionHandler`
(`EqActionHandler`, `src/client/action/eq_action_handler.cpp`). T97 audited
reachability (34/65 `CommandProcessor` commands unreachable, shadowed by the
game registry); T98 deleted the shadowed copies; T99 (Group, the pilot) through
T140 migrated every family (Chat, Spells/Pet, Combat/Social, Trade, Guild,
Corpse/Resurrection, Utility, Merchant, the final sixteen); T141 migrated
`/equip` after extracting its logic into `InventoryManager`; T139 deleted the
34 game-registry commands that had become redundant; T142 routed the two
renderer singletons through the bridge; T143 made `/help` registry-aware. The
recipe for a command whose mechanism is a bridge intent rather than a packet
is a thin `EverQuest::Request*` wrapper (T136/T138) — `EqActionHandler` has no
bridge pointer by design.

`InputActionBridge` dispatches keyboard hotbar presses by the slot's content
type (T174, `InputActionBridge::processHotbarRequests()`). Every slot type is
routed: Spell → `castSpell`, Skill → `useSkill`, Item → `useItemById` (T230
resolves the item id to an inventory slot through
`IActionHandler::findItemSlotByItemId`, the same lookup the mouse path uses),
and Emote/Macro/Combat → `activateHotbarCommandSlot` (T239, the same body as the
mouse path). The DEBUG-logged skips T174 shipped are gone.

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
