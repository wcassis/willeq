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

(executor fills in: session script/coverage of the recording, capture
sizes, skip→run table, any findings, deviations)
