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

---

## Review (executor, slot1, 2026-09-02 05:20-07:10 UTC)

Branch `task/T320` off `fable` `a6811471` ("T320 plan: settle the group-disband-on-zone
against the oracle..."). Evidence: `/mnt/projects/fable/review/T320/` (README there).

eqemu lock taken with `noclobber`, exit 0, server was **down** and **no lock held** on
arrival (so `up.sh --launcher` was the correct start, not the forbidden start-on-top-of-a-
running-fleet). One loginserver, one world, confirmed. `down.sh` clean, lock deleted.

### Leg A — VERDICT: OURS. The oracle's group survives; fable's does not.

Two official Titanium clients under Wine/Xvfb, **Oraclealpha** (willeq_a, :99) and
**Oracledelta** (willeq_d, :98), both in qeynos2 at the same spot, grouped in-game
(`/invite Oracledelta`, then `/invite` to accept). Group formation confirmed
server-side, not by screen-reading: `group_id` gained rows for chars 74 and 78 with
`group_leaders.leadername = Oraclealpha`.

| run | who zoned | group_id after | source zone sent a disband? |
|---|---|---|---|
| oracle, leader zones | Oraclealpha, qeynos2 -> qeynos | **both rows intact, leader intact** | **NO** — zero `OP_GroupUpdate` in the slice |
| oracle, non-leader zones | Oracledelta, qeynos2 -> qeynos | **both rows intact** | **NO** — zero `OP_GroupUpdate` in the slice |
| fable, leader zones (`#gm on`) | Testchar, befallen -> qeynos | destroyed | YES — two `OP_GroupUpdate` size 454, action `06` |
| fable, leader zones (**gm=0**) | Testchar, befallen -> qeynos | destroyed | YES — same |

The observing official client says it in one line
(`review/T320/legA_03_B_after_leader_zoned_STILL_GROUPED.png`):

    Oraclealpha left the zone.

...with the group window still listing both members. That is the
`"%s left the zone."` line T297 recorded as never appearing — it appears for the
official client and never for fable, and it is the whole discriminator.

**So the disband is fable's to fix, not upstream's.** Per the plan, Leg A stops at the
verdict; the fix is a follow-up client unit. Three things it should not have to
rediscover:

1. **T297's narrowing was close but WRONG.** It pointed at `client_process.cpp:639`
   (`if (!bZoning) { ... LeaveGroup(); }`). That block never runs: neither
   `"%s logged out."` nor `"%s left the zone."` nor `"%s has gone linkdead."` appears
   anywhere in fable's source-zone log. The real site is **`Client::OnDisconnect(true)`**
   (`zone/client_process.cpp`), whose FIRST statement is an unconditional
   `LeaveGroup()` — it does not consult `bZoning` at all — and `LeaveGroup()` disbands
   at `MemberCount < 3`. The log order is the proof, exactly as OnDisconnect executes:
   two disband packets, then `OP_UpdateAura` (`RemoveAllAuras()`), then
   `OP_LogoutReply`, then `LogInfo("Client linkdead: Testchar")` from the caller at
   `client_process.cpp:583`.

2. **The trigger is fable's own teardown.** `EverQuest::DisconnectFromZone()`
   (`src/client/eq_zone.cpp:473`, from `ProcessDeferredZoneChange()`) closes the source
   zone's `DaybreakConnection` **8 ms after the approval** — A's log:
   `06:11:49.639 Zone change APPROVED` / `.647 Disconnecting from current zone server`.
   That drops the session out of `ESTABLISHED`, which is the condition on
   `client_process.cpp:583`. The official client leaves the old session up and lets the
   server finish zoning it. **That is the one behavioural difference to close.**

3. **Two of T297's open threads are now closed as non-issues.**
   The `ServerOP_ZoneToZoneRequest` handler running **twice** per `OP_ZoneChange` is
   normal eqemu — the official client's log shows the identical doubling.
   And `#gm on` is **not** the cause: fable disbands with `character_data.gm = 0` too
   (run `b2_nogm`), which was worth measuring because T297's zoning client was GM in
   both of its runs while the oracle's was not — a live confound that could have
   voided the verdict.

**One wire-fidelity difference found and deliberately NOT fixed:** fable echoes
`zone_reason = 0` in `OP_ZoneChange` where the official client echoes back the `type`
it was handed in `OP_RequestClientZoneChange` (`1`). Checked before reporting it as a
lead: every one of eqemu's ten `zone_reason` references is a log-format argument, so it
changes nothing. Recorded, not chased.

### Leg B — the phantom group. Fixed, and the partial-update publisher is named.

**What published the partial update: nothing in our publish path — the packet was
being read with the wrong struct.** eqemu sends **two different structs** on
`OP_GroupUpdate`, and **both are 452 bytes**, so no size check can tell them apart:

| action | struct | layout |
|---|---|---|
| `groupActJoin` (0), `groupActLeave` (1) | `GroupJoin_Struct` | action(4) yourname[64] membername[64] +padding |
| everything else | `GroupUpdate_Struct` / `GroupUpdate2` | action(4) yourname[64] membername[5][64] leadersname[64] |

`ZoneProcessGroupUpdate` handled `GROUP_ACT_JOIN` in the same `case` as
`GROUP_ACT_UPDATE` and read `leadersname` at offset 388 — which in a `GroupJoin_Struct`
is padding, always zero. **That is the empty leader**, and `membername[0]` at offset 68
lining up with the join struct's single `membername` is why the count still came out
right. Hence "2 members, leader: <empty>".

**It has nothing to do with zoning** — the plan suspected as much, and it is worse than
suspected: it fires at *every* group formation, on the *leader's* client, because
Titanium's inviter is told a member joined by `groupActJoin` **and nothing else**
(eqemu sends the inviter a full refresh only on SoD+, `zone/client.cpp:5206`). Measured
at the same instant on both clients, no zoning involved:

    b1_gmon_A_testchar.log:1360     Group update: 2 members, leader:
    b1_gmon_B_oraclerogue.log:314   Group update: 2 members, leader: Testchar

Fixes, all in `src/client/eq_group.cpp` / `eq_zone.cpp`:

- **`ZoneProcessGroupJoin()`** (new): reads `GroupJoin_Struct`, adds the named member
  *incrementally*, never rebuilds the roster, never reads a leader the packet does not
  carry. A JOIN arriving while ungrouped means we created this group by inviting — the
  only way to be an existing member of a brand-new group — so that is where the client
  learns it leads, which is the only signal Titanium gets.
- **`GROUP_ACT_LEAVE`** now reads `GroupJoin_Struct` explicitly. It was right by luck
  (same offset 68); said out loud rather than left to be rediscovered.
- **Ordering.** `setInGroup(true)` fires a `GroupChanged`, and it used to fire straight
  after `ClearGroup()` — so the first thing every consumer saw for a new group was
  `inGroup=true, memberCount=0, leader=""`. It is now the **last** call on both paths,
  so the only in-group `GroupChanged` published is a complete one.
- **`CleanupZone()` now calls `ClearGroup()`.** The client stops trusting what it
  carried across a transition; eqemu's zone-entry path re-establishes the truth
  (`zone/client_packet.cpp:765` -> `RefreshGroupFromDB`, a full `groupActUpdate`) when
  the group still exists, and when it does not, "not in group" is the right answer.
- **`/gsay` with no group now says so.** The plan's complaint was that the client would
  "`/gsay` into a dead group with no error"; the server drops a group message from an
  ungrouped client silently, so the client has to be the one to speak.

**Two-client live proof** (`b4_final_*`, fixed binary, both clients' logs):

| | before (`b2_nogm`) | after (`b4_final`) |
|---|---|---|
| A (leader) at formation | `In group (2 members), leader: ` | `In group (2 members), leader: Testchar` |
| B (invitee) at formation | `leader: Testchar` | `leader: Testchar` |
| **the two clients agree?** | **NO** | **YES** |
| A after zoning | `In group (2 members), leader: ` — phantom | *(no group state at all)* |
| B after A zoned | `Your group has been disbanded` | `Your group has been disbanded` |
| A's `/gsay` after zoning | vanishes silently | `You must be in a group to use that command.` |

Tests: `tests/test_group_packet_shapes.cpp`, 9 cases, including the guard the plan asked
for — **`NoPublishedGroupEverHasMembersWithoutALeader`** replays a JOIN/JOIN/UPDATE/LEAVE
sequence and fails if any published `GroupChanged` carries members with an empty leader.

### Leg C — one channel-name table. There turned out to be THREE.

`ChatChannelName()` (declared in `eq.h`, defined at the top of `eq_chat.cpp`) is now the
only mapping; all consumers call it. `CHAT_CHANNEL_TELL_ECHO = 14` added.

The plan named two drifted chains. **The guard test found a third on its first run** —
in `ZoneSendChannelMessage`'s outgoing debug line, drifted differently again: it had
auction, but no emote/petition/gmsay/raid/broadcast, and fell back to a bare `"unknown"`
that discarded the channel number. Three copies, three different answers.

`tests/test_chat_channel_names.cpp`, 5 cases. The guard looks for the *shape* of an
inline name table (`channel == CHAT_CHANNEL_X ? "x"`) rather than counting comparisons,
so routing checks (tell tracking, the new ungrouped-`/gsay` guard, T243's emote
special-case) don't need a budget that would drift on its own.

`tests/test_zone_chat.cpp`'s `ChannelMessage_UnknownChannelMapsToOther` **changed
contract** (flagged, not silently absorbed): it asserted the flat `"other"` fallback.
That fallback is what hid channel 14 — the bridge said `other` while the console said
`chan14`, and only the console form was diagnosable. Renamed to
`ChannelMessage_UnknownChannelKeepsItsNumber`, now expects `chan77`.

### Verification

| step | result |
|---|---|
| `cmake --build build -j24` (GLES2 test tree) | **EXIT=0** |
| `ctest --test-dir build -j6 --output-on-failure` | **3467/3468**, sole failure `AudioManagerTest.PreloadCommonSounds` (the documented ALSA host quirk, and the only entry in `refactor/test_baseline.txt`) — **zero new failures** |
| `scripts/build-gles2-x86.sh` | **EXIT=0** |
| `scripts/ci/visual_gate.sh` | **=== VISUAL GATE: PASS ===**, goldens untouched (`git status` shows only source + test files) |
| `scripts/build-arm-noble.sh` | **EXIT=0** |
| `scripts/refactor/check_file_sizes.sh fable` | 27 checked, 17 ok / 6 warn / 4 fail — **no new FAILs**; all four are pre-existing files this unit did not touch. Touched files: `eq_chat.cpp` 645, `eq_group.cpp` 760, `eq_zone.cpp` 993 — all ok |
| live: 4 two-client fable runs + 2 two-official-client oracle runs | all clients exit 0 |

### Server state: restored exactly

`review/T320/db_before.txt` records the state as **found**, read before anything ran.
Restored: Testchar (65) and Oraclealpha (74) -> zone 2, `-74/428/3.751`, h160;
Oracledelta (78) -> zone 2, same, h0; Oraclerogue (81) -> zone 36, `-259/-1201/-5.75`,
h0. `group_id` and `group_leaders` emptied (both were empty on arrival; the rows were
this unit's staging). `logsys_categories` 83/84 `log_to_file` back to 0 (set to 3 for
packet evidence). Testchar's `gm` back to 0 (this unit set it with `#gm on` for run
`b1_gmon`, then cleared it deliberately to run the no-GM discriminator).

**Deviation, flagged, unchanged:** `Oraclerogue`'s persisted `gm` flag was **again found
at 1** and left at 1 — the same anomaly T297 flagged, still with no recorded prior value
and nothing this unit issued able to set it. `Oracledwarf` (char 82) and every
`*-deleted-*` character untouched.

### Found and deliberately not fixed

- **The disband itself** (Leg A). Ours, fully narrowed above, out of this unit's scope
  by the plan's own wording — needs its own unit against
  `EverQuest::DisconnectFromZone()`.
- **`zone_reason` echoed as 0** instead of the server's `type`. Verified inert (log-only
  in eqemu) before being written down.
- **`/gu` (guild) fails the same silent way** as `/gsay` did when you are not in a
  guild. Not guarded, because the client does not track guild membership as reliably as
  `GroupState` tracks the group, and guessing there would trade a silent failure for a
  wrong error message.
