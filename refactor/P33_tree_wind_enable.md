# P33 — Wire tree wind animation (Will's ruling: trees move)

**Slot: slot1. x86 only — no Pi. OWNS EQEMU (honor the .eqemu-lock
protocol). Sole agent this dispatch.**

Will's ruling (2026-07-28, recorded in deferred.md): "trees are
supposed to move. the only time when they should not animate is if the
tree wind animation setting is disabled." P32's finding: 
`TreeWindController::update()` — the only code advancing wind time and
the weather-multiplier transition — is unreachable on the live GLES2
path (its old call site was the deleted CPU-instancing branch), so
wind has been frozen since the GLES2 backend shipped. Fix it.

## Work

1. **Find the setting.** Locate the tree-wind animation toggle Will
   refers to (grep config/display settings/constrained config for
   wind/tree animation keys; check what the wind shader family +
   TreeWindController already consult). If a real user-facing toggle
   exists, gate on it. If NONE exists, add one (config +
   display_settings.json, default ON) following the existing settings
   conventions — and say so plainly in the Review.
2. **Wire `TreeWindController::update()` into the render loop** at the
   cadence the controller was designed for (per-frame with deltaTime —
   check its API and the weather-multiplier transition logic), gated
   on the setting. Wind time must advance DETERMINISTICALLY from frame
   deltas, not wall-clock reads inside the controller, wherever the
   choice exists — the benchmark/gate depends on run-to-run
   determinism.
3. **Keep the golden/gate path deterministic and unchanged.** The
   visual gate's frames must remain byte-identical run-to-run and
   IDENTICAL TO CURRENT GOLDENS: pin wind in the benchmark/golden path
   (wind time held at its current frozen value — i.e. the gate renders
   exactly what it renders today). Do not re-bless; if you cannot keep
   the gate byte-stable, STOP and report.
4. **Evidence for Will's bless (production-visible change)**: live
   eqemu session, fixed camera on a tree cluster (gfaydark nekpine or
   qeynos2 gates — P20b's scenes), time-lapse stills at fixed
   intervals (e.g. t+0/2/4/6s, same pose by construction) showing
   branch/canopy movement between frames, PLUS the same sequence with
   the setting OFF showing stillness. Same-pose discipline per the
   P20b evidence bar. Stills to
   `/mnt/projects/fable/videos/p33_tree_wind/` + committed manifest
   under `benchmarks/audits/p33_tree_wind/`. You do NOT declare
   visual acceptability — Will judges.
5. Sanity: confirm the weather-multiplier transition now actually
   transitions (log evidence or a targeted unit test on
   TreeWindController if cheap).

## Rules

- Behavior scope: ONLY wind time/weather-multiplier animation becomes
  live; classification, materials, budgets untouched.
- Perf note: the wind vertex shaders already run (P20b) — this adds
  one CPU update call per frame. If your implementation adds anything
  heavier per-frame, STOP and reconsider.
- Territory: TreeWindController + the frame-loop call site
  (renderer_frame.cpp), the settings plumbing, benchmark wind-pinning
  if needed, evidence dirs. No shader changes, no zone loaders, no
  driver.
- eqemu per protocol (lock, sanctioned scripts, Testchar restore to
  zone_id=36, x=-77.9232, y=21.9086, z=3, h=190, clean shutdown,
  lock removed).

## Verification

- Full current bar at baselines: GLES2 tree 1360/1361 (ALSA only),
  headless 939/940, `build-gles2-x86.sh` clean, visual gate PASS with
  goldens BYTE-IDENTICAL (this is the hard proof of step 3), ARM
  build clean, file sizes clean.
- Gate determinism double-run (two consecutive gate runs, both PASS).
- The step-4 evidence pairs (wind on: movement between stills; wind
  off: stillness), same pose by construction, manifest committed.

## Review

(executor fills in: the setting found-or-added, wiring details +
determinism reasoning, gate byte-stability proof, evidence paths,
weather-transition sanity result, deviations)
