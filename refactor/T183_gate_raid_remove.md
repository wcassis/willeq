# T183 — Gate `/raid remove` on leadership

**Slot: orchestrator-direct on `fable`. x86.** Settles the asymmetry T182
flagged, and puts the raid command guards under test for the first time.

## The inconsistency

The server gates exactly one of these commands on leadership —
`RaidCommandMakeLeader`. Loot type, looter add/remove, lock/unlock and
**disband (remove)** are all ungated: any raid member can issue them.

T181 reasoned from that and left `/raid remove` permissive, saying so plainly
in its doc: *"there is nothing to enforce client-side that the server would
honour."* T181 and T182 then gated `/raid loot`, `/raid looter` and
`/raid lock` on the opposite reasoning — that the real Titanium client offers
those controls only to the raid leader, so the client should too.

Both readings are defensible. Holding both at once was not: the client refused
a non-leader's `/raid loot` while letting the same player kick somebody.
**Will's call was to gate remove**, so the client is now consistently on the
"match the real client's policy" side.

**The gate is OUR policy, not the server's**, and the code says so at the site
rather than implying the server would have refused. A non-leader's remove would
in fact have worked.

## Ordering

`/raid remove <your own name>` is checked **before** the leadership gate. A
non-leader typing their own name wants `/raid leave`, and answering "only the
raid leader can remove a raid member" would send them the wrong way. Pinned by
`NonLeaderRemovingThemselfIsPointedAtRaidLeave`.

Bad loot type is likewise refused before the gate: the specific complaint is
the more useful one, and it applies to leader and member alike.

## The guards get tests

`test_eq_action_handler_raid_guards.cpp` — 8 tests against a **real**
`EverQuest`, with raid state established through the production packet path
(`ReplayZone` + `HC_OP_RaidUpdate`), never by poking a setter. That is the
point: it pins the guards to the packets that really produce the state, so a
decode regression breaks these too.

Covered: every op refused when not in a raid; every op refused consistently
for a non-leader (the T183 subject); the self-removal ordering; the leader
path going through; removing somebody not in the roster refused by name; and
**leadership tracked as it moves** — form a raid we lead, remove successfully,
replay action 30 handing leadership away, and the same command is then
refused. That last one is the behaviour the T181 live probe demonstrated;
here it is pinned.

**Measured red.** With the new gate disabled (`false &&`),
`NonLeaderRemoveIsRefused` fails on both assertions — the handler prints "You
remove Raidmate from the raid." instead of refusing — while
`NonLeaderIsRefusedEveryLeaderOpConsistently` still passes, confirming the new
test isolates exactly this change and does not merely re-cover T181/T182.

## Not re-probed live

No new packet shape and no new command: this only ever *suppresses* a send
that the T181 probe already verified works. The live behaviour it changes is a
refusal, which is a client-side message with no wire component. The raid arc's
wire coverage is unchanged and complete.
