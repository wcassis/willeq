# B1165 (#484): Options > Particles vs the oracle

What the official client does with its Options > Display > Particles > Spell page (and
`ShowSpellEffects`, and the legacy `[Options] ParticleDensity`), read from the
decompile, and what fable now does with each. Decompile = `review/B1141/ghidra/out/`
(`eqgame.exe.c`, `eqgame.dis`, `EQGraphicsDX9.dll.c`, `EQGraphicsDX9.dis`); the
binaries are the lane's own (`md5sum`: eqgame.exe a9de1b8cc5c451b32084656fcacf1103 and
EQGraphicsDX9.dll 2fc40fe9e382612a3d27ec344278116a, identical in review/B1141/bin and
/mnt/projects/EverQuestP1999). Data words were read from the binary with
`review/B1165/worker/scripts/peread.py` (a section-table PE reader; `pefile` is not on
dev-u24), e.g. `python3 -I peread.py eqgame.exe 00793b4c 8 i`.

## 1. The tables and the loader (acceptance A)

| quantity | oracle | where | fable (this card) |
|---|---|---|---|
| Density Off/Low/Medium/High | 0 / 0x3e800000 = 0.25 / 0x3f000000 = 0.5 / 0x3f800000 = 1.0 | options handler eqgame.exe.c 245163-245180 (Spell), 245207-245224 (Env) | `kDensityIniValues` unchanged (already right) |
| Near Clip Near/Medium/Far/Farthest | 0x40000000 = 2 / 0x40800000 = 4 / 0x40c00000 = 6 / 0x41000000 = 8 | eqgame.exe.c 245189-245204 (Spell), 245225-245240 (Env) | `kNearClipIniValues` was {1, 2, 4, 8}; now include/client/graphics/environment/particle_options.h:134 |
| loader clamp | density, opacity to [0, 1]; near clip to [1, _DAT_00641fa0]; default _DAT_006410b4 | eqgame.exe.c 132608-132637; peread: 0x641fa0 = 0x41000000 = 8.0, 0x6410b4 = 0x40000000 = 2.0 | `clampUnitIni` / `clampNearClipIni` (particle_options.h:141), applied in `parseSettingIniText` |
| ini float -> combobox choice | the first choice whose value is >= the float, else the last (`fcom; test ah,0x41; jp` = next while f > v) | near clip 0x53b93f..0x53b985 vs 0x659378.. = 2/4/6/8; density 0x53b994..0x53b9da vs 0x659368.. = 0/0.25/0.5/1 | `choiceForIniFloat` (particle_options.h:148), used at src/client/graphics/ui/options_settings.cpp:420, which used to take the nearest choice |

So the shipped `SpellParticleNearClipPlane=2.000000` is **Near**, and fable's default
near-clip choice is now Near (index 0) for both pages; the knob value stays 2.0 at the
default. Tests: tests/test_options_particles.cpp:184 (`TheNearClipTableIsTheOraclesAndTwoIsNear`,
the ini round trip pins 2.0 = Near and 4/6/8 = Medium/Far/Farthest) and
tests/test_options_particles.cpp:207 (`TheLoaderClampsAndTheComboboxRoundsUp`).

Applied vs shown (fix leg, MR !318 note 7756 finding 3): the oracle APPLIES the clamped ini
float itself -- the loader stores it as read (eqgame.exe.c 132608-132636 -> _DAT_00985810 /
_DAT_00985814 / _DAT_00985818) and eqgame 0x495f29 pushes the density unrounded into
CreateEmitter (sec. 6) -- and only the combobox DISPLAY rounds it to a choice
(0x53b93f..0x53b9da). The first pass stored only the choice, so an ini 0.3 density created
at 0.5. fable now keeps both: the settings slot holds the choice the window shows, and
`OptionIniFloats` (include/client/graphics/ui/options_settings.h:229; `iniFloats` in
include/client/graphics/ui/options_panel.h:106, `optionIniFloats` in
include/client/graphics/ui/ui_preferences.h:147) holds the clamped float whenever it is
not the shown choice's own table value. `parseSettingIniFloat`
(src/client/graphics/ui/options_settings.cpp:399) produces both,
src/client/graphics/ui/ui_preferences.cpp:137 stores both, the save writes the float back
(src/client/graphics/ui/ui_preferences.cpp:418), and the knobs read the applied value
(`particleKnobsFromOptions`, src/client/graphics/ui/options_particles.cpp:74; `spellFxOptionsFromOptions`,
src/client/graphics/ui/options_particles.cpp:95). A change through the window or
`/option` stores the table value, as the oracle's handler does (eqgame.exe.c 245163-245204),
so it clears the float (src/client/graphics/ui/options_values.cpp:27). The same holds for the
Env page's floats and both opacities. Test: tests/test_options_particles.cpp:226
(`AnOffTableIniFloatIsAppliedRawAndShownAsTheChoice`: ini 0.3 density -> shown Medium,
applied 0.3, saved as 0.300000; 3.0 near clip -> shown Medium, applied 3.0; choosing Medium
in the window -> 0.5). Every shipped and captured ini carries table values, so no measured
number changes.

## 2. N2 (what "near clip" culls) -- still an assumption

The three near clips reach EQGraphicsDX9: eqgame 0x445329..0x445351 pushes Actor
(0x985838), Env (0x985828) and Spell (0x985818) to the engine object's vtable +0x38 =
0x1005de60, which stores them at engine +8 / +0xC / +0x10 (constructor default 2.0 at
0x1005dde7..0x1005ddf2; the engine object is CreateGraphicsEngine's +0x10,
`DAT_102326f0` = FUN_1005dd90, vtable 0x101cbe38). The per-particle read of those words
was NOT found in this pass (no vtable method of 0x101cbe38 reads +8/+0xC/+0x10 as a
float; the render body 0x100605b0 does not load the engine global). N2 stays flagged
in particle_options.h; FOLLOW-UP below.

## 3. "On For:" = SpellParticleCastFilter (acceptance B)

* The value table: `DAT_00793b4c` = {1, 2, 4, 8, 17, 18, 20, 24} (peread), stored by the
  handler `DAT_0098581c = (&DAT_00793b4c)[index]` (eqgame.exe.c 245181-245184); the
  loader default is 0x18 = 24 (eqgame.exe.c 132638); the combobox shows the index of the
  value in the table, 7 (All spells) when absent (0x53b9df..0x53ba07). The labels are
  EQUI_OptionsWindow.xml's `ODP_SpellParticlesFilterCombobox` `<Choices>`: My spells,
  Group spells, Raid spells, All PCs, Me + NPC, Group + NPC, Raid + NPC, All spells.
* The loader keeps the ini int RAW, unvalidated (`FUN_004ae64c`, eqgame.exe.c 132638), and
  `FUN_00491cff` tests its BITS (0x491d0f..0x491d41), so an off-table value acts by its bits:
  ini 6 (0x04 | 0x02) behaves as "Raid spells" (4) and the combobox shows index 7, "All
  spells". The first pass REJECTED such an int at parse and kept 24 (review note 7756); the
  fix leg marks the row `rawIniInt` (src/client/graphics/ui/options_settings.cpp:179, any int
  is legal: src/client/graphics/ui/options_settings.cpp:263) and the combobox shows
  `choiceDisplayIndex` (src/client/graphics/ui/options_settings.cpp:298: the value's index,
  else the last). Test: tests/test_options_particles.cpp:276
  (`AnOffTableCastFilterIntIsKeptRawAndDecodedByItsBits`: 6 is kept, shown "All spells", and
  decides like 4 for every pair of self / group / raid / other PC / NPC).
* The decision, `FUN_00491cff(stage, caster, target)` (eqgame.exe.c 115163-115221,
  decoded from the disassembly 0x491cff..0x491e2f, not the decompile's booleans):

  | bit | meaning | flags set |
  |---|---|---|
  | 0x10 | NPC casters (dl, 0x491d0f..0x491d21) | -- |
  | 0x08 | every PC caster ([ebp-3], 0x491d27) | raid + group too |
  | 0x04 | raid ([ebp-4]/cl, 0x491d31) | group too |
  | 0x02 | group ([ebp-2]/bl, 0x491d3d) | -- |
  | 0x01 | never tested: self is always shown | -- |

  Order: no caster and no target -> shown (0x491d43..0x491d4f); 0x08 and 0x10 -> shown
  (0x491d53..0x491d5d); a caster of type 1 (NPC, spawn +0x244) is shown under 0x10,
  else the target decides (0x491d7b..0x491d83); a caster of type >= 2 (a corpse) is
  shown (0x491d88); a PC caster is shown when 0x08, or raid-flag and in my raid, or
  group-flag and in my group, or it is me (0x491d90..0x491dce); otherwise the target is
  shown when it is a PC in my raid / group (same flags) or me (0x491dd0..0x491e1d).
  The function returns TRUE to HIDE: its caller draws on 0 (eqgame.exe.c 122018-122019).
* **Correction to the card's lead:** the group test is `FUN_00485df1` on 0x904248 (five
  64-byte names + one at +0x15c), the raid test is `FUN_004303a6` -> `FUN_00430366` on
  0x91d708 (72 names of 0x118 bytes, count at +0x5060); the issue body has them swapped.
  With the labels this is the only consistent reading (bit 0x02 = "Group spells" tests
  only the group object).
* Stage pairs, from the three callers of `FUN_0049b260`: Source = (caster, caster)
  (BeginCast, eqgame.exe.c 122270); Middle = (missile caster +0xfe4, none) (FUN_00476b12,
  eqgame.exe.c 95153); Target = (action source, the target) (FUN_0047f4af, eqgame.exe.c
  101960). For every caster kind the Source and Middle pairs decide the same way, so a
  hidden Source means a hidden Middle; the Target has its own answer (another PC's bolt
  at me under "My spells" draws only its landing).
* fable: `SpellParticleCastFilterShows` (include/client/spell/spell_fx_options.h:54),
  the `Setting::SpellParticleCastFilter` row (src/client/graphics/ui/options_settings.cpp:177,
  choices src/client/graphics/ui/options_settings.cpp:74, default 24, persisted as the int), the
  `ODP_SpellParticlesFilter*` rows now wired (options_layout.h, options_classification.cpp),
  the game thread fills what the filter reads (`fxActorOf`,
  src/client/spell/spell_packet_handlers.cpp:52: npc_type = +0x244, group by spawn id or
  name, raid roster by name, self) into the SpellCastVisual payloads, and SpellVisualFX
  gates each stage (`stageDraws`, src/client/graphics/spell_visual_fx.cpp:175; Source
  src/client/graphics/spell_visual_fx.cpp:533, landing src/client/graphics/spell_visual_fx.cpp:912,
  Middle src/client/graphics/spell_visual_fx.cpp:1251, missile arrival
  src/client/graphics/spell_visual_fx.cpp:1294).
* Tests: every table entry x self / group / raid / group+raid / other PC / NPC / PC and
  NPC corpse, the null/null arm and the landing / missile pairs:
  tests/test_spell_fx_options.cpp:64, tests/test_spell_fx_options.cpp:86,
  tests/test_spell_fx_options.cpp:95; per stage through SpellVisualFX with record 80:
  tests/test_spell_slot_rules.cpp:644 (`TheOnForFilterGatesEachStage`); the combobox,
  labels, persistence and `/option`: tests/test_options_particles.cpp:223.

## 4. The stage gate and ShowSpellEffects (acceptance C, part 1)

`FUN_0049b260` (eqgame.exe.c 121989-122034): after the filter (0x49b2f7..0x49b2f9) a
spell WITH a spellsnew.eff record (param_2[0x8d] != 0, the EDD path fable draws since
B1149) draws only when SpellParticleDensity > 0 (0x49b30b), SpellParticleOpacity > 0
(0x49b31e) and `DAT_0092484c` != 0 (0x49b331); a spell without one draws when
`DAT_0092484c` != 0 or the stage is 1 (0x49b358..0x49b363), or when `FUN_0044ced9(spell)`
== 1 (spell +0x230 in 61..64, 0x49b365..0x49b36f) -- that third arm's spell field is not
decoded here (FOLLOW-UP; fable's record-less spells have no Middle, so only Source and
landing are affected).

`DAT_0092484c` is `ShowSpellEffects`: [Defaults], default 1, anything but 0/1 reset to 1
(eqgame.exe.c 126007-126011). It has NO Options-window control (the Display page's
checkboxes are PCNames / NPCNames / ShowTargetRing / LevelOfDetail / ShowHelm); its only
switch is `/showspelleffects` (FUN_004688b3, eqgame.exe.c 88015-88057): no argument ->
eqstr 0x3370 / 0x3371 (13168 "FORMAT: /showspelleffects <ON | OFF> (default is ON)",
13169); "off" (eqstr 0x3292 = 12946) -> 0, anything else -> 1, written back to the ini;
then 0x3372 / 0x3373 (13170 / 13171 "Spell particle effects are currently ON/OFF.").

fable: `SpellFxStageDraws` (include/client/spell/spell_fx_options.h:101);
`Setting::ShowSpellEffects` (src/client/graphics/ui/options_settings.cpp:179, persisted,
no window control -- as the oracle); `/showspelleffects` (`runShowSpellEffects`,
src/client/graphics/ui/options_command.cpp:231, routed with /option, in the renderer
command catalog). The Options record reaches SpellVisualFX every frame
(`spellFxOptionsFromOptions`, src/client/graphics/ui/options_particles.cpp:95;
src/client/graphics/sim/renderer_sim_bridge.cpp:459). Tests:
the gate in tests/test_spell_fx_options.cpp:123, tests/test_options_particles.cpp:254 (the
command and its text), tests/test_spell_slot_rules.cpp:644 (ShowSpellEffects 0 creates no
Source and no landing for record 80).

## 5. The legacy [Options] ParticleDensity (acceptance C, part 2) -- documented, not wired

* Loaded from `[Options] ParticleDensity` default 3 (eqgame.exe.c 132740), clamped to
  0..3 (132808-132817), then `FUN_004389fb(ParticleDensity)` (132831; also the
  `/particles OFF|LOW|MED|HIGH` command, eqgame.exe.c 88934, "Particle Cloud Density is
  currently: %s").
* `FUN_004389fb` (eqgame.exe.c 51407-51424, 0x4389fb..0x438a3f) calls the engine's
  vtable +0x94 with 0 / _DAT_00640fe8 = 0x3ea8f5c3 = 0.33 / _DAT_006423ec = 0x3f28f5c3 =
  0.66 / 1.0.
* vtable 0x101cbe38 + 0x94 = 0x10089820 (peread), which stores the float at engine
  +0xA8 (`mov [ecx+0xa8], eax`). The SAME slot is what the Env page's density calls
  (0x53e843..0x53e855: `fld [0x985820]` -> `call [eax+0x94]`). Engine +0xA8 is the density
  argument of the ENVIRONMENT family's CreateEmitter (0x10048c83..0x10048c98 and
  0x10058e0c..0x10058e21 -> 0x1005e2c0 -> 0x1005e0a0(engine +0x38, ...) -> 0x1005a430),
  i.e. the Env page's density, scaling counts at creation (sec. 6's rule).
* So the legacy knob is NOT a spell knob and not a second quantity: it is the Env page's
  density, under an older key. The only behaviour difference is the ORDER: at startup the
  client pushes the legacy value (132831) and never pushes the loaded
  EnvironmentParticleDensity (no other caller of engine vtable +0x94); the Env page's value
  reaches the engine only when its combobox changes. fable applies the Env page's value
  from load. Every capture runs ParticleDensity=3 and EnvironmentParticleDensity=1.0 (both
  1.0, `review/B1159/worker/oracle/m_b1159_rig/eqclient-after.ini` lines 109 and 442), and
  no FX gate measures environment particles. Not wired here: FOLLOW-UP below.

## 6. The Spell page's density on the EDD (spell) path (acceptance D)

The issue's lead (B1149 README sec. 2 row, 0x10060f12..0x10060f43) is the distance LOD
`f`, not the density: refactor/B1149/README.md:31. The density is read at CREATION:

* eqgame 0x495f29 pushes `[0x985810]` (SpellParticleDensity) as the 4th argument (and
  `[0x985814]` opacity the 5th) of the engine's vtable +0x14 call (0x495f39); vtable
  0x101cbe38 + 0x14 = 0x1005e1d0 (peread), which calls 0x1005e0a0(engine +0x14, a1..)
  (the spell family), which calls CreateEmitter 0x1005a430 with the density as its 3rd
  argument (`[esp+0x28]` at 0x1005a463 / `[esp+0x38]` after the four pushes).
* CreateEmitter: density <= 0 -> no emitter (0x1005a463..0x1005a48e); burst (+0x80) b ->
  ftol(b x d), raised to min(b, 16) when below 16 (0x1005a495..0x1005a4bf); per-tick
  (+0x84) t >= 16 -> ftol(t x d) with the tick rate kept (0x1005a4c1..0x1005a4e9), t < 16
  -> t kept and the rate (+0x88) x d (0x1005a4eb..0x1005a4f5). ftol = 0x101a9b60
  (truncation).
* fable before this card: the worker scaled the burst by `scaledCount` (rounded, no 16
  floor) and the tick stream by d every frame -- not the client's rule. Now
  `SpellFxSlotTimingAtDensity` (include/client/spell/spell_fx_timing.h:153) is applied in
  SpellVisualFX's slot timing (src/client/graphics/spell_visual_fx.cpp:171, every record
  stage's timing comes through it); the slot emitter marks those emitters
  (`densityAtCreate`, src/client/graphics/environment/spell_emitter_presets.cpp:434) and the worker no longer
  scales them again (src/client/graphics/sim_particles.cpp:408). Record-less spells keep
  B995's per-frame scaling (their oracle path, FUN_0049ab78, was not traced here).
* Opacity and near clip stay at draw: `applyParticleKnobs`
  (src/client/graphics/environment/unified_particle_renderer.cpp:527).
* Tests: tests/test_spell_fx_options.cpp:150 (the rule: 50 -> 25 at 0.5, 50 -> 16 at
  0.25, 5 kept + rate halved, 33 -> 16 by truncation, 0 -> nothing);
  tests/test_spell_slot_rules.cpp:582 (`TheSpellPagesDensityScalesEveryStageAtCreation`:
  record 80 at level 60, Source / Middle / Target, density 1, 0.5, 0.25 and 0 -- every
  created emitter's counts equal the rule applied to the density-1 counts; density 0
  creates nothing in any stage); tests/test_spell_slot_rules.cpp:685 (the worker leaves a
  created-at-density emitter alone: 15 sprites, and still scales a record-less one: 8).

## 7. The harness and the gate pin (acceptance D)

`tools/model_viewer --spell-test --plan` now runs at a declared Spell page: default the
oracle capture's ini (SpellParticleDensity 1.0, SpellParticleOpacity 1.0,
SpellParticleNearClipPlane 2.0, SpellParticleCastFilter 24, ShowSpellEffects 1 --
`review/B1159/worker/oracle/m_b1159_rig/eqclient-after.ini` lines 63, 88, 106-108), set by
`--spell-particle-density/-opacity/-nearclip/-filter` and `--show-spell-effects`. Every
run prints `[fx-harness] particle options: ...`, writes `<frames>/particle_options.tsv`,
and EXITS 65 when the values differ from the capture's unless `--particle-options` is
passed (tools/model_viewer/model_viewer_fx_harness.cpp:885). It applies them for real:
SpellVisualFX's gate and creation density, and the spell family's opacity / near clip
knobs (tools/model_viewer/model_viewer_fx_harness.cpp:887). Before this card the harness
drew with `identityParticleKnobs()`, i.e. near clip 0 (no cull), not the capture's 2.0.
A gate script that measures existing frames runs `scripts/tools/fx_particle_options.py
<frames>` (prints the five values against the capture ini, exit 65 on a difference
without `--particle-options`, 66 for pre-B1165 frames). The review-tree gate drivers
(review/B1139/scripts/run_scene.sh, review/B1149/scripts/run_scene.sh,
review/B1165/worker/scripts/run_tour.sh) all start model_viewer, so the harness's own
exit 65 covers them; run_tour.sh also runs the checker (PO_CHECK=1).

## 8. Tour-7 before / after at the capture settings (acceptance E)

The first pass's before/after ran on dev-u24 (base fable bd82e0e2, two base runs) and its
458 repeats on the ThinkPad; review note 7756 found 5 of its 8 moved cells OUTSIDE the two
base values, and a cross-host comparison. That table is superseded; this is the fix leg's
measurement, ALL on the ThinkPad (172.16.200.205):

* Binaries: BASE = fable baa94f20 (the MR's target tip) in a throwaway worktree, built by
  tbuild (`/home/user/willeq-builds/kb-b1165-base/build/bin/model_viewer`, md5
  3227cd23e8397f56a8b1136d892bb7ee); AFTER = this branch with baa94f20 merged in (md5
  225431bf2ac26cca9d9feb2223742ce9, rebuilt at the final tip with the same md5:
  `review/B1165/worker/fix/model_viewer_md5_final.txt`).
* Settings: the AFTER runs used the harness default = the capture's Spell page (density 1.0 /
  opacity 1.0 / near clip 2.0 / filter 24 / ShowSpellEffects 1); the checker
  `scripts/tools/fx_particle_options.py` passed on all 21 after runs (`po_check rc=0` in
  each `runs/after*_tour_*/particle_options.log`). The BASE binary has no such flags: it
  draws at density 1, opacity 1, no near clip (`identityParticleKnobs()`), no filter, the
  only Spell page fable had before this card.
* Recipe: `review/B1165/worker/fix/scripts/tp_tour.sh <tag> <slug>` (B1160's tour-7 rows,
  b1118 camera, 30 fps, `--seed 1`, measured on dev-u24 with
  review/B1139/scripts/harness_measure.py, FX_KEEP=1, exactly as run_scene.sh), run
  INTERLEAVED base1, after1, base2, after2, base3, after3 so the box's other load (another
  session's renders, loadavg 2.7-19 in `review/B1165/worker/fix/tour.log`) falls on both.
  42 renders, all `render rc=0 measure rc=0`; every frames dir is KEPT under
  `review/B1165/worker/fix/frames/<tag>_tour_<spell>/` (+ `.measure/`), the summaries under
  `review/B1165/worker/fix/runs/<tag>_tour_<spell>/measure/summary.tsv`.

Result (`review/B1165/worker/fix/scripts/spread_table.py`, output
`review/B1165/worker/fix/tour_spread_E.tsv`): for each of the 7 spells x 15 summary columns =
105 cells, the base min/max over base1..3 and every after value. **All 105 cells are
IDENTICAL in all six runs** (the base spread is zero-width and every after value equals
it), so (E) "no gate number moves at the capture settings" is MET as stated, not as a noise
bound. The summary rows:

| spell | land_cov | land_t | fxt_cov | end | roi_l | roi_s | src_cov | src_ext |
|---|---|---|---|---|---|---|---|---|
| 93 | 1.992 | +2.07 | 1.992 | +6.633 | 0.045 | 0.070 | 0.385 | 2.18 |
| 38 | 1.992 | +2.07 | 1.992 | +6.633 | 0.045 | 0.070 | 0.385 | 2.18 |
| 54 | 1.191 | +3.00 | 1.191 | +6.333 | 0.028 | 0.022 | 0.424 | 1.17 |
| 656 | 1.191 | +3.00 | 1.191 | +6.333 | 0.028 | 0.022 | 0.424 | 1.17 |
| 41 | 2.489 | +1.50 | 2.489 | +5.700 | 0.107 | 0.094 | 1.131 | 2.42 |
| 253 | 4.174 | +1.02 | none | - | 0.087 | 0.218 | 3.331 | 3.18 |
| 458 | 0.501 | +2.95 | 0.501 | +5.183 | 0.061 | 0.040 | 0.117 | 0.64 |

(every column of every run: `tour_spread_E.tsv`.) At the pixel level the harness is not
deterministic run to run, with exactly TWO outcomes per spell
(`review/B1165/worker/fix/scripts/frame_diff.py` -> `frame_identity.tsv`, classes in
`frame_modes.tsv`): a run's frames are byte-identical either to mode A or to mode B; B differs
from A on 206 of 435 frames (191 of 483 for 458), by 1 to 49 pixels per frame (max channel difference 160;
`review/B1165/worker/fix/mode_pixels.tsv`, every differing frame of one A/B pair per spell), and
it starts at frames 1-8, BEFORE the cast (t_begin 1.5 s = frame 45) -- the same frames in
every spell's B. Mode B occurred in base runs (54 base3, 41 base3) and in after runs (93
after2, 54 after1/after3, 656 after1/after3, 458 after1/after3); 38 and 253 were mode A in
all six. So the pixel noise is a scene-level harness state independent of the binary and
of the spell, and it does not reach the summary's 3-digit cells on this host. (The first
pass's dev-u24 runs DID show summary-level moves, e.g. 93 src_cov 0.384 / 0.386 between two
base runs; that host was not used here.)

These runs PREDATE B1169 (#488, MR !321, merged into this branch afterwards), which found
the mechanism: Irrlicht sorts the solid nodes by their texture POINTER, so ASLR changes the
caster / held item / target draw order and the depth ties at the caster's shoulder go to
whichever draws later -- 206 of 435 frames, first frame 1 (1 px, delta 36), up to 49 px a
frame (refactor/B1169/README.md sec. 1). Those are the same numbers as mode B here
(`mode_pixels.tsv`: 206 frames, 191 for 458, 1 to 49 px, frame 1 = 1 px delta 36), so modes A / B are the two
draw orders. B1169 fixed the harness's solid draw order; these 3 + 3 runs were not re-made.

The gates themselves at non-capture settings (93, rendered on the ThinkPad with
`--particle-options`; `review/B1165/worker/runs/dens*_tour_93`, `sse0_tour_93`):
density 0.5 -> land_cov 1.286, src_cov 0.232 (1.993 / 0.386 at 1.0); density 0 and
ShowSpellEffects 0 -> both "Source not drawn" and "Target stage not drawn" in run.log and
the two runs are pixel-identical on all 435 frames (land_cov 0.008, src_cov 0.046: the
caster's own motion, no particle drawn). The checker exits 65 on those frames without
`--particle-options` (`runs/dens0.5_tour_93/particle_options.log`), and the harness exits 65
before rendering at density 0.5 without it (`runs/pin/density05_noflag.log`, rc=65).

## 9. Residues and follow-ups

1. N2 (near clip culls nearer) -- the engine words +8/+0xC/+0x10 are stored, their read is
   not located (sec. 2).
2. Legacy `ParticleDensity` load order (sec. 5): the client starts with the legacy value as
   the Env density and ignores the loaded EnvironmentParticleDensity until its combobox
   changes; fable uses the Env page's value from load. `/particles` is not in fable.
3. The legacy-path third arm of the stage gate (spell +0x230 in 61..64, `FUN_0044ced9`).
4. Off-table ini values: FIXED in the fix leg (sec. 1 "Applied vs shown", sec. 3): the
   clamped float / raw filter int is applied, the choice is only displayed. Not re-checked
   here: the opacity slider's step count. The oracle's slider shows opacity x 100
   (eqgame.exe.c 243097, `FUN_0053ac98(_DAT_00985814 * _DAT_00640fc8)`, 0x640fc8 = 100.0 as a
   double) and sets position x 0.01 (245434, _DAT_00640038 = 0.01); fable's slider has 20
   steps (iniDivisor 20). The applied opacity is the ini float either way.
5. The FX harness's run-to-run pixel noise (sec. 8) is B1169's ASLR solid draw order, fixed
   by B1169 after these runs; no B1165 follow-up beyond re-making (E) identity runs on a
   post-B1169 harness if a pixel-identity gate is wanted.
6. B1163's spell-859 missile-removal Target stage (`fireSpell859RemovalAtPlayer`) fires
   through FUN_0049b260 in the oracle (0x47f5ed), so it now passes the same stage gate
   (src/client/graphics/spell_visual_fx.cpp `stageDraws`); the target is the player, which
   the filter always shows, so only density / opacity / ShowSpellEffects can hide it. Not
   exercised by a test: fable builds no 859 missile (record 85 has no Middle,
   refactor/B1163/README.md:235).
