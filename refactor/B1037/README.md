# B1037 (#356): lane 2 (Rock64, Lima) vs the Pi visual-gate goldens + lane 3 (ROCK 4D, Panfrost) through the gate once

The measurement B1033 asks for before any per-lane golden question
(`docs/decisions.md` 2026-10-12: "whether lane 2 matches the Pi goldens is a
measurement to take first"). REPORT ONLY: this unit changes no golden, no
manifest zone, no budget, no preset. If a lane is not pixel-identical, the
mechanism is named per the B1006 rule and a class-C ruling request is written
under `RULING REQUEST (orchestrator files it)`.

## Inputs (all verified present at run start, logged in `lane*/device.txt`)

- Binary: the published aarch64 fable build
  `/mnt/projects/fable/ci-arm64/015a9dd057aed831f65dcc1144274a6bba653c50/`
  (built 2026-10-19 04:23Z on gitlab-runner-2; `BUILD_INFO` quoted in
  `device.txt`; `by-branch/fable` points at this sha). Every number in this
  README is from THAT binary on THAT sha.
- Gate invocation source of truth: `scripts/ci/visual_gate.py`
  (`run_benchmark()` with the `--platform pi` `PlatformConfig`),
  `tests/goldens/manifest.json` (benchmark config, `productionConfig`,
  per-zone `extraArgs`/`designatedFrames`), Pi goldens
  `tests/goldens/pi/<zone>/f<n>.png` + `manifest["platforms"]["pi"]`.
- Lanes: `PI_LANE=2` (rock64, RK3328, Mali-450, system Mesa 25.2.8 — the same
  Mesa the Pi runs; `scripts/pi/env.sh` lane table), `PI_LANE=3` (rock4d,
  RK3576, Mali-G52, Panfrost, system Mesa 26.0.8; B1030's EGL-config fix is
  in the build, `docs/ci.md` "The aarch64 SBC lanes"). Neither lane mounts
  this worktree (hermes-ext has no NFS), so the python gate cannot run on the
  device: the device runs the gate's exact benchmark command line, and the
  gate's exact comparer runs locally against the pulled-back frames.

## The recipe (written before any run)

Per lane N ∈ {2, 3}:

1. `PI_LANE=N bash scripts/pi/env.sh` sourcing; `pi_lock_take B1037` (atomic
   noclobber ON the device; non-zero exit = stop).
2. `pi_hdmi_force_on` (the two boards share one KVM; the connector is
   `disconnected`, so the kernel is asked to offer its default modes;
   `pi_hdmi_restore` at cleanup — both env.sh, unchanged from lane_smoke.sh).
3. Copy `b1037_run.sh` to the device's `/tmp`, then
   `pi_remote "bash /tmp/b1037_run.sh /tmp/b1037"` which, per zone
   (qeynos2, gfaydark, befallen, iceclad, timorous — the five manifest gate
   zones, in manifest order) runs EXACTLY the gate's `--platform pi`
   benchmark invocation for that zone:

   ```
   WILLEQ_DATA_ROOT=$D WILLEQ_CACHE_DIR=/tmp/b1037/cache timeout 600 \
     $D/bin/gles2_zone_benchmark \
       --zone <zone> --eq-path /mnt/projects/EverQuestP1999 \
       --frames 60 --warmup 5 --res 800 600 --dump-frames 0,1,2 \
       --dump-png-prefix /tmp/b1037/<zone> \
       --csv /tmp/b1037/<zone>.csv --memory-json /tmp/b1037/<zone>_mem.json \
       --occluder-budget 16 \
       --adaptive-lighting --zone-pp-budget 20 --pp-budget-mode count --player-light-level 9 \
       --camera-pose ... --camera-pose ... --camera-pose ...   (the zone's manifest extraArgs) \
       --drm
   ```

   i.e. `run_benchmark()`'s command line word for word (manifest benchmark
   config + `productionConfig.args` + the zone's `extraArgs` + the pi
   config's `--drm`), with the pose strings copied from
   `tests/goldens/manifest.json` at 015a9dd0 into `b1037_run.sh`. While each
   run is alive the script snapshots `/proc/<pid>/maps` the moment
   libgallium is mapped — the gate's `poll_maps` driver-load proof. (The
   benchmark runs under a `timeout 600` wrapper, so the script first resolves
   the wrapper's child pid — the benchmark — then polls that one; a maps
   snapshot of the wrong pid would prove nothing. The gate polls the
   benchmark pid directly because it execs it without a wrapper.) Frames are
   moved to the gate's nested layout `<zone>/f<n>.png` afterwards (the
   `run_benchmark` pi branch does the same).
4. Pull the run dir back over ssh to
   `/home/user/kb-evidence/kb-b1037-lane-goldens/lane<N>/`.
5. Run the gate's OWN compare rule locally:
   `python3 /home/user/kb-evidence/kb-b1037-lane-goldens/scripts/compare_lane.py <evidence-dir>`.
   That script imports `scripts/ci/visual_gate.py` and calls, unmodified:
   `compare_images_exact()` (the one compare rule, decoded-pixel RGB),
   `assert_memory_budgets()`, `assert_memory_ledger()` and
   `report_rss_target_gap()` (256 MiB ceiling assert + 64 MiB target gap
   printout), against the committed Pi goldens
   `tests/goldens/pi/<zone>/f<n>.png` and the `platforms.pi` section. It also
   records draws/stateChanges/polys per designated frame vs the Pi section's
   blessed metrics. `compare_report.txt` is program output, never typed.
6. `pi_lock_release B1037`; `pi_hdmi_restore`; verify `pgrep -af willeq`
   empty on the lane (no leftover processes; no reboot).

No golden, manifest, preset, budget or third_party file is touched anywhere
in this chain; the lanes are left as found (HDMI connector status restored
from the env.sh record; lock released owner-checked).

## Results

Both lanes are **NOT pixel-identical** to the committed Pi goldens — but the
reason is **B976's depth-tie fix landing differently on the two lanes' depth
formats** (its Pi leg is the pending follow-up), not a fresh per-lane rendering
defect, and the genuine per-lane residual is small and 1-LSB. Every number
below is program output from the scripts named at the end (never typed);
raw frames, CSVs, memory JSONs, per-zone logs, `/proc/<pid>/maps` proofs and
thermal blocks are in `/home/user/kb-evidence/kb-b1037-lane-goldens/lane2/`
and `.../lane3/`.

**Driver-load proof (the gate's `poll_maps`), from each zone's
`/proc/<pid>/maps` snapshot taken while libgallium is mapped:**

| lane | `[gldriver]` (client's own census) | loaded, from maps | EGL config |
|---|---|---|---|
| lane 2 (Rock64) | `Mali450`, gallium `libgallium-25.2.8-0ubuntu0.24.04.4.so`, `libLLVM.so.20.1` | `libEGL_mesa.so.0.0.0`, `libgallium-25.2.8-...so`, `libLLVM.so.20.1` | `depth=24 stencil=0`, GBM XRGB8888 |
| lane 3 (ROCK 4D) | `Mali-G52 r1 MC3 (Panfrost)`, gallium `libgallium-26.0.8-1ubuntu0.3.so`, `libLLVM.so.21.1` | `libEGL_mesa.so.0.0.0`, `libgallium-26.0.8-...so`, `libLLVM.so.21.1` | `depth=16 stencil=0`, GBM XRGB8888 |

Neither lane's `gpu_driver`/`gpu_buffers` ledger row is affected by this — the
depth config is a surface property, and the `[memledger]` figures below are the
client's own census.

### Lane 2 (Rock64, Mali-450, Lima, Mesa 25.2.8)

| zone | frame | pixels vs Pi golden | metrics (draws/state/polys) vs Pi |
|---|---|---|---|
| qeynos2 | f0 | differs (134 492 px) | bit-exact |
| qeynos2 | f1 | **IDENTICAL** | bit-exact |
| qeynos2 | f2 | differs (290 px) | bit-exact |
| gfaydark | f0 | differs (36 px) | bit-exact |
| gfaydark | f1 | differs (565 px) | bit-exact |
| gfaydark | f2 | differs (693 px) | bit-exact |
| befallen | f0 | differs (37 px) | bit-exact |
| befallen | f1 | **IDENTICAL** | bit-exact |
| befallen | f2 | **IDENTICAL** | bit-exact |
| iceclad | f0 | differs (1 231 px) | bit-exact |
| iceclad | f1 | differs (51 940 px) | bit-exact |
| iceclad | f2 | differs (13 619 px) | bit-exact |
| timorous | f0 | differs (114 732 px) | bit-exact |
| timorous | f1 | differs (1 218 px) | bit-exact |
| timorous | f2 | differs (143 514 px) | bit-exact |

3 of 15 frames byte-identical (and, under the gate's decoded-pixel compare
rule, pixel-identical); the other 12 differ — qeynos2 f2 by 290 px, the rest
by the counts in the table — and every non-identical
diff is **localized to the lower frame (the flat floor)** with a uniform
tint shift and near-zero edge signature (0.4–97% near a golden edge, the rest
clean floor). **Per-pixel census (`definitive_census.py`, locus = the
B976 fix's pixel set = where x86-post-fix differs from the committed Pi
golden):** on every frame, 58–100% of the locus pixels on lane 2 are **still
at the pre-fix (dark) value**, only 0–19% match the blessed x86 value
exactly, and the A+C share (blessed value, or 1–3 LSB from it) is 0–41%.
Over the full locus, 74–99% of the pixels are within 1 LSB and 92–100% within
3 LSB of the blessed value — the unlit part of the floor is at most a few LSB
away from where the fix puts it. The off-locus diff (pixels the fix never
touched) is 0–3 088 px per frame (mostly ≤1 LSB; at most 6 pixels over 7 LSB
in any one frame — 32–34 LSB on timorous f2, 13 LSB on timorous f0, 18–21 LSB
on qeynos2 f2). In other words: **on Lima the B976 bias lights a limb of the
floor, but the majority of the floor it lit on x86 stays dark** — the
layers' stored-depth gap exceeds the 2-D16-cell bias over most of the floor
on this D24 surface, which is outside B976's tie-only scope. Lane 2 is
therefore still measuring the pre-B976 floor, plus a partial-lit limb — its
diff vs the committed (pre-B976) Pi golden is that limb.

### Lane 3 (ROCK 4D, Mali-G52, Panfrost, Mesa 26.0.8)

| zone | frame | pixels vs Pi golden | metrics (draws/state/polys) vs Pi |
|---|---|---|---|
| qeynos2 | f0 | differs (323 601 px) | bit-exact |
| qeynos2 | f1 | differs (315 178 px) | bit-exact |
| qeynos2 | f2 | differs (285 139 px) | bit-exact |
| gfaydark | f0 | differs (217 722 px) | bit-exact |
| gfaydark | f1 | differs (184 474 px) | bit-exact |
| gfaydark | f2 | differs (143 906 px) | bit-exact |
| befallen | f0 | differs (299 222 px) | bit-exact |
| befallen | f1 | differs (292 748 px) | bit-exact |
| befallen | f2 | differs (280 117 px) | bit-exact |
| iceclad | f0 | differs (205 290 px) | bit-exact |
| iceclad | f1 | differs (241 126 px) | bit-exact |
| iceclad | f2 | differs (170 013 px) | bit-exact |
| timorous | f0 | differs (294 218 px) | bit-exact |
| timorous | f1 | differs (204 395 px) | bit-exact |
| timorous | f2 | differs (351 818 px) | bit-exact |

0 of 15 byte-identical. Two overlapping signatures:

1. **The B976 floor flip, in full** (unlike lane 2, which is partial): the
   `definitive_census.py` A/B/C census shows the lane matching the blessed
   x86 lit value on 45–96% of the locus per frame (80–98% within 1–3 LSB,
   A+C), at 0–1 LSB where it disagrees with the blessed.
2. **A frame-wide ~1-LSB offset the Lima lane does not have** — measurable
   OUTSIDE the floor locus, where the Pi and x86 goldens agree:
   `offlocus_mag.py` shows 2 102–27 254 px per frame (5–6% of the 800×600
   frame) differing from the Pi golden by exactly 1 LSB, 99–100% of them.
   Across the whole frame `framefrac.py` shows 28–57% of pixels at exactly
   1 LSB, and 41–99% of each frame's differing pixels at 1 LSB. This is a
   driver-wide final-color/precision class (the Panfrost/Mali-G52 driver's
   own), not a lighting or geometry defect — the frame content is the same,
   shifted by the last quantization step.

### Memory (the gate's `assert_memory_budgets` + `assert_memory_ledger`)

All fifteen zone runs PASS the 256 MiB `RssAnon` ceiling and every
`owned = RssAnon − gpu_driver + gpu_buffers` figure is under 256 MiB.

| lane | zone | RssAnon | owned | 256 MiB ceiling |
|---|---|---|---|---|
| 2 | qeynos2 | 63.0 | 77.7 | PASS |
| 2 | gfaydark | 54.3 | 66.2 | PASS |
| 2 | befallen | 72.6 | 96.8 | PASS |
| 2 | iceclad | 133.8 | 210.7 | PASS |
| 2 | timorous | 136.3 | 212.8 | PASS |
| 3 | qeynos2 | 78.4 | 89.5 | PASS |
| 3 | gfaydark | 69.7 | 75.1 | PASS |
| 3 | befallen | 72.3 | 83.6 | PASS |
| 3 | iceclad | 179.6 | 226.5 | PASS |
| 3 | timorous | 172.1 | 219.8 | PASS |

The gate's `allocator slack` bound (35% of RssAnon) is **breached on lane 2**
for qeynos2 (35.9%), gfaydark (41.2%) and befallen (49.2%) and PASSes lane 3
entirely. That bound is a glibc-arena fragmentation property of the specific
allocator+driver, not a render or memory-usage issue, and it is not part of
the pixel-golden question — flagged for the record, not ruled here.

## The mechanism (B1006: named before any ruling)

**The committed Pi goldens are pre-B976, and my binary is post-B976 — and the
two lanes apply the fix OPPOSITELY.** The Pi golden set was blessed at
`55ec17b3` (2026-10-12, B1024) from a tree that does not contain the B976 code
(`git cat-file -e 55ec17b3:include/client/graphics/zone_depth_tie_break.h` →
absent). B976 (#294) — the top-facing (+Y) zone region wins a depth tie —
landed in the tree at `3e76c388` and was blessed for the x86 goldens at
`a47d496e` (2026-10-14), whose commit states verbatim: "Pi goldens
(`tests/goldens/pi/`) are NOT in this bless: the Pi leg of the fix has not run
yet (MR FOLLOW-UP)." The binary I ran (`015a9dd0`, 2026-10-19) carries the
fix, so its floor is the post-fix floor, and the committed Pi golden is the
pre-fix floor.

Per-pixel census (`definitive_census.py`, locus = where x86-post-fix differs
from the committed Pi golden, classified per pixel: A = lane matches the
blessed x86 lit value, B = lane still at the pre-fix dark value, C = a third
value within 1–3 LSB of blessed):

- **Lane 3 (Panfrost, depth=16): the fix lands IN FULL.** A is 45–96% of the
  locus per frame and A+C (matching the blessed lit value, or 1–3 LSB from
  it) is 80–98%; over the full locus 99.6–100% of the pixels are within 1 LSB
  and 100% within 3 LSB of the blessed value (the B limb is 1–19%,
  timorous f1's 19% the largest still-dark share; the C share 2–38%,
  qeynos2 f1's 38% the largest). **The floor lights the way the blessed
  image says.** This is the first D16 driver ever measured with B976 in a
  Pi-leg run: it applies, and it applies at the 1-LSB level.
- **Lane 2 (Lima, depth=24): the fix does NOT land as a whole.** B (still
  dark) is 58–100% of the locus per frame — 100% on qeynos2 f1/f2, gfaydark
  all three, befallen f1/f2, timorous f1 (those are the frames where lane 2 is
  byte-identical or near it), and 58–78% on the lit frames (qeynos2 f0 58%,
  iceclad f1 78%, timorous f0 64%). A is 0–19% and A+C is 0–41% — over the
  full locus, 74–99% of the pixels are within 1 LSB and 92–100% within 3 LSB
  of the blessed value, so the unlit part of the floor is at most a few LSB
  from where the fix puts it. I.e. on Lima the 2-D16-cell bias lights only a
  limb of the floor per pose — the layers' stored-depth gap on this D24
  surface exceeds the bias over most of the floor, which is outside B976's
  tie-only scope. Lane 2 therefore renders NEAR the committed pre-fix Pi
  golden, not the post-fix image, and its diff vs the golden is exactly the
  partial-lit limb.

**Neither lane is a rendering regression. The asymmetry is the stored-depth
gap, and it is now measured.** The two floor layers are a near-tie in NDC-z,
but NOT exactly equal: the *unbiased* stored gap between them is a small,
platform-specific number — the rasterizer's own quantization of each layer —
and it varies across the floor. The B976 bias adds exactly 2 D16 cells of
NDC-z to the −Y layer's stored depth on every surface, format-independent by
construction (`zone_depth_tie_break.h`: a constant NDC polygon offset, sized
as the smallest value that breaks the *measured D16* tie with a 1-cell
margin). It reorders only the pixels whose unbiased gap it dominates — which
is how a single constant works on every surface, and why a gap larger than
the bias on some surface is expected behavior, not a defect of the bias.
The census measures where each surface sits on that spectrum: on a **D16**
surface the quantized gap is mostly ≤ 2 D16 cells, so the push wins nearly
everywhere — lane 3 (D16) shows 80–98% of the locus at or within 1–3 LSB of
the blessed lit value (A+C), 1–19% the still-dark limb. On lane 2's
**D24** surface the finer grid preserves a larger rasterized gap that exceeds
the bias over most of the floor, so the −Y layer keeps winning: 58–100% of
the locus stays at the pre-fix value. (This also matches B976's pre-fix table
in `refactor/B976/README.md`: the same near-tie rendered a uniform +Y floor
on x86 D24S8 but a uniform −Y floor on the Pi's D24 Lima — the winner is a
function of **both** the depth format and the driver's depth-compare
semantics. B976's fix removed that dependence on x86, and lane 2 is the first
independent confirmation that it still stands on a real D24 Lima surface: the
same D24 format that rendered −Y pre-fix still mostly renders −Y post-fix
there, because the D24 rasterized gap — not the tie — is what decides it.) Lane 2's Rock64
negotiated `depth=24` (its `eglChooseConfig` picks it — the production
orangepi preset only forces `enableStencilBuffer: false` and lets the driver
choose depth, so each lane's depth is a surface property of its own
Mesa/driver, not a preset setting), while the committed Pi goldens were
blessed on an Orange Pi (Mali-400, Lima) running a **16-bit** depth buffer
(B971's bless note, manifest `platforms.pi`) — so the same Lima driver family
renders the floor two different ways across the two surfaces, and lane 2's
D24 is the one that does NOT match the D16-blessed Pi golden. That is the
B976 Pi-leg follow-up (`a47d496e`: "the Pi leg of the fix has not run yet")
answering itself: the leg is not "re-bless from one device and done" — the
D24 answer is a different image from the D16 answer.

### What is NOT verified (do not over-read the 1-LSB as proven)

- The exact precision path producing lane 3's frame-wide 1-LSB offset
  (final color-store rounding vs fixed-function FP16 in the Mali-G52's tile
  pass) is not read back from the GPU; it is characterized by its measured
  distribution (1-LSB, uniform across brightness, no geometric structure), not
  by a named driver function. A ruling on it needs that mechanism pinned.
- Lane 2's per-pixel floor-limb split (the A/B/C census of
  `definitive_census.py`) is a measurement of *which pixels the bias resolved*
  per pose, not a proof of the D24 tie geometry on Lima: the stored-depth gap
  between the two layers was not read back from the FBO. The "gap > 2 D16
  cells" reading is an inference from the unlit share, consistent with — not a
  measurement of — the B976 README's per-row tie analysis on x86.

## Determinism

Each lane was run TWICE independently (lock → run → pull → unlock, then
again). `determinism.py` (the gate's `compare_images_exact`): **lane 2 and
lane 3 are 15/15 bit-identical between their two runs.** The measured
diff-vs-golden is a stable driver property, not thermal or timing noise. The
evidence dirs `lane2_runA/`, `lane2_run2/`, `lane3_runA/`, `lane3_run2/` hold
both runs.

## RULING REQUEST (orchestrator files it)

**Question for Will:** the B1033/B1037 question "does lane 2 match the Pi
goldens" is unanswerable against the *committed* Pi goldens, because they
predate B976 — but this measurement answers the deeper question the B976 Pi
leg has been waiting to answer, and it changes what the Pi leg is:

1. **B976's Pi leg splits by depth format, and that is now measured, not
   assumed.** The committed Pi goldens are a D16 image (B971: the Orange Pi
   runs a 16-bit depth buffer). Lane 3 (D16) confirms the fix at the 1-LSB
   level: 45–96% of the floor locus matches the blessed lit value (80–98%
   within 1–3 LSB), 1–19% is the still-dark limb. Lane 2's Rock64 negotiates
   a **D24** surface and the same bias no longer dominates the preserved
   rasterized gap: 58–100% of the locus stays at the pre-fix dark value. So a
   re-bless "from the Pi" (D16) will match lane 3's floor but NOT lane 2's —
   the D24 answer is a different image. **Will must decide the D24 floor:**
   enlarge the tie bias so D24 surfaces flip fully (a code change; x86 D16
   stays at 0 px only if the D16 locus is unchanged — B976's acceptance bar
   pins that), keep 2 cells and accept the partial-lit D24 limb (needs a
   D24-blessed golden set or a per-surface golden), or pin the lanes to D16
   surfaces (the production preset does not set depth at all — B971 blessed
   from a D16 Mesa surface, and each Mesa/driver pair negotiates its own — so
   this may already be settled and lane 2 simply measured a surface the
   preset never pins, in which case the D24 data is a note-for-the-board, not
   a gate question).

2. **Then the per-lane 1-LSB classes, once the floor ruling is made:**
   - *Lane 3 (Panfrost):* the residual is a frame-wide 1-LSB offset (present
     on every frame, including the four lane-2-identical ones; 28–57% of the
     frame's pixels at exactly 1 LSB, off-locus and on-locus alike, no
     geometric structure). Rule whether lane 3 gets
     its own per-lane goldens or a 1-LSB frame-wide bound counts as
     "driver-identical" for the gate.
   - *Lane 2 (Lima):* after the floor ruling, the off-floor residual is
     0–3 088 px per frame (timorous f2 is the outlier, mostly ≤1 LSB; the
     rest of the frames are ≤418 px). The only question left is the
     D24-vs-D16 floor gap quantified above, which the D24 ruling resolves by
     construction.

**Not asked of Will, flagged for the record:** lane 2's allocator-slack
breach (35–49% vs the 35% bound) is a glibc-arena property of the Rock64
(2 GiB, different glibc/arena config than the Pi); it does not affect the pixel
or owned-memory figures and is a separate, non-golden, board item if it
matters.

## Scripts (all in `/home/user/kb-evidence/kb-b1037-lane-goldens/scripts/`)

Four of these are also committed in this repo under `refactor/B1037/scripts/`
(bit-identical to their evidence copies): `framefrac.py`, `fulllocus.py`,
`offlocus.py` and `offlocus_mag.py`; the first and last are the ones the
tables above cite by name.

| script | role |
|---|---|
| `b1037_run.sh` | device-side: the gate's exact `run_benchmark` `--platform pi` line per zone + maps proof + thermal |
| `b1037_driver.sh` | host-side: lock, HDMI force-on, scp run, scp back, compare, unlock, leftover check |
| `compare_lane.py` | the gate's own `compare_images_exact` + memory asserts vs the committed Pi goldens |
| `cross_compare.py` / `lane_vs_x86.py` | lane vs Pi-vs-x86-blessed (the B976 separation) |
| `threeway.py` | per-pixel set membership: lane-diff pixels in/out of the fix locus, and which value they equal |
| `definitive_census.py` | the authoritative per-pixel A/B/C census over the fix locus (`definitive_census.txt`) |
| `offlocus_mag.py` | the off-locus diff magnitude (what each lane changes where the goldens agree) — lane 3's frame-wide 1-LSB proof |
| `framefrac.py` | per-frame diff fraction: % of the frame at exactly 1 LSB (the frame-wide offset, on- and off-locus) |
| `mag_hist.py` | magnitude histogram of the lane-vs-x86 residual (the 1-LSB proof) |
| `char_diff.py` / `band_stats.py` / `residual_char.py` | edge signature, per-band tint, brightness-bin shift |
| `floor_sample.py` | **coarse 2×8 px grid sample** over qeynos2 f0's floor — used only to eyeball limb values, NOT the per-pixel census |
| `determinism.py` | run-vs-run bit-identity |
| `md_table.py` / `mem_table.py` | the per-zone result tables above |
