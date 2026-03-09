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
| D01 | Define game event types | pending | |
| D02 | Define renderer intent types | pending | |
| D03 | Create GameStateBridge interface and IrrlichtBridge skeleton | pending | |

### D01: Define Game Event Types

Create `include/client/events/game_events.h` with the event structs that the game
thread will publish. These are domain events — they describe what happened in game
terms, not what the UI should do.

Event categories and types:

**Entity events:**
- `EntitySpawned { spawnId, name, race, gender, class_, level, position, heading, appearance }`
- `EntityDespawned { spawnId }`
- `EntityMoved { spawnId, x, y, z, heading, deltaHeading, animation, velocity }`
- `EntityAppearanceChanged { spawnId, appearance }` (equipment, illusion, etc.)
- `EntityHPChanged { spawnId, hpPercent }`
- `CorpseDecayStarted { spawnId }`

**Player events:**
- `InitialPlayerPosition { x, y, z, heading }` (zone-in only)
- `PlayerStatsChanged { hp, maxHp, mana, maxMana, level, character }` (for HUD)
- `PlayerTargetChanged { spawnId, targetInfo }` (or cleared)
- `PlayerZoneChanged { zoneId, zoneName }` (zone-in/out transitions)

**Chat events:**
- `ChatMessageReceived { channel, sender, text, color }`
- `SystemMessage { text, color }`

**Combat events:**
- `CombatStateChanged { autoAttack, targetId }`
- `DamageEvent { sourceId, targetId, amount, type, spellName }`
- `SpellCastStarted { casterId, spellId, castTime }`
- `SpellCastComplete { casterId, spellId, result }`

**Inventory events:**
- `InventoryItemUpdated { slot, item }` (covers add, remove, swap)
- `CurrencyChanged { platinum, gold, silver, copper }`
- `LootWindowOpened { corpseId, items }`
- `LootWindowClosed { corpseId }`

**Trade events:**
- `TradeStarted { partnerId, partnerName }`
- `TradeItemUpdated { who, slot, item }`
- `TradeCancelled {}`
- `TradeCompleted {}`

**Vendor events:**
- `VendorWindowOpened { vendorId, items }`
- `VendorWindowClosed {}`

**Bank events:**
- `BankWindowOpened { items, sharedItems }`
- `BankWindowClosed {}`

**Group events:**
- `GroupMemberAdded { slot, name, memberId }`
- `GroupMemberRemoved { slot }`
- `GroupDisbanded {}`
- `GroupInviteReceived { inviterName }`

**Pet events:**
- `PetStatusChanged { petId, hp, mana, name }`
- `PetDespawned {}`

**Spell events:**
- `SpellGemUpdated { gemSlot, spellId, spellName, cooldownRemaining }`
- `SpellBookUpdated { spells }` (full refresh)
- `BuffUpdated { slot, spellId, ticksLeft, casterName }`
- `BuffRemoved { slot }`

**Door events:**
- `DoorStateChanged { doorId, state, position }` (open/close/locked)
- `DoorsLoaded { doors }` (zone-in bulk load)

**World/environment events:**
- `TimeOfDayChanged { hour, minute }`
- `WeatherChanged { type, intensity }`

**Skill events:**
- `SkillValueChanged { skillId, value }`
- `SkillsRefreshed { skills }` (full refresh at zone-in)

All event types should be simple structs (POD where possible) with no renderer
dependencies. Use `std::variant<...>` as `GameEvent` to hold any event type.
Include a timestamp field or let the consumer add one.

Acceptance criteria:
- Header compiles with no renderer includes
- All structs are default-constructible and movable
- `GameEvent` variant can hold any event type
- Unit test validates variant construction and visitation

### D02: Define Renderer Intent Types

Create `include/client/events/renderer_intents.h` with the intent structs that
renderers post back to the game thread.

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

**Vendor:**
- `VendorBuyIntent { vendorId, slot, quantity }`
- `VendorSellIntent { vendorId, slot, quantity }`
- `VendorCloseIntent {}`

**Bank:**
- `BankMoveItemIntent { fromSlot, toSlot }`
- `BankCloseIntent {}`

**Trade:**
- `TradeRequestIntent { targetId }`
- `TradeAddItemIntent { slot, item }`
- `TradeAcceptIntent {}`
- `TradeCancelIntent {}`

**Spells:**
- `CastSpellIntent { gemSlot }`
- `MemorizeSpellIntent { gemSlot, spellId }`
- `ForgetSpellIntent { gemSlot }`
- `InterruptSpellIntent {}`

**Skills:**
- `UseSkillIntent { skillId }`

**Pet:**
- `PetCommandIntent { command }` (attack, back, follow, guard, etc.)

**Group:**
- `InviteToGroupIntent { targetName }`
- `DisbandIntent {}`
- `DeclineInviteIntent {}`

**General:**
- `RequestCampIntent {}`
- `RequestQuitIntent {}`
- `SlashCommandIntent { fullCommand }` (fallback for commands not yet mapped)

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

The bridge owns two thread-safe queues (events and intents) using `std::mutex` +
`std::vector` swap pattern (lock, swap with empty vector, unlock, process).

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

| Unit | Description | Status | Commit |
|------|-------------|--------|--------|
| D04 | Publish entity events alongside existing calls | pending | |
| D05 | Publish chat, combat, and player stat events | pending | |
| D06 | Publish inventory, loot, vendor, bank, trade events | pending | |
| D07 | Publish door, group, pet, spell, skill events | pending | |
| D08 | Publish world/environment events (time, weather) | pending | |

Each unit follows the same pattern:
1. Identify all packet handlers and game logic that currently call `m_renderer->...`
   for the event category
2. After each existing direct call, add `m_bridge->pushEvent(...)` with the equivalent
   event struct
3. Add a validation mode: log a warning if the bridge event and the direct call
   would produce different state (sanity check during development)

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
| D09 | IrrlichtBridge handles entity events | pending | |
| D10 | IrrlichtBridge handles chat, combat, player stat events | pending | |
| D11 | IrrlichtBridge handles inventory, loot, vendor, bank, trade events | pending | |
| D12 | IrrlichtBridge handles door, group, pet, spell, skill events | pending | |
| D13 | IrrlichtBridge handles world/environment events | pending | |

Each unit follows the same pattern:
1. Implement `applyEvent()` cases for the category — translate event struct into
   the existing `m_renderer->...` calls that the bridge now owns
2. Verify that the bridge-driven calls produce identical results to the direct calls
3. Add a flag (`bridgeDriven_`) that, when enabled, skips the direct call and relies
   solely on the bridge. Default: off (both paths run).

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
| D15 | Interaction intents: target, combat, door, loot, chat submit | pending | |
| D16 | UI intents: vendor, bank, trade, spell, skill, pet, group | pending | |

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

### D15–D16: Other Intents

Same pattern: identify current callback, replace with intent post from renderer side,
add intent handler on game thread side. The `targetCallback_`, `chatSubmitCallback_`,
`doorInteractCallback_`, etc. each become an intent type.

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
| D18 | Remove direct renderer calls from UI packet handlers (inventory, vendor, bank, trade, loot) | pending | |
| D19 | Remove direct renderer calls from chat, combat, spell, skill, pet, group, door, world handlers | pending | |
| D20 | Remove callback lambdas from InitGraphics(), remove m_renderer from EverQuest | pending | |

### D20: The Final Cut

- Remove `m_renderer` member from EverQuest class
- Remove `#include` of all renderer/graphics headers from eq.h/eq.cpp
- EverQuest now communicates solely through `GameStateBridge*`
- `InitGraphics()` is replaced by `attachBridge(GameStateBridge*)` or similar
- `UpdateGraphics()` is replaced by bridge event/intent drain on the game thread
- The `networkTickCallback_` is eliminated — network runs on game thread exclusively
- `LoadZoneGraphics()` is replaced by a `PlayerZoneChanged` event; the bridge/renderer
  decides how to handle zone loading independently

Acceptance criteria:
- eq.h has zero includes from `client/graphics/`
- eq.cpp has zero references to `IrrlichtRenderer`
- EverQuest can be instantiated with no renderer (headless mode works via null bridge)
- All existing functionality preserved through bridge path

---

## Phase 6 — Thread Separation (D21–D23)

Move EverQuest to its own thread. The main thread becomes the render thread.

| Unit | Description | Status | Commit |
|------|-------------|--------|--------|
| D21 | Move EverQuest tick loop to dedicated game thread | pending | |
| D22 | Synchronize bridge queues with proper threading | pending | |
| D23 | Remove networkTickCallback_ and loading screen coupling | pending | |

### D21: Game Thread

The main loop in `Application::mainLoop()` currently runs everything sequentially:
```
while running:
    processNetworkEvents()
    updateGameState()
    render()
```

New structure:
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
```

The game thread runs at a fixed tick rate. The render thread runs at display refresh
rate. They are fully decoupled — a slow render frame doesn't delay network processing,
and a packet burst doesn't cause frame hitches.

### D22: Queue Synchronization

The bridge queues (events and intents) use the swap-vector pattern:
- Producer: lock, push_back, unlock
- Consumer: lock, swap with empty vector, unlock, process locally

This gives O(1) lock time regardless of queue depth. No contention between producers
and consumers except during the brief swap.

Add sequence numbers to events/intents for ordering guarantees and debugging.
Add high-water-mark warning if queue depth exceeds threshold (indicates one side
is falling behind).

### D23: Eliminate networkTickCallback_

The `networkTickCallback_` exists because the renderer currently calls `TickNetwork()`
during heavy loading to prevent login timeout. With game state on its own thread,
network processing runs continuously regardless of what the renderer is doing.

Remove:
- `networkTickCallback_` from IrrlichtRenderer
- All `if (networkTickCallback_) networkTickCallback_()` calls in advanceBackgroundZoneLoad()
- The callback setup in InitGraphics()

The loading screen flow changes:
- Game thread processes zone-in packets, publishes events (`PlayerZoneChanged`,
  `DoorsLoaded`, `EntitySpawned` batch, etc.)
- Bridge queues these for the renderer
- Renderer sees `PlayerZoneChanged`, begins zone asset loading independently
- Renderer shows/hides loading screen based on its own loading state
- Game thread is unaware of loading screen existence

Acceptance criteria:
- Game state runs on dedicated thread, render on main thread
- Network processing never blocks on rendering
- Rendering never blocks on network processing
- Zone loading works: game thread sends zone data through events, renderer loads
  assets independently
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

---

## Risk Assessment

| Risk | Mitigation |
|------|-----------|
| Subtle ordering bugs (event A must arrive before event B) | Sequence numbers on events; single-producer-single-consumer per bridge; preserve publish order |
| Performance regression from queue overhead | Swap-vector pattern is O(1) lock time; batch high-frequency events (D24) |
| Incomplete event coverage (missed a direct renderer call) | Phase 2 dual-path approach catches misses — both paths run until verified |
| Loading screen timing changes | Game thread is unaware of loading; renderer manages its own loading state based on asset readiness |
| HUD callback reads private EverQuest state | Replace with `PlayerStatsChanged` event pushed from game thread (D05) |
| Zone loading requires networkTickCallback_ | Eliminated in D23 — game thread runs independently, network never stalls |
| Collision ownership unclear for edge cases | Renderer owns all collision; game thread only does distance-based checks (zone lines, aggro range) |
| Regression in headless mode | Null bridge or no bridge attached — game state runs unchanged, just no events consumed |

---

## Dependencies

- No external library additions required
- Uses existing `std::mutex`, `std::thread`, `std::condition_variable`
- Follows `BackgroundWorkQueue` patterns established in Batch A
- `GameState` class and `EventBus` (already in codebase) may be leveraged or replaced
  by the new event system depending on overlap assessment during D01

## Estimated Scope

- ~30-40 event structs (D01)
- ~20-25 intent structs (D02)
- ~1500 lines for bridge interface + IrrlichtBridge (D03, D09-D13)
- ~100+ call sites in eq.cpp migrated from direct to event-based (D04-D08, D17-D19)
- ~14 callbacks replaced with intents (D14-D16, D20)
- Net code reduction in eq.cpp once direct calls removed (Phase 5)
