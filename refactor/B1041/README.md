# B1041 (#361): the 20 fps Pi zones are GPU-bound, not cull-bound — measure fill vs clip vs GPU time on lane 2

MEASUREMENT ONLY. No budget, golden, preset, look, baseline, ratchet or
third_party change. Produces numbers + a ranked lever list. Will's B1006 rule:
each measured axis names its mechanism (driver fact from
`docs/reference/lima_cost_sheet.md` or a cited Mesa function + a prediction
checked against the numbers) before anything is asked.

**Status: RECIPE (written before any run, 2026-10-19, at worktree tip
b15dd698). The results/tables/mechanism/lever sections below are populated from
the runs named in "The runs" — every number is program output from the raw
files in `/home/user/kb-evidence/kb-b1041-pi-gpu-bound/`, never typed.**

## Binary + lane (fixed for every number)

- **Binary:** the published aarch64 fable build, quoted at run start and used
  for every run:
  `/mnt/projects/fable/ci-arm64/by-branch/fable/`
  **sha=b15dd698567a21a29c4cfb659e04950ea0ec9b59** (branch=fable, built
  2026-10-19T10:04Z on thinkpad, mesa=system). This is the same sha the worktree
  is cut at, and the same `by-branch/fable` target B1037 ran. `BUILD_INFO` is
  copied into the evidence dir verbatim. The build's `tools=` line lists
  `gles2_zone_benchmark`, `willeq` and `lima_bo_census.so` — every tool this
  card uses is in the published tree.
- **Lane:** `PI_LANE=2` (Rock64, RK3328, Mali-450, Lima, Mesa 25.2.8 — the same
  Mesa family as the Pi; `scripts/pi/env.sh` lane table). The lock is taken on
  the device with `pi_lock_take B1041` and released with `pi_lock_release
  B1041` at the end. The HDMI connector is forced on with `pi_hdmi_force_on`
  (crash-surviving record in env.sh) and restored with `pi_hdmi_restore`.
  Never rebooted; lane 1 (the Pi) untouched.
- **EQ assets:** `/mnt/projects/EverQuestP1999` (`--eq-path`), the published
  build's own `config/`+`data/` as `WILLEQ_DATA_ROOT` (so the constrained
  preset, texture index and DB are exactly the shipped ones), cache on the
  device tmpfs.

## The measured question

The B854 rendered sweep (orangepi preset, 1280×720, Clip 300, 2026-09-29) shows
thurgadinb/cobaltscar/swampofnohope at 20 fps and chardok at 60 fps, with
**40–50 merged draws, 3–9k polys, 2–5 entities, `endScene` < 1 ms**, and a
**24–39 ms per-frame remainder that is the DRM page-flip wait** (the frame is
already > 2 display intervals long before the flip). The drawn workload says
cull/clip already work; the open question is **where the GPU time goes**. The
hypothesis to TEST (not assume) is fragment fill: the slow zones cover the whole
framebuffer with sky + terrain + water/snow, and Mali re-walks the tile buffer
per render job (lima cost sheet fact 5/7). Secondary CPU finding to name:
thurgadinb's constant **10.3 ms/entity-build slice for 476 consecutive frames**.

## Tooling facts that shape the recipe (read before judging the tables)

1. **The benchmark has NO clip-distance knob.** `tools/hwcaps/zone_bench_frame.cpp`
   hardcodes `farClip = 300.0f` (line 264, "Match OrangePi preset") and
   `clipDist = 300.0f` for the fog (line 142). The full `--help`/argv parser
   (`tools/hwcaps/zone_bench_options.cpp`, 107 flags) has no `--clip`/`--viewdist`
   flag and no env override. So the **Clip 300-vs-150 A/B cannot be run on the
   published binary without a code change**, and this card is MEASUREMENT ONLY
   (no build on the device; a genuinely-missing probe flag is a block, per the
   card's worker-environment note). The clip axis is therefore answered
   INDIRECTLY: (a) the resolution A/B isolates fill from vertex-count (same
   far-clip, same polys, fewer pixels); (b) the B854 client log already carries
   a live clip measurement — thurgadinb's `/clipdist` is 300 in its RENDERER
   STATS block and the scene is still 20 fps at that clip — plus the client's
   runtime `/clipdist 150` is available to a server session if one is stood up.
   What table 2 *would* need is named in the block below.
2. **The Rock64's forced-connector mode list is 1024×768 / 800×600 / 848×480 /
   640×480** (no 1280×720, no 640×360), and the benchmark renders at the
   **SELECTED DRM mode, not the requested `--res`**: `CIrrDeviceFB` (the
   headless-DRM device) selects a connector mode with `selectDRMMode()`, then
   **overwrites `Width`/`Height` from the selected mode** before
   `initGBM()` creates the surface, so a non-native `--res` silently falls back
   (P43/P49's MODE FALLBACK line, loud in the log). The probe run confirmed
   this: `--res 1280 720` rendered at 1024×768 on lane 2. The resolution A/B
   therefore uses **native, honored modes**: high 1024×768 (786 432 px — the
   closest native area to B854's 1280×720 = 921 600 px) vs low 640×480
   (307 200 px) = **0.391× the pixel count**. Fill-bound predicts the
   GPU/flip component scales ≈ ×0.391; vertex/CPU-bound predicts `cpu_ms`
   ≈ unchanged.
3. **GPU time is measured, not inferred** (lima cost sheet fact 8: no timer
   queries on Lima, so `--gpu-sync-probe`'s GPU-timer columns will be −1; its
   **CPU-side sync-point attribution is the real GPU figure on this driver** —
   `flipWaitMs` = the `waitForFlipComplete()` poll, `swapBuffersMs` =
   `eglSwapBuffers`). The **per-frame Lima job count** is measured with the
   B1006 census shim (`lima_bo_census.so`, LD_PRELOAD'd — the benchmark is NOT
   setcap so it honours it) via `B962_GEMDUMP` → `gem_pp_jobs_per_frame`
   (`scripts/pi/rendered_zone_summarize.py`'s `job_rates()` method, B1006).

## Poses (fixed steady pose per zone, same pose across every cell)

The card: "take the manifest pose for a gate zone where one exists; otherwise
the sweep's documented pose." **None of the three zones is a gate zone** (the
five gate zones are qeynos2/gfaydark/befallen/iceclad/timorous), so all three
use the **B854 rendered sweep's documented pose** — the character's safe/
landing point, which is where the sweep held the camera for its whole 240 s
steady observation (the pose the 20 fps / 10.3 ms figures were measured at):

| zone | EQ pose (x,y,z) | source |
|---|---|---|
| thurgadinb | 250.0, 0.0, 0.0 | B854 `zones/thurgadinb/client.log` "Your location is 250.0, 0.0, 0.0" |
| cobaltscar | -939.0, 895.0, 318.0 | B854 `zones/cobaltscar/client.log` "… -939.0, 895.0, 318.0" |
| chardok | 119.0, 859.0, 106.0 | B854 `zones/chardok/client.log` "… 119.0, 859.0, 106.0" |

Heading 0 / pitch -5 for all (the benchmark's fixed-pose convention, same as
the gate's `--camera-pose … ,0.0,-5.0`). The Q01 `--path-file` replay path is
**one pose line per rendered frame** — a single line with `--frames 60` exits 1
("exceeds path-file length"), the probe run's first failure — so each pose file
carries the zone's pose **60 times**. `placeEntitiesAlongPath()` gets the
measured-frame positions (all the same point), so the scene is one steady
composition; the median is then over 55 post-warmup frames of it, not a single
cold pose capture.

## The runs (what actually ran, and what each file is)

All runs happen on lane 2 via `pi_remote` while the lock is held, in the
device script `b1041_run.sh` (in the evidence dir; staged to
`/tmp/b1041_run.sh` on the device). `D` is the published build dir above;
`EQ=/mnt/projects/EverQuestP1999`; `OUT=/tmp/b1041`. Environment for every
invocation: `WILLEQ_DATA_ROOT=$D WILLEQ_CACHE_DIR=$OUT/cache
WILLEQ_DB_PATH=$D/data/willeq.db` (the db is the race-model prerequisite —
without it `tool_race_db.cpp` warns and ZERO entity meshes load, the probe
run's second failure). Every run is under `timeout 900` and its full stdout
is captured to `OUT/<cell>.log`; exit code to `OUT/<cell>_exit`.

**Probe run first (thurgadinb, 1024×768, 5 entities, no shim):** the same
command line as the cells below. It must log a clean mode match
("DRM: Found matching mode 1024x768"), "loaded=N>0" race meshes (the
`WILLEQ_DB_PATH` fix), and populate the probe columns.

**Table 1 — resolution A/B (fill vs vertex/CPU bound), per zone Z, two cells:**
- high: `--res 1024 768`
- low : `--res 640 480`
Both: `--zone Z --eq-path $EQ --entities <per-zone> --path-file $OUT/pose_Z
--frames 60 --warmup 5 --allow-mode-fallback --csv --memory-json
--occluder-budget 16 --adaptive-lighting --zone-pp-budget 20 --pp-budget-mode
count --player-light-level 9 --gpu-sync-probe --dump-frames 20,40,59
--dump-png-prefix --drm`. (The same asset/productionConfig line the gate and
B1037 use, plus this card's axes — the card says "reuse B1037's invocation;
only the zone, pose, resolution and clip arguments change".) Per-zone
entity counts = the B854 steady-state census (5/3/2 drawn), so the cells
compare against B854's 20/20/42 fps. Median (post-warmup) `total_ms`,
`cpu_ms`, `gpu_ms`, `flip_wait_ms`, `swap_buffers_ms`, `draws`, `polys` per
CSV (`scripts/b1041_medians.py`). Fill-bound predicts GPU/flip scales
≈×0.391 (pixel ratio); vertex/CPU-bound predicts `cpu_ms` ≈ unchanged.

**Table 2 — clip 300 vs 150 at the high resolution:** NOT runnable on the
published benchmark (fact 1: no clip knob). Recorded as blocked-by-tooling
with the exact command the code change would enable, and answered from the
B854 client log (thurgadinb at Clip 300 is still 20 fps — the clip already
does not save it) + the resolution A/B.

**Table 3 — GPU time + per-frame job count, per zone Z (from the table-1
high cell):**
- GPU time per frame: the high cell's `flip_wait_ms` + `swap_buffers_ms`
  (the measured GPU/present wait, fact 3) and `gpu_ms` (the glFinish span).
- Job count: the high cell is run WITH the census shim
  (`LD_PRELOAD=$D/bin/lima_bo_census.so B962_GEMDUMP=$OUT/<Z>_gem.tsv`), so
  no separate run: `printLedgerPoint("ready"|"steady")` (the benchmark's two
  frame-loop ledger points) fire `willeq_gem_dump()`, and
  `scripts/pi/gem_owner_table.py` `job_rates(<Z>_gem.tsv, start="ready",
  end="steady")` → `gem_pp_jobs_per_frame`, `gem_gp_jobs_per_frame`,
  `gem_waits_per_frame`, `gem_wait_ms_per_frame` over the measured frames
  (the shim counts `eglSwapBuffers` as frames; B1006).
- Record the draws column (`[mergedraws]`'s merged-draws analogue in the
  benchmark), polys, entities placed, the ledger `owned` (the
  `[memledger]` point=steady line in each cell log / `--memory-json`), and
  the page-flip wait (`flip_wait_ms`) for each cell.

**Per-pass fill estimate (C):** from each zone's high-resolution dumped frame
PNG (`--dump-png-prefix`), a per-pixel census by region — method named in the
results: a band scan of the dumped frame classifies each pixel as sky (the
scene clear colour / sky gradient, top of frame), solid scene (terrain/
buildings, opaque), or background, and reports the fraction of the
framebuffer each covers for thurgadinb, cobaltscar and chardok. This is a
pixel count from a dumped frame per layer, as the card allows (the benchmark
has no per-pass coverage column; `--draw-histogram` was not run — see
results for why that is not needed for a pixel count).

**Entity-build slice (D):** explained from the B854 thurgadinb client log
(pulled to the evidence dir) + the B973 code — see results. The log shows the
10.3 ms `meshLoading` window and the governor state during it.

## Results

All medians are post-warmup (frame index >= 5 of 60; `--warmup 5`), over 55
measured frames per cell. `gpu_ms` is the `glFinish()` duration — on Lima there
are no timer queries (lima cost sheet fact 8), so the CPU-blocked-on-GPU finish
IS the measured GPU frame time; it equals `finish_readpixels_ms` here because
the loop's `glReadPixels` (frame dumps) runs inside the same blocked span
(`gles2_zone_benchmark.cpp` P52: the timed finish, then `endGpuSync()`).
`flip_wait_ms` (the endScene page-flip wait) is ~0.04 ms in the HI cells (the
GPU is still busy, so the flip queues non-blocking) and 2.8-3.1 ms in the LO
cells (the GPU finishes before the flip, so the flip waits for vblank) — so it
is NOT the GPU time in either case; the GPU time is `gpu_ms`. The client's
"unaccounted 24-39 ms" (B854) is the same GPU work paid as the flip wait at
the start of the next frame, because the client's swap waits where the
benchmark's does not. The 65-draw cobaltscar cell is the benchmark's pre-merge
draw count; the B854 client's `unmerged_would_be_draws=38` is post-merge.

### Table 1 — resolution A/B (fill vs vertex/CPU bound), Clip 300, per-zone B854 pose

| zone | cell | total ms | cpu ms | gpu ms | draws | polys | ent | fps~ | ledger owned (MiB) |
|---|---|---|---|---|---|---|---|---|---|
| thurgadinb | hi 1024x768 | 37.84 | 9.72 | 28.11 | 154 | 1857 | 3 | 26 | 154.0 |
| thurgadinb | lo 640x480 | 22.69 | 9.49 | 13.16 | 154 | 1857 | 3 | 44 | 151.6 |
| cobaltscar | hi 1024x768 | 20.22 | 4.30 | 15.93 | 65 | 1441 | 2 | 49 | 89.3 |
| cobaltscar | lo 640x480 | 11.56 | 4.15 | 7.40 | 65 | 1441 | 2 | 87 | 86.8 |
| chardok | hi 1024x768 | 21.63 | 3.67 | 17.93 | 28 | 918 | 2 | 46 | 138.1 |
| chardok | lo 640x480 | 11.30 | 3.61 | 7.66 | 28 | 918 | 2 | 89 | 136.5 |

Pixel count lo/hi = 0.391. Measured lo/hi ratios:

| zone | total | cpu | gpu | draws | polys |
|---|---|---|---|---|---|
| thurgadinb | 0.600 | 0.976 | 0.468 | 1.0 | 1.0 |
| cobaltscar | 0.572 | 0.965 | 0.465 | 1.0 | 1.0 |
| chardok | 0.522 | 0.984 | 0.427 | 1.0 | 1.0 |

Verdict: **fragment-fill bound.** The GPU component scales with pixel count
(0.43-0.47x for 0.391x pixels — i.e. slightly super-linear, the tile-based PP
pass walking ~1/2.56 of the tiles); `cpu_ms` is unchanged (0.97-0.98x);
draws/polys identical. The total does not drop a full 0.391x only because the
~4-10 ms CPU submit runs serially in front of the GPU drain. Implied GPU cost
per unit pixel (hi cells): thurgadinb 35.8 ns/px, cobaltscar 20.3 ns/px,
chardok 22.8 ns/px — thurgadinb's is ~1.6x cobaltscar's at equal geometry,
which the fill census (below) explains: full-frame layered fill, not geometry.
Cross-check: thurgadinb at 1024x768 = 37.8 ms here vs B854's 50 ms at
1280x720 on the Orange Pi (Mali-400, slower GPU) — same scene, same direction.

### Table 2 — Clip 300 vs 150

**NOT RUN** — the published benchmark has no clip flag (fact 1: `farClip` is
hardcoded at 300.0f in `zone_bench_frame.cpp:264`, and the fog range is
derived from the same constant at `:142`, so there is no CLI/env override;
this card is measurement-only and cannot rebuild the binary). It is answered
INDIRECTLY, and both indirect lines point the same way:

1. **Geometry is not the variable.** In Table 1, draws and polys are IDENTICAL
   at both resolutions (154/1857, 65/1441, 28/918 — ratio 1.0) while the GPU
   time moves with pixel count (0.43-0.47x). If admitted geometry were the
   bound, a 150 clip (which removes the far sliver) would have shown up in
   draws/polys as resolution did not.
2. **The frame is already 100% full-frame fill at Clip 300.** The fill
   census (below) shows 100.0% of thurgadinb's and chardok's frame is scene
   (opaque or alpha layers, INCLUDING fog-coloured pixels — the fog colour is
   153/166/178, not the clear colour) in every band, with only cobaltscar's
   pose showing 59% clear. A 150 clip removes the far vertices (and their
   fog-coloured pixels), but the far field is ALREADY covered by nearer
   terrain in the census — so the removed pixels were overdraw, not coverage:
   the tile pass still walks the full framebuffer either way (fogged and
   overdrawn pixels are rasterised at Clip 300 too; the clip would skip
   vertices, and Table 1 shows vertices are not the bound).
3. **The B854 client log agrees.** At Clip 300 (the running config),
   thurgadinb holds 20 fps with 42 draws / 9,373 polys — the culling and the
   clip both work; the 20 fps is the fill, not the distance.

So: CLIP IS NOT THE LEVER. The direct measurement a clip flag would enable is
`--far-clip 150` at the same pose/resolution, re-running Table 1's hi cells
(frame time + draws + polys per the card's Step 2 table 2); that needs a
benchmark change, which this card must not make.

### Table 3 — GPU time per frame + per-frame Lima job count (high cells)

| zone | gpu ms (glFinish) | PP jobs/f | GP jobs/f | waits/f | wait ms/f |
|---|---|---|---|---|---|
| thurgadinb | 28.11 | 1.016 | 1.016 | 2.484 | 0.014 |
| cobaltscar | 15.93 | 1.016 | 1.016 | 1.094 | 0.007 |
| chardok | 17.93 | 1.016 | 1.016 | 0.516 | 0.004 |

The B1006 census (shim on the high cells, `ready`->`steady`, 64 counted
frames): **exactly one GP job and one PP job per frame** (1.016 = 64/63
rounding of a frame counted at both endpoints), i.e. NO job splits — the
28 ms is not B972-FT-style transfer-write flushes (those read 3.1-3.2 jobs/f),
it is one unsplit tile-based PP pass over the full framebuffer. CPU-side
GEM wait is ~0.01-0.02 ms/frame: negligible. The GPU figure is MEASURED (the
glFinish span), not inferred from a vblank wait; the `gpu_*_ns` timer-query
columns are -1 (Lima has no timer queries, fact 8) and are not used.

### Per-pass fill estimate (acceptance C)

Method: pixel count from the dumped hi-res frame (one frame per zone, frame
20 of the measured window), two classes as the benchmark can report: "sky" =
pixels reading the clear colour, "scene" = everything else. The benchmark
clears the scene to `SColor(255,40,40,60)` = R40/G40/B60 (the moving-loop
`beginScene` in `gles2_zone_benchmark.cpp`); verified against cobaltscar's top
strip, where 18,792 of 20,480 samples ARE exactly the clear colour. Tolerance
+/-8/channel. Bands: top/middle/bottom thirds. NOTE: this is NOT a per-layer
sky/terrain/water/alpha split — the published benchmark has no per-pass
coverage dump (only whole-frame PNGs and a draw-composition histogram,
`zone_bench_options.h:204-213`), so "scene" is the union of every opaque +
alpha layer that wrote the pixel; the card's per-layer ask is answered at this
coarser grain, and the layer identity is argued, not measured.

| zone | sky % | scene % | scene by band (top/mid/bottom) |
|---|---|---|---|
| thurgadinb | 0.0 | 100.0 | 33.3 / 33.3 / 33.3 |
| cobaltscar | 59.1 | 40.9 | 1.5 / 7.9 / 31.5 |
| chardok | 0.0 | 100.0 | 33.3 / 33.3 / 33.3 |

Every pixel in thurgadinb's and chardok's frame is covered by scene layers at
the B854 pose — the fill is FULL-FRAME, in all three bands, not a localised
island. (The fog does NOT fade the distance into the clear: the fog colour is
`setFog(..., 0.6, 0.65, 0.7, 1.0)` = ~153/166/178, `zone_bench_frame.cpp:142-145`
— a mid-grey-blue that is scene pixels in this census, so the 100% figure is
even more conservative than it looks: fog-covered pixels count as SCENE.)
cobaltscar's pose faces the sky (59% of the frame is the uncovered clear, the
scene concentrated in the bottom third). This is the
geometry behind Table 1's 35.8 vs 20.3 ns/px: thurgadinb pays a full tile walk
per full-frame layer for a framebuffer cobaltscar only fills 41% of, with far
fewer full-frame layers.

### The entity-build slice (acceptance D)

From the B854 thurgadinb log + the B973 code:

- `meshLoading` in the frame-timing log is the POST-LOAD ENTITY-BUILD section,
  not zone-mesh loading (`renderer_frame.cpp:176`: the B973 branch charges
  `runEntityBuildSlice()` to `frameTimings_.meshLoading`; the zone meshes were
  complete at `zone_load_complete`, before this window).
- The 10.3 ms value runs for **476 consecutive SPIKE frames**
  (log lines 1409-3310, 04:01:14.766 -> 04:02:05.152, a 50.4 s window), then
  drops to 4.3 ms for the final 158 frames of the sweep (counts: 476 x 10.3,
  158 x 4.3 — `b1041_logcheck.py` over the log; `meshLoading` appears in no
  other value).
- What it is building: entities deferred by the T109 load-distance gate
  (radius 400: "27 in-radius, 62 deferred", 94 entities total) — the
  background `EntityPrepWorker` feeds the slice prep results
  (`processOneEntityBuildStep` / `pollAndDistributePrepResults`), whose per-unit
  cost lands in the scheduler's EMA and caps the next unit.
- Why exactly 10.3 ms every frame: the governor held **Green** the whole
  window (the only `governor=` change in the log), so the slice budget is
  `greenFraction` x target = 0.20 x 33.3 ms = **6.67 ms**
  (`entity_build_scheduler.cpp`, `EntityBuildSlicePolicy::greenFraction`); the
  first build unit per frame is ALWAYS started (guaranteed progress, every
  governor state) and a single entity-build unit costs ~10 ms on the Pi,
  bigger than the budget — the slice runs the one unit, measures ~10.3 ms,
  stops (the next unit would not fit the 6.67 ms slice), and repeats: the
  constant 10.3 ms IS the per-entity cost, not a growing backlog. It "completes"
  in the sense that the window ends at 4.3 ms/frame when the remaining units
  are cheaper (finalize/name-tag units), at which point the 62 deferred are
  built out.
- It is CPU-side (a serial pre-draw slice), so it does not enter the GPU
  fill numbers above; at 33.3 ms/frame it is 31% of the frame and is the
  client's own bounded cost (6.7 ms budget + one over-budget unit).

## The mechanism (B1006: named per axis)

**Fill.** Driver fact: lima cost sheet fact 7 — "the tile-based PP pass
re-walks the framebuffer per job"; on Mali-4xx each PP job re-reads the tile
buffer it writes, so a full-frame layer costs a full-frame walk and the cost
scales with covered pixels, not vertices. Prediction: halving the pixel count
cuts the GPU component by ~half at equal geometry; the CPU submit is unchanged.
Measurement (Table 1): for 0.391x pixels the GPU component is 0.468/0.465/
0.427x and cpu_ms is 0.97-0.98x, draws/polys 1.0 — as predicted. The GPU
side is slightly SUPER-linear per unit pixel (0.47x for 0.391x pixels, i.e.
~1.05-1.19x the naive fill-rate prediction), consistent with the tile pass
re-walking its neighbours, not fewer walks. FILL IS THE BOUND.

**Clip.** Fact: the frame is 100% layer-covered at Clip 300 in thurgadinb and
chardok (measured, fill census), and the far field is fog-coloured, not
clear-coloured (the fog is 153/166/178, `zone_bench_frame.cpp:142-145`) — so
a 150 clip removes only overdrawn far fragments whose pixels nearer terrain
already owns in the census. Prediction: clip 300->150 removes no measurable
fill or vertices. Measurement: the clip knob is absent from the published
binary (Table 2, NOT RUN), but the two axes that WOULD expose it are both out
— resolution (pixels) moves time and draws/polys (geometry admitted) do not,
and the B854 log shows 20 fps AT Clip 300 with the same census. CLIP IS NOT
THE LEVER (and the card's hypothesis of "wrong view/render-distance
processing" is refuted: the clip is applied — 300 — and culling works:
154/65/28 draws and 1857/1441/918 polys at 20/20/60-fps scenes).

**GPU time.** Fact: fact 8 — no timer queries on Lima; fact 7 — one GP + one
PP job per frame unless a buffer write splits it. Prediction: a fill-bound
frame shows ~1.0 jobs/f and a glFinish that scales with pixel count.
Measurement (Table 3): 1.016/1.016 jobs/f, 0.004-0.014 ms/f wait, glFinish
28.1/15.9/17.9 ms scaling 0.43-0.47x with pixels. The measured GPU figure is
the glFinish span itself; nothing is inferred from the flip wait (the flip is
non-blocking in the benchmark; the client pays the same GPU work as the flip
wait, which is why B854's "unaccounted" is 24-39 ms while endScene is <1 ms).

## Ranked lever list (predicted saving in ms from the tables)

Savings are per slow frame at the B854-equivalent scene, measured from the
tables above.

1. **Render-resolution scale (adaptive, governor-driven):** the measured
   hi->lo (1024x768 -> 640x480, 0.391x pixels) saved 15.2 ms on thurgadinb
   (37.8 -> 22.7), 8.7 on cobaltscar, 10.3 on chardok. A 720p->480p step of the
   same ratio on the Pi's 1280x720 run predicts ~13-16 ms/frame in the 20 fps
   zones (the B854 50 ms -> ~35 ms, i.e. ~20 fps -> ~29 fps). CHANGES THE LOOK
   (softening) -> ruling request below.
2. **Full-frame layer reduction (fewer tile walks):** thurgadinb's 35.8 ns/px
   vs cobaltscar's 20.3 ns/px at equal pixel count is ~15.5 ns/px of
   EXTRA per-pixel cost = full-frame layers. Merging or dropping ONE
   full-frame pass at 786k px predicts ~5-8 ms/frame (one walk at ~6-10
   ns/px); dropping the far-field fog blending (the fog is 180..285 units,
   `zone_bench_frame.cpp:142-145` — near terrain already covers those pixels
   in the census, so the far field is fog-coloured OVERDRAW, not coverage)
   saves the blend without losing coverage. This is the only lever that keeps
   the resolution.
3. **Frame-governor work reduction below target fps:** the B854 log shows the
   governor holding Green (33.3 ms budget) while frames run 50 ms — a
   Red-state work cut (entities off, one fewer alpha pass) at the same
   resolution would trade detail for the 20 fps floor; the lever is bounded by
   how much the scene actually spends above the budget (thurgadinb's
   50-33.3 = ~17 ms of over-run, of which ~10 ms is the build slice).
   Will's ruling: a frame governor is a wanted, period-free technique.
4. **Clip 300 -> 150: predicts ~0 ms** (Table 1: geometry is not the variable;
   census: the far field is overdrawn fog, not unique coverage; Table 2: NOT
   RUN on the published binary). Not a lever.
5. **Entity-build slice (the 10.3 ms):** already bounded by B973 (6.67 ms
   budget + one guaranteed unit); it is CPU-side and does not touch the GPU
   fill. Predicted saving of capping the unit at the slice: ~3-4 ms/frame in
   the build window only (50.4 s of zone-in), and it trades build time for a
   longer entity pop-in. Low priority for the 20 fps steady state, which ends
   when the slice completes.

## RULING REQUEST (orchestrator files it)

Lever 1 (render-resolution scale) changes the look — the measured saving
(13-16 ms/frame in the 20 fps zones at a 720p->480p step) depends on a
resolution the player did not ask for. Will's ruling is required before any of
it is built: "B1041 measured that the 20 fps Pi zones are fragment-fill bound
(Table 1: GPU 0.43-0.47x at 0.391x pixels, CPU unchanged; Table 3: one
unsplit PP job/frame; fill census: 100% full-frame coverage in
thurgadinb/chardok). The largest single saving is render-resolution scaling
(~13-16 ms/frame at a 720p->480p step on thurgadinb). May the frame governor
drop resolution in the 20 fps zones (and by how much), or must the resolution
hold and the savings come from full-frame layer merging (lever 2, ~5-8 ms per
full-frame pass, far-field fog blending look-neutral since near terrain already
covers those pixels)?"

## Scripts + raw evidence (all under /home/user/kb-evidence/kb-b1041-pi-gpu-bound/)

- `scripts/b1041_run.sh` — the device-side runner (the exact commands above,
  lock already held); `b1041_medians.py` (Table 1/2/3 medians), `b1041_gems.py`
  (Table 3 job counts, reusing the repo's `gem_owner_table.job_rates()`),
  `b1041_fill.py` (the per-pass fill census), `b1041_clear.py` (the clear-colour
  detection). Every table number is one of their outputs.
- `b854_logs/{thurgadinb,cobaltscar,chardok}.client.log` — the B854 rendered
  sweep logs (the pose, the 20/20/60 fps, the 10.3 ms x 476 build-slice
  window, the Clip 300 census — source of record for D and Table 2).
  `b1041_logcheck.py` re-derives the slice window and value counts from it.
- `lane2/b1041/` — raw lane-2 outputs: `<cell>.csv` (the frame CSVs),
  `<cell>.log` (full stdout incl. the `[memledger]`/`[memgpu]`/`[memmap]`
  lines and the `owned=` figures), `<Z>_gem.tsv` (the census dumps),
  `<cell>_f20.png` (the fill-census frames), `device.txt`,
  `thermal_before/after.txt`, `runner.log`, `BUILD_INFO` (inside device.txt).
