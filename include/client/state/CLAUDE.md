# Game State & Event Bus

## Overview

Game state is fully decoupled from rendering. The `GameState` container aggregates 11 domain-specific state modules and an `EventBus`. All state mutations publish events that cross the thread boundary via `GameStateBridge`.

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
| `PetState` | `pet_state.h` | Pet name/level/HP, command state |
| `DoorStateManager` | (in game state layer) | Door positions and open/closed state |
| `TradeskillState` | `tradeskill_state.h` | Tradeskill container contents and combines |

**Lifecycle:**
- `resetForZoneChange()` — clears entities/doors for zone transitions
- `clearAll()` — complete state reset (disconnect)
- `isFullyZonedIn()` — convenience check

## EventBus (`event_bus.h`)

Thread-safe publish/subscribe system for game state changes.

**140+ event types** organized by domain:
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

**EventData** is a `std::variant` holding 100+ typed data structs. Each `GameEvent` pairs a `GameEventType` enum with its data.

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
