# L10 — `ZoneLoadComponent` (family: `renderer_zone_load.cpp` + `_entities` + `_env` + `_objects` — the loading thread's build of a zone)

Batch: `refactor/L_renderer_component_extraction.md` (corrections 1–32).
**Runs ALONE after L09 merges.** Survey at `cfadb78e`: 3,268 lines, 21
bodies, 3 single-family members; re-measure at your tip (L02/L08/L09 have
since retargeted call sites inside these TUs).

⚠ **The loading thread.** `Application` starts a zone-loading thread that
owns the GL context and calls into this family (`CLAUDE.md` §"Zone
loading": `ZoneLoadSnapshot` → loading thread → GPU resources →
completion signal). Read `application_zone_loading.cpp` and
`renderer_zone_load.cpp`'s entry (`loadZoneSequential`?) BEFORE moving:
which thread calls which body, and what the completion handshake touches.
No thread-ownership change (STOP); the entry point(s) `Application` calls
keep forwarders (named callers).

The zone blob (`currentZone_`, `regionMeshNodes_`, `objectNodes_`,
`objectInSceneGraph_`, `portalSystem_`, `currentPvsRegion_`) is on the
context — nothing here takes it. `zoneShader_`, `constrainedTextureCache_`,
`doorManager_` likewise. Siblings reached: `owner_.Eqe1()` (archive
plumbing, 6 methods), `owner_.ZoneStream()` (`indexObjectMeshes`,
`logAssetBuildTime`), `owner_.ZoneLifecycle()` (`buildRegionNeighborMap`,
`createZoneMesh`, `rebuildRegionMesh`, `uploadMeshHardwareBuffers`),
`owner_.PlayerCamera()` (`extractWorldTriangles`, `rayIntersectTriangles`,
`rayTestDoorsAndObjects`, `setupMinimalZoneCollision`), `owner_.Overlays()`
(`applyEnvironmentalDisplaySettings`, `setupFog`, `drawLoadingScreen`).

Text readers: `tests/cmake/*.cmake` may name these TUs for
`gles2_zone_benchmark` (J05 found nineteen add-sites for the WLD loader —
grep); `tools/hwcaps/zone_bench_loader.cpp` mirrors production zone-load
code and names it in comments (Q02) — do not diverge; `docs/`,
`refactor/BOARD.md`, every `CLAUDE.md`.

Predicted header delta: −21 declarations −3 members −banners
+(forwarders for the loading-thread entry points, ≈ 2) +(scaffolding
4..6) ≈ **−15 or better**.

Territory: `include/client/graphics/zone_load/zone_load_component.h`
(new), `src/client/graphics/zone_load/` (git mv of the four TUs),
`irrlicht_renderer.h`, `renderer_init.cpp`, root `CMakeLists.txt` (four
lines + benchmark add-sites), the new test + cmake, allowlist,
`docs/module_map.md`, CALL-SITE-ONLY edits in `application_zone_loading.cpp`,
`irrlicht_bridge.cpp`, the renderer TUs and components named. Visual gate
+ ARM NOT skippable (this IS the zone load path the gate exercises).

STOP: a thread-ownership change; the visual gate diffs any golden.

## Review

Executed in `wt/slot2` on `task/L10-zone-load-component`, branched from `fable`
at `e8201b2f`. Every verification step passed. **The thread-ownership STOP did
not fire, no moved body needed an edit beyond the mechanism's substitutions,
and no call site outside the four TUs needed retargeting at all.**

### Headline

`include/client/graphics/irrlicht_renderer.h` **1,608 -> 1,590 (-18)**;
allowlist lowered to 1,590 in the same commit. `ZoneLoadComponent` takes **21
bodies** (the survey's 21, confirmed at this tip), **2 single-family members**
(the survey said 3) and **`ZoneLoadContext`**, the nested type that is the
parameter of all twenty step methods. Residual: three TUs are **one removed /
two added** (the `zone_load_context.h` include repointed, plus the component
include); `renderer_zone_load.cpp` is **one removed / five added**, the extra
three being the out-of-line ctor/dtor. Suite **3,722 / 3,723**, the ALSA quirk
the only failure. Visual gate PASS, no golden touched. GLES2 exit 0, ARM exit 0.

### Measured vs the plan's lead

| | plan (survey at `cfadb78e`) | measured (at `e8201b2f`) |
|---|---|---|
| lines across the four TUs | 3,268 | **3,268** (1,000 + 551 + 1,139 + 578) |
| `IrrlichtRenderer::` bodies | 21 | **21** — all moved, none deleted |
| single-family members | 3 | **2** (`loadFailed_`, `pendingZoneComputations_`) |
| forwarders | "≈ 2" | **2**, each with a named caller |
| header delta | −15 or better | **−18** (predicted −15 before the first edit; 16 added / 34 removed) |
| thread-ownership STOP | "no thread-ownership change (STOP)" | **did not fire** |
| benchmark cmake add-sites | "grep — J05 found nineteen for the WLD loader" | **ZERO.** `gles2_zone_benchmark` does not compile any of these four TUs; `tools/hwcaps/zone_bench_loader.cpp` MIRRORS them (Q02). Four add-sites total, all in root `CMakeLists.txt:503-506` |
| `tests/cmake/*.cmake` naming a TU | "may" | **none do** |

K15 correction (pp) earned its keep on the member count: the survey's third
single-family member no longer is one at this tip.

### The loading thread: read first, measured, unchanged

`src/client/application_zone_loading.cpp` and the moved
`renderer_zone_load.cpp` entry were read before a byte moved. The shape:

| step | thread | what |
|---|---|---|
| `Application::startLoadingThread()` :66 | main | extracts GL handles, `LoadingThread::releaseContext()`, `m_renderer->setLoading(true)`, starts `EQT::Graphics::LoadingThread` with a callback |
| `Application::loadZoneGraphicsOnThread(status)` :156 | **loading** | owns the GL context for its whole duration: `setLoading(false)`, `unloadZone()`, collision map, zone lines, `setupInstantScene()`, `storeZoneEnvironment()`, entity + door registration, camera, then **`m_renderer->loadZoneSequential(snap.eqClientPath, status)` at :275** |
| `hasLoadFailed()` :277 | **loading** | the failure check, immediately after; sets `status.quitRequested` and returns |
| `Application::joinLoadingThread()` / `checkLoadingComplete()` | main | joins, reacquires the GL context, reads `loadingComplete`/`quitRequested`, publishes the BSP tree |

**Every one of the 21 bodies runs on the loading thread and only there.** The
20 step methods have exactly one caller each — `loadZoneSequential()` — and
`loadZoneSequential()` has exactly one production caller, the line above. The
completion handshake is entirely Application's: the step sequence writes
`LoadingStatus` only through `ZoneLoadContext::updateProgress`
(`status.setProgress()` + `owner_.Overlays().drawLoadingScreen()`), and never
touches `loadingComplete` or `quitRequested` — pinned by
`SequentialLoadWithNoZoneFailsCleanlyThroughTheForwarder`, which asserts both
atomics are still false after a full driver run. The component is a plain
`IrrlichtRenderer` member with the same lifetime as every other L component, so
the thread that calls in is the thread that runs the bodies, exactly as before.
**Nothing about thread ownership changed; the STOP did not fire.** L07
correction 31 restated for the fourth time.

### Forwarders: two, each with a real caller named

Batch corrections 20/23/25 — never an interface, always a caller. Correction 37
re-checked: `IrrlichtRenderer`'s only base is `IZoneLoadSurface`, which declares
`initLoadingScreen()` and `initDeferredGraphics()` — neither is in this move set,
and neither name in this unit appears on `output_renderer.h`'s hierarchy.

| forwarder | real caller (receiver is an `IrrlichtRenderer*`) |
|---|---|
| `loadZoneSequential()` | `src/client/application_zone_loading.cpp:275` |
| `hasLoadFailed()` | `src/client/application_zone_loading.cpp:277` |

`hasLoadFailed()` is K03 correction (d) / K04 correction (f) in its exact shape:
an inline `irrlicht_renderer.h` accessor on a member the unit MOVES
(`loadFailed_`), invisible to every "count `IrrlichtRenderer::` bodies"
measurement, with one external call site that turns it into a forwarder rather
than a deletion. `Application` and `IrrlichtBridge` are **untouched** — the K11
rule held with zero pressure.

**§Mechanism 5 deleted nothing.** Every one of the 21 has a caller. K12 (ff)
checked anyway: **no body in any of the four TUs contains `pushEvent` or
`GameEventType::`**, so no deletion candidate could have been a sole publisher
even if one had existed.

### Members: 112 referenced, 49 via `ctx_`, 61 via `owner_`, 2 moved

Measured on CODE LINES ONLY (comments and string literals excluded by a
tokenising pass, not a regex — K01 (4) / K08 (w)) across all four TUs.

- **2 moved**: `pendingZoneComputations_` (27 references, all four TUs) and
  `loadFailed_` (6 references, three TUs). Neither is referenced by any file
  outside this family. `PendingZoneComputations` is a namespace-scope type in
  `renderer_types.h`, not a nested one, so it needed no qualification.
- **49 on `RendererContext`** (`ctx_.X`): `animatedTextureManager`,
  `boidsManager`, `camera`, `cameraController`, `config`,
  `constrainedTextureCache`, `currentHour`, `currentMinute`, `currentPvsRegion`,
  `currentZone`, `currentZoneName`, `detailManager`, `device`, `doorManager`,
  `driver`, `entityPrepReady`, `entityRenderer`, `fireEffectsEnabled`,
  `fogEnabled`, `frustumCuller`, `governor`, `indoorRegions`, `lightingEnabled`,
  `manualZoneDrawEnabled`, `objectInSceneGraph`, `objectLights`, `objectNodes`,
  `objectPositions`, `occlusionCuller`, `particleManager`, `playerX`, `playerY`,
  `playerZ`, `portalOcclusionEligible`, `portalOcclusionEnabled`, `portalSystem`,
  `progressiveLoadingActive`, `regionBoundingBoxes`, `regionMeshNodes`,
  `renderDistance`, `skyRenderer`, `smgr`, `treeWindController`,
  `tumbleweedManager`, `usePvsCulling`, `weatherEffects`, `wireframeMode`,
  `zoneBspTree`, `zoneShader`. The zone blob the plan named
  (`currentZone_`, `regionMeshNodes_`, `objectNodes_`, `objectInSceneGraph_`,
  `portalSystem_`, `currentPvsRegion_`) is all here, as are `zoneShader_`,
  `constrainedTextureCache_` and `doorManager_` — **this unit took none of them**,
  exactly as the plan required.
- **61 shared, reached through the transition handle** (`owner_.X_`), the
  significant ones being `pendingGlobalEQE1Archives_`, `eqe1CandidateRaces_`,
  `eqe1CandidatesMutex_`, `eqe1Index_`, `eqe1LoadTimeCandidateCount_`,
  `fireGlowLights_` and the four fire-glow flags, the six `icosphere*` members,
  `storedZoneEnvironment_`, `zoneLightData_`/`Positions_`/`Regions_`,
  `objectRegions_`, `objectBoundingBoxes_`, `objectWorldTriangles_`,
  `regionWorldTriangles_`, `doorCollisionData_`, `objectLightInSceneGraph_`,
  `zoneMeshNode_`, `fallbackMeshNode_`, `renderPassTimer_`, `simulationWorker_`,
  `weatherSystem_`, `regionNeighbors_`, `deferredObjects_`, `portalBuildPending_`,
  `sequentialLoadComplete_`, `progressiveLoadStartTime_`, `entityPrepScanCounter_`,
  the four `zoneBounds*` + `zoneBoundsValid_`, `zoneMaxClip_`, `fogThickness_`,
  `userRenderDistance_`, `isIndoorZone_`, `mergeZoneDrawsEnabled_`,
  `vertexAnimatedMeshes_`, `pendingTextureDoors_`, `pendingTextureObjects_`,
  `graphicsArchiveIndex_`, `treeIdentifier_`, `currentVision_`, `playerHeading_`,
  `playerLightLevel_`, `frameNumber_`.

Correction 36/39 checked in both directions: `regionNeighbors_` is written by
L09 and read here; `sequentialLoadComplete_` is written here and read by
L08/L09; `progressiveLoadStartTime_` and `entityPrepScanCounter_` are shared
with L08. All stay.

**Five `IrrlichtRenderer` methods that stayed behind are called as `owner_.X()`:**
`ensureEntityPrepWorkerStarted()` (1), `deferEntityForLoadGate()` (1),
`getDisplaySettings()` (1), `isRegionPvsVisible()` (2), `setRenderDistance()` (1).
The first two are the T109 load gate, defined in
`src/client/graphics/renderer_entity_load_gate.cpp` — **single-family by
reference count (their only callers are in `renderer_zone_load_entities.cpp`)
but outside this unit's named territory, so they were left where they are and
reached through the handle.** Neither touches a member this unit moved; a later
unit can fold that TU in as a pure `git mv` + two substitutions. Recorded here
rather than absorbed silently.

**Seven sibling components are reached across the boundary** through
`IrrlichtRenderer`'s accessors (K06/K07 correction (r)'s already-on-`fable`
form): `owner_.Eqe1()` (8 methods), `owner_.ZoneStream()` (2),
`owner_.ZoneLifecycle()` (4), `owner_.PlayerCamera()` (4), `owner_.Overlays()`
(3), `owner_.SimBridge()` (3), `owner_.Diagnostics()` (1) — matching the plan's
list exactly, with `SimBridge` and `Diagnostics` added (the plan named neither).

### The one compile iteration: `this` as an identity argument (correction n)

The first build failed with **one** error, in `renderer_zone_load.cpp:747`:

```
owner_.renderPassTimer_->setRenderer(this);
   error: cannot convert 'ZoneLoadComponent*' to 'IrrlichtRenderer*'
```

`RenderPassTimer::setRenderer()` takes the renderer it will call back into, so
`this` is an identity argument, not a receiver: K06/K07 correction (n)'s FOURTH
substitution, `this` -> `&owner_`. Fixed in one line; second compile clean.

**The other two `this` uses in these TUs are lambda captures**
(`renderer_zone_load.cpp:162`'s `[this](const std::string& n) { return
owner_.Eqe1().openArchiveContaining(n); }` and
`renderer_zone_load_env.cpp:377`'s `groundRaycast`) and were correctly left
alone — K11 correction (dd). Both capture the component, which is what they
must now do, since both bodies reach `owner_`/`ctx_` through it.

L01 correction 4's ~60-transitive-include rediscovery did NOT bite (seventh
consecutive handle-using component): all four TUs keep
`#include "client/graphics/irrlicht_renderer.h"` because they need the complete
type for `owner_.`, so no include iteration was needed.

### `ZoneLoadContext` moved with the family (correction 10)

It is the parameter type of all twenty step methods, so under batch correction
10 a nested type named in a moving SIGNATURE is exempt from §Mechanism 3 and
follows those signatures. Leaving it on `IrrlichtRenderer` would have forced
`zone_load_component.h` to name `IrrlichtRenderer::ZoneLoadContext` and
re-include the god header for a complete type. `zone_load_context.h` was
`git mv`'d to `include/client/graphics/zone_load/` and its one code line changed
from `struct IrrlichtRenderer::ZoneLoadContext {` to
`struct ZoneLoadComponent::ZoneLoadContext {`; nothing else in it moved, and its
include set is untouched (it still pulls `irrlicht_renderer.h`, which now
carries the component header). **Only the four moving TUs include it**, before
and after.

Note the two `ctx` spellings inside a moved body are DIFFERENT objects and
always were: the parameter `ctx` is the `ZoneLoadContext` threaded through the
step sequence, `ctx_` is the component's `RendererContext` reference. No
collision, and the substitution pass distinguished them by the trailing
underscore.

### Correction 38 (nested types in bodies): fires twice, and axis 2 is live

Two private `IrrlichtRenderer` nested types are named as local declarations
inside moved bodies, which no `owner_.` census can see:

| site | spelling |
|---|---|
| `renderer_zone_load_env.cpp:516` (`loadZoneStep11_Env`) | `FireGlowLight e;` -> `IrrlichtRenderer::FireGlowLight e;` |
| `renderer_zone_load_env.cpp:787` (`loadZoneStep12_Lights`) | `ZoneLightData zld;` -> `IrrlichtRenderer::ZoneLightData zld;` |

Both are the third substitution (K05 (j)). **They make axis 2 of K13 correction
(ii) live for this component independently of the 61 `owner_.` members**, so
L12's census must count them. Batch correction 38 for the second consecutive
unit — and this time in plain local declarations rather than a lambda parameter
list, which is a wider shape than L09's finding suggested.

### Call sites retargeted: zero outside the four TUs

Computed mechanically before the first build and confirmed by the compiler.
`loadZoneSequential` and `hasLoadFailed` are the only names any other file
mentions, and both kept forwarders, so **not one line outside
`src/client/graphics/zone_load/` needed a call-site edit** — no test, no bridge,
no `Application`. `renderer_init.cpp` gained the two-line ctor wiring and
nothing else. This is the first L unit with an empty retarget table, and it is
a property of the family: a 21-body cluster with exactly one entry point.

### Text readers of the moved TUs (K03 (c) / K11 (ee) / correction 27)

Greped `tests/`, `tests/cmake/`, `scripts/`, `tools/`, `docs/`,
`refactor/BOARD.md`, root `CMakeLists.txt` and every `CLAUDE.md` for all five
moved basenames. **No file parses any of them and no test reads any by path**,
so K11 (ee)'s empty-buffer-passes trap could not fire.

- **`refactor/BOARD.md:545` was a LIVE path citation**: B0xx's collision-gap
  entry cited `src/client/graphics/renderer_zone_load_objects.cpp:544` for the
  single `setupMinimalZoneCollision()` call. Repointed to
  `zone_load/renderer_zone_load_objects.cpp:545` (the added include shifted it
  by one) and verified against the file — the line now reads
  `owner_.PlayerCamera().setupMinimalZoneCollision();`. `DocCitations`:
  **388 ok, 0 fail, 1 skip**.
- **`tools/hwcaps` — the Q02 pairing.** `zone_bench_loader.cpp:191` and `:750`
  cite `renderer_zone_load.cpp` and `load_mem_audit.h` by BARE BASENAME, not by
  path, as the production code the benchmark mirrors. **The basenames were
  deliberately kept unchanged**, so both citations still resolve and **not one
  line of benchmark code changed** — the two copies stay byte-for-byte paired.
- **`gles2_zone_benchmark` compiles none of these TUs** (`CMakeLists.txt:1698-1763`
  lists `tools/hwcaps/*` plus the loader/geometry/texture sources; the four
  zone-load TUs are not among them). The plan's "benchmark add-sites" concern
  therefore had nothing to hit — the four add-sites are all in the `willeq_core`
  list at `CMakeLists.txt:503-506`. `tests/cmake/*.cmake` names none of them.
- `docs/module_map.md` — four TU rows rewritten with full repo-relative paths
  and no "(was …)" prose (K05 correction (l)), plus a new `ZoneLoadComponent`
  row in the component table naming both headers.
- `src/client/graphics/CLAUDE.md` and `docs/decisions.md` mention
  `renderer_zone_load.cpp` and `loadZoneSequential` by bare name only; both
  remain accurate and were left alone.

### Correction 21 (text-matching guards): checked, does not apply

Greped `tests/` and `scripts/refactor/` for `IrrlichtRenderer::` as a string
literal. Two hits, both `tests/test_merged_help.cpp:361,378` — L06's
slash-command matcher, already widened to both spellings by that unit.
`test_first_person_camera_single_formula.cpp` names `renderer_camera.cpp` and
`renderer_player_physics.cpp`, neither of which is in this move set (batch
correction 29's intersect-the-sets rule). **No guard locates a zone_load body by
owner spelling**, so there was nothing to widen and no planted break to run.
`ReachabilitySweep` and `DuplicateStateStores` both walk
`include/client/**/*_component.h` by suffix (K05 (k) / K09 (aa)), so
`zone_load_component.h` joined their input set with no file-list edit; both pass.

### Predicted vs measured header delta

| | lines |
|---|---|
| plan | −15 or better |
| predicted before the first edit, at +3..+6 per construct (L01 correction 3) | **−15** |
| measured | **−18** (1,608 -> 1,590; `git diff --numstat` 16 added / 34 removed) |

Exact arithmetic. **34 removed** = 5 (the step-helper block's banner) + 1
(`struct ZoneLoadContext;`) + 8 (steps 0-8) + 12 (steps 8a-13) + 5 (the
`loadZoneSequential` 3-line comment and the two entry declarations) + 3 (the
zone-computations comment, `pendingZoneComputations_`, `loadFailed_`).
**16 added** = 1 include + 4 (the two forwarders under a 2-line note) + 3 (the
`ZoneLoad()` accessor, its comment and a blank) + 2 (the step-block transition
note) + 1 (the member transition note) + 5 (the friend block: blank, 2-line
note, `friend class`, member).

**Batch correction 40 held and was the whole margin.** A first pass that put the
threading statement, the forwarder table and the sibling-accessor list in
`irrlicht_renderer.h` would have measured roughly **+15**; every line of that
argument went into `zone_load_component.h` (**198 lines**) instead, and the god
header kept one- and two-line pointers. Nothing about the code differs.

### Residual (§Mechanism 8)

Normalisation applied to the NEW side only, by name mapping
(`ZoneLoadComponent::` -> `IrrlichtRenderer::`;
`owner_.<Family>().` -> `<family>_->` for the seven sibling accessors;
`owner_.<method>(` -> `<method>(` for the five that stayed;
`IrrlichtRenderer::FireGlowLight`/`ZoneLightData` -> bare;
`ctx_.<name>` -> `<name>_` for all 49 context members, longest-name-first per
K04 (i); `setRenderer(&owner_)` -> `setRenderer(this)`; `owner_.` -> empty),
then comments stripped by a tokenising pass that respects string literals
(K08 (w)), blank lines dropped, both sides compared as multisets.

`renderer_zone_load.cpp`: **746 old code lines vs 750 new.**

```
removed (1):
  #include "client/graphics/zone_load_context.h"
added (5):
  #include "client/graphics/zone_load/zone_load_component.h"
  #include "client/graphics/zone_load/zone_load_context.h"
  IrrlichtRenderer::ZoneLoadComponent(RendererContext& ctx, IrrlichtRenderer& owner)
      : ctx_(ctx), owner_(owner) {}
  IrrlichtRenderer::~ZoneLoadComponent() = default;
```

`renderer_zone_load_entities.cpp`: **406 old vs 407 new.**
`renderer_zone_load_env.cpp`: **962 old vs 963 new.**
`renderer_zone_load_objects.cpp`: **495 old vs 496 new.**

```
removed (1) in each:
  #include "client/graphics/zone_load_context.h"
added (2) in each:
  #include "client/graphics/zone_load/zone_load_component.h"
  #include "client/graphics/zone_load/zone_load_context.h"
```

(the two `IrrlichtRenderer::ZoneLoadComponent` lines are the normaliser's own
name mapping applied to the new ctor/dtor, not a real spelling.)

**Zero moved body lines modified, zero added, zero deleted.** The comment
multiset confirms it: `renderer_zone_load.cpp` **161 -> 164** (the three-line
ctor/dtor note), `renderer_zone_load_entities.cpp` **94 -> 94**,
`renderer_zone_load_env.cpp` **101 -> 101**, `renderer_zone_load_objects.cpp`
**35 -> 35**. Every historical comment survived — the T199 spawn-race archive
argument, the T209 sub-step timing note, the T38 missing-model rationale, the
P04 memory-audit note, the T214 request-log note and the P32 collapsed-`#ifdef`
notes included. K01 correction 4 for the tenth consecutive unit.

### The component header's include set, and why the ctor is out-of-line

`zone_load_component.h` includes **only** `renderer_context.h` (which supplies
`PendingZoneComputations` via `renderer_types.h`) plus `<memory>` and
`<string>`. `IrrlichtRenderer` and `LoadingStatus` are forward-declared; the
twenty step methods are private and take `ZoneLoadContext&`, forward-declared as
a nested `struct` and defined in the private
`zone_load/zone_load_context.h`. **Both the constructor and the destructor are
out-of-line** (batch correction 35, third appearance) in
`zone_load/renderer_zone_load.cpp`, because `pendingZoneComputations_` is a
`unique_ptr` and the constructor's unwind path destroys already-initialised
members. This is the narrowest component-header include set in the batch so far.

### The test (L01 correction 9)

`tests/test_zone_load_component.cpp`, **7 tests, all pass**, registered in
`tests/cmake/renderer_guards.cmake` next to `test_zone_lifecycle_component`
(same `IF(EQT_GRAPHICS AND IRRLICHT_LIBRARY)` guard, same bare never-`Init()`'d
`IrrlichtRenderer` idiom). Both binding paths are pinned by MUTATION through
public accessors only (L01 correction 6):

- **`SequentialLoadWithNoZoneFailsCleanlyThroughTheForwarder`** drives the real
  loading-thread entry point end to end on a bare renderer — the same call
  `application_zone_loading.cpp:275` makes — and exercises every substitution
  class in one call: `ctx_` (the `config`/`currentZoneName`/`currentZone`
  reads), `owner_` (`progressiveLoadStartTime_`, `entityPrepScanCounter_`), the
  sibling accessors (`owner_.SimBridge().stopSimulationWorker()` and
  `owner_.Overlays().drawLoadingScreen()` both run), and both moved members
  (`pendingZoneComputations_` constructed by the driver, reset by Step 1's
  failure path, which sets `loadFailed_`). With no zone name the S3D path
  resolves to `.s3d` and `loadZoneStep1_S3D()` takes its `LOG_FATAL`
  early-return — the production shape for a missing archive. It also asserts
  `status.loadingComplete` and `status.quitRequested` are still false, which is
  the executable form of the threading statement.
- **`ProgressiveLoadingIsArmedOnTheContextBeforeTheFirstStep`** is the `ctx_`
  binding: the component writes `ctx_.progressiveLoadingActive` and
  `IrrlichtRenderer::isProgressiveLoadingActive()` — the renderer's own reader of
  the same context member — sees it. Not a copy.
- **`ProgressIsReportedThroughTheLoadingStatusTheCallerOwns`** pins the one
  channel the loading thread's progress bar reads: `LoadingStatus::percent` on
  the caller's own object.
- **`LoadFailedStartsFalseThroughBothRoutes`** and
  **`TheComponentAndTheForwarderAreTheSameLoad`** pin the moved member through
  the forwarder and the component alike.
- **`AccessorReachesAConstructedComponent`** — the ctor wiring, without which
  both forwarders are production null dereferences.

**Every test passed on its first run**; no production line was changed for the
test.

### Verification

| step | result |
|---|---|
| `cmake --build build -j24` | **exit 0.** Two compile iterations: the first failed with ONE error, the `setRenderer(this)` identity argument above; after `this` -> `&owner_`, clean. No build was ever killed at the 600 s ceiling, so the exit-143 truncation/absence scan never applied |
| `ctest --test-dir build -j6 --output-on-failure` | **3,722 / 3,723 passed**, 1 failure: `AudioManagerTest.PreloadCommonSounds` — the only permitted one. 130 s |
| `test_zone_load_component` (new) | **7 tests, all pass** |
| named guards | `ReachabilitySweep`, `DuplicateStateStores`, `FileSizeCeiling`, `DocCitations`, `MergedHelp`, `FirstPersonCameraSingleFormula`, `ProgressiveLoadRearm`, `SimulationOutputLifetime`, `ZoneStreamComponentTest`, `ZoneLifecycleComponentTest`, `T124GuardsTest`, `ObjectNodeTextureGuard`, `T126EquipTextureGuard` — all pass unchanged |
| `scripts/build-gles2-x86.sh` | **exit 0**, first invocation, zero errors; `build-gles2/bin/willeq` and `bin/gles2_zone_benchmark` both linked |
| visual gate standalone | **PASS** (`=== VISUAL GATE: PASS ===`), 3 zones + 3 memory-coverage runs, every CPU p95 under ceiling (qeynos2 8.167 vs 10.108, gfaydark 5.964 vs 10.672, befallen 9.372 vs 10.948). `git status --porcelain` shows **no golden path** — nothing re-blessed. **This IS the load path the gate exercises**: every zone the gate renders is built by the 21 bodies this unit moved, so the gate is the pixel-level proof that a 3,268-line move changed nothing |
| `scripts/build-arm-noble.sh` | **exit 0**, first invocation, zero errors, `build-arm-noble/bin/willeq` valid 32-bit ARM ELF. Not skippable: `renderer_zone_load_env.cpp` carries an `#ifdef EQT_HAS_DRM` branch (lines 579-593) that only this build compiles (batch correction 9) |
| `scripts/refactor/check_file_sizes.sh --all` | **857 checked, 857 ok, 0 warn, 0 FAIL**, with `irrlicht_renderer.h` lowered 1,608 -> 1,590 |
| residual multiset | **746/750, 406/407, 962/963, 495/496** (removed 1 each, added 5/2/2/2); comment multiset 161->164, 94->94, 101->101, 35->35 |

### Line counts

| file | before | after |
|---|---|---|
| `include/client/graphics/irrlicht_renderer.h` | 1,608 | **1,590** (−18) |
| `src/client/graphics/renderer_zone_load.cpp` -> `src/client/graphics/zone_load/renderer_zone_load.cpp` | 1,000 | 1,009 |
| `src/client/graphics/renderer_zone_load_entities.cpp` -> `.../zone_load/renderer_zone_load_entities.cpp` | 551 | 552 |
| `src/client/graphics/renderer_zone_load_env.cpp` -> `.../zone_load/renderer_zone_load_env.cpp` | 1,139 | 1,140 |
| `src/client/graphics/renderer_zone_load_objects.cpp` -> `.../zone_load/renderer_zone_load_objects.cpp` | 578 | 579 |
| `include/client/graphics/zone_load_context.h` -> `include/client/graphics/zone_load/zone_load_context.h` | 89 | 93 |
| `include/client/graphics/zone_load/zone_load_component.h` | — | 198 (new) |
| `tests/test_zone_load_component.cpp` | — | 127 (new) |

### Deviations

1. **`ensureEntityPrepWorkerStarted()` and `deferEntityForLoadGate()` were NOT
   moved**, though both are single-family by reference count (their only callers
   are in `renderer_zone_load_entities.cpp`). Their TU,
   `src/client/graphics/renderer_entity_load_gate.cpp`, is not in this unit's
   named territory ("git mv of the four TUs"), so they stayed on
   `IrrlichtRenderer` and are reached as `owner_.X()`. Neither touches a member
   this unit moved, so a later unit can fold that TU in as a pure `git mv` plus
   two substitutions. Flagged, not silently absorbed.
2. **`refactor/BOARD.md:545` was edited** — a live path citation the move
   invalidated, required by correction 27. No board judgement changed.
3. **`tools/hwcaps` was NOT edited.** Unlike L09, every hwcaps citation of these
   TUs is a bare basename rather than a path, and the basenames did not change,
   so the Q02 pairing needed no repointing and no benchmark line moved.

### Corrections to the Batch L template

**41. Correction 38's nested-type substitution is not confined to lambdas — a
plain LOCAL DECLARATION of a private nested type is the commoner shape.** L09
found `IrrlichtRenderer::TextureRebuildEntry` in two lambda parameter lists and
recorded the rule in those terms. L10 hit `FireGlowLight e;` and
`ZoneLightData zld;` as ordinary locals in two step bodies — same third
substitution, same axis-2 consequence, and no lambda in sight. **The L12 census
must scan moved bodies for every `IrrlichtRenderer`-scoped nested type as a bare
identifier, not just inside lambda signatures.**

**42. K06/K07 correction (n)'s `this` -> `&owner_` and K11 correction (dd)'s
"not a lambda capture" are one rule with two halves, and a single unit can need
both.** `renderer_zone_load.cpp` has three `this` uses: one identity argument
(`renderPassTimer_->setRenderer(this)`) that MUST become `&owner_`, and two
lambda captures that MUST NOT. Applying either half alone is wrong. The
discriminator is not the syntax but the question "does the callee need the
component or the renderer?" — a capture wants the component (it is where `ctx_`
and `owner_` now live); an argument typed `IrrlichtRenderer*` wants the owner.

**43. A family whose external surface is ONE ENTRY POINT retargets nothing, and
that is the cheapest extraction shape in the batch.** L10 moved 3,268 lines and
21 bodies and edited zero call sites outside its own TUs — the 20 step methods
are private and called only by their driver, and the driver kept a forwarder for
its single caller. **When ranking the remaining units, rank by external call
sites, not by line count**: this unit was ~40% larger than L09 and cost a
fraction of the retargeting.

**44. "Benchmark add-sites" must be checked, not assumed, from the TARGET's
source list.** The plan warned that `tests/cmake/*.cmake` might name these TUs
for `gles2_zone_benchmark` (J05's nineteen WLD add-sites). It does not, and
neither does the target: `gles2_zone_benchmark` compiles `tools/hwcaps/*` plus
the loader/geometry/texture sources and **none of the four renderer zone-load
TUs** — `zone_bench_loader.cpp` MIRRORS them instead (Q02). One grep of the
`ADD_EXECUTABLE` block settles it; inheriting a sibling unit's warning without
re-measuring costs a wrong prediction.
