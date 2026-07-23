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

(executor fills in: hook locations with line refs, flag wiring, test results)
