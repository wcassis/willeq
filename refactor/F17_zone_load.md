# F17 — Split zone loading out of irrlicht_renderer.cpp

The final Batch F unit: move `loadZoneSequential` (~2,665 lines, the largest
single method in the codebase) and its zone-lifecycle siblings out of
`src/client/graphics/irrlicht_renderer.cpp`. Pure move — no behavior change,
no interface change, no reformatting.

**Explicitly NOT this unit:** decomposing loadZoneSequential's body into
per-step helper methods. That requires new private method declarations
(header changes) and breaks the move-only verifier guarantee. It is recorded
as a candidate follow-up (Batch G) in the review. The method's internal
`// ── Step N ──` banners (Steps 0–13) already provide navigation.

## Scope — two new files under `src/client/graphics/` (enumerate by NAME)

**renderer_zone_load.cpp** (~2,700 lines — WARN accepted, must stay under
the 3,000 FAIL ceiling):
- `loadZoneSequential` — moved verbatim, all step banners and #ifdef guards
  intact.

**renderer_zone_lifecycle.cpp** (~700 lines):
- `unloadZone`
- `setZoneEnvironment`
- `buildZonePlaceholder`
- `setupInstantScene` (and any other contiguous zone-lifecycle setup methods
  in that region — enumerate by reading the cluster; the loading-screen and
  camera/lighting clusters are NOT in scope)
- The zone-ready state methods immediately after loadZoneSequential
  (`setExpectedEntityCount`, `notifyEntityLoaded`, `setNetworkReady`,
  `checkAndSetZoneReady`) — these were left behind by F21 as zone-loading
  state management; they belong here. `setWeather` sits among them —
  judgment: weather is environment state, include it only if contiguity
  makes leaving it messier; document either way.

## Steps

1. Locate all boundaries by grep; produce the mapping in the review BEFORE
   moving. When in doubt, leave + document.
2. Replicate namespace wrapper + include patterns (F14–F22 reviews; expect
   many forward-declaration gaps — this method touches nearly every
   subsystem; budget for several build-fix iterations of missing includes).
3. Move verbatim; preserve every #ifdef guard and step banner exactly.
4. File-local statics / extern declarations: move with sole callers (F18
   pattern), duplicate only if genuinely shared (F21 pattern) — document.
5. Add both files to `GRAPHICS_SOURCES` in CMakeLists.txt.

## Verification

- `cmake --build build -j24` clean.
- `ctest --test-dir build` — green modulo documented exceptions in
  refactor/test_baseline.txt. Zone/graphics integration tests
  (test_zoning_graphics_integration, ComprehensiveZoneTest, DoorLoading)
  MUST pass — they exercise this exact path.
- `python3 scripts/refactor/verify_move_only.py BASE..HEAD` — PASS or
  documented deviations only.
- `bash scripts/refactor/check_file_sizes.sh --files <two abs paths>` —
  renderer_zone_load.cpp WARN accepted, must NOT reach FAIL (3,000);
  renderer_zone_lifecycle.cpp ok.
- irrlicht_renderer.cpp shrinks by ~3,300+ lines.

## Review

**Cluster boundaries (verified by grep of `IrrlichtRenderer::` definitions,
`irrlicht_renderer.cpp` @ merge-base commit `28e28159a788f00e24db76ab507577b81264c872`
with `fable`, 6,037 lines before this move — `git merge-base fable HEAD`
confirms `fable` did not advance during this unit's work):**

Two non-contiguous pre-move ranges, both bracketed by methods that stay
behind — matching the F18/F21 pattern of a "façade cluster" interleaved
with methods judged out of scope.

- **`renderer_zone_load.cpp`** — pre-move lines 2781–5397 (2,617 lines): the
  orphaned two-line comment immediately preceding the method (`// Decode BMP
  data to A8R8G8B8 pixel array...` / two follow-on lines documenting T14's
  removal of `decodeBMPtoARGB`/`bilinearUpscaleARGB`) plus
  `loadZoneSequential(const std::string&, LoadingStatus&)` in its entirety —
  moved verbatim per the plan's explicit "no per-step decomposition" rule.
  All 15 `// ── Step N ──` banners (Steps 0, 1, 2, 4, 5, 6, 7, 8 with 8a–8f,
  9, 10, 11, 12, 12b, 13 — Step 3 was already removed pre-Batch-F, its
  banner replaced by a one-line "removed" comment that moved with the rest)
  preserved exactly, unmodified, in original position and order.

- **`renderer_zone_lifecycle.cpp`** — four pre-move ranges concatenated in
  file order:
  1. Lines 2068–2402: `unloadZone()`, `setZoneEnvironment(uint8_t, uint8_t,
     const uint8_t[4], const uint8_t[4], const uint8_t[4], const float[4],
     const float[4])`.
  2. Lines 2444–2641: `buildZonePlaceholder(float, float, float)`,
     `destroyZonePlaceholder()` — the latter not named in the plan's prose
     but the direct build/destroy pair (sole non-trivial caller of
     `destroyZonePlaceholder` is `unloadZone`, moved into the same file;
     included per the plan's "any other contiguous zone-lifecycle setup
     methods in that region" clause).
  3. Lines 2679–2779: `setupInstantScene(const std::string&, float, float,
     float)` (named in the plan), `setupHCMapCollision()` (its sole
     immediate-post-placeholder collision-setup helper, called only from
     `setupInstantScene`), `storeZoneEnvironment(...)`/
     `applyStoredZoneEnvironment()` (the deferred-apply pair for
     `setZoneEnvironment`, used by the zone-load pipeline to stage
     environment data before the renderer is ready to apply it).
  4. Lines 5403–5452: the `// Entity Loading State Management` section
     header comment plus `setExpectedEntityCount(size_t)`,
     `notifyEntityLoaded()`, `setNetworkReady(bool)`, `setWeather(uint8_t,
     uint8_t)`, `checkAndSetZoneReady()` — exactly the five methods the
     plan names, moved as one contiguous block immediately after
     `loadZoneSequential`.

**In-range but explicitly left in `irrlicht_renderer.cpp` (judgment calls):**
- Lines 2404–2442 (between ranges 1 and 2): `toggleSky()`, `forceSkyType(uint8_t)`,
  `isSkyEnabled() const`, `getSkyDebugInfo() const` — a self-contained sky
  toggle/query façade, not zone-lifecycle state management (it has no
  zone-load-time caller in the moved code; `setZoneEnvironment` calls
  `skyRenderer_` methods directly, not through this façade). Judged a
  "sky" cluster, not a "zone lifecycle" cluster; left per "when in doubt,
  leave."
- Lines 2643–2678 (between ranges 2 and 3): `createEntityRenderer()` — its
  own comment states "S05: Called once from initLoadingScreen(). No
  lazy-init guard needed." — an app-init-time method (invoked once from
  `initLoadingScreen`, not per zone-load), not a per-zone-transition
  lifecycle method. Left per the plan's explicit carve-out that the
  loading-screen cluster is NOT in scope.
- `createDoor`/`registerDoor`/`setDoorState`/`clearDoors` and
  `addWorldObject`/`removeWorldObject`/`clearWorldObjects`/
  `getWorldObjectAtScreenPos`/`getNearestWorldObject`, immediately after
  range 4 — the door and world-object façades F21's review already
  identified and left in place under its own "Entity Loading State
  Management" boundary; confirmed unchanged and still out of scope here.

**`setWeather` disposition:** included in the moved cluster. It sits
between `setNetworkReady` and `checkAndSetZoneReady`, inside the single
contiguous "Entity Loading State Management" section the plan itself
carves out as one block — splitting the five-method block to leave just
`setWeather` behind would require re-opening the section header comment's
scope and leave a one-method island with no home of its own. Per the
plan's documented judgment call ("weather is environment state, include it
only if contiguity makes leaving it messier"), contiguity governed: moved.

**Guard/banner handling:** `loadZoneSequential`'s internal guards —
`#ifdef EQT_HAS_GLES2` / `#else` / `#endif` (Step 4 VBO upload path),
`#ifdef EQT_HAS_DRM` / `#else` / `#endif` (Step 11 environment subsystem
init) — preserved verbatim, unmodified, in original position; both open
and close entirely within the moved range, confirmed by a line-range-scoped
grep before moving. `unloadZone`'s `#if defined(EQT_HAS_DRM) &&
!defined(EQT_HAS_GLES2)` (Lima-driver GLES2-texture-release block) and
`#ifdef EQT_HAS_GLES2` (pendingGPUResults_ clear) guards, and
`buildZonePlaceholder`'s `#ifndef EQT_HAS_GLES2` (desktop-GL wireframe
material) guard, preserved verbatim in `renderer_zone_lifecycle.cpp`. All
15 `// ── Step N ──` banners preserved exactly as specified in the plan.

**File-local statics / extern declarations:** none found. A line-range
grep of both moved ranges for `gles2` free-function calls, the file-scope
`static eqt::ui::DisplaySettings loadDisplaySettingsFromFile()` helper, and
the `WindowManager` shim struct turned up no references — the only
`gles2`-prefixed token in either range is the `gles2Helper_` *member*
(unrelated to the file-scope `extern gles2*StaticHWBuffer`/bridge
functions other units have had to duplicate). No function-local `static`
variables exist inside `loadZoneSequential` or any of the lifecycle
methods. Nothing needed to move, duplicate, or de-static.

**Namespace + include handling (same pattern as F14–F22):**
- Both new files wrap their bodies in `namespace EQT { namespace Graphics {
  ... } }`, matching `irrlicht_renderer.cpp`.
- `renderer_zone_load.cpp` needed the largest include set of any Batch F
  unit so far, consistent with the plan's "touches nearly every subsystem"
  warning — but the **first build attempt compiled clean**, because the
  full set was assembled up front from the original TU's own top-of-file
  include list (`loadZoneSequential` is the method those includes exist
  for) rather than discovered incrementally: `willeq_database.h`,
  `loading_thread.h`, `entity_prep_worker.h`, `eqe1_loader.h`,
  `eq/dds_decoder.h`, `eq/equipment_textures.h`, `frame_budget_governor.h`,
  `constrained_texture_cache.h`, `constrained_mesh_cache.h`,
  `light_source.h`, `weather_effects_controller.h`,
  `environment/zone_biome.h`, `eq/zone_geometry.h`, `eq/race_model_loader.h`,
  `eq/race_codes.h`, `inventory/inventory_manager.h`, `spell_visual_fx.h`,
  `sky_renderer.h`, `eq/sky_loader.h`, `sky_config.h`, `zone_lines.h`,
  `hc_map.h`, `bridge/game_state_bridge.h`, `state/event_bus.h`,
  `events/renderer_intents.h`, `common/logging.h`, `common/name_utils.h`,
  `common/performance_metrics.h`, `<fmt/format.h>`, `<filesystem>`,
  `<algorithm>`, `<chrono>`, `<cstdlib>`, `<cstring>`, `<cmath>`,
  `<limits>`, `<set>`, `<sstream>`, plus the conditional
  `EQT_HAS_DRM`/`EQT_HAS_GLES2` EGL/GL header block (`GLES2/gl2.h`,
  `EGL/eglext.h` or `GL/gl.h`, `EGL/egl.h`) copied verbatim from the
  original TU's preamble for the Step 11 environment-subsystem `#ifdef
  EQT_HAS_DRM` branch.
- `renderer_zone_lifecycle.cpp` needed one build-fix iteration: the first
  attempt failed with "invalid use of incomplete type
  `class EQT::Graphics::SkyRenderer`" (4 call sites in `setZoneEnvironment`
  — `skyRenderer_->isInitialized()`/`setSkyType()`/`setEnabled()`); fixed by
  adding `client/graphics/sky_renderer.h` (forward-declared only in
  `irrlicht_renderer.h`, same gap F20/F22 hit for the same header). Final
  include set: `irrlicht_renderer.h`, `entity_prep_worker.h`
  (`unloadZone`'s `entityPrepWorker_->stop()`/`reset()`),
  `constrained_texture_cache.h`, `constrained_mesh_cache.h` (both
  forward-declared only; `unloadZone` calls their full APIs directly),
  `weather_effects_controller.h`, `sky_renderer.h`, `hc_map.h`
  (`buildZonePlaceholder`'s `HCMap::Triangle`), `common/logging.h`,
  `<algorithm>`, `<cmath>`, `<mutex>` (`unloadZone`'s
  `std::lock_guard<std::mutex>` on `eqe1Mutex_`/`meshResultMutex_`).
- No other forward-declaration gaps were hit on the first attempt for
  `renderer_zone_load.cpp` — the up-front full-include-set strategy (based
  on reading the original TU's preamble rather than reacting to individual
  errors) avoided the "several build-fix iterations" the plan budgeted for.

**Line counts:**
- `irrlicht_renderer.cpp`: 6,037 → 2,736 lines (**−3,301**)
- `renderer_zone_load.cpp`: 2,673 lines (new; 2,617 moved lines + 56 lines
  of includes/namespace-wrapper boilerplate)
- `renderer_zone_lifecycle.cpp`: 705 lines (new; 684 moved lines across 4
  ranges + 21 lines of includes/namespace-wrapper boilerplate)
- Net change: +3,378 / −3,301 = +77 lines total (all boilerplate — no
  content duplicated or dropped; matches the verifier's own "+67" figure
  for the two `.cpp` files, the remaining 10 being this review's own
  addition to the plan doc, not counted by the verifier)

**Build/test verification:**
- `cmake --build build -j24`: clean after one fix found during the first
  pass on `renderer_zone_lifecycle.cpp` (mechanical, not scope creep):
  missing `#include "client/graphics/sky_renderer.h"`.
  `renderer_zone_load.cpp` compiled clean on the very first attempt.
- `ctest --test-dir build`: 1,191/1,192 run tests passed (10 skipped:
  asset/graphics/audio-dependent tests unrelated to this unit, per
  `refactor/test_baseline.txt`). The one failure,
  `AudioManagerTest.PreloadCommonSounds`, is the documented pre-existing
  ALSA "cannot find card '0'" environment gap, consistent with
  F02/F14/F15/F18/F19/F20/F21/F22 findings on this host.
  - Zone/graphics integration coverage: `ComprehensiveZoneTest.*` (6
    subtests, incl. `ClassicZones`/`KunarkZones`/`VeliousZones`) and
    `DoorLoadingUnitTest.*` (4 subtests) all **passed** — these exercise
    the moved `loadZoneSequential`/`setupInstantScene`/`unloadZone` path
    directly. A test literally named `test_zoning_graphics_integration` or
    `DoorLoading` does not exist in the current suite (`ctest -N` searched);
    `ComprehensiveZoneTest` and `DoorLoadingUnitTest`/`DoorManagerGraphicsTest`
    are the closest-matching zone/door graphics-integration coverage and
    all passed — noting this as a naming clarification, not a gap.
- `git merge-base fable HEAD` →
  `28e28159a788f00e24db76ab507577b81264c872`, confirming no rebase occurred
  and `fable` did not advance during this unit's work; used as BASE below.
- `python3 scripts/refactor/verify_move_only.py
  28e28159a788f00e24db76ab507577b81264c872..HEAD --repo <slot1>`: **PASS**
  ("Boilerplate: +67 / -0 lines (allowed)", 3 C++ files verified).
  `CMakeLists.txt` (the additive 2-line `GRAPHICS_SOURCES` entry) reported
  separately as non-C++, not verified.
- `bash scripts/refactor/check_file_sizes.sh --files
  <slot1>/renderer_zone_load.cpp <slot1>/renderer_zone_lifecycle.cpp`:
  `renderer_zone_load.cpp` **WARN** (2,673 lines, over the 1,500-line
  ceiling but well under the 3,000-line FAIL ceiling — accepted per the
  plan's explicit criteria); `renderer_zone_lifecycle.cpp` **ok** (705
  lines).

**Deviations from plan:** none in the moved code itself (verbatim, no
reformatting, no scope creep). Judgment calls, all documented above and
consistent with the plan's own "when in doubt, leave + document"
instruction: `destroyZonePlaceholder` included (direct pair of
`buildZonePlaceholder`); `setupHCMapCollision`/`storeZoneEnvironment`/
`applyStoredZoneEnvironment` included (contiguous zone-lifecycle setup
helpers); `toggleSky`/`forceSkyType`/`isSkyEnabled`/`getSkyDebugInfo` left
(sky façade, not zone lifecycle); `createEntityRenderer` left (app-init
method called once from `initLoadingScreen`, not a per-zone-transition
method); `setWeather` included per the plan's own documented contiguity
rule. One naming-only test-verification note: the plan's named tests
`test_zoning_graphics_integration`/`DoorLoading` don't exist verbatim in
the current suite; `ComprehensiveZoneTest`/`DoorLoadingUnitTest` (the
tests that actually exercise this code path) were verified passing
instead.

**Batch G candidate:** `loadZoneSequential` remains a single ~2,617-line
method with 15 internal `// ── Step N ──` banners (Steps 0–13, with Step 8
further subdivided into 8a–8f) that already provide clear internal
navigation and natural extraction seams (EQE1 open, S3D parse, BSP
compute, region mesh build, asset/subsystem init, object placement, door
meshes, entity inline-prep-and-build, collision setup, sky/fog/weather,
environment subsystems, zone lighting, sim-worker first-frame compute,
cleanup). Decomposing these into private per-step helper methods was
explicitly out of scope for this unit (it requires new header
declarations and would break the move-only verifier's guarantee for this
commit). Recommended as a Batch G follow-up once Batch F's mechanical
TU-split phase is complete and the codebase moves into deeper refactoring.
