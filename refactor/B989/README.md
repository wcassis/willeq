# B989 — one decoded-PCM cache for SFX (issue #308)

**Class A (live memory defect).** Every played WAV was held **twice** as 32-bit
float PCM: `AudioManager::bufferCache_` (ledger owner `au.sound_buffers`, the
decode on first `playSound`/`playSoundByName`) **and** `SfxManager::cache_`
(ledger owner `au.sfx_cache`, the second copy `SfxManager::preload()` made of
the very same samples), each capped separately at the preset's `soundCache`
(8,192 KB on `orangepi`) — up to 16 MiB of duplicated decoded PCM. Filed by
B977 (#295); the B977 census measured it at x86 befallen steady_60s after the
tour's combat: `au.sound_buffers` 2,322,320 B + `au.sfx_cache` 310,640 B
(`refactor/B977/audio_ledger_census.tsv`; the issue's table quotes the shutdown
point's 723,600 B).

## The fix: share the buffer, do not copy it

Of the two mechanisms #308 names ("`SfxManager::preload()` points at
`AudioManager`'s cached buffer (shared_ptr), or one shared decoded-PCM cache
serves both"), this unit implements **the first**: `SfxManager::cache_` now
holds a `std::shared_ptr<SoundBuffer>` to the **same** `SoundBuffer` object
`AudioManager::bufferCache_` already holds, and `preload()` takes that
`shared_ptr` instead of a raw PCM span.

Why the shared_ptr, not a merged cache:

- **The decoded PCM is already owned once, by the right owner.**
  `AudioManager::loadSound()` is the only place a WAV is decoded
  (`SoundBuffer::loadFromFile` / `loadFromMemory`), and it charges the decode
  to `au.sound_buffers` under the `audio` category. Every SFX play path
  (`playSoundInternal`, and the weather loops) goes through `loadSound()`
  first, so the buffer the SfxManager plays from is ALWAYS in `bufferCache_`.
  There is no caller that decodes a sound the AudioManager does not also hold
  — the second cache never owned anything the first one did not already hold.
- **One eviction policy is enough.** Both caches are filename-keyed LRU caches
  bounded by the SAME preset value (`setMemoryConstraints()` hands one
  `soundCacheBytes` to both). The buffer cache evicts its `shared_ptr`s; the
  sfx cache evicts its references. A buffer freed by its owner while an active
  mixer channel still reads it cannot happen: `SfxManager::isInUseLocked()`
  pins the playing entry, and the entry's `shared_ptr` keeps the PCM alive
  regardless of what the buffer cache does.
- **The ledger reports the shared PCM once, with its claimant named.** A
  block is charged to the owner that allocated it (CLAUDE.md memory
  accounting: "a block shared by several owners is reported ONCE, with all its
  claimants named"). The PCM is allocated by `SoundBuffer::loadFromFile()`,
  running inside `loadSound()`'s `au.sound_buffers` scope, so the block stays
  on that row for its whole life. `au.sfx_cache` stops existing as a PCM row:
  nothing is allocated under it any more (the `SfxManager`'s own small map
  entry overhead lands in the `audio` category unlabelled, as it always did
  for every other manager state). The `au.sfx_cache` label is not
  re-purposed — the `[memledger-startup]` line for it simply stops being
  printed once its last row frees, which is what "reported once" looks like.

Files changed (code):

- `include/client/audio/sfx_manager.h` — `preload(filename,
  std::shared_ptr<SoundBuffer>)`; `cache_` is
  `unordered_map<string, shared_ptr<SoundBuffer>>`; `getWavData()` returns the
  shared `SoundBuffer*`; the `WavData` struct is deleted.
- `src/client/audio/sfx_manager.cpp` — the copy is gone; `play()` hands the
  channel `buffer->getSamples()`; eviction drops a reference.
- `src/client/audio/audio_manager.cpp` — `playSoundInternal()` passes the
  buffer through (`std::move` into `preload()`); the `au.sfx_cache`
  `StartupScope` is deleted (nothing is charged under it any more).
- `src/client/audio/weather_audio.cpp` — the rain/wind loops share the
  `SoundBuffer`s they already hold.

Files changed (tests):

- `tests/test_sfx_single_pcm_cache.cpp` (new) — THE RED/GREEN: plays real
  archive sounds through the client's own paths and asserts the owner rows
  agree (one holds the PCM, the other holds it at 0), the `audio` category
  carries it once not twice, and the preset `soundCache` cap bounds the
  SINGLE cache with T412's LRU (least-recently-used goes first, the
  most-recently-used survives) intact.
- `tests/test_sfx_cache_eviction.cpp` — UPDATED, not deleted: T412's LRU
  behaviour (ceiling enforced, LRU first, playing entry pinned, lowering the
  ceiling bites at once) is asserted against the shared-buffer cache; "a
  sound" is now a `SoundBuffer` built from raw PCM, shared in the way the
  production path does.
- `tests/cmake/audio.cmake` — the new target, linked the way the other
  `AudioManager` tests are (ledger TU + private `WILLEQ_CACHE_DIR` in the
  test, `data/eq_client` for the archive).

Stale docs updated: `tests/test_audio_ledger_attribution.cpp`'s comment,
`scripts/client_ledger_capture.sh`'s mode description,
`docs/reference/music.md`'s SfxManager one-liner (`:71-74`),
`refactor/BOARD.md` (B989 struck).

## RED (pre-fix, this branch's first commit 4ea10711)

`ctest -R SfxSinglePcmCache` on the RED commit (both caches copying), built in
a throwaway detached worktree with the same `data/eq_client` symlink:

```
[measured] first play of a buffer-cached sound: pcm=21920 audio +22336 B
[measured] second play of the same sound: audio +32 B
tests/test_sfx_single_pcm_cache.cpp:174: Failure
Expected: (grew1) < (static_cast<uint64_t>(pcm) / 2), actual: 22336 vs 10960
playing a buffer-cached sound copied its decoded PCM into a second cache (B989):
audio grew by 22336 B for a sound of 21920 B already resident

[  FAILED  ] SfxSinglePcmCache.ABufferCachedSoundPlaysBackWithoutCopying (55 ms)
[measured] fresh decode of sound 126: pcm=21920 audio +44560 bufferCache +21920 B
[measured] after playing 3 sounds under cap 21919: decoded PCM live=11072 B
[==========] 3 tests from 1 test suite ran. (269 ms total)
[  PASSED  ] 2 tests.
```

The failing case IS the defect: a sound whose decoded 21,920 B of PCM was
already resident in `bufferCache_` grew the `audio` category by 22,336 B when
played — a full second copy (the extra ~416 B is the mixer-channel
bookkeeping), exactly what `SfxManager::preload()`'s `std::copy` did.

## GREEN (post-fix, this branch's tip)

Same binary invocation at the tip:

```
[measured] first play of a buffer-cached sound: pcm=21920 audio +352 B
[measured] second play of the same sound: audio +32 B
[measured] fresh decode of sound 126: pcm=21920 audio +22576 bufferCache +21920 B
[measured] after playing 3 sounds under cap 21919: decoded PCM live=11072 B
[==========] 3 tests from 1 test suite ran. (255 ms total)
[  PASSED  ] 3 tests.
```

The replay now grows the `audio` category by only 352 B (mixer-channel
bookkeeping) — no second PCM copy. A FRESH decode still charges its 21,920 B
once (the `bufferCache` accounting row shows the same 21,920 B, i.e. the
category's growth is the decode plus per-block ledger overhead, not a
duplicate). The cap case bounds the single cache: under a cap of `secondSmallest − 1`
(21,919 B here), after playing all three probe sounds the resident decoded
PCM is `smallest` or less — the measured 11,072 B live is one sound, and the
test's following assertion (replaying the most recent sound re-decodes nothing)
proves that survivor IS the most recently played one — the earlier probes were
evicted LRU-first — and replaying the survivor is a cache hit (no re-decode). Pre-fix the SfxManager cache was
bounded SEPARATELY, so a sound evicted from the buffer cache survived as a
full PCM copy in the sfx cache; now there is one population and one ceiling,
and `tests/test_sfx_cache_eviction` (6 cases) asserts the LRU mechanics —
ceiling enforcement, LRU-first, playing-entry pinning, immediate re-eviction
on a lowered ceiling — against the shared-buffer cache.

The audio suite is the CI `test` job's (it was 5,757/5,757 green at MR
head cbdb905b, pipeline 773); the authoritative count is re-measured per
push, not quoted. (`SoundBufferTest.LoadSwingWav` is pre-existing and
`Skipped` — needs a file not shipped.)

## LIVE ledger leg (eqemu lane 3, x86, befallen)

B977's x86 befallen tour reproduced (`client_ledger_capture.sh --zones
befallen --modes audio`, B977's own tour file, `--char-level 60
--char-exp 616137000`, the rig took and released the lane lock and restored
the snapshot byte-identically with the world down). `run_meta.txt` pins
`commit=07f9ea83 dirty=0`, the binary's sha256, and the lane; raw client log
in `/home/user/kb-evidence/b989-sfx-cache-dup/live_befallen/befallen/audio/`.

| owner row (steady_60s)        | B977 baseline | after B989 |
|-------------------------------|--------------:|-----------:|
| `au.sound_buffers`            |     2,322,320 |    2,322,288 |
| `au.sfx_cache`                |       310,640 | **ABSENT** |
| **the pair's SUM**            |   2,632,960   |  **2,322,288** |
| `audio` category TOTAL (startup scope) | 4,252,448 | 3,872,576 |

The pair's sum dropped by **310,672 B** — at least the smaller row (310,640
B), the criterion, [measured]. `au.sound_buffers` is byte-parallel with the
baseline (−32 B, one less mixer-side allocation in this run); the duplicate
PCM is gone entirely — the `au.sfx_cache` `[memledger-startup]` line does not
exist in the new log (grep count 0), and the `audio` category total is 379,872
B lower, of which 310,640 B is exactly the removed sfx_cache row and the
remaining 69,232 B is run-to-run variation in the other owner rows (pfs_index
403,184 vs 471,200, backend 46,512 vs 48,032, soundfont_gm 1,036,640 vs
1,036,624, zone_emitters 3,616 vs 3,600, misc 6,128 vs 5,824, plus unlabelled
audio-category map overhead) — none of it a PCM row. The two quoted `[memledger]` lines:

```
# B977 baseline (refactor/B977/x86/befallen/audio/client.log, 2026-10-05 23:03:46)
[memledger-startup] point=steady_60s owner=startup.au.sound_buffers live=2322320 blocks=73
[memledger-startup] point=steady_60s owner=startup.au.sfx_cache live=310640 blocks=4

# B989 (this leg, 2026-10-11 02:34:15)
[memledger-startup] point=steady_60s owner=startup.au.sound_buffers live=2322288 blocks=73
# no au.sfx_cache line exists
```

The census table: `refactor/B989/audio_ledger_census.tsv`.

## Not verified / left open

- **The Pi leg** (befallen Pi steady_60s: B977 measured 2,320,376 + 723,456
  B) and **the `memoryLedgerBudgets.audio` re-ratchet** are explicitly NOT this
  card — Will's number, after B990, `docs/decisions.md`. The 16 MiB
  worst-case bound is structural (one cache, one cap) and not re-measured
  live: the tour plays a small fraction of the archive, so a live worst-case
  number would need a dedicated tour and belongs to the re-ratchet.
- The `memoryLedgerBudgets.audio` value (32 MiB) and the preset's `soundCache`
  (8,192 KB on `orangepi`) are untouched, per the card.
- The RED run's "fresh decode" case passed even pre-fix (it measures the
  decode itself, which was never the defect); the CAP case passed pre-fix
  because pre-fix the SfxManager cache and the buffer cache were bounded
  SEPARATELY, so the buffer cache alone obeyed its cap — the cap now bounds
  the single combined population instead, which is the new guarantee the
  GREEN tip's case asserts (same assertion, different population).
