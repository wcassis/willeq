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
   the capture headings (the ruling, granted 2026-10-27) the final sweep moves land and source
   toward it: 4.341 -> 4.855 / 1.838 -> 1.257 (sec. 7). tour-7 on
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
45 deg off. `turnB1130Rig` (`tools/model_viewer/model_viewer_fx_harness.cpp:756`) turns camera,
caster and target about the caster's vertical axis by
`rigYawDeg = caster heading + DirHeading - 90` (`tools/model_viewer/fx_capture_pose.h:91`; 45 deg
for b1130, 157.5 deg for b1130-self, 202.5 deg for b1118, `kB1130NpcPose` / `kB1130SelfPose` /
`kB1118Pose` at `tools/model_viewer/fx_capture_pose.h:64-70`). A rigid turn moves no pixel the
fit solved (the
frame-0 self-check still reads the target at 238 px, feet 615, column 940.2:
`/mnt/projects/fable/review/B1167/worker/runs/fix3_b1130_l60/run.log`).

**Independent check.** B1166's fit knows only two screen pixels, yet its solved target offset
(x -8.888, z 0.924; `review/B1166/worker/probe/after.log:125`), turned by 45 deg, is
(-6.938, -5.632) -- 5.9 deg and 0.94 units from the capture's own offset (-6.4, -6.4)
(`B1167CapturePose.TheTurnedTargetStandsOnTheCapturesBearing`, `tests/test_fx_harness.cpp:780`;
un-turned it is 50.9 deg off). The camera distance the fit solves on a back-turned caster,
30.28 units (`runs/fix1_b1130_l60/run.log`), also lands on the capture's `Camera1-Distance=30`.

**The actors' own headings are NOT applied** (RULING REQUEST): the oracle's caster has her back
three-quarters to the camera (heading 0, camera at 135 deg), its target stands 22.5 deg off
face-on. On this branch both turn with the rig and keep facing the camera
(`tools/model_viewer/model_viewer_spell_test.cpp:296-303`). The two ways to apply them each break a
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
  (`tools/model_viewer/model_viewer_fx_harness.cpp:672`).
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

Both sweeps ran 87 renders each with 0 failures
(`/mnt/projects/fable/review/B1167/worker/sweeps_{before,after}/sweep.log`, last line). Tables:
`refactor/B1167/tables/b1130_table.tsv` (B1166's `compare_b1130.py`: B1155's band verdicts against
`review/B1130/record_table.tsv`, B1157's column beside), `refactor/B1167/tables/b1118_table.tsv`
(`refactor/B1167/scripts/compare_b1118.py`: B1157's `_n` columns, B1142's lane-5 tour reference) and
the summary below, all regenerated by `bash refactor/B1167/scripts/tables.sh`:

```
# b1130 (level 60): per axis, rows whose after is nearer / farther from the oracle than before (|d| > 0.001)
landing_cov: nearer 18, farther 22, unchanged/n-a 0; PASS 0 -> 0; verdict changes: r16 short->n-a
landing_end_s: nearer 13, farther 12, unchanged/n-a 15; PASS 9 -> 9; verdict changes: r6 PASS->over, r16 short->n-a, r27 short->PASS
p90_land: nearer 21, farther 13, unchanged/n-a 6; PASS 16 -> 17; verdict changes: r7 over->PASS, r14 PASS->over, r16 over->n-a, r22 over->PASS, r42 short->over
source_cov: nearer 20, farther 20, unchanged/n-a 0; PASS 4 -> 4; verdict changes: r4 over->PASS, r5 over->PASS, r23 PASS->over, r26 PASS->over, r34 over->short, r40 short->PASS, r42 PASS->short
frames identical before/after: 0 of 40
# b1118: 39 of 47 rows moved (land > 3 %, end > 0.10 s, src > 3 %); frames identical 0
tour 38: land 1.993 -> 1.940 (lane5 2.650), end +6.633 -> +6.667 (lane5 +4.831), src 0.385 -> 0.398, moved src
tour 41: land 2.478 -> 2.502 (lane5 2.666), end +5.667 -> +5.500 (lane5 +6.253), src 1.129 -> 1.124, moved end
tour 54: land 1.204 -> 1.131 (lane5 1.696), end +6.333 -> +6.333 (lane5 +5.054), src 0.416 -> 0.417, moved land
tour 93: land 1.993 -> 1.940 (lane5 3.049), end +6.633 -> +6.667 (lane5 +4.898), src 0.385 -> 0.398, moved src
tour 253: land 4.148 -> 4.292 (lane5 5.028), end - -> - (lane5 -), src 3.323 -> 3.487, moved land,src
tour 458: land 0.500 -> 0.500 (lane5 1.732), end +5.050 -> +5.050 (lane5 +4.989), src 0.120 -> 0.115, moved src
tour 656: land 1.204 -> 1.131 (lane5 0.957), end +6.333 -> +6.333 (lane5 +4.905), src 0.416 -> 0.417, moved land
```

**Owners.** Every row moves (no frame is identical) and every move is this card's: on b1130 the
two changes together (the attached-emitter frame, sec. 3, and the rig yaw, sec. 2), on b1118 the
frame change alone (the b1118 preset is not turned). Neither axis improves as a whole: on b1130
landing_cov is nearer the oracle on 18 rows and farther on 22, source_cov 20 / 20, p90_land
21 / 13, landing_end 13 / 12; PASS counts move 0 -> 0 (landing_cov), 9 -> 9 (end), 16 -> 17
(p90_land), 4 -> 4 (source_cov). The farther rows' first owner is the actors' facing: the frame is
now the actor's, and the harness actors still face the camera where the oracle's caster turned her
back to it (RULING REQUEST; on r35 that one change takes source_cov 1.842 -> 1.263, sec. 0). On
b1118 its own capture's headings were never matched either (FOLLOW-UP 2). One row loses its
landing run on b1130: r16 (spell 242), 0.720 -> n-a (FOLLOW-UP 4).

**The level.** The `before` column at level 60 against B1157's (each record's class level) is
B1171's (#490) delta, not this card's: e.g. r35 1.791 -> 4.590, r7 0.358 -> 0.959
(`refactor/B1167/tables/b1130_table.tsv`). r35 in the sweep reads 4.590 / 4.341 landing and
1.851 / 1.838 Source before / after, within 0.03 of the r35 matrix runs (sec. 0; the two scripts' renders differ by that much,
not traced).

**tour-7 (b1118)** moves on every row (summary above): 93 / 38 land 1.993 -> 1.940, 54 / 656
1.204 -> 1.131, 41 end +5.667 -> +5.500, 253 land 4.148 -> 4.292 and src 3.323 -> 3.487; 458's
land and end hold. Owner: the frame change (sec. 3), the client's rule; the b1118 tour is not
"unchanged", it is explained.

## 6. Verification

- Targeted ctests on the merged tip (fable f61fafb4 merged; built on gitlab-runner-2: model_viewer
  and the ten affected test targets): `100% tests passed, 0 tests failed out of 99`
  (`/mnt/projects/fable/review/B1167/worker/ctest_merged.log`), including the B1130 harness renders
  (`FxHarness.B1130*`, verbose run `ctest_fxharness_b1130_v.log`: caster 209 px feet 598, target
  238 px feet 615, self-check PASS) and the four new tests.
- Hygiene (`tbuild <wt> hygiene`, runner-2): `100% tests passed, 0 tests failed out of 48`
  (`/mnt/projects/fable/review/B1167/worker/hygiene_final.log`). The two inserted SlotRules tests
  moved B1165's anchored cites in `refactor/B1165/README.md`; re-pointed (DocCitations).
- Cite check: `python3 -I scripts/refactor/cite_check.py refactor/B1167/README.md --tree .
  --extra /mnt/projects/fable --dis .../eqgame.dis --dis .../EQGraphicsDX9.dis` -> the count is in
  the MR note; bare addresses hand-checked against `EQGraphicsDX9.dis` (sec. 3's 0x10060b88..0x10060ca8
  block).
- The MR pipeline's id and status are in the MR description / note.
- Bless-only files: none changed (no golden, manifest, baseline, ratchet or
  `config/constrained_presets.json`); the throwaway instrument worktrees are not part of the branch.

## 7. Continuation: the capture's actor headings (the ruling, MR !328 note 8011, 2026-10-27)

**The ruling.** Yes to the headings: the turned target is re-solved on the turned heading, the
standing-height ratchet re-measured for the new pose (238 -> ~245 px), the camera fitted on the
back-turned caster so B1130's caster box (209 px, feet 598) holds and the distance lands at ~30
(the capture's `Camera1-Distance=30`), and the b1118 preset gets the same treatment from its own
capture (FOLLOW-UP 2) in the same leg. Will's rule: the oracle is reproduced exactly, never
approximated.

**The change.** The capture poses gain the actors' headings -- b1130 caster 0 / target 160,
b1130-self caster 160, b1118 caster 160 / target 160 (`kB1130NpcPose` / `kB1130SelfPose` /
`kB1118Pose`, `tools/model_viewer/fx_capture_pose.h:64-70`; the pcaps of sec. 2 and
`review/B1118/oracle_druid/capture.pcap`) -- and `fxharness::poseForPreset`
(`fx_capture_pose.h:73`) names them per preset. The preset's rig turn (`rigYawDeg`, unchanged:
45 / 157.5 / 202.5 deg) now applies to all three presets (`turnB1130Rig`,
`tools/model_viewer/model_viewer_fx_harness.cpp:756, 817`); each actor is fitted at
`presetRotYDeg(pose, heading)` (`fx_capture_pose.h:100`) in the fitted frame, so after the turn
it sits at exactly `rotYForHeading(its capture heading)` -- b1130: caster rotY 0, target rotY
-112.5 (the caster with her back to the camera, as the capture shows); applied to the actors by
`tools/model_viewer/model_viewer_spell_test.cpp:298-303`. The b1130 target is re-solved on the
turned bearing by the frame-0 silhouette fit (`fitFixedNpcTarget`,
`tools/model_viewer/fx_target_fit.cpp:43`; finite-difference
step 0.05 -> 0.25 units, `fx_target_fit.cpp:139` -- 0.05 moves the silhouette less than a pixel
at ~30 units range): **x -8.685 z 0.272**, post-turn x -6.334 z -5.949
(`/mnt/projects/fable/review/B1167/continuation/settle5/settle5_b1130/run.log`, `tgt fit 2`); the
spell test's distance offset turns with the rig (`model_viewer_fx_harness.cpp:676`) or a
distance-14 target lands 90 deg
the wrong way round.

**The proofs (settle5, caster level 60, `/mnt/projects/fable/review/B1167/continuation/settle5/`).** All three presets settle solid:
- **b1130** (fixed_npc): the B1130 caster box holds at **209 px, feet row 598**, distance
  **30.28** (the capture's 30), the caster turned to heading 0 with her back to the camera; the
  target stands on the capture's bearing 160 at **245 px** standing (rows 370..615), feet at row
  615, column 940 -- B1166's anchor rule unchanged (its run.log self-check line).
- **b1130-self** (fixed_self): the caster is the back-turned actor (heading 160); the box holds;
  the target is unused.
- **b1118**: caster heading 160 (90 deg off the pinned chase camera, az 180 = 256/512), target
  heading 160 at the solved x -6.334 z -5.949.

The turned target's bearing is pinned by
`B1167CapturePose.TheTurnedTargetStandsOnTheCapturesBearing`
(`tests/test_fx_harness.cpp:780`): the pre-turn offset sits 47.0 deg off the facing and the
post-turn offset on the 160 bearing within 3 deg / 0.6 units -- per-probe tolerances, because
the face-on probe of the same test still claims the old > 50 deg off-heading bound.

**The ratchet.** `kStandingPx` 238 -> **245** (`tools/model_viewer/fx_target_anchor.h:86`),
re-measured for the back-turned target at the b1130 solved position (x -8.685 z 0.272): the box
is rows 370..615 = 246 px on frame 0; the solved standing height 246 px is d +0 and the
measured 245 px is d +1 (tol 3): PASS, feet at row 615, column 940
(settle5_b1130 run.log self-check line). B1166's self-check (`B1130SelfCheckRejectsAWrongTargetModel`,
`tests/test_fx_harness.cpp:492`) still passes on the new preset and fails on a wrong body. B1172
#491 (285 face-on at the client's actor scale 1.2) landed on fable after this section was
written; the merge with it re-measured the ratchet for the SAME turned pose at that scale:
**294 px** (sec. 8).

**b1118 (FOLLOW-UP 2, closed).** Decoded from B1118's own capture the same way:
`review/B1118/oracle_druid/capture.pcap` shows the SELF caster heading **160 on 168/168 frames**
and the TrollF spawn at heading 160; the committed fable leg B matches
(`fable_B/capture.pcap`: caster heading 160, TrollF x -74.000 y 428.000 z 3.750 heading 160.000
-- `/mnt/projects/fable/review/B1167/continuation/b1118_poses/`). The preset is turned to it (`kB1118Pose`, rig yaw 202.5 deg). Delta
vs the committed B1118 framing: those PNGs were deleted in that leg's cleanup, so the proof is
against the pcaps; B1178 #497 is re-capturing the frame references.

**Re-gate (before = branch tip eff3de46, after = this tip; level 60; ThinkPad).** Tables in
`refactor/B1167/contintables/` (`refactor/B1167/scripts/tables_cont.sh`). `owners_cont.tsv`
was regenerated in this leg by the script's owners step run against the committed
`contintables/` tables (`python3 -I - refactor/B1167/contintables` over the script's owners
heredoc; its two compare steps' sweep frames stay on the worker host, and the committed
tables are those steps' output). The step's invocation is fixed by this leg: `python3 -I
"$T"` is rejected by python (`can't find '__main__' module in '<dir>'` -- `$T` was taken as
the script file and the heredoc never ran), so the previously committed `owners_cont.tsv`
was not produced by the script (nine mangled header lines; 19 farther rows carried the
nearer-owner text). It is now `python3 -I - "$T"`. The regeneration also drops the named
B1164 residual fragments of the old file: the script's keymap looks up one-word keys in a
frags dict whose keys are the two-word labels of b1171's owner column, so every b1130
farther row reads `unowned -- the row owner` (FOLLOW-UP 5). The re-gate results:
- **r35 at the ruling's numbers**: land 4.341 -> **4.855** (oracle 7.283; the ruling's throwaway
  single-row measurement: 4.359 -> 4.858), source 1.838 -> **1.257** (oracle 1.142; ruling:
  1.842 -> 1.263), p90_land 0.102 -> 0.095 (oracle 0.080; ruling's p90_src 0.106 -> 0.117).
- **r16 (FOLLOW-UP 4, closed)**: the landing run lost in the attached-frame change (nan in
  before) comes back with the turned heading: land **0.811** (oracle 1.343; B1171's face-on
  level-60 0.724). The run was geometry-bound, not word-bound: face-on actors put it outside the
  crop; the residual to the oracle's 1.343 is the landing family (B1164's s + B1172's target
  height, `owners_cont.tsv`).
- b1130 40 records: landing_cov **28 nearer / 5 farther**; p90_land 17/13 (PASS 14 -> 19);
  landing_end_s 2/27 (PASS 9 -> 5); source_cov 12/21 (PASS 2 -> 2). The end-time axis moves
  ~0.07 s earlier on most rows -- the fixed crop window sees a different world slice after the
  turn; each moved row's owner is `owners_cont.tsv` (the farther ones carry the same families
  B1171 already named).
- b1118 47 rows: **46 moved**, 0 frames identical (the turn moves every frame). Tour land vs the
  lane-5 reference: 253 **5.018 vs 5.028** (before 4.292), 41 2.770 vs 2.666, 54 1.219 vs 1.696,
  656 1.219 vs 0.957, 458 0.541 vs 1.732, 38 1.978 vs 2.650, 93 1.978 vs 3.049.

**Verification.** Targeted ctest on the ThinkPad (all 15 `test_fx_harness` tests including the
four live B1130 renders and the new bearing test), hygiene, the cite check and the MR pipeline
-- results in the MR note. Bless-only: `kStandingPx` 238 -> 245 is the ruling's re-measured
ratchet (the RULING below, granted); no other bless-only file changed.

## 8. Post-B1172 merge (2026-10-28): the client's actor scale meets the capture headings

**What landed on fable.** B1172 (#491, MR !325; tip `0f4c40ac7`): the official client's actor
scale -- a spawn draws at `size x 0.2` (`include/client/graphics/actor_scale.h`) -- so the b1130
human target (the capture's own wire word, `race 1 gender 0 size 6`) now draws at **1.2** instead
of 1.0; its pose box stands on the caster's ground (B1172's standY, feet y 0.850 at 1.2); and the
b1130 target solve split out of `model_viewer_fx_harness.cpp` into
`tools/model_viewer/fx_target_fit.{h,cpp}` (a pure move for the 1,500-line ceiling). The dark-elf
caster (size word 0 -> default 5 x 0.2 = **1.0**) is untouched, so the camera fit and the caster
box are untouched.

**The merge** (`origin/fable` merged into tip `2e6143258`; four conflicts, resolved by hand):
- `tools/model_viewer/model_viewer_fx_harness.cpp` -- the solver now lives in
  `fx_target_fit.cpp`: this leg's finite-difference step 0.05 -> 0.25 re-applied there
  (`fx_target_fit.cpp:139`), the spell-check's right vector corrected to the B1167 rig turn
  (`turnXZ(rigYawDeg)` merged with B1172's `targetRightOfCaster()`), and the spell test's
  distance-offset turn merged with B1172's standY block (`model_viewer_fx_harness.cpp:676`).
  Harness 1,358 lines, under the ceiling (the operator's 2026-10-28 01:56 note).
- `tools/model_viewer/fx_target_anchor.h` -- B1172's 285 (face-on, scale 1.2) and this leg's 245
  (back-turned, scale 1.0) describe the same ratchet at different poses/scales; the merged value
  is the RE-MEASURED back-turned box at the client's scale 1.2 (below).
- `tests/test_fx_harness.cpp` -- the ratchet constant and its negatives updated to the merged
  pose (291..297 passes; the B1166/B1172 readings become the negatives; the wrong body is now
  the size-5 human at scale 1.0).
- `refactor/B1165/README.md` -- the two stale line cites re-pointed per line (740 / 330 / 359).

**The ratchet, re-measured (the card: measure, do not scale).** `kStandingPx` 245 -> **294**
(+-3, ratio 1.021 of the oracle's 288), rows 322..615 on frame 0, 294 px solved, feet row 615 at
column 940; the b1130 target re-solved on the turned bearing at x -8.727 z -0.039 (post-turn x
-6.144 z -6.199) and standing at y 0.850 on the caster's ground; the B1130 caster box holds at
209 px / feet 598 / distance 30.135 (setme_b1130 run.log self-check line; caster level 60;
NVIDIA context; `/mnt/projects/fable/review/B1167/continuation/merge/measure/`). The measured 294
agrees with the scale-1.0 back-turned 245 x 1.2 to the pixel (the back-turned premium over
face-on is +9 here, +7 at scale 1.0) -- the value is still the measured one, not a derivation.
B1166's self-check PASSES on the new preset; the wrong body (the size-5 human, scale 1.0)
renders 246 px back-turned (238 face-on) -- d -48 against the 294 ratchet -- and FAILs
(setme_b1130_wrong; `B1130SelfCheckRejectsAWrongTargetModel` in ctest). All three presets settle
on the merged binary (setme_b1130 / setme_b1130self / setme_b1118: caster 209/209/228 px, feet
598/598/605, rig turns 45/157.5/202.5 deg).

**Re-gate** (before = the continuation tip `2e6143258`, scale 1.0 = sweeps_afterH; after = this
merged tip, scale 1.2 = sweeps_m; caster level 60; rendered ON the ThinkPad (NVIDIA context),
measured here; tables `refactor/B1167/contintables/merged/`, `merge/tables_merged.sh`):
- b1130 40 rows: **12 byte-identical** (the fixed_self rows -- the caster is scale 1.0 and its
  pose/heading are untouched by the merge, so its frames are byte-identical: the merge's proof,
  in B1172's own style); the 28 fixed_npc rows all differ in every frame (the target now draws
  at 1.2 on the standing pose box). Axis moves vs the oracle: landing_cov **23 nearer / 4 away**
  (up 23 / down 4), p90_land 13/7 (up 8 / down 12 -- the bigger target dilutes the landing
  peak), landing_end_s 6/5, source_cov 0/0 (the source emitters live on the scale-unchanged
  caster). The rows that moved away are the scale change itself (the target draws 20 % larger);
  their residual is the row owner's (B1164's attach-scale + target-height family) --
  `owners_merged.tsv`.
- r35: source 1.257 (unchanged, the ruling's number -- the source is on the caster); landing
  4.855 -> 4.865, p90_land 0.095 -> 0.092, landing_end 5.283 -> 5.317 -- landing and p90 nearer
  the oracle's 7.283 / 0.080 than before the merge (and better than B1171's face-on
  4.612 / 0.121 / 5.350 / 1.855).
- r16's landing run holds: 0.811 -> 0.812 (oracle 1.343; B1171's face-on 0.724); p90 0.113 ->
  0.109 (nearer the oracle's 0.076).
- b1118 47 rows: **13 byte-identical** (the self-target rows -- the target hides), 29 moved,
  5 with frames differing but every axis inside tolerance (r16 r28 r32 r35 r41). Moved axis
  counts vs the lane-5 / B1157 reference: land 6 nearer / 20 away, end 1/1, src 8 nearer /
  6 away (r1 r3 r5 r7 r30 r33). The moved
  frames differ because the target body changed; the reference for those rows predates this
  pose/scale and is itself what B1178 #497 re-captures -- the row owners stand as in sec. 7.

**Verification.** Targeted ctest 16/16 on the ThinkPad (the 15 of sec. 7 + B1172's new ratchet
test), hygiene, the cite check and the MR pipeline -- results in the MR note. Bless-only:
`kStandingPx` 245 -> 294, the ruling's re-measured ratchet for the merged pose (the same
bless-only file); no other bless-only file changed.

## RULING (granted 2026-10-27, MR !328 note 8011 -- applied in sec. 7)

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
2. **b1118's rig yaw -- CLOSED by sec. 7.** The b1118 preset is now turned to B1118's own
   capture (caster 160 / target 160, chase camera az 180 = 256/512, `kB1118Pose`); proof against
   the pcaps (the committed frames were deleted in that leg's cleanup); B1178 #497 re-captures
   the frame references.
3. **The knock-back.** Whirling Wind pushes the oracle's target 2 units (server y +2, z +2) at
   OP_Action (`/mnt/projects/fable/review/B1167/worker/oracle_poses/e35_target.txt:5`); the harness
   target does not move. Owner: the harness's plan packets (no ClientUpdate replay).
4. **r16 (spell 242) loses its b1130 landing run -- CLOSED by sec. 7.** The attached-frame
   change's lost run (0.720 -> n-a) comes back with the capture's headings: land 0.811
   (oracle 1.343; B1171's face-on level-60 0.724). The run was geometry-bound (face-on actors
   put it outside the crop); the residual is the landing family (B1164's s + B1172's target
   height, `refactor/B1167/contintables/owners_cont.tsv`).
5. **`tables_cont.sh`'s keymap never matches its own regex.** The frags parse
   `(\w+ \w+): ` yields keys like `cov short` / `p90 short` / `src over`, but the keymap
   looks up `cov` / `p90` / `src` -- one word, which the regex can never produce -- so the
   named B1164 residual fragments of the pre-fix `owners_cont.tsv` cannot come from the
   committed script (sec. 7). Owner: whoever re-points b1171's owner column or the keymap;
   the table stays the script's verbatim output until then.
