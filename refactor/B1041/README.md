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
thurgadinb's constant **10.3 ms/entity-build slice for 432+ consecutive frames**.

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
   640×480** (no 1280×720, no 640×360). But the benchmark renders into a
   **GBM surface sized to `--res`** (`CIrrDeviceFB::initGBM` →
   `gbm_surface_create(W, H, XRGB8888)`; `getScreenSize()` returns that W×H),
   while the **CRTC scanout** is set to the connector's preferred mode
   (1024×768) at `initDRM`. So the **GPU rasterizes the requested resolution**
   and the display scales it out — the ACHIEVED render resolution (and hence the
   fill cost) is the requested one, and P49's achieved-vs-requested check
   (`setActualResolution`, `--allow-mode-fallback`) passes. This is confirmed
   by the probe run before the grid (below). The resolution A/B is therefore a
   TRUE pixel-count A/B: 1280×720 = 921 600 px vs 640×360 = 230 400 px (exactly
   1/4).
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
the gate's `--camera-pose … ,0.0,-5.0`). One pose per zone, repeated across every
resolution/clip cell so the ONLY thing that changes between cells is the axis
under test. `--path-file` plays the same pose for all measured frames (the Q01
replay path: one pose line, `--frames` repeats it), which gives a steady-state
median over many frames rather than a single cold pose capture.

## The runs (what actually ran, and what each file is)

All commands run on lane 2 via `pi_remote` while the lock is held. `D` is the
published build dir above; `EQ=/mnt/projects/EverQuestP1999`; `OUT=/tmp/b1041`.
Every run is under `timeout 900` and its full stdout is captured to `OUT/<cell>.log`.

**Probe run first (validates the two tooling facts, one zone, short):**
```
WILLEQ_DATA_ROOT=$D WILLEQ_CACHE_DIR=/tmp/b1041/cache timeout 900 \
  $D/bin/gles2_zone_benchmark --zone thurgadinb --eq-path $EQ \
  --path-file <pose thurgadinb> --frames 30 --warmup 10 --res 1280 720 \
  --allow-mode-fallback --csv $OUT/probe.csv --occluder-budget 16 \
  --adaptive-lighting --zone-pp-budget 20 --pp-budget-mode count \
  --player-light-level 9 --gpu-sync-probe --dump \
  --dump-frames 10,20,29 --dump-png-prefix $OUT/probe --drm
```
Confirm from `probe.csv` that the achieved resolution columns read 1280×720
(fact 2) and that the `flip_wait_ms` / `swap_buffers_ms` / `gpu_*` columns are
populated (fact 3). Then the grid.

**Table 1 — resolution A/B (fill vs vertex/CPU bound), per zone Z, two cells:**
- high: `--res 1280 720` + `--gpu-sync-probe` + `--csv` + `--dump-frames
  20,40,59 --dump-png-prefix OUT/<Z>_1280` + `--draw-histogram OUT/<Z>_1280_hist.csv`
- low : `--res 640 360`  + `--gpu-sync-probe` + `--csv` + `--dump-frames
  20,40,59 --dump-png-prefix OUT/<Z>_640`  + `--draw-histogram OUT/<Z>_640_hist.csv`
Both with the same `--path-file` pose, `--frames 60 --warmup 5`,
`--occluder-budget 16 --adaptive-lighting --zone-pp-budget 20 --pp-budget-mode
count --player-light-level 9`, `--allow-mode-fallback`, `--drm`.
Median `total_ms` (post-warmup), `cpu_ms`, `gpu_ms`, `flip_wait_ms`,
`swap_buffers_ms`, `draws`, `polys` from each CSV. Fill-bound predicts the
GPU/flip component scales ~×1/4 (pixel count); vertex/CPU-bound predicts
`cpu_ms` ~unchanged.

**Table 2 — clip 300 vs 150 at 1280×720:** NOT runnable on the published
benchmark (fact 1). Recorded as blocked-by-tooling with the exact command the
code change would enable, and answered from the B854 client log (thurgadinb at
Clip 300 is still 20 fps — the clip already does not save it) + the resolution
A/B. If a server session is stood up, the client's `/clipdist 150` at the same
pose is the direct test.

**Table 3 — GPU time + per-frame job count, per zone Z (from the table-1 high cell):**
- GPU time per frame: the high cell's `flip_wait_ms` + `swap_buffers_ms`
  (the measured GPU/present wait, fact 3) and `gpu_ms` (glFinish span).
- Job count: re-run the high cell with the census shim:
```
env LD_PRELOAD=$D/tools/lima_bo_census.so B962_GEMDUMP=$OUT/<Z>_gem.tsv \
    WILLEQ_DATA_ROOT=$D WILLEQ_CACHE_DIR=/tmp/b1041/cache timeout 900 \
  $D/bin/gles2_zone_benchmark …same high cell… 
```
  then `scripts/pi/rendered_zone_summarize.py` (B1006's `job_rates()`) →
  `gem_pp_jobs_per_frame`,
  `gem_gp_jobs_per_frame`, `gem_waits_per_frame`, `gem_wait_ms_per_frame`
  (the ready→steady_60s deltas as per-frame rates).
- Record `[mergedraws]` (draws column), polys, entities drawn (entitiesVisible),
  the ledger `owned` (`--memory-json` / the `[memledger]` point=steady line), and
  the page-flip wait (`flip_wait_ms`) for each cell.

**Per-pass fill estimate (C):** from each zone's dumped frame PNG
(`--dump-png-prefix`), a per-pixel census by region — the method is named in the
results: a horizontal band scan of the dumped frame classifies each pixel as
sky (the clear colour / sky gradient, top of frame), solid scene (terrain/
buildings, opaque), or transparent/background, and reports the fraction of the
framebuffer each covers for thurgadinb, cobaltscar and chardok. The
`--draw-histogram` (polys/draw, per-material) is the scene-node census that
backs which draws are the full-frame layers. This is a pixel count from a
dumped frame per layer, as the card allows.

**Entity-build slice (D):** explained from the B854 thurgadinb client log
(pulled to the evidence dir) + the B973 code — see results. The log shows the
10.3 ms `meshLoading` window and the governor state during it.

## The runs (populated after the grid) — see results

## Results

*(populated from the runs)*

## The mechanism (B1006: named per axis)

*(populated from the runs)*

## Ranked lever list (predicted saving in ms from the tables)

*(populated from the runs)*

## RULING REQUEST (orchestrator files it)

*(only if a lever changes the look; otherwise "none")*

## Scripts + raw evidence (all under /home/user/kb-evidence/kb-b1041-pi-gpu-bound/)

- `scripts/` — the analysis scripts (frame-time medians, per-pixel fill census,
  B854 log window analysis); every table number is their output.
- `b854_logs/{thurgadinb,cobaltscar,chardok}.client.log` — the B854 rendered
  sweep logs pulled from `/mnt/projects/fable/review/B854-rendered/sweep-full/`
  (the pose + entity-build + clip source of record).
- `lane2/<cell>.csv`, `lane2/<cell>.log`, `lane2/<Z>_gem.tsv`,
  `lane2/<Z>_<res>_f<n>.png`, `lane2/BUILD_INFO`, `lane2/device.txt` — raw
  lane-2 outputs.
