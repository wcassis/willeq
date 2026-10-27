# B1171 (#490): the harness sweeps cast at the level the oracle capture ran

Branch `task/kb-b1171-sweep-level`, cut at fable 10ffe690 (B1166's target fit), merged with
gitlab/fable twice on the orchestrator's word: 070a9215 (B1169's frame determinism) and
f61fafb4 (B1173, B1165's particle options); one conflict, in
`tools/model_viewer/model_viewer_fx_harness.cpp` (B1165's `particle_options.tsv` block and this
card's `caster_level` timing header sit on the same lines; both kept). The specification is
`glab issue view 490` (asks 1-3, acceptance A-E). Evidence: `/mnt/projects/fable/review/B1171/worker/`.

"before" = fable f61fafb4 built in a throwaway worktree (`wt/kb-b1171-before`) running B1166's
sweep layout (the plan level = the record table's `level` column); "after" = this branch at the
same merge, the plan level = the capture's `#level`. Both binaries were built and every frame
rendered on the ThinkPad; every measurement ran on dev-u24. Two earlier rounds of renders (on
the 10ffe690 and the 070a9215 bases) were stopped when fable moved and are kept, unused, under
`superseded_pre_b1169/` and `superseded_070a9215/`.

## 0. Result

- **The defect.** The b1130 sweep runners took the plan's `caster_level` from column 9 of
  `review/B1130/record_table.tsv`, whose header is `level`: the CLASS level the record's spell is
  learned at (1 for 21 of the 40 records, 44 for r28). Every B1130 capture ran the caster at
  `#level 60`: r51 (record 0) plans it (`review/B1130/oracle/r51/plan.tsv:17`) and executes it
  (`review/B1130/oracle/r51/steps.tsv:10`), and so do the other 39 captures (sec. 1).
- **Ask 1.** The level now comes from the capture: `scripts/fx/capture_level.py:72`
  (`capture_level`) reads the last `#level N` (or eqemu's `#set level N`) from the capture's
  plan.tsv AND its executed steps.tsv, and refuses a capture where they disagree or no level
  step ran (no default, no database read). The harness logs the level of every cast in the
  run log (`tools/model_viewer/model_viewer_fx_harness.cpp:1138`, "cast 0: spell 93
  caster_level 60 (plan)", or "(--caster-level)" when the plan row has none) and in a new last
  `caster_level` column of timing.tsv (`tools/model_viewer/model_viewer_fx_harness.cpp:1155`,
  `:1280`). The sweep runner writes the capture's level into each plan and each sweep-log line,
  reads the run's own level back and refuses to measure a mismatch
  (`refactor/B1171/scripts/run_sweep_b1130.sh:56`, `:60`); the table builder refuses every
  verdict whose harness level differs from the oracle row's
  (`refactor/B1171/scripts/compare_b1130.py:95`, `:107-108`; exit 3 for an after row, line 120).
  Tests: `FxCaptureLevel` (12 Python cases, `scripts/ci/test_fx_capture_level.py`, among them
  the refusal and "the class-level column is not read") and two harness ctests
  (`tests/test_fx_harness.cpp:381`, `tests/test_fx_harness.cpp:392`).
- **Ask 2.** The 40-record sweep re-run at level 60: landing_end PASS 2 -> 8 of 40, p90_land
  PASS 6 -> 17, the 9 records whose class-level run drew no landing at all now have one;
  landing_cov PASS stays 0 (39 short, 1 over); source_cov goes from 30 short / 9 over to
  6 short / 30 over. Spell 93 (r0) reads landing_cov 2.243 and end +6.667 against the oracle's
  3.945 / +6.670, B1166's level-60 probe to the digit. Every before verdict is REFUSED
  (level 1-44 vs 60) except r28's, whose frames are byte-identical at 44 and 60. Every
  remaining miss has an owner; five landing_end overs have none yet (sec. 3, FOLLOW-UP 3).
  tour-7 on b1118: unchanged, frames byte-identical (it already ran at 60, now read from the
  B1118 legs).
- **Ask 3.** Audit (sec. 5): the b1130 and b1130-self (fixed_self) rows were the only ones at
  the wrong level. The b1118 tour and the b1118 40-record sweep wrote a literal 60, which the
  B1118 legs and the B1130 captures confirm; they now read it. The B1142 tour gate is the live
  client: its legs ran `/say #level 60` and the client logged `level=60` on every tour cast.
  Five runners that read column 9 or a literal 60 now call the resolver.
- **RULING REQUEST**: B1155's, B1157's and B1166's committed b1130 tables were measured at the
  class level; they stop being valid references (below).

## 1. Where the oracle's level is, and what the table's `level` column is

`scripts/fx/capture_level.py b1130 <record>` maps the record to its capture (column 3,
`capture`) and reads that capture's two files. For the 40 sweep records: 40/40 read 60, every
one planned and executed (step `09_lvl` in 39 captures, `15_lvl` in e11). Record 51 is B1134's
observer row with no lane-5 capture directory; the sweep skips it, as before. The table's
column 9 is the class level: 1 for r0, 4 for r3, 12 for r10, 29 for r40, 44 for r28. The
client's own `kMeasuredCasterLevel` says what the captures ran
(`include/client/graphics/spell_visual_fx.h:140`), and the harness default `--caster-level`
is 60 (`tools/model_viewer/model_viewer_fx_harness.h:62`); neither is read by the sweep any
more, which writes the capture's value into every plan row.

For tour-7, `capture_level.py b1118 <spell>` maps the spell to the B1118 oracle leg its rows
come from (the `video` column of `review/B1118/spell_fx_table.tsv`: oracle_druid for 93 and 253,
oracle_enc for 41, oracle_leg for 38 / 54 / 656 / 458) and reads 60 from each
(`review/B1118/oracle_druid/steps.tsv:16`, `review/B1118/oracle_leg/steps.tsv:16`,
`review/B1118/oracle_enc/steps.tsv:16`).

## 2. The runs

`refactor/B1171/scripts/run_sweep_b1130.sh before|after|control [records]` (B1166's layout:
t_begin 1.5 s, target distance 0, fps 30, sim 60 Hz, seed 1, fixed_npc -> `--camera b1130`,
fixed_self -> `--camera b1130-self`; two shards per variant). Every run's frames dir carries
its plan, run.log ("cast 0: ... caster_level N") and timing.tsv; the sweep logs
(`b1130_{before,after}/sweep.{a,b}.log`) give the capture, the oracle level and the plan level
of each record. All four shards exited 0 (`sweep.*.exit`).

Control (`/mnt/projects/fable/review/B1171/worker/control_identity.txt:1-2`): this branch's
binary at B1166's class-level plans renders r0 and r4 byte-identical to fable f61fafb4 (495/495
and 570/570 frames) with identical measured rows, so the binary change is logging only and
every before/after difference below is the level.

## 3. The b1130 40-record sweep (ask 2)

`refactor/B1171/scripts/compare_b1130.py <before> <after> <out> refactor/B1171/tables/owners.tsv`
-> `refactor/B1171/tables/b1130_table.tsv` (one row per record: capture, oracle_level,
before_level, after_level, frame identity, and per axis the oracle, B1157, B1166, before, after,
delta and the two verdicts, then the moved axes, the verdict changes and the owner);
band_table.py's own banded outputs beside it (`.before.tsv`, `.after.tsv`).

Before vs the committed tables: before equals B1166's committed after-columns in 156 of the 160
cells (40 records x 4 axes); the 4 that differ are r35's (landing_cov 1.598 -> 1.215, end
+5.217 -> +5.317, p90 0.105 -> 0.095, source 0.953 -> 0.892;
`refactor/B1171/tables/b1130_table.tsv:37`), the B1161 (#480) retirement of the 253 override
that merged after B1166's renders (`refactor/B1169/README.md:145`). B1157's columns are the
same class-level layout on an older target fit.

Verdict counts, before (class level) -> after (capture level), raw band verdicts:

| axis | before | after |
|---|---|---|
| landing_cov | 30 short, 1 over, 9 n-a | 39 short, 1 over |
| landing_end_s | 2 PASS, 27 short, 2 over, 9 n-a | 8 PASS, 26 short, 6 over |
| p90_land | 6 PASS, 10 short, 14 over, 10 n-a | 17 PASS, 8 short, 14 over, 1 n-a |
| source_cov | 1 PASS, 30 short, 9 over | 4 PASS, 6 short, 30 over |

In the table every before verdict is written `REFUSED(L<n>!=60)` except r28's (class level 44:
its frames are byte-identical at 44 and 60, line 30, so no slot of that record has a minLevel
between them). The `verdict_changes` column lists, per record, every raw verdict that moved.
landing_end short -> PASS: r0, r1, r12, r23, r25; n-a -> PASS: r19, r22, r27; PASS -> over:
r29; PASS -> short: r33. Rows that drew no landing at the class level and now do (n-a ->
measured): r4, r5, r15, r17, r19, r22, r26, r27, r43.

The named rows: r0 (spell 93) 2.243 / +6.667 vs the oracle 3.945 / +6.670 (line 2); B1166's
level-60 probe read the same two numbers (`refactor/B1166/README.md:216`). r1 (spell 200)
1.380 / +6.567 vs 3.050 / +6.580 (line 3).

**Owners** (`refactor/B1171/tables/owners.tsv`, the table's last column; every missed axis of
every row):

- **landing_cov short (38 rows)**: B1164 (#483), the landing's pixel area (B1166 measured
  spell 93's landing at 0.55x the oracle's lit pixels in the oracle's own window,
  `refactor/B1166/README.md:348`). The after/oracle ratio is a median 0.452 on the 28
  fixed_npc rows and 0.435 on the 11 fixed_self rows (r40 excluded), so the shortfall is not
  the target body: a fixed_self landing is on the caster, whose height is fitted. On fixed_npc
  rows B1172 (#491) is named as well (the human target is 238 px where the oracle's is 288),
  as a second, smaller term the fixed_self rows cannot show.
- **landing_end_s short (26 rows)**: B1164 (#483) through the instrument: the end is the last
  frame whose lit-pixel count exceeds an absolute threshold
  (`/mnt/projects/fable/review/B1131/scripts/landing_onset_abs.py:20`, `:40`), so a landing
  with a fraction of the oracle's area falls under it earlier in its fade. Not separated per
  row.
- **p90_land / source_cov misses**: B1164 (#483), the attach scale s (sprite size and
  per-particle area on both stages). The source_cov swing to "over" is the level: at 60 every
  Source slot of a record is drawn (minLevel 0 / 24 / 39), as the oracle drew them.
- **r35 (spell 253, record 104)**: B1167 (#486), all four axes.
- **r7 source_cov**: B1173 (#492): the Middle-stage missile crosses the Source window
  (`refactor/B1173/README.md:23`).
- **r40 (602 Evacuate: North) landing_cov over / end over (+4.167 vs +0.730)**: the scene, not
  the FX. The oracle caster zones out (OP_DeleteSpawn Act+0.36-0.40, model gone by
  Act+0.79-0.81; `review/B1130/record_table.tsv:11`, landing column); the harness never
  despawns its caster, so the landing keeps drawing (FOLLOW-UP 2).
- **Unattributed, landing_end over**: r6 +6.400 vs +6.260, r10 +6.067 vs +5.720, r29 +7.467 vs
  +7.340, r43 +4.667 vs +4.430, and r41 +5.533 vs +2.750 (lines 8, 12, 31, 41, 39). An area gap
  cannot make a landing last longer, so B1164 is not named here; not traced on this card
  (FOLLOW-UP 3).

## 4. tour-7 on b1118 (ask 2)

`refactor/B1171/scripts/run_tour7_b1118.sh before|after` (B1166's tour rows; before = fable
with B1166's literal 60, after = this branch with the B1118 leg's level, which is 60 for all
seven) and `refactor/B1171/scripts/compare_tour7.py` -> `refactor/B1171/tables/tour7_b1118.tsv`.
All seven spells: oracle level 60, before 60, after 60, frames byte-identical (435 / 435 / 435
/ 435 / 435 / 444 / 483), summary cells identical. The cells equal B1166's committed tour row
for six spells; 253 reads 4.148 / src 3.323 where B1166's row reads 5.306 / 3.804, B1161's
override retirement again (`refactor/B1169/README.md:145`). The B1142 lane oracle and B1157
columns are carried in the table for reference.

## 5. Preset audit (ask 3)

| preset / gate | harness level before | oracle level | verdict |
|---|---|---|---|
| b1130 fixed_npc sweep (B1155, B1157, B1166 runners) | column 9, class level (1-44) | 60, every capture | WRONG; fixed, re-run (sec. 3) |
| b1130-self fixed_self rows (same runners) | column 9 | 60 | WRONG; fixed, re-run |
| b1118 tour-7 (B1157 `tour`, B1166) | literal 60 | 60, B1118 legs | value right, hard-coded; now read |
| b1118 40-record sweep (B1157 `sweep`) | literal 60 | 60, the B1130 captures | value right, hard-coded; now read |
| B1142 tour gate (live willeq on lane 5) | the server's level after `/say #level 60` | 60, B1118 legs | consistent, no change |

B1142's tour plans run `/say #level 60` (`review/B1142/plans/tour_A.txt:23`,
`review/B1142/plans/tour_B.txt:23`, `review/B1142/plans/tour_C.txt:23`); the client
answered "Welcome to level 60!" (`review/B1142/gate/fable_B/fable.log:4767`) and logged
`level=60` on every tour cast (`review/B1142/gate/fable_A/fable.log:7937`, `:9847`,
`:11874`, `:13545`; `review/B1142/gate/fable_B/fable.log:7115`, `:9006`;
`review/B1142/gate/fable_C/fable.log:7162`). Its `new` columns and the B1157 b1118 table are
therefore level-matched and stay valid.

Fixed the same way (the resolver call replaces column 9 or the literal):
`refactor/B1153/scripts/run_sweep_b1130.sh:53`, `refactor/B1153/scripts/batch_after.sh:106`,
`refactor/B1155/scripts/run_sweep_b1130.sh:54`, `refactor/B1157/scripts/run_sweep_b1130.sh:61`,
`refactor/B1166/scripts/run_sweep_b1130.sh:43`, `refactor/B1157/scripts/run_sweep_b1118.sh:52`
and `refactor/B1166/scripts/run_tour7_b1118.sh:25`. Left as written: the B1150-B1153 tour and
b1118 runners (literal 60, the right value, superseded by the runners above and not re-run).

## 6. Verification

- Targeted ctest on the merged tip (f61fafb4 + this branch), gitlab-runner-2:
  `tbuild <wt> test -R 'FxHarness|B1166TargetAnchor|FxCaptureLevel'` 14/14
  (`/mnt/projects/fable/review/B1171/worker/ctest_targeted_merge2.log`).
- Hygiene: the first run failed DocCounts (`docs/module_map.md` named 49 hygiene tests, the
  new `FxCaptureLevel` makes 50); corrected (`/mnt/projects/fable/review/B1171/worker/hygiene.log`).
- Cite check: `checked N cites, 0 problems` (the MR description gives the count at the tip).

## RULING REQUEST

The b1130 tables below were measured with the caster at the record's class level while the
oracle captures ran `#level 60`; under Will's rule they compare the wrong caster and are no
longer valid references for any landing, p90 or source verdict on the b1130 / b1130-self
cameras. Gate tables are bless-only, so this card does not edit them:

1. `refactor/B1155/banded_table.tsv` (all 40 rows except r28, class level 44 = 60 here).
2. `refactor/B1157/b1130_table.tsv` and `refactor/B1157/b1130_table.tsv.after.tsv` (same rows).
3. `refactor/B1166/tables/b1130_table.tsv` (same rows).

Request: bless `refactor/B1171/tables/b1130_table.tsv.after.tsv` (fable f61fafb4 + this branch,
capture level) as the b1130 sweep reference, and mark the three above superseded. Not
invalidated: `review/B1142/gate/regression_table.tsv` and `refactor/B1157/b1118_table.tsv`
(level 60 on both sides, sec. 5).

## FOLLOW-UP

1. The owners above: B1164 (#483) area / attach scale s, B1172 (#491) target height, B1167
   (#486) r35, B1173 (#492) r7.
2. r40 (602, group teleport): the harness has no caster despawn; the oracle's landing ends with
   the caster's OP_DeleteSpawn. A `--caster-despawn` equivalent of `--target-despawn` would
   make the row comparable.
3. landing_end over with no owner: r6, r10, r29, r41, r43 (sec. 3); r41 is the large one
   (+2.8 s). Needs a trace of the landing lifetime words for these records.
