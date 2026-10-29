# B1181 (#500): item missiles — the +0x65 IT model on a type != 9 `0x4033` object

Issue #500. Branch `task/kb-b1181-item-missile`, cut from the B1170 tip and merged
with `fable` at `32c67ab3c` (B1170 MR !330 + B1178/B1179) before any work landed.

On a `0x4033` object (B1168's facts: position +0x10/+0x14/+0x18, velocity
+0x1c/+0x20/+0x24, heading +0x2c, aim +0x30/+0x3c, type +0x63, model name +0x65,
lifetime +0x68, +0x62 the damped flag) the official client draws the `+0x65` IT
model for every object whose type byte is **not 9** (type 9 = spell missile,
B1170's stages, untouched here), flies it, and destroys the model when the
object is removed. fable built the object but drew nothing.

All citations below are `review/B1141/ghidra/out/` (eqgame.exe = `eqgame.dis` /
`eqgame.exe.c`; EQGraphicsDX9.dll = `EQGraphicsDX9.dll.c`), addresses as printed
there.

## 1. The client rule, cited line by line

### 1.1 Load — spawn tail `FUN_00476b12` (eqgame.dis)

The type fork:

```
00476b71  call 0x46d655            ; allocator: per-object block base
00476b76  mov ecx, dword ptr [esi+0x48]
00476b79  add eax, 0x1d4c0         ; the end-time float lives at base+0x1d4c0
00476b7e  mov dword ptr [ecx+0x68], eax   ; +0x68 = &end-time (all objects)
00476b81  cmp byte ptr [esi+0x63], 9      ; the type fork
00476b85  jne 0x476d77            ; type != 9 -> the item-missile path below
```

The type-9 (spell) branch then overwrites +0x68 with the spell model block
(`00476b8b-00476be3`, through `call 0x44d0d0` on the +0x5c index) — that is
B1170's territory and is byte-identical in fable (C3/C4 there, and the
frame-identity run in (B) below).

The item-missile path integrates the velocity, computes the scale, and creates
the actor:

```
00476d77  fld  [esi+0x1c]  / fadd [esi+0x10]  / fstp [esi+0x10]   ; y += vy
00476d80  fld  [esi+0x20]  / fadd [esi+0x14]  / fstp [esi+0x14]   ; z += vz
00476d89  fld  [esi+0x24]  / fadd [esi+0x18]  / fstp [esi+0x18]   ; x += vx
00476d92  cmp byte ptr [esi+0x62], 0        ; the +0x62 flag
00476d96  fld1                        ; scale = 1.0 (the default, all paths)
00476d98  mov eax, [esi+0x48]
00476d9b  mov eax, [eax+0xfe4]        ; the zone/scene pointer
00476da1  jne 0x476dce
00476da3  cmp eax, ebx
00476da5  fstp                        ; zone == NULL:
00476da7  fld [0x6410d0]              ;   scale = 1.5 (rdata, f32 1.5)
00476dad  je 0x476deb
00476daf  mov eax, [eax+0x260]        ; the zone model name
00476db5  sub eax, 0x31               ; '1'
00476db8  je 0x476dc4
00476dba  sub eax, 0x6d               ; 'm'
00476dbd  je 0x476dc4
00476dbf  sub eax, 0x26               ; '#'
00476dc2  jne 0x476deb                ; no match -> keep 1.0
00476dc4  fstp
00476dc6  fld [0x647570]              ; name in {1,m,#} and +0x62==0: scale = 20.5
00476dcc  jmp 0x476deb
00476dce  cmp eax, ebx                ; +0x62 != 0:
00476dd0  je 0x476deb                 ;   zone NULL -> keep 1.0
00476dd2  mov eax, [eax+0x260]
00476dd8  sub eax, 0x31  / 00476ddb je 0x476de7   ; '1' -> 1.0
00476ddd  sub eax, 0x6d  / 00476de0 je 0x476de7   ; 'm' -> 1.0
00476de2  sub eax, 0x26  / 00476de5 jne 0x476deb  ; '#' -> 1.0 / else 1.0
00476de7  fstp
00476de9  fld1
```

So the scale the actor is made with is **1.0 for every zone whose model name
does not start with 1/m/#** (all of them fable ships, including qeynos2),
1.5 when the zone pointer is NULL, and 20.5 only for +0x62==0 in a zone whose
name starts with 1/m/#. fable draws at the loader's own scale (== 1.0 in the
paths the server can reach).

The actor creation call — arguments (x86 cdecl, last push = first arg):

```
00476deb  mov eax, [esi+0x30]          ; aim (+0x30)
00476df5  mov [ebp-0x10], eax
00476df8  mov eax, [esi+0x2c]          ; heading (+0x2c)
00476e31  lea eax, [esi+0x65]          ; the model name at +0x65
00476e00  fstp [esp+0x14]              ; ... scale (computed above)
00476e27  fstp [esp+8]                 ; z (+0x18)
00476e1a  fstp [esp+0xc]               ; heading
00476e0d  fstp [esp+0x10]              ; aim
00476e34  fstp [esp+4]                 ; y (+0x14)
00476e3b  fstp [esp]                   ; x (+0x10)
00476e3e  push eax                     ; name@+0x65
00476e3f  call 0x43972a                ; FUN_0043972a(name, x, y, z, heading, aim, scale)
00476e44  mov ecx, [esi+0x48]
00476e47  mov [ecx], eax               ; object->actor = created actor
00476e4c  cmp [eax], ebx
00476e4e  je 0x476b65                  ; no actor -> skip (nothing drawn)
00476e5b  call [edx+0xc]               ; virtual: actor attach
```

### 1.2 The loader `FUN_0043972a` (eqgame.exe.c line 52128)

The name from +0x65 is taken as-is; a short name (<= 3 chars) gets
`_ACTORDEF` appended and is looked up as an actor definition, with the two
fallbacks `IT63_ACTORDEF` and `RED_BLOB_ACTORDEF` (the decompile at
`eqgame.exe.c` 52128ff — see the B1168 note: the server only ever sends
`IT<id>` names, so the suffix/fallback branch is dead for every payload fable
receives). The model is the equipment IT model — the same
`IT<modelId>` namespace fable's `EquipmentModelLoader` loads for entity
equipment (`src/client/graphics/eq/equipment_model_loader.cpp`); **no new
loader** (the card's rule).

### 1.3 Orient — per frame

The per-frame object update (`FUN_00477cc9`, eqgame.dis `00477d2e`/`00477d3a`:
the two vtable calls +0x58/+0x5c on the flight block) keeps the object's
heading (+0x2c) and aim (+0x30/+0x3c) moving by the B1163/B1170 flight rules
(fable already implements exactly that model in
`src/client/spell/spell_missile_object.cpp`, pinned by its own tests). The
graphics side turns those fields into rotation with the **512-unit trig
tables**: `EQGraphicsDX9.dll.c` `FUN_10184580` (line 309538) and
`FUN_101845a0` (line 309549) index `(uVar1 & 0x1ff) * 0x10 + 0x1088/0x1084`
— 512 entries per table, i.e. **512 angle units = 360 degrees**; the
per-frame table walk `(&DAT_102347a0)[DAT_10235f4c & 0x1ff]` (line 68988)
increments the angle by one unit per step. Hence the model's yaw follows the
flight heading and its pitch the flight aim, each `360/512` degrees per unit,
roll 0 (the spawn's third angle is 0: `00476e00` pushes only scale, the call
has exactly two angles). fable's `MissileModelAnglesFromFlight()`
(`include/client/spell/spell_missile_object.h`) implements exactly this and is
pinned by `tests/test_b1181_item_missile.cpp`
(`AnglesAreTheFlightAnglesInDegrees`, `TheModelFollowsTheFlightModel`).

### 1.4 Remove — lifetime / hit (eqgame.dis)

Inside the per-frame update, after the flight step:

```
00477f91  cmp byte ptr [edi+0x63], 9
00477f95  jne 0x4781f5                 ; type != 9 -> the item-missile removal
004781f5  mov eax, [edi+0x48]
004781fb  mov ecx, [ecx+0x148]         ; &start-time (the allocator base)
00478201  cmp ecx, [eax+0x68]          ; start-time vs &end-time (+0x68)
00478204  jbe 0x478364                 ; start <= end -> still alive
0047820a  jmp 0x477fbf                 ; LIFETIME -> remove
...
00478257  call [eax+0x20]              ; the physics/collision step
0047825a  cmp byte [edi+0x63], 9
00478264  je 0x4782a8                  ; type 9 skips the displacement check
00478266  fld [ebp-0x20]  ... fsub [ebp-0x34]   ; displacement (x, y, z)
00478284  call 0x477bba                ; the collision/LOS query along it
00478289  test al, al
0047828b  je 0x4782a8                  ; no hit -> keep flying
0047828d  jmp 0x477fbf                 ; HIT -> remove
00477fbf  mov ecx, edi
00477fc1  call 0x47789d                ; unlink the object from the live list
00477fc6  push edi
00477fc7  call 0x60d0d5                ; free the object (the actor dies with it)
00477fcc  pop ecx
00477fcf  jmp 0x47845c
```

The model is not destroyed separately: `0x47789d` unlinks the object from the
live-object list (eqgame.dis `0047789d`ff — the +4/+8 node unlink with the
`[0x905cd0]` head) and `0x60d0d5` frees it, which takes the actor (and its
drawn model) with the object. fable's equivalent is `SpellVisualFX`'s
removal path: `destroyMissileModel()` runs on the same FX-tick the object is
removed (lifetime in the flight model, or the hit the renderer observes), so
the model vanishes on the removal frame — pinned by
`AType27ObjectDrawsItsITModel` (the node is gone after the removal).

## 2. The fix

`src/client/graphics/spell_visual_fx_missile.cpp` (three small methods on the
FX clock, all guarded so type 9 never enters them):

- `createMissileModel(m)` — after `Spawn`, only when `m->type != 9` and
  `ParseMissileModelId()` resolves the +0x65 name to an IT model: create the
  scene node from the renderer's **existing** `EquipmentModelLoader` mesh
  (wired in `renderer_entities.cpp` and the `model_viewer` harness via
  `SpellVisualFX::setEquipmentModelLoader` — no new loader).
- `placeMissileModel(m)` — every FX tick: position at the flight's EQ
  (y, x, z) -> (x, z, y) and rotation from `MissileModelAnglesFromFlight()`
  (the 512-unit rule, (1.3)).
- `destroyMissileModel(m)` — on the removal tick (lifetime/hit): remove the
  node and release the mesh ref; `~SpellVisualFX()` sweeps any survivors.

`include/client/spell/spell_missile_object.h` /
`src/client/spell/spell_missile_object.cpp`: `ParseMissileModelId()`
(the name from the payload bytes — `IT` + digits, anything else is -1) and
`MissileModelAnglesFromFlight()` (device-free, so the angle rule is testable
without a client tree).

## 3. Tests (`tests/test_b1181_item_missile.cpp`)

Device-free: `ModelNameFromThePayloadBytes` (IT10 / IT11504 parsed from the
`0x4033` payload bytes, the negatives), `OnlyITNamesParse`,
`AnglesAreTheFlightAnglesInDegrees`, `TheModelFollowsTheFlightModel`
(the per-frame orientation vs the flight model at three points of the flight).

Device-backed (EDT_NULL device, real gequip archives, GTEST_SKIP without the
client tree — the `test_b1170_missile_object.cpp` style):
`AType27ObjectDrawsItsITModel` (a type-27 object named IT10 draws the model at
spawn, follows the flight, and the node is gone after the removal),
`ANameThatIsNotITDrawsNothing`, `AType9ObjectNeverDrawsItsModel` (the type-9
object keeps B1170's stages and never draws its model).

All 7 pass on the ThinkPad (`rbuild test -R B1181ItemMissile`), and the two
neighbour suites (`test_b1170_missile_object`: 9 pass / 1 pre-existing skip;
`test_spell_visual_vocabulary`: 5 pass / 3 pre-existing data skips) are
unaffected.

## 4. Type-9 byte-identity (acceptance B)

The six b1130 gate spells (38, 41, 54, 93, 458, 656 — the two of them with
type-9 missiles included), rendered base (`32c67ab3c`) vs tip with the same
b1130 rig: **all 2,584 frames byte-identical, 0 diff lines** (md5 census
`/home/user/kb-evidence/b1181_gate_md5.tsv`, 5,304 rows). The type-9 path is
not just frame-0-identical; the whole capture is.

## 5. Lane-5 oracle leg (acceptance C)

(Two legs on lane 5 with the B1134 observer rig, observer char 103 Oraclewatch
on the grabbed display, caster char 67 Testcaster on the Xvfb; plans and
pre/post SQL in `refactor/B1181/leg/`. Filled in as the legs land.)

### 5.1 The bolt leg — CAPTURED on both sides (queue r3, 2026-10-29)

The **oracle leg `bolt4`** (rc=0) captured the bolt with the official
observer and the official caster: five `S->C 0x4033` payloads, every one
naming **IT11504** (model at payload bytes 101..117), t=1793236596.935 and
.952 (two casts) delivered to BOTH clients (cports 54534 + 58919) and
t=1793236636.719 to 54534 only. `capture.pcap` (the leg's, 20-byte SLL2
headers), `video.mp4` (the observer's :99 grab) and `caster.mp4` (the
caster's Xvfb grab) are in `~/kb-evidence/kb-b1181-item-missile/leg/bolt4/`;
the first-packet payload is extracted to
`~/kb-evidence/kb-b1181-item-missile/it11504_payload.bin` (136 bytes).

The **fable leg** (`fable/fable-b1181/`, rc=0; willeq observer A, cport
42217, + headless willeq GM caster B, cport 49717, one world session)
captured the same shot on the fable client: six `S->C 0x4033` payloads,
every one naming **IT68** (not IT11504 — the fable leg ran after the
oracle legs' `Spells:UseLiveSpellProjectileGFX` leg-limited rule had been
undone by their teardown, and `fable_leg.sh` does not set it; the payload
bytes are the evidence, not the rule), t=1793237027.584/.600 to BOTH A and
B and t=1793237041.636/.656 to B. A's log records the draw:
`B1181 missile model: IT68 drawn` at 01:23:47.792 (A.log); the hit
(HC_OP_Death on A) landed 01:23:48.952, so the model is drawn for the
~1.16 s flight (dump frames 268..272 at the measured 0.3353 s/frame).
Measured on A's dump frames 255..278 (the changed-pixel census
`track_fable_it68.py` + vision on frames 270/285): **no independent
in-flight object resolves on A's screen** — A's own `/camera user1` (the
tour's, applied 01:23:44.451) is a close-up of A at bolt time (frame 270),
and B, the launch point, sits outside that view; the model is drawn per
the log but its screen size is unmeasurable on this leg. `video.mp4` is A's x11grab
(30 fps, :99); B is headless (`--no-graphics`) and has no video — A's
frames, A's log and the shared pcap are the willeq-side evidence.
The payload is extracted to `~/kb-evidence/kb-b1181-item-missile/it68_payload.bin`.

The **replay** (acceptance C, last row) ran on the CURRENT (post-merge)
tree, ThinkPad, `model_viewer --spell-test` with the leg's own payload
(the run: `WILLEQ_DATA_ROOT=<worktree>`, `--eqe1
/mnt/projects/EverQuest_ETC1/willeq.eqe1 --eqe1-dir
/mnt/projects/EverQuest_ETC1`, the plan
`/home/user/kb-b1181-replay/base_bolt/plan.tsv`, `--camera b1130 --seed 1
--particle-options --missile-payload <payload> --missile-at 1.0`, 30 fps,
6 s):
- `after_it68` (it68_payload.bin): rc=0, log `B1181 missile model: IT68
  drawn`; the measured blob (diff of this run vs the control below,
  `measure_it68_replay.py`) tracks frames 30..36 from (642,505) to
  (971,332); **mid-flight (frame 33) = 22x18 px**, centroid (766,437),
  removed by the hit (frame ~37, t≈1.25 s per the log's pass lines).
- `after_bolt2` (payload_0.bin = IT11504), same scene/seed/binary: rc=0,
  log `B1181 missile model: IT11504 not in the equipment archives --
  nothing drawn` — the control that the diff isolates the model.

Pitfall measured on this run: the post-merge harness **FATALs** with
`Texture 'dafft0001.bmp' not found in any EQE1 flat pack` unless it can
load `data/eqe1_texture_index.json` (via `WILLEQ_DATA_ROOT` or a CWD that
holds `data/`) AND gets `--eqe1-dir` over the pack directory — the old
single-file `--eqe1` invocation no longer resolves the daff particle
textures. The `after_it10`/`base_it10` frames measured in run 3's early
hours were rendered on the PRE-MERGE binary; they must be re-rendered on
the current tree before the final table ships (same command,
it10_payload.bin).

### 5.2 The archery shot — NOT captured (measured condition)

Every archery attempt so far produced **zero `0x4033` packets**: oracle
legs `archery`..`archery6` (all six `capture.pcap`s scanned, `all4033.py`)
and the fable leg's archery window. The measured conditions:
- Oracle legs: the official-client caster never landed a shot (the
  official client's server-side `/target <name>` resolution returned 0 on
  this rig — the finding that motivated the fable leg's client-side
  targeting); `archery6` additionally exited rc=66 (the snapshot restore
  mismatched after the leg).
- Fable leg: B's `/target Oraclewatch` APPLIED (B.log 01:23:09.410
  `Target set to Oraclewatch (ID: 261)`, `HC_OP_TargetHoTT` on the wire)
  and `/attack` APPLIED (01:23:12.433 `Auto-attack enabled`), but the
  auto-attack then produced no swing in the measured window: B.log has no
  swing/attack/damage line after 01:23:12.433 through the bolt, and the
  pcap's archery window (01:22:40..01:23:50) contains no `0x4033` and no
  recognisable swing op from B (cport 49717) — the server never received
  a ranged attack to animate. **Root cause isolated (run 3): the tour used
  `/attack` — the MELEE toggle. Its swing path sends nothing:
  `ProcessCombatRound`'s in-range branch only sets the timer
  (`combat_rounds.cpp` 437-441, "Attack timer ready - server should process
  melee attack", no packet) and a ranged weapon needs the RANGED toggle
  `/autofire` (`command_processor_registration.cpp` 266, "Toggle ranged
  auto-attack (archery/throwing)"), whose `EnableAutoFire`
  (`combat.cpp` 580-605) sends `HC_OP_AutoFire` — the server then swings
  the bow. Fix committed: `fable_b_tour.txt` now uses `/autofire`
  (twice, the on/off toggle); the next run re-runs the fable leg as-is.**

The fable leg's bolt (5.1) is the captured missile shot for acceptance C's
minimum; the archery row of the final table will carry this measured
condition unless the next run lands an arrow shot.
