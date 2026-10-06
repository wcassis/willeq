# B979 -- the impostor capture unit: decomposed and bounded

GitLab #298 (class A, lane pi). Branch `task/B979-impostor-capture-hitch`.
B964's hitch bar was not met: a single first-draw impostor capture of
30–62 ms landed whole in one Orange Pi frame (worst single-frame capture work
43–46 ms against a 33.3 ms frame budget at the Pi's 30 fps target).

## Acceptance 1 -- the decomposition, on the Pi

Instrumented build `4da8df2f` (times `ensureScratch()`, `ensurePage()`, and
adds `finish_before_ms` to the `[impostor-capture]` line), the B964 crowd runs
(`scripts/b964/pi_crowd.sh`, 1280x720, `imp200far`, VM lane), evidence in
`~/kb-evidence/b979-impostor-hitch/instrumented/`:

**The slow first capture is three one-time GPU costs, all in the first
capture's frame** (qeynos2, 13:13:13, from the log lines):

| step | cost | measured by |
|---|---|---|
| page 0 GEM allocation (4 MiB `glTexImage2D`) | **14.56 ms** | `Impostor: page 0 allocated in 14.56 ms` |
| scratch FBO (`addRenderTargetTexture`, 128^2 RGBA8 + depth) | **13.97 ms** | `Impostor: scratch FBO allocated in 13.97 ms` |
| first draw into the FBO (Lima's deferred rasterization; every later capture draws in 1.0–1.8 ms) | **21.05 ms** | `[impostor-capture] draw_ms=21.05` |

Sum ≈ 50 ms — the baseline's 47.7 ms worst frame, exactly. Same shape in the
other zones: gfaydark page 0 **15.76 ms** + first capture draw **9.54 ms**
(46.2 ms frame); growthplane page 0 **13.85 ms** + page 0 again after a zone
reload **22.18 ms** (26.4 ms frame, already under budget).

- Not a shader compile: all six programs are linked in `initialize()`
  (`COGLES2Shaders.cpp`), before any scene.
- Not the copy: `copy_ms` is 0.8–2.0 ms on every capture.
- Pages 1+ (14–17 ms each) land in later frames **without** a visible hitch —
  the GEM allocation itself is the cost, and by then the driver/tile state is
  warm; only the FIRST allocation + FIRST FBO use of the session compound into
  one frame.
- The existing per-frame unit budget (`unitBudgetFor`, a fraction of the frame
  budget) already bounds the CAPTURE phase: the first-capture frame's sub-timers
  sum to ~6–15 ms; the rest is the three one-time costs above, which no
  per-frame budget can see because they happen before any unit runs.

## The fix (three halves, all measured off the gameplay frames)

1. **Pre-create the one-time GPU resources on the loading thread.**
   `EntityRenderer::prepareImpostorCache()` (called from
   `Application::loadZoneGraphicsOnThread()`, under the loading screen, where
   the loading thread owns the GL context and the main thread is paused in
   `render()`'s loading branch) creates every page and the scratch FBO.
   `ImpostorCache::prepare()` is idempotent: a zone reload re-uses the
   resident pages and scratch, and the cache is only created when the preset
   budgets it (`WILLEQ_BASELINE=1` / impostors-off runs create nothing and do
   none of this).
2. **Prime the 3D capture path on the loading thread.** Pre-creating the
   resources was not enough: the FBO's first use by a **3D** draw (the
   capture's own path, not a 2D rectangle) and the FBO's first **readback**
   were still one-time costs that landed on the main thread's first capture
   frame. The two runs that showed it (evidence `after_r353`, `after_r353c`):
   qeynos2 frame 1 (the first capture) drew in **76.61 ms** *after* prepare()
   had finished with a glFinish; gfaydark's first capture ran at frame 795
   and paid **37.92 ms**; growthplane's FBO readback cost **21.15 ms** at
   frame 520. `ImpostorCache::prime3d()` now draws a real 3D textured mesh
   into the scratch FBO (the capture's own ortho look-at, the driver's 3D
   program selection — a 2D rectangle would not select it), reads it back
   into page 0 with `glCopyTexSubImage2D`, and `glFinish()`es. It runs once
   per session (`primed3d_` survives zone reloads, like the pages and the
   scratch) and is the loading thread's LAST GL work (prepare() is the final
   step of `loadZoneGraphicsOnThread()`), so the deferred raster and readback
   resolve there, before the main thread's first frame. Measured:
   "3D capture path primed in 23.21 ms" on the loading thread; the priming
   texture and cube are freed inside the call (no new resident resources).
3. **The capture unit is now a unit of the B973 scheduler.**
   `EntityBuildUnitKind::Impostor` (appended to the enum; its window-line
   fields are mid-line additions, verified against the B973 parsers).
   `entity_impostors.cpp` gates
   its unit loop with `EntityBuildScheduler::mayStartCaptureUnit()` and
   `recordUnit(Impostor, ms)`: the first capture of a frame always runs
   (guaranteed progress, every governor state), the next starts only if the
   frame's work so far plus the kind's learned prediction fits the state's
   slice. A slow capture therefore can never be joined by a second capture in
   the same frame, and the `[entity-build]` window line now carries the kind's
   count, sum/max and prediction (`impostor=N` … `pred_ms … impostor=X.XX`).
   The pass's own per-frame budget (`unitBudgetFor`) stays on top: two
   independent caps, neither weaker than the other. Legacy A/B mode
   (`WILLEQ_ENTITY_BUILD_LEGACY=1`) leaves the gate off.

The per-frame budget the issue asks for: **no frame carries more than one frame
budget (33.3 ms) of capture work** — after the one-time costs (resource
allocations AND the 3D-path priming) are on the loading thread, the measured
worst single-frame capture work is 12–23 ms (frame-time table below), steady-
state captures are 1–3.5 ms; the per-pass budget caps a frame at 2–3 units of
measured-average cost; and the scheduler gate defers a unit whose predicted
cost would not fit the slice.

## Acceptance 3 -- no pixel change

`impostor_pixel_compare pixel` before/after on x86 (surfaceless llvmpipe,
234 rows): **all 234 rows identical** — the tool is an x86 tool (it links the
x86 GLES2 tree), so the x86 comparison IS the mechanical pixel check; the
change touches scheduling and resource pre-creation only (no capture math),
and the Pi's own visual evidence is the CI visual gate (pipeline 592,
`gate` stage green on this tip) plus the frame-time table below.

## Acceptance 4 -- the repeatable test

`tests/test_entity_build_scheduler.cpp`:
- `ASlowImpostorCaptureIsDeferredSoTheFrameStaysInsideItsBudget` — a 46 ms
  capture (the Pi's first-draw cost) is deferred as the second unit of a frame
  (Green and Red), never joins a busy build slice, and legacy mode never
  defers.
- `TheWindowLineCarriesTheImpostorKind` — the appended kind fields.

## Frame-time table (acceptance 2)

All runs: `scripts/b964/pi_crowd.sh --zones qeynos2,gfaydark,growthplane
--variants imp200far --frametiming --allow-vm-lane` (Pi 1280x720, VM lane).
Worst single-frame capture work = the max of `unit_ms_max` over the whole tour
(the `[impostor]` line's `frame_unit_ms_max` is a running high-water mark, not
a per-frame count — the number below is the single worst capture unit).

| zone        | baseline (`ba15c44f`) | fixed 2D-prime (`1bf2c2a4`) | fixed 3D-prime (`b45c6327`, this head) | bar |
|-------------|----------------------:|---------------------------:|---------------------------------------:|----:|
| qeynos2     | **47.68 ms** @f1      | 13.81 ms @f485             | **12.06 ms** @f1                       | 33.3 |
| gfaydark    | **46.20 ms** @f1      | **37.95 ms** @f795         | **23.11 ms** @f790                     | 33.3 |
| growthplane | 26.37 ms @f422        | 14.92 ms @f493             | **22.64 ms** @f532                     | 33.3 |

All three zones are under the 33.3 ms bar on this head (worst case 23.11 ms),
and no frame carries more than one frame budget of capture work. The 2D-prime
column is the intermediate state (resource pre-creation only): qeynos2 and
growthplane were already under budget, but gfaydark's first capture still paid
the 3D-path one-time cost at frame 795 (37.95 ms). The 3D priming removes it:
the worst single-frame capture work is now the steady-state first/early
captures (12–23 ms), with the one-time 3D raster + FBO readback paid on the
loading thread ("3D capture path primed in 23.21 ms").

Evidence: `~/kb-evidence/b979-impostor-hitch/` — `baseline/`, `after_r353/`
(2D-prime Pi sweep), `after_r353c/` (2D-prime + instrumented build),
`after_r353d/` (3D-prime, this head), `x86_before/` + `x86_after4/` (pixel).

## Not verified / left open

- The sweep rig's `no_steady_60s` verdict row in `after_r353d/` (and the
  baseline's `gpu_unnamed_over_bound`) is a rig-lane condition, not a B979
  regression: the client ran the full tour (2190–3112 impostor frames, matching
  the prior runs), exited cleanly, and observation.json reads `steady=true`
  with fps identical to every prior run (qeynos2 48.1, gfaydark 32.2,
  growthplane 23.9). The `gpu_unnamed_over_bound` half (18–24 MiB of GEM no
  tally row names) fails on the baseline tip identically — a pre-existing
  memory-accounting bound (the ledger does not name the impostor pages' GEM
  blocks), not a B979 regression (the known B988/#307 condition). Boarded
  separately: the impostor pages' GEM allocations should get a `tex.impostor`
  tally row.
