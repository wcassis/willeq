# B1013-1 (#334): Lima BO-cache pool timeline — tool-only counters + cost-sheet fact 13

Card 1 of the four B1013 cards. Tool work on x86 only: NO Pi run, NO bless, NO
golden, NO budget, NO eqemu lane. The investigation this card turns into tool
support is `review/B1013/README.md` (orchestrator host; the card body carries
the facts and numbers — every measurement below cites its file or output).

## The mechanism (from the investigation, carried verbatim in substance)

Mesa 25.2.8 lima, the Pi's shipped prefix (commit f97d7dc):

- `lima_bo_create` rounds the size up to a page (`lima_bo.c:284`), then
  `lima_bo_cache_get` (`lima_bo.c:234-277`) walks the bucket from its HEAD
  (the OLDEST free BO), takes the first entry big enough, and if that entry is
  still busy (a timeout-0 `lima_bo_wait` that fails) it `break`s and allocates
  a NEW BO (`lima_bo.c:253-259`).
- `lima_bo_cache_put` (`lima_bo.c:203-232`) appends the BO to the bucket's
  TAIL with a fresh `free_time`, then `lima_bo_cache_free_stale_bos`
  (`lima_bo.c:165-181`) frees from the time list's head only while
  `time - free_time > 6` s — a bucket of N BOs shrinks only when it serves
  fewer than N/6 allocations per second.
- `lima_job_start` (`lima_job.c:242-273`) unreferences every BO right after
  the submit ioctl, so a BO goes back into the cache while the GPU is still
  using it (the head of a bucket is idle, the tail busy).
- `lima_bo_wait` (`lima_bo.c:523-542`) returns `drmIoctl(...) == 0`: for a
  timeout-0 POLL, ioctl return 0 = the cached BO is idle and is REUSED (a
  hit); non-zero = busy (the cache then creates a new BO).

=> every bucket holds the PEAK number of BOs in flight since the last trim;
the pool set in the 1-3 s zone-in burst after `ready` stays fixed while the
client keeps drawing. Steady-state levers (draw count, stride, cull, LOD,
batching) buy ~0 MiB of the guard. This is cost-sheet **fact 13** (added by
this card, `docs/reference/lima_cost_sheet.md`).

## The fixture (tests/data/b1013/, committed in step 1)

Source: the B997 shipped-default Pi leg (ARM build 593eba6d, lane 2,
imp200far, `--idle 180 --hold-after 75`, `--bo-census`),
`/home/user/kb-evidence/b997-draw-count/final-pidefault-20261010T141840Z/qeynos2/imp200far/zones/qeynos2/`
(copied by `/home/user/kb-evidence/b1013-1-pool-timeline-counters/copy_fixture.sh`):

| committed file | source | bytes | sha256 |
|---|---|---|---|
| `qeynos2_default_bo_events.log` | `bo_events.log` whole | 190711 | `0bda1732...67122` |
| `qeynos2_default_gem_owners_ready_steady.tsv` | `gem_owners.tsv` cut lines 66552-70279 (the final `P ready` block through `E steady_60s`) | 231514 | `c2494b41...85f8` |

Both digests are pinned by `test_pool_timeline.py`'s
`test_fixture_bytes_are_pinned`. **Digest note:** the card's `bo_events.log`
digest carried one extra `a` (65 hex chars, not 64); the file's real digest is
the 64-char value in the pin, re-derived from the source copy by
`/home/user/kb-evidence/b1013-1-pool-timeline-counters/check_hash.py`. Every
other card value matched the files exactly.

## Expected values (asserted EXACTLY by `test_pool_timeline_qeynos2_default`)

Pool = the owner row `lima: draw-time BOs (shader binaries, varyings in
flight, PP streams)` + every owner row whose name starts `mesa: stream
uploader` (only `mesa: stream uploader (1 MiB)` is present at these two
points):

- `ready` (mono 2740370.512877): draw-time 5292032 B / 1051 BOs; stream
  uploader 3 BOs = 3145728 B.
- `steady_60s` (mono 2740430.695843): draw-time 9392128 B / 1852 BOs; stream
  uploader 7 BOs = 7340032 B.
- `gem_pool_growth_mib` = ((9392128 + 7340032) − (5292032 + 3145728)) /
  1048576 = 8294400 / 1048576 = 7.91015625 → written `7.91` (2 dp).
- `gem_pool_last_create_s` = 2740381.257002 − 2740370.512877 = 10.744125 s
  (bo_events.log line 4026, `2740381.257002 C 2293 188416 0 fd10`) → written
  `10.744` (3 dp). The column counts the last GEM create of ANY size in
  (ready, steady_60s] — not the README's small+1 MiB subset (+10.0 s).
- `gem_pool_creates_steady_window` = GEM creates in [ready+20 s, steady_60s]
  = [2740390.512877, 2740430.695843] = 0; closes in that window = 0.

Cross-checks asserted in the same test: creates in (ready, steady_60s] = 843
= 3157 − 2314 (the two P lines' `created` field); closes = 15 = 881 − 866
(`closed`) — the event log and the dump agree.

TRAP (from the card, written into the column comment and the test): the
README's "0 creates from ready+17 s until shutdown" is NOT literally true of
this leg — closes run until 2740389.009650 (+18.497 s) and 5 creates of
24-213 KiB land at 2740556.77-2740557.26 (+186 s, after steady_60s). The
STEADY WINDOW [ready+20 s, steady_60s] is clean IN THIS LEG (final-pidefault
qeynos2, the committed fixture), and the column is defined on exactly that
window — never widened to shutdown. Across the 10 B997 Pi legs the window is
clean in every qeynos2 leg (5 of 5) but NOT in every leg: every gfaydark leg
(5 of 5) and both baseline legs carry small nonzero events inside it (cost
sheet fact 13's per-leg census: final-pidefault gfaydark 1 create, picensus
gfaydark 1 create + 4 closes, pisgbatch-sg0 gfaydark 8 closes,
pisgbatch-sg1 gfaydark 1 close, qa-pidefault gfaydark 1 create, baseline
161526 qeynos2 6 creates + 4 closes / gfaydark 2 creates + 10 closes,
baseline 164216 qeynos2 1 create / gfaydark 5 closes). Those are small, late
trims — not pool growth: the growth numbers below hold per leg, and the
frozen-pool mechanism stands.

## Step 5: the F block (per-frame burst counters)

`scripts/pi/lima_bo_census.c` mode 4. Env: `B1013_FRAME_MARKER` (default
`ready`) and `B1013_FRAME_WINDOW` (default 600, clamped [1, 600]). A
`willeq_gem_dump(point)` with `point == marker` ARMS a window at row 0
(re-arming on every marker: the rig's last `ready` is the one that matters);
the swap seals the row and advances the index — row i counts the work AFTER
swap i, up to and including swap i+1. An armed window with sealed rows is
flushed at the NEXT `willeq_gem_dump` call (any point) or the next marker,
BEFORE that call's P line:

```
F	<marker>	<frame>	<creates>	<poll_hit>	<poll_busy>	<gp_submits>	<pp_submits>
...
FE	<marker>	<frames_recorded>
```

- `<creates>`: LIMA_GEM_CREATE that succeeded (observed in `observe_ioctl`).
- `<poll_hit>` / `<poll_busy>`: LIMA_GEM_WAIT with `timeout_ns == 0` whose
  ioctl returned 0 / non-zero (observed in `observed_ioctl`, where the poll
  was previously passed through uncounted — only `timeout_ns != 0` was
  timed).
- `<gp_submits>` / `<pp_submits>`: DRM_IOCTL_LIMA_GEM_SUBMIT per pipe.

Rows are a STATIC array of 600 (no malloc: the header's rule — the client's
allocator is the memory ledger); the row field is bumped with
`__atomic_add_fetch` and `b1013_row()` reads armed/rows as relaxed atomic
loads (the card's "atomics or g_lock" rule). The P/B/E formats are unchanged
(the B1006 13-field P line stays pinned by the self-test's first-block
checks, now scoped to block 1). The unsealed final row of a run (work after
the last swap, no closing swap) is dropped. `read_frames()`
(`scripts/pi/gem_owner_table.py`) parses the blocks: the FE is the block's
own checksum (a count or marker mismatch, an F line right after its FE, or an
FE with no F lines raises `ValueError`); a pre-B1013 dump is `[]`, and
`read_dump` keeps skipping F/FE as its docstring already said.

**RED (the self-test half, spliced onto the pre-step-5 shim):** the drive
(arm at `ready`, frame 0: 2 creates + 1 hit poll + 1 busy poll + 1 GP + 1 PP
submit; frame 1: none; frame 2: 1 busy poll; dump `after`) + the extended
re-read (3 F rows, the FE, cumulative P counters) exist, but the shim does
not emit F/FE:

```
FAIL line 669: fe_count == 1
```

(exit 1; the splice is /tmp-side — `/tmp/retry/red/lima_bo_census.c` on the
worker host, built with the same `gcc -O2 -Wall -Wextra
-DB962_SHIM_SELFTEST` as the ctest target).

**GREEN** (`ctest -R LimaBoCensusShim`, rebuilt after the last edit):

```
lima_bo_census selftest OK: live=1502 bytes=35217408 created=1003004 closed=1001500 frows=3
    Start 389: LimaBoCensusShim
1/1 Test #389: LimaBoCensusShim .................   Passed    0.31 sec
```

The last P line carries the cumulative B1006 counters including the window's
work (frames 4+3=7, gp 2+1=3, pp 3+1=4; the window used timeout-0 polls, so
waits stays 1).

**Python unit tests** (`PoolTimeline` ctest, `test_read_frames_*`): a
synthetic F block interleaved with P/B/E (the self-test's exact three rows),
the four corruption cases, two well-formed blocks across a re-arm, a missing
file, and a pre-B1013 dump. `read_dump` over the synthetic file is pinned
too (order, point meta — the F/FE lines do not leak into it).

```
    Start 391: PoolTimeline
3/4 Test #391: PoolTimeline .....................   Passed    0.14 sec
```

## Step 6: EQT_DRAW_CENSUS_WINDOW

ONE parse rule, in the client header
(`include/client/graphics/draw_census.h drawCensusWindowFromEnv`): null,
empty, non-numeric, 0 or negative → 120; otherwise clamped to [1, 100000].
`DrawCensus` carries the parsed `window`; `drawCensusEndFrame()` (still ONE
statement in `renderer_frame_render.cpp` — G05 untouched) pushes it into the
driver ONCE (function-local static, before the first totals read) through the
new bridge `gles2SetDrawCensusWindow(void* driver, unsigned long frames)`
(`COpenGLES2Driver.cpp`, beside `gles2GetDrawCensusTotals`). The driver's
`B997DrawCensus` gets `window = 120` and its boundary test becomes
`c.emitted % c.window == 0` (guarded by `c.window > 0`). The driver never
parses env itself, so the client's `[drawsite]` and the driver's
`[drawcensus]` can never disagree on the window size. The stale
"120-frame" comments are corrected in both files.

**RED:** `tests/test_draw_census_window.cpp` (2 gtest cases covering the 8
card-mandated values plus 4 extra edges — null/""/"0"/"-5"/"abc"/"12x" → 120;
"1" → 1, "600" → 600, "999999999"/"100000"/"100001" → 100000) does not
compile before the helper exists:

```
tests/test_draw_census_window.cpp:21:32: error: ‘drawCensusWindowFromEnv’ was not declared in this scope
...
gmake[1]: *** [Makefile:1297: test_draw_census_window] Error 2
```

**GREEN** (rebuilt after the last edit):

```
[  PASSED  ] 2 tests.   (DrawCensusWindow.DefaultsAndRejection, DrawCensusWindow.Values)
```

## Local net (step 8)

All targets rebuilt after the last edit, then:

- `ctest -R 'LimaBoCensusShim|PoolTimeline|GemOwnerTable|RenderedZoneSummarizeGlDriver|DrawCensusWindow'`: 6/6 passed (`DrawCensusWindow.*` discovered via `GTEST_DISCOVER_TESTS`).
- `ctest -L hygiene`: 29/29 passed.
- `python3 scripts/pi/rendered_zone_summarize.py --help`: exit 0 (import check).
- The shared `libIrrlicht.a` was rebuilt from the changed driver source with
  `scripts/build-gles2-x86.sh --irrlicht-only` (stamp `d188e71b...`, the
  `gles2SetDrawCensusWindow` symbol present in the lib's
  `COpenGLES2Driver.o`) and `willeq` links against it.

The CI test-job trace lines (`GemOwnerTable`, `LimaBoCensusShim`,
`DrawCensusWindow*` Passed + the build job's `stamp MISMATCH`/lib-build
lines) are pasted in the completion summary after the pipeline.

## Not verified / left open

- **F (frames in flight) is unmeasured** — that is card 2 (the Pi leg),
  which can now measure it with `B1013_FRAME_WINDOW` and
  `EQT_DRAW_CENSUS_WINDOW=1`. No Pi run in this card.
- The per-pixel zone shader's varying slot count (used in fact 5's
  "~85 % of the draw-time BOs are one page") is from the investigation's
  linker-source reading, not a compile on this host.
- The 6.87 MiB "cache-off floor" #334 cites has no source in the tree; B986
  gives 3.7 / 4.1 MiB. Not used by this card.
- The F/FE flush is at the NEXT `willeq_gem_dump` of ANY point — the rig's
  dump cadence decides how often rows are recorded; the window (default 600
  frames) bounds memory, not flush latency.
- CI: the driver change compiles inside the patched Irrlicht rebuild
  (`scripts/ci/irrlicht_input_stamp.sh` sees a changed device source); the
  `stamp MISMATCH` + lib-build lines are pasted in the completion summary.
