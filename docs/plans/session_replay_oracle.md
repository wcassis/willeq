# Session Replay Oracle (Batch R)

Supersedes [`docs/plans/pcap_replay_test_plan.md`](pcap_replay_test_plan.md).

Goal: a deterministic regression oracle for the client, built from a real
play session, at two different layers of the stack.

## Two layers

WillEQ has two independent, complementary replay-test layers. They replay
different things, at different points in the pipeline, for different
purposes — neither replaces the other.

### Layer 1 — Protocol-level pcap replay (`tests/test_*pcap*`, `tests/test_daybreak_harness.cpp`)

Replays **raw UDP packets** captured off the wire (`.pcap`, tcpdump format)
through the Daybreak transport layer — CRC validation, zlib decompression,
fragment reassembly, `OP_Combined`/`OP_AppCombined` unpacking — to catch
regressions in `DaybreakConnection` itself (the code in
`src/common/net/daybreak_connection.cpp` that turns a UDP datagram into a
decoded application packet).

Files (registered in `tests/CMakeLists.txt` as of R03):

| File | What it tests |
|---|---|
| `tests/pcap_test_utils.h` | Header-only `.pcap` file reader/parser (link-layer + IP + UDP framing, dedup, port filtering). No production code dependency. |
| `tests/test_pcap_extractor.cpp` | Exercises the reader above against a real capture. |
| `tests/daybreak_test_harness.h` | Header-only reimplementation of the Daybreak decode pipeline (CRC strip, decompress, fragment assembly, combined-packet unpacking) for fast, dependency-free exploration — does **not** touch `DaybreakConnection`. |
| `tests/test_daybreak_harness.cpp` | Synthetic unit tests against the harness above (`DaybreakHarnessTest.*`, always run — no capture needed) plus capture-driven exploration tests (`PcapDaybreakHarnessTest.*`, skip without a capture). |
| `tests/test_pcap_step_through.cpp` | Verbose, byte-by-byte decode trace over a capture (debugging aid, not an assertion-heavy regression test). |
| `tests/test_daybreak_connection_pcap.cpp` | The one test here that matters most for regressions: feeds real captured bytes through the **actual production** `DaybreakConnection::ProcessPacket()` (via `#define private public`) and checks it decodes app packets without errors. |

All of these need a `.pcap` capture to do anything beyond the always-green
synthetic tests in `test_daybreak_harness.cpp`. Resolution order:

1. `WILLEQ_PCAP` environment variable, if set.
2. `data/captures/session.pcap` (relative to cwd), otherwise.

If neither exists, every capture-dependent `TEST_F` calls
`GTEST_SKIP() << "no capture at " << path` **from the test body itself**
(never from a helper — see the F00 gotcha in `tests/test_ui_sounds.cpp`:
`GTEST_SKIP()` only unwinds the function it's called directly in, so a
skip issued from inside a helper method does not skip the calling
`TEST_F`). `data/captures/` is gitignored — nothing here ships in the repo.

### Layer 2 — App-level session-log replay (`include/common/net/session_log.h`, `tests/test_session_replay.cpp`)

Replays **decoded application packets** (post-CRC, post-decompression,
post-fragment-reassembly — i.e. what a zone handler actually receives) into
a headless `EverQuest` instance and asserts on the resulting **game state**
(entity list, player stats, zone name, ...), not on transport-layer
mechanics. This is the regression oracle for game-logic changes (entity
tracking, movement, combat, chat, etc.) — the layer most day-to-day changes
actually touch.

Format: `include/common/net/session_log.h` — `SessionLogFileHeader` +
a stream of `SessionLogRecordHeader` + payload records, one per decoded
app packet, tagged with stream (Login/World/Zone), direction, and opcode.
Independent of protocol-level compression/fragmentation, so it stays valid
even if the wire format changes.

This layer ships (Batch R, unit R02) with a synthetic in-memory session so
`test_session_replay.cpp`'s core test is green with no capture at all. It
automatically upgrades to golden-snapshot mode when a real recording
exists at `data/captures/session.sleq` (override via `WILLEQ_SESSION_LOG`).
See `refactor/R02_replay_harness.md` for the harness/seam design.

## What exists after Batch R

- Layer 1: all pcap/daybreak-harness test binaries build and register in
  CMake (previously true only of the source files, not the CMake wiring —
  R03). They compile clean, and every capture-dependent test SKIPS on a
  host with no capture. The always-on synthetic tests in
  `test_daybreak_harness.cpp` (`DaybreakHarnessTest.*`) exercise the
  decode-pipeline logic unconditionally.
- Layer 2: recorder CLI flag (R01) + replay harness and synthetic test
  (R02) — see their respective `refactor/R0{1,2}_*.md` docs for status.
- Neither a `.pcap` nor a `.sleq` capture ships in the repo (verify:
  `data/captures/` is gitignored). Both oracles run in degraded-but-green
  mode until Will records one.

## Recording a real session (for Will)

### App-level session log (primary — feeds Layer 2)

```bash
./build/bin/willeq -c willeq.json --record-session data/captures/session.sleq
```

Play a short, varied session covering the state transitions the oracle
cares about:

1. Log in and enter world.
2. Zone at least once (to exercise `NewZone`/zone-spawn packets).
3. Move around for a bit (position updates).
4. Kill something (combat + loot + XP packets).
5. Camp out cleanly.

Then quit. The recorder writes decoded app packets for all three streams
(login/world/zone), both directions, in `session_log.h` format.

The **first** `ctest` run of `test_session_replay.cpp` after this file
exists will detect it, replay it, and — since no golden snapshot exists
yet — **write** `data/captures/session.golden` next to it and report a
"golden created — re-run to verify" skip. Run `ctest` a second time to get
a real pass/fail against that golden snapshot. Commit neither file — both
live under the gitignored `data/captures/` and are meant to stay local
(or be shared out-of-band if the team wants a shared regression fixture).

### Raw pcap (optional — feeds Layer 1)

Only needed if you're chasing a suspected transport-layer bug (CRC,
compression, fragmentation) rather than a game-state bug. Capture while
playing, on the interface that carries traffic to your EQ server:

```bash
tcpdump -i <iface> -w data/captures/session.pcap udp and host <server>
```

Replace `<iface>` with your outbound interface (`ip route get <server-ip>`
will tell you) and `<server>` with the login/world/zone server hostname or
IP. Leave it running for the same kind of session described above, then
Ctrl-C. Re-run the Layer-1 test binaries (`test_pcap_extractor`,
`test_daybreak_harness`, `test_pcap_step_through`,
`test_daybreak_connection_pcap`) or `ctest --test-dir build` generally —
their capture-dependent tests will pick up
`data/captures/session.pcap` automatically (or point `WILLEQ_PCAP` at a
capture stored elsewhere).
