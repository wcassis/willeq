# T320 — Grouping does not survive a zone transition, and the client keeps a phantom group

**Slot: slot1. Branch `task/T320` cut from `fable`. OWNS eqemu (noclobber) AND
the Wine/Xvfb official client. Two clients, two zones. No Pi.**

Both defects were measured by T297 (`refactor/T297_cross_zone_chat_verify.md`
Review — **read it first**, it has the raw packet evidence and the narrowing
already done). T297 was a verification unit and correctly stopped at reporting.

## Leg A — settle WHOSE bug the disband is (the oracle decides)

**The server disbands the group the instant either member zones.** Verified in
both directions (leader zones; non-leader zones — same result mirrored). From
the source zone's own log: two `OP_GroupUpdate` size 454 with first dword
`06 00 00 00` = `groupActDisband`, one per member, immediately after the
ZoneToZone approval — and **no client→server group packet anywhere near the
transition.**

Narrowed to `Client::LeaveGroup()`'s `MemberCount < 3 → DisbandGroup()`, which
on a zone-out is reachable only where `bZoning` is false — i.e. **the source
zone treated a visibly-successful `OP_ZoneChange` handshake as a logout.**

**It is NOT settled whether fable causes this or eqemu does it to everyone**, and
that is this leg's whole job. Two facts keep it open: the discriminating
`"%s logged out."` / `"%s left the zone."` log line never appeared, and the zone
log shows the `ServerOP_ZoneToZoneRequest` response handler running **twice** for
one client `OP_ZoneChange` — unexplained.

**The clean settler is the one T297 named: the official Titanium client doing
the identical `#zone` while grouped.** Two official clients, grouped, one zones.
If the group survives, fable's zone-change handshake is at fault and this
becomes a client unit. If it disbands there too, it is an eqemu defect —
**write it up for upstream and do NOT patch it client-side.**

This is the standing project rule: the oracle decides, not argument
(memory `match-the-oracle-dont-infer-it`). Account/character provisioning is
automated; `Oracledwarf` (race 8, id 82) and the other oracle characters exist
already — **do not delete them, they are prior units' evidence.**

## Leg B — the client's phantom group (ours regardless of Leg A)

After the transition the arriving client reports **`In group (2 members),
leader: `** — count 2, leader name EMPTY — and will happily `/gsay` into a dead
group with no error. In T297's run 1 the zoning client never processed the
disband at all, because it was between zone servers when it was sent.

**Fix this whichever way Leg A goes.** A disband delivered mid-transition is
always lost, so the client needs to **re-establish group state on zone-in**
rather than trusting what it carried across. `GroupState`
(`include/client/state/group_state.h`) is the single home for it since T286.

⚠ **An empty leader name with a non-zero member count is its own bug** — the
count and the roster disagreed, which means something published a partial
update. Find which publish site did that; it may be reachable without zoning at
all.

## Leg C — the chat channel-name drift (cosmetic, cheap, do it last)

`src/client/eq_chat.cpp` builds the channel name **twice, 40 lines apart**, and
the two tables have drifted: the console `[CHAT]` chain omits
`CHAT_CHANNEL_AUCTION` (it printed `chan4` for T297's entire run) while the
bridge chain includes it, and **neither knows channel 14 `TellEcho`** (`chan14`).
One table, one home, both consumers pointed at it — the same shape as T308's
body-type header and T314's follow-up.

## Acceptance

- **Leg A:** a plain verdict with the oracle's log beside fable's, and a
  named owner (us or upstream). If upstream, an issue-ready writeup; nothing
  else.
- **Leg B:** two-client live proof (memory `two-client-live-testing`) that after
  a zone transition the arriving client's group state matches reality — whether
  that means still grouped or correctly ungrouped. Plus a test for the
  empty-leader-with-members case.
- **Leg C:** one table, both consumers, and a test that fails if a third copy
  appears (T314's guard is the precedent).
- Full `ctest -j6` (**NOT -j24**), zero new failures vs `refactor/test_baseline.txt`.
- Visual gate PASS, goldens untouched.

⚠ **If Leg A proves this is eqemu's, STOP Leg A there** — do not go fix the
server. Legs B and C stand on their own.

## Live-session traps recorded this week — read before you start

- The server has twice been found **running with no lock held**. Do NOT run
  `up.sh` on top of a running fleet (it starts a second loginserver that cannot
  bind; the client then reports "could not find world server WillEQ"). Confirm
  one loginserver and one world, or cycle with `down.sh` then `up.sh`.
- **`#gm on` is required before `#kill`/`#damage`/`#npcloot` and fails
  SILENTLY** — account status alone makes `#npctypespawn` and `#set time` work,
  so a GM session looks functional while those three do nothing.
- Use `/say #zone <zone>`, never a DB `zone_id` edit (stale coordinates).
- T297 left a flagged anomaly: **`Oraclerogue`'s persisted `gm` flag was found
  at 1** with no recorded prior value and nothing in that unit able to set it.
  Note what you find; do not assume it is yours.

## Territory

`src/client/eq_chat.cpp`, the group state/publish path, plus your tests.
⚠ Concurrent: slot2 T319 (`output/`, `application.cpp`, `irrlicht_renderer.*`,
`eq_movement.cpp`), slot3 T318 (`spell_effects.cpp`, `eq_guild.cpp`,
`entity_renderer.cpp`, `irrlicht_bridge.cpp`). **`eq_entities.cpp` and
`eq_bridge.cpp` are free now** — T311 Leg B has merged.
