# T346 — The self-death sequence matches the official client (the remainder T344 recorded)

Class A. Slot3. T311 Leg A measured the official Titanium client's death:
(1) C→S `OP_SpawnAppearance` type 14 (`AT_ANIMATION`) param 115 (Lying)
answering our own `OP_Death`; (2) NO further `OP_ClientUpdate` while
dead; (3) C→S `OP_ZoneChange` to the bind point followed by a FULL
zone-in handshake, even into the zone we are already in. T344 wired
`ApplyPositionState(POS_DEAD)` from `ZoneProcessDeath` for self and
proved no `AT_DIE` is sent; it left the three steps above owed and
recorded that `ApplyPositionState(POS_DEAD)`'s `StopMovement()` fires one
last `SendPositionUpdate()`.

## Measure first

Replay T311's death capture (`review/T311/legA/decode/` — the opcode
timeline TSV and the pcap) through `zonetest::ReplayZone` and diff
fable's outbound sequence after `OP_Death` against the oracle's: which of
(1)/(2)/(3) fable already does, and what it sends that the oracle does
not. Paste the two sequences.

## Fix, one step per commit

1. **Lying appearance on death:** on self `OP_Death`, send exactly one
   `AT_ANIMATION` 115 (the same `SendSpawnAppearance` the user path uses)
   — through `SetPositionState`'s send half, or a dedicated call in the
   death handler; record which.
2. **No client updates while dead:** a `POS_DEAD` gate in the movement
   update pump (`sendPositionUpdateToServer` phase — ⚠ G06's golden pins
   `renderer_player_physics.cpp`; the gate probably belongs on the GAME
   side in `MovementComponent`'s send path, which is not golden-pinned —
   measure where the last update originates and gate there), including
   the `StopMovement()` one T344 found. Test: after self death, zero
   `OP_ClientUpdate` on the outbound seam for N ticks.
3. **Bind re-entry:** on self death, after (1), send `OP_ZoneChange` to
   the bind point (the fields the oracle's packet carries — copy them
   from the capture) and drive the existing zone-transition path
   (`ZoneComponent`'s `RequestZoneChange`/`ProcessDeferredZoneChange` —
   K09/K10 kept them) so the full handshake runs even for the same zone.
   Test: the replayed capture produces the same outbound opcode sequence
   as the oracle's (a golden extracted from the T311 TSV; size asserted;
   fault planted by dropping step 1's send).

Live proof: owed unless the lock is free — die once (`/say #kill` on
self or a GM-summoned mob), watch the client respawn at bind with a
full zone-in; `review/T346/`.

STOP: step 3 needs the zone-transition path changed beyond a new caller;
the oracle capture shows a step fable's architecture cannot reproduce
without a design change (report the sequence).

Territory: `src/client/player/player_state.cpp` (death handler),
`src/client/movement/` (the send gate), `src/client/zone/` (only a call
into the existing transition), the test + golden, `refactor/BOARD.md`.
No graphics files unless the gate lands in the physics TU (then the
golden learns it in place and gate + ARM run).

## Review

Branch `task/T346-self-death-sequence` off `fable` at `b6671619`.
Commits: `2aa0fdc8` (step 1), `68232f31` (step 2), `5551c020` (step 3).

### Measure first — the two sequences

**The oracle.** T311 Leg A's capture has FOUR self-deaths, not one, and all
four have the identical client-to-server shape. The window (the 20:12:31
record, `T311A_table2_opcode_timeline.tsv` lines 1032-1047; the other three
at 20:20:04, 20:20:24, 20:22:43):

```
S->C OP_Death              (spawn 0x111 = ours)
S->C OP_ZonePlayerToBind   (33 B: bind_zone_id 1, instance 0,
                            x -510.0, y 57.0, z 31.75, h 0, "South Qeynos")
C->S OP_FloatListThing     [heartbeat]
C->S OP_Save               (192 B)
C->S OP_SpawnAppearance    11 01 0e 00 73 00 00 00  = AT_ANIMATION, 115 Lying
C->S OP_FloatListThing     [heartbeat]
C->S OP_ZoneChange         (88 B)
S->C OP_ZoneChange x2      (approval)
C->S OP_DeleteSpawn        11 01 00 00
S->C OP_LogoutReply, OP_DeleteSpawn, OP_UpdateAura, OP_LogoutReply
   ... then a full zone-in: C->S OP_ZoneEntry, S->C OP_PlayerProfile,
       OP_ZoneSpawns, OP_ReqNewZone/OP_NewZone, OP_ReqClientSpawn, ...
```

**Zero C->S `OP_ClientUpdate` anywhere in that window**, in any of the four,
against 1522 of them across the session.

Three findings the plan did not have, all from re-reading the capture:

1. **The `OP_SpawnAppearance` param is identical in all four** (`0e 00`,
   `73 00 00 00`), so 115/Lying is not a one-sample reading.
2. **Three of the four deaths were SAME-ZONE.** They happened in the
   `qeynos` capture (South Qeynos, zone 1) and their `OP_ZonePlayerToBind`
   carries `bind_zone_id` 1 — the zone the client was already in — and the
   client still sent `OP_ZoneChange` and ran a full zone-in. That is the
   measured proof of the contract's "even into the zone we are already in",
   and it is the exact opposite of T231's rule for
   `OP_RequestClientZoneChange`. Both are now pinned so nobody harmonises
   them.
3. **`OP_Save` and C->S `OP_DeleteSpawn` occur EXACTLY at the four self
   deaths and nowhere else** in the whole capture — they are death steps,
   not housekeeping. `OP_FloatListThing` is the opposite: 1291 sends
   session-wide, a heartbeat. Handling of each is recorded in the golden's
   header; see "What is NOT compared" below.

**fable, before this unit** (the same capture replayed through
`ReplayZonePacket`, four pump ticks either side of `OP_ZonePlayerToBind`):

```
C->S OP_ClientUpdate
C->S OP_ClientUpdate
C->S OP_ClientUpdate
C->S OP_ClientUpdate
C->S OP_ZoneChange
C->S OP_ClientUpdate
C->S OP_ClientUpdate
C->S OP_ClientUpdate
C->S OP_ClientUpdate
```

So of the three steps, fable already did **(3)** and neither of the other
two: no Lying appearance at all, and the update pump ran straight through
the death.

### The seam, and why the obvious one would have lied

T344's seam is the `DaybreakConnection`'s `m_streams[0].sent_packets`. That
map is the RELIABLE resend buffer. `OP_ClientUpdate` is sent **unreliable**
(`SendPositionUpdate` → `SafeQueueZone(p, 0, false)` → `InternalQueuePacket`
→ `InternalBufferedSend`, never touching `sent_packets`). A step-2 test
written on that seam would have reported "zero `OP_ClientUpdate` while dead"
as a PASS with no gate in the code — which is exactly what the first
measurement run did before the seam was fixed.

The ordered seam is therefore the T268 capture seam
(`Test_SetCaptureQueuedPacketsForSeam` / `Test_GetQueuedPackets`), the one
`test_zone_out_handshake.cpp` uses, widened by step 1 to the two senders it
did not cover: `PacketSender::SafeQueueZone` and
`EntitiesComponent::SendSpawnAppearance` (which bypasses `PacketSender`
entirely). Both record before every gate, exactly as `Queue()` already did,
and are no-ops unless armed.

One harness note, recorded rather than hidden: `SafeQueueZone` refuses to
send over a connection whose stats are all zero ("appears invalid"), which
is what a `FakeUdpTransport` connection that has never round-tripped looks
like. The harness sets `conn->m_stats.sent_packets = 1` so the code under
test is what decides, not that guard.

### Step 1 — the Lying appearance

`src/client/player/player_state.cpp`, `ZoneProcessDeath`'s self branch:
`ctx_.Entities().SendSpawnAppearance(AT_ANIMATION, ANIM_LYING)` after
`ApplyPositionState(POS_DEAD)`.

**A dedicated call in the death handler, not `SetPositionState`'s send
half** — the contract asked which, and this is why: that arm is also
reachable from the debug `setPositionState(4)` action, where we are not
actually dead and telling the server we are lying down would be a lie.
T344's `UserDeadState_NeverSendsAtDie` pins the arm as silent and is
untouched. Only a real `OP_Death` for our own spawn id produces this packet.

Pre-fix failure:

```
[  FAILED  ] SelfDeathSequence.SelfDeath_SendsExactlyOneLyingAppearance
  Expected equality of these values:
    appearances.size()   Which is: 0
    1u                   Which is: 1
  the official client answers its own OP_Death with exactly one
  OP_SpawnAppearance (T311 Leg A, all four self-death windows)
```

`ForcedPoseNoEcho.T311SelfDeathCapture_SetsDeadAndSendsNoAppearance` is
renamed `..._SetsDeadAndSendsLyingNotAtDie` and its second assertion
changed, deliberately: T344 wrote it as "no appearance at all goes out from
the death path IN THIS UNIT; the oracle's Lying + `OP_ZoneChange` re-entry is
a separate unit", and named this unit as the successor. What it was really
pinning — that `AT_DIE` never goes out — is still asserted, there and in
`UserDeadState_NeverSendsAtDie`.

### Step 2 — silence while dead

The gate is the first statement of `MovementComponent::SendPositionUpdate()`
(`src/client/movement/movement_update.cpp`), released in
`ZoneComponent::ZoneProcessPlayerProfile` (`src/client/zone/zone_entry.cpp`).

**It is NOT in `renderer_player_physics.cpp`, and that is measured too.**
`sendPositionUpdateToServer()`'s phase 7 only builds a
`PlayerPositionChanged` intent and calls `ctx_.bridge->pushIntent()`; the
actual send is `bridge_component.cpp:782`'s `ctx_.Movement()
.SendPositionUpdate()`. Every other producer of a C->S `OP_ClientUpdate` —
`StopMovement`, the teleport/relocate senders, the movement commands, the
`/loc`-style paths — calls the same function. Gating the renderer would have
caught one of them and made G06's order golden learn a network rule.
**No graphics file is touched and no golden was re-blessed**; `git diff
--name-only $(git merge-base fable HEAD)..HEAD` is listed under Step results.

The gate also catches the update **T344 recorded as owed**:
`ApplyPositionState(POS_DEAD)` takes its `StopMovement()` arm, which ends in
a `SendPositionUpdate()` of its own. It works because `ApplyPositionState`
writes `positionState` *before* the switch, and
`WhileDead_NoClientUpdateEverGoesOut` now sets `setMoving(true)` first so the
arm is actually taken (with `isMoving()` false it never fires, which is why
the very first measurement run showed 8 updates rather than 9).

The release matters as much as the gate: **nothing in the client ever
cleared `POS_DEAD`.** Without it the gate is permanent and a respawned
character never transmits its position again for the rest of the session.
`ApplyPositionState(POS_STANDING)`, not `SetPositionState` — the server is
telling us, so there is nothing to tell it back (T344's rule).

Pre-fix failures:

```
[  FAILED  ] SelfDeathSequence.WhileDead_NoClientUpdateEverGoesOut
  h.countOpcode(HC_OP_ClientUpdate)   Which is: 8
  0u                                  Which is: 0
[  FAILED  ] SelfDeathSequence.AfterZoneInPlayerProfile_ClientUpdateResumes
  Expected: (...positionState()) != (PositionState::Dead),
  actual: <04-00 00-00> vs <04-00 00-00>
  zoning in clears Dead; otherwise the death gate is permanent
[  FAILED  ] SelfDeathSequence.T311Capture_OutboundSequenceMatchesTheOracle
  actual.size()  Which is: 10      expected.size()  Which is: 2
```

### Step 3 — the bind re-entry: measured to need no fix

`ZoneProcessZonePlayerToBind` has called `ZoneComponent::RequestZoneChange`
unconditionally since before this unit, and `ZoneProcessZoneChange`'s
approval arms `zoneChangeApproved` for `ProcessDeferredZoneChange` (K09/K10)
with no same-zone short circuit. The replayed capture produced the C->S
`OP_ZoneChange` in the **measurement run, before any change** — see the
"fable, before this unit" sequence above, which already contains it.
**No production edit was needed in step 3 and none was made.** Reporting a
step as already-correct rather than inventing work for it is the honest
outcome; the plan's own instruction was to measure first.

What step 3 adds is the regression net, which guards a real trap:
`SameZoneDeath_StillRunsTheFullHandshake` (zone 1, bind zone 1 — the
measured case) and `ApprovedZoneChange_ArmsTheDeferredTransition`. T231
established that answering an `OP_RequestClientZoneChange` naming our own
zone triggers eqemu's `SendZoneCancel` teardown; the bind path is the
opposite and must stay so. Both were green when written — preservation pins,
by construction, exactly as T344's leg (b) was.

Interaction worth recording: T55's hazard in `ZoneProcessZonePlayerToBind`
(a locally applied bind position transmitted straight back to the OLD zone
by the next `SendPositionUpdate`) is now **moot on the death path**, because
step 2's gate makes that update never happen.

### The golden

`tests/goldens/self_death_outbound_g07.txt`, produced by
`scripts/refactor/t311_death_golden.py` from the T311 TSV. The script finds
self-deaths by what only our own death causes — the server answering with
`OP_ZonePlayerToBind` (spawn_id == killer_id is NOT the discriminator; two
of the four were killed by another spawn) — and **exits non-zero unless all
four windows produce an identical sequence**. They do:

```
C->S OP_SpawnAppearance type=14 param=115
C->S OP_ZoneChange
```

**What is NOT compared, and why** (all in the golden's own header):

- `OP_FloatListThing` — heartbeat, not a death step.
- `OP_Save` — the oracle *does* send one on every self death, and this is a
  **real gap**: fable's `PlayerComponent::SendSave()` queues a bare 2-byte
  opcode, the oracle's is 192 bytes of character payload. Out of this unit's
  three steps; flagged below.
- `OP_DeleteSpawn` — sent after the server APPROVES the change, from the
  zone teardown (fable: `CleanupZone`), which needs the full handshake this
  headless test does not run. Already pinned by T322's
  `test_zone_out_handshake`.
- The `OP_ZoneChange`'s `zone_id` — the TSV keeps only the first 32 payload
  bytes and the field is at offset 64, so the capture cannot prove it. The
  bind zone is pinned separately from the `OP_ZonePlayerToBind` bytes, whose
  whole 33-byte body IS in the capture
  (`ZonePlayerToBind_RequestsTheBindZoneChange`, `zone_id == 1`).

**Faults planted and caught** (both verified, then reverted):

- dropping step 1's `SendSpawnAppearance` → `actual.size() 1 vs expected 2`,
  with the diff printed (`C->S OP_ZoneChange` alone);
- appending one line to the golden → the size assertion fires with its
  do-not-re-bless message (`expected.size() 3 vs 2u`).

### Step results

- `cmake --build build -j24` — **exit 0**, zero `error:` lines.
- `ctest --test-dir build -j6 --output-on-failure` — **3,830 of 3,831
  passed**; the single failure is `AudioManagerTest.PreloadCommonSounds`, the
  expected ALSA host quirk. `visual_gate` **passed** under `-j6`. Suite count
  up 9 — the new file, nothing removed.
- Named suites re-run individually, all PASS, assertions untouched except the
  one T344 assertion documented above:
  `test_eq_action_handler_transforms_real` 28, `test_game_state_source_writes`
  68, `test_zone_out_handshake` 8, `test_zone_connection_flow` 35,
  `test_forced_pose_no_echo` 8, `test_reachability_sweep` 5,
  `test_movement_component` 6, `test_movement_order` 4 (**G06's golden, still
  green and NOT re-blessed**), `test_self_death_sequence` 9.
- `bash scripts/refactor/check_file_sizes.sh --all` — `checked 879 file(s):
  879 ok, 0 warn, 0 fail`.
- gles2 / visual-gate-specific / ARM runs **skipped: no graphics file is
  touched.** `git diff --name-only $(git merge-base fable HEAD)..HEAD`:
  `scripts/refactor/t311_death_golden.py`,
  `src/client/entities/entities_component_world.cpp`,
  `src/client/movement/movement_update.cpp`, `src/client/packet_sender.cpp`,
  `src/client/player/player_state.cpp`, `src/client/zone/zone_entry.cpp`,
  `tests/cmake/zone_integration.cmake`,
  `tests/goldens/self_death_outbound_g07.txt`,
  `tests/test_forced_pose_no_echo.cpp`, `tests/test_self_death_sequence.cpp`.

### Live proof — OWED, and the lock is ambiguous

Not run. `/mnt/projects/fable/.eqemu-lock` **does not exist** and no
`zone`/`world` process is running, but this unit was dispatched with slot2
described as holding that lock for a live measurement. Rather than take a
server another slot's in-flight unit may be about to bring up, the live run
is left owed: die once (`#kill` on self or a GM-summoned mob), watch the
client send Lying, go silent, and respawn at bind through a full zone-in;
`review/T346/`. **This is the THIRD time this week the lock has been found
absent or stale while a slot was said to hold it** (memory records two
earlier); worth a look at whether the protocol is actually being followed.

### STOPs

Neither STOP fired.

- **The zone-transition path needed no change beyond an existing caller** —
  in fact it needed no new caller either; see step 3.
- **Nothing in the oracle capture is unreproducible by fable's
  architecture.** Two steps of the capture are reproducible but not
  reproduced, and both are recorded rather than quietly dropped: the
  192-byte `OP_Save` (fable's is 2 bytes) and the fact that `OP_Save` and
  C->S `OP_DeleteSpawn` are death steps rather than housekeeping.

### For the board / next unit

`PlayerComponent::SendSave()` sends a bare 2-byte `OP_Save`. The official
client's is a 192-byte body, and it sends one on **every self death** (four
for four in the capture) as the first packet of the death burst — fable
sends none, from anywhere on the death path. Not raised as a board item by
this unit because it is outside its three steps, but it is the next thing
the capture says is wrong.
