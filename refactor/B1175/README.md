# B1175 (#494): Options > Particles -- the opacity sliders' steps and the ini file

Two defects found by the B1165 reviews (refactor/B1165/README.md sec. 9 item 4; MR !318
notes 7756 / 7919), fixed so the window behaves like the official client's. Every number
below is read from the client's own code: decompile = `review/B1141/ghidra/out/`
(`eqgame.exe.c`, `eqgame.dis`), binary = /mnt/projects/EverQuestP1999/eqgame.exe (md5
a9de1b8cc5c451b32084656fcacf1103, the lane's own; B1165 README sec. "binaries"). Data words
were read with `review/B1165/worker/scripts/peread.py`; strings with a NUL-terminated
reader of the same section table (`python3 -I pestr.py eqgame.exe <va>`, kept at
`review/B1175/worker/scripts/pestr.py`). `eqgame.exe.c` line cites are not checked by the
cite checker (it knows no `.c`); every one below was re-read by hand at the line given.

## 1. The opacity sliders (ask 1, acceptance A)

| quantity | client | where | fable (this card) |
|---|---|---|---|
| sliders | Spell, Environment, Actor (ODP_SpellParticlesOpacitySlider / ODP_EnvironmentParticlesOpacitySlider / ODP_ActorParticlesOpacitySlider, window offsets +0x270 / +0x280 / +0x294) | name lookups eqgame.exe.c:246720, :246764, :246818 | Spell and Env wired; Actor stays unwired (B995 N3: fable has no actor particles) |
| positions | 0x65 = 101 (0..100): `push 0x65; call 0x5bd180` for each of the three | 0x541032..0x541060 (eqgame.dis 403711-403725); 0x5bd180 = FUN_005bd180 (eqgame.exe.c:343718) stores the count at +0x15c; the constructor default is `push 0xa` (0x5bd2d4) | maxValue 100 and default 100 in the `Setting::SpellParticleOpacity` row (src/client/graphics/ui/options_settings.cpp:185) and the `Setting::EnvParticleOpacity` row (src/client/graphics/ui/options_settings.cpp:200); `kParticleOpacitySliderPositions` (include/client/graphics/ui/options_settings.h:271) |
| clamp | SetValue FUN_005bcd10 (eqgame.exe.c:343498) clamps a position to [0, count - 1] | eqgame.exe.c:343498 | `isValidSettingValue` 0..100 |
| position -> opacity | `fild [ebp+0x10]; fmul dword [0x640038]; fstp dword` -- position x 0x3c23d70a = 0.01f | eqgame.exe.c:245434 (Spell), :245447 (Env), :245460 (Actor); 0x53ee4e..0x53ee5c | `iniStepValue` = `kOpacityStep` 0.01f (src/client/graphics/ui/options_settings.cpp:91); `settingTableFloat` = p x 0.01f |
| label | `FUN_005738b0(buf, "%d %c", position, 0x25)`; 0x65944c = "%d %c", 0x25 = '%' -> "37 %" | eqgame.exe.c:245436, :245449, :245462; 0x53ee5f..0x53ee74 | `opacitySliderLabel` (include/client/graphics/ui/options_settings.h:281); optionsSettingText calls `opacitySliderLabel` at src/client/graphics/ui/options_panel.cpp:98 |
| float -> position (window open / ini) | `FUN_005bcd10(FUN_0053ac98(_DAT_00985814 * 100.0))`, 0x640fc8 = 100.0 (double); FUN_0053ac98 (eqgame.exe.c:242951): fmod(x, 1) `fcomp [0x6410bc]` (0.5f) -> below: floor (FUN_0060fac0), else ceil (FUN_00612b00), then ftol | eqgame.exe.c:243666, :243670, :243674; 0x53ac98..0x53acd4 | `opacitySliderPosition` (include/client/graphics/ui/options_settings.h:275), used by the ini parse (src/client/graphics/ui/options_settings.cpp:437) |
| written value | the handler's float, "%f" (FUN_004ae55f, eqgame.exe.c:132108), Spell via LAB_0053eb36 (`call 0x4ae55f` at 0x53eb40), Env :245454, Actor :245467 | eqgame.exe.c:245454, :245467 | `settingIniText` "%f" of p x 0.01f |

The applied value stays the raw float (B1165's rule): an ini 0.333 shows position 33 ("33 %")
and applies 0.333; moving the slider applies p x 0.01f. The geometry takes the setting's own
step count (`sliderSteps`, include/client/graphics/ui/options_layout.h:411; click
src/client/graphics/ui/options_lists.cpp:148, knob src/client/graphics/ui/options_panel.cpp:218),
so the T434 sliders keep 0..20. The UI capture `options_display_particles_env` sets 60 (was 12
of 20): the same knob pixel (92 x 60 / 100 = 92 x 12 / 20 = 55) and the same "60 %".

Tests (tests/test_options_particles.cpp):
* `EachOpacitySliderIs101PositionsOfPointZeroOne` (tests/test_options_particles.cpp:525):
  per slider (Spell, Env) and EVERY position 0..100 -- position -> the applied knob value
  (bit-equal to p x the float 0x3c23d70a) -> the label "p %" -> the "%f" ini text -> the
  client's read of that text lands on p again; Actor unwired.
* `TheSliderPositionOfAnIniFloatIsTheClientsRounding` (tests/test_options_particles.cpp:567):
  0.254 -> 25, 0.255 -> 26 (x = 25.5 -> ceil), 2.0 -> 100, -0.5 -> 0; 0.333 shown 33, applied 0.333.
* `TheOpacitySliderSpansItsHundredPositions` (tests/test_options_particles.cpp:590): both ends
  reachable by click, the knob monotone over 101 positions, the right end flush.

## 2. The ini file and section (ask 2, acceptance B)

The client reads every particle key from **section "Defaults"** (string 0x641a60) of
**.\eqclient.ini**, global (one file beside the executable, not per character):

* Loader, eqgame.exe.c:132608-132671: `FUN_004ae67a("Defaults", "SpellParticleDensity" /
  "SpellParticleOpacity" / "SpellParticleNearClipPlane" / "EnvironmentParticleDensity" /
  "EnvironmentParticleOpacity" / "EnvironmentParticleNearClipPlane", ...)` and
  `FUN_004ae64c("Defaults", "SpellParticleCastFilter" / "EnvironmentParticleCastFilter", 0x18)`.
  FUN_004ae67a (eqgame.exe.c:132178) and FUN_004ae64c (eqgame.exe.c:132165) read
  `.\defaults.ini` first and then `.\eqclient.ini`.
* ShowSpellEffects, eqgame.exe.c:126007-126011: `("Defaults", "ShowSpellEffects", ..., ".\eqclient.ini")`.
* Writers: FUN_004ae514 (int, "%d", eqgame.exe.c:132091) and FUN_004ae55f (float, "%f",
  eqgame.exe.c:132108) both write `".\eqclient.ini"`; the window's handlers pass "Defaults"
  (combobox handlers eqgame.exe.c:245163-245204, the write at :245310-245311, `LAB_0053e96e: FUN_004ae514(...)`, with the
  section from `CONCAT44(pcVar10, "Defaults")` at :245186; the opacity sliders at :245454, :245467 and 0x53eb40);
  the bulk writer FUN_004aed0e (eqgame.exe.c:132456) writes every particle key to "Defaults"
  at eqgame.exe.c:132526-132553; `/showspelleffects` writes eqgame.exe.c:88045.

fable before this card kept them in the per-character `UI_<Char>_<server>.ini` `[Options]`.
Now:

* Every such row carries `eqclientSection` = "Defaults" (include/client/graphics/ui/options_settings.h:200;
  `kDefaults` src/client/graphics/ui/options_settings.cpp:87): the eight keys SpellParticleOpacity /
  Density / NearClipPlane / CastFilter, ShowSpellEffects, EnvironmentParticleOpacity / Density /
  NearClipPlane (`isEqClientIniSetting`, include/client/graphics/ui/options_settings.h:284).
* They live in fable's GLOBAL `<cache dir>/eqclient.ini` (include/client/graphics/ui/eqclient_ini.h:18),
  read by `ParseEqClientIni` (src/client/graphics/ui/eqclient_ini.cpp:153: section and key names
  case-insensitive as the Windows profile API; the first `[Defaults]` and the first line of a key
  win) and written by `UpdateEqClientIniText` (src/client/graphics/ui/eqclient_ini.cpp:188:
  WritePrivateProfileString-style -- the file is kept line for line, a present key is replaced in
  place, a missing key goes to the end of `[Defaults]`, a missing section to the end; all of
  fable's eqclient keys are written when one changes, as FUN_004aed0e writes them all).
  `FrameComponent::syncEqClientIni` (src/client/graphics/frame/renderer_frame_preferences.cpp:70)
  loads once and saves on a difference; syncUiPreferences calls `syncEqClientIni` first
  (src/client/graphics/frame/renderer_frame_preferences.cpp:89), so it needs no character.
* `IsPersistedSetting` (src/client/graphics/ui/ui_preferences.cpp:123) now excludes them, so the
  per-character file neither writes nor reads them.

**What happens to an existing per-character value: nothing is migrated.** The client has no such
key in `[Options]`, so fable stops reading it; a value an older fable wrote there is ignored and
dropped at that file's next write, and the global file (or the default = the capture's values)
wins. Documented at include/client/graphics/ui/eqclient_ini.h:40 (NO MIGRATION) and
include/client/graphics/ui/ui_preferences.h:148, where fable documents its ini files.

Not done here (stated, not substituted): fable does not read `defaults.ini` for these keys (the
shipped /mnt/projects/EverQuestP1999/defaults.ini, md5 4682dda2896ce290fc9bcd8fe67a5cb7, has 0
lines matching `particle` or `ShowSpellEffects`), and does not read or write the client
install's own eqclient.ini (eqClientPath is read-only asset data); fable's file is in its cache
directory, as UI_<Char>_<server>.ini is.

Tests (tests/test_options_particles.cpp):
* `TheParticleKeysAreEqclientIniDefaultsNotPerCharacter` (tests/test_options_particles.cpp:621):
  exactly the eight rows, each "Defaults"; the per-character text carries none of the eight keys;
  an old per-character `[Options] SpellParticleOpacity=0.25 / ShowSpellEffects=0 /
  SpellParticleCastFilter=1` loads as 100 / 1 / 24 (ignored); the global text carries them.
* `TheCaptureEqclientIniLoads` (tests/test_options_particles.cpp:673): the lane-5 capture ini
  `review/B1159/worker/oracle/m_b1159_rig/eqclient-after.ini` (byte-identical to
  `review/B1130/oracle/e17/eqclient-after.ini`, md5 453da624da093a2aa62aa71d4b2ca8e2, 9728 bytes,
  CRLF) copied to tests/data/b1175/eqclient-after.ini, loads to its lines 63 (ShowSpellEffects=1),
  88-89 (opacity 1.000000 -> position 100) and 106-110 (density 1.0 = High, near clip 2.0 = Near,
  filter 24), no applied off-table float, equal to fable's default, `IsOracleCaptureSpellFxOptions`;
  writing the same values back changes no byte; a changed opacity rewrites only its own line
  (CRLF kept; ActorParticleOpacity line 90 and `[Options] ParticleDensity=3` line 442 untouched).
* `TheIniReadAndWriteFollowTheProfileApi` (tests/test_options_particles.cpp:713) and
  `TheGlobalFileSavesAndLoadsInTheCacheDir` (tests/test_options_particles.cpp:735, a private
  WILLEQ_CACHE_DIR: save into a copy of the capture, load back).
* The B1165 tests that persisted through the per-character file now persist through eqclient.ini
  (same assertions; opacity steps rescaled x5).

## 3. FX gate captures unchanged (acceptance C)

The FX harness runs at the capture's Spell page (B1165 sec. 7: SpellParticleOpacity 1.0 =
position 100, density 1.0, near clip 2.0, filter 24, ShowSpellEffects 1), and model_viewer does
not link the Options code at all: the ThinkPad builds of BASE (fable f61fafb4, detached
throwaway worktree wt/kb-b1175-base) and AFTER (this branch at ff75d62a) are the SAME binary,
`md5sum` 65f17eb67cb5de5b7866a8dd3badd28d for both
(/home/user/willeq-builds/kb-b1175-base/build/bin/model_viewer and
/home/user/willeq-builds/kb-b1175-particle-sliders/build/bin/model_viewer).

Measured anyway, post-B1169 (f61fafb4 contains B1169's fixed solid draw order): tour-7 (B1160's
rows: 93, 38, 54, 656, 41, 253 self, 458; b1118 camera, 30 fps, `--seed 1`) rendered on the
ThinkPad (172.16.200.205) with `review/B1175/worker/scripts/tp_tour.sh <tag> <slug>` (a copy of
B1165's `tp_tour.sh` writing under review/B1175/worker, plans from review/B1165/worker/plans),
interleaved base1, after1, base2, after2: 28 renders, all `render rc=0 measure rc=0`
(`review/B1175/worker/tour.log`), the particle-options checker `po_check rc=0` on all 28
(`runs/*/particle_options.log`). Measured on dev-u24 with review/B1139/scripts/harness_measure.py
(FX_KEEP=1); every frames dir kept under `review/B1175/worker/frames/<tag>_tour_<spell>/`.

* Summary cells (`scripts/spread_table.py runs tour_spread.tsv base1,base2 after1,after2`, output
  `review/B1175/worker/tour_spread.tsv`): **105 cells, 0 vary across the 4 runs, 0 moved.**
* Pixels (`scripts/tools/fx_frame_identity.py`, `review/B1175/worker/frame_identity.txt`):
  base1 vs after1, base1 vs base2 and after1 vs after2 are byte-identical on every frame of every
  spell -- 435 frames each for 93 / 38 / 54 / 656 / 41, 444 for 253, 483 for 458; 0 differing.

| spell | land_cov | land_t | fxt_cov | end | roi_l | roi_s | src_cov | src_ext |
|---|---|---|---|---|---|---|---|---|
| 93 | 1.992 | +2.07 | 1.992 | +6.633 | 0.045 | 0.070 | 0.385 | 2.18 |
| 38 | 1.992 | +2.07 | 1.992 | +6.633 | 0.045 | 0.070 | 0.385 | 2.18 |
| 54 | 1.191 | +3.00 | 1.191 | +6.333 | 0.028 | 0.022 | 0.424 | 1.17 |
| 656 | 1.191 | +3.00 | 1.191 | +6.333 | 0.028 | 0.022 | 0.424 | 1.17 |
| 41 | 2.489 | +1.50 | 2.489 | +5.700 | 0.107 | 0.094 | 1.131 | 2.42 |
| 253 | 4.174 | +1.02 | none | - | 0.087 | 0.218 | 3.331 | 3.18 |
| 458 | 0.501 | +2.95 | 0.501 | +5.183 | 0.061 | 0.040 | 0.117 | 0.64 |

(identical in all four runs; every column in `tour_spread.tsv`; the same values as B1165's table.)

## 4. Gates (acceptance D, E)

* Targeted ctest on gitlab-runner-2 (tbuild, Release GLES2): the 36 suites of test_options_particles,
  test_options_window, test_options_wiring, test_options_colors, test_options_merchant_residue,
  test_ui_preferences, test_ui_preferences_identity, test_spell_fx_options, test_spell_slot_rules,
  test_frame_component and test_camera_zone_in_preference: 201 / 201 passed; after the last code
  change `^OptionsParticles`: 20 / 20.
* Hygiene on gitlab-runner-2: 48 / 48 (3 skipped by design). The first run failed DocCitations
  (this card's test-file edits moved refactor/B1165/README.md's test_options_particles.cpp and
  options_settings.cpp cites; re-pointed, B1165 README now `checked 122 cites, 0 problems`) and
  ZeroCallers (a tests-only helper; removed).
* CI: the MR's pipeline (see the MR).
* Cite checker on this README: see the hand-off line.

## 5. Residues

1. The Actor page's opacity slider (and the Env / Actor "On For" filters) stay unwired (B995 N3);
   when wired they take the same 101-position rule and their keys (ActorParticleOpacity,
   EnvironmentParticleCastFilter, ActorParticle*) are already kept by the eqclient.ini writer
   (it preserves keys it does not own).
2. Value parsing keeps B1165's rule (junk keeps the default). The client's own readers differ on
   junk: FUN_004ae64c is a GetPrivateProfileInt-style read (non-numeric -> 0, not the default) and
   ShowSpellEffects uses `atol` (non-numeric -> 0 = off) and rewrites an absent or out-of-range
   value as 1 (eqgame.exe.c:126007-126011). Not changed here (outside asks 1-2); FOLLOW-UP.
3. include/client/graphics/ui/CLAUDE.md's Options paragraph says "Renderer settings persist per
   character under their eqclient.ini key"; the particle keys are now the exception. CLAUDE.md
   files are protected for this card, so the sentence is left for the orchestrator.
