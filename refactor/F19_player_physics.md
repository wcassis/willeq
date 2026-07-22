# F19 — Split player movement/collision and raycasting out of irrlicht_renderer.cpp

Move the player-physics clusters out of
`src/client/graphics/irrlicht_renderer.cpp` (12,340 lines). Pure move — no
behavior change, no interface change, no reformatting.

## Scope (orig refs @ a0c02cd; heavily shifted by F14–F16/F21. Enumerate by NAME.)

Move to new `src/client/graphics/renderer_player_physics.cpp`:
1. **Player movement/collision cluster** (orig 11835–12991):
   `updatePlayerMovement` (~828 lines), `checkMovementCollision`,
   `findGroundZ`, collision-setup methods in that contiguous region.
2. **Collision raycasting subsystem** (orig 13447–14053; F15's review lists
   these left-behind): `checkCollisionIrrlicht`, `findGroundZIrrlicht`,
   `extractWorldTriangles`, `rayIntersectTriangles`,
   `rayTestDoorsAndObjects`, `checkCollisionBspFiltered`,
   `findGroundZBspFiltered`, `addCollisionDebugLine`,
   `drawCollisionDebugLines`, `clearCollisionDebugLines`.

In-range non-physics methods (judgment): leave + document.

## Steps

1. Locate both clusters by grep (watch return-type regex traps); produce the
   mapping in the review BEFORE moving.
2. Replicate namespace wrapper + include patterns (F14–F16/F21 reviews).
3. Move verbatim; preserve #ifdef guards exactly.
4. File-local statics: usual rule.
5. Add to `GRAPHICS_SOURCES` in CMakeLists.txt.

## Verification

- `cmake --build build -j24` clean.
- `ctest --test-dir build` — green modulo documented exceptions in
  refactor/test_baseline.txt.
- `python3 scripts/refactor/verify_move_only.py BASE..HEAD` — PASS or
  documented deviations only.
- `bash scripts/refactor/check_file_sizes.sh --files <abs>/renderer_player_physics.cpp`
  — ~1,800 expected: WARN accepted, must not FAIL.
- irrlicht_renderer.cpp shrinks by ~1,700+ lines.

## Review

**Cluster boundaries (verified by grep of `IrrlichtRenderer::` definitions,
irrlicht_renderer.cpp @ branch-point commit `8b29308` (merge-base with
`fable`), 12,340 lines before this move — heavily shifted from the plan's
`orig refs @ a0c02cd` by F14–F16/F21):**

- **Player movement/collision cluster** (pre-move lines 10149–11274, a single
  contiguous block, no `#ifdef` guards). 9 `IrrlichtRenderer::` methods:
  - `updatePlayerMovement(float)` (~828 lines) — reads hotkey/mouse input,
    computes movement vector, dispatches to Irrlicht-BSP or HCMap collision
    paths, updates camera/entity position, throttles server position-update
    callback and bridge intent push, drives movement animation selection.
  - `checkMovementCollision(float,float,float,float,float,float)` — HCMap
    line-of-sight check with collision-debug-line visualization.
  - `findGroundZ(float,float,float)` — HCMap ground raycast (near-phase +
    full-phase) with boat-deck check and collision-debug-line visualization.
  - `setupMinimalZoneCollision()`, `findBspRegionForPoint(float,float,float)`,
    `addRegionToCollision(size_t)`, `addDoorToCollision(uint8_t)`,
    `addObjectToCollision(size_t)` — the "collision-setup methods in that
    contiguous region" called out by the plan; these build/maintain the
    per-BSP-region world-space triangle caches (`regionWorldTriangles_`,
    `objectWorldTriangles_`, `doorCollisionData_`) consumed by the
    BSP-filtered raycasting subsystem below.
  - The section header comment `// --- Player Mode Movement Implementation
    ---` (originally one line before `updatePlayerMovement`, outside the
    cluster's own contiguous byte range) was moved with its cluster rather
    than left orphaned above the next remaining method
    (`processFrameProgressiveLoad`); see "Deviations" below.

  **Explicitly left in `irrlicht_renderer.cpp`, immediately before this
  cluster:** `setClipDistance(float)`, `getClipDistance() const` — a
  diagnostics/renderer-mode setter/getter pair, not movement/collision (same
  pair F15's review noted sitting near its own cluster boundary).
  **Immediately after:** `processFrameProgressiveLoad()` and the whole
  progressive-loading/PVS-neighborhood block (`getRegionPvsDepth`,
  `rebuildPvsNeighborhood`, `isInPvsNeighborhood`, `onPvsRegionChanged`,
  `queueEntityPrepRequests`, `logAssetBuildTime`, `sendLoadProgress`,
  `checkProgressiveLoadingComplete`) — zone-loading/asset-streaming logic,
  not player physics; left untouched per the plan's "when in doubt, leave"
  rule.

- **Collision raycasting subsystem** (pre-move lines 11916–12337, contiguous,
  running to end of file before the namespace-closing braces). 10
  `IrrlichtRenderer::` methods, matching the plan's and F15's review list
  exactly:
  - `checkCollisionIrrlicht(...)`, `findGroundZIrrlicht(...)` — deprecated
    "old meta selector" diagnostic stubs (both just log a warning and
    return a sentinel; kept verbatim including the `LOG_WARN` diagnostic
    text).
  - `extractWorldTriangles(irr::scene::ISceneManager*, irr::scene::IMeshSceneNode*)`
    — one-shot triangle-selector extraction helper, called by both this
    cluster's `setupMinimalZoneCollision`/`addRegionToCollision`/
    `addDoorToCollision`/`addObjectToCollision` (cluster 1) and internally
    unused elsewhere.
  - `rayIntersectTriangles(...)` — Möller–Trumbore ray-triangle intersection,
    the shared primitive both `rayTestDoorsAndObjects` and
    `checkCollisionBspFiltered`/`findGroundZBspFiltered` call.
  - `rayTestDoorsAndObjects(...)`, `checkCollisionBspFiltered(...)`,
    `findGroundZBspFiltered(...)` — the BSP-filtered hot-path collision
    system (per-region world-space triangles, no matrix math), called from
    cluster 1's `updatePlayerMovement`.
  - `addCollisionDebugLine(...)`, `drawCollisionDebugLines(float)`,
    `clearCollisionDebugLines()` — the collision-debug-line ring buffer used
    by both `updatePlayerMovement`, `checkMovementCollision`, `findGroundZ`,
    and `findGroundZBspFiltered`.
  - Two comment headers inside this range moved with it verbatim:
    `// updateNameTagsWithLOS removed — ...` (orphan removal note) and
    `// --- BSP-Filtered Collision (hot path: movement) ---` (+ its two
    follow-on comment lines), plus `// --- Collision Debug Visualization
    ---` later in the range.

**No `#ifdef` guards present in either moved range** — nothing to preserve
beyond ordinary verbatim copy.

**Namespace + include handling (same pattern as F14/F15/F16/F21):**
- New file wraps its body in `namespace EQT { namespace Graphics { ... } }`,
  matching `irrlicht_renderer.cpp`.
- `renderer_player_physics.cpp` needed:
  - `client/bridge/game_state_bridge.h`, `client/state/event_bus.h`,
    `client/events/renderer_intents.h` — `updatePlayerMovement()` calls
    `bridge_->pushIntent(eqt::events::PlayerPositionChanged{...})`;
    `irrlicht_renderer.h` only forward-declares `eqt::bridge::GameStateBridge`
    (same gap F15 hit for `handleMouseTargeting`), so the full type and
    intent struct need direct includes.
  - `client/graphics/constrained_mesh_cache.h` — `setupMinimalZoneCollision()`
    calls `constrainedMeshCache_->isLoaded(...)` on the forward-declared
    `ConstrainedMeshCache` (`irrlicht_renderer.h` line 81); caught by the
    first build attempt ("invalid use of incomplete type
    'class ConstrainedMeshCache'"), fixed by adding the include the original
    TU had unconditionally.
  - `client/hc_map.h` — `BEST_Z_INVALID` and `HCMap::CheckLOSWithHit`/
    `FindBestZ` used by `checkMovementCollision`/`findGroundZ`; already
    included by `irrlicht_renderer.h` transitively, added directly here too
    since these methods are the primary consumers (matches the belt-and-
    suspenders style of F15's `renderer_overlays.cpp`).
  - `client/input/hotkey_manager.h` — `eqt::input::HotkeyManager::instance()`,
    `hasModifier`, `ModifierFlags` used by `updatePlayerMovement`'s
    `isActionHeld` lambda; already pulled in transitively via
    `irrlicht_renderer.h`, included directly for the same belt-and-suspenders
    reason.
  - `common/logging.h`, `<algorithm>`, `<chrono>`, `<cmath>`, `<cstdint>`,
    `<limits>` — `LOG_INFO`/`LOG_WARN`/`LOG_DEBUG`/`LOG_TRACE`, `std::max`,
    `std::chrono::steady_clock`, `std::sin`/`std::cos`/`std::abs`,
    `uint8_t`, `std::numeric_limits<float>::max()` — full includes the
    original TU had unconditionally.
- `PlayerPositionUpdate` (used by `updatePlayerMovement`), `CameraMode`,
  `DoorCollisionData`, `CollisionDebugLine` needed no extra include: all are
  defined directly in `irrlicht_renderer.h` or pulled in transitively via its
  existing includes (`door_manager.h`, `camera_controller.h`).
- Raw Irrlicht types (`irr::core::vector3df`, `irr::core::triangle3df`,
  `irr::scene::ITriangleSelector`, `irr::video::SColor`, etc.) compiled
  without any additional Irrlicht header — these come in transitively via
  `irrlicht_renderer.h`'s own `#include <irrlicht.h>`, same as the original
  single TU.

**Shared-helper analysis:** No file-local `static`/anonymous-namespace
symbols in `irrlicht_renderer.cpp` were referenced by either moved cluster.
The one file-local `static` inside the moved code
(`static bool hadMovementInput`, `static float prevX/prevY/prevZ`,
`static auto lastCallbackTime`, all function-local statics inside
`updatePlayerMovement` itself) moved with the function body verbatim — these
are local statics scoped to the function, not file-scope, so no linkage
concern.

**Line counts:**
- `irrlicht_renderer.cpp`: 12,340 → 10,790 lines (**-1,550**)
- `renderer_player_physics.cpp`: 1,570 lines (new; 1,548 moved lines + 2
  moved comment/blank lines relocated from the cut point + 20 lines of
  includes/namespace-wrapper boilerplate)
- Net change: +1,570 / -1,550 = +20 lines total (all boilerplate — no
  content duplicated or dropped)
- Deviation from the plan's "~1,800 expected" estimate: the plan's own
  scope note flagged this ("heavily shifted by F14–F16/F21; enumerate by
  NAME") — F14–F16/F21 already extracted ~1,600+ debug/overlay/effects lines
  from the region between the two clusters and around them, so the two
  clusters named in the plan are smaller in the current file than the
  original 11835–14053 pre-move range implied. No extra methods were pulled
  in to hit the estimate — cluster membership was decided purely by name
  match against the plan's explicit method list.

**Build/test verification:**
- `cmake --build build -j24`: clean after one fix found during the first
  pass (mechanical, not scope creep): missing
  `#include "client/graphics/constrained_mesh_cache.h"` in
  `renderer_player_physics.cpp` for `setupMinimalZoneCollision()`'s use of
  the forward-declared `ConstrainedMeshCache` type.
- `ctest --test-dir build`: 1191/1192 run tests passed (9 pre-existing
  asset/graphics/audio tests skipped, unrelated to this unit).
  - `AudioManagerTest.PreloadCommonSounds`: failed with the documented ALSA
    "cannot find card '0'" environment gap
    (`refactor/test_baseline.txt`), consistent with F02/F14/F15 findings on
    this host.
- `python3 scripts/refactor/verify_move_only.py 8b29308..HEAD --repo
  <slot2>`: **PASS** ("Boilerplate: +17 / -0 lines (allowed)"). The verifier
  also listed `CMakeLists.txt` as a changed non-C++ file — this unit's own
  edit (adding `renderer_player_physics.cpp` to `GRAPHICS_SOURCES`,
  additive/mechanical, one line). `git merge-base fable HEAD` resolves to
  `8b29308`, confirming no rebase occurred and `fable` did not advance
  during this unit's work.
- `bash scripts/refactor/check_file_sizes.sh --files
  <slot2>/renderer_player_physics.cpp`: **WARN** (1,570 lines, over the
  1,500-line ceiling by 70 lines) — accepted per the plan's verification
  criteria ("WARN accepted, must not FAIL"). Not split further since the
  plan defines this as one cohesive unit (movement + the raycasting
  subsystem it depends on) and 1,570 is well under the 3,000-line FAIL
  threshold.

**Deviations from plan:** none in scope beyond the two already noted above
(smaller-than-estimated line count due to prior units' extractions; the
orphaned section-header comment moved with its cluster rather than left
behind). No methods were left in-range beyond the two explicitly-documented
boundary cases (`setClipDistance`/`getClipDistance` before; progressive-load/
PVS block after) — both match the plan's and F15's prior findings.
