# B1144 (#464) FX-WIRE-EFF-RULES: fable draws spellsnew.eff by its slot words

Branch `task/B1144-fx-eff-rules` (wt/b1144), cut from the unmerged B1134 tip fb6d6481 (on fable e57e1f85).
Evidence: `/mnt/projects/fable/review/B1144/` (its `README.md`, `progress.log`, `runs/`, `oracle/`, `measure/`).
Source of truth: `docs/spellsnew_format.md` and B1141's `review/B1141/eff_fields_from_code.tsv` (every row cites
its eqgame.exe address).

## Prior work found

- `git log` of `spell_visual_fx.cpp`, `spells_new_eff_reader.cpp`, `spell_fx_bone_anchor.h`,
  `config/spell_effects.json`, `spell_cast_fx.cpp`: B1127 (field-145 art), B1131 (Source rows), B1132 (Target
  rows), B1137 (rows keyed by field 145), B1138 (the harness), B1134 (rows 40/51, the observer rig). No commit had
  used the slot words: the reader exposed them raw as `b`/`actor`/`d`, "not used here".
- B1141 README sec. 4 lists exactly what fable ignored: the level gate, anchor 2, Middle-only-with-missile.
- B1105 (`refactor/B1105/README.md`) armed a 2 s Source on an action-only cast, "an implementation bound, not an
  oracle duration claim"; its lane proof showed no glow. B1134 then measured the real /disc: nothing drawn.
- Every oracle capture behind the measured rows ran at `#level 60` (B1118/B1127/B1130 plans: `command #level 60`),
  so every slot of every row was drawn when it was measured (minLevel is 0, 24 or 39; 1/2 on 9 Middle slots).
- Formats touched: spellsnew.eff (B1141 layout), `config/spell_effects.json` (read only, no key added), the
  harness plan TSV (`tools/model_viewer/README.md`), `slots.tsv` (new), OP_SpawnAppearance (8 bytes).

## The rules (from the code) and where fable applies them

| rule (eqgame.exe) | fable |
|---|---|
| slot +4: drawn iff minLevel <= the caster's level; NPC or absent caster = 1 (0x495cc3) | `SpellsNewEffSlot::drawnAtLevel`, `SpellsNewEffReader::drawnSlots`, `SpellsNewEffGateLevel`; `SpellCastVisual*Data::fxGateLevel` filled by `SpellManager` from the caster's Spawn_Struct level / NPC byte |
| the spawn level byte is written by OP_SpawnAppearance type 1 (0x45242f: `{spawn, type, param}`, type 1 -> spawn +0x24c, spawn 0 = self) | `EntitiesComponent::ZoneProcessSpawnAppearance` `AT_WHO_LEVEL` (was unhandled) |
| slot +8 anchor: 0 world, 1 caster, 2 missile, 3 target | per slot in `createCastGlow` / `createLandingFromSlots`; 0 = the stage's entity, unattached |
| slot +12 attach point (0x495cc3, SPELLPOINT_* at 0x47c17e) | `SpellVisualFX::attachPointEntityId`: LHAND/RHAND -> hand bones, HEAD/LEYE/REYE/MOUTH -> head bone, every other point -> the entity |
| Middle plays only from the missile spawner (0x476b12) | not drawn (fable has no missile path; B1128/B1147) |
| Source plays from begin-cast only: 0x49b39d, or 0x4bfe0f replaying a Source 0x49b39d deferred (the only writer of actor gfx +0xecc/+0xed4; constructor 0x48f023 clears them) | `CastGlowRoute` / `ApplySpellCastCompleteFx`: no action-only Source (B1105's glow removed) |
| a stage draws its slots; no Target slot = no landing | `createLandingFromSlots` returns 0 and draws nothing |

What stays as measured (Will 2026-10-23): shape class, sprite sizes, lifetimes, onset and hold come from the
B1131/B1132 rows; durations are B1145's; colour/blend B1146's. Each drawn slot is ONE emitter (the landing: one
spellComplete + impact pair) carrying `1/N` of its row's population, N = the stage's emitter slots (what the
level-60 capture drew), so a level-60 cast draws the row's whole population. The equal split is fable's: the EDD
per-emitter counts (+0x80/+0x84/+0x88) are B1147's. A spell whose B1122 override gives its Source or landing its
own spawnShape keeps that measured geometry on the entity for every slot (the level gate and art still apply). A
spell with no spellsnew.eff record (field 145 = 0, or an empty record) draws its row's own anchor as before (the
client plays spells.eff there, 0x49b260).

Art: a Source slot and a landing's impact half draw their slot emitter's texture; the spellComplete half keeps
T441's Source art, now the textures of the Source slots the same level draws. Measured on the harness (below):
giving the spellComplete half the Target slots' art instead drops 93's landing coverage 18 % (2.39..2.46 against
2.90..3.01), because B1123's populations were calibrated on the Source art.

## Measured anchors as the cross-check (`review/B1144/anchor_crosscheck.tsv`)

The 47 field-145 records with a measured row, file slots against the row's anchor:
- Source: all 9 hands/head rows have LHAND+RHAND / HEAD slots; the 36 body/orbit/feet-ring rows are DEFAULT/CHEST
  slots (where DEFAULT sits on a model is model data; the row places it). 2 discipline duplicates.
- Target: 33 target rows have only anchor-3 slots; record 113 (row 27) and 81 (row 11) are anchor 1 throughout
  (the measured caster landings); 95/98/111 are self-only spells whose slots say target (= the caster); 78 (row 40,
  ports) and 94 (row 8) mix caster and target slots, both self-only; 74 (row 5) is HEAD; 326/328/329/330 have no
  Target stage.

## Gates

### Harness: 93 parity (`review/B1144/runs/`, `--camera b1118 --target-death 0`, pool 8192)

| | seeds | land_cov | end | roi_l |
|---|---|---|---|---|
| fb6d6481 (this unit's base, built from `git archive`) | 1-10 | 2.896 .. 3.006, mean 2.952 | +4.967 .. +5.033 | 0.064 .. 0.071 |
| this branch | 1-10 | 2.906 .. 3.042, mean 2.977 | +4.967 .. +5.067 | 0.065 .. 0.073 |
| lane 5 (B1132 / B1127 captures) | | 2.938 / 2.929 | +4.938 / +4.914 | 0.077 / 0.072 |

Landing coverage: mean +0.9 % on the base (sd 0.042 and 0.037 over the seeds: the shift is inside the seed
scatter; per seed the two runs are not paired, the extra emitters change the random streams); 4 of the 10 seeds
read above the base's 3.006, the highest 3.042 (+3.5 % on lane B1132's 2.938, +1.3 % for the mean). End and
landing ROI p90 within the base's spread. A level-60 cast draws the same total population (shares sum to 1),
the spellComplete half the same art, the impact half the same art mix (one texture per slot instead of a random
pick of the four); the 5-seed variant with the pre-B1144 art lists reads 2.925..3.018, mean 2.983
(`review/B1144/parity_93.tsv`).

### Harness: `/disc` and level gates (`tests/test_fx_harness.cpp`)

- `FxHarness.ADisciplineWithoutBeginCastDrawsNothing`: 4499 Defensive Discipline as OP_Action only (plan column
  `action`): every frame Act-0..Act+2.5 equals a no-cast run's (peak lit px 0), no slot in `slots.tsv`.
- `FxHarness.ALevelOneCasterDrawsOnlyItsLevelZeroSlots`: 93 at level 1 draws Source slots 0-1 and Target slots
  0-1 (record 80's level-0 slots), at level 60 all eight; caster-box Source pixels BC+0.6..Act: level 1 34,483, level 60 49,189 (`review/B1144/tests/fx_harness_gates.txt`); the /disc run peaks at 0 lit px.

### Lane 5: 93 at level 1 and level 60, the observer recording (`review/B1144/oracle/l1_l60_93/`)

Plan `review/B1144/plans/l1_l60_93.tsv` (B1134's P2 geometry, class 6, PvP + guard assist off for the leg): the
caster casts 93 on Oraclewatch twice at level 1 (as found), `#level 60`, twice more. The observer's pcap: the
caster's spawn arrives at level 1; after `#level 60` the observer gets OP_SpawnAppearance (259, type 1, 60) and
OP_LevelAppearance, the caster OP_LevelUpdate 60. Measured on the observer's video with B1134's `obs_measure.py`
(`measure/obs_l1_l60.txt`; stills `strips/l1_vs_l60_observer.png`):

| cast | level | Source (caster) during the cast | landing (observer ROI) active run |
|---|---|---|---|
| 1 | 1 | cov 0.057, dx -0.15..+0.18, h 0.62..0.97: the two hand flames only | Act+0.02 .. +2.65 |
| 2 | 1 | cov 0.077, dx -0.15..+0.18, h 0.63..1.07 | Act+0.01 .. +2.61 |
| 3 | 60 | cov 0.153, dx -0.60..+0.59, h 0.22..1.64: hand flames + a body-wide star field | Act+0.00 .. +7.57 |
| 4 | 60 | cov 0.158, dx -0.59..+0.58, h 0.19..1.59 | Act+0.03 .. +4.43 |

The official client drew record 80's level-24/39 DEFAULT Source slots (277, 278) only at level 60, and the
observer gated by the level OP_SpawnAppearance told it (fable did not handle that packet: fixed here). The level-1
landing ends ~2.6 s after OP_Action, inside what the level-0 Target slots run (280: 1.0 s fixed + 0.25 s delay +
1.5 s life); the level-60 tail is consistent with 282 (5 s fixed, level 39), but the observer ROI also takes in the
caster's held star field, so the tail is not attributed. fable's harness draws the same slot SET (`slots.tsv`:
level 1 Source 0-1 + Target 0-1, level 60 all), with landing coverage 1.84..1.87 and Source coverage 0.081..0.083 at level 1 (seeds 1-3, tour plan) against 2.91..3.04 and 0.103..0.126 at level 60. Its landing still ends +5.0 s at both levels:
durations are the rows' (B1145).

## Tests

- `tests/test_spells_new_eff_reader.cpp` (+7): records 1, 93 (= 1 but for its Middle cell), 113 (Target anchor 1),
  326 (no Target stage), the 40 B1130 records' anatomy (`tests/spells_new_eff_anatomy.h`, generated from
  `review/B1141/crosscheck.tsv` by `review/B1144/scripts/gen_anatomy_header.py`), the 418 records' word ranges and
  B1137's slot census (1128 / 33 / 1212).
- `tests/test_spell_slot_rules.cpp` (new, graphics): the SpellVisualFX seam on the real files: 93's Source by
  level (1: hands; 24: + one DEFAULT; 60: four), per-slot art, the shares adding to the row at level 60, 201's
  head slots, 93's landing on the target by level with its art, 290's landing on the caster, 4499's absent
  landing, 54's override geometry on the entity, a spell with no record keeping the row's anchor.
- `tests/test_spell_impact_visual.cpp`: the B1105 pins flipped (an action-only cast creates NO Source; the
  Complete arm never calls `createCastGlow`), the payloads' gate level (player 60, NPC 1, unknown 1) and
  AT_WhoLevel updating it.
- `tests/test_fx_harness.cpp` (+2, above); `test_spell_target_anchors.cpp`'s bridge census names the gate level.
- RED on fb6d6481: the new tests do not compile there (113 errors: no `minLevel`, `SpellsNewEffAttach`,
  `attachPointEntityId`, 4-argument `createCastGlow`, `fxGateLevel`; `review/B1144/tests/red_fb6d6481_compile.txt`);
  on its code a level-1 93 draws all four Source slots' row (two hand emitters at full row rate), 4499 draws a
  spellComplete landing and an action-only cast arms B1105's glow.
- Builds and suites (`review/B1144/tests/`): whole tree with the CI flags at d91a5def, `cmake --build build-ci -j 4 -- -k`,
  BUILD_EXIT=0, 0 `error:` lines (`build_ci_whole.log`); `ctest -R "Spell|Bridge|Particle|FxHarness|ConfigKeyReachability|Oracle|SlotRules|SourceAnchors|SourceRecordRows|TargetRecordRows|TargetLanding|Landing|CasterOwn"`
  877/877 (41 skips on absent EQ client paths; `ctest_targeted.log`); `ctest -L hygiene` 51/51 (`ctest_hygiene.log`).
  CLAUDE.md 59,953 bytes.

## Files

`include/client/spell/spells_new_eff_reader.h` + `.cpp` (slot words, gate), `include/client/graphics/spell_visual_fx.h`
+ `src/client/graphics/spell_visual_fx.cpp` (`stageSlots`, `attachPointEntityId`, per-slot Source and landing),
`spell_particle_types.h` + `spell_emitter_presets.cpp` (`TargetForSpell` share + head-follow flag,
`TargetHasOverrideShape`), `include/client/bridge/cast_glow_route.h` + `spell_cast_fx.{h,cpp}` (no action-only
Source, gate level passed), `event_payloads_ui.h` + `spell_packet_handlers.cpp` (`fxGateLevel`),
`entities_component.cpp` (AT_WhoLevel), `tools/model_viewer/` (plan columns, `--caster-level`, `slots.tsv`).
No new source file (module_map's model_viewer row updated); no config key added (ConfigKeyReachability unchanged).
No spells_new_edd_reader: this unit needs no EDD numeric word (the equal split is stated above), so B1146 creates
it when it needs one.

## FOLLOW-UP

1. Durations (B1145): the level-1 oracle landing ends Act+2.6, fable's +5.0 at every level.
2. Per-slot populations from the EDD counts and emitter shapes (B1147) instead of the equal split and the row's
   one shape copied to each attach point.
3. CHEST, LFOOT, RFOOT, eyes and mouth have no fable point: CHEST/feet follow the entity (the client's own DEFAULT
   fallback), eyes/mouth the head bone.
4. The caster's own display of this leg (`oracle/l1_l60_93/video-caster.mp4`) is recorded and not measured.
