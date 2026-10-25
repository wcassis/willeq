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

Base (94ac58cd, same rig, `runs/base_s1..5`): 93 dying 2.372..2.445 (per-run land_cov 2.425 / 2.402 / 2.445 /
2.398 / 2.372, `cat runs/base_s1..5/measure/summary.tsv`), end +5.167..+5.200, roi_l 0.073..0.076.

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

Re-stated at the tip (b611c7ae, rbuild rc 0 on every targeted run) against `review/B1142/gate/regression_table.tsv`
(the lane reference, "new" columns = unoccluded since B1142) and its oracle columns. The card's "four-capture
mean 3.079" is not in the lane tables copied to this host -- the unoccluded capture B is 3.049 and that is what
is cited; the four unoccluded captures B 3.049, G93 3.136, G93N 3.140, N93 2.990 (mean 3.079;
`review/B1142/gate/measure_B/G/N93.log`) bracket it, so 3.049 is the lane number, not 2.938 (B1132's OLD rig);
the OLD-rig Bon run reads 2.942 (review/B1142/README.md:14 "Bon (tour B, OLD rig on dacd9534)";
gate/progress.log:6 "Bon start (class 6 plan tour_B env FABLE_TIP=on)"; the `tipon` column of the same table).
The lines read: `measure_B.log` "masked/B/fable_93: unmasked land_cov 3.049 at Act+2.03 (f2628)"; `measure_G.log`
"masked/G93/fable_93: unmasked land_cov 3.136 at Act+1.99 (f4320)" and "masked/G93N/fable_93: unmasked land_cov
3.140 at Act+2.04 (f4208)"; `measure_N93.log` "masked/N93/fable_93: unmasked land_cov 2.990 at Act+2.05 (f2110)";
`measure_BonA.log` "masked/Bon/fable_93: unmasked land_cov 2.942 at Act+2.02 (f2638)".

- **Discs: MET.** RUN coverage 0.89..0.97 of the oracle's on all four, ends within 0.13 s (B1146: ~1/20).
- **93 landing coverage vs the lane 3.049 +/- 3 %: NOT met, by construction.** The lane rows are fable's draw
  with B1123's fitted populations -- the thing this card replaces -- so the file's counts read higher: 5-seed
  mean 3.896 (3.722..4.225), +28 % over the lane, -33 % under the oracle tour 5.847. Same record (80) as 38:
  3.873 against the oracle's 3.889 (-0.4 %). Owner of the residual: the attach scale s (+0x6C), B1151-TRACE
  (t_a6d12b79) -- the oracle's 5.847 vs the harness 3.896 is the per-sprite diameter, not the count.
- **93 dying end within +/- 0.1 s of the lane 4.898: NOT met** (4.500..4.633, -0.27..-0.40; also short of the
  oracle's 4.867). Measured cause: after the corpse cut at Act+4.536 only the caster's Source tail is left, and
  fable's tail is far fainter than the oracle's -- lit px after the cut 956 -> 69 at Act+4.53..4.67 (r3 s1,
  `runs/r3_93_die_s1/measure/fable_93/lit80.tsv`) against the oracle's 8,330 at Act+4.71 and 1,095 at 4.87
  (B1145 sec. 5a). Separately the caster's Source box ends 0.23 s after its emission stops (Act+4.10 -> 4.333)
  where the oracle's Dark Elf female ends 0.66 s after (B1148 leg C1: 4.10 -> Act+4.764). Owner: the per-sprite
  Source tail -- the +0x6C attach scale (B1151-TRACE, t_a6d12b79). Not tuned.
- **93 live end 6.433** (card 6.5; B1130 alive 6.67; lane B1145 6.467) -- within the file's own life, no lane
  row to beat; reported.
- **ROI p90: NOT met** on 11 of 14 rows -- the three in-band rows are 54 Source 0.028 in 0.023-0.031, 656 Source
  0.028 in 0.024-0.032 and 656 landing 0.030 in 0.030-0.040 (sec. 5, the `roi_*_band` columns of
  `review/B1142/gate/regression_table.tsv`); 41's Source 0.089 also short of 0.096, and its landing 0.084 short
  of 0.086 (`gates_r3.tsv` r3_41_live_s1). The quads are the file's world sizes at s = 1. Owner: the client's attach
  scale for +0x6C (B1151-TRACE, t_a6d12b79) -- s = actor +0x10 x the root actor's +0x64 sub-object float +0x60
  (getter 0x1016bd90, setter 0x1016be70, neither exposed through the actor vtable; its eqgame caller -- and
  the value a player model gets -- was NOT traced). Also unwired and possibly in the lit diameter: the
  texture frame animation (+0x130/+0x134, B1151; fable plays one cycle per life) and the atlas' 32 px cells.
- **253 self 9.465 vs lane 5.028 / oracle 7.281; end +7.583 vs oracle 6.522; 458 0.612 vs 1.242; end +5.433 vs
  4.942** (base +5.833): reported, the pool and the file's counts (sec. 4) are the mechanism, owner B1151-TRACE
  (sizes) / the record's own stream end (the +0x78 run per B1145).
- **The 40 records: sec. 6** -- 40 rows, no `missing`, medians source ratio 1.21 / landing ratio 0.64 / end
  delta -0.12 s; the outlier clusters name the attach-scale (B1151-TRACE), frame-animation (B1151) and shape
  (B1150) mechanisms, no new theories.

### What this leaves for B1150-B1153

1. **B1151-TRACE (t_a6d12b79, in parallel):** the +0x6C attach scale s -- the ROI gate (11/14 tour rows short)
   and the 93 dying-end Source tail both wait on it.
2. **B1151:** texture frame animation (+0x130/+0x134), sprite modes 1/2, spin -- the rows where roi_l reads
   above the file's p90 (373/310/313/3) are its absence; also the 32 px atlas cells on the large sprites.
3. **B1150:** shapes/offsets (+0x19C) -- decide the lit shape and part of the coverage the attach scale does not.
4. **B1153:** retire the spellOverrides' rate/size/lifetime/alpha keys for the six spells that have records
   (38/41/54/253/458/656).

## 6. The 40 records (`review/B1149/sweep_table.tsv`)

`scripts/sweep_table.py` over the 2026-10-24 23:48Z re-run (40 rows, no `missing`; the instruments' geometry
differs as the script's header says -- B1130 fixed_npc vs the harness tour -- so the ratios are reported, not
banded). Harness = `--camera b1118` unoccluded, pool 8192, caster level 60. Oracle = review/B1130's fixed-capture
columns (source_cov / landing_cov / landing_end_s / sprite p90); roi_* = the harness's measured sprite diameter.

| row | spell | src cov oracle/harness (ratio) | land cov oracle/harness (ratio) | end oracle/harness (delta s) | p90 src oracle/roi | p90 land oracle/roi |
|---|---|---|---|---|---|---|
| 0 | 93 | 0.170 / 0.224 (1.32) | 3.945 / 4.233 (1.07) | +6.67 / +6.433 (−0.24) | 0.087 / 0.044 | 0.061 / 0.040 |
| 1 | 200 | 0.290 / 0.237 (0.82) | 3.050 / 1.598 (0.52) | +6.58 / +6.433 (−0.15) | 0.061 / 0.068 | 0.112 / 0.086 |
| 2 | 202 | 0.147 / 0.149 (1.01) | 2.250 / 0.865 (0.38) | +5.48 / +4.933 (−0.55) | 0.077 / 0.057 | 0.117 / 0.093 |
| 3 | 377 | 0.076 / 0.052 (0.68) | 11.688 / 8.053 (0.69) | +9.00 / +6.400 (−2.60) | 0.065 / 0.048 | 0.043 / 2.196 |
| 4 | 50 | 1.349 / 2.598 (1.93) | 3.250 / 3.297 (1.01) | +6.78 / +6.533 (−0.25) | 0.187 / 0.234 | 0.139 / 0.148 |
| 5 | 80 | 0.180 / 0.134 (0.74) | 0.559 / 0.224 (0.40) | +4.53 / +4.300 (−0.23) | 0.040 / 0.034 | 0.042 / 0.038 |
| 6 | 722 | 0.380 / 0.372 (0.98) | 0.762 / 0.809 (1.06) | +6.26 / +6.433 (+0.17) | 0.112 / 0.099 | 0.097 / 0.105 |
| 7 | 40 | 0.390 / 0.444 (1.14) | 3.077 / 2.603 (0.85) | +6.81 / +6.667 (−0.14) | 0.238 / 0.083 | 0.102 / 0.104 |
| 8 | 346 | 0.439 / 0.562 (1.28) | 6.271 / 3.397 (0.54) | +3.78 / +3.867 (+0.09) | 0.052 / 0.040 | 0.048 / 0.044 |
| 9 | 375 | 0.861 / 0.699 (0.81) | 1.999 / 1.338 (0.67) | +7.64 / +7.533 (−0.11) | 0.044 / 0.039 | 0.038 / 0.036 |
| 10 | 590 | 1.322 / 1.325 (1.00) | 5.476 / 4.500 (0.82) | +5.72 / +6.900 (+1.18) | 0.189 / 0.115 | 0.101 / 0.113 |
| 11 | 285 | 0.115 / 0.103 (0.90) | 0.997 / 0.520 (0.52) | +5.51 / +5.533 (+0.02) | 0.051 / 0.043 | 0.058 / 0.052 |
| 12 | 208 | 1.096 / 1.331 (1.21) | 5.788 / 4.346 (0.75) | +7.04 / +7.800 (+0.76) | 0.106 / 0.107 | 0.107 / 0.174 |
| 13 | 209 | 0.296 / 0.685 (2.31) | 3.264 / 2.211 (0.68) | +6.39 / +6.267 (−0.12) | 0.086 / 0.105 | 0.108 / 0.113 |
| 14 | 373 | 0.083 / 0.060 (0.72) | 3.995 / 5.278 (1.32) | +8.60 / +8.467 (−0.13) | 0.066 / 0.063 | 0.135 / 1.750 |
| 15 | 289 | 0.806 / 0.767 (0.95) | 2.904 / 1.250 (0.43) | +6.57 / +6.333 (−0.24) | 0.124 / 0.077 | 0.065 / 0.033 |
| 16 | 242 | 0.169 / 0.570 (3.37) | 1.343 / 0.842 (0.63) | +5.70 / +5.500 (−0.20) | 0.105 / 0.103 | 0.076 / 0.030 |
| 17 | 237 | 0.663 / 0.803 (1.21) | 3.655 / 2.317 (0.63) | +5.16 / +4.767 (−0.39) | 0.119 / 0.086 | 0.097 / 0.080 |
| 18 | 378 | 0.296 / 0.303 (1.02) | 4.042 / 1.759 (0.44) | +5.36 / +5.300 (−0.06) | 0.128 / 0.080 | 0.132 / 0.153 |
| 19 | 359 | 1.325 / 1.164 (0.88) | 2.332 / 1.505 (0.65) | +5.58 / +5.500 (−0.08) | 0.049 / 0.041 | 0.037 / 0.032 |
| 20 | 341 | 0.631 / 1.054 (1.67) | 4.679 / 2.771 (0.59) | +6.26 / +6.133 (−0.13) | 0.113 / 0.106 | 0.107 / 0.083 |
| 21 | 218 | 0.825 / 1.050 (1.27) | 5.976 / 4.246 (0.71) | +5.90 / +5.700 (−0.20) | 0.066 / 0.064 | 0.068 / 0.071 |
| 22 | 201 | 0.256 / 0.348 (1.36) | 4.636 / 2.708 (0.58) | +4.83 / +4.667 (−0.16) | 0.039 / 0.051 | 0.040 / 0.041 |
| 23 | 372 | 0.429 / 0.410 (0.96) | 2.320 / 0.873 (0.38) | +6.48 / +6.400 (−0.08) | 0.029 / 0.032 | 0.052 / 0.031 |
| 24 | 348 | 0.703 / 1.037 (1.48) | 3.839 / 2.191 (0.57) | +6.40 / +6.350 (−0.05) | 0.059 / 0.065 | 0.100 / 0.089 |
| 25 | 14 | 0.284 / 0.388 (1.37) | 4.847 / 3.091 (0.64) | +6.64 / +6.467 (−0.17) | 0.165 / 0.118 | 0.092 / 0.058 |
| 26 | 310 | 0.405 / 0.618 (1.53) | 8.003 / 7.139 (0.89) | +5.05 / +5.233 (+0.18) | 0.094 / 0.095 | 0.038 / 1.977 |
| 27 | 290 | 0.179 / 0.198 (1.11) | 0.747 / 0.492 (0.66) | +6.55 / +6.700 (+0.15) | 0.072 / 0.055 | 0.037 / 0.028 |
| 28 | 73 | 0.612 / 1.129 (1.84) | 2.332 / 1.421 (0.61) | +7.62 / +6.300 (−1.32) | 0.137 / 0.162 | 0.115 / 0.125 |
| 29 | 286 | 0.148 / 0.218 (1.47) | 2.033 / 1.052 (0.52) | +7.34 / +7.467 (+0.13) | 0.097 / 0.077 | 0.114 / 0.112 |
| 30 | 231 | 0.074 / 0.092 (1.24) | 5.939 / 2.938 (0.49) | +6.65 / +6.300 (−0.35) | 0.060 / 0.050 | 0.060 / 0.096 |
| 31 | 313 | 0.887 / 0.843 (0.95) | 3.668 / 4.566 (1.24) | +6.32 / +6.217 (−0.10) | 0.174 / 0.075 | 0.194 / 1.911 |
| 32 | 374 | 0.535 / 0.874 (1.63) | 6.547 / 5.157 (0.79) | +8.80 / +7.850 (−0.95) | 0.066 / 0.042 | 0.035 / 0.043 |
| 33 | 382 | 0.051 / 0.048 (0.94) | 2.522 / 0.588 (0.23) | +5.26 / +5.550 (+0.29) | 0.045 / 0.040 | 0.072 / 0.056 |
| 34 | 340 | 0.435 / 0.497 (1.14) | 3.285 / 1.900 (0.58) | +7.15 / +7.033 (−0.12) | 0.093 / 0.091 | 0.103 / 0.077 |
| 35 | 253 | 1.142 / 6.803 (5.96) | 7.283 / 9.454 (1.30) | +6.95 / +7.450 (+0.50) | 0.158 / 0.117 | 0.080 / 0.068 |
| 40 | 602 | 1.353 / 1.537 (1.14) | 0.157 / 1.533 (9.76) | +0.73 / +4.167 (+3.44) | 0.149 / 0.188 | − / 0.076 |
| 41 | 703 | 0.150 / 0.191 (1.27) | 1.892 / 0.852 (0.45) | +2.75 / +5.567 (+2.82) | 0.074 / 0.075 | 0.167 / 0.144 |
| 42 | 700 | 0.077 / 0.104 (1.35) | 0.686 / 0.309 (0.45) | +5.49 / +5.367 (−0.12) | 0.064 / 0.099 | 0.074 / 0.131 |
| 43 | 720 | 0.196 / 0.412 (2.10) | 1.176 / 0.885 (0.75) | +4.43 / +5.633 (+1.20) | 0.090 / 0.046 | 0.126 / 0.068 |

Medians (n 40): source coverage ratio **1.21** (p25..p75 0.95..1.48); landing coverage ratio **0.64**
(0.52..0.81); landing end harness − oracle **−0.12 s** (−0.22..0.14). Rows 36-39 are not in
review/B1130/record_table.tsv (the 40 sweep plans come from its 41 records minus record 51), so the table has
40 rows with gaps there; every row present has all columns filled.

Outlier clusters, mechanisms from this README's own sections (no new theories):

- **Source ratio > 2: 209 (2.31, undead-target), 242 (3.37, single), 720 (2.10, self), 253 (5.96, single).**
  The harness draws the Source at the caster (char 67's Dark Elf female, B1148) and B1130 at its own fixed
  rig, so the ratio is a caster/rig difference, not a rule change; 720/253 additionally read the pool rule in
  sec. 4 (253's class-level peak 4914 > sbc 4096, inside desktop 8192 -- not clamped in this sweep).
- **Landing ratio < 0.5: 382 (0.23), 202 (0.38), 372 (0.38), 80 (0.40), 289 (0.43), 378 (0.44), 703 (0.45),
  700 (0.45), 231 (0.49)** -- the harness landing lands below the oracle's on small-sprite single-targets and
  PBAE/othered records; the same +0x6C attach-scale s (sec. 5) and the unwired texture frame animation
  (B1151) that hold the tour's ROI rows short (sec. 5) hold these too, plus B1130's own box vs the tour's
  geometry. **602 (9.76, end +3.44 s)** is the group-teleport record: B1130's oracle landing is the
  teleport's near-zero p90 (0.157, roi --), the harness draws the record's full landing -- a geometry
  difference, not a rule.
- **|d_end| > 1: 377 (−2.60), 73 (−1.32), 703 (+2.82), 720 (+1.20), 590 (+1.18), 602 (+3.44).** 602 is the
  group-teleport row above (oracle end +0.73 s is the teleport flash, not the record's run). 703/720
  (PBAE/self): B1130's fixed-capture box stopped before the record's full run (the +0x8C delay + +0x78 run
  per B1145 extends past it). 377/73 (targeted-AE) short by ~1.3-2.6 s: the oracle's box stays lit by the
  per-sprite Source tail past the file's end (the +0x6C attach scale, sec. 5, keeps the oracle's sprites
  larger than the harness's s=1 quads longer). 590 (self) runs +1.18 s long: its file's end is the stream's
  own run, which the fixed capture stopped earlier.
- **ROI p90 vs the file's world size:** roi_l short of its p90 on 6 of 39 rows and roi_s on 8 (93 0.51, 40 0.35,
  590 0.61, 289 0.62, 378 0.62, 313 0.43, 374 0.64, 720 0.51) -- the same +0x6C attach-scale s missing rule as
  sec. 5; a handful of rows read roi_l > p90_l (373 1.75, 310 1.98, 313 1.91, 3 2.20) where the sprite's
  grow/shape (+0x130 frames, B1151; shapes +0x19C, B1150) magnify past the p90 the oracle box caught.

## 7. Tests

Run on the tip (b611c7ae) through rbuild on the ThinkPad (B1087; Release GLES2 tree,
`/home/user/willeq-builds/kb-b1149-finish/build`; logs in `/home/user/kb-evidence/kb-b1149-finish/`, rbuild_*.log):
`rbuild configure` rc 0 (rbuild_configure.log); `rbuild --target` rc 0 for model_viewer and the six test
executables this section names (no `test_spell_visual_fx` target exists in the tree; the targets are the six
below, rbuild_build_*.log); `rbuild test -R 'Spell|Bridge|Particle|FxHarness|ConfigKeyReachability|Oracle'`
rc 0, 53/53 passed in 5.8 s with the three capture-dependent skips (162 SpellColourChain measured-stages,
588/589 Oracle glyph templates) -- rbuild_test_targeted.log; `rbuild hygiene` rc 0, 48/48 -- rbuild_hygiene.log.

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

Same tip (b611c7ae), rbuild on the ThinkPad (Release GLES2, the CI configure): configure rc 0, the six test
targets + model_viewer rc 0 (logs `/home/user/kb-evidence/kb-b1149-finish/rbuild_*.log`). The full CI-flag
build of this tip is the MR's `build` job (step 5); nothing here was built whole-tree on purpose.

## FOLLOW-UP

1. The attach scale s (+0x6C): trace the writer of the root actor's +0x64 sub-object +0x60 (0x1016be70) to its
   eqgame caller; the ROI gate waits on it.
2. B1151: texture frame animation (+0x130/+0x134), sprite modes 1/2 (162 entries), spin; B1150: shapes/offsets
   (+0x19C), which also decide coverage.
3. B1153: retire the spellOverrides' rate/size/lifetime/alpha keys for the six spells that have records.
4. The atlas' 32 px cells magnify to blocks on the large sprites (281 is 16 units, ~600 px at the b1118 camera).
