# B1155 (#475): the FX harness's `b1130` / `b1130-self` camera presets, and the 40-record sweep banded against B1130

This is the MEASUREMENT side of the B1147 chain, parallel to B1150 (the shapes/sim).
This card owns the `tools/model_viewer` FX harness, the sweep scripts under
`refactor/B1155/scripts/`, this README, and the `FxHarness` ctest. It does NOT touch
the sim, the reader or `spell_visual_fx.cpp` (B1150 owns them).

## Why the new presets

`review/B1149/scripts/run_sweep.sh` measured the harness on `--camera b1118` (the lane
TOUR framing: the target at the caster. The b1118 preset STATES a 235 px model
(`model_viewer_fx_harness.cpp:93-99`, the original B1118 framing), but the sweep's
per-record runner `review/B1149/scripts/run_scene.sh` passes no `--caster-px`, so the
runs rendered the harness default of 228 px -- `model_viewer_fx_harness.cpp:517-520`,
and e.g. `review/B1149/runs/sw_r16_242/run.log` prints "rendered caster rows 378..605
(228 px, feet row 605)") and could only report
harness/oracle RATIOS against B1130's oracle, because B1130 captured every record with
its OWN fixed geometry: the NPC ~1.4 caster heights to the caster's right (`fixed_npc`)
or the caster alone (`fixed_self`), the caster 209 px, the NPC 325 px. The same spell
(93) measured 3.945 landing coverage in B1130 but 5.847 on the tour -- the camera
mismatch, not the FX. B1155 adds the two presets that match B1130's geometry so the
sweep bands against `review/B1130/record_table.tsv` directly.

## The preset geometry (quoted from B1130)

`review/B1130/README.md` "Geometry" and "Plans":

```
sed -n '57,64p' review/B1130/README.md     # the plans (the TrollF, the #goto, the #stun)
sed -n '73,74p' review/B1130/README.md     # the "Geometry" block (the feet/px facts)
```

```
  - An NPC cast, if the spell allows one. The NPC is `#spawn TrollF 1 1 0 30000`. The caster is then `#goto`'d
    -67.6,434.4 so the NPC stands about 1.4 caster heights to its right, and `#stun 120000` keeps the NPC from closing to
    melee.
...
- **Geometry.** The fixed camera puts the caster's feet at (640,598) and the caster 209 px tall, from head top 389 to
  feet 598 in `oracle/cal_r35/15_time_settle.png`; this is B1118's unit. For the NPC: feet (940,652), 325 px tall.
```

So:

| fact | value | source |
|---|---|---|
| frame | 1280x800 | the settle stills' resolution |
| caster feet | (640, 598) | `oracle/cal_r35/15_time_settle.png`, B1118's unit |
| caster height | 209 px (head top 389 -> feet 598) | the same still |
| target feet | (940, 652) | the same settle still, the `fixed_npc` leg |
| target height | 325 px | the same still; the oracle's level-1 human standing ~1.55x NEARER to the camera than the caster -- its depth, not a race scale (a consistency check, not an input) |
| target | race 1 (human), gender 0 (male), size 6, idle | eqemu's `#spawn [name][race][level][texture][health][gender]...`: `#spawn TrollF 1 1 0 30000` is race=1, level=1, texture=0, hp=30000 with the GENDER arg ABSENT -> `Mob::GetDefaultGender(1)` = MALE; "TrollF" is only a label (`model_viewer_fx_harness.cpp`), `#stun 120000` |

The caster is the b1118 char-67 appearance (the same Testcaster B1118 and B1130 used).

## The solve

The camera is fitted to the caster exactly as b1118's is (`fitFaceOnCamera` in
`tools/model_viewer/model_viewer_fx_harness.cpp`, iterating dist/look until the caster
is 209 px at feet row 598). The target's world offset (x, z) is then SOLVED from its
two feet pixels -- a Newton solve on the rendered silhouette (`fitFixedNpcTarget`),
NOT hand-tuned. A preset that does not solve to the stated pixels within 1 px exits 2.
The solve, as printed by the harness (a fixed_npc run, `--camera b1130`):

```
$ model_viewer --spell-test ... --camera b1130   # the two lines, from the run log
[fx-harness] camera b1130: dist 31.672 look 5.843 above the pose-box feet, pitch 12.0, pose-box height 6.092 -> rendered caster rows 389..598 (209 px, feet row 598), column centroid 642 (requested frame: 389..597, 208 px, feet 597 at column 642)
[fx-harness] camera b1130: target (x -6.702, 0, z 9.732) -> rendered target feet row 652 at column 940, 50 px head top to feet (B1130 fixed: 652 at 940, 325 px)
```

`b1130-self` uses the same camera (the caster is framed identically) with no target.
The solve is pinned by `tests/test_fx_harness.cpp`:
`B1130PresetSolvesTheFixedNpcGeometry` (the caster 209 px / row 598 / col 640, and the
target feet row 652 / col 940) and `B1130SelfPresetSolvesTheCasterAlone` (the caster
alone, no target box in `scene.tsv`).

### The target's rendered height: the human vs the human (documented, not hand-tuned)

The target is the SAME level-1 human male the oracle rendered (race 1, gender 0,
size 6) -- NOT a race-9 Troll (the card's withdrawn guess; see the table above
for why `#spawn TrollF 1 1 0 30000` is a human male). The 325 px in B1130's
still is that human's nearer depth (~1.55x closer to the camera than the
caster), not a race scale.

The target's FEET (row 652, column 940) are the solved FX reference and hold to
1 px. The solve, as printed by the harness (a fixed_npc run, `--camera b1130`):

```
[fx-harness] camera b1130: target (x -6.702, 0, z 9.732) -> rendered target feet row 652 at column 940, 50 px head top to feet (B1130 fixed: 652 at 940, 325 px)
```

The fit frame is the model's REST POSE, which the FABLE client renders as a
collapsed 50 px strip (head top to feet) at the solved feet (row 652, column
940); the ctest pins that 50 px rest pose and the feet to 1 px. In the running
frames the same human stands at 315-316 px (head top 466-467 -> feet 782, column
centroid 952.7) -- within ~3 % of the oracle's 325 px, so the standing height
of the shared human model agrees. Measured by
`refactor/B1155/scripts/measure_b1155_target.py` over the `solve_r1` frames, the
same frames the sweep measures (default frames 10-90 and 179; the target's dark
run in the window columns 850-1080, threshold 60, B1130's feet-minus-head
convention -- its caster 209 px is feet 598 minus head top 389):

```
$ python3 refactor/B1155/scripts/measure_b1155_target.py /home/user/kb-evidence/kb-b1155-b1130-camera/solve_r1
frame  10: head top 467 -> feet 782 = 315 px, column centroid 952.7
...   # 82 lines total, one per frame
frame 179: head top 466 -> feet 782 = 316 px, column centroid 952.7
range over 82 frames: head top 466-467, feet 782-782, height 315-316 px, column centroid 952.7-952.7
```

The 50 px is only the rest pose the solver fits on; the running-frame standing
figure sits at a slightly different feet row than the rest-pose bottom because the
standing pose extends lower (the re-run's landing columns below are the arbiter of
the measurement box's coverage). This is a model-rest-pose detail of the FABLE
client, not a camera error, and it is out of this card's scope
(harness/scripts/tests only). The ctest asserts the feet to 1 px and the rest
pose to the measured 50 px, with the gap written into the test's comment and here.
It does NOT affect the FX measurement: the FX is measured in the crop around the
caster and the target's own box, in caster heights, exactly as B1130 measured it.

## The B1130-unit measurement

`refactor/B1155/scripts/harness_measure_b1130.py` measures one harness run with
**B1130's own instruments, unchanged** (`review/B1132/scripts/`: `fx_measure.py`,
`fx_timing.py`, `landing_onset_abs.py`, `anchor.py`, `fx_sprite_roi.py`), run exactly
as `review/B1130/scripts/cast_measure.py` runs them, minus the pcap (the cast times
come from the harness's `timing.tsv`, as `review/B1139/scripts/harness_measure.py` does
for b1118). The geometry is B1130's two geom files:

| preset | crop | caster feet / px | target feet / px | halfw |
|---|---|---|---|---|
| fixed_npc | 150,60,990,540 | 640,598 / 209 | 940,652 / 325 | 0.7 |
| fixed_self | 150,60,900,540 | 640,598 / 209 | 640,598 / 209 (the caster) | 1.6 |

The columns produced per cast (one row per record) are B1130's own:

| column | instrument | B1130 convention |
|---|---|---|
| `source_onset_s` | `anchor.py` SOURCE | the first 0.3 s of the Source's active run, BC+ |
| `source_cov` | `anchor.py` SOURCE `peak(cast)` | the peak lit-pixel coverage in the cast window, caster heights^2 |
| `source_centroid` | `anchor.py` SOURCE `CAST` | the lit-pixel centroid height in caster heights above the feet |
| `src_h_lo` / `src_h_hi` | `anchor.py` SOURCE `CAST` | the lit-pixel height p5..p95, caster heights |
| `landing_onset_s` / `landing_end_s` / `landing_cov` | `landing_onset_abs.py` (RISE) | the landing's rise onset/end (Act+) and peak coverage, caster heights^2, in the target's own box |
| `p90_src` / `p90_land` | `fx_sprite_roi.py` | the p90 sprite size in the Source ROI / the landing ROI (summarize.py's `roi_c` / `roi_t`) |

The ROI rectangles are `review/B1130/scripts/summarize.py`'s, computed from the preset's
own numbers (its `roi_c = fx-1.5mpx..fx+1.5mpx, fy-1.6mpx..fy+0.1mpx` and
`roi_t = tfx-1.5tm..tfx+1.5tm, tfy-2.0tm..tfy+0.1tm`, crop px, the crop origin the
offset). The B1130 nameplate `--exclude` rectangles do NOT apply: the harness draws no
UI (the oracle's nameplates are oracle-side chrome, like the Tip-of-the-Day window
B1142 removed).

## The 40-record sweep, banded

`refactor/B1155/scripts/run_sweep_b1130.sh` runs all 40 records (B1130's
`record_table.tsv` minus record 51, which B1130 did not capture) on the new preset,
the camera per record's B1130 capture class (`fixed_npc` -> `--camera b1130`,
`fixed_self` -> `--camera b1130-self`), rendering on the ThinkPad and measuring here
with the venv python. The plan casts the record's ACTUAL EQ spell id
(`record_table.tsv` column 5 -- the record number is B1130's index, not the spell).
`refactor/B1155/scripts/band_table.py` then bands every record's nine columns against
the B1130 oracle:

| column | band | source of the band |
|---|---|---|
| `source_cov`, `landing_cov` | the B1130 value +- 3 % | the +-3 % is the outer edge of B1131's verdict band, "No landing row regresses beyond B1121's +-2-3 % capture spread" (`review/B1131/README.md:121`), applied as a percent deviation from the B1130 reference -- the B1142 method (`refactor/B1142/README.md`: "the new captures land within -0.9..+2.6 % of B1127") |
| `landing_end_s` | the B1130 value +- 0.10 s | B1149's verdict method ("within +/- 0.1 s of the lane") |
| `p90_src`, `p90_land` | the B1130 value +- 15 % | the B1142 method (`gate/regression_table.tsv` `roi_*_band` = the oracle roi p90 +- 15 %) |
| `source_onset_s` | the B1130 value +- 0.10 s | B1131/B1132 |
| `source_centroid`, `src_h_lo`, `src_h_hi` | the B1130 value +- 0.15 | B1131/B1132 |

Each cell's verdict is PASS / short / over / n-a. The full banded table is
`refactor/B1155/banded_table.tsv` (regenerate:
`python3 refactor/B1155/scripts/band_table.py /home/user/kb-evidence/kb-b1155-b1130-camera/sweep refactor/B1155/banded_table.tsv`).

### The banded results (40 records, 0 failed to render)

Per-column verdict counts (PASS / short / over / n-a of 40):

| column | PASS | short | over | n-a |
|---|---|---|---|---|
| `source_onset_s` | 34 | 1 | 5 | 0 |
| `source_centroid` | 30 | 5 | 5 | 0 |
| `src_h_lo` | 32 | 0 | 8 | 0 |
| `src_h_hi` | 21 | 19 | 0 | 0 |
| `p90_land` | 16 | 12 | 11 | 1 |
| `p90_src` | 9 | 17 | 14 | 0 |
| `landing_end_s` | 6 | 27 | 7 | 0 |
| `source_cov` | 0 | 33 | 7 | 0 |
| `landing_cov` | 0 | 38 | 2 | 0 |

Median harness/oracle ratios (the card's convergence check), regenerated from
`b1130_rows.tsv` by the one command

```
python3 refactor/B1155/scripts/medians.py /home/user/kb-evidence/kb-b1155-b1130-camera/sweep
```

(the median over the 40 records, record 51 excluded, of the harness/oracle ratio
per record; the fixed_self rows are UNCHANGED by the race-1 target fix -- that
preset draws the caster alone, no target -- so only the fixed_npc row and ALL
move with the re-run):

| | source_cov | landing_cov |
|---|---|---|
| ALL | **0.422** | **0.271** |
| fixed_npc | 0.490 | 0.271 |
| fixed_self | 0.332 | 0.289 |

**What converged, and what didn't.** The B1130 camera did its job on the
**timing and shape** of the Source: `source_onset_s` (34/40 PASS, the B1131/B1132
+-0.10 s band), `source_centroid` (30/40 PASS, +-0.15) and `src_h_lo` (32/40 PASS,
+-0.15) all converge to the oracle -- the B1149 sec. 6 ratios that were distorted by
the b1118-vs-B1130 camera mismatch are gone for these columns. The **coverage does
NOT converge**: `source_cov` is 0/40 PASS (median 0.422x the oracle) and
`landing_cov` is 0/40 PASS (median 0.271x). The FABLE FX sim (B1150's
spellsnew-based particles) draws a Source/landing at 0.42x / 0.27x the official
client's lit-pixel density, measured in B1130's own crop and its own instruments.
The `landing_end_s` is mostly short (27/40) for the same reason: a less-dense
landing runs out sooner. This is a real sim finding, not a camera or measurement
error: the same anchor.py / landing_onset_abs.py that measured the oracle measured
the harness, in the same geometry.

**Owner mechanism per miss.** The coverage and landing-end shortfalls are the FX
**shape/density** -- B1150 (the spellsnew particle sim: sprite size, count, and the
landing's extent/duration). The `src_h_hi` shortfalls (19/40) and the `p90` sprite
shortfalls are the same shape gap (the lit cloud is shorter and its p90 sprite is
smaller). A few `source_cov`/`p90` OVERS (7 source_cov, 14 p90_src, 11 p90_land)
are where the FABLE sim happens to draw denser than the oracle for that spell --
same owner, the opposite direction. Nothing here is a frames/scale/modes issue
(B1151), a missile/Middle issue (B1152), or a retire issue (B1153): the
timing/shape columns pass, so the sim's frame count and mode are right; the
lit-pixel density is the gap.

## What this supersedes

B1149 sec. 6's harness/oracle ratios (median Source 1.21, landing 0.64) were the
b1118-vs-B1130 camera mismatch. This card's sweep, on the B1130 camera, replaces them:
the harness is now measured in B1130's own geometry and banded against B1130's own
oracle, per cell. This does NOT edit B1149's README.

## FOLLOW-UP

- The b1130 target is the level-1 human male the oracle rendered (race 1, the
  card's withdrawn race-9 guess). The solver fits the model's REST POSE (50 px,
  feet row 652 / column 940); in the running frames the same human stands at
  315-316 px -- within ~3 % of the oracle's 325 px -- but with its feet at row 782,
  not 652, because the rest pose's bottom is not the standing pose's feet, so the
  fit places the standing figure slightly nearer the camera. A future unit that
  wants the target's OWN standing box at the oracle's still (feet 652, 325 px)
  must fit the solver on a standing frame, not the rest pose (done by B1166,
  #485: `refactor/B1166/README.md`; the oracle target is 288 px with feet at row
  615, B1130's 325 / 652 began at the name label). This card's FX
  measurement is unaffected (measured in the crop, in caster heights; the re-run
  landing columns are the arbiter).
- The per-miss owner mechanism (B1150 shapes / B1151 frames-scale-modes / B1152
  missile-Middle / B1153 retire / unattributed) is assigned per column in the
  **Owner mechanism per miss.** paragraph above -- the banded table itself carries
  no owner column (oracle/harness/verdict per cell only).
