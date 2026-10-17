# Input & Action System

## Overview

Input flows through a four-layer pipeline: capture → hotkey resolution → action dispatch → game execution. The input and action systems live in separate directories (`input/` and `action/`) to maintain separation of concerns.

## Input Layer (`include/client/input/`)

**IInputHandler** (`input_handler.h`) — Abstract interface defining the input contract:
- `InputAction` enum (**33** discrete one-shot actions plus a `Count` sentinel, re-counted T371): Quit, Screenshot, Jump, ToggleAutoAttack, TargetSelf, etc. J21's 40 is stale — T365 deleted `ToggleInventory`/`ToggleSkills`/`ToggleGroup` and T368 deleted `ToggleVendor`/`TogglePetWindow`/`ToggleTrainer`/`ToggleSpellbook` (board B090/B100), all seven having no consumer in `InputActionBridge::processDiscreteActions()`, and B534 deleted `ToggleCameraMode` (issue #181: its only consumer was `GraphicalMode`'s arm, which reached `IOutputRenderer`'s empty default body; no input handler ever produced it — the live F9 route is `HotkeyAction`/`RendererAction::ToggleCameraMode`, a different enum). `InputActionEnumTest.T368_NoConsumerlessUITogglesRemain` pins it; the UI toggles are renderer-side `RendererAction`s, a different enum
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
- `HotkeyAction` enum (**95** actions plus a `Count` sentinel, re-counted UIGAP-small -- T471 read 102, T461 99, T415 95, T371 94; B905 (#225) DELETED eight Global actions that were bound and had no consumer (`ToggleUILock`, `SaveUILayout`, `ResetUIDefaults`, `ConfirmDialog`, `CancelDialog`, `SubmitInput`, `CancelInput`, `ChatAutocomplete`): none is on the oracle's Options > Keys pages and, Global, they shadowed Tab (CycleTargets), Escape and Return. `tests/test_hotkey_dispatch_census.cpp` presses every default AND shipped binding on a real `RendererEventReceiver` and fails on a shadowed binding or one with no consumer; the key table grows with every UI panel, T461 added the four camera-orbit actions and T471 `CameraRaise`/`CameraLower`, binding all six to the official Camera-page keys T466 measured) covering movement, toggles, targeting, interaction
- `HotkeyBinding` struct: key code + `ModifierFlags` (Ctrl/Shift/Alt) + action
- `HotkeyMode` enum: Global (always active) vs Player (gameplay only)
- Loads/saves from `config/hotkeys.json`, supports runtime overrides from `willeq.json`
- **A file that loads REPLACES the compiled-in defaults** (`loadFromFile()` clears every binding; the defaults are only the fallback for a file that cannot be read), so a default binding must be in `setupDefaults()` AND the shipped `config/hotkeys.json`. T464 (board B535) found five that production never reached (Alt+C, L, F10, Shift+F10, Shift+F11); `HotkeyShippedFileTest.T464_TheShippedFileCarriesEveryDefaultBinding` compares the two tables binding by binding. The B561 exemption (`MusicVolumeUp` = F11, shadowed by F11's Global `ToggleAllUI`) is DELETED (B582, ruling 2026-09-18): shipped file and defaults now agree with no exception -- F10 = `ToggleAllUI` (Global), F11 and L unbound, no volume key on any oracle page -- pinned by `HotkeyShippedFileTest.B582_TheOfficialKeymapIsBound` and `B582_APreB582UserFileStillLoads` (a user copy predating B582 still loads its volume bindings)
- **B433: user rebinding** (the Options window's Keys page, `hotkey_manager_user.cpp`): `setBinding()` takes the key off every other action first, `saveUserBindings()` writes the whole set to `<cache dir>/hotkeys.json` (never the shipped file), the client loads that copy at startup when it exists (`startupBindingsPath()`), and `restoreShippedBindings()` deletes it (Reload defaults). Still global, never per character
- Fast hash lookup with separate movement key map for continuous (held) keys
- Conflict detection between bindings
- String parsing (e.g., "Ctrl+Shift+F1")

## Action Layer (`include/client/action/`)

**CommandProcessor** (`command_processor.h`) — Slash command text processing.
Implementation split across eight TUs in `src/client/action/` (J04, plus
T268/T269/T280): `command_processor.cpp` holds the core (parse, dispatch,
registry queries), `command_processor_registration.cpp` holds
`registerBuiltinCommands()` and its **157** `registerCommand({…})` calls
(T347 added `/cleartarget`, B899 `/bag`, B897 `/friends` and `/ignore`; `tests/goldens/command_registration_order_g03.txt`
pins the count and the order — read the golden, not this line), and the
handlers live in `command_processor_{social,spells_gm,world,bank,loot,train}.cpp`
behind the shared private header `command_processor_internal.h`. See
`docs/module_map.md`.
- Maps command name → handler function via `CommandInfo` structs
- Built-in commands: chat (/say, /tell), movement (/loc, /sit), combat (/target, /cast), group (/invite), utility (/who, /help)
- Alias support, tab completion, category browsing
- `getCompletions()` is reached from the in-game chat box's Tab key (T357/B057) via `Application::commandCompletions()`, which merges it with `eqt::ui::CommandRegistry::getCompletions()` and, since T365 (B091), with registry 3's `eqt::help::RendererCommandCompletions()` -- all three registries, matching what `/help` lists (it listed three and Tab offered two until T365); the consumer is `FrameComponent::applyCommandCompletion()` in `src/client/graphics/frame/renderer_frame_input.cpp`. Both registries' `getCompletions()` bodies are one shared helper since T362 (`include/client/command_completion.h`)
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
