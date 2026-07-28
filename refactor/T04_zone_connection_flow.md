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

Per the protocol. Specifically state:
- Which of the four gating prerequisites you were able to test
  independently, and which you could not isolate.
- Every robustness case that crashed or misbehaved, with evidence.
- What you would need a seam for, if anything — that feeds the next
  unit's decision.

An honest "I covered the gating thoroughly and could not meaningfully
reach X" is worth more than broad coverage I cannot trust.
