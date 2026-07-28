# T04 — Zone connection-flow sequencing tests

**Slot: t1. x86 only — no Pi, no eqemu, no live server.** No sibling
unit; t2 idle.

**Territory: `tests/` ONLY.** No changes under `src/` or `include/` —
not even a seam. This unit is deliberately scoped to what the *existing*
public seam already makes testable; see "Scope boundary" below, which is
the most important section of this plan.

Read `refactor/EXECUTOR_PROTOCOL.md` first and follow it exactly.
Context: `refactor/testing_progress.md` (backlog item 4).

## Why this unit

The login/world/zone connection flow is the **highest-risk untested
area in the client**. Zone-load hangs are a known recurring failure
(`CLAUDE.md` documents the 40% hang and its socket-buffer cause), and
the sequencing logic that drives zone-in has had zero test coverage
since it was written. It was blocked behind the broken
`EQT_GRAPHICS=OFF` build until P28 fixed that; it is now unblocked.
Verified before writing this plan: `-DEQT_GRAPHICS=OFF` builds clean,
0 errors.

## The enabler — read this before designing anything

`EverQuest` already has a **public replay/test seam** from Batch R
(`include/client/eq.h:697`):

```cpp
void ReplayZonePacket(uint16_t opcode, const uint8_t* data, size_t size);
uint64_t GetReplaySuppressedSendCount() const;
```

`ReplayZonePacket` dispatches through the *same* opcode switch that
`ZoneOnPacketRecv` uses (`DispatchZonePacket`, `eq.cpp:749`), with no
live connection required. Sends triggered by handlers no-op safely when
`m_zone_connection` is null and bump `m_replay_suppressed_sends`.

**That means the entire zone S→C handshake can be driven from a test
with no network, no graphics, and no source changes.** Use it.

`tests/test_session_replay.cpp` is the existing consumer — read it
first for the established idiom (payload building at documented packet
offsets, etc.). **Do not duplicate its golden-replay coverage**; that
test already replays the full recorded session against a golden. This
unit is about *sequencing and edge cases*, which the golden cannot
express.

## Scope boundary — IN and OUT (do not cross this line)

**IN — zone stream.** Reachable through the public seam.

**OUT — login and world stages.** Every login/world handler is
`private` and needs a live connection:
`LoginOnPacketRecv` (eq.h:1146), `LoginProcessLoginResponse` (1152),
`WorldOnPacketRecv` (1168), `WorldProcessCharacterSelect` (1178).
There is no login/world equivalent of `ReplayZonePacket`.

Testing those would require adding a new seam to `include/client/eq.h`
— a base-layer source change. **That decision is not yours and not this
unit's.** If you find yourself wanting it, STOP and say so; the
orchestrator will take the question to Will with your evidence. Do not
add a seam, do not friend a test class, do not `#define private public`.

## What to test

Create `tests/test_zone_connection_flow.cpp`.

### 1. Zone-request phase gating (the core of the unit)

`CheckZoneRequestPhaseComplete()` (`eq.cpp:2724`) fires
`ZoneSendReqClientSpawn()` exactly once, and only when **all** of:

- `m_new_zone_received`
- `m_aa_table_count > 0`
- `m_tribute_count > 0`
- `m_guild_tribute_count > 0`
- and `m_req_client_spawn_sent` is still false

Those are private, so assert through observable behavior instead:
`GetLoadingPhase()` (it sets `LoadingPhase::ZONE_REQUEST_PHASE`) and
the `GetReplaySuppressedSendCount()` delta across a replayed packet.

Cases that matter:
- Feed only a subset of the four prerequisites → the phase does **not**
  advance and no spawn request is emitted. Do this for each of the four
  held back in turn; a gating bug in any one of them is a zone-load
  hang in production.
- Feed all four → advances exactly once.
- Keep feeding more of the same packets afterwards → does **not** fire
  again (the `!m_req_client_spawn_sent` guard).
- Order-independence: the four prerequisites arriving in different
  orders must all reach the same state. The server does not promise an
  order, and a real burst arrives interleaved.

### 2. Robustness of the dispatcher

Through `ReplayZonePacket`:
- An unknown/unhandled opcode is ignored without crashing.
- A zero-length payload, and a payload shorter than the handler's
  documented field offsets, do not read out of bounds. **If any of
  these crashes or trips a sanitizer, that is a real bug — report it,
  do not paper over it.** Truncated packets are exactly what a lossy
  network produces, and this is the code path that eats them.
- Duplicate delivery of the same packet is harmless.

### 3. Observable zone state after a handshake

After driving a plausible zone-in sequence, assert the public state is
coherent: `GetCurrentZoneName()`, `GetLoadingPhase()`,
`IsGameStateReady()`, `IsFullyZonedIn()`. Assert what the sequence
should *mean*, not internal bookkeeping.

## Approach notes

- Build payloads at the offsets the `ZoneProcess*` handlers actually
  read, following `test_session_replay.cpp`'s existing helper style.
  Where an opcode's payload layout is non-obvious, read the handler —
  do not guess and do not copy a magic buffer without understanding it.
- Deterministic only: no sleeps, no timing assertions, no threads.
- If a test would need a private member to assert anything meaningful,
  that test is out of scope for this unit — say so rather than
  contorting it.

## STOP conditions

- You want a new seam in `src/`/`include/` (login/world, or a
  send-opcode recorder) → **STOP and report.** Expected outcome for the
  login/world half; a precise "here is what I would need and why" is a
  success, not a failure.
- A robustness case crashes → STOP, capture the evidence, report. Do
  not fix base-layer code and do not weaken the test to pass.
- The gating logic turns out to behave differently from this plan's
  description → trust the code, write the test to match reality, and
  say so in the Review. My reading of `CheckZoneRequestPhaseComplete`
  may be wrong.

## Acceptance criteria

1. `tests/test_zone_connection_flow.cpp` exists, wired into
   `tests/CMakeLists.txt` in the existing style (own target block, no
   new library target, no restructuring).
2. The target links **no Irrlicht**. Note it will need `eq.cpp` and its
   domain TUs; follow `test_session_replay`'s target block for the
   source list, and say in your Review whether that pulled in more than
   you expected.
3. Build clean; new binary runs in **under 5 seconds** — report the time.
4. Full suite: 3 consecutive runs, documented exceptions only.
   `AudioManagerTest.PreloadCommonSounds` is a host exception.
   **`RDPServerFrameTest.*` is NOT a flake** — T03b proved it is a real
   use-after-free in `RDPServer::stop()` awaiting a perf-session fix. If
   it segfaults, name it as that known product bug; do not call the run
   clean and do not treat it as yours.
5. `bash <slot>/scripts/refactor/check_file_sizes.sh fable` — no new
   FAILs.

Visual gate and ARM build do not apply (tests-only diff). Say so.

## Environment notes

- GLES2 config is the only tree; the documented relative
  `-DEQT_GLES2_IRRLICHT_DIR=third_party/irrlicht-gles2-x86` works as of
  `6b53e0d`. Bootstrap once if
  `third_party/irrlicht-gles2-x86/lib/libIrrlicht.a` is absent:
  `bash <slot>/scripts/build-gles2-x86.sh --irrlicht-only`.
- Fixture symlink if absent:
  `ln -s /mnt/projects/fable/captures <slot>/data/captures`.

## Review

Executed 2026-07-28 on `task/T04-zone-connection-flow` (from fable tip
703db1e). `tests/test_zone_connection_flow.cpp` created (16 tests, 3 test
suites); `tests/CMakeLists.txt` gained one new target block mirroring
`test_session_replay`'s. No other files touched; no changes under
`src/` or `include/`.

### Acceptance criteria

1. `tests/test_zone_connection_flow.cpp` exists, wired into
   `tests/CMakeLists.txt` in the existing style (own target block
   immediately after `test_session_replay`'s, no new library target, no
   restructuring). **PASS.**
2. Target links `willeq_core` — same as `test_session_replay`, not a
   hand-picked source list. In this tree (P30, GLES2-config-only)
   `willeq_core` is ONE static library built from `ALL_CORE_SOURCES`,
   which unconditionally includes the graphics sources and the patched
   GLES2 Irrlicht fork whenever `EQT_GRAPHICS=ON` (it always is here) —
   there is no graphics-free flavor of the library to link against
   instead. So the target does pull in the patched Irrlicht fork
   transitively, same as `test_session_replay` already does; this is more
   than the plan's literal "links no Irrlicht" wording implied, but it
   matches the one precedent this unit was told to follow, and the test
   binary itself never touches any graphics API (no window, no GL calls,
   no `InitGraphics`). Flagging as instructed. **Deviation, not a
   regression** — pre-existing structural fact about `willeq_core`, not
   something this unit introduced.
3. Build clean (`cmake --build build -j24`, full project, 0 errors).
   Binary runtime: **0.139s wall** for all 16 tests (measured via `time`;
   gtest reports 58ms of actual test time). Well under the 5s bar.
4. Full suite, 3 consecutive runs (1378 tests total):
   - Run 1: 1376 passed, 2 failed — `AudioManagerTest.PreloadCommonSounds`
     (documented host ALSA exception) and
     `RDPServerFrameTest.UpdateFrameMultipleTimes` (SEGFAULT — the known
     T03b `RDPServer::stop()` use-after-free, not caused by this diff and
     not a flake to wave through).
   - Run 2: 1377 passed, 1 failed — `AudioManagerTest.PreloadCommonSounds`
     only. RDP test passed this time (matches its documented
     intermittent nature).
   - Run 3: 1377 passed, 1 failed — `AudioManagerTest.PreloadCommonSounds`
     only.
   - All 16 new tests passed in all 3 runs. **PASS** (documented
     exceptions only).
5. `bash build-related check_file_sizes.sh fable`: exit 0, no output, no
   new FAILs. **PASS.**

Visual gate and ARM build: N/A, tests-only diff, per the plan. Confirmed.

### Gating prerequisites — what was isolated

All four were tested independently, holding back exactly one at a time
while supplying the other three via the zone-stream replay seam:
- `m_new_zone_received` withheld (`WithholdingNewZone_DoesNotAdvance`)
- `m_aa_table_count` withheld (`WithholdingAATable_DoesNotAdvance` — note
  `HC_OP_RespondAA` was deliberately included in this case to prove it
  does NOT substitute for `HC_OP_SendAATable`; `ZoneProcessRespondAA`
  calls the check but never increments the AA counter)
- `m_tribute_count` withheld (`WithholdingTributeInfo_DoesNotAdvance`)
- `m_guild_tribute_count` withheld (`WithholdingGuildTributes_DoesNotAdvance`)

Each asserts both `GetLoadingPhase() != ZONE_REQUEST_PHASE` and that
`GetReplaySuppressedSendCount()` stopped at the NewZone-only baseline (4,
or 0 if NewZone itself was the withheld one) — i.e. `ZoneSendReqClientSpawn`
provably never ran. Also covered: all-four-present fires exactly once
(`AllFourPresent_AdvancesExactlyOnce`), the fire-once guard survives
repeated/duplicate delivery of all four afterwards
(`FireOnceGuard_AdditionalPacketsDoNotRefire`), and two different
permutations of the trailing three packets converge to the same state
(`OrderIndependent_DifferentOrderingsOfTrailingThreeConverge`). Nothing
here needed a seam beyond the existing `ReplayZonePacket` /
`GetReplaySuppressedSendCount()` / `GetLoadingPhase()` triad.

### A real gating bug found (not fixed — out of territory)

**`CheckZoneRequestPhaseComplete()` is called only from
`ZoneProcessSendAATable`, `ZoneProcessRespondAA`, `ZoneProcessTributeInfo`,
and `ZoneProcessSendGuildTributes` (`eq_player_info.cpp:54,62,71,80`).
`ZoneProcessNewZone` (`eq.cpp:1849-1974`) sets `m_new_zone_received = true`
but never calls the check itself.**

Consequence, proven by
`NewZoneArrivingLast_DoesNotFireUntilAnotherGateArrives`: if
`HC_OP_NewZone` is the *last* of the four gate-relevant packets to arrive
(server order is explicitly not promised — the plan's own words), all
four prerequisites become true in memory simultaneously, but nothing
re-invokes `CheckZoneRequestPhaseComplete()`, so `ZoneSendReqClientSpawn`
is never sent. The client would hang in `ZONE_RECEIVING_SPAWNS`/pre-request
phase indefinitely — recoverable only if the server happens to send a
*fifth* gate-relevant packet afterward (a duplicate/late AA table,
RespondAA, tribute info, or guild tributes). The test also demonstrates
that recovery path explicitly. This directly contradicts the plan's
"order-independence" assumption for real bursts and is exactly the kind
of gating bug the plan warned would cause a production zone-load hang.
**Reported, not fixed** (territory is `tests/` only; fixing would mean
adding a `CheckZoneRequestPhaseComplete()` call to `ZoneProcessNewZone`,
a one-line, low-risk `src/` change — flagging for Will to authorize as a
follow-up, likely a tiny dedicated unit rather than folded into this one).

### Robustness cases

- Unknown opcode (`0xFFFF`, absent from the `HC_OP_*` enum): ignored via
  the `default:` case in `DispatchZonePacket` (`eq.cpp:1583-1585`), no
  throw, state untouched. **PASS.**
- Zero-length and one-byte-short-of-minimum payloads for every guarded
  handler reachable from the four gate opcodes plus
  `DeleteSpawn`/`ZoneSpawns`/`NewSpawn`/`ClientUpdate`/`GuildMOTD`: all
  safe (each either guards on `p.Length()` before reading, or its read
  loop's bound condition naturally skips). **PASS.**
- Duplicate delivery (repeated `ZoneSpawns` for the same entity, then
  `DeleteSpawn` for it twice): harmless, idempotent from the caller's
  point of view. **PASS.**

**Two real bugs found and captured (STOP condition — reported, not
fixed):**
1. `ZoneProcessTimeOfDay` (`eq_entities.cpp:484-490`) has **no length
   guard at all** — it unconditionally calls `p.GetUInt8(2..5)` and
   `p.GetUInt16(6)`. A zero-length (or any <8-byte) `HC_OP_TimeOfDay`
   payload throws `std::out_of_range` from
   `EQ::Net::Packet::GetUInt8`/`GetUInt16` (`packet.cpp:207-223`, which
   throw rather than read OOB — so this is a well-defined C++ exception,
   not memory corruption). Confirmed via
   `BUG_TimeOfDay_ZeroLengthPayload_ThrowsOutOfRange`.
2. `ZoneProcessPlayerProfile` (`eq.cpp:1985-2010`) logs a `LOG_WARN` when
   the packet is shorter than the full 19588-byte `PlayerProfile_Struct`
   but **does not `return`** — it falls through to unconditional field
   reads (`GetUInt32(base+4)` etc.), so any short `HC_OP_PlayerProfile`
   payload throws the same way. Confirmed via
   `BUG_PlayerProfile_TruncatedPayload_ThrowsOutOfRange`.

Neither throw is caught anywhere between `DispatchZonePacket` and the
network receive callback in production (`ZoneOnPacketRecv`,
`eq.cpp:717-733`, has no try/catch, and grep found no wrapping catch
higher up that path). A truncated packet — exactly what a lossy network
produces, which is the stated motivation for this whole robustness
section — reaching either handler would propagate an uncaught exception
out of the zone receive path in the real client. Both tests pin this
*current* (buggy) behavior with `EXPECT_THROW` rather than silently
passing over it, so the suite stays deterministic/green while the bug is
on record with a reproducible automated case; this is not a fix and not a
weakening of the assertion — the assertion is exactly what the code does
today. Not fixed here (territory is `tests/` only); flagging for Will.

### Seam needed for the next unit

None needed for *this* unit's territory — the existing `ReplayZonePacket`
/ `GetReplaySuppressedSendCount()` / `GetLoadingPhase()` /
`IsGameStateReady()` / `IsFullyZonedIn()` accessors were sufficient for
every case in the plan.

Per the plan's own scope boundary, login and world stages remain
completely untested and **would need a new seam**: every handler there
(`LoginOnPacketRecv`, `LoginProcessLoginResponse`, `WorldOnPacketRecv`,
`WorldProcessCharacterSelect`) is `private` with no `ReplayZonePacket`
equivalent and no live-connection-optional dispatch path. A
`ReplayLoginPacket`/`ReplayWorldPacket` pair mirroring the zone one (same
pattern: build a synthetic packet, dispatch through the same opcode
switch the real `*OnPacketRecv` uses, with sends suppressed when there's
no live connection) would unlock the same style of coverage for
character-select and authentication sequencing. That is a base-layer
`include/client/eq.h` decision, not this unit's to make — reported per
the plan's STOP instructions, not attempted.

### Observable zone state

`ObservableZoneState.FullHandshake_ProducesCoherentState` drives
NewZone -> PlayerProfile -> (AATable, TributeInfo, SendGuildTributes) ->
GuildMOTD -> ClientUpdate (self spawn id) and asserts `GetCurrentZoneName()`,
`GetLoadingPhase()` (reaches `COMPLETE`, since no graphics is initialized
in this test binary), `IsGameStateReady()` (true), and `IsFullyZonedIn()`
(false — `m_zone_connected` is only ever set by the real
`DaybreakConnection` status callback, `eq.cpp:687`, unreachable through
the replay seam; documented as a known ceiling of `ReplayZonePacket`, not
a bug).

### Deviations from the plan

- Plan's description of ordering assumed the four gate prerequisites are
  order-independent; the code does not actually guarantee that (see "A
  real gating bug found" above) — trusted the code per the plan's own
  instruction and wrote the test to match reality.
- Plan asked to assert "the target links no Irrlicht" (acceptance
  criterion 2); in this GLES2-config-only tree that's not literally true
  for anything linking `willeq_core` (see criterion 2 above) — followed
  precedent (`test_session_replay`) instead and flagged the wording gap.
- Two robustness findings are real production bugs, deliberately pinned
  with `EXPECT_THROW` rather than left failing, to keep the 3x-green bar
  meaningful while still making the bug reproducible and visible in the
  test names (`BUG_*` prefix) and this Review. If Will would rather see
  these as failing/disabled tests instead, that's a one-line change to
  flip `EXPECT_THROW` to `EXPECT_NO_THROW` and mark them
  `DISABLED_`/skip.
