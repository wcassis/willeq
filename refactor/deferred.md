# Deferred / Known-Open Items

Live list of open work items extracted from "done" batches and audits
(2026-07-22 doc reconciliation). When an item is fixed, delete it here and note
the commit. Sources are historical docs — do not edit those.

## Decoupling residue (from 2026-07-22 coupling audit of fable @ a0c02cd)

- **`EntityPoseState` enum owned by renderer, read by game state** — 9 uses in
  eq.cpp (~5781–5806, ~18255–18271) cast a renderer-owned enum
  (irrlicht_renderer.h:702) to uint8_t for `EntityPoseStateChanged` events.
  Move the enum to the state layer.
- **eq.cpp has 14 graphics includes** (eq.cpp:24–46), incl. irrlicht_renderer.h,
  loading_thread.h, weather_effects_controller.h, detail_manager.h, wld_loader.h.
  Most are asset/config loaders (`WldLoader`, `PfsArchive`, `BspTree`,
  `WeatherConfigLoader`, `SpellEffectsConfig`) that belong in a shared asset
  layer, not `EQT::Graphics`.
- **`InitGraphics(int, int, IrrlichtRenderer*, GameStateBridge*)`**
  (eq.cpp:17936) — game state takes a raw renderer pointer.
- **Two `void*` payloads cross the bridge**: event_bus.h:713 (`void* map` →
  HCMap*, cast at irrlicht_bridge.cpp:610) and event_bus.h:836
  (`void* navmesh` → PathfinderNavmesh*). Replace with typed
  ownership-transferring handoffs.
- **Dead pointer:** `setInventoryManager()` / `inventoryManager_`
  (irrlicht_renderer.cpp:14781, eq.cpp:17999) — assigned, never read. Delete.
- **Dual movement channel:** both `PlayerPositionChanged` intent
  (eq.cpp:18537) and a still-live `renderer->setMovementCallback(...)`
  (eq.cpp:18011) despite D20a claiming callbacks were removed. Collapse to
  intent-only.
- Renderer reads spell static data directly (spell_visual_fx.cpp:194,282 via
  spell_database.h) and item DB (equipment_model_loader.cpp:73) — acceptable
  (static data), but note if a shared asset layer is created.
- `EverQuest::GetDebugLevel()` static call from door_manager.cpp:955.

## From Batch U / ui_fixes.md

- **Input-lag fix (original Chunk 1) deferred** — application-loop 1 ms sleep +
  60 fps input gating (application.cpp ~438/462) causes 17–20 ms input latency.
  Was waiting on D21b; D21b is done, so this is now unblocked.
- Target casting bar not yet in new UI (irrlicht_bridge.cpp:748).

## From arch_design_update.md / audit_results_post.md

- **Unified priority queue only partially implemented** — `BackgroundWorkQueue`
  instances (zone load, deferred work, BSP preload) remain strict FIFO;
  mesh/texture rebuild queues sort by distance only, not full `WorkPriorityKey`.
- **Door/object placeholder cube** — only asset type without a placeholder
  (arch_design_update.md §7 TODO).
- **Entity/equipment texture failure path** allocates GPU texture before budget
  check (post-audit §3.4, High).
- **GL calls on render thread as fallback** when GPU upload thread unavailable
  (texture_atlas.cpp:187,357) — desktop GL always takes this path (§7.2, High).
- **Unbudgeted `driver_->addTexture()` calls** — sky, animated textures,
  animated trees, zone geometry fallback, software cursor (§3.5, Medium).
- **Budget caches not thread-safe** — ConstrainedTextureCache /
  ConstrainedMeshCache have no mutex; blocks future multi-worker expansion (§3.6).
- `loadDisplaySettingsFromFile()` on render thread (irrlicht_renderer.cpp:3454).
- Dead code in zone_lines.cpp — three `#if 0` blocks (~130 lines) +
  `hasBspZoneLines()` (always false), `debugTestCoordinateMappings()`.
- RDP peer threads not joined until `stop()` (§1.3).

## From Batch Z

- Z5.3 empty-draw measurement (1-triangle buffer isolation) — TODO.

## Tracker hygiene

- Batch E table in progress.md has empty commit columns (work is committed;
  hashes were never recorded).
