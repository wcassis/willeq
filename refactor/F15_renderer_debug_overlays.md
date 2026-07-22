# F15 — Split debug toggles and overlays out of irrlicht_renderer.cpp

Lift the debug-toggle cluster and the overlay/targeting cluster out of
`src/client/graphics/irrlicht_renderer.cpp` (15,243 lines after F14).
Pure move — no behavior change, no interface change, no reformatting.

## Scope (original refs @ a0c02cd; F14 removed 1,151 lines so positions have
shifted. Enumerate by NAME.)

Two new files under `src/client/graphics/`:

**renderer_debug.cpp** — the debug toggle cluster (orig ~10901–11533):
every `toggle*` / `cycle*` debug method (~30 of them) plus any contiguous
small debug setters in that block. **Explicitly NOT in scope:** `run()` and
screenshot capture — they are main-loop/user functionality living in the same
region; leave them in irrlicht_renderer.cpp.

**renderer_overlays.cpp** — the overlay + targeting cluster (orig
~14054–15295): map overlay, navmesh overlay, target outline/selection
rendering, zone-line overlay, FPS display, and the targeting methods
(`setCurrentTarget`, `handleMouseTargeting`, `getEntityAtScreenPos`,
`checkEntityLOS`). Enumerate every `IrrlichtRenderer::` definition in the
contiguous range by grep and list them in the review; anything in-range that
is clearly NOT overlay/targeting (judgment call) stays put, documented.

## Steps

1. Locate boundaries by grep of `IrrlichtRenderer::` definitions; watch for
   return types that break simple regexes (F14 hit this with `int64_t`).
2. Preserve any `#ifdef` feature guards exactly (EQT_HAS_GLES2 / EQT_HAS_DRM
   nesting occurs in this file).
3. Both files need the `namespace EQT { namespace Graphics { ... } }` wrapper
   (the original TU opens it once at file scope) and full-type includes for
   classes that irrlicht_renderer.h only forward-declares — F14's review
   documents this pattern.
4. File-local `static` helpers: move if exclusively used by moved code, else
   leave and document.
5. Add both files to `GRAPHICS_SOURCES` in CMakeLists.txt.

## Verification

- `cmake --build build -j24` clean.
- `ctest --test-dir build` — green modulo the two documented exceptions in
  refactor/test_baseline.txt (RDP flake: re-run isolated; ALSA
  AudioManagerTest.PreloadCommonSounds: expected-fail on this host).
- `python3 scripts/refactor/verify_move_only.py fable..HEAD` — PASS or
  documented de-static-ing only.
- `bash scripts/refactor/check_file_sizes.sh --files <abs>/renderer_debug.cpp <abs>/renderer_overlays.cpp` — ok (overlays ~1,300 expected, may WARN if larger; must not FAIL).
- irrlicht_renderer.cpp shrinks by ~1,800+ lines.

## Review

**Cluster boundaries (verified by grep of `IrrlichtRenderer::` definitions,
irrlicht_renderer.cpp @ branch-point commit 70c0115, 15,243 lines after F14):**

- `renderer_debug.cpp`: lines 10702–11221 of the pre-move file — a single
  contiguous block, no `#ifdef` guards. 19 `IrrlichtRenderer::` methods:
  - `toggleWireframe()`
  - `toggleHUD()`
  - `toggleAllUI()`
  - `toggleNameTags()`
  - `toggleFog()`
  - `toggleZoneLights()`
  - `togglePlayerLight()`
  - `swapZoneMeshMaterials()` — helper used by `togglePlayerLight()` (and by
    debug-shader toggling); moved with its caller
  - `swapToDebugShader(irr::s32)`
  - `swapFromDebugShader(irr::s32)`
  - `toggleObjectLights()`
  - `toggleDirectionalLight()`
  - `cycleObjectLights()`
  - `toggleOldModels()`
  - `isUsingOldModels() const` — paired getter for `toggleOldModels()`, moved
    together
  - `toggleManualZoneDraw()`
  - `loadIndoorRegionMap(const std::string&)` — not a toggle itself, but a
    contiguous small helper that exists only to support the portal-occlusion
    debug toggles immediately following it; included per the plan's "small
    debug setters in that block" allowance
  - `togglePortalOcclusion()`
  - `togglePortalDebugDraw()`
  - `toggleStencilDebugDraw()`

  **Explicitly left in `irrlicht_renderer.cpp`, immediately after this
  cluster:** `setFrameTimingEnabled(bool)`, `setClipDistance(float)`,
  `getClipDistance() const` — a diagnostics/renderer-mode setter/getter pair
  under an explicit `// --- Renderer Mode Implementation ---` comment
  boundary, not a toggle/cycle debug method. `run()` (line 10614, pre-move)
  and `saveScreenshot()` (line 10638) sit immediately *before* this cluster
  and were left untouched per the plan's explicit exclusion.

- `renderer_overlays.cpp`: two non-contiguous ranges in the pre-move file
  (the plan's "anything in-range that is clearly NOT overlay/targeting stays
  put, documented" applied to the gap between them), concatenated in file
  order:
  1. Map/navmesh overlay + targeting (pre-move lines 13467–14190):
     - `toggleMapOverlay()`
     - `updateMapOverlay(const glm::vec3&)`
     - `getMapOverlayColor(float, float, float, const glm::vec3&) const`
     - `toggleNavmeshOverlay()`
     - `updateNavmeshOverlay(const glm::vec3&)` (body under `#ifdef
       EQT_HAS_NAVMESH`)
     - `getNavmeshAreaColor(uint8_t) const`
     - `drawNavmeshOverlay()`
     - `drawTargetOutline()` / `drawTargetSelectionBox()` — mutually
       exclusive pair under `#ifdef EQT_HAS_GLES2` / `#else` / `#endif`,
       preserved verbatim
     - `setCurrentTarget(uint16_t, const std::string&, uint8_t, uint8_t)`
     - `clearCurrentTarget()`
     - `setCurrentTargetInfo(const TargetInfo&)`
     - `updateCurrentTargetHP(uint8_t)`
     - `handleMouseTargeting(int, int)`
     - `getEntityAtScreenPos(int, int)`
     - `checkEntityLOS(const irr::core::vector3df&, const
       irr::core::vector3df&)`
  2. Zone-line overlay + FPS display (pre-move lines 14388–14707):
     - `setZoneLineDebug(bool, uint16_t, const std::string&)`
     - `drawZoneLineOverlay()`
     - `setZoneLineBoundingBoxes(const std::vector<EQT::ZoneLineBoundingBox>&)`
     - `clearZoneLineBoundingBoxes()`
     - `toggleZoneLineVisualization()`
     - `removeZoneLineBoxSceneNodes()`
     - `recreateZoneLineBoxSceneNodes()`
     - `createZoneLineBoxMesh(const EQT::ZoneLineBoundingBox&)`
     - `createZoneLineBoxMeshForNode(const EQT::ZoneLineBoundingBox&,
       ZoneLineBoxNode&)`
     - `drawZoneLineBoxLabels()`
     - `drawFPSCounter()`

  **In-range but explicitly left in `irrlicht_renderer.cpp` (judgment call,
  not overlay/targeting):**
  - Immediately *before* the first range: `checkCollisionIrrlicht`,
    `findGroundZIrrlicht`, `extractWorldTriangles`,
    `rayIntersectTriangles`, `rayTestDoorsAndObjects`,
    `checkCollisionBspFiltered`, `findGroundZBspFiltered`,
    `addCollisionDebugLine`, `drawCollisionDebugLines`,
    `clearCollisionDebugLines` — collision/raycast plumbing and a distinct
    "collision debug line" drawing feature, not mentioned in the plan's
    overlay/targeting list and not overlay/targeting rendering.
  - Between the two ranges (the gap at pre-move lines 14192–14387):
    `setInventoryManager`, `toggleInventory`, `openInventory`,
    `closeInventory`, `showNoteWindow`, `isInventoryOpen`,
    `setCharacterInfo`, `setCharacterDeity`, `setExpProgress`,
    `updateCharacterStats`, `updatePlayerAppearance`,
    `updateEntityAppearance`, `setChatSubmitCallback`,
    `setReadItemCallback` — inventory/character-info/UI callback plumbing
    under a `// --- Inventory UI Methods ---` header, unrelated to
    overlays/targeting.
  - Immediately *after* the second range (from `setEntityWeaponSkills`
    onward through EOF): combat-animation queuing methods, icosphere
    mesh/shader build + fire-glow lighting/rendering, and EQE1 archive
    loader methods (`getOrOpenEQE1`, `loadEQE1ArchiveConfig`,
    `resolveEQE1Archives`) — none are overlay/targeting; left untouched
    through end of file.

**Guard handling:** `#ifdef EQT_HAS_NAVMESH` around `updateNavmeshOverlay`'s
body and `#ifdef EQT_HAS_GLES2` / `#else` / `#endif` around the
`drawTargetOutline` / `drawTargetSelectionBox` pair were both fully
self-contained within the first moved range and preserved verbatim,
unmodified, in their original positions.

**Namespace + include handling (same pattern as F14):**
- Both new files wrap their bodies in `namespace EQT { namespace Graphics {
  ... } }`, matching the enclosing namespace `irrlicht_renderer.cpp` opens
  once at file scope.
- `renderer_debug.cpp` needed `<json/json.h>` and `<fstream>` (used by
  `loadIndoorRegionMap`) — full includes the original TU had unconditionally
  but which are not pulled in by `irrlicht_renderer.h`.
- `renderer_overlays.cpp` needed:
  - `client/bridge/game_state_bridge.h`, `client/state/event_bus.h`,
    `client/events/renderer_intents.h` — `handleMouseTargeting()` calls
    `bridge_->pushIntent(eqt::events::...)`; `irrlicht_renderer.h` only
    forward-declares `eqt::bridge::GameStateBridge`, so the full type and
    the intent structs need direct includes.
  - `client/zone_lines.h` — `EQT::ZoneLineBoundingBox` is only
    forward-declared in `irrlicht_renderer.h` (line 62); the zone-line box
    methods construct and read its members directly.
  - `client/pathfinder_nav_mesh.h` under `#ifdef EQT_HAS_NAVMESH` —
    `updateNavmeshOverlay()` calls `navmesh_->GetTrianglesInRadius(...)` on
    the forward-declared `PathfinderNavmesh` (`irrlicht_renderer.h` line 59);
    caught by the first build attempt ("invalid use of incomplete type
    'class PathfinderNavmesh'"), fixed by adding the same conditionally
    guarded include the original TU had.
  - `<algorithm>`, `<cstdint>`, `<limits>` — `std::min`/`std::max`
    (`getMapOverlayColor`), `SIZE_MAX` (`checkEntityLOS`),
    `std::numeric_limits<float>::max()` (`updateMapOverlay`,
    `updateNavmeshOverlay`, `getEntityAtScreenPos`) — full includes the
    original TU had unconditionally.
- `BspTree` (used by `checkEntityLOS`) and `TargetInfo` (used by the
  targeting methods) needed no extra include: `BspTree` comes in
  transitively via `irrlicht_renderer.h` → `zone_geometry.h` →
  `wld_loader.h` → `zone_bsp.h`, and `TargetInfo` is a full struct defined
  directly in `irrlicht_renderer.h`.
- Raw GL calls in `drawTargetOutline` (`glEnable`, `glStencilFunc`, `GLint`,
  etc.) compiled without any additional GL header — these come in
  transitively through Irrlicht's own headers the same way they did in the
  original single TU; no change needed.

**Shared-helper analysis:** No file-local `static`/anonymous-namespace
symbols in `irrlicht_renderer.cpp` were referenced by either moved cluster —
the only two file-local statics in the whole file
(`loadDisplaySettingsFromFile`, `logDriverDetails`) are near the top of the
file and unrelated to debug toggles or overlays.

**Line counts:**
- `irrlicht_renderer.cpp`: 15,243 → 13,679 lines (**-1,564**)
- `renderer_debug.cpp`: 531 lines (new; 520 moved lines + includes/namespace
  wrapper)
- `renderer_overlays.cpp`: 1,064 lines (new; 724 + 320 = 1,044 moved lines +
  includes/namespace wrapper + 1 blank separator line between the two
  concatenated ranges)
- Net change: +1,595 / -1,564 ≈ +31 lines total (headers/namespace
  boilerplate in two new TUs, offset by the two doubled blank lines left at
  the cut points in `irrlicht_renderer.cpp`, which were left as-is rather
  than hand-tidied to avoid touching content beyond the plan's scope)

**Build/test verification:**
- `cmake --build build -j24`: clean after one fix found during the first
  pass (mechanical, not scope creep): missing `#ifdef EQT_HAS_NAVMESH` /
  `#include "client/pathfinder_nav_mesh.h"` / `#endif` in
  `renderer_overlays.cpp` for `updateNavmeshOverlay()`'s use of the
  forward-declared `PathfinderNavmesh` type.
- `ctest --test-dir build`: 1191/1192 run tests passed (9 pre-existing
  asset/graphics tests skipped, unrelated to this unit).
  - `AudioManagerTest.PreloadCommonSounds`: failed with the documented ALSA
    "cannot find card '0'" environment gap (`refactor/test_baseline.txt`),
    consistent with the F02/F14 findings on this host. Not re-verified by
    stash-bisection again since it's already independently confirmed twice.
  - `RDPServerFrameTest.UpdateFrameMultipleTimes`: passed in the full run;
    the documented flake did not reproduce.
- `python3 scripts/refactor/verify_move_only.py fable..HEAD --repo
  <slot3>`: PASS ("Boilerplate: +0 / -21 lines (allowed)"). The verifier
  also listed `CMakeLists.txt` and `refactor/F03_login_world.md` as changed
  non-C++ files — both are drift from `fable` advancing (F03 was merged to
  `fable` by another concurrent executor after this branch was cut from
  commit `70c0115`), not changes made by this unit; this unit's own
  `CMakeLists.txt` edit (adding the two new source files to
  `GRAPHICS_SOURCES`) is the only actual change this unit made to that
  file, and it is additive/mechanical. `git merge-base fable HEAD` still
  resolves to `70c0115` (this unit's branch point), confirming no rebase
  occurred.
- `bash scripts/refactor/check_file_sizes.sh --files <slot3>/renderer_debug.cpp
  <slot3>/renderer_overlays.cpp`: both `ok` (531 and 1,064 lines,
  comfortably under the 1,500-line ceiling; no WARN).

**Deviations from plan:** none in scope. Two clarifications beyond the
plan's line-range estimates (both required by the plan's own "confirm exact
set by grep" instruction): `loadIndoorRegionMap` included in the debug
cluster (contiguous small helper serving the portal-debug toggles, not
itself a toggle); the overlay/targeting cluster is two non-contiguous
ranges rather than one, with inventory/character-info UI methods (between
the ranges) and collision-debug-line / combat-animation / icosphere /
fire-glow / EQE1-loader methods (surrounding the ranges) left in place as
judgment calls, documented above.
