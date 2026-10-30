# B1193: the FX harness frame cost — createScreenShot + CPU downscale + PNG encode on the render thread

**#512.** The FX harness (`model_viewer --spell-test --frames-dir`) rendered every
frame **createScreenShot + CPU bilinear downscale + PNG encode (zlib) + file
write on the render thread**: ~0.15 s/frame, ~5–7 fps. The 47-row gate rendered
at that rate on the ThinkPad — the one saturated host (Prometheus 24 h: thinkpad
40 % busy with 100 % peaks, every other host under 10 %) — so the gate wall was
the harness's frame output, not the render.

## The fix

`tools/model_viewer/frame_writer.{h,cpp}`: a bounded queue (`kQueueMax = 8`) and
one writer thread. Per frame:

| phase | thread |
|---|---|
| render + endScene | render |
| createScreenShot (GPU→CPU blit, unavoidable) | render |
| push into the bounded queue (back-pressure at 8 ≈ 265 MB at 3840×2160 — the pre-downscale readback the queue holds; 33 MB is its post-downscale size) | render |
| bilinear downscale (same algorithm as the old path) | writer |
| PNG encode (raw DEFLATE, windowBits 15, level 0, filter 0 — zlib level 0) **or** P6 PPM RGB24 behind `--frames-raw` | writer |
| file write | writer |

The render thread never waits on disk. `--frames-sync` keeps the pre-B1193
render-thread path (the "before" for the A/B). Per-phase timing lands in
`frame_cost.tsv` (render / readback rows on the render thread; downscale /
encode / write on the writer thread).

**Byte-identity, proven.** `FrameWriterOutputIsPixelIdenticalToTheSyncPath`
(tests/test_fx_harness.cpp) renders the same fixed-seed row both ways and
compares the pixel arrays (PNG-decoded vs the raw P6 read) — the written pixels
are identical, so every measure that reads pixels (B1139 `harness_measure.py`,
B1155 `harness_measure_b1130.py` — both `cv2.imread`, so they also accept the
raw PPM) stays like-with-like. B1169's fixed-seed frame identity (the
`^FxHarness.` suite, `--fx-seed` default 1, tests/particle_test_seed.h) passes
on the writer path.

PNG IDAT is **raw DEFLATE (windowBits 15)** — not `15+32` (a zlib wrapper,
invalid for PNG, and rejected by the ThinkPad's zlib 1.3). The driver's
createScreenShot is A8R8G8B8; the PNG carries RGBA and the PPM drops the
alpha.

## The measurement

Render host: the ThinkPad (the renders are CPU-bound, one core each, GPU 0 %).
Every run starts under quiet conditions (load1 < 6 and no other
`model_viewer --spell-test` running) — the gate baseline renders share the box,
and a run measured under contention is not a number. The two rows are the
gate's: `tour_93` (14.5 s, camera b1118, 435 frames) and `b1130_r2` (17.0 s,
camera b1130, 510 frames); the row plans are committed under
`refactor/B1193/rows/`.

Committed tooling (`refactor/B1193/scripts/`):

- `run_fps_ab.sh` — one row, one mode (sync|writer), one CONC: the wall time is
  what the gate pays (startup + frames × per-frame cost); writes
  `summary.tsv` (tag, mode, conc, frames, wall, fps, load at start/end).
- `frame_cost_table.py` — the per-phase table from `frame_cost.tsv`.
- `b1193_driver.sh` — the full matrix: the 60-frame split runs (2.0 s) + the
  full rows at CONC=1 and CONC=2, each run quiet-gated.

Exact command line (render host):

```
nohup bash /home/user/src/willeq-wt/kb-b1193-harness-frame-cost/refactor/B1193/scripts/b1193_driver.sh
```

(frames → `/home/user/kb-frames/kb-b1193-harness-frame-cost/b3/`, rsynced to
`~/kb-evidence/kb-b1193-harness-frame-cost/`).

### The per-frame split (60-frame runs, 2.0 s, CONC=1; load 0.70–1.25 at every run)

ms/frame, from `frame_cost.tsv` (`frame_cost_table.py`):

| phase | tour_93 `--frames-sync` | tour_93 writer | b1130_r2 `--frames-sync` | b1130_r2 writer |
|---|---|---|---|---|
| render + endScene (render thread) | 0.294 | 0.344 | 0.299 | 0.337 |
| createScreenShot readback (render thread) | 55.298 | 56.730 | 58.282 | 58.936 |
| downscale (render thread → writer thread) | 41.424 | 27.110 | 44.410 | 28.224 |
| encode (render thread → writer thread) | inside `write` | 17.040 | inside `write` | 17.740 |
| file write (render thread → writer thread) | 35.906 | 1.414 | 39.960 | 1.438 |
| **render-thread total** | **55.591** | **57.074** | **58.581** | **59.273** |
| **output work total** | **77.330** | **45.564** | **84.371** | **47.402** |

In the sync path the output work is the render thread's: downscale and
`saveAs` (encode + write) sit in the frame loop, which is the ~133 ms/frame
measured whole-frame (≈ the 0.15 s/frame the issue named). In the writer path
the render thread pays render + readback only (~57–59 ms/frame, a ~17 fps
ceiling: the createScreenShot readback is now the dominant render-thread
cost); the output work (~45–47 ms/frame) overlaps on the writer thread and is
faster than the render thread, so the bounded queue never back-pressures.
Follow-up (a separate card, one mechanism): the createScreenShot readback
(~57 ms/frame) is now the whole render-thread cost — the issue's 30+ fps
target is unreachable for every-frame capture until that readback drops
below ~33 ms/frame; it is a driver-level readback question (the renders are
CPU-bound, GPU 0 %), not a threading one.

### Before/after frames/s (full rows; wall includes ~1.6 s startup, fps = frames / wall)

| row | mode | CONC | frames | wall s | fps | load start→end |
|---|---|---|---|---|---|---|
| tour_93 | `--frames-sync` | 1 | 435 | 72.64 | 5.99 | 1.34→1.49 |
| tour_93 | writer | 1 | 435 | 29.05 | 14.97 | 1.58→1.71 |
| tour_93 | `--frames-sync` | 2 | 435 | 80.52 | 5.40/row | 1.43→2.75 |
| tour_93 | writer | 2 | 435 | 35.37 | 12.30/row | 2.75→3.56 |
| b1130_r2 | `--frames-sync` | 1 | 510 | 75.44 | 6.76 | 0.94→1.86 |
| b1130_r2 | writer | 1 | 510 | 32.33 | 15.77 | 1.08→1.44 |
| b1130_r2 | `--frames-sync` | 2 | 510 | 83.63 | 6.10/row | 1.44→3.30 |
| b1130_r2 | writer | 2 | 510 | 38.78 | 13.15/row | 2.28→2.68 |

Wall (CONC=1): tour_93 72.64 → 29.05 s (**2.50×**), b1130_r2 75.44 → 32.33 s
(**2.33×**). Throughput at CONC=2 (frames/s, two rows in parallel):
tour_93 10.8 → 24.6 (**2.28×**), b1130_r2 12.2 → 26.3 (**2.15×**). The 47-row
gate's render leg at CONC=2: ≈32 min (`--frames-sync`) → ≈14 min (writer).
The per-row fps at CONC=2 is the contested figure (each row sees its sibling);
the wall and the frames/s are what the gate pays.

Load at each run's start and end is in the table (the quiet gate held: every
run started under load 2.75, most under 1.6; the CONC=2 rows run with their
sibling, so their end-load is the two of them). The "before" wall here is this
branch's `--frames-sync` path measured on the same host, same day, same quiet
condition — like-with-like; the gate-baseline tip 8c2bfe7c2 tour row
(b1118_r93: 437 frames / 66 s render at CONC=3,
`/mnt/scratch/gate-baseline/8c2bfe7c2/logs/row_times.tsv`) is the same regime.

## Files

- `tools/model_viewer/frame_writer.{h,cpp}` — the writer thread, PNG/PPM
  encoders, `frame_cost.tsv`.
- `tools/model_viewer/model_viewer_fx_plan.cpp` — the `--plan` parser + preset
  defaults, split out of the harness (which stays under the 1,500-line ceiling).
- `tools/model_viewer/model_viewer_fx_harness.{h,cpp}` — per-frame output via
  `FrameWriter` + per-phase timing; `--frames-sync` reference path.
- `tools/model_viewer/model_viewer_startup.cpp` — `--frames-sync` /
  `--frames-raw`.
- `tests/test_fx_harness.cpp` — `FrameWriterOutputIsPixelIdenticalToTheSyncPath`.
- `refactor/B1193/{rows,scripts}/` — the row plans and the committed A/B
  tooling.
