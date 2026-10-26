# B1158: the visual FX ledger

One side-by-side clip per measured spell record — the **oracle** client capture
on the LEFT, the **willeq** render on the RIGHT, same b1130 camera rig,
aligned at BeginCast by one stated frame rule — plus an 8-cell strip, a
verdict strip, and an index page. This is the way to *look at* what the FX
work is doing: for each casting type / effect / unique combo, the full effect
in both clients on one timeline.

**Scripts only.** Nothing is re-rendered or re-measured here: every number a
clip quotes comes from a measured file (the oracle leg's
`video.calibration.tsv`, the leg's `all-opcodes.tsv` / B1138 measure headers,
the harness `timing.tsv` / `meta.json`, `b1130_rows.tsv`), and `ffprobe` for
fps and frame counts. No model judges a frame.

## Layout of the output

```
<out-dir>/
  clips/<id>.mp4            2560x920 @ 30 fps: oracle LEFT / willeq RIGHT,
                            title bar + alignment line on top, verdict strip
                            on the bottom, teal line = BeginCast, amber line
                            = Action, Act-relative clocks on both halves
  clips/<id>_strip.png      8 cells: t = Act-0.5 / 0.0 / 0.5 / 1.5 / 3 / 5 / 7 / 7.5 s
  specs/<id>.json           the measured inputs for one clip (what make_clip.py consumed)
  alignments.jsonl          one JSON line per clip: the frames the rule computed
  index.tsv / index.html    record, spell, clip, verdicts, alignment frames
  clip.log                  per-clip stderr (the human alignment line) + failures
```

The first run (this commit) is the **B1155 pre-animation baseline** gate:
`~/kb-evidence/kb-b1158-visual-ledger/VISUAL-FX/b1155-baseline/`.

## The alignment rule (stated once)

Both sides are cut from **BC − 1.0 s to Act + max(8.0 s, measured_end + 0.5 s)
in their own timelines**, so the two BeginCasts sit at the same clip time by
construction (clip frame 30). The frames each side is cut at are computed,
never hand-offset:

* **Oracle:** video frame `F` ↔ epoch `t0 + F/fps`, where `t0 = calibrated_t0`
  from the leg's `video.calibration.tsv` (the epoch of the video's frame 0;
  equivalently `video_t0 = launch_epoch + (marker_epoch − launch_epoch) −
  marker_frame/marker_rate`). Then
  `F_BC = round((bc_epoch − t0) × fps)`, `F_Act = round((act_epoch − t0) × fps)`.
  The BeginCast/Action epochs are the leg's own `OP_BeginCast` /
  `OP_Action` rows (`all-opcodes.tsv` or the B1138 measure header).
* **Willeq:** the harness video is 30 fps and its timeline starts at run
  start, so `F_BC = round(bc_t × 30)`, `F_Act = round(act_t × 30)`, with
  `bc_t`/`act_t` from the run's `timing.tsv` (or `meta.json` `t_begincast` /
  `t_action`).

`make_clip.py` prints the rule's result for every clip (also in the title bar
and in `alignments.jsonl`):

```
[<id>] oracle <label> f<BC-frame> (BC) / f<Act-frame> (Act) @ <fps> fps (t0 <epoch>)
       | willeq <label> f<BC-frame> (BC) / f<Act-frame> (Act) @ 30 fps
       | window BC-1.0..Act+<end>s, <N> frames
```

### Worked example 1 — rec 0, 93 Burst of Flame, fixed_npc (oracle 25 fps)

* Oracle `B1130/r51` (`review/B1130/record_table.tsv` row 0 → capture `r51`):
  `calibrated_t0 = 1792678587.641`; `OP_BeginCast = 1792678886.463`;
  `OP_Action = 1792678887.973` (the leg's measure header
  `review/B1130/measure/r51_fixed_npc.txt`, which `ledger_inventory.py` reads; the
  `all-opcodes.tsv` row is the same packet at ms precision, `.462`).
  `F_BC = round((1792678886.463 − 1792678587.641) × 25) = 7471`,
  `F_Act = round((1792678887.973 − 1792678587.641) × 25) = 7508`.
* Willeq gate `fixed_npc_r0` (`timing.tsv`): BeginCast `t = 1.500 s`,
  Action `t = 3.000 s` → `F_BC = round(1.5 × 30) = 45`, `F_Act = 90`.
* The clip therefore cuts the oracle from `298.822 − 1.0 s` and the willeq
  video from `0.5 s`; both BeginCasts land on clip frame 30, both Actions
  on clip frame 75 (1.5 s after BC on both sides — a 1.5 s cast).

### Worked example 2 — missile M1_38, 38 Lightning Bolt, record 80

* Oracle `B1152/m1_rig38` (the rig leg of the B1152 missile capture; the
  client record is 30 fps, so the two sides share a rate but not an epoch):
  `calibrated_t0 = 1792885372.141`, BeginCast epoch `1792885842.426`,
  Action epoch `1792885844.958` → `F_BC = 14109`, `F_Act = 14185`.
* Willeq: the B1158 harness render of the same plan
  (`scripts/plans/m1_rig38.plan`: cast at `t = 1.5 s`, `cast_ms = 2500`,
  target other, `--camera b1130`, `model_viewer --headless --fps 30`):
  `F_BC = 45`, `F_Act = 120` (Action at `t = 4.000 s` per the run's
  `timing.tsv` — the same 2.5 s cast as the oracle's
  `1792885844.958 − 1792885842.426 = 2.532 s`).
* The B1152 missile verdicts (launch / reach / arrival, from the B1152 gate)
  ride in the strip; M2_54 carries the no-missile rule instead (record 82
  has no Middle stages).

## What a verdict colour means

The bottom strip and `index.html` use the gate's banded verdicts
(`refactor/B1155/banded_table.tsv` for the first run):

| colour | verdict | meaning |
|---|---|---|
| green | `PASS` | the measured quantity is inside the oracle-derived band |
| yellow | `short` | measured below the band (willeq lags / is smaller) |
| red | `over` | measured above the band (willeq leads / is bigger) |
| grey | `n-a` | the row does not apply to this record |

Gate columns: `Source` / `Landing` / `End` (stage windows in band?), `p90-src`
/ `p90-land` (peak coverage), `Src-onset`, `Src-cen`, `Src-lo`, `Src-hi`
(onset / centroid / horizontal extent of the source stage). Missile legs
carry the B1152 columns (`launch`, `reach`, `arrival`); observer legs (B1134
/ B1143) are oracle-only in this gate — their strip row is empty and the
right half is labelled `oracle-only leg`.

## How to regenerate

```bash
bash refactor/B1158/scripts/make_ledger.sh <gate-run-dir> <out-dir> [band-table.tsv]
```

* `<gate-run-dir>` — a harness gate sweep in the B1155 layout:
  `measure/<kind>_r<rec>/video.mkv` + `runs/<kind>_r<rec>/timing.tsv`
  (the B1157 re-gate is expected to keep this layout; when it lands, point
  the command at it and the same ledger is rebuilt against the new renders).
* `<out-dir>` — created fresh each run (specs, clips, index).
* `[band-table.tsv]` — the gate's own banded verdict table; omit for a gate
  without one (verdicts come out empty).
* `LEDGER_EXTRA_MEASURE=<dirs>` — extra measure dirs for legs outside the
  gate (the two missile willeq renders live in
  `~/kb-evidence/kb-b1158-visual-ledger/sweep/measure/`).
* `LEDGER_JOBS=n` — parallel clip encodes (default 3).

The inventory is the union of: every record the gate ran (banded against its
table), the two B1152 missile legs, and the B1134/B1143 observer legs
(oracle-only). The oracle source for each is resolved from
`review/B1130/record_table.tsv` (column `capture`) or the leg's
`video.calibration.tsv`; where the on-host copy of an oracle video is absent
the script falls back to the ThinkPad-NFS mirror under
`~/kb-evidence/kb-b1158-visual-ledger/sources/`.

To re-encode a single clip, run
`python3 refactor/B1158/scripts/make_clip.py <out-dir>/specs/<id>.json <out-dir>/clips`.

## Reproducing the missile willeq renders

```bash
ssh thinkpad '/home/user/willeq-builds/<worktree>/build/bin/model_viewer \
  --spell-test --client /mnt/projects/EverQuestP1999 \
  --eqe1 /mnt/projects/EverQuest_ETC1/willeq.eqe1 \
  --plan refactor/B1158/scripts/plans/m1_rig38.plan \
  --frames-dir /home/user/kb-frames/<worktree>/m1_rig38 \
  --fps 30 --seconds 17.5 --sim-hz 60 --seed 1 --headless --camera b1130'
```

(m2_rig54.plan: `--seconds 16.0`; then rsync the frames back, encode
`video.mkv` at 30 fps, and point `LEDGER_EXTRA_MEASURE` at the result.)

## Hygiene

This commit changes only `refactor/B1158/` and the evidence under
`~/kb-evidence/kb-b1158-visual-ledger/`. The in-repo doc checks (ctest
label `hygiene`: `DocCounts`, `DocClaims`, `DocCitations`,
`FileSizeCeiling` and the rest) run clean: `rbuild <worktree> hygiene`
reports 0 failures on this tree.
