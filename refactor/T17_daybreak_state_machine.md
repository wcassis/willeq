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

(Executor fills: evidence, per-priority coverage, timer findings, bugs
pinned, mutation spot-check results, deviations.)
