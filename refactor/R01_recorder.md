# R01 — Session recorder

Add an app-packet session recorder to the client: when started with
`--record-session <file>`, every decoded application packet (all three
streams S→C, plus zone-stream C→S) is appended to a session log in the
format defined by `include/common/net/session_log.h`.

## Design

- **New class** `EQT::Net::SessionRecorder`
  (`include/common/net/session_recorder.h` + `src/common/net/session_recorder.cpp`):
  `open(path, character, zone)`, `record(stream, direction, opcode,
  data, size)`, `close()`. Timestamps: `std::chrono::steady_clock` deltas
  from open, in microseconds. Internal mutex (hooks are game-thread today;
  the mutex future-proofs). Buffered FILE*/ofstream append; flush on close
  and every N records (pick something reasonable, e.g. 64).
- **EverQuest integration**: `std::unique_ptr<SessionRecorder>
  m_session_recorder` member + `void StartSessionRecording(const
  std::string& path)` public method (opens recorder, logs INFO).
- **Hook points** (record AFTER opcode extraction, payload excluding the
  opcode — matching session_log.h's documented convention):
  - Zone S→C: the top of the `ZoneOnPacketRecv` dispatcher (eq.cpp).
  - World S→C: `WorldOnPacketRecv` (eq_login_world.cpp).
  - Login S→C: `LoginOnPacketRecv` (eq_login_world.cpp).
  - Zone C→S: `QueuePacket` and `SafeQueueZonePacket` (eq.cpp) — examine
    how the opcode sits in the outgoing packet and record consistently.
  Each hook is 2–3 lines: `if (m_session_recorder) m_session_recorder->record(...)`.
- **CLI flag**: find where the binary parses arguments (`--no-graphics`
  handling) and add `--record-session <file>`, wired to
  `StartSessionRecording` after EverQuest construction.
- Keep it lightweight — no recording of graphics/bridge traffic, no replay
  logic (that is R02).

## Verification

- `cmake --build build -j24` clean.
- `ctest --test-dir build` — green modulo documented exceptions.
- New unit test `tests/test_session_recorder.cpp` (register in
  tests/CMakeLists.txt): record a handful of synthetic packets through
  SessionRecorder, re-read the file with raw struct reads, assert
  header/record integrity (magic, version, opcodes, payload round-trip,
  monotonic timestamps).
- Manual smoke not required (no server available in this environment).

## Review

### Hook locations

- Zone S→C: `src/client/eq.cpp`, top of `EverQuest::ZoneOnPacketRecv`, right after
  `DumpPacket("S->C", opcode, p)` and before the opcode `switch`. Records
  `p.Data()+2, p.Length()-2` under `SessionStream::Zone` /
  `SessionDirection::ServerToClient`.
- World S→C: `src/client/eq_login_world.cpp`, top of `EverQuest::WorldOnPacketRecv`,
  right after `DumpPacket("S->C", opcode, p)`. `SessionStream::World`.
- Login S→C: `src/client/eq_login_world.cpp`, top of `EverQuest::LoginOnPacketRecv`,
  right after `DumpPacket("S->C", opcode, p)`. `SessionStream::Login`.
- Zone C→S (path 1): `src/client/eq.cpp`, `EverQuest::QueuePacket(uint16_t opcode,
  EQ::Net::DynamicPacket* packet)` — hook right after the null-connection guard.
  This overload already receives the payload with the opcode stripped
  (`packet->Data()/Length()`), so no offset arithmetic is needed here.
- Zone C→S (path 2): `src/client/eq.cpp`, `EverQuest::SafeQueueZonePacket(EQ::Net::Packet&
  p, int stream, bool reliable)` — hook right after the null-connection guard.
  `p` here is the full packet (opcode at offset 0), so the hook extracts
  `p.GetUInt16(0)` and records `p.Data()+2, p.Length()-2`.

All four hooks are guarded by `if (m_session_recorder && p.Length() >= 2)` (or the
non-null-`packet` equivalent) and are 2-4 lines each, matching the plan's "2-3 line
hook" intent (a couple grew to 4 lines to fit the multi-line `record()` call cleanly
under the project's line-length/style conventions).

### Flag wiring

- `include/client/application.h`: `ApplicationConfig::recordSessionPath` (new field,
  default empty).
- `src/client/application.cpp`, `Application::parseArguments()`: `--record-session
  <file>` parsed alongside the other flags; help text line added next to
  `-np, --no-pathfinding` (kept outside the `EQT_HAS_GRAPHICS` block since recording
  works in headless mode too).
- `src/client/application.cpp`, `Application::initialize()` (or equivalent init path):
  immediately after `m_eqClient->OpenDatabase(...)`, `if
  (!config.recordSessionPath.empty()) m_eqClient->StartSessionRecording(config.recordSessionPath);`.

### Recorder design notes

- `EQT::Net::SessionRecorder` (`include/common/net/session_recorder.h` +
  `src/common/net/session_recorder.cpp`) matches the plan: `open(path, character,
  zone)`, `record(stream, direction, opcode, data, size)`, `close()`.
- Timestamps use `std::chrono::steady_clock` deltas from `open()`, in microseconds,
  stored as `SessionLogRecordHeader::timestampUs` — matches `session_log.h`.
- Internal `std::mutex` guards all three public methods (hooks are game-thread only
  today; this future-proofs against a multi-threaded recorder client).
- Buffered `FILE*` (`fopen`/`fwrite`), flushed on `close()` and every 64 records
  (`FLUSH_INTERVAL`).
- `open()` refuses to reopen an already-open recorder (returns false, logs a
  warning) rather than silently leaking the old file handle.
- `EverQuest::StartSessionRecording()` (`src/client/eq.cpp`, right before
  `OpenDatabase`) constructs the recorder, calls `open()` with the current
  `m_character`/`m_current_zone_name`, and resets the `unique_ptr` back to null on
  failure so hooks stay no-ops.
- `EverQuest::m_session_recorder` (`include/client/eq.h`) is a
  `std::unique_ptr<EQT::Net::SessionRecorder>`, null unless recording was started —
  every hook checks it first, so there is zero overhead when not recording beyond
  the null check.

### Test coverage

New `tests/test_session_recorder.cpp` (registered in `tests/CMakeLists.txt`,
links `src/common/net/session_recorder.cpp` directly — no EverQuest/network deps):

- `OpenWritesValidHeader` — opens, closes, re-reads the file header with raw
  `fread`, checks magic/version/character/zone.
- `RecordRoundTripsPayloadAndOpcode` — records 3 synthetic packets (two with
  payloads on different streams/directions, one zero-length payload), re-reads
  every record header + payload with raw struct reads, verifies opcode, stream,
  direction, size, payload bytes, monotonic timestamps, and clean EOF after the
  last record.
- `RecordBeforeOpenIsNoOp` — `record()` on an unopened recorder does not crash and
  does not increment the record count.
- `DoubleOpenFails` — a second `open()` call while already open returns false and
  does not disturb the first file.
- `CloseIsIdempotent` — calling `close()` twice does not crash.

All 5 new tests pass. Full suite: 1196/1197 passed; the one failure
(`AudioManagerTest.PreloadCommonSounds`) is the pre-existing, documented
ALSA-environment exception in `refactor/test_baseline.txt` (unrelated to this
change). Build is clean (only pre-existing warnings in unrelated files, e.g.
`DES_ncbc_encrypt` deprecation and `void*` pointer-arithmetic warnings in
`eq_merchant.cpp`/`eq_entities.cpp`/`eq.cpp` that predate this unit).

### Deviations from the plan

- None of substance. Minor: the CLI help text for `--record-session` was placed
  next to `-np, --no-pathfinding` rather than inside the `EQT_HAS_GRAPHICS` block
  (alongside `--zone-load` etc.) since session recording is independent of
  graphics/headless mode.
- `SessionRecorder` lives in `namespace EQT::Net` (matching `session_log.h`, which
  the plan says not to change) even though most of `include/common/net/` uses bare
  `namespace EQ::Net` — this is `session_log.h`'s existing convention, preserved
  as-is per the "do not change the format" instruction.
