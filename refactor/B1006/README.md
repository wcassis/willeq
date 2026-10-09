# B1006 — Lima job census: submit/wait counters in the census shim + a jobs-per-frame guard row (GitLab #327)

**Class B (tooling / regression net).** **Status: DONE — measured on the Pi
2026-10-08, both B972 builds, both zones; the guard bound is pinned from the
no-cut figure.** Companion P unit: B1007 (#328), the Lima cost sheet
(`docs/reference/lima_cost_sheet.md`) and the mechanism-before-ruling rule in
`CLAUDE.md`.

## Why

The B972-FT finding (MR !94: the option-2 bounded-VBO cut costs ~30 % Pi
frame time for ~30 KB of upload per frame) went to Will as a three-way ruling
request with no mechanism. Read against the Mesa 25.2.8 Lima source, the cause
is a partial `glBufferSubData` into a buffer the current frame's job already
references: `lima_transfer_map` flushes that job and blocks in `lima_bo_wait`,
once per write, independent of bytes — and the impostor path rewrites one
shared VBO per page per frame. Nothing in the rig could show it: the P52 GPU
sync probe needs timer queries Lima lacks, and the census shim counted buffers,
not jobs. This unit makes the job stream a column and a guard.

## What was done

- `scripts/pi/lima_bo_census.c` (mode 3, always on): counts
  `DRM_IOCTL_LIMA_GEM_SUBMIT` per pipe (GP / PP), the `DRM_IOCTL_LIMA_GEM_WAIT`
  calls with a non-zero timeout (a timeout-0 call is the BO cache's poll) and
  the wall time inside them, and `eglSwapBuffers` as the frame counter; all
  five appended to every `P` line of the owner dump. The host self-test
  (`LimaBoCensusShim`) drives the three new ioctls and the swap and re-reads
  the `P` line.
- `scripts/pi/gem_owner_table.py` `job_rates()`: the ready → steady_60s deltas
  as per-frame rates; a pre-B1006 dump (8 fields) yields `None`, never a crash
  (`scripts/pi/test_gem_owner_table.py`).
- `scripts/pi/rendered_zone_summarize.py`: five END-appended columns
  (`gem_frames_steady`, `gem_pp_jobs_per_frame`, `gem_gp_jobs_per_frame`,
  `gem_waits_per_frame`, `gem_wait_ms_per_frame`) and the
  `gem_pp_jobs_over_bound` row, `GEM_PP_JOBS_PER_FRAME_BOUND = 2.0` (re-pinned
  to 2.5 by Will's 2026-10-09 ruling on #326: the shipped B972-FT frame is two
  PP jobs by design, `refactor/B972-FT/README.md`).
- `scripts/b964/pi_crowd.sh --bo-census <so>`: the crowd rig never loaded the
  shim before, so every B972 crowd row had no census at all.

## Measurement (Pi, 2026-10-08, `review/B1006/`)

Crowd tour, `imp200far`, 800x600, cache-on, Mesa nollvm, local eqemu lane,
shim loaded, no-cut build first. `fps_median` and the `gem_*` columns from each
run's `results.tsv`:

| build | zone | fps_median | PP jobs / frame | waits / frame | wait ms / frame |
|---|---|---|---|---|---|
| no-cut `c7abab9c` | qeynos2 | 31.0 | 1.20 | 1.06 | 0.013 |
| no-cut `c7abab9c` | gfaydark | 25 | 1.31 | 1.13 | 0.017 |
| cut `a893b467` | qeynos2 | 21 | **3.12** | **10.45** | **17.19** |
| cut `a893b467` | gfaydark | 17 | **3.22** | **10.53** | **21.47** |

- The no-cut frame is one render job with one near-instant wait (the swap).
- The cut frame is split into ~3 render jobs and spends 17–21 ms per frame
  blocked in `lima_bo_wait`. Frame time went 32 → 48 ms on qeynos2 and 40 →
  59 ms on gfaydark: the wait IS the regression, to within a millisecond or
  two. Ten waits per frame against two extra jobs is consistent with the code:
  the impostor pages write a vertex AND an index buffer each, and the particle
  batches write theirs, but only a write that hits a buffer the current job
  references makes a new job.
- `gpu_unnamed_steady` reproduced MR !94's memory cut (qeynos2 19.9 → 14.1 MiB,
  gfaydark 27.8 → 21.0 MiB). All four rows carry the B988/#307
  `gpu_unnamed_over_bound` signature, which is the known condition and comes
  first in the signature order; the new row sits behind it and fires on both
  cut runs (3.12 and 3.22 > 2.0).

Prediction check (the B1007 rule applied to itself): the mechanism predicts a
cost proportional to writes per frame, not bytes, and no change in draw count.
Measured: 10× the waits, ~2 extra jobs, the same `fps` loss the wait sum
predicts. A per-draw theory (identical draw count on both builds) predicts no
delta and is refuted by the same rows.

## Not verified / left open

- The GPU-side cost of a split (the tile-based PP pass re-walking the
  framebuffer per job) is not separated from the CPU wait here; the wait sum
  already accounts for the whole delta, so the split's own cost is small or
  overlapped. Measuring it needs the ring fix (B972-FT, issue #326) to remove
  the waits first.
- B962's static zone VBO halving (no per-frame writes at all) is a different
  mechanism and stays unexplained; B997 must measure it before converting the
  zone pools.
- The row's bound (2.0) has headroom over 1.31 but has only been measured on
  two zones with impostors on; a zone that legitimately splits its frame (a
  render-to-texture pass) would need the bound revisited, with the mechanism
  named.

## Evidence

- `review/B1006/nocut-crowd/{qeynos2,gfaydark}/imp200far/` and
  `review/B1006/cut-crowd/{qeynos2,gfaydark}/imp200far/` — `results.tsv`,
  `sweep.out`, `zones/<zone>/{client.log,gem_owners.tsv,bo_events.log}`.
- `review/B1006-ab.log` — the driver script's start/end lines.
- Issue #326 note (2026-10-08) carries the table for the B972-FT fix card's
  step-1 evidence.
