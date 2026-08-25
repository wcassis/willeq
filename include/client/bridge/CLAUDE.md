# Event/Intent Bridge

## Overview

The bridge is the sole communication channel between the game thread and the render thread. Game state changes flow as **events** (game → renderer). User actions flow as **intents** (renderer → game). No direct method calls cross the thread boundary.

## Components

**GameStateBridge** (`game_state_bridge.h`) — Abstract base with dual swap-vector queues:
- `pushEvent(GameEvent)` — game thread enqueues state change
- `drainEvents()` → calls `applyEvent()` for each — the consumer thread drains (render thread for plain bridges; GAME thread via `GameStateSyncBridge::projectPendingEvents()` in graphics runs since T220, see below)
- `pushIntent(RendererIntent)` — renderer thread enqueues user action
- `drainIntents()` → returns vector — game thread consumes
- Two independent mutexes (events and intents never contend)
- **Swap-vector pattern**: drain swaps the entire vector in O(1), then processes outside the lock

**IrrlichtBridge** (`irrlicht_bridge.h`) — Concrete bridge for 3D rendering:
- `applyEvent()` implements an 839+ case switch translating events into `IrrlichtRenderer` method calls
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

## Event Types (140+)

Defined in `include/client/state/event_bus.h`. See `include/client/state/CLAUDE.md` for the full categorization.

## Intent Types (50+)

Defined in `include/client/events/renderer_intents.h`. `RendererIntent` is a `std::variant` holding all intent structs:

- **Movement**: `PlayerPositionChanged` (position + velocity)
- **Targeting**: `TargetIntent` (T51: `AttackIntent`/`ToggleAutoAttackIntent` removed -- never had a producer; auto-attack is reached via direct `CombatManager` calls from `eq_commands_movement.cpp`/`ActionDispatcher`, not the intent system)
- **Interaction**: `DoorInteractIntent`, `LootCorpseIntent`, `ZoningEnabledIntent`
- **Commerce**: `VendorBuyIntent`, `VendorSellIntent`, `BankerInteractIntent`, `TradeRequestIntent`
- **Spells**: `CastSpellIntent`, `MemorizeSpellIntent`, `ScribeSpellIntent`, `InterruptSpellIntent`
- **Skills**: `SkillActivateIntent`
- **Pet**: `PetCommandIntent`
- **Group**: `GroupInviteIntent`, `GroupAcceptIntent`, `DisbandIntent`
- **Hotbar**: `HotbarActivateIntent`, `HotbarChangedIntent`
- **Inventory**: `MoveItemIntent`, `DeleteItemIntent`, `EquipmentChangedIntent`
- **Chat**: `ChatSubmitIntent`, `ChatLinkClickIntent`
- **Audio**: `MusicVolumeChangeIntent`, `EffectsVolumeChangeIntent`
- **Diagnostics**: `RequestMemoryReport`, `RequestSceneDump`, `SlashCommandIntent`
- **Tradeskill**: `TradeskillCombineIntent`, `TradeskillCloseIntent`

## Two-stage drain in graphics runs (T220)

`GameStateSyncBridge` (`game_state_sync_bridge.h`) decorates the mode bridge
and projects events into `Application::m_gameState` -- the READ model the
game-thread action/command layer consults. Before T220 that projection ran
on the render thread (`Application::render()` -> `EverQuest::PreRenderTick()`
-> `ProcessBridgeEvents()`), racing every game-thread read of that state with
no lock (TSAN-confirmed: `CombatState::setTarget()` on the render thread vs
`CombatState::hasTarget()` under `/hail` on the game thread). The drain is
now split, and `PreRenderTick()` is gone:

- GAME thread, `Application::updateGameState()` ->
  `GameStateSyncBridge::projectPendingEvents()`: `drainEvents()` (D24-coalesced),
  apply the projection switch, append the batch to a second FIFO.
- RENDER thread, `Application::render()` ->
  `GameStateSyncBridge::forwardProjectedEvents()`: swap out that FIFO,
  re-coalesce, `IrrlichtBridge::applyEvent()` each. Never touches GameState.

Renderer-visible order is unchanged (single-producer FIFOs both hops).
Headless runs keep the single-stage `EverQuest::ProcessBridgeEvents()` ->
`applyEvent()` (project + forward) on the game thread; so do single-threaded
test harnesses. Do not call `ProcessBridgeEvents()` from a graphics run.

## Coalescing (D24)

High-frequency events are deduplicated before delivery:
- `coalesceEvents()` — multiple `EntityMoved` events per spawn_id collapse to the latest
- `coalesceIntents()` — multiple `PlayerPositionChanged` intents collapse to the latest

## Key Design Rules

- **EverQuest has zero graphics dependencies**: No `m_renderer` member, no graphics includes
- **Bridge is the only crossing point**: All 343+ former direct `m_renderer->` calls were replaced with events
- **Events are immutable**: Safe to read from any thread after drain
- **Multi-renderer support**: Any `GameStateBridge` subclass can consume events (Irrlicht, Console, future RDP)
