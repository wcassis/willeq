# D04: Publish Entity Events Alongside Existing Calls

## Plan

### Overview

Add event publishing alongside existing direct `m_renderer->` calls for entity events.
Both paths run simultaneously — the bridge receives events but doesn't act on them yet
(Phase 3 will wire up consumption). This is the first dual-path unit.

### Prerequisites

- Add `GameStateBridge*` member to EverQuest class (first unit to need it)
- Null-safe: all push calls guarded by `if (m_bridge)`

### Steps

1. Add bridge member to EverQuest class (`include/client/eq.h`):
   - Forward declare `eqt::bridge::GameStateBridge` (outside `#ifdef EQT_HAS_GRAPHICS`)
   - Add `eqt::bridge::GameStateBridge* m_bridge = nullptr;` private member
   - Add `void setBridge(eqt::bridge::GameStateBridge* bridge) { m_bridge = bridge; }`

2. Add `#include "client/state/event_bus.h"` to eq.cpp (for event data structs)

3. Publish EntitySpawned event in `OnSpawnAddedGraphics()` (line ~19370):
   - After `m_renderer->registerEntity(...)`, push EntitySpawned event
   - Include: spawnId, name, x/y/z, heading, raceId, classId, level, gender, npcType, isCorpse

4. Publish EntityDespawned event in `OnSpawnRemovedGraphics()` (line ~19529):
   - After `m_renderer->removeEntity()` or `startCorpseDecay()`, push EntityDespawned
   - Also push CorpseDecayStarted for the corpse decay path

5. Publish EntityMoved event in `OnSpawnMovedGraphics()` (line ~19547):
   - After `m_renderer->updateEntity(...)`, push EntityMoved event
   - Include: spawnId, x/y/z, heading, dx/dy/dz, animation

6. Publish EntityAppearanceChanged in wear change handlers (lines ~12193, ~12274, ~12350):
   - After each `m_renderer->updateEntityAppearance(...)`, push EntityAppearanceChanged
   - Use SpawnAppearanceType value 0 (general appearance) since full appearance data

7. Publish EntityLightChanged in `AT_LIGHT` handler (line ~5962):
   - After `m_renderer->setEntityLight(...)`, push EntityLightChanged
   - Also publish in `OnSpawnAddedGraphics()` when light > 0

8. Publish EntityAnimationEvent in animation handlers:
   - After `m_renderer->setEntityAnimation(...)` at lines ~5817, ~6230, ~17747, ~19404
   - Include: spawnId, animCode (as uint8_t anim_id), loop, playThrough

9. Publish EntityPoseStateChanged in pose handlers:
   - After `m_renderer->setEntityPoseState(...)` at lines ~5815, ~19403
   - Include: spawnId, poseState

10. Publish EntityDeathAnimation in death handlers:
    - After `m_renderer->playEntityDeathAnimation(...)` at lines ~5830, ~13032
    - Include: spawnId

11. Publish CombatAnimation in combat handler:
    - After `m_renderer->queueCombatAnimation(...)` at line ~17726
    - Include: sourceId, targetId, damageType, damageAmount, damagePercent

12. Skip zone-loading-time calls (LoadZoneGraphicsOnLoadingThread, LoadZoneGraphics):
    - These are bulk operations on the loading thread
    - Zone loading is handled by the renderer itself, not through the bridge

13. Build and verify compilation

## Acceptance Criteria

- All existing direct renderer calls remain unchanged (no behavioral change)
- Bridge receives events for all runtime entity operations
- EverQuest can run with no bridge attached (null check on m_bridge)
- No new warnings or errors in build
- Existing tests pass unchanged
- No renderer includes added to event_bus.h
- Zone-loading-time entity registrations do NOT publish events

## Review

All steps completed as planned. Minor deviation: step 2 uses
`#include "client/bridge/game_state_bridge.h"` instead of `event_bus.h` since
event_bus.h is already included transitively through game_state.h, and the bridge
header provides the `pushEvent()` method needed.

Call sites covered:
- OnSpawnAddedGraphics: EntitySpawned, EntityLightChanged, EntityPoseStateChanged, EntityAnimationEvent
- OnSpawnRemovedGraphics: EntityDespawned, CorpseDecayStarted
- OnSpawnMovedGraphics: EntityMoved
- HandleWearChange (2 sites) + UpdatePlayerAppearanceFromInventory + ZoneProcessIllusion: EntityAppearanceChanged (3 sites)
- SetSpawnAppearance AT_LIGHT: EntityLightChanged
- SetSpawnAppearance AT_ANIM_SPEED: EntityPoseStateChanged, EntityAnimationEvent
- SetSpawnAppearance AT_DIE: EntityDeathAnimation
- ZoneProcessEmote: EntityAnimationEvent
- ApplyDamage (melee): CombatAnimation
- ApplyDamage (non-melee): EntityAnimationEvent
- Kill handler: EntityDeathAnimation

Zone-loading bulk registration (LoadZoneGraphicsOnLoadingThread, LoadZoneGraphics)
correctly skipped — no bridge events published there.

Build succeeds, 72 relevant tests pass. All acceptance criteria met.
