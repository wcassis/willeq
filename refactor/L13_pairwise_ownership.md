# L13 — Pairwise ownership: the two-component members move to the component that owns them (B050 clusters 1, 2, 5; the type hoists of cluster 3)

Batch: `refactor/L_renderer_component_extraction.md` (corrections 1–53).
**Runs ALONE among renderer units.** Ruling (orchestrator, 2026-09-04): the
promotion threshold is NOT lowered to ≥2 — a 160-member `RendererContext`
renames the god class. Instead Batch K's §3 rule applies pairwise: **a
member used by two components moves to the one that WRITES it (or, if
both write, the one with more uses), and the other reaches it through
`ctx_.<Owner>().accessor()`.** That is what shrinks `irrlicht_renderer.h`
— L12's promotions left every declaration in place.

## Inputs (B050 at `58628347`)

- Cluster 1, the frame-timing blob (`frameTimings_` 200, `frameTimingsAccum_`
  135, `frameTimingsSampleCount_` 11, `lastPolygonCount_` 6, `currentFps_` 4):
  owner = `FrameComponent` (it accumulates); `DiagnosticsComponent` and
  `ZoneLifecycleComponent` read via `ctx_.Frame().timings()` (a const ref
  accessor) — measure who writes what first; `frameTimings_` is typed by
  a nested type (cluster 3) → hoist `FrameTimings` to `renderer_types.h`
  with a `using` alias in the class (L05 correction 22 shape).
- Cluster 2, 55 two-family members (`playerConfig_` 56 — player 46 / frame
  10 → player; `storedZoneEnvironment_` 37; `simulationWorker_` 26 — ⚠ the
  worker handle: NO ownership change, it stays on the renderer (L07 STOP);
  `cameraMode_` …): one decision line per member in the Review, then the
  move. Any member whose two users are on different THREADS stays (list
  them).
- Cluster 5, 16 single-component members still on the renderer (`sunLight_`
  16, `pendingGPUResults_` 6, `zoneLightsEnabled_` 5, …): move to their one
  component.
- Cluster 3 type hoists: `FireGlowLight`, `ZoneLightData` (⚠ two other
  same-named structs in the tree — namespace it or rename to
  `RendererZoneLightData`; measure the ambiguity), `RenderPassTimer` (a
  nested CLASS with a virtual base — hoist to its own header
  `render_pass_timer.h`), `DoorCollisionData`. Then the four members they
  type follow the pairwise rule.
- Clusters 4 and 6: reasoned keeps; untouched.

## Method

Per member: measure writers/readers per component (comment- and
string-filtered), decide owner, move the declaration into the owner's
header (private, with the smallest accessor the other side needs —
prefer `const T&` getters; a setter only where the other side writes),
rewrite the other side's `owner_.x_` to `ctx_.<Owner>().x()`, rewrite the
renderer's own remaining uses (init/shutdown/teardown) the same way.
Scripted, code lines only, multiset proofs per moved declaration block
are not meaningful here — the proof is: **zero behaviour change by test**,
the census before/after, and the header count.

Predicted: `irrlicht_renderer.h` 1,556 → **≈ 1,350 or better** (≈ 75
member declarations + their banners leave); `owner_.` 1,065 → **≈ 250**.
STOP: any thread/sync change; a member with cross-thread readers; a
fixture's observable changes; the visual gate diffs a golden.

## Territory

Every component header/TU under `src/client/graphics/<family>/` and
`include/client/graphics/<family>/`, `irrlicht_renderer.h`,
`renderer_context.h`, `renderer_types.h` (the hoists), `render_pass_timer.h`
(new), `src/client/graphics/renderer/*.cpp` (init/teardown call sites),
the renderer tests' call sites (assertions untouched; text guards widened
+ re-proved), allowlist, `docs/module_map.md`, `refactor/BOARD.md` (B050
re-measured). Visual gate + ARM NOT skippable.

## Review

Executed ALONE among renderer units in `wt/slot2` on `task/L13-pairwise-ownership`,
branched from `fable` at **`55de4d18`**. Every verification step passed, no
golden moved, and no thread or synchronisation was touched.

### The headline

| | before (`55de4d18`) | after | delta |
|---|---|---|---|
| `owner_.` residue across the eleven components (B050) | **1,065** | **151** | **-914 (-85.8%)** |
| `include/client/graphics/irrlicht_renderer.h` | **1,556** | **1,343** | **-213** |
| `RendererContext` members | 54 core + 44 promoted + 11 pointers | 54 core + **48** promoted + 11 pointers | +4 |
| member declarations moved out of the god class | -- | **72** into nine component headers, **4** into the context | |

Both predictions in the plan are met: `irrlicht_renderer.h` predicted
"≈ 1,350 or better" and landed **1,343**; `owner_.` predicted "≈ 250" and
landed **151**.

### Residue by B050 cluster

| # | cluster | before | after | what happened |
|---|---|---|---|---|
| 1 | the frame-timing blob (12 members) | **374** | **0** | ten members to `FrameComponent`, two to `DiagnosticsComponent` |
| 2 | two-family state (55 members) | **480** | **45** | 50 members moved; five are reasoned stays (below) |
| 3 | members typed by an `IrrlichtRenderer`-nested type (4) | **69** | **0** | the four types hoisted, then the four members promoted into `RendererContext` on L12's unchanged `>= 3` rule |
| 4 | the two EQE1 mutexes | **12** | **12** | untouched, as B050 said they would be -- a sync primitive's access path is what the second STOP forbids |
| 5 | single-component members still on the renderer (16) | **49** | **13** | ten moved; six are reasoned stays |
| 6 | `IrrlichtRenderer`'s public methods (36 names) | **81** | **81** | untouched by design -- the renderer's external API, called by `Application`, `IrrlichtBridge` and `graphical_mode` as well as by components (the K11 rule). B050 already recorded this as "not a defect" |
| | **total** | **1,065** | **151** | |

The 151 that remain are exactly **81 method uses + 70 uses in the thirteen
reasoned stays**. There is no unclassified residue.

Per component, `owner_.` before -> after: `frame` 346 -> 9, `diagnostics`
190 -> 11, `zone_lifecycle` 136 -> 32, `player` 84 -> 7, `overlays` 73 -> 7,
`zone_load` 72 -> 14, `sim` 59 -> 23, `eqe1` 49 -> 7, `slash` 37 -> 37 (all
37 are public methods -- L12 already proved this), `zone_stream` 19 -> 4,
`rdp` 0 -> 0.

Census script: `scratchpad/L13/census.py` (family list derived from
`ls include/client/graphics/*/*_component.h`, per correction 49 -- it printed
the eleven families every run), with the write/read split in `analyse.py`
and the whole decision table produced in one pass by `table2.py`.

### The four type hoists (cluster 3), done first

| type | to | why not `renderer_types.h` / what it cost |
|---|---|---|
| `FireGlowLight` | `renderer_types.h` | plain struct, no cost |
| `DoorCollisionData` | `renderer_types.h` | plain struct, no cost |
| `FrameTimings` | `renderer_types.h` | plain struct, 54 fields, no cost |
| `ZoneLightData` -> **`RendererZoneLightData`** | `renderer_types.h`, RENAMED | see below |
| `RenderPassTimer` | **new `include/client/graphics/render_pass_timer.h`** | a CLASS with a virtual base (`irr::scene::ILightManager`); `renderer_types.h` is included by `renderer_context.h`, which must stay light, so it got its own header |

Each keeps a `using` alias inside `class IrrlichtRenderer` (correction 22's
shape), so every existing spelling -- `IrrlichtRenderer::ZoneLightData`,
`::DoorCollisionData`, `::FrameTimings`, `::RenderPassTimer::` on the
out-of-line `OnRenderPassPreRender` body -- compiles untouched. **Zero call
sites had to change for the hoists themselves.**

**The `ZoneLightData` ambiguity, measured.** Two other structs of that name
exist: `EQT::ZoneLightData` (namespace scope, `include/client/zone_object_data.h`)
and the nested `SimulationZoneData::ZoneLightData` (`simulation_worker.h`).
The nested one is unreachable unqualified and irrelevant. The namespace-scope
one is the hazard, and the measured answer is that a hoist to
`EQT::Graphics::ZoneLightData` would **not be ambiguous -- it would SHADOW**:
an unqualified `ZoneLightData` written anywhere inside `namespace EQT::Graphics`
would silently stop meaning `EQT::ZoneLightData`. Today there are no such
uses (`EQT::ZoneLightData` is named only inside `zone_object_data.h` itself,
which is in `namespace EQT`, not `EQT::Graphics`), so the hoist would compile
and be correct -- and would leave a trap for the next person to write the
name. Silent shadowing is worse than ambiguity, because a compiler reports
ambiguity. Renamed to `RendererZoneLightData`; the class alias keeps
`IrrlichtRenderer::ZoneLightData` valid, so `renderer_zone_load_env.cpp`'s
`IrrlichtRenderer::ZoneLightData zld;` is unchanged.

`RenderPassTimer` was a nested class and therefore had implicit access to
`IrrlichtRenderer`'s private members (it reaches `captured3DView_`, `driver_`,
`manualZoneDrawEnabled_`, `portalOcclusionEnabled_`, `portalOcclusionEligible_`,
`ZoneLifecycle()`, and -- until this unit -- `frameTimings_` and
`have3DTransforms_`). Hoisted, it needs `friend class ::EQT::Graphics::RenderPassTimer;`
on `IrrlichtRenderer`. That is the ONE friend line this unit adds, and it buys
69 header lines. Its two reaches into moved members became
`renderer_->Frame().timings().manualZoneDraw` and
`renderer_->Frame().have3DTransforms()`.

With the types at namespace scope, all four members promoted into
`RendererContext` on L12's unchanged rule -- 69 substitutions, exactly cluster
3's 69 uses, in one pass, and the build was clean first time.

### The per-member decision table

Rule applied: **a member used by two components moves to the one that WRITES
it; if both write, to the one with more uses.** Writes were counted with a
comment- and string-filtered scanner that treats assignment, compound
assignment, `++`/`--`, a write through a field/element chain (`x_.field = v`,
`x_[i] = v`) and a mutating container call as writes. `W`/`R` below are per
component; `RENDERER` is `src/client/graphics/renderer/`'s own four TUs.

**-> `FrameComponent`** (14)

| member | uses | evidence | accessor |
|---|---|---|---|
| `frameTimings_` | 200 | frame W42/R156, zone_lifecycle W2 | `timings()` mutable -- zone_lifecycle and `RenderPassTimer` write it |
| `frameTimingsAccum_` | 135 | frame W50, diagnostics R85 | `timingsAccum()` mutable (`setFrameTimingEnabled` resets it) |
| `frameTimingsSampleCount_` | 11 | frame W2/R1, diagnostics R8 | `timingsSampleCount()` mutable |
| `frameTimingEnabled_` | 1 (+1 renderer W) | frame R1 | `frameTimingEnabled()` mutable |
| `lastPolygonCount_` | 6 | frame W1/R4, diagnostics R1 | `lastPolygonCount()` mutable |
| `currentFps_` | 4 | frame W1/R1, overlays R2 | `currentFps()` const |
| `frameCount_` | 3 | frame only | none needed |
| `lastFpsTime_` | 2 | frame W1/R1, renderer W1 | `lastFpsTime()` mutable |
| `renderCostAvgUs_` | 3 | frame W2, diagnostics R1 | `renderCostAvgUs()` const |
| `essentialSimCostAvgUs_` | 3 | frame W2, diagnostics R1 | `essentialSimCostAvgUs()` const |
| `chatInputFocused_` | 11 | frame W5/R5, player R1 | `chatInputFocused()` const |
| `have3DTransforms_` | 5 | frame R4, overlays R1, `RenderPassTimer` W | `have3DTransforms()` mutable |
| `loadingProgress_`, `loadingText_` | 1 each | frame R1 each, renderer's public `setLoadingProgress()` writes | mutable |
| `fireGlowTime_` | 3 | frame W1, eqe1 R2 | `fireGlowTime()` const |

**-> `DiagnosticsComponent`** (12): `sunLight_` 16 (diagnostics R16, renderer
W1/R5), `playerLightNode_` 19 (diagnostics R12, sim R7 -- neither writes, so
the use count decides), `zoneLightsEnabled_` 5, `debugDirectionalLightEnabled_` 4,
`debugPlayerLightEnabled_` 5, `debugObjectLightsEnabled_` 4, `portalDebugDraw_` 4,
`stencilDebugDraw_` 4, `hudEnabled_` 6, `maxObjectLights_` 6,
`sceneProfileEnabled_` 3, `sceneProfileFrameCount_` 3.

**-> `OverlaysComponent`** (12): `currentTargetId_` 12, `currentTargetName_` 5,
`characterModelView_` 10, `showMapOverlay_` 7, `mapOverlayRotation_` 5,
`mapOverlayMirrorX_` 4, `showNavmeshOverlay_` 7, `navmeshOverlayRotation_` 5,
`navmeshOverlayMirrorX_` 4, `hotkeysText_` 8, `hudText_` 7, `guienv_` 14.

**-> `PlayerCameraComponent`** (3): `playerConfig_` 56, `cameraMode_` 21,
`playerSpawnId_` 4.

**-> `ZoneLifecycleComponent`** (17): `storedZoneEnvironment_` 37,
`textureRebuildQueue_` 13, `zoneMergeBuffers_` 12, `sortedDrawEntries_` 11,
`zoneTriangleSelector_` 12, `terrainOnlySelector_` 12, `pendingTextureDoors_` 8,
`zoneCollisionNode_` 7, `mergeZoneDrawsDisabledForZone_` 4,
`mergeZoneDrawsBuildEvictionCount_` 4, `zoneMaxClip_` 4, `userRenderDistance_` 4,
`loadedEntityCount_` 3, `networkReady_` 2, `lastPlaceholderBuildPos_` 2.
Two `IrrlichtRenderer`-nested types travelled with the members they type
(correction 10): `StoredZoneEnvironment` and `TextureRebuildEntry`, both
PUBLIC on the component because `renderer_gpu_upload.cpp` names
`TextureRebuildEntry` in a sort comparator.

**-> `Eqe1ArchiveComponent`** (5): `icosphereVertices_` 8, `icosphereIndices_` 9,
`icosphereVBO_` 6, `icosphereIBO_` 5, `eqe1Index_` 4. The two icosphere
GL handles are `#ifdef EQT_HAS_DRM`, and the `#ifdef` travelled with them --
compiled ONLY by `build-arm-noble.sh` (correction 9); proved below.

**-> `SimBridgeComponent`** (3): `spellVisualFX_` 8, `playerLightLevel_` 5,
`activeLightCount_` 2.

**-> `ZoneStreamComponent`** (4): `pvsNeighborhood_` 6, `pvsNeighborhoodAnchor_` 3,
`objectNodeTextureGuard_` 1, `spellVisualFXTextureGuard_` 1. The last two
rejoin the four sibling T124 guards L08 had already put there, and
`ZoneStreamComponent::unregisterRemainingT124Guards()` -- which `shutdown()`
already calls -- was ALREADY unregistering both, so no eviction-listener
lifetime changed.

**-> `ZoneLoadComponent`** (3): `zoneLightRegions_` 10, `treeIdentifier_` 3,
`graphicsArchiveIndex_` 2.

### The stays, with reasons

Thirteen members, 70 uses. **No member with a cross-thread reader or writer
moved, and no synchronisation primitive changed its access path.**

| member | uses | reason |
|---|---|---|
| `simulationWorker_` | 26 | the worker HANDLE. L07's STOP and this plan's own input line: no ownership change. sim and zone_load reach it through `owner_` |
| `loading_` | 1 | `std::atomic<bool>`, "true while loading thread owns GL context", read through a hot inline `isLoading()` with explicit acquire/release ordering. A thread member |
| `eqe1Mutex_` | 6 | `mutable std::mutex` (B050 cluster 4, L12 deviation 3) |
| `eqe1CandidatesMutex_` | 6 | `mutable std::mutex` (same) |
| `pendingGPUResults_` | 6 | drained from the GPU upload thread |
| `pendingVBOUploads_` | 5 | the GPU upload thread's in-flight set (`#ifdef EQT_HAS_GLES2`) |
| `backgroundThreadPool_` | 2 | a thread-pool handle |
| `currentFrameSimResults_` | 7 | one of the four SimulationWorker-output pointers with the T115/T116 lifetime contract |
| `portalVisibleRegions_` | 5 | **the rule said zone_lifecycle (W1/R3 against sim's W1) and it was moved, then reverted** -- see deviation 2 |
| `forcePvsUpdate_` | 2 | a genuine three-way 1-1-1 tie: player writes it, frame writes it, `IrrlichtRenderer::setRenderDistance()` writes it. The rule gives no answer on a tie, so the honest outcome is that it stays |
| `softwareCursorTexture_` | 2 | **moved, then reverted** -- see deviation 3 |
| `initialized_` | 1 | the renderer's own lifecycle flag; init/core is a terminal owner (correction 10) |
| `cachedDisplaySettings_` | 1 | the backing store of the renderer's own public `getDisplaySettings()`; init/core terminal owner |

### Header arithmetic

| | lines |
|---|---|
| at dispatch (`fable`, `55de4d18`) | **1,556** |
| the five type hoists (6 + 8 + 7 + 55 + 69 lines of struct/class body) | **-145** |
| the five `using` aliases + their 3-line banner + the `friend` line | **+9** |
| `#include "client/graphics/render_pass_timer.h"` | **+1** |
| 72 member declarations moved to component headers | **-72** |
| eight orphaned section banners the moves emptied (`// HUD elements`, `// FPS tracking`, `// Target selection`, `// Player light source`, `// Polygon count tracking` + its dangling L11 fragment, and two L13 notes of my own) | **-14** |
| two `stays` rationale blocks written into the god header (`softwareCursorTexture_`, `portalVisibleRegions_`) | **+14** |
| net accounted | **-207** |
| **measured** | **1,343 (-213)** |
| allowlist entry | lowered **1,556 -> 1,343** in the same commit |

The 6-line difference is blank-line collapse where adjacent members left.
**Correction 40/53 was obeyed from the start**: every component header was
written before its members were substituted, and every ownership rationale
went into the OWNING component's header, not the god header. The one place
I broke my own rule -- two L13 notes explaining the `storedZoneEnvironment_`
and `textureRebuildQueue_` moves, written into `irrlicht_renderer.h` -- cost
6 lines there and was moved into `zone_lifecycle_component.h`, which is
correction 53 reproducing in miniature.

Component headers grew as expected (this is where the declarations went):
diagnostics 122->168, eqe1 159->185, frame 255->308, overlays 190->243,
player 207->229, sim 134->161, zone_lifecycle 241->330, zone_load 214->234,
zone_stream 175->202; `rdp` and `slash` unchanged at 78 and 85.

### Accessor shape

Every moved member is `private` on its owner with the SMALLEST accessor the
other side needs:

- nothing outside the owner writes -> `T const& x() const { return x_; }` only;
- something outside the owner writes -> `T& x() { return x_; }` plus the const
  overload.

A mutable-reference getter rather than a setter is what makes every rewrite a
pure receiver substitution -- `owner_.x_` becomes `ctx_.<Owner>().x()` at a read
site and at a write site alike, so no expression is restructured and no
behaviour can change. **The const overload is spelled east-const
(`T const&`, not `const T&`)**: for a pointer member `const T*&` binds the
wrong thing and does not compile, which the first build caught.

`IrrlichtRenderer`'s ten component accessors (`Frame()`, `Diagnostics()`, ...)
gained `const`. They return a non-const `<X>Component&` from a `const`
member function, which is legal because the `unique_ptr` member is what is
const, not the pointee -- and it is what lets the god header's existing
`const` inline accessors (`isFirstPersonMode()`, `getCameraMode()`,
`getCurrentTargetId()`, `isPlayerLightEnabled()`, `isNetworkReady()`,
`isFrameTimingEnabled()` and four more) keep their bodies inline. **No inline
accessor had to be de-inlined**, because `irrlicht_renderer.h` already
includes all eleven component headers -- a fact worth recording, since the
plan budgeted for de-inlining that turned out to be unnecessary.

### Text guards -- widened where needed and RE-PROVED with planted breaks

No guard's needle intersected a spelling this unit changed, so none needed
widening. That claim is worth nothing unless the guards are live over the
files this unit rewrote, so all four that read a changed file were proved
with a planted break (correction 21):

| guard | file it reads that L13 changed | planted break | result |
|---|---|---|---|
| `DebugHotkeyArmSourceInvariants.AllFourArmsExistInProcessCommonInput` | `frame/renderer_frame_input.cpp` | delete the `RA::ToggleWireframe` arm | **FAILS** |
| `UIHideSourceInvariants.StaticUiBlockIsGatedOnAllUIHidden` | `frame/renderer_frame_render.cpp` | drop `&& !allUIHidden_` from the gate | **FAILS** |
| `ScrollSourceInvariants.TheFrameRoutesTheWheelIntoTheStaticUI` | `frame/renderer_frame_input.cpp` | rename `staticUIInput_->processWheel(` | **FAILS** |
| `FirstPersonCameraSingleFormula.OnlyTheHelperTUDrivesTheCameraInFirstPerson` | `player/renderer_player_physics.cpp` | add a component-style duplicate formula (`ctx_.camera->setPosition` inside a `CameraMode::FirstPerson` branch) in a NON-helper TU | **FAILS** |

The first attempt at the fourth was planted in `renderer_camera.cpp` and the
guard PASSED -- correctly, because that file is `kHelperTU`, the one file
allowed to be both. Re-planted in `renderer_player_physics.cpp` (also changed
by this unit) and it fired. **A planted break that lands in the one location
the detector exempts proves nothing; check the exemption list before
concluding a guard is blind.**

All four breaks reverted, and the working tree was proved byte-identical to
its pre-plant state by diffing an exported `git diff` before and after
(`pre_planted.patch` vs `post_planted.patch`: **IDENTICAL**). No `git stash`
was used anywhere in this unit.

Five test files carried COMMENTS asserting that a moved member "stayed on
IrrlichtRenderer". None was a text guard and none was a fixture access -- all
compiled and passed untouched -- but they were rewritten anyway, because a
comment that says the opposite of the code is how the next unit gets misled
(`test_debug_hotkey_arms`, `test_diagnostics_component`, `test_frame_component`,
`test_irrlicht_camera_command`, `test_overlays_component`,
`test_player_camera_component`, `test_zone_lifecycle_component`).

### Verification

| step | result |
|---|---|
| `cmake --build build -j24` | **exit 0**. Nine build waves; six produced errors, every one of them named below in the deviations. Two waves hit the 600 s ceiling (exit 143) during the link of a 3.1 GB archive -- the protocol's truncation scan ran both times: 246 binaries + 2 archives, valid ELF/`ar` magic, **bad = 0**, no zero-byte file, no absent expected artifact, no stray `ar` temporary. In the first, 71 binaries had been linked after the archive completed and were deleted and relinked rather than trusted (the J07 "the rebuild lies to you" trap) |
| `ctest --test-dir build -j6 --output-on-failure` | **3,731 / 3,732 passed**; the single failure is `AudioManagerTest.PreloadCommonSounds` (the sanctioned ALSA host quirk). `FileSizeCeiling`, `DocCitations`, all `ReachabilitySweep.*`, `DuplicateStateStores.*` and `visual_gate` PASS |
| `bash scripts/build-gles2-x86.sh` | **exit 0** (run in full) |
| visual gate STANDALONE (`bash scripts/ci/visual_gate.sh`) | **=== VISUAL GATE: PASS ===**; `git status --porcelain tests/goldens` **EMPTY** -- no golden touched |
| `bash scripts/build-arm-noble.sh` | **exit 0**; `strings build-arm-noble/bin/willeq` finds **140** icosphere symbols, so the `EQT_HAS_DRM`-only `icosphereVBO_`/`icosphereIBO_` really are compiled on the component they moved to (correction 9) |
| `bash scripts/refactor/check_file_sizes.sh --all` | **860 ok, 0 warn, 0 FAIL**; `irrlicht_renderer.h` ratchet lowered to 1,343 |

An earlier full `ctest -j6` run also failed
`HeadlessShutdownTest.NonLifecycleCommandIsHeldNotDroppedOrApplied`
("`failed to open data/willeq.db (sqlite rc=14)`"). It passes standalone and
did not recur on the final run -- a `-j6` contention flake on the sqlite open,
unrelated to this unit and not a new waiver.

### Deviations and corrections

1. **A NAME COLLISION nearly landed, and only a compile error caught it.**
   `chatInputFocused_` moved to `FrameComponent`, so the renderer's own TUs
   had their bare `chatInputFocused_` rewritten to `Frame().chatInputFocused()`
   -- and one of those hits was in `RendererEventReceiver::OnEvent`, which has
   its OWN `std::atomic<bool> chatInputFocused_`, a completely different member
   set by a different thread. The rewrite compiled to "`Frame` was not declared
   in this scope" only because `RendererEventReceiver` is not `IrrlichtRenderer`;
   had the receiver been a friend or the name been on a class with an
   accessible `Frame()`, it would have silently retargeted a cross-thread flag.
   Reverted. **Rewriting a BARE member name across the owning class's own TUs
   is not a safe substitution -- it is a substitution over an unqualified
   identifier, and any other class in those TUs with the same member name is a
   silent hit.** Every one of the 46 bare-name rewrites in
   `src/client/graphics/renderer/` was then read individually (they are listed
   in the unit's scratch); the receiver was the only collision.

2. **`portalVisibleRegions_` was moved and then reverted to a stay.** The rule
   picks `ZoneLifecycleComponent` (W1/R3 against sim's W1), the move was made,
   and `tests/test_simulation_output_lifetime.cpp` -- a friend fixture -- failed
   to compile. Corrections 29/46 say to intersect the fixture's member set with
   the move set FIRST; L11 measured that intersection as empty for this fixture
   and I trusted the measurement instead of re-taking it. The right answer is
   not to retarget the fixture: `portalVisibleRegions_` is one of **four**
   SimulationWorker-output pointers (`regionPvsDepthMap_`, `particleRenderBuffer_`,
   `currentFrameSimResults_` are the others) whose shared cross-thread lifetime
   invariant is the whole subject of that fixture (T115's crash read this member
   on the next frame after the loading thread's stack had been reused). Two of
   the four are context members and one is a stay; moving the fourth onto a
   component splits exactly the group the fixture exists to keep together.
   Reverted, with the reason written into the god header beside it.

3. **`softwareCursorTexture_` was moved and then reverted to a stay.** It is
   single-family (frame R2) and cluster 5 says move it -- but
   `renderer_init.cpp` binds a REFERENCE to it into a
   `SingleTexturePointerGuard` that the texture cache holds as an eviction
   listener. Moving the referent into `FrameComponent` makes the guard's
   validity depend on component teardown order, which is a lifetime question a
   purely structural unit must not open. Reverted; correction 10's init/core
   terminal owner, and the reason is in the god header.
   The contrast with `objectNodeTextureGuard_`/`spellVisualFXTextureGuard_`,
   which DID move, is the point: there the GUARD moved, not its referent, and
   `ZoneStreamComponent` already had an unregister path that `shutdown()` calls.

4. **`playerConfig_` went to `PlayerCameraComponent`, against the letter of the
   rule.** Measured: player W0/R46, frame W4/R6. The writer clause says frame.
   But frame's four "writes" are two hotkey handlers toggling
   `playerConfig_.collisionEnabled` and `.collisionDebug`; nothing constructs or
   populates the config on either side (it is loaded at init), and player has
   46 of the 56 uses. Taken as player, with a mutable accessor for the two
   toggles, and B050's own input line agrees. **The writer clause identifies a
   PRODUCER; when neither side produces, it is not a tie-break, and the use
   count is.**

5. **The rule is applied per BLOCK where members are written together and read
   together, not per member.** Three blocks would otherwise have been split
   across two owners by margins of one or two uses:
   - the map-overlay block (`showMapOverlay_` 7, `mapOverlayRotation_` 5,
     `mapOverlayMirrorX_` 4): per member the rule gives overlays, frame, frame.
     Overlays draws all three and has 9 of the 16 uses; frame's writes are two
     hotkey toggles. **All three to overlays.**
   - the navmesh-overlay block, identically. **All three to overlays.**
   - the scene-profile pair (`sceneProfileEnabled_` 3, `sceneProfileFrameCount_` 3):
     diagnostics writes both, three lines apart, in the `/sceneprofile` start
     handler; frame reads the flag and increments the counter. Per member the
     rule gives diagnostics and then frame (2 uses against 1). **Both to
     diagnostics**, because splitting them puts one line of a three-line
     assignment block in a different owner.
   Recorded as correction 54.

6. **Two components needed an out-of-line ctor AND dtor** (corrections 35 and
   45, and this is the fourth and fifth sighting): `OverlaysComponent` for
   `characterModelView_` (`std::unique_ptr<eqt::ui::CharacterModelView>`) and
   `SimBridgeComponent` for `spellVisualFX_` (`std::unique_ptr<EQ::SpellVisualFX>`).
   Both types are only forward-declared in the component headers, so the
   destructor -- and the ctor's unwind path -- instantiate a deleter over an
   incomplete type. Bodies went into `overlays_component.cpp` and
   `renderer_sim_bridge.cpp`, each of which already included the complete type.

7. **`eqe1Index_` needed a mutable accessor, not a const one.** Both families
   read it by the write-detector's reckoning (eqe1 R2, zone_load R2), but
   zone_load calls a NON-CONST method on it. **A read/write census over
   assignment syntax does not see a non-const member function call**, and the
   only thing that caught it was the compiler. Any member whose type is a class
   should get a mutable accessor unless every use is provably const.

8. **The eleven-family census reproduced B050's numbers exactly** before any
   edit -- 1,065 total and all ten non-zero per-family counts identical -- which
   is what made the after-numbers trustworthy. Correction 49's family list was
   derived from the tree and printed on every run.

9. **`git checkout --` on four files to undo the planted breaks destroyed this
   unit's own edits to them**, because the branch tip is still `fable`. Caught
   immediately (the `git apply` in the same step failed on a relative path, so
   the files sat at HEAD), and restored from the patch exported before the
   plants. The lesson is the protocol's own: to stage a revert-and-prove step,
   export a patch and `git apply -R` / `git apply` it -- **do not use
   `git checkout --` either**, on a branch whose work is uncommitted, for
   exactly the reason `git stash` is banned. Proved recovered by diffing the
   before/after patches: identical.

### Corrections to the Batch L template

**54. A pairwise ownership rule is applied per BLOCK, not per member, wherever
a set of members is written together and read together.** L13 measured three
such blocks (the two overlay blocks and the scene-profile pair) where the
per-member rule split a group across two owners by a one- or two-use margin,
putting one line of a multi-line assignment block in a different component
from its neighbours. Owner = the family with the most uses ACROSS the block.
State the block rule in the plan, or the executor will either follow the
letter and produce cross-referencing pairs, or deviate silently.

**55. "Moves to the one that WRITES it" identifies a PRODUCER, and is not a
tie-break when neither side produces.** `playerConfig_` is written 4 times by
the frame family (two hotkey toggles of two bool fields) and 0 times by the
player family, which has 46 of its 56 uses; the config is populated at init by
neither. When the only writes are input-handler toggles of state a single
other family consumes, fall through to the use count. Same shape as the
overlay rotation/mirror members.

**56. Rewriting a BARE member name across the owning class's own TUs is
unqualified-identifier substitution and can silently hit another class.**
L13's `chatInputFocused_` -> `Frame().chatInputFocused()` pass hit
`RendererEventReceiver`'s own same-named `std::atomic<bool>` in
`renderer_event_receiver.cpp` -- a different member, on a different thread, in
a file that is in the renderer's own directory. A compile error caught it only
by luck of scope. **Every bare-name rewrite must be read individually, and a
member name that also exists on another class in the same TU set must be
rewritten by hand or not at all.** The `owner_.x_` and `ctx_.x` forms are safe
because the receiver disambiguates; the bare form is not.

**57. A planted break must land somewhere the detector does not exempt.**
L13's first attempt to re-prove `FirstPersonCameraSingleFormula` planted a
duplicate camera formula in `renderer_camera.cpp` and the guard PASSED --
correctly, because that file is the detector's `kHelperTU`, the one file
allowed to be both. A planted break that lands on an exemption is
indistinguishable from a blind guard. Read the detector's exemption list
before choosing the site, and prefer a file the unit actually changed.

**58. Corrections 35/45 (out-of-line ctor + dtor for a moved `unique_ptr` to
an incomplete type) fire on EVERY unit that moves such a member**, not just on
the extraction units -- L13 hit them twice while moving single members into
already-existing components whose ctors had been inline and correct for eleven
units. Add "does the move set contain a `unique_ptr` to a forward-declared
type?" to the pre-flight, and expect to de-inline the ctor as well as write a
dtor.

**59. A read/write census built on assignment syntax cannot see a non-const
member function call, and will size the accessor wrong.** `eqe1Index_` measured
as read-only on both sides and got a `const&` getter; `zone_load` calls a
mutating method on it. Give any member of CLASS type a mutable accessor unless
every use is provably const, and treat the write census as a lower bound.

**60. The const overload of a reference-returning accessor must be spelled
east-const.** `const T& x() const` is wrong the moment `T` is a pointer type
(`const irr::video::ITexture*&` binds the pointee's constness, not the
pointer's, and does not compile); `T const& x() const` is right for every `T`.
A generator that emits accessors must use the east form.
