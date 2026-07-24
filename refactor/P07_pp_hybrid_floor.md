# P07 — Hybrid floor for the area-mode per-pixel budget

Follow-up to P06 (refactor/P06_area_aware_budget.md, merged 23aee56).
Will's decision 2026-07-24: implement the hybrid floor. Slot: wt/slot1,
branch task/P07-pp-floor.

Motivation (P06 orchestrator review + contact sheet
benchmarks/contact_sheets/p06_area_budget/contact_sheet.png): pure area
mode is statistically all-lightweight (81.71 vs 81.47ms whole-run; PP
engages in 89/1000 frames) because the camera-containing region nearly
always trips the near-plane full-screen fallback and exhausts the budget
at region 1. Visually that unlights near geometry — per-vertex light
interpolates to ~zero on giant polys (near-black walls at poses 291/509).
Meanwhile the city pairs are visually identical between modes yet area is
12-31% faster there. The floor keeps the nearest region(s) per-pixel
(visual anchor, can never go black) while area accumulation still demotes
everything behind them.

## Design

1. **Floor rule (area mode only)**: the first `adaptiveLightingAreaPPFloor`
   regions in the nearest-first order ALWAYS get per-pixel, regardless of
   estimated area. New config field, int, default 1. Their estimated area
   still accumulates into the running total, so regions after the floor
   demote exactly as in P06 area mode. Count mode untouched; floor is
   ignored in count mode.
2. Config plumbing mirrors P06's fields (constrained_renderer_config.h/.cpp
   JSON override `adaptiveLightingAreaPPFloor`). Defaults unchanged
   elsewhere: mode stays `count` everywhere — no preset flips in this unit.
3. Benchmark parity: `--pp-area-floor N` (default 1) in
   gles2_zone_benchmark.cpp; apply the same floor in zone_bench_loader.cpp's
   area-mode pre-pass.
4. Production fix site: renderer_zone_draw.cpp area-mode branch — the
   decision becomes
   `regionUseLW = (regionsDrawn >= floor) && cumulativeArea > limit`
   with cumulativeArea still accumulated for floor regions.

## Verification

- Build + ctest (known host exceptions: AudioManagerTest.PreloadCommonSounds,
  RDPServerFrameTest.UpdateFrameMultipleTimes) + x86 GLES2 build + visual
  gate PASS unchanged (default count) + ARM build + file-size check.
- **Pi measurement (P02 protocol via scripts/pi/pi_common.py, same qeynos2
  config as P06: --entities 200 --seed 42 --navmesh-path, 1280x720)**,
  variants: count, area-floor1 (k=0.75), all-LW (--zone-pp-budget 0).
  First verify the moving path still reproduces P06's committed
  count.csv poses (compare pos columns at frames 291/292/509/627-635);
  the path has drifted across sessions before (see deferred.md). If it
  drifted, fall back to STATIC pose capture (--camera-pose, the P06
  contact-sheet poses listed below) for the pose-level timings and use the
  moving run only for whole-run averages.
  Poses (EQ coords X,Y,Z,HEADING,PITCH from P06 count.csv):
  - outskirts/anomaly: 240.6,19.6,6.1,172.6,67.9 (fr291) and
    240.9,18.3,8.6,180.0,77.4 (fr292)
  - corridor: 271.2,184.1,5.5,45.8,-1.2 (fr509)
  - city: 286.1,94.0,43.1,6.5,-28.8 (fr627), 287.5,105.6,36.7,6.5,-28.8
    (fr631), 288.4,114.3,31.8,6.5,-28.8 (fr634)
- **Acceptance**:
  - Anomaly poses (291/292): area-floor1 within noise of count-mode
    total_ms (the huge region keeps PP by design; ~17ms expected, NOT the
    7ms of pure area — that cost is accepted as the price of correct near
    lighting; those poses are ~50fps, above KPI).
  - City poses (627/631/634): area-floor1 retains most of pure-area's
    win vs count — target >= half of P06's measured 12-31% saving.
  - Whole-run avg: area-floor1 <= count-mode avg (91.18ms in P06's run;
    path drift may shift absolute numbers — compare within-session).
  - draws/regions/polys byte-identical count vs area-floor1 (full-file
    programmatic check, P05/P06 precedent).
- **Contact sheet**: regenerate the P06 llvmpipe captures (x86 build,
  --eq-path /mnt/projects/EverQuestP1999, same 4 poses, count vs
  area-floor1) into benchmarks/contact_sheets/p07_pp_floor/ — the
  anomaly/corridor poses must now be LIT in area-floor mode (no black
  near geometry), the city pairs should remain near-identical. `git add
  -f` the PNGs and the Pi CSVs (benchmarks/ paths are gitignored).
- Default flip to area+floor remains AFTER this unit: orchestrator review,
  Will's visual sign-off on the new sheet, golden re-bless, Pi tier-2
  baseline — not in scope here.

## Review

(executor fills in)
