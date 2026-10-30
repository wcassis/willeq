# B1196 (#515): the Rock 4D as a second gate-render host

**Verdict: NOT comparable at tip `8c2bfe7c2`** (the B1189 merge). The arm64
build has **no surfaceless render path** — the card's required path
("surfaceless native 1280x800") exits 1 on lane 3 — so the only render the
Rock 4D can do is the KMS path, and on that path 13 of the 84 measurable
b1130 host-pair cells (90 cells, 6 n-a) and 1 of the 27 tour cells fall
outside the gate's own B1178 band, concentrated in the pixel-tail columns
(`p90_land`, `p90_src`,
`landing_end_s`, `source_cov`). The Rock 4D is also 2.2-3.8x (tour) to 4.3-5.2x (b1130)
slower per row (throughput below). To make lane 3 a second gate
host, the arm64 device needs a surfaceless-EGL path (the capability is
present on the device's Mesa — `device_info_egl.txt` lists
`EGL_MESA_platform_surfaceless` + `EGL_KHR_surfaceless_context` — the client
code is what is missing); this card is diagnose-only and does not implement it.

The 17 rows (tour-7 + b1130 r0, r1, r2, r3, r4, r5, r6, r7, r9, r10) were
rendered and measured on both hosts with the same plans, seed, sim-hz, camera
and fps, and every measured column was diffed host-to-host against the B1178
band definitions. All tables: `refactor/B1196/tables/`.

## 1. The two hosts, as they ran

| | ThinkPad (the baseline host) | Rock 4D (lane 3) |
|---|---|---|
| GPU / driver | Quadro RTX 3000, `OpenGL ES 3.2 NVIDIA 580.178.04` | Mali-G52 r1 MC3, `OpenGL ES 3.1 Mesa 26.0.8-1ubuntu0.3` (Panfrost) |
| EGL | `EGL (surfaceless): Version 1.5, vendor=NVIDIA`, `Config depth=24 stencil=0` | `EGL: Version 1.5` (Mesa), `Config depth=16 stencil=0` |
| render path | surfaceless EGL, **native 1280x800** (harness: `surface 1280x800 for a 1280x800 frame (scale 1.000x1.000)`) | KMS on `/dev/dri/card0`, HDMI-A-1 connector **forced on** (no monitor), kernel mode list `1024x768 800x600 800x600 848x480 640x480`, preferred 1024x768: harness `surface 1024x768 for a 1280x800 frame (scale 0.800x0.960)` — a non-uniform 4:3→16:10 stretch, then an upscale to the written frame |
| particle pool | `[fx-harness] spellParticlePool 8192 (desktop)` | `[fx-harness] spellParticlePool 4096 (sbc)` |

Every one of the 17 rows on each host carries these lines in its
`measure/<tag>/run.log` (host-pair rows: `tables/egl.tsv`; the per-row values
dedupe to exactly the two rows above). Device facts (model, kernel 7.1.8-edge-rockchip64,
Armbian 26.8.2 resolute, CPU parts, Mesa 26.0.8, `EGL_MESA_platform_surfaceless`
in the eglinfo client extensions, plus the `diff -rq` byte-identity record of
the ci-arm64 publish): `refactor/B1196/evidence/device_info.txt` (committed;
the raw copy is `~/kb-evidence/kb-b1196-rock4d-render/device_info.txt`) and
`~/kb-evidence/kb-b1196-rock4d-render/device_info_egl.txt`.

The 4096-vs-8192 pool is a second host-dependent input (the preset profile
differs by host). No measured column is explained by pool clipping alone —
the rows' landing populations are far under 4096 (the `landing_cov` column is
in-band on 8/10 rows, sec. 4) — but it is a like-with-like gap and is named
here rather than hidden.

## 2. The arm64 build cannot do the card's surfaceless path (measured)

The card asks for "surfaceless native 1280x800" on lane 3. That path exists
only in the **x86** forked DRM device
(`docker/irrlicht-drm-x86/CIrrDeviceFB.cpp` — "DRM/GBM/EGL initialization
failed - falling back to surfaceless EGL"); the arm64 device
(`docker/irrlicht-drm/CIrrDeviceFB.cpp`, built by `scripts/build-arm64-noble.sh`
and published to `ci-arm64/<sha>/`) has no surfaceless fallback, and
`WILLEQ_DRM_DEVICE` is a strict override. Measured on the device (ci-arm64
publish of `8c2bfe7c2daa45360c57003a9e15971b77507bfc`, the binary's
`config/` and `data/` byte-identical to the worktree tree at that sha,
`diff -rq` clean — the record is in
`refactor/B1196/evidence/device_info.txt`):

```
$ WILLEQ_DRM_DEVICE=/dev/dri/no-such-card .../bin/model_viewer --spell-test ...
DRM: WILLEQ_DRM_DEVICE=/dev/dri/no-such-card: card does not open (explicit override, no fallback)
DRM/GBM/EGL initialization failed
GLES2 Driver: ? (?) [?]
GLES2: Color2D shader FAILED
GLES2: Shader initialization failed
[2026-10-29 19:25:59.251] [ERROR] [MAIN] Failed to create Irrlicht device
```

full log: `~/kb-evidence/kb-b1196-rock4d-render/logs/surfaceless_attempt.log`.
The log ends there — zero frames, because the shader init fails before the
first frame. The exit code is 1 by the code path, not by this log: the error
leaves the device null in the creation helper, whose `return false` at
`tools/model_viewer/model_viewer_startup.cpp:426` main turns into `return 1`
at `tools/model_viewer.cpp:70-71`.
So the 17 Rock 4D rows below ran on the only path the build has: KMS on
card0 with the connector forced on (`pi_hdmi_force_on`), 1024x768 surface,
16-bit depth. This is the same *class* of path the 2026-10-29 render-path
change rejected for the ThinkPad (`/mnt/scratch/gate-baseline/README.md`:
"3x supersampled numbers that were not like-with-like with the oracle's
native render") — a mismatched surface, resampled to the requested frame —
plus 16-bit depth instead of 24.

## 3. The baseline: `8c2bfe7c2-sl` did not exist, so this card rendered it

The card's baseline pointer is `/mnt/scratch/gate-baseline/<sha>-sl/`
("every card from now on judges its AFTER against a surfaceless baseline
(<sha>-sl)" — `/mnt/scratch/gate-baseline/README.md`). For this tip only
`/mnt/scratch/gate-baseline/8c2bfe7c2/` existed, and it **predates the
2026-10-29 12:45Z render-path change**: its b1130 rows are on the old i915
path (`measure/b1130_r0/run.log`: `DRM: Available mode [0]: 3840x2160@60Hz
(preferred)` ... `surface 3840x2160 for a 1280x800 frame (scale 3.000x2.700)`),
while its tour rows are already the NVIDIA-sideless path. Judging against it
would mix two paths in the "before". Per the card ("if it is not there yet,
render those 17 rows on the ThinkPad yourself at CONC=2 when its load is <
10"), this card rendered the 17 rows into
`/mnt/scratch/gate-baseline/8c2bfe7c2-sl/` with the **canonical runner**,
`/mnt/projects/fable/review/GATE-BASELINE/render_gate.sh` (current form:
b1130 rows surfaceless, `B1130_DRM=1` default), render on the ThinkPad,
measure on hermes-ext, CONC=2 (the runner's yield gate — wait while ThinkPad
load > 12 — never fired: 0 `waiting:` lines in the run log):

```
CONC=2 SLUG=kb-b1196-rock4d-render WT=/home/user/src/willeq-wt/kb-b1196-rock4d-render \
  BIN=/home/user/willeq-builds/kb-b1196-rock4d-render/build/bin/model_viewer \
  EV=/mnt/scratch/gate-baseline/8c2bfe7c2-sl \
  bash /mnt/projects/fable/review/GATE-BASELINE/render_gate.sh all \
       tour_93 tour_38 tour_54 tour_656 tour_41 tour_253 tour_458 \
       r0 r1 r2 r3 r4 r5 r6 r7 r9 r10
```

`BIN` is an `rbuild` build of this worktree (cut at `8c2bfe7c2` with no C++
changes) — the same tip the Rock 4D ran. Result: 17/17 measured, wall 1490 s
(`/mnt/scratch/gate-baseline/8c2bfe7c2-sl/logs/gate.all.log`, `logs/row_times.tsv`).

The Rock 4D side ran through `scripts/render_gate_rock4d.sh` (a lane-3 port
of the canonical runner; every difference from it is named in its header):
same plan generation, same `--fps 30 --sim-hz 60 --seed 1 --headless`,
same cameras, same measure invocations (B1139 `harness_measure.py` for the
tour rows, B1155 `harness_measure_b1130.py` for the b1130 rows), CONC=3
(matching the baseline runner's own default for a dedicated host; the device
was idle, load 1.4-2.8 per row, `measure/<tag>/time.txt`). Result: 17/17
measured, wall 1639 s (`~/kb-evidence/kb-b1196-rock4d-render/logs/gate.rock4d.log`).
The 17 plans on both sides are byte-identical (`diff -q` per tag between
`/mnt/scratch/kb-evidence/kb-b1196-rock4d-render/gate/plans/` and
`/mnt/scratch/gate-baseline/8c2bfe7c2-sl/plans/`).

## 4. The delta tables (every column, both hosts, against the B1178 bands)

Built by `scripts/build_tables_b1196.py` (exact command:
`python3 -I refactor/B1196/scripts/build_tables_b1196.py`; it reads only the
files named in its header — no retyped numbers). Band/verdict rules are
copied verbatim from `refactor/B1178/scripts/gate_after.py` (as carried in
`refactor/B1188/scripts/build_tables_b1188.py`): cov ±3 % of oracle, p90 ±15 %,
times ±0.10 s, other b1130 metrics ±0.15, tour roi_l/roi_s the absolute
kTour windows (`tests/spell_fx_population_oracle.h`); the b1130 oracle
reference is `refactor/B1153/tables/gate_c.tsv`. "in_band" is the **host-pair**
test: |rock4d − thinkpad| ≤ the column's band width at the oracle (for the
roi window columns: both hosts on the same side of the window).

`tables/counts.tsv` (host-pair in-band cells; plus each host's own verdicts
against the B1178 reference):

| table | column | rows | in-band | same-side | out | n-a | rock4d PASS/short/over/n-a |
|---|---|---|---|---|---|---|---|
| tour | land_cov | 7 | 7 | – | 0 | – | 4/3/0/0 |
| tour | end | 7 | 6 | – | 0 | 1 | 1/4/1/1 |
| tour | roi_l | 7 | 0 | 6 | 1 | 0 | 1/6/0/0 |
| tour | roi_s | 7 | 0 | 7 | 0 | 0 | 4/1/2/0 |
| b1130 | source_cov | 10 | 6 | – | 4 | – | 2/5/3/0 |
| b1130 | landing_cov | 10 | 8 | – | 0 | 2 | 0/7/1/2 |
| b1130 | landing_end_s | 10 | 4 | – | 4 | 2 | 1/6/1/2 |
| b1130 | p90_src | 10 | 8 | – | 2 | – | 1/4/5/0 |
| b1130 | p90_land | 10 | 5 | – | 3 | 2 | 4/0/4/2 |
| b1130 | source_onset_s | 10 | 10 | – | 0 | – | 9/1/0/0 |
| b1130 | source_centroid | 10 | 10 | – | 0 | – | 6/0/4/0 |
| b1130 | src_h_lo | 10 | 10 | – | 0 | – | 9/0/1/0 |
| b1130 | src_h_hi | 10 | 10 | – | 0 | – | 4/1/5/0 |

The two hosts agree with each other, within the gate's own tolerance, on the
population and timing columns: `landing_cov` (8/10 in-band, the 2 n-a rows
r4/r6 are `nan` on both hosts — the landing never exceeds the lit-pixel
threshold in either), `source_onset_s` (10/10), `source_centroid` (10/10),
`src_h_lo`/`src_h_hi` (10/10), tour `land_cov` (7/7) and tour `end` (6/6
measured). They disagree, outside the tolerance, in the pixel-tail columns
(`tables/delta_b1130.tsv`, `d_rock_minus_think` column):

- `landing_end_s` — 4 rows out, all rock4d **later** by +0.133 s (r9),
  +0.167 s (r1), +0.200 s (r2, r5); both hosts on the same side of the
  oracle on all four (short r9/r1/r2, over r5: oracle 4.53, thinkpad 5.4,
  rock4d 5.6 — thinkpad_v = rock4d_v on every row).
- `p90_land` — 3 rows out: r7 +0.020, r9 +0.006, and **r3 +1.665**
  (thinkpad 0.149 → rock4d **1.814**, a 12x outlier on one row — spell 377,
  `record_table` record 3; the 90th-percentile "sprite" in the landing ROI,
  i.e. a frame where the resampled landing reads as one bright blob spanning
  the window instead of discrete sprites).
- `p90_src` — 2 rows out: r6 +0.021, r9 +0.009.
- `source_cov` — 4 rows out (r0 −0.007, r2 −0.006, r3 −0.006, r9 −0.036);
  the band here is 3 % of a small number (width 0.002-0.026), so sub-0.01
  differences trip it; r9's −0.036 is the only one of material size.
- tour `roi_l` — 1 row out: spell 253, thinkpad 0.107 (PASS, exactly at the
  window edge 0.079-0.107) vs rock4d 0.117 (over) — the hosts straddle the
  window boundary (+0.010 apart).

Suspected mechanism (hypothesis, named per the finding rule, not asserted):
the KMS path renders a 1024x768 surface at 16-bit depth and resamples it
(0.800x0.960, non-uniform) to the 1280x800 frame. Thin bright particles are
exactly what resampling and a coarser depth buffer smear — the same failure
mode the old i915 4K path showed ("the old path under-measured thin bright
pixels", `/mnt/scratch/gate-baseline/README.md`) — and it would predict
deltas precisely in the tail columns (p90, end, small cov) with the
population columns (cov, onset, centroid) stable, which is what the tables
show. The r3 12x `p90_land` outlier is the strongest single symptom.

## 5. Throughput (measured, per row)

`tables/throughput.tsv` (rock4d CPU from the bash `time` user+sys in
`measure/<tag>/time.txt`; the baseline runner captures no per-row CPU, so the
thinkpad `cpu_s` column is empty by construction):

- **Rock 4D**: 2.39-3.88 fps (median ~3.6 on b1130, ~3.2 on tour);
  251-412 ms CPU per frame (251.1-411.9) — one A72 core per row, load 1.4-2.8
  at CONC=3 (the render is CPU-bound, as the issue says; the GPU idles).
  Full 17-row gate wall: 1639 s (~27 min) at CONC=3 on an idle device.
- **ThinkPad**: 7.05-12.85 fps on tour (7.05-7.41 on the five heavy rows;
  tour_41 12.85, tour_458 12.12), 13.39-19.72 fps on b1130; 50.7-141.9 ms wall
  per frame. Full 17-row gate wall: 1490 s (~25 min) at CONC=2, shared box.

So lane 3 is 2.2-3.8x (tour: 128.6-186 s per row vs 34-62 s) to 4.3-5.2x
(b1130: 136.2-154.7 s vs 27-36 s) slower per row in wall time. A 47-row gate
(tour-7 + b1130 40) would be ~75 min on lane 3 (1639 s x 47/17) vs ~69 min on
the ThinkPad (1490 s x 47/17 = 4134 s) at these rates (linear extrapolation
of the two measured 17-row walls).

## 6. Leaving the hosts as found

- Rock 4D: connector forced on only for the run; `pi_hdmi_restore` ran at the
  end of `refactor/B1196/scripts/render_gate_rock4d.sh` (the committed script)
  and the final on-device check shows the connector `disconnected` (its found
  state), `pgrep model_viewer` = 0, no `/tmp/kb-b1196-*` dirs (the transient
  smoke dir removed the same way, from a script file — the host's
  command-line scanner refuses `rm -r` in inline commands).
- ThinkPad: `pgrep -af model_viewer` clean after the baseline; the 17 rows'
  transient frames were deleted by the canonical runner; the build under
  `/home/user/willeq-builds/kb-b1196-rock4d-render/` is left in place (rbuild
  convention, warm ccache).

## 7. Files

| what | where |
|---|---|
| delta tables (per row, per column, both hosts + B1178 refs + verdicts) | `refactor/B1196/tables/delta_tour.tsv`, `delta_b1130.tsv` |
| per-column in-band counts + both hosts' own verdicts | `refactor/B1196/tables/counts.tsv` |
| per-row frames/s, ms/frame, ms CPU/frame, load | `refactor/B1196/tables/throughput.tsv` |
| EGL/driver/depth/surface per row per host | `refactor/B1196/tables/egl.tsv` |
| the one-paragraph verdict | `refactor/B1196/tables/verdict.txt` |
| the lane-3 runner (port of the canonical one; differences in its header) | `refactor/B1196/scripts/render_gate_rock4d.sh` |
| the table builder | `refactor/B1196/scripts/build_tables_b1196.py` |
| device facts + the `diff -rq` byte-identity record (committed) | `refactor/B1196/evidence/device_info.txt` |
| rock4d raw measure (17 × summary/b1130_rows + time.txt + run.log) | `~/kb-evidence/kb-b1196-rock4d-render/measure/` |
| rock4d run log, row times, surfaceless-failure log, KMS smoke log | `~/kb-evidence/kb-b1196-rock4d-render/logs/` (+ `device_info_egl.txt`; the raw `device_info.txt` copy, kept in sync with the committed one) |
| rock4d plans (byte-identical to the baseline's) | `/mnt/scratch/kb-evidence/kb-b1196-rock4d-render/gate/plans/` |
| the surfaceless ThinkPad baseline this card rendered (17 rows, CONC=2) | `/mnt/scratch/gate-baseline/8c2bfe7c2-sl/` |
