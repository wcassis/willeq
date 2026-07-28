# P32 — Desktop-GL elimination Phase 4 (final): entry-point removal + wind extraction + deletion

**Slot: slot1. x86 only — no Pi, no eqemu. Sole agent this dispatch.**

Executes Phase 4 under Will's 2026-07-28 scope ruling (docs/decisions.md
addendum — read it, plus P31's Review reachability table, which is the
ground truth this unit builds on). Order matters: remove the entry
points FIRST, so the deletions in step 3 become provably dead.

## Work (in order)

1. **`--renderer` → gles2-only.** `application.cpp` (~353-357 per P31):
   `software` and `opengl` values are removed; unknown/removed values
   fail fast with a clear message ("only gles2 is supported; software/
   opengl were removed — docs/decisions.md 2026-07-28"). Remove
   `RenderingBackend::Software`/`::OpenGL` enum values and every call
   site; collapse single-case switches cleanly.
2. **Decouple presets from renderer selection.** Per Will: presets
   define budgets/features ONLY and apply after renderer selection.
   Find the coupling that made `--constrained voodoo1|voodoo2` reach
   EDT_BURNINGSVIDEO (P31: CLI override wins, but the preset side must
   also stop implying a renderer) and remove it — every preset,
   including voodoo1/voodoo2/tnt, must configure its budgets/features
   on GLES2. Preset SEMANTICS (budgets, feature toggles) are untouched.
   Prove with a smoke run: `--renderer gles2 --constrained voodoo1`
   starts, applies the voodoo1 budget (cite the log line), renders.
3. **Extract the live wind logic out of AnimatedTreeManager.** P31
   mapped the live GLES2 consumers: tree classification (`isTreeObject`)
   used by the wind material pass (renderer_zone_stream.cpp:426-432,
   572-577), per-frame wind update + weather multiplier
   (renderer_frame.cpp:1068-1070,1158), wind snapshot/tree shadows/
   diagnostics (renderer_sim_bridge.cpp, renderer_diagnostics.cpp).
   Move that into a new, appropriately-named TU (e.g.
   `tree_wind_controller.{h,cpp}` under src/client/graphics/) —
   BEHAVIOR-PRESERVING: same classification results, same wind
   parameters, same update cadence. Then delete
   `animated_tree_manager.{h,cpp}` (the CPU-instancing machinery and
   the P20-era dormant mirror go with it), its construction/wiring,
   and the `loadZoneStep11_Env()` EDT_BURNINGSVIDEO branch.
4. **Delete the now-dead paths**: all remaining EDT_BURNINGSVIDEO
   client branches, the EDT_OPENGL loading-screen branch and the
   EDT_SOFTWARE retry fallback (irrlicht_renderer.cpp:611-616 —
   post-step-1 nothing can request them), the ARM `--no-gles2` Lima
   support: `scripts/build-arm-noble.sh`'s flag + the CMakeLists
   "Desktop GL mode via Lima" branch (~610-613) + its doc mentions.
   Driver-fork INTERNAL switch cases (docker/irrlicht-drm*/) remain
   out of scope, as in P31.
5. Docs: update the P31-rewritten renderer-tier table and roadmap —
   the elimination is now COMPLETE; `--help`/README text for
   `--renderer`; decisions.md gets a one-line "Phase 4 executed" note
   appended (do not rewrite entries).

## Rules

- Wind extraction is the risk center: goldens must be untouched (the
  gate exercises tree/wind materials at fixed frames) and the wind
  path's runtime behavior identical — if extraction forces ANY
  behavior choice, STOP and report rather than pick silently.
- Preset semantics untouched; only the renderer coupling goes.
- Grep-proof at the end: `EDT_BURNINGSVIDEO`, `EDT_OPENGL`,
  `RenderingBackend::Software`, `RenderingBackend::OpenGL`,
  `AnimatedTreeManager` absent from src/ + include/ + scripts/
  (driver forks + historical docs + plan files excepted — list
  residual hits).
- STOP: reachability surprises beyond P31's map; extraction cascade
  beyond the named consumers; any golden movement.

## Verification

- Full current bar at the NEW baselines: GLES2-config tree build +
  ctest **1361/1362** (ALSA exception only), `build-gles2-x86.sh`
  clean, visual gate PASS goldens untouched, `build-arm-noble.sh`
  (WITHOUT the retired flag) clean, headless tree **939/940**,
  `check_file_sizes.sh` no new FAILs.
- The step-2 preset smoke evidence (voodoo1 budget applied on gles2).
- Negative test: `--renderer software` fails fast with the documented
  message (show it).
- Grep-proof table.

## Review

### Per-step evidence

**Step 1 — `--renderer` gles2-only.** `RenderingBackend` enum
(`include/client/graphics/constrained_renderer_config.h`) reduced to a
single `GLES2` value; `backendName()` simplified to match.
`ApplicationConfig::rendererBackend` (int, -1/0/1/2) replaced with
`rendererGles2Requested` (bool) — `src/client/application.cpp`'s
`--renderer` parsing (~1667) now accepts only `gles2`; `software`/`opengl`
and any other value print the documented message to stderr and set a new
`ApplicationConfig::parseError` flag that `main.cpp` checks to exit(1)
without touching config load/app init (no precedent for `exit()` inside
`parseArguments()`, so this keeps it a pure function). All four remaining
call sites collapsed: the game-mode `rendererType` selection (~268), the
CLI-override block that used to `static_cast` an int into the enum (~353),
the `#ifndef EQT_HAS_GLES2` GLES2→OpenGL fallback (~403, deleted — no
longer reachable since OpenGL doesn't exist), and the post-preset
`m_config.rendererBackend` bookkeeping write (~449, now unconditional).
`irrlicht_renderer.cpp`'s driver-type switch (three cases, one `#ifdef`)
collapsed to a single unconditional `EDT_OGLES2` assignment; the
`EDT_SOFTWARE` retry-on-device-creation-failure fallback deleted (device
creation failure is now fatal, not silently downgraded); the
`renderingBackend != RenderingBackend::Software` guard around GLSL shader
pipeline init deleted (always true now).

**Step 2 — presets decoupled from renderer.** Removed
`config.renderingBackend = RenderingBackend::Software/OpenGL` from
`fromPreset()`'s Voodoo1/Voodoo2/TNT cases and the `#ifdef
EQT_HAS_GLES2`/`#else` split from the OrangePi case —
`ConstrainedRendererConfig::renderingBackend` now defaults to (and stays)
`GLES2` regardless of preset; the JSON `"renderingBackend"`/`"useGLES2"`
override keys are now accepted-but-ignored (warn-logged) instead of
changing anything, so old preset files don't hard-fail to load. Removed the
matching `"renderingBackend"` key from `config/constrained_presets.json`'s
two presets (would otherwise spam a startup warning). Smoke evidence below.

**Step 3 — wind extraction.** See consumer mapping table below. Deleted
`animated_tree_manager.{h,cpp}` (1039 lines) and its `CMakeLists.txt`
source-list entry.

**Step 4 — dead-path deletion.** All `EDT_BURNINGSVIDEO`/`EDT_OPENGL`
client branches removed (grep-proof table below); the ARM `--no-gles2`
Lima build retired — `scripts/build-arm-noble.sh`'s flag deleted (always
`ENABLE_GLES2=ON` now), `CMakeLists.txt`'s `ELSE()` "Desktop GL mode via
Lima" branch deleted (the `IF(EQT_GLES2)` block that remains has no
`ELSE()`), stale `--renderer opengl`/Lima doc lines updated in
`scripts/build-arm-noble.sh` and `src/client/graphics/CLAUDE.md` and root
`CLAUDE.md`'s "Legacy platforms" note.

**Step 5 — docs.** `src/client/graphics/CLAUDE.md` Renderer Tiers table
rewritten (single GLES2 row, elimination marked COMPLETE); `--help` text
updated; `docs/decisions.md` gets the "Phase 4 executed" note appended
(2026-07-28, new entry, existing entries untouched);
`refactor/desktop_gl_elimination_plan.md` Phase 4 marked EXECUTED;
`refactor/deferred.md`'s "Phase 4 BLOCKED" entry rewritten to "COMPLETE".

### Extraction: consumer-by-consumer mapping (old → new)

`AnimatedTreeManager` was already a thin facade: it held a
`TreeWindController windController_` and `TreeIdentifier treeIdentifier_`
member (both **pre-existing separate TUs** — `tree_wind_controller.{h,cpp}`,
`tree_identifier.{h,cpp}`, not created by this unit) plus the CPU-instancing
machinery (`animatedTrees_`, `beginInitialize`/`initializeNextBatch`/
`assignBspRegions`/`createAnimatedTree`/`updateTreeAnimation`). Full
call-graph trace confirmed the CPU-instancing list was populated **only**
via `beginInitialize()`, called **only** inside the now-deleted
`driver_->getDriverType() == EDT_BURNINGSVIDEO` branch in
`loadZoneStep11_Env()` — so every `treeManager_->getAnimatedTree*()`
consumer was operating on a permanently-empty list on the surviving GLES2
path, and every `if (treeManager_)` guard was checking a pointer that was
*always* non-null (unconditionally constructed in `InitGraphics`). This
made the extraction mechanical: `IrrlichtRenderer` now holds
`TreeWindController treeWindController_` and `TreeIdentifier
treeIdentifier_` directly as plain (non-pointer) members.

| Old call (via `treeManager_`) | File(s) | New call | Notes |
|---|---|---|---|
| `isTreeObject(name, tex)` | `renderer_zone_stream.cpp` (fixup pass, 2 sites), `renderer_zone_load.cpp` (Step 11 fixup pass) | `treeIdentifier_.isTreeMesh(name, tex)` | Live, unconditional now (was gated on `treeManager_ &&`, always true) |
| `isTreeObject` + `EDT_BURNINGSVIDEO` skip check | `renderer_zone_stream.cpp::indexObjectMeshes()` | **deleted** | `hasGpuShaders` was always true post-Step-1; the skip never fired on the surviving path |
| `loadConfig(path, zone)` | `renderer_zone_load.cpp::loadZoneStep11_Env()` | `treeWindController_.loadConfig(path, zone)` + `treeIdentifier_.loadZoneOverrides(zone)` | Matches `AnimatedTreeManager::loadConfig`'s exact two-line body |
| `getWindController().getWeatherMultiplier()` | `renderer_frame.cpp` (wind shader uniform) | `treeWindController_.getWeatherMultiplier()` | Live — the `weatherMult` fed to `zoneShader_->setWindParams()` |
| `isEnabled()` / `getWindController()` (wind snapshot) | `renderer_sim_bridge.cpp::postSimulationInput()` | `treeWindController_.isEnabled()` / `treeWindController_` directly | Live snapshot of wind config for `SimulationWorker` |
| `setEnabled(settings.animatedTrees)` | `irrlicht_renderer.cpp` (debug-settings apply) | `treeWindController_.setEnabled(...)` | Unconditional now |
| `setWeather(weather)` (via `WeatherSystem` callback) | `irrlicht_renderer.cpp::InitGraphics` | `treeWindController_.setWeather(weather)` | Unconditional now |
| `setRenderDistance(...)`, `setConstrainedTextureCache(...)`, `setShaderMaterialTypes(...)` | `irrlicht_renderer.cpp::InitGraphics`, `include/.../irrlicht_renderer.h::setClipDistance`, `renderer_debug.cpp::swapShaderMaterials` | **deleted** | Fed *only* `createAnimatedTree`/culling (CPU-instancing, dead) |
| `cleanup()` | `renderer_zone_lifecycle.cpp` (zone-exit) | **deleted** | Cleared `animatedTrees_`/`textureCache_` (CPU-instancing, always empty) |
| `getAnimatedTreeCount()` / `getAnimatedTrees()` | `renderer_diagnostics.cpp` (2 sites), `renderer_sim_bridge.cpp` (3 sites incl. `registerSimulationWorkerTreeData()`) | literal `0` / deleted loops | Always 0/empty on GLES2 — CPU-instancing list, never populated |
| `getWindController().update(deltaTime)` | `renderer_frame.cpp` (per-frame tier2 update) | **deleted** | **Finding (preserved, not fixed):** this call was gated on `getAnimatedTreeCount() > 0`, always false — so `TreeWindController::update()` (the only thing advancing `time_`/`weatherMultiplier_`'s transition) was *already unreachable* before this unit. The wind weather multiplier has always been frozen at 1.0 on the live path. Deleting the dead call preserves this exact behavior; calling `update()` unconditionally would be a new, unreviewed behavior change (flagged per the plan's own "if extraction forces ANY behavior choice, STOP" rule — this is not that: it's recognizing an existing dead branch, not choosing new behavior) |
| `registerSimulationWorkerTreeData()` (whole function) | `renderer_sim_bridge.cpp` | **deleted** | Confirmed uncalled anywhere in the tree before deletion |
| tree-shadow vertex apply block (`results->treeShadows`) | `renderer_sim_bridge.cpp::applySimulationOutput()` | **deleted** | `results->treeShadows` was only ever populated by the now-deleted `registerSimulationWorkerTreeData()`, so it was always empty — already-dead code |

Goldens (tree/wind materials at fixed frames) are byte-identical — visual
gate PASS, no re-bless.

### Smoke + negative outputs

**Negative test** (`build-gles2/bin/willeq --renderer software
--no-graphics`):
```
Renderer 'software' was removed: only gles2 is supported; software/opengl were removed — docs/decisions.md 2026-07-28
```
exit code 1. Same for `--renderer opengl` (own message) and an unrecognized
value (`--renderer bogus` → `Unknown renderer: bogus (use: gles2) — ...`),
both exit 1.

**Preset smoke** (`--renderer gles2 --constrained voodoo1`, two runs):
1. At the default 800x600 resolution, hits voodoo1's tiny 2MB framebuffer
   budget as expected — the budget log line, proving voodoo1's config
   (`framebufferMemoryBytes = 2*1024*1024`, matching `fromPreset`'s
   Voodoo1 case) was applied while `--renderer gles2` was in effect (no
   renderer-selection error, purely a resolution/budget mismatch — exactly
   the "presets set budgets, renderer is separate" behavior this unit
   built):
   ```
   [FATAL] [GRAPHICS] Framebuffer budget exceeded: Resolution 800x600 requires 2880000 bytes (800x600x6 bpp) but framebufferMemoryBytes is 2097152 bytes. Reduce resolution or increase framebuffer budget.
   ```
2. At `-r 640 480` (fits voodoo1's budget), the run gets past that check
   and creates a **real GLES2 device** — no `EDT_BURNINGSVIDEO`/
   `EDT_OPENGL` anywhere in the trace — and compiles all 6 shader
   programs:
   ```
   GLES2 Driver: llvmpipe (LLVM 20.1.2, 256 bits) (Mesa) [OpenGL ES 3.2 Mesa 25.2.8-0ubuntu0.24.04.2]
   GLES2: Color2D shader compiled
   GLES2: UI2D shader compiled
   GLES2: Solid3D shader compiled
   GLES2: AlphaTest3D shader compiled
   GLES2: AtlasSolid3D shader compiled
   GLES2: AtlasAlpha3D shader compiled
   GLES2 Driver initialized
   ```
   The process then blocks on network connect (no eqemu server — out of
   this unit's territory) and was killed via `timeout`; the app's log
   buffer only flips to the drain thread on `LOG_FATAL` or graceful
   shutdown (a pre-existing logging design, not touched here), so the
   `LOG_INFO`/`LOG_DEBUG` preset-selection lines from that particular run
   never got flushed before the kill — the FATAL-budget run above supplies
   that evidence instead (real message, not fabricated). Not pursued
   further given the no-eqemu territory restriction.

### Verification numbers

| Check | Result |
|---|---|
| `cmake --build build -j24` (GLES2-config) | clean |
| `ctest --test-dir build` | **1360/1361** (1361 registered — one fewer than the P31-era 1362 baseline; see Deviations. Sole failure: `AudioManagerTest.PreloadCommonSounds`, documented ALSA host exception. `RDPServerFrameTest.UpdateFrameMultipleTimes`, the documented intermittent, failed once across two full runs and passed standalone on retry — consistent with "intermittent") |
| `bash scripts/build-gles2-x86.sh` | clean |
| `scripts/ci/visual_gate.sh` | **PASS**, goldens untouched |
| `build-nogfx` (headless) ctest | **939/940** (sole failure: same ALSA exception) — matches the plan's NEW baseline exactly |
| `bash scripts/build-arm-noble.sh` (default, no `--no-gles2` flag exists anymore) | clean, all binaries built |
| `scripts/refactor/check_file_sizes.sh fable` | no new FAILs (5 pre-existing WARNs, all already over 1500 lines before this unit and not pushed toward the 3000 FAIL line — 2 actually shrank); 3 ERRORs are expected/harmless: `animated_tree_manager.{h,cpp}` were intentionally deleted (not a size problem), `tests/test_zone_connection_flow.cpp` is pre-existing worktree drift unrelated to this unit (not in `tests/CMakeLists.txt`, deleted from disk before this session started — see Deviations) |

### Grep-proof table

| Pattern | Hits under `src/`+`include/`+`scripts/` | Disposition |
|---|---|---|
| `EDT_BURNINGSVIDEO` | 0 code hits (11 explanatory comments across 7 `.cpp`/1 `.h`) | Clean |
| `EDT_OPENGL` | 0 hits | Clean |
| `RenderingBackend::Software` | 0 code hits (4 comments) | Clean |
| `RenderingBackend::OpenGL` | 0 code hits (1 comment) | Clean |
| `AnimatedTreeManager` | 0 code hits (16 comments across 9 `.cpp`/3 `.h` — all P32 rationale notes or two pre-existing unrelated comments in `simulation_worker.h`/`renderer_zone_merge.h`) | Clean |

`docker/irrlicht-drm*/` driver-fork internals (out of scope, untouched):
still contain `EDT_BURNINGSVIDEO`/`EDT_OPENGL` switch cases — confirmed not
touched. `src/client/graphics/CLAUDE.md` retains historical mentions in its
now-rewritten Renderer Tiers section (documenting what was removed, not
live code) — expected per the plan's own exception list.

### Deviations

1. **Test-count baseline shifted by -1** (1362→1361 registered): editing
   `tests/test_constrained_renderer.cpp` to match the new GLES2-only
   `RenderingBackend` (renamed 2 tests, deleted
   `JsonOverride_ExplicitBackendOverridesUseGLES2` — it tested two JSON
   keys both affecting an enum that no longer has multiple values, so the
   test's premise is gone) removed one test case net. Not a regression;
   pass/fail pattern (single ALSA exception) matches.
2. **`manualZoneDrawEnabled_` `#ifdef EQT_HAS_GLES2`/`EDT_BURNINGSVIDEO`
   branch** (`renderer_zone_load.cpp:697-701`) was not named in the plan's
   consumer list (P31's map covered `renderer_zone_stream.cpp`/
   `renderer_frame.cpp`/`renderer_sim_bridge.cpp`/
   `renderer_diagnostics.cpp`) but is a real `EDT_BURNINGSVIDEO` reference
   found during the final grep-proof sweep — collapsed to the
   unconditional `true` case (same value the `#ifdef` branch already
   evaluated to in every build this bar exercises). Deleted per the plan's
   own "all remaining EDT_BURNINGSVIDEO client branches" instruction.
3. **`tests/test_zone_connection_flow.cpp`** exists in `fable` (added by
   T04, merged before this unit's checkout) but was already absent from
   this worktree's disk *and* was never registered in
   `tests/CMakeLists.txt` — pre-existing drift unrelated to this unit
   (confirmed via `git log`, never touched by any P32 edit). Left as-is;
   flagged for the orchestrator rather than silently "fixed" by an
   out-of-territory unit.
4. **`docker/Dockerfile.arm-noble`'s `ENABLE_GLES2` build-arg / `IF
   "$ENABLE_GLES2" = "ON"` branch** and the tool-target (`model_viewer`,
   `s3d_dump`, and similar) `IF(EQT_GLES2) ... ELSE() ...` link-library
   branches in `CMakeLists.txt` were left untouched — the plan named only
   `CMakeLists.txt`'s "Desktop GL mode via Lima" branch (~610-613, the
   main `willeq` target) and `scripts/build-arm-noble.sh`'s flag. These
   other `ELSE()` branches are now unreachable via any supported script
   (no flag reaches `EQT_GLES2=OFF` anymore) but remain constructible via
   raw `-DEQT_GLES2=OFF -DEQT_DRM=ON` cmake invocation — same
   ambiguous-reachability shape P31 flagged, now narrower (retired in
   spirit, not blocked at configure time). Not touched, to stay in the
   plan's named territory.
5. Smoke-test evidence for step 2 is split across two runs (budget-exceeded
   FATAL + successful device creation at a fitting resolution) rather than
   one single continuous run reaching a graceful log-flushed exit — see
   "Smoke + negative outputs" above for why (no eqemu server in this
   unit's territory to let the process reach a natural shutdown; the app's
   log-buffer-flips-only-on-FATAL design is pre-existing and out of scope
   to change).

### Final commit

See commit on branch `task/P32-phase4-final` (hash reported in the
executor's final message).
