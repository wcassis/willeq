# B1172 (#491): the official client's model-height rule -- size x 0.2 -- applied

Branch `task/kb-b1172-model-height`, cut at fable 9136b7a3, merged with fable 070a9215 (B1166 +
B1169) before any re-gate render and with f61fafb4 (B1173 + B1165) before the final push. The
specification is `glab issue view 491` (asks 1-3, acceptance A-E). Evidence:
`/mnt/projects/fable/review/B1172/worker/`. Every binary was built and every frame rendered on the
ThinkPad; every measurement ran on dev-u24 (`python3 -P`); the final ctests ran on gitlab-runner-1.

| binary | tree | sha256 |
|---|---|---|
| before | fable 070a9215 (worktree `wt/kb-b1172-anchor`, a local measuring branch, never pushed) | `review/B1172/worker/model_viewer_shas.txt:1` |
| after | this branch at 30860af2 (fable 070a9215 merged + the fix + the ratchet) | `review/B1172/worker/model_viewer_shas.txt:2` |

Both re-gate binaries predate the f61fafb4 merge (B1165's Options > Particles, B1173 docs); the
before/after delta is this card's alone, the merged tip is covered by the ctests (sec. 6).

## 0. Result

- **The client has no per-race art scale.** It draws a spawn at `size x 0.2` (every race except
  403 and >= 405), where `size` is the spawn's own size word or, when that word is not > 0, a
  race/gender default from a table in its code (Human 6, Dark Elf 5, Dwarf 4, Troll 8, ...); a
  dozen races then force their own scale (sec. 1).
- **Which model was off: the human target, by exactly 1.2.** The oracle's target is `race=1
  gender=0 size=6.000` on the wire, so the client draws it at 1.2; the dark-elf caster's size word
  is `0.000`, so the client draws her at her default 5 x 0.2 = 1.0. fable drew both at 1.0
  (`config/race_models.json` scale x size / 6). At the oracle's own target anchor (b1130, feet
  940,615): oracle 288 px, fable before 238 px (0.826x), after 285 px (0.990x); target / caster
  1.378 oracle, 1.139 before, 1.364 after; the camera, fitted on the caster, did not move (sec. 2).
- **The fix** (sec. 3): `include/client/graphics/actor_scale.h` is the client's rule, function
  for function; every spawn's node is scaled by it, except the LOCAL player's node, which fable
  draws with a translation-only matrix and so keeps the 1.0 it is drawn at (FOLLOW-UP 1). The
  harness caster and the live self entity carry the wire's size 0; the b1118 target stands on the
  caster's ground. B1166's standing-height ratchet moves 238 -> 285 px, re-measured.
- **Re-gate** (sec. 4): b1130 40 records -- the 12 fixed_self rows byte-identical, the 28
  fixed_npc rows all move (target drawn larger), landing_cov PASS 0 -> 0, landing_end PASS 0 -> 1
  (r22), p90_land PASS 6 -> 6; b1118 tour-7 + 40 records -- 13 rows (the self-target ones)
  byte-identical, 34 move, landing coverage up on 31 of them (x0.955..x1.238), source coverage
  down by at most 0.054. Before == B1157 in every cell except spell 253's two rows (B1161's
  retirement).
- **Per-target-height FX rows** (sec. 5): B1166's Fear 229 below-the-feet shortfall closes
  (h_p5 -0.06 -> -0.23, ground 0.14 -> 0.20 against the oracle's -0.26 / 0.17); the spell-93
  landing's lit pixels grow only 6-16 %, because fable still attaches the landing at s = 1 where
  the client multiplies it by this same actor scale (FOLLOW-UP 2).

## 1. The client's rule, line by line (acceptance A)

Decompile `review/B1141/ghidra/out/eqgame.exe.c` / `.dis` (P1999 `eqgame.exe`) and
`EQGraphicsDX9.dll.c` / `.dis`. Constants are read from the PE images with
`refactor/B1172/scripts/pe_data.py` (stdlib section walk; e.g. `python3 -I
refactor/B1172/scripts/pe_data.py /mnt/projects/EverQuestP1999/eqgame.exe f 00640fec` prints 0.2).

**1a. The size word reaches the player object.** FUN_004816d7 copies a Spawn_Struct into the
EQPlayer: `0x4816f6` `mov eax, [edi+0x4b]` / `0x4816fd` `mov [esi+0x23c], eax` -- Spawn_Struct
offset 75, `size` at `include/common/packets/spawn.h:75` -- the race at `0x48180c` `push
[edi+0x11c]` -> `0x481812` `call 0x47a741`, which stores it at `0x47a74a` `mov [ecx+0x260], eax`,
the gender at `0x48182c` / `0x481832` (`[edi+0x14e]` -> `[esi+0x24e]`, offset 334). An
`OP_SpawnAppearance` AT_Size (29) reaches the same field through `0x4524e9` `call 0x481d5d`, which
stores the new size at `0x481d66` `mov [esi+0x23c], eax` and re-runs the rule at `0x481d6c`
`call 0x47fa9e`.

**1b. FUN_0047fa9e, the rule.**

| step | address | what it does |
|---|---|---|
| size test | `0x47fb07` `fld [esi+0x23c]`, `0x47fb0d` `fcomp [0x63f4cc]` (0.0), `0x47fb18` `jp 0x47fb3b` | size > 0: keep it; size <= 0: the default |
| default | `0x47fb2b` `call 0x47a147` (race `[esi+0x260]`, gender `[esi+0x24e]`), `0x47fb30` `fst [esi+0x23c]` | the race/gender default is WRITTEN BACK as the size |
| size -> scale | `0x47fb82` `call 0x47a0e9`, `0x47fb87` `fstp [edx+0xc4]` | actor+0xC4 = the uniform scale |
| heights | `0x47fb99` `fmul [esi+0x238]`, `0x47fbb0` `fmul [eax+0xc4]` | the "ROffset" height and 6.2 (`0x47fb6d`) scaled by it -- not draw-relevant |
| race override | `0x47fbbe` `mov [ebp-0x28], 0xbf800000` (-1.0), `0x47fbff` (the call to FUN_0047a239), `0x47fc2c`..`0x47fc41` `mov [ecx+0xc4], eax` | a forced scale replaces actor+0xC4 when the override wrote one |

**1c. FUN_0047a147, the default size.** Races 1..12 through the jump table at 0x47a209
(`0x47a160` `jmp [eax*4+0x47a209]`; its dwords read with `pe_data.py d`): 1 Human 6.0,
2 Barbarian 7.0, 3 Erudite 6.0, 4 Wood Elf 5.0, 5 High Elf 6.0, **6 Dark Elf 5.0** (`0x47a1a7`
`fld [0x640e58]`), 7 Half Elf 5.5, 8 Dwarf 4.0, 9 Troll 8.0, 10 Ogre 9.0, 11 Halfling 3.5,
12 Gnome 3.0; the compare chain after `0x47a172`: 42 and 120 are 3.0 for gender 2 else 6.0
(`0x47a1e9`), 43 4.7, 77 5.0, 81 3.5, 88 3.0, 90 7.0, 92 8.0, 93 9.0, 94 4.0, 112 5.0, 130 7.0,
330 5.0; every other race 6.0 (`0x47a1e1` `fld [0x64264c]`).

**1d. FUN_0047a0e9, size -> scale.** `0x47a0ef` `call 0x47937c` asks whether the race is 403 or
>= 405 (`0x47937c` `cmp [esp+4], 0x193`, `0x479386` `cmp [esp+4], 0x195`). For every other race
`0x47a136` `fmul [0x640fec]` = **size x 0.2**, and only when the dword 0x923c30 is non-zero
(`0x47a12b`) also `0x47a13e` `fmul [0x647cd0]` (x 0.769). For 403 / >= 405: `0x47a111` `fmul
[0x647cd4]` (1/6) and, when 0x923c30 and the byte 0x923798 are both 0, `0x47a117` `fmul
[0x642d50]` (x 1.3). The dword 0x923c30 is `NewZone_Struct` offset 592: the client copies the
700-byte OP_NewZone body to 0x9239e0 at `0x45c006` and, when that dword changed (`0x45bff5` /
`0x45c10d`), re-runs FUN_0047fa9e on every spawn. It reads 0 in both oracle captures decoded here
(`review/B1172/worker/pcap_sizes.txt:3`, `review/B1172/worker/pcap_sizes.txt:10`). 0x923798 is
only ever stored as 0 / a reset value (`0x4937a6`, `0x4a4d3b`); it matters for races 403 / >= 405.

**1e. FUN_0047a239, the forced scales** (decoded from `0x47a23c` on; the scale is its second
argument, the -1.0 slot): race 72 2.0 (`0x47a29b` `fld [0x6410b4]`), race 108 0.05 (`0x47a2d7`
`fld [0x642ae8]`), races 49, 73, 114, 141, 142, 143, 158, 196, 252, 404 1.0 (`0x47a371` `fld1` ->
`0x47a373` `mov eax, [ebp+0xc]` -> `0x47a376` `fstp [eax]`). Its other cases write heights only.
Races below 26 -- all twelve player races -- return at once (`0x47a258` `jl 0x47a378`).

**1f. Into the renderer.** The actor build FUN_0043c84b hands actor+0xC4 to the graphics actor's
vtable slot 0x50: `0x43d3d8` `fld [eax+0xc4]` -> `0x43d3e8` `call [edx+0x50]` with (scale, 1); the
size change does the same through `0x481d77` / `0x481d8b` `call 0x438ebc`, a thunk to slot 0x50.
In EQGraphicsDX9 slot 0x50 of the 167-entry actor vtables starting at 0x101c95a8 holds 0x10036aa0
(read with `pe_data.py`): it rejects a scale <= 0 (`0x10036aa7` `fcomp [0x101ee6b0]`) and passes
it on through `0x10036bbc` `call [edx+0x1f4]` -> `0x1003b860` `add ecx, 0x64` -> `0x1003b863` `jmp
0x101901f0` -> `0x101901f7` `mov [esi+0x60], eax`: the scene node's uniform scale. B1164 traced
how that scale reaches a bone's world matrix (`refactor/B1164/README.md:289`, sec. 4). **The
camera projection** maps world to pixels (T466's 62.8 degree horizontal FOV, which the harness
fit uses: `tools/model_viewer/model_viewer_fx_harness.cpp:115`); a uniform scale about a figure's
feet multiplies its on-screen height by that factor at a fixed depth, which sec. 2 measures
(238 -> 285 px = x1.197).

**So:** human target, size 6.000 -> 6 x 0.2 = **1.2**; dark-elf caster, size 0.000 and no AT_Size
naming her (`review/B1172/worker/pcap_sizes.txt:5` and `:6` are spawns 177 / 176, she is 258)
-> default 5.0 -> **1.0**. B1164's lane-5 gdb read of the attach scale (MR !323, branch
task/kb-b1164-pin-s) measured the same two numbers on the oracle: 1.200000 and 1.000000.

**fable's path before this card** (copies of the 9136b7a3 files under
`review/B1172/worker/before_src/`): `review/B1172/worker/before_src/include/client/graphics/entity_renderer.h:110`
-- `entityScaleForServerSize`, race scale x serverSize / 6 -- fed at
`review/B1172/worker/before_src/src/client/graphics/entity_mesh_build.cpp:224`,
`review/B1172/worker/before_src/src/client/graphics/entity_build_step.cpp:340`,
`review/B1172/worker/before_src/src/client/graphics/entity_size.cpp:45` and in the harness at
`review/B1172/worker/before_src/tools/model_viewer/model_viewer_spell_test.cpp:253`. The race
scale is `config/race_models.json`'s `scale` column through `getRaceScale` at
`src/client/graphics/eq/race_codes.cpp:178`: Human 1.0, Dark Elf 1.0, Dwarf 0.7, Troll 1.4, ...
-- a table the client does not have (its defaults x 0.2 are 1.2 / 1.0 / 0.8 / 1.6). The harness
caster was size 6.0 at `review/B1172/worker/before_src/tools/model_viewer/model_viewer_fx_harness.cpp:471`,
which is fable's own zone-in placeholder at `src/client/zone/zone_entry.cpp:627`, not a wire value:
every capture decoded here has her at 0.000 -- `review/B1172/worker/pcap_sizes.txt:2`, `:9`, `:16`,
`:22`, two oracle and two fable lane legs. The local player's node draws translation-only at
`src/client/graphics/eq/animated_mesh_scene_node.cpp:271`, so whatever its scale it is drawn at 1.0.

## 2. Like-for-like heights (acceptance B)

Oracle side: B1164's / B1166's difference method (a still with the figure minus a figure-free still
at the same camera, threshold 20, the ring removed, >= 3 px per row). fable side: the harness's
silhouette on its clear colour (B1164's `b1164_silhouette.py`, and B1166's self-check, which counts
rows the same way). Same camera on both sides: the harness camera is fitted to the oracle caster.

**b1130 camera (r51), target at the oracle's measured feet 940,615** -- fable rows from the
b1130 self-check line, before `review/B1172/worker/wrong_model/default_before.log:115`, after
`review/B1172/worker/wrong_model/default_final.log:113`:

| figure | oracle | fable before | fable after |
|---|---|---|---|
| human target | 288 px, rows 328..615 | 238 px, rows 378..615 | **285 px, rows 331..615** |
| dark-elf caster | 209 px, rows 389..598 | 209 px, rows 390..598 (the camera fit) | 209 px, rows 390..598; camera dist 31.650 in both |
| target / caster | **1.378** | 1.139 | **1.364** |
| pose box, units | -- | human 6.706, -3.119..3.587; caster 6.092 | human 8.047, -3.743..4.304; caster 6.092 |

Oracle target `review/B1166/worker/oracle_target/r51_26_box_thr20.txt:2`, oracle caster
`review/B1130/README.md:73`; caster fit `review/B1172/worker/wrong_model/default_before.log:103`
and `review/B1172/worker/wrong_model/default_final.log:103`; pose boxes on the line after each.
The caster is the camera's yardstick, so her own pixel height cannot test her scale; the trace says
1.0 before and after, and the ratio agrees: 1.364 against the oracle's 1.378. B1164's lane-5
co-located read, 268 / 205 = 1.307 (MR !323), is on another camera.

**b1118 camera (B1118 tour), target co-located with the caster** -- frame 10, before any FX, the
union of the two figures, the taller one's head on top (`review/B1172/worker/b1118_silhouette_f10.txt:1`, `:2`):

| | oracle B1118 `27_spawn93_settle - 25_mana253` | fable before | fable after |
|---|---|---|---|
| head-top row | 317 at threshold 40, 315 at 20 | 367 | 311 |
| bottom | clipped by the chat window at row 600 | 607 | 615, stood on the caster's ground |
| height | >= 284 px, a lower bound | 241 px | 305 px |

The oracle row is `review/B1164/worker/dev3/hostpx.txt:2` and B1164's dev4 threshold sweep. Its
feet are hidden, so only the head row compares: 50 rows low before, 4-6 rows high after. Part of
the after excess is the stand: the human's pose-box bottom is put on the caster's and its rendered
feet sit 10 rows below hers -- 615 against 605,
`review/B1172/worker/runs_after/frames/tour_93/run.log:103` and `:106`. B1118's caster is clipped
too (her feet row 605 is below the border), so no b1118 ratio is decidable.

## 3. The fix (ask 3)

- `include/client/graphics/actor_scale.h:30` `clientDefaultActorSize` is 1c; `:59`
  `clientRaceUsesSixthScale` and `:69` `clientSizeToActorScale` are 1d, both flags taken as
  arguments, default 0 = the captures' value; `:81` `clientRaceForcedActorScale` is 1e; `:93`
  `clientActorScale` is 1b.
- `include/client/graphics/entity_renderer.h:108` `entityNodeScale` gives the actor scale to every
  spawn and 1.0 to the local player's node, which is drawn translation-only (FOLLOW-UP 1). Callers:
  `src/client/graphics/entity_mesh_build.cpp:225`, `src/client/graphics/entity_build_step.cpp:340`
  and, for AT_Size, `src/client/graphics/entity_size.cpp:47`. `race_models.json`'s scale column now
  only gates the invisible races (scale 0: 127, 240), as before.
- The live self entity's record carries the wire's "size not said" at
  `src/client/zone/zone_entry.cpp:680`; `player().size()` keeps the 6.0 placeholder because the
  wire-z conversion was measured with it (T331).
- Harness: the caster and the target are scaled by the client's rule at
  `tools/model_viewer/model_viewer_spell_test.cpp:254`; the caster's size is the wire's 0 at
  `tools/model_viewer/model_viewer_fx_harness.cpp:514`; the b1118 target stands on the caster's
  ground from `tools/model_viewer/model_viewer_fx_harness.cpp:853`, B1166's rule for b1130.
- **The ratchet moved** (the orchestrator's note): `tools/model_viewer/fx_target_anchor.h:69`,
  `kStandingPx` 238 -> 285 +-3, provenance in its comment -- the self-check line of
  `review/B1172/worker/wrong_model/default_final.log:113`. Its wrong-body list was re-measured at
  the anchor with `--target`: human female 1:1:6 260 px, dark-elf female 6:1:6 261, the human at
  size 7 332, troll 9:0:6 280, the human at size 5 -- scale 1.0, fable's old drawing -- 238; the
  barbarian 2:0:6 renders 284, a same-height body the ratchet cannot reject (under the client's
  rule every classic race at the same size word is drawn nearly the same height). The self-check
  negative test now uses the size-5 human, because the dark-elf female's solve does not converge on
  gitlab-runner-1 at 1.2 (`review/B1172/worker/wrong_model/r1_6_1_6.log`, "did not solve").
- Tests: `tests/test_actor_scale.cpp:26` covers the two oracle figures, `:35` the classic-race
  table, `:46`, `:62`, `:73` the other defaults, the size word and both flags, `:86` the forced
  scales; `tests/test_entity_size_change.cpp:229` the local-player node scale,
  `tests/test_entity_size_change.cpp:332` the production build drawing the human at 1.2 and the
  dark elf at 1.0 with drawn height = mesh height x that, `tests/test_entity_size_change.cpp:436` a
  ship staying at its forced 2.0 through a resize. Updated: the b1130 frame check at
  `tests/test_fx_harness.cpp:436`, the ratchet tests at `tests/test_fx_harness.cpp:466` and
  `tests/test_fx_harness.cpp:658`, and the held-item facing test, which now compares in model units
  because the NPC is drawn at 1.2 and the player at 1.0, at `tests/test_held_item_attachment.cpp:503`.

## 4. Re-gate: tour-7 + both sweeps, before / after (ask 3, acceptance C)

Runs: `refactor/B1172/scripts/make_jobs.py` -> `review/B1172/worker/jobs.tsv` (B1157's tour rows
and `refactor/B1152/plans/sweep/list.tsv` on `--camera b1118`, caster level 60; B1166's b1130
layout from `review/B1130/record_table.tsv`), `refactor/B1172/scripts/run_all.sh before|after
jobs.tsv 3` -> `run_one.sh` (render on the ThinkPad into `review/B1172/worker/runs_{before,after}/`,
measure here: b1118 with B1139's `harness_measure.py`, b1130 with `refactor/B1155/scripts/harness_measure_b1130.py`,
each from its variant's own tree). 87 runs per side, 0 failed
(`review/B1172/worker/runs_before/sweep.log`, `review/B1172/worker/runs_after/sweep.log`).

**b1130, 40 records** -- `refactor/B1172/tables/b1130_table.tsv` (B1166's comparator,
`refactor/B1172/scripts/compare_b1130.py`, banded by B1155's `band_table.py` against the oracle;
B1157's committed value beside each):

| | result | owner |
|---|---|---|
| fixed_self, 12 rows | frames byte-identical, every cell unchanged | -- (no target; the caster's scale is 1.0 before and after) |
| fixed_npc, 28 rows | every frame differs; landing_cov moves on all 21 rows with a value on both sides, -0.099..+0.493 | B1172: the target drawn at 1.2 |
| landing_cov verdicts | PASS 0 -> 0; r22 n-a -> short (0.153, oracle 4.636), r29 short -> n-a | B1172 target scale; the gap itself is B1164's attach scale (FOLLOW-UP 2) and the sweep's caster level (B1166 FOLLOW-UP 1) |
| landing_end verdicts | PASS 0 -> 1: r22 n-a -> PASS (+4.733, oracle +4.830); r29 short -> n-a | as above |
| p90_land verdicts | PASS 6 -> 6; r22 n-a -> over, r29 short -> n-a | as above |
| source_cov | PASS 0 -> 0; one cell moves, r7 0.211 -> 0.210 | B1173's r7 owner (target placement) |

**b1118, tour-7 + 40 records** -- `refactor/B1172/tables/b1118_table.tsv`
(`refactor/B1172/scripts/compare_b1118.py`; B1157's committed land_cov_n / end_n / src_cov_n /
src_on25_n / src_cen_n beside each):

| | result | owner |
|---|---|---|
| before vs B1157 | equal in every cell but tour 253 and r35 (spell 253): land 5.306 -> 4.148 / 5.297 -> 4.126, src 3.804 -> 3.323 | B1161 (#480), the retired 253 override -- not this card |
| self-target rows, 13 | frames byte-identical | -- |
| other rows, 34 | land_cov x0.955 (r41, 703) .. x1.238 (r13, 209), up on 31; e.g. tour 93 / 38 1.993 -> 2.149 | B1172: the target drawn at 1.2 and stood on the ground |
| landing end | unchanged on 12 of 27 rows with a value, else -0.200 (r7) .. +0.066 (r34); r27 gains an end, +4.167 | B1172 target scale (the landing reads around a larger body) |
| source | src_cov -0.054 .. +0.001 | B1172: the larger target overlaps the caster's Source window |

No gate band is crossed by design here: the b1118 rig is harness-against-harness, and every move is
the target's size, the one variable this card changes.

## 5. The per-target-height FX rows re-measured (B1164 sec. 5, B1166 sec. 5)

Named rows re-rendered with both binaries: `review/B1172/worker/named2_{before,after}/`, plans and
lengths of B1166's `run_named_rows.sh` (B1162's Fear 229 at 10 s, B1164's tour_93 at 13.2 s, both
`--camera b1130`). Measured with the merged `refactor/B1166/scripts/measure_anatomy.py` and
B1164's `b1164_regions.py` window -1.5,0.6,0.16,1.8, B1166's excludes; files under
`review/B1172/worker/named_measure2/`.

**Fear 229 anatomy, anchor 940,615, crop 150,60,990,740:**

| run | target px | run cov | cen_h | h p5..p95 | ground | file |
|---|---|---|---|---|---|---|
| oracle e13, spell 209 (B1166) | 288 | 0.930 | 0.65 | -0.26..1.71 | 0.17 | `review/B1166/worker/named_measure/oracle_e13_a615_288_c740/row.tsv:2` |
| before | 288 | 0.224 | 0.72 | -0.06..1.63 | 0.14 | `review/B1172/worker/named_measure2/fear229_before_a615_288_c740/row.tsv:2` |
| after | 288 | 0.278 | 0.63 | -0.23..1.62 | 0.20 | `review/B1172/worker/named_measure2/fear229_after_a615_288_c740/row.tsv:2` |
| after, own height | 285 | 0.284 | 0.63 | -0.23..1.64 | 0.20 | `review/B1172/worker/named_measure2/fear229_after_a615_285_c740/row.tsv:2` |

**Verdict change**: B1166's FOLLOW-UP 4, the landing reaching less far below the feet than the
oracle, was the target's size: h_p5 -0.06 -> -0.23 against -0.26, ground 0.14 -> 0.20 against
0.17, cen_h 0.72 -> 0.63 against 0.65. The run coverage stays 0.28 against 0.93 (FOLLOW-UP 2).

**Spell 93, window in target heights^2 (peak frame f154):**

| run | anchor | peak | lit px | file |
|---|---|---|---|---|
| oracle r51 (B1166) | 940,615 / 288 | 1.776 (f80) | 147318 | `review/B1166/worker/named_measure/regions_win2_oracleR51_93_a615_288.txt:38` |
| before, b1130 | 940,615 / 288 | 0.974 | 80773 | `review/B1172/worker/named_measure2/regions_win2_t93_b1130_before_a615_288.txt:17` |
| after, b1130 | 940,615 / 288 | 1.035 | 85886 | `review/B1172/worker/named_measure2/regions_win2_t93_b1130_after_a615_288.txt:17` |
| after, b1130, own height | 940,615 / 285 | 1.060 | 86104 | `review/B1172/worker/named_measure2/regions_win2_t93_b1130_after_a615_285.txt:17` |
| before, b1118 (B1164's row, 1.484) | 640,607 / 241 | 1.480 | 85944 | `review/B1172/worker/named_measure2/regions_win2_t93_b1118_before_h241.txt:17` |
| after, b1118 | 640,615 / 305 | 1.059 (f152) | 98471 | `review/B1172/worker/named_measure2/regions_win2_t93_b1118_after_h305.txt:15` |

The lit pixels grow 6 % on b1130 and 15 % on b1118 -- the larger body itself -- while the
landing's own dimensions do not, so per target height^2 the after rows read LOWER (b1118 1.480 ->
1.059). B1164's factorisation (0.60 x 0.82) loses its first factor: the target / caster ratio is
now the oracle's (sec. 2); what remains is the landing's per-target-height area, 0.58x the oracle's
lit pixels in the same window (85886 / 147318). The client multiplies an attached landing by this
same actor scale (B1164 sec. 4: s = the DEFAULT point's world-matrix row, measured 1.200000 on
lane 5); fable passes `kSpellFxAttachScale = 1.0` at
`include/client/spell/spells_new_edd_reader.h:262`. That is FOLLOW-UP 2, not this card's edit.

## 6. Verification

All on gitlab-runner-1 at the merged tip (fable f61fafb4 merged), `RBUILD_HOST=user@gitlab-runner-1 tbuild`:

- targeted ctest `-R 'FxHarness|B1166TargetAnchor|ClientActorScale|EntitySize|EntityRescale|HeldItem|Position|ZoneEntry|SelfRace|SelfDeath|Particle|Options'`:
  309/309 passed (`review/B1172/worker/ctest_final.log`); the real-mesh tests ran with the client
  tree linked as CI links it (`data/eq_client`).
- hygiene: 48/48 (`review/B1172/worker/hygiene_final.log`).
- the full suite before the last two merges: 6495 tests, all passed but the two this card then
  fixed -- the dark-elf wrong-body negative and this README's anchored cites
  (`review/B1172/worker/ctest_full_runner1.log`); the MR pipeline runs the full suite on the tip.
- cite checker: `python3 -I scripts/refactor/cite_check.py refactor/B1172/README.md --tree .
  --extra /mnt/projects/fable --dis .../eqgame.dis --dis .../EQGraphicsDX9.dis` -> `checked 126
  cites, 0 problems, 4 address warnings` (the warnings are 0x101c95a8, a vtable, and 0x101ee6b0, a
  float constant, each cited twice: data words); every cited line was printed and hand-checked.

## RULING REQUEST

None. No golden, manifest, baseline, `config/constrained_presets.json` or third_party file changed;
`config/race_models.json`, an input of the pinned `data/willeq.db`, is untouched. One measured
ratchet moved with its provenance, as the orchestrator asked: B1166's `kStandingPx` 238 -> 285
(sec. 3). The harness caster's size 6.0 -> 0.0 is a wire fact (sec. 1), not a preset value.

## FOLLOW-UP

1. **The local player is drawn at 1.0** (owner: the player render path +
   first-person eye). `EQAnimatedMeshSceneNode::render` draws the local player with a
   translation-only matrix, so a human player is drawn at 1.0 where the client draws 1.2 (a
   dwarf 1.0 where it draws 0.8). Drawing it at the actor scale also moves the first-person eye,
   whose fraction T431 fitted against the oracle on the 1.0 model, and the wire-z offset; it needs
   its own live leg. This card keeps the player's node at 1.0 so its z offset still matches what
   is drawn.
2. **The landing attach scale and ONE scale table** (owner: B1164's MR !323, branch task/kb-b1164-pin-s). The client
   multiplies a target-attached landing by the target's actor scale, 1.2 here. !323's
   `include/client/spell/spell_fx_attach_scale.h` encodes 0.2 x size and a copy of the default
   table for races < 26; when it lands, it should call `clientActorScale` from
   `include/client/graphics/actor_scale.h` (one source of truth; this card's trace also decodes
   FUN_0047a239, which !323 left untraced: it writes no scale for races below 26, so !323's scope
   holds, and it forces 1.0 / 2.0 / 0.05 for the twelve races in sec. 1e).
3. **A b1130 run whose target solve does not converge exits through SIGSEGV** after "did not
   solve" (gitlab-runner-1, `--target 6:1:6`, `review/B1172/worker/wrong_model/r1_6_1_6.log`;
   owner: the B1166 fit's failure path). Not traced.
4. **0x923798** (sec. 1d), the byte that drops the 1.3 for races 403 / >= 405, is not traced past
   its two reset stores; `clientSizeToActorScale` takes it as an argument, 0 by default.
