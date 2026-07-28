# T03a — Unhardcode zone port 7000 in the pcap test family

**Slot: t1. x86 only — no Pi, no eqemu (the fixture is already
recorded; you must NOT start the server).** No sibling unit; t2 idle.

**Territory: `tests/` ONLY** — `tests/pcap_test_utils.h` and the four
pcap/daybreak test files. No changes under `src/` or `include/`, not
even a seam. If you think you need one, STOP and report.

Read `refactor/EXECUTOR_PROTOCOL.md` first and follow it exactly.
Context: `refactor/testing_progress.md` (T01 survey, "Incoming"
section) and `refactor/deferred.md` (the P29 entry that routed this to
the testing session).

## Why this unit

P29 recorded real capture fixtures. They are live at
`/mnt/projects/fable/captures/`, reached via a `data/captures`
symlink. But the test family that consumes them assumes the zone
server listens on **port 7000**, and it does not: the akk-stack image
auto-boots ~15 static zones that claim 7000–7011, so real captures
land on 7012+.

The fixtures are therefore only half paying off. Every future capture
against this image will hit the same wall.

## Ground truth (measured by the orchestrator, trust it over the docs)

`/mnt/projects/fable/captures/session.pcap`: linktype **276**
(LINKTYPE_LINUX_SLL2 — already handled correctly by
`getLinkHeaderSize()`, not your problem), 5,603 UDP packets.

Actual ports, by packet count:

| port | packets | role |
|-----:|--------:|------|
| 7026 | 3172 | zone — qeynos2 |
| 39697 | 3172 | client side of the qeynos2 session |
| 9000 | 1286 | world |
| 7012 | 920 | zone — befallen |
| 36481 | 920 | client side of the befallen session |
| 5998 | 225 | login |
| 49657 / 40737 / 53881 / 35934 | — | client ephemeral ports |

**There is no port 7000 traffic in the capture at all.**

## The two defects

1. **`tests/pcap_test_utils.h:146`** —
   `std::set<uint16_t> server_ports = {5998, 5999, 7000, ..., 7007, 9000};`
   This set drives server↔client direction classification. It misses
   7012 and 7026, so **4,092 of 5,603 packets are misclassified**. The
   visible symptom: `PacketStats_ServerToClient` analyzes only 222
   packets (login traffic) instead of the full session, finds zero
   compressed packets, and fails.
2. **`filter_src_port = 7000`** hardcoded in
   `tests/test_daybreak_harness.cpp:179`,
   `tests/test_daybreak_connection_pcap.cpp:70`,
   `tests/test_pcap_extractor.cpp:207`, and the `src_port == 7000`
   comparisons throughout `tests/test_pcap_step_through.cpp`
   (lines ~496, 502, 534, 550, 586). Filtering on 7000 yields zero
   packets, so those tests skip or fail.
   `test_daybreak_connection_pcap.cpp` additionally hardcodes the
   server IP in `createTestConnection("172.18.0.3", 7000)` at lines
   193, 302, 393, 464.

## Current failure set (reproduced by the orchestrator)

Run with `WILLEQ_PCAP=/mnt/projects/fable/captures/session.pcap`.
**4 FAILs:**
- `PcapExtractorTest.PacketStats_ServerToClient`
- `PcapExtractorTest.FindCompressedPackets`
- `PcapStepThroughTest.StepThroughZoneServerSession`
- `PcapStepThroughTest.WriteDetailedLogToFile`

**8 SKIPs:**
- `DaybreakConnectionPcapTest.{ReplayZoneServerPackets,
  DetailedFragmentTracking, SingleCompressedFragmentDebug,
  ReplayWithCRCBypassed}`
- `PcapDaybreakHarnessTest.{ProcessZoneServerPackets,
  ProcessFirstFewPackets_Verbose, AnalyzeCompressedFragments}`
- `PcapStepThroughTest.StepThroughFragmentSequence`

This is your before/after baseline. Record it, then show the after.

## Steps

### Step 1 — port discovery in `tests/pcap_test_utils.h`

Add, alongside the existing `getTestPcapPath()`:

- `getTestZonePort()` — returns the zone server port to use.
  Resolution order:
  1. `WILLEQ_ZONE_PORT` environment variable, if set and non-empty.
  2. Autodiscovery from the capture (below).
- A discovery helper that scans the pcap and returns the server ports
  it finds, most-trafficked first.

**Discovery method — use the handshake, not a heuristic.** Scan for
`OP_SessionResponse` (opcode `0x0002`); the port that *sends* it is a
server port, and its peer is the client. That is robust against
ephemeral client ports landing anywhere in the range. Classify the
discovered server ports: login/world are the well-known 5998/5999/9000;
anything else is a zone.

If discovery finds multiple zone sessions (this fixture has two:
befallen 7012 and qeynos2 7026), `getTestZonePort()` returns the
**busiest** one, so a test asking for "the zone server" gets the
substantive session. Expose the full list too — a test that wants to
iterate sessions should be able to.

Make the `server_ports` set at line 146 **derived from discovery**
rather than a hardcoded literal. Keep the well-known login/world ports
as a seed; add the discovered ones.

Cache discovery per-path so you are not re-scanning a 1.3 MB file once
per test.

### Step 2 — replace the hardcoded 7000s

Update the four test files to call `getTestZonePort()` instead of the
literal. In `test_pcap_step_through.cpp` that means replacing the
`src_port == 7000` comparisons; take care with `!=` at line ~550.

For `test_daybreak_connection_pcap.cpp`'s
`createTestConnection("172.18.0.3", 7000)`: discover the server IP the
same way (it is the source IP of the `OP_SessionResponse`) rather than
swapping one hardcoded value for another. If that turns out to be
awkward, an `WILLEQ_ZONE_IP`-style override with the discovered value
as default is acceptable — say which you chose and why.

### Step 3 — keep the no-fixture path working

The tests must still `GTEST_SKIP()` cleanly when no capture exists —
that behavior predates this unit and other checkouts rely on it. A
missing capture must not become a hard failure, and discovery on a
missing/corrupt file must not crash.

## Expectations, and one thing I am NOT promising

With direction classification fixed, `PacketStats_ServerToClient`
should see the whole session rather than 222 login packets, and the
two compression tests should then find compressed packets — the zone
stream is where compression lives.

**But I have not verified that this server actually compresses.** If,
after the fix, `compressed_packets` is still 0 across the full zone
traffic, that is a *finding*, not something to force green. Do not
weaken the assertion to make it pass. Report it, and leave the test
failing with a clear message — I would rather have an honest red than
a test that asserts nothing. Same for any of the 12 that do not come
back: report which and why.

## STOP conditions

- A fix requires changing `src/` or `include/` → STOP and report.
- You conclude the fixture itself is wrong/incomplete → STOP. The
  capture is the perf session's artifact (P29); we do not re-record it,
  and you must not start eqemu to make a new one.
- A test needs its assertions weakened to pass → STOP and report rather
  than weakening.

## Acceptance criteria

1. No hardcoded `7000` remains as a zone-port assumption in `tests/`.
   (`test_formatted_message.cpp:75` contains `7000` inside a hex item
   link string — unrelated, leave it alone.)
2. Build clean: GLES2 config, which is now the only tree. Note the
   configure gotcha: `-DEQT_GLES2_IRRLICHT_DIR` must be an **absolute**
   path (CMake's `EXISTS` is undefined for relative paths) — the
   relative form documented in `CLAUDE.md` fails. Bootstrap first if
   `third_party/irrlicht-gles2-x86/lib/libIrrlicht.a` is absent:
   `bash <slot>/scripts/build-gles2-x86.sh --irrlicht-only`.
3. Create the fixture symlink in your slot if absent:
   `ln -s /mnt/projects/fable/captures <slot>/data/captures`.
4. The 4 FAILs above are fixed, and as many of the 8 SKIPs as
   legitimately can be now run and pass. Give a before/after table
   naming every one of the 12 and its new state.
5. Full suite green: 3 consecutive runs, documented exceptions only
   (`AudioManagerTest.PreloadCommonSounds`;
   `RDPServerFrameTest.*` — rerun isolated before believing).
6. Tests still skip cleanly with no capture present. Demonstrate it
   (e.g. point `WILLEQ_PCAP` at a nonexistent path).
7. `bash <slot>/scripts/refactor/check_file_sizes.sh fable` — no new
   FAILs.

Visual gate and ARM build do not apply (tests-only diff). Say so.

## Review

Per the protocol: evidence per criterion, the before/after table for
all 12 tests, the three green runs, deviations flagged. State plainly
anything you could not make pass and why. An honest partial beats an
overclaim I will catch reading the diff.
