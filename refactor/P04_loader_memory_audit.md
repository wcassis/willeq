# P04 — EQE1 loader fix + full sequential-loader memory audit

Will's directive (2026-07-24, decision recorded in docs/decisions.md):
ETC1 stays; fix the loader; and audit the ENTIRE sequential loader path so
no unneeded resources persist in memory. Historical signal: `/pmem`
(runPmemDiagnostics, eq_commands.cpp) has shown a large amount of UNTRACKED
RSS — memory not attributable to the known pools.

## Part 1 — Loader fix (the P03 finding)

The v1 EQE1 loader heap-loads entire pack files (eqe1_loader.cpp),
including global archives never sampled in the zone (P03 measured 139MB of
never-referenced archive bytes resident on a 494MB board — the confirmed
cause of the EQE1 stall bursts and the leading explanation for the P01
panic via memory pressure).

Fix design (verify feasibility against the format first — E01's format doc
and eqe1_format.h):
1. **mmap the pack files** instead of whole-file heap reads; entry payloads
   are consumed via pointers into the mapping (upload reads them once;
   page cache handles residency; madvise(MADV_DONTNEED/SEQUENTIAL) after
   upload of each entry, or unmap per-archive after its entries upload).
2. **Open/map only referenced archives**: resolve which archives the zone
   actually references before mapping (resolveEQE1Archives + the
   name-lookup index); global archives open lazily on first lookup miss.
3. Preserve v1 format compatibility and the v2 (P03) payload paths; ETC1
   upload semantics byte-for-byte unchanged.

## Part 2 — Sequential loader memory audit (loadZoneSequential steps 0-13)

The G01 decomposition gives a clean audit surface: 20 step methods +
ZoneLoadContext in renderer_zone_load.cpp.

1. **Instrument**: flag-gated per-step memory sampling (RSS via
   /proc/self/status VmRSS, plus the known pools: ConstrainedTextureCache/
   MeshCache bytes, EQE1 mapped bytes, entity prep buffers) logged at each
   step boundary — a `--load-mem-audit` mode on willeq/benchmark. Also
   extend /pmem (runPmemDiagnostics) to print the same accounted-vs-RSS
   breakdown so the "untracked RSS" gap becomes a number with named
   suspects.
2. **Audit each step** for resources that persist past their need:
   decode/staging buffers not freed after GPU upload, WLD/S3D parse
   artifacts retained, ZoneLoadSnapshot lifetime, prep-worker results kept
   after distribution, pendingZoneComputations_ contents,
   RaceModelLoader/EquipmentModelLoader caches vs their budgets, glibc
   arena bloat (malloc_trim consideration — measure before adding).
3. **Fix what the audit finds** — each fix separately justified in the
   review with before/after RSS numbers. Behavior bar: full suite + visual
   gate + replay oracle green; zone loads still work end-to-end (benchmark
   load phase + willeq headless zone-in via the replay/tour machinery).
4. **Quantify the win on the Pi**: post-fix load-phase RSS curve + steady
   RSS for the three golden zones, EQE1 stall-burst check (expect 8/8
   burst-free now even with the full production pack dir — THE acceptance
   test: P03's confound becomes the proof), CmaFree/page-cache behavior.
   P02 protocol applies (interleaving, caps, /tmp I/O, reboots).

## Deliverables

- The accounted-vs-untracked RSS breakdown, before and after, per step.
- Loader fix with Pi-verified stall elimination on the FULL pack dir.
- Fix list with per-fix RSS deltas.
- Updated /pmem output documented (it's a user-facing diagnostic Will uses).

## Review

### Part 1 — Loader fix (mmap + referenced-only opening)

**mmap loader** (`eqe1_loader.cpp/.h`): `EQE1Loader::open()` now mmaps the
pack read-only (`MAP_PRIVATE` + `MADV_SEQUENTIAL`) instead of reading the
whole file into a heap `std::vector`. Entry pointers point into the mapping
(same pointer-stability contract as E04 required of the heap buffer — new
tests below prove it); `close()` munmaps. Backing pages are page-cache
resident (clean, reclaimable under pressure) instead of pinned anonymous
heap. `getMappedBytes()` added for the instrumentation and /pmem.

**Referenced-only opening** (`renderer_zone_load.cpp` Step0,
`renderer_effects.cpp`, `constrained_texture_cache.*`): Step0 still opens
the zone archives (`{zone}`, `{zone}_obj`, `{zone}_chr`) eagerly but no
longer opens the ~33 global archives. They are queued
(`pendingGlobalEQE1Archives_`) and opened one at a time by
`tryOpenNextGlobalEQE1()` only when a `ConstrainedTextureCache` lookup
misses against everything already open — wired via
`setEQE1LazyOpener()`. `findCompressedEntry`/`getEQE1RawData` retry after
each lazy open until found or the list is exhausted (so the LOG_FATAL
missing-texture behavior is preserved: it fires only after every configured
archive has been tried). Teardown resets the queue alongside
`eqe1Loaders_`.

**Thread-safety hardening** (pre-existing race, made hot by lazy-open):
`eqe1Loaders_` in the cache was appended by prep-worker threads via
`addEQE1Loader` with no lock while the render thread iterated it. Now
guarded by a dedicated `eqe1LoadersMutex_` (not `mutex_`, which the lazy
opener re-enters on the same thread); readers snapshot the pointer vector
per lookup round.

**Format compatibility**: v1 and v2 parse/dispatch logic is untouched — only
the backing storage changed. `uploadCompressedETC1()`/`uploadCompressedV2()`
not modified at all (ETC1 upload semantics byte-for-byte unchanged).
Evidence: visual gate PASS (golden frames exact), `verify_eqe1 --all` 9/9 on
both v2 directories (`EverQuest_RGB565`, `EverQuest_RGBA8888`) and OK on
production ETC1 packs, `test_eqe1_loader` 13/13 (11 from P03 + 2 new mmap
tests: payload bytes via the mapping match the file bytes at the index
offset, and pointers survive moves; `getMappedBytes` == file size while
open, 0 after close).

### Part 2 — Audit instrumentation

- `--load-mem-audit` flag on **willeq** (`ApplicationConfig.loadMemAudit` →
  `ConstrainedRendererConfig.loadMemAudit`) samples after every
  `loadZoneSequential` step (00_start, 00_EQE1, 01_S3D … 13_Cleanup) and on
  **gles2_zone_benchmark** (same flag) at its load steps (bench_00_start,
  bench_02_s3d, bench_04b_eqe1, bench_07_meshes, bench_11_entities,
  bench_99_done). Shared emitter: `src/client/graphics/load_mem_audit.cpp`.
  One greppable line per step:
  `[LOAD-MEM-AUDIT] step=… rss_kb=… eqe1_mapped=… texcache=… meshcache=…
  entityprep=… accounted_kb=… untracked_kb=…`
  (printf to stdout, deliberately not LOG_INFO: the default NONE log level
  silently swallowed samples in benchmark runs; the lines only exist under
  the explicit flag). Note `eqe1_mapped` counts **mapped** bytes (an upper
  bound on resident contribution — the mapping is page-cache backed), so
  `untracked_kb` can go negative right after archive open; that is the
  fix working, not an accounting bug.
- entityprep column is currently always 0 — the prep-worker buffers have no
  stable byte-count accessor; left as a named gap rather than invented.

### The headline table — per-step accounted-vs-untracked, before vs after

x86 GLES2 benchmark, FULL production pack dir (`/mnt/projects/EverQuest_ETC1`,
the P03 confound setup), `--frames 5 --warmup 0 --load-mem-audit`, RSS in KB.
"Before" binary = this tree with only the loader fix reverted (instrumentation
retained; its eqe1 column reads 0 because the v1 loader had no mapped-bytes
accessor — its archive bytes are exactly what lands in `untracked`):

| step | qeynos2 before → after | gfaydark before → after | befallen before → after |
|---|---|---|---|
| 00_start | 7,336 → 7,380 | 7,508 → 7,548 | 7,416 → 7,396 |
| 02_s3d | 111,840 → 112,016 | 107,864 → 107,736 | 102,684 → 102,352 |
| 04b_eqe1 | **223,572 → 113,984** | **218,084 → 109,732** | **210,040 → 104,284** |
| 07_meshes | 250,628 → 144,140 | 248,004 → 138,156 | 238,504 → 131,428 |
| 11_entities | 251,364 → 144,812 | 250,764 → 140,924 | 238,900 → 131,888 |
| 99_done (final) | **251,364 → 144,812** | **250,764 → 140,924** | **238,900 → 131,888** |
| peak (time -v) | 271,204 → 164,620 | 250,600 → 140,804 | 258,572 → 151,632 |

- **The loader fix removes 107-110 MB of resident memory** on every golden
  zone (EQE1 archive open: +112 MB RSS before, +2 MB after).
- **The historical "untracked RSS" mystery is solved**: before the fix the
  EQE1 open step alone contributed 110-112 MB to the untracked gap (heap
  bytes no pool reported). After the fix, untracked at steady state is
  ~21-24 MB (glibc arenas, Irrlicht/driver internals, misc heap) — the gap
  went from "most of RSS" to a plausible residue.

### Audit findings and fixes (each with evidence)

Audit method: full read of the G01 step methods + the named suspects list;
per-finding measurements as cited.

1. **EQE1 whole-pack heap loading** (Part 1, above): −107 to −110 MB final
   RSS per zone (table above). THE dominant finding.
2. **Game-side zone sources retained for the whole in-zone duration**
   (`EverQuest::m_zone_archive`, `m_zone_wld`): after `LoadZoneSources`
   extracts the BSP tree (a self-owned `shared_ptr` — `zone_bsp.h`),
   neither is read again anywhere (verified by grep across src/client), yet
   both stayed alive until zone-leave. `PfsArchive::open` stores the
   compressed bytes of *every* archive entry in `files_`; the WldLoader
   keeps all parse artifacts (the game-side instance never gets
   `releasePostLoadData()`). Measured by replicating the exact
   LoadZoneSources sequence (standalone harness linking the production
   pfs/wld/zone_bsp objects), RSS deltas after freeing each:
   | zone | archive freed | wld freed (BSP kept, verified live) | total |
   |---|---|---|---|
   | qeynos2 | −5,848 KB | −1,684 KB | −7.5 MB |
   | gfaydark | −8,448 KB | −4,288 KB | −12.7 MB |
   | befallen | −6,092 KB | −2,332 KB | −8.4 MB |
   **Fix**: `LoadZoneSources` now resets both immediately after BSP
   extraction (eq_zone.cpp).
3. **ZoneLoadSnapshot never released after load completion**
   (`Application::m_zoneLoadSnapshot`): held its entity/door/zone-line
   copies (plus, before fix 4, shared_ptr refs pinning finding 2's data)
   until the *next* zone's snapshot overwrote it. **Fix**: cleared in the
   game thread right after `OnGraphicsComplete()` (the loading thread is
   already joined before the completion flag is set, so nothing can still
   read it). Size is entity-count dependent (vectors of Entity/DoorState);
   the structural half of the win is that it no longer extends finding 2's
   lifetime indefinitely.
4. **Dead snapshot fields** `ZoneLoadSnapshot::zoneArchive`/`zoneWld`:
   written in `CreateZoneLoadSnapshot`, read nowhere (the sequential loader
   parses its own S3D in `loadZoneStep1_S3D`). Removed. This is what made
   fix 2 safe — the "shared with sequential loader" comment was stale.
5. **PendingZoneComputations**: the audit found the big fields are already
   moved out step-by-step (G01 did this well); three items were not:
   `zoneAtlasPreload`/`objAtlasPreload` and `prebuiltMeshCache` are **dead
   fields** (never populated or read; vestiges of the pre-E10 atlas path) —
   removed; `equipmentIndex` and `weatherConfig` wrappers are now `reset()`
   at their consumption sites (steps 5 and 10, matching the existing
   `skyLoadData` precedent) instead of surviving to step 13. Bytes: small
   (wrappers were already gutted by moves); justified as lifetime hygiene,
   not RSS.
6. **Non-findings** (checked, no action): prep-worker results are already
   cleared per entity iteration (`ctx.equipmentData.clear()` at 8d entry;
   8e's rawTextures copy is shared_ptr refcounts, not pixel duplication);
   WldLoader post-load artifacts on the *renderer* side are already trimmed
   by `releasePostLoadData()` in step 13 with the documented mesh-cache
   retention set; RaceModelLoader/EquipmentModelLoader have refcount-driven
   mesh eviction plus the `chrCacheMaxEntries` LRU cap.
   `malloc_trim`: measured in the finding-2 harness (deltas above are
   post-trim); NOT added to production — the freed blocks return to glibc
   regardless and the harness showed the arenas release fine at these block
   sizes without it.
7. **Follow-ups flagged, not fixed** (would need their own evidence):
   `RaceModelLoader::armorTextureCache_` map entries are never erased (raw
   bytes are released elsewhere, but entry count grows for process life);
   `PfsArchive` has no partial-eviction (moot for the zone archive after
   fix 2, still relevant for `_chr`/`_obj` archives held by loaders);
   entityprep bytes accessor for the audit column.

### Pi verification (P02 protocol: interleaved, capped, /tmp I/O, scripted reboot, CmaFree telemetry)

**THE acceptance test — full production pack dir, stall-burst check**:
24 randomized/interleaved invocations (8 per golden zone, seed 404), full
`/mnt/projects/EverQuest_ETC1` (the exact setup that stalled 3/3 warm in
P03), 60 frames each, session cap 12 with scripted reboot mid-run.
Steady-state per-invocation p95 (total_ms, warmup pose-cycles discarded):

| zone | per-rep p95 range (8 reps) | cross-rep p95 spread | verdict |
|---|---|---|---|
| qeynos2 | 30.43-30.59 ms | 0.157 ms | 8/8 burst-free |
| gfaydark | 38.87-39.14 ms | 0.273 ms | 8/8 burst-free |
| befallen | 42.65-42.83 ms | 0.186 ms | 8/8 burst-free |

All three zones ≤0.28 ms spread — identical to P03's zone-only-directory
result. **P03's confound is now the proof: the loader was the instability.**
CmaFree stayed in the 67-102 MB band the whole session, no monotonic
ratchet, no abort. (Data: `benchmarks/pi_runs/p04_acceptance/`.)

**Post-fix load-phase RSS curve + steady RSS on the Pi** (full pack dir,
`--load-mem-audit`, `benchmarks/pi_runs/p04_rss_curve/`), RSS in KB:

| step | qeynos2 | gfaydark | befallen |
|---|---|---|---|
| start | 3,564 | 3,436 | 3,476 |
| s3d | 75,788 | 70,420 | 67,636 |
| eqe1 open (26 archives, 113-120 MB mapped) | 77,744 | 72,352 | 69,592 |
| meshes | 94,676 | 91,472 | 92,176 |
| steady/done | **95,296** | **94,168** | **92,528** |

Steady RSS ~92-95 MB on the 494 MB board; archive open costs ~2 MB resident
(vs ~110 MB heap before — which is why warm full-dir runs used to thrash).
Steady p95s in this session (30.48/38.95/42.72) match the acceptance run.
Pi left pristine: per-run /tmp files removed after each pull, staged binary
removed, governor restored, CmaFree stable at session end.

### /pmem changes (user-facing)

`getMemoryReport()` (renderer_diagnostics.cpp), visible in-game via `/pmem`
(aliases `/memory`, `/mem_report`), chat + log output:

1. New Graphics section line, immediately after `[Texture Cache]`:
   `[EQE1 Flat Packs] <N> MB mmap'd (<K> archives open, mmap/page-cache backed)`
   — this pool was previously invisible (it *was* the untracked gap).
   Counted into the tracked total.
2. Final line renamed to name the concept:
   `--- Tracked: <T> / RSS: <R> (<pct>%, untracked: <gap>) ---`
   (was `gap:`). With the EQE1 pool now tracked and the loader fixed, the
   untracked number is small enough to mean something; remaining suspects
   are documented at the code site (glibc arenas, driver internals,
   not-yet-named staging buffers).
3. The same accounted-vs-untracked breakdown is available per load step via
   `--load-mem-audit` (same arithmetic, same pools), so a /pmem reading can
   be correlated with the load-phase curve.

### Verification summary (production-change bar)

- Full ctest: 1225 tests, green except two **pre-existing** flakes, both
  re-verified against the unmodified fable tree on this host:
  `AudioManagerTest.PreloadCommonSounds` (the ALSA flake documented in P03)
  and `RDPServerFrameTest.UpdateFrame*` (intermittent SEGFAULT ~1-2/10 runs
  on the **pre-change** build as well; not P04-related).
- Replay oracle: `SessionReplayGolden` PASS against the committed recording
  (`data/captures/auto_session.sleq`).
- Visual gate: PASS (x86 GLES2 build via `scripts/build-gles2-x86.sh`).
- `test_eqe1_loader`: 13/13 (11 from P03 + 2 new mmap tests).
- `verify_eqe1`: 9/9 on both v2 pack dirs; production ETC1 packs OK.
- ARM cross-compile from this worktree: clean (`build-arm-noble/`).

### Deviations

- **The in-run stall detector I first scripted was wrong** (pooled warmup
  frames + cpu_ms across heterogeneous poses, flagging deterministic
  pose-cost variance as "bursts"). The reported verdict uses the protocol's
  own steady-state summarizer (`summarize_csv`, warmup pose-cycles
  discarded) and P03's cross-invocation-p95-spread criterion. Raw CSVs are
  in the run dirs for re-analysis.
- **willeq's per-step audit table was produced from the benchmark, not a
  live willeq zone-in**: exercising `loadZoneSequential` end-to-end needs a
  server session (akk-stack was out of scope for this unit), and the replay
  oracle drives game state, not the renderer. The willeq `--load-mem-audit`
  instrumentation is in place and flag-gated for the next live session; the
  benchmark path exercises the identical loader/cache/EQE1 code where the
  finding lived, and findings 2-4 were measured with a standalone harness
  replicating the exact production call sequence against the production
  objects.
- **audit output is printf, not LOG_INFO** — deliberate; rationale in the
  instrumentation section and at the code site.
- The `entityprep` audit column is a named zero (no accessor yet) rather
  than a fabricated estimate.
