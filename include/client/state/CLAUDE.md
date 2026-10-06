# Game State & Event Bus

## Overview

Game state is fully decoupled from rendering. The `GameState` container aggregates 10 domain-specific state modules and an `EventBus`. All state mutations publish events that cross the thread boundary via `GameStateBridge`.

## GameState Container (`game_state.h`)

Central owner of all state modules:

| Module | Header | Responsibility |
|--------|--------|----------------|
| `PlayerState` | `player_state.h` | Position, stats (HP/mana/endurance), level, name, movement mode |
| `EntityManager` | `entity_manager.h` | NPC/player/corpse tracking by spawn_id |
| `WorldState` | `world_state.h` | Zone name/ID, collision maps, time of day |
| `CombatState` | `combat_state.h` | Target, auto-attack, combat mechanics |
| `GroupState` | `group_state.h` | Group membership, member HP/mana |
| `InventoryState` | `inventory_state.h` | Equipment (22 slots), general inventory (8 bags), currency |
| `SpellState` | `spell_state.h` | Spellbook, memorized gems (8), buffs, cooldowns |
| `TradeState` | `trade_state.h` | Player-to-player trade session state |
| `PetState` | `pet_state.h` | Pet name/level/HP, command state |
| `DoorStateManager` | `door_state_manager.h` (`include/client/`) | Door positions and open/closed state (`doors_`), and since T376 (B141) the pending-click set (`pendingClicks_`, `addPendingClick()`/`isClickPending()`) that `EntitiesComponent` used to duplicate |

**Lifecycle:**
- `resetForZoneChange()` — clears entities/doors for zone transitions
- `clearAll()` — complete state reset (disconnect)
- `isFullyZonedIn()` — convenience check

## EventBus (`event_bus.h`)

Thread-safe publish/subscribe system for game state changes.

**T451 split the header** (it was 1,269 lines, riding the ceiling) into the
enum, five payload families and an umbrella. `event_bus.h` still holds the
`EventData` variant, `GameEvent` and `class EventBus`, and includes all six of
the others, so **every existing include of
`include/client/state/event_bus.h` compiles unchanged and it is still the one
include a consumer needs**:

| header | holds |
|---|---|
| `event_types.h` | `enum class GameEventType` -- the one enumerator list, and what `DocCounts` and `ReachabilitySweep` read |
| `event_payloads_entity.h` | player, entity, door, zone |
| `event_payloads_social.h` | chat, combat, group, time of day, pet, windows |
| `event_payloads_inventory.h` | inventory, audio volumes, loot, trade |
| `event_payloads_spell.h` | spells, buffs, skills, weather, zone lifecycle |
| `event_payloads_ui.h` | UI, hotbar, spell visuals, diagnostics |

A new event adds its enumerator to `event_types.h`, its payload struct to the
family header it belongs to, and its variant alternative to `event_bus.h` --
three files, none of which is the 1,269-line one this replaced.

**102 event types** (`GameEventType`, re-counted 2026-10-05 by B430, which added `GameOptionChanged`; T439 added `VolumesChanged`; T421 added `ItemDisplayShown` and T419 `TradeskillRecipeListCleared` / `TradeskillRecipeReceived`) organized by domain; `IrrlichtBridge` has a case for all 102 in its switch, `ConsoleBridge` logs a subset headless. **This number is netted by `DocCounts`** (`scripts/refactor/check_doc_counts.py`), so a new enumerator that leaves it stale is a red test, not a silent drift:
- **Player**: PositionChanged, StatsChanged, MovementModeChanged, LevelChanged
- **Entity**: EntitySpawned, EntityDespawned, EntityMoved, EntityAppearanceChanged, EntityAnimation
- **Door**: DoorSpawned, DoorStateChanged
- **Zone**: ZoneLoading, ZoneLoaded, ZoneChanged
- **Chat**: ChatMessage, TellReceived, ChannelMessage
- **Combat**: CombatHit, CombatMiss, TargetChanged, AutoAttackToggled
- **Group**: GroupInviteReceived, GroupMemberJoined, GroupMemberLeft, GroupUpdated
- **Pet**: PetSpawned, PetDespawned, PetStatsChanged
- **Spell**: SpellCastStarted, SpellCastComplete, SpellInterrupted, BuffApplied, BuffRemoved, GemStateChanged
- **Skill**: SkillUpdated, SkillActivated
- **Inventory**: ItemReceived, ItemMoved, EquipmentChanged, CurrencyChanged
- **Loot**: LootWindowOpened, LootItemReceived
- **Trade**: TradeStarted, TradeItemAdded, TradeCompleted
- **Window**: InventoryToggled, SpellbookToggled, SkillsToggled
- **UI**: HotbarChanged, VisionChanged
- **Diagnostic**: MemoryReport, SceneDump

**EventData** is a `std::variant` of typed data structs (one per event that carries payload). Each `GameEvent` pairs a `GameEventType` enum with its data.

**Usage:**
```cpp
// Subscribe (filtered by type)
auto handle = eventBus.subscribe(GameEventType::EntitySpawned,
    [](const GameEvent& e) { /* handle spawn */ });

// Publish
eventBus.publish(GameEvent{GameEventType::EntitySpawned, EntitySpawnData{...}});

// Unsubscribe
eventBus.unsubscribe(handle);
```

## Key Design Rules

- **Zero graphics dependencies**: No state module includes any graphics header
- **Events are immutable**: Published events are const references, safe to read from any thread
- **State modules publish events on mutation**: Callers don't need to manually publish
- **All resource allocation at startup** (Batch S): State managers are pre-allocated, not lazily created during zone connect
