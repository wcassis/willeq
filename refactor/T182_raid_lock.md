# T182 — Raid lock / unlock

**Slot: orchestrator-direct on `fable`. x86.** The piece T181 deliberately left
out. `/raid lock` and `/raid unlock` — `RaidCommandRaidLock` (8) and
`RaidCommandRaidUnlock` (9) — over T179's sender.

## The one thing that made this more than two lines

**eqemu sends the SAME action whether the state changed or was already what you
asked for.** `Handle_OP_RaidCommand`:

```cpp
case RaidCommandRaidLock:
    if (!raid->IsLocked()) raid->LockRaid(true);   // -> SendRaidLock(), broadcast
    else                   raid->SendRaidLockTo(this);  // -> to you alone
```

Both paths put `raidLock` (17) on the wire with the same body shape. Asking for
a state you are already in is **not an error**, and nothing in the packet says
which case you got. So "the raid is now locked" and "the raid is already
locked" can only be told apart by state the CLIENT tracks — which is why
`m_raid_locked` exists rather than the handler just printing on arrival.

It is maintained from actions 17/18 and cleared wherever membership is
(Create, Disband, a real NoRaid, and our own removal). It re-establishes
itself on zone-in: the rejoin path sends `SendRaidLockTo` only when the raid is
locked, so an unlocked raid correctly produces nothing and the flag stays
false.

`/raid status` reports the lock only when it is set — an unlocked raid is the
default and saying so every time is noise.

## Sending when the state may already match

The client sends anyway rather than short-circuiting on its tracked state. The
state could be stale (another leader locked it in a zone we were not in yet),
and refusing on stale state would block a command the server would have
honoured. The reply is the authority; our state only chooses the wording.

## Leadership gate — and the asymmetry it leaves

The server does **not** gate lock/unlock on leadership; the gate is
client-side, the same judgement as `/raid loot`, and matches what the real
Titanium client exposes (only the leader gets these controls).

**That leaves one inconsistency, and it is worth a decision rather than a
quiet choice:** `/raid remove` is *not* gated, on the grounds that the server
does not gate it either (T181) — but real EQ makes kicking leader-only too, so
by the standard applied to loot and lock, remove should be gated as well.
Currently the client refuses a non-leader's `/raid loot` and `/raid lock` while
letting their `/raid remove` through. Both positions are defensible; being on
both at once is not. **Flagged for Will, not settled here.**

## Tests

`test_command_processor` 361 (was 358): routing for both spellings, and a
trailing argument on `/raid lock` being ignored rather than turned into an
error the player has to decode. The usage line must name both.

## Live probe — DONE, VERIFIED (2026-08-23). Evidence `review/T182/`.

- `/raid lock` → `action=8`, broadcast `raidLock` → **"The raid is now
  locked."**; B, in the raid, saw the same broadcast.
- `/raid lock` **again** → `action=8` again, and A said **"The raid is already
  locked."** — the distinction that only tracked state can make.
- `/raid status` → "The raid is locked."
- `/raid unlock` → `action=9` → "The raid is now unlocked."; again → "The raid
  is not locked."
- **Non-leader `/raid lock` refused client-side, no packet sent** (B).

**Also closed the two T181 gaps in the same run:** `/raid loot groupleader`
(`Loot type changed to: 2.`) and `/raid looter remove` (`Removing Oraclerogue
as a raid looter.`). **Every raid command this client has is now wire-verified.**

## Not done

`RaidCommandMoveGroup` (6) — moving members between raid groups. The roster
already carries group numbers (T180) and prints them, but nothing sets them, so
every raid this client builds is one flat groupless list. That is the next raid
feature if there is one; it is a real feature, not a leftover.
