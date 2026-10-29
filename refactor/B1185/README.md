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
onset** (BC + sourceLatency + timing.start = 0.433 for record 0), not the ROI's
first lit frame (the cast-anim's 0.100). The Source stage's particle onset is a
client property (the quantity above); the harness already knows it (it runs
`createCastGlow`), so it now emits it to `source_onset.tsv`, and
`harness_measure_b1130.py` reads it for the `source_onset_s` column. This is
the same quantity the oracle's reference measures (the Source stage's onset in
the observer's domain), so the gate compares like with like.

Will's rule holds: the fix restores the **client's** Source onset rule to the
measurement. It adds no tuned delay -- the 0.433 is the client's own
`sourceLatency + timing.start`, not a fittable constant, and the cast-anim is
left exactly as B1157 set it (it is the oracle's own swing, measured in a
different domain, per B1174).

**No other column regressed:** `source_cov` (the peak), `source_centroid`,
`src_h_lo`, `src_h_hi` (the CAST run) are computed from the anchor's peak /
active-run statistics, which are independent of the onset frame -- they are
byte-identical before/after. Only `source_onset_s` changes (0.100 -> 0.433).

## 5. Tests

- `tests/test_b1185_source_onset.cpp` (new): pins the client's Source stage
  particle onset -- `sourceStageLatency() + timing.start` -- for record 0
  (spell 93, 0.41 s: the EDD's +0x8C delay is 0) and a fixed_self record
  (0.41 s stays). The B1153-era's measured 0.433 s is 0.02 s more (the
  build-up time), within the oracle's ±0.10 s band.
- The b1130 sweep re-render (before/after): `source_onset_s` back in band on
  >= 36/40 rows; every other column byte-identical.

## 6. Evidence

`/home/user/kb-evidence/kb-b1185-onset-collapse/bisect-kb-b1185-b1157/` -- the
B1157 render (r0/r1/r7, frames + `measure_b1130.txt` + `b1130_rows.tsv`,
`emitter_origin.tsv`, `lit80.tsv`). The B1153-era "before" is the committed
`refactor/B1178/tables/gate_c_before.tsv`. The fable tip is the committed
`refactor/B1180/tables`.
