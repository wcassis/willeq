# B1015 — one decoded-PCM cache of SFX PCM really (issue #336)

**Class A behaviour defect.** B989 (MR !131, #308) removed the COPY between
the two filename-keyed LRUs but left the SECOND LRU itself:
`SfxManager::cache_` beside `AudioManager::bufferCache_`, each with its own
cap (`setMemoryConstraints`, `src/client/audio/audio_manager.cpp:620-627` at
the base, set both). The AUDIT-WAVE7 finding (2026-10-11) stands:

1. When `bufferCache_` evicted a sound the sfx cache still referenced, the
   next play re-decoded it into `bufferCache_` — `preload()` was a no-op for
   a filename it had (`sfx_manager.cpp:28-29` at the base) — so two decoded
   copies were resident.
2. The two LRUs picked different victims (the sfx LRU skipped in-use entries,
   the buffer cache did not), so the cap bounded a PROJECTION of the
   resident PCM: the MR's own cap case (cap 21,919) held 11,072 B in the
   buffer cache and 21,920 B in the sfx cache — 32,992 B = 11,072 + 21,920 of
   PCM live. The "structural bound" claim (`refactor/B989/README.md`,
   `refactor/BOARD.md` at the base) was false.
3. Pre-existing since T412: when every older sfx entry was playing, the sfx
   LRU evicted the entry it had JUST preloaded and `play()` returned -1 —
   the requested sound silently never played (the audit's probe measured
   sfxCount=1, sfxEvictions=2).

The merged B989 tests passed on a PROXY: `test_sfx_single_pcm_cache.cpp` read
only `getSoundBufferCacheBytes()` (the buffer cache's own bytes) and
`test_sfx_cache_eviction.cpp`'s `APlayingSoundIsNotEvicted` kept its own
`playing` shared_ptr, so neither could see the defect. This unit's tests
assert the BEHAVIOUR (total resident PCM, buffer identity, and `play()`'s
verdict), so a return of the defect is a red test.

## The fix

ONE cache holds every decoded SFX PCM reference under ONE cap, with in-use
pinning in that one cache:

- **`SoundBufferCache`** (new: `include/client/audio/sound_buffer_cache.h`,
  `src/client/audio/sound_buffer_cache.cpp`) — the old `bufferCache_` (map +
  LRU list + inline eviction loop) grown into a class: one population, one
  cap (`setMaxBytes` = the preset's `soundCache`, applied by
  `AudioManager::setMemoryConstraints`), a `Source` (the old `loadSound()`
  decode half, now `decodeSound()`, bound as a callback so the ~0.4 s of
  first-fetch archive I/O runs without the cache lock), a `Pin` (a callback
  into `SfxManager::isInUse()`), and `peek()` — a non-loading residency read
  the tests use to tell "evicted" from "resident" (a `getOrLoad` miss would
  re-decode and hide the eviction). The PCM is charged once, by the loader,
  to `au.sound_buffers` (CLAUDE.md memory accounting: a block shared by
  several owners is reported once, with its owner named).
- **`SfxManager` keeps no cache** (`include/client/audio/sfx_manager.h`,
  `src/client/audio/sfx_manager.cpp`): `cache_`, `lruOrder_`,
  `cacheMaxBytes_`, `cacheSizeBytes_`, `evictionCount_`, `preload()`,
  `getWavData()`, `setCacheMaxBytes()` and its LRU helpers are deleted.
  `play(filename, …)` becomes `play(const SoundBuffer* buffer, …)` — every
  path already resolved the buffer through the one cache
  (`AudioManager::playSoundInternal` loads first), and the channel stores a
  raw `float*` into the buffer's vector. `isInUseLocked()` is
  `isInUse()`, moved next to the only cache left.
- **The pin, stated plainly**: the shared_ptr is NOT what keeps a playing
  channel safe (the channel holds a raw `float*`,
  `sfx_manager.cpp` at the base: `ch->samples = buffer->getSamples()`). What
  keeps it safe is the `SoundBufferCache`'s in-use pin: it asks
  `SfxManager::isInUse()` (does an ACTIVE channel point into that buffer?)
  before evicting and refuses any entry an active channel names.
- **The entry being inserted is never the one evicted**: a fetch inserts at
  the FRONT of the LRU and `enforceBudgetLocked()` walks from the BACK,
  skipping pinned entries — the freshest entry is a floor, not a victim.
  When nothing else is evictable the cache stays lawfully over budget and
  WARNs; over budget is better than a use-after-free, and the cap is
  re-checked on the next insertion or `rebalance()` (called from
  `AudioManager::stopAll()` once the mixer is quiet — the new public
  `AudioMixer::stopAll()` deactivates every channel and waits for the render
  thread to quiesce, so a stopped channel releases its pin).
- **`APlayingSoundIsNotEvicted` drops its local shared_ptr before flooding**
  (`tests/test_sfx_cache_eviction.cpp`): the cache entry is the last owner,
  so the in-use rule can no longer be broken and hidden by a test-held
  reference. The test drives the real `SoundBufferCache` with a source map
  and the production pin wiring (a real `SfxManager` + `AudioMixer`).
- The debug payload's `sfxCacheBytes` row is dead (always 0, no longer
  printed or counted — `src/client/graphics/diagnostics/
  diagnostics_component.cpp`, `src/client/commands/commands_component.cpp`);
  the ONE cache's total is the `Sound Buffer Cache` row.

Untouched, per the card: `config/constrained_presets.json` (the
`soundBufferCacheBytes` derivation), every budget, `tests/goldens/*`,
`refactor/B989/b989-live-evidence.tar.gz`.

## RED (pre-fix, on origin/fable 0136137b)

Committed at `2be904e8` and `792609d6` (the two RED commits); output
`/home/user/kb-evidence/b1015-one-pcm-cache-really/red/red2_run.txt` (built
in a detached worktree at the RED head, the two behaviour tests failing on
origin/fable):

(a) `BufferCacheEvictionResurrectsTheDuplicate` — play A, non-play fetch of B
(cap = A's size, so B's fetch evicts A from the buffer cache), replay A:
the sfx entry and the buffer-cache entry are TWO objects, 53,104 B live
(21,920 B in each):

```
[measured] cap=24872 all three steps grew audio by 53104 B (bound 63176) sfxA=0x5a19118e6ec0 bufferCacheA=0x5a1911974840 sfxCacheBytes=21920 bufferCacheBytes=21920
/home/user/kb-evidence/b1015-one-pcm-cache-really/red-worktree/tests/test_sfx_single_pcm_cache.cpp:422: Failure
Expected equality of these values:
  sfxA
    Which is: 0x5a19118e6ec0
  bcA.get()
    Which is: 0x5a1911974840
two decoded copies of Kick1.WAV are resident -- the buffer cache evicted the sound the sfx cache still held (B1015 finding 1)
```

(b) `TheCapBoundsTheTotalResidentPcm` — the MR's own cap case (ids
126/130/131, cap = middle size − 1 = 21,919): 42,240 B of decoded PCM live
under 21,919 (bound 38,303):

```
[measured] cap case: sizes=21920,24872,11072 cap=21919 total decoded PCM live=42240 B (bound 38303) bufferCacheBytes=11072 sfxCacheBytes=21920 sfxCount=1 sfxEvictions=2
/home/user/kb-evidence/b1015-one-pcm-cache-really/red-worktree/tests/test_sfx_single_pcm_cache.cpp:483: Failure
Expected: (pcmDelta) <= (static_cast<uint64_t>(cap) + 16ll * 1024), actual: 42240 vs 38303
the plays left 42240 B of decoded PCM live under cap 21919 -- the total resident decoded PCM is not bounded by the soundCache cap: the two caches pick different victims (B1015 finding 2)
```

(c) `TheSoundBeingInsertedIsNeverEvicted` — the same audit cap-case sequence
back-to-back, asserting each requested sound's decode grows the `audio`
category by at least half its PCM (a `play()` that returned -1 grows it by
a few bytes): the audit's run is the red record for it — on the audit's rig
the three plays landed within milliseconds and the two older channels were
still playing at each insertion (sfxCount=1, sfxEvictions=2 — sounds 130 and
131 evicted at their own preload and never played). On this host (llvmpipe,
~10× faster) the "older channel still playing" window does not hold, so (c)
passes pre-fix; it pins the invariant (every requested sound plays) either
way and stays a net against the fixed code.

## GREEN (post-fix, this branch's tip)

`/home/user/kb-evidence/b1015-one-pcm-cache-really/green/final_run.txt`:

```
[measured] first play of a buffer-cached sound: pcm=21920 audio +48 B
[measured] second play of the same sound: audio +32 B
[measured] fresh decode of sound 126: pcm=21920 audio +22272 bufferCache +21920 B
[measured] after playing 3 sounds under cap 21919 (stopped): decoded PCM live=11072 B
[measured] cap=24872 sequence grew audio by 30784 B (bound 63176) oneCacheA=0x648cbf900570 bufferCacheBytes=21920 entryCount=1
[measured] cap case: sizes=21920,24872,11072 cap=21919 total decoded PCM live after stop=11072 B (bound 21919)
[measured] play 126 (pcm 21920 B, cap 21919): audio +30752 B
[measured] play 130 (pcm 24872 B, cap 21919): audio +25168 B
[measured] play 131 (pcm 11072 B, cap 21919): audio +11376 B
[measured] oneCacheBytes=57864 entryCount=3
[  PASSED  ] 6 tests.   (test_sfx_single_pcm_cache)
[  PASSED  ] 5 tests.   (test_sfx_cache_eviction, incl. APlayingSoundIsNotEvicted with the test-held shared_ptr dropped)
```

(a) post-fix: the replay grew the `audio` category by 30,784 B across all
three steps (one decode of each sound, 21,920 + 11,072, plus channel and
ledger overhead) — the RED's 53,104 B minus exactly the resurrected second
copy (53,104 − 30,784 = 22,320 ≈ 21,920 B PCM + channel). After the cap
evicts, one cache holds one sound (21,920 B, the freshest), and the buffer a
later fetch hands out is the resident one. (b) post-fix: the total resident
decoded PCM after the plays stop is 11,072 B ≤ the cap 21,919 — the RED's
42,240 B (32,992 B PCM + overhead) is gone; the survivor is the most
recently played (replaying 131 re-decodes nothing, < 4,096 B). (c) post-fix:
every requested sound plays (+30,752 / +25,168 / +11,376 B — each ≥ half its
PCM; while they play the cache is lawfully over budget at 57,864 B — 21,920
+ 24,872 + 11,072 + overhead — every entry pinned; `stopAll()` releases the
pins and `rebalance()` reclaims).

The playback-rate and spatial suites stay green at the tip (local, rebuilt
binaries): `test_spatial_audio` 14/14, `test_weather_audio` 20/20,
`test_sound_assets` 19 passed / 1 pre-existing `Skipped`
(`SoundBufferTest.LoadSwingWav` needs a file not shipped),
`test_sound_effects` 15/15, `test_sfx_single_pcm_cache` 6/6,
`test_sfx_cache_eviction` 5/5. The CI `test` job re-measures the full suite.

## CI (MR !151, pipeline 859, head c0969eb7)

All four stages green on gitlab-runner: hygiene 4357, build 4358, test 4359,
gate 4361. Test job 4359: the three behaviour tests are **Passed, not
Skipped** (5,747/5,747, 0 failed):

```
Test #5417: SfxSinglePcmCache.BufferCacheEvictionResurrectsTheDuplicate ..   Passed    0.12 sec
Test #5418: SfxSinglePcmCache.TheCapBoundsTheTotalResidentPcm               ..   Passed    0.13 sec
Test #5419: SfxSinglePcmCache.TheSoundBeingInsertedIsNeverEvicted           ..   Passed    0.13 sec
100% tests passed, 0 tests failed out of 5747
```

## Docs touched

- `refactor/B989/README.md` — steady_60s is BEFORE the tour's combat
  (B989 02:34:15 vs `#npctypespawn` 02:34:29; B977 23:03:46 vs 23:03:58);
  the `shutdown` row added: B977 723,600 + 2,322,320 = 3,045,920 vs B989
  2,322,288, a drop of 723,632 (re-derived from the raw logs — see below);
  the cited `refactor/B977/x86/befallen/audio/client.log` does not exist in
  the repo — the hermes-ext path is cited now and the 4 baseline rows are
  committed in `refactor/B989/audio_ledger_census.tsv` (which now also
  carries the shutdown rows); the lane difference named (B977 lane 1, a
  13-zone sweep; B989 lane 3, befallen only) and the unmeasured "run-to-run
  variation" explanation of the 68,016 B `pfs_index` delta replaced with the
  legs-differing fact; the live binary's `commit=07f9ea83` is noted as
  rewritten (not on GitLab) with its C++ equal to `28d14c48`; the
  "structural bound" restated from the fixed code with the arithmetic.
- `refactor/BOARD.md` — the B989 entry: same corrections, plus the B1015
  supersession note (the "one cache, one cap" wording was false until
  B1015 deleted `SfxManager::cache_`).
- `refactor/B989/audio_ledger_census.tsv` — the citation fixed (the B977 TSV
  is a per-zone TOTALS table with no owner columns; the owner rows are the
  ones committed here) and the shutdown rows added for both legs.
- `docs/reference/music.md` — the SfxManager one-liner (":70-75"): the
  "one decoded-PCM copy" claim is TRUE after the fix — re-checked against
  the code: `SfxManager` holds no `SoundBuffer` at all, `SoundBufferCache`
  is the only holder, and its `getSizeBytes()` is the total resident PCM the
  cap bounds; the section now says so.
- `tests/test_audio_playback_rate.cpp:6-7` — stale comment ("preload" →
  "play").
- `scripts/client_ledger_capture.sh:33-36` — the `au.sfx_cache` note.

The re-derived live numbers (raw logs, re-read this unit):

- B977: `/home/user/kb-evidence/b977-audio/x86/befallen/audio/client.log`
  (hermes-ext; NOT in the repo) — `#npctypespawn` at line 1903
  (2026-10-05 23:03:58.644); steady_60s at line 1838 (23:03:46.166):
  `au.sound_buffers` live=2322320 blocks=73, `au.sfx_cache` live=310640
  blocks=4, TOTAL live=4252448; shutdown at line 2057 (23:04:31.769):
  `au.sound_buffers` live=2322320, `au.sfx_cache` live=723600 blocks=8,
  TOTAL live=4663456. Its `run_meta.txt`: `commit=1b8ebb6f`,
  `lane=172.16.200.99` (lane 1), `zones=qeynos2,qeynos,freportw,gfaydark,befallen,…`
  (13 zones — befallen the 5th).
- B989: the tarball's `live_befallen/befallen/audio/client.log` (in
  `refactor/B989/b989-live-evidence.tar.gz`; also extracted at
  `/home/user/kb-evidence/b1015-one-pcm-cache-really/b989-tar/extract/`) —
  `#npctypespawn` at line 2028 (2026-10-11 02:34:29.086); steady_60s at
  line 1926 (02:34:15.096): `au.sound_buffers` live=2322288, NO
  `au.sfx_cache` line (grep count 0), TOTAL live=3872576, `au.pfs_index`
  live=403184; shutdown at line 2255 (02:35:02.220): identical rows
  (2322288 / 403184 / 3872576). Its `run_meta.txt`: `commit=07f9ea83`
  (rewritten; C++ == `28d14c48` — `git diff 28d14c48 07f9ea83 -- '*.cpp'
  '*.h'` is empty), `lane=172.16.200.97` (lane 3), `zones=befallen`.
- The arithmetic: 3,045,920 = 723,600 + 2,322,320 (B977 shutdown pair);
  723,632 = 3,045,920 − 2,322,288 (the shutdown drop); 2,632,960 =
  2,322,320 + 310,640 (B977 steady_60s pair); 310,672 = 2,632,960 −
  2,322,288; 379,872 = 4,252,448 − 3,872,576 (the audio-category total drop);
  69,232 = 379,872 − 310,640 (the non-sfx_cache part of it); 68,016 =
  471,200 − 403,184 (the `pfs_index` delta, attributed to the legs
  differing — different lanes, different eqemu fleets, different commits —
  not to run-to-run variation of an identical run).

## Not verified / left open

- **Live re-run (offline card).** No live leg this unit: the fix changes
  which PCM is resident under the cap, but the tour plays a small fraction
  of the archive and the live `au.sound_buffers` row was byte-parallel in
  B989 (−32 B) — the duplicate is now impossible by construction
  (`SfxManager` holds no `SoundBuffer` at all: the type system says so),
  and the behaviour tests pin the eviction/pin/insert rules directly.
  Verdicts tagged [measured] here are the local ctest runs and the raw-log
  re-reads; nothing was re-run on a lane.
- **ASan**: not run (the local host's libasan is not exercised by the
  project's build flags, and the pin's safety is asserted structurally —
  the cache entry is the last owner in `APlayingSoundIsNotEvicted`, and the
  pin is the same check T412 shipped and the audio thread has always relied
  on). [assumed]
- **TSan**: not run (host libtsan 13.3.0 aborts under kernel 6.8.0-139-generic
  — the same host limitation B1014 recorded). [assumed]
- `refactor/B989/b989-live-evidence.tar.gz` is untouched, per the card (a
  rule question for the orchestrator).
- The `sfxCacheBytes` payload field is dead in `event_payloads_ui.h` /
  `renderer_draw_types.h` (always 0, one writer, one consumer that ignores
  it) — a one-line deletion each, but the payload is a bridge struct the
  renderer consumes; striking it is a one-MR follow-up, not this card.
