# B1170 (#489): the spell missile exactly as the client draws it

Branch `task/kb-b1170-missile-exact`, cut from `fable 9136b7a3` (B1168 merged);
`gitlab/fable` merged in twice (merge, not rebase): `070a9215` (B1166, B1169)
cleanly, and `f61fafb4` (B1165, B1173) with six conflicted files resolved by
hand (section E3).
Will's rule (2026-10-27): willeq copies the official client's spell effects
exactly. Every address below is an instruction start in
`/mnt/projects/fable/review/B1141/ghidra/out/eqgame.dis` (the `0x1…` ones in
`/mnt/projects/fable/review/B1141/ghidra/out/EQGraphicsDX9.dis`), checked by
`refactor/B1168/scripts/check_addrs.py` and `scripts/refactor/cite_check.py`
(section G); `exe.c N` is a line of `/mnt/projects/fable/review/B1141/ghidra/out/eqgame.exe.c`;
float constants were read from the image with `refactor/B1170/scripts/rdata.py`.
Evidence: `/mnt/projects/fable/review/B1170/worker/`.

**Result.** fable no longer draws a Middle stage on a cast: the B1152
record-driven missile (and its 0.302 s / 30 u/s calibration) is gone, pinned by
a test and by the harness (section D). fable now handles the server's
`0x4033` message the way the client does: it decodes it, builds the missile
object, runs the client's spawn, homing and physics per pass, draws the
record's Middle stage on the object and fires the 54 / 859 Target stages at
removal (sections A, B, E). The lane-5 server never sends a spell (type 9)
object, so the oracle leg stops at the recorded server condition and the
harness-vs-model pin (section C). Re-gate: section F; the bless list: RULING
REQUEST.

## (A) The client path, traced

### A1. The message builds the object

The message handler `FUN_0045b8f0` keeps the payload in `ebx`
(`0045b90a mov ebx, dword ptr [ebp + 0x10]`) and dispatches on the opcode:

```
004606ee  sub ecx, 0x105
004606f6  sub ecx, 8
004606f9  je 0x46070f            ; 0x3f26 + 0x105 + 8 = 0x4033
0046070f  push 0x88              ; the 0x88-byte object
00460732  push ebx               ; the payload = the constructor's 3rd argument
00460737  call 0x476f34
```

With a non-null third argument the constructor `FUN_00476f34` takes its
RECEIVED branch (exe.c 95448-95499): it copies the payload into the object
field by field at the SAME offsets, e.g.

```
004770c1  mov eax, dword ptr [edi + 0x14]
004770eb  mov eax, dword ptr [edi + 0x28]
004770ee  mov dword ptr [esi + 0x28], eax
00477103  mov eax, dword ptr [edi + 0x4c]
0047711b  mov eax, dword ptr [edi + 0x5c]
0047712d  mov al, byte ptr [edi + 0x62]
00477133  mov al, byte ptr [edi + 0x63]
00477139  mov al, byte ptr [edi + 0x64]
0047713f  lea eax, [edi + 0x65]
00477147  call 0x60e450
```

| offset | field (`client/spell/spell_missile_object.h`, `Wire`) | the lane-5 server writes (Arrow_Struct) |
|---|---|---|
| +0x10 / +0x14 / +0x18 | position (EQ y, x, z) | src_y / src_x / src_z |
| +0x1c / +0x20 / +0x24 | velocity | 0 |
| +0x28 | thrust word | velocity (`speed`, 1.5 for a spell) |
| +0x2c | heading (512 units) | launch_angle |
| +0x30 / +0x34 / +0x38 | pitch, pitch rate, heading rate | tilt 125, 0, 0 |
| +0x3c | the thrust's vertical angle (`aim`) | arc |
| +0x4c / +0x50 | source / target spawn id | source_id / target_id |
| +0x54 / +0x58 | two words | item_id / 125 |
| +0x5c | spell id (read only for type 9) | unknown092 = 16 |
| +0x60 / +0x61 / +0x62 | light, a byte, the damping selector | 0 / 0 / skill |
| +0x63 / +0x64 | object type (9 = spell) / second type | item_type / 0 |
| +0x65 | model name (strcpy) | IDFile |

Both branches then zero a velocity component under 0.001 in magnitude
(exe.c 95500-95511, `[0x641088]` = 0.001). The server column is the lane-5
source (`/opt/akk-stack/code` commit `ba2ca5eada80ac758ef8c2a2510cb39b0563119a`,
excerpts with line numbers in `/mnt/projects/fable/review/B1170/worker/server_excerpts.txt`).

### A2. One pass per frame, once 33 units passed

The frame loop walks the missile list (`0044553f mov edi, dword ptr [0x905cd0]`):

```
00445550  cmp dword ptr [eax], ebx      ; no actor yet ->
00445556  call 0x476b12                 ;   the spawn
0044555e  cmp byte ptr [eax + 0x1005], 1
00445569  call 0x47789d                 ;   a failed spawn is removed
0044557d  sub ecx, dword ptr [eax + 0x50]
00445580  cmp ecx, 0x21
00445583  jb 0x44559f                   ; < 33 units since the last pass: nothing
00445587  call 0x4766aa                 ; homing / hit
0044558f  call 0x4bfe0f                 ; physics
0044559a  call 0x477cc9                 ; removal
```

### A3. The spawn `FUN_00476b12`

- No caster entity (`00476b4f push dword ptr [esi + 0x4c]` / `00476b52 call 0x401b28`):
  `00476b65 mov byte ptr [eax + 0x1005], 1`, removed with no stage.
- Lifetime time() + 120000 (`00476b79 add eax, 0x1d4c0`); for a type-9 object
  (`00476b81 cmp byte ptr [esi + 0x63], 9`) the spell id must be in 1..9999
  (`00476b8e call 0x44d0d0`) and its record must exist (`00476ba1 call 0x40cbd6`),
  else 00476b65; then time() + spell `+0x28` (spells_us.txt field 18,
  `00476bbe add eax, dword ptr [edi + 0x28]`), or + 30000 for travel type 3
  (`00476bc3 cmp byte ptr [edi + 0x188], 3`, `00476bd1 add eax, 0x7530`),
  else + 50 (`00476bdd add eax, 0x32`).
- Target type (spell `+0x180`, field 98: `00476be6 mov al, byte ptr [edi + 0x180]`):
  8, 0x15..0x17, 0x1c (`00476bec cmp al, 8` .. `00476c00 cmp al, 0x1c`) put the
  object on its target and zero the velocity and the word (`00476d6b fstp dword ptr [esi + 0x28]`):
  **stage 2** (`00476d52 mov dword ptr [ebp - 0xc], 2`). Every other type is
  **stage 1** (`00476c11 mov dword ptr [ebp - 0xc], 1`) with a one-shot
  catch-up of 5.5 velocity steps (`00476ce8 fld dword ptr [0x64263c]`) plus
  0.02 x the caster's tick age (`00476c36 fmul dword ptr [0x6410b8]`), skipped
  for travel type 3 (`00476cd5 cmp byte ptr [eax + 0x188], 3`).
- A type other than 9 fires no stage: `local_c` (the spell record) stays 0.
- The stage fires with the object as the anchor-2 argument and spell `+0x28`
  as the caller duration:

```
00476eb9  push dword ptr [eax + 0x28]
00476ebf  push esi
00476ec2  push dword ptr [ecx + 0xfe4]
00476ec9  push dword ptr [ebp - 0xc]
00476ecc  call 0x49b260
```

  so an anchor-2 slot rides the object (`00495d8e mov eax, dword ptr [ebp + 0x20]`,
  `00495d99 mov ecx, dword ptr [eax + 0x10]`), an anchor-1 slot sits on the caster,
  and anchor 0 / 3 are skipped (their position / target arguments are 0:
  `00495dc3 je 0x495e6d`, `00495d65 je 0x495e6d`).
- Homing state: bearing and aim from the message (`00476eda mov dword ptr [eax + 0xfec], ecx`,
  `00476ee6 mov dword ptr [eax + 0xff0], ecx`), stored distance 2000
  (`00476eef mov dword ptr [eax + 0xff4], 0x44fa0000`), last pass = now
  (`00476f01 mov dword ptr [ecx + 0x50], eax`), refresh time now + 2000 for
  type 9 (`00476f19 add dword ptr [esi + 0xe4], 0x7d0`), + 500 otherwise
  (`00476f25 add dword ptr [esi + 0xe4], 0x1f4`).

### A4. Homing `FUN_004766aa`

Nothing for a zero word or no target (`004766de fld dword ptr [esi + 0x28]` ..
`004766f6 je 0x476ad0`); a target that is the object's own source is cleared
(`0047670b mov eax, dword ptr [edx + 0x258]` / `0047671e mov dword ptr [ecx + 0xfe8], ebx`).
The refresh interval follows the stored distance: under 200 it is 0
(`00476730 fcomp dword ptr [0x641f90]`, `00476749 xor edi, edi`), 200..250 →
250 (`0047679a mov edi, 0xfa`), 250..350 → 500 (`004767a7 mov edi, 0x1f4`),
350 and over → 1000 (`004767b9 mov edi, 0x3e8`); the compare is unsigned
(`004767d1 cmp eax, edi` / `004767d3 jbe 0x476930`). Under 200, a stored
distance under 2.0 with a target actor is the HIT
(`00476743 fcomp dword ptr [0x6410b4]`, `00476752 mov eax, dword ptr [edx + 0x1a8]`,
`00476769 mov byte ptr [eax + 0x1005], 1`). The refresh aims at the target's
z + 0.5 x its height (`004767df fld dword ptr [eax + 0x238]`, `004767e5 fmul dword ptr [0x6410bc]`),
stores the bearing (`0047684d call 0x46e42f`) and the XY distance
(`004768b1 call 0x439c41`, `004768b9 fstp dword ptr [eax + 0xff4]`), and the
vertical aim 128 − atan(distance / |dz|) (`004768f3 call dword ptr [eax + 0x18]`,
`004768f6 fsubr dword ptr [0x641f8c]`), 0 for |dz| <= 0.5
(`004768c2 fcomp dword ptr [0x6410bc]`), negated below (`0047691f fchs`).
Under 32 units the heading and aim snap to the bearing and aim
(`00476939 fcomp dword ptr [0x64112c]`, `0047694c mov dword ptr [esi + 0x2c], eax`);
otherwise the heading turns by at most 16 (`004769d3 fcomp dword ptr [0x641f9c]`,
`00476a2e fsub st(1)` / `00476a37 fadd dword ptr [esi + 0x2c]`) and the aim
moves only in 16-unit steps, not at all for a difference in 1..16
(`00476a79 fcomp st(1)` / `00476a80 jnp 0x476acd`).

### A5. Physics `FUN_004bfe0f`, the missile arm

`004bfe1e test ebx, ebx` / `004bfe37 je 0x4bfffb` (no entity): dt = 0.02 x the
elapsed units (`004c0033 fmul dword ptr [0x6410b8]`), at most 3.0
(`004c0049 mov dword ptr [ebp + 8], 0x40400000`); heading += dt x heading
rate, wrapped into [0, 512) (`004c01c7 fmul dword ptr [esi + 0x28]` ..
`004c0228 jnp 0x4c0208`); pitch += dt x pitch rate, clamped to ±128
(`004c0245 mov dword ptr [esi + 0x20], 0xc3000000`, `004c025c mov dword ptr [esi + 0x20], 0x43000000`).
Thrust: y = cos(heading) x word, x = sin(heading) x word
(`004c02a4 call dword ptr [eax + 0xc]`, `004c02c5 call dword ptr [eax + 8]`),
z = sin(aim) x word when the aim is non-zero (`004c02f8 call dword ptr [eax + 8]`).
Scale 0.8 and damping 0.625 (`004c04c1 fld dword ptr [0x6431d8]`,
`004c04cd fld dword ptr [0x643f7c]`), 0.85 when `+0x62` is 0
(`004c075c cmp byte ptr [eax + 0x62], 0` / `004c0762 mov dword ptr [ebp - 4], 0x3f59999a`);
`v = damp x v` (`004c0785 fmul dword ptr [esi + 0xc]`), components under
1.19e-7 zeroed (`004c07d1 fcomp dword ptr [0x645e54]`), `v += thrust`
(`004c0837 fadd dword ptr [esi + 0xc]`); with `+0x62` = 0 an XY speed under
0.05 stops the object (`004c0878 call dword ptr [eax + 0x10]`,
`004c0888 fcomp dword ptr [0x642ae8]`); `pos += dt x v`
(`004c0998 fmul dword ptr [esi + 0xc]` .. `004c09b4 fstp dword ptr [esi + 8]`).
The actor's bob terms are off (`004c09c3 cmp eax, -1`): its constructor sets
them to −1 (exe.c 112299 / 112301 `_Dst[0x21]` / `_Dst[0x22] = 0xffffffff`, the `FUN_0048f023` body).

The trig is EQGraphicsDX9's: two 512-entry tables (`0x101848a0` builds
cos(i x `[0x101ec4b0]`), the sin table is the same values shifted by 128), read
by truncating the angle and masking it (`0x10184507 call 0x101a9b60`,
`0x1018450c and eax, 0x1ff`, `0x10184511 fld dword ptr [esi + eax*4 + 0x804]`);
the vertical-aim atan is a search of tan[i] = sin[i] / cos[i]
(`0x101846a0 fld dword ptr [esp + 4]` .. `0x101847fe dec esi`): the largest
index whose tan does not exceed the ratio.

### A6. Removal `FUN_00477cc9` / `FUN_0047789d`

`FUN_00477cc9` removes the object when the hit flag is set
(`004782b8 cmp byte ptr [eax + 0x1005], 1` → `004782d2 jmp 0x477fbf`) or the
frame time passes the lifetime (`0047821b cmp ecx, dword ptr [eax + 0x68]` /
`0047821e ja 0x477fbf`). `FUN_0047789d` then, for a type-9 object with an actor
(`0047793d cmp byte ptr [esi + 0x63], 9`, `00477947 cmp al, bl`), frees the
emitters linked to it except spell 859's, which are only unlinked
(`00477951 call 0x476ad4`; `00476aeb cmp dword ptr [ebx + 0x5c], 0x35b`),
and fires a Target stage for two spells only:

- spell 54 (`00477959 cmp eax, 0x36`): stage 2 at the object's position one
  velocity step back (`00477a59 fsub dword ptr [esi + 0x1c]` ..
  `00477a6c fsub dword ptr [esi + 0x24]`), caster = the source, no target
  (`00477aa1 push 2`, `00477aa3 call 0x49b260`);
- spell 859 (`00477962 cmp eax, 0x35b`): within 40.0 XY units of the local
  player, stage 2 at the player behind the four early-outs B1163 (C) traced
  (`include/client/graphics/spell_b1163_missile_rules.h`, unchanged).

Every other spell's Target stage is OP_Action's, as before.

## (B) The motion model, and `missile_model.py`'s 24 rows

`tests/test_b1170_missile_object.cpp` (`B1170MissileObject.ReproducesTheModelsTwentyFourRows`)
flies the object through `Spawn` + `Pass` for every row of
`python3 -I refactor/B1163/scripts/missile_model.py`. 23 rows are the
script's passes. Row (9.05, 1.5 / 0.625, 100 units) is **7, not 5**: at
dt = 2.0 the object passes the target on pass 3; the script's 1-D loop stores
the SIGNED gap (−2.09 < 2.0 reads as a hit on pass 5), the client stores the
XY distance, a magnitude (`004768b1 call 0x439c41`), so the object turns back
(under 32 units the heading snaps to the bearing) and hits on pass 7. With an
absolute distance and no turn-back the script would never hit at all; the
client's path is the one the test pins.

## (C) The oracle leg: the lane-5 server never sends a type-9 object

Read-only queries under the lease (taken and released,
`/mnt/projects/fable/review/B1170/worker/lane5_readonly_query.txt`,
`lane5_readonly_query2.txt`; the stack was down before and is down after):

```
38   Lightning Bolt       targettype 5   player_1 PLAYER_1
54   Frost Bolt           targettype 5
93   Burst of Flame       targettype 5
859  Lava Breath - Test   targettype 8
140 spells have targettype 1, e.g. 68 Bolt of Flame (IT11504), 322 Flame Bolt (IT11504)
Spells:UseLiveSpellProjectileGFX = false   Spells:FRProjectileItem_Titanium = 1113
```

A targettype-1 spell does reach `TrySpellProjectile` (zone/spells.cpp lines
2585-2586) and the server sends `0x4033` through `Mob::ProjectileAnimation`
(zone/special_attacks.cpp lines 1793-1860). But that function sets the type
byte from the skill, never from the spell: its `skillInUse` defaults to
Archery (zone/mob.h line 340), `item_type = GetItemTypeBySkill(skillInUse)`
(special_attacks.cpp line 1817), which returns only 0, 1, 2, 3, 4, 19, 27, 35
or 45 (zone/mob.cpp lines 7198-7222, common/item_data.h lines 55-101) — 27,
ItemTypeArrow, for every spell path; the rule only switches the model name
(`player_1` vs item 1113). `+0x63` is never 9, so the client fires no spell
stage on any lane-5 `0x4033` (A3: the stage needs type 9), and the server
puts 16 in `+0x5c`, not the spell id.

**So no spell, rule or single spell row on this server can make the oracle
draw a Middle stage**, and a capture would show an item model (the IT file),
which fable does not draw (FOLLOW-UP). As the card allows, the leg stops at
this recorded server condition and the harness-vs-model pin below.

**Harness vs model.** `refactor/B1170/scripts/make_payload.py` writes a
SYNTHETIC type-9 payload for spell 38 (word 1.5, `+0x62` 1; and word 1.0,
`+0x62` 0), `tools/model_viewer --missile-payload <file> --missile-at 4.0`
feeds it to the FX on the b1130 rig (source and target remapped to the
harness's caster and target, the heading set to the bearing as the server's
`CalculateHeadingToTarget` would), and
`refactor/B1170/scripts/missile_flight_model.py` flies the same rules in
Python from the geometry the harness printed and compares frame by frame
(`/mnt/projects/fable/review/B1170/worker/harness_missile/model_vs_harness.txt`):

```
payload_38:    model first frame 120, last 128; harness first 120, last 128; max position difference 0.0001 units  MATCH
payload_38_d0: model first frame 120, last 128; harness first 120, last 128; max position difference 0.0001 units  MATCH
```

The run log shows the Middle stage on the object: `B1170 missile stage 1:
spell=38 record=80 slots 0:2/1@16128..0.25 (of 1)` (EDD 279, anchor 2, on the
object's anchor id 0x3F00, life 0.25 s), and the removal at the hit.

## (D) Step 1: no Middle-stage flight without an object

Removed: `createCastGlow`'s `Missile`, `launchMissile`, `fireMissileLanding`,
`fireSpell859RemovalAtPlayer` (its gate now runs on the object's removal),
the `createLanding` deferral, `kMissileSpeed` / `MissileFlightTime()` /
`MissileReachAt()` / `MissileLandAt()` and the three calibration constants,
and the `SpellCastVisualStartedData::targetId` that only fed the missile.
`spell_b1163_missile_rules.h` keeps the decoded constants and the 859 gate.

- Test: `SpellVisualVocabularyTest.ACastDrawsNoMiddleStageWithoutAMissileObject`
  — a record-80 cast builds no object (`missileObjectCount() == 0`) and its
  landing is drawn on OP_Action (B1152 returned 0 there, "deferring to the
  in-flight missile").
- Harness (`refactor/B1170/scripts/render_b1130gate_tp.sh`, the B1163 b1130
  gate plans; first pass with `before` = 9136b7a3 and `step1` = 85803903,
  evidence `gate_before/`, `gate_step1/`; repeated on the final fable tip with
  B1169's deterministic harness, section F; md5s in
  `/mnt/projects/fable/review/B1170/worker/binaries.md5`):
  before, 38 and 93 print 3 `B1152 missile` lines each and defer the landing;
  after, none, and `createLanding: caster=1 target=2 spell=38 row=80 slots …`
  at OP_Action. `refactor/B1170/scripts/frame_diff_series.py`
  (`/mnt/projects/fable/review/B1170/worker/gate_step1/frame_diff_summary.txt`):
  38 and 93 differ in 220 of 465 frames, first at frame 106 (3.533 s, the
  first frame after B1152's launch at OP_Action 4.000 − 0.486 s); 54, 41, 458,
  656 and 859: 0 of 420 frames differ.

## (E) Step 2: the object in fable

- `include/client/spell/spell_missile_object.h`, `src/client/spell/spell_missile_object.cpp`
  — device-free: `DecodeWire`, the trig tables, `Bearing`, `AngleDiff`,
  `WrapAngle`, `Spawn` (A3), `Home` (A4), `Physics` (A5), `RemovedAfterPass` /
  `RemovalFireFor` (A6), `ParseSpellsUsLine` (fields 18 / 98 / 122).
- `src/client/graphics/spell_visual_fx_missile.cpp` — `SpellVisualFX::spawnMissileObject`
  and the per-frame loop (A2) on the FX clock (time() read as ms); stage 1
  draws the Middle slots on a ParticleManager anchor that follows the object
  (`ParticleManager::setFxAnchorPosition`), stage 2 and the removal fires go
  through `createLandingFromSlots` with the call's explicit arguments
  (`StageArgs`: a slot whose actor argument is 0 is skipped).
- Wire: `HC_OP_SomeItemPacketMaybe = 0x4033` → `PlayerComponent::ZoneProcessSomeItemPacketMaybe`
  → `SpellManager::handleMissileObject` → `SpellCastVisualStartedData::missileObject`
  → `ApplySpellCastStartedFx` → `spawnMissileObject` (the renderer supplies
  EQ positions + model heights and the local player).
- The object keeps its target's height as read at the spawn (the client's
  actor height `+0x238` is per actor, not the animated pose's box).
- Tests (`tests/test_b1170_missile_object.cpp`): the decoder on the server's
  payload and on short / tiny-velocity input, the trig tables and angles, the
  24 rows (B), the 33-unit pass gate and the hit / lifetime rule, the spawn
  rules (no-flight types, lifetimes, failures), the removal fires and the
  four early-outs, a type-9 flight, the spells_us.txt fields. Plus
  `SpellVisualVocabularyTest.AMissileObjectFliesAndIsRemovedAtTheHit`: spell 38
  (travel 3, a 30000-unit life) closes a 20-unit gap and is removed at the hit;
  spell 93 (field 18 = 0, travel 0) is removed by its 50-unit lifetime on its
  first pass.

### E2. The B1165 stage gate

Every stage the object fires goes through `SpellVisualFX::stageDraws`, the
FUN_0049b260-entry gate B1165 implements (eqgame.exe.c:121989-122034):
the spawn's stage 1 / 2 on (source, none) — `00476ecc` passes 0 as the
target and the position (`00476ec0 push ebx`, `00476ec1 push ebx`; `00476ebf push esi`
is the object, `00476ec2 push dword ptr [ecx + 0xfe4]` the caster); spell 54's
removal fire on (source, none) (`00477aa3`); spell 859's on (source, the
player) (`0047f5ed`), which FUN_00491cff always shows for a target that is me,
so only density / opacity / ShowSpellEffects hide it. The source's and the
target's On-For inputs come from the game thread (`fxActorOf`,
`SpellManager::handleMissileObject`) on the same event. Tests:
`SlotRules.TheSpellPagesDensityScalesEveryStageAtCreation` and
`SlotRules.TheOnForFilterGatesEachStage` now take the Middle from a `0x4033`
object (B1165 drove it through the removed B1152 missile).

### E3. The merge with `f61fafb4`, resolved by hand

`spell_visual_fx.h/.cpp`, `spell_cast_fx.cpp`, `spell_packet_handlers.cpp`,
`event_payloads_ui.h`, `test_spell_impact_visual.cpp` conflicted, all on
B1165's additions to the B1152 missile this card removes. Resolution: the
`Missile` struct, `m_missiles`, `fireMissileLanding`, `fireSpell859RemovalAtPlayer`
and the createLanding deferral stay removed together with B1165's On-For fields
on them; `createCastGlow` keeps B1165's Source gate on (caster, caster) and
takes only `casterFx` (its `targetId` / `targetFx` fed only the missile);
`createLanding` keeps B1165's stage-2 gate; `SpellCastVisualStartedData`
keeps `casterFx` and gains `missileTargetFx` for the object; B1165's
`fxActorOf` is kept and also fills the object's inputs; the source-string test
expects the new `createCastGlow` call. Two test lines B1165 / B1150 cite
moved by the new test helper and their READMEs' line cites were updated
(DocCitations).

## (F) Re-gate (ThinkPad; before = fable `f61fafb4`, after = this branch)

Both binaries built on the ThinkPad from the final trees (md5s in
`/mnt/projects/fable/review/B1170/worker/binaries.md5`: `before_t2`,
`after_t2`), B1169's deterministic harness (`--fx-seed 1`, fixed solid draw
order; each run log carries the `determinism (B1169)` line). Renders:
`refactor/B1170/scripts/regate_tp.sh` (tour-7 + 40 records on b1118, 40
records on b1130, B1157's plans and command lines), frame identity
`refactor/B1170/scripts/regate_identity.py`
(`/mnt/projects/fable/review/B1170/worker/regate/identity_t2.tsv`):

- **70 of 87 runs are byte-identical before / after** (0 differing frames).
- **The 17 that differ are exactly the runs whose before-log prints 3
  `B1152 missile` lines and a deferred landing**: b1118 tour 93, tour 38, r0
  (93), r1 (200), r2 (202), r6 (722), r7 (40), r18 (378), r25 (14), r26 (310);
  b1130 records 0, 1, 2, 6, 7, 25, 26. Each first differs at the B1152 launch
  (e.g. tour 93 / r0 at frame 76 = 2.533 s = OP_Action 3.0 − 0.486 + one frame).
- The b1130 gate spells (section D's plans): 38 and 93 differ (220 of 465
  frames, first 106), 54 / 41 / 458 / 656 / 859 are identical
  (`gate_t2_identity.tsv`).
- An earlier pass without B1169's fix differed in 1 px on 71 frames of some
  non-missile rows; re-rendering showed the same binary reproduces both
  outcomes (`regate/identity_before_vs_after_rerun.tsv`): the pre-B1169 draw
  order, not this change. The final pass above has none.

The 17 runs, measured (`refactor/B1170/scripts/measure_regate.sh`, B1139's
`harness_measure.py` / B1155's `harness_measure_b1130.py` + `band_table.py`;
tables `refactor/B1170/regate_b1118.tsv`, `refactor/B1170/regate_b1130.tsv`,
built by `refactor/B1170/scripts/build_regate_table.py`). Owner of every row:
the removed B1152 record-driven Middle (the oracle drew no Middle on these
casts, B1168).

b1118 (land_cov, end; src_cov):

| row | spell | land_cov b → a | end b → a | src_cov b → a |
|---|---|---|---|---|
| tour 93 / tour 38 / r0 | 93 / 38 / 93 | 1.993 → 2.113 | +6.633 → +6.467 | 0.385 → 0.363 |
| r1 | 200 | 1.308 → 1.251 | +6.233 → +6.167 | 0.304 → 0.304 |
| r2 | 202 | 0.694 → 0.671 | +4.633 → +4.633 | 0.301 → 0.300 |
| r6 | 722 | 0.401 → 0.391 | - | 0.340 → 0.340 |
| r7 | 40 | 0.902 → 1.067 | +5.633 → +5.900 | 0.524 → 0.411 |
| r18 | 378 | 0.945 → 0.982 | +5.200 → +5.067 | 0.433 → 0.433 |
| r25 | 14 | 2.088 → 1.992 | +6.533 → +6.433 | 0.448 → 0.448 |
| r26 | 310 | 1.870 → 1.492 | +4.667 → +4.567 | 0.401 → 0.401 |

The tour 93 / 38 after values are B1151's pre-missile tour values (2.113 /
+6.467, `refactor/B1157/b1118_table.tsv` `land_cov_b` / `end_b`), and r0's and
r26's are B1151's record values (2.113, 1.492; `refactor/B1152/README.md` sweep
table): removing the missile restores what fable drew before B1152.

b1130 (oracle / before / after, verdicts before → after):

| record | source_cov | landing_cov | landing_end_s |
|---|---|---|---|
| 0 | 0.170 / 0.213 / 0.176, over → over | 3.945 / 0.553 / 0.552, short → short | 6.670 / 4.667 / 4.667, short → short |
| 1 | 0.290 / 0.092 / 0.092, short → short | 3.050 / 0.243 / 0.252, short → short | 6.580 / 3.400 / 3.300, short → short |
| 2 | 0.147 / 0.275 / 0.272, over → over | 2.250 / nan / 0.316, n-a → short | 5.480 / nan / 5.067, n-a → short |
| 6 | 0.380 / 0.098 / 0.098, short → short | 0.762 / 0.142 / 0.145, short → short | 6.260 / 1.200 / 1.167, short → short |
| 7 | 0.390 / 0.211 / 0.144, short → short | 3.077 / 0.577 / 0.529, short → short | 6.810 / 6.100 / 5.333, short → short |
| 25 | 0.284 / 0.122 / 0.122, short → short | 4.847 / 0.830 / 0.846, short → short | 6.640 / 4.433 / 4.433, short → short |
| 26 | 0.405 / 0.195 / 0.195, short → short | 8.003 / nan / nan, n-a | 5.050 / nan / nan, n-a |

No verdict changes. Record 7 (spell 40) is the one B1173 flagged: its
source_cov moves 0.211 → 0.144 (B1173's diagnostic patch measured
0.210 → 0.142 on its tip). `nan` is the instrument finding no landing run.

## (G) Checks

- Builds and tests on `gitlab-runner-2` (the session's build host for this
  slug from the coordinator's switch on): the spell / bridge / harness / dispatch
  targets, `tbuild test -R 'Bridge|EventBus|FxHarness|GameStateSync|Reachability|Spell|SlotRules|OptionsParticles|ZoneDispatch|ZoneOpcode|B1163|B1170'`
  → **100 % tests passed, 0 failed out of 417** on the merged tip
  (`/mnt/projects/fable/review/B1170/worker/ctest.log`); the three suites that
  skip without the EQ client were also run with `EQ_CLIENT_PATH`:
  `test_b1170_missile_object` 10/10, `test_spell_visual_vocabulary` 8/8,
  `test_spell_slot_rules` 18/18 (`ctest_client_data_runner2.txt`).
- `tbuild hygiene` → 100 % of 48 (`hygiene.log`).
- Continuation leg (2026-10-28, this branch's table retirement + bless):
  `rbuild kb-b1170-missile-exact hygiene` → 100 % of 48 (1 skipped,
  `MemoryCapHuntScript`, as usual); section H's cite-checker line re-measured
  on the edited README (0 problems).
- Renders on the ThinkPad (`model_viewer` built there), section F.
- Cite checker and the bare-address check: section H.

## (H) Citations

- `python3 -I scripts/refactor/cite_check.py refactor/B1170/README.md --tree . --extra /mnt/projects/fable --dis /mnt/projects/fable/review/B1141/ghidra/out/eqgame.dis --dis /mnt/projects/fable/review/B1141/ghidra/out/EQGraphicsDX9.dis`
  → `checked 38 cites, 0 problems, 4 address warnings` (the warnings: `0x101ec4b0` twice, a data constant cited as data; `0x4365db` twice, the wrap helper whose entry the .dis decodes as a jump table, see below).
- Every bare address and quoted `address instruction` pair:
  `python3 -I refactor/B1168/scripts/check_addrs.py refactor/B1170/README.md /mnt/projects/fable/review/B1141/ghidra/out/eqgame.dis /mnt/projects/fable/review/B1141/ghidra/out/EQGraphicsDX9.dis`
  → `checked 152 bare addresses, 135 quoted instructions, 0 problems`. The same check over the new code
  (`include/client/spell/spell_missile_object.h`, `src/client/spell/spell_missile_object.cpp`,
  `src/client/graphics/spell_visual_fx_missile.cpp`, `tests/test_b1170_missile_object.cpp`)
  reports no address that is not an instruction start (its MISMATCH lines are
  prose after a quoted instruction); the one non-instruction function entry,
  the wrap helper at `0x4365db` (the .dis decodes a jump table over it), is
  cited by its instructions `004365df` .. `00436625`.

## RULING REQUEST

BLESS-ONLY items this branch touches or invalidates, with their rulings:

1. **`tests/goldens/zone_opcodes_g01.txt` +1 line** `0x4033 HC_OP_SomeItemPacketMaybe`
   (and `ZoneDispatchTable.GoldenListIsTheMeasured145Arms` now expects 146):
   the new 0x4033 handler. **Blessed by Will 2026-10-28** (the #489 note):
   the line and the 146-arm expectation are approved and stand; the golden's
   comment says so.
2. **No longer oracle facts — removed from the gate tables** (orchestrator
   ruling 2 on #489, 2026-10-27: they are derived tables built on a flight
   the oracle never drew, B1168; remove them in a continuation leg,
   replacement values = section F's after column, no bless needed): applied
   in the B1170 continuation commits of 2026-10-28. For every row the
   missile-era values are replaced by section F's after column; cells without
   an after value are marked RETIRED, and the reason is recorded on the row:
   - `refactor/B1157/b1118_table.tsv`: rows `tour 93`, `tour 38`, `r0`, `r1`,
     `r2`, `r6`, `r7`, `r18`, `r25`, `r26` — `land_cov_n`, `end_n`,
     `src_cov_n` now hold section F's after; `src_on25_n`, `src_cen_n`
     (no section F re-measure) and, for the eight `r*` rows, the `*_b` B1152
     baselines (missile-era renders) are marked RETIRED; the tour 93 / 38
     `owner` text fixed.
   - `refactor/B1157/b1130_table.tsv`: records 0, 1, 2, 6, 7, 25, 26 — the
     `b1157_*` columns now hold section F's after; the `delta_*` columns
     (no section F re-measure) are marked RETIRED; the `v57_*` verdicts were
     re-checked against section F's after verdicts and are unchanged.
   - `refactor/B1152/missile_oracle.tsv`: `flight_start*`, `flight_end*`,
     `middle_onset*`, `middle_end*` (not a flight, refactor/B1168/README.md A5)
     struck as RETIRED in the headers with the reason in a comment, and with
     them the B1152 gate rows launch@−0.486 / reach@−0.184 / land@+0.116
     for 38 / 93 (refactor/B1152/README.md "The missile timing gate",
     refactor/B1163/README.md (B) band / harness tables).
   - `/mnt/projects/fable/review/B1142/gate/regression_table.tsv`: **none** — its
     fable columns were rendered before B1152 existed and its oracle columns
     are oracle captures with no Middle stage, so no cell holds the missile.
3. **The event**: the 0x4033 object rides `SpellCastVisualStarted`
   (`SpellCastVisualStartedData::missileObject`) because a new
   `GameEventType` changes the event-type count root CLAUDE.md states in three
   places, which this card may not edit. **Ruled (orchestrator note on #489,
   2026-10-27): keep it on `SpellCastVisualStarted`; no new event type.**

## FOLLOW-UP

- **A real type-9 capture**: needs a server that writes 9 at `+0x63` and the
  spell id at `+0x5c` (eqemu's ProjectileAnimation never does; a server patch
  or a live-era server). Then: lane-5 leg per card ask 3, the captured payload
  fed with `--missile-payload`.
- **Item missiles** (type ≠ 9: every lane-5 `0x4033`, arrows and the
  bolt-graphics spells): the client draws the `+0x65` IT model on the object;
  fable builds and flies the object but draws nothing.
- Not modelled in the object: the caster-race branch of the spawn
  (`00476c3c`, races 0x31 / 0x9e / 0xc4), `FUN_00477cc9`'s world-collision
  sweep and its proximity hits (`FUN_0047658a`), the hit flag's position snap
  (`004782c1` .. `004782cf`), the legacy spells.eff stage path for a spell
  with no spellsnew.eff record (`0x49ab78`), `FUN_0047f4af`'s SPA-driven
  position override for 859 (`0047f57b` .. `0047f594`), and the time() unit
  (read as ms; B1168 (B)).

## Files

- `include/client/spell/spell_missile_object.h`, `src/client/spell/spell_missile_object.cpp` — the object.
- `src/client/graphics/spell_visual_fx_missile.cpp`, `include/client/graphics/spell_visual_fx.h`,
  `src/client/graphics/spell_visual_fx.cpp` — drawing it; the B1152 missile removed.
- `include/client/graphics/environment/particle_manager.h`, `src/client/graphics/environment/particle_manager.cpp` — FX anchors.
- `include/client/eq_opcodes.h`, `src/client/core/eq_strings.cpp`, `src/client/zone/zone_dispatch.cpp`,
  `include/client/player/player_component.h`, `src/client/player/player_buffs.cpp`,
  `include/client/spell/spell_manager.h`, `src/client/spell/spell_packet_handlers.cpp`,
  `include/client/state/event_payloads_ui.h`, `src/client/bridge/spell_cast_fx.cpp`,
  `src/client/bridge/irrlicht_bridge.cpp`, `src/client/graphics/renderer/renderer_entities.cpp` — the wire.
- `tools/model_viewer/` — `--missile-payload` / `--missile-at`, `missile.tsv`.
- `tests/test_b1170_missile_object.cpp`, `tests/test_spell_visual_vocabulary.cpp`,
  `tests/test_b1163_missile_rules.cpp`, `tests/test_spell_impact_visual.cpp`,
  `tests/test_zone_dispatch_table.cpp`, `tests/goldens/zone_opcodes_g01.txt` (blessed by Will 2026-10-28), `tests/cmake/spells.cmake`.
- `refactor/B1170/scripts/` — `rdata.py`, `make_payload.py`, `missile_flight_model.py`,
  `frame_diff_series.py`, `render_b1130gate_tp.sh`, `regate_tp.sh`, `regate_identity.py`.
