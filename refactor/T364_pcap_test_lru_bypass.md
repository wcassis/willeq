# T364 — pcap test reads through production (B009); chr-cache LRU bypass (B065)

Class B, slot3. Two disjoint items; one branch, two commits.

## Leg A — B009

`tests/test_daybreak_connection_pcap.cpp` reads `connect_code`, `encode_key`
and `max_packet_size` raw off the wire — the exact bug T21 fixed in
production — so it cannot catch a regression of T21 and passes today only
because eqemu's key is a byte-swap palindrome. Make the test read through
the production path (the same parse `DaybreakConnection` uses). Prove it
bites: temporarily revert T21's byte-swap in production (find the commit /
the line), run the test → it must FAIL with a non-palindromic key (pick or
construct a capture whose key is not its own byte-swap; if every capture
on disk is palindromic, synthesise the handshake bytes in the test and say
so). Restore. Strike B009.

## Leg B — B065

`RaceModelLoader::loadModelFromCachedChr` (`model_loading_chr.cpp`) writes
`otherChrCaches_[lowerFilename]` directly instead of through
`storeOtherChrCacheLocked()`, so that entry never enters
`chrCacheLruOrder_`, can never be evicted, yet counts against
`maxChrCacheEntries_`. Route it through the store helper. Test, pre-fix
failing: after loading through that path, the entry is in the LRU order
and is evicted when the cap is exceeded (use the loader's existing seams;
T352's driver under `review/T352/` shows how to stand the loader up with
real archives under EDT_NULL if a unit test cannot). Strike B065. Note
T352's lock in `getMergedTextures` — do not touch `mesh_building.cpp`.

## Proof

Build, `ctest -j6`, the new/changed tests with pre-fix/post-fix runs, file
sizes; Leg B touches `src/client/graphics` → visual gate standalone + ARM
(rebuild all three trees first; willeq.db v6).

STOP: none expected.

---

## Review

Branch `task/T364-pcap-test-lru-bypass`, base `fable` tip `0a67a6a9`.
Leg A `f734ac3a`, Leg B `ca96cb56`; the board strikes and this Review are the branch tip.
No eqemu lock taken; nothing here needs a server. No STOP hit.

### Leg A — B009

**How the test now reads the handshake.** `processSessionResponse()` no
longer memcpys the wire into `DaybreakConnection`'s private members. It sets
`m_connect_code` to the host-order value of the recorded `connect_code` (what
`SendConnect()` would have picked for us — the one value a test legitimately
supplies), puts the connection back into `StatusConnecting`, and feeds the
recorded bytes through `DaybreakConnection::ProcessPacket()`. Everything the
connection then believes about the session comes from the production
`OP_SessionResponse` arm in `daybreak_connection_decode.cpp`.
`OP_SessionResponse` is exempt from `PacketCanBeEncoded()`
(`daybreak_connection_send.cpp:118`), so it reaches that arm without CRC or
decompression, and the CRC-bypass replay's `m_crc_bytes = 0` is applied
*after* the production parse instead of instead of it. The four pre-existing
replay tests are otherwise unchanged; their call sites are now wrapped in
`ASSERT_NO_FATAL_FAILURE` because the helper asserts.

**What bytes it uses, and why two sources were needed.** Measured over
`data/captures/session.pcap` (SLL2 link type, 11 `OP_SessionResponse`
datagrams across login/world/zone):

| raw `connect_code` | raw `encode_key` | raw `max_packet_size` | count |
|---|---|---|---|
| `0x00000000` | `0x00000000` | 131072 | 7 |
| `0xFFFFFFFF` | `0x00000000` | 131072 | 2 |
| `0xFFFFFFFF` | `0xFFFFFFFF` | 131072 | 2 |

`connect_code` and `encode_key` are byte-swap **palindromes in every
capture** — T21's own RNG-collapse finding, `{0, 0xFFFFFFFF}` — so no capture
on disk can discriminate them, exactly as B009 said. Only
`max_packet_size` differs: network-order 512 read raw is 131072.

So two tests:

- `RecordedHandshakeYieldsNetworkOrderMaxPacketSize` — real capture, real
  zone-stream handshake, asserts `max_packet_size == 512` (plus `crc_bytes
  == 2` and compression on pass 1).
- `SynthesisedHandshakeByteSwapsAllThreeFields` — a 17-byte
  `DaybreakConnectReply` whose wire bytes are written out big-endian **by
  hand** (so the expectation does not lean on the same endian helper the
  production code uses): `connect_code` `0x11223344`, `encode_key`
  `0xA1B2C3D4`, `max_packet_size` 512. Needs no capture.

**Pre/post.** `NetworkToHost()` removed from all three reads in
`daybreak_connection_decode.cpp` (the T21 arm), target rebuilt:

| run | result |
|---|---|
| new tests, T21 reverted | **2 FAILED** — `max_packet_size` 131072 != 512; status stayed `StatusConnecting` ("connect_code was not compared in host order") |
| pre-T364 version of the file (`git show HEAD:`), same revert | **4 of 4 PASSED** — B009 restated as a measurement |
| new tests, T21 restored | **6 of 6 PASSED** |

`git diff --stat src/common/net/daybreak_connection_decode.cpp` is empty on
the branch; production is byte-identical to `fable`.

### Leg B — B065

**It was four sites, not one.** `loadModelFromCachedChr` wrote
`otherChrCaches_[key]` raw at the two negative-cache inserts (archive fails
to open; archive opens with no characters), the positive insert, and a
fourth for the animation-source archive further down the same function. All
four now call `storeOtherChrCacheLocked()`. The positive insert's debug log
now reads the size back through the returned iterator, since the helper may
evict; the animation-source log captures the count before the store for the
same reason. `mesh_building.cpp` untouched (T352's lock).

**Test.** `tests/test_chr_cache_lru.cpp` (`ChrCacheLruTest`, 3 cases),
registered in `tests/cmake/ui_rendering.cmake` under the same
`EQT_GRAPHICS AND IRRLICHT_LIBRARY` gate and `EDT_NULL` + `willeq_core`
pattern as `test_race_330_loader`. It drives the **user-reachable** entry
point `ensureModelDataLoaded()` — in old-models mode a race whose
`race_models.s3d_file` is a plain zone archive routes straight into
`loadModelFromCachedChr` — for five races in five different archives (13
airplane, 15 butcher, 16 northkarana, 17 befallen, 22 beholder), cap 2.
Offline: shipped `data/willeq.db`, real client tree, no server, no DISPLAY.

The accounting had **no observable surface** (`otherChrCaches_`,
`chrCacheLruOrder_`, `maxChrCacheEntries_` all private; `getMemoryStats()`
reports texture bytes only), so `getChrCacheAccountingForTest()` was added
beside `setMaxChrCacheEntries()` — entries, LRU-order size, count of keys
absent from the LRU order, and the cap. Same `ForTest` precedent as
`overlays_component.h` / `frame_component.h`.

| case | pre-fix | post-fix |
|---|---|---|
| `EveryCachedChrEntryIsRegisteredInTheLruOrder` | **FAIL** — `untracked=6` of `entries=6`, `lruOrder=0` | PASS |
| `CachedChrEntriesAreEvictedOnceTheCapIsExceeded` | **FAIL** — 6 entries against a cap of 2; `entries(6) != lruOrder(0)` | PASS |
| `UnlimitedCapKeepsEveryEntry` | PASS | PASS |

Pre-fix produced by exporting the production hunk and `git apply -R`, then
re-applying — never `git stash`.

**Surprise worth recording: on this path the cap did nothing at all.** B065
described entries that "count against `maxChrCacheEntries_` yet can never be
evicted", which reads as partial degradation. Measured, `chrCacheLruOrder_`
was **empty** after five loads: every single entry the JSON-specified-S3D
path cached was untracked, so LRU eviction never had a candidate and the
constrained-preset chr-cache cap was inert for the whole of this path.

**New board item B095** (confirmed by reading, not measured): the identical
bypass survives at four more sites — `loadModelFromZoneChr`'s
animation-source insert in the same TU, and
`searchZoneChrFilesForModel`'s two negative-cache inserts plus a third that
open-codes the helper's push/evict inline (`model_loading_search.cpp`).
Left out of scope because the plan and the board entry name
`loadModelFromCachedChr`; flagged rather than silently absorbed.

### Verification

| step | result |
|---|---|
| `cmake --build build -j24` | **EXIT 0.** First attempt hit exit 143 at the 600 s ceiling; post-143 sweep found 0 zero-byte files, 0 non-ELF binaries over 262 artifacts, 2 archives with 15/321 members, no `build*/lib/st??????` temps — but `bin/test_rdp_component` **outright DELETED** by the killed link (the absence case, exactly as the protocol warns). Rebuild restored it (53 MB, valid ELF). |
| `ctest -j6 --output-on-failure` | **3893 of 3894 passed** (exit 8). Sole failure `AudioManagerTest.PreloadCommonSounds` — the waived ALSA host quirk. No `visual_gate`/`HeadlessShutdownTest`/discovery load artefacts. |
| `scripts/build-gles2-x86.sh` | EXIT 0 |
| `scripts/ci/visual_gate.sh` | **PASS.** p95 process-CPU: qeynos2 5.999 (ceiling 10.108), gfaydark 5.508 (10.672), befallen 7.175 (10.948). Goldens untouched (`git status` clean of them). |
| `scripts/build-arm-noble.sh` | EXIT 0; `build-arm-noble/bin/willeq` is ELF 32-bit ARM EABI5 |
| `check_file_sizes.sh fable` | 4 checked, **4 ok, 0 warn, 0 fail** (largest `race_model_loader.cpp` 1153) |

All three trees rebuilt for willeq.db v6. `EQT_GLES2_IRRLICHT_DIR` in
`build/CMakeCache.txt` points at the slot's own
`third_party/irrlicht-gles2-x86`.

### Deviations

- Three commits, not two: Leg A, Leg B, then the board strikes + this
  Review, so the strikes could name the real commit hashes.
- Leg B added a public `getChrCacheAccountingForTest()` to
  `race_model_loader.h`. The alternative was `#define private public` over
  the Irrlicht-including header; the `ForTest` accessor is the established
  pattern in this directory.
- The visual gate ran concurrently with the ARM cross-build. It passed with
  p95 at 55-66% of ceiling, so no standalone re-run was needed.
