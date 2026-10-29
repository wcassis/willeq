# B1189 (#508): effect-path + EDD-vocabulary census

Report only — no implementation. This is the specification in issue #508, answered:
every client path that creates a spell visual (cited against the decompile, with the
willeq implementing site or `NONE`), the EDD/EFF vocabulary the 47 measured records
exercise vs the whole file population, the legacy-path spell population, and the
uncovered paths/values ranked by how many castable spells use them.

**Will's rule applied throughout:** every client path is in scope; a `NONE` row is a
defect to be carded, never an option.

## 0. Sources and method

| input | path | note |
|---|---|---|
| spellsnew.eff | `/mnt/projects/EverQuestP1999/spellsnew.eff` | 112,024 B = 418 records × 0x10c, no header |
| spellsnew.edd | `/mnt/projects/EverQuestP1999/spellsnew.edd` | 881,096 B = 8 B header (`EDD\0`+ver 110) + 2,118 entries × 0x1a0 |
| spells_us.txt | `/mnt/projects/EverQuestP1999/spells_us.txt` | 8,066 spell rows, `^`-separated, **variable width** (209/211/213/215 fields) |
| eqgame.exe | `review/B1141/ghidra/out/eqgame.exe.c` | decompile, cited per row |
| EQGraphicsDX9.dll | `review/B1141/ghidra/out/EQGraphicsDX9.dll.c` | decompile |
| measured rows | `tests/spell_anim_rows.h` (B1137) + `refactor/B1184/tables/tour.tsv` | the 47 measured records |
| byte layouts | `docs/spellsnew_format.md`, `review/B1141/edd_fields_from_code.tsv` | the 70 PROVEN EDD fields |

Scripts (committed, `refactor/B1189/scripts/`): `spellsnew_lib.py` (parsers),
`vocabulary.py` (the §2 tables → `refactor/B1189/tables/*.tsv`), `ranking.py`
(§3 → `tables/ranking.tsv`). **Every count in this README is produced by those
scripts over the mounted files — none is retyped.** Regenerate:
`python3 refactor/B1189/scripts/vocabulary.py && python3 refactor/B1189/scripts/ranking.py`.

**The 47 measured records.** `tests/spell_anim_rows.h` (B1137) pins one
spellsnew.eff record index (`anim`, = the client's `field 145`) per measured
Source/Target row: 41 rows (the 40-record b1130 survey + row 51, the b1134 discipline
leg, record 326). `refactor/B1184/tables/tour.tsv` adds the 7 tour spells. Together the
47 measured rows map to **42 distinct spellsnew.eff records** (the 41 committed
`anim` values + record 137, tour spell 41). That 42-record set is "the measured
population" below.

**The P1999 `spells_us.txt` is variable-width** — rows carry 209, 211, 213 or 215
fields (209: 4,223 rows, 211: 3,547, 213: 292, 215: 4 — counted by hand over the file). The spellsnew.eff record
index therefore sits in a different column per spell (139 for the 209-field rows,
further right for wider rows). `spellsnew_lib.spellsnew_record()` resolves a spell's
record by scanning a small window of candidate columns (139–145) and taking the first
that names a **non-empty** spellsnew.eff record — the client's own gate at
`review/B1141/ghidra/out/eqgame.exe.c:69391` (`0 < idx < record_count && name != ""`). This is why a single
fixed column (the "field 145" of the Titanium file) does not reproduce the survey on
the P1999 file.

## 1. Dispatch-path census (Ask 1)

Every path by which the official client creates a spell visual. Client cite =
`review/B1141/ghidra/out/*.c:LINE` (the function-definition line). Willeq cite =
`file:LINE` or `NONE`. Stage numbers: 0 Source, 1 Middle, 2 Target.

| # | path | trigger / dispatch | condition that selects it | data it reads | client cite | willeq site |
|---|---|---|---|---|---|---|
| P1 | **Stage 0 Source, spellsnew** (cast glow) | `OP_BeginCast` 0x3990 → `0x49b39d` → `0x49b260(0)` → `0x495cc3` | `spell[0x8d] != 0` (a spellsnew record exists; gate `0x44dbf0`) | spellsnew.eff record + slot words (EDD idx/minLevel/anchor/attach) → spellsnew.edd entry | `review/B1141/ghidra/out/eqgame.exe.c:122037` (`0x49b39d`), `review/B1141/ghidra/out/eqgame.exe.c:121984` (`0x49b260`), `review/B1141/ghidra/out/eqgame.exe.c:118440` (`0x495cc3`), `review/B1141/ghidra/out/eqgame.exe.c:69391` (record gate) | `src/client/player/player_buffs.cpp:92` → `src/client/bridge/spell_cast_fx.cpp:12` → `src/client/graphics/spell_visual_fx.cpp:533` (createCastGlow), slots at `src/client/graphics/spell_visual_fx.cpp:160` |
| P2 | **Stage 0 Source, legacy fallback** | same `OP_BeginCast` → `0x49b39d` → `0x49b260(0)` → `0x49ab78` | `spell[0x8d] == 0` (no spellsnew record; the stage player's select `if (param_2[0x8d] == 0)` at `review/B1141/ghidra/out/eqgame.exe.c:122020`; `0x49b39d`'s own `0x49ab78` call at `review/B1141/ghidra/out/eqgame.exe.c:122113` is a fixed effect `&DAT_0087ad00, 0x3d`, not the record path) | legacy spells.eff record via `spell[0x8b]` (gate `0x44dbf0`, 0x2ba pointer stride) | `review/B1141/ghidra/out/eqgame.exe.c:122020` (select), `review/B1141/ghidra/out/eqgame.exe.c:122022` (the `0x49ab78(param_1,param_2[0x8b],…)` call), `review/B1141/ghidra/out/eqgame.exe.c:121698` (`0x49ab78`), `review/B1141/ghidra/out/eqgame.exe.c:68688` (`0x44d773` spells.eff loader) | **NONE** — the legacy `spells.eff` path is not modelled (the "Not modelled" list, `refactor/B1170/README.md:515`) |
| P3 | **Stage 2 Target, spellsnew** (landing) | spell complete/impact → `0x47789d` → `0x49b260(2)` → `0x47f614` | stage 2, `spell[0x8d] != 0`; `0x47789d` compares `*(int *)(param_1 + 0x5c) == 0x36` (a value comparison, `review/B1141/ghidra/out/eqgame.exe.c:95659`) | spellsnew.eff Target stage slots → spellsnew.edd | `review/B1141/ghidra/out/eqgame.exe.c:95588` (`0x47789d`), `review/B1141/ghidra/out/eqgame.exe.c:121984` (`0x49b260`) | `src/client/bridge/spell_cast_fx.cpp:34` → `src/client/graphics/spell_visual_fx.cpp:851` (createLanding) → `src/client/graphics/spell_visual_fx.cpp:971` (createLandingFromSlots) |
| P4 | **Stage 1 Middle, spellsnew** (missile flight) | server 0x4033 missile object → `0x476b12` (the ONLY stage-1 caller) → `0x49b260(1)` | a missile object exists — a spell with no missile never plays its Middle | spellsnew.eff Middle stage; caller duration = spell field 18 (AE duration) | `review/B1141/ghidra/out/eqgame.exe.c:94981` (`0x476b12`), `review/B1141/ghidra/out/eqgame.exe.c:121984` (`0x49b260`) | `src/client/zone/zone_dispatch.cpp:846` → `src/client/player/player_buffs.cpp:83` (ZoneProcessSomeItemPacketMaybe) → `src/client/graphics/spell_visual_fx_missile.cpp:245` |
| P5 | **Target/missile visual create** (OP_Action) | `OP_Action` 0x395e → `0x45b8f0` → `0x45448b` | combat/spell action with a target visual | spellsnew.eff Target slots | `review/B1141/ghidra/out/eqgame.exe.c:79516` (`0x45b8f0`), `review/B1141/ghidra/out/eqgame.exe.c:74010` (`0x45448b`) | `src/client/zone/zone_dispatch.cpp:864` → `src/client/commands/combat_actions.cpp:132` (`ZoneProcessAction`) |
| P6 | **Spawn-driven deferred Source** (NPC) | NPC spawn → `0x4bfe0f` → `0x49b260(0)` (deferred Source, no begin-cast; `0x4bfe0f` never calls `0x49b39d`) | NPC spawn carrying a spellsnew record, no begin-cast | spellsnew.eff Source stage | `review/B1141/ghidra/out/eqgame.exe.c:146548` (`0x4bfe0f`), `review/B1141/ghidra/out/eqgame.exe.c:146701` (its only stage-0 call, `FUN_0049b260(0,…)`) | **NONE** — createCastGlow is called only from the begin-cast arm (`src/client/bridge/spell_cast_fx.cpp:31`); `ZoneProcessNewSpawn` (`src/client/entities/entities_component_updates.cpp:253`) spawns the entity but does not draw the deferred Source |
| P7 | **Ability / discipline visual** | ability record (same 0x10c stride) → `0x4b6908` | ability/discipline use, not a spell | ability record (spellsnew.eff-shaped) | `review/B1141/ghidra/out/eqgame.exe.c:138087` (`0x4b6908`) | **NONE** — ability/discipline visuals are not modelled (B1162) |
| P8 | **Buff-tick visual (legacy)** | per-frame buff list walk `0x4368b3` → `0x49ab78(2, …*0x2ba, …)` | buff `+0x22=='5'`, `+0x23=='\x01'` (`review/B1141/ghidra/out/eqgame.exe.c:49807`), `+0x94==0`, `+0x66` (effect idx) with `0 < +0x66 < 0x100` (`review/B1141/ghidra/out/eqgame.exe.c:49813`) | legacy spells.eff record (0x2ba stride) | `review/B1141/ghidra/out/eqgame.exe.c:49742` (`0x4368b3`), `review/B1141/ghidra/out/eqgame.exe.c:49813` (the `0x49ab78(2, &DAT_008513b8 + sVar2*0x2ba, …)` call) | **NONE** — willeq tracks buff state in ticks (`src/client/spell/buff_manager.cpp:118`) but draws no tick-driven legacy aura FX |
| P9 | **Legacy `spells.eff` path (general)** | `0x49ab78`, reached from P2/P8 and the stage dispatcher | `spell[0x8d]==0` | legacy spells.eff (0x2ba record / 0x3a0 sound stride) | `review/B1141/ghidra/out/eqgame.exe.c:121698` (`0x49ab78`) | **NONE** (`refactor/B1170/README.md:515`) |
| P10 | **SPA overrides** (spell-particle-asset) | `0x47f4af`, reads the asset's `+0x180` | a per-spell asset override is present | spellsnew asset words | `review/B1141/ghidra/out/eqgame.exe.c:101890` (`0x47f4af`), `review/B1141/ghidra/out/eqgame.exe.c:101920` (the `+0x180` read) | PARTIAL — `spellOverrides` (`src/client/graphics/environment/spell_effects_config.cpp:204`, applied at `:488`) + the two Options (`src/client/graphics/environment/unified_particle_renderer.cpp:521`); 5 spellOverrides remain (38/41/54/458/656 in `config/spell_effects.json`; 253 retired by B1161 #480, `CLAUDE.md:417`; the `docs/spellsnew_format.md:51` "six" sentence is stale B1141-era text) |
| P11 | **Missile/impact position integrator** (spell 859's path) | `0x47f614`, integrates `+0x3c/+0x40/+0x44` | the spell-859-class missile visual | the missile actor + emitter direction | `review/B1141/ghidra/out/eqgame.exe.c:101965` (`0x47f614`) | PARTIAL — the record-driven missile emitter `src/client/graphics/spell_visual_fx_missile.cpp:274` flies the Middle; the client's dedicated 859 integrator has no willeq twin |
| P12 | **Global spell-FX flags** `0x923798` / `0x923c30` | global toggles read across the FX paths | the DAT_00923798 (char) and DAT_00923c30 flags, when set | the flags gate the whole FX system | `review/B1141/ghidra/out/eqgame.exe.c:15569` (`0x923798` first ref), `review/B1141/ghidra/out/eqgame.exe.c:79559` (`0x923c30`) | **NONE** — the client's global FX on/off switches have no willeq equivalent (willeq always draws when a slot passes) |
| P13 | **Aura / ground circle** | `0x491cff` | aura/ground-circle effect | spellsnew asset | `review/B1141/ghidra/out/eqgame.exe.c:115161` (`0x491cff`) | **NONE** — the aura/ground-circle entry points (createBuffAura, createGroundCircle, createRainEffect, createImpactAtPosition) were **deleted by B1138** (`include/client/graphics/spell_visual_fx.h:190`); the .cpp holds only createCastGlow/createImpact/createLanding/createLandingFromSlots/createSpellComplete; the `src/client/graphics/CLAUDE.md:56` "no production caller" sentence is stale |
| P14 | **Buff/aura** | `0x44d0d0` | buff/aura effect | spellsnew asset | `review/B1141/ghidra/out/eqgame.exe.c:68314` (`0x44d0d0`) | **NONE** — entry points deleted by B1138 (`include/client/graphics/spell_visual_fx.h:190`); nothing in the tree draws a buff aura |
| P15 | **`OP_CastSpell` 0x304b** (cast initiation) | client casts → `0x45b8f0`/`0x411f3d` | the player initiates a cast | the cast request | `review/B1141/ghidra/out/eqgame.exe.c:79516` (`0x45b8f0`), `review/B1141/ghidra/out/eqgame.exe.c:18097` (`0x411f3d`) | `src/client/spell/spell_publish.cpp:40` — willeq *sends* HC_OP_CastSpell; the resulting visual arrives via P1 (OP_BeginCast) |
| P16 | **`OP_MagicAffectVisual` 0x525d** | `0x45b8f0` | magic-affect-visual opcode | the affect payload | `review/B1141/ghidra/out/eqgame.exe.c:79516` (`0x45b8f0`) | **NONE** — no `HC_OP_MagicAffectVisual` handler in the tree |
| P17 | **`OP_InstillDoubt` 0x389e** | client receive-side arm NOT FOUND in the decompile — the only `0x389e` occurrence is the send-side store in `0x41f4a0` (`review/B1141/ghidra/out/eqgame.exe.c:28460`, `uVar14 = 0x389e;` → `FUN_005dfa00`); any visual on this opcode is unproven | instill-doubt (send side proven only) | — | `review/B1141/ghidra/out/eqgame.exe.c:28460` (send-side store) | `src/client/zone/zone_dispatch.cpp:791` (`OnInstillDoubt`) — state only, no FX |
| P18 | **Particle engine (draw side, EQGraphicsDX9)** | EDD load → emitter create → render → texture/blend | per-particle, every frame | spellsnew.edd entry words | `review/B1141/ghidra/out/EQGraphicsDX9.dll.c:70024` (`0x100644e0` EDD load), `review/B1141/ghidra/out/EQGraphicsDX9.dll.c:63389` (`0x1005a430` CreateEmitter), `review/B1141/ghidra/out/EQGraphicsDX9.dll.c:68017` (`0x100605b0` Render), `review/B1141/ghidra/out/EQGraphicsDX9.dll.c:67340` (`0x1005f400` texture/blend) | `src/client/graphics/environment/unified_particle_renderer.cpp` (the renderer), emitter config `src/client/graphics/environment/spell_effects_config.cpp` |

**NONE rows (defects to be carded, per Will's rule):** P2, P6, P7, P8, P9, P12, P13,
P14, P16. **PARTIAL:** P10, P11.

## 2. EDD/EFF vocabulary coverage (Ask 2)

Population sizes (from `vocabulary.py`): **42 measured records** reference **284
distinct spellsnew.edd entries**; the whole file has **417 non-empty EFF records**
that reference **1,655 distinct EDD entries** (of 2,118 in the file). "file_ref"
below = entries referenced by any non-empty EFF record; "file_all" = all 2,118.
`in_measured` = does the 42-record set exercise this value. The TSVs in
`refactor/B1189/tables/` are the source of truth; the tables are reproduced here.

### 2.1 Spawn shape (`+0xA8`)

| shape | meaning | measured | file_ref | file_all | in_measured |
|---|---|---|---|---|---|
| 0 | point | 28 | 226 | 297 | Y |
| 1 | even ring | 5 | 157 | 202 | Y |
| 2 | stacked rings | 0 | 8 | 13 | **N** |
| 3 | cylinder surface | 1 | 85 | 125 | Y |
| 4 | disc | 34 | 209 | 275 | Y |
| 5 | sphere surface | 117 | 647 | 808 | Y |
| 6 | box surface | 0 | 4 | 5 | **N** |
| 7 | line/cone | 0 | 3 | 9 | **N** |
| 8 | torus | 2 | 9 | 18 | Y |
| 9 | random ring | 97 | 307 | 366 | Y |

### 2.2 Sprite mode (`+0x70`)

| mode | meaning | measured | file_ref | file_all | in_measured |
|---|---|---|---|---|---|
| 0 | billboard | 284 | 1493 | 1911 | Y |
| 1 | streak (axis 1) | 0 | 160 | 205 | **N** |
| 2 | world-oriented quad | 0 | 2 | 2 | **N** |

### 2.3 Flags

| flag | offset | value | measured | file_ref | file_all | in_measured |
|---|---|---|---|---|---|---|
| orient_track | `+0x60` | 0 / 1 | 39 / 245 | 1312 / 343 | 1728 / 390 | Y / Y |
| no_depth | `+0x64` | 0 / 1 | 0 / 284 | 158 / 1497 | 206 / 1912 | **N** / Y |
| additive | `+0x68` | 0 / 1 | 2 / 282 | 500 / 1155 | 649 / 1469 | Y / Y |
| attach_scale | `+0x6C` | 0 / 1 | 3 / 281 | 92 / 1563 | 105 / 2013 | Y / Y |
| live_follow | `+0x74` | 0 / 1 | 274 / 10 | 1507 / 148 | 1900 / 218 | Y / Y |
| neg_radius | `+0x198` | 0 / 1 | 284 / 0 | 1654 / 1 | 2117 / 1 | Y / **N** |
| shape_scale | `+0x19C` | 0 / 1 | 0 / 284 | 89 / 1566 | 102 / 2016 | **N** / Y |

### 2.4 Frames (`+0x130`) and animation rate (`+0x134`)

| frames | meaning | measured | file_ref | file_all | in_measured |
|---|---|---|---|---|---|
| 1 | whole texture | 70 | 708 | 855 | Y |
| 2–4 | 2×2 atlas | 54 | 171 | 199 | Y |
| 5–8 | 4×2 atlas | 0 | 0 | 0 | **N (absent from the file)** |
| 9–16 | 4×4 atlas | 160 | 776 | 1064 | Y |
| >16 | whole texture | 0 | 0 | 0 | **N (absent from the file)** |

| fps bucket | measured | file_ref | file_all | in_measured |
|---|---|---|---|---|
| 0 | 1 | 34 | 59 | Y |
| <1 | 0 | 0 | 0 | **N (absent)** |
| 1–10 | 41 | 274 | 326 | Y |
| 11–30 | 236 | 1307 | 1673 | Y |
| 31–60 | 0 | 30 | 49 | **N** |
| >60 | 6 | 10 | 11 | Y |

### 2.5 Motion (velocity-word signatures, `+0xEC..+0x12C`)

The full per-signature table is `tables/edd_motion.tsv`. The signatures **present in
the file but in no measured record** (file_ref count in parentheses): swirl (132),
ax2 (2), ax2+ax3 (2), ax2+radial (4), ax3 (1), ax3+radial+swirl (3), up+ax2 (8),
up+ax2+ax3 (1), up+ax2+radial (4), up+radial+drift (8), up+radial+swirl+drift (10),
up+radial+swirl+gravity (28), up+swirl+gravity (13), and the remaining `ax*`/`drift`
combinations listed in the TSV. The measured set is dominated by `static` (59),
`up` (48), `up+radial` (39), `radial` (66), `radial+gravity` (26), `up+swirl` (2).

### 2.6 Textures (`+0x40`)

311 distinct values appear in the file (310 named + one "(none)"); the full table is `tables/edd_texture.tsv`.
The measured 42 records draw a subset; the file-only textures (e.g. `Dart*`, `Shard*`,
`SmokeTrail*`, `Jellyfisha`) are the "any spell but no measured record" set.

### 2.7 EFF slot words (measured records vs file)

| anchor (`+8`) | meaning | measured | file_ref | in_measured |
|---|---|---|---|---|
| 0 | fixed world position | 8 | 134 | Y |
| 1 | caster | 349 | 3800 | Y |
| 2 | the missile | 8 | 13 | Y |
| 3 | target | 139 | 1057 | Y |

| attach (`+12`) | meaning | measured | file_ref | in_measured |
|---|---|---|---|---|
| 0 | actor position | 174 | 2473 | Y |
| 1 | DEFAULT | 94 | 200 | Y |
| 2 | CHEST | 159 | 685 | Y |
| 3 | HEAD | 31 | 270 | Y |
| 4 | LHAND | 21 | 384 | Y |
| 5 | RHAND | 20 | 409 | Y |
| 6 | LFOOT | 0 | 160 | **N** |
| 7 | RFOOT | 4 | 262 | Y |
| 8 | actor origin | 0 | 0 | N (absent) |
| 9 | LEYE | 0 | 14 | **N** |
| 10 | REYE | 0 | 14 | **N** |
| 11 | MOUTH | 0 | 0 | N (absent) |
| 12 | CHEST→floor | 1 | 133 | Y |

The `file_ref` column counts raw EFF slots (across all 417 non-empty records); §3.1's
reach counts **spells**. They differ for the 0-reach attach values: attach 9/10 each
have 14 file slots, but those slots sit in 4 records that no spell in `spells_us.txt`
references, so 0 castable spells reach LEYE/REYE (and attach 8/11 have no slots at
all).

| min level (`+4`) | measured | file_ref | in_measured |
|---|---|---|---|
| 0 | 327 | 4795 | Y |
| 1 | 3 | 5 | Y |
| 2 | 2 | 4 | Y |
| 24 | 86 | 103 | Y |
| 39 | 85 | 96 | Y |
| 503 | 1 | 1 | Y |

### 2.8 Legacy-path population (spells with NO spellsnew.eff record)

**157 of 8,066 spells** resolve to no non-empty spellsnew.eff record (the union-of-
candidates rule of §0); the rest (7,909) carry a spellsnew record. These 157 are the
spells that go through the legacy `spells.eff` path (P2/P8/P9). Full list:
`tables/legacy_spells.tsv`; examples: 523, 637, 638, 639, 852, 888, 889, 1023, 1352,
1777, 1778, 1779, 1780, 1781, 1782, 1787, 1956, 2322, 2323, 2324.

## 3. Ranked follow-ups (Ask 3)

Ranked by how many **castable** spells use each uncovered path or value. Every
number is the `castable_reaching` column of `tables/ranking.tsv` (independently
re-derived in `scripts/ranking.py`). All 8,066 spells in the P1999 file have ≥1 class
level, so the castable population == the total spell population; a per-value reach is
a subset of it. No implementation in this card — each row names the follow-up card to
file and a one-line mechanism. NONE rows are defects to be carded (Will's rule), not
options.

### 3.1 Uncovered VALUES (file vocabulary no measured record exercises)

| rank | value | castable spells | mechanism (one line) | proposed card |
|---|---|---|---|---|
| 1 | flag `no_depth=0` (`+0x64`) | 1,497 | coverage gap, not a drawing gap: record particles already honour `+0x64` (`src/client/graphics/environment/unified_particle_renderer.cpp:503` `depthOn = p.hasEddMotion() && p.eddNoDepthWrite == 0`, word carried at `src/client/graphics/environment/spell_emitter_presets.cpp:404`), but no MEASURED record has `+0x64==0` — measure one | B1189-F5 |
| 2 | motion `swirl` alone | 1,140 | emit the EDD swirl speed words; the slot emitter currently drops pure-swirl | B1189-F2 |
| 3 | sprite mode 1 (streak) `+0x70` | 675 | orient the quad along axis 1 instead of billboard | B1189-F3 |
| 4 | attach 6 (LFOOT) `+12` | 641 | add the LFOOT `SPELLPOINT_*` bind so attach-6 slots draw | B1189-F4 |
| 5 | motion `up+radial+swirl+gravity` | 395 | compose the four velocity words (all individually present except swirl → F2) | (F2) |
| 6 | motion `up+ax2` | 159 | add the axial-2 velocity word | (F2) |
| 7 | motion `up+swirl+gravity` | 154 | compose up+swirl+gravity (swirl → F2) | (F2) |
| 8 | motion `up+radial+drift` | 132 | add the drift velocity word | (F2) |
| 9 | shape 2 (stacked rings) `+0xA8` | 86 | add the stacked-rings spawn shape to `shapeAt` | B1189-F6 |
| 10 | fps 31–60 `+0x134` | 82 | the measured set tops out at 30 fps; support the 31–60 band (pin it) | B1189-F7 |
| 11 | motion `ax2` | 71 | axial-2 word | (F2) |
| 12 | motion `up+radial+swirl+drift` | 56 | (F2) | (F2) |
| 13 | shape 6 (box surface) `+0xA8` | 46 | add the box-surface spawn shape | (F6) |
| 14 | motion `up+drift` | 43 | (F2) | (F2) |
| 15 | motion `up+ax2+radial` | 38 | (F2) | (F2) |
| 16 | motion `ax2+radial` | 35 | (F2) | (F2) |
| 17 | shape 7 (line/cone) `+0xA8` | 28 | add the line/cone spawn shape | (F6) |
| 18 | flag `shape_scale=0` (`+0x19C`) | 24 | honour "shape/offsets NOT scaled by attach scale" | B1189-F8 |
| 19 | motion `ax3+radial+swirl` | 21 | (F2) | (F2) |
| 20 | sprite mode 2 (world quad) `+0x70` | 16 | draw a world-oriented quad | (F3) |

The remaining uncovered motion signatures each reach ≤11 castable spells (full list in
`tables/ranking.tsv`) and all fold into **B1189-F2** (the swirl/axial/drift
velocity-word family). **Distinct value cards:** F2 (swirl/axial/drift motion words),
F3 (sprite modes 1+2), F4 (attach 6 LFOOT, 641), F5 (`no_depth`, 1,497), F6 (spawn
shapes 2/6/7), F7 (31–60 fps band, 82), F8 (`shape_scale`, 24).

### 3.2 Uncovered PATHS (the NONE rows from §1)

| path | castable spells / population | mechanism (one line) | proposed card |
|---|---|---|---|
| P2/P8/P9 legacy `spells.eff` | the **157 legacy spells** (no spellsnew record, §2.8) | draw a legacy `spells.eff` record (0x2ba stride) when `spell[0x8d]==0`, exactly as `0x49ab78` does | B1189-F1 |
| P6 spawn-driven Source (NPC) | not countable from `spells_us.txt` (NPC spawns, not the spell table) | draw the Source stage on NPC spawn without a begin-cast (`0x4bfe0f`) | B1189-F9 |
| P7 ability / discipline | not countable from `spells_us.txt` (ability records are a separate table, B1162) | draw ability records (0x10c stride) as spell visuals | B1189-F10 |
| P12 global FX flags `0x923798`/`0x923c30` | system-wide (gates all FX) | replicate the client's global FX on/off switches (always-on is a deviation, not an option) | B1189-F11 |
| P13 aura / P14 ground-circle | not countable from `spells_us.txt` (effect-class gated) | re-create the aura/ground-circle paths from the client code (the B1138-deleted entry points are gone, not dormant) | B1189-F12 |
| P16 `OP_MagicAffectVisual` 0x525d | affect users (not countable from `spells_us.txt`) | add an `HC_OP_MagicAffectVisual` handler that draws the affect | B1189-F13 |
| P10/P11 SPA + 859 integrator (PARTIAL) | 5 spells (38/41/54/458/656; 253 already retired by B1161) + spell 859 | retire the 5 spellOverrides once the file drives them; match the 859 integrator | B1189-F14 |

Frames 5–8 and >16, and fps <1, are **absent from the file** (0 entries) — no card;
dead vocabulary the client supports but the P1999 data never uses.

## 4. Verification

- Scripts run clean over the mounted P1999 data (see §0 for the regenerate command).
- Cite check (worker-side, `.c`-aware): `python3 -I scripts/refactor/cite_check.py
  refactor/B1189/README.md --tree . --extra /mnt/projects/fable
  --dis /mnt/projects/fable/review/B1141/ghidra/out/eqgame.dis
  --dis /mnt/projects/fable/review/B1141/ghidra/out/EQGraphicsDX9.dis`
  → `checked N cites, 0 problems, W address warnings`.
- Hygiene: `docs` + `scripts` only; no C++ touched, so the C++ hygiene tests are
  unaffected. The `DocCitations` anchored-cite rule (the only hygiene rule that reads
  `refactor/B*/`) passes because this README uses bare `file:line` and `review/...`
  cites (the latter report SKIP off a non-sibling checkout, never FAIL).
