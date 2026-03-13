# Batch D — Game State Threading

## Goal

Decouple game state (network, combat, inventory, spells, chat) from rendering into
separate threads. Game state becomes an "island" that publishes events and receives
intents. One or more renderers (3D, console, etc.) connect via a bridge adapter.

## Architecture

```
┌─────────────────┐     Events      ┌──────────────┐     Commands     ┌──────────────┐
│   Game Thread    │ ──────────────► │    Bridge     │ ──────────────► │ Renderer(s)  │
│                  │                 │  (per-renderer│                 │              │
│  EverQuest       │ ◄────────────── │   adapter)   │ ◄────────────── │ Irrlicht     │
│  Network         │   Intents       │              │   Input Events  │ Console      │
│  Combat          │                 └──────────────┘                 │ etc.         │
│  Inventory       │                                                  └──────────────┘
│  Spells          │
└─────────────────┘
```

### Key Principles

1. **Game state never references rendering.** No `#include` of renderer headers, no
   `m_renderer->` calls. Game state publishes domain events ("entity spawned", "loot
   received") and the bridge translates them to renderer commands.

2. **Renderer owns movement + collision.** Player position is client-authoritative.
   The renderer handles input, computes desired movement, performs multi-layer collision
   (HCMap ground, BSP geometry, objects, doors, entities), resolves the final position,
   and posts `PlayerPositionChanged` back through the bridge. The game thread accepts
   the position, sends `SendPositionUpdate()` to the server, and checks zone lines.
   Server only sends player position at zone-in; during gameplay, position flows
   renderer → game thread → server.

3. **Bridge is the synchronization boundary.** Both sides communicate through
   thread-safe queues. No shared mutable state. No locks held across the boundary.

4. **Multiple renderers supported.** Each renderer gets its own bridge instance.
   A console renderer's bridge would print chat to stdout and accept keyboard commands.
   A 3D renderer's bridge drives IrrlichtRenderer. Zero renderers (headless) is valid.

5. **Three-thread model.** The system has three threads during gameplay:
   - **Main thread (render)** — owns GL context during gameplay, runs Irrlicht frame loop
   - **Game thread (new)** — runs EverQuest tick: network, game state, event publishing, intent consumption
   - **Loading thread (existing)** — temporarily owns GL context during zone load, runs `loadZoneSequential()`

   The loading thread is managed by the renderer, not the game thread. The game thread
   posts `PlayerZoneChanged` through the bridge. The renderer spawns its loading thread,
   transfers GL context, loads assets, joins. The game thread is unaware of this.

### Collision Architecture (Unchanged)

The renderer retains all collision detection layers:

| Layer | Data Source | Availability |
|-------|-----------|--------------|
| Ground/terrain | HCMap | Game thread has `m_zone_map`; renderer gets a copy |
| Zone BSP geometry | S3D/WldLoader | Renderer only |
| Objects (crates, barrels) | S3D objects.wld | Renderer only |
| Doors/placeables | Server packet + S3D mesh | Both (game has positions, renderer has meshes) |
| Other entities | Bounding volumes | Both (game has positions, renderer has volumes) |

The renderer resolves collision against all layers before reporting the final position.
The game thread never needs BSP/S3D/object mesh data for collision purposes.

### Position Flow

```
Renderer:  W key held
  → compute desired position (direction + speed + deltaTime)
  → check collision (HCMap, BSP, objects, doors, entities)
  → resolve final position (wall slide, step-up, blocked)
  → update camera
  → post PlayerPositionChanged { x, y, z, heading } to bridge

Game thread:
  → accept position from bridge
  → update m_x, m_y, m_z, m_heading
  → SendPositionUpdate() to server
  → check zone lines (distance-based, no mesh collision needed)
  → update water state
```

At zone-in, the server sends the initial player position. The game thread posts
`InitialPlayerPosition` through the bridge, and the renderer uses it to place the
camera. After that, position only flows renderer → game thread.

---

## Process

Each unit follows: **Plan -> Implement -> Review -> Commit**

1. Write plan with numbered steps and acceptance criteria to unit file
2. Implement, re-reading the plan file during work
3. Review: re-read plan, check each step, note deviations in review section
4. Commit only after review passes
5. Update progress table below

---

## Phase 1 — Event Infrastructure (D01–D03)

Build the event types, queues, and bridge interface without changing any existing code
paths. Everything compiles and tests pass. No behavioral changes.

| Unit | Description | Status | Commit |
|------|-------------|--------|--------|
| D01 | Extend existing EventBus with missing event types + consolidate door state | pending | |
| D02 | Define renderer intent types (reconcile with ActionDispatcher) | pending | |
| D03 | Create GameStateBridge interface and IrrlichtBridge skeleton | pending | |

### D01: Extend Existing EventBus with Missing Event Types

`eqt::state::EventBus` already exists in `include/client/state/event_bus.h` with ~30
event types and full variant/subscribe/publish infrastructure. D01 becomes an extension
task, not a greenfield implementation.

Add missing event types to `include/client/state/event_bus.h`:

**Entity events (additions):**
- `CorpseDecayStarted { spawnId }`
- `EntityAppearanceChanged { spawnId, raceId, gender, appearance }`
- `EntityLightChanged { spawnId, lightLevel }`
- `EntityAnimationEvent { spawnId, animCode, loop, playThrough }`
- `EntityPoseStateChanged { spawnId, poseState }`
- `EntityDeathAnimation { spawnId }`
- `CombatAnimation { sourceId, targetId, damageType, damageAmount, damagePercent }`

**Combat/spell events (additions):**
- `SpellCastStarted { casterId, spellId, castTime, targetId }`
- `SpellCastComplete { casterId, spellId, result }`
- `DamageEvent { sourceId, targetId, amount, type, spellName }`
- `BuffUpdated { slot, spellId, ticksLeft, casterName }`
- `BuffRemoved { slot }`
- `VisionChanged { visionType }` (ultra/infra/normal)

**Currency/inventory events (additions):**
- `CurrencyChanged { platinum, gold, silver, copper }`
- `BankCurrencyChanged { platinum, gold, silver, copper }`

**Loot events (additions):**
- `LootWindowOpened { corpseId, corpseName }`
- `LootWindowClosed { corpseId }`
- `LootItemAdded { corpseId, slot, item }`
- `LootItemRemoved { corpseId, slot }`

**Trade events (additions):**
- `TradeStarted { partnerId, partnerName, isNpc }`
- `TradeItemUpdated { who, slot, item }`
- `TradeAcceptStateChanged { ownAccepted, partnerAccepted }`
- `TradeCancelled {}`
- `TradeCompleted {}`

**Vendor events (additions):**
- `VendorWindowOpened { vendorId, vendorName, sellRate }`
- `VendorItemAdded { vendorSlot, item }`
- `VendorWindowClosed {}`

**Bank events (additions):**
- `BankWindowOpened { platinum, gold, silver, copper }`
- `BankWindowClosed {}`

**Trainer events (additions):**
- `TrainerWindowOpened { npcId, npcName, skills }`
- `TrainerWindowClosed {}`

**Skill events (additions):**
- `SkillValueChanged { skillId, value }`
- `SkillsRefreshed { skills }`

**World/environment events (additions):**
- `WeatherChanged { type, intensity }`
- `SwimmingStateChanged { isSwimming, swimSpeed, isLevitating }`

**Zone lifecycle events (additions):**
- `NavmeshChanged { navmesh }` (pointer, for collision)
- `CollisionMapChanged { map }` (pointer, for collision)
- `ZoneLineBoundingBoxes { boxes }`

**UI/misc events (additions):**
- `ZoneLineDebug { enabled, targetZoneId, debugText }`
- `ExpProgressChanged { progress }`
- `CharacterInfoChanged { name, level, className, deity }`
- `WorldObjectSpawned { dropId, x, y, z, heading, modelName, objectType }`
- `PetWindowOpened {}`
- `PetWindowClosed {}`
- `NoteWindowOpened { text, type }` (book/note reading)

**Door state consolidation:**
- Remove `eqt::state::Door` / `eqt::state::DoorState` (state layer duplicate)
- Keep `EQT::DoorState` / `EQT::DoorStateManager` (S06, canonical game state)
- Keep `EQT::Graphics::DoorVisual` (renderer-only)
- Update `eqt::state::GameState` to use `EQT::DoorStateManager` instead of its own `DoorState`

Acceptance criteria:
- All event structs are default-constructible and movable
- Existing EventBus variant updated to hold all new event types
- No renderer includes
- Door state is single canonical `EQT::DoorState` throughout game-state layer
- Compiles and existing tests pass unchanged

### D02: Define Renderer Intent Types

Create `include/client/events/renderer_intents.h` with the intent structs that
renderers post back to the game thread.

Reconcile with existing `ActionDispatcher` (`include/client/action/action_dispatcher.h`).
The `ActionDispatcher` uses direct method calls — keep it as the renderer-internal
action system. Add a `RendererIntent` variant for the cross-thread bridge queue. The
`InputActionBridge` stays inside the renderer; it converts input → actions → intents
that cross the bridge.

Intent types:

**Movement:**
- `PlayerPositionChanged { x, y, z, heading, animation, isMoving }`

**Targeting:**
- `TargetIntent { spawnId }` (0 = clear target)

**Combat:**
- `ToggleAutoAttackIntent {}`
- `AttackIntent { targetId }`

**Chat:**
- `ChatSubmitIntent { channel, text }`

**Interaction:**
- `DoorInteractIntent { doorId }`
- `LootCorpseIntent { spawnId }`
- `LootItemIntent { corpseId, slot }`
- `LootAllIntent { corpseId }`
- `DestroyAllLootIntent { corpseId }`
- `CloseLootIntent { corpseId }`
- `WorldObjectInteractIntent { dropId }`
- `ZoningEnabledIntent { enabled }`
- `ReadItemIntent { bookText, type }`

**Vendor:**
- `VendorToggleIntent {}` (replaces vendorToggleCallback_ — NPC class/distance validation stays on game thread)
- `VendorBuyIntent { npcId, slot, quantity }`
- `VendorSellIntent { npcId, slot, quantity }`
- `CloseVendorIntent { npcId }`

**Bank:**
- `BankerInteractIntent { npcId }` (replaces bankerInteractCallback_ — validation stays on game thread)
- `BankCurrencyMoveIntent { coinType, amount, fromBank }`
- `BankCurrencyConvertIntent { fromCoinType, amount }`
- `CloseBankIntent {}`

**Trade:**
- `TradeRequestIntent { targetId }`
- `TradeAcceptIntent {}`
- `TradeCancelIntent {}`

**Trainer:**
- `TrainerToggleIntent {}` (replaces trainerToggleCallback_ — validation stays on game thread)

**Spells:**
- `CastSpellIntent { gemSlot }`
- `MemorizeSpellIntent { gemSlot, spellId }`
- `ForgetSpellIntent { gemSlot }`
- `ScribeSpellIntent { spellId, bookSlot, sourceSlot }`
- `SpellbookStateIntent { isOpen }` (sends appearance animation)
- `InterruptSpellIntent {}`

**Buffs:**
- `BuffCancelIntent { slot }`

**Skills:**
- `SkillActivateIntent { skillId }`

**Pet:**
- `PetCommandIntent { command }` (attack, back, follow, guard, etc.)

**Group:**
- `GroupInviteIntent { targetName }`
- `DisbandIntent {}`
- `DeclineInviteIntent {}`

**General:**
- `RequestCampIntent {}`
- `RequestQuitIntent {}`
- `SlashCommandIntent { fullCommand }` (game-affecting commands cross the bridge)
- `RequestMemoryReport {}`
- `RequestSceneDump {}`
- `HotbarChangedIntent {}`

Note: Renderer-internal slash commands (`/sort`, `/portal`, `/plight`, `/olight`,
`/zlight`, `/fire`, `/sky`, `/frametiming`, `/fog`, `/clip`, `/stencil`, `/detail`,
`/togglegrass`, etc.) do NOT cross the bridge — they stay in the renderer.

Use `std::variant<...>` as `RendererIntent` to hold any intent type.

Acceptance criteria:
- Header compiles with no renderer includes and no EverQuest includes
- All structs are default-constructible and movable
- `RendererIntent` variant can hold any intent type
- Unit test validates variant construction and visitation

### D03: Create GameStateBridge Interface and IrrlichtBridge Skeleton

Create `include/client/bridge/game_state_bridge.h` — abstract interface that
any renderer adapter implements.

```cpp
class GameStateBridge {
public:
    virtual ~GameStateBridge() = default;

    // Game thread calls: push events for the renderer to consume
    void pushEvent(GameEvent event);

    // Renderer thread calls: push intents for the game thread to consume
    void pushIntent(RendererIntent intent);

    // Game thread calls: drain intents from the renderer
    std::vector<RendererIntent> drainIntents();

    // Renderer thread calls: drain events from the game thread
    std::vector<GameEvent> drainEvents();

    // Renderer-specific: translate a GameEvent into renderer commands
    virtual void applyEvent(const GameEvent& event) = 0;

    // Game-thread-specific: translate a RendererIntent into game state mutations
    // (handled by EverQuest, not the bridge — bridge just queues)
};
```

The bridge wraps the existing `EventBus` for cross-thread use. `EventBus::publish()`
currently calls listeners synchronously on the same thread. The bridge adds:
- A thread-safe event queue (game thread → renderer)
- A thread-safe intent queue (renderer → game thread)
- `drainEvents()` / `drainIntents()` with swap-vector pattern

Create `include/client/bridge/irrlicht_bridge.h` and
`src/client/bridge/irrlicht_bridge.cpp` — skeleton `IrrlichtBridge : GameStateBridge`.
`applyEvent()` is a switch/visit on the variant with stub implementations (LOG_DEBUG
each event type). No actual renderer calls yet.

Acceptance criteria:
- `GameStateBridge` has no renderer or EverQuest includes
- `IrrlichtBridge` includes renderer headers but not eq.h
- Thread-safe queue passes unit test (two threads, push/drain, no data races)
- Compiles and links with existing build (CMakeLists.txt updated)
- No existing code modified except CMakeLists.txt

---

## Phase 2 — Dual-Path Event Publishing (D04–D08)

Add event publishing alongside existing direct renderer calls. Both paths run.
The bridge receives events but doesn't act on them yet. This phase is safe to do
incrementally — each unit adds publishing to one category of packet handlers.

Call site breakdown from the 351 `m_renderer->` sites in eq.cpp:

| Category | Sites |
|----------|-------|
| Entity (spawn, move, appearance, animation, light, death) | ~40 |
| Player (position, spawn ID, stats, exp) | ~20 |
| Chat + system messages | ~5 |
| Combat (damage, animations, target HP) | ~15 |
| Inventory + currency | ~15 |
| Loot window | ~12 |
| Vendor window | ~30 |
| Bank window | ~10 |
| Trade window | ~15 |
| Trainer window | ~10 |
| Door | ~3 |
| Spell/buff/vision | ~10 |
| Group/pet | ~10 |
| World/environment | ~5 |
| Zone lifecycle | ~20 |
| Debug/toggle (slash commands) | ~40 |
| Callback wiring (InitGraphics) | ~25 |
| Direct pointer coupling | ~5 |
| WindowManager UI queries | ~50 |

| Unit | Description | Status | Commit |
|------|-------------|--------|--------|
| D04 | Publish entity events alongside existing calls | pending | |
| D05 | Publish chat, combat, and player stat events | pending | |
| D06a | Publish inventory + currency events | pending | |
| D06b | Publish loot + vendor events | pending | |
| D06c | Publish bank + trade events | pending | |
| D07 | Publish door, group, pet, spell, skill events | pending | |
| D08 | Publish world/environment + zone lifecycle events | pending | |

Each unit follows the same pattern:
1. Identify all packet handlers and game logic that currently call `m_renderer->...`
   for the event category
2. After each existing direct call, add `m_bridge->pushEvent(...)` with the equivalent
   event struct
3. Add a validation mode: log a warning if the bridge event and the direct call
   would produce different state (sanity check during development)

**D06a: Inventory + currency events** (~15 sites) — `InventoryItemUpdated`,
`CurrencyChanged`

**D06b: Loot + vendor events** (~42 sites) — `LootWindowOpened/Closed`,
`LootItemAdded/Removed`, `VendorWindowOpened/Closed`, `VendorItemAdded`,
`refreshVendorSellableItems`

**D06c: Bank + trade events** (~25 sites) — `BankWindowOpened/Closed`,
`BankCurrencyChanged`, `TradeStarted/Completed/Cancelled`, `TradeItemUpdated`,
`TradeAcceptStateChanged`

Acceptance criteria per unit:
- All existing direct renderer calls remain (no behavioral change)
- Bridge receives events for the category
- Game thread can be run with no bridge attached (null check or no-op bridge)
- No new warnings or errors in build
- Existing tests pass unchanged

---

## Phase 3 — Bridge Consumes Events (D09–D13)

Wire up `IrrlichtBridge::applyEvent()` to actually drive the renderer. At this point,
both the direct calls AND the bridge calls drive the renderer (redundant but verifiable).

| Unit | Description | Status | Commit |
|------|-------------|--------|--------|
| D09 | IrrlichtBridge handles entity events | done | `f97ff45` |
| D10 | IrrlichtBridge handles chat, combat, player stat events | done | `69589ce` |
| D11a | IrrlichtBridge handles inventory + currency events | done | `69589ce` |
| D11b | IrrlichtBridge handles loot + vendor events | done | `69589ce` |
| D11c | IrrlichtBridge handles bank + trade events | done | `69589ce` |
| D12 | IrrlichtBridge handles door, group, pet, spell, skill events | done | |
| D13 | IrrlichtBridge handles world/environment + zone lifecycle events | done | |

Each unit follows the same pattern:
1. Implement `applyEvent()` cases for the category — translate event struct into
   the existing `m_renderer->...` calls that the bridge now owns
2. Verify that the bridge-driven calls produce identical results to the direct calls
3. Add a flag (`bridgeDriven_`) that, when enabled, skips the direct call and relies
   solely on the bridge. Default: off (both paths run).

**D11a: Inventory + currency** — bridge calls renderer inventory/currency update methods

**D11b: Loot + vendor** — bridge opens/closes/updates loot and vendor windows

**D11c: Bank + trade** — bridge opens/closes/updates bank and trade windows

Acceptance criteria per unit:
- With `bridgeDriven_=false`: both paths run, no visible change
- With `bridgeDriven_=true`: only bridge path runs, behavior identical
- Can toggle at runtime for A/B validation

---

## Phase 4 — Intent Handling (D14–D16)

Wire up the game thread to consume intents from the renderer and execute them.
This replaces the current callback lambdas that capture `this`.

| Unit | Description | Status | Commit |
|------|-------------|--------|--------|
| D14 | Movement intent: renderer posts PlayerPositionChanged, game thread consumes | pending | |
| D15 | Interaction intents: target, combat, door, loot, chat, vendor, banker, trainer, world object, zoning, read item | pending | |
| D16 | UI intents: spell, buff, skill, loot actions, vendor buy/sell, bank, trade, pet, group, camp/quit, hotbar | pending | |

### D14: Movement Intent (Detailed)

This is the most important intent. Current flow:
- Renderer calls `movementCallback_` → `OnGraphicsMovement()` → directly mutates
  `m_x`, `m_y`, `m_z`, `m_heading` → calls `SendPositionUpdate()`.

New flow:
- Renderer posts `PlayerPositionChanged { x, y, z, heading, animation, isMoving }`
  to bridge intent queue (replaces `movementCallback_` invocation)
- Game thread drains intents each tick, applies position, sends network update
- `OnGraphicsMovement()` body is reused but called from intent processing, not
  from a renderer callback

At zone-in, the game thread posts `InitialPlayerPosition` through the bridge
event queue. The renderer uses it to place the camera and set initial collision state.

Also add via events (replaces pointer passing):
- `CollisionMapChanged` and `NavmeshChanged` events replace `setCollisionMap()` /
  `setNavmesh()` pointer passing
- `ZoneLineBoundingBoxes` event replaces `setZoneLineBoundingBoxes()`
- The renderer stores its own copy/reference to collision data received via events

### D15: Interaction Intents (Detailed)

Replace all game-logic-heavy callbacks. The validation/game logic stays on the game
thread; only the user's intent crosses the bridge.

- `TargetIntent` — replaces `targetCallback_`. The 75 lines of trade-initiation and
  target-info-filling logic moves to the game thread's intent handler.
- `DoorInteractIntent` — replaces `doorInteractCallback_`
- `LootCorpseIntent` — replaces `lootCorpseCallback_`
- `VendorToggleIntent` — replaces `vendorToggleCallback_`. The 40 lines of NPC
  class/distance validation stays on the game thread.
- `BankerInteractIntent { npcId }` — replaces `bankerInteractCallback_`. Validation
  logic stays on game thread.
- `TrainerToggleIntent` — replaces `trainerToggleCallback_`. Validation stays on
  game thread.
- `WorldObjectInteractIntent { dropId }` — replaces `worldObjectInteractCallback_`
- `ZoningEnabledIntent { enabled }` — replaces `zoningEnabledCallback_`
- `ChatSubmitIntent { text }` — replaces `chatSubmitCallback_`
- `ReadItemIntent { bookText, type }` — replaces `readItemCallback_`

### D16: UI Intents (Detailed)

Replace all UI-driven callbacks with intents.

**Spell intents:**
- `CastSpellIntent { gemSlot }` — replaces both `spellGemCastCallback_` and the
  WindowManager gem cast callback
- `ForgetSpellIntent { gemSlot }` — replaces gem forget callback
- `ScribeSpellIntent { spellId, bookSlot, sourceSlot }` — replaces scribe callback
- `SpellbookStateIntent { isOpen }` — replaces spellbook state callback (sends
  appearance animation)

**Buff/skill intents:**
- `BuffCancelIntent { slot }` — replaces buff cancel callback
- `SkillActivateIntent { skillId }` — replaces skill activate callback

**Loot action intents:**
- `LootItemIntent { corpseId, slot }` — replaces loot item callback
- `LootAllIntent { corpseId }` — replaces loot all callback
- `DestroyAllLootIntent { corpseId }` — replaces destroy all callback
- `CloseLootIntent { corpseId }` — replaces close loot callback

**Vendor intents:**
- `VendorBuyIntent { npcId, slot, quantity }` — replaces vendor buy callback
- `VendorSellIntent { npcId, slot, quantity }` — replaces vendor sell callback
- `CloseVendorIntent { npcId }` — replaces vendor close callback

**Bank intents:**
- `CloseBankIntent {}` — replaces bank close callback
- `BankCurrencyMoveIntent { coinType, amount, fromBank }` — replaces bank currency
  move callback (the 80 lines of coin math stays on game thread)
- `BankCurrencyConvertIntent { fromCoinType, amount }` — replaces bank currency
  convert callback (the 60 lines of conversion logic stays on game thread)

**Trade intents:**
- `TradeRequestIntent { targetId }` — replaces trade request
- `TradeAcceptIntent {}` / `TradeCancelIntent {}` — replaces trade window callbacks

**Pet/group intents:**
- `PetCommandIntent { command }` — replaces pet button callbacks
- `GroupInviteIntent { targetName }` / `DisbandIntent {}` — replaces group callbacks

**General intents:**
- `RequestCampIntent {}` / `RequestQuitIntent {}` — replaces quit callback
- `HotbarChangedIntent {}` — replaces hotbar changed callback

Acceptance criteria per unit:
- Callback still works (dual path) with intent as secondary
- Intent-only path produces identical behavior when callbacks are removed
- Network packets sent are identical in both paths

---

## Phase 5 — Remove Direct Coupling (D17–D20)

Remove the direct `m_renderer->...` calls from packet handlers, remove the callback
lambdas, and make the bridge the sole communication path. After this phase, EverQuest
no longer includes any renderer headers.

| Unit | Description | Status | Commit |
|------|-------------|--------|--------|
| D17 | Remove direct renderer calls from entity packet handlers | pending | |
| D18a | Remove direct renderer calls from inventory/loot/vendor packet handlers | pending | |
| D18b | Remove direct renderer calls from bank/trade/trainer packet handlers | pending | |
| D19 | Remove direct renderer calls from remaining handlers (chat, combat, spell, skill, pet, group, door, world, zone lifecycle, swimming, weather, time) | pending | |
| D20a | Remove callback lambdas from InitGraphics() | pending | |
| D20b | Remove raw pointer coupling from InitGraphics() | pending | |
| D20c | Remove m_renderer from EverQuest | pending | |
| D20d | Remove debug/toggle slash commands from EverQuest | pending | |

### D18a: Remove inventory/loot/vendor direct calls

Remove the ~57 `m_renderer->getWindowManager()` calls in inventory, loot, and vendor
packet handlers. All communication now goes through bridge events only.

### D18b: Remove bank/trade/trainer direct calls

Remove the ~35 `m_renderer->getWindowManager()` calls in bank, trade, and trainer
handlers. All communication now goes through bridge events only.

### D20a: Remove callback lambdas from InitGraphics()

Remove all 15 `set*Callback()` calls:
- `movementCallback_`, `targetCallback_`, `lootCorpseCallback_`, `zoningEnabledCallback_`
- `vendorToggleCallback_`, `bankerInteractCallback_`, `trainerToggleCallback_`
- `doorInteractCallback_`, `worldObjectInteractCallback_`
- `spellGemCastCallback_`, `chatSubmitCallback_`, `readItemCallback_`
- And others

Remove the 6 `Setup*Callbacks()` calls and their implementations:
- `SetupLootCallbacks`
- `SetupVendorCallbacks`
- `SetupBankCallbacks`
- `SetupTrainerCallbacks`
- `SetupTradeWindowCallbacks`
- `SetupTradeskillCallbacks`

All are replaced by intents from Phase 4.

### D20b: Remove raw pointer coupling from InitGraphics()

Remove direct pointer passing that couples renderer to game state:
- Remove `setInventoryManager(m_inventory_manager.get())` — bridge pushes
  `InventoryItemUpdated` events instead
- Remove `setCollisionMap(m_zone_map.get())` — bridge pushes `CollisionMapChanged`
  event instead
- Remove `initGroupWindow(this)` — bridge pushes `GroupMemberAdded/Removed` events
  instead; window no longer holds `EverQuest*`
- Remove `initSpellGemPanel(m_spell_manager.get())` — bridge pushes `SpellGemUpdated`
  events instead
- Remove `initBuffWindow(m_buff_manager.get())` — bridge pushes `BuffUpdated/Removed`
  events instead
- Remove `initPlayerStatusWindow(this)` — bridge pushes `PlayerStatsChanged` events
- Remove `initSkillsWindow(m_skill_manager.get())` — bridge pushes `SkillValueChanged`
  events
- Remove `setHUDCallback` — renderer formats HUD from its own state copy

### D20c: Remove m_renderer from EverQuest

The final decoupling:
- Remove `m_renderer` member from EverQuest class
- Remove `#include` of all renderer/graphics headers from eq.h/eq.cpp
- Replace `InitGraphics()` with `attachBridge(GameStateBridge*)`
- EverQuest communicates solely through bridge
- `LoadZoneGraphicsOnLoadingThread()` is replaced by a `PlayerZoneChanged` event —
  the renderer (via bridge) receives zone data and manages its own loading thread

### D20d: Remove debug/toggle slash commands from EverQuest

Move renderer-specific commands to the renderer:
- The ~40 slash commands that toggle renderer features (`/sort`, `/portal`, `/plight`,
  `/olight`, `/zlight`, `/fire`, `/sky`, `/frametiming`, `/stencil`, `/fog`, `/clip`,
  `/detail`, `/togglegrass`, etc.) move to the renderer
- Game thread posts `SlashCommandIntent { fullCommand }` through bridge
- Renderer handles renderer-specific commands directly
- Game-affecting commands (`/camp`, `/quit`, `/who`, `/tell`, etc.) stay on game thread
- `/pmem` becomes a bridge round-trip: game thread collects its stats, pushes event,
  renderer collects its stats, merges, displays

Acceptance criteria (all of D20):
- eq.h has zero includes from `client/graphics/`
- eq.cpp has zero references to `IrrlichtRenderer`
- EverQuest can be instantiated with no renderer (headless mode works via null bridge)
- All existing functionality preserved through bridge path

---

## Phase 6 — Thread Separation (D21–D22)

Move EverQuest to its own thread. The main thread becomes the render thread.

Note: D23 (Remove networkTickCallback_ and loading screen coupling) from the original
plan has been **deleted** — this was already completed by Batch L (Loading Thread
Separation, commit `58f0791`).

| Unit | Description | Status | Commit |
|------|-------------|--------|--------|
| D21 | Move EverQuest tick loop to dedicated game thread | pending | |
| D22 | Synchronize bridge queues with proper threading | pending | |

### D21: Game Thread

The main loop in `Application::mainLoop()` currently runs everything sequentially.
New three-thread structure:

```
Game thread (new):
    while running:
        TickNetwork()
        drainIntents() → process each
        UpdateMovement()
        pushEvents() (any state changes this tick)
        sleep/pace to tick rate (~60Hz)

Render thread (main thread — required for GL context):
    while running:
        drainEvents() → applyEvent() each
        processFrame()
        postIntents() (movement, input, interactions)

Loading thread (existing — managed by renderer):
    loadZoneSequential()  (temporarily owns GL context)
```

The game thread runs at a fixed tick rate. The render thread runs at display refresh
rate. They are fully decoupled — a slow render frame doesn't delay network processing,
and a packet burst doesn't cause frame hitches.

The loading thread is managed by the renderer, not the game thread. The game thread
posts `PlayerZoneChanged` through the bridge. The renderer spawns its loading thread,
transfers GL context, loads assets, joins. The game thread is unaware of this.

### D22: Queue Synchronization

The bridge queues (events and intents) use the swap-vector pattern:
- Producer: lock, push_back, unlock
- Consumer: lock, swap with empty vector, unlock, process locally

This gives O(1) lock time regardless of queue depth. No contention between producers
and consumers except during the brief swap.

Add sequence numbers to events/intents for ordering guarantees and debugging.
Add high-water-mark warning if queue depth exceeds threshold (indicates one side
is falling behind).

Acceptance criteria:
- Game state runs on dedicated thread, render on main thread
- Network processing never blocks on rendering
- Rendering never blocks on network processing
- Zone loading works: game thread sends zone data through events, renderer loads
  assets independently via its loading thread
- Clean shutdown: game thread signals stop, renderer finishes current frame, both
  threads join
- No data races (verified under TSan if available)

---

## Phase 7 — Cleanup and Optimization (D24–D26)

| Unit | Description | Status | Commit |
|------|-------------|--------|--------|
| D24 | Optimize high-frequency events (entity position batching) | pending | |
| D25 | Add console bridge implementation | pending | |
| D26 | Update documentation and CLAUDE.md | pending | |

### D24: Event Batching

`EntityMoved` events fire per network packet per entity — potentially 100+ per tick.
Batch consecutive moves for the same entity into a single event with the latest
position. The bridge can coalesce these in `drainEvents()` before dispatching.

Similarly, `PlayerPositionChanged` intents fire every render frame. The game thread
only needs the latest position per tick — coalesce in `drainIntents()`.

### D25: Console Bridge

Prove the multi-renderer architecture by implementing `ConsoleBridge`:
- `EntitySpawned` → print "[SPAWN] Goblin (id=42) at (100, 200, 5)"
- `ChatMessageReceived` → print "[SAY] Player: Hello"
- `PlayerStatsChanged` → print "[HP] 1200/1500  [Mana] 800/1000"
- Reads stdin for slash commands → posts `SlashCommandIntent`

This validates that the game state truly has no renderer dependencies.

### D26: Documentation

- Update CLAUDE.md Architecture section with new threading model
- Update thread inventory table
- Document bridge event/intent types
- Document position flow and collision ownership
- Add sequence diagram for zone-in flow through bridge
- Document the three-thread model (render, game, loading)
- Document the door state consolidation (single `EQT::DoorState`)
- Remove references to `networkTickCallback_` and `advanceBackgroundZoneLoad`

---

## Risk Assessment

| Risk | Mitigation |
|------|-----------|
| Subtle ordering bugs (event A must arrive before event B) | Sequence numbers on events; single-producer-single-consumer per bridge; preserve publish order |
| Performance regression from queue overhead | Swap-vector pattern is O(1) lock time; batch high-frequency events (D24) |
| Incomplete event coverage (missed a direct renderer call) | Phase 2 dual-path approach catches misses — both paths run until verified |
| Loading screen timing changes | Game thread is unaware of loading; renderer manages its own loading state based on asset readiness; loading thread is renderer-managed |
| HUD callback reads private EverQuest state | Replace with `PlayerStatsChanged` event pushed from game thread (D05) |
| Collision ownership unclear for edge cases | Renderer owns all collision; game thread only does distance-based checks (zone lines, aggro range) |
| Regression in headless mode | Null bridge or no bridge attached — game state runs unchanged, just no events consumed |
| InitGraphics() complexity (~500 lines, 15+ callbacks, raw pointer coupling) | Split into 4 sub-units (D20a-D20d) for incremental decoupling |

---

## Dependencies

- No external library additions required
- Uses existing `std::mutex`, `std::thread`, `std::condition_variable`
- Follows `BackgroundWorkQueue` patterns established in Batch A
- Leverages existing `eqt::state::EventBus` (extended in D01)
- Leverages existing `ActionDispatcher` / `InputActionBridge` (reconciled in D02)
- `networkTickCallback_` and `advanceBackgroundZoneLoad` already removed by Batch L
- `EQT::DoorStateManager` (from Batch S, S06) is the canonical door state

## Estimated Scope

- ~45 event struct additions to existing EventBus (D01)
- ~40 intent structs (D02)
- ~1500 lines for bridge interface + IrrlichtBridge (D03, D09-D13)
- ~351 call sites in eq.cpp migrated from direct to event-based (D04-D08, D17-D19)
- ~25+ callbacks replaced with intents (D14-D16, D20a)
- ~8 raw pointer couplings removed (D20b)
- Net code reduction in eq.cpp once direct calls removed (Phase 5)
- 31 total units (original 26, +5 from splits, -1 from D23 deletion)
