# Batch R — Session Recording & Replay (game-state regression oracle)

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
| R01 | Session recorder in the client: `--record-session <file>` CLI flag; write SessionLog records for decoded app packets (both directions, all three streams) at the EverQuest packet boundary | | | |
| R02 | Replay harness + test: headless EverQuest, public replay seam, feed S→C records into the zone dispatcher, snapshot-assert game state; ships with a synthetic session so the test is green NOW; auto-activates golden mode when a real recording exists | | | |
| R03 | Register the orphaned pcap protocol tests in CMake with skip-if-no-capture guards; replace docs/plans/pcap_replay_test_plan.md with a current doc (oracle design + exact recording instructions for Will) | | | |
