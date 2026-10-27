# B1161 (#480): retire the 253 spellOverride -- pool rule re-derived, cast-anim term on the record path

Branch `task/kb-b1161-retire-253`. Evidence: `/home/user/kb-evidence/kb-b1161-retire-253/`
(frames + measurements for all 18 runs; the orchestrator copies it to
`/mnt/projects/fable/review/B1161/` at hand-back). The specification is
`glab issue view 480`. The predecessor is B1153's FOLLOW-UP, items 2 and 3
(`refactor/B1153/README.md` "FOLLOW-UP (proposed, no cards created)"):
"Re-derive the rule against the record-driven field and retire the 253
override for good" (item 2) and "Wire it into the record path" (item 3, of
B1145's +4.35 s cast-animation term that "sits in the file as an override
word").

**Merge note (orchestrator 2026-10-26 22:05Z):** `origin/fable` had moved to
`1150a34a` (the B1153 merge, MR !311) before the retire step; `git fetch
origin && git merge origin/fable` fast-forwarded this branch (its pre-merge
tip was `cc85c331`, the B1159 merge -- the actual start tip, per `git
reflog`) and the retire edits landed on top. The BEFORE numbers below are on
`cc85c331`; the AFTER numbers are on `1150a34a` + the retire diff. The
B1157/B1153 merges between the two tips are pixel-neutral on the six
non-253 tour rows (bit-identical before/after, sec. 5).

## 0. Result

- The 253 (record 35) `spellOverride` is out of `config/spell_effects.json`;
  `spellOverrides` keeps its five: 38/41/54/458/656.
- The pool rule is re-derived against the record-driven field (sec. 2): a
  spell with a spellsnew.eff record draws its slots' FILE populations and
  the rule scales NONE of them -- the pool (8192 desktop / 4096 sbc) is a
  CAP, not a scaler (B1149 sec. 4). The half-pool bound is the no-record
  style landing's (and the kept overrides'). Pinned by `SpellPoolRule` over
  the largest, next-largest and a small record.
- The +4.35 s cast-animation term is on the record path via the client's
  computed rule -- `EQ::kSpellFxCastAnimationSeconds`, never a per-spell
  word (sec. 3). The record-driven 253 run ends at 6.68 s, inside B1118's
  band against the oracle's 6.52 s; the pre-B1161 style base gave 5.18 s.
- Rendered: 253's b1118 landing PEAK moves 5.303 (0.813 of the oracle, OVER
  the band) to 4.157 (0.637, IN the 0.45..0.72 band) -- reproducing B1153's
  own retirement-probe gate (5.308/0.814 -> 4.188/0.642). No pool overflow
  anywhere: the pool drops when full, on desktop and sbc alike. The five
  kept overrides and the override-free control (93) are bit-identical
  before/after.

## 1. What existed before the retire step

The 253 override (as carried at `cc85c331`, `config/spell_effects.json`
`spellOverrides."253"`): `impact` + `spellComplete` (B1120/B1122's measured
landing geometry: the frame-filling field, population words summing to a
2160-sprite field) and `castGlow`. Its `emitterLifetime 4.35` was B1145's
computed cast-animation term (+4.35 s past OP_Action) placed in the file
"because the record path does not honour it yet" (B1153, sec. 1).

B1153's retirement probe (commit 701ac572, reverted 423890ee) kept 253 for
two owned gaps (its README, "The 253 retirement probe (and why it was
reverted)"):

1. `SpellPoolRule` FAILED: the record-driven field's steady population is
   ~14100 (desktop) / ~5930 (sbc) live sprites; `applyPopulationRule` was
   derived against the override's 2160 field.
2. `WhirlingWindLastsAsLongAsTheOracles` FAILED: the record-driven last
   death was 5.18 s vs the oracle's 6.52 s (band [6.52, 7.32]).

The pool rule (`src/client/graphics/environment/
spell_effects_config.cpp:236`, "THE POOL RULE"), as carried at `cc85c331`:
every landing plus its held
Source peaked at HALF THE POOL -- applied to the style landing AND to the
overrides' style-drawn landings, with per-record factors for the record
path. The record path itself (B1149's `applySlotEmitter`) already drew the
slots' FILE populations; the rule's record-side machinery (per-record scale
factors) was the retired residue this unit removes.

## 2. The pool rule, re-derived against the record-driven field

The oracle's pool is a CAP, not a scaler: EQGraphicsDX9 gives each emitter a
ring sized from its own words (capped at 100000), and when a ring is full the
rest of that frame's emission is dropped (`cmp edx,[ebp+0x7c]; je
0x100621f7`, 0x10061180..0x10061197); its create path has no global cap
(0x1005e0a0 -> 0x1005a430). Fable applies the same rule at its ONE pool:
`SimulationWorker::allocateParticle()` returns -1 when the free list is
empty (`src/client/graphics/sim_particles.cpp:774`) -- new particles are
dropped while the pool is full, no count is scaled. The pool size rides the
worker input from this config (`src/client/graphics/sim/
renderer_sim_bridge.cpp:470`). All three cites are in
`refactor/B1149/README.md` sec. 4.

So the rule is, restated in `spell_effects_config.cpp:236` and the
`landingPopulation.rule` text in `config/spell_effects.json`:

- a spell with NO record lands the style (impact + spellComplete); that
  style landing plus its own held Source peaks at HALF THE POOL (two
  overlapping no-record landings fit), and `applyPool` rescales its rates;
- a spell WITH a record draws its slots' FILE populations (B1149) and the
  rule scales none of them -- the pool caps at runtime (drop while full);
- the five kept overrides' style-drawn landings keep the half-pool bound.

The largest field a class-level 1-60 spell draws is record 35's
(spellsnew.eff 104, spell 253): 3746 (Source) + 1168 (Target) = 4914 peak
live -- `awk '$1==104' /mnt/projects/fable/review/B1149/pool_census.tsv`
prints `104  Wind104eff V2  3746  1168  4914`. Inside the desktop 8192,
over the sbc 4096 by 818, where the pool fills and drops; its steady field
(~14100 desktop / ~5930 sbc, B1153's probe arithmetic) exceeds both. The
next-largest is record 113 (spell 290): 107 + 2417 = 2524 (same census,
`awk '$1==113'`). Record 80 (spells 93/38) is absent from the census, which
prints only the records with sum > 2048 (its header) -- its field is
<= 2048, inside both pools.

Pinned device-free by `SpellPoolRule.WhirlingWindFieldDrawsTheFileCountsAndThePoolCapsThem`
(`tests/test_spell_colour_and_overrides.cpp`, parametrised over both
platforms' pools from `config/constrained_presets.json`): for records
104/113/80 the record-path style definition carries the STYLE's own
(kStyle-scaled) rates -- no record-specific factor -- and record 104's 4914
is asserted to fit the 8192 pool and over the 4096 one (which fills and
drops). The no-record half keeps its own pin,
`EveryLandingPeaksAtTheRuleForThisPool`.

## 3. The cast-animation term on the record path

The client's rule: eqgame passes a cast-driven stage a duration of
`cast_ms +` the caster model's CastingAnim track length (eqgame `0x49b977`;
the track is `0x47ddd4`, model data -- B1148). B1145 measured the emission's
end at a median 4.35 s past OP_Action (`review/B1145/cast_anim/`): that
median is `EQ::kSpellFxCastAnimationSeconds`
(`include/client/spell/spell_fx_timing.h:57`), carried per caster from the
skeleton track at cast time by `createCastGlow` (`sourceCallerMs =
duration_ms - sourceLatency + castAnimMs`, `src/client/graphics/
spell_visual_fx.cpp:649`) -- so the rendered record SOURCE already honours
the term. What the record path did NOT honour it in was the STYLE
DEFINITION: `getStyleSettings` returned the style's 2.85 s `emitterLifetime`
for a record's landing, so the record-driven last death was
0.33 + 2.85 + 2.0 = 5.18 s (B1153's probe number).

The retire step moves the term onto the record path in ONE place --
`SpellEffectsConfig::getStyleSettings` (`src/client/graphics/environment/
spell_effects_config.cpp:504`): a record's landing emitter (`impact` /
`spellComplete`, `record >= 0`) lives `EQ::kSpellFxCastAnimationSeconds`
-- the client's constant, not a word from a per-spell block (a kept
override's own measured `emitterLifetime` still wins,
`spell_effects_config.cpp:537`). No per-spell constant anywhere: the config
carries no 253 key, and the code names the one shared constant.

Pinned by `SpellStyleOverrides.WhirlingWindLastsAsLongAsTheOracles`
(`tests/test_spell_colour_and_overrides.cpp`): the record-path definition
(`SpellPresets::Impact/SpellComplete(col, 253, 104)` -- record 104 = survey
row 35, `tests/spell_anim_rows.h`) has last death
triggerDelay + emitterLifetime + lifetimeMax = 0.33 + 4.35 + 2.0 = 6.68 s,
inside B1118's band against the oracle's 6.52 s ([6.52, 7.32]), and
asserts `emitterLifetime == EQ::kSpellFxCastAnimationSeconds`. The rendered
record landing is timed from the EDD words (`applySlotEmitter` overwrites
the style timing), so this change moves no rendered pixel for a record
spell -- it is the definition-level end the band governs.

## 4. The override is gone; the reader is gone

`config/spell_effects.json`: the `"253"` key (impact / spellComplete /
castGlow) deleted; `spellOverrides` keeps 38/41/54/458/656. The config's
key census (`tests/test_config_key_reachability.cpp`,
`ConfigKeyReachability`) is green -- its `spellOverrides` doc block now
reads "five measured clouds that stay -- 38/41/54/458/656; the 253 one was
retired by B1161". There was no 253-specific code reader: the override is
data read by the generic `loadSpellOverrides` / `getStyleSettings` merge,
which is pinned to the five by the census. The tombstones sit in the two
comments that named the six (`src/client/graphics/spell_visual_fx.cpp:544`,
`:634`).

## 5. Before/after (the only variable: the 253 retire + the rule)

Sweep: `bash /home/user/kb-evidence/kb-b1161-retire-253/scripts/run_b1161.sh
<before|after>` (evidence `before/` on `cc85c331`, `after/` on
`1150a34a` + the retire diff; geometry copies B1149's sweep scripts
exactly: t_begin 1.5 s, distance 0, seed 1; b1118 secs = 1.5 + cast/1000 +
11.5, b1130 secs = 1.5 + cast/1000 + 13.5; b1118 level 60, b1130 level =
the record's own B1130 level = 5 for record 35). Rendered on the ThinkPad
(`model_viewer --spell-test --headless`), measured with B1139's
`harness_measure.py` (b1118) and B1155's `harness_measure_b1130.py`
(b1130).

### tour-7 on b1118 (level 60; `summary.tsv` per run)

| spell | land_cov before -> after | abs_end before -> after | src_cov before -> after |
|---|---|---|---|
| 253 | 5.306 -> 4.148 | +5.317 -> +5.283 | 3.804 -> 3.323 |
| 38  | 1.993 -> 1.993 | +6.633 -> +6.633 | 0.385 -> 0.385 |
| 41  | 2.478 -> 2.478 | +5.667 -> +5.667 | 1.129 -> 1.129 |
| 54  | 1.204 -> 1.204 | +6.333 -> +6.333 | 0.416 -> 0.416 |
| 458 | 0.500 -> 0.500 | +5.050 -> +5.050 | 0.120 -> 0.120 |
| 656 | 1.204 -> 1.204 | +6.333 -> +6.333 | 0.416 -> 0.416 |
| 93  | 1.993 -> 1.993 | +6.633 -> +6.633 | 0.385 -> 0.385 |

The five kept overrides and the override-free control 93 are bit-identical
(no row worse without an owner); only 253 moves -- the override's landing
geometry and its held-Source words going out. The BEFORE phase reproduces
the baseline the after-tables compare to, `refactor/B1157/b1118_table.tsv`
exactly (tour 253: 5.306 / +5.317 / src 3.804; sweep row r35: 5.297 /
+5.217 / src 3.806), so the AFTER deltas are measured against B1157's own
numbers.

### 253 in band on noov (b1118 PEAK, B1150's method)

`grep ^PEAK <run>/fable_253/shape.txt`: before `PEAK f124 Act+0.88 cov
5.303`, after `PEAK f124 Act+0.88 cov 4.157`. Against the oracle's 6.52 s
(B1118: `refactor/B1118/README.md:185` -- f6543 Act+0.802 -> f6686
Act+6.522, peak f6552, cov 7.28; B1153's denominator), the ratio is
5.303/6.522 = 0.813 (OVER) -> 4.157/6.522 = 0.637, inside the
harness/oracle landing-ratio band 0.45..0.72 (B1150's p25..p90 over the
40-row survey, `refactor/B1150/README.md`). B1153's own probe measured the
same move on its tree: 5.308 (0.814) -> 4.188 (0.642 IN). The `end` column
is info only in this card family (B1150: the missile/Middle stack moved
every end; B1153 gate (a): "end is info only") -- the rendered end
(+5.283) is the EDD fade field's own crossing (record 104's Target slots:
T = -5.0 fixed, L 3.0..3.5, fade-out 3.25..4.0, decoded below), and the
override's words never reached the rendered record path (`applySlotEmitter`
overwrites the style timing), which is why it does not move with the
retire.

### record 35 on b1130 (level 5; `b1130_rows.tsv` per run)

| | before | after |
|---|---|---|
| landing_onset | +0.117 | +0.317 |
| landing_end | +5.150 | +5.217 |
| landing_cov | 1.785 | 1.203 |
| source_cov | 0.947 | 0.889 |

The b1130 preset has no oracle row of its own (B1153: the decision is taken
on the b1118 lane numbers; the b1130 half is the withov/noov delta on that
geometry) -- the delta is the override's words leaving, nothing else moves.

### The pool: no overflow, desktop and sbc

The pool drops when full by construction (`sim_particles.cpp:774`), so the
live count never exceeds the pool on either platform. Record 104's field
(4914 peak; ~14100 desktop / ~5930 sbc steady) fits the desktop 8192 peak
and overflows nothing -- the sbc 4096 fills and drops the rest of the
frame's emission, exactly the oracle's ring-full rule (B1149 sec. 4).
Pinned by the parametrised `SpellPoolRule` test (both platforms from
`config/constrained_presets.json`: 8192 / 4096).

### Record 104's EDD words (decoded from the shipped files)

`/home/user/eq-oracle/client/spellsnew.eff` record 104 ("Wind104eff V2") +
`spellsnew.edd` (offsets per `docs/spellsnew_format.md`; entries are 1-based
after the 8-byte magic, 416 bytes each):

| stage | slot | edd | T | L | burst | perTick | tps | fade-out |
|---|---|---|---|---|---|---|---|---|
| Source | 0 (lvl 39) | 428 | 0 (caller) | 1.0 | 65 | 9 | 75 | 1.2 |
| Source | 1 (lvl 24) | 429 | 0 (caller) | 1.0 | 65 | 9 | 75 | 1.2 |
| Source | 2 (lvl 24) | 430 | 0 (caller) | 1.5 | 8 | 8 | 100 | 1.2 |
| Source | 3 (lvl 0)  | 431 | 0 (caller) | 1.75 | 8 | 8 | 75 | 1.2 |
| Target | 0 (lvl 24) | 432 | -5.0 | 3.5 | 2 | 3 | 75 | 3.25 |
| Target | 1 (lvl 0)  | 433 | -5.0 | 3.0 | 2 | 3 | 25 | 4.0 |
| Target | 2 (lvl 39) | 434 | -5.0 | 3.0 | 2 | 2 | 25 | 4.0 |

T = 0 takes the caller's duration (Source: cast + 4.35; the render log's
slot ranges give the run as 5.44 s, i.e. the client's own 1.50 s cast
minus the 0.41 s stage latency plus the term -- `after/frames/
b1161_after_tour_253/run.log` `createCastGlow ... slots 0:2@1+0.41..6.85`);
T = -5.0 is a fixed 5.0 s run (Target: no caller duration). The slot ranges
the renderer logs (each range = stage start..last death of that slot):
Source `+0.41..6.85/6.85/7.35/7.60` relative to OP_BeginCast (the 0.41 s
stage latency; last death BC+7.60 = Act+6.10), Target `+0.33..8.83/8.33/
8.33` relative to OP_Action (the 0.33 s stage latency; last death
Act+8.83) -- same log, `createLanding ... slots 0:3/2@1+0.33..8.83 ...`.
The visible end is the field's fade, earlier on both.

## 6. Verification

- Affected suites (the 27 spell/override/timing/reachability suites):
  `cd <worktree> && rbuild kb-b1161-retire-253 test -R 'AnimRows|CastAnimation|CastAnimationEntity|ConfigKeyReachability|FxHarness|SlotRules|SlotRulesPoints|SlotRulesSim|SourceAnchors|SourceRecordRows|SpellBurstOracleMatch|SpellBurstPool|SpellBurstPoolSizing|SpellColourChain|SpellColourFromSpriteArt|SpellFxTimingFiles|SpellFxTimingRules|SpellLandingPool|SpellLandingVolume|SpellParticlePoolWiring|SpellPoolExceptions|SpellPoolPlatforms|SpellPoolRule|SpellStyleOverrides|TargetAnchors|TargetLandingBridge|TargetRecordRows'`
  -> `100% tests passed, 0 tests failed out of 125` (14 skipped: the
  `CastAnimationEntity` live cases that need the oracle models; evidence
  `affected_suites.log`).
- Hygiene (doc size/counts/claims/citations + comment claims): `rbuild
  kb-b1161-retire-253 hygiene` -> `100% tests passed, 0 tests failed out of
  48`, exit 0.
- The full ctest suite runs on the GitLab runner (CI `build-test`, B1042);
  the worktree build tree is partial by construction (rbuild builds named
  targets), so the local run (evidence `full_suite.log`) is `32% tests
  passed, 453 tests failed out of 662` with every failure a missing binary:
  450 `*_NOT_BUILT` plus the three asset-pin tests
  (`TexturesReproducible` / `PackReproducible` / `DbReproducible`), whose
  error names the absent generator (`generate_textures`, `eqe1_builder`,
  `willeq_db_builder`). This diff touches no `data/` pin (the changed files
  are the 8 in `git diff --stat`).
- Cite checker (B1156):
  `python3 -I scripts/refactor/cite_check.py refactor/B1161/README.md --tree . --extra /mnt/projects/fable --dis /mnt/projects/fable/review/B1141/ghidra/out/eqgame.dis --dis /mnt/projects/fable/review/B1141/ghidra/out/EQGraphicsDX9.dis`
  -> `checked 18 cites, 0 problems, 0 address warnings`
