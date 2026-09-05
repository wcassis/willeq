# T378 — the 70 zone-stream direct senders go through PacketSender (B145)

Class A (ten are the B140 crash shape), slot1, no server. Read the B145
board text (T376's census, per file) and T376's §Review — especially
surprise 1: `SafeQueueZone` refuses a zero-stats connection, so every test
harness that observes BELOW it needs `conn->m_stats.sent_packets = 1`
(the line `PoseHarness`, `DeathHarness`, `CombineHarness` carry), while
`PacketSender::Queue` has no such guard.

1. Re-take the census on your tip (comment-filtered grep for
   `->QueuePacket(` under `src/client`, `include/client`, excluding
   `packet_sender.cpp` and the K13 `m_eq->QueuePacket` forwarder). The
   number must match T376's 70 or the difference is explained.
2. Route them family by family, ONE COMMIT PER FAMILY (player, zone
   handshake, merchant, gm, group, chat, movement, core): each direct
   `ctx_.zone_connection->QueuePacket(p, a, b)` becomes
   `ctx_.net.SafeQueueZone(p, a, b)` with the same arguments; delete the
   now-redundant `if (ctx_.zone_connection …)` wrappers ONLY where the
   wrapper guards nothing else (read each one). The ten crash-shape sites
   first.
3. Tests: (a) a table-driven test that every routed sender, called with
   no zone connection, returns without crashing (T376's
   `ClickSendersPacketSender.NoZoneConnection_*` shape — one case per
   family, listing its senders); (b) for each family, one recorded-session
   assertion that its most user-visible packet appears in the `.sleq`
   (pre-fix failing: 0 records). Every existing harness that goes red on
   the stats guard gets the one-line arm, no assertion weakened.
4. After the last family: the census returns ZERO outside `PacketSender`
   and the login/world TU (B146, separate). Add the census as a
   source-reader pin (`tests/test_direct_sender_census.cpp`) so a new
   direct sender fails ctest.
5. Order goldens for every TU touched (G01 zone, G07 self-death, G08
   OP_Save, the bridge intent table, movement G06): run them all; a
   statement-text golden learns a reviewed edit IN PLACE, both faults
   re-planted. `SendZoneChange` in `everquest.cpp` is on the zoning path —
   the zone-in tests must stay green.
6. Strike B145.

## Proof

Build, `ctest -j6`, the tests with pre-fix/post-fix runs per family, all
goldens, file sizes. No graphics change.

STOP: a sender's arguments do not map onto `SafeQueueZone`'s signature
(report it; route the rest).

## Review

Executed 2026-09-05 in `wt/slot1`, branch `task/T378-direct-senders` off
`fable` at `6a1c8a38` (verified). Ten commits: eight families, the census pin,
the fixture arm. No STOP condition fired. No graphics change, no server, no
re-bless; `git status --porcelain tests/goldens/` is empty.

| commit | family | sites | files |
| --- | --- | --- | --- |
| `13e60ab8` | chat | 2 | `src/client/chat/chat_component.cpp` |
| `835ca086` | group | 6 | `src/client/group/group_component.cpp` |
| `f8205cba` | movement | 1 | `src/client/movement/movement_state.cpp` |
| `4774190e` | player | 25 | `src/client/player/player_misc.cpp` 17, `src/client/player/player_component.cpp` 5, `src/client/player/player_state.cpp` 1, `src/client/player/player_buffs.cpp` 2 |
| `8fc1005b` | zone handshake | 12 | `src/client/zone/zone_handshake.cpp` |
| `2794291a` | merchant | 13 | `src/client/merchant/merchant.cpp` 9, `src/client/merchant/merchant_vendor_bank.cpp` 4 |
| `ee2ac431` | gm | 9 | `src/client/gm/gm_component.cpp` |
| `f2e234c8` | core | 1 | `src/client/core/everquest.cpp` |
| `6e4f9d9e` | — | census pin | `tests/test_direct_sender_census.cpp` |
| `9bad7fe4` | — | fixture arm | four test harnesses |

**69, not 70, and the difference is explained.** The re-taken census (step 1,
comment-filtered, excluding `packet_sender.cpp` and the K13 `m_eq->QueuePacket`
forwarder) read 69 zone-stream sites in twelve files. T375 (`bec6fd51`, merged
into `fable` after B145 was written) routed `SendReport` in
`src/client/player/player_misc.cpp` on its way past, taking that file from 18
to 17. Every other per-file number in the board matched exactly.

### The final census

Zero outside `eqt::PacketSender` and the login/world TU:

```
src/client/login_world/login_world_component.cpp: 12
  (4 m_login_connection, 8 m_world_connection)  -- board B146, out of scope
```

`tests/test_direct_sender_census.cpp` (3 cases) now re-runs that sweep on every
ctest, in three directions so it cannot go vacuously green: no offender outside
the two exemptions; `packet_sender.cpp` still holds the real sends; and the
B146 exemption is still exactly 12. **Proven by re-planting the fault** —
reverting `ZoneSendChannelMessage` to `ctx_.zone_connection->QueuePacket(p)`
turns `DirectSenderCensus.NoZoneStreamSenderBypassesPacketSender` red with
`src/client/chat/chat_component.cpp:135`, then the revert was undone
(`git status` clean; no stash was used at any point).

### Wrappers: two deleted, the rest kept, each read

- **Deleted (2), both in group:** the bare `if (ctx_.zone_connection) { send; }`
  in `SendGroupFollow` and `SendGroupDecline`. Nothing else inside the braces.
- **Kept — the eleven zone-handshake `if/else`.** Their `else` arm calls
  `ctx_.net.NoteSuppressedSend()`, and `SafeQueueZone`'s own null guard logs and
  returns `false` **without** bumping that counter. Deleting the wrapper would
  have silently retired the replay-suppressed-send count the Batch R harnesses
  read. `NoZoneConnection_ZoneHandshakeStillCountsSuppressedSends` pins it: it
  is green before and after, and goes red if a later edit removes the `else`.
- **Kept — the `if (ctx_.zone_connection && isZoneConnected())` wrappers**
  (player 23, gm 9, merchant_vendor_bank 4, core 1). Each also gates
  `isZoneConnected()` and the log line or chat reply that follows the send.
- **Kept — merchant.cpp's nine early returns.** Each logs a refusal.

### Tests, per family

`tests/test_direct_senders_packet_sender.cpp`, 17 cases. Every family gets
(a) a no-connection case listing its senders and (b) a recorded-session case
that opens a real `SessionRecorder` onto a temp `.sleq` and reads it back off
disk (T360's shape, T376's file layout).

| family | (a) NoZoneConnection | pre-fix | post-fix | (b) RecordedSession | pre-fix | post-fix |
| --- | --- | --- | --- | --- | --- | --- |
| chat | 2 senders | **exit 139** | PASS | `OP_ChannelMessage`, `OP_Emote` | **0 / 0** | PASS |
| group | 6 senders | **exit 139** | PASS | `OP_GroupInvite` | **0** | PASS |
| movement | `SendJump` | **exit 139** | PASS | `OP_Jump` | **0** | PASS |
| player | 25 senders | **exit 139** | PASS | `OP_RandomReq`, `OP_ApplyPoison` | **0 / 0** | PASS |
| zone handshake | 12 senders | **exit 139** | PASS | `OP_ClientReady`, `OP_ReqNewZone`, `OP_ZoneEntry` | **0 / 0 / 0** | PASS |
| merchant | 13 senders | PASS (see note) | PASS | `OP_MoveItem`, `OP_RecipeDetails` | **0 / 0** | PASS |
| gm | 9 senders | PASS (see note) | PASS | `OP_GMSummon`, `OP_GMHideMe` | **0 / 0** | PASS |
| core | `SendPetition` | PASS (see note) | PASS | `OP_Petition` | **0** | PASS |

Plus `NoZoneConnection_ZoneHandshakeStillCountsSuppressedSends`: PASS in both
directions, by design.

**The "PASS (see note)" rows are honest, not weak.** Merchant, gm and core hold
no crash-shape site — every one of their sends already sat behind a null test —
so their (a) case is a regression net rather than a fault reproduction, and it
says so in the file. The finding for those three families is entirely the (b)
row: **no `--record-session` capture has ever contained an inventory drag, a GM
command or a petition.**

**The harness installs the connection MANAGER in both modes.** Without one the
whole group family returns at `!ctx_.zone_connection_manager` before reaching
the send, and `NoZoneConnection_Group` would have been green for the wrong
reason. A live manager holding a dropped connection is also the real shape of
the defect.

Every pre-fix run was taken against the routed-so-far tree with only that
family's sources unrouted; every post-fix run is the whole file green
(2, 4, 6, 8, 11, 13, 15, 17 cases as the families landed).

### The fixture consequence, at scale

`9bad7fe4`. Seventeen existing cases in four files went red on
`SafeQueueZone`'s zero-stats "connection appears invalid" guard rather than on
the code under test — the trap B145 predicted, from T346/T360/T376:
`BankCoinIntent` (4), `ReadBook` (2), `ScribeScroll` (3),
`EqActionHandlerTransformsReal` (8). All four harnesses observe the outbound
stream one layer BELOW `SafeQueueZone` (the `DaybreakConnection` resend map),
and a `FakeUdpTransport` connection that has never round-tripped has all-zero
stats. Each now carries `conn->m_stats.sent_packets = 1` with the same comment
the three earlier harnesses carry. **No assertion weakened, no expected byte
sequence changed**; all 59 cases across the four files pass.

### Verification

| step | result |
| --- | --- |
| `cmake --build build -j24` (GLES2 tree) | **EXIT=0** |
| `ctest --test-dir build -j6` | **3993 / 3994**, exit 8, sole failure `AudioManagerTest.PreloadCommonSounds` (the waived ALSA quirk) |
| G01 `zone_opcodes_g01.txt` (`ZoneDispatchTable`) | 6/6 PASS |
| G03 `command_registration_order_g03.txt` | 4/4 PASS |
| G04 `application_init_order_g04.txt` | 4/4 PASS |
| G05 `frame_render_order_g05.txt` | 4/4 PASS |
| G06 `movement_order_g06.txt` | 4/4 PASS |
| G07 `self_death_outbound_g07.txt` + G08 `op_save_body_g08.txt` (`SelfDeathSequence`) | 14/14 PASS |
| bridge intent table (`BridgeIntentTable`) | 6/6 PASS |
| zone-in path (`ZoneInZ`, 7 cases) | PASS |
| `git status --porcelain tests/goldens/` | empty — **no golden was edited** |
| `DocCitations` / `DocClaims` / `DocCounts` / `FileSizeCeiling` | all PASS |
| `check_file_sizes.sh fable` | 21 files checked, **21 ok / 0 warn / 0 fail** (largest touched: `src/client/core/everquest.cpp` 1098) |
| visual gate / `build-gles2` / ARM | not run — this plan's bar is "Build, ctest, tests, goldens, file sizes. No graphics change." |

**No golden needed a learned edit.** None of the eight goldens transcribes a
sender statement (`grep -l QueuePacket tests/goldens/` is empty); G07/G08
observe the outbound *packets*, which are unchanged, and all fourteen
`SelfDeathSequence` cases — including the two recorded-session observers T360
added — are green untouched. The re-planted-fault requirement was therefore
discharged on the new census pin instead, above.

Suite went 3968 (T376's final) -> 3994: +17 sender cases, +3 census cases, the
rest from `fable` moving on under both branches.

### Deviations and surprises

1. **`SendZoneChange` does not exist.** The plan (step 5) and B145 both name
   `SendZoneChange` in `everquest.cpp` as the core family's one site. There is
   no function by that name anywhere in that TU. The site is **`SendPetition`**,
   at `everquest.cpp:928`. The plan's underlying warning was still honoured: the
   zoning path is the zone-handshake family, whose twelve senders this branch
   routed, and the zone-in tests (`ZoneInZ`, `ZoneDispatchTable`,
   `SelfDeathSequence`'s zone-change cases) are all green. Corrected on the
   board.
2. **B145 said twelve files; it is thirteen.** The board's own per-file list
   contains thirteen entries — it counted `merchant_vendor_bank.cpp` as part of
   `merchant.cpp`. Corrected on the board.
3. **The zone handshake now goes out through `QueuePacketAsync`** whenever the
   game thread is not the thread that constructed `EverQuest` (which is the
   normal case: `Application::initEverQuestClient` runs on the render thread).
   This was checked rather than assumed, because the handshake is the one
   family where a deferral would be a live regression:
   `DaybreakConnectionManager::ProcessPacketsFromTransport` raises the
   `StatusConnected` callback in step 1 of its tick and drains
   `ProcessAsyncQueue` in step 2 of the **same** tick, so the handshake still
   leaves in the tick that established the session, in the same relative order.
   Every other `SafeQueueZone` caller (`SendPositionUpdate` included) has taken
   that path since K13.
4. **`SafeQueueZone`'s zero-stats guard cannot block a real zone entry.** The
   client reaches `StatusConnected` only by *receiving* `OP_SessionResponse`,
   which increments `m_stats.recv_packets`, and the guard fires only when
   `recv_packets == 0 && sent_packets == 0`. Checked before routing the
   handshake, since a false refusal there would hang zoning at 0%.
5. **One exit-143 build**, on the very first target build — that was a cold tree
   after checkout, so it was a full `willeq_core` compile, not a slow link. The
   post-143 sweep found no `build*/lib/st??????` ar temps and both archives
   present and non-empty by member count (`libwilleq_core.a` 322, `libEtcLib.a`
   15); gmake had printed its own `Deleting file` line for the single killed
   target, which the relaunched full build produced (valid ELF, EXIT=0).
6. **A new test target needs an explicit `cmake -S . -B build`.** Appending to
   `tests/cmake/*.cmake` did not trigger a reconfigure on `cmake --build`; the
   first attempt died with `No rule to make target`. Cheap, but it costs a build
   cycle if you read it as a real failure.
