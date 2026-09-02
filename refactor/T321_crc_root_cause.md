# T321 — Root-cause the CRC bug: every zone packet is accepted unvalidated

**Slot: slot2. Branch `task/T321` cut from `fable`. x86 only. No Pi.
eqemu is held by slot1 (T320) — this unit is designed to be done WITHOUT it:
T311 Leg A left a 73,506-packet capture. Take a live leg only if the lock
frees.**

Tier 2 item 3 of `refactor/deferred.md`'s class-A list, and **the widest live
correctness hole left in the client**.

## The defect

`src/client/eq.cpp:717` —
`zone_opts.skip_crc_validation = true;  // TODO: Remove after CRC bug is fixed`,
set on **every zone connect**. `ValidateCRC()`
(`src/common/net/daybreak_connection.cpp:1211-1213`) early-returns `true` when
that flag is set, so every corrupted or truncated zone-stream packet is accepted
and parsed as if valid, for the whole session.

**Nothing on the board describes the underlying CRC bug.** That is this unit's
job — the flag flip is trivial and must not be done blind.

## Scope: DIAGNOSE, do not flip

1. **Reproduce the failure from the existing capture, not from a running
   server.** `review/T311/legA/` holds a full session pcap (73,506 packets)
   with login, zone-in, movement, two deaths and respawns. Feed zone-stream
   packets through `ValidateCRC()` with validation ENABLED and find which ones
   fail and why. ⚠ **Titanium traffic sits inside EQStream** — T311 Leg A hit
   exactly this and had to read the zone process's own packet log instead of
   the raw pcap. Read its Review for how it decoded, and reuse that, rather
   than rediscovering it.
2. **Name the mechanism.** The candidates, in the order the code suggests:
   - `m_encode_key` wrong or stale at validation time — it is negotiated in
     `OP_SessionResponse` (`:944-951`) and used as the Crc32 seed (`:1228`,
     `:1232`);
   - `m_crc_bytes` disagreeing between the two sides (`:354`, `:387`, `:911`,
     `:944`) — note it is set from options at one site and from the session
     reply at another;
   - byte-order handling of the trailing CRC (`NetworkToHost` at `:1227`/`:1231`
     for the 2- and 4-byte cases);
   - the length passed to `Crc32` (`p.Length() - m_crc_bytes`) versus what the
     server computed over — an off-by-the-CRC-field error is the classic form;
   - compression/encoding passes applied in a different order than the server's
     (`m_encode_passes`, same `OP_SessionResponse` block).
   **Say which it is, with the bytes that prove it.** If more than one is wrong,
   say so.
3. **Check the world and login streams too.** The flag is set for the ZONE
   manager here; find whether the other connection managers set it, and whether
   their CRC actually passes today. **If world/login validate fine, the
   difference between them and zone is the shortest path to the answer** —
   that comparison is the single highest-value experiment in this unit.
4. **Only then**: if the fix is small and provable, make it and flip the flag.
   If it is not, **leave the flag alone**, write the diagnosis, and board the
   fix as its own unit. A wrong flip turns a silent accept into a silent
   disconnect, which is worse.

## Acceptance

- **A test that validates real captured packets** — pass the recorded bytes
  through `ValidateCRC()` and assert the outcome, so the diagnosis is pinned
  whether or not the flag flips. This is the deliverable even if nothing is
  fixed.
- If you flip the flag: a live zone session that connects, zones, and stays
  connected — **and say plainly that a short session does not prove the absence
  of a rare mismatch.**
- Full `ctest -j6` (**NOT -j24**), zero new failures vs `refactor/test_baseline.txt`.
- `check_file_sizes.sh --files` no new FAILs.

⚠ Known load-dependent flake under concurrent slots:
`HeadlessShutdownTest.NonLifecycleCommandIsHeldNotDroppedOrApplied` — re-run it
alone before reporting it.

## Territory

`src/common/net/`, `include/common/net/`, `src/client/eq.cpp` (the flag line
only), plus your tests. ⚠ `src/client/eq.cpp` was just split by J02 and is now
1,022 lines — **re-locate the flag rather than trusting the line number above.**
Concurrent: slot1 T320 (live group/zone, `eq_chat.cpp`), slot3 J03
(`graphics/entity_renderer.cpp`).
