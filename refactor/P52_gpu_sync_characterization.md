# P52 — Residual GPU-sync characterization (P38 audit item 6)

**Slot: slot3. Pi-GATED: the Pi is owned by P51 (slot1). Phase 1
(x86 prep) ONLY — no Pi contact of any kind; STOP at READY-FOR-PI
and wait for the orchestrator's explicit go. P02 protocol then
applies (budget ≤8 invocations). Authorized by Will 2026-08-01.**

## Background

The last open item from P38's ranked audit: GPU-sync is ~50-54% of
frame time at realistic load (P38 e5; P11b p95 49.6%) and nobody
knows what the GPU is actually doing. Related known facts: swap
floor ~3.9ms with eglSwapInterval ruled out (P41) and
waitForFlipComplete() named as next suspect; lighting lever = ~1.3ms
of GPU (P41, Will decided keep per-pixel); fill-bound scaling proven
sane (P43); P51 may re-baseline the canonical numbers — read its
outcome before designing Pi runs (its Review will state the honest
instrument configuration; use THAT).

## Work

Phase 1 (x86, no Pi):
1. Instrumentation, flag-gated: per-scope GPU timing. Probe
   EXT_disjoint_timer_query availability (llvmpipe vs Lima may
   differ — make the capability check runtime, degrade gracefully to
   CPU-side sync-point attribution: which calls block —
   eglSwapBuffers, glFinish/glReadPixels, waitForFlipComplete — and
   for how long, per frame phase: zone pass / entity pass /
   particles+UI / swap).
2. Experiment matrix staged as scripts: canonical replay under the
   honest config, with per-phase timers; variants isolating fill
   (resolution within EXACT DRM modes only — P43/P49 rule), vertex/
   tiler load (entity count sweep), swap/pacing (the flip-wait
   hypothesis), state overhead (P42's counters).
3. Commit prep; report READY-FOR-PI with the matrix, predicted
   attributions, and invocation budget per experiment. END TURN.

Phase 2 (after orchestrator go only): run the matrix, attribute the
GPU-sync share to named causes with numbers, deliver a ranked
options note (report-only — fixes are follow-up units) appended to
refactor/perf_truth_audit.md as a dated section.

## Rules

- Instrumentation lands flag-gated, zero overhead when off (P38
  counter precedent); no production behavior changes; goldens
  untouched (gate PASS); no presets.
- Bar: builds clean, ctest baseline, gate PASS, ARM clean, file
  sizes. CSVs → benchmarks/pi_runs/p52_gpu_sync/.
- Do not park on builds; you will not be auto-resumed.

## Review

### Phase 1 (x86 prep) — 2026-08-01, `wt/slot3`, `task/P52-gpu-sync`, cut from `fable` at `a94091d`

No Pi contact of any kind (P51/slot1 owns it this whole phase). Everything
below is x86-only.

#### 1. Instrumentation design

New flag-gated probe, following P38's `gl_call_counters.h` precedent
exactly (single process-wide struct, `enabled` bool guarding every call
site, off by default, never touched by production):

- `docker/irrlicht-drm/gpu_sync_probe.h` (new) — `GPUSyncProbe` struct +
  plain-typed free-function API (`int`/`double`/`bool`/`uint64_t*` only, no
  GL/EGL types in any signature) so application-layer code
  (`src/client/graphics/*`) can `extern`-declare the functions locally
  without including this header or any GL headers — same interop shape as
  the existing `gles2CreateStaticHWBuffer()` precedent. Implementation
  (query function pointers, capability probe, double-buffered query
  bookkeeping) lives entirely in `docker/irrlicht-drm/COpenGLES2Driver.cpp`
  so nothing outside that TU needs GLES2 headers.
- **GPU-side**: `GL_EXT_disjoint_timer_query` (`GL_TIME_ELAPSED_EXT`,
  begin/end), runtime-probed via `eglGetProcAddress` + the `GL_EXTENSIONS`
  string (never assumed — the plan explicitly named llvmpipe-vs-Lima
  divergence risk). Double-buffered across frames so readback
  (`glGetQueryObjectuivEXT` + `GL_QUERY_RESULT_AVAILABLE_EXT`) never stalls
  the frame that issued the query; a not-yet-ready result is dropped for
  that frame, never blocked on.
- **CPU-side sync-point attribution** (always collected when the probe is
  on, independent of GPU query support): `eglSwapBuffers()` wall time and
  the DRM `waitForFlipComplete()` wall time, timed directly around the
  named suspect calls in `docker/irrlicht-drm/CIrrDeviceFB.cpp`'s
  `drmPageFlip()` (ARM) and the x86 fork's `present()`
  (`docker/irrlicht-drm-x86/CIrrDeviceFB.cpp` — `flip_wait_ms` is always 0
  there by construction, no DRM path on x86); `glFinish()`/`glReadPixels()`
  timed at the benchmark tool's own existing GPU-sync point.
- Four phases (`GPUSyncPhase`: ZonePass/EntityPass/ParticlesUI/Swap) hooked
  into the **production** renderer at `src/client/graphics/
  renderer_zone_draw.cpp`'s `RenderPassTimer::OnRenderPassPreRender`
  (zone-pass query brackets the existing manual zone-draw hook exactly;
  entity-pass query opens right after and closes in
  `OnRenderPassPostRender(ESNRP_SOLID)`, tracked via a new
  `entityGPUQueryActive_` bool since the split only exists when
  `manualZoneDrawEnabled_`) and `renderer_frame.cpp` (particles+UI phase
  brackets particle render through the zone-line overlay; Swap phase
  brackets `driver_->endScene()`, followed by exactly one
  `eqtGPUPhaseCollectResults()` call per frame).
- The offline `gles2_zone_benchmark` tool cannot reproduce the
  zone-pass/entity-pass split (it submits draws directly, not through
  `IrrlichtRenderer`'s manual-zone-draw/`RenderPassTimer` machinery) — its
  new `--gpu-sync-probe` flag wraps the combined zone+entity submission as
  a documented proxy `ZonePass` scope, purely to prove the GPU-query
  mechanics round-trip correctly on llvmpipe (see x86 sanity numbers
  below); `gpu_entity_ns`/`gpu_particles_ui_ns` are always `-1` in the
  benchmark's own CSVs by design (only the real `willeq` render loop
  exercises those two phases). New CSV columns (both the benchmark and
  `zone_bench_metrics.{h,cpp}`): `gpu_zone_ns,gpu_entity_ns,
  gpu_particles_ui_ns,gpu_swap_ns` (`-1` = not measured, distinct from a
  real `0`) `,swap_buffers_ms,flip_wait_ms,finish_readpixels_ms`
  (append-only trailing columns, same convention as every prior P0x
  addition).
- Kept the benchmark tool's own footprint small: extracted the call-site
  wrappers into a new `tools/hwcaps/gpu_sync_bench_hooks.h` rather than
  inlining ~80 lines directly in `gles2_zone_benchmark.cpp` (see file-size
  note below for why).

#### 2. Capability probe result on llvmpipe (x86 sanity)

`GPUSyncProbe: GL_EXT_disjoint_timer_query AVAILABLE (fns=1, ext_string=1)`
— llvmpipe (Mesa 25.2.8) exposes it. **Lima's support is unknown and must
not be assumed** — this is exactly why the probe is runtime, not compiled
in; Phase 2's first job on the Pi is reading this same log line for Lima.

#### 3. x86 sanity numbers

`build-gles2/bin/gles2_zone_benchmark --zone befallen --eq-path
/mnt/projects/EverQuestP1999 --res 800 600 --frames 60 --warmup 10
--entities 5 --gpu-sync-probe` (surfaceless EGL, no Pi):

| column | sample values (measured frames) | sane? |
|---|---|---|
| `gpu_zone_ns` (proxy scope) | 1.9M–3.5M (1.9–3.5ms) | yes — same order of magnitude as `collect_cull_ms + submission_ms` (~1.9–2.4ms CPU) |
| `gpu_entity_ns` / `gpu_particles_ui_ns` | always `-1` | expected — benchmark tool doesn't exercise those phases (see above) |
| `gpu_swap_ns` | 0.9M–1.9M (0.9–1.9ms) | non-zero but small — likely trailing-command-completion time from the double-buffered query landing one frame after actual submission, not real swap GPU work; flagged as a Phase-2 interpretation note, not a bug |
| `swap_buffers_ms` | ~0.008–0.010ms | sane — `eglSwapBuffers` on a surfaceless pbuffer is a near-no-op on Mesa (matches the existing code comment) |
| `flip_wait_ms` | always `0.000` | correct by construction — no DRM page-flip path on x86 |
| `finish_readpixels_ms` | 2.5–3.9ms | matches the benchmark's own pre-existing "GPU sync" summary column almost exactly (that IS this glFinish(), now also recorded through the new column) — confirms correct wiring, not a duplicate/independent number |

No negative, wildly-out-of-range, or NaN values across two independent
runs (30 and 60 measured frames). This is the "produces sane per-phase
numbers" bar the plan set for Phase 1 — met, with the explicit caveat that
`gpu_entity_ns`/`gpu_particles_ui_ns` are only exercised by the real
`willeq` binary (compiled clean, see bar results below; not runtime-driven
here — no live game-loop harness in Phase 1's x86-only scope).

#### 4. Staged Pi experiment matrix

`refactor/scratch/p52_pi_gpu_sync_matrix.py` (committed, not run — Phase 2
only, full docstring inside covers all of the below). Reuses
`scripts/pi/pi_common.py`'s existing panic-safe machinery (same idiom as
P38/P41/P43's own scratch scripts) — zero new Pi-side risk surface.
Parameterized on `pi_common.production_config_args()`, which already reads
`tests/goldens/manifest.json`'s `productionConfig` block — if P51's Review
changes that block (the "honest instrument config" note), this matrix
picks it up with no code change; a `HONEST_CONFIG_EXTRA` list is provided
for any P51 flag that ISN'T in that block. Zone: `befallen` (P38/P41/P43's
canonical dungeon zone, static poses, no navmesh dependency). Every
resolution used is confirmed EXACT-DRM-match per P43/P49's rule (never
400x300 again).

| # | label | res | entities | flags | purpose |
|---|---|---|---|---|---|
| A | canonical_e5 | 800x600 | 5 | `--gpu-sync-probe --gl-counters` | primary per-phase attribution + state-overhead cross-check (P42 precedent), one invocation |
| B | stress_e205 | 800x600 | 205 | `--gpu-sync-probe` | entity-sweep high end + swap-floor load-invariance check |
| C | mid_e40 | 800x600 | 40 | `--gpu-sync-probe` | entity-sweep midpoint (~orangepi visible-entity cap) |
| D | zero_e0 | 800x600 | 0 | `--gpu-sync-probe` | sharpest swap-floor isolation (near-zero GPU work) |
| E | fill_lo | 640x480 | 5 | `--gpu-sync-probe` | fill-rate point below shipped res |
| F | fill_hi | 1024x768 | 5 | `--gpu-sync-probe` | fill-rate point above shipped res |

**Budget: 6 invocations, ceiling 8** (per-plan "≤8" — 2 spare for a
SessionAbort recovery or a stale-result re-run, matching P38's own
documented recovery pattern). 300 measured frames/invocation (P08's
fixed-pose convention; deliberately lighter than P38/P41's 1000-frame
convention since this is report-only characterization, not a blessed KPI
— documented as a Phase-2-adjustable choice in the script if percentiles
prove noisy). Each invocation's CSV is verified against its own
`actual_width`/`actual_height` columns (P43/P49 refusal discipline) and
the run is discarded with a loud error if a mode fallback ever fires
unexpectedly.

**Predicted attributions** (full reasoning in the script's docstring, not
repeated here): (1) `gpu_swap_ns` expected near-zero/noisy on Lima too —
the real swap cost should live in `flip_wait_ms` (CPU), not the GPU query;
(2) `flip_wait_ms` predicted roughly constant across A/B/C/D and across
E/F if P41's swap-floor-is-display-cadence hypothesis is right — a load-
dependent `flip_wait_ms` would reopen it in a new form; (3)
`gpu_zone_ns`/`gpu_entity_ns` expected to track `polys_avg`/`draws_avg`
roughly linearly across the A/C/B sweep; (4) A's `tex_binds`/
`tex_param_calls` are a cross-check against P38's state-dedup finding, not
predicted to dominate. Phase 2 replaces every prediction with measured
numbers in a dated `perf_truth_audit.md` section, ranked by measured
contribution — report-only, no fix landed in that unit either.

#### 5. Bar results

- **Build**: `cmake --build build -j24` and `cmake --build build-gles2
  -j24` both clean, zero errors. `bash scripts/build-gles2-x86.sh
  --irrlicht-only` rebuilt the shared patched-Irrlicht static lib clean
  (COpenGLES2Driver.o/CIrrDeviceFB.o compiled with the new instrumentation,
  no warnings from the new code).
- **ctest baseline**: `ctest --test-dir build -j4`: **99% passed, 1 failed
  out of 1673** (`AudioManagerTest.PreloadCommonSounds`, the documented
  ALSA host exception — matches the established baseline shape).
- **Visual gate**: `scripts/ci/visual_gate.sh` — first run FAILed on
  `befallen p95 CPU ms 41.568 > ceiling 40.144` (a ~3.6% overage on a
  x2.0-baseline ceiling); two immediate re-runs both **PASS** cleanly.
  Confirmed transient host-load noise, not a regression from this unit —
  the probe is disabled by default (`g_gpuSyncProbe.enabled = false`
  unless a caller explicitly calls `eqtEnableGPUSyncProbe(true)`, which
  only `gles2_zone_benchmark --gpu-sync-probe` ever does — confirmed via
  `grep -rn eqtEnableGPUSyncProbe`, zero call sites in production code),
  so it cannot have added measurable per-frame cost to a `willeq`-less
  gate run in the first place. Goldens untouched.
- **ARM cross-build**: `scripts/build-arm-noble.sh` clean, zero `error:`
  lines, `gpu_sync_probe.h` correctly propagated through the Docker COPY/cp
  chain (verified in the build log) and compiled into the ARM
  `COpenGLES2Driver.o`/`CIrrDeviceFB.o`. `build-arm-noble/bin/willeq`:
  ELF 32-bit ARM, 338M unstripped with debug symbols (matches the script's
  standard non-stub shape).
- **File sizes**: `scripts/refactor/check_file_sizes.sh a94091d` —
  **pre-existing FAIL, not introduced by this unit**:
  `tools/hwcaps/gles2_zone_benchmark.cpp` was already 3015 lines (over the
  3000 FAIL threshold) at this branch's cut point `a94091d`, confirmed via
  `git show a94091d:tools/hwcaps/gles2_zone_benchmark.cpp | wc -l` — 15
  lines over threshold before this unit touched anything (P49's 2026-07-31
  review recorded it at 2982/WARN; some intervening unit between then and
  `a94091d` pushed it into FAIL territory without flagging it, an
  unidentified hygiene gap worth a future audit). This unit's own additions
  were extracted into a new file (`tools/hwcaps/gpu_sync_bench_hooks.h`,
  80 lines, well under WARN) specifically to minimize further growth of an
  already-oversized file rather than inlining ~80 lines directly; net
  addition to `gles2_zone_benchmark.cpp` itself is +29 lines (3044 total).
  Recommend a future hygiene unit split this file (it has been WARN since
  at least P38 and crossed FAIL before this unit arrived) — out of P52's
  own scope to fix. The three pre-existing WARNs (`COpenGLES2Driver.cpp`
  2133 lines, `irrlicht_renderer.h` 2090 lines, `renderer_frame.cpp` 1662
  lines) are all still WARN, not newly FAIL, after this unit's additions.

#### 6. Files touched / added

`docker/irrlicht-drm/gpu_sync_probe.h` (new), `docker/irrlicht-drm/
COpenGLES2Driver.cpp`, `docker/irrlicht-drm/CIrrDeviceFB.cpp`, `docker/
irrlicht-drm-x86/CIrrDeviceFB.cpp`, `docker/Dockerfile.arm-noble`, `docker/
Dockerfile.arm64-noble` (also fixed a pre-existing gap there: `gl_call_
counters.h` was never copied for arm64-noble, meaning `COpenGLES2Driver.cpp`
has been referencing a missing header on that Dockerfile since P38 — added
alongside `gpu_sync_probe.h`'s own copy lines, same "for consistency"
rationale P49 used when it touched this file), `scripts/build-gles2-x86.sh`,
`include/client/graphics/irrlicht_renderer.h`, `src/client/graphics/
renderer_zone_draw.cpp`, `src/client/graphics/renderer_frame.cpp`,
`tools/hwcaps/gpu_sync_bench_hooks.h` (new), `tools/hwcaps/
gles2_zone_benchmark.cpp`, `tools/hwcaps/zone_bench_metrics.{h,cpp}`,
`refactor/scratch/p52_pi_gpu_sync_matrix.py` (new, staged/not run).

#### 7. Deviations from the plan

- Entity-pass/particles-UI GPU queries are production-only (not exercised
  by the offline benchmark tool) — named explicitly in the plan's item 1
  ("per-scope GPU timing") but the benchmark's draw-submission architecture
  makes a true split impossible there; documented as a proxy scope rather
  than silently misrepresenting it as the real split.
- File-size bar hit a pre-existing FAIL this unit did not cause (see §5) —
  minimized rather than silently absorbed or claimed clean.
- No other scope growth; goldens/production behavior/defaults untouched.

**READY-FOR-PI.** Waiting for the orchestrator's explicit Phase-2 go-ahead
after P51 releases the Pi. Not polling.

### Phase 2 (Pi) — 2026-08-01, same branch, go-ahead from the orchestrator
### after P51 released the Pi (pristine exit, CmaFree 88,248 kB)

**Partial — budget exhausted before the full matrix produced usable data.
Two real methodology bugs found; one fixed within budget, one identified
but unfixed. Full ranked attribution, options note, and both bugs'
details: `refactor/perf_truth_audit.md`'s 2026-08-01 P52 section (this
is the plan's required deliverable location, so the full writeup lives
there, not duplicated here). Summary below.**

Setup: merged `fable` (P51's re-baseline, no code changes) into this
branch — clean merge, no conflicts. Rebuilt the ARM binary in this
worktree (`ARM_BUILD_BIN_DIR` override — `scripts/pi/env.sh` defaults to
the main worktree's `build-arm-noble/`, which does not have this unit's
instrumentation) — clean, non-stub. Own pre-flight gate run before any
timed work (ssh reachable, CmaFree 89,424 kB healthy, `preflight()`
asset/binary-freshness checks passed).

**First job per the plan** — checked the `GL_EXT_disjoint_timer_query`
capability line on Lima before trusting any GPU-side number:
**NOT available** (`fns=1, ext_string=0` — non-null `eglGetProcAddress`
results but no extension-string advertisement; the dual-signal capability
check this unit's Phase 1 built specifically to catch this class of false
positive worked as designed). Confirmed on both real (post-fix) Pi
invocations. All `gpu_*_ns` CSV columns are `-1` on every Pi run this
unit produced, correctly — GPU-side timer queries are not usable on this
hardware; fell back entirely to the CPU-side sync-point attribution path,
per the plan's own contingency.

**Bug 1** (found, fixed within budget): the original 6-invocation matrix
(`refactor/scratch/p52_pi_gpu_sync_matrix.py`) used `pi_common.py`'s
`--camera-pose`-flag-building helpers — routes into
`gles2_zone_benchmark.cpp`'s structurally separate static-pose capture
loop, which runs NONE of this unit's per-frame instrumentation (nor,
found the same way, the pre-existing P08/P38 phase-timing/GL-counter
columns). All 6 CSVs read `0`/`-1` across every relevant column,
confirmed by diffing against P51's own `--path-file`-based CSVs (real,
non-zero values, same binary shape). Fixed by switching to `--path-file`
(P38's committed `befallen_fixedpose_300f.path` fixture, the same
moving-path loop P51's own scaling probe used) —
`refactor/scratch/p52_pi_gpu_sync_pathfile_fix.py`.

**Bug 2** (found, NOT fixed — no budget left): the 2-invocation Bug-1 fix
run omitted the shipped-preset flags (`--merge-zone-draws
--composite-scenegraph --eqe1-path`) P38/P41/P51's own canonical replays
all carry. `B_stress_e205_pathfile` measured 232.7ms avg total frame time
— ~7x P51's honest 34.3-34.6ms e205 band — a config-fidelity confound
(unmerged zone draws + non-composite entity submission dominates at high
entity count), not a real number. Must not be compared against the KPI
band. No further Pi invocation was available to correct it: this unit's
≤8-invocation ceiling (2 reserve) was spent 6-on-Bug-1 + 2-on-the-fix.

**What the 2 recovered runs DO establish** (structural findings, not
magnitude-sensitive to Bug 2 — full reasoning in `perf_truth_audit.md`):
CPU-side sync-point attribution (`swap_buffers_ms`/`flip_wait_ms`/
`finish_readpixels_ms`) works correctly on real Lima hardware and
produces sane numbers at both loads; `waitForFlipComplete()`/
`eglSwapBuffers()` are NOT where the CPU blocks (~0.10-0.15ms total,
both loads) — the block is almost entirely `glFinish()` GPU-completion
wait (~11-90ms) — **a null result for P41's own swap-floor hypothesis**
(perf_truth_audit finding (c)/(e), which named `waitForFlipComplete()`
as the next suspect for the ~14.4-15.0ms wall-clock floor). The entity
sweep the orchestrator specifically asked this unit to reproduce/refine
(P51's +0.455ms(e5)/+2.22ms(e205) entity-promotion curve) was **not**
meaningfully measured — Bug 1 nulled the original matrix, Bug 2 makes the
recovery run's entity-count delta uninterpretable — and the fill-rate
isolation (E/F) never ran at all.

**Bar**: no new source-code changes this phase (Phase 2 is pure
measurement/analysis on top of Phase 1's already-committed
instrumentation) — build/ctest/gate/ARM-cross bar unchanged from Phase 1's
own results (see above), re-verified unaffected since nothing here
touches C++.

**Pi discipline**: 8/8 invocations of the ≤8 ceiling used (6 invalid +
2 valid-instrumentation-but-Bug-2-confounded). Two SessionAbort-triggering
CmaFree episodes (73,540→812 kB after the first matrix; 69,208→404 kB
after the e205 recovery leg) — both the documented episodic CMA-drain
pattern already characterized by P02/P51, both recovered via
`sync && drop_caches` (no reboot), confirmed back to 88,944 kB and 85,856
kB respectively. Final pristine exit independently re-verified after
cleanup (not just trusted from the script's own governor-restore):
governor `ondemand` on all 4 cores, `/tmp` swept of every `p52_*`
artifact (the matrix script's `rsync_from()` pulls but does not delete
remote files — a real gap, fixed by explicit `rm` in the cleanup step,
not by script changes given budget exhaustion; flagging for the
follow-up unit to build a proper remote-cleanup step into
`pi_common.py`'s `run_zone_bench_generic` family), no
`gles2_zone_benchmark`/`willeq` process, both `/dev/dri/card{0,1}` free,
CmaFree 85,880 kB (in line with P51's own 88,248 kB pristine-exit value).

**Deliverables**: `refactor/perf_truth_audit.md` 2026-08-01 P52 section
(ranked attribution + options note, the plan's required deliverable),
`refactor/deferred.md` P52 entry, both matrix scripts (original +
path-file fix, both committed as the record of what was tried and why),
CSVs under `benchmarks/pi_runs/p52_gpu_sync/` (6 invalid + 2 recovered,
all kept, all labeled).

**Deviation from the plan**: the plan's Phase 2 charter ("attribute the
GPU-sync share to named causes with numbers, deliver a ranked options
note") is only partially met — a real percentage-of-frame number was not
produced (Bug 2), though the mechanism-level question (swap/flip vs.
GPU-completion) was answered with high confidence, and Lima's lack of
GPU-timer-query support is now a definitive, load-bearing fact for any
future characterization work on this hardware. Recommend the follow-up
unit named in the options note; not requesting additional budget
unilaterally mid-unit per the ≤8 ceiling's own discipline — reporting for
the orchestrator's call instead.
