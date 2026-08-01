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

(executor fills in)
