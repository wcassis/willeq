# S2 — Step-2 decoupling residue

Close the remaining game-state/renderer coupling items from the 2026-07-22
audit (tracked in refactor/deferred.md). Unlike Batch F these are NOT
move-only — they change signatures and delete dead code. Verification: build
+ full ctest green; behavior changes are called out explicitly below.
Executed directly by the orchestrator (judgment-heavy, small diff).

## Items

1. **EntityPoseState → state layer.** New `include/client/state/entity_pose_state.h`
   defining `eqt::state::EntityPoseState`. IrrlichtRenderer keeps a
   `using EntityPoseState = eqt::state::EntityPoseState;` alias so renderer
   call sites are unchanged. Game-state uses (eq_entities.cpp, eq_bridge.cpp)
   switch to the state enum.
2. **Dual movement channel → intent-only.** Delete
   `setMovementCallback`/`movementCallback_` and the InitGraphics lambda.
   renderer_player_physics.cpp builds the `PlayerPositionChanged` intent
   directly (gate on `bridge_`). **Behavior fix:** today OnGraphicsMovement
   runs TWICE per update — once synchronously on the render thread (a
   thread-safety violation: game-state mutation + network send off-thread)
   and once via the queued intent, double-sending position packets. After:
   once, on the game thread.
3. **Dead inventory pointer.** Delete `inventoryManager_` member; rename the
   misnamed `setInventoryManager(InventoryManager*)` (which actually does
   one-time deferred graphics init) to `initDeferredGraphics()` with no
   parameter. No game-thread object pointer crosses to the renderer.
4. **`PlayerPositionUpdate` out of eq.h.** `OnGraphicsMovement` takes
   `eqt::events::PlayerPositionChanged` (the intent struct) directly.
5. **Typed bridge payloads.** `void* map` → `HCMap*` and `void* navmesh` →
   typed pointer in event_bus.h (forward declarations); remove consumer
   casts in irrlicht_bridge.cpp.
6. **eq.cpp include prune.** Remove graphics includes eq.cpp no longer needs
   post-F10 (compile-driven).

## Explicitly deferred (documented, not done here)

- `InitGraphics(IrrlichtRenderer*, ...)` keeps the renderer parameter: it
  legitimately drives `initLoadingScreen(config)` + `initDeferredGraphics()`.
  Full removal requires Application-side init restructuring — a separate
  design decision.
- Asset-loader namespace relocation (`WldLoader`, `PfsArchive`, `BspTree`,
  … out of `EQT::Graphics`) — larger reorganization, own unit.
- `EverQuest::GetDebugLevel()` static call from door_manager.cpp — trivial,
  harmless, low value.
- `output/graphical_renderer.h` legacy interface (has its own
  movement-callback machinery; IrrlichtRenderer does not inherit from it) —
  candidate for deletion in a dead-code pass, not touched here.

## Review

(filled in on completion)
