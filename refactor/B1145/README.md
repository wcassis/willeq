# B1145 (#465) FX-WIRE-TIMING: spell FX timing from spellsnew.edd

Branch `task/B1145-fx-timing` (wt/b1145), cut from the B1144 tip 8f577477 (on fable 510abc39). Evidence:
`/mnt/projects/fable/review/B1145/` (`README.md`, `progress.log`, `crosscheck.tsv`, `tour_table.tsv`,
`cast_anim/`, `runs/`, `scripts/`). Source of truth: `docs/spellsnew_format.md`,
`review/B1141/edd_fields_from_code.tsv` (+0x78..+0x8C) and `eff_fields_from_code.tsv` (the durations
eqgame passes).

## 0. Result

- Every drawn spellsnew.eff slot is timed by its emitter's EDD words: start = the stage's measured
  latency + +0x8C, emission for the run (fixed |+0x78|, or the stage's duration when +0x78 >= 0) minus the
  delay at the file's burst/stream envelope, each particle +0x7C. Source duration = cast time + the
  cast-animation term; Target = 0 (Middle = field 18; not drawn, no missile).
- **The cast-animation term (eqgame 0x47ddd4) was MEASURED, not traced**: 4.35 s (median of 20 unsaturated
  B1130 other-target captures, IQR 4.12..4.81; sec. 3). 0x47ddd4 returns a track length of the caster
  model's animation (model data), so the code alone cannot give the number.
- Cross-check BEFORE replacing (sec. 4): the C++ code reproduces B1141's crosscheck exactly (39 records,
  median -0.11 s, mean -0.12, 25 within 0.5 s, 29 within 1.0 s, Spearman 0.834). Both outliers are the
  capture, not the file.
- Level-1 93 (B1144's open item): the file's level-0 Target slots (280: 1 s fixed + 0.25 s delay + 1.5 s
  life; 281: a one-sprite burst living 1 s) end 2.5 s after the stage starts; the observer measured
  Act+2.61/2.65. The harness (separated target) now ends the level-1 landing at **Act+2.667** (was 4.133);
  level 60 at Act+6.500 (B1130's alive capture 6.67; was 4.767); the caster's Source at Act+4.97..5.00
  (oracle r51 caster box 4.908; was 3.97..4.23).
- **Gate NOT met: 93 end within +-0.1 s of lane +4.938.** The tour rig's target DIES at OP_Action
  (OP_Death +0.000, corpse OP_DeleteSpawn +1.036). The oracle cuts the landing of every nuke whose target
  dies (93/38/54/656 end 4.85..5.10 against the file's 6.5), while an alive target runs the file's end
  (B1130 r51 93: 6.67; tour 41 Weaken: 6.224 vs 6.2). The cut is a client death/despawn behaviour, not an
  EDD word, and is untraced (FOLLOW-UP 1). The branch harness reads +6.733 on the dying-target tour.
- Coverage moved (sec. 6): 93 landing 2.92..2.96 -> 2.32..2.36 (-20 %) at the same peak live count.

## 1. Prior work found

- `git log` of `spell_visual_fx.cpp`, `src/client/spell/`, `config/spell_effects.json`, `tools/model_viewer`:
  B1121 (the landing's END by a fed schedule: peak `spawnRate` 0.6 s, `sustainRate` from 1.6 s to a measured
  `emitterLifetime`, particle life 1.6..2.0 kept), B1131 (Source rows: measured onset, holdOther, holdSelf;
  `releaseCastGlow` holds past OP_Action), B1132 (Target rows: measured onset/end -- read by no runtime
  path, tests only), B1137 (rows keyed by field 145), B1141 (the meanings; crosscheck), B1144 (slot rules,
  equal 1/N share per slot), B1146 (colour words, its own EDD byte access). No `spell_fx_timing*` file existed.
- Timing paths the measured rows fed: Source `onset` -> `SourceForRecord` triggerDelay; Source
  `holdOther/holdSelf` -> `releaseCastGlow` and the pool rule's held-Source overlap (`applyPopulationRule`,
  `maxHold()`); landing end -> the impact/spellComplete styles' `emitterLifetime` 2.85 + `triggerDelay`
  0.33 + life (and the overrides' 3.0/2.9/4.15/4.35); Target `onset`/`end` -> nothing at runtime.
- Why fable ended 93's landing at +5.0 at every level: the style schedule above is level-blind; the slot
  rule (B1144) only split its population 1/N per slot.
- `holdSelf` 0.33 was never a measurement of the Source (B1131: no record separates Source from landing on
  a self cast); the file gives the Source one duration whatever the target.

## 2. Code

| file | what |
|---|---|
| `include/client/spell/spells_new_edd_reader.h`, `src/client/spell/spells_new_edd_reader.cpp` (new) | `SpellsNewEddReader`: the one reader of the 416-byte entries (bytes, name, texture, frames) + `SpellsNewEddTiming` (+0x78, +0x7C, +0x80, +0x84, +0x88, +0x8C) |
| `include/client/spell/spell_fx_timing.h` (new, header-only) | `SpellFxSlotTimingFor` (run/start/window/lifetime/end/peakLive), `SpellFxCallerDurationMs` (Source = cast + `kSpellFxCastAnimationSeconds`, Middle = field 18, Target = 0) |
| `spells_new_eff_reader.{h,cpp}` | reads the EDD through the reader (`edd()`); texture field is 32 bytes (B1141), not 64 |
| `spells_new_edd_colour.{h,cpp}` | API kept; reads both files through `SpellsNewEffReader`, decodes via `eddInt/eddFloat` |
| `spell_particle_types.h`, `spell_emitter_presets.cpp` | `applySlotTiming` (trigger = latency + delay; `emitterLifetime` = window; life = L with the motion time-scaled by old life / L so the measured extents hold; burst + stream at the file's ratio, peak kept), `CombinedPeakLive`, `ScaleToCombinedPeak` |
| `spell_visual_fx.{h,cpp}`, `spell_visual_fx_nodes.cpp` | `slotTiming()`; per-slot timing in `createCastGlow` and `createLandingFromSlots`, the stage then scaled to its untimed (B1144) combined peak; `releaseCastGlow` leaves a spellsnew.eff Source alone |
| `spell_effects_config.{h,cpp}` | `sourceStageLatency()` (default row onset 0.41), `targetStageLatency()` (spellComplete triggerDelay 0.33); rows inherit the default onset and the computed hold (4.35) |
| `config/spell_effects.json` | the rows' measured `onset`/`holdOther`/`holdSelf` (41 Source rows) and `onset`/`end` (40 Target rows) removed; the `default` rows keep theirs (spells with no field-145 record) |

What a slot does NOT take from the file yet (B1147): counts (the stage still carries B1144's equal shares,
scaled together), sizes, shapes, motion, the client's fade/peak alpha (B1146 `clientAlpha`).
Stage start: no EDD word holds it (every measured record's first slot has +0x8C = 0, yet the Source shows at
BC+0.37..0.50 and the landing ~0.4 s after OP_Action), so it stays the measured latency.

## 3. The cast-animation term: measured

`review/B1145/scripts/source_end.py` decodes the 28 B1130 `fixed_npc` captures (target ~300 px to the right),
masks rise > 80 over the pre-cast median in a narrow caster box (|dx| <= 0.45 model, h -0.1..1.7), and writes
the lit series past OP_Action (`cast_anim/source_end.tsv`, `_series.txt`). `scripts/cast_anim_fit.py`: a
caller-duration Source slot stops emitting at OP_Action + A and its live count halves L/2 later, so
A = t50 - Lw/2 (t50: the box falls below half its Act+3..4 plateau; Lw the live-weighted life of the record's
caller slots). Unsaturated boxes (n 20): **median 4.35 s, IQR 4.12..4.81**; CastingAnim 42 (n 2) 4.08,
43 (n 3) 4.35, 44 (n 14) 4.29; the cast times span 1.0..5.9 s and A does not follow them (Source duration =
cast + a constant, as 0x49b977 says). The outliers are the model's limits (e33: one L = 4 s emitter fading
for 3.75 s; e27: no plateau). Traced side: `0x47ddd4(spell+0x186)` indexes the caster actor's animation table
by the spell's CastingAnim (field 120) and returns the track's length through `0x60e98c` (ftol); which track
and its length are model data. One constant for every caster is a simplification (the survey used one
caster); `test_spell_fx_timing` pins its use.

## 4. Cross-check before replacing (`review/B1145/crosscheck.tsv`, `tests/test_spell_fx_timing.cpp`)

Computed by `SpellFxSlotTimingFor` on the shipped files (Target, caller 0, all slots = the `#level 60`
captures), against B1130's measured landing end: n 39, median -0.11 s, mean -0.12, 25 within 0.5 s, 29
within 1.0 s, Spearman 0.834 -- B1141's numbers, through this code.

- **f145 90 (row 3, 377 Icestrike, +9.00 vs 6.4)**: a rain (field 18 = 7500 ms). The capture's pcap
  (`review/B1130/oracle/e3/all-opcodes.tsv`) has a SECOND wave of OP_Action 2.501 s after the first; each
  plays the Target stage, so the last particle dies at 2.501 + 6.4 = 8.90 (measured 9.00, the frame also
  saturated).
- **f145 100 (row 41, 703 Chords of Dissonance, 2.75 vs 5.5)**: the RUN line is the target ROI. The
  capture's whole crop (`measure/e41_fixed_npc/lit80.tsv`) stays lit until Act+5.67 (2703 px) and is dark at
  Act+5.75 (83 px): the file's T = 5 slots (406/407) end there. Which actor those PB-AE slots sat on the ROI
  cannot tell.
- Level 1, 93: 2.5 vs the observer's 2.61/2.65 (+0.13, inside the 39 records' spread).
- Source: every measured "still lit at Act+x" hold of B1131's 23 other-target rows is inside the computed
  Source end (A + the caller slots' life).

Then the rows' measured values were removed from the config and kept as the test bands
(`tests/spell_fx_timing_oracle.h`, generated by `review/B1145/scripts/gen_timing_header.py` from
`git show 8f577477:config/spell_effects.json`, B1130's record table and `source_end.tsv`).

## 5. Harness (`review/B1145/runs/`, `--camera b1118`, pool 8192)

| run | landing end (Act+) | landing cov | note |
|---|---|---|---|
| base 8f577477, tour 93 L60 | +5.000 (seed 1), +5.033 (tour) | 2.962 / 2.921 | |
| B1145, tour 93 L60 seeds 1-3 | +6.733 x3 | 2.316..2.358 | target dies at OP_Action (`--target-death 0`) |
| B1145, tour 93 L1 | +5.500 | 1.516 | distance 0: the caster's Source (A + 1 s) is in the same pixels |
| base, separated (14 units) L1 / L60, target box | +4.133 / +4.767 | | `scripts/box_end.py` |
| B1145, separated L1 / L60, target box | **+2.667** / **+6.500** | | oracle: observer 2.61/2.65; B1130 alive 6.67 |
| caster box Source end, base / B1145 | 3.97..4.23 / 4.97..5.00 | | oracle r51 4.908 |

Tour (all seven, `tour_table.tsv`): 41 (target alive) +6.433 vs oracle 6.224 (lane fable 6.278); 253 (self)
+8.117 vs 6.522 and 458 (alive) +7.800 vs 4.942: their emitters fade for most of their life (432:
+0x94 3.25 of L 3.5; 458's Source 552: 3.75 of 4), which the oracle's visible end shows and fable's
alpha (clientAlpha off, B1146) does not; at distance 0 458's tail is its held Source (A + L 4). 93/38/54/656
(targets die): +6.73..6.77 vs 4.85..5.10 (sec. 0). Lane 5 was not used: the death evidence is in hand
(dying vs alive captures of the same client and spell; 41 as the alive control in the tour rig) and one leg
cannot trace the mechanism.

## 6. What moved besides timing

- Landing coverage at the same peak live count: 93 -20 %, 38 -22 %, 41 -9 %, 458 -18 %, 253 +72 %
  (its field now runs 5 s at its file envelope). The slots' lives (L) and envelopes differ from the style's
  1.6..2.0 s cohort; counts and sizes are B1147's.
- The pool rule's held Source is the computed 4.35 s for every row (was the measured holdOther, 0.32..5.5):
  253's sbc field 1076 (was >= 1500; `SpellPoolRule.WhirlingWindIsASteadyFrameFillingField`).

## 7. Tests

`tests/test_spell_fx_timing.cpp` (new, 10: rules, decode, the cross-check, outliers, level 1, Source holds);
`test_spell_slot_rules.cpp` (+2 timing seams, shares by combined peak); `test_spell_source_anchors.cpp`
(onset = latency inside the measured onset band; holds computed); `test_spell_target_anchors.cpp` (port band
from the header); `test_spell_burst_oracle_match.cpp` (Source onset); `test_spell_colour_and_overrides.cpp`
(253 sbc bound); `test_fx_harness.cpp` (the other-target check compares the caster box with a reference run
whose OP_Action falls past the window -- the Source now holds to ~Act+4.35 + life -- and the target-box
presence bound is 2,000 px: record 112's file envelope reads 2,677 there).
Builds and suites (`review/B1145/`): whole tree with the CI flags, `cmake --build build-ci -j 4 -- -k`,
BUILD_EXIT=0, 0 `error:` lines (`build_ci_whole_final.log`); `ctest -R "Spell|Bridge|Particle|FxHarness|
ConfigKeyReachability|Oracle|SlotRules|SourceAnchors|SourceRecordRows|TargetRecordRows|TargetLanding|Landing|
CasterOwn"` 897/898 (`ctest_targeted.log`; the one red, FxHarness's other-target check, fixed after it and
re-run 3/3 with only `test_fx_harness` rebuilt); `ctest -L hygiene` 51/51 (`ctest_hygiene.log`; the EDD
reader's count accessor is `emitterCount()`: an `entryCount()` made ZeroCallers read SkillCapTable's
same-named accessor as production-called). No config map added (ConfigKeyReachability unchanged).
CLAUDE.md 59,5xx characters.

## FOLLOW-UP

1. Trace what a target's death/despawn does to its Target emitters (eqgame OP_Death 0x6160 -> 0x4a8cd5,
   OP_DeleteSpawn 0x55bc -> 0x4581b7; particle manager vtbl +0x20 reassign 0x1005e4c0, +0x30 remove-by-actor
   0x1005e760, +0x34 remove-by-handle 0x1005e930). Until then fable runs a dying target's landing to the file
   end (1.6..1.9 s past the oracle on 93/38/54/656).
2. Per-caster cast-animation length (0x47ddd4: the model's CastingAnim track) instead of one 4.35 s.
3. B1147: counts, sizes, shapes, motion, client alpha; then re-calibrate coverage (sec. 6).
