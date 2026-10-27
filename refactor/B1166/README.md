# B1166 (#485): the b1130 target, solved standing at the oracle's measured feet

Branch `task/kb-b1166-target-fit`, cut at fable 4895adc2. The specification is
`glab issue view 485` (asks 1-4, acceptance A-F), with two orchestrator corrections
(B1164 reviews, MR !315 notes 7744 and 7754): the anchor is NOT the issue's 652 / 325,
it is the oracle target as measured here (sec. 1). Evidence: `/mnt/projects/fable/review/B1166/worker/`.
The "before" binary is fable 4895adc2 built in a throwaway worktree (`wt/kb-b1166-before`);
both binaries were built and every frame rendered on the ThinkPad; every measurement ran on
dev-u24.

## 0. Result

- **The defect.** `fitFixedNpcTarget` solved the target's world (x, z) on the human's REST
  pose: the probe run of the before binary prints "target (x -6.702, 0, z 9.732) -> rendered
  target feet row 652 at column 940, 50 px"
  (`/mnt/projects/fable/review/B1166/worker/probe/before.log:109`), while the running frames,
  which skin the idle, drew the standing human 316 px tall with its feet at row 782 (B1162's
  `review/B1162/worker/harness/fear229_b1130/measure_anchor.log:1`). Every fixed_npc landing
  was measured against a body that was not there.
- **The fix** (`tools/model_viewer/model_viewer_fx_harness.cpp:246-377`): the solve forces the
  animated pose before it measures (line 267, the same rule the caster fit applies at line
  114), stands the target's pose box on the ground plane the frames draw (lines 268-288), and
  fits the two feet pixels to the ONE anchor `kB1130TargetAnchor`
  (`tools/model_viewer/fx_target_anchor.h:37`: feet column 940, feet row 615, 288 px), which the
  preset reads (`tools/model_viewer/model_viewer_fx_harness.cpp:899-901`) and the measure script reads too
  (`refactor/B1155/scripts/harness_measure_b1130.py:44-48`).
- **The self-check** (`tools/model_viewer/model_viewer_fx_harness.cpp:797-819`, called on the written frame 0 at
  line 1154, exit 5 on FAIL): feet row +-1 px and column centroid +-3 px of the anchor; height
  +-3 px of the standing height the solve rendered, and within 25 % of the oracle's 288 px
  (tolerances `tools/model_viewer/fx_target_anchor.h:51-54`). After the fix:
  "rows 378..615 (238 px, feet row 615), column 940.2 ... ratio 0.826 (tol 0.25): PASS"
  (`/mnt/projects/fable/review/B1166/worker/probe/after.log:128`). Unit tests offline
  (`tests/test_fx_harness.cpp:545-605`) plus the harness ctest
  (`tests/test_fx_harness.cpp:397`) that also requires the run log's PASS line.
- **The height is not solvable** (sec. 2): two feet pixels fix (x, z); the FABLE human male at
  the oracle's spot renders 238 px, 0.826x the oracle's 288. That is a model-height gap,
  not a fit error, and no preset value can close it without inventing one (the size is the
  oracle's own 6.0, sec. 1). No RULING REQUEST is needed for the anchor; see FOLLOW-UP 2.
- **b1130 sweep** (sec. 3): 12/12 fixed_self rows byte-identical before/after; 28/28
  fixed_npc rows moved; before == B1157's committed table in every cell (0 of 160 cells
  differ). Landing PASS stays 0/40: the landing misses now have a measured first owner, the
  sweep plans' caster level (FOLLOW-UP 1).
- **tour-7 on b1118** (sec. 4): all seven summary rows identical before/after; 6/7 spells
  byte-identical frames, and tour_93's re-render with each binary is byte-identical to the
  other (the first before run differed by single pixels on 71 frames, a run-to-run artefact of
  that run, not of the change). The b1118 camera did not move.
- **Named rows** (sec. 5): 4499 unchanged (frames byte-identical, verdict unchanged); Fear 229's
  anatomy is now comparable and its verdict changes from n/a to a measured below-the-feet
  shortfall; B1164's willeq-b1130 93 row changes from 0.995x to 0.55x of the oracle's lit area
  in the oracle's own pixel window.

## 1. The anchor: the oracle r51 target, measured

Method (`refactor/B1166/scripts/oracle_target_box.py`): the r51 settle still
`review/B1130/oracle/r51/26_tgt_npc_settle.png` minus a target-free still at r51's camera
(`review/B1164/worker/dev3/oracle/ab_b_away_slain_25fps/16_empty_r51cam.png`, B1164's leg
(b)); a pixel is target when its max-channel difference exceeds THR; the client's green
selection ring is removed by colour (green exceeds red and blue by > 30 in the target still);
3x3 close; the connected component through the torso seed (940, 450) -- which drops the name
label, a separate component; a row is a figure row when it holds >= 3 component pixels
(B1164's minpx); feet = the last figure row above the ring.

Sensitivity (`refactor/B1166/tables/r51_threshold_sweep.txt`, lines 3-38):

| THR | rows | height | columns | centroid |
|---|---|---|---|---|
| 40 | 329..593 | 265 px | 910..970 | 940.3 |
| 30 | 329..596 | 268 px | 907..983 | 939.9 |
| 25 | 328..601 | 274 px | 900..983 | 939.9 |
| **20** | **328..615** | **288 px** | 899..984 | 938.9 |
| 15 | 328..615 | 288 px | 721..1034 (leaks into the ground) | 933.2 |
| 10 | 328..615 | 288 px | 717..1034 (leaks) | 929.8 |
| 40, video f7505 | 330..594 | 265 px | 892..970 | 938.4 |
| 20, video f7505 | 328..617 | 290 px | 825..987 (leaks) | 928.2 |

Why 20: the right boot is dark brown on dark ground. At column 944 its difference against
the target-free still is 27 at row 598 and 18-22 at rows 603-612
(`/mnt/projects/fable/review/B1166/worker/oracle_target/r51_boot_profile_cols936_954.txt:11-28`),
so any threshold above ~20 ends the figure at 593-601 (B1164's first-pass 265 and its
"at least 275" are this cut). In the target still itself the boot's colour runs to row 612
(RGB 75,70,16), darkens through 613-615, and the ring starts at 616 (RGB 57,108,33) (same
file, lines 25-29). 20 is the lowest threshold whose component stays on the figure (15 and
below leak into the ground, columns 721..1034). Head top 328 / 329 moves by one row with the
threshold; the feet do not move between 20 and 10. The video frame (lossy) agrees within
2 rows. **Anchor: feet row 615, column 940 (the centroid reads 938.9-940.3 at thresholds
20-40; B1130's 940 kept), 288 px.** It is the same in two other fixed_npc captures:
e13 and e3 both read rows 328..615, 288 px, centroid 938.2 / 940.3
(`/mnt/projects/fable/review/B1166/worker/oracle_target/other_fixed_npc_captures_thr20.txt:3`, `:7`).

B1130's 325 px / feet (940,652) (`review/B1130/geom/r51.json:8`, `:9`) started at the
name label's top row; B1164's first pass (265 / 593) used threshold 40; B1164's second pass
(286, rows 329..614) agrees with this one within a row at each end.

The target model: r51's capture spawns `TrollF000` race 1, gender 0, size 6.000
(`/mnt/projects/fable/review/B1166/worker/oracle_target/r51_spawns.txt:187`, decoded by
`refactor/B1166/scripts/r51_spawn_sizes.py` from `review/B1130/oracle/r51/capture.pcap`),
which is what the preset renders (race 1, gender 0, size 6).

## 2. Why the height cannot be fitted (ask 1's "prove it")

The solve has two unknowns, the target's world x and z on the ground plane, and two pixel
facts, the feet column and the feet row. It converges to both (`/mnt/projects/fable/review/B1166/worker/probe/after.log:125`:
"target (x -8.888, y 0.226, z 0.924) -> rendered target feet row 615 at column 940, 238
px"). The height is then the model's own: 238 px, 0.826x the oracle's 288. The only lever
left would be the target's size, and the oracle's own spawn says 6.0 (sec. 1), so a scale
here would be an invented value. The pose boxes say the same: the human's is 6.706 units
tall (-3.119..3.587) and the dark elf caster's 6.092 (`/mnt/projects/fable/review/B1166/worker/probe/after.log:113`, and the caster
fit line in the same log), a ratio of 1.10, while the oracle draws them 288 / 209 = 1.38 at
nearly the same depth (feet rows 615 vs 598). B1164 saw the same gap on the b1118 camera:
the oracle's human 284 px (a lower bound, clipped at the chat window) against willeq's 241
(`review/B1164/worker/dev3/hostpx.txt:2`, `:13`). Owner: the race-model heights
(FOLLOW-UP 2), not this harness.

The ground stand: the ground plane sits at the caster's pose-box bottom (-2.893); the
human's pose box at y 0 starts at -3.119, so without the stand the plane hid the lowest
0.226 units of its legs in every written frame while the fit, drawn before the plane
exists, counted them. The stand applies the plane's own rule (pose box on the ground) to
the target (`/mnt/projects/fable/review/B1166/worker/probe/after.log:113`).

Which comparison is like with like: the target now stands where the oracle's stood on
screen, so its world point and depth match the oracle's and every crop-based column
(coverage, end, onset, in caster heights) compares the same scene geometry. The measure
uses the same anchor (940,615 / 288 px) for its target ROI and anatomy on both sides; a
target-height normalisation by the harness's own 238 px is reported beside it where it
matters (sec. 5).

## 3. The b1130 40-record sweep (ask 3)

`refactor/B1166/scripts/run_sweep_b1130.sh before|after` (B1157's plans, fps 30, sim 60 Hz,
seed 1; frames in `/mnt/projects/fable/review/B1166/worker/b1130_{before,after}/frames/`),
measured with each tree's own `harness_measure_b1130.py`, banded by B1155's
`band_table.py`, joined by `refactor/B1166/scripts/compare_b1130.py` into
`refactor/B1166/tables/b1130_table.tsv` (oracle, B1157, before, after, delta and both
verdicts for landing_cov, landing_end_s, p90_land and source_cov; `frames_identical` is a
sha256 comparison of every frame). Both sweeps exited 0 (`b1130_*/sweep.log`).

- **before == B1157**: 0 of the 160 cells (40 records x 4 axes) differ, so fable 4895adc2
  reproduces B1157's table exactly and every delta below is this card's.
- **fixed_self (12 rows)**: frames byte-identical, every cell unchanged (the b1130-self
  preset draws no target).
- **fixed_npc (28 rows)**: every frame differs (the target moved); landing_cov moved on every
  row with a landing. Verdict changes: r16 and r23 get a landing run where the before run had
  none (n-a -> short; r16 0.104, r23 0.274); r33 landing_end short -> PASS (+5.117 -> +5.183, oracle
  +5.260); p90_land r13 over -> PASS, r1/r2/r29 PASS -> short, r23 n-a -> over (both the
  landing and the p90 ROI moved: the ROI is built from the anchor, 652/325 -> 615/288). landing_cov PASS stays 0 before / 0 after.
- **The one Source move**: r7 (spell 40) source_cov 0.231 -> 0.210. The frames are
  identical left of column 800 through frame 91 and differ from frame 92 (BC+1.57)
  (`/mnt/projects/fable/review/B1166/worker/b1130_r7_first_left_diff.txt:1`); a re-render
  of the before binary is byte-identical to the first (`/mnt/projects/fable/review/B1166/worker/b1130_r7_rerun_identity.txt:1`),
  so it is deterministic and caused by the target's placement. Not traced (FOLLOW-UP 3).
- **Why the landing rows are still out of band (owner)**: the sweep plans pass
  `caster_level` = the record table's class level (r0: 1; `/mnt/projects/fable/review/B1166/worker/b1130_after/plans/r0.plan:2`),
  while all 46 B1130 captures were taken after `#level 60` (r51's step 09:
  `review/B1130/oracle/r51/steps.tsv`; the client's own constant says the same,
  `include/client/graphics/spell_visual_fx.h:126-131`). Spell 93 rendered at level 60 with
  the fix (`/mnt/projects/fable/review/B1166/worker/named_measure/t93_b1130_after_band/b1130_rows.tsv:2`)
  reads landing_cov 2.243 and end +6.667 against the oracle's 3.945 and +6.670; at level 1
  (r0) it reads 0.537 and +4.700. First owner: the sweep layout's caster level (B1155/B1157,
  FOLLOW-UP 1). The residual 0.57x of coverage at level 60 is B1164's (#483) pixel-area gap.

## 4. tour-7 on b1118 (ask 3: "b1118 must not move")

`refactor/B1166/scripts/run_tour7_b1118.sh before|after` (B1157's tour rows), measured by
B1139's `harness_measure.py`; `refactor/B1166/scripts/compare_tour7.py` ->
`refactor/B1166/tables/tour7_b1118.tsv`. Every summary row is identical before/after and
equals B1157's committed tour columns land_cov_n / end_n (93/38 1.993 / +6.633, 54/656
1.204 / +6.333, 41 2.478 / +5.667, 253 5.306, 458 0.500 / +5.050; `refactor/B1166/tables/tour7_b1118.tsv:2-8`).
Frames: 38, 54, 656, 41, 253 and 458 byte-identical. tour_93 (`refactor/B1166/tables/tour7_b1118.tsv:2`): 71
frames differ by one pixel each on the caster (e.g. frame 1: one pixel at (640,402)); a
re-render with each binary gives before == after byte for byte, and the first before run is
the odd one (`/mnt/projects/fable/review/B1166/worker/tour7_93_rerun_identity.txt:1-4`). The
b1118 preset never calls the changed code (the stand, the forced pose and the self-check
are under the b1130 preset only, `tools/model_viewer/model_viewer_fx_harness.cpp:897-904`, `tools/model_viewer/model_viewer_fx_harness.cpp:1154`).

## 5. The named rows re-measured (ask 4)

Renders: `refactor/B1166/scripts/run_named_rows.sh before|after` with each card's own plan
and length. Frame identity: `refactor/B1166/tables/named_frame_identity.txt`.

**B1162 4499 (`--camera b1130-self`)**: 480/480 frames byte-identical before/after and
identical to B1162's evidence frames (`refactor/B1166/tables/named_frame_identity.txt:1`, `:4`). Verdict unchanged
(no landing, as B1162 measured).

**B1162 Fear 229 (`--camera b1130`)**: `refactor/B1166/scripts/measure_anatomy.py`
(fx_measure.py + landing_onset_abs.py + anchor.py LANDING, unchanged) ->
`refactor/B1166/tables/fear229_anatomy.tsv`. The before run at B1130's 652/325 reproduces
B1162's preset row (run cov 0.178, cen_h 0.87, h 0.25..1.60, ground 0.00; line 5). The
oracle reference B1162 used, B1130 row 13 (e13, spell 209, same camera), reproduces B1130's
own row at 652/325 (0.706 / 0.77 / 0.23..1.63 / 0.00; line 2) and moves at the measured
anchor. Like with like = the same anchor (940,615 / 288), the same crop extended to row 800
(150,60,990,740) and the same excludes on both sides. Every exclude is FULL-FRAME x,y,w,h,
anchor.py's own convention (`/mnt/projects/fable/review/B1130/scripts/anchor.py:30` subtracts the crop
origin): the two name labels B1130 excluded for e13 (540,315,200,45 / 880,265,160,45, `review/B1130/geom/e13.json:12-13`)
plus the two lower-screen UI boxes the first run intended (written crop-relative there as
0,540,540,200 / 900,540,90,200, i.e. full-frame 150,600,540,200 / 1050,600,90,200), which the
extended crop reaches; the oracle form adds cast_measure.py's two UI boxes. Commands (run from
`/mnt/projects/fable/review`, `X="540,315,200,45 880,265,160,45 150,600,540,200 1050,600,90,200"`):

```
python3 -P <wt>/refactor/B1166/scripts/measure_anatomy.py oracle B1130/oracle/e13/video.mp4 1792693504.407 \
  1792693802.902 1792693804.934 1792693817.934 B1166/worker/named_measure/oracle_e13_a615_288_c740ff \
  150,60,990,740 940,615 288 $X
python3 -P <wt>/refactor/B1166/scripts/measure_anatomy.py harness B1166/worker/named_<before|after>/fear229_b1130/frames \
  B1166/worker/named_measure/fear229_<before|after>_a615_<288|238>_c740ff 150,60,990,740 940,615 <288|238> $X
```

(the oracle times are e13's, `B1166/worker/named_measure/oracle_e13_a615_288_c740ff/m/meta.json`.)

| run | anchor | run cov | cen_h | h p5..p95 | ground | line |
|---|---|---|---|---|---|---|
| oracle e13 (209) | 940,615 / 288 | 1.002 | 0.61 | -0.26..1.70 | 0.21 | 4 |
| oracle 229 observer rep 1 / 2 (B1162, another camera) | its own | 0.676 / 0.649 | 0.72 / 0.76 | -0.34..1.91 / -0.29..1.94 | 0.20 / 0.18 | -- |
| harness 229 before (target at 782) | 940,615 / 288 | 0.275 | 0.58 | -0.41..1.64 | 0.29 | 7 |
| harness 229 after | 940,615 / 288 | 0.209 | 0.70 | -0.04..1.64 | 0.14 | 10 |
| harness 229 after, own height | 940,615 / 238 | 0.283 | 0.87 | -0.05..2.00 | 0.12 | 11 |

(observer rows: `review/B1162/worker/measure/combat13_v3/b1162_combat13_v3_229_1_npc.txt:215`,
`review/B1162/worker/measure/combat13_v3/b1162_combat13_v3_229_2_npc.txt:215`.)

Correction (MR !320 note 7834, finding 1): the first version of this table passed those two
UI boxes crop-relative (0,540,540,200 / 900,540,90,200). Read full-frame, the second box
blanked x 900..990, y 540..740 -- the target's own lower legs and the ground under its feet --
on every row with crop 150,60,990,740 (the superseded `*_c740` dirs; their `measure.txt` line 7
shows the boxes). The script now asserts the convention: an exclude must overlap the crop and
must not touch the target's column between its head and 0.15 heights below its feet; the old
box fails it (`/mnt/projects/fable/review/B1166/worker/named_measure/exclude_assert_negative.txt:7`, exit 1).
The c540 rows (lines 3, 6, 9) end at row 600 and were not touched by either box.

**Verdict change**: B1162's "not compared until B1166 = n/a" becomes measured. cen_h 0.70 sits
between e13's 0.61 and the observer pair's 0.72 / 0.76 (below the pair, not inside it); the
landing reaches less far below the feet than all three references (h_p5 -0.04 vs -0.26 /
-0.34 / -0.29; ground 0.14 vs 0.21 / 0.20 / 0.18), and the shortfall holds when normalised by
the harness's own 238 px (h_p5 -0.05, ground 0.12). The margins are smaller than the first
(mis-excluded) version stated. Owner: unattributed between the Target-stage spawn volume
below the feet (B1150's landing shapes) and the caster level (sec. 3) -- the B1162 plan
passes level 60, so the level is not it here; FOLLOW-UP 4.

**B1164 willeq-b1130 93**: B1164's window instrument (`b1164_regions.py`, copied unchanged to
`/mnt/projects/fable/review/B1166/worker/scripts/`, md5 = the B1164 branch's), window
-1.5,0.6,0.16,1.8, B1164's excludes:

| run | anchor | peak window (target heights^2) | lit px | file |
|---|---|---|---|---|
| oracle r51 (or_80 = f7584) | 940,615 / 288 | 1.776 | 147318 | `regions_win2_oracleR51_93_a615_288.txt` |
| willeq b1130 before (B1164's anchor) | 960,782 / 316 | 1.483 (f158) | 148095 | `regions_win2_t93_b1130_before_a782_316.txt` |
| willeq b1130 after | 940,615 / 288 | 0.976 (f154) | 80960 | `regions_win2_t93_b1130_after_a615_288.txt` |
| willeq b1130 after, own height | 940,615 / 238 | 1.424 (f154) | 80633 | `regions_win2_t93_b1130_after_a615_238.txt` |

(all in `/mnt/projects/fable/review/B1166/worker/named_measure/`.) The before row reproduces
B1164's 1.485 at f158 within 0.002 (B1164 rendered from its own tree, so the frames are not
byte-identical). **Verdict change**: B1164's "willeq = 0.995x the oracle in the same window"
held only because the willeq target stood nearer the camera than the oracle's (316 px at
row 782), which enlarged its landing on screen. With the target where the oracle's stands,
the landing's lit pixels are 80960 / 147318 = 0.55x the oracle's in the same pixel window
(0.80x if normalised by each side's own target height). The sim is not exonerated by the
window measure; this joins B1164's (#483) pixel-area gap. The band instrument on the same
runs: landing_cov 2.577 -> 2.243 (oracle 3.945), landing_end +6.667 both (oracle +6.670)
(`/mnt/projects/fable/review/B1166/worker/named_measure/t93_b1130_before_band/b1130_rows.tsv:2`, `/mnt/projects/fable/review/B1166/worker/named_measure/t93_b1130_after_band/b1130_rows.tsv:2`).

## 6. Verification

- `tbuild <wt> test -R 'FxHarness|B1166TargetAnchor'`: 9/9 passed
  (`/mnt/projects/fable/review/B1166/worker/ctest_fxharness_1.log`), re-run at the final tip
  (`ctest_final.log`), plus `tbuild <wt> hygiene` (`hygiene_final.log`).
- Cite check: see the MR.

## RULING REQUEST

None. The anchor is a measurement (sec. 1), not a bless-only file; no golden, manifest,
baseline or `config/constrained_presets.json` value changed.

## FOLLOW-UP

1. **The b1130 sweep's caster level** (owner: the B1155/B1157 sweep layout). Its plans pass
   the record table's class level (1 for most records); the oracle captures were all taken
   at `#level 60`. At level 60 spell 93's landing ends at +6.667 (oracle +6.670) instead of
   +4.700. Re-run the b1130 sweep at caster level 60 before reading its landing verdicts.
2. **The human-vs-dark-elf model height** (owner: race model scale / the official model
   heights): the FABLE human at the oracle's spot is 0.826x the oracle's height (238 vs 288
   px); the b1118 camera shows the same direction (241 vs >= 284).
3. **r7 (spell 40) Source window moves with the target's placement** from BC+1.57
   (frame 92); deterministic, mechanism not traced.
4. **Fear 229 below-the-feet extent** (h_p5 -0.04 vs -0.26..-0.34, ground 0.14 vs
   0.18-0.21): owner candidates the Target-stage spawn volume (B1150 shapes); not proven
   here.
5. Every B1130 geometry file with tfeet 940,652 / tmpx 325 carries the label-row error;
   the oracle-side anatomy columns of `review/B1130/record_table.tsv` (target cen_h, h) were
   measured at that anchor (e13 moves from cen_h 0.77 / h 0.23..1.63 to 0.76 / 0.13..1.72
   at the measured anchor, same crop; `refactor/B1166/tables/fear229_anatomy.tsv:2-3`).
