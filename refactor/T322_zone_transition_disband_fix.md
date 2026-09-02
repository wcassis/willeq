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

---

## Review (executor, slot1, 2026-09-02 07:15-09:40 UTC)

Branch `task/T322` off `fable` `6e3a5b48` ("T322 plan: match the official client's
zone-transition connection lifecycle so the group survives"). Evidence:
`/mnt/projects/fable/review/T322/` (README there).

eqemu lock taken with `noclobber`, exit 0; server was **down** and **no lock held** on
arrival, so `up.sh --launcher` was the correct start. One loginserver, one world,
confirmed by `ps` inside the container before and after the mid-session restart (the
restart was to pick up `logsys_categories` 83/84). `down.sh` clean, lock deleted.

### What the oracle actually does with the source-zone session — answered from T320's own capture, no live session spent

`review/T320/legA_qeynos2_zonelog_slice.log` already had it, at the very end of the
slice. After the server's `OP_ZoneChange` approval the official Titanium client
**does not close the socket at all**. It keeps sending `OP_ClientUpdate`, then:

    C->S OP_SaveOnZoneReq  Size [194]     (2 opcode + 192 body; eqemu ignores the payload)
    C->S OP_DeleteSpawn    Size [6]       (2 opcode + its OWN entity id, 0x113)
    S->C OP_LogoutReply
    S->C OP_DeleteSpawn                   (broadcast to everyone else)
    S->C OP_SpecialMesg  "Oraclealpha left the zone."

**The SERVER closes the session**, from `Handle_OP_DeleteSpawn`
(`zone/client_packet.cpp:5727`) → `Disconnect()` → `eqs->Close()` and
`client_state = DISCONNECTED`.

That is the whole mechanism, and it turns on WHICH of `Client::Process`'s two
disconnect exits runs:

| how the session ends | eqemu path | group |
|---|---|---|
| socket closed by the client, no packet (**fable, before**) | `client_process.cpp:582` — `!eqs->CheckState(ESTABLISHED)` → `OnDisconnect(true)`, whose first statement is an unconditional `LeaveGroup()` that never consults `bZoning` | **disbanded** |
| `client_state = DISCONNECTED` set from inside the packet loop (**the oracle**) | the block at `client_process.cpp:620` runs first in the same iteration, sees `bZoning`, prints `"%s left the zone."`, calls `Group::MemberZoned()`, then `OnDisconnect(false)` | **kept** |

`Client::Disconnect()` is `inline void Disconnect() { eqs->Close(); client_state = DISCONNECTED; }`
(`zone/client.h:486`) — that one line is why the graceful hand-off wins the race.

### The second defect, which the first one exposed

**Fixing the teardown alone is not enough, and this unit measured that rather than
assuming it.** With the hand-off in place the source zone preserved the group
perfectly — and Testchar's `group_id` row was still deleted, at the transition, by the
DESTINATION side (`review/T322/c2_leader_zones_group_poll.txt`: present at t+150s,
gone at t+155s, while Oraclerogue's row and the leader row survived).

The deleter is **world**, not zone:

```
world/client.cpp:934   if (!is_player_zoning) {
                           GroupIdRepository::DeleteWhere(... character_id = X AND name = 'Y');
```

`is_player_zoning` is `(r->zoning == 1)` from the `zoning` byte at offset 188 of
`OP_SendLoginInfo`. fable computed it as
`(world().isZoneConnected() || world().zoneChangeRequested())` — **and both are
cleared by `CleanupZone()` → `resetZoneState()`, which runs inside
`DisconnectFromZone()` one line before the world reconnect.** So the byte was
**always 0**, on every login fable has ever made, including the zone-to-zone one.
Measured, not reasoned: `zoning=0` on all three logins in
`c2_leader_zones_A_testchar.log`, `zoning=1` on the transition login in
`e1_leader_zones_A_testchar.log`.

And a third consequence, found by running it: with `zoning = 1` eqemu skips the whole
character-select block (`world/client.cpp` 516, 523, 847) — guild list, char info and
**expansion info**. fable's trigger for sending `OP_EnterWorld` was
`WorldProcessExpansionInfo`, so the client reached "Post enter world received" and sat
there forever (`review/T322/d1_leader_zones_A_testchar.log`, ~9 minutes of
`SendPositionUpdate skipped: zoned_in=false`). `OP_PostEnterWorld` is the zoning-pass
trigger. The three changes are one change; none of them ships alone.

### The changes

`src/client/eq_zone.cpp`
- **`BeginGracefulZoneOut()`** (new): sends `OP_SaveOnZoneReq` (192-byte zero body)
  then `OP_DeleteSpawn(MySpawnId())`, disables zone reconnect (the server's close is
  now expected), arms `m_zone_out_handshake_pending` + a 2000 ms deadline. Carries the
  full mechanism as a block comment so the next reader cannot "simplify" it back.
  Returns false with no live `m_zone_connection` — nobody to hand off to, so the
  caller must fall through to the hard teardown.
  It also ignores a second call while one is in flight: eqemu answers one
  `OP_ZoneChange` with **two** approvals (its doubled `ServerOP_ZoneToZoneRequest`
  handler, normal — the oracle sees it too), and without the guard fable sent the
  hand-off twice.
- `ProcessDeferredZoneChange()` latches `m_zone_to_zone_in_progress = true` **before**
  the teardown.
- `DisconnectFromZone()` clears the hand-off flags — whatever brought us here, so no
  stale flag can wedge the next transition.
- **`zone_reason`** (the plan's ride-along): `RequestZoneChange()`'s last parameter is
  now `uint32_t zone_reason` (default 1 = ZoneUnsolicited) instead of `bool solicited`,
  and the solicited path echoes back the `type` the server handed us. eqemu always
  sends `type = 0x01` (`zoning.cpp` 983 and 1022); the source zone now logs
  `zone_reason [1]`, matching the oracle. Verified inert server-side (all ten
  references are log-format arguments) — wire fidelity only. It supersedes T52's
  reasoning, which picked the constant by argument rather than against the oracle.

`src/client/eq_movement.cpp` — `UpdateMovement()`'s zone-change branch calls
`BeginGracefulZoneOut()` and returns; a second branch waits for the acknowledgement
and runs `ProcessDeferredZoneChange()` on **ack or deadline**. The logout branch above
it is untouched and still runs first.

`src/client/eq_player_info.cpp` — `ZoneProcessLogoutReply()` routes the SAME opcode two
ways: while a hand-off is in flight it is an acknowledgement (no `RequestQuit()`, no
`m_pending_logout_disconnect`); otherwise the pre-existing logout behaviour, byte for
byte. Also clears the zoning latch on a real logout.

`src/client/eq_login_world.cpp` — the `zoning` byte reads `m_zone_to_zone_in_progress`;
`WorldProcessPostEnterWorld()` sends `OP_EnterWorld` on a zoning pass.

`include/client/eq.h`, `src/client/eq.cpp` — the two flags, the latch cleared when the
destination zone session comes up, and the test seams.

### The failure modes the plan named, and what was checked

| failure mode | what guarantees it |
|---|---|
| **leaked connection** | `DisconnectFromZone()` runs on ack **or** on the deadline — there is no path that arms the wait and never tears down. It also clears the flags itself, so the teardown is exactly once. Live: 6 transitions across 5 runs, every client exited 0, no accumulating sockets. |
| **closing too late / confusing the destination** | the ack arrived in **16-72 ms** in every live transition (e1: `.425` sent → `.470` acked → `.475` teardown), far inside the 2000 ms bound, and the destination zone-in is unchanged from before. |
| **zone change fails partway** | the deadline branch: `LOG_WARN` + the old hard teardown. The connection is still closed and the transition still proceeds. |
| **the logout path must still disconnect** | this is THE regression, and it is guarded three ways: the routing flag is only ever set by `BeginGracefulZoneOut()`, so with no hand-off in flight `ZoneProcessLogoutReply()` is unchanged; `tests/test_zone_out_handshake.cpp` pins both directions plus "a teardown clears the flag so a LATER logout is unaffected"; and live, A's `/camp` in both deliverable runs logged out, disbanded the group server-side and shut the client down (`review/T322/e1_logout_path_proof.txt`). |

### Two-client live acceptance — both directions, T320's Leg B shape

`A` = Testchar (zones), `B` = Oraclerogue (observer). Direction set by who invites.

| | T320 `b2_nogm` (before) | T322 `e1` leader zones | T322 `e2` non-leader zones |
|---|---|---|---|
| source zone sent a disband? | **YES**, two `groupActUpdate` action `06` | **NO** — zero `OP_GroupUpdate` in the slice | **NO** |
| observer's chat line | `Your group has been disbanded` | **`Testchar left the zone.`** | **`Testchar left the zone.`** |
| `group_id` rows across the transition | destroyed | **both intact**, `group_leaders` intact | **both intact** |
| zoning client's world login | `zoning=0` | `zoning=1` | `zoning=1` |
| arriving client's group state | phantom, then nothing | `In group (2 members), leader: Testchar` | `In group (2 members), leader: Oraclerogue` |
| **the two clients agree?** | **NO** | **YES** | **YES** |
| group chat across the zone boundary afterwards | n/a (no group) | **both directions** | **both directions** |
| `/camp` afterwards still logs out + disbands | yes | **yes** | **yes** |

The cross-zone `/gsay` landing on the other client is the strongest evidence available
to a headless client and is stronger than a screenshot of a group window: it proves the
group is alive **on the server** and that both clients are in it.

**Deviation, flagged:** the plan asked for screenshots. fable's acceptance clients are
headless (`--no-graphics`), the same shape T320 used for its own Leg B table, so there
is no UI to photograph. Replaced by the `group_id`/`group_leaders` poll (every 5 s
across each whole run) plus the cross-zone chat above. The oracle screenshots the plan
compares against are T320's and are unchanged.

### Verification

| step | result |
|---|---|
| `cmake --build build -j24` (GLES2 test tree) | **EXIT=0** |
| `ctest --test-dir build -j6 --output-on-failure` | **3506/3507**, sole failure `AudioManagerTest.PreloadCommonSounds` (the documented ALSA host quirk, the only entry in `refactor/test_baseline.txt`) — **zero new failures** |
| `scripts/build-gles2-x86.sh` | **EXIT=0** |
| `scripts/ci/visual_gate.sh` | **=== VISUAL GATE: PASS ===**, goldens untouched (`git status` shows only the six source files, `tests/CMakeLists.txt` and the new test) |
| `scripts/build-arm-noble.sh` | **EXIT=0** |
| `scripts/refactor/check_file_sizes.sh fable` | 13 checked, 7 ok / 5 warn / 1 fail — **no new FAILs**; the single FAIL is `graphics/irrlicht_renderer.cpp`, which this unit did not touch. Touched files: `eq_zone.cpp` 1094 ok, `eq_login_world.cpp` 945 ok, `eq.cpp` 1026 ok; `eq.h`, `eq_movement.cpp`, `eq_player_info.cpp` were already WARN before this unit and are unchanged in status |
| live: 5 two-client fable runs (10 client processes) | all exited 0 |

New test: `tests/test_zone_out_handshake.cpp`, 8 cases — the exact hand-off packets and
their order, the entity id they carry, the wait being armed instead of an immediate
teardown, the no-connection fallback, **both** `OP_LogoutReply` routings, the flag
being cleared by the teardown, and the zoning latch surviving `DisconnectFromZone()`
while a logout clears it.

### Server state: restored exactly

`review/T322/db_before.txt` is the state as found (read before anything ran);
`db_after.txt` is what was left. `diff` is empty apart from a stray query-error line in
the `before` capture. Testchar (65) back to `zone_id=2, -74/428/3.751, h160`.
`group_id` and `group_leaders` emptied (both empty on arrival; the rows were this
unit's staging). `logsys_categories` 83/84 `log_to_file` back to 0.

**⚠ THIRD SIGHTING, flagged and unchanged:** `Oraclerogue`'s persisted `gm` flag was
found at **1** again — T297, T320, and now T322, three consecutive units, none of
which issued anything that could set it. Left at 1. Something unidentified is writing
it; this is no longer plausibly a stale value from one forgotten session.
`Oracledwarf` (82) and every `*-deleted-*` character untouched.

### Found and deliberately not fixed

- **`m_zone_to_zone_in_progress` is not cleared if a transition dies between the world
  reconnect and the destination zone session.** It is cleared on zone-connect and on
  logout; a transition that fails in between leaves the latch set, so a subsequent
  world login would claim `zoning=1`. A client in that state is already wedged (it has
  no zone), and adding a second timeout to a path this unit could not make fail live
  would be untested code guarding an unobserved case. Named here rather than guessed at.
- **T320's assumption that eqemu re-establishes group state on zone-in "when the group
  still exists" was never exercised** — under T320 the group was always gone by then.
  It is exercised now and it works (`RefreshGroupFromDB` → `groupActUpdate`, both
  directions), but only because the `group_id` row survives, which is defect 2 above.
  Worth knowing that `CleanupZone()` → `ClearGroup()` depends entirely on the world
  flag being right.
- **fable sends an empty 2-byte `OP_ZoneEntry` immediately before the real 70-byte
  one** (visible in every destination-zone packet log). eqemu ignores it. Not chased.
- **The second `OP_LogoutReply`** the server queues from `OnDisconnect(false)` arrives
  after fable has already torn the connection down, so it is never delivered. Harmless;
  said out loud because a reader watching the wire will see two.
