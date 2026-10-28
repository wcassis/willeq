# B1184 (#503): the gate on ONE tree vs ONE reference set

**Tree (one):** `fable` tip **a99b3822** — the B1180 merge (B1180 #499 width factor,
B1172 #491 target scale, B1166 #485 anchor, B1164 #483 attach scale `s`, level 60,
B1177 #496 solver, B1170 #489 missile object), plus the post-B1180 merges
(B1182, B1167/B1170/B1179 merge-fix legs). Rendered with the tip's
`model_viewer` (built by `rbuild`, never on this host — B1087).

**Reference set (one):** the **B1178** references — the pinned-camera (b1118 rig)
tour references in `tests/spell_fx_population_oracle.h` (`kTour`, the B1178 v3
values), the regenerated B1130 `record_table.tsv` anatomy for the 40-record
b1130 sweep, and the committed `refactor/B1153/tables/gate_b.tsv` for the
40-record b1118 sweep (B1178 did not re-reference the sweep). The judgment rule
is `refactor/B1178/scripts/gate_after.py` (itself the committed form of
`refactor/B1155/scripts/band_table.py`): source_cov/landing_cov ±3 % relative,
p90_src/p90_land ±15 % relative, landing_end_s/source_onset_s ±0.10 s, the rest
±0.15 — copied verbatim into `scripts/build_tables.py` (this card's evidence
scripts, outside the repo).

**Why this gate.** B1180's re-gate (README sec. 3) was judged against the
PRE-B1178 references — its own words: "the AFTER counts against the B1178
references are the next gate's job". B1178 then swapped the references and
judged the OLD fable values against them (zero flips on b1130; the five tour
flips). This card is the first gate that renders the CURRENT tip and judges it
against the B1178 references with the committed scripts. One AFTER label: this
table set.

## Method (committed scripts, no harness edits)

Render on the ThinkPad (`model_viewer --spell-test ... --headless`, tour/b1118
at `--camera b1118`, b1130 at the B1155 rig), frames streamed per row to
`/mnt/scratch/kb-evidence/kb-b1184-gate-tip/` (never `~`), measured HERE with
`review/B1139/scripts/harness_measure.py` +
`refactor/B1155/scripts/harness_measure_b1130.py` exactly as
`refactor/B1149/scripts/run_scene.sh` drives them. Plans: the card's recipe
(`t_begin_s 1.5, cast_ms 1500, caster_level 60` — the oracle caster's level,
the B1180 level requirement, so spell availability matches the captures);
identical recipe on every row (spot-verified), and the `d_*` columns below
show it is the trees, not the plans, that differ.

**The judge is pinned.** `scripts/build_tables.py:verify()` re-derives two
committed results from the same band rule before it accepts the run:
(a) b1130 — judging `refactor/B1153/tables/gate_c.tsv`'s own
oracle_*/harness_* pairs reproduces the committed `verdict_*` cells with a
0-cell diff and **104 PASS** total (B1178 Ask 3: "the committed table ...
reproduces the 104 PASS / 0 gates"); (b) tour — judging the B1142
`new_*` fable values against `kTour` reproduces the B1178 (b) table's
"vs NEW ref (v3)" column (the five flips: 38/656/41 end, 458 end, 253 land;
`refactor/B1178/README.md` lines 353-363). Both held with 0 errors on this run.

All 87 rows (tour-7: 7, b1130: 40, b1118: 40) rendered and measured; no row
refused, no substitution. No golden, manifest, baseline, preset or harness
value was touched — the harness is untouched (nothing in
`src/`, `include/`, `tests/` changes in this card).

## Tables (committed under `refactor/B1184/tables/`)

- `tour.tsv` — 7 rows; oracle (B1178 v3 `kTour`), mine, verdict per
  land_cov/end/roi_l/roi_s, the B1180-after values + per-row deltas, red list,
  owner.
- `b1130.tsv` — 40 rows; the nine metrics (source_cov, landing_cov,
  landing_end_s, p90_src, p90_land, source_onset_s, source_centroid,
  src_h_lo, src_h_hi) as oracle/mine/verdict, plus B1180-after + delta on all
  nine, red list, owner.
- `b1118.tsv` — 40 rows; the committed sweep gate (zero-width bands against the
  B1151 baseline, `gate_b.tsv`), mine + verdict per metric, B1180-after + delta
  on land_cov/end/src_cov, red list, owner.
- `counts.tsv` — per table x column: BEFORE (the B1178-AFTER state) and NOW
  (this tip, this run) PASS/short/over/n-a.
- `distance.tsv` — rows with no red cell (n-a cells neutral), per table,
  before / now.

"BEFORE" per table: b1130 = the committed `gate_c.tsv` verdicts (B1178 proved
the reference re-measure flips zero cells, so the committed table IS the
B1178-AFTER table); tour = B1142 `new_*` values judged against `kTour` (the
B1178 (b) table, re-derived — pinned by `verify()`); b1118 = the committed
`gate_b.tsv` verdicts (B1178 never judged the sweep).

## Per-column results (BEFORE = B1178 AFTER state, NOW = tip a99b3822)

```
b1130            before PASS/short/over/n-a      now PASS/short/over/n-a
source_cov       0 / 34 / 6 / 0                  4 / 6 / 30 / 0
landing_cov      0 / 38 / 1 / 1                  0 / 38 / 2 / 0
landing_end_s    2 / 35 / 2 / 1                  10 / 26 / 4 / 0
p90_src          10 / 10 / 20 / 0                23 / 7 / 10 / 0
p90_land         8 / 11 / 19 / 2                 18 / 5 / 16 / 1
source_onset_s   36 / 1 / 3 / 0                  2 / 38 / 0 / 0
source_centroid  20 / 2 / 18 / 0                 27 / 0 / 13 / 0
src_h_lo         14 / 1 / 25 / 0                 28 / 2 / 10 / 0
src_h_hi         14 / 16 / 10 / 0                27 / 2 / 11 / 0

b1118 (zero-width regression net vs the B1151 baseline; B1178 did not
       re-reference this sweep, so "before" is the committed gate_b state)
land_cov         32 / 3 / 5 / 0                  6 / 4 / 30 / 0
end              26 / 1 / 5 / 8                  11 / 10 / 11 / 8
src_cov          38 / 0 / 2 / 0                  2 / 5 / 33 / 0
roi_l            32 / 5 / 3 / 0                  7 / 8 / 25 / 0
roi_s            36 / 2 / 2 / 0                  6 / 11 / 23 / 0

tour             before                          now
land_cov         0 / 5 / 2 / 0                   0 / 7 / 0 / 0
end              2 / 3 / 1 / 1                   1 / 1 / 4 / 1
roi_l            7 / 0 / 0 / 0                   3 / 4 / 0 / 0
roi_s            3 / 2 / 2 / 0                   3 / 2 / 2 / 0
```

Per-row values and deltas: the tables above — recomputed by the script from
the measure files, never retyped. The `d_*` columns (mine minus B1180's after
column) show what THIS tip's tree (the B1180 merge's width-factor pin, the
post-B1180 merges) moved, since B1180's own re-gate rendered its pre-merge
branch 80202c81f: `source_onset_s` is d = 0.0 on all 40 b1130 rows (the
collapse is pre-B1184 — FU2); the other b1130 columns are zero on 32-36 rows
and move on eight (r0/93, r1/200, r2/202, r6/722, r7/40, r18/378, r25/14,
r26/310), at most landing_cov +0.237 (93) and landing_end +0.700 (spell 40);
the b1118 d columns move on six rows of the same family (r0/93, r1/200,
r7/40, r18/378, r25/14, r26/310), with |d| at most 0.415 (land_cov, r26/310)
and 0.367 (end, r7/40); the tour d columns move on 93/38 (land_cov +0.135,
2.905 -> 3.040; end -0.133, 6.633 -> 6.500).

## Owners (every red cell; mechanisms from the evidence, not guesses)

**landing_cov — 38 short / 2 over (b1130), all 7 short (tour). Owner: B1184-FU1
(NEW, the window-method unit).** The landing population is NOT under-applied:
record 80 (spell 93) is drawn at its full file envelope — burst 50 (+0x80) +
per 50 (+0x84) x tps 30 (+0x88) over the 0.75 s window (+0x78 1.00 s minus
+0x8C 0.25 s), each particle +0x7C 1.50 s — 1175 live at peak (B1149
`dump_pop.py`'s envelope; the tick rule pinned at 0x10060e8b..0x10060ffd in
`src/client/graphics/sim_particles.cpp:429-443 (the burst stands for tick 0)`,
the emitter deactivates at its window in
`src/client/graphics/sim_particles.cpp:344-350 (emitter.active = false;)`).
The residual is the measurement domain: the harness window is
per-target-height^2 (the anchor-fitted target's height), the oracle reference
the oracle target's height (B1171's attribution, carried by B1180 README
sec. 3). The 93 chain, measured: 1.993 (B1157, no `s`) -> 2.719 (B1164 `s`
= 1.2) -> 2.905 (+ B1172 target scale; 0.50x of the old ref 5.847) -> 3.040
(+ B1180 width factor; 0.68x of the B1178 ref 4.479). r31 (3.835 vs 3.668) and
r40 (0.876 vs 0.157) are over: r40 is the committed harness-scene row (the
oracle caster zones out, `--caster-despawn 0.80`, B1171's committed owner);
r31 keeps the B1164/B1172 chain owner.

**landing_end_s — 26 short / 4 over (b1130); tour 41 short, 54/93/458/656
over. Owner: B1179 #498.** B1179 proved no lifetime-word defect: the end is
the last frame above the lit-pixel threshold, so it moves with the landing
extent, not with a word. The measured coupling on this tip: tour 93 fable
6.500 vs oracle 4.994 (+1.506 s; record 80's last particle dies at
T + L = 6.5 s from the em282 slot — the fable end sits AT the nominal death,
the oracle's end earlier in the same particle's fade), 54 6.333 vs 5.045
(+1.288), 656 6.333 vs 6.033 (+0.300), 458 5.117 vs 4.697 (+0.420), 41
5.700 vs 6.637 (-0.937), and 38 6.500 vs 6.566 is now IN BAND
(-0.066 — the one tour end flip, short -> PASS, since B1178 (b)). b1130: the
column improved 2 -> 10 PASS (the end follows the landing extent as the
sprite size landed).

**source_onset_s — 38 short (b1130). Owner: B1184-FU2 (NEW, the onset
collapse).** Measured across the three gates: the committed B1153-era
(`gate_c.tsv`) has the per-row distribution 0.433x33 / 0.500-0.733x7 (36/40
in band vs the oracle's 0.370-0.580 on 39 rows, one at 0.800); B1180's
re-gate (its `b1130_band.tsv`, tree 80202c81f) shows a constant 0.100 s on
38/40 rows with only rec 6 and 42 (fixed_self, spells 722/700) keeping 0.433;
B1184's tip keeps that exact split (d = 0.0 on every row; the plans are the
identical recipe). 0.100 s = 3 frames at 30 fps: the source glow now starts
on a constant post-click offset, not on the per-row cast track (the oracle's
onset varies per row, the caster's cast track — `refactor/B1174/README.md`:
the oracle draws the CastingAnim from Act+0.42 and fable now plays that
track on its standing source). The collapse predates B1184 (B1180's README
reports only land/end/p90, so it was unowned there). FU2 traces which card
between B1155 and B1180 re-attached the Source stage (candidates: B1174 #493
OP_Action cast-anim gate, B1152 stage-fire timing, B1169 harness
determinism) and restores the per-row onset.

**source_cov — 30 over (b1130). Owner: B1164 #483 `s` + B1180 #499 (sprite
size).** The direction flipped since B1153 (then 34 short / 6 over): the
sprite-size chain (attach scale 1.2, target scale, width factor — all in this
tree) enlarged the source stage past the oracle's coverage on most rows. The
same chain owns the p90 reds below.

**p90_src / p90_land — b1130: p90_src 7 short / 10 over, p90_land 5 short /
16 over (+ 1 n-a). Owner: B1164 #483 `s` + B1180 #499 (sprite size; the ~0.75x
landing residual on 93 is the same chain's residual after the width pin).**
The r35 (spell 253) row's p90 reds keep their committed owner B1167 #486.

**source_centroid — 13 over (b1130). Owner: B1157 (cast-anim residual: the
raised hand moves the Source window's centroid — fable runs +0.15 to +0.46
window-normalized above the oracle on the 13 red rows, e.g. 93 0.91 vs 0.71)
+ B1166 (anchor).** Unchanged vs B1180 (d = 0.0 on every measured row):
pre-B1184 drift, not this tip's.

**src_h_lo / src_h_hi — b1130: src_h_lo 2 short / 10 over, src_h_hi 2 short /
11 over. Owner: B1157 (the committed "blank `src_*_n` + the NVIDIA
re-baseline" attribution — the B1184 card's named owner).** Both columns
improved (14 -> 28 and 14 -> 27 PASS) as the sprite size landed; the residual
is the source window's vertical extent (the 24 red cells sit +0.15 to +0.64
above the oracle — the window is taller — e.g. 93 hi 1.70 vs 1.25, 18 hi
1.72 vs 1.08 — or -0.18 to -0.21 below on the four short cells, r13/r29/r35/
r42).

**b1118 sweep — 34/40 rows red somewhere (zero-width net).** This sweep's
committed gate is a zero-width regression check against the B1151 baseline
(`gate_b.tsv` band column `in +0.000..+0.000`): red means "the value moved
since B1151's run", which is the expected accumulated post-B1151 chain, not a
per-card regression. Every red row's owner is that chain — B1157 (cast-anim),
B1164 (`s`), B1166 (anchor), B1170 (missile), B1172 (target scale), B1177
(solver), B1180 (width factor) — and the `d_*` columns (vs B1180's after
column) show B1184's tip's own contribution is small and confined to six rows
of the 93/200/40/378/14/310 family (|d| <= 0.415; the d_* paragraph above).
r35 (spell 253, self-cast): land_cov 5.297 (B1151 baseline) -> 4.23 (B1180's
re-gate, kept on this tip, d = 0.0) — B1180's unattributed self-landing
regression, owned there as the next unit to touch the self-landing window
(not `s`, which enlarges self sprites).

No row in any table reads "unowned".

## Distance to the finish line

Rows with no red cell (n-a neutral), per table, before (B1178 AFTER state) /
now (tip a99b3822) — `distance.tsv`:

```
table   rows   before   now
b1130   40     0        0
tour    7      0        0
b1118   40     32       2     (zero-width net vs B1151: the 30-row move is the
                                 post-B1151 chain, see the sweep owner above)
```

The per-column movement (`counts.tsv`) is the real finish-line picture: the
sprite-size chain (B1164/B1172/B1180) landed — landing_end_s 2 -> 10 PASS,
p90_src 10 -> 23, p90_land 8 -> 18, source_centroid 20 -> 27, src_h_lo
14 -> 28, src_h_hi 14 -> 27, and tour end 38 flipped short -> PASS. What
holds the tables open is two measurement-domain units and one timing
regression: **landing_cov (38/40 rows, 0.68x on 93) -> FU1 (the window
method)**; **source_onset_s (38/40 rows, the 10-frame collapse) -> FU2**;
and the tour landings all sit short of the B1178 v3 references (the same
window domain — the pinned-camera landings are the longer ones). When FU1
bands the harness window to the capture's own and FU2 restores the per-row
source onset, the b1130 table's red set is the source_cov/p90 sprite-size
residual and the b1118 net returns to green-by-construction.

## Ledger clips (93, 38, 253)

Published at `/home/user/kb-evidence/VISUAL-FX/b1184-gate/` (clips/,
specs/, index.tsv, index.html) via the B1158 pipeline
(`refactor/B1158/scripts/make_clip.py` + `make_index.py`, spec schema of
`refactor/B1158/scripts/ledger_inventory.py`): oracle LEFT = the B1178 v3
pinned-camera official-client captures (`review/B1178/worker/measure-v3-r3`:
93/253 in `b1118ref_dru5`, 38 in `b1118ref_wiz5`), willeq RIGHT = this gate's
tour renders (same b1118 rig, tip a99b3822), aligned at BeginCast by the
stated frame rule (index.tsv: oracle frames 75/120 or 75/105 or 75/120 BC/Act
vs willeq 45/90/98 — the clip window is BC-1.0..Act+8.0). All three strips
verified: the caster is visible on both sides, the framing matches, and the
effects are present on both sides. The index verdicts match `tour.tsv`
(93: Landing=short; End=over. 38: Landing=short; End=PASS. 253:
Landing=short; End=n-a).

## Acceptance status

- **(A)** 87/87 rows rendered on the tip and judged against the B1178
  references by the committed band rule — done (gate.all.log:
  "rows=87 measured_total=87"; `verify()` 0 errors).
- **(B)** per-column counts + per-row deltas recomputed by script, tables
  committed under `refactor/B1184/tables/` — done.
- **(C)** every red cell owned with a stated mechanism + a number; two new
  follow-ups named (B1184-FU1 the window-method unit, B1184-FU2 the source
  onset collapse) — done (the owner column of every table).
- **(D)** three ledger clips published (93/38/253), oracle left / willeq
  right, caster visible on both sides — done (`/home/user/kb-evidence/VISUAL-FX/b1184-gate/`).
- **(E)** README cite-checker clean, hygiene + CI green, harness untouched —
  the gate scripts live in the evidence dir (outside the repo); the repo
  changes are `refactor/B1184/` only (this README + the tables).
- **(F)** MR "Closes #503".
