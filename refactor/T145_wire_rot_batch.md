# T145 — Five dead features and four malformed senders

**Slot: slot2. x86. NO eqemu lock (T144 holds it). No Pi.**

**Territory: `include/common/packet_structs.h` (ONLY the seven structs named
below), `src/client/eq_player_info.cpp`, `src/client/eq_gm.cpp`, `tests/`,
`refactor/`.**

**T144 runs in parallel and also edits `include/common/packet_structs.h`.** It
owns the `Object_Struct` block (~line 846) and `src/client/eq_entities.cpp`.
**Touch neither.** Confine your header edits to the seven struct definitions
below and their asserts. Do not reflow, reorder, reformat, or "tidy" any other
part of that file — a whitespace sweep turns a clean parallel merge into a
conflict. `refactor/T147_*` will do the file-wide cleanup afterwards; leave it
for that unit.

Read T133's and T134's Reviews first. This unit is the same bug class as both,
found by the sweep they motivated.

## Part A — four more double-opcode-prefix senders

`EverQuest::QueuePacket(uint16_t opcode, DynamicPacket*)` (`eq.cpp:3855`)
ALWAYS prepends the opcode itself, so callers must pass a **body-only** packet.
The 1-arg `m_zone_connection->QueuePacket(pkt)` is the opposite convention and
is not involved here. A sweep of all 43 two-arg call sites found 39 correct and
**4 broken, all in `src/client/eq_player_info.cpp`**:

| function | line | opcode | effect |
|---|---|---|---|
| `SendCamp` | 899 | `HC_OP_Camp` | 4 bytes sent, server expects 2 |
| `SendLogout` | 911 | `HC_OP_Logout` | same |
| `SendWhoAllRequest` | 1042 | `HC_OP_WhoAllRequest` | size=154, expected 152 |
| `SendInspectRequest` | 1125 | `HC_OP_InspectRequest` | size=10, expected 8 |

`SendCamp`/`SendLogout` each carry a comment saying "OP_Camp is an empty packet
- just the opcode" immediately above a line that writes the opcode into the
body. `Packet::PutInt16` (`src/common/net/packet.cpp:18-26`) silently auto-
resizes, so the "empty" packet is 2 bytes. **Fix: delete the `PutUInt16` line.**
The bare `DynamicPacket` is then zero-length, matching `SendGetGuildMOTD`
(`eq_guild.cpp:159-161`).

`SendWhoAllRequest`/`SendInspectRequest` are byte-for-byte the T134
`SendRezzAnswer` shape — `Resize(2 + sizeof(S))`, opcode at 0, `memcpy` to +2.
**Fix exactly as `770ca383` did:** `Resize(sizeof(S))` then
`PutData(0, &s, sizeof(S))`, deleting the opcode write.

`SendLogout` is reached on every successful `/camp` (called from the camp-timer
completion at `eq_player_info.cpp:888`), so this fires in normal play.

**`/who` is on this list, and memory records T82 as having "proved /who works."**
It cannot have — the packet has never been well-formed. Note this in the Review;
it is the third feature-family where dispatch-layer acceptance certified a
corpse, and that pattern is now the programme's dominant lesson.

## Part B — five structs whose passing `static_assert` hid a dead feature

Every one of these has a `static_assert` that passes at the wrong value. A size
assert catches neither field drift nor a wrong-but-self-consistent total — that
is exactly how `Resurrect_Struct` survived at 224.

**Verify each against eqemu yourself before changing it.** Ground truth:
`/mnt/projects/claude/EQEmu/common/patches/titanium_structs.h` and
`/mnt/projects/claude/EQEmu/common/eq_packet_structs.h`. The disambiguation
rule, established by this sweep: **`titanium.cpp` is the arbiter** — if an
opcode has an `ENCODE`/`DECODE` there, the `Titanium::structs::` version is the
wire format; otherwise the base `eq_packet_structs.h` struct goes on the wire
raw and the zone handler's size check is the gate.

1. **`FaceChange_Struct`** (`:1794`, ours 8, eqemu `titanium_structs.h:1860` is
   **7**). We appended an invented `unused` padding byte. `DECODE(OP_FaceChange)`
   (`titanium.cpp:2779`) uses `DECODE_LENGTH_EXACT`, which on mismatch logs and
   sets the opcode to `OP_Unknown` (`common/patches/ss_define.h:166-171`).
   **Every `/face` is discarded before reaching the handler** while
   `SendFaceChange` (`eq_player_info.cpp:1826`) logs local success. Delete the
   byte; assert 7. (Memory records T82 as having proved `/face` works. It did
   not.)

2. **`EnvDamage2_Struct`** (`:1595`, ours 32, eqemu `titanium_structs.h:2251` is
   **31**). eqemu has `uint16 unknown4` @4 then `uint32 damage` @6; ours has
   `uint16 damage` @4. `dmgtype` is @22 not @16; `constant` is `uint16` @27 not
   `uint8` @31. Our guard at `eq_player_info.cpp:1519` demands
   `>= 2 + 32 = 34`; real packets are 33, so **every fall/drown/lava/trap damage
   packet is rejected** and those messages never appear. Adopt eqemu's field
   list verbatim; assert 31; fix the guard.

3. **`GMKick_Struct`** (`:1921`, ours 132, eqemu `titanium_structs.h:2042` is
   **129**) — we invented a `padding[3]`. `Handle_OP_GMKick`
   (`zone/client_packet.cpp:6729`) does a bare `return` on size mismatch with
   **no log line at all**, and `Handle_OP_GMKill` (:6760) uses the same struct.
   So `/kick` and `/kill` (`eq_gm.cpp:86`, `:103`) log local success and do
   nothing server-side, leaving zero trace. Delete the padding; assert 129.
   Check `GMKill_Struct` for the same invented padding while you are there.

4. **`ControlBoat_Struct`** (`:1528`, ours 20, eqemu `eq_packet_structs.h:5327`
   is **8**) — wrong size *and* wrong semantics. eqemu is
   `uint32 boatId; bool TakeControl; char unknown[3]`. Our `heading` and `type`
   fields do not exist; the real field is a single take-control/release
   boolean. `Handle_OP_ControlBoat` rejects with a visible
   `"Wrong size: OP_ControlBoat, size=[20], expected [8]"`. Rewrite; assert 8;
   and rework the sender at `eq_player_info.cpp:1407`, which currently sends a
   heading it has no field for. **If reworking that sender needs a behavioural
   decision rather than a mechanical change, STOP and report** — do not invent
   boat-steering semantics.

5. **`PopupResponse_Struct`** (`:1657`, ours 12, eqemu `eq_packet_structs.h:1903`
   is **8**). eqemu has `unknown0000` + `popupid` and **no third field** — the
   button is not sent. Our `response` field is fictional. Quest/GM popup buttons
   do nothing. Drop `response`; assert 8; fix `SendPopupResponse`
   (`eq_player_info.cpp:1642`).

Also, low severity but do it while you are in the file:
**`DisciplineTimer_Struct`** (`:1617`, ours 8, eqemu `eq_packet_structs.h:5418`
is **12** — a trailing `uint32 Unknown08`). Our read guard is `<` not `!=` and
both consumed fields are at correct offsets, so nothing is broken today. Add the
field, assert 12. It is a landmine only if this struct is ever used to send.

## Do NOT rename the merchant pair

`Merchant_Purchase_Struct` (24B, `:1151`) and `Merchant_Sell_Struct` (16B,
`:1172`) are **inverted relative to eqemu's names** — but both our wire paths
are correct, because eqemu's own naming is counterintuitive:
`Handle_OP_ShopPlayerBuy` (`client_packet.cpp:14026`) expects
`Merchant_Sell_Struct` (24) and `Handle_OP_ShopPlayerSell` (:14274) expects
`Merchant_Purchase_Struct` (16). A future cleanup that "fixes" the names to
agree with eqemu breaks both merchant directions at once. **Add a comment in
the header saying exactly that**, so the next reader does not tidy it.

## Tests

Per-struct layout tests using `offsetof`, not just `sizeof` — a size assert is
what failed here. For each of the seven structs, assert the offset of every
named field against the eqemu-derived value. Plus serialization tests for the
four Part A senders asserting the exact byte length and that byte 0 of the body
is **not** the opcode low byte.

T144 is building a captured-bytes test for `Object_Struct` from a real hex dump.
You have no eqemu lock, so you cannot capture; `offsetof` tests are your gate.
Say so plainly in the Review rather than implying wire verification.

## Acceptance

This unit ends **without live wire verification** — T146 does that leg once
T144 releases the lock. Be explicit about it: these fixes are verified against
eqemu source and by layout tests, **not observed on the wire**. Do not write
"/face works now." Write what you actually established.

## Verification

Full protocol bar (build, ctest, GLES2 build, visual gate goldens untouched,
ARM cross-compile, `check_file_sizes.sh` from your slot on your task branch).
Suite baseline 2433, sole expected failure the documented ALSA
`AudioManagerTest.PreloadCommonSounds`.

## STOP conditions

- Any struct where your own eqemu reading disagrees with the numbers above —
  trust your reading, report the discrepancy, and do not change that struct.
- `ControlBoat_Struct`'s sender needing invented semantics (see above).
- A fix rippling beyond the named territory.

## Review

(executor fills)
