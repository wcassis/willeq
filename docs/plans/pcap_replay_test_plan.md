# PCAP Replay Test Plan — SUPERSEDED

This plan (protocol-level pcap replay testing for a single hard-to-diagnose
compressed-fragment bug) is superseded by
[`docs/plans/session_replay_oracle.md`](session_replay_oracle.md).

Status as of Batch R (session replay oracle):

- The compressed-fragment bug this plan was written to chase is fixed —
  `DaybreakConnection` correctly decompresses fragment payloads before
  extracting sequence/total-size, and `tests/daybreak_test_harness.h`'s
  synthetic unit tests (`DaybreakHarnessTest.*`) cover that logic without
  needing a capture.
- The Phase 1-3 artifacts this plan describes (`tests/pcap_test_utils.h`,
  `tests/test_pcap_extractor.cpp`, `tests/daybreak_test_harness.h`,
  `tests/test_daybreak_harness.cpp`, `tests/test_pcap_step_through.cpp`,
  `tests/test_daybreak_connection_pcap.cpp`) still exist and now build and
  register in CMake (they were orphaned — never added to
  `tests/CMakeLists.txt` — until R03). They no longer reference the
  original `/tmp/willeq_audit_capture2.pcap` path; see the new doc for the
  current capture path and how to record one.
- `tests/test_pcap_replay.cpp` (a fifth, duplicative file that re-implemented
  pcap parsing already covered by `pcap_test_utils.h` and hardcoded
  assertions tied to one historical capture's byte contents) was deleted in
  R03 rather than fixed; see that unit's review notes for the full
  justification.
- The broader goal — a regression oracle that would have caught this class
  of bug automatically — is now `docs/plans/session_replay_oracle.md`'s
  session-log replay design (Batch R), which operates one layer higher
  (decoded application packets) than this plan's raw-pcap protocol
  debugging approach.

The original plan content below this point is retained for historical
reference only; do not extend it further. New work belongs in
`docs/plans/session_replay_oracle.md`.

---

*(Original Phase 1-3 implementation log removed from this superseded copy
— see git history for `docs/plans/pcap_replay_test_plan.md` prior to Batch R
if the original text is needed.)*
