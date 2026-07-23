# R03 — Register orphaned pcap protocol tests; refresh replay documentation

The Daybreak protocol-level pcap tests (tests/pcap_test_utils.h,
test_pcap_extractor.cpp, daybreak_test_harness.h, test_daybreak_harness.cpp,
test_pcap_step_through.cpp, test_pcap_replay.cpp,
test_daybreak_connection_pcap.cpp) exist but were never added to
tests/CMakeLists.txt, and they hardcode a /tmp pcap path that no longer
exists. Wire them in so they build (no rot) and skip cleanly without a
capture; refresh the stale plan doc.

## Steps

1. **Survey the orphaned sources**: they predate current tree state — fix
   compile breaks minimally (API drift fixes only, no redesign). If one is
   fundamentally obsolete/duplicative, deleting it is allowed — justify in
   the review.
2. **Capture path**: replace hardcoded `/tmp/willeq_audit_capture2.pcap`
   with `WILLEQ_PCAP` env var, default `data/captures/session.pcap`
   (relative to repo/cwd like other asset tests). Every test that needs the
   file does `GTEST_SKIP() << "no capture at ..."` when absent — remember
   the GTEST_SKIP-in-helper gotcha F00 fixed in test_ui_sounds.cpp: skip
   from the TEST body, not from a helper.
3. **Register targets** in tests/CMakeLists.txt with minimal source lists
   (follow the file's existing patterns; note the WilleqDatabase link
   lesson from F00 if net sources pull it in).
4. **Docs**: replace docs/plans/pcap_replay_test_plan.md content with a
   short header marking it superseded, pointing to a NEW
   `docs/plans/session_replay_oracle.md` you write covering: the two-layer
   design (protocol-level pcap tests vs app-level session-log oracle,
   session_log.h format), what exists after Batch R, and EXACT recording
   instructions for Will: run willeq with `--record-session
   data/captures/session.sleq` during a short play session (log in, zone,
   move, kill something, camp), plus the optional tcpdump command for a raw
   pcap (`tcpdump -i <iface> -w data/captures/session.pcap udp and host
   <server>`), and where the golden file appears on first replay-test run.

## Verification

- `cmake --build build -j24` clean — all newly registered targets compile.
- `ctest --test-dir build` — green; the new pcap tests all SKIP (no capture
  on this host) rather than fail.
- Review documents: per-file disposition (registered/fixed/deleted+why),
  skip behavior confirmation, doc contents.

## Review

### Per-file disposition

| File | Disposition | Notes |
|---|---|---|
| `tests/pcap_test_utils.h` | Fixed (minimal) | Header-only `.pcap` reader, no production-code deps. Added `getTestPcapPath()` (WILLEQ_PCAP env var, else `data/captures/session.pcap`) so every test file resolves the capture path the same way. No other API drift. |
| `tests/test_pcap_extractor.cpp` | Fixed (minimal) | Only change: path constant now calls `getTestPcapPath()`. Kept the existing fixture-`SetUp()` skip pattern — `GTEST_SKIP()` in `SetUp()` is an officially-supported skip location (gtest docs: "aborts current function"; the framework checks the skip flag after `SetUp()` runs), unlike calling it from an arbitrary helper member function invoked *from* the test body (the F00 gotcha in `test_ui_sounds.cpp`, which does not propagate). 10/10 tests registered, all SKIP without a capture. |
| `tests/daybreak_test_harness.h` | Registered, unchanged | Self-contained decode-pipeline reimplementation (CRC strip, zlib inflate, fragment reassembly, combined/app-combined unpacking). No dependency on production `DaybreakConnection` or any drifted API — compiled as-is. |
| `tests/test_daybreak_harness.cpp` | Fixed (minimal) | Path constant switched to `getTestPcapPath()`; skip messages now include the resolved path. The 4 `DaybreakHarnessTest.*` synthetic tests (`SessionSetup`, `DecompressSimplePacket`, `CombinedPacket`, `FragmentAssembly_Uncompressed`) need no capture and PASS unconditionally — these are the tests that actually regression-guard the compressed-fragment bug this whole plan was originally written to chase. The 3 `PcapDaybreakHarnessTest.*` tests already skipped correctly from the TEST body (via a bool-returning `hasPcapData()` helper, not a skip-issuing one) — no structural change needed. |
| `tests/test_pcap_step_through.cpp` | Fixed (minimal) | Same path-constant fix; skip pattern was already correct (bool-returning helper, `GTEST_SKIP()` called directly in each `TEST_F` body). 4/4 tests SKIP without a capture. |
| `tests/test_daybreak_connection_pcap.cpp` | Fixed (API drift) | This is the one file with real drift against current `daybreak_connection.h`: `DaybreakConnectionManager` no longer has a default constructor (it now requires `(const DaybreakConnectionManagerOptions&, std::unique_ptr<UdpTransport>)` or a transport-only overload). Fixed `createTestManager()` to build a real `PosixUdpTransport` (added `#include "common/net/posix_udp_transport.h"`) and pass it through — the transport is never `open()`ed/`Tick()`ed in these tests (they drive `ProcessPacket()` directly), so no socket is ever bound. Everything else (constructor signature, `m_streams[].fragment_total_bytes`/`fragment_current_bytes`, `m_self` as `weak_ptr`, `m_connections` map shape, `StaticPacket`, opcodes/enums) matched the current header exactly — no other changes needed. Also updated path constant + skip messages. This is the highest-value file in the set: it drives the actual production `DaybreakConnection::ProcessPacket()`, not a reimplementation. 4/4 tests SKIP without a capture. |
| `tests/test_pcap_replay.cpp` | **Deleted** | Duplicative and stale: (1) it reimplements the same `.pcap`-parsing logic already in `pcap_test_utils.h`, with no code reuse between them; (2) its assertions are hardcoded to one historical capture's exact bytes (a specific `spawn_id=550` on port 7007, and a literal byte pattern `00035a78013591bd...` from a since-fixed compressed-fragment bug) rather than general protocol correctness, so it can never usefully pass again even with a fresh capture; (3) it `#include`s `daybreak_connection.h` but never actually exercises `DaybreakConnection` — the include was dead weight; (4) none of its 5 `TEST_F`s had any skip guard at all, so registering it as-is would have made it FAIL (not skip) on a capture-less host, violating the verification bar. Fixing it "minimally" would have meant either gutting the assertions (leaving nothing of testing value) or leaving hardcoded-and-unverifiable checks in place — not worth carrying forward. `tests/test_daybreak_connection_pcap.cpp` already covers the "real `DaybreakConnection` against a capture" use case this file was also reaching for, correctly and generally. |

### Skip-behavior confirmation

Fresh `cmake --build build -j24` (from a from-scratch reconfigure) is clean
across the whole project, including the 4 newly-registered pcap targets
(`test_pcap_extractor`, `test_daybreak_harness`, `test_pcap_step_through`,
`test_daybreak_connection_pcap`).

`ctest --test-dir build`: 1217 tests total, 1216 passed/skipped, 1 failed
(`AudioManagerTest.PreloadCommonSounds` — pre-existing, unrelated: fails
because this host has no ALSA playback device
(`snd_pcm_open_noupdate` errors in the test's own output), not because of
anything touched in this unit; verified by running it in isolation).

All 21 capture-dependent pcap/daybreak tests report SKIPPED, none FAILED:
`PcapExtractorTest.*` (10), `PcapDaybreakHarnessTest.*` (3),
`PcapStepThroughTest.*` (4), `DaybreakConnectionPcapTest.*` (4). The 4
synthetic `DaybreakHarnessTest.*` tests (no capture needed) PASS
unconditionally, confirmed by direct binary run.

### Docs

- `docs/plans/pcap_replay_test_plan.md` replaced with a short superseded-header
  pointing at the new doc, plus a status summary of what changed and why
  (bug fixed, files registered, `test_pcap_replay.cpp` deleted+why),
  original phase log dropped (recoverable via git history).
- `docs/plans/session_replay_oracle.md` (new): describes the two-layer
  design (Layer 1 = protocol-level pcap replay against `DaybreakConnection`,
  covered by this unit; Layer 2 = app-level session-log replay against
  `EverQuest` game state, covered by R01/R02), a per-file table of what
  Layer 1 covers, the `WILLEQ_PCAP` / `data/captures/session.pcap`
  resolution rule and the GTEST_SKIP-from-body rule, what exists after
  Batch R, and exact recording instructions for Will: primary
  `--record-session data/captures/session.sleq` flow (login → zone → move
  → kill → camp) plus the golden-snapshot-created-on-first-run behavior,
  and the optional `tcpdump -i <iface> -w data/captures/session.pcap udp
  and host <server>` command for a raw pcap if a transport-layer bug needs
  chasing.
- `.gitignore`: added `data/captures/` (was not previously ignored;
  R02's doc already assumed it was — this unit adds the actual entry).

### Deviations from the plan

- Deleted `tests/test_pcap_replay.cpp` instead of fixing it (justified above
  — plan explicitly permits this).
- `tests/test_pcap_extractor.cpp` keeps its `SetUp()`-based skip rather than
  moving the check into every `TEST_F` body; this is intentional (see
  disposition table) — `SetUp()` is a distinct, gtest-supported skip
  location, not the anti-pattern the plan's F00 callout warns against.
