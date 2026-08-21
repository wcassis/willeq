# T168 — The remaining zero-caller senders, triaged. All five deleted.

**Slot: slot1. x86 only. NO eqemu, NO lock, NO Pi (offline).** Orchestrator
executed directly.

**Territory: `include/client/eq.h`, `src/client/eq_player_info.cpp`,
`src/client/eq_group.cpp`, `src/client/eq_merchant.cpp`,
`include/common/packet_structs.h` (one struct's comments),
`scripts/ci/check_sender_reachability.py`, `tests/CMakeLists.txt`,
`refactor/`.**

## The sweep, re-run rather than inherited

The standing note said "the remaining 11 (boats, corpse/petition, misc player,
raid, popup, zone)". That list was stale — T154 already deleted the
corpse/petition/popup/zone four. **Re-running the sweep from scratch found
exactly five zero-caller senders**, out of **94** sender-shaped
`EverQuest::Send*`/`Request*` methods:

`SendBoardBoat` · `SendControlBoat` · `SendLeaveBoat` · `SendRaidInvite` ·
`SendTradeCoins`

**All five are dead on arrival, not merely unwired** — the arc's rate held one
last time. Each was checked against eqemu's own source, not against our
comments.

| sender | verdict | why |
|---|---|---|
| `SendTradeCoins` | **DELETE** | **Wrong direction entirely.** There is no `ConnectedOpcodes[OP_TradeCoins]` anywhere in eqemu. The server *emits* this opcode, from inside `Handle_OP_MoveCoin` (`zone/client_process.cpp:1598`), to tell the OTHER trader coins were added. The C→S route is `OP_MoveCoin` — `SendMoveCoin()`, which is wired and used. Nothing we sent here could ever have been read. |
| `SendBoardBoat` | **DELETE** | Sent a **4-byte entity id**. `Handle_OP_BoardBoat` (`:4133`) requires `5 < size <= 64` and resolves the body as a **name** via `entity_list.GetMob(boatname)`. Rejected on size before anything else. |
| `SendLeaveBoat` | **DELETE** | Guarded on `m_is_on_boat`, which only the broken boarder ever set, so it was unreachable in practice — and `Handle_OP_LeaveBoat` (`:9938`) ignores the body entirely, so the id it carried was meaningless anyway. |
| `SendControlBoat` | **DELETE** | The T151/T154 hazard: `Handle_OP_ControlBoat` (`:5421`) resolves the id through `entity_list.GetMob()` and any hit that is not a controllable boat writes `RecordPlayerEventLog(POSSIBLE_HACK)` **against our own account**. Its only id source was the broken boarder. |
| `SendRaidInvite` | **DELETE** | Sent a bare **64-byte name**. eqemu routes `OP_RaidInvite` to `Handle_OP_RaidCommand` (`:12298`), which requires at least `sizeof(RaidGeneral_Struct)` = **140 bytes** and switches on an `action` field at offset 0 that our body did not contain at all. Rejected with "Wrong size" every time. |

## Why delete, and not fix-and-park

`refactor/T147` names the trap: a sender that looks like a half-built feature
but cannot work is worse than no sender, because the next person reads it as
"nearly done". T154 already applied this to four senders. Fixing a body and
leaving it callerless just reproduces the condition this arc exists to
eliminate — an UNVERIFIED sender.

**The knowledge is what was worth keeping, so it was moved, not lost.** Each
deletion site keeps a comment saying what was there and why it could not work,
and the protocol facts now sit on the opcode definitions in `eq.h`, where
anyone reaching for the opcode will see them:

- `HC_OP_RaidInvite` carries the exact 140-byte `RaidGeneral_Struct` layout
  (`action@0`, `player_name@4[64]`, `unknown1@68`, `leader_name@72[64]`,
  `parameter@136`) and the action codes (`3` = `RaidCommandInvite`, `0` =
  `RaidCommandInviteIntoExisting`), so a future raid unit re-adds it correctly
  in one motion.
- `HC_OP_BoardBoat` carries all three boat findings plus the architecture
  blocker.
- `HC_OP_TradeCoins` says it is inbound-only and names `OP_MoveCoin` as the
  outbound route.

**The raid deletion is the one judgment call**, and it is deliberate: the raid
RESPONSE side is also wrong. `ZoneProcessRaidUpdate` decodes 4 of 6 raid
actions incorrectly and **action 20 — what an invite actually produces —
falls through to "Unknown"**. A correct invite whose result the client cannot
read is not a feature. Raid is one unit that fixes the parser and adds the
sender together, or it is nothing.

`IsOnBoat()` (zero callers) and the `m_is_on_boat`/`m_boat_id` members went
with the boat senders. Opcodes and structs were kept throughout, per T154's
precedent.

## A stale comment that was actively misleading

`TradeCoins_Struct`'s `unknown[3]` was annotated **"Magic bytes: must be
{0xD2, 0x4F, 0x00} for server to accept"**. Those bytes are eqemu's own
`unknown5 = 0x4fD2, unknown7 = 0`, written by the SERVER into the packet it
sends us. The comment described a server-built constant as a client
obligation, on an opcode with no server accept path at all — plausibly how the
sender came to be written. Corrected in place, with eqemu's field layout cited,
and a missing `static_assert(sizeof(...) == 12)` added since we do parse this
inbound.

## The result is now a gate, not a fact someone has to rediscover

`scripts/ci/check_sender_reachability.py`, registered as the ctest entry
`check_sender_reachability`, **fails if any `Send*`/`Request*` method has zero
production call sites.** Pure source scan — no build artefact, no server, no
display — so it runs in every configuration.

**It was measured, not asserted:** run against the pre-delete tree it prints
exactly the five senders above and exits 1; against this branch it passes at
89 senders. The script's docstring is explicit about what it does NOT catch —
it counts call sites, not reachability (pattern #10 had a caller and no
*reachable* one), and it cannot see opcode routes that bypass a named sender
(T151's `sendSkillUsePacket` blind spot, 17 skill opcodes).

The `ALLOWLIST` is empty and should stay that way. A sender nobody calls and
nobody is allowed to call is the trap, not the exception.

## One-level reachability check on the other 89

Beyond the zero-caller sweep I checked whether any sender's callers are
*themselves* unreferenced — the shape of reachability pattern #10. **Zero
suspects.** That is a crude approximation (a virtual override or a
registration-table entry counts as a reference), so it is evidence, not proof,
and it is recorded as such.

## Verification

- Build clean; suite **2604/2605** (2596 + 8 from T167 + this gate), sole
  failure the documented ALSA `AudioManagerTest.PreloadCommonSounds`.
- `build-gles2-x86.sh` clean; **visual gate PASS**, goldens untouched;
  `build-arm-noble.sh` clean.
- `check_file_sizes.sh fable`: **0 FAIL**. `eq_player_info.cpp` is 49 lines
  shorter.
- No test lost coverage: nothing referenced any of the five.

## What this closes, and what it does not

**The zero-caller-sender arc is CLOSED.** 28 of 97 unreachable when it opened
(T146), zero now, with a gate to keep it there.

Still open in the neighbourhood, each needing its own unit:
- **`ZoneProcessRaidUpdate`** mis-decodes 4 of 6 raid actions; action 20 falls
  through to "Unknown". Blocks the whole raid family.
- **`OP_InspectAnswer`**: the parser is wired but no client generates the
  packet, so two WillEQ clients can never populate an inspect window.
- **Boats** need a ride/entity model before any boat opcode can be written
  correctly; steering is `OP_ClientUpdate` with a foreign `spawn_id`, not a
  boat opcode at all.
