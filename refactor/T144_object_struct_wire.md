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

Executed in `/mnt/projects/fable/wt/slot1`, task branch
`task/T144-object-struct`, off `fable` tip `857ddc46`. eqemu lock held for
the whole live phase and deleted after clean shutdown. Full evidence:
`/mnt/projects/fable/review/T144/` (README + annotated hex dump + raw dump +
three filtered live-session logs + one context screenshot).

### CAPTURE FIRST — result: analysis CONFIRMED, no STOP

`object_name` starts at **wire offset 44**, not 56. Captured before a single
line of the fix was written, by logging `EQ::Net::Packet::ToString()` at the
top of `ZoneProcessGroundSpawn` (temporary instrumentation, removed before
commit — the committed tree contains no capture hook).

11 real `OP_GroundSpawn` packets from a live qeynos zone-in. Every one has
`'I','T'` at dump offset `0x2E` == wire 44. Decoding all 11 at the Titanium
offsets and cross-checking against the **server's own `object` DB table** for
`zoneid=1` — an authority completely outside the client — gives 10/10 exact
matches on name, type, x, y and heading (the 11th packet is a genuine dropped
item with no DB row).

Zone choice: **qeynos**, not the default `arena`, because its `object` table
holds 10 tradeskill containers of 7 distinct types in one zone
(`SELECT ... FROM object WHERE zoneid=1`), giving a wide sample from a single
zone-in. `Testchar` was temporarily moved there and restored afterwards.

| field | wire offset | packet 1 decoded | qeynos `object` row 228452 |
|---|---|---|---|
| drop_id | 12 | 199 | (runtime entity id, no DB equivalent) |
| zone_id | 16 | 1 | zoneid 1 |
| zone_instance | 18 | 0 | — |
| heading | 28 | 117.0 | 117 |
| z | 32 | 0.0 | 0.002 |
| x | 36 | -177.0 | -177 |
| y | 40 | 484.0 | 484 |
| object_name | **44** | `"IT10804_ACTORDEF"` | IT10804_ACTORDEF |
| object_type | 80 | 17 (blacksmithing) | 17 |
| unknown084 | 84 | 0xFFFFFFFF | (server sets 0xFF) |
| spawn_id | 88 | 0 | (server sets 0) |

### The variable length is the format, not jitter

Every captured packet satisfies `body_len == strlen(object_name) + 91`,
exactly as `ENCODE(OP_GroundSpawn)` writes it
(`strlen(name) + sizeof(structs::Object_Struct) - 1`). Observed bodies: 95,
104, 105, 107. **The 95-byte one is a 97-byte packet — literally the "97
bytes instead of 102" the old comment in `eq_entities.cpp:957` dismissed as
server jitter.** It is a dropped `IT63` item. The in-plain-sight tell the plan
predicted was there, and it is now a named test case
(`GroundSpawn_CapturedBytes_ShortPacketGroundItem`).

A second in-plain-sight tell the capture surfaced that the plan did not
predict: the bytes after the name's NUL are **uninitialised server heap**
(`ENCODE` only `strcpy`s into a fresh `new unsigned char[]`). One forge packet
carries a leftover `"_ACTORDEF"` there from a previous allocation. Any reader
treating `object_name` as a fixed 32-byte field produces
`"IT66_ACTORDEF\0_ACTORDEF"`. Pinned by
`GroundSpawn_NameIsNulTerminatedNotFixedWidth`.

### What the old struct actually did — worse than "objects render wrong"

`drop_id` was read at wire offset 16, **which is `zone_id`**. So every object
in the zone decoded to `drop_id == 1`, and `m_world_objects` (a map keyed by
`drop_id`) collapsed all 11 into a single entry. And `object_type` was read at
offset 92, past the end of the real 92-byte struct, and came out `0` every
time — so `isTradeskillContainer()` was false for every object, so
`IrrlichtBridge`'s `WorldObjectSpawned` case (which only calls
`addWorldObject()` for tradeskill types) registered **zero** clickable world
objects.

That is stronger than the plan's estimate. The plan said forges "cannot be
opened" because `drop_id` was wrong. In fact the feature could not even be
*attempted*: there was no click volume to hit, the intent could never fire,
and if it somehow had, `eq_bridge.cpp`'s `WorldObjectInteractIntent` handler
gates on `isTradeskillContainer()` too. **The whole tradeskill-container
feature was dead at every layer** — the reachability pattern again, this time
caused by a wire bug rather than a missing caller.

Before/after on identical runs (`review/T144/run_before_parsed.log` vs
`run_after_live.log`):

| | before | after |
|---|---|---|
| distinct world objects tracked | 1 (all collided on `drop_id=1`) | 11 |
| names | `'RDEF'`, `'EF'`, `'F'`, `'66_ACTORDEF'` | `IT66_ACTORDEF`, `IT10804_ACTORDEF`, … |
| object_type | 0 for all 11 | 17/16/40/15/19/21/22 + 0 for the item |
| tradeskill containers detected | 0 | 10 |
| coordinates | e.g. `(1.29e19, 3.56e9, 0.0)` | match the DB row exactly |
| renderer click volumes registered | 0 | 10 |

### Acceptance — AT THE WIRE

Not accepted on "the struct is now 92 bytes". Accepted on a server round trip
in a live session:

```
Clicking object 190 ('IT66_ACTORDEF') type=17 (Forge)
Opening tradeskill container: drop_id=190 type=17 icon=1115 name=''
Clicking object 199 ('IT10804_ACTORDEF') type=17 (Forge)
Opening tradeskill container: drop_id=199 type=17 icon=1115 name='Half Elf Forge'
Clicking object 189 ('IT63') type=0 (Small Bag)
Publishing InventorySlotChanged ... itemId=13702 name='Wooden Fishing Tackle'
```

The client sends `OP_ClickObject` carrying the `drop_id` it parsed, and the
**server answers `OP_ClickObjectAction` naming the same object**, including the
container's real display name. That is the exact field whose breakage was most
user-visible, round-tripping. Clicking the plain ground item (type 0)
correctly opens no container — the server hands the item to inventory instead,
which is a second, independent confirmation that `drop_id` is right (the
server looked up the correct entity). The eqemu zone log carries no size
complaint for either opcode.

**No visual leg, and none is claimed.** `IrrlichtRenderer::addWorldObject()`
builds an invisible click AABB and nothing else — ground-spawn objects have no
mesh or scene node in this client, so there is no before/after screenshot that
could show this fix. The one screenshot in `review/T144/` is labelled context
only. It also shows the pre-load "instant scene" rather than qeynos geometry:
that is an **unrelated observation, not investigated here** and not a T144
regression (the headless runs in the same zone reached `[ZONE] qeynos loaded`
normally). Daylight was forced before capture per protocol.

### The regression test — the thing that was missing

`tests/test_packet_structs.cpp` gains 8 cases (2 layout, 6 behavioural), built
on the **real captured bytes** as `static const uint8_t[]` arrays — three
packets spanning the length range (95 / 104 / 107 body bytes). Expected values
are the DB's, so the assertions are anchored outside the client. The parse
under test is the production one: `ZoneProcessGroundSpawn`'s field reads were
extracted into `eqt::ParseGroundSpawn(body, body_len, out)` in
`src/client/world_object.cpp` precisely so a test could drive them without an
`EverQuest` instance, and `tests/CMakeLists.txt` now links that TU into
`test_packet_structs`.

This is the template T147 was asked to generalise. Note what it does that a
`sizeof` assert cannot: `Object_Struct_Size` would have passed at 100 bytes
too, with a different literal. `GroundSpawn_CapturedBytes_TradeskillForge`
could not.

### The minimum-length guard — what was chosen and why

The plan flagged that "a short object name legitimately produces a packet that
cannot reach `object_type`" and asked for a decision. **Measured against the
format, that case does not exist**, and the guard is written as pure defence
rather than as a normal path:

- body length is `strlen(name) + 91`, so the shortest possible body is 91
  bytes (empty name), and `object_type` at 80..83 is always present.
- only `spawn_id` (88..91) could be truncated, by one byte, in the empty-name
  case — and we do not read `spawn_id`.
- the `object_name` field is likewise never truncated: at least 84 body bytes
  means at least 40 remain from offset 44, more than the 32-byte field.

So `ParseGroundSpawn` requires 84 body bytes (through `object_type`) and
returns `false` below that, leaving the out-param untouched; the `std::min`
bound on the name scan is belt-and-braces against a malformed packet, and the
comments say so rather than implying a case that cannot occur. The old code's
`2 + 96` demand is gone, as is the `object_type = 0` fallback it needed — that
fallback was itself a symptom, silently defaulting the field the feature turns
on. Covered by `GroundSpawn_RejectsBodyTooShortForObjectType`.

### `WorldObject` field removal — no renderer blast radius

`size`, `solid_type`, `incline`, `tilt_x`, `tilt_y` **deleted**, not corrected:
they do not exist in the Titanium wire format at all (they come from eqemu's
server-internal struct), so every one of them was noise. A repo-wide search
found **zero consumers** outside the parse itself — the `incline`/`size` hits
elsewhere are `DoorState`/`Door_Struct`, an unrelated struct. The plan's STOP
condition ("removing them reaches into renderer code") did not trigger; the
removal is mechanical. A comment in `world_object.h` records why they must not
come back.

### Verification

| step | result |
|---|---|
| 1. `cmake --build build -j24` | PASS |
| 2. `ctest --test-dir build --output-on-failure` | **2445/2446**, sole failure the documented ALSA `AudioManagerTest.PreloadCommonSounds` |
| 3. `scripts/build-gles2-x86.sh` | PASS |
| 4. visual gate | **PASS**, goldens untouched (`git status` clean of any golden) |
| 5. `scripts/build-arm-noble.sh` | PASS |
| 6. `check_file_sizes.sh fable` (from slot1, on task branch) | 8 files checked, **0 FAIL**, 4 pre-existing WARNs (`packet_structs.h`, `command_processor.cpp`, `eq_entities.cpp`, `test_command_processor.cpp` — all already >1500 before this unit; `eq_entities.cpp` got *shorter*) |

Suite total 2446 vs the plan's 2433 baseline = exactly the 13 tests added
(8 packet-struct + 5 command-processor).

### Parallel-unit boundary (T145)

Held. `git diff include/common/packet_structs.h` is three hunks, all inside the
`Object_Struct` block and its `static_assert` (lines ~840–890). No reformat, no
reflow, nothing touched elsewhere in that file. `FaceChange_Struct`,
`EnvDamage2_Struct`, `GMKick_Struct`, `GMKill_Struct`, `ControlBoat_Struct`,
`PopupResponse_Struct`, `DisciplineTimer_Struct`, `src/client/eq_player_info.cpp`
and `src/client/eq_gm.cpp` are untouched.

### Deviations

1. **Added `/clickobject <drop_id>` (alias `/clickobj`) to
   `action::CommandProcessor`** — `include/client/action/command_processor.h`,
   `src/client/action/command_processor.cpp`, `tests/test_command_processor.cpp`.
   These three files are outside the plan's stated territory. Flagged, not
   absorbed.

   *Why it was necessary, not convenient:* the plan's acceptance bar is "a
   tradeskill container **actually opens** when clicked", and the plan's
   standing lesson is that dispatch-layer acceptance certifies dead features.
   But the **only** production trigger for `SendClickObject` was a renderer
   keypress — `HotkeyAction::Interact` → `RA::WorldObjectInteract` →
   `getNearestWorldObject()` → `WorldObjectInteractIntent` → `eq_bridge.cpp`.
   No headless, scripted, stdin or piped channel can reach it; `--script` only
   dispatches slash commands, and no registry had one. Without this
   registration the unit could have been accepted *only* at the dispatch layer
   — precisely what the plan forbids.

   *It also retires dead code.* `ActionDispatcher::clickWorldObject()` →
   `EqActionHandler::clickWorldObject()` → `EverQuest::SendClickObject()` was
   fully plumbed with **zero production callers** (tests only) — the
   reachability pattern again. The command is a thin wrapper over that existing
   chain; no new dispatcher method, no new handler virtual, no state-domain
   work. `ActionDispatcher::interactNearestWorldObject()` is still the stub it
   was (`"Use clickWorldObject with drop ID"`, TODO: needs an ObjectState in
   GameState) — deliberately left alone, as implementing it is a state-domain
   job, not this unit's.

   *No collision risk with T145*, which owns `eq_player_info.cpp` and
   `eq_gm.cpp`. 5 tests cover registration, alias, dispatch, argument
   rejection (empty / non-numeric / trailing junk / >u32) and the
   not-zone-connected path.

2. **`ParseGroundSpawn` extracted into `world_object.{h,cpp}`** rather than the
   field reads staying inline in `ZoneProcessGroundSpawn`. Both files are in
   the plan's territory; the move is what makes the plan's own required
   captured-bytes test possible (`test_packet_structs` cannot construct an
   `EverQuest`). `tests/CMakeLists.txt` gains one source line for it.

3. **Zone changed from the default `arena` to `qeynos`**, and `Testchar`'s
   position temporarily written to (1, -290, 172, 0) and restored to the
   protocol's canonical (36, -77.9232, 21.9086, 3, h=190) afterwards — a
   documented, sanctioned character-position write. `arena` has no `object`
   rows at all, so it could not have produced a single `OP_GroundSpawn`.
