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

---

# Review (slot2, `task/T321`, cut from `fable` @ `baad09da`, 2026-09-02)

## Verdict

**There is no CRC bug in the current code.** `ValidateCRC()`, `EQ::Crc32()`, the
key negotiation, the CRC byte order and the CRC length are all correct, and
every one of the **73,356** CRC-bearing packets in T311 Leg A's capture — login,
world and zone, both directions — validates. `skip_crc_validation` is a **stale
workaround for handshake bugs that T21 fixed on 2026-07-31**, six months after
the flag was added. It was never retested.

**The flag was NOT flipped.** Reasons in "Why the flag is still set" below.

## How the capture was read (and why it did NOT need EQStream)

T311 Leg A had to fall back to the zone process' packet log because *application*
payloads sit inside EQStream. **That does not apply to CRC work.** The Daybreak
CRC is computed over the raw UDP datagram *before* any decompression, XOR pass,
combining or fragment reassembly — `ProcessPacket()` calls `ValidateCRC(p)` on
the datagram as received (`daybreak_connection.cpp:539`) and only then decodes.
So the raw pcap is directly usable, and no EQStream implementation is needed.

The capture is `LINKTYPE_LINUX_SLL2` (`tcpdump -i any`). A ~60-line pcap/SLL2/
IPv4/UDP reader plus a re-implementation of `ValidateCRC`'s exact expression
(`zlib.crc32` over `LE32(key) || body`, compared against
`ntohs(last two bytes)`) walked all 73,506 datagrams. `-i any` records each
datagram twice (veth + bridge); duplicates were kept for the sweep (they are
identical bytes) and de-duplicated only when building the committed fixture.

## The comparison the plan asked for: world and login vs zone

All three handshakes were decoded from the recorded `OP_SessionResponse`
(`DaybreakConnectReply`, 17 bytes, wire order):

| stream | port | connect_code | **encode_key** | **crc_bytes** | pass1 | pass2 | max_packet |
|---|---|---|---|---|---|---|---|
| login | 5998 | `38b549d2` | `ffffffff` | **2** | 0 (None) | 0 | 512 |
| world | 9000 | `4c1e17fb` | `00000000` | **2** | 0 (None) | 0 | 512 |
| zone  | 7002 | `7d573d8b` | `ffffffff` | **2** | **1 (Compression)** | 0 | 512 |

first zone response, byte for byte:

```
00 02 | 7d 57 3d 8b | ff ff ff ff | 02 | 01 | 00 | 00 00 02 00
zero    connect_code  encode_key   crc  p1   p2   max_packet_size
```

**Login and world validate CRC today and always have.** Neither manager sets
`skip_crc_validation` (`DaybreakConnectionManagerOptions` defaults it to
`false`, `daybreak_connection.h:293`), and for a client-role connection
`m_crc_bytes` is not taken from `options.crc_length` at all — it is
`0` until the server's `OP_SessionResponse` sets it (`:385`, `:944`). The
recorded server asks for 2 on every stream, so login and world have been
running full CRC validation on every packet for the whole life of the client.

**The only structural difference between the zone stream and the other two is
compression** (`encode_pass1 = 1`). Everything else in the three handshakes is
identical. Compression cannot affect CRC: it is applied *inside* the CRC on the
send side (`InternalSend`: encode passes, then `AppendCRC`) and stripped
*after* validation on the receive side (`ProcessPacket`: `ValidateCRC`, then
`temp.Resize(temp.Length() - m_crc_bytes)`, then decompress). The zone-only flag
therefore has no handshake-level justification.

## The sweep

Every datagram that reaches `ValidateCRC()` live (i.e. passes
`PacketCanBeEncoded()` and is not the `OP_KeepAlive`/`OP_OutboundPing`
early-out) was checked with the session's negotiated key and `crc_bytes`:

| stream | dir | checked | **failed** |
|---|---|---|---|
| login 5998 | C→S | 32 | **0** |
| login 5998 | S→C | 48 | **0** |
| world 9000 | C→S | 264 | **0** |
| world 9000 | S→C | 1,794 | **0** |
| zone 7002 | C→S | 4,082 | **0** |
| zone 7002 | S→C | 39,176 | **0** |
| zone 7012 | C→S | 2,606 | **0** |
| zone 7012 | S→C | 25,354 | **0** |
| | | **73,356** | **0** |

Covering: `OP_Combined`, `OP_Packet`, `OP_Ack`, `OP_Fragment`,
`OP_SessionDisconnect`, `OP_SessionStatRequest/Response`, `OP_OutOfSession`, and
raw `OP_AppCombined`-style frames whose first byte is non-zero — in both
directions, across two zone processes and eight sessions including two deaths
and four zone-ins.

## Each candidate mechanism, and why it is refuted

1. **`m_encode_key` wrong or stale** — refuted for this capture: every packet
   validates with `NetworkToHost(reply.encode_key)`, the value the production
   handler stores (`:943`). See "What this capture cannot prove" for the byte-
   order caveat.
2. **`m_crc_bytes` disagreeing between the sites** — refuted. On a client-role
   connection `options.crc_length` is never used: the ctor sets `m_crc_bytes = 0`
   (`:387`) and only `OP_SessionResponse` sets it (`:944`). There is no path by
   which the option and the session reply can disagree on the client.
3. **Byte order of the trailing CRC** — refuted. `NetworkToHost` on the trailing
   `int16_t` is what matches the wire; the un-swapped read matches nothing.
   Pinned by `DaybreakCrcAlgorithm.RecordedZoneFrameCrcIsReproducible`.
4. **Length passed to `Crc32` (`p.Length() - m_crc_bytes`)** — refuted, and it
   is the discriminating case: including the CRC field, or omitting one byte,
   fails on every packet. Pinned by
   `CorruptedAndTruncatedZonePacketsAreRejected` (a 1-byte truncation is
   rejected).
5. **Encode passes applied in the wrong order** — refuted, and irrelevant to
   CRC anyway (see the compression note above). The send path runs the passes
   forward (`[0]` then `[1]`) and the receive path backward (`i = 1 → 0`), which
   is the correct pairing.

## The independent runtime proof

The capture is the official client's traffic, so on its own it proves the
*algorithm* rather than *fable's* runtime state. That gap is closed by fable's
own send path: every zone packet fable transmits gets `AppendCRC(out)`
(`:1760`) using **the same `Crc32` and the same `m_encode_key`**, and the eqemu
zone server validates client CRCs unconditionally — it has no
`skip_crc_validation`. Fable zones in successfully today. **Therefore fable's
negotiated key and its CRC computation are already being validated by the
server, in the very sessions where its own receive-side validation is turned
off.** Receive uses the identical key and function, so it cannot be wrong while
send is right.

## What this capture cannot prove

**The key's byte order is unexercised.** eqemu carries the same
`Random::Int(uint32_min, uint32_max)` truncation that T21 fixed in fable, so
every `encode_key` in the capture is `0x00000000` or `0xFFFFFFFF` — both
byte-swap palindromes. Against a server that issues a genuinely random key, the
`NetworkToHost` at `:943` is argued correct by symmetry with the send path (the
server-role `OP_SessionRequest` handler wire-encodes with `HostToNetwork` at
`:910`) and by the runtime proof above, **but it is not measured.** A capture
against a server with a non-palindromic key would close this; there is no such
capture and eqemu cannot produce one.

## Where the flag came from

- `895aad1e` (2026-01-22, "Fix zone loading hang by increasing UDP socket buffer
  to 1MB") added `zone_opts.skip_crc_validation = true` alongside 287 lines of
  other network changes. `ValidateCRC()` and `Crc32()` at that commit are
  functionally identical to today's.
- `ad221f29` / merge `def17c2d` (**T21**, 2026-07-31) fixed three
  session-handshake bugs in exactly this path: the `OP_SessionResponse` handler
  had been reading `connect_code`, **`encode_key`** and `max_packet_size` as raw
  wire bytes with no `NetworkToHost`, and both ctors' RNG had collapsed to
  `{0, 0xFFFFFFFF}`.
- T21's own commit message notes the raw `encode_key` read was *masked* by the
  RNG collapse. It was masked for eqemu; it was not masked for anything else,
  and `max_packet_size` was genuinely wrong (131072 instead of 512), which is
  the same class of defect and the same three lines.
- Nothing between January and now retested the flag. That is the whole story:
  **a workaround outlived its bug by six months because nothing pinned it.**

## Why the flag is still set

The plan's acceptance for a flip is *"a live zone session that connects, zones,
and stays connected"*. That was not available:

- eqemu is not reachable from this host (`127.0.0.1:9000` refused; the capture's
  server is `172.18.0.3`, `docker ps` returns nothing here). `/mnt/projects/fable/.eqemu-lock`
  is absent, but the dispatch says slot1/T320 is running live, so the absent
  lock is ambiguous rather than a green light.
- A **stale live client from an earlier unit is still running in this slot**:
  pid 971494, `build-gles2/bin/willeq -c review/T312/willeq_t312.json --egl-x11`,
  started 02:06, 113% CPU for over five hours. Starting a second live client
  from slot2 on top of that is not a clean experiment. **This process should be
  killed by whoever owns T312.**

And per the plan: a short session would not prove the absence of a rare
mismatch anyway. So the diagnosis ships pinned by tests, and the one-line
deletion ships as its own short live unit.

**Recommendation (high confidence): delete the line.** The change is
`git rm`-sized — remove `zone_opts.skip_crc_validation = true;` from
`ConnectToZone()` (`src/client/eq.cpp:718`; the `DaybreakConnectionManagerOptions
zone_opts` local and the `(zone_opts)` ctor argument can stay, they are harmless
and other options may want them). Note that the *failure mode of a real
mismatch is a dropped datagram, not a disconnect*: a dropped reliable frame is
re-sent by the server on ack timeout, so validation costs a round trip in the
rare case and prevents corrupt bytes from being parsed as game state in every
case. Acceptance for that unit: zone in, move, zone out, and grep the log for
`DROPPED_CRC` (the drop already logs at WARN with a full hex dump,
`daybreak_connection.cpp:540`) — plus the honest statement that a short session
does not prove the absence of a rare mismatch.

## Deliverable

`tests/test_daybreak_crc_capture.cpp` (7 tests, new binary
`test_daybreak_crc_capture`) + fixture `tests/data/t321_crc_capture.txt`
(212 KB): the recorded `OP_SessionResponse` of each of the three streams plus a
sample of each covering every (direction, protocol opcode) pair, taken from
`review/T311/legA/T311A_official_client_session.pcap`.

The session parameters are **not** hand-set — the recorded `OP_SessionResponse`
is fed through the production `ProcessPacket()` so the real handshake code
negotiates key/crc_bytes/passes, then the real `ValidateCRC()` is called on the
recorded bytes.

| test | pins |
|---|---|
| `RecordedSessionResponsesNegotiateTwoCrcBytes` | all three streams negotiate `crc_bytes=2`, `max_packet=512`, via the production handler |
| `OnlyTheZoneStreamNegotiatesCompression` | the single difference between zone and login/world |
| `EveryCapturedZonePacketPassesCrcValidation` | zone packets pass with `skip_crc_validation = false` |
| `EveryCapturedLoginAndWorldPacketPassesCrcValidation` | login and world likewise |
| `CorruptedAndTruncatedZonePacketsAreRejected` | one flipped bit at three offsets, and a 1-byte truncation, are all rejected — validation discriminates |
| `SkipCrcValidationAcceptsACorruptedZonePacket` | **the live defect**: with the flag eq.cpp sets, a corrupted packet is accepted |
| `DaybreakCrcAlgorithm.RecordedZoneFrameCrcIsReproducible` | `Crc32` key seeding + trailing-CRC byte order against every fixture zone frame, independent of session plumbing |

## Found, deliberately not fixed

- **`tests/test_daybreak_connection_pcap.cpp:139-146`** (`processSessionResponse`)
  reads `connect_code`, `encode_key` and `max_packet_size` **raw**
  (`*(uint32_t*)&pkt.data[2]`), i.e. it still encodes the pre-T21 bug that
  `ad221f29` removed from production. It passes only because eqemu's
  `encode_key` is a palindrome and the test never compares `connect_code`. It is
  a test that would not notice a regression of T21's fix. Left alone: it is a
  different test's semantics and outside this unit's brief. New test above uses
  the production handler instead.
- **The T312 client process** described under "Why the flag is still set".
- **`ValidateCRC`'s `default: return false`** for any `m_crc_bytes` other than 2
  or 4: a server negotiating e.g. 1 or 3 CRC bytes would fail every packet
  rather than log anything. eqemu only ever sends 2, so this is theoretical.

## Verification

| step | result |
|---|---|
| `cmake --build build -j24` (GLES2 tree) | **PASS**, EXIT=0 |
| `ctest --test-dir build -j6 --output-on-failure` | **3484 / 3485**; sole failure `AudioManagerTest.PreloadCommonSounds` (documented ALSA host quirk). This unit adds 7 tests (3478 before). |
| `check_file_sizes.sh --files` on the changed files | no new FAIL (`test_daybreak_crc_capture.cpp` 341 lines ok; `tests/CMakeLists.txt` WARN at 2794 lines is pre-existing) |
| visual gate / ARM / `build-gles2-x86.sh` | **not run — narrowed by the plan's Acceptance section.** No production source changed; the diff is one new test TU, one fixture and a `tests/CMakeLists.txt` block. |

`HeadlessShutdownTest.NonLifecycleCommandIsHeldNotDroppedOrApplied` passed in
the `-j6` run; no re-run needed.
