# T181 — Raid leader-only ops

**Slot: orchestrator-direct on `fable`. x86.** The last deferred piece of the
raid arc (T179 → T180 → here): remove a member, pass leadership, set the loot
rule. All five new commands reuse T179's `OP_RaidCommand` sender and T180's
roster; no new wire machinery.

## What the server checks — and what it does not

This is the whole reason these commands need client-side guards. From
`Handle_OP_RaidCommand` (`zone/client_packet.cpp`):

| Command | Code | Server-side leader check? | Failure mode |
|---|---|---|---|
| Disband (remove) | 5 | **NO** | any member can remove any member |
| LootType | 20 | **NO**, and no validation either | bad value stored verbatim |
| AddLooter / RemoveLooter | 21 / 22 | **NO** | — |
| MakeLeader | 30 | **YES** (`strcmp(raid->leadername, GetName())`) | **silence** |

Three consequences the implementation is shaped by:

1. **MakeLeader fails silently for a non-leader.** The arm is simply not
   entered; nothing is sent back. A client that just forwards the command
   leaves the player staring at nothing, so `/raid leader` pre-checks
   leadership and says why it refused.
2. **LootType is never validated.** `parameter` goes straight into
   `Raid::ChangeLootType`, and a value outside 1..4 matches no arm of the
   switch in `zone/attack.cpp:2887` — it would be stored and would silently
   disable looting for the entire raid. **The parse is the guard**, so it
   lives in `EQT::ParseRaidLootType` and is pinned by test rather than left to
   the command layer.
3. **Remove is not gated at all.** Any raid member can remove any other. There
   is nothing to enforce client-side that the server would honour, so this is
   recorded rather than papered over: no fake "only the leader can do that".

## There is no "disband the whole raid" command

T179's deferred note said "disband-all". There is no such op. The raid
dissolves only when its last member leaves — every `RaidCommandDisband` arm
ends `if (!raid->RaidCount()) raid->DisbandRaid()`. A leader wanting to break
up a raid removes everyone, or everyone leaves. Recorded so the next reader
does not go looking for the packet.

## Commands

```
/raid remove <name>      (alias /raid kick)     -> 5,  leader_name = target
/raid leader <name>      (alias /raid makeleader) -> 30, leader_name = new leader
/raid loot <mode>        (alias /raid loottype)  -> 20, parameter = 1..4
/raid looter add <name>                          -> 21, leader_name = looter
/raid looter remove <name>                       -> 22, leader_name = looter
```

`<mode>` accepts a digit or a word: `leader` / `groupleader` / `selected` /
`all` (`RaidLootTypes`, `zone/zonedb.h:381`; 4 = All is what a new raid starts
with). Separators inside words are ignored so `group leader` and
`group-leader` both work — but **digits are matched on the trimmed text before
any stripping**, because the first cut stripped first and read `-1` as loot
type 1. That bug was caught by its own test.

Membership checks use T180's roster: naming somebody not in the raid is a
server no-op, so catching it client-side turns silence into a sentence. An
**empty** roster means "not told yet", not "not a member", and lets the command
through rather than refusing on missing information.

`/raid remove <your own name>` is byte-identical on the wire to `/raid leave`;
it points at `/raid leave` instead of quietly doing it.

## Scope note — the looter pair was not asked for

The brief was remove / make-leader / loot type. `/raid looter add|remove` is
included because loot type 3 (`Selected`) means "the raid leader and the named
looters" and is **inert without a way to name them** — it would have been a
setting that does nothing. Two subcommands over the same sender. Easy to drop
if that judgement is wrong.

**Not done, and deliberately:** `RaidCommandRaidLock` (8) / `RaidCommandRaidUnlock`
(9). Equally trivial, but independent new functionality rather than something
an asked-for feature needs. The decoder already handles the resulting actions
17/18 with messages, so wiring them later is a sender call and a subcommand.

## Tests

`test_raid_update_decode` 43 (was 37), `test_command_processor` 358 (was 349).
- Loot-type parse: words and digits, whitespace tolerance, and a rejection set
  built from the near-misses that matter (`0`, `5`, `-1`, `+1`, `1-`, `1 2`) —
  values the server would have accepted without complaint. Rejection must also
  leave the output untouched.
- `RaidLootType` enum pinned against `RaidLootTypes` in `zone/zonedb.h`.
- All five command codes round-tripped through `encodeRaidCommand`, kept beside
  the decode-side action codes so the two directions cannot drift.
- Routing and arity for every new subcommand and alias, plus the usage line
  having to name all of them.

## NOT wire-verified

Written after the T180 probe with eqemu already down. The sender is the one
T179 and T180 verified live, and the field roles come from the handler arms
quoted above, but **none of these five commands has been seen on the wire.**
The probe is a two-account raid: `/raid loot leader` then `/raid looter add`,
`/raid remove` the other member, and — from the NON-leader — `/raid leader`,
which must be refused client-side rather than vanish.
