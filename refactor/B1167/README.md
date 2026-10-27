# B1167 (#486): record 104 (Whirling Wind) at b1130 -- the caster level, the attached emitters' frame, and the rig's world yaw

Branch `task/kb-b1167-record104`, cut at fable 10ffe690 (B1166's b1130 target fit), with fable
070a9215 (B1169, harness determinism) and f61fafb4 (B1173, B1165) merged in (merges, no
conflicts but one include line, `tools/model_viewer/model_viewer_fx_harness.h`). The
specification is `glab issue view 486` (asks 1-3, acceptance A-F). Evidence:
`/mnt/projects/fable/review/B1167/worker/`. Every frame was rendered on the ThinkPad and
measured on dev-u24; compiles and ctests ran on gitlab-runner-2 (model_viewer for the renders on
the ThinkPad). **Every willeq run in this README casts at caster level 60** (the B1130 captures'
`#level 60`: `review/B1130/oracle/e35/steps.tsv`, step `09_lvl`) unless a row says level 5.

Note on the issue text: its "280/282/9/281 words" are record 80's Target emitters (spell 93,
B1164). Record 104 has **three** Target emitters, 432/433/434 (slot 3 is empty), and **four**
Source emitters, 428/429/430/431 (`python3 -I refactor/B1150/scripts/eff_record.py 104`). Their
every drawing word: `python3 -I refactor/B1167/scripts/edd_words.py 428 429 430 431 432 433 434`
(`/mnt/projects/fable/review/B1167/worker/edd_words_r104.txt`).

## 0. Result

| r35 on b1130 (spell 253, record 104) | source_cov | landing_cov | landing_end_s | p90_src |
|---|---|---|---|---|
| oracle (B1130 e35; `refactor/B1157/b1130_table.tsv.after.tsv:35`) | 1.142 | 7.283 | 6.950 | 0.158 |
| B1161 after (level 5, old anchor) | 0.891 | 1.207 | +5.217 | 0.111 |
| fable, **level 5**, fixed anchor (`mx_before_l5`) | 0.892 | 1.215 | +5.317 | 0.171 |
| fable, **level 60** (`mx_before_all`) | 1.855 | 4.612 | +5.350 | 0.335 |
| **this branch, level 60** (`mx_after_all`) | 1.842 | 4.359 | +5.383 | 0.106 |
| this branch + the capture headings (RULING REQUEST, `mx_afterA_all`) | 1.263 | 4.858 | +5.317 | 0.117 |

(`/mnt/projects/fable/review/B1167/worker/runs/<run>/measure/b1130_rows.tsv`, B1155's
`harness_measure_b1130.py` at `kB1130TargetAnchor`; `before` = fable 070a9215 + an env-gated
slot mask, `after` = this branch + the same mask -- with the mask unset both are frame-identical
to their plain binaries in every number: `mx_pristine_before_all` = `mx_before_all`,
`mx_branch_all` = `mx_after_all`, `runs/window_table.tsv`.)

1. **The first owner of B1161's 1.207 is the caster level** (sec. 1). B1161 (and the B1157 /
   B1166 b1130 sweeps) cast record 35 at the record table's DRU level 5; the oracle cast at 60.
   At level 5 the client's level gate draws ONE of the three Target emitters (433, minLevel 0)
   and ONE of the four Source emitters (431); at 60 it draws all seven. Level 60 alone moves the
   landing 1.215 -> 4.612 (oracle 7.283) and the Source 0.892 -> 1.855 (oracle 1.142). Owner of
   the sweep's level: B1171 (#490), already fixing it.
2. **The mis-applied word: an attached emitter's frame** (sec. 3). EQGraphicsDX9's Render builds
   every ATTACHED emitter's axes from its actor (`+0x60 == 1` on a model point: the point's world
   matrix; otherwise the actor's matrix). willeq created every record slot with
   `useDynamicDir = false`, so the sim never asked for the actor's direction and drew every record
   emitter in the WORLD frame (axis 2 = world +X) whatever the actor faced. Fixed in the client
   (live renderer and harness alike); unit tests `SlotRules.AnAttachedSlotOrientsToItsActor`,
   `SlotRulesSim.AnAttachedRecordEmitterSpawnsInItsActorsFrame`.
3. **The rig's world yaw** (sec. 2). Record 104's Target emitters carry the one world-fixed word,
   `+0x12C` world +X drift (20-25 u/s). The b1130 rig was built in an arbitrary world frame (world
   +X ran straight to screen-left); the capture's camera sat 45 deg round from that. The rig is now
   turned to the capture's yaw (`Camera1-DirHeading=192` + the caster's capture heading), proven by
   the target landing on the capture's bearing (5.9 deg) from B1166's screen-only fit.
4. **What is left** (sec. 4): the landing's TARGET COLUMN. In B1164's window round the target
   the oracle lights 0.984 target-heights^2 inside |dx| < 0.5; willeq's three Target emitters
   together light 0.118 there (after) -- their particles leave the column at the file's speeds
   (433/434: +-15..25 u/s along axis 3, drift 20-25 u/s), times the attach scale s that willeq
   passes as 1 (B1164 sec. 4). Not attributable to a word from evidence here: FOLLOW-UP 1.
5. **Acceptance C**: on this branch p90_src moves toward the oracle (0.335 -> 0.106; |d| 0.177 ->
   0.052), source_cov does not move (1.855 -> 1.842), landing_cov moves away (4.612 -> 4.359). With
   the capture headings (the ruling) all three move toward it: 1.263 / 4.858 / 0.117. tour-7 on
   b1118 moves (the orient fix is the client's, b1118 included): sec. 5.

## 1. Re-measured at the fixed anchor: level 5 vs level 60

Renders: `refactor/B1167/scripts/run_r35.sh <slug> <tag> b1130 <level>` (B1157/B1166's plan
layout: t_begin 1.5 s, cast 1750 ms, target other at distance 0, 30 fps, sim 60 Hz, seed 1), on
fable 070a9215. The harness logs the slots it drew
(`/mnt/projects/fable/review/B1167/worker/runs/base2_b1130_l5/run.log`, `.../base2_b1130_l60/run.log`):

- level 5: `createCastGlow ... level=5 row=104 slots 3:2@1+0.41..7.60 (of 4)` and
  `createLanding ... slots 1:3/2@2+0.33..8.33 (of 3)` -- Source 431 and Target 433 only;
- level 60: `slots 0:... 1:... 2:... 3:... (of 4)` and `slots 0:... 1:... 2:... (of 3)`.

The gate is the client's (`docs/spellsnew_format.md:73`: "drawn iff value <= caster level"); the
min levels are 39/24/24/0 (Source) and 24/0/39 (Target). The B1130 capture of r35 (e35) ran
`#level 60` before the cast (`review/B1130/oracle/e35/steps.tsv`, `09_lvl`), so a level-5 harness
row compares two Source and two Target emitters fewer than the oracle drew. Every number below is
level 60.

## 2. The rig's world frame (the capture's facts)

Decoded from the e35 capture by `refactor/B1167/scripts/oracle_poses.py` (outputs
`/mnt/projects/fable/review/B1167/worker/oracle_poses/e35_{target,caster}.txt`):

- the target `TrollF000` (id 259) spawned at server (-74, 428), heading 160 (512ths), and keeps
  that heading (`/mnt/projects/fable/review/B1167/worker/oracle_poses/e35_target.txt:1`); at the cast's OP_Action it is pushed to (-74, 430), z +2
  (`/mnt/projects/fable/review/B1167/worker/oracle_poses/e35_target.txt:5`: Whirling Wind's knock-back -- FOLLOW-UP 3);
- the caster, after `#goto -67.6 434.4`, sends heading 0 in every OP_ClientUpdate to the end of
  the capture (`e35_caster.txt`: 132 of its 222 updates, all after the goto; before it 160);
- `Camera1-DirHeading=192.000000` in `eqclient-before.ini` of all 47 B1130 captures (F9 x3 =
  user camera 1, `review/B1130/README.md` "Plans"); a fixed_self leg has the caster at (-74, 428)
  heading 160 (e4).

The headings are decoded by the client's own rule (raw 12-bit field / 4 = 512ths:
`src/client/entities/entities_component.cpp:174-175`), positions are swapped server x/y -> zone
(`src/client/entities/entities_component.cpp:156-158`), and an actor is drawn at node rotation
Y = -heading deg (`src/client/graphics/entity_mesh_build.cpp:343-350`).

The b1130 rig before B1167: caster at the origin facing +Z, the camera fitted on +Z
(`kHarnessFaceOnRotY`), the target solved at the anchor (B1166) -- world +X ran exactly to
screen-left. The client's camera sits at DirHeading from the player's heading, so in the
capture's world the camera is at 0 + 135 = 135 deg from the caster, i.e. the fitted rig is
45 deg off. `turnB1130Rig` (`tools/model_viewer/model_viewer_fx_harness.cpp:904`) turns camera,
caster and target about the caster's vertical axis by
`rigYawDeg = caster heading + DirHeading - 90` (`tools/model_viewer/fx_capture_pose.h:67`; 45 deg
for b1130, 157.5 deg for b1130-self, `kB1130NpcPose` / `kB1130SelfPose` at
`tools/model_viewer/fx_capture_pose.h:53-54`). A rigid turn moves no pixel the fit solved (the
frame-0 self-check still reads the target at 238 px, feet 615, column 940.2:
`/mnt/projects/fable/review/B1167/worker/runs/fix3_b1130_l60/run.log`).

**Independent check.** B1166's fit knows only two screen pixels, yet its solved target offset
(x -8.888, z 0.924; `review/B1166/worker/probe/after.log:125`), turned by 45 deg, is
(-6.938, -5.632) -- 5.9 deg and 0.94 units from the capture's own offset (-6.4, -6.4)
(`B1167CapturePose.TheTurnedTargetStandsOnTheCapturesBearing`, `tests/test_fx_harness.cpp:729`;
un-turned it is 50.9 deg off). The camera distance the fit solves on a back-turned caster,
30.28 units (`runs/fix1_b1130_l60/run.log`), also lands on the capture's `Camera1-Distance=30`.

**The actors' own headings are NOT applied** (RULING REQUEST): the oracle's caster has her back
three-quarters to the camera (heading 0, camera at 135 deg), its target stands 22.5 deg off
face-on. On this branch both turn with the rig and keep facing the camera
(`tools/model_viewer/model_viewer_spell_test.cpp:293-294`). The two ways to apply them each break a
standing check:

| variant | caster frame 0 | target frame 0 | blocked by |
|---|---|---|---|
| fit on the face-on caster, then turn her to heading 0 (`fix4`) | 200 px, feet 591 | 239 px, feet 616 | B1130's caster box 209 / 598 (`tests/test_fx_harness.cpp`, `checkB1130Frame`) |
| fit on the back-turned caster (`fix1`, `mx_afterA_*`) | 209 px, feet 598 | 245 px, feet 615 | B1166's standing-height ratchet 238 +-3 (`tools/model_viewer/fx_target_anchor.h`, bless-only) |

(`runs/fix4_b1130_l60/run.log`, `runs/fix1_b1130_l60/run.log`; the caster box of fix4 measured on
its frame 0.) The second keeps every B1130 fact and is the one the numbers favour (sec. 0); its
runs used a throwaway instrument build with the ratchet at 245 (`wt/kb-b1167-instr`, never
merged).

## 3. The mis-applied word: an attached emitter takes its actor's frame

**The client.** CParticleSystem::Render (`0x100605b0`, `EQGraphicsDX9.dll.c` lines 68462-68541):
at `0x10060b88` it tests `+0x60`; with `+0x60 == 1` and an attach point (`0x10060b9b`, instance
+0x6C) the emitter's three axes are the point's world-matrix rows -- axis 1 = point +0xC0 (row 2,
`0x10060ba6`), axis 2 = point +0xA0 (row 0, `0x10060bc4`), axis 3 = -(point +0xB0) (row 1 negated,
`0x10060be1`) -- each scaled by 1/s19c (`0x10060c11`). Otherwise an emitter attached to an actor
(instance +0x68) takes the actor's own matrix from vtable +0x234 (`0x10060c9f`, `0x10060ca8`,
same rows). Only an unattached emitter keeps its fixed frame. Row 0 of a model's matrix is its rest
facing (+X for every race model: `kHarnessFaceOnRotY`'s note); for a standing actor row 2 is up.

**willeq before.** The sim builds the record emitter's frame from `dynamicDir` -- axis 1 world up,
axis 2 the direction made horizontal, axis 3 their cross
(`src/client/graphics/sim_particles.cpp:1003-1010`, and per frame for `+0x60` / `+0x74` at
`src/client/graphics/sim_particles.cpp:914-924`) -- but only when the emitter was created with
`useDynamicDirection` (`src/client/graphics/sim_particles.cpp:395`). Every record slot was created
with it false (fable f61fafb4, `createCastGlow`'s and `createLandingFromSlots`'s
`createSpellEffect` calls), so the frame was always the world's (axis 2 = +X), and the direction
callbacks answered only hand ids. On a face-on harness caster that is a 90 deg error; in the live
client it follows the world, not the actor.

**The fix.**
- `src/client/graphics/spell_visual_fx.cpp:727` / `src/client/graphics/spell_visual_fx.cpp:1061`:
  every record slot that is not anchored to the world (anchor 0) is created with
  `useDynamicDir = true`;
- `resolveSpellFxAttachDirection` (`include/client/graphics/spell_fx_bone_anchor.h:64`): the
  direction an attached emitter orients to -- a hand id its bone's direction (as before), every
  other id the node's +X in world space (row 0); used by the live renderer
  (`src/client/graphics/renderer/renderer_entities.cpp:357`) and the harness
  (`tools/model_viewer/model_viewer_fx_harness.cpp:663`).
- Tests: `SlotRules.AnAttachedSlotOrientsToItsActor` (`tests/test_spell_slot_rules.cpp:310`, record
  80 at level 60: all Source and all 8 landing commands request the direction) and
  `SlotRulesSim.AnAttachedRecordEmitterSpawnsInItsActorsFrame` (`tests/test_spell_slot_rules.cpp:374`:
  a +0xBC offset of 5 lands at (0, 0, 5) for an actor facing +Z, (5, 0, 0) facing +X, and -- the
  pre-B1167 command -- at x 5 whatever the facing).

Known approximation (unchanged here): the sim makes axis 2 horizontal and axis 1 world up; the
client uses the point's full matrix, which differs for a tilted bone (a hand point, or an
animated chest). Record 104's slots are all CHEST (falls back to DEFAULT,
`docs/spellsnew_format.md:75`).

## 4. Per-emitter attribution (ask 1)

`refactor/B1167/scripts/run_matrix.sh`: each run keeps only the named slots
(`WILLEQ_FX_SLOTMASK_0` / `_2`, an env-gated instrument in the throwaway worktrees only), level 60,
b1130. Gate axes (`b1130_rows.tsv`) and B1164's window / extent at the fixed anchor
(`refactor/B1167/scripts/window_table.py`: max-RGB rise > 80 over a tail median, the gate crop,
the e35 geometry's four exclusion boxes on every run; `win` = B1164's window -1.5..0.6 x
0.16..1.8 target heights round the 940,615 / 288 px target; `col` = its |dx| < 0.5 share;
`runs/window_table.tsv`). The oracle in the same instrument, e35 frames 7348..7521 (Act+0.03 ..
+6.95), tail 7610..7650: `/mnt/projects/fable/review/B1167/worker/window_oracle_e35.tsv`.

| run | source_cov | landing_cov | peak (Act+) | crop cov | win | col | dx p5..p95 | up p5..p95 |
|---|---|---|---|---|---|---|---|---|
| **oracle e35** | 1.142 | 7.283 | 2.39 | 6.632 | 2.084 | 0.984 | -2.38..+0.25 | +0.17..+1.68 |
| before, all | 1.855 | 4.612 | 1.65 | 4.161 | 1.050 | 0.309 | -2.41..-0.05 | +0.14..+1.66 |
| after, all | 1.842 | 4.359 | 1.02 | 4.002 | 1.389 | 0.425 | -2.40..+0.10 | +0.24..+1.66 |
| after + headings | 1.263 | 4.858 | 1.48 | 4.403 | 1.322 | 0.538 | -2.36..+0.00 | +0.15..+1.67 |
| before, Source only | 1.855 | 4.012 | 0.68 | 3.659 | 0.955 | 0.309 | -2.39..+0.04 | +0.14..+1.69 |
| after, Source only | 1.842 | 4.020 | 0.08 | 3.625 | 1.169 | 0.381 | -2.41..+0.22 | +0.26..+1.76 |
| after + headings, Source only | 1.263 | - | 0.02 | 4.279 | 1.072 | 0.593 | -2.42..+0.04 | +0.12..+1.75 |
| before / after, S428 | 0.378 / 0.855 | 1.337 / 0.885 | | 1.260 / 0.753 | 0.382 / 0.376 | 0.208 / 0.000 | | |
| before / after, S429 | 0.319 / 0.725 | - / - | | 0.815 / 0.644 | 0.243 / 0.321 | 0.117 / 0.000 | | |
| before / after, S430 | 1.087 / 0.469 | - / - | | 1.166 / 1.511 | 0.295 / 0.339 | 0.000 / 0.155 | | |
| before / after, S431 | 0.892 / 0.253 | 1.102 / - | | 1.045 / 1.207 | 0.375 / 0.294 | 0.000 / 0.220 | | |
| before, Target only | 0.065 | 1.241 | 1.92 | 1.170 | 0.337 | 0.128 | -2.42..+0.16 | +0.14..+1.27 |
| after, Target only | 0.065 | 0.935 | 2.88 | 0.908 | 0.360 | 0.118 | -2.12..+0.21 | +0.24..+1.31 |
| after + headings, Target only | 0.068 | 0.979 | 2.85 | 0.944 | 0.364 | 0.145 | -2.16..+0.18 | +0.22..+1.34 |
| before / after, T432 | - | 1.098 / 0.825 | | 1.027 / 0.794 | 0.295 / 0.322 | 0.085 / 0.103 | | |
| before / after, T433 | - | 0.134 / 0.076 | | 0.125 / 0.071 | 0.031 / 0.022 | 0.019 / 0.007 | | |
| before / after, T434 | - | - / - | | 0.058 / 0.059 | 0.023 / 0.029 | 0.005 / 0.022 | | |

(`-` = the gate found no landing run; with no Source and no Target, `S0_T0`, the caster alone
reads source_cov 0.065 and crop cov 0.002.) Per emitter, word by word:

- **The landing is mostly the Source.** The Source alone is 4.012 / 4.020 of the 4.612 / 4.359
  landing_cov (87 / 92 %): 428-431 are held to the caster's duration (+0.41..+6.85..7.60 s after
  BeginCast, the log above) and their rings (shape 9, r 8 / 8 / 4x9 / 11, radial 3.25..7 u/s)
  cross the landing crop. The oracle's landing therefore also carries its Source, and the
  Source's frame decides most of the gate's landing number.
- **Shape / spread (428-431, `+0xA8..+0xC8`).** The orient fix turns the rings' tilt axes from
  world +X to the caster's facing: 428/429 (tilt B +-65) move their light OUT of the target column
  (col 0.208 / 0.117 -> 0.000) and 430/431 (tilt A 81 / 80) INTO it (0.000 -> 0.155 / 0.220); the
  Source window total rises 0.955 -> 1.169 toward the oracle's 2.084. Ruled IN as a cause (the
  frame), with the face-on harness caster still 135 deg off the oracle's (sec. 2).
- **World drift (`+0x12C`, 432-434: 25 / 20 / 20 u/s; 430: 5).** Ruled IN for direction: with
  the rig turned, the Target field recedes up-left into the scene as on the oracle's stills,
  where before it ran flat to screen-left (frames `runs/yaw45_S0_Tall/frames/frame_00169.png` vs
  `runs/m_S0_Tall/frames/frame_00169.png`, and the oracle's
  `/mnt/projects/fable/review/B1167/worker/look/oracle_e35_f7360.png`). It does not raise the
  Target's coverage (Target only 1.241 -> 0.935 / 0.979).
- **Velocity (`+0xEC..+0x124`) and the attach scale (`+0x6C` / `+0x19C` = 1 on all seven).** The
  oracle's target COLUMN is 0.984 target-heights^2; willeq's Target emitters together light
  0.118..0.145 there. 433 / 434 leave at +-15..25 u/s along axis 3 (life 3 s) and 432 at radial
  1..2 u/s with drift 25 u/s: at s = 1 every Target particle is out of the column within ~0.3 s.
  Every one of those speeds is x s in the client (`0x10062705`, B1164 sec. 4), and willeq passes
  s = 1 (`kSpellFxAttachScale`). s is the length of row 0 of the target's attach-point matrix --
  model data at runtime, not traced (B1164 could not pin it either). Neither ruled in nor out:
  FOLLOW-UP 1.
- **Size curve (`+0xCC..+0x194`).** One value per emitter (1.25 / 1 / 1 / 0.875, 1.5 / 1 / 1;
  +0x18C = 1, +0x16C = 1); 432's shrink-out 3 s. Applied as written since B1149; nothing here
  moves with them. Ruled out as the shortfall: p90_src after (0.106) is BELOW the oracle's 0.158,
  so the sprites are not too small on screen in the Source ROI either.
- **Population.** All seven at the file's counts (B1161 sec. 2, pool 8192 in the harness:
  `spellParticlePool 8192 (desktop)` in every run.log). Ruled out.

## 5. Before / after gates (ask 2): tour-7 + both sweeps

`refactor/B1167/scripts/run_sweeps.sh before|after` (before = fable f61fafb4, after = this branch;
layout in the script header; b1130 rows at caster level 60 EXPLICITLY, B1157/B1166 ran them at the
record's level). Tables: `refactor/B1167/tables/`.

SWEEP_SECTION

## 6. Verification

VERIFICATION_SECTION

## RULING REQUEST

Apply the B1130 capture's actor headings in the b1130 presets (caster 0 / target 160 on
b1130, caster 160 on b1130-self; `tools/model_viewer/fx_capture_pose.h`) by fitting the camera on
the back-turned caster -- which keeps B1130's caster box (209 px, feet 598) and brings the camera to
30.28 units (the capture's `Camera1-Distance=30`) -- and re-measure B1166's standing-height ratchet
`kStandingPx` from 238 to the 245 px the human target then renders (bless-only; sec. 2). Measured
effect on r35: source 1.842 -> 1.263, landing 4.359 -> 4.858, p90_src 0.106 -> 0.117 (oracle
1.142 / 7.283 / 0.158; sec. 0).

## FOLLOW-UP

1. **The Target column (attach scale s of the TARGET's point).** Oracle 0.984 target-heights^2 in
   |dx| < 0.5 vs willeq 0.118 (sec. 4); the Target emitters' speeds are x s in the client and
   willeq passes s = 1. Owner: B1164's attach-scale line (#483 FOLLOW-UP) -- the target model's
   point matrix needs a runtime read (lane 5) or the model-data trace of `0x10039480`'s input.
2. **b1118's rig yaw.** The b1118 preset is not turned (its capture, B1118's, has its own headings
   and camera); record 104's drift and every actor-oriented emitter there still sit in the
   pre-B1167 world frame relative to that camera. Owner: the b1118 preset (B1139 layout); same
   method as sec. 2 on `review/B1118/oracle_druid/capture.pcap`.
3. **The knock-back.** Whirling Wind pushes the oracle's target 2 units (server y +2, z +2) at
   OP_Action (`/mnt/projects/fable/review/B1167/worker/oracle_poses/e35_target.txt:5`); the harness
   target does not move. Owner: the harness's plan packets (no ClientUpdate replay).
