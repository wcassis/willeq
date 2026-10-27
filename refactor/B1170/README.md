# B1170 (#489): the spell missile exactly as the client draws it

Branch `task/kb-b1170-missile-exact`, cut from `fable 9136b7a3` (B1168 merged).
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
  gate plans, binaries `before` = 9136b7a3, `step1` = 85803903, `after` = the
  MR head's code; md5s in `/mnt/projects/fable/review/B1170/worker/binaries.md5`):
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
- Tests (`tests/test_b1170_missile_object.cpp`): the decoder on the server's
  payload and on short / tiny-velocity input, the trig tables and angles, the
  24 rows (B), the 33-unit pass gate and the hit / lifetime rule, the spawn
  rules (no-flight types, lifetimes, failures), the removal fires and the
  four early-outs, a type-9 flight, the spells_us.txt fields. Plus
  `SpellVisualVocabularyTest.AMissileObjectFliesAndIsRemovedAtTheHit`: spell 38
  (travel 3, a 30000-unit life) closes a 20-unit gap and is removed at the hit;
  spell 93 (field 18 = 0, travel 0) is removed by its 50-unit lifetime on its
  first pass.

## (F) Re-gate (ThinkPad, before = 9136b7a3, after = the MR code)

RE-GATE-TABLE

## (G) Checks

CHECKS

## RULING REQUEST

RULING-REQUEST

## FOLLOW-UP

FOLLOW-UP-LIST

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
  `tests/test_zone_dispatch_table.cpp`, `tests/goldens/zone_opcodes_g01.txt` (BLESS), `tests/cmake/spells.cmake`.
- `refactor/B1170/scripts/` — `rdata.py`, `make_payload.py`, `missile_flight_model.py`,
  `frame_diff_series.py`, `render_b1130gate_tp.sh`, `regate_tp.sh`, `regate_identity.py`.
