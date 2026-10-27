# B1161 (#480): retire the 253 spellOverride -- the pool rule re-derived against the record-driven field, the cast-animation term on the record path

Branch `task/kb-b1161-retire-253`. The issue is the specification (`glab
issue view 480`). It follows from B1153's FOLLOW-UP items 2 and 3
(`refactor/B1153/README.md`, "FOLLOW-UP (proposed, no cards created)").
This is the rework after the cold review of MR !314 (note 7654, 13
findings). Evidence for the rework (before and after, local renders, the
scripts, ctest logs) is in `/mnt/projects/fable/review/B1161/worker/rework/`.
The first pass's evidence stays in `/mnt/projects/fable/review/B1161/worker/`
for history; none of its numbers are used below.

## 0. Result

- The 253 (record 35) `spellOverride` is gone from `config/spell_effects.json`.
  `spellOverrides` keeps five spells: 38, 41, 54, 458 and 656.
  `ConfigKeyReachability` is green.
- The pool rule, re-derived against the record-driven field (sec. 2): a spell
  with a spellsnew.eff record draws its slots' FILE populations, and no rule
  scales them. The pool (8192 desktop, 4096 sbc) is a CAP, as in the oracle.
  Three ctests pin it:
  - records 104, 113, 91 and 80 are re-derived from their EDD words;
  - record 104 runs through the real worker: 4793 live at 8192, and 4071 at
    4096, where the pool fills and drops the rest;
  - record 0 is the no-record path.
- The cast-animation term (sec. 3) is the client's SOURCE duration. eqgame
  passes a Source stage `cast_ms` + the CastingAnim track, and passes a Target
  stage no duration. The record path already carries the term in
  `createCastGlow`'s Source caller duration.
  - The first pass also wrote the term into the landing style definitions
    (`getStyleSettings`, `record >= 0`). The client has no such rule. It also
    caught record 0 = none (210 spells) and would have pushed every record
    spell's definition out of band (review findings 1 and 2). That term is
    removed.
  - Record 104's computed Source end is Act+6.10, against the oracle's Source
    end of 6.073.
  - The 253 LANDING end does NOT meet the B1118 band. The rendered landing
    ends at +5.250 against the oracle's 6.522, before and after this card. This
    is a record-path gap that this card does not close (sec. 3, FOLLOW-UP).
- The 253 tour row is IN band on noov (sec. 5). The landing PEAK coverage
  ratio, harness over oracle 7.281, moves from 0.731 (over the B1150 p25..p75
  band of 0.45..0.72) to 0.577 (in).
- The b1130 record-35 row moves three axes further from the oracle
  (source_cov, landing_cov, p90_src) and one into band (src_h_lo), against
  B1155's nine banded columns. The measured cause is the override's words
  leaving (sec. 5). The owner is named in FOLLOW-UP 2.

### Acceptance (issue #480, lines A-F)

| line | requirement | status | rests on |
|---|---|---|---|
| A | pool rule re-derived + ctest | MET | sec. 2; `tests/test_spell_colour_and_overrides.cpp:588`, `tests/test_spell_landing_volume.cpp:361` |
| B | cast-animation term on the record path, with a cited rule | MET for the term (Source caller duration, eqgame 0x47f5e1..0x47f5ed). The 253 landing END is NOT MET: +5.250 after vs the oracle's 6.522 (+5.317 before). Owner B1154 #474 | sec. 3; `src/client/graphics/spell_visual_fx.cpp:649`; FOLLOW-UP 1 |
| C | override gone, tables, 253 in band, sbc census | MET: `spellOverrides."253"` gone; 253 PEAK ratio 0.577 (band 0.45..0.72); sbc 4071 of 4096 | sec. 4, sec. 5 (b1118 table), sec. 2 (`test_spell_landing_volume.cpp:361`) |
| D | no row worse without an owner | MET: the only row that moves further, b1130 r35, is owned by FOLLOW-UP 2 | sec. 5 (record 35 on b1130, tour-7 table) |
| E | ctest + hygiene + CI | MET: 128/128 card suites, hygiene 48 (1 skipped), CI green at every pushed tip (2228 @ 37db24cb, 2243 @ 27003d64; the merge tip's pipeline is the MR head pipeline) | sec. 6 |
| F | `Closes #480` | MET | the MR description |

## 1. Where the card started

The 253 override at `1150a34a` (the B1153 merge, which is this branch's merge
base) was `config/spell_effects.json` `spellOverrides."253"`. It held
`impact` + `spellComplete` (B1120/B1122's measured landing geometry and sizes)
and `castGlow` (a 1950/s Source cloud). Its `emitterLifetime 4.35` was B1145's
cast-animation term.

B1153's retirement probe kept 253 for two reasons (its README, "The 253
retirement probe (and why it was reverted)"):

- `SpellPoolRule` failed on a modelled steady field of ~14100 / ~5930 sprites.
  That number was B1153's STYLE model, `steady(impact(253)) +
  steady(complete(253))`, not the file's field (sec. 2 gives the file's).
- `WhirlingWindLastsAsLongAsTheOracles` failed: the style DEFINITION's last
  death was 5.18 s against the band [6.52, 7.32].

Both failures were about the style definition. The rendered record path
builds its landing from record 104's EDD slots, and the slot emitter
overwrites the definition's timing and counts
(`src/client/graphics/environment/spell_emitter_presets.cpp:430`,
`c.emitterLifetime = std::max(t.window(), kOneStep)`).

## 2. The pool rule, re-derived against the record-driven field

In the oracle the pool is a CAP, not a scaler. EQGraphicsDX9 gives each
emitter a ring sized from its own words. When a ring is full, the rest of that
frame's emission is dropped (`cmp edx,[ebp+0x7c]; je 0x100621f7`,
0x10061180..0x10061197). The create path has no global cap (0x1005e0a0 ->
0x1005a430). Fable applies the same rule at its ONE pool:
`SimulationWorker::allocateParticle()` returns -1 when the free list is empty
(`src/client/graphics/sim_particles.cpp:774`). New particles are dropped while
the pool is full, and no count is scaled. These cites are B1149's
(`refactor/B1149/README.md` sec. 4).

The rule, restated in the comment at
`src/client/graphics/environment/spell_effects_config.cpp:236` and in
`landingPopulation.rule` in `config/spell_effects.json`:

- A spell with NO record (field 145 = 0) lands the style. That style landing
  plus its own held Source peaks at HALF THE POOL, so two such landings fit,
  and `applyPool` rescales their rates.
- A spell WITH a record draws its slots' FILE populations. The rule scales
  none of them, and the pool caps at runtime.
- The five kept overrides' style-drawn landings keep the half-pool bound.

Each record's field is re-derived from its EDD words. Per slot it is burst +
perTick x tps x min(window, L) (`include/client/spell/spell_fx_timing.h:119`,
`peakLive`). Source slots use the caller duration cast + 4.35 s; Target slots
use none. Each stage sums every slot a level-60 caster draws, which is an
upper bound. The four records below are the three largest fields of the 40
B1130 records plus a small one. The census rows come from
`python3 -I review/B1149/scripts/pool_census.py` (that script decodes the
same words with its own reader). The 80 / 89 / 82 / 121 rows are not in the
committed census, which prints only sums > 2048. They are the same script
filtered to those records:

| record | spell | Source | Target | field | 8192 | 4096 |
|---|---|---|---|---|---|---|
| 104 | 253 Whirling Wind | 3746 | 1168.5 | 4914.5 | fits | over by 818 |
| 113 | 290 Color Flux | 107 | 2417.12 | 2524.12 | fits | fits |
| 91 | 115 Dismiss Summoned | 1554 | 796 | 2350 | fits | fits |
| 80 | 93 / 38 | 304.5 | 1302.5 | 1607 | fits | fits |

`awk -F'\t' '$1==104 || $1==113 || $1==91' /mnt/projects/fable/review/B1149/pool_census.tsv`
prints the first three rows: `4914`, `2524` and `2350`. The 40-record set is
`tests/spell_fx_timing_oracle.h`. Of its 40 records, only 104, 113, 91 and
112 (2338) have a census row.

The rule is pinned by three tests, all device-free:

- `SpellPoolRule.TheRecordFieldsAreTheFileCountsAndOnlyRecord104ExceedsAPool`
  (`tests/test_spell_colour_and_overrides.cpp:588`) runs at both platform
  pools. It decodes the four records slot by slot with `SpellsNewEffReader`
  and checks each stage against the census within 0.5. It then checks that
  104 is over 4096 and inside 8192, and that the other three fit both pools.
- `SpellLandingPool.TheLargestRecordFieldIsCappedNotScaled`
  (`tests/test_spell_landing_volume.cpp:361`) exercises the cap through the
  real `SimulationWorker`. It builds record 104 from the shipped EDD the way
  `createCastGlow` / `createLandingFromSlots` do: 4 Source slots at
  OP_BeginCast, then 3 Target slots x 2 halves at OP_Action (1.75 s). It
  prints `[ B1161 ] record 104 field peak 4793 live at pool 8192, 4071 at
  pool 4096`:
  - On 8192 the field runs at the file's counts. It peaks at 4793, which is
    over the sbc pool and was never scaled down.
  - On 4096 it fills the pool. The peak is 4071, the pool less the sprites
    that die inside the counted frame. That is 722 fewer live at the peak
    (4793 at pool 8192 vs 4071 at pool 4096); the test measures no drop
    count.

  This is the sbc census that issue Ask 3 asks for, measured. Local run:
  `affected_suites.log`.
- `SpellStyleOverrides.RecordZeroIsTheNoRecordPath`
  (`tests/test_spell_colour_and_overrides.cpp:635`) covers spells 0, 1106,
  253, 93, 290 and 54, for every style. Record 0, record -1 and the spell's
  real record give the same `emitterLifetime`, rates and burst, and the same
  landing last death.

## 3. The cast-animation term on the record path

The client's rule: eqgame passes a cast-driven stage (the Source) a duration
of `cast_ms` plus the caster model's CastingAnim track (eqgame `0x49b977`;
the track is `0x47ddd4`, B1148). It passes the Target stage none (eqgame
`0x47f5e1`). `SpellFxCallerDurationMs`
(`include/client/spell/spell_fx_timing.h:87`; its Target arm at
`include/client/spell/spell_fx_timing.h:95` is `return 0;`) encodes both. B1145 measured the term as a median 4.35 s
(`EQ::kSpellFxCastAnimationSeconds`,
`include/client/spell/spell_fx_timing.h:57`). B1148 reads it per caster from
the skeleton track.

The record path carries the term in ONE place: `createCastGlow`'s Source
caller duration (`sourceCallerMs = SpellFxCallerDurationMs(...)`,
`src/client/graphics/spell_visual_fx.cpp:649`). It has done so since
B1145/B1148. The landing passes `SpellFxCallerDurationMs(SpellFxStage::Target,
0, 0)` (`src/client/graphics/spell_visual_fx.cpp:1012`). No style word
reaches either path, because `applySlotEmitter` overwrites the definition's
timing.

The first pass added a second copy of the term to `getStyleSettings`, for the
`impact` / `spellComplete` landing definitions of any `record >= 0`. That
copy is removed. The style lookup adds nothing per record
(`src/client/graphics/environment/spell_effects_config.cpp:478`,
`(void)record;`) for three reasons:

- It is not the client's rule: the client gives the landing no duration.
- It caught record 0, which means "no record". 210 spells have field 145 = 0,
  `getRecord(0)` is nullptr (`src/client/spell/spells_new_eff_reader.cpp:121`),
  and those spells' definitions moved from 5.18 s to 6.68 s.
- It changed no rendered record landing. It only moved a definition that the
  B1121 end-band test then read. With real records, every record spell's
  definition left that band; 93 and 38 (record 80) reached 6.68 s against
  [4.87, 5.67].

The header doc that led to the condition is corrected
(`include/client/graphics/environment/spell_effects_config.h:135`). It now
reads field 145, 0 = none.

Pinned by `SpellStyleOverrides.WhirlingWindsRecordPathCarriesTheTermOnItsSource`
(`tests/test_spell_colour_and_overrides.cpp:682`), from record 104's EDD
words:

- The Source caller is 1750 + 4350 = 6100 ms.
- The four Source slots end at BC+7.85 = Act+6.10, which is 1.75 + 4.35 plus
  the longest Source life of 1.75, less the 1.75 s cast.
- The oracle's caster-box Source end for row 35 is Act+6.073 (B1145,
  `tests/spell_fx_timing_oracle.h:67`), inside B1145's 0.5 s.
- The Target takes no caller duration. Its three slots end 8.5 s after the
  stage starts, matching `review/B1145/crosscheck.tsv` row 35
  (`target_end_l60 8.5`).

`SpellBurstOracleMatch.EveryMeasuredLandingEndsWhereTheOraclesDoes`
(`tests/test_spell_burst_oracle_match.cpp:257`) now builds each spell with its
client record: 93/38 -> 80, 54/656 -> 82, 41 -> 89, 458 -> 121
(`spells_us.txt` field 145). All six stay in their bands because the record
adds nothing to a definition. 253 left that table with its override: its
definition is now the style's, and its landing is the record's slots.

### The 253 run against the oracle's ends (rendered, the AFTER tour run)

Rendered log, `after/frames/b1161_after_tour_253/run.log`:

- `createCastGlow ... slots 0:2@1+0.41..6.85 1:2@1+0.41..6.85
  2:2@1+0.41..7.35 3:2@1+0.41..7.60`. These are times from OP_BeginCast; the
  plan's cast is 1750 ms (`after/plans/b1161_after_tour_253.plan`).
- The Source run is 1.75 - 0.41 + 4.10 = 5.44 s. The 4.10 s is the harness
  caster's own CastingAnim track (B1148, `refactor/B1149/README.md` sec. 5),
  not the 4.35 s fallback.
- The Source's last death is BC+7.60 = Act+5.85.
- `createLanding ... slots 0:3/2@1+0.33..8.83 1:3/2@1+0.33..8.33
  2:3/2@1+0.33..8.33`, times from OP_Action. The Target's last death is
  Act+8.83.

The harness measures the landing's VISIBLE end (`abs_end`, `summary.tsv`) at
+5.317 before and +5.250 after. The oracle's is 6.522 (B1118, lane 5,
`refactor/B1118/README.md:185`: f6543 Act+0.802 -> f6686 Act+6.522).
`review/B1142/gate/regression_table.tsv` row 253 has `oracle_end 6.522`.
The record-driven run therefore ends 1.27 s short of the oracle's landing
end. The override never changed that end (+5.317 with it). This card does not
close it either: the rendered record landing is timed by record 104's EDD
fade field (Target T = -5.0 fixed, L 3.0..3.5, fade-out 3.25..4.0). The end
is info only in this card family (B1153 gate (a)), but issue Ask 2 asks for
it, so it is reported as NOT met with these numbers (FOLLOW-UP 1).

## 4. The override is gone; the reader is gone

`config/spell_effects.json` loses the `"253"` key (impact / spellComplete /
castGlow); `spellOverrides` keeps 38/41/54/458/656. No code read 253
specifically. The override was data, read by the generic `loadSpellOverrides`
/ `getStyleSettings` merge, and the key census in
`tests/test_config_key_reachability.cpp` (`ConfigKeyReachability`, green)
pins it to the five. The two comments in `src/client/graphics/spell_visual_fx.cpp`
that named the six now name the five.

## 5. Before/after (the merge base vs the fixed tip, one variable)

Both phases were rendered and measured LOCALLY on dev-u24 by the same script:
`bash /mnt/projects/fable/review/B1161/worker/rework/scripts/run_b1161_local.sh
<phase> <tree> <model_viewer>`.

- **before**: the merge base `1150a34a`, a `git archive` tree with its own
  Release build.
- **after**: the fixed tip `7a76f033`, recorded in `after/sweep.log`.

The geometry is the first pass's (B1149/B1157's sweep):

- t_begin 1.5 s, distance 0, seed 1, 30 fps, 60 Hz;
- b1118 secs = 1.5 + cast/1000 + 11.5, at level 60;
- b1130 secs = 1.5 + cast/1000 + 13.5, at level 5, record 35's B1130 level.

Measurement used B1139's `harness_measure.py` for b1118 and B1155's
`harness_measure_b1130.py` for b1130. The tables come from
`bash .../rework/scripts/tables.sh`.

`git diff --stat 1150a34a..7a76f033 -- src include config` touches config
(the 253 key and the rule text), comments, and `getStyleSettings`, which
returns to its `1150a34a` behaviour. So the only rendered variable is the
253 key. The first pass compared a BEFORE on `cc85c331` with an AFTER on
`1150a34a` + diff, which spanned B1153's rework (review finding 8); that
comparison is superseded.

### tour-7 on b1118 (`summary.tsv` per run: land_cov / abs_end / src_cov)

| spell | before (1150a34a) | after (7a76f033) |
|---|---|---|
| 93 | 1.993 / +6.633 / 0.386 | 1.993 / +6.633 / 0.384 |
| 38 | 1.993 / +6.633 / 0.386 | 1.993 / +6.633 / 0.384 |
| 54 | 1.199 / +6.333 / 0.414 | 1.199 / +6.333 / 0.414 |
| 656 | 1.199 / +6.333 / 0.414 | 1.199 / +6.333 / 0.414 |
| 41 | 2.492 / +5.667 / 1.131 | 2.492 / +5.667 / 1.130 |
| 253 | 5.326 / +5.317 / 3.818 | 4.169 / +5.250 / 3.331 |
| 458 | 0.501 / +5.050 / 0.120 | 0.501 / +5.050 / 0.120 |

Every measured value of rows 54, 656 and 458 is identical. Row 54 is not
bit-identical: 132 of its 435 frames differ, with every measured value the
same. For 656, 0 of 435 frames differ, and for 458, 0 of 483 frames differ
(458 renders 483 frames; `cmp` over `frames/*/frame_*.png`).

Rows 93, 38 and 41 differ in src_cov by 0.002 / 0.002 / 0.001. Every one of
their 435 frames differs, starting at frame 0, by 17..126 pixels per frame
(median 33 / 33 / 35), all inside y 387..604, x 604..714. Frame 0 is 1.5 s
before OP_BeginCast, so no spell is drawn in it. In frame 0 of 93, 29 pixels
differ, inside y 478..510, x 636..654: z-fighting speckle where the target's
held-weapon model meets its body (crop `rework/crop93.png`). This is
renderer non-determinism in the model pass, not the spell path.

None of these rows moves its land_cov or end, so no row other than 253 is
worse (acceptance D).

The local renderer is not the ThinkPad's. B1157's ThinkPad baseline
(`refactor/B1157/b1118_table.tsv`) has tour 253 at 5.306 / +5.317 / 3.804;
the local before has 5.326 / +5.317 / 3.818. So the comparison above is
local before vs local after. B1157's table is quoted only for orientation.

### 253 in band on noov (b1118, B1150's ratio)

B1150 defines the ratio as harness coverage / oracle coverage
(`refactor/B1150/README.md:262`). Its band is the ratio's p25..p75 over the
40 rows, 0.45..0.72 (`refactor/B1150/README.md:338`). The oracle's 253
landing coverage is 7.281 (`review/B1142/gate/regression_table.tsv` row 253,
`oracle_land_cov`). PEAK lines come from `grep ^PEAK
<measure>/<run>/fable_253/shape.txt`.

| run | before PEAK cov | ratio | after PEAK cov | ratio |
|---|---|---|---|---|
| tour 253 (self, the acceptance row) | f130 Act+1.08 5.326 | 0.731 OVER | f124 Act+0.88 4.204 | 0.577 IN |
| r35_b1118 (other, level 60) | f124 Act+0.88 5.320 | 0.731 OVER | f124 Act+0.88 4.171 | 0.573 IN |

The summary `land_cov` gives the same verdicts: tour 5.326 -> 4.169 (0.731
-> 0.573) and r35 5.317 -> 4.146 (0.730 -> 0.569).

B1153's decision table printed this move as "0.814 -> 0.642", which is
coverage divided by the oracle's end TIME of 6.522 (FOLLOW-UP 3). With the
correct units it is 5.308 / 7.281 = 0.729 -> 4.188 / 7.281 = 0.575.

### record 35 on b1130 (level 5, `b1130_rows.tsv`)

| | before (1150a34a) | after (7a76f033) | oracle (B1157 b1130 row 35) | B1157 harness |
|---|---|---|---|---|
| source_cov | 0.953 | 0.891 | 1.142 | 0.953 |
| landing_cov | 1.791 | 1.207 | 7.283 | 1.791 |
| landing_end_s | +5.150 | +5.217 | 6.950 | 5.217 |
| landing_onset_s | +0.117 | +0.317 | | |

The oracle and B1157 columns come from
`refactor/B1157/b1130_table.tsv.after.tsv` row 35. Before and after, every
axis is short of the oracle (B1157's verdicts: short / short / short).

Against B1155's nine banded columns (`refactor/B1155/README.md:163-171`),
the retire moves THREE axes FURTHER from the oracle and ONE into band:

- landing_cov 1.791 -> 1.207 (-0.584);
- source_cov 0.953 -> 0.891 (-0.062);
- p90_src 0.124 -> 0.111, short of the band 0.134..0.182 (0.158 +- 15 %)
  both times;
- src_h_lo 0.150 -> 0.170 moves INTO its band 0.16..0.46 (0.31 +- 0.15).

landing_end moves toward the oracle (5.150 -> 5.217).

The measured cause is the single variable, the 253 key. Before, the
override's landing words drew a denser field: B1120's frame-filling geometry
and B1123's sizes (startMid 0.496 / 0.6105 for impact / spellComplete,
against the style's 0.198 / 0.244, `tests/test_spell_colour_and_overrides.cpp`
`B1123SizesAreTheOraclesRoi`). Its 1950/s Source cloud added to the Source.
After, record 104's EDD field draws its own sparse shapes and sizes. That is
B1150's under-spread case (`refactor/B1150/README.md:332`, "The under-spread
rows"): the retired dense cloud lit more pixels than the record's EDD shape.

The b1118 noov row moved INTO band. The b1130 row moved three axes further below
bands it was already below (and one axis into band). Owner: the record-104 coverage gap on the record path
(FOLLOW-UP 2). The issue asks for the 253 row in band on noov, and it is.

### Record 104's EDD words (decoded from the shipped files)

These come from `/mnt/projects/EverQuestP1999/spellsnew.eff` record 104
("Wind104eff V2") and `spellsnew.edd`. Entries start after the 8-byte magic,
416 bytes each; offsets per `docs/spellsnew_format.md`. The cold review
re-decoded and confirmed this table.

| stage | slot | edd | T | L | burst | perTick | tps | fade-out |
|---|---|---|---|---|---|---|---|---|
| Source | 0 (lvl 39) | 428 | 0 (caller) | 1.0 | 65 | 9 | 75 | 1.2 |
| Source | 1 (lvl 24) | 429 | 0 (caller) | 1.0 | 65 | 9 | 75 | 1.2 |
| Source | 2 (lvl 24) | 430 | 0 (caller) | 1.5 | 8 | 8 | 100 | 1.2 |
| Source | 3 (lvl 0)  | 431 | 0 (caller) | 1.75 | 8 | 8 | 75 | 1.2 |
| Target | 0 (lvl 24) | 432 | -5.0 | 3.5 | 2 | 3 | 75 | 3.25 |
| Target | 1 (lvl 0)  | 433 | -5.0 | 3.0 | 2 | 3 | 25 | 4.0 |
| Target | 2 (lvl 39) | 434 | -5.0 | 3.0 | 2 | 2 | 25 | 4.0 |

## 6. Verification

- **Card suites**, the 27-suite regex of the card (`AnimRows|CastAnimation|...|TargetRecordRows`):
  - Locally, on the fixed tip with `EQ_CLIENT_PATH=/mnt/projects/EverQuestP1999`
    so the EDD-reading tests run: `100% tests passed, 0 tests failed out of
    128`, 1 skipped (`CastAnimationEntity.AModelWithoutTheTrackKeepsItsCurrentAnimation`).
    Evidence `rework/affected_suites.log`.
  - Through `tbuild` on the ThinkPad, where the tree has no `data/eq_client`:
    `100% tests passed, 0 tests failed out of 128`, 10 skipped. The skips
    are the 6 `CastAnimationEntity` cases,
    `SpellColourChain.TheMeasuredStagesAreWhatSpellsEffActuallyHolds`, and
    three of this card's EDD tests, which skip without the client files.
    Evidence `rework/affected_suites_thinkpad.log`.
  - CI links `data/eq_client` to the client (`.gitlab-ci.yml`), so the EDD
    tests run there.
- **Hygiene**: `tbuild <wt> hygiene` (the CI hygiene job's `ctest -L
  hygiene`) -> `100% tests passed, 0 tests failed out of 48`, 1 skipped
  (`MemoryCapHuntScript`). Evidence `rework/hygiene.log`.
- **Cite checker** (this README):
  `python3 -I scripts/refactor/cite_check.py refactor/B1161/README.md --tree . --extra /mnt/projects/fable --dis /mnt/projects/fable/review/B1141/ghidra/out/eqgame.dis --dis /mnt/projects/fable/review/B1141/ghidra/out/EQGraphicsDX9.dis`
  -> `checked 37 cites, 0 problems, 0 address warnings`.
- **Pipeline**: the final push runs with CI. Its id and status are in the
  MR description and the MR note.
- **Changed files**: this branch changes 10 (`git diff --stat
  1150a34a..HEAD`): config, the header, the config `.cpp`,
  `spell_visual_fx.cpp`, four test files, this README, and CLAUDE.md (T441 paragraph only). No `data/` pin,
  golden, manifest, baseline or preset is touched.

## FOLLOW-UP (proposed, no cards created)

1. **253's rendered landing end.** It is +5.250 against the oracle's 6.522
   (B1118), and record 104's computed Target last death is 8.5 s against
   B1130's 6.950. The end is set by record 104's EDD fade field (Target
   fade-out 3.25..4.0, L 3.0..3.5), not by any style word. Issue Ask 2's
   "end within the B1118 band" is not met. It needs the visible-end
   mechanism of the EDD fade (the B1154 #474 fade-onset line).
2. **Record 104's coverage on the record path.** At b1130 it is 1.207
   landing / 0.891 Source against the oracle's 7.283 / 1.142. At b1118 it is
   in band (0.577). This is B1150's sparse-EDD-shape case, now without the
   dense override that masked it at b1130.
3. **`refactor/B1153/README.md` carries the same ratio-units error as this
   card's first pass.** Its decision table (line 122 and the gate table at
   line 198) prints 0.814 / 0.642, which is coverage / oracle END TIME
   (6.522). The B1150 ratio is coverage / oracle coverage 7.281: 0.729 /
   0.575. Not fixed here.
