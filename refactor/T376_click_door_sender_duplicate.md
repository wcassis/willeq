# T376 — SendClickDoor through PacketSender (B140); one pending-click set (B141)

Class B, slot3, no server. Read the B140 and B141 board text and T374's
§Review.

## Leg A — B140

`EntitiesComponent::SendClickDoor` ends in an unguarded
`ctx_.zone_connection->QueuePacket(...)` — segfaults with no zone
(measured exit 139) and bypasses `PacketSender::SafeQueueZone`, so the
session recorder and the T268 seam never see a door/world-object click.
Route it through `ctx_.net.SafeQueueZone` (T360's shape — the one call
that guards, records, and orders). Then census: comment-filtered grep for
`zone_connection->QueuePacket` / `->QueuePacket(` outside `PacketSender`
under `src/client`; every remaining direct sender is either routed in
this leg (same shape, one commit each if more than two) or boarded with
its reason. Tests, pre-fix failing: (a) `SendClickDoor` with no zone
connection returns without crashing (the T360 harness); (b) a recorded
session contains the click. Strike B140.

## Leg B — B141

`EQT::DoorStateManager` carries a complete, correct, zero-caller
duplicate of the pending-click set (`pendingClicks_` + four accessors,
with the erase-on-update and clear-on-zone the reached copy lacked until
T374). Duplicated-then-drifted, and the reached copy was the broken one.
Decide by ownership: the manager owns door state → the manager's set is
the one copy; `EntitiesComponent` calls it (`markPendingClick` on send,
the manager consumes on `OP_MoveDoor`, clears on zone) and its own
`m_pending_door_clicks` + `ClearPendingDoorClicks()` are deleted. T374's
`DoorClickZoneReset` tests must stay green unedited (they test behaviour,
not the member); if they name the member, that is the test reading
implementation — fix the test to the behaviour. K12 ff check. Strike
B141.

## Proof

Build, `ctest -j6`, the tests with pre-fix/post-fix runs, the G01 golden
unchanged, file sizes. No graphics change.

STOP: a direct sender in the census cannot go through PacketSender
because it runs before the zone connection exists (login/world — those
belong to a different sender; board them, do not route).

## Review

Executed 2026-09-05 in `wt/slot3`, branch
`task/T376-click-door-sender-duplicate` off `fable` at `e580ce97` (verified).
Two legs, two code commits plus a doc commit. No STOP condition hit. No
graphics change, no server, no re-bless, goldens untouched.

| leg | commit | subject |
| --- | --- | --- |
| A (B140) | `89a908c9` | door and world-object clicks go out through PacketSender |
| B (B141) | `8962be28` | one pending-door-click set, owned by DoorStateManager |
| docs | (this commit) | board strikes + census + this Review |

### Leg A — B140

All THREE direct senders in
`src/client/entities/entities_component_world.cpp` now call
`ctx_.net.SafeQueueZone(p, 0, true)`: `SendClickDoor`, `SendClickObject` and
**`SendCloseContainer`**. The board named only the two click senders; the third
is the same shape in the same TU and leaving it would have left the file
half-routed. `PacketSender` was already reachable as `ctx_.net` (T360's shape),
so no new context service was needed.

**Test: `tests/test_click_senders_packet_sender.cpp`** (new target in
`tests/cmake/action_handlers.cmake`), five cases, two observers.

| test | pre-fix | post-fix |
| --- | --- | --- |
| `ClickSendersPacketSender.NoZoneConnection_ClickDoorReturnsWithoutCrashing` | **CRASH** — the binary died with exit 139 on this case, taking the other four with it | PASS |
| `…NoZoneConnection_ClickObjectReturnsWithoutCrashing` | (never reached) | PASS |
| `…NoZoneConnection_CloseContainerReturnsWithoutCrashing` | (never reached) | PASS |
| `…OrderedCaptureSeam_SeesTheDoorAndObjectClicks` | **FAIL** — `doors` 0, `objects` 0 (run under a filter, since the crash cases abort the process) | PASS |
| `…RecordedSession_ContainsTheDoorAndObjectClicks` | **FAIL** — `OP_ClickDoor` 0, `OP_ClickObject` 0, `OP_ClickObjectAction` 0 in the `.sleq` | PASS |

Pre-fix whole-binary run: **exit 139**. Pre-fix filtered run of the two
observer cases: exit 1, 0 passed / 2 failed. Post-fix: exit 0, **5 passed**.

The two observers are the two different claims T360 separated: the T268 seam
(`Test_GetQueuedPackets`) says "the client decided to send this", and a real
`SessionRecorder` onto a temp `.sleq`, read back off disk, says "this went out
through `PacketSender`". Bodies are checked too — the door id survives as the
first payload byte on both paths.

**One fixture consequence, flagged — not a defect, and the exact deviation
T360 recorded.** `CombineHarness` in `tests/test_tradeskill_combine.cpp`
observes the outbound stream one layer BELOW `SafeQueueZone` (the
`DaybreakConnection` resend map), and `SafeQueueZone` refuses a connection
whose stats are all zero ("appears invalid") — which is what a
`FakeUdpTransport` connection that has never round-tripped looks like. Routing
`SendCloseContainer` therefore turned
`TradeskillCombine.CloseIntentBecomesA92ByteClickObjectActionWithOpenZero` red
on that production guard rather than on the code under test. The harness now
sets `conn->m_stats.sent_packets = 1`, the same line and the same reason
`PoseHarness` (T360) and `DeathHarness` (T346) carry. No assertion weakened;
all 23 cases in that file pass. `TradeskillCombine`'s sibling combine case did
NOT go red, because `SendTradeSkillCombine` goes through `PacketSender::Queue`,
which has no stats guard — only `SafeQueueZone` does.

### The direct-sender census

Comment-filtered grep for `->QueuePacket(` under `src/client` and
`include/client`, at `e580ce97` + Leg A. **Three categories, and the first is
not a finding at all:**

1. **Already routed, 19 sites.** `m_eq->QueuePacket(opcode, &packet)` in
   `combat.cpp` (11), `spell/spell_manager.cpp` (3), `skill/skill_manager.cpp`
   (3) and two comment mentions is `EverQuest`'s forwarder to
   `PacketSender::Queue` — the K13 exception kept deliberately for non-family
   callers. These feed the T268 seam and the recorder already.
2. **Zone-stream direct senders, 70 sites in 12 files — BOARDED as B145.**
   `ctx_.zone_connection->QueuePacket(...)`, plus one
   `m_zone_connection->QueuePacket(packet)` in
   `src/client/core/everquest.cpp`'s `SendZoneChange`. Not routed here: this is
   a wide mechanical sweep across seven unrelated families (merchant, player,
   group, gm, chat, movement, zone handshake), each with its own test harnesses
   that may hit the same zero-stats guard Leg A hit, and the plan's "one commit
   each if more than two" does not scale to 70 across territory this unit does
   not own. Split by null-safety:
   - **10 are the B140 crash shape** — an unguarded `shared_ptr` deref with no
     `ctx_.zone_connection` test anywhere in the enclosing function:
     `chat_component.cpp` x2, `group_component.cpp` x4,
     `movement_state.cpp` (`SendJump`), `player_buffs.cpp`
     (`SendApplyPoison`), `player_state.cpp` (`SendTradeSkillCombine`),
     `zone_handshake.cpp` (`ZoneSendStreamIdentify`). Each gates on
     `IsFullyZonedIn()` or similar, which makes the null case unlikely but not
     impossible — the same reasoning that made B140 a B.
   - **60 sit inside `if (ctx_.zone_connection && ...)`** and are
     recorder/seam-blind only.
3. **Login and world streams, 12 sites — BOARDED as B146**, under the plan's
   STOP condition. `src/client/login_world/login_world_component.cpp`:
   4 on `m_login_connection`, 8 on `m_world_connection`. They run before the
   zone connection exists, so `SafeQueueZone` is the wrong destination; they
   need their own sender. **Measured while boarding:** that TU records
   `SessionStream::Login` and `SessionStream::World` for
   `ServerToClient` only (two `ctx_.session_recorder->record` calls, both
   S->C), so a `.sleq` of a fresh login carries the server's half of the
   handshake and none of the client's.

### Leg B — B141

Decided by ownership, as the plan directed. `DoorStateManager` owns door state,
so its `pendingClicks_` is the one copy:

- `SendClickDoor` -> `ctx_.game_state.doors().addPendingClick(door_id)`
- `ZoneProcessMoveDoor` reads `isClickPending(door_id)` **before**
  `setDoorState()`, which consumes the entry — the ordering trap the board
  named. Asking afterwards always answers false.
- `ZoneComponent::CleanupZone`'s **second** call is deleted:
  `doors().clear()` already clears `doors_` and `pendingClicks_` together, so
  T374's zone reset is back to one line.
- `EntitiesComponent::m_pending_door_clicks`, `ClearPendingDoorClicks()` and
  the then-unused `#include <set>` are deleted.

**K12 (ff) checked before deleting:** `ClearPendingDoorClicks()`'s body called
no `pushEvent` and named no `GameEventType` — it logged a count and cleared.
`ReachabilitySweep` is green.

**T374's two `DoorClickZoneReset` cases are UNEDITED and green.** They assert
`DoorStateChanged.userInitiated` off the bridge, i.e. behaviour, not the
member, so nothing in them named the thing that moved. The file's header
comment was retyped (it described `m_pending_door_clicks` and called B140
open); the two `TEST(...)` bodies are byte-identical.

**New tests, same file, three cases** through the manager's own public
accessors:

| test | pre-fix | post-fix |
| --- | --- | --- |
| `DoorPendingClickOwnership.ClickRegistersOnTheDoorStateManager` | **FAIL** — `isClickPending` false after `SendClickDoor` (the manager's set was never written) | PASS |
| `DoorPendingClickOwnership.MoveDoorConsumesTheManagersPendingClick` | **FAIL** — same, at the `ASSERT_TRUE` precondition | PASS |
| `DoorPendingClickOwnership.ZoneCleanupClearsTheManagersPendingClicks` | **FAIL** — same | PASS |
| `DoorClickZoneReset.*` (T374's two, unedited) | PASS | PASS |

Pre-fix binary run: exit 1, 2 passed / 3 failed. Post-fix: exit 0, 5 passed.

### Verification

| step | result |
| --- | --- |
| `cmake --build build -j24` (GLES2 tree) | EXIT=0 (both legs) |
| `ctest -j6` after Leg A | 3963 / 3965, exit 8 — `AudioManagerTest.PreloadCommonSounds` (waived ALSA) plus the `TradeskillCombine` fixture consequence above, fixed in the same leg |
| `ctest -j6` final | **3967 / 3968**, exit 8, sole failure `AudioManagerTest.PreloadCommonSounds` |
| G01 golden | `git status --porcelain tests/goldens/` empty; `ZoneDispatchTable` 6/6 |
| `DocCitations` / `DocClaims` / `DocCounts` / `FileSizeCeiling` | all PASS |
| `check_file_sizes.sh fable` | 7 files checked, **7 ok / 0 warn / 0 fail** (largest touched: `zone_component.cpp` 1111) |
| visual gate / `build-gles2` / ARM | not run — the plan narrows the bar ("No graphics change") |

Suite went 3965 -> 3968: +5 new cases in Leg A, +3 in Leg B, -5 because `fable`
moved on under us (this branch is based at `e580ce97`, before T375 merged).

### Two things worth carrying forward

1. **One exit-143 build**, on the very first (single-target) link of the new
   test — `libwilleq_core.a` is large and the static link is slow. Post-143
   sweep run per protocol: no `build*/lib/st??????` ar temps, both archives
   present and non-empty by member count (`libwilleq_core.a` 322,
   `libEtcLib.a` 15), 276 binaries scanned for ELF magic and minimum size,
   `bad=0`. gmake had printed its own `Deleting file` line for the one killed
   target, which was the only casualty; the relaunched build produced it,
   EXIT=0.
2. **A new board entry can fail `DocCitations`.** B145's per-file census was
   first written with repo-relative paths shortened to `player/player_misc.cpp`
   and the checker read each backticked path as a citation, failing the
   hygiene test with twelve "file does not exist" lines. Board text that lists
   many files should either spell every path in full from the repo root or
   drop the backticks and name the directory once. Cheap to hit, cheap to fix,
   but it turns a doc edit into a red suite.
