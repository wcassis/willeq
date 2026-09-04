# L07 — `SimBridgeComponent` (family: `renderer_sim_bridge.cpp`, 1,113 lines, 9 bodies — the simulation-worker input/output bridge)

Batch: `refactor/L_renderer_component_extraction.md` (corrections 1–23 apply).
Wave 2, two-wide with L06. Measured at `1ea4a59c`.

Bodies and callers (word grep; the compiler confirms):
`applySimulationOutput` (`renderer_zone_stream.cpp`, `renderer_zone_merge.cpp`,
`renderer_zone_load_env.cpp`), `applySimulationResults` (`renderer_frame.cpp`,
`renderer_frame_input.cpp`; `simulation_worker.cpp` and `weather_system.h`
name it — read: a callback type or a comment), `bindFrameSimulationBuffers`
(`renderer_zone_lifecycle.cpp`, `renderer_zone_load_env.cpp`),
`buildSimulationZoneData` (`renderer_zone_load_env.cpp`),
`getSimWorkerDebugInfo` (`renderer_slash_commands.cpp` — L06's; if L06 has
merged by your tip the caller is `slash/…` and the call is
`owner_.SimBridge().getSimWorkerDebugInfo()` there), `postSimulationInput`
(`renderer_frame.cpp`, `renderer_frame_input.cpp`),
`registerSimulationWorkerVertexAnimData` / `startSimulationWorkerEarly`
(`renderer_init.cpp`), `stopSimulationWorker` (`irrlicht_renderer.cpp`,
`renderer_zone_load.cpp`, `renderer_zone_lifecycle.cpp`). All in-renderer:
zero forwarders expected (name a real external caller or there is none).

Members: 2 single-family (survey) — measure; the rest is the zone blob and
PVS/portal toggles (12 shared) reached via `ctx_`. ⚠ The simulation worker
runs on ANOTHER THREAD: `simWorker_`/`simulationWorker_` ownership, the
`SimulationOutput` lifetime (T116) and the `SimulationOutputLifetime` test
fixtures that are `friend`s of `IrrlichtRenderer` (6 of the 11 friends) —
read `include/client/graphics/simulation_worker.h` and
`tests/test_simulation_output_lifetime.cpp` BEFORE moving anything. If a
fixture reaches a member you move, the fixture is retargeted at the call
site (assertions untouched) and the friend line moves to the component; if
that changes what the test can observe, STOP and report.

Text readers: `tests/test_object_node_texture_guard.cpp:8` names the TU in
a comment — verify; `test_player_camera_component.cpp:122` likewise.

Predicted header delta: −9 declarations −2 members −banners +(scaffolding
4..6) ≈ **−8 or better**.

Territory: `include/client/graphics/sim/sim_bridge_component.h` (new),
`src/client/graphics/sim/` (git mv), `irrlicht_renderer.h`, `renderer_init.cpp`
(ctor line + 2 call sites), root `CMakeLists.txt`, `tests/test_sim_bridge_component.cpp`
+ cmake, `tests/test_simulation_output_lifetime.cpp` (call sites only, if
forced), allowlist, `docs/module_map.md`, CALL-SITE-ONLY edits in the
renderer TUs named. Visual gate + ARM NOT skippable.

STOP: a `friend` fixture's observable changes; a thread-ownership change
of any kind.

## Review

Executed in `wt/slot2` on `task/L07-sim-bridge-component`, branched from
`fable` at `c471e81b`. Every verification step passed. **Neither STOP condition
fired.**

### Headline

`include/client/graphics/irrlicht_renderer.h` **1,659 -> 1,649 (-10)**;
allowlist lowered to 1,649 in the same commit. `SimBridgeComponent` takes all
**9 bodies** and the **2 single-family members** the plan predicted. Residual
on the moved TU: **779 old code lines vs 780 new, the difference being one
include; zero moved body lines modified, zero deleted.** Build clean on the
FIRST compile, zero errors at any point in any of the three toolchains. Suite
3,678 / 3,679, the ALSA quirk the only failure. Visual gate PASS, no golden
touched.

### The two STOP conditions, and why neither fired

**1. Thread ownership.** `simulationWorker_` -- the `std::unique_ptr`, its
construction in `initLoadingScreen()`, its `start()`/`stop()` lifetime and its
double-buffered `output_[2]` -- **stayed on `IrrlichtRenderer` and is reached
through `owner_.simulationWorker_`** (20 code uses). Nothing about the
component's shape asked for anything else: the mechanism's `owner_` handle is
exactly the right tool for a member that another family also touches
(`renderer_init.cpp`, `renderer_zone_load.cpp`, `renderer_entities.cpp`,
`renderer_zone_load_env.cpp` all name it). No synchronisation primitive, no
callback, no back-pointer and no buffer lifetime changed. The component header
states this at the top so the next reader does not have to re-derive it.

**2. The `SimulationOutputLifetime` friend fixtures.** The plan's worry was that
a fixture might reach a member that moved. **It does not.** The fixtures reach
six private `IrrlichtRenderer` members -- `usePvsCulling_`,
`regionPvsDepthMap_`, `portalVisibleRegions_`, `particleRenderBuffer_`,
`currentFrameSimResults_`, `particleManager_` -- and **all six stayed**, each
under §Mechanism 3 (`usePvsCulling_` and `particleManager_` are
`RendererContext` core; the four T116 pointer members are read by
`renderer_zone_draw.cpp`, `renderer_zone_stream.cpp`,
`renderer_zone_lifecycle.cpp` and `renderer_frame_render.cpp`). The two members
that DID move (`pendingParticleEntityPositions_`/`Directions_`) are named by no
test at all.

So **the six `friend class SimulationOutputLifetimeTest_*_Test;` lines stayed
exactly where they are** and no friend line moved to the component. The only
edit to `tests/test_simulation_output_lifetime.cpp` is **call-site-only**: nine
lines change receiver from `renderer.applySimulationOutput(...)` /
`renderer.bindFrameSimulationBuffers(...)` to
`renderer.SimBridge().applySimulationOutput(...)` /
`renderer.SimBridge().bindFrameSimulationBuffers(...)`, plus a six-line comment
recording why nothing else had to change. **Zero assertions touched. Nothing
the test can observe changed** -- same production bodies, same member reads,
same sentinel-address comparisons -- so there was nothing to STOP on. All six
tests pass unchanged.

**This is the first L unit where the plan's flagged risk resolved to "measure
it and the answer is no".** The generalisable rule: a `friend` fixture is only
at risk from an extraction if it names a member the unit MOVES. Naming a member
the unit leaves behind, even one the moved bodies write, costs nothing -- the
fixture's access path and the member's address are both untouched.

### Measured vs the plan's lead

| | plan (at `1ea4a59c`) | measured (at `c471e81b`) |
|---|---|---|
| bodies | 9 | **9** |
| single-family members | 2 | **2** (`pendingParticleEntityPositions_`, `pendingParticleEntityDirections_`) |
| forwarders | "zero expected" | **zero** |
| header delta | -8 or better | **-10** |

The plan's lead was exact on every count. The nine bodies and their callers were
as listed; the compiler named no others.

**The two `simulation_worker.cpp` / `weather_system.h` mentions of
`applySimulationResults` are comments, as the plan suspected** -- verified:
`simulation_worker.cpp:474` and `weather_system.h:114,120` are prose describing
which thread applies the results, not callers. Same for the six mentions in
`renderer_frame_input.cpp`, the three in `renderer_zone_stream.cpp` and the two
in `renderer_zone_merge.cpp`. K04 correction (g) again: a caller table must be
comment-filtered, and here **21 of the 31 word-grep hits outside the family were
prose.**

### Members: 70 referenced, 40 via `ctx_`, 28 via `owner_`, 2 moved

Measured on CODE LINES ONLY (trailing `//` stripped outside string literals,
K08 correction w) -- the raw whole-file grep inflated the census by seven
members that survive only in comments (`zoneData_`, `protectedRegions_`,
`meshLoadQueue_`, `output_`, `constrainedMeshCache_`, `gpuUploadThread_`,
`entityPrepWorker_`). K01 correction 4 for the seventh consecutive unit: a
regex pass over the whole file would have rewritten every one of those
historical notes, including the T67/T292 tombstone that explains why
`protectedRegions_` is gone.

- **40 on `RendererContext`** (`ctx_.X`, 214 uses): `config`, `entityRenderer`,
  `smgr`, `zoneShader`, `regionMeshNodes`, `zoneBspTree`, `objectNodes`,
  `currentPvsRegion`, `camera`, `weatherEffects`, `currentZone`, `skyRenderer`,
  `playerX`, `playerY`, `playerZ`, `particleManager`, `detailManager`,
  `currentZoneName`, `boidsManager`, `regionBoundingBoxes`,
  `manualZoneDrawEnabled`, `progressiveLoadingActive`, `portalOcclusionEnabled`,
  `usePvsCulling`, `entityPrepReady`, `portalOcclusionEligible`,
  `cameraController`, `objectLights`, `collisionMap`, `objectInSceneGraph`,
  `portalSystem`, `renderDistance`, `frustumCuller`, `tumbleweedManager`,
  `occlusionCuller`, `indoorRegions`, `treeWindController`, `currentHour`,
  `currentMinute`, `objectPositions`.
- **28 shared, reached through the transition handle** (`owner_.X_`, 104 uses):
  `simulationWorker_`, `zoneLightData_`, `vertexAnimatedMeshes_`,
  `playerLightNode_`, `zoneLightPositions_`, `spellVisualFX_`,
  `objectLightInSceneGraph_`, `zoneReady_`, `weatherSystem_`,
  `playerLightLevel_`, `sortedZoneDrawList_`, `zoneLightRegions_`,
  `playerHeading_`, `particleRenderBuffer_`, `objectRegions_`,
  `objectBoundingBoxes_`, `maxObjectLights_`, `regionPvsDepthMap_`,
  `portalVisibleRegions_`, `occlusionCulledRegions_`, `loadingScreenVisible_`,
  `loading_`, `frameNumber_`, `debugPlayerLightEnabled_`,
  `debugObjectLightsEnabled_`, `currentVision_`, `currentFrameSimResults_`,
  `activeLightCount_`.
- **2 moved.**

Three `IrrlichtRenderer` methods that stayed behind are called as
`owner_.Foo()` (3 uses): `isInPvsNeighborhood()`, `rebuildPvsNeighborhood()` and
`onPvsRegionChanged()` -- all three PVS-neighborhood bookkeeping owned by the
zone_stream family (L08's territory), not this one.

**`loading_` is the K04 correction (f) case again, and it went the other way.**
It is referenced by no TU except this one, which makes it look single-family to
every file-level grep -- but `irrlicht_renderer.h:132-133`'s inline
`setLoading()`/`isLoading()` accessors are its real users and they have callers
all over the renderer. It stays on `IrrlichtRenderer`. **An inline accessor can
make a member look single-family as easily as it can hide one from a body
count**; L03 correction 14 and L05's `playerMovement_` are the same rule read in
the other direction, and this is the first L unit to hit that direction.

### Forwarders: zero, as predicted

Every one of the nine bodies is called only from inside the renderer, so all 11
call sites were retargeted (§Mechanism 5) and no declaration survives on
`IrrlichtRenderer`. `application.cpp`, `graphical_mode.cpp`,
`irrlicht_bridge.cpp` and `application_zone_loading.cpp` are **untouched** --
the K11 rule held with zero pressure. L05 correction 25 (drop the forwarder
column, derive it from real callers) cost nothing here because the plan already
said "zero forwarders expected"; the measurement agreed.

### Call sites retargeted (11) -- computed before the first build; the compiler named no others

| file | sites |
|---|---|
| `src/client/graphics/renderer_zone_load_env.cpp` | 2 (`buildSimulationZoneData` at Step12b, `applySimulationOutput` at Step12b) |
| `src/client/graphics/renderer_init.cpp` | 2 (`registerSimulationWorkerVertexAnimData`, `startSimulationWorkerEarly`) + the ctor line |
| `src/client/graphics/renderer_frame.cpp` | 2 (`applySimulationResults`, `postSimulationInput`) |
| `src/client/graphics/irrlicht_renderer.cpp` | 1 (`stopSimulationWorker`) |
| `src/client/graphics/renderer_zone_load.cpp` | 1 (`stopSimulationWorker`) |
| `src/client/graphics/renderer_zone_lifecycle.cpp` | 1 (`stopSimulationWorker`) |
| `src/client/graphics/renderer_slash_commands.cpp` | 1 (`getSimWorkerDebugInfo`) |
| `tests/test_simulation_output_lifetime.cpp` | 9 (call-site only; see above) |

**⚠ MERGE NOTE for the orchestrator.** `getSimWorkerDebugInfo`'s caller had
**not** moved on this tip: L06 was still in flight, so the site is
`src/client/graphics/renderer_slash_commands.cpp:375` at the CURRENT path, and
that is where L07 edited it. This is K06/K07 correction (r)'s cross-family
collision by construction -- slot1's L06 is moving that whole TU. The hunk to
keep is the retarget itself; in `slash/` it should read
`owner_.SimBridge().getSimWorkerDebugInfo()`.

### Text readers of the moved TU (K03 c / K11 ee)

Greped `tests/`, `tests/cmake/`, `scripts/`, `tools/`, `docs/` and
`CMakeLists.txt` for `renderer_sim_bridge`. **No file parses it.** Nine hits,
all prose:

- `tests/test_object_node_texture_guard.cpp:8` and
  `tests/test_player_camera_component.cpp:122` -- the two the plan flagged.
  **Verified: comments naming the bare basename**, which L07 did not change (only
  the directory moved). Both tests pass unchanged.
- `tools/hwcaps/gles2_zone_benchmark.cpp` (4) and
  `tools/hwcaps/zone_bench_loader.h` (1), plus `tests/goldens/manifest.json` --
  comments citing `renderer_sim_bridge.cpp:745` etc. as the source of the
  benchmark's x3 player-light colour boost. **The basename still resolves; the
  line numbers are now off by one** (the added `#include`). Deviation recorded
  rather than fixed: `tools/` is outside this unit's territory, `DocCitations`
  does not check bare basenames (its shape-2 rule requires a `/` in the path),
  and the boost line itself is unchanged. Worth a one-line sweep in L12.
- `docs/decisions.md:313` -- bare basename in prose, still correct.

`tests/cmake/` and `scripts/` have no reader at all; only `CMakeLists.txt:518`
and `docs/module_map.md` needed repointing.

### Predicted vs measured header delta

| | lines |
|---|---|
| plan | -8 or better |
| predicted before the first edit, with L01 correction 3's +3..+6 per construct | **-12** |
| measured | **-10** (1,659 -> 1,649; `git diff --numstat` 22 added / 32 removed) |

The prediction was optimistic by 2 lines and the arithmetic is exact: **32
removed** = 1 (`getSimWorkerDebugInfo` declaration) + 28 (the whole private
"SimulationWorker helpers" block: 9 declarations plus 19 lines of banner and
T116 contract prose, L02 correction 11) + 3 (the two particle members and their
banner). **22 added** = 1 include + 6 (the L07 transition note replacing the
declaration block) + 2 (the note replacing the particle members) + 5 (the
`SimBridge()` accessor and its comment) + 8 (the friend line, the member, and
the five-line note explaining the friendship axis and the unchanged thread
ownership).

L05 correction 23 held and is why the delta is not worse: **all of this unit's
prose -- the threading statement, the T116 contract in full, the friend-axis
census -- went into `sim_bridge_component.h` (134 lines), not into the god
header**, which kept only a six-line pointer to it.

### Residual (§Mechanism 8)

Normalisation applied to the NEW side only, by name mapping
(`SimBridgeComponent::` -> `IrrlichtRenderer::`; `ctx_.<name>` -> `<name>_` for
all 40 context members, longest-name-first per K04 (i); `owner_.` -> empty),
then trailing `//` comments stripped outside string literals (K08 w), blank
lines dropped, both sides compared as multisets.

`renderer_sim_bridge.cpp` -> `sim/renderer_sim_bridge.cpp`:
**779 old code lines vs 780 new.**

```
removed (0):
added (1):
  #include "client/graphics/sim/sim_bridge_component.h"
```

**Zero moved body lines modified.** The 1,113-line TU became 1,114 lines; every
comment, including the 14-line ordering rationale in `applySimulationResults()`
and the two T116 contract blocks, is byte-identical.

The TU keeps `#include "client/graphics/irrlicht_renderer.h"` (it needs the
complete type for `owner_.`), so **L01 correction 4's ~60-transitive-include
rediscovery did NOT bite** -- fourth consecutive handle-using component for
which it has not, and this unit needed no include iteration at all.

### The component header's include set

`sim_bridge_component.h` includes `renderer_context.h` and
**`simulation_worker.h`**, plus `<glm/glm.hpp>`, `<cstdint>`, `<string>`,
`<unordered_map>` and `<vector>`. The `simulation_worker.h` include is the
interesting one: three of the nine signatures name `SimulationZoneData` /
`SimulationOutput`, and the two moved members are
`std::unordered_map<uint16_t, glm::vec3>`. **None of these is an
`IrrlichtRenderer` nested type**, so L02 correction 10 / L05's `CameraMode` case
did not arise -- there is no cycle, because `irrlicht_renderer.h` already
includes `simulation_worker.h` (line 48) ahead of the component includes.

### The test (L01 correction 9)

`tests/test_sim_bridge_component.cpp`, **9 tests, all pass**, registered in
`tests/cmake/renderer_guards.cmake` next to `test_player_camera_component` (same
`IF(EQT_GRAPHICS AND IRRLICHT_LIBRARY)` guard, same bare never-`Init()`'d
`IrrlichtRenderer` idiom). Both binding paths are pinned by MUTATION through
public accessors only (L01 correction 6):

- **`ctx_`, renderer -> component**: `BuildZoneDataReadsTheContextTheRendererWroteThrough`
  and `BuildZoneDataSeesContextChangesOnEveryCall` write `usePvsCulling`,
  `indoorRegions` and the two `constrainedConfig` portal flags through
  `Context()` and read them back off the built `SimulationZoneData`. On a bare
  renderer every container `buildSimulationZoneData()` walks is empty or null and
  each is guarded, so the whole 155-line body runs.
- **`ctx_`, component -> renderer**:
  `PortalOcclusionToggleWritesTheRenderersMemberThroughTheContext` drives
  `applySimulationOutput()`'s four-condition portal-occlusion gate in both
  directions and reads `isPortalOcclusionEnabled()` back;
  `PortalOcclusionToggleStaysPutWhenTheGateIsClosed` proves the gate, not the
  write, is what the first test measured.
- **`owner_`**: `DebugInfoReadsTheOwnersWorkerHandle`,
  `WorkerLifetimeCallsAreNoOpsWithNoWorkerOnTheOwner`,
  `PostSimulationInputIsANoOpWithNoWorkerOnTheOwner` and
  `ApplyResultsIsANoOpWithNoWorkerOnTheOwner`. **These four are the executable
  statement that the thread ownership did not move**: every worker touch in the
  component goes through `owner_.simulationWorker_`, which is null on a bare
  renderer, and each of the five worker-facing entry points early-outs on it.
- `AccessorReachesAConstructedComponent` -- the ctor wiring, without which every
  one of the 11 retargeted call sites is a production null dereference.

The T116 lifetime split itself is deliberately NOT re-tested here:
`tests/test_simulation_output_lifetime.cpp` already calls both real bodies, and
after L07 it reaches them through `SimBridge()`, so it is now also the coverage
for that receiver.

### §5: nothing deleted, nothing boarded

All nine methods have live callers (11 sites), so §Mechanism 5's zero-caller
delete never applied and there was no zero-caller surface to board. K12 (ff)
checked and clear anyway: no body in the TU contains `pushEvent` or
`GameEventType::`.

### Verification

| step | result |
|---|---|
| `cmake --build build -j24` | **exit 0, zero compile errors at any point.** The FIRST invocation compiled everything cleanly and was then killed by the 600 s ceiling during `ar` (exit 143), which DELETED `lib/libwilleq_core.a` -- the protocol's absence case, not the truncation case. Artifact presence asserted first (`bin/willeq`, `lib/libwilleq_core.a`, the four named test binaries: all PRESENT), then all **246** binaries and archives validated (ELF magic + size, `ar t`): **bad=0** |
| `ctest --test-dir build -j6 --output-on-failure` | **3,678 / 3,679 passed**, 1 failure: `AudioManagerTest.PreloadCommonSounds` -- the only permitted one |
| the named guards | `test_simulation_output_lifetime` **6/6 pass unchanged**, `test_object_node_texture_guard` passes unchanged, `BridgeQueueRaceTest` (both cases) passes unchanged, `FirstPersonCameraSingleFormula` 4/4, `FileSizeCeiling`, `DocCitations`, `ReachabilitySweep` (4), `DuplicateStateStores` all pass |
| `test_sim_bridge_component` (new) | **9 tests, all pass** |
| `scripts/build-gles2-x86.sh` | **exit 0**, zero errors; `build-gles2/bin/willeq` and `gles2_zone_benchmark` both valid x86-64 ELF. (First invocation hit the 600 s ceiling; re-run clean.) |
| visual gate standalone | **PASS** (`=== VISUAL GATE: PASS ===`), 3 zones + 3 memory-coverage runs, every CPU p95 under ceiling (qeynos2/gfaydark/befallen). `git status --porcelain` shows **no golden path** -- nothing re-blessed. This family computes the frame's visibility, lighting and particle state, so the gate is the proof the move changed no pixels |
| `scripts/build-arm-noble.sh` | **exit 0**, zero errors, `build-arm-noble/bin/willeq` valid 32-bit ARM ELF. (First invocation hit the 600 s ceiling; re-run clean.) |
| `scripts/refactor/check_file_sizes.sh --all` | **847 checked, 847 ok, 0 warn, 0 FAIL**, with `irrlicht_renderer.h` lowered 1,659 -> 1,649 |
| residual multiset | **779 old vs 780 new; removed 0, added 1** (the component include) |

### Line counts

| file | before | after |
|---|---|---|
| `include/client/graphics/irrlicht_renderer.h` | 1,659 | **1,649** (-10) |
| `src/client/graphics/renderer_sim_bridge.cpp` -> `src/client/graphics/sim/renderer_sim_bridge.cpp` | 1,113 | 1,114 |
| `include/client/graphics/sim/sim_bridge_component.h` | — | 134 (new) |
| `tests/test_sim_bridge_component.cpp` | — | 154 (new) |

### Corrections to the Batch L template

**26. A `friend` fixture is only at risk from an extraction if it names a member
the unit MOVES.** L07's plan flagged the six `SimulationOutputLifetime` friend
lines as a STOP candidate; the measurement was that all six members the fixtures
touch stay on `IrrlichtRenderer`, so the friend lines stay, no assertion moves,
and the whole exposure is nine call-site receivers. Before flagging a friend
fixture in a plan, intersect the fixture's member set with the unit's MOVED
member set -- if the intersection is empty, there is no risk to flag, and saying
so up front saves the executor a full re-derivation. L11 (the frame family, 24
members, the fixtures' real territory) is where this intersection will be
non-empty.

**27. K04 correction (f) reads in BOTH directions: an inline accessor can make a
shared member look single-family.** Every prior L unit hit the (f) case as
"a body count missed a member". `loading_` is the mirror image: it appears in
exactly one TU plus the header, which is the file-level signature of a
single-family member, and it is shared -- its real users are the two inline
accessors `setLoading()`/`isLoading()`, whose callers are all over the renderer.
**A single-family claim must be re-checked against the header's inline
accessors before the member moves**, not only a "member is missing from the
plan" claim.

**28. `owner_` IS the answer to a thread-ownership STOP condition, not a
workaround for it.** The mechanism's transition handle leaves the member, its
constructor, its destructor and its synchronisation exactly where they were and
changes only the syntax of the reach. A unit whose family talks to another
thread therefore does not need a thread-ownership decision at all as long as the
owning member is shared -- and it will be shared, because a worker that only one
family touched would not have needed a bridge. The four `owner_`-path tests in
this unit are how that gets pinned rather than asserted.
