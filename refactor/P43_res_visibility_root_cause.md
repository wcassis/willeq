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

(executor fills in)
