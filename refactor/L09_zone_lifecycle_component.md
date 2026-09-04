# L09 — `ZoneLifecycleComponent` (family: `renderer_zone_lifecycle.cpp` + `renderer_zone_draw.cpp` + `renderer_gpu_upload.cpp` — unload/ready/environment, portal draw, GPU upload thread)

Batch: `refactor/L_renderer_component_extraction.md` (corrections 1–28;
26 is why `zone_draw` and `gpu_upload`, which own no state, ride with
`zone_lifecycle`). **Runs ALONE after L08 merges.** Measured at `8f77ef89`:
2,340 lines, 28 bodies, ~4 single-family members (survey; re-measure).

Callers with a REAL external receiver (forwarders, each named):
`irrlicht_bridge.cpp` (`setExpectedEntityCount`, `setNetworkReady`,
`setWeather`, `unloadZone`), `application_zone_loading.cpp`
(`setupInstantScene`, `storeZoneEnvironment`, `unloadZone`),
`bridge_component_ui_audio.cpp` / `zone_audio_manager.cpp` / `weather_audio.cpp`
(`unloadZone`, `setWeather` — read: real calls on an `IrrlichtRenderer`
receiver, or same-named methods of other classes? `weather_system.cpp`,
`tree_wind_controller.cpp`, `weather_effects_controller.cpp` almost
certainly have their OWN `setWeather` — K10 t). `notifyEntityLoaded`,
`setExpectedEntityCount`, `setNetworkReady` are also named in
`output_renderer.h` / `console_renderer.h` / `null_renderer.h` — same-named
methods on other classes, NOT an interface (correction 13).

In-renderer callers retarget: `renderer_frame.cpp` (`drainGPUResults`,
`processCompletedUploads`, `processOneGPUResult`), `renderer_frame_input.cpp`
(`buildZonePlaceholder`, `setupHCMapCollision`), `renderer_zone_load*.cpp`
(`buildRegionNeighborMap`, `createZoneMesh`, `rebuildRegionMesh`,
`uploadMeshHardwareBuffers`, `drawZoneGeometrySorted`), `zone_stream/…`
(`destroyZonePlaceholder`, `drawZoneGeometrySorted` → `owner_.ZoneLifecycle()`),
`sim/…` (`drawZoneGeometrySorted`), `sim_visibility.cpp`
(`drawZoneGeometryWithPortals` — the simulation worker's file: if the WORKER
THREAD calls it, STOP and report), `player/…` (`rebuildRegionMesh` →
`owner_.ZoneLifecycle()`; `player_camera_component.h` names it — a comment?),
`diagnostics/…` (`drawZoneGeometrySorted`), `slash/…` (`toggleGPUUploadThread`),
`renderer_init.cpp` (`initGPUUploadThread`, `setWeather`),
`renderer_entity_load_gate.cpp` (`unloadZone`), `texture_atlas.h`
(`processCompletedUploads` — read). `applyStoredZoneEnvironment`,
`checkAndSetZoneReady`, `deleteMeshHardwareBuffers`, `drawPortalQuad`,
`drawPortalRecursive`, `drawRegionMesh`, `setZoneEnvironment`: in-family or
dead (§5 + K12 ff; `unloadZone` and `setNetworkReady` may publish — check).

⚠ `gpuUploadThread_` is under `#ifdef EQT_HAS_GLES2` and is core (in the
context, guarded). The GPU upload thread is ANOTHER THREAD: no
ownership change (STOP). `tools/hwcaps/zone_bench_*.cpp` and
`gpu_sync_bench_hooks.h` name `renderer_zone_draw.cpp` in comments — verify;
`Q02` applied a fix in both places — do not diverge them.

Predicted header delta: −28 declarations −4 members −banners +(forwarders
≈ 6) +(scaffolding 4..6) ≈ **−20 or better**.

Territory: `include/client/graphics/zone_lifecycle/zone_lifecycle_component.h`
(new), `src/client/graphics/zone_lifecycle/` (git mv of the three TUs),
`irrlicht_renderer.h`, `renderer_init.cpp`, root `CMakeLists.txt`, the new
test + cmake, allowlist, `docs/module_map.md`, CALL-SITE-ONLY edits in the
renderer TUs, components, bridge and application files named. Visual gate
+ ARM NOT skippable (portal draw IS the zone render path).

## Review

Executed in `wt/slot2` on `task/L09-zone-lifecycle-component`, branched from
`fable` at `799b1775`. Every verification step passed. **The GPU-upload /
worker-thread STOP did not fire, and no moved body needed an edit beyond the
mechanism's substitutions.**

### Headline

`include/client/graphics/irrlicht_renderer.h` **1,631 -> 1,608 (-23)**;
allowlist lowered to 1,608 in the same commit. `ZoneLifecycleComponent` takes
**25 bodies** (the plan's survey said 28 at `8f77ef89`; re-measured at this tip
there are 27 `IrrlichtRenderer::` bodies in the three TUs, **two of which were
deleted outright under §Mechanism 5** rather than moved) and **6 single-family
members** (the plan predicted ~4). Residual: `renderer_zone_draw.cpp` and
`renderer_gpu_upload.cpp` are **zero removed / one added** (the component
include); `renderer_zone_lifecycle.cpp` is **4 removed / 4 added**, the four
removed lines being exactly the two deleted empty bodies. Suite **3,715 /
3,716**, the ALSA quirk the only failure. Visual gate PASS, no golden touched.
ARM exit 0.

### Measured vs the plan's lead

| | plan (survey at `8f77ef89`) | measured (at `799b1775`) |
|---|---|---|
| lines across the three TUs | 2,340 | **2,340** (739 + 1,155 + 446) |
| `IrrlichtRenderer::` bodies | 28 | **27** — the 28th is `IrrlichtRenderer::RenderPassTimer::OnRenderPassPreRender`, a NESTED class's method, which did not move |
| bodies moved | 28 | **25** (2 deleted under §5) |
| single-family members | ~4 | **6** |
| forwarders | "expect ~6" | **6**, each with a named caller |
| header delta | −20 or better | **−23** (predicted −25 before the first edit) |
| `sim_visibility.cpp` STOP | "if the WORKER calls it, STOP" | **did not fire — the one mention is a comment** |
| `tools/hwcaps` Q02 divergence | "do not diverge" | **6 citations repointed, code untouched** |

**Re-measure at the unit tip (K15 `pp`) earned its keep twice here**: the
28th "body" was a nested class's, and two of the 27 were dead.

### The worker-thread STOP: measured, does not fire

The plan's hard STOP was `drawZoneGeometryWithPortals` in
`src/client/graphics/sim_visibility.cpp` — the simulation worker's file. Read:

| site | verdict |
|---|---|
| `src/client/graphics/sim_visibility.cpp:159` | **comment** — "masking at render time (drawZoneGeometryWithPortals) — no BFS needed" |
| `src/client/graphics/sim/renderer_sim_bridge.cpp:713` | comment |
| `src/client/graphics/diagnostics/diagnostics_debug.cpp:483,502` | comments |
| `src/client/graphics/renderer_zone_load_env.cpp:695`, `renderer_zone_load_objects.cpp:322`, `zone_stream/renderer_zone_stream.cpp:241` | comments |
| `include/client/graphics/constrained_renderer_config.h:254,269`, `renderer_zone_merge.h:37` | comments |
| `tools/hwcaps/zone_bench_loader.cpp:1225,1247,1266` | comments |

**Zero calls to `drawZoneGeometrySorted()` or `drawZoneGeometryWithPortals()`
from any thread but the render thread — in fact zero calls from anywhere except
`RenderPassTimer::OnRenderPassPreRender()`.** K04 correction (g) for the ninth
consecutive unit: of the twelve word-grep hits for the two draw entry points
outside the family, **twelve were prose**.

Nothing about thread ownership changed. `gpuUploadThread_` is a
`RendererContext` core member under `#ifdef EQT_HAS_GLES2` and stayed there;
`GPUUploadThread` still owns its own thread; `initGPUUploadThread()`,
`processCompletedUploads()`, `drainGPUResults()`, `processOneGPUResult()` and
`toggleGPUUploadThread()` drive it from the render thread through
`ctx_.gpuUploadThread` exactly as before. L07 correction 31 restated.

### `IrrlichtRenderer::RenderPassTimer` did NOT move — and it is the sole caller

`renderer_zone_draw.cpp` is a multi-class TU (batch correction 34) hosting
three anonymous-namespace free functions (`estimateRegionScreenArea`,
`computeLightEffectiveRadius`, the Q02 `textureGroupId`) and
`IrrlichtRenderer::RenderPassTimer::OnRenderPassPreRender()`. **`RenderPassTimer`
is a nested class of `IrrlichtRenderer`, is handed to Irrlicht's scene manager
as an `ILightManager`, and is torn down by the renderer**, so it stays. Its body
was left byte-identical apart from the two lines that now read
`renderer_->ZoneLifecycle().drawZoneGeometryWithPortals()` /
`...drawZoneGeometrySorted()`. Its own members (`passStart_`, `currentPass_`,
`firstPassSeen_`, `entityGPUQueryActive_`) and its
`renderer_->captured3DView_`/`captured3DProj_`/`have3DTransforms_` writes were
NOT rewritten — the substitution pass was scoped to the 25 moved body RANGES,
not to the file. Correction 34 held, and would have silently mangled this body
had it not.

The three anonymous-namespace helpers are namespace-scope and moved with the
file untouched.

### §5: two deletions, one deliberate keep

**Deleted (zero callers tree-wide, in-family included — K03 correction (a)):**

| name | shape |
|---|---|
| `notifyEntityLoaded()` | **empty body**, 4-line comment saying "kept for interface compatibility" |
| `checkAndSetZoneReady()` | **empty body**, 3-line comment saying the same |
| `areEntitiesLoaded()` | inline accessor on `entitiesLoaded_` (K04 correction (f)) |

**Their "interface compatibility" premise is false and the batch's correction 13
is now stale in the other direction.** Correction 13 says "`class
IrrlichtRenderer` has NO base class"; at this tip it reads
`class IrrlichtRenderer : public IZoneLoadSurface` (K16, board B006). But
`IZoneLoadSurface` declares exactly two methods — `initLoadingScreen()` and
`initDeferredGraphics()` — and neither is in this unit's move set, so the
correction's *conclusion* stands: `unloadZone`, `setExpectedEntityCount`,
`notifyEntityLoaded` and `setNetworkReady` in `output_renderer.h` /
`null_renderer.h` / `console_renderer.h` / `command_processor_test_fixture.h`
are same-named methods on a hierarchy `IrrlichtRenderer` is not part of.
**Measure the base class, then measure what it declares** — recorded as
correction 37.

K12 (ff) checked and clear: **no body in any of the three TUs contains
`pushEvent` or `GameEventType::`**, so neither deletion could have been a sole
publisher.

**KEPT and boarded: `applyStoredZoneEnvironment()` + `setZoneEnvironment()`.**
Both have zero external callers (`applyStoredZoneEnvironment()` has zero
callers at all; `setZoneEnvironment()`'s only caller is
`applyStoredZoneEnvironment()`), so §Mechanism 5 would delete the pair — 60
lines. It was not applied, under L03 correction 17's shape: the state they
consume **is written live** (`application_zone_loading.cpp:210` and `:320` call
`storeZoneEnvironment()` through the forwarder) **and is read live, but by a
DIFFERENT and later path** — `renderer_zone_load_env.cpp` reads
`storedZoneEnvironment_` directly at lines 232–256 and clears `.pending` at
327. So this is a **superseded duplicate of the apply path, not dead
scaffolding**, and deleting it would erase the evidence of the duplication
rather than resolve it. Board it as a measured wire-or-delete decision; the
component header carries the full argument.

**Two other dead-but-referenced findings, moved verbatim and boarded rather
than fixed out of territory:**

- `gles2Helper_` is **never constructed anywhere tree-wide**. Its only two
  references are `if (gles2Helper_) { gles2Helper_->releaseSharedResources(); }`
  inside `unloadZone()`'s
  `#if defined(EQT_HAS_DRM) && !defined(EQT_HAS_GLES2)` branch — a branch that
  can never fire. It is *referenced*, so it is not §Mechanism 3's
  zero-reference delete; it moved.
- `entitiesLoaded_` is now **write-only**: `setNetworkReady()` and
  `unloadZone()` clear it, nothing reads it, and L09 deleted its only reader
  (`areEntitiesLoaded()`, zero callers).

### Members: 106 referenced, 44 via `ctx_`, 62 via `owner_`, 6 moved

Measured on CODE LINES ONLY (trailing `//` stripped outside string literals,
K08 correction (w)) and scoped to the 25 moved body ranges (correction 34).

- **44 on `RendererContext`** (`ctx_.X`): `animatedTextureManager`,
  `boidsManager`, `cameraController`, `collisionMap`, `config`,
  `constrainedTextureCache`, `currentPvsRegion`, `currentZone`,
  `currentZoneName`, `detailManager`, `device`, `doorManager`, `driver`,
  `entityPrepReady`, `entityPrepWorker`, `entityRenderer`, `fogEnabled`,
  `frustumCuller`, `gpuUploadThread`, `indoorRegions`, `lightingEnabled`,
  `manualZoneDrawEnabled`, `objectInSceneGraph`, `objectLights`, `objectNodes`,
  `objectPositions`, `occlusionCuller`, `particleManager`, `playerX`, `playerY`,
  `portalOcclusionEligible`, `portalOcclusionEnabled`, `portalSystem`,
  `regionBoundingBoxes`, `regionMeshNodes`, `renderDistance`, `skyRenderer`,
  `smgr`, `tumbleweedManager`, `usePvsCulling`, `weatherEffects`,
  `wireframeMode`, `zoneBspTree`, `zoneShader`.
- **56 shared, reached through the transition handle** (`owner_.X_`) — the
  large ones being the four texture-rebuild queues, the four pending-texture
  sets, the five `eqe1*` members, `simBridge_`, `overlays_`, `diagnostics_`,
  `zoneMeshNode_`, `fallbackMeshNode_`, `zonePlaceholderNode_`,
  `storedZoneEnvironment_`, `zoneMergeBuffers_`, `sortedZoneDrawList_`,
  `sortedDrawEntries_`, `regionNeighbors_`, `pvsNeighborhood_`/`Anchor_`,
  `networkReady_`, `zoneReady_`, `loadedEntityCount_`, `frameTimings_`,
  `captured3DView_`/`Proj_`, `currentFrameSimResults_`, `portalVisibleRegions_`,
  `regionPvsDepthMap_`, `regionWorldTriangles_`, `objectWorldTriangles_`,
  `doorCollisionData_`, `zoneTriangleSelector_`, `terrainOnlySelector_`,
  `zoneCollisionNode_`, `collisionManager_`, `zoneLightData_`,
  `zoneLightPositions_`, `objectBoundingBoxes_`, `objectLightInSceneGraph_`,
  `occlusionCulledRegions_`, `particleRenderBuffer_`, `vertexAnimatedMeshes_`,
  `isIndoorZone_`, `zoneMaxClip_`, `userRenderDistance_`, `fogThickness_`,
  `zoneBoundsValid_`, `mergeZoneDraws*` (3), `portalBuildPending_`,
  `sequentialLoadComplete_`, `entityPrepScanCounter_`, `gles2*`,
  `lastPlaceholderBuildPos_`, `pendingGPUResults_`, `pendingVBOUploads_`,
  `pendingGlobalEQE1Archives_`.
- **6 moved**: `gpuUploadEnabled_` (with its `#ifdef EQT_HAS_GLES2` guard and
  its accessor `isGPUUploadThreadEnabled()`), `gles2Helper_` (with its
  `#if defined(EQT_HAS_DRM) && !defined(EQT_HAS_GLES2)` guard),
  `cachedPlaceholderTriangles_`, `pendingTextureRegions_`, `entitiesLoaded_`,
  `expectedEntityCount_`.

Correction 36 (the L08 teardown-path trap) was checked in reverse: **this unit
IS the teardown path** — `unloadZone()` is the reader that keeps
`pvsNeighborhood_`, `pvsNeighborhoodAnchor_` and `regionNeighbors_` on
`IrrlichtRenderer` for L08. It did NOT take them: `regionNeighbors_` is written
by this unit's `buildRegionNeighborMap()` and read by L08's
`rebuildPvsNeighborhood()`, and the two `pvsNeighborhood*` members are written
by L08 and cleared here. All three are genuinely two-family and stay under
§Mechanism 3 for L10/L11.

Four `IrrlichtRenderer` methods that stayed behind are called as
`owner_.Foo()`: `isLoading()` (11 uses), `setRenderDistance()` (2),
`clearWorldObjects()` (1), `getDisplaySettings()` (1). One component is reached
across the boundary: `owner_.ZoneStream().logAssetBuildTime()` in
`rebuildRegionMesh()` (K06/K07 correction (r)'s already-on-`fable` form).

### Forwarders: six, each with a real caller named

L05 correction 25 / batch correction 20 — every forwarder is justified by a
NAMED caller, never by an interface:

| forwarder | real callers (receiver is an `IrrlichtRenderer*`) |
|---|---|
| `unloadZone()` | `src/client/bridge/irrlicht_bridge.cpp:740`, `src/client/application_zone_loading.cpp:191`, `src/client/mode/graphical_mode.cpp:213` |
| `setupInstantScene()` | `src/client/application_zone_loading.cpp:206`, `:316` |
| `storeZoneEnvironment()` | `src/client/application_zone_loading.cpp:210`, `:320` |
| `setExpectedEntityCount()` | `src/client/bridge/irrlicht_bridge.cpp:179` |
| `setNetworkReady()` | `src/client/bridge/irrlicht_bridge.cpp:180` |
| `setWeather()` | `src/client/bridge/irrlicht_bridge.cpp:623` |

**`setWeather()` is the K10 (t) trap in its cleanest form yet.** Six OTHER
classes declare a same-named method — `WeatherAudio`, `ParticleManager`,
`TreeWindController`, `WeatherEffectsController`, `WeatherSystem` and
`WorldState` — and of the 27 word-grep hits for `setWeather` exactly **one** has
an `IrrlichtRenderer` receiver. `renderer_init.cpp:621`'s
`treeWindController_.setWeather(weather)` and
`weather_effects_controller.cpp:159-163`'s `particleManager_->setWeather(...)`
are other classes' methods and were left alone. `unloadZone()` is the same
shape: `ZoneAudioManager::unloadZone()` (called from
`bridge_component_ui_audio.cpp:593` and `zone_audio_manager.cpp:19,28`) and
`GraphicalMode::unloadZone()` are different methods on different classes; only
`GraphicalMode`'s *body* forwards to the renderer.

`Application`, `IrrlichtBridge` and `GraphicalMode` are **untouched** — the K11
rule held with zero pressure.

### Call sites retargeted (17) — computed before the first build; the compiler named one more

| file | sites | receiver |
|---|---|---|
| `src/client/graphics/renderer_init.cpp` | 1 + the ctor line | `zoneLifecycle_->initGPUUploadThread()` |
| `src/client/graphics/irrlicht_renderer.cpp` | 1 | `zoneLifecycle_->unloadZone()` (destructor path) |
| `src/client/graphics/renderer_frame.cpp` | 3 | `processCompletedUploads`, `drainGPUResults`, `processOneGPUResult` |
| `src/client/graphics/renderer_frame_input.cpp` | 2 | `buildZonePlaceholder`, `setupHCMapCollision` |
| `src/client/graphics/renderer_zone_load.cpp` | 4 | 2x `createZoneMesh`, `buildRegionNeighborMap`, `uploadMeshHardwareBuffers` |
| `src/client/graphics/zone_lifecycle/renderer_zone_draw.cpp` | 2 | `renderer_->ZoneLifecycle().drawZoneGeometry{WithPortals,Sorted}()` from `RenderPassTimer` |
| `src/client/graphics/zone_stream/renderer_zone_stream.cpp` | 1 | `owner_.ZoneLifecycle().destroyZonePlaceholder()` |
| `src/client/graphics/player/renderer_player_collision.cpp` | 1 | `owner_.ZoneLifecycle().rebuildRegionMesh()` |
| `src/client/graphics/slash/renderer_slash_commands.cpp` | 2 | `owner_.ZoneLifecycle().{toggleGPUUploadThread,isGPUUploadThreadEnabled}()` |

**Zero test files needed a call-site edit.** The one thing the grep missed and
the compiler found is described next.

### The one compile iteration: a nested type in a lambda, not in a signature

The first build failed with six errors, all one cause: two sort comparators in
`renderer_gpu_upload.cpp` take
`[](const TextureRebuildEntry& a, const TextureRebuildEntry& b)` — the **private
nested type `IrrlichtRenderer::TextureRebuildEntry`, named in a lambda BODY
rather than in a moved signature.** K05 correction (j)'s third substitution
(`Foo` -> `EverQuest::Foo` for any owner-scoped name) applies verbatim to nested
types in bodies, and L02 correction 10's "a nested type in a moving SIGNATURE is
exempt" does not reach this case at all. Fixed by qualifying both, which is a
substitution the mechanism already allows. **It also makes axis 2 of K13
correction (ii) live for this component** — the friendship is needed for the
private nested type as well as for the 56 `owner_.` members, which no `owner_.`
census would have shown. Recorded as correction 38. Second compile: clean.

L01 correction 4's ~60-transitive-include rediscovery did NOT bite (sixth
consecutive handle-using component): all three TUs keep
`#include "client/graphics/irrlicht_renderer.h"` because they need the complete
type for `owner_.`, so no include iteration was needed.

### Text readers of the moved TUs (K03 (c) / K11 (ee) / correction 27)

Greped `tests/`, `tests/cmake/`, `scripts/`, `tools/`, `docs/`,
`refactor/BOARD.md` and every `CLAUDE.md` for all three basenames. **No file
parses any of them**, and no test reads any by path — so K11 (ee)'s
empty-buffer-passes trap could not fire.

- **`refactor/BOARD.md:541` was a LIVE citation and `DocCitations` caught it**:
  B047 cites `src/client/graphics/renderer_zone_draw.cpp:197-198` for the
  comment claiming collision is set up progressively. Repointed to
  `zone_lifecycle/renderer_zone_draw.cpp:198-199` (the added include shifted it
  by one) and re-verified against the file. **This is correction 27 firing for
  real for the first time** — the checker went from 1 fail to 0.
- **`tools/hwcaps` — the Q02 pairing (six citations, three files)**:
  `gpu_sync_bench_hooks.h:28`, `zone_bench_merge.cpp:25`,
  `zone_bench_loader.cpp:986, 1011, 1056` all name `renderer_zone_draw.cpp`
  as the production copy their code mirrors (`textureGroupId()`'s FNV-1a fix,
  the projected-area estimator, the attenuation solve). **All six repointed to
  the new path; not one line of benchmark CODE changed**, so the two copies are
  still byte-for-byte paired. The basenames were deliberately kept unchanged so
  every one of these citations stays a one-token edit.
- `docs/module_map.md` — four rows, written with full repo-relative paths and
  no "(was …)" prose (K05 correction (l)).
- `CMakeLists.txt:508, 518, 526` repointed. `tests/cmake/commands.cmake` names
  none of the three.

### Correction 21 (text-matching guards): checked, does not apply

Greped `tests/` and `scripts/refactor/` for `IrrlichtRenderer::` as a string
literal. Two hits, both in `tests/test_merged_help.cpp` (lines 361, 378), and
both are **L06's slash-command matcher**, already widened to both spellings by
that unit and unrelated to this family. The `IrrlichtRenderer::CameraMode`
occurrences in `test_irrlicht_camera_command.cpp` /
`test_player_camera_component.cpp` are compiled type names, not text guards
(and L05 correction 22 left that alias in place). **No guard locates a
zone_lifecycle, zone_draw or gpu_upload body by owner spelling**, so there was
nothing to widen and no planted break to run. `ReachabilitySweep` and
`DuplicateStateStores` both walk `include/client/**/*_component.h` by suffix
(K05 (k) / K09 (aa)), so the new header joined their input set with no file-list
edit; both pass.

### Predicted vs measured header delta

| | lines |
|---|---|
| plan | −20 or better |
| predicted before the first edit, at +3..+6 per construct (L01 correction 3) | **−25** |
| measured | **−23** (1,631 -> 1,608; `git diff --numstat` 43 added / 66 removed) |

The prediction was optimistic by 2 lines and the arithmetic is exact.
**66 removed** = 12 (`unloadZone`'s comment + decl and `setZoneEnvironment`'s
6-line comment + 3-line decl) + 11 (the instant-scene/store/apply block:
4 declarations across 7 lines + 4 banner lines) + 7 (`toggleGPUUploadThread`
+ its banner + the 4-line `#ifdef` accessor pair) + 5 (the entity-state block's
4 declarations + `checkAndSetZoneReady`) + 2 (`setWeather` + banner) + 2
(`createZoneMesh`/`rebuildRegionMesh`) + 5 (`gpuUploadEnabled_` and the four
GPU-driver declarations) + 3 (`gles2Helper_` and its two `#if`/`#endif` lines)
+ 1 (`drawZoneGeometrySorted`) + 3 (the two VBO helpers + banner) + 1
(`buildRegionNeighborMap`) + 5 (the four portal-draw declarations, one of them
two lines) + 3 (`cachedPlaceholderTriangles_` + the two placeholder methods) + 3
(`pendingTextureRegions_` + its 2-line comment) + 2 (`entitiesLoaded_`,
`expectedEntityCount_`) + 1 (`buildZonePlaceholder`'s banner).
**43 added** = 1 include + 3 (the `ZoneLifecycle()` accessor + comment) + 4
(the friend line, the member and a 3-line note) + 13 (the six forwarders,
`storeZoneEnvironment` and `setupInstantScene` taking 4 and 3 lines) + 22
(fourteen one-to-three-line transition notes replacing the deleted blocks).

**L05 correction 24 held, and this unit needed it more than most**: the whole
argument — the threading statement, the RenderPassTimer decision, the six
forwarders' caller table, the `setWeather` same-name trap, the three
dead-but-referenced findings and the §5 keep — went into
`zone_lifecycle_component.h` (**241 lines**), while the god header kept
one- and two-line pointers. A first pass that put the caller table and the
friendship argument in `irrlicht_renderer.h` measured **+7 (1,638)**; moving
that prose to the component header turned it into **−23** with not one
declaration changed.

### Residual (§Mechanism 8)

Normalisation applied to the NEW side only, by name mapping
(`ZoneLifecycleComponent::` -> `IrrlichtRenderer::`;
`IrrlichtRenderer::TextureRebuildEntry` -> `TextureRebuildEntry`;
`owner_.ZoneStream().` -> `zoneStream_->`;
`renderer_->ZoneLifecycle().` -> `renderer_->`; `ctx_.<name>` -> `<name>_` for
all 54 context members, longest-name-first per K04 (i);
`owner_.<method>(` -> `<method>(` for the four that stayed; `owner_.` -> empty),
then trailing `//` comments stripped outside string literals (K08 (w)), blank
lines dropped, both sides compared as multisets.

`renderer_zone_draw.cpp` -> `zone_lifecycle/renderer_zone_draw.cpp`:
**767 old code lines vs 768 new.**

```
removed (0):
added (1):
  #include "client/graphics/zone_lifecycle/zone_lifecycle_component.h"
```

`renderer_gpu_upload.cpp` -> `zone_lifecycle/renderer_gpu_upload.cpp`:
**362 old code lines vs 363 new.**

```
removed (0):
added (1):
  #include "client/graphics/zone_lifecycle/zone_lifecycle_component.h"
```

`renderer_zone_lifecycle.cpp` -> `zone_lifecycle/renderer_zone_lifecycle.cpp`:
**502 old code lines vs 502 new.**

```
removed (4):
  void IrrlichtRenderer::checkAndSetZoneReady() {
  void IrrlichtRenderer::notifyEntityLoaded() {
  }   x2
added (4):
  #include "client/graphics/zone_lifecycle/zone_lifecycle_component.h"
  IrrlichtRenderer::ZoneLifecycleComponent(RendererContext& ctx, IrrlichtRenderer& owner)
      : ctx_(ctx), owner_(owner) {}
  IrrlichtRenderer::~ZoneLifecycleComponent() = default;
```

(the two `IrrlichtRenderer::ZoneLifecycleComponent` lines are the normaliser's
own name mapping applied to the new ctor/dtor, not a real spelling.)

**Zero moved body lines modified, zero deleted, apart from the two §5
deletions.** The comment multiset confirms it: `renderer_zone_draw.cpp` **248
`//` comment lines before and 248 after**, `renderer_gpu_upload.cpp` **42 ->
42**, `renderer_zone_lifecycle.cpp` **137 -> 130** — the seven missing lines
being exactly the 4-line and 3-line comments inside the two deleted empty
bodies. Every historical comment survived, including the P52 GPU-sync
injection-point note in `RenderPassTimer`, the T214 EQE1-retention argument in
`unloadZone()` and the Q02 `textureGroupId()` sync note. K01 correction 4 for
the ninth consecutive unit.

### The component header's include set, and why the ctor is out-of-line

`zone_lifecycle_component.h` includes `renderer_context.h` plus
`portal_system.h` (for `Portal` in `drawPortalQuad()`'s signature) and
`client/hc_map.h` (for `HCMap::Triangle`, the moved
`cachedPlaceholderTriangles_`), and the five standard headers its member types
need. It **deliberately does not include `gles2_egl_helper.h`**: the god header
pulls that one in only under
`#if defined(EQT_HAS_DRM) && !defined(EQT_HAS_GLES2)`, and including it here
unconditionally would widen every renderer consumer's transitive set — and
guarding the include would make the member's completeness configuration-
dependent. `GLES2EGLHelper` is forward-declared and **both the constructor and
the destructor are out-of-line** (batch correction 35, second appearance),
defined in `zone_lifecycle/renderer_zone_lifecycle.cpp` where
`irrlicht_renderer.h` makes it complete.

### The test (L01 correction 9)

`tests/test_zone_lifecycle_component.cpp`, **14 tests, all pass**, registered in
`tests/cmake/renderer_guards.cmake` next to `test_zone_stream_component` (same
`IF(EQT_GRAPHICS AND IRRLICHT_LIBRARY)` guard, same bare never-`Init()`'d
`IrrlichtRenderer` idiom). Both binding paths are pinned by MUTATION through
public accessors only (L01 correction 6):

- **`owner_`**: `NetworkReadyIsTheRenderersOwnFlag` writes through the component
  (`ZoneLifecycle().setNetworkReady()`) and reads back through the renderer's
  own `isNetworkReady()` — `networkReady_` stayed on `IrrlichtRenderer` because
  `renderer_entities.cpp` reads it, so this is the executable statement that the
  component writes through the handle and not into a copy.
  `NetworkNotReadyClearsTheZoneReadyFlagThroughOwner` is a second, independent
  `owner_` binding on `zoneReady_`.
- **`ctx_`**: `ZoneMeshCreationIsANoOpWithNoZoneOnTheContext`,
  `HardwareBufferHelpersRejectANullNode`,
  `WeatherIsForwardedToTheContextController` and
  `ManualZoneDrawIsANoOpWithNoDriver` drive the `ctx_` null guards.
- **The L08/L09 component boundary**:
  `NeighborMapBuildIsANoOpWithNoPortalSystem` calls this unit's
  `buildRegionNeighborMap()` (the writer of `owner_.regionNeighbors_`) and then
  L08's `rebuildPvsNeighborhood()`/`isInPvsNeighborhood()` (the readers) —
  the two components still share that one member through `owner_` with no copy.
- **The moved members**: `UploadThreadToggleFlipsTheComponentsOwnFlag` flips
  `gpuUploadEnabled_` twice and reads it back through
  `isGPUUploadThreadEnabled()` — exactly what `/upload`'s handler does two lines
  apart in `renderer_slash_commands.cpp`.
- **The §5 keep**: `StoredEnvironmentSurvivesTheForwarderAndIsApplied` keeps
  both halves of the boarded environment pair callable, so the wire-or-delete
  decision stays a one-line decision.
- **The largest body**: `UnloadZoneIsSafeOnABareRendererThroughBothRoutes`
  drives the 311-line `unloadZone()` end-to-end through both the forwarder and
  the component, which reaches ~60 members across both binding paths — the
  cheapest whole-substitution-set proof available without a device.
- `AccessorReachesAConstructedComponent` — the ctor wiring, without which all
  17 retargeted call sites and 6 forwarders are production null dereferences.

**Every test passed on its first run**; no production line was changed for the
test.

### Verification

| step | result |
|---|---|
| `cmake --build build -j24` | **exit 0.** Two compile iterations: the first failed with 6 errors, all the `TextureRebuildEntry` nested-type case above; after qualifying both comparators, clean. No build was ever killed at the 600 s ceiling, so the exit-143 truncation/absence scan never applied |
| `ctest --test-dir build -j6 --output-on-failure` | **3,715 / 3,716 passed**, 1 failure: `AudioManagerTest.PreloadCommonSounds` — the only permitted one. (An earlier run had a second failure, `DocCitations`, from `BOARD.md:541`'s stale path; fixed in-unit and re-run clean.) |
| `test_zone_lifecycle_component` (new) | **14 tests, all pass** |
| named guards | `ReachabilitySweep` 5/5, `DuplicateStateStores` 1/1, `FileSizeCeiling`, `DocCitations`, `MergedHelp` 21/21, `FirstPersonCameraSingleFormula` 4/4, `ProgressiveLoadRearm` 8/8, `SimulationOutputLifetime` 6/6, `ZoneStreamComponentTest` 14/14, `T124GuardsTest` 7/7, `ObjectNodeTextureGuard` 2/2, `T126EquipTextureGuard` 3/3 — all pass unchanged |
| `scripts/build-gles2-x86.sh` | **exit 0**, first invocation, zero errors; `build-gles2/bin/willeq` valid x86-64 ELF |
| visual gate standalone | **PASS** (`=== VISUAL GATE: PASS ===`), 3 zones + 3 memory-coverage runs, every CPU p95 under ceiling (qeynos2 7.254 vs 10.108, gfaydark 5.446 vs 10.672, befallen 5.893 vs 10.948). `git status --porcelain` shows **no golden path** — nothing re-blessed. This unit owns the entire manual zone draw, the portal/stencil recursion and the region-mesh build, so the gate is the pixel-level proof the move changed nothing |
| `scripts/build-arm-noble.sh` | **exit 0**, first invocation, zero errors, `build-arm-noble/bin/willeq` valid 32-bit ARM ELF. Not skippable: `renderer_zone_draw.cpp` and `renderer_gpu_upload.cpp` both carry `#ifdef EQT_HAS_DRM` branches that only this build compiles (batch correction 9) |
| `scripts/refactor/check_file_sizes.sh --all` | **855 checked, 855 ok, 0 warn, 0 FAIL**, with `irrlicht_renderer.h` lowered 1,631 -> 1,608 |
| residual multiset | **767/768, 362/363 and 502/502** (removed 0/0/4, added 1/1/4); comment multiset 248 -> 248, 42 -> 42, 137 -> 130 |

### Line counts

| file | before | after |
|---|---|---|
| `include/client/graphics/irrlicht_renderer.h` | 1,631 | **1,608** (−23) |
| `src/client/graphics/renderer_zone_lifecycle.cpp` -> `src/client/graphics/zone_lifecycle/renderer_zone_lifecycle.cpp` | 739 | 732 |
| `src/client/graphics/renderer_zone_draw.cpp` -> `src/client/graphics/zone_lifecycle/renderer_zone_draw.cpp` | 1,155 | 1,156 |
| `src/client/graphics/renderer_gpu_upload.cpp` -> `src/client/graphics/zone_lifecycle/renderer_gpu_upload.cpp` | 446 | 447 |
| `include/client/graphics/zone_lifecycle/zone_lifecycle_component.h` | — | 241 (new) |
| `tests/test_zone_lifecycle_component.cpp` | — | 199 (new) |

### Deviations

1. **§Mechanism 5's zero-caller delete was NOT applied to
   `applyStoredZoneEnvironment()`/`setZoneEnvironment()`** — kept under L03
   correction 17's shape and boarded. Argued in full above and in the component
   header. Flagged, not silently absorbed.
2. **`include/client/graphics/player/player_camera_component.h:22` got a
   two-line comment edit** (outside the plan's territory, which named
   call-site-only edits). Its note "there are exactly two: `isLoading()` and
   `rebuildRegionMesh()`" became factually wrong the moment `rebuildRegionMesh()`
   moved; it now says so. No code line in that file changed.
3. **`refactor/BOARD.md` and five `tools/hwcaps` comment lines were edited** —
   both are citation repointing required by correction 27 and by the plan's
   "do not diverge them" instruction. No benchmark code changed.

### Corrections to the Batch L template

**37. Correction 13's premise is stale; its conclusion is not. MEASURE THE BASE
CLASS, THEN MEASURE WHAT IT DECLARES.** L03 wrote "`class IrrlichtRenderer` has
NO base class". At `799b1775` it reads
`class IrrlichtRenderer : public IZoneLoadSurface` — K16 added it (board B006)
after correction 13 was written. L09 hit four methods
(`unloadZone`, `setExpectedEntityCount`, `notifyEntityLoaded`,
`setNetworkReady`) that ARE pure virtuals in `include/client/output/output_renderer.h`,
and a unit that stopped at "it has a base class now" would have kept two
zero-caller empty bodies as "interface overrides". The right check is two steps:
find the base, then read what the base declares. `IZoneLoadSurface` declares
exactly `initLoadingScreen()` and `initDeferredGraphics()`; `OutputRenderer` is
a hierarchy `IrrlichtRenderer` is not part of. Every remaining L unit should
re-read `zone_load_surface.h` rather than trust either wording.

**38. K05 correction (j)'s third substitution reaches a nested type named in a
BODY, and that alone makes the `friend` line's axis 2 live.** L02 correction 10
exempted a nested type in a moving SIGNATURE from §Mechanism 3 — but
`renderer_gpu_upload.cpp` names the private `IrrlichtRenderer::TextureRebuildEntry`
in two lambda parameter lists inside two bodies, which is neither a signature
nor a member and which no `owner_.` census sees. It cost six compile errors and
one qualification. **When the L12 census decides which `friend` lines can go,
axis 2 must be counted as "qualified private STATICS **and nested types**",
not just statics.**

**39. A family that owns the TEARDOWN path is the mirror of correction 36, and
must be checked not to over-claim.** L08 found that `pvsNeighborhood_` looked
single-family to a body count and was kept on `IrrlichtRenderer` because
zone_lifecycle clears it on unload. L09 IS zone_lifecycle, so the same three
members (`pvsNeighborhood_`, `pvsNeighborhoodAnchor_`, `regionNeighbors_`) now
look single-family from THIS side — `unloadZone()` is their only reader here.
They are still two-family and still stay. **A teardown-only reader is not
ownership; the member goes to whichever unit takes the last LIVE user, and
`unloadZone()`/`shutdown()` never count as that.**

**40. Put the unit's whole argument in the component header BEFORE measuring
the god-header delta.** L09's first pass put the six forwarders' caller table
and the friendship argument in `irrlicht_renderer.h` and measured **+7**; the
identical extraction with that prose in `zone_lifecycle_component.h` measured
**−23**. Nothing about the code changed — 30 lines of comment moved one file
over. L05 correction 24 said budget the prose in the component header; L09 says
it can be the entire difference between a unit that violates the ratchet and one
that beats its prediction. Write the component header first.
