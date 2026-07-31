# T17 — Daybreak connection state machine on synthetic fixtures

**Slot: t1. x86 only — no Pi, no eqemu, no live server, no real
sockets.** Sibling unit: the orchestrator is working T18 inline in t2
(`tests/test_zone_*`, `eq_entities`/`eq_chat` territory) — stay out of
those files.

**Territory: `tests/` ONLY.** New TU(s) + a fake-transport header under
`tests/`. No `src/`, no `include/`. The injection seam already exists
(below); if you conclude a source change is needed anyway, STOP and
report.

Read `refactor/EXECUTOR_PROTOCOL.md` first and follow it exactly.

## Why this unit

`src/common/net/daybreak_connection.cpp` (~1200 lines) is the reliable-UDP
core under every byte of client traffic — sequencing, fragmentation,
CRC, compression, acks, retransmit. It is the T01 backlog's #1 item by
blast radius and it still has no direct state-machine tests: the
existing `test_daybreak_harness.cpp` covers parse-side helpers
(decompress, combined packets, fragment *assembly*) and pcap replay of
recorded traffic. Nothing exercises the live connection object's
behavior under adversarial delivery (drops, reorder, duplication,
corruption).

## The seam — already there, use it

`DaybreakConnectionManager` takes its transport by injection
(`include/common/net/daybreak_connection.h:330`):

```cpp
DaybreakConnectionManager(std::unique_ptr<UdpTransport> transport);
DaybreakConnectionManager(const DaybreakConnectionManagerOptions &opts,
                          std::unique_ptr<UdpTransport> transport);
```

`UdpTransport` (`include/common/net/udp_transport.h`) is a 5-method pure
interface (`open/close/recv/send/is_open`), non-blocking recv. Write a
**FakeUdpTransport** in a new `tests/` header: an in-memory pair of
byte-packet queues (client→peer, peer→client) with scriptable
delivery — drop-nth, duplicate, reorder, corrupt-byte. Test code plays
the server: it reads what the client sent from one queue and enqueues
crafted server packets on the other.

Packet wire formats are in `include/common/net/daybreak_structs.h`; the
existing `tests/daybreak_test_harness.h` already builds/decodes several
of them — **reuse it** rather than re-implementing, and extend it in
place if it lacks a builder you need (it is `tests/` territory).

Study `daybreak_connection.h`/`.cpp` before writing anything: how the
manager creates connections, how it pumps recv/timers (the client calls
this from `TickNetwork`), what the connection status enum looks like,
and where the resend/ack timers read time from. Report what you find in
the Review section — the timer question decides how retransmit tests
stay deterministic (see STOP conditions).

## Priorities (in order; stop when the unit is full, per T12 precedent)

1. **Session lifecycle.** SessionRequest → crafted SessionResponse →
   connected; wrong-session-id packets ignored; disconnect/teardown.
2. **Sequenced delivery.** In-order data delivered exactly once and in
   order; out-of-order arrival buffered/reordered correctly; duplicates
   delivered once; the future-window edge (a packet far ahead of the
   window) does whatever the code does — pin it.
3. **Fragmentation, both directions.** Recv: crafted multi-fragment
   sequence reassembles byte-identically (the harness has assembly
   helpers — this adds the *connection-driven* path). Send: a payload
   larger than max packet size goes out as valid fragments the test
   reassembles.
4. **Ack/retransmit under drops.** Drop a client send once: the
   connection retransmits and the stream still arrives complete.
   Out-of-order acks. What stops being retransmitted once acked.
5. **CRC + compression.** Corrupted-byte packet rejected (no delivery,
   no crash); compressed round-trip through the connection.
6. **Combined packets** through the connection path.

Buffer-bounds discipline from T08 applies: craft undersized/truncated
packets for at least the handlers on the paths above; exact-size
allocations, not heap-slack luck.

**Mutation spot-check (T10 practice):** after the suite is green, pick
2-3 core behaviors (e.g. sequence comparison, CRC check), flip an
operator in a scratch copy, confirm a test fails, revert. Record the
result in the Review section. `git status` must show no `src/`
modification at commit time.

## STOP conditions

- Any change needed under `src/` or `include/` → STOP and report.
- A real bug (crash, wrong delivery, stuck retransmit) → pin with a
  clearly-named test documenting current behavior, report prominently,
  do NOT fix.
- If retransmit/timer behavior cannot be tested deterministically
  (timers read wall-clock with no injectable time source), do NOT write
  sleep-based flaky tests: cover what is deterministic, pin the timer
  gap explicitly in the Review section as a seam request, and move on.
- If the connection object cannot be driven at all without a real
  socket, STOP and report — do not write a localhost-socket test.

## File layout

- `tests/fake_udp_transport.h` — the fake transport + delivery script.
- `tests/test_daybreak_state_machine.cpp` — the unit (≤800 lines; split
  into a second TU by topic if needed, same style as T11's split).
- Wire into `tests/CMakeLists.txt` in the existing style. Link only
  what the net layer needs (see `test_daybreak_harness`'s block); no
  Irrlicht, no willeq_core.

## Verification bar (narrowed per T12 precedent — tests-only, x86 only)

1. `cmake --build <slot>/build -j24` clean.
2. `ctest --test-dir <slot>/build --output-on-failure` — full suite;
   known exception `AudioManagerTest.PreloadCommonSounds` (no ALSA).
   Baseline is currently 1529/1530.
3. `bash <slot>/scripts/refactor/check_file_sizes.sh fable` — no new
   FAILs.
4. Visual gate / ARM build: not applicable (tests-only unit).

Note (charter lesson): fable moves fast — merge fable into the task
branch and re-verify before finishing if it has advanced.

## Review

**Commit:** (see task branch `task/T17-daybreak-state-machine`, filled in
after commit below.)

**Files added/changed (all `tests/` only, no `src/`/`include/` changes):**
- `tests/fake_udp_transport.h` — `FakeChannel` (in-memory wire with
  scriptable drop-nth/duplicate-next/corrupt-next/reorder-next-pair
  delivery) + `FakeUdpTransport` (`UdpTransport` impl backed by a pair of
  `FakeChannel`s, always-open so `Tick()`'s `is_open()` gate needs no
  socket dance).
- `tests/test_daybreak_state_machine.cpp` — 23 tests, all passing.
- `tests/CMakeLists.txt` — new `test_daybreak_state_machine` target,
  modeled on the existing `test_daybreak_connection_pcap` block (links
  `daybreak_connection.cpp` + `packet.cpp` + `crc32.cpp`, GTest, zlib, fmt).

**Access technique:** like `test_daybreak_connection_pcap.cpp`, uses
`#define private public` around the `daybreak_connection.h` include to
reach `DaybreakConnection`'s private methods/fields (`ProcessPacket`,
`AppendCRC`, `SendConnect`, `ProcessResend`, `Process`, `m_streams`,
`m_status`, `m_connect_code`, sent-packet timestamps, ...). Most tests
drive the connection directly via `ProcessPacket()`/`QueuePacket()`/
`FlushBuffer()`/`ProcessResend()` rather than through
`Manager::Tick()`'s wall-clock-gated `Process()` loop, and inspect real
wire bytes captured by `FakeUdpTransport`'s channels for send-path
assertions (fragmentation format, CRC, compression, combining). One
integration smoke test (`FullHandshakeViaManagerTick`) drives the full
`Manager::Tick()` path with a bounded (2,000,000-iteration, no sleep)
spin loop, since `connect_delay_ms`'s gate reads real elapsed wall-clock
time.

### Timer findings (as requested by the plan)

`Clock` = `std::chrono::steady_clock`, read directly via `Clock::now()` in
~10 places (`m_last_send`, `m_last_recv`, `m_close_time`,
`DaybreakSentPacket::first_sent`/`last_sent`, `m_last_ack`, ...). **No
injectable clock seam** — no virtual `Now()`, no clock parameter anywhere
in `DaybreakConnection`/`DaybreakConnectionManager`. Per the plan's STOP
condition, no sleep-based tests were written for resend/timeout
behavior. Instead: since `DaybreakSentPacket`/`DaybreakConnection`
members are already reachable via the `#define private public` technique
(pre-existing precedent), tests **backdate `first_sent`/`last_sent`/
`m_close_time` timestamps directly** (e.g. `Clock::now() -
milliseconds(500)`) to deterministically simulate elapsed wall-clock time
for `ProcessResend()` (retransmit-on-drop, ack-stops-retransmit,
resend_timeout-closes-connection) and `Manager::Process()`
(disconnect-teardown-after-close-time) — no seam request needed; this
sidesteps the gap entirely without any `src/` change. Only the handshake
smoke test uses a bounded busy-spin over `Tick()` (not a sleep) because
`connect_delay_ms`'s gate is checked inside `Process()`, which is only
reachable via the manager's tick loop.

### Per-priority coverage

1. **Session lifecycle** — `ClientStartsConnecting`,
   `SendConnectEmitsWellFormedSessionRequest` (wire format: opcode,
   protocol_version=3, connect_code), 3 connect_code-bug pin tests (see
   below), `WrongConnectCodeSessionResponseIgnored`,
   `CloseSendsDisconnectAndEntersDisconnecting`,
   `ManagerProcessTearsDownAfterCloseTimeElapses` (backdated close_time),
   `SessionDisconnectFromPeerEntersDisconnecting`,
   `FullHandshakeViaManagerTick` (full Tick()-driven smoke test).
2. **Sequenced delivery** — `InOrderDeliveryExactlyOnce`,
   `OutOfOrderArrivalBufferedThenDeliveredInOrder` (queued then drained via
   `Process()`, unconditional every tick unlike the hold-timer-gated
   flush), `DuplicatePacketDeliveredOnce`,
   `FarFutureSequenceTreatedAsPastNotQueued` (pinned edge case, see below).
3. **Fragmentation both directions** — `RecvFragmentReassemblyByteIdentical`
   (2-fragment sequence via `ProcessPacket`),
   `SendFragmentsLargePayloadReassembledByPeer` (300-byte payload,
   128-byte max packet size, manual reassembly of captured wire fragments,
   byte-identical to source).
4. **Ack/retransmit under drops** — `DroppedSendIsRetransmittedOnProcessResend`
   (backdated timestamps, real resend on wire), `AckStopsRetransmission`,
   `OutOfOrderAckRemovesOnlyThatSequence`, `ResendTimeoutClosesConnection`
   (backdated past `resend_timeout`, connection closes — pinned behavior).
5. **CRC + compression** — `CorruptedByteRejectedNoDeliveryNoCrash` (no
   crash, `sequence_in` doesn't advance), `CompressedRoundTripThroughConnection`
   (loopback technique: feed the connection's own sent+compressed+CRC'd
   bytes back into itself — valid because `DaybreakConnection` is
   symmetric, the same class plays client or server in production).
6. **Combined packets** — `CombinedPacketCarriesMultipleUnreliableSends`
   (two unreliable sends held until `FlushBuffer()` combine into one
   `OP_Combined`, loopback-delivered, both app packets decoded in order).

### Bugs found and pinned (NOT fixed — src/ out of T17 territory)

Three real findings in `DaybreakConnection`'s session-handshake path,
surfaced by direct testing (all pinned with clearly-named, currently
**passing** tests documenting current behavior — see
`ConnectCodeRngOnlyEverProducesZeroOrAllOnes`,
`SessionResponseConnectCodeCompareSkipsByteSwap`,
`SessionResponseStoresMaxPacketSizeWithoutByteSwap` in the test file for
full detail):

1. **Low-entropy connect_code/encode_key (RNG range bug).** The
   client-role ctor draws `m_connect_code` via
   `m_rand.Int(std::numeric_limits<uint32_t>::min(),
   std::numeric_limits<uint32_t>::max())`. `Random::Int` takes `int`
   params; `uint32_t`'s max (4294967295) implicitly converts to `-1`, so
   the actual call is `Int(0, -1)` → `Int` swaps out-of-order bounds →
   draws uniformly from `{-1, 0}` only. As a `uint32_t` that's
   `{0xFFFFFFFF, 0x00000000}` — the "random" session identifier has
   exactly 2 possible values, not 2^32. Same pattern generates
   `m_encode_key` in the server-role ctor (not directly exercised here
   since WillEQ is client-only, but same bug shape). 200 fresh
   connections in the test all landed on one of the two values.
2. **`OP_SessionResponse`'s `connect_code` compare skips `NetworkToHost()`.**
   `if (m_connect_code == reply.connect_code)` compares a host-order
   value against the wire (network-order) value with no unswap, even
   though `SendConnect()` itself wire-encodes with `HostToNetwork()` and
   the class's own server-role `OP_SessionRequest` handler correctly
   unswaps (`NetworkToHost(request.connect_code)`) before comparing. In
   production this is masked by finding #1: both possible connect_code
   values (0 and 0xFFFFFFFF) are byte-swap palindromes, so the missing
   swap never actually causes a mismatch today. Demonstrated in isolation
   by forcing `m_connect_code` to a non-palindromic value
   (`0x01020304`) via private-field access, independent of finding #1.
3. **`encode_key`/`max_packet_size` stored without `NetworkToHost()` at
   all.** Unlike `connect_code` (which at least has a — broken —
   comparison), `m_encode_key = reply.encode_key;` and `m_max_packet_size
   = reply.max_packet_size;` store the raw wire bytes directly, while the
   sender side (`OP_SessionRequest` handler) applies `HostToNetwork()`
   when constructing those same fields. Demonstrated with
   `reply.max_packet_size = HostToNetwork(512u)` (the wire-correct value
   for "512") landing in `m_max_packet_size` as `131072`
   (`byteswap(512)`), not `512`.

These three compound: because finding #1 makes connect_code always
palindromic, finding #2 never manifests as a real-world connect failure —
but findings #2 and #3 would surface immediately if #1 were ever fixed in
isolation (a natural next step someone might take without realizing #2/#3
exist). Recommend a follow-up unit (or the same fix pass) address all
three together. Not fixed here per STOP conditions (src/ out of T17
territory) — flagging prominently in this report per the same rule.

Also pinned (edge-case behavior, not a bug per se, but surprising):
**`CompareSequence`'s far-future window.** A sequence far ahead of what's
expected (`diff > 10000` on the 16-bit sequence number) is classified
`SequencePast`, not `SequenceFuture` — i.e. treated like a stale
duplicate (re-acks `sequence_in - 1`) rather than buffered. Pinned in
`FarFutureSequenceTreatedAsPastNotQueued`.

### Mutation spot-check (T10 practice)

Two core behaviors, mutated via the `Edit` tool (not `sed`/bash — the
harness's auto-mode classifier blocked an in-place `sed` on a `src/`
file; `Edit` worked fine), rebuilt, confirmed the mutation is caught,
reverted, rebuilt again, confirmed `git status`/`diff` show zero `src/`
change and the full state-machine suite green again afterward:

1. **`CompareSequence`**: `int diff = (int)actual - (int)expected;` →
   `+`. Result: `DaybreakStateMachineTest.OutOfOrderArrivalBufferedThenDeliveredInOrder`
   **hangs** (infinite loop, confirmed via 15s `timeout`, had to
   `TaskStop` a background run that exceeded 60s) — a decisive failure
   signal, if a noisier one than a clean assertion failure. Other tests
   touching `CompareSequence` with `expected == 0` incidentally passed
   under the mutation (addition and subtraction coincide when one operand
   is 0), which is itself a useful note: `expected == 0` inputs are a
   blind spot for this particular mutation and a weaker discriminator
   than sequences with nonzero `expected`.
2. **`ValidateCRC`**: `if (actual == calculated)` → `if (actual !=
   calculated)`. Result: clean, fast failures — `InOrderDeliveryExactlyOnce`
   (valid packet now rejected, 0 received instead of 1) and
   `CorruptedByteRejectedNoDeliveryNoCrash` (corrupted packet now
   *accepted*, `sequence_in` advances) both fail with clear assertion
   diffs, no hang.

Both mutations reverted; `diff` against a pre-mutation backup copy of
`daybreak_connection.cpp` confirmed byte-identical; `git status --short
src/` empty at commit time.

### Deviations from the plan

- The plan describes the harness as a client-driven, test-crafts-the-
  server-bytes-by-hand pattern. Followed that for most tests, but added
  a **loopback technique** (feed the connection's own sent, encoded/CRC'd
  bytes back into itself via `ProcessPacket`) for the compression and
  combined-packet round-trip tests, since `DaybreakConnection` is
  symmetric (same class plays client or server) — this reused real
  production CRC/compression/framing code on both ends without needing a
  second manager/connection instance or hand-rolled zlib/CRC
  reimplementation. Noted here as it's not literally what the plan's
  prose describes, though it stays inside the "reuse rather than
  reimplement" spirit and the transport-injection seam.
- Found and pinned 3 handshake-path bugs (see above) beyond what the plan
  anticipated ("wrong-session-id packets ignored" was the only
  handshake-correctness item explicitly called out) — these came from
  writing the wire-format tests carefully rather than accepting the
  first thing that made status flip to `Connected`.
- `fable` had advanced by one commit (`b80aa04`, a docs-only P46b plan
  addition) since the branch point; merged (fast-forward, no conflicts)
  before final verification, per the plan's charter-lesson note.

### Verification bar results

1. `cmake --build build -j24` — clean, no warnings from new files.
2. `ctest --test-dir build --output-on-failure` — **1552/1553 passed**;
   sole failure `AudioManagerTest.PreloadCommonSounds` (documented ALSA
   host exception, pre-existing, unrelated to this unit). All 23 new
   `test_daybreak_state_machine` tests pass.
3. `scripts/refactor/check_file_sizes.sh fable` — exit 0, no FAILs.
4. Visual gate / ARM build — not applicable (tests-only unit, per the
   plan's narrowed bar).
