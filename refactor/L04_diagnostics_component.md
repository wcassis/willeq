# L04 — `DiagnosticsComponent` (family: `renderer_diagnostics.cpp` + `renderer_debug.cpp` — memory reports, scene dumps, profiling, debug toggles)

Batch: `refactor/L_renderer_component_extraction.md`. Wave 1, three-wide
with L02/L03. Measured at `cfbbde57`: 1,735 lines, 28 bodies, 1
single-family member (`frameTimings_`/`frameTimingsAccum_` are
nested-typed — K05 j — measure), reads 40+ core members through the context
(read-mostly; this is the breadth test of `RendererContext`).

⚠ Forwarders that stay (count them): the `IOutputRenderer` overrides
`toggleHUD`, `toggleNameTags`, `toggleWireframe` (implemented by
`graphical_mode.cpp` callers via the interface); `dumpScene` and
`getMemoryReport` are called from `irrlicht_bridge.cpp` (bridge rule) and
named in `event_bus.h` (a comment — read it); `runSceneProfile` from
`application.cpp`. Everything called only from `renderer_slash_commands.cpp`
(L06, still on `IrrlichtRenderer`) and `renderer_frame*.cpp` retargets to
`diagnostics_->X()`.

⚠ `scripts/ci/test_eqe1_totalestimate_accounting.py` names
`getMemoryReportBreakdown()` and `renderer_diagnostics.cpp` in comments —
read it; if it parses the TU, repoint (K03 c). `isUsingOldModels` is named
in `race_model_loader.h` (a comment or a callback — read).

Zero-external-caller methods (`getMemoryReportBreakdown`,
`swapZoneMeshMaterials`, `toggleFog`, `toggleOldModels`) — in-family or dead;
§5 + K12 (ff) (`dumpScene` publishes `DiagnosticsSceneDump`? — check every
candidate for `pushEvent`).

Predicted header delta: −28 declarations −(1..3) members −banners
+(forwarders ≈ 6) +(scaffolding 4..6) ≈ **−20 or better**.

Territory: `include/client/graphics/diagnostics/diagnostics_component.h`
(new), `src/client/graphics/diagnostics/` (git mv of the two TUs),
`irrlicht_renderer.h`, `renderer_init.cpp` (ctor line), root `CMakeLists.txt`,
`tests/test_diagnostics_component.cpp` + cmake, allowlist,
`docs/module_map.md`, CALL-SITE-ONLY edits in the renderer TUs,
`irrlicht_bridge.cpp`, `application.cpp`, `graphical_mode.cpp`. Visual gate
+ ARM NOT skippable. L02/L03 share `irrlicht_renderer.h`, `renderer_init.cpp`,
`renderer_frame_*.cpp`, `renderer_slash_commands.cpp` at other lines.

## Review

Executed in `wt/slot3` on `task/L04-diagnostics-component`, branched from
`fable` at `d13f8ddc`. Every verification step passed; nothing STOPped.

### Headline

`include/client/graphics/irrlicht_renderer.h` **1,871 -> 1,859 (-12)**;
allowlist lowered to 1,859 in the same commit. 28 bodies measured, **27
moved**, one (`measureSection`) relocated to the family that actually owns it.
The residual is **zero moved-body lines added and zero modified** in both TUs.
Suite 3,613 -> **3,621**, one failure (the ALSA quirk).

**The breadth answer: `RendererContext` carried this family.** The moved
bodies reach **30 of the context's 54 members (329 uses)** through `ctx_`, and
**39 further `IrrlichtRenderer` members plus 3 methods (254 uses)** through the
`owner_` transition handle. The context was NOT widened; every one of the 39
is a member shared with another family that sits below the 5-family core
threshold, and §Mechanism 3 says the unit that extracts the LAST family using
it moves it. Full list in the table below.

### ⚠ The plan's forwarder premise is FALSE, and the measurement found a live defect

The plan says `toggleHUD`, `toggleNameTags` and `toggleWireframe` are
"`IOutputRenderer` overrides ... implemented by `graphical_mode.cpp` callers
via the interface" and must stay as forwarders. **`class IrrlichtRenderer`
does not derive from `IOutputRenderer`** (`irrlicht_renderer.h:114`,
`class IrrlichtRenderer {`, no base clause). `output_renderer.h`'s
`toggleWireframe`/`toggleHUD`/`toggleNameTags` are `virtual ... {}` with EMPTY
default bodies, and a tree-wide grep finds **no class anywhere that overrides
them**. `GraphicalMode::toggleWireframe()` calls
`m_renderer->toggleWireframe()` on a `std::unique_ptr<output::IOutputRenderer>`
and therefore reaches the empty default. These three have **zero callers on
`IrrlichtRenderer`**.

Following the lead one hop further found the real defect:

- `RendererEventReceiver::OnEvent` queues `RendererAction::ToggleWireframe`,
  `ToggleHUD`, `ToggleNameTags` (`renderer_event_receiver.cpp:181-183`) and
  `ToggleOldModels` is in the same enum (`renderer_types.h:111-112`);
- `IrrlichtRenderer::processCommonInput()`'s switch
  (`renderer_frame_input.cpp:115-145`) has **no arm for any of the four** —
  they fall through its `default: break;`;
- the hotkeys are bound and parsed (`hotkey_manager.cpp:111-113`,
  `input_handler.h:63-65`).

So four bound debug hotkeys have done nothing, on both routes, for as long as
the RA queue has existed. **This is the T313/T315 pattern (a handler that was
never wired) for the fourth time.** It is a behaviour change to fix and this
unit is structural, so it was **boarded, not fixed**.

**Deviation, flagged not absorbed:** the four implementations were kept, moved
into `DiagnosticsComponent` as **public** methods reachable through
`Diagnostics()`, and their `IrrlichtRenderer` declarations were deleted. §5's
zero-caller rule says delete outright; that would have destroyed the
implementations a one-line fix needs, so the K07 (o) "recorded keep decision
overrides §5" shape was applied and recorded here instead. `toggleFog` is the
same case with no RA action at all and got the same treatment. **No forwarder
was written for any of the five** — writing a forwarder for a method with zero
callers would have kept `irrlicht_renderer.h` as the API surface for dead
surface, which is what Batch L exists to remove.

### Forwarders that stay: 3

| forwarder | caller | rule |
|---|---|---|
| `dumpScene()` | `src/client/bridge/irrlicht_bridge.cpp:906`, on the `DiagnosticsSceneDump` event | bridge |
| `getMemoryReport()` | `src/client/bridge/irrlicht_bridge.cpp:890` (`/pmem`) | bridge |
| `runSceneProfile()` | `src/client/application.cpp:604` (`--scene-profile`) | K11 |

Plus `Diagnostics()`, the accessor (scaffolding, not a forwarder).

K12 (ff) checked: **no body in either TU contains `pushEvent` or
`GameEventType::`** — the family is a consumer of `DiagnosticsSceneDump`
(published by `eqt::commands::CommandsComponent`), never a publisher. Nothing
was deleted on a zero-caller finding, so the correction never bound.

### `measureSection` did NOT move — deviation

`measureSection()` sat in `renderer_diagnostics.cpp` but is **frame-phase
state, not diagnostics**: `sectionStart_` is declared under the header's
"Frame phase shared state" comment next to `frameStart_` and `frameNumber_`,
and all **35** call sites are the frame family's (`renderer_frame.cpp` 8,
`renderer_frame_input.cpp` 12, `renderer_frame_render.cpp` 15). Moving it
would have meant 35 `diagnostics_->` edits in two files L02 and L03 are also
editing, to put a frame primitive in the wrong component. It stays declared on
`IrrlichtRenderer` and its six-line body moved **verbatim** to
`src/client/graphics/renderer_frame.cpp`, the family that owns it; L11 takes
it with the rest of the frame family. Zero call sites changed. This is the
inverse of K10 correction (u) — a body sitting in a TU that is not its
family's — and it is proved by the residual below.

### The `owner_` reaches (39 members + 3 methods, 254 uses)

`activeLightCount_`, `allUIHidden_`, `debugDirectionalLightEnabled_`,
`debugObjectLightsEnabled_`, `debugPlayerLightEnabled_`, `eqe1Loaders_`,
`eqe1Mutex_`, `essentialSimCostAvgUs_`, `fallbackMeshNode_`,
`fireGlowIcospheresEnabled_`, `fireGlowLightingEnabled_`, `fireGlowLights_`,
`frameTimingsAccum_`, `frameTimingsSampleCount_`, `hotkeysText_`,
`hudEnabled_`, `hudText_`, `icosphereProgram_`, `lastPolygonCount_`,
`maxFireGlowLights_`, `maxObjectLights_`, `mergeZoneDrawsBuildEvictionCount_`,
`mergeZoneDrawsDisabledForZone_`, `mergeZoneDrawsEnabled_`,
`playerLightNode_`, `portalDebugDraw_`, `renderCostAvgUs_`,
`renderPassTimer_`, `sceneProfileEnabled_`, `sceneProfileFrameCount_`,
`sortedDrawEntries_`, `stencilDebugDraw_`, `sunLight_`, `zoneCollisionNode_`,
`zoneLightData_`, `zoneLightsEnabled_`, `zoneMergeBuffers_`, `zoneMeshNode_`,
`zonePlaceholderNode_`; methods `isLoading()`, `updateTimeOfDay()`,
`getEQE1MappedBytesTotal()`.

The single-family member the plan predicted is **`debugOriginalMaterials_`,
and it is the only one** — every other member the bodies touch has at least
one reader in another renderer TU. It moved with its private nested type
`DebugMaterialEntry` (named nowhere else in the tree). `frameTimings_` /
`frameTimingsAccum_` were measured as the plan asked: `frameTimings_` is not
referenced by this family at all, and `frameTimingsAccum_` has 51 references
outside it — **neither is single-family**; the K05 (j) nested-type question
never arose for them.

`SceneBreakdown`, an `IrrlichtRenderer` private nested type, IS named by
`profileSceneBreakdown()` and became `IrrlichtRenderer::SceneBreakdown` — the
third substitution class (K05 j). With that plus 39 private member reaches,
the component needs `friend class DiagnosticsComponent;` on **both** axes of
K13 (ii). Contrast `RdpComponent`, which needed none.

### Predicted vs measured header delta

| | lines |
|---|---|
| predicted (plan) | **-20 or better** |
| measured | **-12** (1,871 -> 1,859) |

Exact arithmetic (deletions 47, additions 35):

| block | lines |
|---|---|
| 27 declarations deleted | **-27** |
| `DebugMaterialEntry` struct + its comment + `debugOriginalMaterials_` | **-7** |
| banner/comment lines that went with the deleted declarations | **-13** |
| replacement comments on the 4 surviving accessor blocks | **+9** |
| 3 forwarders + `Diagnostics()` accessor (as inline bodies; `getMemoryReport` is 3 lines) | **+7** |
| comments on those (the K11-rule note + the T69 note) | **+7** |
| `friend class DiagnosticsComponent;` + `diagnostics_` + its 7-line note + blank | **+10** |
| include | **+1** |
| net | **-12** |

**The miss is again the comment budget, and by the same amount L01 missed
by.** L01's correction 3 raised the per-construct scaffolding estimate from +1
to +3..+6 lines; this unit shows that is still low when the unit has a FINDING
to record — the unwired-toggle note and the `owner_`-breadth note are 17 of
the 35 added lines. **The rule to carry forward: cost scaffolding at +3..+6
per construct AND add the lines needed to record whatever the caller census
turns up; a unit that finds nothing pays the lower number.** The prediction
was not wrong about the mechanism (27 declarations did leave), only about the
prose. Delta is comfortably negative, so no STOP condition was reached.

### Residual (§Mechanism 8)

Normalisation applied to the NEW side only, then both sides sorted and
comment/blank lines dropped. Mapping: `DiagnosticsComponent::` ->
`IrrlichtRenderer::`; `IrrlichtRenderer::SceneBreakdown` -> `SceneBreakdown`;
`ctx_.<name>` -> `<name>_` for all 30 context members (longest-name-first, K04
i); `owner_.` -> empty.

`src/client/graphics/renderer_diagnostics.cpp` -> `diagnostics/diagnostics_component.cpp`,
880 old code lines against 875 new:

```
removed (6):  -- the measureSection relocation, verbatim, to renderer_frame.cpp
  int64_t IrrlichtRenderer::measureSection() {
      auto now = std::chrono::steady_clock::now();
      auto us = std::chrono::duration_cast<std::chrono::microseconds>(now - sectionStart_).count();
      sectionStart_ = now;
      return us;
  }
added (1):
  #include "client/graphics/diagnostics/diagnostics_component.h"
```

`src/client/graphics/renderer_debug.cpp` -> `diagnostics/diagnostics_debug.cpp`,
444 old code lines against 445 new:

```
removed (0):
added (1):
  #include "client/graphics/diagnostics/diagnostics_component.h"
```

**Zero moved body lines added, zero modified, in either TU.** Unlike L01, no
include had to be added for an incomplete type: both TUs still include
`irrlicht_renderer.h`, because `owner_.<member>` needs the complete type — so
L01 correction 4 does NOT bind for a component that uses its transition
handle. The six relocated lines appear verbatim in
`src/client/graphics/renderer_frame.cpp:46-51` (K10 u's proof on the second
TU).

### Call sites retargeted (21, all found by the compiler)

| file | sites |
|---|---|
| `src/client/graphics/renderer_slash_commands.cpp` | 13 (`toggleAllUI`, `togglePlayerLight`, `swapToDebugShader`, `swapFromDebugShader`, `toggleObjectLights`, `toggleDirectionalLight`, `toggleManualZoneDraw`, `toggleMergeZoneDraws`, `togglePortalOcclusion`, `togglePortalDebugDraw`, `toggleStencilDebugDraw`, `dumpScene`, `runSceneProfile`) |
| `src/client/graphics/renderer_frame_input.cpp` | 3 (`toggleAllUI`, `toggleZoneLights`, `cycleObjectLights` — the three RA arms that DO exist) |
| `src/client/graphics/renderer_frame_render.cpp` | 1 (`profileSceneBreakdown`) |
| `src/client/graphics/renderer_frame.cpp` | 1 (`logFrameTimings`) |
| `src/client/graphics/renderer_zone_load.cpp` | 1 (`dumpScene`) |
| `src/client/graphics/renderer_zone_lifecycle.cpp` | 1 (`loadIndoorRegionMap`) |
| `src/client/graphics/renderer_hud_env.cpp` | 1 (`isUsingOldModels`) |

⚠ **K06/K07 correction (r) applies to the last row.** `renderer_hud_env.cpp`
is L03's TU (the overlays family) and it is being moved in this same wave.
`diagnostics_` is not on `fable` yet, so the in-renderer form
`diagnostics_->isUsingOldModels()` was written; when L03 lands, that hunk
resolves to `owner_.Diagnostics().isUsingOldModels()`. Same class of conflict,
one line, in `renderer_frame_input.cpp` / `renderer_frame_render.cpp` /
`renderer_slash_commands.cpp`, which L02/L03 also edit at other lines.

`application.cpp`, `graphical_mode.cpp` and `irrlicht_bridge.cpp` are
**untouched** — the K11 rule held with zero pressure, as in L01.

### Text readers of the moved TUs (K03 c / K11 ee)

Greped `tests/`, `tests/cmake/`, `scripts/`, `tools/`, `CMakeLists.txt` and
`docs/` for both filenames. Every hit outside `CMakeLists.txt` is **prose in a
comment**, not a parse:

- `scripts/ci/test_eqe1_totalestimate_accounting.py:8,45` — docstring only;
  the test runs `gles2_zone_benchmark` and asserts on its `--memory-json`. It
  never opens the TU. Ran green in the suite.
- `scripts/ci/visual_gate.py:21`, `tools/hwcaps/zone_bench_memory.{h,cpp}` (6
  hits), `tests/goldens/manifest.json:166` — all comments naming the
  accounting's origin.
- `check_doc_citations.py`'s live-doc set is CLAUDE.md files, `docs/*.md`,
  `refactor/BOARD.md`, `EXECUTOR_PROTOCOL.md` and `refactor/K_*.md`; the bare
  filenames in `docs/module_map.md` had no `/` so they were never citations.
  They are now full repo-relative paths anyway. `DocCitations` passes.
- The plan's `race_model_loader.h` lead for `isUsingOldModels` is
  **K10 correction (t) again**: line 299 is `RaceModelLoader`'s OWN
  `isUsingOldModels()`, a different class with the same name. The moved body
  calls it on `loader->`, unchanged.

Only `CMakeLists.txt:503-504` needed repointing. `test_reachability_sweep.cpp`
and `test_duplicate_state_stores.cpp` name no renderer path — no edit, and
neither is a renderer guard.

### Verification

| step | result |
|---|---|
| `cmake --build build -j24` | **exit 0**. One intermediate failure, one cause: 8 `isLoading was not declared` — an `IrrlichtRenderer` inline method the pre-build call census missed (see corrections). Two exit-143 timeouts; artifact scan after each found **0 truncated and 0 missing** (236 binaries + 2 archives) |
| `ctest --test-dir build -j6 --output-on-failure` | **3,620 / 3,621 passed**, 17 skipped, **1 failure: `AudioManagerTest.PreloadCommonSounds`** — the only permitted one. `FileSizeCeiling`, `DocCitations`, `ReachabilitySweep`, `DuplicateStateStores` all PASS |
| `test_diagnostics_component` (new) | **8 tests, all pass** |
| `scripts/build-gles2-x86.sh` | **exit 0** (two exit-143 resumes; final run clean, `willeq` and `gles2_zone_benchmark` both valid ELF) |
| visual gate standalone | **PASS** (`=== VISUAL GATE: PASS ===`), 3 zones + 3 memory-coverage runs. `git status --porcelain` shows **no golden path** — nothing re-blessed |
| `scripts/build-arm-noble.sh` | **exit 0** |
| `scripts/refactor/check_file_sizes.sh --all` | **836 checked, 836 ok, 0 warn, 0 FAIL**, with the `irrlicht_renderer.h` allowlist entry lowered 1,871 -> 1,859 |

### Line counts

| file | before | after |
|---|---|---|
| `include/client/graphics/irrlicht_renderer.h` | 1,871 | **1,859** (-12) |
| `src/client/graphics/renderer_diagnostics.cpp` -> `src/client/graphics/diagnostics/diagnostics_component.cpp` | 1,162 | 1,160 |
| `src/client/graphics/renderer_debug.cpp` -> `src/client/graphics/diagnostics/diagnostics_debug.cpp` | 573 | 577 |
| `include/client/graphics/diagnostics/diagnostics_component.h` | -- | 122 (new) |
| `tests/test_diagnostics_component.cpp` | -- | 121 (new) |
| `src/client/graphics/renderer_frame.cpp` | 512 | 527 (+15: `measureSection` + its 9-line note) |

### The test (L01 correction 9)

`tests/test_diagnostics_component.cpp`, 8 tests, registered in
`tests/cmake/renderer_guards.cmake` next to `test_rdp_component` (same
`IF(EQT_GRAPHICS AND IRRLICHT_LIBRARY)` guard, same bare never-`Init()`'d
`IrrlichtRenderer` idiom). Every assertion that can be a mutation is one, and
this unit could pin something L01 could not: **both binding paths, end to
end, through public accessors only.**

- `PortalOcclusionToggleReadsAndWritesThroughTheContext` — the strongest one.
  `togglePortalOcclusion()` READS `ctx_.portalOcclusionEligible` (false on a
  fresh renderer, so the first call must be a no-op) and WRITES
  `ctx_.portalOcclusionEnabled`. The test sets eligibility through
  `Context()`, calls the component, and reads the result back through
  `IrrlichtRenderer::isPortalOcclusionEnabled()`. Context write -> component
  read -> component write -> renderer read, one round trip.
- `WireframeToggleWritesTheRenderersOwnMember` — the `ctx_` write path.
- `PortalDebugDrawToggleWritesThroughTheOwnerHandle`,
  `AllUIToggleWritesThroughTheOwnerHandle`,
  `StencilDebugDrawToggleWritesThroughTheOwnerHandle` — the `owner_` path,
  each observed through `IrrlichtRenderer`'s own public reader.
- `MemoryReportForwarderReachesTheComponent`,
  `DumpSceneAndSceneProfileForwardersAreLive` — the three surviving
  forwarders reach a non-null component.

### Corrections to the Batch L template

1. **L01 correction 5 is WRONG and must be struck.** `IrrlichtRenderer` does
   not derive from `IOutputRenderer` and overrides none of its virtuals;
   `setCurrentTarget`, `clearCurrentTarget`, `toggleHUD`, `toggleNameTags`,
   `toggleWireframe`, `dumpScene` and `getMemoryReport` are ordinary members
   that happen to share a name with an `IOutputRenderer` default-`{}` virtual.
   **"It is named like an `IOutputRenderer` method" is not a reason to keep a
   forwarder** — L04's three forwarders are justified by their ACTUAL callers
   (`irrlicht_bridge.cpp`, `application.cpp`), which is the K11 rule and needs
   no interface story. Every L plan that cites correction 5 should re-derive
   its forwarder list from a receiver-aware caller scan.

2. **A same-named virtual on an interface the class does not implement is a
   caller-census trap of exactly K10 (t)'s shape** — and it hid a live
   four-hotkey defect for months. When a lead says "reached via an interface",
   verify the class actually derives from it before believing the lead.

3. **The `owner_` transition handle is the load-bearing part of a breadth
   family, not a vestige.** L01's `RdpComponent` never used it and correction
   7 called it "unused so far"; `DiagnosticsComponent` uses it 254 times
   across 42 names. Both extremes are correct outcomes of §Mechanism 3.
   A corollary: **because the bodies use `owner_.<member>`, the moved TU must
   KEEP `#include "client/graphics/irrlicht_renderer.h"`, so L01 correction 4
   (dropping it costs ~60 transitive includes) does not bind for a
   handle-using component.** Only a component that reaches everything through
   the context pays that cost.

4. **The pre-build call census must not use a non-overlapping regex.** The
   census that finds bare `identifier(` calls to methods staying behind missed
   `isLoading()` in all 8 places because the enclosing `if (` matched first
   and Python's `finditer` is non-overlapping — `if (isLoading())` consumed the
   `(` that preceded the real call. Scan for every `identifier\s*\(` and reject
   by looking at the PRECEDING character instead. Eight identical build errors,
   one iteration; K08 correction (x)'s "compute the whole retarget
   mechanically before the first build" is right, but only if the scanner is.

5. **A body can be in the wrong TU for its family, and the fix is to move it
   sideways, not into the component.** `measureSection` is the case
   (see above). The test: whose STATE does it touch, and whose call sites are
   they? Both said frame, not diagnostics. This is K10 (u) read backwards and
   every remaining L unit should ask it of each body before moving it.

6. **The `--all` file-size run and the ratchet are cheap; the two 10-minute
   build ceilings are not.** Both `cmake --build` and `build-gles2-x86.sh`
   hit exit 143 in this slot with a warm tree. Budget two to three
   re-invocations per full-build step for an L unit and run the artifact scan
   after each (this unit's scans were clean every time, but the GLES2 lib
   directory held two multi-GB killed-`ar` temp files from an unrelated
   2026-09-02 run — noise, but it is exactly what the protocol's
   absence-vs-corruption note warns reads as damage).
