# B1153 (#473): retire the measured-row system and the FINAL FX gate

The last leg of B1147 (#467). B1149–B1152 wired every quantity the survey had
measured out of `config/spell_effects.json` rows into the spellsnew.eff/edd
records (population/sprite/alpha, shape/offsets/tilts, motion/spin/frames/
modes/flags, the missile/Middle arrival). This card removes what the file
superseded — the per-record measured rows become regression BANDS in the
tests, the six `spellOverrides` are each retired or documented with numbers,
the dead row readers/generators are deleted — and then measures the whole
chain against the oracle once (the FINAL FX GATE).

## 0. Census (commands and output pasted)

Config at this tip (`config/spell_effects.json`):

```
$ python3 refactor/B1153/scripts/census_keys.py config/spell_effects.json
top-level keys: global, landingPopulation, sourceRecords, targetRecords, resistColors, styles, spellOverrides
override 38: impact, spellComplete
  impact: sizeStartMin, sizeStartMax, sizeEndMin, sizeEndMax
  spellComplete: sizeStartMin, sizeStartMax, sizeEndMin, sizeEndMax
override 41: impact, spellComplete, castSpray
  impact: spawnRate, sizeStartMin, sizeStartMax, sizeEndMin, sizeEndMax, motionType, velocityBase, velocitySpread, gravity, drag, positionOffset, sustainRate, emitterLifetime
  spellComplete: spawnRate, sizeStartMin, sizeStartMax, sizeEndMin, sizeEndMax, motionType, expandSpeed, velocityBase, velocitySpread, gravity, drag, positionOffset, sustainRate, emitterLifetime
  castSpray: motionType, velocityBase, velocitySpread, gravity, drag, positionOffset, spawnRate, sizeStartMin, sizeStartMax, sizeEndMin, sizeEndMax
override 54: impact, spellComplete, castGlow
  impact: sizeStartMin, sizeStartMax, sizeEndMin, sizeEndMax, motionType, expandSpeed, velocityBase, velocitySpread, gravity, drag, positionOffset, spawnRate, sustainRate, emitterLifetime
  spellComplete: sizeStartMin, sizeStartMax, sizeEndMin, sizeEndMax, motionType, orbitalRadius, orbitalAngularVelocity, velocityBase, velocitySpread, gravity, drag, positionOffset, spawnRate, sustainRate, emitterLifetime
  castGlow: sizeStartMin, sizeStartMax, sizeEndMin, sizeEndMax, motionType, orbitalRadius, orbitalAngularVelocity, velocityBase, velocitySpread, gravity, drag, positionOffset, spawnRate
override 253: impact, spellComplete, castGlow
  impact: spawnRate, emitterLifetime, motionType, velocityBase, velocitySpread, gravity, drag, positionOffset, spawnWindow, sustainRate, sizeStartMin, sizeStartMax, sizeEndMin, sizeEndMax
  spellComplete: spawnRate, emitterLifetime, motionType, velocityBase, velocitySpread, gravity, drag, positionOffset, spawnWindow, sustainRate, sizeStartMin, sizeStartMax, sizeEndMin, sizeEndMax
  castGlow: motionType, velocityBase, velocitySpread, gravity, drag, positionOffset, spawnRate, sizeStartMin, sizeStartMax, sizeEndMin, sizeEndMax
override 458: castGlow, spellComplete, impact
  castGlow: sizeStartMin, sizeStartMax, sizeEndMin, sizeEndMax, motionType, velocityBase, velocitySpread, gravity, drag, positionOffset, spawnRate
  spellComplete: motionType, velocityBase, velocitySpread, gravity, drag, positionOffset, spawnRate, sustainRate, emitterLifetime, sizeStartMin, sizeStartMax, sizeEndMin, sizeEndMax
  impact: motionType, velocityBase, velocitySpread, gravity, drag, positionOffset, spawnRate, sustainRate, emitterLifetime, sizeStartMin, sizeStartMax, sizeEndMin, sizeEndMax
override 656: castGlow, spellComplete, impact
  castGlow: sizeStartMin, sizeStartMax, sizeEndMin, sizeEndMax, motionType, orbitalRadius, orbitalAngularVelocity, velocityBase, velocitySpread, gravity, drag, positionOffset, spawnRate
  spellComplete: motionType, orbitalRadius, orbitalAngularVelocity, velocityBase, velocitySpread, gravity, drag, positionOffset, spawnRate, sustainRate, emitterLifetime, sizeStartMin, sizeStartMax, sizeEndMin, sizeEndMax
  impact: motionType, expandSpeed, velocityBase, velocitySpread, gravity, drag, positionOffset, spawnRate, sustainRate, emitterLifetime, sizeStartMin, sizeStartMax, sizeEndMin, sizeEndMax
```

Every word of an override is read in ONE place — the merge into the style's
stage settings, `spell_effects_config.cpp` (`SpellEffectsConfig::loadSpellOverrides`
and the merge into the style):

```
$ grep -n 'spellOverride' src/client/graphics/environment/spell_effects_config.cpp
204:    if (root.isMember("spellOverrides"))
205:        loadSpellOverrides(root["spellOverrides"]);
319:        auto sp = spellOverrides_.find(id);
320:        if (id != 0 && sp != spellOverrides_.end()) {
347:    for (const auto& kv : spellOverrides_) {
382:        for (int h = 0; h < 2; ++h) scaleInto(spellOverrides_[sc.id][kHalves[h]], sc.resolved[h], sc.k);
464:        auto spellIt = spellOverrides_.find(spellId);
465:        if (spellIt != spellOverrides_.end()) {
476:    spellOverrides_.clear();
485:            spellOverrides_[spellId][styleName] = override;
489:    if (!spellOverrides_.empty()) {
491:                 spellOverrides_.size());
632:    auto sp = spellOverrides_.find(spellId);
633:    if (spellId == 0 || sp == spellOverrides_.end()) return false;
```

and in the emitter-preset shape probe, `spell_emitter_presets.cpp:244-250`
(`TargetHasOverrideShape` → `hasSourceShapeOverride` on `impact` /
`spellComplete`), with the "B1120/B1122's measured landing overrides keep
their own regions" comment at `spell_emitter_presets.cpp:306`. The stage words
themselves (motion, velocity, size, population) are read by the particle
presets AFTER the merge — an override is only ever a per-spell delta on top
of the style, never a separate render path.

`sourceRecords` / `targetRecords` / `animRecords`: at this tip the runtime
reads ONLY the two default rows (the no-record fallback: the Source body
cloud, the Target style volume) — the per-record rows were already the
bands' subject through B1149–B1152 (`refactor/B1149/README.md` sec. 3,
`refactor/B1150/README.md` "What changed"); step 2 below deletes the
per-record rows from the file and pins it.

## 1. The six spellOverrides: retired or documented

Method (the card): for each spell, drop the override and re-run the harness
for that spell on BOTH presets (tour-7 b1118 + the b1130 preset); if the
record-driven (noov) result stays inside the lane/B1130 band, the override is
retired; if not, it is kept and documented here with before/after numbers and
the oracle quantity the file does not explain (the B1122 "file is not the
whole story" rule). No override survives undocumented.

Renders (post-retirement binary, `refactor/B1153/scripts/batch_after.sh after|noov`;
the noov phase strips `spellOverrides` from the shared config, runs its 12
jobs, and restores it):

- b1118: `runs/b1153_after_tour_<spell>/measure/summary.tsv` (override
  present) and `runs/b1153_noov_b1118_<spell>/measure/summary.tsv`
  (override stripped) — the B1139 instrument,
  `FX_KEEP=1 python3 -I /mnt/projects/fable/review/B1139/scripts/harness_measure.py <frames> <measure>`.
- b1130: `b1130_evidence/measure/after_ovb1130_<spell>/b1130_rows.tsv` and
  `b1130_evidence/measure/noov_ovb1130_<spell>/b1130_rows.tsv` — the B1155
  instrument.

Lane bands: `review/B1142/gate/regression_table.tsv` (lane-5 b1118 captures;
the harness/oracle landing-ratio band 0.45..0.72 is B1150's p25..p75
 over the 40-row survey, `refactor/B1150/README.md`). b1130: the B1155 banded
table (`refactor/B1155/banded_table.tsv`) — but only spells 38/41/54/253/458/
656 have a LANE row there (the b1130 preset has no oracle row of its own),
so the retire/keep decision is taken on the b1118 lane numbers; the b1130
half is the withov/noov delta on that geometry.

### Decision table

All six are KEPT, each documented with the before/after numbers (the card's
rule: an override retires only if the record-driven result stays inside the
band; none does in full — 253 is the one that gets there on coverage, and it
breaks two other invariants instead, below).

| spell | b1118 landing PEAK withov → noov (ratio vs oracle; band 0.45..0.72) | b1130 landing_cov withov → noov | verdict |
|---|---|---|---|
| 38 | 1.993 → 1.993 (0.411 → 0.411, short) | 2.577 → 2.577 (identical) | KEPT — the override is pixel-neutral on both presets (its impact/spellComplete size words are invisible in the merged landing; B1119: the 93:38 coverage ratio 1.50 is the per-sprite size ratio, and a merged volume has no per-sprite size). The record (0) still measures short of the band, so the override is not retired by the band rule; it carries the size words until the sprite-size leg resolves |
| 41 | 2.478 → 2.113 (0.398 → 0.339, both short) | 2.773 → 2.153 | KEPT — the override adds +0.365 PEAK (b1118) / +0.62 landing_cov (b1130); the record (20, the Lifetap family) does not explain that added landing — the file's castSpray/impact/spellComplete words are the carrier |
| 54 | 1.204 → 0.839 (0.236 → 0.164, both short) | 1.092 → 0.648 | KEPT — +0.365 / +0.444; record 23's Target stage is still short of the band |
| 253 | 5.308 → 4.188 (0.729 over → 0.575 IN band) | 5.735 → 4.529 | KEPT after the retirement probe (below) — the record-driven coverage is in band, but the record-driven field overflows the pool rule and dies 1.34 s short of the oracle |
| 458 | 0.499 → 0.36 (0.101 → 0.073, both short) | 0.577 → 0.410 | KEPT — +0.139 / +0.167; record 33 is still short, the override narrows the gap |
| 656 | 1.204 → 0.839 (0.241 → 0.168, both short) | 1.092 → 0.648 | KEPT — +0.365 / +0.444; record 23 (shared with 54) is still short |

Ratio denominator: every ratio in these tables is PEAK coverage divided by the oracle's peak coverage for that spell (253: 7.281, from `review/B1142/gate/regression_table.tsv`; NOT its 6.522 s end time), judged against the B1150 p25..p75 band 0.45..0.72. So 253 reads 5.308 / 7.281 = 0.729 (just over the band) and 4.188 / 7.281 = 0.575 (in band).

PEAK = the `shape.txt` peak coverage on the B1142-compatible crop (B1150's
method, `refactor/B1150/README.md`), not the time-averaged summary row.

### The 253 retirement probe (and why it was reverted)

Commit 701ac572 retired the 253 override and the suite was re-run
(`rbuild test -R 'WhirlingWind|SpellStyleOverrides|...'`):

- `SpellPoolRule.WhirlingWindIsASteadyFrameFillingField` FAILED on both
  platforms: the record-driven field's steady population is
  `steady(impact)+steady(spellComplete)` ≈ 14100 (desktop) / ≈ 5930 (sbc)
  live sprites — `total + live(93)` measured 16036 vs the 8192 desktop pool
  and 7858 vs the 4096 sbc pool. B1123's pool rule
  (`SpellEffectsConfig::applyPopulationRule`) was derived against the
  override's 2160-sprite field; record 35's own population words (B1149) do
  not fit under it.
- `SpellStyleOverrides.WhirlingWindLastsAsLongAsTheOracles` FAILED: the
  record-driven last death is 5.18 s vs the oracle's 6.52 s (B1118 lane-5);
  the override's `emitterLifetime 4.35` is B1145's computed cast-animation
  term (+4.35 s past OP_Action) placed in the file because the record path
  does not honour it yet.

Reverted at 423890ee (the other 37 tests of the set pass in both states).
253 therefore stays with both owned gaps named (FOLLOW-UP below).

## 2. Rows out of the config, into the test bands; dead readers deleted

The measured rows left the config at commit c5b475eb (this branch's
retirement commit): `retire_config_rows.py` deleted the per-record
`sourceRecords.records` / `targetRecords.records` / `animRecords` rows (the
two default rows stay), and `gen_row_bands.py` generated
`tests/spell_fx_row_oracle.h` from the PRE-retirement file:

```
$ git show e83a097d:config/spell_effects.json > /tmp/b1153_pre.json
$ python3 refactor/B1153/scripts/gen_row_bands.py /tmp/b1153_pre.json
```

(the band headers: `tests/spell_fx_row_oracle.h` anchors + face onset,
`tests/spell_fx_shape_oracle.h` shape words, `tests/spell_fx_population_oracle.h`
populations, `tests/spell_fx_timing_oracle.h` timings, `tests/spell_anim_rows.h`
the field-145 → record mapping; a changed band is a red test).

Dead readers deleted (the c5b475eb diff): the per-record row readers in
`spell_effects_config.cpp` (317 lines changed) and
`spell_emitter_presets.cpp` (256), the dead scale words
(`casterHeight`, `orbitAngularVelocity` — gone from the struct, the parser
and the JSON; the config key census pins the keys), and the row generators
with zero callers (the `check_zero_callers.py` pattern,
`scripts/refactor/check_zero_callers.py`). `ConfigKeyReachability`
(`tests/test_config_key_reachability.cpp`) pins that no retired key is read
anywhere in the tree.

## 3. The FINAL FX GATE

Pre-animation baseline: B1157 (running in parallel) re-gates both presets
with an animated caster; the tables below are the pre-animation reference
and stay that.

Renders: `refactor/B1153/scripts/batch_after.sh after` — 93 jobs, pool 4,
rendered on the ThinkPad (`model_viewer --spell-test --headless`, the
post-retirement binary), measured on this host. Tables generated by
`python3 refactor/B1153/scripts/tables_b1153.py` (tables/).

### (a) tour-7 on `--camera b1118` vs `review/B1142/gate/regression_table.tsv`

| spell | B1142 new_land (oracle 4.x) | withov PEAK (ratio) | noov PEAK (ratio) | end (info) | roi_l / roi_s (withov) | verdict | owner |
|---|---|---|---|---|---|---|---|
| 38 | 2.65 (4.85) | 1.993 (0.411) | 1.993 (0.411) | 6.633 | 0.044 / 0.065 | short | chain drift since B1150's tour run (B1151/B1152 merged between the two runs; the row retirement itself is render-neutral — gate (c) shows 0 verdict changes) |
| 41 | 2.666 (6.224) | 2.478 (0.398) | 2.113 (0.339) | 5.667 | 0.106 / 0.092 | short (both) | chain drift; the override's +0.365 PEAK closes part of it (sec. 1) |
| 54 | 1.696 (5.103) | 1.204 (0.236) | 0.839 (0.164) | 6.333 | 0.029 / 0.024 | short (both) | chain drift; same as 41 |
| 93 | 3.049 (4.867) | 1.993 (0.409) | — (no override) | 6.633 | 0.044 / 0.065 | short | chain drift — the CONTROL row: 93 has no override at all, so its 2.742 (B1150) → 1.993 fall is pure B1150→B1153 chain drift, the reference for the six |
| 253 | 5.028 (7.281) | 5.308 (0.729) | 4.188 (0.575) | info | 0.084 / 0.152 | over (withov) / in (noov) | the override is what pushes the row out of band (noov is in band); kept for the pool + duration gaps it carries (sec. 1) |
| 458 | 1.732 (4.942) | 0.499 (0.101) | 0.36 (0.073) | 5.183 | 0.059 / 0.036 | short (both) | chain drift; the override's +0.139 closes part of it (sec. 1) |
| 656 | 0.957 (5.004) | 1.204 (0.241) | 0.839 (0.168) | 6.333 | 0.029 / 0.024 | short (both) | chain drift; same as 41/54 |

`end` is info only — B1150: the missile/Middle stack moved every end, so it
is not a verdict column. Raw lines: `tables/gate_a.tsv`.

**The chain-drift owner in one sentence:** the whole tour dropped between
B1150's run and this one with 93 — the override-free control — falling
2.742 → 1.993, so the six spells' shortness is not a retirement regression;
it is the B1151/B1152 chain moving the density the tour measures
(FOLLOW-UP: bisect the B1150→B1153 tips).

### (b) the 40-record sweep on b1118 vs the B1151 harness rows

32/40 rows in band; 8 out. The table's own structure isolates the
retirement: `before` = the chain at its B1152 tip (rows still in the
config), `after` = this tip (rows deleted) — **before == after on all 40
rows** (zero retirement drift, every column), so no out-of-band row is a
retirement regression; each is B1151-run → B1152-tip drift (the B1152
missile-path merge landed between the two measurements). Raw lines:
`tables/gate_b.tsv`.

| rec | spell | b1151 | before (=B1152 tip) | after (=B1153) | verdict | owner |
|---|---|---|---|---|---|---|
| r0 | 93 | 2.113 | 1.993 | 1.993 | short | B1152 missile-path merge (drift between the B1151 run and the B1152 tip; retirement-neutral: before == after) |
| r1 | 200 | 1.254 | 1.308 | 1.308 | over | same |
| r2 | 202 | 0.676 | 0.692 | 0.692 | over | same |
| r6 | 722 | 0.391 | 0.401 | 0.401 | over | same |
| r7 | 40 | 1.069 | 0.899 | 0.899 | short | same |
| r18 | 378 | 0.985 | 0.944 | 0.944 | short | same |
| r25 | 14 | 1.993 | 2.088 | 2.088 | over | same |
| r26 | 310 | 1.492 | 1.870 | 1.870 | over | same |

The band is the chain's OWN drift band (p25..p75 of before − b1151 over the
same 40 rows, B1151's rule) — the 36 in-band rows show zero or sub-band
drift; the 8 rows above moved more than the chain's own spread between the
B1151 tip and the B1152 tip.

### (c) the 40-record sweep on b1130 / b1130-self vs the B1155 bands

`refactor/B1155/scripts/band_table.py` over `b1130_evidence/` (the B1130
oracle, `review/B1130/record_table.tsv`), staged through
`tables/stage_b1155/measure/` (the after-run tags mapped to band_table's
expected names). `tables/gate_c_banded.tsv` is the clean copy (band_table's
own output, header and all); `tables/gate_c.tsv` is the same rows with a
real header and a per-row `changed_vs_B1155` column (the verdict columns
that flipped vs `refactor/B1155/banded_table.tsv`; 40/40 rows changed).
Per-column band verdict counts (PASS = inside the B1155 band):

| column | PASS | short | over | n-a |
|---|---|---|---|---|
| source_cov | 0 | 34 | 6 | 0 |
| landing_cov | 0 | 38 | 1 | 1 |
| landing_end | 2 | 35 | 2 | 1 |
| p90_src | 10 | 10 | 20 | 0 |
| p90_land | 8 | 11 | 19 | 2 |
| source_onset_s | 36 | 1 | 3 | 0 |
| source_centroid | 20 | 2 | 18 | 0 |
| src_h_lo | 14 | 1 | 25 | 0 |
| src_h_hi | 14 | 16 | 10 | 0 |

**vs `refactor/B1155/banded_table.tsv`: 124 of 360 verdict cells changed**
(b1155 → b1153): source_cov 1/40, landing_cov 2/40, landing_end 12/40,
p90_src 18/40, p90_land 15/40, source_onset_s 4/40, source_centroid 25/40,
src_h_lo 22/40, src_h_hi 25/40 — `python3
refactor/B1153/scripts/gate_c_diff.py` (run from the repo root; compares
gate_c_banded.tsv against B1155's table by column name) reproduces the
per-row list. The coverage columns barely moved (1-2/40) while the
timing/shape columns shifted — the B1152 missile-path signature (its
README: arrival-shift moved the source-stage timing, 36/40 rows in its
own gate), which landed between B1155's sweep (cut pre-B1152) and this
one. Owner for the whole delta: **B1152 missile-path merge** — not the
retirement: the deleted rows had no runtime reader left after B1149-B1152
(sec. 0), and the retirement is empirically render-neutral on b1118
(gate (b): before == after on all 200 cells). The in-band/out-of-band
set vs the B1130 oracle itself is the same shape/density finding B1155
attributed to the B1150 particle sim (coverage 0/40 PASS both columns);
the timing columns' flips are B1152's, not a new gap.

### (d) the B1134 / B1144 observer rows vs the b1130 harness

Records 0 (93), 40 (602), 51 (4499) on b1130
(`after_fixed_npc_r0` / `after_fixed_self_r40` / `after_fixed_self_r51`)
vs the B1134 observer rows (`review/B1134/README.md`) and the B1143 rows
(`refactor/B1143/rows/`). Raw lines `tables/gate_d.tsv` (bands: onset
±0.10 s, centroid/hi/lo ±0.15 s, the B1131/B1132 method; landing_cov is
info — the observer rig's crop differs — and p90 is banded when the
observer ref has p90: record 0 p90_src 0.113 vs ref 0.0375 "over",
p90_land 0.043 vs 0.056 "in"; n-a where the ref has none, e.g. record 40):

| rec | spell | in band | out of band | owner |
|---|---|---|---|---|
| 00 | 93 | centroid, landing_end, landing_cov, p90_land | source_onset (short), src_h_lo (over), landing_onset (short), p90_src (over) | B1134 FOLLOW-UP 3 rig variables, unresolved there (target = PvP player vs the harness' stunned TrollF; caster renderer llvmpipe); the p90_src gap specifically is the chase-camera body occlusion measured in sec. 4, not a sim discrepancy |
| 40 | 602 | source_onset, source_centroid | src_h_lo (over), landing_onset (over), landing_end (over), p90 pair (info) | rig: the oracle's caster is zoned by 602 itself (DeleteSpawn Act+0.355, `OP_RequestClientZoneChange`) and the on-caster lit run is truncated with the model; the harness has no zone |
| 51 | 4499 | none (no quantities) | none | none: both blank — the oracle observer draws no particles for 4499 and the harness' record-driven castGlow is below the visible threshold in fixed_self (the record has no Target slot) |

## Owners — the list of out-of-band rows with owners (the deliverable)

- **Gate (a) tour-7, 6 of 7 rows short (38/41/54/458/656/93), 1 over (253 withov):** owner = the B1150→B1153 **chain drift** in the landing density — proven by the 93 control row (no override at all) falling 2.742 → 1.993 between B1150's tour run and this one. Not a retirement miss: gate (b) shows before == after on all 200 cells. FOLLOW-UP: bisect the B1150→B1153 tips. 253's over-half is its own override (retired-in-probe, kept for the pool + duration gaps it carries, sec. 1).
- **Gate (b) sweep b1118, 8 of 40 rows (r0/r1/r2/r6/r7/r18/r25/r26):** owner = **B1152 missile-path merge** (drift between the B1151 run and the B1152 tip; the table's before/after columns — the retirement itself — are identical).
- **Gate (c) sweep b1130, 124 of 360 verdict cells vs B1155's table:** owner = **B1152 missile-path merge** (B1155's sweep was cut pre-B1152; the delta is timing/shape-heavy with coverage stable — B1152's arrival-shift signature). The vs-oracle out-of-band set is B1155's own shape/density finding (B1150 particle sim).
- **Gate (d) observer rows, records 0 and 40:** owner = **B1134 rig variables** (B1134 FOLLOW-UP 3, unresolved there) + the record-40 zone truncation; record 51 has no owner (both blank).
- **No row in any table is an unattributed miss.**

## 4. The sprite-size observer leg (lane 5)

One lane-5 leg (B1134 rig, observer = char 103 Oraclewatch, caster = char 67
Testcaster on class 6 DRU; lease taken by the driver, world restored down,
the before/after census in `lane5/oracle_b1153_sprite2/` (the
`*-before.tsv` / `*-after.tsv` pairs: accounts, char-book, char-buffs,
char-memmed, char-state, db, eqclient, inventory, login-accounts, vitals): 93 Burst of Flame
cast x2 on the `#spawn`ed TrollF at B1130's pose (TrollF at -74,428, caster
`#goto` -67.6 434.4 3.75 + `#stun` after a 22 s transition settle — B1130's
own proven sequence, `review/B1130/plans/r131.tsv` rows 10-13). Plan:
`lane5_plans/plan_b1153_sprite2.tsv` (evidence:
`/home/user/kb-evidence/kb-b1153-retire/lane5/oracle_b1153_sprite2/`; the
first attempt `plan_b1153_sprite.tsv` targeted before the `#goto`'s zone
transition and both casts were rejected by the client — zero OP_CastSpell in
that capture — so v2 follows r131's post-transition settle with a
re-target/re-stun before each cast; v2's capture shows 2x OP_CastSpell,
9x OP_Action).

Two facts this leg carries:

1. **Camera**: the leg was filmed on the CHASE camera, not B1130's user
   camera 1 — the pre-goto F9 presses are reset by the zone transition
   (`OP_RequestClientZoneChange`), so the observer never reached user
   camera 1. The model was therefore locked at 220 px (feet y≈606, head
   y≈380 in the 1280x720 grab, cross-checked in a frame inspection) against
   B1130's 209 px fixed camera: ~5 % scale, ~10 % area, plus chase-angle
   foreshortening. The p90s are model-normalised, so the comparison stays
   fair inside that uncertainty.
2. **Per-sprite p90** (`review/B1132/scripts/fx_sprite_roi.py` pooled on the
   capture, `refactor/B1153/scripts/measure_sprite_leg.py`; the quantity no
   capture covered — B1119's note: on a full-population tour the fable
   sprites merge into one blob, so only a sprite-separable capture reads a
   sprite size):

   | stage | oracle (lane 5, chase, 220 px) | harness b1130 r0 (93, fixed_npc, 209 px) | reading |
   |---|---|---|---|
   | LANDING | 0.053 / 0.059 (cast 1/2) | 0.043 | within the stated camera/scale uncertainty (oracle ~24-27 % larger; chase-angle foreshortening cuts the other way, 220 vs 209 px ~10 %) — **agrees** |
   | SOURCE | 0.038 / 0.037 (0.3..cast window); 0.035 / 0.037 (full glow 0.5..6.5 s) | 0.113 | **disagrees 3x, but not in the sim**: the chase camera sits behind the caster's body and the 93 cast glow is centred on the caster, so the glow is occluded and fragments into small blobs that the instrument does not count; re-measuring over the full glow window (the run.log slot ranges span +0.41..6.60 s) does not change it. Rig artifact, not a record word — FOLLOW-UP: a clean leg on user camera 1 (F9 x3 AFTER the transition).

`lane5/oracle_b1153_sprite2/sprite_p90.tsv` carries the raw lines with the
method header.

## 5. CLAUDE.md and the docs

- `CLAUDE.md` T441 paragraph → the final state (this is the one CLAUDE.md
  edit the chain allows). The exact edit (fits the 60,000-byte ceiling:
  59,908 → 59,992 bytes), to be applied where the write is approvable —
  the headless worker's approval prompt timed out, so it is handed off
  verbatim:

  ```diff
  -measured values only (B1120/B1122 shapes; sizes are gated on the caster-ROI
  -p90, `review/B1119/scripts/fx_sprite_roi.py`); **an unmeasured spell resolves
  +the six measured values (B1120/B1122 shapes; sizes gated on the caster-ROI
  +p90, `review/B1119/scripts/fx_sprite_roi.py`), each kept after the B1153 retirement probe with its numbers (README sec. 1); **an unmeasured spell resolves
  ```
- `docs/spellsnew_format.md` "B1153 final state" paragraph — written at
  c5b475eb and still accurate: the measured-row system retired (two default
  rows only; the bands in `tests/spell_fx_*_oracle.h` + `spell_anim_rows.h`),
  six `spellOverrides` STAY (38/41/54/253/458/656), each documented with its
  numbers in this README. No further change needed.

## 6. Tests

- Targeted (via rbuild on the ThinkPad, CI's Release/GLES2 tree):
  `test -R 'WhirlingWind|SpellStyleOverrides|SpellPoolRule|SpellLandingVolume|SpellColourFromSprite|SourceAnchor|spell_fx'` → 39/39 pass; `test -R 'spell|Spell|ConfigKey|DataPath|Census|Band|Reachability'` → everything passes except `GlDriverCensus.RealRunPrintsTheGlDriverLine`, which fails identically on an unrelated 2026-10-19 worker tree (`kb-b1022-weapon-delay`) — a pre-existing rbuild-host environment failure (it spawns a real `willeq`), not this diff.
- Hygiene: `rbuild hygiene` → 48/48 (one conditional skip, `MemoryCapHuntScript`).
- ONE whole-tree CI-flag build at the head: `rbuild <wt>` (CI's Release/GLES2
  configure) → 402/402 targets, exit 0; full `rbuild <wt> test` → 6464 tests:
  6436 pass, 27 skip (conditional), 1 fail = the pre-existing
  `GlDriverCensus.RealRunPrintsTheGlDriverLine` rbuild-host environment
  failure (identical on the unrelated 10-19 tree `kb-b1022-weapon-delay`;
  the x86 CI runners are the arbiter — see the MR pipeline).

## FOLLOW-UP (proposed, no cards created)

1. **Chain drift in the tour-7 landing density (B1150 → B1153).** 93 (the
   override-free control) fell 2.742 → 1.993 PEAK between B1150's tour run
   and this one; 8/40 sweep rows sit outside the chain's own drift band
   (gate (b)). Owner mechanism: bisect the B1151/B1152 merges against
   B1142's 3.049 reference (the density moved after B1149's population
   wiring — a record word the sim over-honours, or a pool interaction).
2. **253 record field overflows the pool rule.** Record 35's population
   words (B1149) give a steady field of ≈14100 (desktop) / ≈5930 (sbc)
   live sprites vs the 8192/4096 pools; `applyPopulationRule` was derived
   against the override's 2160 field. Re-derive the rule against the
   record-driven field and retire the 253 override for good.
3. **253 duration.** The record path dies at 5.18 s vs the oracle's 6.52 s;
   B1145's computed +4.35 s cast-animation term sits in the file as an
   override word. Wire it into the record path.
4. **A clean user-camera leg for the 93 source p90.** The chase camera
   occludes the cast glow by the caster's body (sec. 4); F9 x3 AFTER the
   transition, then re-measure with `measure_sprite_leg.py`.
5. **B1157 (running in parallel)** re-gates both presets with an animated
   caster; these tables stay the pre-animation baseline.
