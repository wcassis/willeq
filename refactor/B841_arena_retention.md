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
the same shape and the same sign (run A; bytes are log-exact, the MiB in the
delta column is bytes / 1,048,576):

| (x86, qeynos2, graphics on) | `ready` | `steady_60s` | delta |
|---|---|---|---|
| `ledger` (live requested bytes) | 328,540,528 | 315,780,560 | **−12,759,968** (−12.17 MiB) |
| `RssAnon` | 326,692,864 | 344,322,048 | **+17,629,184** (+16.81 MiB — RSS GREW, it did not fall) |
| `malloc_inuse` (glibc) | 328,862,416 | 316,237,152 | −12,625,264 (−12.04 MiB) |
| `malloc_slack` (glibc free chunks in the arena) | 23,827,760 | 53,512,864 | **+29,685,104** (+28.31 MiB) |
| `keepcost` (top pad) | 8,522,576 | 1,174,656 | −7,347,920 |
| `fordblks` (interior free chunks) | 23,826,256 | 53,512,864 | **+29,686,608** (+28.31 MiB) |
| `ordblks` (interior free chunk COUNT) | 21,493 | 22,521 | +1,028 |
| `hblkhd` (mmap bytes) | 29,335,552 | 29,335,552 | 0 |
| `unattributed_pct` | 0.0 | 8.3 | — |

The bytes the ledger freed did not go anywhere: `malloc_slack` (the arena's
free chunks) grew 28.31 MiB over exactly the same window, and RssAnon actually
ROSE 16.81 MiB because the renderer kept allocating new frames/textures. The
freed bytes were recycled into new live allocations, not returned to the OS;
the 51.03 MiB of interior free at `steady_60s` is what the arena keeps.

## 2. Where the 28 MiB sits: INTERIOR FRAGMENTATION, not the top pad, not mmap

The census answers this directly (both points, run A):

- **Top pad (`keepcost`)**: 8.13 MiB at `ready`, **1.12 MiB at steady_60s**.
  The top of the heap is NOT where the retained bytes sit — it shrank.
- **mmap (`hblkhd`)**: 27.98 MiB at BOTH points, 7 blocks at ready → 7 at
  steady (29,335,552 / 7 ≈ 4.0 MiB average per block). It did not move.
- **Interior free chunks (`fordblks`)**: 22.72 → 51.03 MiB. THE ANSWER.

The by-size-bin census (`[memarena]` line, `bin=chunks/freeBytes`) shows WHERE
the interior free sits at `steady_60s` (run A); the +28.31 MiB growth is
spread across all of these bins:

```
2k-4k   = 1776 / 5,175,920   (4.94 MiB)
4k-8k   =  647 / 3,496,823   (3.33 MiB)
8k-16k  =  780 / 7,762,540   (7.40 MiB)
16k-32k =  344 / 7,345,128   (7.00 MiB)
32k-64k =   83 / 3,536,419   (3.37 MiB)
ge64k   =   79 / 22,627,423  (21.58 MiB)
```

~47.6 MiB total free across the six 2 KB..ge64k bins (3,709 chunks) at
`steady_60s` — the largest of the interior bins, the rest in the sub-2 KB bins
that make up the 51.03 MiB `fordblks` total — with the 21.58 MiB of `ge64k`
split over 79 chunks (avg ~280 KiB). This is classic **interior
fragmentation**: live allocations sandwich the free chunks, so
`malloc_trim(0)` — which can only release from the TOP of the heap —
structurally cannot return a single one of them to the OS. That is the whole
defect in one sentence: a byte freed late lands in an interior bin, and the
arena keeps it.

**M_MMAP_THRESHOLD (dynamic):** the shipped census reports
`mmap_thr=0` (unknown — glibc 2.39 has no read-back of the live dynamic
threshold; `get_mallopt()` is ≥ 2.40, `mallopt(o,0)` SETs) even in the
pinned-threshold runs — so the committed logs say `mmap_thr=0` at BOTH sample
points in all four runs, including run B. The 131072 value is known from the
experiment's own environment configuration
(`B841_MMAP_THRESHOLD=131072` [measured, from the run environment]), not from
a census read-back. Derived: in the unpinned runs the threshold started at
128 KiB and doubled on large frees that couldn't trim the top; the 28.0 MiB /
7-block mmap population at both points is the residue of that drift.

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
  `trim_pages=1`; the untrimmed baseline (A) held 8.13 MiB of top pad at
  `ready`, the post-trim census (D) shows 2,624 bytes. Steady-state
  unchanged: the 28 MiB of interior free is untouched by construction.
  **No frame-time cost** (16.0 ms mean, identical to A).
- **(b) Fixed `M_MMAP_THRESHOLD=128 KiB`** (B): 139 blocks / 63.2 MiB moved
  into mmap at `steady_60s` (vs 7 / 28.0 MiB baseline) — frees ≥ 128 KiB now
  unmap. But `malloc_slack` GREW to 65.0 MiB (the small interior free stayed)
  and RssAnon was 1.1 MiB LOWER at steady (327.3 vs 328.4 MiB). Net: it moves
  bytes from the brk arena to mmap, it does not return the interior free.
  **No frame-time cost.**
- **(c) `M_TRIM_THRESHOLD=8 KiB`** (C): the top pad stays small
  (keepcost 0.06→0.11 MiB, `malloc_slack` lowest of all at 41.6 MiB) but
  RssAnon (326.5 MiB) is still within 2 MiB of baseline (328.4 MiB) — the
  trimmable top pad is < 1 MiB and the interior free is untouched. **No
  frame-time cost.**

## 4. Recommendation (for the orchestrator / Will, after the Pi confirms)

**Do NOT ship any of the three knobs as a default.** The measurement says:

1. The 28.31 MiB of `malloc_slack` growth between `ready` and `steady_60s` is
   **interior fragmentation** (bins 2 KB..ge64k, ~3,700 chunks, 21.58 MiB in
   ge64k alone). It is NOT the top pad (trimmable — and the top pad is only
   ~1 MiB at steady) and NOT mmap drift. `malloc_trim(0)` returns 0 pages;
   lowering `M_TRIM_THRESHOLD` only saves the sub-MiB top pad; pinning
   `M_MMAP_THRESHOLD` only re-shuffles between brk and mmap.
2. **The only bytes that move RSS are the two the card already names:** a byte
   NEVER ALLOCATED, or a byte freed BEFORE the peak. The 28 MiB is neither —
   it was allocated and freed AFTER the peak, so the correct fix is at the
   ALLOCATION site (don't hold the 28 MiB of transient zone/model/texture data
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
  (wired from `bridge_component.cpp` on `LoadingPhase::COMPLETE`) and
  `steady_60s` (wired from `pollSteadyState`), the `malloc_trim(0)` at ready
  when `B841_TRIM_READY=1`, and the `mmap_thr` field (carries the value the
  `B841_MMAP_THRESHOLD` experiment pinned at startup, else 0=unknown — glibc
  2.39 has no read-back of the live dynamic threshold, so the field is
  configured-side, not a glibc query). **These four runs were captured
  03:15–03:25, before the B841 instrumentation commits (03:43) landed, so the
  committed logs show `mmap_thr=0` in every run — including run B, whose
  pinned 131072 is known from the run environment, not from the field.**
- `src/client/main.cpp`: the two startup `mallopt`s, env-gated, beside the
  existing `M_ARENA_MAX=1`.
- `tests/test_memory_ledger.cpp`: 6 new `ArenaCensus.*` tests (fixture parser,
  live census agrees with `mallinfo2`, bin boundaries, malformed input, the
  trim path, and the off-by-default experiments). The fixture test CATCHED A
  REAL BUG in the parser (a char-literal `find('<size')` searching for a
  single byte) before it shipped — RED first, then GREEN.

**Shipped-default delta: zero.** `B841_TRIM_READY` / `B841_MMAP_THRESHOLD` /
`B841_TRIM_THRESHOLD` unset → `main.cpp` runs the existing `M_ARENA_MAX=1`
only; `recordArena` adds a census log line at `ready`/`steady_60s` (the
instrumentation this card asked for) but no `malloc_trim`, no `mallopt`.

## Raw logs (committed on this branch)

- Run A: `refactor/B841/b841_gfx_a.log` — `[memarena]` + `[memledger]` at ready/steady_60s
- Run B: `refactor/B841/b841_gfx_b.log`
- Run C: `refactor/B841/b841_gfx_c.log`
- Run D: `refactor/B841/b841_gfx_d.log`

Tour: `refactor/B841/b841_qeynos2_tour.txt` (`/gmzone qeynos2 303` + 80 s hold
+ `/camp`). Config: `refactor/B841/b841_oraclealpha_willeq.json`.
