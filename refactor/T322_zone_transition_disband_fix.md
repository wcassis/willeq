# T322 — Stop disbanding the group on every zone transition (T320 Leg A's owner)

**Slot: slot1. Branch `task/T322` cut from `fable`. OWNS eqemu (noclobber) AND
the Wine/Xvfb official client. Two clients, two zones. No Pi.**

T320 Leg A settled the ownership question against the oracle and **stopped at
the verdict, as its plan required.** This unit does the fix. **Read
`refactor/T320_group_zone_transition.md`'s Review first** — it has the oracle
evidence, the refutation of the earlier narrowing, and the exact trigger.

## What is settled — do not re-derive any of it

**The disband is OURS.** Two official Titanium clients, grouped, one does the
identical `#zone`: **the group survives, both directions**, zero `OP_GroupUpdate`
from the source zone, `group_id`/`group_leaders` rows intact. The observing
official client prints `Oraclealpha left the zone.` and keeps its group window.

**The mechanism:** `EverQuest::DisconnectFromZone()` (`src/client/eq_zone.cpp:488`),
reached from the deferred zone change in `UpdateMovement()`
(`src/client/eq_movement.cpp:519`), **closes the source-zone connection 8 ms
after the approval**, dropping the session out of `ESTABLISHED`. **The official
client leaves it up.** Server-side that lands in `Client::OnDisconnect(true)`,
whose *first statement* is an unconditional `LeaveGroup()` that never consults
`bZoning`.

⚠ **T297's narrowing was REFUTED by T320 — `client_process.cpp:639` never runs.**
Do not chase it.

⚠ **Two confounds are already eliminated:** the doubled `ZoneToZoneRequest`
handler is normal eqemu (the oracle doubles too), and `#gm on` is not the cause
(fable disbands with `gm=0`). T297's zoning client was GM and the oracle's was
not — that could have voided the verdict, and T320 controlled for it.

## The work

**Match the official client's connection lifecycle across a zone change.** The
question this unit must answer with measurement, not argument: **what does the
oracle actually do with the source-zone session, and when?** Does it leave the
socket open until the destination zone is established? Does it send something
first? Does it ever close it, and on what event?

**Take that from the oracle's own traffic** (memory `match-the-oracle-dont-infer-it`);
T320's captures under `/mnt/projects/fable/review/T320/` may already answer it —
check before spending a live session. Then make fable do the same thing.

⚠ **This is a connection-lifecycle change on the critical path of every zone
transition.** The failure modes to guard against, and to say you checked:
- leaking the old connection (never closed → socket/session exhaustion over a
  long play session);
- closing it too late and confusing the destination zone;
- a zone change that FAILS partway — the old connection must still be cleaned up;
- the logout path (`m_pending_logout_disconnect`, `eq_movement.cpp:516-521`) and
  the world-side path (`eq_login_world.cpp:903`) both call the same function and
  **must keep their current behaviour** — a logout genuinely should disconnect.

## Acceptance

- **Two-client live, both directions** (leader zones; non-leader zones), showing
  the group SURVIVES and both clients agree on the roster afterwards — the same
  shape T320 used for its Leg B table, so the results are directly comparable.
  Screenshots + server-side `group_id`/`group_leaders` rows to
  `/mnt/projects/fable/review/T322/` with a README.
- **The logout path still disconnects** — prove it, live or by test. This is the
  regression that a careless fix causes.
- A test pinning the lifecycle decision itself, so the next reader cannot
  "simplify" it back.
- Full `ctest -j6` (**NOT -j24**), zero new failures vs `refactor/test_baseline.txt`;
  visual gate PASS, goldens untouched; gles2 and ARM builds.

## Also here (small, T320 found, related)

`fable echoes `zone_reason = 0` instead of the server's `type`. T320 checked all
ten eqemu references — **all log-format arguments, inert.** Fix it anyway while
you are in this path, or say why not; it is one field.

## Live-session traps (all recorded this week)

- Do NOT run `up.sh` on top of an already-running fleet — it starts a second
  loginserver that cannot bind and the client reports "could not find world
  server WillEQ". Confirm one loginserver and one world, or `down.sh` first.
- **`#gm on` is required before `#kill`/`#damage`/`#npcloot` and fails silently.**
- Use `/say #zone <zone>`, never a DB `zone_id` edit.
- ⚠ **`Oraclerogue`'s persisted `gm` flag has now been found at 1 by TWO
  consecutive units** (T297, T320), neither of which could have set it. If you
  see it again, that is a third sighting — say so; something unidentified is
  writing it.
- `Oracledwarf` and the other oracle characters are prior units' evidence — do
  not delete them.

## Territory

`src/client/eq_zone.cpp`, `src/client/eq_movement.cpp`, `src/client/eq_group.cpp`,
plus your tests. ⚠ Concurrent: slot2 J05 (`graphics/eq/wld_loader.cpp` + many
CMake sites), slot3 J11 (`graphics/irrlicht_renderer.cpp`).
