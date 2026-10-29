# B1195: are the r540 oracle lanes (Wine on Xvfb) comparable to lane 5 (Quadro) for spell-FX rows?

Diagnose leg (report only). #514. One scripted leg on lane 5 and one on lane 2, the same
observer rig, the B1132 instrument, the B1178 band rule applied lane-to-lane.

Short answer: **the r540 lanes are comparable for settled pixel-intensity rows (the `p90` class)
and for nothing else here.** Every TIMING row is off by a systematic +0.4 to +0.6 s content
delay in the Xvfb/Wine/llvmpipe display path, and the additive-glow coverage rows
(`source_cov`, `roi_source`) render 12–51 % short on the software-GL lane. Details and
numbers below.

## 1. Method — one leg, one plan, one instrument

- Leg: `plans/b1195_lane5_rig93_253.tsv` and `plans/b1195_lane2_rig93_253.tsv` — the B1134 P2
  rig adapted: Testcaster (Druid) casts spell 93 (Burst of Flame) twice on Oraclewatch
  (Warrior, the observer client), then spell 253 (Whirling Wind) twice on self. The observer's
  camera is pinned (F9 x3 selects the user camera; the camera and particle-clip-plane keys
  are identical in `lane5_eqclient_ini.txt` / `lane2_eqclient_ini.txt` — the only ini
  differences between the lanes are the profile-name lines and a `ScreenshotNumber` key).
- Instrument: `scripts/obs_measure.py` wrapping the committed B1132 rig —
  `fx_timing.py` (stage onset/end/peak/coverage from the strict lit-pixel series) and
  `anchor.py` (per-anchor ROI coverage), over the observer video; timing anchors
  (OP_BeginCast/OP_CastSpell/OP_Action) from the leg's pcap via `scripts/probe_casts.py`.
- Band rule: `refactor/B1178/scripts/gate_after.py band()` applied lane-to-lane by
  `scripts/lane_table.py` — `±3 %` relative on the coverage/ROI rows, `±15 %` relative on the
  p90 rows, `±0.10 s` on source onset and landing end, `±0.15 s` absolute on the rest.
- Capture: 1280x800 h264, both clients, nominal 30 fps (all four videos,
  `ffprobe_video.tsv`). Geometry per lane read from that lane's own settle still
  (`lane5_geom.json`, `lane2_geom.json`; the two agree within a few px — same client,
  camera and pose).

Leg log (leases, all via the lane scripts; the card slug is the lease owner):

| lane | leg | start (UTC) | rc | lease after | evidence |
|---|---|---|---|---|---|
| 5 | lane5e (valid) | 2026-10-29T18:15:19Z | 0 | `state=FREE` (18:30:04Z) | `progress.log` |
| 2 | lane2c (valid) | 2026-10-29T18:25:04Z | 0 | `state=FREE` (18:42:13Z) | `progress.log` |

Earlier legs and why they are not used (all in `progress.log`; failure stills in the
evidence dirs): lane5b (16:20→16:35Z, rc=0) — the observer camera was never selected
(missing F9 x3; the fix commit is in this branch); lane5c (16:42→17:04Z, rc=0) — the
level-1 PK caster died to a city guard at 16:57:27Z before any cast (the in-client
`#level 60` had no effect; `probe_window` on its pcap shows the death and zero cast
packets); lane5d (17:49→18:08Z, no rc line — the driver was stopped by hand after the
same PK death — on lane 2 the killer is named in its stills/pcap: "Fippy Darkpaw"
(city cat), 539 damage, one hit) and lane2a (17:49→18:08Z,
rc=66) — the same death on lane 2 at 17:59:30Z; lane2b (18:15→18:19Z, rc=64) — the plan
validator rejects `#set level` (it only recognizes `#level`, which the client does not
execute). The level fix landed as DB-level pre/post SQL —
`plans/guard_assist_off.sql` (`level=60` before the leg, guard-faction-assist off) and
`plans/guard_assist_on.sql` (restored after) — plus clearing the lane-2 VM's stale
Wine/Xvfb strays (`probe/vm_cleanup.sh`; lane2a's rc=66 half was a stale-Wine EULA
death).

Cast verification (pcap, `probe_casts.py` → `cast_probe_lane5e.txt`, `cast_probe_lane2c.txt`):
four OP_CastSpell and the matching OP_Action landings on each lane — 93 twice on the
observer, 253 twice on self; no death packets in either leg's window.

## 2. Environment (the only variables between the lanes)

| | lane 5 | lane 2 | evidence |
|---|---|---|---|
| display | Xorg | Xvfb :98 (caster) / :99 (observer, x11grab) | `lane*_eqgame_glmaps.txt` |
| GL driver | `libGLX_nvidia.so.580.178.04` + `libnvidia-glcore` (Quadro) | `libGLX_mesa.so` + `libgallium-25.2.8-0ubuntu0.24.04.4` + `libLLVM.so.20.1` (llvmpipe) via `wine/i386-unix/opengl32.so` | same |
| video | 30/1, 17762 frames / 592.07 s | 30/1, 20802 frames / 693.40 s | `ffprobe_video.tsv` |
| eqclient.ini | identical except profile-name lines + a `ScreenshotNumber` key (camera and particle clip planes match) | | diff of `lane*_eqclient_ini.txt` |

## 3. The table (lane 5 reference, lane 2 judged against it)

Full data: `lane_table.tsv` (4 cast rows x 13 metrics, built by
`scripts/lane_table.py measure_lane5 lane5 measure_lane2 lane2 lane_table.tsv` over the
`measure_lane5/` / `measure_lane2/` dirs). `in`/`short`/`over` = the B1178 band verdict.
Note: the 253 LANDING timing rows use the `anchor.py` "active run" line on both lanes,
because the `fx_timing.py` main LANDING line is lane-asymmetric (finding F3).

### 3.1 TIMING rows — the onset/peak/end rows do not agree on lane 2

| row | 93 r1 l5 / l2 (Δ) | 93 r2 l5 / l2 (Δ) | 253 r1 l5 / l2 (Δ) | 253 r2 l5 / l2 (Δ) | verdict (l2 vs l5) |
|---|---|---|---|---|---|
| source_onset_s (BC+) | 0.396 / 0.828 (+0.432) | 0.381 / 0.768 (+0.387) | 0.395 / 0.826 (+0.431) | 0.389 / 0.862 (+0.473) | OVER x4 (band ±0.10) |
| source_peak_s | 1.363 / 1.494 | 1.315 / 1.501 (OVER) | 1.495 / 1.493 | 1.489 / 1.495 | in x3, over x1 (±0.15) |
| source_end_s | 1.496 / 1.494 | 1.481 / 1.501 | 1.495 / 1.493 | 1.489 / 1.495 | in x4 — but see F1 (window clamp) |
| landing_onset_s (Act+) | 0.632 / 1.103 (+0.471) | 0.513 / 1.087 (+0.574) | 0.008 / 2.806 | 0.012 / 0.000 | over x3, in x1 (±0.10) |
| landing_peak_s | 0.965 / 1.870 (+0.905) | 0.980 / 1.620 (+0.640) | 1.042 / 3.039 | 0.578 / 5.067 | OVER x4 (±0.15) |
| landing_end_s | 4.765 / 5.203 (+0.438) | 4.713 / 5.020 (+0.307) | 5.975 / 7.706 | 2.745 / 7.300 | OVER x4 (±0.10) |
| landing_dur_s | 4.13 / 4.10 (in) | 4.20 / 3.93 (SHORT, −0.27) | 5.97 / 4.90 (SHORT) | 2.73 / 7.30 (OVER) | 1 in, 3 out (±0.15) |

The systematic shift is a **display content delay of ~+0.45 s on lane 2**, not a clock
offset (the video clocks are calibrated per leg and both videos are nominal 30 fps,
`ffprobe_video.tsv`), and not a uniform pipeline lag (below, F1).

### 3.2 PIXEL rows — the p90 class agrees; the glow coverage does not

| row | 93 r1 l5 / l2 (Δ) | 93 r2 l5 / l2 (Δ) | 253 r1 l5 / l2 (Δ) | 253 r2 l5 / l2 (Δ) | verdict (l2 vs l5) |
|---|---|---|---|---|---|
| source_cov (model^2) | 0.400 / 0.382 (SHORT) | 0.384 / 0.344 (SHORT) | 3.242 / 2.759 (SHORT) | 3.662 / 3.114 (SHORT) | SHORT x4 (±3 %) |
| p90_src | 0.398 / 0.391 (in) | 0.388 / 0.350 (in) | 3.270 / 2.328 (SHORT) | 3.578 / 2.459 (SHORT) | in x2, short x2 (±15 %) |
| roi_source | 0.162 / 0.115 (SHORT) | 0.159 / 0.139 (SHORT) | 1.062 / 0.609 (SHORT) | 0.889 / 0.433 (SHORT) | SHORT x4 (±3 %) |
| landing_cov | 1.189 / 1.306 (OVER) | 1.199 / 1.210 (in) | 1.782 / 4.309 (run lines, F3) | 1.451 / 1.359 (SHORT) | mixed, none stable (±3 %) |
| p90_land | 0.898 / 0.856 (in) | 0.923 / 0.867 (in) | 4.728 / 4.111 (in) | 3.739 / 4.002 (in) | **in x4 (±15 %)** |
| roi_landing | 0.211 / 0.174 (SHORT) | 0.315 / 0.396 (OVER) | 1.225 / 0.964 (SHORT) | 0.745 / 0.899 (OVER) | out of band x4 (±3 %) |

## 4. Findings

**F1 — the ~+0.45 s delay is in the displayed content, not the clock or the game.**
The raw lit-pixel series around the 93 r1 cast (strict rise>80 count, same counting on
both lanes, `ramp_93_1.txt`): lane 5's cast glow steps up from ~11,200 to 12,525 lit
pixels at BC+0.66 s and reaches 18,986 by BC+0.70 s, at its full ~22,000 by BC+1.36 s
(the instrument's source peak);
lane 2's series stays flat at ~10,000 until BC+0.96 s, steps up 12,359 → 17,593 →
20,692 at BC+1.13/+1.30/+1.46 s, is still climbing at Act (BC+1.498 s), and is at
22,213 lit at BC+2.00 s — half a second after the cast ended — where lane 5 has already
decayed to baseline (8,896 at BC+1.86 s). The game events are identical in both pcaps
(`cast_probe_lane5e.txt`, `cast_probe_lane2c.txt`). So the Xvfb/Wine/llvmpipe path
delivers each game state to the captured screen ~0.4–0.5 s late: the r540's CPU-bound
rasterization (llvmpipe + Wine under Xvfb; the GPU idles, per the 2026-10-29 Prometheus
feedback in the card) holds a frame queue behind the game clock. The delay is jittery,
not a fixed constant: +0.387…+0.473 s on source onsets, +0.307…+0.905 s on landing
onset/peak across the four casts (`lane_table.tsv`) — the same order as the B1178 timing
bands (±0.10/±0.15 s), which is why a one-shot calibration cannot rescue the timing rows.
(Corollary: the `in` verdicts on `source_end_s` are a window-clamp artifact — the SOURCE
stage is only defined over [BC, Act] in `fx_timing.py`, so the "end" sits at the window
edge on both lanes and measures nothing.)

**F2 — the settled pixel-intensity rows agree; the additive-glow rows do not.**
`p90_land` (90th-percentile lit level of the landing window, /model^2) is within the
±15 % band on all four casts — the post-cast volumes reach the same lit level on both
lanes once they settle. But the SOURCE-stage rows are systematically short on lane 2:
`source_cov` −4.5…−15.0 % and `roi_source` −12.6…−51.3 % across the casts
(`lane_table.tsv`). The casting-glow/additive-sprite rendering carries less lit mass
inside the anchored window under llvmpipe — the raw ramp (F1) shows the same glow
rising in coarser, later steps and still near peak 0.5 s after Act, so the software-GL
path changes the glow's per-frame lit-pixel mass, not only its timing. What inside the
rasterizer does that (blend precision vs particle-count behaviour) is not pinned down
here — the two lanes are identical in everything else (§2: same resolution, capture,
client config), so the changed variable is the rasterizer itself. The landing
coverage/ROI rows (`landing_cov`, `roi_landing`) scatter −18…+26 % around the reference
— outside the ±3 % coverage band — so only the p90 class is usable.

**F3 — the 253 LANDING main row is a lane-dependent gate artifact.**
`fx_timing.py`'s main LANDING line requires the lit series to exceed
`max(thr, 1.5 × source_tail)` (the source stage's own tail), so the gate level differs
per lane: lane 5's 253 post-cast volume (already above the gate at window start) reports
"none above thr", while lane 2's same volume reports onset at Act+2.806 s. The
`anchor.py` "LANDING active run" summary — the same window, the same counting, on both
lanes — is the like-for-like row (used in §3.1/§3.2); even on it, the 253 post-cast shape
is not comparable (lane 2's volume runs to Act+7.30…7.71 s vs lane 5's Act+2.75…5.98 s,
peak at Act+3.04…5.07 s vs lane 5's Act+0.58…1.04 s — the F1 delay stretched over a
6–8 s effect).

## 5. Recommendation (no implementation)

1. **Rows that may run on the r540 lanes (2 and 3): the settled pixel-intensity rows —
   the `p90` class only** (e.g. landing p90, and source p90 for fast-settling effects
   like 93). Rig settings: exactly this leg's — same observer rig and pinned camera,
   1280x800 h264 30 fps capture on the observer, geometry read from the leg's own settle
   still, and measure the p90 over the settled window (≥ Act+3 s for a 4–8 s landing
   volume; the four `p90_land` rows above are all in band at ±15 %).
2. **Rows that may NOT run on the r540 lanes:** every TIMING row (onset/peak/end —
   systematic +0.4…+0.9 s, jitter ±0.15 s, against ±0.10/±0.15 s bands), the
   additive-glow coverage rows (`source_cov` −5…−15 %, `roi_source` −13…−51 %), and the
   landing coverage/ROI rows (10–26 % scatter vs the ±3 % band).
3. **What would make the rest comparable** (none demonstrated here, so no numbers):
   (a) **GPU passthrough** to the r540 VM (real rasterizer instead of llvmpipe) would
   attack both the delay and the glow dimming; it is unmeasured — it needs its own
   diagnose leg before any timing row is allowed back; (b) **per-leg latency
   calibration** is not sufficient: the measured delay jitter (0.307–0.905 s across the
   four casts) is the same order as the bands, so even a perfectly calibrated constant
   would leave the landing-peak rows (Δ up to +0.905 s) out of band; (c) **capturing
   frames inside the client** (a frame hook instead of x11grab on the Xvfb root) would
   remove the display lag but changes what the oracle lane captures (no longer the
   displayed output) — a ruling, not a measurement, is needed first.
4. **Process:** keep lane 5 as the reference lane for the gate's timing/glow rows; use
   the r540 lanes to relieve lane 5 of the p90-class pixel work. Any future card that
   moves a row class to the r540 lanes must re-run this same leg pair for that class
   (like-with-like, measured — the bands above were built from this one leg pair, 2 reps
   per spell).

## 6. Evidence index

Evidence root: `~/kb-evidence/kb-b1195-oracle-lanes/` (small files; the videos and
per-frame series live under `/mnt/scratch/kb-evidence/kb-b1195-oracle-lanes/`).

| file | what it is |
|---|---|
| `progress.log` | leg/lease log (in this dir, repo path `refactor/B1195/progress.log`) |
| `lane_table.tsv` | the §3 table (4 casts x 13 metrics + band verdicts) |
| `measure_lane5/`, `measure_lane2/` | per-cast `fx_timing`/`anchor` logs + `lit80.tsv` series (8 casts) |
| `ramp_93_1.txt` | the F1 raw lit ramp, lane 5 vs lane 2, 93 r1 |
| `cast_probe_lane5e.txt`, `cast_probe_lane2c.txt` | OP_CastSpell/OP_Action verification from the pcaps |
| `lane5_geom.json`, `lane2_geom.json` | per-lane crop/anchors/excludes + the measurement notes |
| `lane5_eqgame_glmaps.txt`, `lane2_eqgame_glmaps.txt` | loaded GL libraries (the §2 drivers) |
| `lane5_eqclient_ini.txt`, `lane2_eqclient_ini.txt` | client config diff |
| `ffprobe_video.tsv` | video fps/frame-count census |
| `/mnt/scratch/.../lane5e/`, `/mnt/scratch/.../lane2c/` | full leg dirs: `capture.pcap`, `video*.mp4`, step stills, per-step opcode dumps |

Commands that produced the tables (run from the worktree; evidence dirs as above):

```
python3 refactor/B1195/scripts/probe_casts.py /mnt/scratch/kb-evidence/kb-b1195-oracle-lanes/lane5e/capture.pcap
python3 refactor/B1195/scripts/probe_casts.py /mnt/scratch/kb-evidence/kb-b1195-oracle-lanes/lane2c/capture.pcap
python3 refactor/B1195/scripts/obs_measure.py /mnt/scratch/kb-evidence/kb-b1195-oracle-lanes/lane5e \
    ~/kb-evidence/kb-b1195-oracle-lanes/measure_lane5 ~/kb-evidence/kb-b1195-oracle-lanes/lane5_geom.json \
    93 253 --observer Oraclewatch --target 93:observer,253:caster
python3 refactor/B1195/scripts/obs_measure.py /mnt/scratch/kb-evidence/kb-b1195-oracle-lanes/lane2c \
    ~/kb-evidence/kb-b1195-oracle-lanes/measure_lane2 ~/kb-evidence/kb-b1195-oracle-lanes/lane2_geom.json \
    93 253 --observer Oraclewatch --target 93:observer,253:caster
python3 refactor/B1195/scripts/lane_table.py ~/kb-evidence/kb-b1195-oracle-lanes/measure_lane5 lane5 \
    ~/kb-evidence/kb-b1195-oracle-lanes/measure_lane2 lane2 ~/kb-evidence/kb-b1195-oracle-lanes/lane_table.tsv
```
