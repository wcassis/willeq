# T180 — Raid roster, `/raid list`, and the action-10 split

**Slot: orchestrator-direct on `fable`. x86.** Two things, both in the raid
decoder, both left open by earlier units:

1. T179's deferred follow-up — a member-list roster and `/raid list`.
2. T172's OURS-side defect — action 10 at zone-in is not `raidNoRaid`.

They are one unit because they are the same file and the same mistake class:
believing there is one packet where there are two.

## Finding 1 — Titanium never delivers a roster packet

eqemu's action 6 (`raidMembers`) is the packet that carries the whole member
list. `common/patches/titanium.cpp`'s `ENCODE(OP_RaidUpdate)` **has no `case`
for it**: it falls through the `default` arm, which builds a bare 136-byte
`RaidGeneral_Struct` and copies only `action`, `parameter` and the two names.
The member list is dropped on the floor before it ever reaches the wire.

So the board item T169 wrote — "action-6 roster parse" — describes a packet
that does not exist on this client. T179's doc had already reasoned its way to
the same conclusion from the send side; this unit confirms it from the encoder.

**What the server does send** is one action-0 `raidAdd` per member:
- `Raid::SendBulkRaid` (`zone/raids.cpp:1317`) walks the members and sends one
  `SendRaidAdd` to a client that joins or zones in. It has **18 call sites**
  in `zone/` — every join, every group move, every zone entry.
- `Raid::SendRaidAddAll` broadcasts one to the whole raid when someone joins.
- `SendRaidAdd` puts the member's name in **both** name fields, the group in
  `parameter` (`RAID_GROUPLESS = 0xFFFFFFFF` when in the raid but in no group),
  and class/level/isGroupLeader in the `RaidAddMember_Struct` tail.
- A group change is **Remove + Add** (`Raid::SendRaidMove`), never an edit.

The roster is therefore ASSEMBLED, not received. `EQT::RaidRoster`
(`raid_update.{h,cpp}`) does that, kept out of `EverQuest` so the policy is
testable without a live client.

**The trap the tests exist to catch:** `SendBulkRaid` replays the *entire*
roster on *every* zone-in. An `Apply` that appended on Add — the obvious
implementation — would double the roster each time the player zoned, and
nothing would look wrong until somebody counted. Add upserts by name.

## Finding 2 — action 10 is two packets wearing one number

T172 observed `RaidUpdate too short for action 10 (raidNoRaid): 138 bytes` on
**every** zone entry, with no raid in existence anywhere. The cause, confirmed
here at the send site:

`Client::CompleteConnect` (`zone/client.cpp:859`) sends `OP_RaidUpdate`
carrying a **`ZoneInSendName_Struct`** — `{uint32 unknown0, char name[64], char
name2[64], uint32 unknown132}`, 136 bytes — with `unknown0 = 0x0A` and both
names set to the player's own. We read `unknown0` as `action` and see 10, which
is `raidNoRaid`. 136 + 2 opcode bytes = the 138 observed.

And a real `raidNoRaid` is sent by **nothing**: `grep -rn raidNoRaid` over
eqemu finds it in `common/raid.h` and in the six patch encoders, and in no
`zone/` code path at all. Every action 10 a Titanium client will ever see is
the zone-in handshake.

**The fix is a discrimination, not a size bump.** T172's write-up already said
so; this is the implementation. `DecodeRaidUpdate` splits on length:
- **136 bytes** → `zone_in_name = true`, names parsed (they sit at the Titanium
  general offsets), and the caller must not touch raid state.
- **140 bytes** → a genuine `raidNoRaid` in eqemu's internal layout. Decoded for
  completeness; never yet observed.

**Why this was worth more than silencing a WARN.** Bumping `EMU_GEN_SIZE` to
136 — the one-character "fix" — would have made every zone entry parse as
"you are not in a raid", which clears `m_in_raid`, the leader, the pending
invite, and (as of this unit) the whole roster. The client would have dropped
its raid membership every time the player zoned, and the T179 live probe would
not have caught it: that probe never zoned. Pinned by
`RaidRoster.ZoneInHandshakeLeavesTheRosterAlone`.

## `/raid list`

`/raid list` (alias `/raid members`) through the usual stack —
CommandProcessor → ActionDispatcher → EqActionHandler. Sorted by raid group,
then by name; groupless members last under their own heading; the raid leader
and each group leader tagged. Groups print 1-based (the wire is 0-based).

Two states that are not an empty list, and say so:
- not in a raid → "You are not in a raid."
- in a raid, no members held → "Raid member list not received yet." An empty
  printout would read as an empty raid, which is a different fact.

Class/level are omitted for a member whose Add arrived without the
`RaidAddMember` tail, rather than printed as "Level 0 Class0".

## Visibility change

`EverQuest::GetClassName` moved from private to public. `/raid list` renders
class names from `EqActionHandler`, which is not a friend of `EverQuest`; the
alternative was pulling the graphics-side `EQT::Graphics::getClassName` into a
non-graphics TU, i.e. a new dependency direction for a switch statement. It is
a static pure lookup over a wire enum — no state, nothing to encapsulate.

## Tests

`test_raid_update_decode` 37 (was 24), `test_command_processor` 349 (was 346).
- 4 on the action-10 split: the 136-byte handshake decodes and flags, the
  140-byte body is still a real `raidNoRaid`, a genuinely short action 10 is
  still rejected, and no other action ever sets the flag.
- 13 on `RaidRoster`, against what the server actually sends: the Add tail, the
  groupless sentinel, replayed adds upserting, Remove+Add group changes, both
  remove codes, self-removal/disband/real-NoRaid emptying it, Create resetting
  it, the zone-in handshake leaving it alone, non-membership actions leaving it
  alone, and invalid updates changing nothing.
- 3 on `/raid list` routing (subcommand, alias, case-insensitivity) plus the
  usage line now advertising `list`.

## NOT wire-verified

eqemu is down. The roster is built from packets this codebase already decodes
correctly (T169 verified action 0 live, with names, in T172's leg A), and the
zone-in handshake was observed live by T172 — so both findings rest on live
evidence, but the **assembled roster and the `/raid list` output have not been
seen on a live server.** A two-account raid (`Testmelee` + `Oraclerogue`) with
a zone-in in the middle is the probe: it exercises the upsert path and the
handshake path in one run, and the WARN should be gone from the log.
