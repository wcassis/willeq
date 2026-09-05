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
