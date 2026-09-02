# T323 — Delete the CRC skip flag and prove a live session survives it

**Slot: slot2. Branch `task/T323` cut from `fable` @ `c243742d`. x86 only. No Pi.
This unit owns eqemu.**

Follow-on to `refactor/T321_crc_root_cause.md`, which did all the diagnosis.
Read its Review first; this unit is the remaining one-line change plus live
acceptance.

## What T321 established (do not re-derive)

- **There is no CRC bug.** All 73,356 CRC-bearing datagrams in a real captured
  session — login, world and zone, both directions, eight sessions — pass
  `ValidateCRC()` with validation ENABLED. Zero failures.
- Only the zone manager sets `skip_crc_validation`; login and world have always
  validated. The only handshake difference is compression, which is applied
  inside the CRC on send and stripped after validation on receive.
- The flag is a stale workaround for `OP_SessionResponse` byte-order bugs that
  T21 fixed on 2026-07-31. Nothing retested it in the six months since.
- ⚠ A real CRC mismatch **DROPS A DATAGRAM — it does not disconnect.** A dropped
  reliable frame is re-sent on ack timeout. The failure mode to watch for is
  silent packet loss, not a visible disconnect.

## Scope

1. Delete `zone_opts.skip_crc_validation = true;` from `EverQuest::ConnectToZone()`
   (`src/client/eq.cpp`; re-located, not taken from T321's stale line number).
   The `zone_opts` local and the `(zone_opts)` ctor argument stay.
2. Take a live session against eqemu: connect, zone in, move, zone again, camp.
   Grep the log for `DROPPED_CRC`.
3. Keep T321's `tests/test_daybreak_crc_capture.cpp` passing. If deleting the
   flag makes one of its 7 tests meaningless, **say so in the Review** rather
   than quietly deleting it.

## Acceptance

- A live zone session that connects, zones and stays connected, with the log
  checked for CRC drops.
- **The Review states plainly that a short session does not prove the absence of
  a rare mismatch.**
- `tests/test_daybreak_crc_capture.cpp` (7 tests, 212 KB of recorded bytes)
  still passes.
- Full `ctest -j6` (**NOT -j24**), zero new failures vs
  `refactor/test_baseline.txt`; `check_file_sizes.sh --files` no new FAILs.

## Territory

`src/client/eq.cpp` (the one line), `tests/test_daybreak_crc_capture.cpp`
(comment/framing only), this plan file. Concurrent: slot3 (ARM build, graphics).

## Live-session traps (recorded this week)

- Do NOT run `up.sh` on top of an already-running fleet — a second loginserver
  cannot bind and the client then reports "could not find world server WillEQ".
- `#gm on` is required before `#kill`/`#damage`/`#npcloot` and fails silently.
- Use `/say #zone <zone>`, never a DB `zone_id` edit.
- ⚠ `Oraclerogue`'s persisted `gm` flag has been found set to 1 by three
  consecutive units (T297, T320, T322). Check it a fourth time and report.
- Restore any DB state changed; diff before/after.

---

# Review (slot2, `task/T323`, cut from `fable` @ `c243742d`, 2026-09-02)

## Verdict

**The flag is deleted and two live sessions held with zone-stream CRC
validation enabled.** 3,592 datagrams measured through `ValidateCRC()` across
six zone-ins, **zero drops**. T321's diagnosis is confirmed in the runtime it
could not reach.

## The change

One line, `EverQuest::ConnectToZone()`, `src/client/eq.cpp:718` (re-located,
not taken from T321's line number — J02's split moved it by one):

```
-	zone_opts.skip_crc_validation = true;  // TODO: Remove after CRC bug is fixed
```

The `DaybreakConnectionManagerOptions zone_opts` local and the `(zone_opts)`
ctor argument stay, per T321's recommendation — they are harmless and other
options may want them. `skip_crc_validation` now has **no production caller**;
it defaults to `false` (`daybreak_connection.h:293`) on all three managers.

## Live acceptance

Media and full detail: `/mnt/projects/fable/review/T323/` (see its `README.md`).
eqemu was down and unlocked on arrival; lock taken atomically, one loginserver
and one world confirmed via `status.sh` before any client ran, `down.sh` clean,
lock released.

Two headless sessions of the T322 shape, single client (Testchar):

| run | log | shape |
|---|---|---|
| 1 (acceptance) | `t323_live_testchar.log` (INFO) | login -> qeynos2 -> befallen -> `/move` x2 -> qeynos -> `/move` -> `/camp`. Zone servers 7000/7001/7024, world reconnected twice, ~4.5 min, exit 0. |
| 2 (measurement) | `t323_live_nettrace.log.gz` (INFO + `--log-module=NET:TRACE`) | same shape, shorter. Every datagram logged at each stage. |

Run 2's counters (measured, not argued):

| counter | value |
|---|---|
| `RECV[` | **3,602** |
| `DECODED[` / `DECODED_NOCMP[` / `DECODED_RAW[` | 3,240 / 352 / 10 |
| **passed `ValidateCRC()`** | **3,592** (`DECODED_RAW` is the `!PacketCanBeEncoded` branch, which never reaches it) |
| **`DROPPED_CRC`** | **0** |
| `DROPPED` (any reason) | **0** |

`RECV == DECODED` exactly, so nothing was dropped at any stage and no datagram
took the `OP_KeepAlive`/`OP_OutboundPing` early-out. Run 1 likewise: zero
`DROPPED_CRC`, zero `Tossed packet`.

Positions confirm the movement actually happened rather than the script merely
issuing commands: befallen `-20.4, -635.5, -21.8` then `-56.5, -603.1, -22.0`;
qeynos `-73.0, 395.5, 3.8`.

## ⚠ What a short session does not prove

**Two sessions totalling ~8 minutes and ~4,000 datagrams do not prove the
absence of a rare mismatch.** T321 said this and it must not get lost. The
honest claim is *no mismatch was observed*, not *no mismatch can occur*.

Two things make that limit less alarming than it sounds:

1. **A real mismatch drops a datagram; it does not disconnect.** A dropped
   reliable frame is re-sent by the server on ack timeout. So the cost of a
   rare mismatch is a round trip, and the benefit is that corrupt bytes are
   never parsed as game state. "The session stayed connected" is therefore
   *weak* evidence on its own — the `DROPPED_CRC == 0` count is the real one.
2. **A future occurrence is self-documenting.** `DROPPED_CRC` logs at WARN with
   a full hex dump (`daybreak_connection.cpp:540`) and additionally raises an
   `on_error_message` ("Tossed packet that failed CRC of type ..."). Nothing
   further needs to be instrumented to catch one.

Still unmeasured, unchanged from T321: **the encode key's byte order**. eqemu
only ever issues `0x00000000` or `0xFFFFFFFF`, both byte-swap palindromes, so
`NetworkToHost` at `:943` is argued correct by symmetry with the send path and
by the fact that eqemu validates fable's outbound CRCs unconditionally — but it
is not measured, here or in T321, and eqemu cannot produce a capture that
would.

## What happened to T321's tests

**All 7 pass, none deleted, none became meaningless.** Six were never about the
flag at all — they pin the algorithm, the negotiated parameters, and that
validation discriminates. Only one needed re-framing:

| test | status after the flip |
|---|---|
| `RecordedSessionResponsesNegotiateTwoCrcBytes` | unchanged, unaffected |
| `OnlyTheZoneStreamNegotiatesCompression` | unchanged, unaffected |
| `EveryCapturedZonePacketPassesCrcValidation` | unchanged — and now pins exactly what production does |
| `EveryCapturedLoginAndWorldPacketPassesCrcValidation` | unchanged |
| `CorruptedAndTruncatedZonePacketsAreRejected` | unchanged — the discrimination proof |
| `SkipCrcValidationAcceptsACorruptedZonePacket` | **re-framed, kept.** It was "the live defect, pinned". It is now a guard on the option's remaining semantics: the option still exists and still controls `ValidateCRC`'s early-out, so the test is a real assertion about `daybreak_connection.cpp` and it documents what re-enabling the flag would cost. Deleting it would remove the only coverage of that early-out branch. |
| `DaybreakCrcAlgorithm.RecordedZoneFrameCrcIsReproducible` | unchanged |

Comment-only edits: the file header of `tests/test_daybreak_crc_capture.cpp`,
the block above `SkipCrcValidationAcceptsACorruptedZonePacket`, its inline
`opts_` comment, and the corresponding block in `tests/cmake/network.cmake` —
all four said "which src/client/eq.cpp sets on every zone connect", which is no
longer true. No test logic changed.

## Server state

`character_data` snapshotted before and after; `diff db_before.txt
db_after_restore.txt` is **empty**. Only Testchar's pose moved during the runs,
restored to **as found** (`zone_id=2, x=-74, y=428, z=3.751, h=160`) rather
than the protocol's written default of zone 36 — per that protocol's own
warning to SELECT the row first.

**⚠ `Oraclerogue` (id 81) had `gm = 1` on arrival.** `db_before.txt` was taken
before this unit started any client, and this unit never ran `#gm` or touched
that row. That is the **fourth** consecutive unit to find it set — T297, T320,
T322, and now T323 — none of which could have set it. Something unidentified
is writing it; it is no longer plausible as a leftover.

## Deviation

One self-inflicted false start, recorded so it is not re-learned: after the
comment-only test edits I launched `ctest` believing the incremental rebuild
had finished, because I read a `build2.exit` that **did not exist** — the
`cat` was chained after another command and its stderr was lost in the
output. The rebuild was in fact still inside `cmake --check-build-system`,
regenerating all 174 `*_include.cmake` discovery shims, and `ctest` read them
mid-write. The result was a **"Maximum recursion depth of 1000 exceeded"**
CMake error at discovery — exit 8, in 60 s, naming an innocent test
(`test_hunt_target_body_type`). This is a **third** distinct cause of a
`ctest` exit-8 discovery failure, alongside the two the protocol already
records (T293 truncated binaries; J05/J07 cold-cache timeout): **a
reconfigure running concurrently with ctest.** No binary was damaged and
nothing needed rebuilding — waiting for the real `build2.exit` (EXIT=0) and
re-running was sufficient. The protocol's "classify by exit status" rule held;
the failure was in trusting an exit file I had not confirmed existed, which is
the same class as J20's zero-second `wait_for.sh` MET.

## Verification

| step | result |
|---|---|
| `cmake --build build -j24` (GLES2 tree) | **PASS**, EXIT=0 (twice: before the test-comment edits and after) |
| `ctest --test-dir build -j6 --output-on-failure` | **3506 / 3507**. Sole failure `AudioManagerTest.PreloadCommonSounds` — the documented ALSA host quirk, and the only entry in `refactor/test_baseline.txt`. **Zero new failures.** All 7 `DaybreakCrcCapture*` tests Passed. |
| `check_file_sizes.sh --files` on the 4 changed files | **4 ok, 0 warn, 0 fail** (`eq.cpp` 1025 lines) |
| live zone session | **PASS** — two sessions, six zone-ins, 3,592 validated datagrams, 0 `DROPPED_CRC` |
| DB restore | **PASS** — before/after diff empty |
| eqemu lifecycle | lock taken atomically, one loginserver + one world confirmed, `down.sh` EXIT=0, lock deleted |
| visual gate / ARM / `build-gles2-x86.sh` | **not run — narrowed by the plan's Acceptance section.** The production diff is one deleted line in a network-options struct; nothing touches rendering or ARM-specific code. |
