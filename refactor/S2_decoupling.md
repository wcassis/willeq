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

All six items completed; one audit claim corrected.

1. **EntityPoseState** — new `include/client/state/entity_pose_state.h`
   (`eqt::state::EntityPoseState`). irrlicht_renderer.h aliases it
   (`using EntityPoseState = eqt::state::EntityPoseState;`) so all renderer
   call sites compile unchanged. eq_entities.cpp's 5 uses and eq_bridge.cpp's
   4 uses switched to the state enum. **eq_entities.cpp's
   irrlicht_renderer.h include is gone — the enum was its only graphics
   reference.**
2. **Movement channel** — `setMovementCallback`/`movementCallback_` deleted
   from irrlicht_renderer.h; the InitGraphics lambda deleted;
   renderer_player_physics.cpp builds the `PlayerPositionChanged` intent
   directly, gated on `bridge_`. Behavior fix as planned: OnGraphicsMovement
   now runs exactly once per update, on the game thread (previously twice —
   once synchronously on the render thread, a thread-safety violation, plus
   doubled SendPositionUpdate traffic).
3. **Inventory pointer — audit claim corrected.** `inventoryManager_` was
   NOT dead: IrrlichtBridge's InventorySlotChanged handler (render thread)
   read the game-thread InventoryManager through it — a live cross-thread
   shared-state leak, worse than the audit's "dead pointer" description.
   Fix: `InventorySlotChangedData` now carries the display fields
   (itemName/iconId/quantity), populated at the three publish sites on the
   game thread (eq_merchant.cpp move/delete handlers, eq.cpp snapshot loop);
   the bridge handler consumes event data only. `inventoryManager_` member
   deleted; `setInventoryManager(InventoryManager*)` renamed to
   `initDeferredGraphics()` (no parameter — it was always one-time deferred
   init: render distance + SpellVisualFX creation).
4. **PlayerPositionUpdate** — OnGraphicsMovement takes
   `eqt::events::PlayerPositionChanged`; the graphics struct became fully
   unused and was deleted from irrlicht_renderer.h. eq.h no longer
   forward-declares it. (The `PlayerPositionUpdate{Client,Server}_Struct`
   wire structs in packet_structs.h are unrelated and untouched.)
5. **Typed payloads** — `CollisionMapChangedData.map` is `HCMap*`,
   `NavmeshChangedData.navmesh` is `PathfinderNavmesh*` (forward-declared in
   event_bus.h; non-owning handoffs, ownership unchanged). The
   `static_cast<HCMap*>` in irrlicht_bridge.cpp removed; navmesh had no
   remaining consumer cast.
6. **eq.cpp include prune** — 10 of 12 graphics includes removed
   (irrlicht_renderer.h, weather_effects_controller.h,
   weather_config_loader.h, weather_quality_preset.h, wld_loader.h,
   detail_manager.h, detail_types.h, seasonal_controller.h,
   spell_effects_config.h, constrained_renderer_config.h — the last comes
   in via eq.h as documented). Compile-driven: only loading_thread.h
   (LoadingStatus) and ui/command_registry.h (CommandRegistry unique_ptr)
   are genuinely needed, now under EQT_HAS_GRAPHICS.

**Verification:** full build clean; ctest 1,191/1,192 (sole failure =
documented ALSA expected-fail). Not move-only by design — reviewed as a
behavior-affecting change; the two intentional behavior changes are items
2 (single movement dispatch) and 3 (inventory UI state sourced from event
payloads instead of live cross-thread reads).

**Deferred items** listed in the plan header remain deferred; deferred.md
updated to match.
