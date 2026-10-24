# B1149 (#469) FX-SIM-1 populations: file counts, sprite sizes, the client's alpha

Branch `task/B1149-fx-sim-populations` (wt/b1149), cut from fable 94ac58cd, rebased onto 50af6150 (B1142) and then 7bdf14a4 (B1148).
First leg of B1147 (#467). Evidence: `/mnt/projects/fable/review/B1149/` (`README.md`, `progress.log`, `runs/`,
`strips/`, `scripts/`, `pool_census.tsv`, `gates_r2.tsv`, `sweep_table.tsv`, build/ctest logs). No lane used.
Source of truth: `docs/spellsnew_format.md`, `review/B1141/edd_fields_from_code.tsv`, and the EQGraphicsDX9
disassembly/decompile in `review/B1141/ghidra/out/` (addresses below).

## 1. Prior work found

- B1144 (`refactor/B1144/README.md`): one emitter per drawn slot carrying 1/N of the measured row's population;
  the landing slot is a spellComplete + impact pair, the spellComplete half on the Source slots' art.
- B1145: per-slot timing from the EDD (`spell_fx_timing.h`), and `applySlotTiming` + `ScaleToCombinedPeak`
  re-scaled each stage to its untimed (B1144) peak: the file's burst/stream ENVELOPE, the row's COUNT.
- B1146: the colour words; `clientAlpha` implemented in the sim/renderer but passed `false` (it left the discs
  at 1/20 of the oracle's lit px on hand-fitted populations).
- B1123/B1131/B1132: the populations being replaced -- `landingPopulation.exceptions`, the Source rows'
  `population` rule (80/s x coverage / record 0's), the Target rows' `population` + `referenceCoverage`.
- B1119: sizes were fitted on fable's point sprites (`gl_PointSize = size x H / w`, clamped to 100 px).
- B1139/B1142: harness defaults; B1142 (merged under this branch) turned the b1118 occluder off and made
  `review/B1142/gate/regression_table.tsv` the lane baseline.
- `git log` of every file touched: spell_visual_fx.cpp (B1146, B1144, B1145), spell_emitter_presets.cpp,
  sim_particles.cpp (B1139, B1146), unified_particle_renderer.cpp (B972-FT, B1124, B1146),
  spell_effects_config.{h,cpp} (B1123, B1131, B1132, B1145), spells_new_edd_reader (B1145).

## 2. The client's rules (read from the code)

| rule | address | fable |
|---|---|---|
| first frame emits ftol(+0x80 x f); each new tick (ticks = ftol((age - +0x8C) x +0x88)) emits ftol(new ticks x +0x84 x f) | Render 0x10060e8b..0x10060ffd | `EmitterConfig::burstCount`, `ticksPerSecond`, `tickParticles`; `SimulationWorker` tick loop |
| f = max(min(1, +0xA0 / camera distance), floor); density = SpellParticleDensity (eqclient.ini default 1.0, clamped 0..1; the oracle ini says 1.000000) | 0x10060f12..0x10060f43; eqgame `"SpellParticleDensity"` | +0xA0 is 80 on most entries and the b1118/lane camera is 29.05 units away: f = 1 (not decoded) |
| ring capacity per emitter from the same words, capped at 100000; no global cap in the DLL's create path (0x1005e0a0 -> 0x1005a430) | CreateEmitter 0x1005a430 | -- |
| ring full: the rest of this frame's emission is dropped (`cmp edx,[ebp+0x7c]; je 0x100621f7`) | 0x10061180..0x10061197 | fable's one pool drops new particles when full (`allocateParticle` -1): the same rule at the pool |
| w = +0xCC + (+0x184 - +0xCC) x r, h = +0x17C + (+0x180 - +0x17C) x u (+0x18C = 1: r = u), x s x +0x16C | 0x10061262..0x100612a8, 0x1006325e | `EmitterConfig::eddSprite`, drawn at spawn |
| mode 0: half-height = h x proj x s x D16C x (1/depth) x 0.5 (0x101c594c = 0.5), half-width the same with w: a WORLD-unit quad | 0x100633b5..0x100633ff | `UnifiedParticleRenderer` sprite-quad program (6 vertices per EDD particle, view right/up) |
| w,h x age/+0x98 while young, x remaining/+0x9C near death; +0x190 / +0x194 height / width only | 0x1006326c..0x100632e9 | `UnifiedParticle::eddSpriteSize()` |
| alpha = min(fadeIn x fadeOut, +0xA4) x opacity; SRCALPHA, ONE when +0x68 | 0x10063a42..; 0x1005f7c2 | B1146's path, now ON for every spellsnew.eff emitter, white included |
| s (+0x6C) = actor +0x10 x root-actor vtbl+0x84 (= sub-object +0x64's float +0x60, getter 0x1016bd90) | 0x1006090d..0x1006094a | passed as 1: the writer 0x1016be70 (sub-object vtable slot 0x101ceaec) has no actor-vtable thunk and its eqgame caller was NOT traced |

## 3. What changed

- `SpellPresets::applySlotEmitter(slotDefs, timing, sprite, latency, attachScale)` replaces `applySlotTiming` +
  `CombinedPeakLive` + `ScaleToCombinedPeak` (and `SpellVisualFX`'s `scaleStageToUntimedPeak`): timing as B1145;
  counts = the file's burst + per-tick, divided over the slot's geometry carriers (one Source def; a head row's
  disc + column; a landing slot's spellComplete + impact halves) in the ratio of their measured peaks, so the slot
  as a whole emits exactly the file's count; sprite = the file's ranges.
- A landing slot's two halves both draw the slot's own texture and colour (B1144's Source-art spellComplete half
  existed for B1123's calibration).
- `applyStageEmitterColours(..., clientAlpha = true)` by default; with it every region is active (white too).
- `UnifiedParticle`: EDD sprite bit (flags bit 5), height + grow/shrink words; `flags` cleared at spawn (a reused
  pool slot kept its last particle's EDD bits).
- `UnifiedParticleRenderer`: a second program (QUAD_VS/QUAD_FS) drawing EDD particles as GL_TRIANGLES from the
  same packed VBO (B972-FT ring unchanged: quads are packed after the point batches).
- `SpellsNewEddSprite` in the shared EDD reader.
- Retired from `config/spell_effects.json` (values kept as bands in `tests/spell_fx_population_oracle.h`,
  generated by `review/B1149/scripts/gen_population_header.py` from `git show 94ac58cd:config/...`, B1130's
  record table and B1142's tour table): `landingPopulation.exceptions`, `sourceRecords.population` and every
  Source row's `coverage`, every Target row's `coverage`/`population`, `targetRecords.referenceCoverage`;
  `SourceRecord::spawnRate/coverage`, `TargetRecord::coverage/population`, `targetReferenceCoverage()`,
  `hasSourceRateOverride()` (no caller left). The B1123 pool rule stays for spells with no field-145 record.
- NOT retired here (B1153's): the spellOverrides' rates/sizes/lifetimes/alphas for 38/41/54/253/458/656; for those
  spells (all have records) only their spawnShape/motion is still read.

## 4. Pool (`review/B1149/pool_census.tsv`, `scripts/pool_census.py`)

File peak live per record, level 60, Source (cast + 4.35 s) + Target summed (an upper bound): of the 416 records
with a slot, 27 exceed 4096 and 14 exceed 8192 (fireworks, TMT heads, anniversary records: no class-level 1-60
spell draws them). Of the records a class-level 1-60 player spell draws, the largest is 104 (253 Whirling Wind):
Source 3746 + Target 1168 = 4914 -- inside the desktop 8192, OVER the sbc 4096 by 818 if both stages peak together.
The client has no global rule (per-emitter rings, 100000 cap); fable applies the client's ring-full rule at its one
pool: new particles are dropped while the pool is full. No count is scaled.

## 5. Gates (harness, `--camera b1118` = unoccluded since B1142, pool 8192; `review/B1149/gates_r3.tsv`)

On the tree rebased onto 7bdf14a4 (B1148: the per-caster Source term from the caster's skeleton track -- 4.10 s for
the harness caster, char 67's Dark Elf female -- and the corpse fade). `gates_r2.tsv` holds the same runs on
50af6150 (term 4.35): every coverage/ROI number identical, only the ends moved. Lane = B1142's fable-on-lane table
(pre-B1149 populations); oracle = its oracle columns (the B1118 tour).

| spell | land_cov (seeds) | lane | oracle | end Act+ (r3) | r2 (4.35 term) | lane | oracle | roi_l (band) | roi_s (band) |
|---|---|---|---|---|---|---|---|---|---|
| 93 dying, s1-5 | 4.225 3.952 3.722 3.853 3.726 (mean 3.896) | 3.049 (2.990..3.140) | 5.847 | 4.600 4.567 4.633 4.500 4.533 | 4.800..4.900 | 4.898 | 4.867 | 0.036..0.040 (0.064-0.086) | 0.044..0.057 (0.071-0.097) |
| 93 live | 4.233 | | | 6.433 | 6.467 | | (B1130 alive 6.67) | | |
| 38 dying | 3.873 | 2.650 | 3.889 | 4.500 | 4.800 | 4.831 | 4.85 | 0.038 (0.059-0.079) | 0.047 (0.063-0.085) |
| 54 dying | 1.052 | 1.696 | 2.461 | 4.700 | 4.967 | 5.054 | 5.103 | 0.031 (0.037-0.051) | 0.028 in |
| 656 dying | 1.085 | 0.957 | 2.493 | 4.750 | 4.983 | 4.905 | 5.004 | 0.030 in (0.030-0.040) | 0.028 in |
| 41 live | 2.750 | 2.666 | 4.154 | 6.133 | 6.133 | 6.253 | 6.224 | 0.084 (0.086-0.116) | 0.089 (0.096-0.130) |
| 458 live | 0.612 | 1.732 | 1.242 | 5.433 | 5.467 | 4.989 | 4.942 | 0.054 (0.086-0.116) | 0.031 (0.037-0.049) |
| 253 self | 9.465 | 5.028 | 7.281 | abs 7.583 | 7.583 | 6.538 | 6.522 | 0.072 (0.079-0.107) | 0.121 (0.151-0.205) |

Base (94ac58cd, same rig, `runs/base_s1..5`): 93 dying 2.402..2.445, end +5.167..+5.200, roi_l 0.073..0.076.

Separated target (`runs/r3_sep_box_end.txt`, B1145's box_end.py): level 60 landing ends Act+6.400, level 1
Act+2.200 (observer 2.61/2.65; B1145 2.667); the caster's Source box ends Act+4.333 (level 60) / 4.133 (level 1)
with its emission stopping at Act+4.10.

Disciplines (`review/B1146/plans/discs.tsv`, B1130 anchor.py LANDING on the caster, halfw 0.7, exactly as B1134
measured the observer; `runs/r3_discs_anchor.txt`, strip `strips/r2_discs_act3.png`):

| record (spell) | oracle RUN cov / peak / end | harness RUN cov / peak / end | B1146 (clientAlpha on, fitted rows) |
|---|---|---|---|
| 326 (4499) | 0.260 / 0.338 / Act+8.21 | 0.252 / 0.338 / Act+8.20 | ~1/20 of the lit px |
| 329 (4498) | 0.197 / 0.277 / 8.11 | 0.185 / 0.245 / 8.13 | |
| 330 (4670) | 0.263 / 0.365 / 8.28 | 0.242 / 0.324 / 8.20 | |
| 328 (4721) | 0.203 / 0.292 / 8.04 | 0.180 / 0.248 / 8.17 | |

The harness discs' active run starts Act+1.03..1.20 against the oracle's Act+0.01..0.55; the onset was not part of
this card and was not investigated.

### Verdicts

- **Discs: met** -- RUN coverage 0.89..0.97 of the oracle's on all four, ends within 0.13 s (B1146: ~1/20).
- **93 coverage vs lane +-3 %: NOT met, by construction.** The lane rows are fable's own draw with B1123's fitted
  populations, the thing this card replaces; with the file's counts the harness reads +28 % over the lane mean and
  -33 % under the oracle tour (5.847). 38 (same record 80) reads 3.873 against the oracle's 3.889 (-0.4 %).
- **93 dying end within +-0.1 s of 4.938: NOT met** (4.500..4.633, -0.31..-0.44; also short of the oracle's
  4.867). Measured cause: after the corpse cut at Act+4.536 only the caster's Source tail is left, and fable's
  tail is far fainter than the oracle's -- lit px after the cut 956 -> 69 at Act+4.53..4.67 (r3 s1,
  `runs/r3_93_die_s1/measure/fable_93/lit80.tsv`) against the oracle's 8,330 at Act+4.71 and 1,095 at 4.87
  (B1145 sec. 5a). Separately, the caster's Source box ends 0.23 s after its emission stops (Act+4.10 -> 4.333),
  where the oracle's Dark Elf female ends 0.66 s after (B1148 leg C1: 4.10 -> Act+4.764). The file's own counts
  and alpha (276 fades from birth: +0x94 1.2 s > its 1 s life) leave fewer lit pixels per sprite than the oracle
  shows; the word/rule fable lacks that scales a sprite is the +0x6C attach scale s (below). Not tuned.
  Before the B1148 rebase (term 4.35) the same populations read 4.800..4.900 (met against 4.867/4.898).
- **93 live end 6.433** (card 6.5; B1130 alive 6.67). 253 +7.583 vs oracle 6.522 (base +8.117); 458 +5.433 vs
  4.942 (base +5.833).
- **ROI p90: NOT met** on 11 of 14 rows (all short, 0.5..0.97 of the band's low edge; 54/656 Source and 656
  landing in band). The quads are the file's world sizes at s = 1. Missing rule: the client's attach scale for
  +0x6C (s = actor +0x10 x the root actor's +0x64 sub-object float +0x60; getter 0x1016bd90 = sub-object vtable
  +0x3C, exposed as actor vtbl +0x84/+0xA0; setter 0x1016be70 = sub-object vtable +0x54 (vtable 0x101cea98), not
  exposed through the actor vtable; its caller -- and the value eqgame gives a player model -- was NOT traced).
  Also not wired and possibly in the lit diameter: the texture frame animation (+0x130/+0x134, B1151; fable plays
  one cycle per life) and the atlas' 32 px cells.
- **The 40 records:** sec. 6.

## 6. The 40 records (`review/B1149/sweep_table.tsv`)

SWEEP_PLACEHOLDER

## 7. Tests

- `test_spell_slot_rules.cpp`: `EachSourceSlotEmitsItsFilesPopulationAndSize` (record 80's Source: 2/2/5/33 burst,
  1/1/5/33 per tick at 32/32/10/18, widths 0.875/0.875/1.25/1.0; level 1 two hand slots),
  `EachLandingSlotIsOneEmitterOfItsFilesPopulation` (280 50+50x30, 281 1, 282 2+2x28 w 6, 9 3+3x10; own art; client
  alpha), `SlotRulesSim.TheEmitterEmitsTheBurstThenTicksAndDrawsTheFilesSize` (the sim: 5 + 4 ticks x 2.5 = 15,
  world size x scale); the timing/art tests updated.
- `test_spell_edd_colour.cpp`: production default is the client's alpha, white included
  (`AWhiteEmitterIsOnTheClientsAlphaToo`); RGB-only kept as the test-only setting.
- `test_spell_source_anchors.cpp`, `test_spell_target_anchors.cpp`, `test_spell_colour_and_overrides.cpp`: the
  retired fields read from `tests/spell_fx_population_oracle.h`; `TheFittedExceptionsAreRetiredToBands`.
- `test_config_key_reachability.cpp`: the `landingPopulation.exceptions` data map removed.
- RED on 94ac58cd by construction: the new tests name members that do not exist there (`ticksPerSecond`,
  `tickParticles`, `eddSprite`, `spell_fx_population_oracle.h`); its code gave record 80's slots B1144 shares
  re-scaled to the row's peak (sec. 1). Not compiled against the base.

## 8. Builds

BUILD_PLACEHOLDER

## FOLLOW-UP

1. The attach scale s (+0x6C): trace the writer of the root actor's +0x64 sub-object +0x60 (0x1016be70) to its
   eqgame caller; the ROI gate waits on it.
2. B1151: texture frame animation (+0x130/+0x134), sprite modes 1/2 (162 entries), spin; B1150: shapes/offsets
   (+0x19C), which also decide coverage.
3. B1153: retire the spellOverrides' rate/size/lifetime/alpha keys for the six spells that have records.
4. The atlas' 32 px cells magnify to blocks on the large sprites (281 is 16 units, ~600 px at the b1118 camera).
