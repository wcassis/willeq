# T349 — `OP_Save`: the 192-byte body and its place in the self-death burst (B059)

Class A. Slot1. `PlayerComponent::SendSave` queues a bare 2-byte `OP_Save`;
the official client's is 192 bytes and it is the FIRST C→S packet of every
self-death burst (T311 capture, four deaths agree), before the Lying
appearance. fable sends none from the death path.

## Measure

Decode the 192-byte body from `review/T311/legA/decode/` (the pcap or the
per-packet dumps): diff the four instances against each other (what
varies per death, what is constant) and against the character's state
at each moment (position? the `Save_Struct` in `include/common/packets/`
if one exists; eqemu's `zone/client_packet.cpp` `Handle_OP_Save` in
`/mnt/projects/akk-stack/code/` says what the server reads — cite it as
an absolute path). Establish which fields the server actually consumes;
the rest are filled to match the oracle's bytes.

## Fix

1. `SendSave()` builds the 192-byte body (a packed struct in
   `include/common/packets/`, `static_assert`ed to 192, per the J14
   convention).
2. `ZoneProcessDeath`'s self branch sends it first, before the Lying
   appearance (T346's step 1). The self-death golden
   `tests/goldens/self_death_outbound_g07.txt` learns the new first line
   IN PLACE (T337 shape), both faults re-planted.
3. Test: the struct's layout against the captured bytes (a golden of one
   captured `OP_Save` body with the per-death-varying fields masked);
   `SendSave` after a replayed death produces a 192-byte packet matching
   it. Pre-fix failure pasted.

Territory: `src/client/player/player_misc.cpp`, `src/client/player/player_state.cpp`
(`ZoneProcessDeath` self branch — T346's territory, merged), the new
packet struct header, the goldens (in place), tests, `refactor/BOARD.md`.
No graphics files.

STOP: the body's semantics cannot be established from the capture and
the server source (then report what is known and leave it).

## Review

Branch `task/T349-op-save-body` off `fable` at `920280dd`.

### The four bodies, diffed

The opcode-timeline TSV cannot do this job -- it keeps only the first 32 of
the 192 payload bytes. The full bodies are in the SERVER-SIDE packet logs,
`review/T311/legA/zonelogs/qeynos2_zone_packetlog.log` (one death) and
`qeynos_bind_zone_packetlog.log` (three), each logged as `Size [194]` = the
2-byte opcode plus 192 body bytes. `DumpPacketToString` prints Size-2 bytes,
so all four dumps are exactly 192 long.

Byte-by-byte across the four:

| offsets | across the four | what it is |
|---|---|---|
| 000..003 | all four differ | unknown; no correlation to anything in the capture |
| 004 | `15` in all four | unknown constant |
| 005..008 | differs (634.4550 / 57.0 x3) | **server y** |
| 009..012 | differs (-160.0627 / -510.0 x3) | **server x** |
| 013..016 | differs (3.750999 / 31.752995 / 31.750992 x2) | **wire-frame z** |
| 017..018 | differs (`e0 01` / `00 00` x3) | unknown |
| 019..048 | zero in all four | -- |
| 049..052 | `99 29 00 00` in all four | unknown constant 0x2999 |
| 053..056 | `30 00 00 00` in all four | unknown constant 48 |
| 057..109 | zero in all four | -- |
| 110..174 | differs wildly | **uninitialized client memory** |
| 175..176 | zero in all four | -- |
| 177..180 | `99 29 00 00` | unknown constant 0x2999 (again) |
| 181..184 | `30 00 00 00` | unknown constant 48 (again) |
| 185..188 | `37 00 00 00` | unknown constant 55 |
| 189..191 | zero in all four | -- |

**The position triple is the one thing that decodes, and it decodes exactly.**
For every one of the four deaths, offsets 5/9/13 are BYTE-IDENTICAL to the
y/x/z of the last C->S `OP_ClientUpdate` before that death -- z bit pattern
included (`60 10 70 40` = 3.750999 in the qeynos2 death; `22 06 fe 41` =
31.752995 and `08 02 fe 41` = 31.750992 in two of the qeynos ones, which is
how we know it is not a rounded re-computation). Same y,x,z ORDER as
`BecomeCorpse_Struct` (`/mnt/projects/akk-stack/code/common/
eq_packet_structs.h:1378`), and the `OP_BecomeCorpse` the server sent one
packet earlier carries the same three floats. `t349_op_save_golden.py`
re-checks this against the logs on every run and exits non-zero if any of the
four fails it.

**It is the DEATH position, not the bind position.** The burst order is
`OP_Death`, `OP_ZonePlayerToBind`, then `OP_Save`, so the naive reading is
that the client has already moved to bind. It has not: death 1's `OP_Save`
carries (634.455, -160.063, 3.751) -- where the character was standing in
qeynos2 -- while the `OP_ZonePlayerToBind` it had just received named
(-510, 100, 31.75) in South Qeynos. The three qeynos deaths agree only
because the character died standing at (-510, 57, 31.75), near its own bind
point. One sample would have read this backwards; four did not.
(In passing: the 33-byte `OP_ZonePlayerToBind` body decodes as zone_id 1,
instance 0, x -510.0, **y 100.0**, z 31.75 -- `test_self_death_sequence.cpp`'s
comment on those bytes says "y 57.0", which is the CLIENT's y at the time, not
the packet's. Left alone; it is a comment on a byte array this unit did not
touch and no assertion reads it.)

**Offsets 110..174 are uninitialized client memory.** They hold 32-bit values
that read as Windows heap and image pointers (`0x1c832998`, `0x1c556920`,
`0x01d88978`, `0xffffffff`), they differ in every sample, and the fourth
death does not even share the FIELD SHAPE of the other three -- where deaths
1-3 have `4d d8 01 38 00 00 00 ff 7f 9b 1a` at 108..118, death 4 has zeros.
Four bytes inside that region (113, 128, 136, 152) happen to agree across the
four; the golden masks the region WHOLESALE rather than per byte, because
those are stale stack that did not move, and emitting them as constants would
have made fable copy four arbitrary numbers out of another process's stack.

### What the server reads: nothing

`/mnt/projects/akk-stack/code/zone/client_packet.cpp:13661`

```
void Client::Handle_OP_Save(const EQApplicationPacket *app)
{
	// The payload is 192 bytes - Not sure what is contained in payload
	Save();
	return;
}
```

`Save_Struct` is `uint8 unknown00[192]` in both
`/mnt/projects/akk-stack/code/common/eq_packet_structs.h:5039` and the
Titanium copy at `common/patches/titanium_structs.h:3203`, there is no
titanium.cpp DECODE for `OP_Save` (only rof/rof2/sof/sod/uf have one, and
those are pure `DECODE_LENGTH_EXACT` length checks), and the handler does not
check `app->size` either. So the SERVER-VISIBLE contract is the opcode plus
192 bytes, and nothing more. Every field name in `EQT::Save_Struct` comes
from the capture, and the header says so.

The STOP was "the body's semantics cannot be established from the capture and
the server source". It **did not fire, but only partly**: one field group is
established to the byte (the position), the server source establishes that
nothing else matters, and the remainder is reported as unknown rather than
invented. The honest caveat, recorded in the struct comment, the golden
header and here: all four samples come from ONE client process, so an
observed "constant" may be process-lifetime-stable stack content rather than
a real field. Since the server reads none of it, the distinction has no
server-visible consequence.

### The change

- **`include/common/packets/character.h`** -- new `EQT::Save_Struct`, packed,
  `static_assert(sizeof(...) == 192)` (J14 convention: the domain header opens
  and closes its own `#pragma pack` region, the umbrella is untouched), plus
  six `kSaveUnknown*` constants so the sender and the layout test name the
  observed values once.
- **`src/client/player/player_misc.cpp`** -- `SendSave(bool announce = true)`
  builds the body. The position uses the SAME axis swap and the SAME
  wire-frame z expression as `SendMovementUpdate`, `zAwaitingFrameConversion`
  guard included: PosX is the server's y, PosY the server's x, and PosZ is the
  RENDER frame unless the self-spawn's raw wire z has not been converted yet
  (T311 -- converting it twice compounds +0.625 per login).
  It also now sends through **`ctx_.net.SafeQueueZone`** instead of
  `ctx_.zone_connection->QueuePacket`: that is the sender the T268/T346
  ordered capture seam and the session recorder both watch, and this unit's
  golden compares OP_Save's POSITION in the burst. A direct `QueuePacket` is
  invisible to both -- visible in the pre-fix probe below, where
  `SaveBody_MatchesTheCaptureGolden` reports `countOpcode(HC_OP_Save) 0`
  even though the old code did queue something.
- **`src/client/player/player_state.cpp`** -- `ZoneProcessDeath`'s self branch
  calls `SendSave(/*announce=*/false)` FIRST: before
  `ApplyPositionState(POS_DEAD)` as well as before the Lying appearance,
  because the body carries our current position and the oracle's carries the
  position we died at. `announce=false` because the burst prints nothing;
  "Saving character..." belongs to the `/save` command, whose call site is
  unchanged.

### The goldens

**`tests/goldens/self_death_outbound_g07.txt` learned its new first line IN
PLACE** (T337 shape): `scripts/refactor/t311_death_golden.py` gained `OP_Save`
in `COMPARED` and was re-run over the same TSV, so the file is still generated
from the capture and was not hand-edited. Its "NOT compared" note for
`OP_Save` -- which said fable's was a bare 2-byte opcode -- is replaced by a
pointer to the body golden. The four windows still agree (the script exits
non-zero otherwise). New content:

```
C->S OP_Save
C->S OP_SpawnAppearance type=14 param=115
C->S OP_ZoneChange
```

**`tests/goldens/op_save_body_g08.txt`** is new: one cell per body byte, 16 a
line, produced by `scripts/refactor/t349_op_save_golden.py` from the two
zonelogs. `yy`/`xx`/`zz` mark the position triple (compared -- the test
supplies the position and the bytes must come back), `..` marks a byte not
compared (the 110..174 noise region, plus 0..3 and 17..18), and every hex cell
is compared verbatim.

**Faults planted and caught (both verified, then reverted):**

- one golden cell perturbed (`048: 00 99 29` -> `00 98 29`) ->
  `OP_Save body byte 49 diverges from the oracle`, `Which is: 153` vs
  `expected 152`;
- one line appended to `self_death_outbound_g07.txt` -> the size assertion
  fires with its do-not-re-bless message (`expected.size() 4 vs 3u`).

### Pre-fix failure

Production reverted to the pre-fix `SendSave` (bare 2-byte opcode, direct
`QueuePacket`) with no death-path call, tests unchanged:

```
[  FAILED  ] SelfDeathSequence.SelfDeath_SendsThe192ByteSaveFirst
  h.sentPackets()[0].opcode          Which is: 31794   (HC_OP_SpawnAppearance)
  static_cast<uint16_t>(HC_OP_Save)  Which is: 29547
  OP_Save is the FIRST C->S packet of the oracle's self-death burst
  h.sentPackets()[0].bytes.size()    Which is: 8
  192u                               Which is: 192
  h.countOpcode(HC_OP_Save)          Which is: 0   vs 1u

[  FAILED  ] SelfDeathSequence.SaveBody_MatchesTheCaptureGolden
  h.countOpcode(HC_OP_Save)          Which is: 0   vs 1u

[  FAILED  ] SelfDeathSequence.T311Capture_OutboundSequenceMatchesTheOracle
  actual.size()   Which is: 2        expected.size()   Which is: 3
  fable's outbound death sequence:
    C->S OP_SpawnAppearance type=14 param=115
    C->S OP_ZoneChange
```

`PacketStructsTest.Save_Struct_Layout` and
`Save_Struct_DecodesTheCapturedPosition` have no pre-fix form: the struct did
not exist, so the pre-fix tree does not compile them. Their value is the T145
one -- `offsetof` on every named field, not just `sizeof`, so a 192-byte
struct whose fields drifted would still be caught.

### Step results

- `cmake --build build -j24` -- **exit 0** (`BUILD_EXIT=$?`), zero `error:`
  lines. Two runs hit the 600 s tool timeout mid-build (exit 143); after each,
  zero-byte files under `build/bin` were deleted (`test_setheading_conversion`,
  `test_profile_appearance`, `test_zone_out_handshake` the first time, none the
  second), `ar t` re-counted the two archives (`libwilleq_core.a` 321 members,
  `libEtcLib.a` 15), and the build was resumed to a clean exit.
- `ctest --test-dir build -j6 --output-on-failure` -- **3,840 of 3,842
  passed**. This unit adds 5 tests (`test_packet_structs` 42 -> 44,
  `test_self_death_sequence` 9 -> 12); the rest of the delta from T346's
  3,831 arrived on `fable` before this branch point. Two failures, neither
  this unit's:
  - `AudioManagerTest.PreloadCommonSounds` -- the expected ALSA host quirk.
  - `visual_gate` -- `befallen: p95 process-CPU ms 10.949 (ceiling 10.948)`,
    a 0.001 ms miss under parallel-ctest CPU contention. **Re-run alone it
    PASSES** (`ctest -R '^visual_gate$'` -> `Passed 36.40 sec`), and this
    unit touches no graphics file.
- Named suites re-run individually, all PASS: `test_self_death_sequence` 12,
  `test_packet_structs` 44, `test_wire_layout_t145` 11.
- `bash scripts/refactor/check_file_sizes.sh --all` -- `checked 879 file(s):
  879 ok, 0 warn, 0 fail`.
- gles2 / visual-gate-specific / ARM runs **skipped: no graphics file is
  touched.** `git diff --name-only $(git merge-base fable HEAD)..HEAD`:
  `include/client/player/player_component.h`,
  `include/common/packets/character.h`,
  `scripts/refactor/t311_death_golden.py`,
  `scripts/refactor/t349_op_save_golden.py`,
  `src/client/player/player_misc.cpp`,
  `src/client/player/player_state.cpp`,
  `tests/goldens/op_save_body_g08.txt`,
  `tests/goldens/self_death_outbound_g07.txt`,
  `tests/test_packet_structs.cpp`, `tests/test_self_death_sequence.cpp`,
  plus the two documents (`refactor/BOARD.md`, `refactor/T349_op_save_body.md`).

### Live proof -- ~~owed~~ RUN BY T358 (2026-09-05), `review/T358/`

~~Not run; no eqemu lock was held by this unit and slot3 was described as
running a live measurement. Owed, and cheap: die once against a real server
and confirm the zone log shows `[Packet C->S] [OP_Save] [0x736b] Size [194]`
followed by `Save for [<char>] took [...]` -- the server-side line that proves
it was accepted -- immediately before the Lying `OP_SpawnAppearance`.~~

Done, and the prediction held to the letter, from the qeynos zone process' own
packet log (`review/T358/logs/server_qeynos_zone_packetlog.log`):

    18:57:12  C->S  OP_SpawnAppearance  Size [10]    0F 01 0E 00 73 00 00 00
    18:57:12  C->S  OP_Save             Size [194]   00 00 00 00 15 F6 28 64 42 00 00 FF C3 0F 5C 12 42 ...
              [Zone] [Info] Save for [Testchar] took [0.01101253]

`Size [194]` = 2 opcode + 192 body; `unknown004 = 0x15` as decoded here; the
floats are `F6 28 64 42` = 57.04 (server y) and `00 00 FF C3` = -510.0 (server
x), **byte-for-byte the last pre-death C->S `OP_ClientUpdate`'s y/x**, which is
the oracle property this unit established.

**ONE DEVIATION, stated:** this Review says `OP_Save` is the FIRST C->S packet of
the burst. Live, the server receives the **Lying appearance first** and `OP_Save`
second. The client's own log puts them in the intended order in the same
millisecond (`Character save requested` then `Sent SpawnAppearance`), so the
reversal is on the wire: `SendSave` goes through `PacketSender::SafeQueueZone`,
which defers to `QueuePacketAsync` off the main thread, while
`SendSpawnAppearance` queues synchronously and overtakes it. Boarded as **B068**
together with the recorder gap that hid the appearance from `--record-session`.

### For the board / next unit

Two loose threads, neither a defect and neither raised as a board item:

1. `test_self_death_sequence.cpp`'s comment on `kT311ZonePlayerToBindBody`
   labels the bind y as 57.0; the bytes say 100.0 (57.0 was the CLIENT's y at
   that moment). No assertion reads the field, so nothing is wrong in the
   tree, but the next person to decode that array should not trust the
   comment.
2. `renderZToWireZ` with the `zAwaitingFrameConversion` guard is now written
   out at three call sites (`movement_update.cpp`, `spell_cast_checks.cpp`,
   `player_misc.cpp`). A one-line helper in `position_frames.h` would
   consolidate it; not done here because `movement_update.cpp` is outside this
   unit's territory.
