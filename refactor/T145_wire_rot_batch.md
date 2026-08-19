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

Executed in `/mnt/projects/fable/wt/slot2`, task branch
`task/T145-wire-rot-batch`, off `fable` tip `857ddc46`.
**NO eqemu lock (T144 held it). Nothing in this unit was observed on the
wire.** Everything below is verified against eqemu source and by `offsetof`
layout tests. T146 closes the wire leg.

### THE HEADLINE FINDING WAS NOT IN THE PLAN

`include/common/packet_structs.h` opens `#pragma pack(push, 1)` at **line 34**
and pops it at **line ~1375** — roughly **700 lines before EOF**. Every struct
below that point is compiled with **natural alignment, not packed**. That
region contains all six of this unit's Part B structs and every Guild struct.

This surfaced the moment `EnvDamage2_Struct` was given eqemu's correct field
list: it measured **36 bytes, not 31**, and the new `static_assert` failed the
build. The old 32-byte layout passed its assert only because that particular
mix of fields happened to be alignment-neutral at 32.

**This is very likely the root cause of the wire-rot class this arc is
chasing.** A struct written correctly against eqemu, dropped into the unpacked
region, silently gains padding; the author then "fixes" the size assert to
whatever the compiler produced, and the result is a self-consistent,
permanently-passing assert on a wrong wire layout. That is exactly the shape of
four of the five defects in this batch (an invented `unused` byte, an invented
`padding[3]`, an `unused`-padded 8 for a 7-byte struct, a 12 for an 8).

**Bounded for T147, with the risk already measured.** Fixed here with a
**local** `#pragma pack(push,1)`/`pop` around `EnvDamage2_Struct` only —
moving the file-level pop to EOF changes the layout of every struct in a
700-line region at once and is a file-wide audit, i.e. T147's job, and would
also have collided with T144's concurrent edits. To de-risk that job I ran the
experiment anyway in scratch: a copy of the header with the `pop` moved to EOF
(whole tail region packed) **compiles clean — every `static_assert` below line
1375 still holds under `pack(1)`**. So the missing pack is currently *latent*
everywhere except `EnvDamage2_Struct`, and moving the pop to EOF is a safe,
assert-verified change for T147 rather than a speculative one.

### Verification of the plan's numbers against eqemu — all seven confirmed

Read directly at `/mnt/projects/claude/EQEmu/`. **No discrepancies in any
size, offset, or line number; no struct was skipped under the STOP
condition.** The arbiter rule (`titanium.cpp` has an ENCODE/DECODE ⇒
`Titanium::structs::` is the wire format; otherwise the base
`eq_packet_structs.h` struct goes raw) was applied per struct and recorded:

| struct | ours (before) | eqemu | eqemu location | arbiter | server-side gate |
|---|---|---|---|---|---|
| `FaceChange_Struct` | 8 | **7** | `titanium_structs.h:1860` | `DECODE(OP_FaceChange)` `titanium.cpp:2779` ⇒ Titanium wins | `DECODE_LENGTH_EXACT` ⇒ opcode rewritten to `OP_Unknown` |
| `EnvDamage2_Struct` | 32 | **31** | `titanium_structs.h:2251` **==** `eq_packet_structs.h:3020` | no ENCODE/DECODE ⇒ raw | `Handle_OP_EnvDamage` exact check, `client_packet.cpp:6301` |
| `GMKick_Struct` | 132 | **129** | `titanium_structs.h:2042` **==** `eq_packet_structs.h:2766` | no ENCODE/DECODE ⇒ raw | `Handle_OP_GMKick` **bare `return`, no log**, `:6729` |
| `ControlBoat_Struct` | 20 | **8** | `eq_packet_structs.h:5327` | no ENCODE/DECODE ⇒ raw | `Handle_OP_ControlBoat` visible "Wrong size", `:5421` |
| `PopupResponse_Struct` | 12 | **8** | `eq_packet_structs.h:1903` | no ENCODE/DECODE ⇒ raw | `Handle_OP_PopupResponse` `:11976` |
| `DisciplineTimer_Struct` | 8 | **12** | `eq_packet_structs.h:5418` | n/a (receive only) | our own read guard is `<`, so benign today |
| merchant pair | 24 / 16 | **24 / 16** | `titanium_structs.h:1704`/`:1712` | n/a | `:14026` / `:14274` — **both already correct, no rename** |

Two arbitration calls worth recording because they could have gone wrong:

- **`FaceChange_Struct`**: the base `eq_packet_structs.h:2558` version is **24
  bytes** (Drakkin heritage/tattoo/details + entity_id). Taking that as ground
  truth would have made `/face` *worse*. `titanium.cpp` has the DECODE, so the
  7-byte Titanium struct is the wire format. The arbiter rule earned its keep
  here.
- **`EnvDamage2_Struct` and `GMKick_Struct`**: `titanium.cpp` has **no**
  ENCODE/DECODE for either opcode, so by the rule the base struct is
  authoritative — but I checked both anyway and the base and Titanium versions
  are byte-identical, so the plan's `titanium_structs.h` line references give
  the same answer.

`GMKill_Struct`: checked as the plan asked. We have **no** `GMKill_Struct` —
`eq_gm.cpp`'s `SendGMKill` reuses `GMKick_Struct`. eqemu's `GMKill_Struct` has
the identical three fields, so that reuse is correct and the 129-byte fix
covers `/kick` and `/kill` together.

### Part A — the four malformed senders, independently re-swept

Confirmed `EverQuest::QueuePacket(uint16_t, DynamicPacket*)` (`eq.cpp:3855`)
always prepends the opcode, so its callers must pass a body-only packet. My own
sweep of the two-arg call sites found **exactly the four the plan named, and no
others** — every other `Resize(2 + sizeof(S))` site in `src/client/` (`eq_group.cpp`,
`eq_merchant.cpp`, `eq.cpp:3602`, `eq_player_info.cpp:696`) uses the *other*,
correct convention: `m_zone_connection->QueuePacket(p, 0, true)`, where the
packet legitimately carries its own opcode and no second prefix is added.

| function | opcode | before | after | server-side check |
|---|---|---|---|---|
| `SendCamp` | `HC_OP_Camp` | 4B (2B body: the opcode) | **2B, zero-length body** | `Handle_OP_Camp` `:4269` — *no size check* |
| `SendLogout` | `HC_OP_Logout` | 4B | **2B, zero-length body** | `Handle_OP_Logout` `:10264` — *no size check* |
| `SendWhoAllRequest` | `HC_OP_WhoAllRequest` | 154 | **154 → 2+152** | `DECODE_LENGTH_EXACT(structs::Who_All_Struct)`, `titanium.cpp:3312` |
| `SendInspectRequest` | `HC_OP_InspectRequest` | 10 | **2+8** | `DECODE_LENGTH_EXACT(structs::Inspect_Struct)`, `titanium.cpp:2848` |

Camp/Logout fixed by deleting the `PutUInt16` line (the comment "OP_Camp is an
empty packet - just the opcode" sat directly above the line that made it
non-empty; `Packet::PutInt16` silently auto-resizes). Who/Inspect fixed exactly
as `770ca383` did for `SendRezzAnswer`: `Resize(sizeof(S))` + `PutData(0, ...)`.

**A severity correction to the plan, in both directions:**

- `SendCamp`/`SendLogout` were **less** broken than the plan implies.
  `Handle_OP_Camp` and `Handle_OP_Logout` have **no size check at all** — they
  ignore the payload entirely. So `/camp` has been working despite sending 4
  bytes for a 2-byte packet. This was malformed, not dead. Fixed anyway; do not
  credit this unit with repairing `/camp`.
- Our `Who_All_Struct` (152) and `Inspect_Struct` (8) were **already correct**
  against eqemu's Titanium structs. Only the senders were malformed. Note
  eqemu's `Who_All_Struct` carries a stale `// 76 length total` comment; the
  fields are `char[64] + 6×uint32 + uint8[64]` = **152**.

### `/who` and `/face`: the third and fourth dispatch-layer-certified corpses

`SendWhoAllRequest` produced a 154-byte packet where `DECODE_LENGTH_EXACT`
demands 152; on mismatch the DECODE logs and rewrites the opcode to
`OP_Unknown` (`ss_define.h:165-171`), so **`Handle_OP_WhoAllRequest` was never
reached**. `SendFaceChange` produced 8 bytes where the DECODE demands 7, same
outcome. Both senders logged local success the whole time.

**Memory records T82 as having "proved /who works" and "proved /face works".
It cannot have proved either** — neither packet has ever been well-formed, and
the failure mode is silent on the client side by construction. This is the
third and fourth feature-family where dispatch-layer acceptance
(`registry=action`, a local success log) certified a corpse. T132→T133 was the
first, T134's `SendRezzAnswer` the second. **The lesson is now the programme's
dominant one: a local "sent OK" log is not evidence, and neither is a
dispatch-layer test.** The only acceptance bar that has ever caught this class
is a server-side observation.

### Two findings beyond the plan, both flagged not absorbed

**1. `OP_EnvDamage` is CLIENT→SERVER. Our receive path is unreachable dead
code, and fixing the struct does not resurrect anything.**

The plan says "every fall/drown/lava/trap damage packet is rejected and those
messages never appear," implying a server→client path blocked by our
too-strict guard. The numbers in the plan are right and the guard really was
wrong (it demanded `>= 2+32 = 34` for a 33-byte packet). But the direction is
not: `OP_EnvDamage` is registered in eqemu's `ConnectedOpcodes`
(`zone/client_packet.cpp:198`) and handled by `Client::Handle_OP_EnvDamage` —
**the server never sends it**. A repo-wide grep of eqemu finds no outbound
`OP_EnvDamage` anywhere. So `ZoneProcessEnvDamage` cannot fire against an
eqemu server no matter what size the struct is, and this client has **no
`OP_EnvDamage` sender at all** — environmental damage is the *client's* job to
report, and we never do.

The struct and the guard are now correct (the guard needed no edit — it is
`2 + sizeof(S)` and follows the struct), and the path would parse correctly if
something sent it. **But nobody should read this unit as having restored fall
damage messages.** The real gap is a missing sender; that is a feature, not a
rot fix, and is out of scope here. Recorded in a header comment and in the
test file.

**2. The `dmgtype` value mapping was inverted.** `ZoneProcessEnvDamage` mapped
`250=falling, 252=burning, 253=lava`. eqemu's
`EQ::constants::EnvironmentalDamage` (`common/emu_constants.h:308`) is
`250=Lava, 251=Drowning, 252=Falling, 253=Trap` — three of four wrong, and
"burning" is not a category at all. Corrected in the same function. Latent
today for the reason above, but it would have produced wrong text the moment
the path became live.

### `ControlBoat_Struct` — the STOP condition was evaluated and did not fire

The plan says to STOP if reworking the sender needs a behavioural decision.
It does not, for two reasons:

1. **`SendControlBoat` has zero callers.** Grep across `src/`, `include/`, and
   `tests/` finds only the definition (`eq_player_info.cpp:1398`) and the
   declaration (`eq.h:1058`). Nothing invokes it — **the reachability pattern
   again, at #10.** So no existing call site constrains the signature.
2. **eqemu's struct is unambiguous** and leaves nothing to invent: `uint32
   boatId; bool TakeControl; char unknown[3]`. Our `heading` and `type` were
   the invented semantics; removing them is subtraction, not design. Boat
   heading is simply not carried by this opcode.

Signature changed to `SendControlBoat(bool take_control)`. Flagged here as a
deviation-shaped change (a public signature moved) even though it is
mechanical and unreachable.

### `PopupResponse_Struct` — the `button` parameter is kept but not sent

eqemu's struct has `unknown0000` + `popupid` and no third field; the handler
dispatches on the popup id alone. `SendPopupResponse(popup_id, button)` keeps
its `button` parameter for source compatibility with its callers but
explicitly discards it, with a comment saying so, rather than silently
changing an API. If a caller genuinely needs to distinguish buttons, that is a
protocol question for a later unit — it cannot be answered by sending a field
the wire does not have.

### Do NOT rename the merchant pair — comment added, plus a test

Verified from both ends before writing the comment. eqemu:
`Handle_OP_ShopPlayerBuy` (`client_packet.cpp:14026`) expects eqemu's
`Merchant_Sell_Struct` (**24B**); `Handle_OP_ShopPlayerSell` (`:14274`)
expects eqemu's `Merchant_Purchase_Struct` (**16B**). Ours:
`eq_merchant.cpp:1231` sends our 24-byte `Merchant_Purchase_Struct` on
`HC_OP_ShopPlayerBuy` and `:1272` sends our 16-byte `Merchant_Sell_Struct` on
`HC_OP_ShopPlayerSell`. **Both directions are correct today.** A block comment
in the header spells this out with file:line on both sides, and
`MerchantPair_SizesAreInvertedRelativeToEqemuNames_ByDesign` fails loudly if
the sizes are ever swapped to match the names.

### Tests

New file `tests/test_wire_layout_t145.cpp` (299 lines), new ctest target
`test_wire_layout_t145`, **11 tests, all passing**. `offsetof` on every named
field, not just `sizeof` — the plan's point, and the right one: every struct in
this batch already had a *passing* size assert.

- 7 layout tests (the six changed structs + the merchant non-change), each with
  a comment recording its eqemu file:line and which side of the arbiter rule it
  landed on. `EnvDamage2` additionally pins field *widths* (`damage` is 4 bytes
  at 6, `constant` is 2 bytes at 27) — the old struct was wrong in four
  independent ways while summing to a self-consistent 32.
- 4 sender wire-shape tests. Body length and framed length are asserted against
  size literals taken from the **eqemu handlers** (152, 8), not from our own
  `sizeof`, so the test does not simply agree with the code it guards; each
  additionally asserts the body does **not** begin with the opcode. Camp/Logout
  reconstruct the *old* buggy body and pin that it was 2 bytes starting with
  the opcode — a regression pin, not just a shape assertion.

Opcode values are duplicated as local constants (with `eq.h` line references)
rather than including `client/eq.h`, to keep this a layout test that does not
link the client.

**Honesty bound, stated in the test file's header comment:** these are static
assertions against eqemu's source. They are not wire observation. No packet
from this unit has been sent to a server.

### Verification bar

| step | result |
|---|---|
| `cmake --build build -j24` (GLES2 config) | **PASS**, clean, zero errors |
| `ctest --output-on-failure` | **2443/2444**, sole failure the documented ALSA `AudioManagerTest.PreloadCommonSounds`. Baseline 2433 → 2444 (+11, this unit's tests) |
| `scripts/build-gles2-x86.sh` | **PASS** (exit 0) |
| Visual gate | **PASS**, `=== VISUAL GATE: PASS ===`; `git status tests/goldens` empty — **goldens untouched** |
| `scripts/build-arm-noble.sh` | **PASS** (exit 0) |
| `check_file_sizes.sh fable` (from slot2, on task branch) | **0 FAIL**. 3 WARN — `eq.h`, `packet_structs.h`, `eq_player_info.cpp`, all pre-existing `>1500` levels, unchanged in kind. New test file `ok` at 299 |

### Parallel-unit boundary — respected

Header edits are confined to the six struct definitions, their asserts, and
the merchant comment block. **`Object_Struct` (~line 846) untouched.** Not one
line of `src/client/eq_entities.cpp`, `include/client/world_object.h`, or
`src/client/world_object.cpp` was read for edit or modified. No reflow, no
reorder, no whitespace tidy anywhere in `packet_structs.h` — and the missing
`pack(pop)`, which was the biggest temptation to sweep the file, was
deliberately left to T147 with a local fix and a pointer instead.

`include/client/eq.h` was edited (one declaration, `SendControlBoat`'s
signature) — not in the plan's stated territory but the unavoidable
counterpart of a sender change the plan explicitly scoped in. Flagged, not
absorbed.

### Deviations

1. **`include/client/eq.h` touched** (one line + comment) for
   `SendControlBoat`'s signature. Outside the plan's file list; forced by the
   sender rework the plan required.
2. **A local `#pragma pack(push,1)`/`pop` added around `EnvDamage2_Struct`.**
   Not in the plan, which did not know about the file-level pack region ending
   early. The minimal-blast-radius choice; the file-wide fix is handed to T147
   with the compile experiment already done.
3. **Two fixes beyond the plan's list**, both in territory
   (`eq_player_info.cpp`): the inverted `dmgtype` mapping, and the direction
   finding for `OP_EnvDamage` recorded as comments in the header and the
   handler.
4. **Severity of `SendCamp`/`SendLogout` corrected downward** — the server
   ignores those payloads entirely. This unit did not fix `/camp`.
5. **`SendControlBoat`'s public signature changed** (`float, uint8_t` → `bool`).
   Zero callers, but a signature is a signature.
6. **`SendPopupResponse` keeps a parameter it no longer sends.** Deliberate;
   alternative was a silent API change or an out-of-scope protocol decision.

### What this unit did NOT establish — read this before writing a board entry

- **Nothing was observed on the wire.** No eqemu lock, no server, no capture,
  no packet sent. T146 owns that leg.
- **Do not write "/face works now" or "/who works now."** What is established
  is: the structs and sender shapes now match eqemu's source, and layout tests
  pin them. Whether the server accepts them and the features behave is
  unverified.
- `/camp` was already working; this unit tidied a malformed packet the server
  was ignoring.
- Environmental damage messages are **still not going to appear** — the
  receive path is unreachable by design and the missing piece is a sender we
  do not have.
- `SendControlBoat` and `SendPopupResponse` have no exercised call path at all
  (`SendControlBoat` has literally zero callers), so even T146 cannot confirm
  them without new callers first.
