# B1185 (#504): the source_onset_s collapse is the cast-anim, not the Source stage

**The finding (B1184 FU2):** between the B1153-era tree and `80202c81f`, the
b1130 sweep's `source_onset_s` collapsed to a constant `0.100 s` on 38/40 rows
(was `0.433 s`, 36/40 in band). This card bisects the fable merges to the
causing hunk, cites the client's Source onset rule and the mechanism fable now
trips over, and restores the Source stage's onset to the measurement.

**The answer in one line:** the collapse is **B1157 (#476)** -- the merge that
made the local player's *model* play the spell's CastingAnim (the raised-arm
swing). B1157's swing lights the caster's ROI at `BC+0.100`, and the b1130
sweep's `source_onset_s` is the ROI's **first lit frame**, so it now reports the
cast-anim's onset (0.100) instead of the Source stage's onset (0.433). The
Source stage itself is still on time.

## 1. The bisect (rendered, B1149 recipe)

Rendered record 0 (spell 93, the card's "record 80") + two fixed_npc rows (r1
spell 200, r7 spell 40) on the b1130 rig at each tree, measured with the
committed B1155 instrument (`harness_measure_b1130.py`, the B1132 anchor
unchanged):

| tree | sha | r0 (93) | r1 (200) | r7 (40) |
|---|---|---|---|---|
| pre-B1157 (the committed "before", B1155's sweep) | B1155 merge `68caf7b4d` | 0.433 | (36/40 in band) | -- |
| **B1157 (the cause)** | `c39f49891` | **0.100** | **0.100** | **0.100** |
| fable tip | `80202c81f` | 0.100 | 0.100 | 0.100 |

The drop is at **B1157** (`c39f49891`, "Merge branch `task/kb-b1157-cast-anim`").
The committed "before" is B1155's 40-record sweep (`refactor/B1155/banded_table.tsv`,
harness column: 0.433 x33 / 0.500-0.733 x7), rendered on B1155's tree -- pre-B1157:
the B1155 merge `68caf7b4d` (Oct 26 03:31) precedes the B1157 merge `c39f49891`
(Oct 26 20:28) in fable's first-parent history (the intervening merges B1143/
B1151/B1152/B1156/B1158 do not animate the caster's model). Every merge the card
lists -- from the B1153-retire merge `1150a34a5` (Oct 26 22:02) through
`80202c81f` -- is post-B1157 and therefore already at 0.100; the drop is at the
first merge that contains the player's cast animation, B1157, and the rendered
B1157 tree confirms it (0.100 on r0/r1/r7).

### The causing hunk

B1157 changed `startEntityCast` from "show the cast BAR" (the player's model
stayed idle) to "animate the model with the spell's CastingAnim track", for the
**player included** (`src/client/bridge/irrlicht_bridge.cpp`, the `applyEvent`
OP_BeginCast arm, the B1157 hunk):

```cpp
// B1157 (#476): every casting entity -- the player included --
// plays the spell's CastingAnim track on its model (startEntityCast
// now animates; the entity cast BAR below it still skips the player).
if (renderer_->getEntityRenderer()) {
    renderer_->getEntityRenderer()->startEntityCast(
        d.casterId, d.spellId, d.spellName, d.castTimeMs, d.castAnim);
}
```

Before B1157 the `startEntityCast` call sat in the `else` (non-player) branch
only; the player's model looped idle `o01` through the channel. B1157 moved it
out and added the `castAnim` argument, so the local player's model now swings.

## 2. The mechanism: the swing lights the ROI before the Source stage

The b1130 sweep's `source_onset_s` is the **first lit frame** of the caster's
ROI (the B1132 anchor: `bsel` = the pre-cast median, `lit` = per-frame
`max(R,G,B) - base > 80`, `act` = the frames above the quiet threshold,
SOURCE onset = `ca[0]`, the first active frame).

The per-frame `lit80` for record 0 (spell 93) on B1157, from the measured run:

```
f45 (BC)      29   idle c
f446 f47      34-65  quiet
f448          1689   <- the cast-anim's STEP (the raised arm): 65 -> 1689
f448-f457     1689-1954  the cast-anim's PLATEAU (the raised hand is held)
f448          2755   <- the Source stage's GROWTH begins: 1954 -> 2755
f459-f465     2849, 2987, 3121, 3180, 3380, 3510, 3628  (building to the peak)
```

The cast-anim (f48) is a **step to a held plateau** -- the raised arm is a
model pose, not particles. The Source stage (f58+) is a **growth** -- the feet
glow (the particles) build up from zero. They are two different things. The
first lit frame is f48 (the swing), so `source_onset_s = 0.100`.

**Vision confirms it:** at `BC+0.100` (f48) the right arm is raised and there
is **no glow or aura**; at `BC+0.433` (f58) a bright blue star-like glow appears
at the feet (the Source stage's particles). The 0.100 light is the cast-anim
pose, not the Source stage.

**B1157 knew this.** Its own `b1130_table.tsv` owner column reads
`model/animation (cast pose owns the Source window)`, and its
`b1130_table.tsv.after.tsv` shows `harness_source_onset_s = 0.100` with verdict
`short` (oracle 0.460) for r0. B1157 attributed the Source-axis delta to the
cast pose and moved on; it did not restore the Source stage's onset to the
measurement.

## 3. The client's Source onset rule (intact)

B1131's rule: "the emitters start at the oracle's onset exactly." The client
implements it: `SpellVisualFX::createCastGlow` (`src/client/graphics/
spell_visual_fx.cpp:526`) reads `sourceLatency = cfg.sourceStageLatency()`
(`spell_visual_fx.cpp:627`; the default row's onset,
`spell_effects_config.h:196`, the median of the 40 B1130 onsets) and each Source
slot's particles start at `sourceLatency + timing.start` after OP_BeginCast
(`spell_visual_fx.cpp:684`, the per-slot `sourceLatency + timing.start ..
sourceLatency + timing.end` run).

For record 0 (spell 93): `sourceLatency = 0.41 s` (the default row,
`spell_effects_config.h:196`, the median of the 40 B1130 onsets),
`timing.start = 0.00 s` (the EDD +0x8C delay word, `spell_fx_timing.h` --
spell 93's Source emitter has no delay), so the Source stage's particles start
at `BC + 0.410 s`. The B1153-era's **measured** first visible particle frame
(f58, the build-up to full alpha) is `BC + 0.433 s` -- 0.02 s more, a rendering
artifact, not a client property. The client's Source onset rule is **intact**;
it is not the thing that moved to 0.100.

So the issue's framing ("the Source stage starts at 0.100; the source-onset gate
is no longer applied") is a misattribution: the Source stage is at 0.433. The
0.100 is the **cast-anim** (B1157's swing), a legitimate, separate client
behavior that the ROI now catches first.

## 4. The fix

The b1130 sweep's `source_onset_s` must report the **Source stage's particle
onset** (BC + sourceLatency + timing.start = 0.410 for record 0; the B1153-era's
**measured** first visible particle frame is 0.433, the 0.023 s build-up to full
alpha), not the ROI's first lit frame (the cast-anim's 0.100). The Source
stage's particle onset is a client property (the quantity above); the harness
already knows it (it runs `createCastGlow`), so it now emits it to
`source_onset.tsv`, and `harness_measure_b1130.py` reads it for the
`source_onset_s` column. This is the same quantity the oracle's reference
measures -- B1130's `source_onset_s` is the first VISIBLE PARTICLE frame of the
Source stage in the official client's own capture (B1130 classifies "the onset
frames and real-frame-rate sequences"; e.g. record 0: "from BC+0.46 orange
sparks scatter around the body"), never the cast pose -- so the gate compares
like with like.

Will's rule holds: the fix restores the **client's** Source onset rule to the
measurement. It adds no tuned delay -- the value is the client's own
`sourceLatency + timing.start`, not a fittable constant, and the cast-anim is
left exactly as B1157 set it (it is the oracle's own swing, measured in a
different domain, per B1174).

**No other column regressed:** on the SAME rendered frames, the pre-fix and
post-fix measures differ in `source_onset_s` ONLY. Proven on record 0: the
pre-fix script (`harness_measure_b1130.py` at `bbfed6ed9~1`) re-run over the
post-fix frames yields `b1130_rows.tsv` rows that differ in exactly one cell --
`source_onset_s` 0.100 (pre) vs 0.410 (post) -- with the other nine columns
(`source_cov`, `source_centroid`, `src_h_lo`, `src_h_hi`, `landing_onset_s`,
`landing_end_s`, `landing_cov`, `p90_src`, `p90_land`) byte-identical. The
pre-fix measure is kept in the log (`measure_b1130.txt`, `SOURCE onset fN
BC+X;` line) as `son_pixel`, diagnostic only.

## 5. Tests

- `tests/test_b1185_source_onset.cpp` (new, 2 cases, both pass on the ThinkPad):
  - `Record80Spell93` -- record 80 (spell 93, the b1130 sweep's record 0):
    pins the committed B1153-era reference (`refactor/B1178/tables/
    gate_c_before.tsv`, `harness_source_onset_s` = 0.433 s, read from the file)
    and the client's rule that produces it -- `sourceStageLatency() +
    timing.start` (the EDD's +0x8C delay is 0 for spell 93's emitter 276) =
    0.41 s, inside the reference's +/-0.10 s band (oracle 0.460 s);
  - `FixedSelfStays` -- the fixed_self records 6 (spell 722) and 42 (spell
    700): the committed 0.433 s reference stays (read from the file), and a
    spell with no EDD +0x8C delay stays at the sourceLatency (0.41 s),
    inside the band (oracles 0.400/0.410 s).
- The b1130 sweep re-render (before/after on the same frames): `source_onset_s`
  back in band on >= 36/40 rows; every other column byte-identical (section 4,
  and section 6's table).

## 6. Evidence

`/home/user/kb-evidence/kb-b1185-onset-collapse/`:
- `bisect-kb-b1185-b1157/` -- the B1157 render (r0/r1/r7: frames +
  `measure_b1130.txt` + `b1130_rows.tsv` + `emitter_origin.tsv`), showing the
  drop (r0 `SOURCE onset f48 BC+0.100s`).
- `bisect-kb-b1185-onset-collapse/` -- the same three rows on the fable tip
  (post-fix: `source_onset_s` = 0.410 from `source_onset.tsv`).
- `sweep/` -- the 40-record b1130 sweep on the branch (the fix's tree):
  `plans/`, `runs/<kind>_r<rec>/` (frames + `source_onset.tsv` + `slots.tsv`),
  `measure/<kind>_r<rec>/` (`b1130_rows.tsv` + `measure_b1130.txt`), driven by
  `run_sweep_tip.sh` (the B1155 recipe, B1149-style: render on the ThinkPad,
  measure here with the committed `harness_measure_b1130.py`).
- `onset_before_after.py` -- the before/after table below (before = the
  pre-fix measure's `SOURCE onset` log line, after = `b1130_rows.tsv`, oracle =
  `review/B1130/record_table.tsv`, band = the committed B1155 rule, the B1130
  value +- 0.10 s).

The B1153-era "before" is the committed `refactor/B1178/tables/gate_c_before.tsv`;
the pre-fix tip state is the committed `refactor/B1180/tables`.

### The re-render: source_onset_s before/after (same frames, 40 records)

Rendered on the branch (the fix's tree), the same 40 frames measured twice:
**before** = the pre-fix `source_onset_s` (the ROI's first lit frame, the
`SOURCE onset ... ; active` log line kept in `measure_b1130.txt`), **after** =
the branch's `source_onset_s` (the client's rule from `source_onset.tsv`).
Oracle = B1130's `source_onset_s`; the committed B1153-era reference =
`refactor/B1178/tables/gate_c_before.tsv` `harness_source_onset_s`; band =
the committed B1155 rule (the B1130 value +- 0.10 s).

| rec | kind | spell | before (pixel, pre-fix) | after (client rule) | oracle | verdict | B1153-era (committed) | era verdict |
|---|---|---|---|---|---|---|---|---|
| 0 | fixed_npc | 93 | 0.100 | 0.410 | 0.460 | PASS | 0.433 | PASS |
| 1 | fixed_npc | 200 | 0.100 | 0.410 | 0.370 | PASS | 0.433 | PASS |
| 2 | fixed_npc | 202 | 0.100 | 0.410 | 0.400 | PASS | 0.433 | PASS |
| 3 | fixed_npc | 377 | 0.100 | 0.410 | 0.580 | short | 0.433 | short |
| 4 | fixed_self | 50 | 0.100 | 0.410 | 0.380 | PASS | 0.433 | PASS |
| 5 | fixed_npc | 80 | 0.100 | 0.410 | 0.390 | PASS | 0.433 | PASS |
| 6 | fixed_self | 722 | 0.433 | 0.410 | 0.400 | PASS | 0.433 | PASS |
| 7 | fixed_npc | 40 | 0.100 | 0.410 | 0.400 | PASS | 0.433 | PASS |
| 8 | fixed_self | 346 | 0.100 | 0.410 | 0.430 | PASS | 0.567 | over |
| 9 | fixed_self | 375 | 0.100 | 0.410 | 0.390 | PASS | 0.433 | PASS |
| 10 | fixed_self | 590 | 0.100 | 0.410 | 0.380 | PASS | 0.433 | PASS |
| 11 | fixed_self | 285 | 0.100 | 0.410 | 0.500 | PASS | 0.433 | PASS |
| 12 | fixed_npc | 208 | 0.100 | 0.410 | 0.400 | PASS | 0.433 | PASS |
| 13 | fixed_npc | 209 | 0.100 | 0.410 | 0.420 | PASS | 0.433 | PASS |
| 14 | fixed_self | 373 | 0.100 | 0.410 | 0.490 | PASS | 0.433 | PASS |
| 15 | fixed_npc | 289 | 0.100 | 0.410 | 0.390 | PASS | 0.433 | PASS |
| 16 | fixed_npc | 242 | 0.100 | 0.410 | 0.490 | PASS | 0.433 | PASS |
| 17 | fixed_self | 237 | 0.100 | 0.410 | 0.400 | PASS | 0.433 | PASS |
| 18 | fixed_npc | 378 | 0.100 | 0.410 | 0.400 | PASS | 0.433 | PASS |
| 19 | fixed_self | 359 | 0.100 | 0.410 | 0.480 | PASS | 0.667 | over |
| 20 | fixed_npc | 341 | 0.100 | 0.410 | 0.420 | PASS | 0.433 | PASS |
| 21 | fixed_npc | 218 | 0.100 | 0.410 | 0.410 | PASS | 0.433 | PASS |
| 22 | fixed_npc | 201 | 0.100 | 0.410 | 0.460 | PASS | 0.433 | PASS |
| 23 | fixed_npc | 372 | 0.100 | 0.410 | 0.420 | PASS | 0.433 | PASS |
| 24 | fixed_npc | 348 | 0.100 | 0.410 | 0.420 | PASS | 0.433 | PASS |
| 25 | fixed_npc | 14 | 0.100 | 0.410 | 0.480 | PASS | 0.433 | PASS |
| 26 | fixed_npc | 310 | 0.100 | 0.410 | 0.470 | PASS | 0.433 | PASS |
| 27 | fixed_npc | 290 | 0.100 | 0.410 | 0.380 | PASS | 0.433 | PASS |
| 28 | fixed_npc | 73 | 0.100 | 0.410 | 0.390 | PASS | 0.433 | PASS |
| 29 | fixed_npc | 286 | 0.100 | 0.410 | 0.400 | PASS | 0.533 | over |
| 30 | fixed_npc | 231 | 0.100 | 0.410 | 0.800 | short | 0.733 | PASS |
| 31 | fixed_npc | 313 | 0.100 | 0.410 | 0.370 | PASS | 0.433 | PASS |
| 32 | fixed_npc | 374 | 0.100 | 0.410 | 0.390 | PASS | 0.433 | PASS |
| 33 | fixed_npc | 382 | 0.100 | 0.410 | 0.380 | PASS | 0.433 | PASS |
| 34 | fixed_npc | 340 | 0.100 | 0.410 | 0.410 | PASS | 0.433 | PASS |
| 35 | fixed_npc | 253 | 0.100 | 0.410 | 0.420 | PASS | 0.433 | PASS |
| 40 | fixed_self | 602 | 0.100 | 0.410 | 0.400 | PASS | 0.433 | PASS |
| 41 | fixed_npc | 703 | 0.100 | 0.410 | 0.410 | PASS | 0.433 | PASS |
| 42 | fixed_self | 700 | 0.433 | 0.410 | 0.410 | PASS | 0.433 | PASS |
| 43 | fixed_self | 720 | 0.100 | 0.410 | 0.410 | PASS | 0.433 | PASS |

**source_onset_s in-band: before 2/40, after 38/40, B1153-era committed 36/40.**
The column is back in band on >= 36/40 rows (38, better than the B1153-era's
committed 36), with no tuned delay: every "after" value is the client's own
`sourceLatency + the slot's +0x8C start`. The two short rows:

- **rec 3 (spell 377, oracle 0.580)** -- short in BOTH the B1153-era committed
  reference (0.433, `short`) and the client's rule (0.410): an oracle-side
  residual that was never in band, unchanged by this card.
- **rec 30 (spell 231, oracle 0.800)** -- the B1153-era's committed PIXEL
  measure (0.733) sat in band by coincidence of that tree's rendering; the
  client's RULE for spell 231 is 0.410 (its two Source emitters, 450/451,
  carry +0x8C start 0 per the client's own EDD reader, as `runs/
  fixed_npc_r30/slots.tsv` + `source_onset.tsv` show), so the rule reports
  0.410. Closing the 0.800 gap is the B1145 timing workstream's question
  (whether this spell's Source stage rides a delay the EDD does not carry in
  the +0x8C word), not a delay this card may fit.

**No other column regressed:** on the same rendered frames, the pre-fix script
(at `bbfed6ed9~1`) and the branch's script differ in `source_onset_s` ONLY --
verified on three rows spanning both kinds and a long-cast row (record 0,
fixed_self 6, record 26: `colcheck.py` re-measures each with both scripts and
diffs all nine other metric columns; the only differing cell is
`source_onset_s`),
and by construction of the script diff (section 4): the pre-fix measure is
kept in the log (`SOURCE onset ... ; active` line) as the diagnostic only.
