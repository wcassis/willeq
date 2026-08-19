# T144 — `Object_Struct` is 8 bytes wrong and every ground spawn is garbage

**Slot: slot1. x86. eqemu lock REQUIRED (capture leg is the acceptance gate). No Pi.**

**Territory: `include/common/packet_structs.h` (the `Object_Struct` block ONLY,
around line 846), `src/client/eq_entities.cpp` (`ZoneProcessGroundSpawn`,
`ZoneProcessClickObject`), `include/client/world_object.h` +
`src/client/world_object.cpp`, `tests/`, `refactor/`,
`/mnt/projects/fable/review/T144/`.**

**T145 runs in parallel and also edits `include/common/packet_structs.h.**
It owns `FaceChange_Struct`, `EnvDamage2_Struct`, `GMKick_Struct`,
`GMKill_Struct`, `ControlBoat_Struct`, `PopupResponse_Struct`,
`DisciplineTimer_Struct`, and `src/client/eq_player_info.cpp` +
`src/client/eq_gm.cpp`. **Touch none of those.** Confine your header edit to
the `Object_Struct` definition and its `static_assert`. Do not reflow, reorder,
reformat, or "tidy" any other part of that file — a whitespace sweep here
turns a clean parallel merge into a conflict.

Read T134's Review first — specifically "Two independent, pre-existing wire
bugs found and fixed". This unit is the same shape and the same method.

## The finding

`EQT::Object_Struct` (`packet_structs.h:846`) is 100 bytes. The Titanium wire
struct is **92** — `Titanium::structs::Object_Struct`,
`/mnt/projects/claude/EQEmu/common/patches/titanium_structs.h:2085`, populated
field-by-field by `ENCODE(OP_GroundSpawn)` at
`/mnt/projects/claude/EQEmu/common/patches/titanium.cpp:698`.

Ours was derived from eqemu's **server-internal** variant
(`eq_packet_structs.h:2822`) — it carries `size`, `solid_type`, `incline`,
`tilt_x`, `tilt_y`, which **do not exist in the Titanium wire format at all** —
plus an invented `padding014` that shifts even that by a further 2, and it
drops `spawn_id`. Divergence begins at offset 8 and nothing after it is right:

| field | our offset | true Titanium offset |
|---|---|---|
| drop_id | 16 | 12 |
| zone_id | 20 | 16 |
| zone_instance | 22 | 18 |
| heading | 40 | 28 |
| z | 44 | 32 |
| x | 48 | 36 |
| y | 52 | 40 |
| object_name | 56 | 44 |
| object_type | 92 | 80 |

`ZoneProcessGroundSpawn` (`eq_entities.cpp:976-1000`) reads all nine of those
literal offsets. Consequences, none of which produce a size rejection:

- **`drop_id` is wrong**, so the `OP_ClickObject` we send back
  (`eq_entities.cpp:2394`) names an object that does not exist server-side —
  containers, forges and ovens cannot be opened.
- **x/y/z/heading are garbage** — objects render in the wrong place or off-world.
- **`object_name` is read 12 bytes into the coordinate block.**
- **`object_type` is read from `unknown084`/`spawn_id`**, so
  `isTradeskillContainer()` is decided by noise.
- `worldObj.size` reads eqemu's zeroed `unknown008` and is always 0.

**The tell was already in the code.** The comment at `eq_entities.cpp:957` —
"Server may send shorter packets (e.g., 97 bytes instead of 102)" — is not
jitter. `ENCODE` emits `strlen(object_name) + sizeof(structs::Object_Struct) - 1`,
i.e. a genuinely variable-length packet. That was read as a quirk for as long
as this code has existed. Same class of in-plain-sight evidence as T134's
"Resurrection offer from  for corpse '<garbage>' (spell 0)".

## CAPTURE FIRST — this is not optional and not a formality

The layout above is derived by reading eqemu source. It is high confidence,
**but T134's clincher was a real hex dump and this unit gets one too.** Do the
capture BEFORE writing the fix, so the fix is written against observed bytes:

1. Take the eqemu lock per the protocol (`/mnt/projects/fable/.eqemu-lock`).
2. Get a live client into a zone with a known ground spawn, or drop an item to
   create one.
3. Capture the raw `OP_GroundSpawn` bytes — the eqemu zone log's own hex dump
   is the T134 precedent, and the client's packet path can also dump them.
4. **Read where `object_name` actually starts.** If it is at wire offset 44
   (46 counting the 2-byte opcode) the analysis above is confirmed. If it is
   at 56, the analysis is WRONG — stop, report, and do not fix anything.

Put the hex dump, annotated, in `review/T144/`. It is the single most valuable
artifact this unit produces: **there is currently no captured-bytes test for
any packet struct in this repo**, which is precisely why this class of bug
keeps surviving `static_assert`s.

## The fix

Replace the struct body with the 92-byte Titanium layout (`linked_list_addr[2]`,
`unknown008[2]` as u16, `drop_id`, `zone_id` u16, `zone_instance` u16,
`unknown020`, `unknown024`, `heading`, `z`, `x`, `y`, `object_name[32]`,
`unknown076`, `object_type`, `unknown084`, `spawn_id`); assert 92; drop
`size`/`solid_type`/`incline`/`tilt_x`/`tilt_y` from `WorldObject` and from
every consumer; rewrite the nine literal offsets in `ZoneProcessGroundSpawn`.

The minimum-length guard must be recomputed: `object_type` at 80 means you need
2 + 84 bytes to reach it, not the current 2 + 96. Because the packet is
`strlen(name) + 91`, a short object name legitimately produces a packet that
cannot reach `object_type` — **handle that as a real case, not an error path**,
and say in the Review what you chose and why.

## Regression test — the thing that was missing

Add a **captured-bytes test**: the real hex dump from step 3 as a byte array in
the test, fed through the parse path, asserting the decoded `drop_id`,
coordinates, name and `object_type` match what the dump visibly contains. Not a
`sizeof` assert — those all passed while this bug was live. This test is the
template the rest of the header needs and T147 will generalize it.

## Acceptance — AT THE WIRE

Three units in a row (T132, T133, T134) proved that dispatch-layer acceptance
certifies dead features. **Do not accept this on "the struct is now 92 bytes."**
Accept on: a ground spawn appears at the correct location in a live session,
and a tradeskill container (forge/oven) **actually opens** when clicked — that
exercises `drop_id` round-tripping through `OP_ClickObject`, which is the field
whose breakage is most user-visible. Before/after evidence in `review/T144/`,
captured identically. Force daylight (`/say #set time 11 0`) before any capture.

## Verification

Full protocol bar (build, ctest, GLES2 build, visual gate goldens untouched,
ARM cross-compile, `check_file_sizes.sh` from your slot on your task branch).
Suite baseline 2433, sole expected failure the documented ALSA
`AudioManagerTest.PreloadCommonSounds`.

## STOP conditions

- The capture shows `object_name` at 56, not 44 — the analysis is wrong. Report,
  change nothing.
- Removing `size`/`solid_type`/`incline`/`tilt_x`/`tilt_y` from `WorldObject`
  reaches into renderer code beyond a mechanical field removal. Report the
  blast radius rather than refactoring the renderer inside this unit.
- eqemu unreachable or the lock is held — report blocked, do not fix on
  inference alone.

## Review

(executor fills)
