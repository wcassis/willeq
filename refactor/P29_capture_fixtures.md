# P29 — Record the missing capture fixtures (session.sleq + pcap)

**Slot: slot3. x86 only — no Pi. OWNS EQEMU — honor the .eqemu-lock
protocol in EXECUTOR_PROTOCOL.md (check/create
/mnt/projects/fable/.eqemu-lock before up.sh, delete after clean
shutdown).** Siblings: P27 (slot1), P28 (slot2) — no file overlap; you
change NO source code.

Executes TESTING_HANDBACK.md item 2 (read it first). `data/captures/`
does not exist, so ~50 pcap/session-replay tests skip and
`daybreak_connection.cpp` (1195 lines, the reliable-UDP core) has 0%
practical coverage. One recorded live session fixes it.

## Work

1. Storage layout: captures are UNTRACKED (binary fixtures; confirm
   .gitignore treatment). Canonical home: create
   `/mnt/projects/fable/captures/` (NFS) and symlink
   `data/captures -> /mnt/projects/fable/captures` in the canonical
   checkout `/mnt/projects/fable/main` AND your slot3 worktree (same
   pattern as the untracked `data/eq_client` symlink). Document the
   layout + env overrides (WILLEQ_PCAP, WILLEQ_SESSION_LOG) in a
   `README.md` INSIDE /mnt/projects/fable/captures/.
2. Recording: bring up eqemu (lock first). One session covering
   login → world → zone-in (befallen) → movement → chat → a second
   zone if stable (qeynos2) → clean exit, run with
   `--record-session data/captures/session.sleq`. Simultaneously
   capture the same session's UDP traffic as `session.pcap` (tcpdump
   on the loopback/docker-bridge interface serving the eqemu ports —
   check what the repo's docs/tests expect via tests/pcap_test_utils.h
   and docs/; if packet capture needs privileges this host won't
   grant, deliver session.sleq anyway and STOP on the pcap half with
   the exact failure).
3. Activate + verify: run the full pcap/session-replay test family
   (test_daybreak_connection_pcap, test_daybreak_harness,
   test_pcap_extractor, test_pcap_step_through, test_session_replay)
   against the new fixtures. test_session_replay self-creates
   `session.golden` on first run — run it twice to prove
   record+replay determinism. Report exactly which tests went
   skip→run and their results. If a test FAILS on the real capture,
   that is a FINDING (the fixture just found a bug or a stale
   assumption) — report it, do not massage the capture until it
   passes.
4. Full-suite spot: desktop-tree ctest — total skips should drop by
   the activated family; nothing previously passing may regress.

## Rules

- NO source-code changes at all. If activation reveals a test that
  needs a code fix, report it as a finding.
- eqemu per protocol (lock, sanctioned start, restore Testchar to
  zone_id=36, x=-77.9232, y=21.9086, z=3, h=190, clean shutdown,
  remove lock).
- The commit contains only: the captures README is NOT in the repo
  (it lives on NFS); your plan's Review section IS. If data/captures
  needs a .gitignore entry to stay clean, add it and say so.

## Verification

- The skip→run table with results; determinism double-run of
  session_replay; desktop ctest summary before/after fixture
  placement; pristine server exit + lock removed.

## Review

### Storage layout

- Created `/mnt/projects/fable/captures/` (NFS), with `README.md` inside
  documenting the file table, env overrides, and recording recipe.
- Symlinked `data/captures -> /mnt/projects/fable/captures` in both
  `/mnt/projects/fable/main` and this worktree (`wt/slot3`).
- **Deviation from plan (sanctioned by the plan's own text):** the
  existing `.gitignore` rule was `data/captures/` (trailing slash), which
  only matches a real directory — it does **not** match a symlink named
  `data/captures`, so `git status` showed the new symlink as untracked.
  Changed the rule to `data/captures` (no trailing slash, matching the
  existing `data/eq_client` convention on the line above it) —
  `tests/pcap_test_utils.h`/`docs/plans/session_replay_oracle.md` say
  `data/captures/` is gitignored, but the pattern as written didn't
  actually achieve that once it's a symlink. Verified clean afterward
  (`git status --porcelain` shows only the `.gitignore` line changed).

### Recording

One live session against akk-stack eqemu (lock held throughout, see
below), account `willeq_test` / character `Testchar`, via
`./build/bin/willeq --no-graphics --record-session data/captures/session.sleq --script data/captures/tour.txt -c data/captures/tour_willeq.json`,
with `sudo tcpdump -i any -w data/captures/session.pcap "udp port 5998 or udp port 9000 or udp portrange 7000-7030"` running concurrently (headless
`--script` tour, not manual play — see Deviations). Coverage: login ->
world -> zone-in Befallen -> 3 movement waypoints -> chat -> GM
`#zone qeynos2` -> 2 movement waypoints -> chat -> `/camp` (clean logout,
confirmed "Logout confirmed by server" in the run log). Provenance files
(`run_tour.sh`, `tour_willeq.json`, `tour.txt`, `run.log`) are kept next
to the fixtures on NFS; see `/mnt/projects/fable/captures/README.md`.

Capture sizes:
| file | size |
|---|---|
| `session.sleq` | 554,186 bytes (2,943 decoded app-packet records) |
| `session.pcap` | 1,344,914 bytes (5,603 packets: 225 login/5998, 1,286 world/9000, 920 befallen-zone/7012, 3,172 qeynos2-zone/7026) |
| `session.golden` | 10,355 bytes (self-created by `test_session_replay` on first replay) |

### Skip -> run table (pcap/session-replay family, `WILLEQ_PCAP`/`WILLEQ_SESSION_LOG` pointed at the new fixtures)

| test | before | after | result |
|---|---|---|---|
| PcapExtractorTest.ReadPcapFile_Basic | skip | run | PASS |
| PcapExtractorTest.ReadPcapFile_ServerToClientOnly | skip | run | PASS |
| PcapExtractorTest.ReadPcapFile_ClientToServerOnly | skip | run | PASS |
| PcapExtractorTest.PacketStats_ServerToClient | skip | run | **FAIL** (finding) |
| PcapExtractorTest.VerifyPacketStructure | skip | run | PASS |
| PcapExtractorTest.FindFragmentPackets | skip | run | PASS |
| PcapExtractorTest.FindCompressedPackets | skip | run | **FAIL** (finding) |
| PcapExtractorTest.VerifySessionResponse | skip | run | PASS |
| PcapExtractorTest.FilterByZoneServerPort | skip | run | PASS |
| PcapExtractorTest.PacketsAreInTimestampOrder | skip | run | PASS |
| DaybreakHarnessTest.* (4 tests, always-on synthetic) | run | run | PASS (unaffected) |
| PcapDaybreakHarnessTest.ProcessZoneServerPackets | skip | **still skip** | GTEST_SKIP (finding — see below) |
| PcapDaybreakHarnessTest.ProcessFirstFewPackets_Verbose | skip | still skip | GTEST_SKIP (same cause) |
| PcapDaybreakHarnessTest.AnalyzeCompressedFragments | skip | still skip | GTEST_SKIP (same cause) |
| PcapStepThroughTest.StepThroughFirst50Packets | skip | run | PASS |
| PcapStepThroughTest.StepThroughZoneServerSession | skip | run | **FAIL** (finding) |
| PcapStepThroughTest.StepThroughFragmentSequence | skip | still skip | GTEST_SKIP (same cause) |
| PcapStepThroughTest.WriteDetailedLogToFile | skip | run | **FAIL** (finding) |
| DaybreakConnectionPcapTest.ReplayZoneServerPackets | skip | still skip | GTEST_SKIP (same cause) |
| DaybreakConnectionPcapTest.DetailedFragmentTracking | skip | still skip | GTEST_SKIP (same cause) |
| DaybreakConnectionPcapTest.SingleCompressedFragmentDebug | skip | still skip | GTEST_SKIP (same cause) |
| DaybreakConnectionPcapTest.ReplayWithCRCBypassed | skip | still skip | GTEST_SKIP (same cause) |
| SessionReplaySynthetic.DrivesGameStateThroughPublicAccessors | run | run | PASS (unaffected, no capture needed) |
| SessionReplayGolden.MatchesRecordedSessionOrCreatesBaseline | skip (1st run) | run (2nd+) | PASS — see determinism below |

10 activated to PASS, 4 activated to FAIL (real findings below), 8 remain
skipped (same root cause as the 4 failures, just a `GTEST_SKIP` instead
of a hard assert when the filtered packet count is 0) — matches the
14-test swing seen in the desktop-wide skip count (38 -> 24, see below).

### Finding: pcap tests hardcode zone-server port 7000; this environment's zone processes never land there

`tests/pcap_test_utils.h`'s `PcapReadOptions::server_ports` default set
is `{5998, 5999, 7000-7007, 9000}`, and `tests/test_daybreak_harness.cpp`,
`tests/test_daybreak_connection_pcap.cpp`, `tests/test_pcap_step_through.cpp`
hardcode `filter_src_port = 7000` / `pkt.src_port == 7000` directly (no
env override, unlike `WILLEQ_PCAP`). In this akk-stack container, the
image's own entrypoint (`entrypoint.pl` -> `/tmp/spire-launcher
eqemu-server:launcher start`) auto-boots ~15 static zone processes
(butcher, erudnext, freporte, qeynos, oot, iceclad, nro, oasis, nedaria,
abysmal, natimbi, timorous, firiona, overthere, ...) on container start,
**independent of `up.sh`'s own zone-launch step** (confirmed: `ps` inside
the container showed these already running, parented to the
`spire-launcher` process, immediately after `up.sh` finished its own
"starting zone befallen" step). World's per-zone port allocator hands out
ports in order, so that fleet claims 7000-7011 before our explicitly
requested zones ever get a port: Befallen landed on 7012, qeynos2 on
7026. Every capture recorded against this akk-stack image will hit the
same thing — the "first zone gets port 7000" assumption baked into up.sh's
own comments does not hold once the container's built-in launcher fleet
is up, which per its entrypoint is always. This is an environment/test
mismatch, not a capture problem — the recording is real and exercises the
zone protocol correctly (3,172 + 920 zone packets across two real zone
UDP streams); the tests just look on the wrong port. Not fixed here per
the plan's "no source changes" rule; flagging for whoever owns
`tests/pcap_test_utils.h` / the three hardcoded-7000 test files to either
add a `WILLEQ_ZONE_PORT` override or discover the port from the pcap
itself (e.g. by scanning for `OP_SessionRequest`/`OP_SessionResponse`
pairs) rather than assuming 7000.

### Determinism double-run (`test_session_replay`, `SessionReplayGolden.MatchesRecordedSessionOrCreatesBaseline`)

1. First run (no `session.golden` present): `[SKIPPED] ... Golden created
   at .../session.golden -- re-run to verify.`
2. Second run: `[ OK ] SessionReplayGolden.MatchesRecordedSessionOrCreatesBaseline (21 ms)` — `[PASSED] 1 test.`
3. Third run (repeat, to rule out first-pass luck): `[ OK ]` again,
   `[PASSED] 1 test.`

Record + replay is deterministic against the new `session.sleq` fixture.

### Desktop ctest, before/after fixture placement

- Before (no env override — matches a plain `ctest` invocation, since
  ctest's working directory for these tests is `build/tests` and the
  fixtures are only reachable via `WILLEQ_PCAP`/`WILLEQ_SESSION_LOG` or a
  `build/tests/data` symlink that doesn't exist — see note below):
  1235 tests, 99% passed, 1 failed (`AudioManagerTest.PreloadCommonSounds`
  — documented host exception), 38 skipped.
- After (`WILLEQ_PCAP`/`WILLEQ_SESSION_LOG` pointed at the new fixtures):
  1235 tests, 99% passed, 5 failed (the same known
  `AudioManagerTest.PreloadCommonSounds` exception + the 4 port-7000
  findings above), **24 skipped** (down from 38 — the 14-test swing
  matches the skip->run table). No previously-passing test regressed.
  `RDPServerFrameTest.UpdateFrameMultipleTimes` (the other documented
  intermittent exception) did not trigger either run.

**Note on activation ergonomics:** `tests/pcap_test_utils.h`'s doc comment
says the default path is "relative to cwd, matching other asset-test
conventions," but ctest actually runs these tests with cwd = `build/tests`
(confirmed via `ctest -V`), and no `build/tests/data` symlink exists in
this tree, so a bare `ctest` run will always skip the family regardless of
whether `data/captures/` is populated at the repo root — only the
`WILLEQ_PCAP`/`WILLEQ_SESSION_LOG` env vars (or a `build/tests/data`
symlink, which is build-directory-local and wouldn't survive a clean
rebuild) actually activate them. Worth a doc fix or a
`WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}` on these `add_test()` calls;
not attempted here (source change).

### Server + lock end state

- Testchar restored to `zone_id=36, x=-77.9232, y=21.9086, z=3, heading=190`
  (verified via `SELECT` after the `UPDATE`).
- `down.sh` run: loginserver/world/zone processes killed, `docker compose
  down` removed all akk-stack containers (mariadb, eqemu-server,
  fail2ban-*) and the `akk-stack_backend` network. `docker ps -a` shows
  nothing running afterward.
- `/mnt/projects/fable/.eqemu-lock` removed.

### Deviations from the plan

1. **Recording method:** used `--no-graphics --script data/captures/tour.txt`
   (a scripted, deterministic tour) rather than manual interactive play.
   This executor has no interactive display; the `--script` mechanism
   (`wait N` + slash commands) is the same mechanism `scripts/eqemu/
   record_tour.sh` (H03) already uses for exactly this purpose. Coverage
   matches the plan's required beats (login/world/zone-in/movement/chat/
   second-zone/clean-exit).
2. **Zone-ID mapping detour:** `character_data.zone_id` is keyed by the
   `zone` table's `zoneidnumber` column, not its `id` (autoincrement PK)
   column — e.g. befallen is `id=19` but `zoneidnumber=36`; qeynos2 is
   `id=303` but `zoneidnumber=2`. First attempt set `zone_id=19` expecting
   Befallen and got zoned into Rivervale instead (`zoneidnumber=19`).
   Corrected before the fixture-quality recording (the two "before"
   attempts were discarded, not shipped as fixtures).
3. **tcpdump interface:** initial capture used `-i lo`, expecting the
   client to connect to `127.0.0.1` for world/zone the way it did for
   login. In fact the world server advertises its own address
   (`10.0.30.23`, the host's real LAN IP) for world/zone connections, and
   while `ip route get 10.0.30.23` shows this is routed via `dev lo`,
   `-i lo` still captured *only* the 5998 login traffic and none of
   world/zone — switched to `-i any` (Linux cooked capture, `LINKTYPE_LINUX_SLL2`,
   which `tests/pcap_test_utils.h` already parses via
   `getLinkHeaderSize()`), which correctly captured all four streams.
4. **`.gitignore` fix** — see Storage layout above; explicitly sanctioned
   by the plan's own text ("If data/captures needs a .gitignore entry to
   stay clean, add it and say so").
5. Two full-session capture attempts before this one were discarded: one
   lost `session.pcap` to an `rm -f` race in an earlier draft of
   `run_tour.sh` (tcpdump had the path open when it was unlinked), one
   zoned into the wrong place (deviation #2). Neither is shipped.

No source code was changed. Only change in the repo: `.gitignore`
(one line) and this Review section.
