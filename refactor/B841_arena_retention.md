# B841: a byte freed late is not a byte returned — the arena retention, byte-for-byte

**Lane:** x86 vm (this dev box), headless-with-graphics (surfaceless EGL /
llvmpipe), qeynos2. **Issue:** #95. **Status:** MEASUREMENT DELIVERED — the
retention is EXPLAINED and three controlled experiments give numbers. No
shipped default changed: every experiment is env-gated and a run without the
variables is byte-for-byte the shipped behaviour (only `M_ARENA_MAX=1` in
`main.cpp`).

## 1. The defect, reproduced [measured]

The card's premise was measured on the Pi (T496/T500: 17.4 MB freed,
0.09 MB RssAnon fall, slack 31.58 -> 47.67 MB). It REPRODUCES on x86 with
the same shape and the same sign:

| (x86, qeynos2, graphics on) | `ready` | `steady_60s` | delta |
|---|---|---|---|
| `ledger` (live requested bytes) | 328,540,528 | 300,402,896 | **−28,137,632** (−26.86 MB) |
| `RssAnon` | 326,692,864 | 344,322,048 | **+17,629,184** (+16.83 MB — RSS GREW, it did not fall) |
| `malloc_inuse` (glibc) | 328,862,416 | 316,237,152 | −12,625,264 (−12.04 MB) |
| `malloc_slack` (glibc free chunks in the arena) | 23,827,760 | 53,512,864 | **+29,685,104** (+28.30 MB) |
| `keepcost` (top pad) | 8,522,576 | 1,174,656 | −7,347,920 |
| `fordblks` (interior free chunks) | 23,826,256 | 53,512,864 | **+29,686,608** (+28.30 MB) |
| `ordblks` (interior free chunk COUNT) | 21,493 | 22,521 | +1,028 |
| `hblkhd` (mmap bytes) | 29,335,552 | 29,335,552 | 0 |
| `unattributed_pct` | 0.0 | 8.3 | — |

The bytes the ledger freed did not go anywhere: `malloc_inuse` fell 12.04 MB
and `malloc_slack` grew 28.30 MB at exactly the same instants (the difference
is glibc counting chunk overhead + the top pad, which shrank 7.35 MB). RssAnon
actually ROSE 16.83 MB over the window because the renderer kept allocating
new frames/textures; the freed bytes were recycled into new live allocations,
not returned to the OS.

## 2. Where the 28 MB sits: INTERIOR FRAGMENTATION, not the top pad, not mmap

The census answers this directly (both points, run A):

- **Top pad (`keepcost`)**: 8.13 MB at `ready`, **1.12 MB at steady_60s**.
  The top of the heap is NOT where the retained bytes sit — it shrank.
- **mmap (`hblkhd`)**: 27.98 MB at BOTH points, 7 blocks at ready → 7 at
  steady (all ≥ 29,335,552 / 7 ≈ 4.1 MB — the 1.5 MB eqe1 pack mapping plus
  a few large arena mmaps). It did not move.
- **Interior free chunks (`fordblks`)**: 22.72 → 51.03 MB. THE ANSWER.

The by-size-bin census (`[memarena]` line, `bin=chunks/freeBytes`) shows WHERE
the 28 MB of interior free sits at `steady_60s` (run A):

```
2k-4k   = 1776 / 5,175,920   (4.94 MB)
4k-8k   =  647 / 3,496,823   (3.34 MB)
8k-16k  =  780 / 7,762,540   (7.40 MB)
16k-32k =  344 / 7,345,128   (7.01 MB)
32k-64k =   83 / 3,536,419   (3.38 MB)
ge64k   =   79 / 22,627,423  (21.58 MB)
```

~58 MB total free across ~4,000+ interior chunks spanning every bin from 2 KB
to >64 KB, with the 21.6 MB of `ge64k` split over 79 chunks (avg ~292 KB,
max is the biggest freed arena region). This is classic **interior
fragmentation**: live allocations sandwich the free chunks, so
`malloc_trim(0)` — which can only release from the TOP of the heap —
structurally cannot return a single one of them to the OS. That is the whole
defect in one sentence: a byte freed late lands in an interior bin, and the
arena keeps it.

**M_MMAP_THRESHOLD (dynamic):** reported `mmap_thr=0` (unknown) in the census
because glibc 2.39 has no read-back (`get_mallopt()` is ≥ 2.40; `mallopt(o,0)`
SETs). Derived: it was never pinned here, so it started at 128 KiB and
doubled on large frees that couldn't trim the top; the 29 MB / 7-block mmap
population at both points is the residue of that drift. When the
`B841_MMAP_THRESHOLD` experiment pins it, the census now reports the exact
value (`mmap_thr=131072` in run B) [measured].

## 3. Experiments (each a separate live run, qeynos2, graphics on)

| | A baseline | B `B841_MMAP_THRESHOLD=131072` | C `B841_TRIM_THRESHOLD=8192` | D `B841_TRIM_READY=1` |
|---|---|---|---|---|
| **RssAnon @ steady_60s** | 344,322,048 | 343,216,128 | 342,396,928 | 345,268,224 |
| **malloc_slack @ steady_60s** | 53,512,864 | 68,204,400 | 43,590,032 | 54,051,456 |
| malloc_inuse @ steady_60s | 316,237,152 | 300,857,488 | 323,952,240 | 316,472,704 |
| keepcost @ steady_60s | 1,174,656 | 132,320 | 112,256 | 826,688 |
| hblkhd (mmap) @ steady_60s | 29,335,552 | 66,285,568 | 66,019,328 | 29,335,552 |
| trim_pages returned | 0 | 0 | 0 | 1 (at ready) |
| **Frame Time (mean, over the run)** | 16.0 ms | 16.0 ms | 16.0 ms | 16.0 ms |

- **(a) `malloc_trim(0)` once at `ready`** (D): returned **1 page** (4 KB) —
  `trim_pages=1`, keepcost 8.13 MB → 0.00 MB at ready. Steady-state unchanged:
  the 28 MB of interior free is untouched by construction. **No frame-time
  cost** (16.0 ms mean, identical to A).
- **(b) Fixed `M_MMAP_THRESHOLD=128 KiB`** (B): 140 blocks / 64.2 MB moved
  into mmap (vs 7 / 28.0 MB baseline) — frees ≥ 128 KiB now unmap. But
  `malloc_slack` GREW to 68.2 MB (the small interior free stayed) and RssAnon
  was 1.1 MB LOWER at steady (343.2 vs 344.3 MB). Net: it moves bytes from the
  brk arena to mmap, it does not return the interior free. **No frame-time
  cost.**
- **(c) `M_TRIM_THRESHOLD=8 KiB`** (C): the top pad stays small
  (keepcost 0.07→0.11 MB, `malloc_slack` lowest of all at 41.6 MB) but RssAnon
  (342.4 MB) is still within 2 MB of baseline — the trimmable top pad is < 1 MB
  and the interior free is untouched. **No frame-time cost.**

## 4. Recommendation (for the orchestrator / Will, after the Pi confirms)

**Do NOT ship any of the three knobs as a default.** The measurement says:

1. The 28 MB of `malloc_slack` growth between `ready` and `steady_60s` is
   **interior fragmentation** (bins 2 KB..ge64k, ~4,000 chunks, 21.6 MB in
   ge64k alone). It is NOT the top pad (trimmable — and the top pad is only
   ~1 MB at steady) and NOT mmap drift. `malloc_trim()` returns 0 pages;
   lowering `M_TRIM_THRESHOLD` only saves the sub-MB top pad; pinning
   `M_MMAP_THRESHOLD` only re-shuffles between brk and mmap.
2. **The only bytes that move RSS are the two the card already names:** a byte
   NEVER ALLOCATED, or a byte freed BEFORE the peak. The 28 MB is neither —
   it was allocated and freed AFTER the peak, so the correct fix is at the
   ALLOCATION site (don't hold the 28 MB of transient zone/model/texture data
   live across the frame), not at the allocator. Point the next memory unit at
   the `game_state` + `entity_models` + `textures_*` categories that own the
   live bytes, not at `main.cpp` mallopts.
3. Keep the three knobs where they are (env-gated, off by default) — they are
   the controlled version of the dynamic-threshold question and cost nothing at
   16.0 ms mean frame time. Re-run all four on the Orange Pi before any
   decision, per the card ("the orchestrator decides after the Pi confirms").

## 5. Instrumentation (what landed on this branch)

- `include/client/core/memory_ledger.h`: `ArenaReport` (mallinfo2 + per-bin
  free-chunk census), `arenaNow()`, `recordArena(point, trim)`,
  `parseMallocInfo()` (split out from `arenaNow` so a fixture can prove the
  parser), `arenaExperiments()` (the three env knobs).
- `src/client/core/memory_ledger.cpp`: the census (mallinfo2 +
  `malloc_info(0, f)` XML → per-bin fold), the `recordArena` at `ready`
  (wired from `bridge_component.cpp` `LoadingPhase::COMPLETE`) and
  `steady_60s` (wired from `pollSteadyState`), the `malloc_trim(0)` at ready
  when `B841_TRIM_READY=1`, and the `mmap_thr` field (reports the pinned value
  when the experiment is on, else 0=unknown).
- `src/client/main.cpp`: the two startup `mallopt`s, env-gated, beside the
  existing `M_ARENA_MAX=1`.
- `tests/test_memory_ledger.cpp`: 6 new `ArenaCensus.*` tests (fixture parser,
  live census agrees with `mallinfo2`, bin boundaries, malformed input). The
  fixture test CATCHED A REAL BUG in the parser (a char-literal `find('<size')`
  searching for a single byte) before it shipped — RED first, then GREEN.

**Shipped-default delta: zero.** `B841_TRIM_READY` / `B841_MMAP_THRESHOLD` /
`B841_TRIM_THRESHOLD` unset → `main.cpp` runs the existing `M_ARENA_MAX=1`
only; `recordArena` adds a census log line at `ready`/`steady_60s` (the
instrumentation this card asked for) but no `malloc_trim`, no `mallopt`.

## Raw logs (this box)

- Run A: `/tmp/b841_gfx_a.log` — `[memarena]` + `[memledger]` at ready/steady_60s
- Run B: `/tmp/b841_gfx_b.log`
- Run C: `/tmp/b841_gfx_c.log`
- Run D: `/tmp/b841_gfx_d.log`

Tour: `data/captures/b841_qeynos2_tour.txt` (`/gmzone qeynos2 303` + 80 s hold
+ `/camp`). Config: `data/captures/b841_oraclealpha_willeq.json`.
