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

**Territory respected**: only `tests/pcap_test_utils.h` and the four named
test files changed. No `src/` or `include/` touched, not even a seam.
`git diff --stat`: 5 files, +250/-45.

### Implementation summary

`pcap_test_utils.h`:
- Split the old single-pass `readPcapFile()` into `readPcapFileRaw()`
  (parses every UDP packet, no port filtering, no dedup — the new shared
  low-level parser) and `readPcapFile(filename, options)` (applies
  `PcapReadOptions` filtering/dedup/`max_packets` on top of the raw list).
  Output is behaviourally identical to the old single-pass version for
  existing callers; this split exists so port discovery does not need to
  go through `PcapReadOptions` (which would be circular, since
  `PcapReadOptions::server_ports`'s default now comes from discovery).
- Added `discoverPorts(path)`: scans the raw packet list for
  `OP_SessionResponse` (opcode `0x02`); the *source* of that packet is a
  server port/IP, its peer the client — the handshake-based method the
  plan called for, not a port-number heuristic. Classifies well-known
  ports (5998/5999/9000) as login/world; any other discovered server port
  is a zone-session candidate. Counts total packets (either direction)
  touching each zone candidate to rank sessions busiest-first. Cached
  per-path in a `static std::map`, so each test binary scans the 1.3MB
  capture once regardless of how many fixtures/tests call it. Never
  crashes on a missing/corrupt file — returns `valid=false` with just the
  well-known seed ports.
- `PcapReadOptions::server_ports` default now calls `defaultServerPorts()`
  (well-known seed + discovered ports), replacing the old hardcoded
  `{5998, 5999, 7000..7007, 9000}` literal.
- Added `getTestZonePort()` (env `WILLEQ_ZONE_PORT` override, else busiest
  discovered zone session), `getTestZoneIp()` (companion, `WILLEQ_ZONE_IP`
  override), and `getTestZoneSessions()` (full list, busiest-first, for
  tests that want to iterate more than "the" zone server).

Four test files: replaced every hardcoded `filter_src_port = 7000` /
`src_port == 7000` / `src_port != 7000` comparison with `getTestZonePort()`
(cached per-fixture in `test_pcap_step_through.cpp` to avoid repeated
lookups within one test body). `test_daybreak_connection_pcap.cpp`'s
`createTestConnection("172.18.0.3", 7000)` (4 call sites) now reads
`createTestConnection(getTestZoneIp(), getTestZonePort())` — chose true
discovery over an IP-override env var since discovery already had to find
the source IP to classify the handshake, so reusing it was not awkward;
`WILLEQ_ZONE_IP` is still available as an override for anyone who needs one
without re-recording.

`test_pcap_extractor.cpp`'s `FilterByZoneServerPort` test also gained an
`EXPECT_GT(result.packets.size(), 0u)` it did not have before — under the
old hardcoded-7000 filter this test always matched 0 packets and passed
vacuously (the `EXPECT_EQ` loop body never ran). This is a strengthening,
not a weakening: it now asserts the discovered zone port actually yields
packets, which is what the test's name always implied.

### Discovered ground truth (matches the plan's measured values)

Busiest zone session found by `discoverPorts()`: qeynos2, IP from the
`OP_SessionResponse` source, port **7026**. This matches the plan's table
(7026, 3172 raw packets) exactly, and the resulting session is confirmed
via `test_daybreak_harness`'s output: `Session established:
compression=1`, `crc=2 enc1=1 enc2=0`. `PacketStats_ServerToClient` now
sees 1682 total packets (1415 protocol / 267 app) instead of 222
login-only packets.

### Compression finding — not weakened, genuinely found

The plan flagged that compression might not exist in this server's traffic
and, if so, the test should stay red rather than be weakened.
**It exists**: `FindCompressedPackets` / `PacketStats_ServerToClient` find
**1017 compressed packets** (0x5a marker) out of 1682 S->C packets once
direction classification covers the full zone session. No assertion was
weakened; both tests pass on real, non-vacuous data.

### Before/after table — all 12 named tests

| Test | Before | After |
|---|---|---|
| `PcapExtractorTest.PacketStats_ServerToClient` | FAIL | PASS |
| `PcapExtractorTest.FindCompressedPackets` | FAIL | PASS |
| `PcapStepThroughTest.StepThroughZoneServerSession` | FAIL | PASS |
| `PcapStepThroughTest.WriteDetailedLogToFile` | FAIL | PASS |
| `DaybreakConnectionPcapTest.ReplayZoneServerPackets` | SKIP | PASS |
| `DaybreakConnectionPcapTest.DetailedFragmentTracking` | SKIP | PASS |
| `DaybreakConnectionPcapTest.SingleCompressedFragmentDebug` | SKIP | PASS |
| `DaybreakConnectionPcapTest.ReplayWithCRCBypassed` | SKIP | PASS |
| `PcapDaybreakHarnessTest.ProcessZoneServerPackets` | SKIP | PASS |
| `PcapDaybreakHarnessTest.ProcessFirstFewPackets_Verbose` | SKIP | PASS |
| `PcapDaybreakHarnessTest.AnalyzeCompressedFragments` | SKIP | PASS |
| `PcapStepThroughTest.StepThroughFragmentSequence` | SKIP | PASS |

All 12 came back — no partial. (`PcapExtractorTest.FilterByZoneServerPort`,
not in the named 12, also went from a vacuous pass to a genuine pass with
1178 packets, see above.)

### Acceptance criteria

1. **PASS** — no hardcoded `7000` remains as a zone-port assumption in
   `tests/`. Verified with
   `grep -rn "7000" tests/pcap_test_utils.h tests/test_daybreak_harness.cpp
   tests/test_daybreak_connection_pcap.cpp tests/test_pcap_extractor.cpp
   tests/test_pcap_step_through.cpp` → no matches.
   `tests/test_formatted_message.cpp:75` (`"...000070000000..."` hex item
   link string) left untouched, per the plan's carve-out.
2. **PASS** — GLES2-config build clean:
   `cmake --build /mnt/projects/fable/wt/t1/build -j24` full build and the
   four affected test targets both built with no errors/warnings from the
   changed files. Bootstrap/config were already in place in this slot
   (`third_party/irrlicht-gles2-x86/lib/libIrrlicht.a` present,
   `EQT_GLES2_IRRLICHT_DIR` already absolute in `build/CMakeCache.txt`) —
   no reconfigure needed.
3. **PASS** — `data/captures` symlink already present in the slot
   (`/mnt/projects/fable/wt/t1/data/captures -> /mnt/projects/fable/captures`),
   confirmed before starting.
4. **PASS** — all 4 FAILs fixed, all 8 SKIPs now run and pass. See table
   above.
5. **PASS** — 3 consecutive full `ctest --test-dir build --output-on-failure`
   runs with `WILLEQ_PCAP=/mnt/projects/fable/captures/session.pcap`:
   - Run 1: 1360/1362 passed. Failures:
     `AudioManagerTest.PreloadCommonSounds` (documented),
     `RDPServerFrameTest.UpdateFrameMultipleTimes` (documented,
     intermittent — SEGFAULT this run).
   - Run 2: 1361/1362 passed. Failure: `AudioManagerTest.PreloadCommonSounds`
     (documented). RDP test passed this run (matches "intermittent").
   - Run 3: 1361/1362 passed. Same as run 2.
   - All 21 pcap/daybreak-family test cases (the 12 fixed plus the 9
     already-passing ones) passed in all 3 runs — no flakiness introduced.
   - No other regressions in any of the three runs.
6. **PASS** — no-capture path still skips cleanly. Demonstrated by pointing
   `WILLEQ_PCAP=/tmp/nonexistent_capture.pcap` at all four binaries: all
   exit 0, all pcap-dependent tests report `SKIPPED` (10+4+4+3 = 21
   skips), zero crashes. `discoverPorts()` on the missing file returns
   `valid=false` with the well-known-only seed set, as designed.
7. **PASS** — `bash scripts/refactor/check_file_sizes.sh fable` exit 0, no
   output (no FAILs). `pcap_test_utils.h` grew by ~250 lines but is a
   header full of small inline functions, well under any size threshold
   the script flags (confirmed by exit 0/empty output rather than
   asserting a specific line count, since the script itself is the source
   of truth here).

Visual gate and ARM build: **not applicable** — tests-only diff, per the
plan and the Testing Charter ("Graphics bar steps ... only when a unit
touches something they cover").

### Deviations from the plan

- **IP resolution for `createTestConnection`**: the plan offered a choice
  between full IP autodiscovery and a `WILLEQ_ZONE_IP`-style override.
  Implemented **both**: `getTestZoneIp()` defaults to the discovered
  `OP_SessionResponse` source IP (true discovery, the plan's preferred
  option) but still honors `WILLEQ_ZONE_IP` as an override, mirroring
  `getTestZonePort()`'s `WILLEQ_ZONE_PORT` pattern for symmetry. This is
  additive, not a deviation from intent — flagging it since the plan
  treated the two as alternatives rather than "do both."
- **`FilterByZoneServerPort` strengthened**: not explicitly requested by
  the plan (it named the file/line for the hardcoded literal but didn't
  call out this specific test's vacuous-pass problem), but leaving an
  `EXPECT_EQ` loop that iterates zero elements felt like exactly the kind
  of "test that asserts nothing" the plan explicitly said it did not want
  to see left standing. Added one `EXPECT_GT` line; flagging it as a
  scope judgment call rather than doing it silently.
- Nothing was STOPped. No `src/`/`include/` change was needed, the
  fixture was not found to be wrong, and no assertion needed weakening —
  the compression finding came back positive, so the "honest red" fallback
  path in the plan was not exercised.

### What I could not make pass

Nothing. All 12 named tests pass, all acceptance criteria pass, 3
consecutive green full-suite runs achieved with only the two
charter-documented host exceptions.
