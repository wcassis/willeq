# T179 — Raid commands: /raid accept (and invite/leave/status)

**Slot: orchestrator-direct on `fable`. x86.** The feature T169 unblocked and
T172 exposed as missing: T172 watched a raid invite arrive on the wire with the
client able only to print "this client cannot accept raid invites yet." T179 is
the sender + the `/raid` command family that answers it.

## Scope

The full T132 stack for one new sender:
- `EQT::encodeRaidCommand` — pure encoder for the outbound body.
- `EverQuest::SendRaidCommand` — queues it on the wire.
- Raid state on `EverQuest`, maintained from the decoded `OP_RaidUpdate` stream.
- `/raid [status|invite <name>|accept|leave]` through
  CommandProcessor → ActionDispatcher → EqActionHandler → the sender.

**Deliberately NOT in scope: a full member-list roster.** Titanium receives the
roster as individual action-0 `raidAdd` packets (eqemu's `SendBulkRaid` →
`SendRaidAdd` per member; `GetRaidDetails` only loads the DB and queues nothing),
which T169 already decodes per member. Maintaining a full member vector + a
`/raid list` is a clean follow-up; T179 tracks only what accept/leave/status
need (in-raid, leader, pending inviter). The board's "action-6 roster parse" was
based on eqemu's internal comment — for Titanium there is no action-6 packet to
parse.

## Protocol (verified against eqemu ba2ca5ead)

- **Opcode:** Titanium `OP_RaidInvite = 0x5891` (`patch_Titanium.conf`), routed
  by eqemu to `Handle_OP_RaidCommand` — our `HC_OP_RaidInvite`.
- **Body:** the 140-byte INTERNAL `RaidGeneral_Struct` (the handler length-checks
  against it, rejecting anything shorter): `action@0, player_name@4[64],
  unknown1@68, leader_name@72[64], parameter@136`. NOT Titanium's 136-byte one.
- **Action codes** (`common/raid.h`): 1 = AcceptInvite, 3 = Invite, 5 = Disband.
- **Accept field roles are inverted from intuition** and pinned by test:
  `RaidCommandAcceptInvite` resolves `player_name` as the **inviter** and
  `leader_name` as **us** (the invitee) — `zone/client_packet.cpp`. The invite we
  receive (action 20) carries the inviter in `leader_name`, so `/raid accept`
  sends `player_name = received.leader_name`, `leader_name = our own name`.

## What each command does

| command | action | player_name | leader_name |
|---|---|---|---|
| `/raid invite <name>` | 3 | `<name>` (invitee) | us |
| `/raid accept` | 1 | pending inviter | us |
| `/raid leave` | 5 | us | us |
| `/raid` / `/raid status` | — | (reads raid state) | |

`/raid accept` refuses with "You have no pending raid invitation." when none is
stored. The pending inviter is captured in `ZoneProcessRaidUpdate`'s action-20
arm, and the message there now reads "... Type /raid accept to join." instead of
"this client cannot accept raid invites yet." State is cleared on
join/leave/disband/no-raid.

## Acceptance

- `encodeRaidCommand`: **5 tests** — exact 140-byte size, fields at the internal
  offsets, parameter@136, name NUL-termination/truncation to the 64-byte field,
  empty names.
- `/raid` routing: **6 tests** — accept/leave/invite(name)/bare+status route to
  the right dispatcher method; invite-without-name and unknown-subcommand fail
  without dispatching.
- **Gates:** `check_sender_reachability` PASS (SendRaidCommand has its three
  production callers — 91 senders, 0 unreached); `check_build_tree_orphans` PASS.
- Full build clean; full ctest **2680/2681** (only the standing ALSA failure).
- **Live two-client acceptance:** see the Review addendum / `review/T179/`.

## Follow-ups

- A full raid roster model (member list from the action-0 stream) + `/raid list`.
- `/raid` leader-only operations (disband-all, make-leader, loot type) — more
  action codes on the same sender.

## Review addendum — live two-client acceptance (and the wire-truth it caught)

Run 2026-08-22 (`review/T179/`). Official Titanium client `Testbuddy` (Wine) in
tutorialb invited WillEQ `Testmelee` (headless, T179 binary); WillEQ answered
with `/raid accept`.

**First run FAILED and that is the point of the probe.** `/raid accept` sent
`OP_RaidCommand action=1 player='Testbuddy' leader='Testmelee'`, WillEQ printed
"You accept...", but **no raid was created** — the final `/raid status` said "not
in a raid", `raid_members` was empty, and the zone log showed nothing. Cause:
eqemu's `DECODE(OP_RaidInvite)` (titanium.cpp) reads the **Titanium 136-byte**
`RaidGeneral_Struct` (`leader_name@68, parameter@132`), not the 140-byte internal
one T168's note described. The 140-byte body put `leader_name` at 72, so the
DECODE read offset 68 (our zeroed `unknown1`) as `leader_name` → empty →
`GetClientByName("")` → null → the accept arm silently returned. Fixed the
encoder to the 136-byte layout.

**Second run VERIFIED end to end** (`t179_a.log`):

```
/raid status            -> You are not in a raid.
(invite arrives)        -> Testbuddy invites you to join a raid. Type /raid accept to join.
/raid accept            -> Sent OP_RaidCommand action=1 player='Testbuddy' leader='Testmelee'
                           You accept Testbuddy's raid invitation.
(+48 ms, server built the raid and sent it back:)
   action=8  raidCreate  leader='Testbuddy'   -> Testbuddy has formed a raid.
   action=0  raidAdd     Testbuddy / Testmelee -> You have joined the raid.
   action=30 raidMakeLeader Testbuddy          -> Testbuddy is now the raid leader.
/raid status            -> You are in a raid led by Testbuddy.
```

`raid_members` afterwards held both rows (Testbuddy `israidleader=1`, Testmelee
member) — `raid_members_after.txt`. Server state restored (raid deleted), eqemu
down, lock released.

**The lesson, again:** a new sender is unverified until the wire says otherwise.
The encoder passed 5 unit tests against the layout T168 documented, and that
layout was wrong; only the live server's DECODE settled it.
