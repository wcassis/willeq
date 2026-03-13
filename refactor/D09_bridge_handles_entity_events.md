# D09: IrrlichtBridge Handles Entity Events

## Plan

### Overview

Wire the IrrlichtBridge to actually call IrrlichtRenderer methods when entity events
are received. This is the first Phase 3 unit — it establishes the pattern for the
bridge consuming events and translating them into renderer calls.

### Prerequisites

1. IrrlichtBridge needs a pointer to IrrlichtRenderer to make calls
2. The bridge needs to be instantiated and wired up somewhere
3. The renderer needs to call drainEvents() + applyEvent() in its frame loop

However, for D09 we focus ONLY on filling in the applyEvent() stubs with actual
renderer calls. The bridge wiring (instantiation, setBridge(), drainEvents() in
render loop) is infrastructure that will be needed but can be deferred until all
Phase 3 stubs are filled in. The stubs currently log at TRACE level — we replace
them with actual renderer calls that will execute once the bridge is wired.

### Steps

1. Add IrrlichtRenderer pointer to IrrlichtBridge:
   - Add `IrrlichtRenderer* renderer_ = nullptr;` member
   - Add `void setRenderer(IrrlichtRenderer* r) { renderer_ = r; }`
   - Forward declare IrrlichtRenderer (avoid circular include)

2. Implement EntitySpawned handler:
   - Extract EntitySpawnedData from event
   - Call `renderer_->createEntity(...)` with spawn data
   - Guard with `if (!renderer_) return;`

3. Implement EntityDespawned handler:
   - Call `renderer_->removeEntity(spawnId)`

4. Implement EntityMoved handler:
   - Call `renderer_->updateEntity(...)` with position/velocity/animation

5. Implement EntityStatsChanged handler:
   - No direct renderer call for entity stats — keep as log-only stub
   - (HP bars are drawn from entity data, not a separate renderer call)

6. Implement EntityAppearanceChanged handler:
   - Call `renderer_->updateEntityAppearance(...)` with race/gender/appearance

7. Implement EntityLightChanged handler:
   - Call `renderer_->setEntityLight(spawnId, lightLevel)`

8. Implement EntityAnimationEvent handler:
   - Call `renderer_->setEntityAnimation(spawnId, animCode, loop, playThrough)`
   - Note: animCode in event is uint8_t, renderer expects string — need conversion

9. Implement EntityPoseStateChanged handler:
   - Call `renderer_->setEntityPoseState(spawnId, pose)`

10. Implement EntityDeathAnimation handler:
    - Call `renderer_->playEntityDeathAnimation(spawnId)`

11. Implement CorpseDecayStarted handler:
    - Call `renderer_->startCorpseDecay(spawnId)`

12. Implement CombatAnimation handler:
    - Call `renderer_->queueCombatAnimation(...)` with combat data

13. Implement PlayerMoved handler:
    - Call `renderer_->setPlayerPosition(x, y, z, heading)`

14. Implement PlayerStatsChanged handler:
    - No direct renderer call — stats are read from GameState
    - Keep as log-only stub

15. Build and verify compilation

## Acceptance Criteria

- All entity event stubs in applyEvent() make actual renderer calls
- Events that have no corresponding renderer method remain as log stubs
- All renderer calls guarded by `if (!renderer_) return;`
- No new warnings or errors in build
- IrrlichtBridge includes forward declaration, not full renderer header

## Review

All steps completed. Additional work beyond original plan:

**Event data struct fixes** (prerequisite for bridge consumption):
- EntityAnimationEventData: added `std::string animName` field. All 4 push sites
  in eq.cpp updated to populate animName from the local animCode/damageAnim string.
  The uint8_t animCode field retained for backward compatibility.
- EntityAppearanceChangedData: replaced generic appearanceType/Value with full
  appearance fields (face, hair, beard, texture, helm, equipment[9], equipmentTint[9],
  isPlayer flag). All 3 push sites updated to copy full appearance data. Bridge
  consumer can now reconstruct the EntityAppearance struct and call both
  updateEntityAppearance() and updatePlayerAppearance() when isPlayer is set.

**Bridge implementation:**
- IrrlichtBridge header: added forward declaration of EQT::Graphics::IrrlichtRenderer,
  renderer_ member pointer, setRenderer() method.
- IrrlichtBridge cpp: includes irrlicht_renderer.h. Entity event cases (12 types)
  now extract event data via std::get and call corresponding renderer methods.
- PlayerMoved calls setPlayerPosition(). EntityStatsChanged and PlayerStatsChanged
  remain as log stubs (no direct renderer call — stats read from GameState).
- All renderer calls guarded by `if (renderer_)`.
- Non-entity stubs retained with D10-D13 unit labels in comments.

Build succeeds, 73 relevant tests pass. All acceptance criteria met.
