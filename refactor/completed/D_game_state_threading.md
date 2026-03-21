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
| D14 | Movement intent: renderer posts PlayerPositionChanged, game thread consumes | done | |
| D15 | Interaction intents: target, combat, door, loot, chat, vendor, banker, trainer, world object, zoning, read item | done | |
| D16 | UI intents: spell, buff, skill, loot actions, vendor buy/sell, bank, trade, pet, group, camp/quit, hotbar | done | |

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
| D17a | Add missing entity event types + fix weapon skill state ownership | done | |
| D17b | Remove direct renderer calls from entity packet handlers | done | |
| D18a | Remove direct renderer calls from inventory/loot/vendor packet handlers | done | |
| D18b | Remove direct renderer calls from bank/trade/trainer packet handlers | done | |
| D19a | Remove direct renderer calls from small handlers (chat, combat, spell, skill, pet, group, door, world, swimming, weather, time) | done | |
| D19b | Remove direct renderer calls from zone lifecycle functions | done | |
| D19c | Remove remaining packet handler and chat routing renderer calls | pending | |
| D20a | Remove callback lambdas + activate intent processing | done | |
| D20b | Remove raw pointer coupling, remaining callbacks, and renderer lifecycle from InitGraphics() | pending | |
| D20c | Refactor UpdateGraphics, zone loading ownership, and game-side renderer queries | pending | |
| D20d | Remove debug/toggle slash commands from EverQuest | pending | |
| D20e | Remove m_renderer from EverQuest | pending | |
| D20f | Decouple RDP server from renderer | pending | |

### D17a: Add missing entity event types + fix weapon skill state ownership

**Prerequisite for D17b.** Several entity-related `m_renderer->` calls have no
corresponding bridge event, so the direct calls cannot be removed until events exist.

**Missing event types to add:**
- `EntityLightChanged` — already exists in event_bus.h but NOT published from
  `ZoneProcessSpawnAppearance` (line 6377). Add `publishEvent()` there.
- `PlayerSpawnIdSet { spawnId }` — new event. `setPlayerSpawnId()` is called at
  4 sites (lines 5851, 6980, 7367, 20139/20251). The bridge needs this to mark
  the player entity for special handling (camera follow, rotation fix).
- `PlayerAppearanceChanged { raceId, gender, appearance }` — new event, distinct
  from `EntityAppearanceChanged`. Called at lines 7375, 12812, 12911, 13005 for
  the inventory paperdoll UI update.
- `CombatSkillAnimation { spawnId, animCode }` — new event for the 6
  `queueSkillAnimation()` calls in `ZoneProcessEmote` (kick, bash, tiger strike,
  flying kick, dragon punch, round kick).
- `ReceivedDamageAnimation { spawnId }` — new event for `queueReceivedDamageAnimation()`
  (line 6512).
- `ExpectedEntityCountChanged { count }` — new event for `setExpectedEntityCount()`
  (lines 7427, 19825).
- `EntityWeaponSkillsChanged { spawnId, primarySkill, secondarySkill }` — new event
  for `setEntityWeaponSkills()` (line 20952).

**Weapon skill state ownership fix:**
`ZoneProcessEmote` (lines 6432-6433) calls `m_renderer->getEntityPrimaryWeaponSkill()`
and `getEntitySecondaryWeaponSkill()` — the game thread queries renderer state. This
violates the bridge model (game thread must not call renderer). Fix: track weapon
skills in game state (they originate from spawn data in the entity struct). The emote
handler reads from game state instead of querying the renderer.

**Also fix:** `GroupInviteReceived` case in irrlicht_bridge.cpp (line 279) uses
`GroupChangedData` instead of a dedicated struct. Add `GroupInviteReceivedData` with
inviter name field.

Acceptance criteria:
- All entity-related `m_renderer->` calls have a corresponding bridge event published
- Weapon skills tracked in entity game state, not queried from renderer
- All new events handled in `IrrlichtBridge::applyEvent()`
- Existing behavior unchanged (dual-path)

### D17b: Remove direct renderer calls from entity packet handlers

Remove all `m_renderer->` calls from entity packet handlers, relying solely on
bridge events. Covers these functions/handlers:

| Function/Handler | Calls to remove | Event coverage |
|---|---|---|
| `OnSpawnAddedGraphics()` | registerEntity, setEntityLight, setEntityPoseState, setEntityAnimation, updatePlayerAppearance | All covered |
| `OnSpawnMovedGraphics()` | setPlayerPosition, updateEntity | PlayerMoved, EntityMoved |
| `OnSpawnRemovedGraphics()` | clearCurrentTarget, startCorpseDecay, removeEntity | TargetChanged, CorpseDecayStarted, EntityDespawned |
| `ZoneProcessSpawnAppearance` | setEntityPoseState, setEntityAnimation, playEntityDeathAnimation, setEntityLight | All covered (light added in D17a) |
| `ZoneProcessEmote` | queueSkillAnimation (×6), queueReceivedDamageAnimation, setEntityAnimation, getEntity*WeaponSkill (×2) | Covered by D17a new events + state fix |
| `ZoneProcessWearChange` | updateEntityAppearance, updatePlayerAppearance | EntityAppearanceChanged, PlayerAppearanceChanged (D17a) |
| `ZoneProcessMobHealth` | updateCurrentTargetHP | EntityStatsChanged |
| `ZoneProcessClientUpdate` | createEntity, setPlayerSpawnId, setEntityLight, updatePlayerAppearance, setExpectedEntityCount | EntitySpawned + D17a new events |
| `ZoneProcessZoneSpawns` | setPlayerSpawnId, setPlayerPosition | PlayerSpawnIdSet (D17a), PlayerMoved |
| `ZoneProcessDeleteSpawn` | closeLootWindow (via getWindowManager) | Assign to D18a (loot UI) |
| Combat handlers | playEntityDeathAnimation (13769), setEntityAnimation (18653) | EntityDeathAnimation, EntityAnimationEvent |
| `setEntityWeaponSkills` (20952) | setEntityWeaponSkills | EntityWeaponSkillsChanged (D17a) |

After this unit, the `OnSpawnAddedGraphics()`, `OnSpawnMovedGraphics()`, and
`OnSpawnRemovedGraphics()` helper functions can be reduced to event-publish-only
(or inlined at call sites).

Acceptance criteria:
- Zero `m_renderer->` calls remain in entity packet handlers
- All entity rendering driven solely through bridge events
- Entity spawn, move, appearance, animation, death all work correctly
- `#ifdef EQT_HAS_GRAPHICS` blocks in entity handlers contain only event publishing

### D18a: Remove inventory/loot/vendor direct calls

Remove the ~57 `m_renderer->getWindowManager()` calls in inventory, loot, and vendor
packet handlers. All communication now goes through bridge events only.

Also includes `ZoneProcessDeleteSpawn` line 7510 (`closeLootWindow` on corpse despawn)
which is entity-triggered but loot-UI-specific.

### D18b: Remove bank/trade/trainer direct calls

Remove the ~35 `m_renderer->getWindowManager()` calls in bank, trade, and trainer
handlers. All communication now goes through bridge events only.

### D19a: Remove direct renderer calls from small handlers

Remove `m_renderer->` calls from non-entity, non-UI-window packet handlers:

| Category | Calls | Event coverage |
|---|---|---|
| Chat | ~5 (showNoteWindow, getChatWindow) | Events exist |
| Combat | 2 (playEntityDeathAnimation, setEntityAnimation for damage) | Events exist |
| Door | 1 (createDoor at 6030) | DoorSpawned event exists |
| World object | 1 (addWorldObject at 6728) | WorldObjectSpawned event exists |
| Weather | 1 (setWeather at 6776) | WeatherChanged event exists |
| Pet | ~4 (openPetWindow, closePetWindow) | Events exist |
| Group | ~4 (invite windows) | Events exist |
| Swimming | setSwimmingState calls | SwimmingStateChanged event exists |
| Spell/skill | ~6 (getParticleManager, getWeatherEffects) | Partial — verify |
| Player light | togglePlayerLight (9890) | Belongs in D20d (slash command) |

Acceptance criteria:
- Zero `m_renderer->` calls in chat, combat, door, world, weather, pet, group,
  swimming, spell/skill packet handlers
- All rendering driven through bridge events

### D19b: Remove direct renderer calls from zone lifecycle functions

This is the largest and most complex removal. Covers the zone loading pipeline:

**Functions in scope:**

| Function | Lines | m_renderer calls | Notes |
|---|---|---|---|
| `LoadZoneGraphicsOnLoadingThread()` | 20071-20178 | ~20 (registerEntity bulk, setupInstantScene, loadZoneSequential, setCollisionMap, storeZoneEnvironment, registerDoor, setPlayerPosition, setCameraMode, etc.) | Runs on loading thread |
| `LoadZoneGraphics()` | 20180-20295 | ~15 (same pattern as above, for non-loading-thread path) | Runs on main thread |
| `StartLoadingThread()` | 20018-20053 | 4 (getDevice, getDriver, getGUIEnvironment, setLoading) | GL context transfer |
| `JoinLoadingThread()` | 20055-20069 | 1 (setLoading) | GL context restore |
| `OnGraphicsComplete()` | 1192-1210 | 2 (setZoneReady, hideLoadingScreen) | Loading completion |
| Zone cleanup | 14261, 14599 | setCollisionMap(nullptr) | Zone end/reconnect |

**New events needed for zone data transfer:**
- `ZoneSetupData { zoneName, playerX/Y/Z, entities[], doors[], collisionMap, navmesh, zoneLineBBoxes, environment }` — a single comprehensive event carrying all zone-in data so the renderer can manage its own loading pipeline
- Or decompose into multiple events with ordering guarantees

**Key architectural change:** After D19b, the renderer owns `LoadZoneGraphicsOnLoadingThread()`
and `LoadZoneGraphics()`. The game thread publishes zone data through events; the
renderer decides how to load (loading thread vs instant). `StartLoadingThread()` and
`JoinLoadingThread()` move to the renderer.

Acceptance criteria:
- Zero `m_renderer->` calls in zone lifecycle code
- Zone loading works end-to-end through bridge events
- Loading screen, progress updates, and zone-ready notification all function
- Both loading-thread and instant-load paths work

### D19c: Remove remaining packet handler and chat routing renderer calls

Remove all remaining `m_renderer->` calls from packet handlers and chat routing
functions that were not covered by D17-D19b. These are dual-path calls where
bridge events already carry the same data — the `m_renderer` blocks are redundant.

**Chat routing (7 call sites) — remove `#ifdef EQT_HAS_GRAPHICS` / `m_renderer` blocks:**
- `AddChatMessage()` (lines 7260-7279) — routes channel chat to chatWindow via renderer
- `AddChatSystemMessage()` (lines 7362-7370) — routes system messages via renderer
- `AddChatCombatMessage()` (lines 7388-7405) — routes combat messages via renderer
- Spell fade handler (lines 12782-12792) — spell wear-off via renderer chatWindow
- Formatted message handler (lines 12899-12918) — NPC dialogue via renderer chatWindow
- Formatted message handler (lines 12997-12998) — second path via renderer chatWindow
- `ZoneProcessConsider()` (line 17585) — consider results with colored text

All 7 already publish bridge events (ChatMessage, SystemMessage, etc.) — the renderer
blocks are dead dual-path code left from the incremental migration.

**Packet handler calls (6 call sites):**
- `ZoneProcessItemPlayerPacketContainer()` (lines 2214-2217) — item display in vendor/loot
  window via renderer. Need new bridge events: `VendorItemReceived`, `LootItemReceived`
- `ZoneProcessItemPacketContainer()` (line 2665) — spell scribe → spellBookWindow refresh.
  Need new bridge event: `SpellScribeCompleted`
- `ZoneProcessClickObjectAction()` (line 14474) — tradeskill container open via renderer.
  Need new bridge event: `TradeskillContainerOpened`
- `ZoneProcessClickObjectAction()` (line 14486) — tradeskill container close via renderer.
  Need new bridge event: `TradeskillContainerClosed`
- `ZoneProcessTradeSkillCombine()` (lines 14507-14508) — **dead code** (empty body). Remove.
- `/skills` slash command (line 9762-9763) — `toggleSkillsWindow()` via renderer.
  Need new bridge event: `ToggleSkillsWindow`

**Group window calls (4 call sites) — redundant, bridge events already exist:**
- `ZoneProcessGroupInvite()` (lines 14954-14959) — `showPendingInvite()` + `openGroupWindow()`
- `ZoneProcessGroupUpdate()` (lines 15008-15012) — `hidePendingInvite()`
- `ZoneProcessGroupDisband()` (lines 15201-15205) — `hidePendingInvite()`
- `ZoneProcessGroupCancelInvite()` (lines 15223-15227) — `hidePendingInvite()`

**Pet window calls (2 call sites) — redundant, bridge events already exist:**
- `OnPetCreated()` (lines 19257-19258) — `openPetWindow()` via renderer
- `OnPetButtonStateChanged()` (lines 19292-19299) — **dead code** (body does nothing). Remove.

Acceptance criteria:
- Zero `m_renderer->` calls in chat routing functions
- Zero `m_renderer->` calls in item, tradeskill, group, pet packet handlers
- Dead code removed (tradeskill combine ack, OnPetButtonStateChanged)
- All rendering driven solely through bridge events
- `#ifdef EQT_HAS_GRAPHICS` blocks removed from these functions

### D20a: Remove callback lambdas + activate intent processing

Remove direct renderer callback lambdas from InitGraphics() and activate the intent
handlers in `ProcessBridgeIntents()` that were logged as stubs in D15/D16. After
this unit, intents are the sole path for renderer→game communication.

**What D20a actually completed:**

*Direct renderer callbacks removed (11):*
- `setMovementCallback` — replaced by `PlayerPositionChanged` intent (D14)
- `setTargetCallback` — replaced by `TargetIntent` (D15)
- `setLootCorpseCallback` — replaced by `LootCorpseIntent` (D15)
- `setZoningEnabledCallback` — replaced by `ZoningEnabledIntent` (D15)
- `setVendorToggleCallback` — replaced by `VendorToggleIntent` (D15)
- `setBankerInteractCallback` — replaced by `BankerInteractIntent` (D15)
- `setTrainerToggleCallback` — replaced by `TrainerToggleIntent` (D15)
- `setDoorInteractCallback` — replaced by `DoorInteractIntent` (D15)
- `setWorldObjectInteractCallback` — replaced by `WorldObjectInteractIntent` (D15)
- `setSpellGemCastCallback` — replaced by `CastSpellIntent` (D16)
- `setReadItemCallback` — replaced by `ReadItemIntent` (D15)

*WindowManager callbacks removed (10):*
- `setGemCastCallback`, `setGemForgetCallback`, `setSpellbookStateCallback`,
  `setScribeSpellRequestCallback` — spell intents (D16)
- `setBuffCancelCallback` — `BuffCancelIntent` (D16)
- `setSkillActivateCallback` — `SkillActivateIntent` (D16)
- `setGroupInviteCallback`, `setGroupDisbandCallback`,
  `setGroupDeclineCallback` — group intents (D16)
- `setPetCommandCallback` — `PetCommandIntent` (D16)
- `setChatSubmitCallback` — `ChatSubmitIntent` (D15)

*Setup*Callbacks() emptied (4 functions):*
- `SetupLootCallbacks` — body emptied (intents handle actions)
- `SetupVendorCallbacks` — body emptied
- `SetupBankCallbacks` — body emptied
- `SetupTradeWindowCallbacks` — callbacks removed, `initTradeWindow` config retained

*Intent activation:* All 30+ intent handlers in `ProcessBridgeIntents()` activated
with real game logic. Only 3 remain as stubs: `HotbarChangedIntent` (UI-only),
`RequestMemoryReport` (D20d), `RequestSceneDump` (D20d).

**Deferred to D20b (still present in InitGraphics):**
- `setHUDCallback` — complex HUD string builder from game state
- `setHotbarActivateCallback` — complex: spell cast, item use, emote, skill
- `setGroupAcceptCallback` — no intent yet
- `setHotbarCreateCallback` — hotbar creation handler
- `setLinkClickCallback` — chat link click handler
- `setEntityNameProvider` — lambda captures `m_entities`
- `setCommandRegistry(m_command_registry.get())` — raw pointer
- `setBuffFadeCallback` — vision buff handler
- `setOnSkillActivated`, `setOnSkillUpdate` — skill feedback callbacks
- `SetupTradeskillCallbacks()` — 2 callbacks still fully implemented
- `SetupTrainerCallbacks()` — 2 callbacks still fully implemented
- `SetupInventoryCallbacks()` — 3 callbacks still fully implemented
- `SetupTradeManagerCallbacks()` — 8 callbacks still fully implemented

Acceptance criteria:
- Direct renderer callbacks removed (11 of 12 — `setHUDCallback` deferred to D20b)
- WindowManager callbacks removed (10 of 14+ — 4 deferred to D20b)
- Setup*Callbacks: 4 emptied, 4 deferred to D20b
- All 30+ activated intent types in ProcessBridgeIntents() have real processing
- All existing functionality preserved through intent-only path

### D20b: Remove raw pointer coupling, remaining callbacks, and renderer lifecycle from InitGraphics()

Split into 5 sub-units (D20b1–D20b5) due to scope.

#### D20b1: Remaining Setup*Callbacks → intents

Convert all remaining renderer→game callback lambdas in Setup*Callbacks functions
to intent types. Add intent structs, push from renderer WindowManager, handle in
ProcessBridgeIntents(), then empty the callback bodies.

**SetupTradeskillCallbacks (2 callbacks):**
- `setOnTradeskillCombine` → `TradeskillCombineIntent` (new intent). Lambda calls
  `SendTradeSkillCombine()` with slot determined by querying tradeskill window state
  (world container vs inventory). Intent carries `isWorldContainer` + `containerSlot`.
- `setOnTradeskillClose` → `TradeskillCloseIntent` (new intent). Lambda calls
  `SendCloseContainer()` for world containers. Intent carries `dropId`.

**SetupTrainerCallbacks (2 callbacks):**
- `setSkillTrainCallback` → `TrainSkillIntent` (new intent, carries `skillId`)
- `setTrainerCloseCallback` → `CloseTrainerIntent` (new intent)

**SetupInventoryCallbacks (3 callbacks):**
- `setMoveItemCallback` → `MoveItemIntent` (new intent, carries `fromSlot`, `toSlot`)
- `setDeleteItemCallback` → `DeleteItemIntent` (new intent, carries `slot`)
- `setEquipmentChangedCallback` → `EquipmentChangedIntent` (new intent)

**SetupTradeManagerCallbacks (network callbacks):**
- Trade manager callbacks (sendTradeRequest, sendTradeRequestAck, sendTradeCoins,
  sendTradeAcceptClick, sendCancelTrade, sendMoveCoin, etc.) call network methods
  directly. These already have corresponding intent types (TradeRequestIntent,
  TradeAcceptIntent, TradeCancelIntent) or are responses to bridge events. Move
  network sending to ProcessBridgeIntents handlers and empty the function.

Acceptance criteria:
- All 4 Setup*Callbacks functions have empty bodies
- New intent types added to renderer_intents.h and RendererIntent variant
- ProcessBridgeIntents handles all new intent types
- WindowManager pushes intents instead of calling lambdas

#### D20b2: InitGraphics callbacks → intents + events

Remove remaining callback registrations from InitGraphics.

**Action callbacks → intents (renderer→game):**
- `setHotbarActivateCallback` → `HotbarActivateIntent` (new intent carrying slot
  index + action type). Complex handler for spell cast, item use, emote, skill
  activation. Logic moves to ProcessBridgeIntents.
- `setGroupAcceptCallback` → `GroupAcceptIntent` (new intent)
- `setHotbarCreateCallback` → `HotbarCreateIntent` (new intent, carries `skillId`)
- `chatWindow->setLinkClickCallback` → `ChatLinkClickIntent` (new intent, carries
  link type + data)

**Notification callbacks → bridge events (game→renderer):**
- `setHUDCallback` → remove. Renderer builds HUD from PlayerStatsChanged events.
  Complex HUD string builder moves to renderer side.
- `setBuffFadeCallback` → already publishes VisionChanged bridge event. Remove the
  callback registration; buff manager publishes VisionChanged directly.
- `setOnSkillActivated` on skill_manager → skill activation feedback with cooldown.
  Already have SkillValueChanged event; add cooldown data or separate event.
- `setOnSkillUpdate` on skill_manager → skill-up notification. Already have
  SkillValueChanged event.

Acceptance criteria:
- Zero callback lambdas registered in InitGraphics
- All action callbacks converted to intents
- Notification callbacks replaced by direct bridge event publishing
- HUD string builder logic moved to renderer

#### D20b3: Chat window + collision map decoupling

**Chat window coupling (3 points):**
- `setCommandRegistry(m_command_registry.get())` → renderer creates its own
  CommandRegistry populated from a command list event, or command auto-completion
  is driven by intent round-trip
- `setEntityNameProvider(...)` → renderer builds entity name cache from
  EntitySpawned/EntityDespawned events (names already in event data)
- Remove these 2 pointer passes from InitGraphics

**Collision map (5 call sites):**
- 2 clearing calls (`setCollisionMap(nullptr)` before zone map reset) — remove
  `#ifdef` blocks, renderer clears its own collision map on ZoneChanged event
- 3 setting calls (`setCollisionMap(m_zone_map.get())`) — renderer receives
  collision map pointer via CollisionMapChanged event (already defined in event_bus.h)
- Renderer holds its own `HCMap*` updated via bridge events

Acceptance criteria:
- Zero chat window pointer/lambda coupling in eq.cpp
- Zero `setCollisionMap()` calls in eq.cpp
- Renderer manages its own collision map pointer via events

#### D20b4: Window pointer decoupling

Refactor UI windows to maintain local state copies driven by bridge events instead
of polling game state via raw pointers. This is the hardest part — 3 windows hold
`EverQuest*` back-pointers, and 5 windows hold manager pointers.

**Manager pointer windows (poll manager each frame):**
- `initSpellGemPanel(m_spell_manager)` → SpellGemPanel maintains local gem state
  from SpellGemChanged events
- `initBuffWindow(m_buff_manager)` → BuffWindow maintains local buff list from
  BuffUpdated/BuffRemoved events
- `initSkillsWindow(m_skill_manager)` → SkillsWindow maintains local skill values
  from SkillValueChanged/SkillsRefreshed events
- `initTradeWindow(m_trade_manager)` → TradeWindow already receives trade events;
  remove direct manager access

**EverQuest* back-pointer windows (poll EQ each frame):**
- `initGroupWindow(this)` → GroupWindow maintains local member list from
  GroupChanged/GroupMemberUpdated events. Currently queries m_group_members,
  m_group_leader_name, m_is_group_leader, m_group_member_count.
- `initPlayerStatusWindow(this)` → PlayerStatusWindow maintains local stats from
  PlayerStatsChanged/CharacterInfoChanged events. Currently queries HP, mana,
  endurance, level, class, name, AC, ATK, weight, currency.
- `initPetWindow(this, m_buff_manager)` → PetWindow maintains local pet state from
  PetCreated/PetStatsChanged/PetButtonStateChanged events. Currently queries
  pet spawn ID, name, level, HP, buff data.

**Inventory manager:**
- `setInventoryManager(m_inventory_manager)` → renderer manages its own inventory
  view state from InventorySlotChanged/CursorItemChanged events

Acceptance criteria:
- Zero raw game-state pointers passed to any UI window
- All windows work from event-driven local state copies
- All init*Window calls removed from InitGraphics

#### D20b5: Renderer lifecycle cleanup

Move renderer lifecycle management from EverQuest to Application.

- Remove `m_irrlichtBridge` creation from InitGraphics — Application creates bridge
  and passes to both EverQuest (as GameStateBridge*) and renderer
- Remove `m_renderer->setBridge(m_irrlichtBridge.get())` — Application handles this
- Remove `m_renderer->initLoadingScreen(config)` — Application handles this
- Simplify `ShutdownGraphics()` — renderer shutdown owned by Application;
  EverQuest only calls `detachBridge()` to stop publishing events
- Remove loading thread management from EverQuest — renderer owns its loading thread

Acceptance criteria:
- EverQuest does not create, configure, or destroy the renderer
- InitGraphics() reduced to `attachBridge(GameStateBridge*)` or equivalent
- ShutdownGraphics() reduced to `detachBridge()`
- Application owns full renderer lifecycle

### D20c: Refactor UpdateGraphics, zone loading ownership, and game-side renderer queries

Split into 4 sub-units (D20c1–D20c4) due to scope.

#### D20c1: Move UpdateGraphics to Application

Application calls the renderer directly instead of going through EverQuest.

**What moves to Application::render():**
- `m_renderer->processFrame(deltaTime)` — the main render call
- Loading thread join/complete detection — checking if loading thread finished,
  calling JoinLoadingThread() + OnGraphicsComplete()
- `ProcessBridgeIntents()` / `ProcessBridgeEvents()` — drain bridge queues per-frame

**What stays in EverQuest (called from Application before render):**
- `m_spell_manager->update(deltaTime)` — spell cooldowns, memorization timeouts
- `m_buff_manager->update(deltaTime)` — buff duration ticking

**EverQuest changes:**
- Remove `UpdateGraphics()` method entirely
- Add `TickManagers(float deltaTime)` for spell/buff updates (called by Application)

**Application changes:**
- `Application::render()` calls `m_renderer->processFrame()` directly
- Calls `ProcessBridgeIntents()` / `ProcessBridgeEvents()` before processFrame
- Handles loading thread lifecycle (join detection, OnGraphicsComplete)

Acceptance criteria:
- EverQuest has no UpdateGraphics() method
- Application calls renderer->processFrame() directly
- Spell/buff manager ticking called from Application

#### D20c2: Convert per-frame renderer polls to events

Remove per-frame state pushes from UpdateGraphics that should be change-driven.

**Target HP polling:**
- Currently: UpdateGraphics polls target HP on a 1-second timer, pushes
  TargetHPUpdated event
- Fix: publish TargetHPUpdated from packet handlers when HP actually changes
  (ZoneProcessMobHealth already publishes EntityStatsChanged). Remove the poll.

**Time of day:**
- Currently: UpdateGraphics pushes TimeOfDayChanged every frame
- Fix: only push when server sends time update (already done in ZoneProcessTimeOfDay).
  Remove the per-frame push.

**Audio listener sync (camera position):**
- Currently: UpdateGraphics queries camera transform from renderer for audio
- Fix: renderer pushes camera position as part of processFrame output, or audio
  manager moves to renderer side. For now, Application reads camera after render.

**Volume hotkeys:**
- Currently: UpdateGraphics reads `[`/`]` key state from renderer event receiver
- Fix: renderer pushes VolumeChangeIntent when keys are pressed

Acceptance criteria:
- No per-frame state polls in the render path
- Target HP and time of day changes are event-driven only
- Audio sync handled by Application or renderer

#### D20c3: Zone loading pipeline through bridge

Move zone loading functions from EverQuest to renderer-managed lifecycle.

**Functions to move:**
- `LoadZoneGraphicsOnLoadingThread()` / `LoadZoneGraphics()` — zone asset loading
  moves to renderer (it already runs on renderer's loading thread)
- `StartLoadingThread()` / `JoinLoadingThread()` — loading thread management
  moves to renderer (Application coordinates via bridge events)
- `OnGraphicsComplete()` — post-load setup. Renderer notifies completion via
  intent/event, game thread handles game-state side

**Zone cleanup:**
- Zone cleanup calls (`unloadZone()`, `showLoadingScreen()`, `setZoneReady(false)`,
  `setCollisionMap(nullptr)`) currently in BeginZoneChange — renderer handles via
  ZoneChanged bridge event

**BSP water detection:**
- `CheckWaterState()` references `m_renderer->getZoneBspTree()` — currently this
  function doesn't exist in the codebase. If/when implemented, renderer should handle
  water detection and push SwimmingStateChanged via bridge.

Acceptance criteria:
- Zone loading functions live in renderer, not EverQuest
- Loading thread owned by renderer
- Zone cleanup triggered by bridge events

#### D20c4: Remove remaining game-side renderer queries

Remove the last direct `m_renderer->` calls from EverQuest that aren't zone loading.

**requestQuit (2 call sites):**
- `/quit` slash command and `ZoneProcessLogoutReply()` call
  `m_renderer->requestQuit()`. Replace with Application-level quit signaling
  (EverQuest calls `m_bridge->pushIntent(RequestQuitIntent{})`, Application
  checks for quit requests, or Application exposes a quit callback).

**Hotbar config save/load:**
- `SaveHotbarConfig()` reads hotbar state from renderer's WindowManager
- `LoadHotbarConfig()` writes hotbar state to renderer's WindowManager
- Move to renderer side: renderer saves/loads its own hotbar config, triggered
  by a bridge event or during renderer init/shutdown

Acceptance criteria:
- Zero `m_renderer->requestQuit()` calls in EverQuest
- Hotbar save/load does not access renderer from game thread
- All remaining m_renderer-> calls are zone loading (D20c3 scope) or removed
- Audio positioning solved (event or render-side)
- Zero game-side queries to renderer state (BSP tree, hotbar config, quit)
- `requestQuit` works through bridge or Application-level signaling

### D20d: Remove debug/toggle slash commands from EverQuest

Move renderer-specific commands to the renderer (~26 commands):

*Toggle/debug commands (~19, originally listed):*
- `/sort`, `/upload`, `/portal`, `/stencil`, `/governor`, `/plight`, `/olight`,
  `/zlight`, `/fire`, `/renderdist`, `/clipdist`, `/sky`, `/skytype`, `/time`,
  `/skills` (window toggle), `/filter`, `/timestamp`, `/particlereload`,
  `/weatherreload`

*Detail manager commands (7, added by audit):*
- `/detail`, `/togglegrass`, `/toggleplants`, `/togglerocks`, `/toggledebris`,
  `/season`, `/detailinfo`
- All 7 access `m_renderer->getDetailManager()` (lines 10058-10215)

*Diagnostic commands (added by audit):*
- `/pmem` — `runPmemDiagnostics()` (lines 7519-7611). Game thread collects its stats
  (process RSS, entity count, connection stats, audio metrics), pushes event. Renderer
  merges with its stats (`getMemoryReport()`), displays combined report. Needs
  `RequestMemoryReport` intent handler (currently stub) and `MemoryReportEvent`
- `/loaddump` — `runLoadDiagnostics()` (line 7624). `m_renderer->dumpScene()`. Needs
  `RequestSceneDump` intent handler (currently stub)

*Implementation approach:*
- Game thread posts `SlashCommandIntent { fullCommand }` through bridge
- Renderer handles renderer-specific commands directly
- Game-affecting commands (`/camp`, `/quit`, `/who`, `/tell`, etc.) stay on game thread
- `/filter` needs special handling — accesses chat window state (may need intent)

Acceptance criteria:
- Zero renderer-specific slash commands in EverQuest
- Zero `m_renderer->getDetailManager()` calls in EverQuest
- Zero `m_renderer->getMemoryReport()` / `dumpScene()` calls in EverQuest
- All toggle/debug/detail/diagnostic commands work through bridge
- `RequestMemoryReport` and `RequestSceneDump` intent stubs activated

### D20e: Remove m_renderer from EverQuest

The final decoupling (after D19c and D20a-D20d are complete):
- Remove `m_renderer` member from EverQuest class (eq.h line 1579)
- Remove `GetRenderer()` accessor from eq.h (line 997)
- Remove `#include` of all renderer/graphics headers from eq.h/eq.cpp
  (currently: `constrained_renderer_config.h`, `loading_thread.h`)
- Move `inventory_constants.h` out of `graphics/ui/` to `include/client/state/`
  (it contains pure game-state constants — slot IDs, equipment enums, container counts —
  with zero rendering dependencies, but is trapped in the graphics directory tree)
- Replace `InitGraphics()` with `attachBridge(GameStateBridge*)`
- `ShutdownGraphics()` reduced to `detachBridge()` (renderer cleanup owned by Application)
- EverQuest communicates solely through bridge

Acceptance criteria:
- eq.h has zero includes from `client/graphics/`
- eq.cpp has zero references to `IrrlichtRenderer`
- EverQuest can be instantiated with no renderer (headless mode works via null bridge)
- All existing functionality preserved through bridge path

### D20f: Decouple RDP server from renderer

`SetupRDPAudio()` (lines 20487-20493) accesses `m_renderer->getRDPServer()` to
connect audio transport to the RDP server. The RDP server should not be accessed
through the renderer.

Options:
  (a) Application owns RDP server directly (not via renderer) — RDP server created
      at startup, passed to renderer for frame delivery, passed to audio for transport
  (b) Renderer exposes audio transport interface via bridge event — game thread
      receives `AudioTransportAvailable` event with connection info

Acceptance criteria:
- Zero `m_renderer->getRDPServer()` calls in EverQuest
- RDP audio setup works without renderer access
- RDP and VNC can still run simultaneously

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

**Application class refactoring (prerequisite from D20c):**
By D21, Application already owns the renderer directly (not via EverQuest). D21 adds:
- Spawn game thread running `EverQuest::TickLoop()` (network + game state)
- Main thread runs render loop: `drainEvents() → processFrame() → postIntents()`
- Startup sequencing: Application creates renderer, creates EverQuest, attaches bridge,
  spawns game thread, enters render loop
- Shutdown sequencing: game thread signals stop, renderer finishes current frame,
  both threads join, renderer destroyed, EverQuest destroyed

**Zone loading handoff:**
The game thread publishes `PlayerZoneChanged` with zone data through the bridge.
The renderer receives this event and manages its own loading thread (GL context
transfer, `loadZoneSequential()`, entity/door registration). The game thread
continues processing network packets during loading — zone loading no longer blocks
game state updates.

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
| InitGraphics() complexity (~800 lines, 25+ callbacks, 13 raw pointer passes) | Split into 5 sub-units (D20a-D20e) for incremental decoupling |
| Zone loading pipeline complexity (LoadZoneGraphics + LoadZoneGraphicsOnLoadingThread) | Dedicated D19b unit; zone data flows through bridge events; renderer owns loading thread |
| UpdateGraphics per-frame polls (target HP, time of day, camera, volume) | D20c converts polls to change-driven events before renderer ownership transfer |
| Weapon skill state owned by renderer, queried by game thread (ZoneProcessEmote) | D17a fixes ownership: game state tracks weapon skills from spawn data |

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
- ~13 raw pointer couplings removed (D20b)
- Zone loading pipeline restructured (D19b, D20c)
- UpdateGraphics per-frame polls converted to events (D20c)
- Application class refactored to own renderer directly (D20c, D21)
- Net code reduction in eq.cpp once direct calls removed (Phase 5)
- 34 total units (original 26, +9 from splits/additions, -1 from D23 deletion)
