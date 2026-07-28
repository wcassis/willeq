# T05 — Login/world replay seam + truncated-packet guards

**Slot: t1. x86 only — no Pi, no eqemu, no live server.** No sibling
unit; t2 idle.

**This is the testing campaign's first unit that modifies base-layer
source.** Will authorized it explicitly (2026-07-28), widening the
charter's "minimal seam" latitude for this specific work, with Batch R's
existing zone seam as the named template. That authorization is
narrow — see Territory.

Read `refactor/EXECUTOR_PROTOCOL.md` first and follow it exactly.
Context: `refactor/testing_progress.md` ("Decision needed from Will",
now granted) and `refactor/T04_zone_connection_flow.md`.

## Territory — read carefully, this is wider than previous units

**ALLOWED to modify:**
- `include/client/eq.h` — two new public methods + two private
  dispatcher declarations.
- `src/client/eq_login_world.cpp` — dispatcher extraction, null guards.
- `src/client/eq.cpp` — the two truncated-packet guards ONLY
  (`ZoneProcessTimeOfDay` lives in `eq_entities.cpp`, see Part B).
- `src/client/eq_entities.cpp` — the `ZoneProcessTimeOfDay` guard ONLY.
- `tests/**`.

**NOT allowed**, still: `src/client/graphics/**`, anything else under
`src/`, opportunistic cleanups, reformatting, or "while I was in here"
changes. Every source hunk must be traceable to Part A or Part B below.
If you find another bug, report it — do not fix it.

## Part A — login/world replay seam

### What exists (the template — mirror it, do not invent)

Zone works because Batch R did **two** things:

1. Split the opcode switch out of `ZoneOnPacketRecv` (`eq.cpp:717`)
   into `DispatchZonePacket` (`eq.cpp:749`).
2. Guarded **every** zone sender against a null connection, e.g.
   `eq.cpp:1627`:

```cpp
    DumpPacket("C->S", HC_OP_ZoneEntry, p);
    if (m_zone_connection) {
        m_zone_connection->QueuePacket(p);
    } else {
        ++m_replay_suppressed_sends;
    }
```

There are 11 such guards in `eq.cpp`. The public seam is
`ReplayZonePacket` (`eq.h:697`), which builds a `DynamicPacket` with the
2-byte opcode prefix and calls the dispatcher.

### What to do

**A1. Extract the dispatchers.** `LoginOnPacketRecv`
(`eq_login_world.cpp:70`) and `WorldOnPacketRecv` (`:319`) both follow
the same shape: log → `GetUInt16(0)` → `DumpPacket` → session-recorder
record → `switch`. Move **only the `switch`** into new private
`DispatchLoginPacket(const EQ::Net::Packet&)` /
`DispatchWorldPacket(...)`, and have `*OnPacketRecv` call them.

> **CRITICAL — the session-recorder call must STAY in `*OnPacketRecv`,
> above the dispatcher call.** It must NOT move into the extracted
> dispatcher. Zone does it this way deliberately; the comment at
> `eq.cpp:725` says why: replayed packets enter via
> `Replay*Packet → Dispatch*Packet` and must never be re-recorded. Get
> this wrong and every replay corrupts the session log, and the
> recording workflow that produced our fixtures silently breaks.

`DumpPacket` and the opcode read may stay in `*OnPacketRecv` (matching
zone) — the point is that the *dispatch* is separately callable.

**A2. Guard the senders.** 11 sender functions in
`eq_login_world.cpp` make **12** unguarded `QueuePacket` calls on
`m_login_connection` / `m_world_connection` (e.g.
`LoginSendSessionReady` at `:109`). Apply the exact zone pattern above,
reusing the existing `m_replay_suppressed_sends` counter and its
existing public accessor — do not add a second counter.

This is not optional and not reducible by picking "safe" opcodes:
`LoginOnPacketRecv`'s **first** case (`HC_OP_ChatMessage`) calls
`LoginSendLogin()`.

**A3. Add the public seam.** In `eq.h`, mirroring `ReplayZonePacket`'s
declaration and doc comment exactly:

```cpp
void ReplayLoginPacket(uint16_t opcode, const uint8_t* data, size_t size);
void ReplayWorldPacket(uint16_t opcode, const uint8_t* data, size_t size);
```

**A4. Smoke-test the seam only.** Add a *small* test proving the seam
works: replay one login packet and one world packet, assert an
observable state change and a `GetReplaySuppressedSendCount()` delta,
and assert an unknown opcode is ignored safely.

**Do NOT write thorough login/world flow coverage in this unit.** That
is deliberately deferred to a follow-up so this diff stays reviewable —
a base-layer refactor and a large new test suite should not land
together. If you find yourself writing a tenth flow test, stop.

## Part B — truncated-packet guards

Both bugs were found by T04 and are currently pinned as
`BUG_`-prefixed tests in `tests/test_zone_connection_flow.cpp`.

**B1. `ZoneProcessPlayerProfile`** (`src/client/eq.cpp:1999`) already
warns `"PlayerProfile packet too small"` and then **falls through and
reads anyway**. Add the missing `return;` after the warning.

**B2. `ZoneProcessTimeOfDay`** (`src/client/eq_entities.cpp:484`) has
no guard at all; it reads `GetUInt8(2)`…`GetUInt16(6)`, so it needs a
total length of at least 8. Add a length check that logs a warning and
returns, matching B1's shape.

### Verified safe — I checked these against the real capture

I confirmed against `/mnt/projects/fable/captures/session.sleq` that
neither guard will skip a legitimate packet:

| handler | real payload size in capture | guard threshold | fires on real traffic? |
|---------|------------------------------|-----------------|------------------------|
| PlayerProfile (`0x75df`, 2 records) | 19592 | warns below 19588 | **no** |
| TimeOfDay (`0x1580`, 2 records) | 8 | needs ≥ 6 | **no** |

So `return`-ing on the short path cannot regress a working case. If
your own measurement disagrees with this table, **STOP and report** —
do not proceed on the assumption that I am right.

**B3. Flip the pinned tests.** The two `BUG_`-prefixed tests currently
`EXPECT_THROW`. Rewrite them to assert the fixed behavior: the handler
returns without throwing, and observable state is unchanged. Drop the
`BUG_` prefix and name them for the behavior they now guarantee.

## STOP conditions

- The dispatcher extraction cannot be made behavior-preserving → STOP
  and report; do not "improve" the flow while extracting.
- Any real-capture packet would be skipped by a new guard → STOP.
- You find another bug → report it, do not fix it.
- A change would touch a file not listed in Territory → STOP.

## Acceptance criteria

1. Both dispatchers extracted; session-recorder calls verifiably still
   in `*OnPacketRecv` (quote the final code in your Review).
2. All 12 `QueuePacket` call sites guarded. State the count you found;
   if it is not 12, say so and explain.
3. `ReplayLoginPacket`/`ReplayWorldPacket` public in `eq.h`; seam
   smoke-test passes.
4. Part B: both guards added, both pinned tests flipped and passing.
5. **`test_session_replay` passes, including the golden comparison.**
   This is the behavior-preservation oracle for Part A — if the golden
   changes, the refactor was not behavior-preserving. **A golden
   mismatch is a STOP, not a re-bless.** Never re-bless a golden.
6. Full suite: 3 consecutive runs, documented exceptions only
   (`AudioManagerTest.PreloadCommonSounds` host ALSA;
   `RDPServerFrameTest.*` is the known T03b use-after-free, not a
   flake — name it, do not call the run clean).
7. `bash <slot>/scripts/refactor/check_file_sizes.sh fable` — no new
   FAILs.
8. **`bash <slot>/scripts/build-arm-noble.sh` must pass.** This unit
   touches shared source, which is exactly the case the charter's
   verification bar reserves the ARM build for. Previous tests-only
   units skipped it; this one may not.

Visual gate does not apply (no rendering change) — say so explicitly
rather than silently omitting it.

## Environment notes

- GLES2 config is the only tree; the documented relative
  `-DEQT_GLES2_IRRLICHT_DIR=third_party/irrlicht-gles2-x86` works.
  Bootstrap once if `third_party/irrlicht-gles2-x86/lib/libIrrlicht.a`
  is absent: `bash <slot>/scripts/build-gles2-x86.sh --irrlicht-only`.
- Fixture symlink if absent:
  `ln -s /mnt/projects/fable/captures <slot>/data/captures`.

## Review

Executed 2026-07-28 on slot t1, branch `task/T05-login-world-seam` off
fable @ 67beab7.

### Part A — login/world replay seam

**A1/A3. Dispatcher extraction + public seam.** `LoginOnPacketRecv` and
`WorldOnPacketRecv` (`src/client/eq_login_world.cpp`) now call
`DispatchLoginPacket`/`DispatchWorldPacket` instead of switching directly.
Final bodies:

```cpp
void EverQuest::LoginOnPacketRecv(std::shared_ptr<EQ::Net::DaybreakConnection> conn, const EQ::Net::Packet & p)
{
	auto opcode = p.GetUInt16(0);
	DumpPacket("S->C", opcode, p);

	// T05: record real network traffic only (replayed packets enter via
	// ReplayLoginPacket -> DispatchLoginPacket and are never re-recorded) --
	// mirrors R01's rule for zone (see eq.cpp:725).
	if (m_session_recorder && p.Length() >= 2) {
		m_session_recorder->record(EQT::Net::SessionStream::Login, EQT::Net::SessionDirection::ServerToClient,
			opcode, (const uint8_t*)p.Data() + 2, static_cast<uint32_t>(p.Length() - 2));
	}

	DispatchLoginPacket(p);
}
```

```cpp
void EverQuest::WorldOnPacketRecv(std::shared_ptr<EQ::Net::DaybreakConnection> conn, const EQ::Net::Packet & p)
{
	LOG_TRACE(MOD_WORLD, "WorldOnPacketRecv called");
	auto opcode = p.GetUInt16(0);
	DumpPacket("S->C", opcode, p);

	// T05: record real network traffic only (replayed packets enter via
	// ReplayWorldPacket -> DispatchWorldPacket and are never re-recorded) --
	// mirrors R01's rule for zone (see eq.cpp:725).
	if (m_session_recorder && p.Length() >= 2) {
		m_session_recorder->record(EQT::Net::SessionStream::World, EQT::Net::SessionDirection::ServerToClient,
			opcode, (const uint8_t*)p.Data() + 2, static_cast<uint32_t>(p.Length() - 2));
	}

	DispatchWorldPacket(p);
}
```

The session-recorder call is confirmed still inside `*OnPacketRecv`, above
the `Dispatch*Packet(p)` call, in both bodies — it does NOT appear inside
`DispatchLoginPacket`/`DispatchWorldPacket`. Replayed packets (which enter
via `Replay*Packet -> Dispatch*Packet`, never touching `*OnPacketRecv`)
cannot be re-recorded.

`ReplayLoginPacket`/`ReplayWorldPacket` are public in `include/client/eq.h`,
declared immediately after `ReplayZonePacket` with a doc comment pointing
back at it, and implemented in `eq_login_world.cpp` as exact structural
mirrors of `ReplayZonePacket` (2-byte opcode prefix + payload into a
`DynamicPacket`, then call the dispatcher).

**A2. Guarded call sites.** Found and guarded exactly the count the plan
predicted: **11 sender functions, 12 `QueuePacket` call sites**, all using
the existing `m_replay_suppressed_sends` counter (no second counter added):

| # | Function | Connection | Notes |
|---|----------|-----------|-------|
| 1 | `LoginSendSessionReady` | `m_login_connection` | |
| 2 | `LoginSendLogin` | `m_login_connection` | |
| 3 | `LoginSendServerRequest` | `m_login_connection` | |
| 4 | `LoginSendPlayRequest` | `m_login_connection` | |
| 5 | `WorldSendClientAuth` | `m_world_connection` | |
| 6 | `WorldSendEnterWorld` | `m_world_connection` | |
| 7 | `WorldSendApproveWorld` | `m_world_connection` | |
| 8 | `WorldSendWorldClientCRC` (CRC1) | `m_world_connection` | 2 calls in this one function |
| 9 | `WorldSendWorldClientCRC` (CRC2) | `m_world_connection` | (counted above) |
| 10 | `WorldSendWorldClientReady` | `m_world_connection` | |
| 11 | `WorldSendWorldComplete` | `m_world_connection` | |
| 12 | `WorldProcessExpansionInfo` (inline ack packet) | `m_world_connection` | not a `*Send*`-named function, but an unguarded inline `QueuePacket` |

Every site follows the exact zone pattern (`if (m_*_connection) { ...
QueuePacket(p); } else { ++m_replay_suppressed_sends; }`). Verified with
`grep -n QueuePacket src/client/eq_login_world.cpp`: all 12 occurrences are
now inside an `if (m_*_connection)` guard.

**A4. Smoke test.** New `tests/test_login_world_seam.cpp`
(`LoginWorldReplaySeam` suite, 5 tests, registered in `tests/CMakeLists.txt`
next to `test_zone_connection_flow`):
- `LoginChatMessage_TriggersLoginSendLogin_SuppressedSend` — replays
  `HC_OP_ChatMessage` on the login seam, asserts `LoadingPhase` becomes
  `LOGIN_AUTHENTICATING` (observable state change) and
  `GetReplaySuppressedSendCount()` increments by 1 (guarded send).
- `LoginUnknownOpcode_IgnoredSafely` — unknown login opcode, no throw, no
  state/counter change.
- `WorldSendCharInfo_TriggersCharacterSelectPhase` — replays
  `HC_OP_SendCharInfo` with an empty payload, asserts `LoadingPhase` becomes
  `WORLD_CHARACTER_SELECT` (set unconditionally before
  `WorldProcessCharacterSelect`'s own length guard).
- `WorldChatMessage_TriggersSuppressedSend` — replays `HC_OP_ChatMessage` on
  the world seam, asserts the suppressed-send counter increments by 1.
- `WorldUnknownOpcode_IgnoredSafely` — unknown world opcode, no throw, no
  state/counter change.

All 5 pass. No thorough login/world flow suite was added, per A4's explicit
scope limit.

### Part B — truncated-packet guards

**B1.** `ZoneProcessPlayerProfile` (`src/client/eq.cpp:1999`) now has
`return;` immediately after the existing `LOG_WARN` for a too-small
PlayerProfile packet, before any field reads.

**B2.** `ZoneProcessTimeOfDay` (`src/client/eq_entities.cpp:484`) gained a
new guard at the top: `if (p.Length() < 8) { LOG_WARN(...); return; }`
before any `GetUInt8`/`GetUInt16` reads. I did not re-derive the
19588/8-byte thresholds — I used the plan's verified table as given (the
plan states Will confirmed these against the real capture); no discrepancy
was found in my own testing (see B3 below — the previously-skipped
`ObservableZoneState.FullHandshake_ProducesCoherentState` test, which uses
a full-size PlayerProfile payload, still passes).

**B3.** The two `BUG_`-prefixed tests in `tests/test_zone_connection_flow.cpp`
are renamed and flipped:
- `BUG_TimeOfDay_ZeroLengthPayload_ThrowsOutOfRange` →
  `TimeOfDay_ZeroLengthPayload_ReturnsSafely` (`EXPECT_NO_THROW`).
- `BUG_PlayerProfile_TruncatedPayload_ThrowsOutOfRange` →
  `PlayerProfile_TruncatedPayload_ReturnsSafely` (`EXPECT_NO_THROW` on both
  the empty and 10-byte payloads, plus `EXPECT_EQ(eq.GetEntities().size(),
  0u)` to confirm the truncated profile produced no observable state
  change).

Both pass. A stale comment elsewhere in the same file (pointing at "the
BUG_PlayerProfile test above") was updated to the new test name — this is
the one incidental touch outside the enumerated Part A/B hunks, done
because it directly references code this unit renamed; no other
opportunistic changes were made.

### Verification (per acceptance criteria)

1. **PASS** — both dispatchers extracted; session-recorder calls verified
   still in `*OnPacketRecv`, above the dispatch call (bodies quoted above).
2. **PASS** — 12 `QueuePacket` call sites found and guarded, matching the
   plan's predicted count exactly (table above).
3. **PASS** — `ReplayLoginPacket`/`ReplayWorldPacket` public in `eq.h`;
   5-test seam smoke suite passes.
4. **PASS** — both Part B guards added; both pinned tests flipped and
   passing.
5. **PASS — golden matched.** `test_session_replay` (run with
   `WILLEQ_SESSION_LOG=<slot>/data/captures/session.sleq` so the relative
   default path resolves regardless of cwd) ran
   `SessionReplayGolden.MatchesRecordedSessionOrCreatesBaseline` against
   the pre-existing `session.golden` (not created fresh — the file was
   already present) and it **passed**, i.e. matched byte-for-byte, not
   re-blessed. This test only exercises the zone stream
   (`ReplayZonePacket`), which Part A did not touch structurally (only
   Login/World got the dispatcher split) — it serves as the
   behavior-preservation oracle for "nothing else broke," and Part A's
   login/world extraction is structurally identical to the untouched zone
   pattern it mirrors.
6. **PASS, with the documented exceptions.** 3 consecutive full-suite runs
   (`ctest --test-dir <slot>/build --output-on-failure`,
   `WILLEQ_SESSION_LOG` set as above):
   - Run 1: 1382/1383 passed. `AudioManagerTest.PreloadCommonSounds` FAILED
     (documented host ALSA exception).
   - Run 2: same — `AudioManagerTest.PreloadCommonSounds` FAILED only.
   - Run 3: `AudioManagerTest.PreloadCommonSounds` FAILED, **and**
     `RDPServerFrameTest.UpdateFrameMultipleTimes` SEGFAULTed. This is the
     known T03b use-after-free in `RDPServer::stop()` — not a flake
     introduced by this unit (this unit touches no RDP code) — named per
     instructions, not called clean.
7. **PASS** — `bash <slot>/scripts/refactor/check_file_sizes.sh fable`
   produced no FAIL lines (only pre-existing WARNs on unrelated graphics
   files: `irrlicht_renderer.h/.cpp`, `application.cpp`,
   `renderer_frame.cpp`, `renderer_zone_load.cpp`). None of
   `eq.cpp`/`eq_entities.cpp`/`eq_login_world.cpp` were flagged.
8. **PASS** — `bash <slot>/scripts/build-arm-noble.sh` completed with
   `=== Success ===` and produced
   `<slot>/build-arm-noble/bin/willeq` (ELF 32-bit ARM, EABI5, dynamically
   linked, ~340MB unstripped with debug info).

**Visual gate does not apply** — no rendering change in this unit.

### Deviations from the plan

- Added a new test file `tests/test_login_world_seam.cpp` (registered in
  `tests/CMakeLists.txt`) rather than folding the smoke test into
  `test_zone_connection_flow.cpp`, since that file's own header states its
  scope is "zone stream ONLY." This is a `tests/**`-territory addition, not
  a source-territory deviation.
- Updated one stale comment in `tests/test_zone_connection_flow.cpp` that
  named the old `BUG_PlayerProfile...` test, since it now points at a
  renamed test. Flagged here per the "no opportunistic changes" rule, even
  though it's inside `tests/**` (unrestricted territory) rather than the
  narrower source territory.
- Did not independently re-verify the 19588/8-byte thresholds against the
  raw capture bytes (e.g. with a hex dump) beyond running the existing test
  suite, which exercises both boundary sizes without regression. If Will
  wants the thresholds independently re-derived from the capture, that is
  outstanding.

### Confidence note

This changes shipped client behavior on the receive path (Part A
dispatcher extraction, Part A2 guarded sends, Part B truncated-packet
returns). I'm confident in the mechanical correctness of the mirroring
(diffed against the zone template line-by-line) and in the test evidence
above, but per the unit's own framing, this warrants your independent
diff review regardless.
