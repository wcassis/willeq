# B1198: the harness frame-capture readback candidates — the B1193 named residual

**#517.** B1193 (#512, MR !344) moved the frame encode + write off the render
thread and left the render thread with render + the `createScreenShot`
synchronous GPU readback. The card's question: on the ThinkPad's real GL/DRM
path, what does each readback option the driver exposes cost, and which is the
cheapest that is still correct (byte-identical)? Report-only: this MR adds the
measurement, the candidates (behind `--readback-mode`, default `driver` = the
current path untouched), and the recommendation. No harness behaviour change.

One out-of-leg docs fix, flagged for the reviewer: the fable tip this card was
cut at (ff782a71) is **red in CI's hygiene stage** (pipelines 2561, 2577,
five fable pushes in a row) on three drifted line citations in
`refactor/B1189/README.md` — `spell_visual_fx.cpp:526/844/933` vs the
533/851/971 the functions sit at at the tip (the B1188 merge moved them). This
MR re-anchors those three lines (docs-only, `refactor/B1189/README.md`);
without that this MR's CI cannot be green, and no merge can land while fable
stays red.

## The setup

- Render host: the ThinkPad, DRM on the i915 (Intel UHD 630) virtual
  connector, which offers one mode: **3840×2160@60** — so every frame is
  rendered to a 3840×2160 surface (33.2 MB in the driver's A8R8G8B8) and the
  requested 1280×800 frame (4.1 MB) is a **downscale of the full surface**.
  That is the whole game: the current path reads 33.2 MB per frame.
- Every run: 60 frames @ 30 fps (2.0 s), CONC=1, `--frames-raw`, quiet box
  (load at start recorded per run, 0.25–0.73; no other `model_viewer`
  running). The two rows are B1193's gate rows: `tour_93` (camera b1118) and
  `b1130_r2` (camera b1130); the row plans are B1193's committed plans
  (`refactor/B1193/rows/`).
- Branch: the fable tip the card was cut at (ff782a71) **plus B1193's branch**
  (MR !344, not yet on fable — the per-phase timers this measures with are
  B1193's).
- Committed scripts, exact command lines in each header:
  `refactor/B1198/scripts/b1198_driver.sh` (the 2×5 matrix),
  `refactor/B1198/scripts/run_readback_ab.sh` (one row, one candidate),
  `refactor/B1198/scripts/dirty_audit.py` (the dirty-gate audit + the fbo
  delta, per frame), `refactor/B1198/scripts/build_tables.py` (the table
  below, straight from the raw TSVs).
- Raw data committed under `refactor/B1198/tables/`: `summary.tsv` (the
  driver's run log, all 10 runs) and per run `c1/frame_cost.tsv` (B1193's
  per-phase split: render / readback / downscale / encode / write),
  `c1/readback_cost.tsv` (the candidate's sub-phases: raw / copyout / blit /
  post, per frame), `c1/audit.tsv` + `c1/fbo_delta.tsv` (dirty rows), and the
  run's own `run.log` (the probe rectangle is in the harness's startup line).

## The candidates

`--readback-mode` (default `driver`; the default run is the current path,
byte-identical by construction — the candidate code is not on it):

| candidate | what it does on the render thread |
|---|---|
| `driver` | the current path: `COpenGLES2Driver::createScreenShot` (docker/irrlicht-drm/COpenGLES2Driver.cpp:2136) — `glReadPixels` of the whole 3840×2160 front buffer + the driver's own row-flip + RGBA→BGRA post-work |
| `glread` | the harness's own `glReadPixels` of the full surface + the same post-work in the (optimised) `model_viewer` binary — isolates the driver's unoptimised post-work from the read itself |
| `pbo` | `glReadPixels` into a PBO, `glMapBufferRange` copy out, same post-work — the driver's PBO readback path |
| `fbo` | `glBlitFramebuffer` (GL_LINEAR) 3840×2160 → a 1280×800 FBO, then read back the 4.1 MB FBO — the downscale happens on the GPU, so the CPU transfer is 8× smaller |
| `dirty` | a probe rectangle (the union of caster + target pose boxes over every plan placement, grown 100 % on every side, clamped to the frame; `tools/model_viewer/readback_probe.cpp`) is read back each frame; a frame whose probe bytes equal the previous frame's is SKIPPED, a changed frame is captured through the `driver` path |

## The table

Medians over the 60 frames (`build_tables.py` on `tables/summary.tsv` + the
per-run `frame_cost.tsv`; "readback ms/f (amortized)" = the render-thread
readback phase spread over all 60 frames, "readback median (captured)" = over
the frames actually read back; the fps ceiling = 1000 / (render + amortized
readback)):

| row | candidate | frames | captured | skipped | render ms/f | readback ms/f (amortized) | readback median (captured) | clean-frame probe ms | fps ceiling | wall s | load at start |
|---|---|---|---|---|---|---|---|---|---|---|---|
| b1130_r2 | driver | 60 | 60 | 0 | 0.31 | 53.99 | 53.72 | - | 18.4 | 5.37 | 0.39 |
| b1130_r2 | glread | 60 | 60 | 0 | 0.28 | 30.96 | 29.86 | - | 32.0 | 4.02 | 0.44 |
| b1130_r2 | pbo | 60 | 60 | 0 | 0.30 | 28.00 | 27.04 | - | 35.3 | 3.93 | 0.68 |
| b1130_r2 | fbo | 60 | 60 | 0 | 6.48 | 9.74 | 9.84 | - | 61.6 | 3.00 | 0.71 |
| b1130_r2 | dirty | 60 | 26 | 34 | 0.30 | 36.02 | 60.40 | 16.36 | 27.5 | 4.41 | 0.73 |
| tour_93 | driver | 60 | 60 | 0 | 0.31 | 53.43 | 53.31 | - | 18.6 | 4.75 | 0.25 |
| tour_93 | glread | 60 | 60 | 0 | 0.34 | 31.34 | 29.74 | - | 31.6 | 3.17 | 0.39 |
| tour_93 | pbo | 60 | 60 | 0 | 0.29 | 27.25 | 26.17 | - | 36.3 | 3.10 | 0.56 |
| tour_93 | fbo | 60 | 60 | 0 | 5.36 | 10.71 | 10.69 | - | 62.2 | 2.18 | 0.68 |
| tour_93 | dirty | 60 | 26 | 34 | 0.29 | 33.28 | 56.83 | 14.63 | 29.8 | 3.33 | 0.71 |

## Correctness

Committed test `FxHarness.ReadbackCandidatesAreIdenticalOrAccountForTheDelta`
(tests/test_fx_harness.cpp): renders a fixed-seed row with `driver` and each
candidate and compares pixels:

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
  fbo_delta.tsv`, driver vs fbo): frame 0 of tour_93 differs at 13,133 px
  (1.28 %), max channel delta 202, bbox 589,305,691,605; b1130_r2 at 19,036 px
  (1.86 %), max delta 246, bbox 595,322,991,615 — the deltas are confined to
  the figure's silhouette, exactly where resampling filters differ most, and
  they stay in that 1.28–1.86 % / ≤246 band frame to frame (the test commits
  bounds from the same measurement). The delta is a property of the GPU
  blit's filter, not of the FBO machinery: a byte-exact cheap readback would
  need a nearest-neighbour blit of the exact 3×3 ratio, which GL_LINEAR does
  not offer.

## Why the numbers are what they are

- **The readback is the frame's GPU completion point.** `glReadPixels`
  (docker/irrlicht-drm/COpenGLES2Driver.cpp:2136, the `driver` path; the
  same call in `glread`/`pbo`) forces the i915 to finish the whole frame
  before a single byte is usable — the CPU transfer of 33.2 MB then runs
  single-threaded on the render thread. `pbo` frame 0 splits it
  (`tables/b1130_r2_pbo_c1_s2.0/c1/readback_cost.tsv`): raw read 0.23 ms,
  `glMapBufferRange` copy-out 13.0 ms, post-work 13.7 ms — the transfer is
  ~26 ms of the 27; the PBO only removes a re-synchronisation, not the bytes.
- **The 23 ms between `driver` (53.7) and `glread` (29.9) is compilation, not
  a different read.** Both read the same 33.2 MB; the gap is the driver's
  post-work loop (row-flip + RGBA→BGRA, ~13–14 ms measured) compiled in the
  irrlicht-drm mirror, which the x86 tree builds without the `model_viewer`
  binary's `-O3` (the mirror's own compile line carries no optimisation
  flags — a fix there is a one-flag change to the patch build, outside this
  card's report-only scope). `glread` is that same post-work at `-O3`.
- **`fbo` reads 8× fewer bytes** (4.1 MB vs 33.2 MB: `tables/b1130_r2_fbo_
  c1_s2.0/c1/readback_cost.tsv` frame 0 — raw 10.0 ms, blit 0.003 ms, post
  1.4 ms) and so takes ~9.8–10.7 ms — but its render phase carries
  +5.1–6.2 ms (5.36/6.48 vs 0.29–0.34). Instrumented during this card (temporal
  `beginScene`/`drmPageFlip` timers, removed again — the committed tree is
  clean): the sub-steps of `drmPageFlip` are all sub-millisecond and the cost
  sits inside `beginScene` — the i915's deferred-present flush and the
  `glClear` of the full 3840×2160 surface waiting on the FBO readback's
  queue position; a surfaceless run of the same binary (no DRM,
  `WILLEQ_DRM_DEVICE` pointed at an absent card) has no such cost, so it is a
  property of this display path's queue, not of the blit. It is real per
  frame (median, both rows), so the fbo total is 15.4–16.2 ms, not 10–11.
- **`dirty` is bounded by the probe, not by the gate.** The probe rectangle
  spans the full surface height (both pose boxes reach it, +100 % growth),
  so a "clean" frame still reads back up to 28 MB — 14.6–16.4 ms per clean
  frame (the `clean-frame probe ms` column; `readback_cost.tsv`'s
  `raw_ms` on skipped rows). On these two rows 43 % of frames animate
  (particles), so the gate saves a third of the frames at a 15 ms floor:
  27.5–29.8 fps — still under 30. It is the right shape for a STANDING
  scene (a held cast, a corpse fading, the 3.5 s of B1145's
  OP_DeleteSpawn window) but not a 30 fps fix for these rows as they are.
  Its composition with a cheaper probe (an fbo-sized probe rectangle) is a
  follow-up, not a ruling here.

## The recommendation

- **Cheapest that is byte-identical: `pbo`.** Readback 26.2–27.0 ms, total
  render-thread cost ≈ 26.5–27.3 ms → 35–36 fps, past the card's 33 ms
  bar. It is 5.1–5.6 ms under `glread` (a pure 10–11 % of the read) and
  26.3–26.7 ms under the current `driver` path — with the driver's 23 ms
  unoptimised post-work, half the residual, gone at the same time (see the
  `glread` row). `tests/test_fx_harness.cpp` proves `pbo` byte-identical
  frame for frame, so every pixel measure (B1139/B1155, both `cv2.imread`)
  stays like-with-like. **Adopt `pbo` as the harness readback, and build the
  irrlicht mirror with optimisation** — the second of those two is a one-flag
  change to the patch build and takes the last 13–14 ms of the post-work;
  either as a follow-up card, not folded into this report.
- **Cheapest overall: `fbo`, at the price of 1.28–1.86 % of pixels in the
  figure silhouette** (≤246 channel delta, the GPU LINEAR blit vs the CPU
  bilinear). Its total 15.4–16.2 ms → ~62 fps, and the gate's measures read
  pixels through `cv2` — a filter change of that magnitude is a RE-BASELINE of
  every measure against the oracle, not a readback swap. Will's ruling to
  board; if it lands, the i915 `beginScene` queue cost above is the next
  thing to kill (a page-flip the frame's own blit does not force, or a
  surface at the requested resolution).
- **`dirty` stays a complement**: zero false-cleans on both rows, 43 % of
  frames free — but at the current probe size it does not reach 30 fps on an
  animating row. With an fbo-sized probe it would compose with either choice.

The readback is **not irreducible on this path**: the `driver` cost is 33.2 MB
of CPU transfer + an unoptimised post-work loop, and both have named,
measured alternatives above. What is irreducible at the current surface is the
~10 ms floor of reading 4.1 MB plus the queue effect — below that lies the
surface-resolution question (a 3840×2160 GBM surface for a 1280×800 frame is
the virtual connector's only offer mode), not the readback mechanism.
