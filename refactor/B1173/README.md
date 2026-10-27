# B1173 (#492): why r7's Source coverage moves with the target -- it is the Middle stage (the missile), not the Source

Branch `task/kb-b1173-r7-source`, cut at fable 10ffe690, then merged with gitlab/fable 070a9215
(B1169's harness determinism, a fast-forward: the branch had no commits yet). The specification
is `glab issue view 492` (asks 1-2, acceptance A-D). Evidence:
`/mnt/projects/fable/review/B1173/worker/`. Every frame was rendered on the ThinkPad by a
model_viewer built from this branch (at 070a9215) plus a DIAGNOSTIC patch that is NOT committed
(`/mnt/projects/fable/review/B1173/worker/diag_no_middle.patch`: `FX_DIAG_TARGET=x,y,z`
overrides the solved b1130 target point and skips B1166's frame-0 self-check;
`FX_DIAG_NO_MIDDLE=1` makes `launchMissile` create no emitters). Every measurement ran on
dev-u24 with B1155's `harness_measure_b1130.py`, as B1166's sweep did. No game code changes.

## 0. Result

- **The Source does not read the target, on either side.** Fable creates every Source-slot
  emitter with target 0 (`src/client/graphics/spell_visual_fx.cpp:702`, `createSpellEffect`)
  and skips any Source slot anchored on the missile or the target
  (`src/client/graphics/spell_visual_fx.cpp:672`). The client's Source call passes the CASTER
  as both the caster and the target actor (`0x49b97c push esi` / `0x49b97d push esi`, then
  `0x49b982 call 0x49b260`; decompiled `FUN_0049b260(0,local_210,iVar4,iVar4,0,0,...)` in
  `FUN_0049b39d`), so even an anchor-3 Source slot would resolve to the caster
  (`FUN_00495cc3`: anchor 3 reads its param_5, sec. 2).
- **What moves is the Middle stage.** Spell 40's spellsnew.eff record is 88 (spells_us.txt
  field 145 = 88; the run log prints `record=88`). Record 88 has a Middle stage (four slots of
  edd 333, anchors 2 / 1 / 3 / 2, sec. 1), so fable builds a missile at the first live tick
  (`src/client/graphics/spell_visual_fx.cpp:570`), launches it 0.486 s before the hit
  (`src/client/graphics/spell_visual_fx.cpp:594`; the run log: `launch@-0.4857`) and flies
  its two anchor-2 emitters caster -> target (`src/client/graphics/spell_visual_fx.cpp:1209`,
  `src/client/graphics/spell_visual_fx.cpp:1218`, `src/client/graphics/spell_visual_fx.cpp:1231`;
  the lerp `src/client/graphics/sim_particles.cpp:694`). edd 333 is a random ring of radius
  5.5 units (shape 9) with 9 particles at once and 6 per tick at 15 ticks/s, so its particles
  cover the caster's column window while the emitter is still near her.
- **Frame 91 / 92.** The cast begins at frame 45 (t 1.500), the hit (OP_Action) is at frame
  105 (t 3.500), so the launch is t 3.014: the first 60 Hz step after it is t 3.017, drawn
  in frame 91 (t 3.033). Frame 91 is the first frame with Middle-stage pixels in both
  placements (14438 px differ from the run without the Middle,
  `refactor/B1173/tables/r7_frame_compare.txt:6-7`), and left of column 800 they are identical
  for the two placements; frame 92 (t 3.067) is the first frame whose pixels left of column 800 differ
  between the placements (10141 px, line 2-3 of the same file, the same 10141 px B1166
  measured). Frame 91 is placement-independent because on the step the emitter is
  created the target's position is not yet in the worker's position map (the request set is
  built from the live effects at the end of the step,
  `src/client/graphics/sim_particles.cpp:750`), so the target end of the lerp is still the
  emitter's default (0, 0, 0) (`include/client/graphics/environment/unified_particle.h:412`)
  and the emitter sits at the caster, which the harness stands at the world origin
  (`emitter_origin.tsv`: entity 1 at 0, 0, 0); from the
  next step on it follows the real target (`src/client/graphics/sim_particles.cpp:689`).
- **Measured (sec. 3).** Same binary, only the target point changed: with the Middle,
  source_cov 0.231 (B1166's before point) / 0.210 (B1166's after point) -- B1166's row
  reproduced at the new fable tip; without the Middle, 0.142 / 0.142, and the frames left of
  column 800 are byte-identical through the Action (frames 0..105: 0 frames differ). The
  Middle stage is the only channel through which the target reaches r7's Source row.
- **The client** draws the Middle stage only from the missile spawner `FUN_00476b12`
  (`0x476ecc call 0x49b260`), and its anchor-2 emitters ride the missile, whose per-tick
  update re-aims it at the target (B1152's `FUN_00476f34` / `0x47f4af`). So a client missile
  is target-relative too: **where a missile flies, both sides read the target, and a Source
  ROI that overlaps the flight moves with the target. That is the rule; r7 is explained.**
- **But in the oracle's r7 leg no missile flew** (sec. 4): `FUN_00476b12` only runs on the
  missile list `[0x905cd0]`, whose only builder is `FUN_00476f34`, reached only from the
  `0x4033` message arm (`0x460737`, B1163); the r132 capture (the oracle leg of record 7)
  carries 0 rows of opcode 0x4033, and B1130's record table says "projectile: none" for
  spell 40. Fable flies a missile for spell 40 because its rule is "the record has a drawn
  Middle stage" (`src/client/graphics/spell_visual_fx.cpp:571`). That is a deviation from the
  client: fable's record-driven Middle flight for spell 40 is to be removed. Owner: **B1170
  (#489)**, which removes the same flight for record 80 (38 / 93): the oracle drew no Middle
  stage in those legs either (`refactor/B1168/README.md:152-158`, `:193`), so there is no
  untraced drawing path to wait for. Will's rule (2026-10-27): willeq copies the client's
  spell effects exactly. RULING REQUEST: none.

## 1. Spell 40's record (field 145 -> 88)

spells_us.txt row 40 (Strengthen) field 145 = 88. Read with the docs' layout
(`docs/spellsnew_format.md:65` onward; record = 0x10c bytes, stage s at +0x40 + 0x44*s, four
16-byte slots: edd, min level, anchor, attach point; edd entry i at 8 + 0x1a0*i):

| stage | slot | edd | min level | anchor | attach | edd name / texture |
|---|---|---|---|---|---|---|
| Source | 0 | 330 | 0 | 1 caster | 4 LHAND | ResEffectsV2 S1-1 / spelaa.png |
| Source | 1 | 330 | 0 | 1 caster | 5 RHAND | ResEffectsV2 S1-1 / spelaa.png |
| Source | 2 | 332 | 0 | 1 caster | 2 CHEST | ResEffectsV2 S1-3 / flare_blsp50a.png |
| Source | 3 | 331 | 24 | 1 caster | 2 CHEST | (level-gated out at level 1) |
| Middle | 0 | 333 | 0 | 2 missile | 2 | ResEffectsV2 S3-1 / flare_blsp50a.png |
| Middle | 1 | 333 | 0 | 1 caster | 2 CHEST | same |
| Middle | 2 | 333 | 0 | 3 target | 2 | same |
| Middle | 3 | 333 | 0 | 2 missile | 1 | same |
| Target | 2 | 335 | 0 | 3 target | 2 CHEST | ResEffectsV2 S3-3 / flare_blsp50a.png |

(Target slots 0, 1, 3 are level 39 / 24 / 39.) The harness's own `slots.tsv` lists the same
Source 0-2 and Target 2 (`/mnt/projects/fable/review/B1173/worker/r7_diag/frames/tip/slots.tsv`).
None of the Source entries has a target-relative word: 330 is a sphere-surface spawn
(shape 5) with up/radial speeds and gravity, 332 an even ring (shape 1, radius 0.75) with a
radial and swirl speed; +0x74 (follow) is 0 on both. edd 333 (the Middle): duration -5 (a
fixed 5 s), life 2 s, burst 9, 6 per tick at 15 ticks/s, shape 9 (random ring) radius 5.5,
radial speed 6..7, gravity 5, +0x60 orient 1.

## 2. The client, with these words

- **Who plays which stage** (`docs/spellsnew_format.md:55-59`): Source from the begin-cast
  (`0x49b982`), Middle ONLY from the missile spawner `FUN_00476b12`, Target from the arrival
  (`0x47f5ed`) and the removal (`0x477aa3`). The five `call 0x49b260` sites are
  `0x476ecc`, `0x477aa3`, `0x47f5ed`, `0x49b982`, `0x4bff96`.
- **The slot walk** `FUN_00495cc3` (called by `FUN_0049b260` with
  `(*param_2, stage, record, caster, target, position, missile, ...)`): anchor 1 takes its
  param_4 (the caster actor), anchor 2 its param_7 (the missile: position `+0x10..+0x18`,
  followed object `*(+0x48)`), anchor 3 its param_5 (the target actor), anchor 0 its param_6
  (a position); a null one skips the slot (`goto switchD_00495e05_default`). The emitter is
  then created by the particle manager's vtable +0x14 (EQGraphicsDX9 `0x1005e1d0`, per the
  docs).
- **The Source call** passes the caster twice (`0x49b97c`, `0x49b97d`): no Source slot can
  reach the target.
- **The Middle call** (`0x476ecc`; decompiled
  `FUN_0049b260(local_10,local_c,*(desc+0xfe4),0,0,param_1,...)`): caster = `desc+0xfe4`,
  which the spawner fills from the effect's `+0x4c` id (`FUN_00401b28`), target = 0,
  position = 0, missile = the effect itself. So in the client's Middle stage an anchor-2 slot
  rides the missile, an anchor-1 slot sits on the caster, and anchor-3 / anchor-0 slots are
  skipped (target and position are 0). The call is reached only when the effect's `+0x63`
  byte is 9 (a spell missile: `local_c` is set only inside that branch, and the call is
  guarded by `local_c != 0`).
- **The missile's motion**: B1152's TRACE sec. 5 (`refactor/B1152/TRACE.md`):
  `FUN_00476f34` / the arrival loop recompute target minus missile every tick, and the Target
  stage fires on arrival (`0x47f5ed`). An anchor-2 emitter on that missile therefore moves
  toward the target, as fable's lerp does.
- **When a missile exists at all**: the missile list head `[0x905cd0]` is written by the
  constructor (`0x476f6f`, `0x476f8b`), the removal (`0x4778e2`, `0x4778fd`) and a null write
  in a teardown (`0x49765d`); `FUN_00476b12`'s only caller (`0x445556`) walks that list
  (`0x44553f`). B1163 (`refactor/B1163/README.md:35-58`) found the constructor's only call
  at `0x460737`, the `0x4033` arm of the message handler.

## 3. The measurement (ask 1: which emitter, and frame 92)

`refactor/B1173/scripts/run_r7_diag.sh` renders B1166's r7 plan
(`/mnt/projects/fable/review/B1166/worker/b1130_after/plans/r7.plan`: t_begin 1.5, spell 40,
other, cast 2000 ms, level 1; fps 30, sim 60 Hz, seed 1, `--camera b1130`, 17.0 s) five
times with the diagnostic binary; `refactor/B1173/scripts/compare_r7_diag.py` compares the
frames. Rows: `refactor/B1173/tables/r7_rows.tsv` (copied from each run's
`b1130_rows.tsv`); frame comparisons: `refactor/B1173/tables/r7_frame_compare.txt`.

| run | target point (x, y, z) | Middle | source_cov | peak(cast) frame | landing_cov | landing_end |
|---|---|---|---|---|---|---|
| tip (no diagnostic env, self-check PASS) | solved | on | 0.210 | f104 | 0.576 | +6.200 |
| after_mid (B1166 after point) | -8.888, 0.226, 0.924 | on | 0.210 | f104 | 0.576 | +6.200 |
| before_mid (B1166 before point) | -6.702, 0, 9.732 | on | 0.231 | f99 | 0.358 | +4.467 |
| after_nomid | -8.888, 0.226, 0.924 | off | 0.142 | f104 | 0.593 | +6.200 |
| before_nomid | -6.702, 0, 9.732 | off | 0.142 | f104 | 0.213 | +5.500 |

(`refactor/B1173/tables/r7_rows.tsv:2-6`; the peak frames from each run's
`measure_b1130.txt`, e.g. after_mid's line 153 of
`/mnt/projects/fable/review/B1173/worker/r7_diag/measure/after_mid/measure_b1130.txt`.) The two
target points are B1166's solved points
(`/mnt/projects/fable/review/B1166/worker/probe/after.log:125`,
`/mnt/projects/fable/review/B1166/worker/probe/before.log:109`). The tip's frames differ from
after_mid's on every frame by a few target pixels (the override is the 3-decimal print of the
solved point: 912 px at frame 0, `refactor/B1173/tables/r7_frame_compare.txt:1`), and their rows
are identical.

Frame comparisons (`refactor/B1173/tables/r7_frame_compare.txt:2-7`):

- with the Middle, after vs before: left of column 800, 14 of frames 0..105 differ, the first
  at frame 92 (10141 px); 176 of 510 overall;
- without the Middle, after vs before: left of column 800, **0 of frames 0..105 differ**; the
  first difference is frame 118 (Act+0.43), the Target stage, which is anchored on the target
  by the record (anchor 3) on both sides;
- with vs without the Middle, at either point: the first difference is frame 91.

So: the emitter that reads the target is record 88's Middle anchor-2 emitter (edd 333, slots
0 and 3), flown by fable's missile; at frame 91 it starts drawing (placement-independent, sec.
0), at frame 92 it is 2 steps into its 0.302 s flight toward the target and its ring differs
between the placements. The peak of the Source window is in the flight (f99 / f104, flight
t 3.014..3.316 = frames 90.4..99.5, then the ring's 2 s particles), which is why the peak
coverage moves.

Why only the Middle's own pixels change and not the Source's particles: all particles draw
from one generator (`include/client/graphics/simulation_worker.h:997`), but no draw count
depends on an emitter's position except the frustum test
(`src/client/graphics/sim_particles.cpp:386`), and both flights stay in view; the "without
the Middle" pair being byte-identical left of 800 through the Action shows the Source
draws nothing target-dependent.

## 4. The rule, and what fable does that the client did not (ask 2)

Both sides agree on the mechanism: the Source never reads the target; a Middle-stage
missile emitter does (it flies to the target). B1166's r7 source_cov move is therefore not a
Source defect, and B1166's table row is explained by this README (sec. 3 table: 0.231 /
0.210 with the Middle, 0.142 / 0.142 without).

They disagree on whether spell 40 HAS a missile in this scene. In the client a missile
object exists only after a `0x4033` message (sec. 2; B1163), and the oracle's record-7 leg
had none:

```
awk -F'\t' '$4=="0x4033"' /mnt/projects/fable/review/B1130/oracle/r132/all-opcodes.tsv | wc -l   # 0
```

(its cast steps 32/33 carry OP_CastSpell, OP_BeginCast, OP_ClientUpdate, OP_FloatListThing,
OP_HPUpdate, OP_ManaChange, OP_MoveDoor, OP_SpecialMesg only), and the record table says
`projectile = none` for spell 40 (`review/B1130/record_table.tsv`, row 7). The lane's server
(akk-stack zone/spells.cpp, the single-target branch near line 2585) sends the projectile
packet only for target type 1 (`ST_TargetOptional`, "only used for targeted projectile
spells", common/spdat.h line 997) through `Mob::TrySpellProjectile`; spell 40 is target type 5.
Fable draws the Middle whenever the record has a drawn Middle stage and there is a target
(`src/client/graphics/spell_visual_fx.cpp:571`); for spell 40 that adds a target-relative
flight the oracle leg did not show. That flight is a deviation from the client and is to be
removed; owner **B1170 (#489)**, which removes the same flight for record 80 (38 / 93), where
the oracle also drew no Middle stage (`refactor/B1168/README.md:152-158`, `:193`). Removing it
drops r7's source_cov from 0.210 to 0.142 (sec. 3); r7's remaining Source gap (oracle 0.390)
is the Source slots' own.

History (superseded by Will's rule and B1168). The dev's earlier reasoning, kept verbatim:

> Fable draws the Middle whenever the record has a drawn Middle stage and there is a target
> (`src/client/graphics/spell_visual_fx.cpp:571`), so for spell 40 it adds a target-relative
> flight the oracle leg did not show. Removing it measurably drops r7's source_cov from 0.210 to
> 0.142 (sec. 3), further from the oracle's 0.390; r7's Source gap is then the Source slots' own.
>
> Not changed here, because the same rule is what draws B1152's record-80 missile for 38 and
> 93, whose oracle legs also had no `0x4033` row and still showed a flight whose client path is
> untraced (B1163's FOLLOW-UP); gating fable's Middle on a missile message would remove that
> calibrated flight too. That is a ruling, not a fix this card can make by itself.

A second difference, in the other direction, found on the way: when the client's Middle stage
fires, an anchor-1 Middle slot is drawn on the caster (`FUN_00495cc3` anchor 1 = param_4 =
the `desc+0xfe4` caster of the `0x476ecc` call), while fable's missile launch draws only the
anchor-2 slots (`src/client/graphics/spell_visual_fx.cpp:1209`). Record 88's Middle slot 1 is
such a slot. It is not target-relative and does not move with the placement, so it is out of
this card's scope (FOLLOW-UP 2).

## 5. What was not run

- No game code changed, so there is no fix test and no tour-7 re-render; the five r7 runs
  above are the before/after evidence (the tip binary is the new fable tip 070a9215 plus the
  inert diagnostic knobs; its rows equal B1166's after rows).
- Hygiene and CI: on the MR (README, tables and two scripts only).

## RULING REQUEST

None. Will's rule (2026-10-27): willeq copies the client's spell effects exactly; fable's
Middle flight for spell 40 (and for 38 / 93, per B1168) is a deviation, removed by B1170 (#489).

## FOLLOW-UP

1. Removing the Middle flight where the client builds no missile: owned by **B1170 (#489)**
   (with a unit test on `createCastGlow`'s missile rule and a re-render of r7 and tour-7).
2. Fable's `launchMissile` skips anchor-1 Middle slots that the client draws on the caster
   (sec. 4, `src/client/graphics/spell_visual_fx.cpp:1209`); owner **B1170 (#489)**.
