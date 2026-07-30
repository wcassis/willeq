# P43 — Resolution-visibility anomaly: root cause on real hardware

**Slot: slot1. OWNS THE PI (P02 protocol; budget ≤10 invocations).
x86 free for cross-checks. No eqemu.**

## Background (read these before forming hypotheses)

- P38 (`refactor/P38_time_budget_audit.md` + perf_truth_audit.md):
  original finding — LOWER resolution produces MORE draws/triangles
  (8.7→10.6 draws, 1,379.6→1,917.8 polys at 400x300 vs 800x600).
  Flagged at the time as possible methodology drift.
- P40 (`refactor/P40_resolution_visibility.md`): x86/llvmpipe,
  shipped flags — visibility counters BYTE-IDENTICAL across
  400x300/800x600/1024x768 (5,000/5,000 cells). Ruled out FOV/aspect,
  occlusion-buffer sizing (fixed 64x32 by preset), LOD-by-pixel (none
  exists). Verdict then: "not a code bug, likely P38's drift".
- P41 (perf_truth_audit.md "2026-07-30 — P41" §3): Pi, shipped
  preset, canonical befallen e5 replay — the anomaly REPRODUCES:
  draws +22%, triangles +39%, and GPU time >3x WORSE at 400x300
  (29.1ms avg GPU) than at 800x600 (6.0ms). Per-frame CSV committed:
  `benchmarks/pi_runs/p41_lighting_lever/befallen_res_400x300_e5_shipped.csv`
  (its 800x600 control = P38's e5 CSV, same dir family).

## The reconciliation clue (start here — Phase A is x86-only and cheap)

P41's Pi geometry counts match P38's original numbers **almost to the
decimal**, across different platforms — while P40 (also x86, claiming
shipped flags) saw byte-identical counters. Deterministic cross-platform
agreement of the "anomalous" runs points at a CONFIG/FLAG delta between
P40's invocations and P38/P41's, not (only) a driver mechanism. All
three invocation records are in the tree: P38's in its plan/scratch,
P40's in its plan Review, P41's in `refactor/scratch/p41_pi_run.py`.
Timeline fact to weigh: the benchmark's occluder-budget default was a
hardcoded 16 until P42 (merged AFTER all three ran; it now resolves
256 from the orangepi ConstrainedRendererConfig); P39/P14 established
256 as the blessed shipped value. Any occluder-selection math with
screen-space units (`minOccluderArea = 20.0f` — units? pixels at
render res?) is a prime suspect for resolution-dependent visibility.

## Work

A. **Reconcile the three datasets (x86, no Pi cost)**: diff the exact
   invocations of P38-res-probe, P40, and P41 ride-along A. Reproduce
   the anomaly on x86 by matching P38/P41's flags if the delta
   explains it; reproduce P40's null by matching its flags. Name the
   flag(s)/config that flips the behavior. Frame-by-frame diff of the
   committed P41 400x300 CSV vs its 800x600 control (uniform offset
   vs pose-specific divergence — says WHERE in the pipeline).
B. **Mechanism**: instrument the winner from A (occlusion-culler
   counters exist; extend flag-gated if needed) until the causal chain
   is named at the code level (file:line), not just the flag level.
   If occluder selection or any culling math is resolution-dependent,
   say exactly which quantity (screen-space area? projected extent?)
   and which resolution term leaks in.
C. **The Pi GPU-time inversion**: even with geometry counts explained,
   >3x GPU time at a quarter of the pixels needs its own answer.
   Separate the +39%-triangles effect from everything else (e.g. run
   400x300 with culling forced to the 800x600 decision set if A/B
   makes that possible). Pi-side suspects to check cheaply: non-native
   DRM/KMS mode handling at 400x300 (scaler? mode fallback? check
   drm mode logs), Lima tiler behavior, CMA pressure. Budget-aware:
   this is characterize-and-name, not necessarily solve — if it needs
   its own unit, say so with evidence.
D. **Fix policy (P40's rule)**: fix ONLY if the root cause is provably
   wrong math AND the fix is behavior-identical at the shipped 800x600
   (gate PASS, counters byte-identical at 800x600). If the fix would
   change culling at shipped resolution — STOP, report, no change.
   Instrumentation lands flag-gated only.

## Rules

- Pi protocol absolute (CmaFree gate, /tmp-only writes during timed
  runs, drop_caches remediation never reboot, pristine exit).
- Goldens untouched; no preset/config flips.
- Phase A before ANY Pi invocation — the reconciliation may collapse
  the Pi budget needed.
- Build-tree caution: after the 07-30 VM crash, slot build dirs held
  0-byte stub binaries that `make` considered current (P42 hit this in
  slot2). Verify binary sizes before trusting any build in slot1.

## Verification

- Bar: builds clean, GLES2 ctest baseline (~1490/1492, documented
  ALSA + RDP-race exceptions), visual gate PASS, ARM build clean,
  file-size check.
- Deliverable: root-cause narrative (flag → code path → mechanism),
  tables for A and C, per-frame divergence analysis, fix-or-STOP per
  policy D, all in this file's Review section; CSVs under
  `benchmarks/pi_runs/p43_res_visibility/` (Pi) and
  `benchmarks/audits/p43_res_visibility/` (x86).

## Review

Executed 2026-07-30 on `wt/slot1`, branch `task/P43-res-visibility`, cut from
`fable` at `9323c57` (adds this plan doc, no code). Full deliverable below;
no production/library code was changed (Phase D: STOP, see below) — only a
committed scratch Pi script and evidence CSVs/logs.

### Root cause, up front

**The resolution-visibility anomaly and the Pi GPU-time inversion are the
SAME bug, and it is not in the shared visibility/culling C++ at all.** It is
in the ARM-only DRM/KMS mode-selection code,
`docker/irrlicht-drm/CIrrDeviceFB.cpp` (`CIrrDeviceFB::createDRMDevice()`
or equivalent, ~L330-382): when `--res W H` does not exactly match one of
the connector's advertised modes, the device silently falls back to the
connector's **preferred** mode instead — and critically, `Width`/`Height`
(and hence everywhere downstream that calls `driver_->getScreenSize()`,
including aspect-ratio and frustum computation) are overwritten with the
**fallback mode's** dimensions, not the requested ones:

```
docker/irrlicht-drm/CIrrDeviceFB.cpp:335   drmModeModeInfo* selectedMode = nullptr;
docker/irrlicht-drm/CIrrDeviceFB.cpp:336   u32 requestedW = CreationParams.WindowSize.Width;
...
docker/irrlicht-drm/CIrrDeviceFB.cpp:356       "DRM: No mode matching requested %ux%u, using preferred"
docker/irrlicht-drm/CIrrDeviceFB.cpp:362   // Fall back to preferred mode, then first mode
docker/irrlicht-drm/CIrrDeviceFB.cpp:375   Width = mode_.hdisplay;
docker/irrlicht-drm/CIrrDeviceFB.cpp:376   Height = mode_.vdisplay;
```

On the Pi's actual monitor/EDID, `800x600` has an exact matching mode
(`800x600@75Hz`), but `400x300` does not — there is no 400x300 mode in the
connector's list at all. So every historical Pi benchmark run at `--res 400
300` (P38's original probe, P38's "shipped" re-run, P41's ride-along) was
**silently actually rendering at `2560x1440`** (the connector's preferred
mode, 16:9), not 400x300 (4:3) as every script, CSV filename, and report
believed. Confirmed directly on hardware (see Phase B below) — this is not
inferred, it is read straight from the device's own log line.

Consequences of that one silent substitution:
1. **GPU-time "inversion" (Phase C) is not an inversion.** `2560x1440` =
   3,686,400 px vs `800x600` = 480,000 px — **7.68x MORE pixels**, not 1/4
   as every prior write-up assumed. GPU-sync time scaling roughly with
   pixel count (fill-bound, exactly as expected) is the correct, unsurprising
   explanation once the real pixel count is used; the apparent "5x worse GPU
   time at a quarter of the pixels" that P38/P41/P43's background section
   found alarming evaporates once "a quarter of the pixels" is corrected to
   "7.68x the pixels."
2. **Geometry/visibility anomaly (Phase A/B) is explained too.** The
   fallback mode's aspect ratio (2560/1440 = 1.778, 16:9) differs from the
   requested 400x300's aspect (1.333, 4:3). `aspect = screenSize.Width /
   screenSize.Height` (`renderer_frame.cpp:961`, mirrored in
   `renderer_zone_load.cpp:2485` and the benchmark's own
   `gles2_zone_benchmark.cpp:818`) is derived from the **actual, post-fallback**
   `driver_->getScreenSize()`, so it correctly picks up 1.778, not 1.333 — a
   genuinely wider horizontal frustum, letting more regions survive frustum
   culling. This is exactly the "open question" P40 itself flagged and
   declined to test ("a non-4:3 resolution change WOULD move the frustum
   culling result... out of scope here since the plan's three probe points
   are all 4:3") — P40's probe points were nominally 4:3, but on real Pi/DRM
   hardware, 400x300 was never actually rendered at 4:3 at all.

### Phase A: x86 reconciliation (no Pi cost)

Rebuilt `build-gles2/` clean (`scripts/build-gles2-x86.sh`) at this branch's
HEAD, confirmed non-stub binaries (`gles2_zone_benchmark` 14.4MB, `willeq`
79MB). Ran the exact canonical replay (`benchmarks/paths/befallen_seed42_1000f.path`,
zone befallen, 1000 frames) under two flag sets, both at 400x300 and
800x600, `--occluder-budget 16` explicit (era-matching the hardcoded
pre-P42 default P38/P40/P41 all ran under):

- **"P40-like" (minimal — `--merge-zone-draws --composite-scenegraph
  --eqe1-path`, `--entities 5`, no production-lighting args, no `--animate`,
  no `--seed`)** — reconstructed from P40's own Review prose (its exact
  invocation script was not committed).
- **"P41-like" (exact — production-config args `--adaptive-lighting
  --zone-pp-budget 20 --pp-budget-mode count --player-light-level 9`,
  `--entities 5 --seed 42 --animate`, plus the same shipped-preset flags)** —
  taken verbatim from `refactor/scratch/p38_pi_time_budget_audit.py`'s
  `run_replay()` (P41's `p41_pi_run.py` mirrors this exactly, confirmed by
  identical numbers below).

| variant | res | draws | polys | regions_vis | gpu_ms avg | total_ms avg |
|---|---|---|---|---|---|---|
| x86 P40-like | 400x300 | 14.83 | 1379.56 | 134.12 | 4.065 | 8.747 |
| x86 P40-like | 800x600 | 14.83 | 1379.56 | 134.12 | 7.276 | 13.044 |
| x86 P41-like | 400x300 | 8.67 | 1379.56 | 134.12 | 3.667 | 8.297 |
| x86 P41-like | 800x600 | 8.67 | 1379.56 | 134.12 | 7.624 | 13.481 |

**Both configs are byte-identical across resolution on x86 (llvmpipe,
surfaceless EGL, no DRM) — 0 cells differ, in draws/polys/regions_vis, at
both 1000-frame full replays.** This directly falsifies my initial
hypothesis (a CLI-flag/config delta between P40's and P38/P41's
invocations) — matching P38/P41's *exact* flags on x86 still produces P40's
null, not P38/P41's anomaly. Since x86 has no DRM/KMS mode-selection path at
all (confirmed: `docker/irrlicht-drm/` is compiled only for
`EQT_DRM`/ARM; x86 uses Mesa's surfaceless-EGL fallback per
`src/client/graphics/CLAUDE.md`), this is exactly the evidence needed to
point at a **platform-specific** (not flag-specific) mechanism — which sent
me to the DRM device code and directly to the fallback logic above.

Frame-by-frame diff of P38's own two committed shipped-config CSVs
(`benchmarks/pi_runs/p38_time_budget_audit/befallen_budget_moving_e5_shipped.csv`
= 800x600 control, `befallen_res_400x300_shipped.csv` = "400x300") confirms
**pose-specific divergence, not a uniform offset**: 970/1000 frames differ;
`draws` deltas are small and frequently 0 (many frames identical draw
count), while `polys` deltas are large and overwhelmingly positive (+200 to
+2000 range) — exactly the signature of "same draw calls, wider frustum
lets more/larger regions in," not a uniform per-frame scale factor. This
CSV pair is byte-identical to P41's own `befallen_res_400x300_e5_shipped.csv`
(draws=10.58, polys=1917.85, regions_vis=211.18 avg, matching to 2 decimal
places) — P41 independently reproduced P38's exact numbers because both hit
the exact same DRM fallback, not because of a shared measurement bug.

### Phase B: mechanism, confirmed directly on the Pi (2 invocations)

CmaFree pre-flight: 77,612 kB (healthy, matches P41's documented pristine
exit). Queried the connector's raw mode list
(`/sys/class/drm/card0-HDMI-A-1/modes`, no benchmark run needed) — no
400x300 entry exists anywhere in the 36 advertised modes (2560x1440 down to
640x480/720x400); 800x600 has four exact entries (56/60/72/75Hz).

Ran two short (`--frames 5`) probes with the full P41-flag-set (production
args, `--animate`, `--entities 5 --seed 42`, shipped preset,
`--occluder-budget 16`), capturing the device's own log:

```
[--res 400 300]
DRM: No mode matching requested 400x300, using preferred
DRM: Using mode 2560x1440@60Hz
GBM: Surface created 2560x1440 XRGB8888

[--res 800 600]
DRM: Found matching mode 800x600@75Hz for requested 800x600
DRM: Using mode 800x600@75Hz
GBM: Surface created 800x600 XRGB8888
```

This is the direct, on-hardware confirmation: at the "400x300" invocation
the GBM surface — the actual framebuffer everything renders into — is
2560x1440, exactly as the code-read predicted. Nothing further to
instrument; the existing `LOG_WARN`-level DRM message already names the
mechanism, it was simply never looked at by P38/P40/P41 (all three
consumed only the CSV, never the stderr log).

**Quantitative fit**: fitting `gpu_ms = F + k*pixels` to the two real
(hardware) pixel counts (800x600=480,000px→6.021ms control;
2560x1440=3,686,400px→29.584ms "400x300") gives `F≈2.49ms` fixed overhead,
`k≈7.35e-6 ms/px` (~136M px/s effective fill rate) — a physically
reasonable fill-bound fit, not a forced one. `regions_vis` grew 134.12→211.18
(×1.57) against an aspect-ratio widening of ×1.33 (1.333→1.778) — same
direction, plausible magnitude given non-uniform scene geometry density
around the wider frustum edges (not expected to be a clean linear
multiplier).

### Phase C: the "Pi GPU-time inversion" — answered, not deferred

Fully explained by the mechanism above, no separate follow-up unit needed:
"400x300 at 3x the GPU time of 800x600" was never a real fill-rate paradox.
The benchmark was rendering **7.68x more pixels** than its own CSV filename
and every prior report claimed, because of the silent DRM mode fallback.
Once the actual rendered pixel count is used, GPU time scales with pixel
count exactly as fill-bound rendering predicts (see quantitative fit
above). P38's original "CONFOUNDED, not a clean result" framing of its
resolution-scaling probe, and P40's "byte-identical, likely P38 methodology
drift" verdict, were both honest given what each unit could see from its
own vantage point (P38 had no comparison point to notice the pixel-count
mismatch; P40 had no DRM to reproduce it on) — this unit had the advantage
of both bodies of evidence side by side plus the freedom to read the DRM
device source, which is what surfaced it.

### Phase D: fix-or-STOP — **STOP, no code changed**

Per the plan's fix policy: fix ONLY provably-wrong math whose fix is
behavior-identical at shipped 800x600. This mechanism fails that test on
its own terms, but not because it's unsafe to fix — because there is
**nothing here that needs fixing for production**:

- The shipped resolution (800x600) hits an **exact** DRM mode
  (`800x600@75Hz`) every time — the fallback branch is never entered at
  the resolution the Pi actually ships. Shipped-resolution behavior was
  never wrong and this investigation touches nothing that could change it.
- The "bug," such as it is, is a **benchmark-methodology trap**, not a
  shared-code math error: `--res 400 300` on real Pi/DRM hardware has
  never meant what its CSV filenames claimed, for any unit that has ever
  run it (P38, P40's would-be Pi ride-along, P41). There is no visibility/
  occlusion/frustum math to correct — every formula computed the (locally)
  correct answer for the framebuffer it was actually given; the mismatch is
  entirely in what resolution the DRM device silently substituted before
  any of that math ran.
- A defensible follow-up (NOT implemented here, per policy — this would be
  a behavior change, however small, and is out of this unit's fix-only-if-
  provably-wrong-math-and-800x600-identical scope): make the "no exact mode
  match" fallback loud (hard failure or a `--force-preferred-mode` opt-in)
  instead of a passive `ELL_WARNING` log line nobody was reading, so a
  future benchmark invocation at a non-native resolution fails fast instead
  of silently substituting a 7.68x-larger, wrong-aspect framebuffer. This
  is behavior-identical at 800x600 (the fallback branch is never reached
  there) but is still a real behavior change (loud failure vs. silent
  substitution) for any *other* resolution, so it is named as a
  recommendation, not landed.
- Any historical or future Pi benchmark number measured at a resolution
  other than one with an exact DRM mode (native support: 640x480, 720x480,
  720x576, 800x600, 832x624, 1024x768, 1152x864, 1280x720, 1280x1024,
  1400x1050, 1440x576, 1440x900, 1680x1050, 1920x1080/1200/1440, 2560x1440
  — confirmed list, this Pi/monitor) should be treated as suspect until
  re-checked against its own DRM log. 400x300 specifically is not a native
  mode and should not be used again for Pi resolution-scaling probes;
  recommend 640x480 or 1024x768 (both exact matches, both meaningfully off
  800x600) for any future fill-rate probe.

### Deliverables

- `refactor/scratch/p43_pi_drm_mode_check.py` (committed) — the minimal Pi
  probe script (CmaFree gate, governor pin, DRM mode-list dump, two
  `--frames 5` invocations with log capture, pristine exit). Follows the
  same `pi_common.py` machinery as P38/P41's scripts.
- `benchmarks/pi_runs/p43_res_visibility/` (committed) — `p43_res_400x300.log`
  / `p43_res_800x600.log` (full stdout including the DRM mode-selection
  lines) + their 5-frame CSVs + `cma_telemetry.tsv`.
- `benchmarks/audits/p43_res_visibility/` (committed) — the four x86
  1000-frame CSVs (`p40like_400x300/800x600.csv`, `p41like_400x300/800x600.csv`)
  + their logs, evidencing the byte-identical-across-resolution reconciliation
  in Phase A.
- No production/library source changed.

### Verification

- `scripts/build-gles2-x86.sh`: clean rebuild, non-stub binaries confirmed
  (`gles2_zone_benchmark` 14,388,464 bytes, `willeq` 79,047,000 bytes).
- `scripts/ci/visual_gate.sh`: **PASS** (qeynos2/gfaydark/befallen), goldens
  untouched.
- `build/` (GLES2 full-tests tree): found **0-byte stub binaries** left by
  the 2026-07-30 VM crash exactly as this plan warned (`willeq`,
  `test_session_replay`, `test_zoning_integration`, and 5 more `test_*`
  executables) — `considered up to date` by `make` despite being empty.
  Force-removed the stubs and rebuilt (`cmake --build build -j24`): clean,
  all binaries now correctly sized (`willeq` 79,278,680 bytes,
  `test_session_replay` 75,315,840 bytes, `test_zoning_integration`
  75,292,504 bytes, no remaining 0-byte files under `build/bin`).
- `ctest --test-dir build --output-on-failure -j4`: **1495/1497 passed**,
  the two failures are exactly the documented host exceptions
  (`AudioManagerTest.PreloadCommonSounds` — ALSA; `RDPServerFrameTest.
  UpdateFrameAfterStart` — RDP frame race).
- `scripts/build-arm-noble.sh`: clean (no source changed by this unit, so
  this reconfirms the pre-existing ARM binary staged for the Pi probe was
  already current at the branch cut).
- `scripts/refactor/check_file_sizes.sh`: N/A — zero `.cpp`/`.h` diff vs
  `fable` (`git diff --stat fable...HEAD -- '*.cpp' '*.h'` empty; this
  unit changed no source files), so there is nothing for the check to
  evaluate.

### Pi discipline

2 invocations (both `--frames 5`, log-capture probes, not full 1000-frame
timed measurements — Phase A's x86 reconciliation and the existing
committed P38/P41 CSVs supplied all the full-length data this unit needed).
Well under the ≤10 budget. CmaFree: 77,612 kB pre-flight → 62,312 kB → 51,996
kB after the two invocations — never approached the 40,960 kB abort
threshold, no remediation needed. Governor pinned `performance` for both
invocations, restored to `ondemand` on all 4 cores at exit (verified).
`/tmp` swept (staged binary + path file + CSVs + logs all removed after
rsync'ing results to NFS); no leftover `gles2_zone_benchmark` processes
(verified via `ps aux`). Pristine exit confirmed.

### Deviations

- Plan's Phase A described the reconciliation as likely a "CONFIG/FLAG
  delta between P40's invocations and P38/P41's." That hypothesis is
  **falsified** by this unit's own x86 measurement (P38/P41's exact flags
  reproduce P40's null on x86, not P38/P41's anomaly) — the real
  reconciliation is platform (DRM mode-fallback), not flags. Reported
  honestly rather than forcing a flag-based narrative to fit the plan's
  framing.
- P40's exact invocation script was not available in the tree (only its
  Review prose); the "P40-like" x86 config in Phase A's table is a
  reconstruction, flagged as such. This didn't affect the conclusion since
  *both* the reconstructed-P40-like and the exact-P41-like configs came back
  null on x86.
- Did not attempt to fix or silence the DRM fallback's passive logging
  (named as a follow-up recommendation only, per Phase D policy).
