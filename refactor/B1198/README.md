# B1198: the harness frame-capture readback candidates — the B1193 named residual

**#517.** B1193 (#512, MR !344) moved the frame encode + write off the render
thread and left the render thread with render + the `createScreenShot`
synchronous GPU readback. The card's question: on the ThinkPad's real GL
paths, what does each readback option the driver exposes cost, and which is
the cheapest that is still correct (byte-identical)? Report-only: this MR
adds the measurement, the candidates (behind `--readback-mode`, default
`driver` = the current path untouched), and the recommendation. No harness
behaviour change.

This fix leg (review note 8761): the first leg committed the i915-DRM runs
only; the gate's surfaceless path the issue specifies was missing, and the
recommendation was i915-path-only as written. This leg merges `origin/fable`
(tip 7d1af0d9, which also carries !344's corrected B1193 docs — the branch's
pre-merge copy was the pre-!344 state; post-merge `refactor/B1193/README.md`
is byte-identical to the fable tip), commits the gate-path table with its raw
TSVs (`tables/surf/`, the reviewer's recheck runs), and makes the
recommendation explicitly path-conditional with both tables. The out-of-leg
`refactor/B1189/README.md` re-anchor this branch carried is now redundant —
fable landed the same fix independently (5c9cde90e) and the post-merge file
is the fable-tip version.

## The two render paths

- **The gate path — the one the issue specifies.** Surfaceless:
  `WILLEQ_DRM_DEVICE` pointed at an absent card, so the driver opens no DRM
  device and renders at the native requested frame, **1280×800** (4.1 MB).
  #517: "Measured at 1280x800, the client's gate resolution (Will 2026-10-29:
  the b1130 rows render at 1280x800 surfaceless; the 3840x2160 virtual
  surface is not the gate's path)".
- **The i915 DRM path — what the first leg measured.** DRM on the i915
  (Intel UHD 630) virtual connector, which offers one mode: **3840×2160@60**
  — so every frame is rendered to a 3840×2160 surface (33.2 MB in the
  driver's A8R8G8B8) and the requested 1280×800 frame (4.1 MB) is a
  **downscale of the full surface**. The current path reads 33.2 MB per
  frame there.

Every run: 60 frames @ 30 fps (2.0 s), CONC=1, `--frames-raw`, quiet gate
(load1 < 6.0 AND no other `model_viewer --spell-test` running,
`scripts/quiet.py`). The two rows are B1193's gate rows: `tour_93` (camera
b1118) and `b1130_r2` (camera b1130); the row plans are B1193's committed
plans (`refactor/B1193/rows/`).

- Branch: the fable tip the card was cut at (ff782a71) **plus B1193's branch**
  (MR !344, merged into fable as 7d1af0d9), now merged with `origin/fable`
  (merge c84c7f44) — the per-phase timers this measures with are B1193's.
- Committed scripts, exact command lines in each header:
  `refactor/B1198/scripts/b1198_driver.sh` (the i915 2×5 matrix),
  `refactor/B1198/scripts/matrix_gate.sh` (the gate-path 2×5 matrix — the
  reviewer's recheck matrix ran this exact shape,
  `/mnt/projects/fable/review/B1198-recheck/matrix.sh`),
  `refactor/B1198/scripts/run_readback_ab.sh` (one row, one candidate),
  `refactor/B1198/scripts/dirty_audit.py` (the dirty-gate audit + the fbo
  delta, per frame), `refactor/B1198/scripts/build_tables.py` (the tables
  below, straight from the raw TSVs),
  `refactor/B1198/scripts/frame_hash_census.py` (the frame-hash byte-identity
  census).
- Raw data committed under `refactor/B1198/tables/`: per run
  `c1/frame_cost.tsv` (B1193's per-phase split: render / readback / downscale
  / encode / write), `c1/readback_cost.tsv` (the candidate's sub-phases: raw
  / copyout / blit / post, per frame), `c1/audit.tsv` + `c1/fbo_delta.tsv`
  (dirty rows, i915 runs) and the run's own `c1/run.log` (the probe rectangle
  is in the harness's startup line), plus `tables/summary.tsv` (the i915
  driver's run log, all 10 runs). The gate-path runs are committed under
  `tables/surf/` (same per-run layout, no audit files — gate-path identity is
  the frame-hash census, `tables/surf/surfhash_verdict.txt`).

## The candidates

`--readback-mode` (default `driver`; the default run is the current path,
byte-identical by construction — the candidate code is not on it):

| candidate | what it does on the render thread |
|---|---|
| `driver` | the current path: `COpenGLES2Driver::createScreenShot` (docker/irrlicht-drm/COpenGLES2Driver.cpp:2126) — `glReadPixels` (line 2136) of the front buffer + the driver's own row-flip + RGBA→BGRA post-work (3840×2160 on the i915 path, 1280×800 on the gate path) |
| `glread` | the harness's own `glReadPixels` of the full surface + the same post-work in the (optimised) `model_viewer` binary — isolates the driver's unoptimised post-work from the read itself |
| `pbo` | `glReadPixels` into a PBO, `glMapBufferRange` copy out, same post-work — the driver's PBO readback path |
| `fbo` | `glBlitFramebuffer` (GL_LINEAR) to a requested-size FBO, then read back the FBO — on the i915 path that is a 3840×2160→1280×800 downscale (8× fewer CPU bytes); on the gate path the surface already IS 1280×800, so the blit is 1:1 |
| `dirty` | a probe rectangle (the union of caster + target pose boxes over every plan placement, grown 100 % on every side, clamped to the frame; `tools/model_viewer/readback_probe.cpp`) is read back each frame; a frame whose probe bytes equal the previous frame's is SKIPPED, a changed frame is captured through the `driver` path |

## The table

"render ms/f" and "readback ms/f (amortized)" are **means** over the 60
frames (`build_tables.py` on the per-run `frame_cost.tsv`; "readback ms/f
(amortized)" = the render-thread readback phase spread over all 60 frames);
"readback median (captured)" and "clean-frame probe ms" are **medians** (over
the frames actually read back / over the skipped frames). "fps ceiling" =
1000 / (render mean + amortized readback mean). "load at start" / "load at
end" are the load1 sampled before the run / after it (`run_readback_ab.sh`'s
summary line, fields 9/8).

### The gate path (surfaceless 1280×800 — the issue's specified path)

Rendered on the ThinkPad by the recheck matrix (the reviewer's note 8761
runs, committed here in `tables/surf/`; `build_tables.py
refactor/B1198/tables/surf` reproduces the table from those TSVs; the
committed script that runs this shape is `scripts/matrix_gate.sh`). Quiet
gate < 6.0 per run; the load at every run start (5.16–5.83) is in the recheck
matrix log (`/mnt/projects/fable/review/B1198-recheck/matrix.log`); "load at
end" is per run in the table; no other `model_viewer` running.

| row | candidate | frames | captured | skipped | render ms/f (mean) | readback ms/f (amortized, mean) | readback median (captured) | clean-frame probe ms (median) | fps ceiling | wall s | load at start | load at end |
|---|---|---|---|---|---|---|---|---|---|---|---|---|
| b1130_r2 | driver | 60 | 60 | 0 | 0.49 | 9.92 | 9.50 | - | 96.1 | 2.46 | - | 5.47 |
| b1130_r2 | glread | 60 | 60 | 0 | 0.52 | 5.34 | 5.11 | - | 170.5 | 2.23 | - | 5.83 |
| b1130_r2 | pbo | 60 | 60 | 0 | 0.54 | 4.89 | 4.59 | - | 184.2 | 2.26 | - | 6.08 |
| b1130_r2 | fbo | 60 | 60 | 0 | 0.36 | 3.31 | 3.18 | - | 272.2 | 1.42 | - | 5.39 |
| b1130_r2 | dirty | 60 | 26 | 34 | 0.21 | 3.75 | 7.10 | 0.81 | 252.9 | 1.27 | - | 5.39 |
| tour_93 | driver | 60 | 60 | 0 | 0.60 | 11.83 | 11.85 | - | 80.4 | 3.47 | - | 5.47 |
| tour_93 | glread | 60 | 60 | 0 | 0.55 | 5.63 | 5.44 | - | 161.9 | 2.05 | - | 5.83 |
| tour_93 | pbo | 60 | 60 | 0 | 0.53 | 4.96 | 4.82 | - | 181.8 | 1.96 | - | 5.83 |
| tour_93 | fbo | 60 | 60 | 0 | 0.34 | 3.18 | 3.02 | - | 284.3 | 1.26 | - | 5.39 |
| tour_93 | dirty | 60 | 26 | 34 | 0.20 | 3.33 | 6.81 | 0.54 | 283.5 | 1.09 | - | 5.39 |

"load at start" is "-" in the table because the recheck runs predate the
`run_readback_ab.sh` update that commits the start load as summary field 9;
the per-run start loads are the matrix log's `load_at_start=` lines above.

### The i915 DRM path (3840×2160 virtual surface — the first leg's runs)

`build_tables.py refactor/B1198/tables` on the committed raw TSVs
(`tables/summary.tsv` is the run log). "load at start" is "-": the first
leg's start loads were written to stderr only and never committed (note
8761's finding 4); only the end load is committed, and that is what the
column used to mislabel as "load at start".

| row | candidate | frames | captured | skipped | render ms/f (mean) | readback ms/f (amortized, mean) | readback median (captured) | clean-frame probe ms (median) | fps ceiling | wall s | load at start | load at end |
|---|---|---|---|---|---|---|---|---|---|---|---|---|
| b1130_r2 | driver | 60 | 60 | 0 | 0.31 | 53.99 | 53.72 | - | 18.4 | 5.37 | - | 0.39 |
| b1130_r2 | glread | 60 | 60 | 0 | 0.28 | 30.96 | 29.86 | - | 32.0 | 4.02 | - | 0.44 |
| b1130_r2 | pbo | 60 | 60 | 0 | 0.30 | 28.00 | 27.04 | - | 35.3 | 3.93 | - | 0.68 |
| b1130_r2 | fbo | 60 | 60 | 0 | 6.48 | 9.74 | 9.84 | - | 61.6 | 3.00 | - | 0.71 |
| b1130_r2 | dirty | 60 | 26 | 34 | 0.30 | 36.02 | 60.40 | 16.36 | 27.5 | 4.41 | - | 0.73 |
| tour_93 | driver | 60 | 60 | 0 | 0.31 | 53.43 | 53.31 | - | 18.6 | 4.75 | - | 0.25 |
| tour_93 | glread | 60 | 60 | 0 | 0.34 | 31.34 | 29.74 | - | 31.6 | 3.17 | - | 0.39 |
| tour_93 | pbo | 60 | 60 | 0 | 0.29 | 27.25 | 26.17 | - | 36.3 | 3.10 | - | 0.56 |
| tour_93 | fbo | 60 | 60 | 0 | 5.36 | 10.71 | 10.69 | - | 62.2 | 2.18 | - | 0.68 |
| tour_93 | dirty | 60 | 26 | 34 | 0.29 | 33.28 | 56.83 | 14.63 | 29.8 | 3.33 | - | 0.71 |

## Correctness

Committed test `FxHarness.ReadbackCandidatesAreIdenticalOrAccountForTheDelta`
(tests/test_fx_harness.cpp): renders a fixed-seed row with `driver` and each
candidate and compares pixels. On the i915 path (where it runs) that is:

- `driver`, `glread`, `pbo`: **byte-identical** (whole frame, every frame).
- `dirty`: every CAPTURED frame byte-identical (by construction — it is the
  driver path); every SKIPPED frame checked offline against the driver
  baseline run's pixels: **0 false-cleans on both rows, 34/60 skipped each**
  (`dirty_audit.py`; `tables/*_dirty_c1_s2.0/c1/audit.tsv`, one verdict per
  frame). The probe rectangle per run is in that run's `run.log`
  (tour_93: 1065,0..2748,2160; b1130_r2: 384,0..3840,2160 surface px).
- `fbo`: **not byte-identical** — the GPU's LINEAR blit 3840×2160→1280×800
  replaces the CPU bilinear downscale, and the two filters disagree at
  high-contrast edges. Measured per frame (`tables/*_dirty_c1_s2.0/c1/
  fbo_delta.tsv`, driver vs fbo, all 60 frames per row). Frame 0: tour_93
  differs at 13,133 px (1.28 %), max channel delta 202, bbox
  589,305,691,605; b1130_r2 at 19,036 px (1.86 %), max delta 246, bbox
  595,322,991,615. Across the 60 committed frames the band is wider than
  frame 0: tour_93 spans 1.28–3.62 % — frames 58–59 are 30,303 px (2.96 %)
  and 37,120 px (3.62 %), both max channel delta 255, the bbox widening to
  309,243,956,618 / 309,235,956,623; b1130_r2 spans 1.86–2.11 %, max delta
  247 (frames 3–47), frames 58–59 at 21,445–21,623 px (2.09–2.11 %), 237.
  Max channel delta across both files: **255**. The deltas are confined to
  the figure's silhouette, where resampling filters differ most. The delta
  is a property of the GPU blit's filter, not of the FBO machinery: a
  byte-exact cheap readback would need a nearest-neighbour blit of the exact
  3×3 ratio, which GL_LINEAR does not offer. The committed test pins only
  its own 12-frame spell_201 row (tests/test_fx_harness.cpp:1127-1130,
  ≤40,000 px / ≤240) — these 60-frame rows are not covered by it, and the
  max delta 255 would exceed the test's 240 bound if they were.

### The gate path: every candidate is byte-identical there

The recheck's frame-hash census (`scripts/frame_hash_census.py`: sha256[:16]
of every `frame_*.ppm`, compared to the driver path's frame at the same
index) — `tables/surf/surfhash_verdict.txt`, and
`/mnt/projects/fable/review/B1198-recheck/surfhash_verdict.txt`:

```
surf  tour_93   driver:60/60 glread:60/60 pbo:60/60 fbo:60/60 dirty:26/26
surf  b1130_r2  driver:60/60 glread:60/60 pbo:60/60 fbo:60/60 dirty:26/26
drm   tour_93   driver:60/60 glread:60/60 pbo:60/60 fbo:0/60 dirty:26/26
drm   b1130_r2  driver:60/60 glread:60/60 pbo:60/60 fbo:0/60 dirty:26/26
```

On the gate path the probe rectangle clamps to the 1280×800 surface
(`tools/model_viewer/readback_probe.cpp:64-68`, grown 100 % then clamped:
tour_93 352,0..919,800; b1130_r2 127,0..1280,800, per the surf runs'
`run.log`) and the fbo blit is 1:1 — the surface already IS the requested
frame, so the i915 filter delta does not exist there (surf fbo 60/60; the
fbo `readback_cost.tsv` raw phase is the 4.1 MB frame, ~0.8–2.6 ms). The
i915 fbo 0/60 is the downscale filter disagreement above.

## Why the numbers are what they are

- **On the gate path the readback is not the bottleneck.** The CURRENT
  `driver` path already runs at 80/96 fps (amortized readback 11.83/9.92
  ms) — the issue's 30 fps target and its ~33 ms criterion do not bind
  there. The "53 ms = 33 MB CPU transfer" figure is the i915 virtual
  surface's cost, not the gate path's.
- **The readback is the frame's GPU completion point (i915 path).**
  `glReadPixels` (docker/irrlicht-drm/COpenGLES2Driver.cpp:2136, the
  `driver` path; the same call in `glread`/`pbo`) forces the i915 to finish
  the whole frame before a single byte is usable — the CPU transfer of
  33.2 MB then runs single-threaded on the render thread. `pbo` frame 0
  splits it (`tables/b1130_r2_pbo_c1_s2.0/c1/readback_cost.tsv`): raw read
  0.23 ms, `glMapBufferRange` copy-out 13.0 ms, post-work 13.7 ms — the
  transfer is ~26 ms of the 27; the PBO only removes a re-synchronisation,
  not the bytes.
- **The 23 ms between `driver` (53.7) and `glread` (29.9) is compilation,
  not a different read (i915 path).** Both read the same 33.2 MB; the gap is
  the driver's post-work loop (row-flip + RGBA→BGRA, ~13–14 ms measured)
  compiled in the irrlicht-drm mirror, which the x86 tree builds without the
  `model_viewer` binary's `-O3` (the mirror's own compile line carries no
  optimisation flags — a fix there is a one-flag change to the patch build,
  outside this card's report-only scope). `glread` is that same post-work
  at `-O3`.
- **`fbo` reads 8× fewer bytes (i915 path)** (4.1 MB vs 33.2 MB:
  `tables/b1130_r2_fbo_c1_s2.0/c1/readback_cost.tsv` frame 0 — raw 10.0 ms,
  blit 0.003 ms, post 1.4 ms) and so takes ~9.8–10.7 ms — but its render
  phase carries +5.1–6.2 ms (5.36/6.48 vs 0.29–0.34). Instrumented during
  the first leg (temporal `beginScene`/`drmPageFlip` timers, removed again —
  the committed tree is clean): the sub-steps of `drmPageFlip` are all
  sub-millisecond and the cost sits inside `beginScene` — the i915's
  deferred-present flush and the `glClear` of the full 3840×2160 surface
  waiting on the FBO readback's queue position. A surfaceless run of the
  same binary has no such cost — measured on the gate path above: fbo
  render 0.34/0.36 ms, blit 0.01–0.45 ms per frame
  (`tables/surf/*_fbo_c1_s2.0/c1/readback_cost.tsv`) — so it is a property
  of the i915 display path's queue, not of the blit. It is real per frame
  (mean, both rows), so the fbo total there is 15.4–16.2 ms, not 10–11.
- **`dirty` is bounded by the probe, not by the gate.** On the i915 path the
  probe rectangle spans the full surface height (both pose boxes reach it,
  +100 % growth), so a "clean" frame still reads back up to ~30 MB —
  14.6–16.4 ms per clean frame (the `clean-frame probe ms` column;
  `readback_cost.tsv`'s `raw_ms` on skipped rows). On these two rows 43 % of
  frames animate (particles), so the gate saves a third of the frames at a
  15 ms floor: 27.5–29.8 fps — still under 30. On the gate path the same
  mechanism clamps to the 1280×800 surface (the rects above), a clean frame
  measures 0.54/0.81 ms, and 57 % of frames on these rows skip at that
  floor — 252.9/283.5 fps ceiling, tied with fbo on amortized cost. It is
  the right shape for a STANDING scene (a held cast, a corpse fading, the
  3.5 s of B1145's OP_DeleteSpawn window); its composition with a cheaper
  probe (an fbo-sized probe rectangle) is a follow-up, not a ruling here.

## The recommendation — path-conditional

### The gate path (surfaceless 1280×800 — the issue's specified path)

The readback is **not the 30 fps blocker on this path**: the current
`driver` path already runs at 80/96 fps, and every candidate is
byte-identical to it (the census above), so any swap leaves every pixel
measure like-with-like. If the cheapest byte-identical readback is wanted
there: **`fbo`** — 3.18/3.31 ms amortized, the fastest (render 0.34/0.36
ms, no queue penalty; the blit is 1:1), byte-identical 60/60. `dirty` ties
  its amortized cost (3.33/3.75) and additionally skips 57 % of frames on
these rows (0.54/0.81 ms probe floor). `pbo` is byte-identical but
1.6–1.8 ms slower than fbo on this path. Adopting any of these is a
follow-up card, not a ruling here; on this path the residual the issue
names does not bind, so no adoption is forced.

### The i915 path (3840×2160 virtual surface) — i915-only

- **Cheapest that is byte-identical: `pbo`.** Readback 26.2–27.0 ms, total
  render-thread cost 27.5–28.3 ms (render + amortized readback) → 35–36 fps,
  past the 33 ms bar. It is 3.0–4.1 ms under `glread` (a ~10–13 % of the
  read) and 26.0–26.2 ms under the current `driver` path — with the
  driver's 23 ms unoptimised post-work, half the residual, gone at the same
  time (see the `glread` row). The tests prove `pbo` byte-identical frame
  for frame, so every pixel measure (B1139/B1155, both `cv2.imread`) stays
  like-with-like. **Adopt `pbo` as the harness readback for this path, and
  build the irrlicht mirror with optimisation** — the second of those two is
  a one-flag change to the patch build and takes the last 13–14 ms of the
  post-work; either as a follow-up card, not folded into this report.
- **Cheapest overall: `fbo`, at the price of the measured downscale delta**
  (1.28–3.62 % of pixels on tour_93, max channel delta 255; 1.86–2.11 % /
  247 on b1130_r2 — the GPU LINEAR blit vs the CPU bilinear, "Correctness"
  above). Its total 15.4–16.2 ms → ~62 fps, and the gate's measures read
  pixels through `cv2` — a filter change of that magnitude is a RE-BASELINE
  of every measure against the oracle, not a readback swap. Will's ruling to
  board; if it lands, the i915 `beginScene` queue cost above is the next
  thing to kill (a page-flip the frame's own blit does not force, or a
  surface at the requested resolution).
- **`dirty` stays a complement**: zero false-cleans on both rows, 43 % of
  frames free on the i915 path but at a 15 ms probe floor (27.5–29.8 fps —
  not a 30 fps fix for these rows as they are); on the gate path the probe
  floor is 0.54–0.81 ms. With an fbo-sized probe it would compose with
  either choice.

The readback is **not irreducible on either path**: on the i915 path the
`driver` cost is 33.2 MB of CPU transfer + an unoptimised post-work loop,
and both have named, measured alternatives above; on the gate path the
current path already runs at 80–96 fps, so the 30 fps target binds only on
the i915 path — and `pbo` clears it there (35–36 fps). What is irreducible
at the current surface is the ~10 ms floor of reading 4.1 MB plus the i915
queue effect — below that lies the surface-resolution question (a 3840×2160
GBM surface for a 1280×800 frame is the virtual connector's only offer
mode), not the readback mechanism.
