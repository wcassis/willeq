# Event/Intent Bridge

## Overview

The bridge is the sole communication channel between the game thread and the render thread. Game state changes flow as **events** (game → renderer). User actions flow as **intents** (renderer → game). No direct method calls cross the thread boundary.

## Components

**GameStateBridge** (`game_state_bridge.h`) — Abstract base with dual swap-vector queues:
- `pushEvent(GameEvent)` — game thread enqueues state change
- `drainEvents()` → calls `applyEvent()` for each — the consumer thread drains (render thread for plain bridges; GAME thread via `GameStateSyncBridge::stagePendingEvents()` in graphics runs since T220, see below)
- `pushIntent(RendererIntent)` — renderer thread enqueues user action
- `drainIntents()` → returns vector — game thread consumes
- Two independent mutexes (events and intents never contend)
- **Swap-vector pattern**: drain swaps the entire vector in O(1), then processes outside the lock

**IrrlichtBridge** (`irrlicht_bridge.h`) — Concrete bridge for 3D rendering:
- `applyEvent()` implements a 97-case switch (one per `GameEventType`, counted 2026-09-03) translating events into `IrrlichtRenderer` method calls
- Owns a pointer to `IrrlichtRenderer` (set via `setRenderer()`)
- Implementation in `src/client/bridge/irrlicht_bridge.cpp`

**ConsoleBridge** (`console_bridge.h`) — Lightweight bridge for headless mode:
- `applyEvent()` logs events to console output
- Zero graphics dependencies
- Used with `--no-graphics` flag

## Data Flow

```
Game Thread                          Render Thread (main)
───────────                          ────────────────────
EverQuest::tick()                    processFrame()
├─ Network packets received          ├─ bridge.drainEvents()
├─ GameState updated                 │  └─ applyEvent() per event
├─ bridge.pushEvent(...)             │     └─ renderer method calls
│  (state changes)                   │
└─ bridge.drainIntents()             └─ bridge.pushIntent(...)
   └─ process user actions              (user actions)
```

## Event Types (100)

Defined in `include/client/state/event_bus.h`. See `include/client/state/CLAUDE.md` for the full categorization.

## Intent Types (46)

Defined in `include/client/events/renderer_intents.h`. `RendererIntent` is a `std::variant` holding all intent structs:

- **Movement**: `PlayerPositionChanged` (position + velocity)
- **Targeting**: `TargetIntent` (T51: `AttackIntent`/`ToggleAutoAttackIntent` removed -- never had a producer; auto-attack is reached via direct `CombatManager` calls from `src/client/commands/commands_movement.cpp` (K12 moved it off the flat `eq_commands_movement.cpp`)/`ActionDispatcher`, not the intent system)
- **Interaction**: `DoorInteractIntent`, `LootCorpseIntent`, `LootItemIntent`, `LootAllIntent`, `CloseLootIntent`, `WorldObjectInteractIntent`, `ZoningEnabledIntent`, `ReadItemIntent`
- **Commerce**: `VendorToggleIntent`, `VendorBuyIntent`, `VendorSellIntent`, `BankerInteractIntent`, `CloseBankIntent` (T436 -- `BankerInteractIntent`'s handler returns early on an already-open window, so the bank window's Done button and close box needed a close-only intent, the `CloseTrainerIntent` shape), `BankCurrencyMoveIntent`, `BankCurrencyConvertIntent`
- **Spells**: `CastSpellIntent`, `MemorizeSpellIntent`, `ScribeSpellIntent`, `SpellbookStateIntent`
- **Buffs**: `BuffCancelIntent`
- **Skills**: `SkillActivateIntent`
- **Hotbar**: `HotbarActivateIntent`
- **Inventory**: `MoveItemIntent`, `DeleteItemIntent`, `EquipmentChangedIntent`, `CursorGestureIntent` (B442: every item window's slot click, the QuantityWnd's Accept, a purse row, and `/pickup` `/putdown` `/coin` -- one intent, resolved on the game thread by `eqt::inventory::ApplyCursorGesture()` / `PlanCoinGesture()` in `include/client/inventory/cursor_gesture.h`, which calls the existing `InventoryManager` pickup/place bodies that push `MoveItemIntent`; B442b added the ops `ToggleBag` (a bag's right click -> the `/bag` body), `RequestDrop`/`ConfirmDrop` (`OP_GroundSpawn`) and `RequestDestroy`/`ConfirmDestroy` (`OP_MoveItem 30 -> -1`), and `coinSource` for the bank's coin rows -- no new intent struct; its game-side replies are `GameEventType::CursorCoinChanged` and `ConfirmationRequested`)
- **Chat**: `ChatSubmitIntent`
- **Social**: `SocialListIntent` (B897 -- the Friends window's Add / Delete / Who; its handler runs the same `eqt::state::ApplySocialEdit()` / `RunFriendsWho()` bodies `/friends`, `/ignore` and `/friendswho` run, and the edited lists come back on `GameEventType::SocialListsChanged`, published once per `GameTick` by `BridgeComponent::SyncSocialLists()`)
- **Audio**: `MusicVolumeChangeIntent`, `EffectsVolumeChangeIntent` (T439, board B432: both carry an ABSOLUTE step 0..20 -- the oracle's slider unit, `include/client/audio/volume_steps.h` -- not a delta; the applied value comes back on `GameEventType::VolumesChanged`), `PlaySoundIntent` (T328 Leg B: the measured UI sounds — itemclth, wearclth, BuyItem)
- **Options window** (B430/B433): `GameOptionIntent` -- a server-owned Options setting (anonymous/roleplay, autosplit, the three auto-consents as `OP_SpawnAppearance`; the Filters page as the whole `OP_SetServerFilter` payload). One handler, `BridgeComponent::ApplyGameOption()` (`bridge_component_options.cpp`), which publishes the applied value back on `GameEventType::GameOptionChanged`, the `VolumesChanged` shape
- **Trainer**: `TrainerToggleIntent`, `TrainSkillIntent`, `CloseTrainerIntent` (B898: the trainer window's Train / Done buttons push the last two, the same intents `/train <id>` / `/trainclose` push)
- **Tradeskill**: `TradeskillCombineIntent`, `TradeskillCloseIntent`
- **Model**: `PlayerModelGeometryChanged`
- **Tracking**: `TrackingIntent` (B895 -- the Tracking Window's Track button, filter toggles and two comboboxes, one intent onto `TrackingController`, the bodies `/track` and `/track*` reach)

## Two-stage drain in graphics runs (T220, T227)

`GameStateSyncBridge` (`game_state_sync_bridge.h`) decorates the mode bridge
and is the ONE bridge instance a run has. Since T227 there is also ONE
`state::GameState` -- `EverQuest::m_game_state`, written by the packet
handlers and read by the action/command layer, both on the GAME thread --
so the decorator projects nothing; it only stages and forwards. (Before
T227 it projected events into a second, Application-owned copy; before T220
that projection ran on the render thread and raced the game thread --
TSAN-confirmed. `PreRenderTick()` is gone.)

- GAME thread, `Application::updateGameState()` ->
  `GameStateSyncBridge::stagePendingEvents()`: `drainEvents()` (D24-coalesced),
  append the batch to a second FIFO.
- RENDER thread, `Application::render()` ->
  `GameStateSyncBridge::forwardStagedEvents()`: swap out that FIFO,
  re-coalesce, `IrrlichtBridge::applyEvent()` each. Never touches GameState.

Renderer-visible order is unchanged (single-producer FIFOs both hops).
Headless runs keep the single-stage `EverQuest::ProcessBridgeEvents()` ->
`applyEvent()` (forward) on the game thread; so do single-threaded test
harnesses. Do not call `ProcessBridgeEvents()` from a graphics run.

## Coalescing (D24)

High-frequency events are deduplicated before delivery:
- `coalesceEvents()` — multiple `EntityMoved` events per spawn_id collapse to the latest
- `coalesceIntents()` — multiple `PlayerPositionChanged` intents collapse to the latest

## Key Design Rules

- **EverQuest has zero graphics dependencies**: No `m_renderer` member, no graphics includes
- **Bridge is the only crossing point**: every former direct `m_renderer->` call was replaced with an event
- **A game-side pose change reaches the renderer only as a renderer-bridge `PlayerMoved`** (B911/B912). `PlayerFacade::SetPosition/SetHeading` fire `PlayerMoved` on the STATE layer's `EventBus`, which never reaches the renderer. Game-thread movers publish through `MovementComponent::PublishSelfPoseToRenderer()` (`/turn`, `/face`, the command-walk tick and its `StopMovement()`); `PlayerMovedData::isMoving` there means "the game thread is walking the player" and the renderer latches it until the next `PlayerMoved`
- **Events are immutable**: Safe to read from any thread after drain
- **Multi-renderer support**: Any `GameStateBridge` subclass can consume events (Irrlicht, Console, future RDP)
