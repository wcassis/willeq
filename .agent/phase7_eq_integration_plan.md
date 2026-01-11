# Phase 7: EQ Class Integration Plan

## Overview

Phase 7 migrates the EverQuest class to use GameState as the single source of truth for game state, rather than maintaining its own duplicate state. This is the final phase of the state management refactor.

**Current Architecture:**
```
Server Packets → EQ Class (owns state) → Application::sync() → GameState → Renderers/UI
```

**Target Architecture:**
```
Server Packets → EQ Class (packet handlers) → GameState (owns state) → Renderers/UI
                                                    ↑
                                            EQ Class reads from here
```

## Goals

1. Eliminate duplicate state between EQ class and GameState
2. Make GameState the authoritative source of truth
3. Have packet handlers update GameState directly
4. Have EQ class methods read from GameState instead of internal members
5. Remove Application::syncGameStateFromClient() as it becomes unnecessary

## Migration Strategy

Each sub-phase follows this pattern:
1. **Identify** - List state variables to migrate
2. **Add** - Ensure GameState has equivalent storage (may already exist)
3. **Update Writers** - Modify packet handlers to write to GameState
4. **Update Readers** - Modify EQ class methods to read from GameState
5. **Remove** - Delete duplicate member variables from EQ class
6. **Test** - Verify functionality is preserved

---

## Sub-Phase 7.1: Movement State Migration ✅ COMPLETE

**Priority:** High (foundational for other systems)

**Status:** COMPLETE

**Implementation Summary:**
- PlayerState already had all required movement state fields (position, heading, velocity, isMoving, moveSpeed, animation, MovementMode, PositionState, movement target, follow target, jump state)
- EQ class already has embedded `m_game_state` member - no pointer injection needed
- Updated movement accessors to read from `m_game_state.player()`:
  - `GetPosition()` → reads from GameState
  - `GetHeading()` → reads from GameState
  - `IsMoving()` → reads from GameState
- Updated movement mutators to write to both local and GameState (gradual migration):
  - `SetHeading()` → writes to both `m_heading` and `m_game_state.player().setHeading()`
  - `SetMoveSpeed()` → writes to both `m_move_speed` and `m_game_state.player().setMoveSpeed()`
  - `SetPosition()` → writes to both `m_x/m_y/m_z` and `m_game_state.player().setPosition()`
  - `SetMoving()` → writes to both `m_is_moving` and `m_game_state.player().setMoving()`

**Files Modified:**
- `include/client/eq.h` - Changed inline accessors to declarations
- `src/client/eq.cpp` - Added implementation section (~line 8388)

**State to Migrate:**
```cpp
// Current EQ class members:
float m_x, m_y, m_z;              // Current position
float m_heading;                   // Current heading
bool m_is_moving;                  // Movement flag
int16_t m_animation;               // Current animation
MovementMode m_movement_mode;      // RUN/WALK/SNEAK
PositionState m_position_state;    // STANDING/SITTING/CROUCHING/FEIGN_DEATH/DEAD

// Movement target (for pathfinding)
float m_target_x, m_target_y, m_target_z;
float m_move_speed;

// Follow behavior
std::string m_follow_target;
float m_follow_distance;

// Jump state
bool m_is_jumping;
float m_jump_start_z;
std::chrono::steady_clock::time_point m_jump_start_time;
```

**GameState Updates Required:**
- PlayerState already has position/heading
- Add: MovementMode, PositionState enums
- Add: isMoving, animation, moveSpeed
- Add: movementTarget (x,y,z)
- Add: followTarget, followDistance
- Add: jump state (isJumping, jumpStartZ, jumpStartTime)

**Packet Handlers to Update:**
- `HandleSpawnAppearance()` - Position state changes
- Position update handlers
- Animation handlers

**EQ Methods to Update:**
- `GetPosition()` - Read from GameState
- `GetHeading()` - Read from GameState
- `IsMoving()` - Read from GameState
- `SetPosition()` - Write to GameState
- `UpdateMovement()` - Use GameState

---

## Sub-Phase 7.2: Player Attributes & Currency Migration ✅ COMPLETE

**Priority:** High

**Status:** COMPLETE

**Implementation Summary:**
- PlayerState already had all required attribute, currency, and character info fields
- Updated 29 accessors in EQ class to read from `m_game_state.player()`:
  - Character info: GetLevel, GetClass, GetRace, GetGender, GetDeity
  - Vital stats: GetCurrentHP, GetMaxHP, GetCurrentMana, GetMaxMana, GetCurrentEndurance, GetMaxEndurance
  - Attributes: GetSTR, GetSTA, GetDEX, GetAGI, GetINT, GetWIS, GetCHA
  - Currency: GetPlatinum, GetGold, GetSilver, GetCopper, GetBankPlatinum, GetBankGold, GetBankSilver, GetBankCopper
  - Resources: GetWeight, GetMaxWeight, GetPracticePoints
- Updated packet handlers to write to both local members and GameState:
  - `ZoneProcessPlayerProfile()` - sets all character data including name
  - `ZoneProcessHPUpdate()` - HP changes
  - `ZoneProcessManaChange()` - mana and endurance changes
  - `ZoneProcessMoneyUpdate()` - currency updates
  - Vendor purchase handler - currency deduction
  - Training handler - practice point decrement
  - HP reset on respawn
  - Weight update in UpdateInventoryStats

**Files Modified:**
- `include/client/eq.h` - Changed 29 inline getters to declarations, added GetGender
- `src/client/eq.cpp` - Added accessor implementations and GameState sync in 8 locations

**State to Migrate:**
```cpp
// Vital Stats (already synced, need to flip direction)
uint32_t m_cur_hp, m_max_hp;
uint32_t m_mana, m_max_mana;
uint32_t m_endurance, m_max_endurance;

// Character Info
uint8_t m_level;
uint32_t m_class, m_race, m_gender, m_deity;
std::string m_character, m_last_name;

// Attributes
uint32_t m_STR, m_STA, m_DEX, m_AGI, m_INT, m_WIS, m_CHA;

// Currency
uint32_t m_platinum, m_gold, m_silver, m_copper;
uint32_t m_bank_platinum, m_bank_gold, m_bank_silver, m_bank_copper;

// Resources
float m_weight, m_max_weight;
uint32_t m_practice_points;
```

**GameState Updates Required:**
- PlayerState already has HP/mana/endurance/level
- Add: class, race, gender, deity
- Add: characterName, lastName
- Add: base attributes (STR, STA, DEX, AGI, INT, WIS, CHA)
- Add: currency (on-hand and bank) - already in PlayerState
- Add: weight, maxWeight

**Packet Handlers to Update:**
- `HandlePlayerProfile()` - Main source of player data
- `HandleHPUpdate()` - HP changes
- `HandleManaChange()` - Mana changes
- `HandleStatChange()` - Attribute changes
- `HandleLevelUpdate()` - Level changes
- `HandleMoneyUpdate()` - Currency changes

**EQ Methods to Update:**
- All `GetXXX()` stat accessors → read from GameState
- All `SetXXX()` stat mutators → write to GameState

---

## Sub-Phase 7.3: World & Zone State Migration ✅ COMPLETE

**Priority:** Medium

**Status:** COMPLETE

**Implementation Summary:**
- WorldState already had all required fields (zone info, time, zone line state)
- PlayerState already had bind point fields
- Updated zone accessors to read from GameState:
  - `GetCurrentZoneName()` → reads from `m_game_state.world().zoneName()`
  - `GetTimeOfDay()` → reads from `m_game_state.world().timeHour()/timeMinute()`
- Updated packet handlers to write to both local members and GameState:
  - `ZoneProcessNewZone()` - zone name and ID
  - `ZoneProcessTimeOfDay()` - full time data
  - `CleanupZone()` - zone state reset
  - PlayerProfile handler - bind point data
  - `LoadZoneLines()` - zone line detection state reset
  - `CheckZoneLine()` - zone line trigger/clear state
- IsZoningEnabled/SetZoningEnabled remain using local member (user preference, not game state)

**Files Modified:**
- `include/client/eq.h` - Updated GetCurrentZoneName and GetTimeOfDay to read from GameState
- `src/client/eq.cpp` - Added GameState sync in 6 locations

**State to Migrate:**
```cpp
// Zone Info
std::string m_current_zone_name;
uint16_t m_current_zone_id;

// In-game Time
uint8_t m_time_hour, m_time_minute;
uint8_t m_time_day, m_time_month;
uint16_t m_time_year;

// Bind Point
uint32_t m_bind_zone_id;
float m_bind_x, m_bind_y, m_bind_z, m_bind_heading;

// Zone Line Detection
bool m_zoning_enabled;
bool m_zone_line_triggered;
std::chrono::steady_clock::time_point m_zone_line_trigger_time;
float m_last_zone_check_x, m_last_zone_check_y, m_last_zone_check_z;
uint16_t m_pending_zone_id;
float m_pending_zone_x, m_pending_zone_y, m_pending_zone_z, m_pending_zone_heading;
```

**GameState Updates Required:**
- WorldState already has zoneName, zoneId, zoneConnected
- Add: GameTime struct (hour, minute, day, month, year)
- Add: BindPoint struct (zoneId, x, y, z, heading)
- WorldState already has zone line state - verify completeness

**Packet Handlers to Update:**
- `HandleZoneEntry()` - Zone info
- `HandleTimeOfDay()` - Game time
- `HandleNewZone()` - Zone change

**EQ Methods to Update:**
- `GetZoneName()`, `GetZoneId()` → read from WorldState
- Zone line detection methods → use WorldState

---

## Sub-Phase 7.4: Entity State Migration ✅ COMPLETE

**Priority:** Medium-High

**Status:** COMPLETE

**Implementation Summary:**
- EntityManager already had comprehensive entity storage and query API
- Used gradual sync approach (Option C) to minimize risk - keep local m_entities, sync to EntityManager
- Created conversion helpers to map between Entity struct types (EQ uses snake_case, EntityManager uses camelCase)
- Added helper functions:
  - `SyncEntityToGameState(const Entity& entity)` - converts and syncs entity to EntityManager
  - `RemoveEntityFromGameState(uint16_t spawnId)` - removes entity from EntityManager
- Updated m_my_spawn_id setters to also update `m_game_state.player().setSpawnId()` (3 locations)
- Updated entity add operations to call `SyncEntityToGameState()` (3 locations):
  - Self entity add during zone entry
  - Zone spawns bulk add
  - New spawn individual add
- Updated entity remove to call `RemoveEntityFromGameState()`
- Updated entity clear (zone cleanup) to call `m_game_state.entities().clear()`

**Files Modified:**
- `include/client/eq.h` - Added entity sync helper declarations
- `src/client/eq.cpp` - Added helper implementations and sync calls in entity operations

**State to Migrate:**
```cpp
// Entity Tracking
std::map<uint16_t, Entity> m_entities;
uint16_t m_my_spawn_id;

// World Objects
std::map<uint32_t, eqt::WorldObject> m_world_objects;

// Door State
std::map<uint8_t, Door> m_doors;
std::set<uint8_t> m_pending_door_clicks;
```

**GameState Updates Required:**
- EntityManager already has entity storage
- Need to ensure Entity struct matches between EQ and EntityManager
- WorldState or separate WorldObjectManager for world objects
- DoorState already exists

**Migration Approach:**
- This is complex because many systems access m_entities directly
- Option A: Make EQ class use EntityManager reference
- Option B: Share the same map instance
- Option C: Gradual migration with deprecation warnings ← **CHOSEN**

**Packet Handlers to Update:**
- `HandleSpawn()` - Entity creation
- `HandleDespawn()` - Entity removal
- `HandlePositionUpdate()` - Entity movement
- `HandleHPUpdate()` - Entity HP
- `HandleSpawnAppearance()` - Entity appearance changes
- `HandleDoorSpawns()` - Door creation
- `HandleDoorAction()` - Door state changes
- `HandleGroundSpawn()` - World object creation

---

## Sub-Phase 7.5: Group State Migration ✅ COMPLETE

**Priority:** Medium

**Status:** COMPLETE

**Implementation Summary:**
- GroupState already had all required fields (inGroup, isLeader, leaderName, members array, memberCount, pendingInvite)
- Updated 7 accessors in EQ class to read from `m_game_state.group()`:
  - `IsInGroup()` → reads from GroupState
  - `IsGroupLeader()` → reads from GroupState
  - `GetGroupMemberCount()` → reads from GroupState
  - `GetGroupLeaderName()` → reads from GroupState
  - `HasPendingGroupInvite()` → reads from GroupState
  - `GetPendingInviterName()` → reads from GroupState
- Added `SyncGroupMemberToGameState()` helper to convert between EQ::GroupMember (snake_case) and eqt::state::GroupMember (camelCase)
- Updated packet handlers to sync to both local members and GameState:
  - `ClearGroup()` - calls `m_game_state.group().clearGroup()`
  - `ZoneProcessGroupInvite()` - syncs pending invite
  - `ZoneProcessGroupUpdate()` - syncs all group state (status, leader, members) for JOIN/UPDATE/LEAVE/MAKE_LEADER actions
  - `ZoneProcessGroupDisband()` - already uses ClearGroup() which syncs
  - `ZoneProcessGroupCancelInvite()` - syncs pending invite clear

**Files Modified:**
- `include/client/eq.h` - Changed 7 inline accessors to declarations, added SyncGroupMemberToGameState declaration
- `src/client/eq.cpp` - Added accessor implementations and sync calls in 5 packet handler locations

**State to Migrate:**
```cpp
bool m_in_group;
bool m_is_group_leader;
std::string m_group_leader_name;
std::array<GroupMember, 6> m_group_members;
int m_group_member_count;

// Invitations
bool m_has_pending_invite;
std::string m_pending_inviter_name;
```

**GameState Updates Required:**
- GroupState already exists with similar structure
- Verify member struct compatibility
- Add: pending invite state if not present

**Packet Handlers to Update:**
- `HandleGroupUpdate()` - Group membership
- `HandleGroupInvite()` - Invitations
- `HandleGroupDisband()` - Leaving group
- `HandleGroupLeaderChange()` - Leader change

---

## Sub-Phase 7.6: Combat State Migration ✅ COMPLETE

**Priority:** Medium

**Status:** COMPLETE

**Implementation Summary:**
- CombatState already had all required fields (combatTarget, combatStopDistance, inCombatMovement, lastCombatMovementUpdate, lastSlainEntityName)
- Changed `SetCombatStopDistance()` from inline to declaration, now syncs to GameState
- Updated combat movement methods to sync to GameState:
  - `StartCombatMovement(string, float)` - syncs all combat movement state
  - `StartCombatMovement(uint16_t)` - syncs combat target, stop distance, and movement flag
  - `UpdateCombatMovement()` - syncs when combat movement ends (target lost)
- Updated death handler to sync `m_last_slain_entity_name` to GameState
- Updated `CleanupZone()` to sync combat state reset (clears combat target and movement flag)

**Files Modified:**
- `include/client/eq.h` - Changed SetCombatStopDistance to declaration
- `src/client/eq.cpp` - Added SetCombatStopDistance implementation and sync calls in 5 locations

**Note:** Combat state members are accessed directly within the EQ class (internal implementation), so no public getters were changed. Only the mutators needed GameState sync.

**State to Migrate:**
```cpp
// Combat Movement
std::string m_combat_target;
float m_combat_stop_distance;
bool m_in_combat_movement;

// Last Action
std::string m_last_slain_entity_name;
```

**GameState Updates Required:**
- CombatState already exists
- Add: combatTarget name/ID
- Add: combatStopDistance
- Add: inCombatMovement flag
- Add: lastSlainEntityName (for messages)

**Packet Handlers to Update:**
- `HandleDeath()` - Entity death
- Combat action handlers

---

## Sub-Phase 7.7: Loot State Migration ✅ COMPLETE

**Priority:** Low-Medium

**Status:** COMPLETE

**Implementation Summary:**
- PlayerState already had `lootingCorpseId` with `setLootingCorpse()` and `clearLootingCorpse()` methods
- Updated `m_player_looting_corpse_id` assignments to sync to GameState:
  - `RequestLootCorpse()` - calls `m_game_state.player().setLootingCorpse(corpseId)`
  - `CloseLootWindow()` - calls `m_game_state.player().clearLootingCorpse()`
  - Entity despawn handler - calls `m_game_state.player().clearLootingCorpse()` when corpse deleted while looting
- Other loot state fields (`m_pending_loot_slots`, `m_loot_all_in_progress`, `m_loot_all_remaining_slots`, `m_loot_complete_corpse_id`) are internal implementation details for the loot-all feature and don't need GameState exposure

**Files Modified:**
- `src/client/eq.cpp` - Added sync calls in 3 locations

**Note:** The loot state members are private with no public accessors. Only `lootingCorpseId` needed to be synced as it's the externally visible state; the other fields are internal implementation details.

**State to Migrate:**
```cpp
uint16_t m_player_looting_corpse_id;
std::vector<int16_t> m_pending_loot_slots;
bool m_loot_all_in_progress;
std::vector<int16_t> m_loot_all_remaining_slots;
uint16_t m_loot_complete_corpse_id;
```

**GameState Updates Required:**
- Add LootState class or extend PlayerState
- Track: lootingCorpseId (already in PlayerState)
- Add: pendingLootSlots, lootAllInProgress, lootAllRemainingSlots

**Packet Handlers to Update:**
- `HandleLootResponse()` - Loot window open
- `HandleLootItem()` - Item looted
- `HandleLootComplete()` - Loot window close

---

## Sub-Phase 7.8: Behavioral Flags & Cleanup ✅ COMPLETE

**Priority:** Low

**Status:** COMPLETE

**Implementation Summary:**
- PlayerState already had all required behavioral flag fields (isSneaking, isAFK, isAnonymous, isRoleplay, isCamping)
- Changed 4 inline getters to declarations, now reading from GameState:
  - `IsCamping()` → reads from `m_game_state.player().isCamping()`
  - `IsAFK()` → reads from `m_game_state.player().isAFK()`
  - `IsAnonymous()` → reads from `m_game_state.player().isAnonymous()`
  - `IsRoleplay()` → reads from `m_game_state.player().isRoleplay()`
- Updated 7 setter methods to sync to GameState:
  - `SetAFK()` - syncs AFK state
  - `SetAnonymous()` - syncs anonymous state
  - `SetRoleplay()` - syncs roleplay state
  - `SetSneak()` - syncs sneaking state
  - `StartCampTimer()` - syncs camping state (true)
  - `CancelCamp()` - syncs camping state (false)
  - `UpdateCampTimer()` - syncs camping state on timer expiry
- Cleaned up `Application::syncGameStateFromClient()`:
  - Removed redundant polling for position, stats, and zone info (now synced directly from EQ packet handlers in Phases 7.1-7.3)
  - Kept sub-syncs for pet, NPC interaction, and spell states (not yet migrated to direct sync)

**Files Modified:**
- `include/client/eq.h` - Changed 4 inline getters to declarations
- `src/client/eq.cpp` - Added getter implementations and sync calls in 7 setter methods
- `src/client/application.cpp` - Removed redundant polling from syncGameStateFromClient

**Note:** Input state members (`m_move_forward`, `m_move_backward`, `m_turn_left`, `m_turn_right`) were left in EQ class as they are input handling state rather than game state. The sneaking state has no public getter in EQ class, only a setter which now syncs to GameState.

**State to Migrate:**
```cpp
// Behavioral Flags
bool m_is_sneaking;
bool m_is_afk;
bool m_is_anonymous;
bool m_is_roleplay;
bool m_is_camping;
std::chrono::steady_clock::time_point m_camp_start_time;

// Input State (may stay in EQ for now)
bool m_move_forward, m_move_backward;
bool m_turn_left, m_turn_right;
```

**Final Cleanup:**
- Remove Application::syncGameStateFromClient() and sub-methods
- Remove state tracking variables from Application
- Audit all remaining m_* members in EQ class
- Update documentation

---

## Implementation Order

Recommended order based on dependencies and risk:

1. **Sub-Phase 7.2** - Player Attributes & Currency (low risk, high value)
2. **Sub-Phase 7.3** - World & Zone State (low risk, isolated)
3. **Sub-Phase 7.1** - Movement State (medium risk, many dependencies)
4. **Sub-Phase 7.5** - Group State (low risk)
5. **Sub-Phase 7.6** - Combat State (low risk)
6. **Sub-Phase 7.7** - Loot State (low risk)
7. **Sub-Phase 7.4** - Entity State (high risk, many dependencies)
8. **Sub-Phase 7.8** - Cleanup (final)

---

## Testing Strategy

For each sub-phase:

1. **Unit Tests** - Test GameState classes directly
2. **Integration Tests** - Test packet handling updates state correctly
3. **Manual Testing** - Verify in-game behavior unchanged
4. **Regression Testing** - Run full test suite

**Key Scenarios to Test:**
- Zone entry and exit
- Combat (targeting, attacking, taking damage)
- Movement (walking, running, pathfinding)
- Spellcasting and cooldowns
- Grouping (invite, join, leave)
- Trading (vendor, bank, player trade)
- Pet commands
- Inventory operations

---

## Risk Mitigation

1. **Backward Compatibility** - Keep old accessors working during transition
2. **Feature Flags** - Allow switching between old/new code paths
3. **Incremental Changes** - Small, testable commits
4. **Rollback Plan** - Each sub-phase should be revertable

---

## Estimated Complexity

| Sub-Phase | Files Modified | Risk Level | Complexity |
|-----------|---------------|------------|------------|
| 7.1 Movement | 5-8 | Medium | High |
| 7.2 Attributes | 3-5 | Low | Medium |
| 7.3 World/Zone | 3-5 | Low | Low |
| 7.4 Entities | 10+ | High | Very High |
| 7.5 Group | 3-5 | Low | Low |
| 7.6 Combat | 3-5 | Low | Low |
| 7.7 Loot | 3-5 | Low | Medium |
| 7.8 Cleanup | 5-8 | Low | Medium |

---

## Notes

- The EQ class will still own network connections and packet handling
- Managers (SpellManager, CombatManager, etc.) may need GameState references
- Some state may be better left in EQ class (connection state, packet sequencing)
- Consider event-driven updates vs polling where appropriate
- Graphics-specific state (#ifdef EQT_HAS_GRAPHICS) needs special handling
