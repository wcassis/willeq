# Batch R — Session Recording & Replay (game-state regression oracle)

**STATUS: COMPLETE (2026-07-23).** Part A landed green (suite now 1,224
tests). Part B awaits a real recording from Will — see
docs/plans/session_replay_oracle.md for the exact steps; the golden
snapshot self-creates on the first replay-test run against it.

Goal: a deterministic regression oracle for the game-state layer — record a
real play session's decoded application packets, replay them into a headless
EverQuest instance, and assert the resulting game state against a golden
snapshot. Supersedes the narrow protocol-debugging scope of
docs/plans/pcap_replay_test_plan.md (whose Phase 1–3 artifacts exist in
tests/ but were never registered in CMake, and whose /tmp capture is gone).

Shared format: `include/common/net/session_log.h` (authored by orchestrator;
records post-Daybreak app packets — direction, stream, opcode, payload — so
replay is independent of protocol-level compression/fragmentation).

Constraint discovered during planning: NO capture file exists anywhere on
this host. Part A (below) is fully autonomous and lands green using a
synthetic session; Part B needs Will to record a real session (instructions
in R03's doc update) — the golden-snapshot test then activates on the real
recording.

| Unit | Scope | Status | Slot | Commit |
|---|---|---|---|---|
| R01 | Session recorder: `--record-session <file>`; hooks at all three S→C streams + zone C→S; 5 unit tests | done | slot1 | `6f0304a` |
| R02 | Replay harness: ReplayZonePacket seam, 11 null-connection send guards, synthetic-session test PASSING, golden mode armed | done | slot2 | `0fd3b25` |
| R03 | Pcap protocol tests registered (21 skip cleanly, 4 synthetic pass; 1 unfixable file deleted); docs/plans/session_replay_oracle.md written | done | slot3 | `0fcb41f` |
