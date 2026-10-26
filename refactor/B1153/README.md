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
the noov phase strips `spellOverrides` from the shared config, runs its 11
jobs, and restores it):

- b1118: `runs/b1153_after_tour_<spell>/measure/summary.tsv` (override
  present) and `runs/b1153_noov_b1118_<spell>/measure/summary.tsv`
  (override stripped) — the B1139 instrument,
  `FX_KEEP=1 python3 -I /mnt/projects/fable/review/B1139/scripts/harness_measure.py <frames> <measure>`.
- b1130: `b1130_evidence/measure/after_ovb1130_<spell>/b1130_rows.tsv` and
  `b1130_evidence/measure/noov_ovb1130_<spell>/b1130_rows.tsv` — the B1155
  instrument.

Lane bands: `review/B1142/gate/regression_table.tsv` (lane-5 b1118 captures;
the harness/oracle landing-ratio band 0.45..0.72 is B1150's p25..p90
 over the 40-row survey, `refactor/B1150/README.md`). b1130: the B1155 banded
table (`refactor/B1155/banded_table.tsv`) — but only spells 38/41/54/253/458/
656 have a LANE row there (the b1130 preset has no oracle row of its own),
so the retire/keep decision is taken on the b1118 lane numbers; the b1130
half is the withov/noov delta on that geometry.

### Decision table

<!-- PLACEHOLDER-OVERRIDE-TABLE -->

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

The 93 reference is `new_land_cov` 3.049 (the B1142 re-take), NOT the old
rig's 2.938. Verdicts: the harness/oracle landing-ratio band 0.45..0.72
(B1150) and the table's own roi_l/roi_s bands. The withov/noov half serves
step 1 for the six override spells.

<!-- PLACEHOLDER-GATE-A -->

### (b) the 40-record sweep on b1118 vs the B1151 harness rows

The retirement must be render-neutral: PASS = within ±0.05
(src_cov/land_cov/roi) / ±0.10 s (end) of the B1151 harness rows
(`refactor/B1151/runs/b1151_r*/measure/summary.tsv`, the pre-retirement
chain's own measurements at its tip).

<!-- PLACEHOLDER-GATE-B -->

### (c) the 40-record sweep on b1130 / b1130-self vs the B1155 bands

`refactor/B1155/scripts/band_table.py` over `b1130_evidence/` (the B1130
oracle, `review/B1130/record_table.tsv`), plus a delta column against
`refactor/B1155/banded_table.tsv`: a verdict CHANGE is a retirement
regression and gets an owner below.

<!-- PLACEHOLDER-GATE-C -->

### (d) the B1134 / B1144 observer rows vs the b1130 harness

Records 0 (93), 40 (602), 51 (4499) on b1130
(`after_fixed_npc_r0` / `after_fixed_self_r40` / `after_fixed_self_r51`)
vs the B1134 observer rows (`review/B1134/README.md`: r51 row 0 for 93, the
P3 ports row for 40, the P4/P4b/P5 row 51 for 4499) and the B1143 rows
(`refactor/B1143/rows/`). Bands: onset ±0.10 s, centroid/hi/lo ±0.15 s
(the B1131/B1132 method).

<!-- PLACEHOLDER-GATE-D -->

### Owners for every miss

<!-- PLACEHOLDER-OWNERS -->

## 4. The sprite-size observer leg (lane 5)

One lane-5 leg (B1134 rig, observer = char 103 Oraclewatch, caster = char 67
Testcaster on class 6 DRU for the leg; lease `t_6a9fc67e` taken by the
driver, restored with the world down, `lane_before/after.txt` = the
before/after census in `lane5/oracle_b1153_sprite/`): 93 Burst of Flame cast
ON the `#spawn`ed TrollF at B1130's pose (TrollF at -74,428, caster
`#goto` -67.6 434.4 3.75 + `#stun` — B1130's own sequence,
`review/B1130/legs_commands.tsv` rows 10-13), the observer at B1130's
fixed-camera vantage on user camera 1, x2. Plan:
`lane5/plan_b1153_sprite.tsv` (evidence: `/home/user/kb-evidence/kb-b1153-retire/lane5/`).
Measurement: per-sprite p90 with `review/B1132/scripts/fx_sprite_roi.py` on
the oracle frames vs the SAME instrument on the b1130 harness frames
(`b1130_evidence/measure/after_fixed_npc_r0/fable_93/`) — the quantity no
capture covered (the B1119 note: on the full-population tour the fable
sprites merge into one blob, so only a sprite-separable capture reads a
sprite size).

<!-- PLACEHOLDER-SPRITE-LEG -->

## 5. CLAUDE.md and the docs

- `CLAUDE.md` T441 paragraph → the final state (this is the one CLAUDE.md
  edit the chain allows): <!-- PLACEHOLDER-T441 -->
- `docs/spellsnew_format.md` "B1153 final state" paragraph → the surviving
  override set (it was written for the pre-gate "six stay" state).

## 6. Tests

Targeted ctest + hygiene via rbuild on the ThinkPad, then ONE whole-tree
CI-flag build at the head: <!-- PLACEHOLDER-TESTS -->

## FOLLOW-UP (proposed, no cards created)

<!-- PLACEHOLDER-FOLLOWUPS -->
