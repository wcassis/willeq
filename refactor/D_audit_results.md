# D01-D09 Audit: Missing Bridge Coverage

Full audit of all direct renderer calls vs bridge event data completeness.
Fixes should be applied before continuing with D10+.

---

## Critical — Entity Spawn & Player Identity

### ~~1. EntitySpawnedData missing appearance, serverSize, isPlayer~~ DONE
- [x] Update struct in event_bus.h — added `isPlayer`, `serverSize`, full appearance fields
- [x] Update push site in eq.cpp — populates all fields from entity data
- [x] Update bridge consumer in irrlicht_bridge.cpp — builds EntityAppearance, calls `registerEntity` (not `createEntity`), passes `isPlayer`, `serverSize`, `level`, correct `isNPC` logic

### ~~2. Player entity creation has no bridge events~~ DONE
- [x] Push `EntitySpawned` with `isPlayer=true` at player creation site (~line 7295)
- [x] Bridge consumer calls `setPlayerSpawnId()` + `updatePlayerAppearance()` when `isPlayer` is true
- No separate `PlayerSpawnIdSet` event needed — folded into EntitySpawned

### ~~3. setPlayerSpawnId never bridged~~ DONE (folded into issue 2)
- [x] Runtime call site covered by EntitySpawned with `isPlayer=true`
- Remaining 4 call sites (lines 5793, 6899, 19930, 20041) are zone-loading bulk registration — excluded per convention

### ~~4. PlayerMoved never pushed~~ DONE
- [x] 6 runtime push sites added: PlayerProfile zone-in, two Z-correction sites, /goto warp, console-driven movement, server entity moved (player)
- Zone-loading bulk sites (LoadZoneGraphicsOnLoadingThread, LoadZoneGraphicsDirect) excluded per convention

---

## High — Blocks Future D-Units

### ~~5. DoorSpawnedData missing incline, size, opentype~~ DONE
- [x] Replaced `state` (uint8_t) with `incline` (uint32_t), `size` (uint16_t), `opentype` (uint8_t), `isOpen` (bool)
- [x] Updated push site in eq.cpp
- [x] Updated push site in game_state.cpp (EventBus callback)

### ~~6. DoorStateChangedData missing userInitiated~~ DONE
- [x] Added `userInitiated` (bool, default false) to struct
- [x] Updated push site in eq.cpp to pass `user_initiated`
- game_state.cpp EventBus callback defaults to false (no user context available there)

### ~~7. TargetChanged uses wrong data type, missing target info~~ DONE
- [x] Created `TargetChangedData` struct with full target info (spawnId, name, level, hpPercent, raceId, gender, classId, bodyType, npcType, helm, showHelm, texture, equipment[9], equipmentTint[9])
- [x] Added to EventData variant
- [x] Updated target-selected push site — populates all fields from entity
- [x] Updated target-cleared push site — default-constructed (spawnId=0 = cleared)

### 8. Target state owned by renderer — must move to game state
The renderer currently owns `currentTargetId_` / `currentTargetName_` and game logic
queries `m_renderer->getCurrentTargetId()` to make decisions. This is backwards — target
selection is server-authoritative game state. The renderer should be a pure consumer.

**~~8a. Add `m_current_target_id` to EverQuest~~ DONE**
- [x] Added `m_current_target_id` member, `GetCurrentTargetId()`, `SetCurrentTargetId()` to eq.h
- [x] Set on target selection in eq.cpp (server-confirmed path)
- [x] Set/cleared in `updateRendererTargetInfo()` (single funnel for all action handler target changes — covers SetTarget, CycleTargets, ClearTarget, assist, group target)
- [x] Cleared on entity despawn in `OnSpawnRemovedGraphics()`

**~~8b. Replace all `m_renderer->getCurrentTargetId()` queries with `m_current_target_id`~~ DONE**
- [x] eq.cpp HP update check — now uses `m_current_target_id == spawn_id`
- [x] eq.cpp periodic target HP refresh — now uses `m_current_target_id`
- [x] eq.cpp despawn target clear — already changed in 8a

**~~8c. Bridge the HP update for current target~~ DONE**
- [x] HP update site pushes `TargetChanged` with full entity data when `m_current_target_id == spawn_id`
- [x] Zone-loading periodic refresh excluded per convention

**~~8d. Renderer-internal target clears must route through game state~~ DONE**
- [x] Renderer ESC/ClearTarget → `bridgeQueue_` → `InputActionBridge` → `EqActionHandler::clearTarget()` → `updateRendererTargetInfo(0)` → `SetCurrentTargetId(0)`. Already works via 8a fix.
- [x] `CombatManager::SetTarget()` now calls `m_eq->SetCurrentTargetId(entity_id)`. Covers auto-hunting consider cycle (line 1422) which targets entities and sends packets to the server.
- [x] `CombatManager::ClearTarget()` now calls `m_eq->SetCurrentTargetId(0)`. Covers target death (line 1153) and consider cycle cleanup (line 1433) — both send clear packets to the server and are real target changes, not temporary bookkeeping.
- [x] Renderer's `currentTargetId_` is now a display-only cache. All game logic queries use `m_current_target_id`.

---

## Medium — Behavioral Differences

### ~~9. TradeCancelled/TradeCompleted use wrong carrier type~~ DONE
- [x] Added `TradeCancelledData` and `TradeCompletedData` (empty structs) to event_bus.h
- [x] Added to EventData variant
- [x] Updated all 3 push sites (2 TradeCancelled, 1 TradeCompleted)

### ~~10. setEntityWeaponSkills has no event~~ DONE
- [x] Added `EntityWeaponSkillsChanged` event type + `EntityWeaponSkillsChangedData` struct (spawnId, primaryWeaponSkill, secondaryWeaponSkill)
- [x] Added to EventData variant
- [x] Added push site alongside direct renderer call

### ~~11. Bridge calls createEntity vs direct path calls registerEntity~~ DONE
- **Direct path**: `registerEntity()` (deferred multi-frame pipeline)
- **Bridge consumer**: `createEntity()` (immediate build)
- **Fix**: Bridge consumer should call `registerEntity()` to match production behavior.
- [x] Update irrlicht_bridge.cpp EntitySpawned handler — fixed as part of issue 1

### ~~12. Event types defined but never pushed~~ DONE
Audit originally listed 13 types with "zero push sites in eq.cpp" — 6 were already pushed from state manager files (not eq.cpp).
- [x] `PlayerPositionStateChanged` — added push site in `SetPositionState()`, added `PlayerPositionStateChangedData` struct
- [x] `PlayerMovementModeChanged` — added push site in `SetMovementMode()`, added `PlayerMovementModeChangedData` struct
- [x] `ZoneLoaded` — added push sites in `OnGraphicsComplete()` and `OnGameStateComplete()`, added `ZoneLoadedData` struct
- [x] `PetWindowOpened` — removed (no pet window implementation exists)
- [x] `PetWindowClosed` — removed (no pet window implementation exists)
- [x] `PetStatsChanged` — already pushed from `pet_state.cpp`
- [x] `CursorItemChanged` — already pushed from `inventory_state.cpp`
- [x] `SpellGemChanged` — already pushed from `spell_state.cpp`
- [x] `CastingStateChanged` — already pushed from `spell_state.cpp`
- [x] `SpellMemorizing` — added `fireSpellMemorizingEvent()` in `spell_state.cpp`, called from `setMemorizing()`
- [x] `SkillsRefreshed` — added push site after `updateAllSkills()` in PlayerProfile processing
- [x] `TradeskillContainerOpened` — already pushed from `tradeskill_state.cpp`
- [x] `TradeskillContainerClosed` — already pushed from `tradeskill_state.cpp`

---

## Low — Data Truncation & Unpopulated Fields

### ~~13. EntityMovedData::animation truncated~~ DONE
- **Push site**: `static_cast<uint8_t>(animation)` from `int32_t`
- **Fix**: Change `EntityMovedData::animation` to `int32_t`.
- [x] Update struct in event_bus.h — changed `uint8_t animation` to `int32_t animation`

### ~~14. Unpopulated struct fields~~ DONE
- [x] `BuffUpdatedData::casterName` — populated by looking up `buff.player_id` in `m_entities` at push site
- [x] `SpellCastStartedData::targetId` — removed field. BeginCast packet has no target; target is only known when the spell lands (Action_Struct)
- [x] `EquipmentStatsChangedData::ac/atk` — populated from `equipStats.ac`/`equipStats.atk` (calculated by `calculateEquipmentStats()`). Also fixed direct renderer call which passed `0, 0`

### ~~15. Unbridged cleanup/utility renderer calls~~ DONE — no action needed
All calls are zone-loading bulk operations (excluded per convention) or renderer-internal debug concerns:
- [x] `setCollisionMap(nullptr)` — zone cleanup, clearing renderer reference before destroying zone map
- [x] `setCameraMode()` — zone loading initial camera setup in LoadZoneGraphicsDirect/OnLoadingThread
- [x] `setNavmesh()` — zone cleanup (nullptr) and zone load (set navmesh). All in zone load/cleanup paths
- [x] `setZoneLineDebug()` — renderer-internal debug visualization overlay, not game state
- [x] `setZoneLineBoundingBoxes()` — zone loading, passing zone line geometry to renderer
