# T378 — the 70 zone-stream direct senders go through PacketSender (B145)

Class A (ten are the B140 crash shape), slot1, no server. Read the B145
board text (T376's census, per file) and T376's §Review — especially
surprise 1: `SafeQueueZone` refuses a zero-stats connection, so every test
harness that observes BELOW it needs `conn->m_stats.sent_packets = 1`
(the line `PoseHarness`, `DeathHarness`, `CombineHarness` carry), while
`PacketSender::Queue` has no such guard.

1. Re-take the census on your tip (comment-filtered grep for
   `->QueuePacket(` under `src/client`, `include/client`, excluding
   `packet_sender.cpp` and the K13 `m_eq->QueuePacket` forwarder). The
   number must match T376's 70 or the difference is explained.
2. Route them family by family, ONE COMMIT PER FAMILY (player, zone
   handshake, merchant, gm, group, chat, movement, core): each direct
   `ctx_.zone_connection->QueuePacket(p, a, b)` becomes
   `ctx_.net.SafeQueueZone(p, a, b)` with the same arguments; delete the
   now-redundant `if (ctx_.zone_connection …)` wrappers ONLY where the
   wrapper guards nothing else (read each one). The ten crash-shape sites
   first.
3. Tests: (a) a table-driven test that every routed sender, called with
   no zone connection, returns without crashing (T376's
   `ClickSendersPacketSender.NoZoneConnection_*` shape — one case per
   family, listing its senders); (b) for each family, one recorded-session
   assertion that its most user-visible packet appears in the `.sleq`
   (pre-fix failing: 0 records). Every existing harness that goes red on
   the stats guard gets the one-line arm, no assertion weakened.
4. After the last family: the census returns ZERO outside `PacketSender`
   and the login/world TU (B146, separate). Add the census as a
   source-reader pin (`tests/test_direct_sender_census.cpp`) so a new
   direct sender fails ctest.
5. Order goldens for every TU touched (G01 zone, G07 self-death, G08
   OP_Save, the bridge intent table, movement G06): run them all; a
   statement-text golden learns a reviewed edit IN PLACE, both faults
   re-planted. `SendZoneChange` in `everquest.cpp` is on the zoning path —
   the zone-in tests must stay green.
6. Strike B145.

## Proof

Build, `ctest -j6`, the tests with pre-fix/post-fix runs per family, all
goldens, file sizes. No graphics change.

STOP: a sender's arguments do not map onto `SafeQueueZone`'s signature
(report it; route the rest).
