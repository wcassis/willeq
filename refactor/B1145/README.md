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
- **A dying target's landing is cut by the client's corpse-actor delete** (sec. 5a, traced): OP_DeleteSpawn
  queues a corpse's actor on the fade list and deletes it -- with every emitter attached to it -- once more
  than 3499 ms have passed. Wired (`SpellVisualFX::onSpawnDeleted`): with the target dying at OP_Action and
  despawned at +1.027 (lane leg B's pcap), fable's landing now drops at Act+4.53 (47,331 -> 5,017 lit px in
  one frame), the oracle's cliff at 4.55..4.71; a live target still runs the file's end (+6.733).
- **Gate still NOT met by +0.23..0.26 s**: 93 dying reads +5.167..+5.200 (seeds 1-3) against lane +4.938.
  What is left past the cut is the caster's Source tail (distance 0, same pixels): its sprites decay from
  5,017 to the threshold over ~0.5 s where the oracle's decay over ~0.2 s (8,330 -> 1,095, 4.71..4.87),
  because fable's four Source slots carry EQUAL shares (B1144) where the file's 278 (L 0.25) holds ~150 live
  sprites against the hands' ~34 -- per-slot counts are B1147's. 38/54/656 dying read +5.20..+5.23 (oracle
  4.85..5.10).
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
| B1145, tour 93 L60 seeds 1-3, death only (no despawn) | +6.733 x3 | 2.316..2.358 | first wiring |
| B1145 final, tour 93 L60 seeds 1-3, death at Act + despawn +1.027 | **+5.200 / +5.167 / +5.200** | 2.334 / 2.321 / 2.361 | lane +4.938: NOT met (+0.23..0.26, the Source tail) |
| B1145 final, tour 93 L60, live target | +6.733 | 2.328 | the file's end |
| B1145 final, 38 / 54 / 656 dying, 41 / 458 live (single-spell plans) | +5.233 / +5.200 / +5.217, +6.433 / +5.833 | | oracle 4.850 / 5.103 / 5.004, 6.224 / 4.942 |
| B1145, tour 93 L1 | +5.500 | 1.516 | distance 0: the caster's Source (A + 1 s) is in the same pixels |
| base, separated (14 units) L1 / L60, target box | +4.133 / +4.767 | | `scripts/box_end.py` |
| B1145, separated L1 / L60, target box | **+2.667** / **+6.500** | | oracle: observer 2.61/2.65; B1130 alive 6.67 |
| caster box Source end, base / B1145 first / final | 3.97..4.23 / 4.97..5.00 / 4.47..4.53 | | oracle r51 4.908 (the visible end of fable's equal-share slots, not the emission end) |
| B1145 final, separated L1 / L60, target box | +2.667 / +6.167 | | L60's sparse tail moves with the random stream (6.500 on the first wiring) |

Tour (all seven, `tour_table.tsv`): 41 (target alive) +6.433 vs oracle 6.224 (lane fable 6.278); 253 (self)
+8.117 vs 6.522 and 458 (alive) +7.800 vs 4.942: their emitters fade for most of their life (432:
+0x94 3.25 of L 3.5; 458's Source 552: 3.75 of 4), which the oracle's visible end shows and fable's
alpha (clientAlpha off, B1146) does not; at distance 0 458's tail is its held Source (A + L 4). 93/38/54/656
(targets die): the whole-tour run above has no despawn (+6.73..6.77); with the corpse-actor delete wired and
the despawn modelled see the final rows (sec. 5a). Lane 5 was not used: the mechanism is in the code, and the
oracle tour's own frames and pcap check it.

## 5a. A dying target: the corpse-actor delete (traced, `review/B1141/ghidra/out/`)

| step | address | what |
|---|---|---|
| OP_Death (0x6160) | eqgame 0x4a8cd5 -> 0x484e79 | the spawn becomes a corpse (spawn +0x244 = 2, "%s's corpse"), death animation; nothing touches its emitters |
| OP_DeleteSpawn (0x55bc) | eqgame 0x4581b7 -> 0x4849b2 (ecx = the spawn) | for a corpse (+0x244 > 1) whose race is not 216/472 (0x47bfd0) sets actor-gfx +0x19c = 1, then 0x48f4a4 |
| actor release | eqgame 0x48f4a4 | +0x19c set: 0x401022/0x401351 put the CActor on the corpse-fade list (start = now, 0x401100) instead of deleting it; else 0x439a46 deletes it now |
| fade list, per frame | eqgame 0x445243 -> 0x4012bb -> 0x401127 | from 1001 ms plays effect 0x5e at the hand/chest/feet points, from 1300 ms fades the model's alpha (1 - 0.0005/ms, vtbl +0x1b0); when now - start > 0xdab (3499 ms): 0x401110 -> 0x439a46 |
| actor delete | eqgame 0x439a46 -> world vtbl +0x48 = DLL 0x10056b50 -> +0x08 = 0x10056b60 | the CActor's scalar-deleting destructor |
| emitters | DLL actor destructors 0x10040150 / 0x100489e0 / 0x10049fb0 -> particle manager (DAT_102326f0, vtable 0x101cbe38) vtbl +0x30 = 0x1005e760 -> 0x1005e550 | every emitter whose actor (+0x68) is this actor, or whose bone (+0x6c) belongs to it, is unlinked and deleted with its particle ring (vtbl +0x14(1)) |

The Target stage attaches its emitters to the target spawn's actor (slot walk 0x495cc3: actor = *(spawn
+0x1a8), bone = the attach point; handle = target spawn id << 16 | spell). So with OP_Death at Act+0.000 and
OP_DeleteSpawn at +1.036 (oracle tour pcap, spawn 260) the landing runs until the first frame past 1.036 + 3.5
= Act+4.536 and vanishes at once: the oracle's cliff (lit 72,736 at 4.55 -> 8,330 at 4.71; 38: 4.57..4.69,
its DeleteSpawn +1.032). Nothing is fitted. A live spawn's OP_DeleteSpawn deletes its actor at the packet.
The reassign (vtbl +0x20 = 0x1005e4c0, from 0x485873's model rebuild) moves emitters to a new actor and
clears the bone; remove-by-handle (vtbl +0x34 = 0x1005e930, from 0x4543fa / 0x4544ed / 0x4b6b1b on a buff's
removal) deletes by handle. Neither runs on death or despawn. Frames: `review/B1145/oracle_frames/`.

fable: `SpellVisualFX::onSpawnDeleted(spawn, corpse, race)` (called by `IrrlichtRenderer::removeEntity` on
EntityDespawned, and by the harness at `--target-despawn`) removes the spawn's and its bone pseudo-entities'
spell effects with their particles -- at once for a live spawn or race 216/472, 3.5 s later
(`kCorpseActorDeleteSeconds`, run in `update()`) for a corpse. Test:
`SlotRules.ADeletedSpawnTakesItsAttachedEffectsWithIt`. Not wired: the corpse model's own alpha fade and
effect 0x5e.

Also fixed with it: the measured cast-animation term is the Source's emission end after OP_Action, so the run
from fable's Source start (BC + the 0.41 s latency) is that much shorter (it had ended 0.41 s late).

## 6. What moved besides timing

- **Coverage parity: pending B1147** (known interim; no band loosened). Landing coverage at the same peak
  live count: 93 -20 % (2.32..2.36 vs 2.92..2.96), 38 -22 %, 41 -9 %, 458 -18 %, 253 +72 %
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
Builds and suites (`review/B1145/`, final tree): whole tree with the CI flags, `cmake --build build-ci -j 4 --
-k`, BUILD_EXIT=0, 0 `error:` lines (`build_ci_whole_final.log`); `ctest -R "Spell|Bridge|Particle|FxHarness|
ConfigKeyReachability|Oracle|SlotRules|SourceAnchors|SourceRecordRows|TargetRecordRows|TargetLanding|Landing|
CasterOwn"` 899/899 (`ctest_targeted.log`, incl. `SlotRules.ADeletedSpawnTakesItsAttachedEffectsWithIt`);
`ctest -L hygiene` 51/51 (`ctest_hygiene.log`; the EDD reader's count accessor is `emitterCount()`: an
`entryCount()` made ZeroCallers read SkillCapTable's same-named accessor as production-called). No config map
added (ConfigKeyReachability unchanged). CLAUDE.md 59,652 characters.

## FOLLOW-UP

1. Done here (sec. 5a). Left: the corpse model's alpha fade (from 1.3 s, 0.0005/ms) and effect 0x5e, and the
   Source tail's counts (B1147), which hold the dying-target tour gate +0.23 s out.
2. Per-caster cast-animation length (0x47ddd4: the model's CastingAnim track) instead of one 4.35 s.
3. B1147: counts, sizes, shapes, motion, client alpha; then re-calibrate coverage (sec. 6).
