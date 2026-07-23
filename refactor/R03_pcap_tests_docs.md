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

(executor fills in)
