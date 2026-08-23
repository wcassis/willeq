# T184 — Raid groups (`/raid group`)

**Slot: orchestrator-direct on `fable`. x86.** Will's condition was "if that's
supported by the Titanium client (based on the eqemu server)". It is, and the
reason is worth stating precisely, because the obvious reading of the server
code says otherwise.

## Is it supported? Yes — and the path is not the obvious one

`Handle_OP_RaidCommand`'s `RaidCommandMoveGroup` arm
(`zone/client_packet.cpp:12804`) ends by calling `Raid::GroupUpdate(gid)`,
which sends **`OP_GroupUpdate`** — the ordinary *group window* packet, not a
raid packet. Reading only that, a Titanium client would never learn raid-group
assignments through the raid stream, and `/raid list` could never show them.

But the arm first calls `Raid::MoveMember`, and **`MoveMember` calls
`SendRaidMoveAll`** (`zone/raids.cpp:330`), which is:

```
SendRaidRemoveAll(who)      -> every client: raidRemove2 for the moved member
  SendRaidCreate(c)         -> the moved client only: fresh roster stream
  SendBulkRaid(c)           ->   ... and every other member replayed
SendRaidAddAll(who)         -> every client: raidAdd, parameter = the NEW group
SendMakeLeaderPacket(...)
```

So the group change **does** reach every client over `OP_RaidUpdate`, as
**Remove followed by Add carrying the new group in `parameter`** — which is
exactly the path T180's roster already handles and tests, the one documented
there as "a group change is Remove+Add (`Raid::SendRaidMove`), never an edit".
No decode work was needed. T180 built the machinery for this before there was
anything to drive it.

`raidAdd` also carries `isGroupLeader`, which T180 decoded and `/raid list`
already prints — never exercised until now, because every raid the client
could build was one flat groupless list.

## The command

```
/raid group <name> <1-12|none>     -> action 6, leader_name = name,
                                      parameter = the WIRE group
```

**Numbering: the user-facing number is 1-based, the wire is 0-based.**
`/raid list` prints "Group 1".."Group 12" (T180), so a user who reads "Group 2"
off the roster and types `2` must land in that same group. The conversion
happens once, in `EQT::ParseRaidGroup`.

`none` / `out` / `ungrouped` select the ungrouped bank — any wire value >= 12
takes eqemu's else branch; we send `kRaidGroupless` because it says what it
means.

**`0` is rejected.** It is precisely what a user thinking in wire numbers
types, and eqemu would have read it as group 1. **`13` is rejected** for the
mirror reason: eqemu reads anything >= 12 as "ungrouped", so an unchecked 13
would move somebody *out* of their group when they asked for a group. eqemu
validates neither.

## Two failure cases the server answers with silence

Both are in the arm, and the roster is the only reason the client can say
anything at all:

- `if (group_count < 6)` — moving into a group that already holds six falls out
  of the arm with **no reply**. The roster counts, so the client says
  "Group 1 is full (6 of 6)."
- `if (parameter == old_group) break;` — an explicit no-op, also silent. The
  roster knows the member's current group, so the client says "Oraclerogue is
  already in Group 2."

The count can be stale — it is only as current as the last update received —
but it is wrong in the **safe** direction: at worst the client refuses
something that would have worked. It cannot cause a bad move, because if the
roster says there is room and there is not, the server does what it did
before: nothing.

Leader-gated client-side, consistently with T181/T182/T183. The server gates
this one no more than the others.

## Upstream bug found while checking

`Raid::SendRaidGroupAdd` and `SendRaidGroupRemove` (`zone/raids.cpp:1534`,
`:1544`) build a `ServerPacket`, fill it, and `safe_delete` it **without ever
calling `worldserver.SendPacket`** — while every sibling in the file sends
first. Reachable from the zone-entry rejoin path
(`zone/client_packet.cpp:612-613`). Written up as
`review/upstream-eqemu/03_raid_group_add_remove_never_sent.md`. Not ours;
in-zone behaviour is unaffected because `MoveMember`'s `SendRaidMoveAll` covers
the same ground directly.

## Tests

`test_raid_update_decode` 51 (was 43), `test_command_processor` 364 (was 361),
`test_eq_action_handler_raid_guards` 13 (was 8).
- The 1-based↔0-based conversion both ways, and a rejection set built from the
  values that would have done damage silently (`0`, `13`, `-1`, `+1`).
- `RaidRoster::CountInGroup`, and — the important one —
  `GroupCountsFollowAMoveDeliveredAsRemoveThenAdd`: the counts must follow a
  move delivered as Remove+Add with no group-specific handling at all, which
  is the whole basis for this feature working on Titanium.
- Both silent-failure guards against a real `EverQuest`, with the group state
  established by replaying real packets; plus the ungrouped bank being exempt
  from the full-group check (it has no limit, and every member starts there).

## Live probe — DONE, VERIFIED (2026-08-23). Evidence `review/T184/`.

Two clients; B does nothing but `/raid list`, because the point is what an
observing second instance sees.

- A: `/raid group Oraclerogue 2` → `action=6 leader='Oraclerogue' parameter=1`.
- **B's own roster, with B having done nothing:**
  ```
  Raid members (2):
    Group 2:
      Oraclerogue (Level 10 Rogue) [group leader]
    Not in a group:
      Testmelee (Level 1 Warrior) [raid leader]
  ```
  B learned its own group change purely from the raid stream, and eqemu had
  promoted it to group leader (`group_count == 0` → `SetGroupLeader`) — the
  `[group leader]` tag T180 wrote and nothing had ever exercised.
- Repeating the same move → "Oraclerogue is already in Group 2." (no packet).
- `/raid group Oraclerogue 0` → "'0' is not a raid group." (no packet).
- Moving A itself in, then B out to the bank: both clients agree afterwards,
  and eqemu reassigned group leadership to the remaining member.
- **DB confirms the numbering independently:** `raid_members.Testmelee.groupid
  = 1`, displayed as "Group 2".

**Not exercised live:** the full-group refusal — it needs seven characters in
one raid and we have two provisioned. It is covered by unit test against a
roster built from real packets, and the arithmetic is the same either way.
