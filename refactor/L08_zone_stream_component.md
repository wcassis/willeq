# L08 — `ZoneStreamComponent` (family: `renderer_zone_stream.cpp` + `renderer_zone_merge.cpp` — progressive load, PVS neighbourhood, deferred objects, texture guards)

Batch: `refactor/L_renderer_component_extraction.md` (corrections 1–28).
**Runs ALONE after L07 merges** (L07 retargets `applySimulationOutput`
call sites inside these two TUs). Measured at `8f77ef89`: 1,440 lines, 15
bodies, 2 single-family members (survey; re-measure — and collect inline
accessors, K04 f).

Callers (word grep; the compiler confirms; forwarders only for a NAMED
external caller — expect none): `renderer_frame.cpp` (`buildDeferredObject`,
`processFrameProgressiveLoad`, `queueEntityPrepRequests`),
`renderer_sim_bridge.cpp` → after L07, `sim/…` (`checkProgressiveLoadingComplete`,
`getRegionPvsDepth`, `isInPvsNeighborhood`, `onPvsRegionChanged`,
`rebuildPvsNeighborhood`, `queueEntityPrepRequests` — write
`owner_.ZoneStream().X()` there), `renderer_zone_draw.cpp`,
`renderer_zone_load*.cpp` (`indexObjectMeshes`, `logAssetBuildTime`),
`renderer_init.cpp` (`ensureBoidsTextureGuard`, `ensureDoorTextureGuard`),
`overlays/overlays_hud_env.cpp` (`ensureBoidsTextureGuard` → `owner_.ZoneStream()`),
`renderer_entity_load_gate.cpp`, `irrlicht_renderer.cpp`
(`unregisterRemainingT124Guards`), `simulation_worker.cpp` and
`constrained_renderer_config.h` / `eqe1…` (`queueEntityPrepRequests`,
`processFrameProgressiveLoad` — read: comments, callback types, or real
calls from the worker thread? If the WORKER calls a renderer method, that
is a thread boundary — STOP and report the shape). `isEntityBlockingProgressiveLoad`,
`sendLoadProgress`: in-family or dead (§5 + K12 ff).

⚠ `renderer_zone_merge.h` is a real header included by three T124/T126
texture-guard tests (`test_object_node_texture_guard.cpp`,
`test_t126_equip_texture_guard.cpp`, `test_t124_texture_holder_guards.cpp`)
— read it: it may declare free functions (`swapTextureInMeshSceneNode`) that
are NOT `IrrlichtRenderer` members. Keep the header at its path (or move
it with the TU and repoint the three includes — call-site edits only,
assertions untouched); record which.

Predicted header delta: −15 declarations −2 members −banners +(scaffolding
4..6) ≈ **−12 or better**.

Territory: `include/client/graphics/zone_stream/zone_stream_component.h`
(new), `src/client/graphics/zone_stream/` (git mv of the two TUs and, if
moved, `renderer_zone_merge.h`), `irrlicht_renderer.h`, `renderer_init.cpp`
(ctor line + 2 call sites), root `CMakeLists.txt`, the new test + cmake,
the three guard tests (include path only), allowlist, `docs/module_map.md`,
CALL-SITE-ONLY edits in the renderer TUs and components named. Visual gate
+ ARM NOT skippable.

## Review

Executed in `wt/slot2` on `task/L08-zone-stream-component`, branched from
`fable` at `df0e06e6`. Every verification step passed. **The worker-thread STOP
did not fire, and neither did correction 21's text-guard clause.**

### Headline

`include/client/graphics/irrlicht_renderer.h` **1,657 -> 1,631 (-26)**;
allowlist lowered to 1,631 in the same commit. `ZoneStreamComponent` takes
**14 bodies** (the plan's survey said 15 at `8f77ef89`; re-measured at this tip
it is 14 -- see below) and the **2 single-family members** the plan predicted.
Residual on both moved TUs: **zero old code lines removed, zero moved body
lines modified**; the only added lines are the component include (both TUs) and
the out-of-line ctor/dtor (merge TU). Suite 3,701 / 3,702, the ALSA quirk the
only failure. Visual gate PASS, no golden touched. The three T124/T126 guard
tests pass unchanged.

### `renderer_zone_merge.h`: the decision is IT DID NOT MOVE

The plan flagged this header as the unit's open question. Read in full, the
answer is unambiguous: **it declares nothing that belongs to this component.**
Every name in it is namespace-scope `EQT::Graphics` --

- `ZoneMergeBuffers` (the P10 cross-region merge),
- five `TextureEvictionListener` subclasses (`RegionMeshTextureGuard`,
  `SingleTexturePointerGuard`, `EntityMeshTextureGuard`,
  `ObjectNodeTextureGuard`, `DoorTextureGuard`, `ForwardingTextureGuard`),
- the four T124 free functions (`swapTextureInMaterial`, `swapTextureInMesh`,
  `swapTextureInNodeMaterials`, `swapTextureInMeshSceneNode`),
- the `unregisterGuard()` template

-- and **not one is an `IrrlichtRenderer` member**. The plan's own suspicion
("it may declare free functions that are NOT `IrrlichtRenderer` members") is
correct for the entire file.

It therefore stays at `include/client/graphics/renderer_zone_merge.h`. Moving
it would have cost **fifteen include-path edits** (twelve renderer TUs plus the
three guard tests `test_object_node_texture_guard.cpp`,
`test_t126_equip_texture_guard.cpp`, `test_t124_texture_holder_guards.cpp`) and
stranded **twenty-odd doc-comment citations** across `renderer_draw_types.h`,
`constrained_texture_cache.h`, `door_manager.h`, `spell_visual_fx.h`,
`boids_manager.h`, `irrlicht_renderer.h` and `particle_manager.cpp`, buying
nothing. **The three guard tests' include lines are byte-identical to `fable`
and all three pass unchanged.** The TU that defines those classes moved with
the family; its header did not, and the two are not required to travel
together. Recorded as correction 33 below.

### The worker-thread STOP: measured, and it does not fire

The plan's hard STOP was "if the WORKER calls a renderer method this unit
moves, that is a thread boundary -- report the shape." Every mention outside
the renderer is **prose**:

| site | verdict |
|---|---|
| `src/client/graphics/simulation_worker.cpp:509` | comment -- "for PVS depth gating in `queueEntityPrepRequests()`" |
| `include/client/graphics/constrained_renderer_config.h:193,194` | comment describing the streaming path |
| `include/client/graphics/constrained_renderer_config.h:209` | comment citing `checkProgressiveLoadingComplete()` |
| `src/client/graphics/renderer_entity_load_gate.cpp:10,46` | comments |
| `src/client/graphics/eqe1/eqe1_archive_component.cpp:478` | comment |

**Zero calls from any thread but the render/loading thread.** K04 correction
(g) for the eighth consecutive unit: of the 32 word-grep hits for the fifteen
method names outside the family, **eleven were prose.**

Nothing about thread ownership changed. The two worker handles this family
drives -- `entityPrepWorker_` and `gpuUploadThread_` -- are `RendererContext`
core members and stayed there; the component reaches them as `ctx_.` and calls
`requestPrep()` / `updateDepths()` / `reprioritize()` from the render thread
exactly as before. The two lambdas handed across those calls
(`[this](size_t bspRegion) { return getRegionPvsDepth(bspRegion); }` and the
`reprioritize()` priority callback) kept their captures verbatim -- K11
correction (dd): a `[this]` capture is not the `this`-as-identity-key case, and
`this` is now the component, whose `getRegionPvsDepth()` is the same body.
**L07 correction 31 restated: `owner_` is the answer, not a workaround.**

### Measured vs the plan's lead

| | plan (survey at `8f77ef89`) | measured (at `df0e06e6`) |
|---|---|---|
| bodies | 15 | **14** |
| single-family members | 2 | **2** (`doorTextureGuard_`, `boidsTextureGuard_`) |
| forwarders | "expect none" | **zero** |
| header delta | -12 or better | **-26** |
| `renderer_zone_merge.h` | move or keep? | **kept at its path** |

**The 15 -> 14 body count is the only lead that missed, and it is a survey-age
artifact, not a defect.** The 14 are `indexObjectMeshes`, `buildDeferredObject`,
`processFrameProgressiveLoad`, `getRegionPvsDepth`, `rebuildPvsNeighborhood`,
`isInPvsNeighborhood`, `onPvsRegionChanged`, `queueEntityPrepRequests`,
`logAssetBuildTime`, `sendLoadProgress`, `isEntityBlockingProgressiveLoad`,
`checkProgressiveLoadingComplete` (stream TU) and `unregisterRemainingT124Guards`,
`ensureDoorTextureGuard`, `ensureBoidsTextureGuard` (merge TU). K15 correction
(pp) again: **re-measure at the unit tip, always.**

The plan's caller list was otherwise exact, including its two flagged
uncertainties resolving to "prose", and the compiler named no method the grep
had missed.

### Members: 57 referenced, 32 via `ctx_`, 23 via `owner_`, 2 moved

Measured on CODE LINES ONLY (trailing `//` stripped outside string literals,
K08 correction w), and scoped to the fourteen moved body ranges rather than the
whole file -- **that scoping is load-bearing for `renderer_zone_merge.cpp`**,
whose other 500 lines are `ZoneMergeBuffers` and the five guard classes with
their OWN `built_`, `totalBytes_`, `placeholder_`, `entities_`, `doors_`,
`objectNodes_`, `regionMeshNodes_` members. A whole-file member pass would have
rewritten six other classes' private state into `ctx_.`/`owner_.` and compiled
in some places. **K01 correction 4's "code lines only" rule has a second half
nobody had written down: in a TU that hosts more than one class, "code lines"
also means "the moving class's lines."** Recorded as correction 34.

- **32 on `RendererContext`** (`ctx_.X`): `animatedTextureManager`,
  `boidsManager`, `config`, `constrainedTextureCache`, `currentPvsRegion`,
  `currentZone`, `detailManager`, `device`, `doorManager`, `driver`,
  `entityPrepReady`, `entityPrepWorker`, `entityRenderer`, `fogEnabled`,
  `governor`, `gpuUploadThread`, `indoorRegions`, `lightingEnabled`,
  `manualZoneDrawEnabled`, `objectInSceneGraph`, `objectLights`, `objectNodes`,
  `objectPositions`, `portalOcclusionEligible`, `portalOcclusionEnabled`,
  `portalSystem`, `progressiveLoadingActive`, `regionBoundingBoxes`, `smgr`,
  `wireframeMode`, `zoneBspTree`, `zoneShader`.
- **23 shared, reached through the transition handle** (`owner_.X_`):
  `deferredObjects_`, `doorTextureRebuildQueue_`, `entityPrepScanCounter_`,
  `eqe1_`, `loadingScreenVisible_`, `objectBoundingBoxes_`,
  `objectLightInSceneGraph_`, `objectNodeTextureGuard_`, `objectRegions_`,
  `objectTextureRebuildQueue_`, `pendingTextureObjects_`, `pendingVBOUploads_`,
  `playerSpawnId_`, `portalBuildPending_`, `progressiveLoadStartTime_`,
  `pvsNeighborhood_`, `pvsNeighborhoodAnchor_`, `regionNeighbors_`,
  `regionPvsDepthMap_`, `sequentialLoadComplete_`, `spellVisualFXTextureGuard_`,
  `textureRebuildQueue_`, `treeIdentifier_`.
- **2 moved.**

Two `IrrlichtRenderer` methods that stayed behind are called as `owner_.Foo()`
(3 uses): `destroyZonePlaceholder()` (zone_lifecycle) and `isRegionPvsVisible()`
(zone_draw, twice).

**`pvsNeighborhood_` / `pvsNeighborhoodAnchor_` are the interesting member
call.** They look single-family to a body count -- only this family's four PVS
methods read or write them -- but `renderer_zone_lifecycle.cpp` clears both on
zone unload, so §Mechanism 3 keeps them on `IrrlichtRenderer` and the unit
extracting zone_lifecycle (L09) is the one that may take them. The same is true
of `regionNeighbors_`, whose builder `buildRegionNeighborMap()` is
zone_load's. K04 correction (f)/L07 correction 30 in its third variant: **a
member can look single-family to a BODY count and be shared by a TEARDOWN
path**, which is a different blind spot from an inline accessor.

**`objectNodeTextureGuard_` and `spellVisualFXTextureGuard_` stayed while their
two siblings moved.** All four are T124 eviction listeners and all four are
named by `unregisterRemainingT124Guards()`, but only two are constructed by
this family's `ensure*()` helpers; the other two are constructed in
`renderer_init.cpp` and `renderer_entities.cpp`. §Mechanism 3 splits the block
per member (L03 correction 15) and the shutdown sweep reaches the other two as
`owner_.`.

### Forwarders: zero, as predicted

Every one of the fourteen bodies is called only from inside the renderer, so
all call sites were retargeted (§Mechanism 5) and no declaration survives on
`IrrlichtRenderer`. `application.cpp`, `graphical_mode.cpp`,
`irrlicht_bridge.cpp` and `application_zone_loading.cpp` are **untouched** --
the K11 rule held with zero pressure. L05 correction 25 cost nothing.

### Call sites retargeted (32) -- computed before the first build; the compiler named no others

| file | sites | receiver |
|---|---|---|
| `src/client/graphics/renderer_zone_load.cpp` | 5 | `zoneStream_->logAssetBuildTime` |
| `src/client/graphics/renderer_zone_load_env.cpp` | 5 | `zoneStream_->logAssetBuildTime` |
| `src/client/graphics/renderer_zone_load_objects.cpp` | 4 | 3x `logAssetBuildTime`, 1x `indexObjectMeshes` |
| `src/client/graphics/renderer_zone_load_entities.cpp` | 1 | `zoneStream_->logAssetBuildTime` |
| `src/client/graphics/renderer_frame.cpp` | 3 | `processFrameProgressiveLoad`, `buildDeferredObject`, `queueEntityPrepRequests` |
| `src/client/graphics/renderer_init.cpp` | 2 + the ctor line | `ensureBoidsTextureGuard`, `ensureDoorTextureGuard` |
| `src/client/graphics/renderer_zone_draw.cpp` | 1 | `zoneStream_->getRegionPvsDepth` |
| `src/client/graphics/irrlicht_renderer.cpp` | 1 | `zoneStream_->unregisterRemainingT124Guards` |
| `src/client/graphics/sim/renderer_sim_bridge.cpp` | 3 | `owner_.ZoneStream().{isInPvsNeighborhood,rebuildPvsNeighborhood,onPvsRegionChanged}` |
| `src/client/graphics/overlays/overlays_hud_env.cpp` | 1 | `owner_.ZoneStream().ensureBoidsTextureGuard()` |
| `tests/test_progressive_load_rearm.cpp` | 10 + 1 comment + 1 `using` | `ZoneStreamComponent::isEntityBlockingProgressiveLoad` |

The two component call sites are written in K06/K07 correction (r)'s already-on-`fable`
form (`owner_.ZoneStream().Foo()`) because both target families are merged.

**`tests/test_progressive_load_rearm.cpp` is call-site-only.** The predicate is
a `static`, so the ten sites are a class-name change inside `EXPECT_FALSE`/
`EXPECT_TRUE`; **zero assertions, zero expected values and zero comments about
the T111 semantics were touched**, and all 8 tests pass unchanged. The
predicate stayed `public static` on the component for exactly the reason T111
extracted it -- no device, no scene graph, no instance.

### Correction 21 (text-matching guards): checked, does not apply

Greped `tests/` and `scripts/` for `"IrrlichtRenderer::` as a string literal.
**One hit: `tests/test_merged_help.cpp:361`, and it is L06's slash-command
matcher**, already widened to both spellings by that unit and unrelated to this
family. No guard locates a zone_stream body by owner spelling, so there was
nothing to widen and no planted break to run. `ReachabilitySweep` and
`DuplicateStateStores` both walk `include/client/**/*_component.h` by suffix
(K05 k / K09 aa), so the new header joined their input set with no file-list
edit; both pass.

### Text readers of the moved TUs (K03 c / K11 ee / correction 27)

Greped `tests/`, `tests/cmake/`, `scripts/`, `tools/`, `docs/`,
`refactor/BOARD.md` and every `CLAUDE.md` for `renderer_zone_stream` and
`renderer_zone_merge`. **No file parses either.** Outside `refactor/`:

- `docs/module_map.md` -- three rows, repointed to full repo-relative paths.
- `docs/decisions.md:312` -- bare basename in prose, still resolves;
  `DocCitations`' shape-2 rule requires a `/`, so it is not checked. Left as
  found (same call as L07's `tools/hwcaps` citations).
- `tests/cmake/`, `scripts/`, `tools/`, `BOARD.md`, every `CLAUDE.md` -- **no
  hit at all** for either basename.
- Only `CMakeLists.txt:519-520` needed repointing.

### Predicted vs measured header delta

| | lines |
|---|---|
| plan | -12 or better |
| predicted before the first edit, with L01 correction 3's +3..+6 per construct | **-27** |
| measured | **-26** (1,657 -> 1,631; `git diff --numstat` 29 added / 55 removed) |

The prediction was pessimistic by 1 line and the arithmetic is exact.
**55 removed** = 7 (the "Deferred/progressive asset loading" block: 3
declarations + 4 banner lines) + 9 (the T124 guard-helper block: 3 declarations
+ 6 banner lines) + 24 (the completion/predicate/prep-queue/reporting block: 6
declarations + 18 lines of T111 contract prose, L02 correction 11) + 2
(`getRegionPvsDepth`/`onPvsRegionChanged`) + 2 (`rebuildPvsNeighborhood`/
`isInPvsNeighborhood`) + 10 (`doorTextureGuard_` and its 9-line T124 banner) +
1 (`boidsTextureGuard_`, banner left behind with `spellVisualFXTextureGuard_`
per L03 correction 15).
**29 added** = 1 include + 3 + 2 + 5 + 1 + 3 transition notes replacing the six
deleted blocks (15) + 4 (the `ZoneStream()` accessor and its comment) + 9 (the
friend line, the member, and the seven-line note on the friendship axis and the
unchanged thread ownership).

L05 correction 24 held: **all of this unit's prose -- the
`renderer_zone_merge.h` decision, the threading statement, the four-way guard
census -- went into `zone_stream_component.h` (175 lines)**, not into the god
header, which kept a three-line pointer to it.

### Residual (§Mechanism 8)

Normalisation applied to the NEW side only, by name mapping
(`ZoneStreamComponent::` -> `IrrlichtRenderer::`; `ctx_.<name>` -> `<name>_`
for all 53 context members, longest-name-first per K04 (i); `owner_.` -> empty),
then trailing `//` comments stripped outside string literals (K08 w), blank
lines dropped, both sides compared as multisets.

`renderer_zone_stream.cpp` -> `zone_stream/renderer_zone_stream.cpp`:
**610 old code lines vs 611 new.**

```
removed (0):
added (1):
  #include "client/graphics/zone_stream/zone_stream_component.h"
```

`renderer_zone_merge.cpp` -> `zone_stream/renderer_zone_merge.cpp`:
**334 old code lines vs 338 new.**

```
removed (0):
added (4):
  #include "client/graphics/zone_stream/zone_stream_component.h"
  ZoneStreamComponent::ZoneStreamComponent(RendererContext& ctx, IrrlichtRenderer& owner)
      : ctx_(ctx), owner_(owner) {}
  ZoneStreamComponent::~ZoneStreamComponent() = default;
```

**Zero moved body lines modified, zero deleted.** A separate comment-multiset
pass confirms it: `renderer_zone_stream.cpp` has **165 `//` comment lines before
and 165 after, byte-identical**; `renderer_zone_merge.cpp` 161 -> 164, the
three new lines being the ctor/dtor note. Every historical comment survived,
including the 12-line T292 tombstone in `processFrameProgressiveLoad()` that
quotes the removed `protectedRegions_` code and the 20-line T111 rationale in
`isEntityBlockingProgressiveLoad()`. K01 correction 4 for the eighth
consecutive unit.

Both TUs keep `#include "client/graphics/irrlicht_renderer.h"` (they need the
complete type for `owner_.`), so **L01 correction 4's ~60-transitive-include
rediscovery did NOT bite** -- fifth consecutive handle-using component for
which it has not, and this unit needed no include iteration at all.

### The component header's include set, and why the ctor is out-of-line

`zone_stream_component.h` includes ONLY `renderer_context.h` plus `<chrono>`,
`<cstddef>`, `<cstdint>`, `<memory>`, `<string>`. It **deliberately does not
include `renderer_zone_merge.h`**, even though its two members are
`std::unique_ptr<DoorTextureGuard>` and `std::unique_ptr<ForwardingTextureGuard>`:
`irrlicht_renderer.h` includes this component header, so pulling the merge
header in would widen every renderer consumer's transitive include set for two
pointers. The two guard classes are forward-declared instead.

**That forced the one deviation from L07's shape: BOTH the constructor and the
destructor are declared here and defined in
`zone_stream/renderer_zone_merge.cpp`.** The first build failed with four
`invalid application of sizeof to incomplete type` errors -- and the frame
`required from here` pointed at the **inline constructor**, not the destructor:
a constructor's own unwind path destroys already-initialised members, so it
needs the complete type exactly as `~ZoneStreamComponent()` does. Out-lining
only the destructor is not enough. This is precisely the pattern
`ZoneMergeBuffers` itself documents in `renderer_zone_merge.h` for its
`buckets_`/`ranges_` incomplete nested types, applied one level up. Recorded as
correction 35.

No `IrrlichtRenderer` nested type appears in any moved signature or body
(checked all eleven: `ZoneLoadContext`, `TextureRebuildEntry`,
`StoredZoneEnvironment`, `ZoneLightData`, `FireGlowLight`, `DoorCollisionData`,
`WorldObjectVisual`, `FrameTimings`, `SceneBreakdown`, `RenderPassTimer`,
`EntityPoseState`/`CameraMode` aliases -- zero hits), so L02 correction 10 did
not arise. `DeferredObject` is namespace-scope in `renderer_types.h`.

### The test (L01 correction 9)

`tests/test_zone_stream_component.cpp`, **14 tests, all pass**, registered in
`tests/cmake/renderer_guards.cmake` next to `test_sim_bridge_component` (same
`IF(EQT_GRAPHICS AND IRRLICHT_LIBRARY)` guard, same bare never-`Init()`'d
`IrrlichtRenderer` idiom). Both binding paths are pinned by MUTATION through
public accessors only (L01 correction 6):

- **`owner_`, and the L07/L08 component boundary**:
  `RegionPvsDepthReadsTheMapSimBridgeBoundOnTheOwner`,
  `RegionPvsDepthSeesLaterWritesThroughTheSamePointer`,
  `RegionPvsDepthStaysUnknownWhenTheBindGateIsClosed` and
  `RegionPvsDepthIsUnknownWithNoBoundMap`. `regionPvsDepthMap_` is a T116
  front-buffer pointer that stayed on `IrrlichtRenderer`:
  `SimBridgeComponent::bindFrameSimulationBuffers()` is its only writer and
  `ZoneStreamComponent::getRegionPvsDepth()` is its main reader, so **these
  four are the executable statement that L07's and L08's components still share
  exactly one member, through `owner_`, with no copy in between** -- the second
  test mutates the bound buffer after binding and reads the new value back
  through the component. The fixture declares the `SimulationOutput` BEFORE the
  renderer so it is destroyed AFTER it, which is the T116 lifetime contract
  expressed as member order.
- **`ctx_`, renderer -> component**: `ProgressiveLoadGateIsTheContextFlag`
  drives `processFrameProgressiveLoad()`'s gate in both directions through
  `Context().progressiveLoadingActive` and reads it back through the renderer's
  own `isProgressiveLoadingActive()`; with the gate open the whole body runs
  (every subsystem it steps is null on a bare renderer and each is guarded) and
  ends in `checkProgressiveLoadingComplete()`, which cannot complete because
  `sequentialLoadComplete_` is false -- so the flag surviving proves the body
  ran to the end. `CompletionCheckDoesNotCompleteBeforeTheSequentialLoadDoes`
  pins that gate on its own (a completion test that completed there would be the
  T109 shape again). `IndexObjectMeshesIsANoOpWithNoZoneOnTheContext` and
  `EntityPrepQueueIsANoOpWithNoWorkerOnTheContext` cover the `ctx_` null guards.
- **The two moved members**:
  `GuardRegistrationIsIdempotentAndNeedsTheTextureCache` calls each `ensure*()`
  helper twice and then the unregister sweep twice -- the idempotence the two
  lazy-construction sites in `renderer_init.cpp` and `overlays_hud_env.cpp`
  depend on.
- **The PVS neighbourhood**: `PvsNeighborhoodIsEmptyUntilRebuilt`,
  `RebuildWithNoNeighbourMapLeavesTheNeighborhoodEmpty` and
  `PvsRegionChangedIsANoOpWithNoWorkersOnTheContext`.
- `AccessorReachesAConstructedComponent` -- the ctor wiring, without which all
  32 retargeted call sites are production null dereferences.
- `BlockingPredicateNeedsNoComponentInstance` -- the predicate is still callable
  on the class, which is what keeps `test_progressive_load_rearm.cpp` a pure
  unit test.

**Two of these tests failed on their first run and both were the test's fault,
not the move's**: `bindFrameSimulationBuffers(nullptr)` dereferences its
argument (its gate reads `persistent->regionVisible` before any null check),
and the predicate returns false for `inSceneGraph=false` regardless of the
player flag. Both were fixed in the test; **no production line changed**, and
the null-pointer dereference in `bindFrameSimulationBuffers()` is pre-existing
L07 behaviour with no production caller that can reach it (its two callers pass
`&output_[...]`), so it is noted here rather than fixed out of territory.

### §5: nothing deleted, nothing boarded

All fourteen methods have live callers (32 sites), so §Mechanism 5's
zero-caller delete never applied. K12 (ff) checked and clear anyway: no body in
either TU contains `pushEvent` or `GameEventType::`.

### Verification

| step | result |
|---|---|
| `cmake --build build -j24` | **exit 0.** Two compile iterations: the first failed with 4 `incomplete type` errors on the inline ctor/dtor (see above); after out-lining both, clean. No build was ever killed at the 600 s ceiling, so the exit-143 truncation/absence scan never applied |
| `ctest --test-dir build -j6 --output-on-failure` | **3,701 / 3,702 passed**, 1 failure: `AudioManagerTest.PreloadCommonSounds` -- the only permitted one |
| the three T124/T126 texture-guard tests | `ObjectNodeTextureGuard` 2/2, `T126EquipTextureGuard` 3/3, `T124TextureHolderGuards` 7/7 -- **all pass unchanged, include lines byte-identical to `fable`** |
| other named guards | `ProgressiveLoadRearm` 8/8 (retargeted, assertions untouched), `SimulationOutputLifetime` 7/7, `MergedHelp` 21/21, `FirstPersonCameraSingleFormula` 4/4, `FileSizeCeiling`, `DocCitations`, `ReachabilitySweep` (5), `DuplicateStateStores` -- all pass |
| `test_zone_stream_component` (new) | **14 tests, all pass** |
| `scripts/build-gles2-x86.sh` | **exit 0**, first invocation, zero errors; `build-gles2/bin/willeq` valid x86-64 ELF |
| visual gate standalone | **PASS** (`=== VISUAL GATE: PASS ===`), 3 zones + 3 memory-coverage runs, every CPU p95 under ceiling (qeynos2, gfaydark 5.574 vs 10.672, befallen 5.404 vs 10.948). `git status --porcelain` shows **no golden path** -- nothing re-blessed. This family builds the deferred object meshes, assigns their wind/alpha materials and decides which regions the PVS lets through, so the gate is the proof the move changed no pixels |
| `scripts/build-arm-noble.sh` | **exit 0**, first invocation, zero errors, `build-arm-noble/bin/willeq` valid 32-bit ARM ELF |
| `scripts/refactor/check_file_sizes.sh --all` | **853 checked, 853 ok, 0 warn, 0 FAIL**, with `irrlicht_renderer.h` lowered 1,657 -> 1,631 |
| residual multiset | **610 vs 611 (stream: removed 0, added 1) and 334 vs 338 (merge: removed 0, added 4)**; comment multiset 165 -> 165 and 161 -> 164 |

### Line counts

| file | before | after |
|---|---|---|
| `include/client/graphics/irrlicht_renderer.h` | 1,657 | **1,631** (-26) |
| `src/client/graphics/renderer_zone_stream.cpp` -> `src/client/graphics/zone_stream/renderer_zone_stream.cpp` | 873 | 874 |
| `src/client/graphics/renderer_zone_merge.cpp` -> `src/client/graphics/zone_stream/renderer_zone_merge.cpp` | 567 | 576 |
| `include/client/graphics/renderer_zone_merge.h` | 379 | 379 (unmoved, unmodified) |
| `include/client/graphics/zone_stream/zone_stream_component.h` | — | 175 (new) |
| `tests/test_zone_stream_component.cpp` | — | 221 (new) |

### Corrections to the Batch L template

**33. A family's HEADER does not have to travel with its TU.** L08's plan
treated `renderer_zone_merge.h` as a move-or-repoint decision. The right test is
not "does the TU move?" but **"does the header declare anything that belongs to
the component?"** -- and here it declared nothing at all: six namespace-scope
classes and five free functions, zero `IrrlichtRenderer` members. A header whose
contents are not part of the extraction stays where it is no matter how many of
its definitions move, and keeping it saved fifteen include edits and twenty
citations. Ask the ownership question about the DECLARATIONS, not the file.

**34. "Code lines only" (K01 correction 4) has a second half: in a multi-class
TU, it also means "the moving class's lines."** `renderer_zone_merge.cpp` hosts
`ZoneMergeBuffers` and five guard classes alongside the three
`IrrlichtRenderer` bodies this unit moved, and those classes have their own
`built_`, `placeholder_`, `entities_`, `doors_`, `objectNodes_`,
`regionMeshNodes_` members -- several sharing NAMES with `IrrlichtRenderer`
members. A member-substitution pass scoped to the file rather than to the moving
bodies would have rewritten six other classes' private state into
`ctx_.`/`owner_.` and some of it would have compiled. Scope every substitution
to the moved body RANGES, and the residual proof to the same ranges.

**35. Out-lining a component's DESTRUCTOR is not enough when a member is an
incomplete type -- the CONSTRUCTOR needs the complete type too.** L08 forward-
declared its two guard classes to keep `renderer_zone_merge.h` out of
`irrlicht_renderer.h`'s transitive set, declared `~ZoneStreamComponent()`
out-of-line, and still took four `sizeof(incomplete type)` errors whose
`required from here` frame pointed at the inline constructor: a constructor's
unwind path destroys already-initialised members. Declare both, define both in
the TU where the members are complete. Every prior L component held only
complete types, so this is the batch's first appearance of a pattern
`ZoneMergeBuffers` already documents one level down.

**36. A member can look single-family to a BODY count and be shared by a
TEARDOWN path.** `pvsNeighborhood_`/`pvsNeighborhoodAnchor_` are read and
written by nothing but this family's four PVS methods -- and cleared by
`renderer_zone_lifecycle.cpp` on zone unload, which keeps them on
`IrrlichtRenderer` under §Mechanism 3. This is a third variant of K04 (f) /
L07 (30): the first was an inline accessor hiding a member from a body count,
the second was one making a shared member look single, and this is a
constructor-free member whose only other user is the destructor path. Intersect
a single-family claim with the unload/clear TU as well as the header.
