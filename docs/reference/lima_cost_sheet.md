# Lima cost sheet — what the Pi's GPU driver charges for

B1006 (GitLab #327). The Orange Pi renders through Mesa's `lima` Gallium
driver on a Mali-400. Four cards (B955, B962, B975, B972) each re-derived a
piece of the same driver behaviour, and the B972-FT frame-time finding reached
Will with no mechanism at all. This sheet is the curated set: each fact names
the Mesa function that implements it and the willeq card that measured it.
**Read it before planning any Pi draw-path, upload or GPU-memory change, and
before explaining a Pi frame-time or GEM-memory delta.** A fact without a
measurement is marked so. Mesa paths are the `src/gallium/drivers/lima/` tree
of the shipped Mesa (the B962 checkout on the Pi, `~/tmp-stash/mesa2528/repo`);
the line numbers move, the function names do not.

## Uploads: what a buffer write costs

1. **A partial write into a buffer the current frame's job references flushes
   that job and blocks until the GPU finishes it.** `glBufferSubData` reaches
   `u_default_buffer_subdata` (gallium `u_transfer.c`), which always sets
   `PIPE_MAP_WRITE` and, for a write smaller than the buffer, `DISCARD_RANGE`.
   `lima_transfer_map` (`lima_resource.c`) then takes its synchronising branch:
   `lima_flush_job_accessing_bo` submits every pending job that references the
   buffer — the frame in progress included, `lima_job.c` keeps it in the same
   table — and `lima_bo_wait` waits with an infinite timeout. The cost is per
   write, independent of the bytes written. Measured: B972-FT (MR !94), the
   impostor path's one shared VBO rewritten once per page gave ~30 % frame
   time on qeynos2 and gfaydark for ~30 KB per frame of data; B1006
   (`refactor/B1006/README.md`) then counted it — the cut build spends 17 ms
   (qeynos2) and 21 ms (gfaydark) per frame inside `lima_bo_wait` across ~10
   waits, against ~0.015 ms in one wait for the client-array build, which is
   the whole frame-time delta.
   The test `usage & PIPE_MAP_READ_WRITE` is a mask test (`READ | WRITE`), so
   a write-only map takes the branch; a reader who assumed it needs both bits
   concluded "plain memcpy, no wait" and was wrong.
2. **A whole-buffer write renames the storage instead.** The same path with
   `DISCARD_WHOLE_RESOURCE` (the write covers the buffer) makes
   `lima_transfer_map` allocate a fresh BO and drop the old one: no wait, but a
   new GEM allocation per write, which the census sees as churn and the
   `gpu_buffers` row as growth until the cache recycles it. B975's "one slack
   vertex" pattern (`src/client/graphics/name_tag_batch.cpp`) exists to keep
   every write partial and therefore on fact 1's path on purpose; it is cheap
   there only because the tag pass writes once per frame.
3. **Client-array draws stream through Mesa's upload manager with no wait.**
   `u_upload_mgr.c` maps its 1 MiB stream buffers `WRITE | UNSYNCHRONIZED |
   PERSISTENT | COHERENT`, so `drawVertexPrimitiveList` with client pointers
   never enters fact 1's branch. The price is memory: those slots are unnamed
   GEM, the bulk of the `gpu_unnamed_steady` guard (B962, B986, MR !94).
4. **The fix shape for a per-frame dynamic buffer** is therefore: build the
   whole pass into one buffer, write it once per frame, draw sub-ranges by
   index offset, and rotate two or three buffers across frames so no write
   touches a buffer any pending job references. Per-frame alternation alone
   leaves the within-frame writes on fact 1's path. Not yet measured on
   willeq (B972-FT's fix card is where it will be).

## Draws: what a draw call costs

5. **Every draw allocates a varying-output BO.** `lima_update_varying`
   (`lima_draw.c`) creates `ctx->gp_output` with `lima_bo_create` per draw,
   sized by the draw's vertex count, and adds it to both pipes. The BO cache
   (`lima_bo_cache_get`, `lima_bo.c`) probes candidates with a timeout-0
   `lima_bo_wait`, which is a poll, not a block. This cost is per draw and is
   paid identically by VBO and client-array draws, so it never explains a
   delta between two builds with the same draw count; it is the draw floor
   (B998's gfaydark finding is draw-count bound).
6. **B962's static zone VBO halved fps with no per-frame writes at all**
   (`refactor/B962/README.md`, "Tried and reverted"). That is not fact 1 (a
   buffer never written never reaches `lima_transfer_map`). The mechanism is
   **unexplained**; any card that puts zone geometry in a VBO (B997) must
   measure it before copying any pattern.

## Frames and jobs

7. **One GP job and one PP job per frame unless something splits them.**
   The driver accumulates draws into `ctx->job` and submits at the flush
   (`eglSwapBuffers`); fact 1's flush submits the partial frame early, so each
   such write adds a PP job. Measured by B1006: 1.2–1.3 PP jobs per frame on
   the client-array build, 3.1–3.2 on the B972 cut, and 2.2–2.3 on the shipped
   ring + one deliberate `glFlush()` after the impostor pass (B972-FT, Will's
   2026-10-09 ruling: that one split costs +1.2 / +0.5 ms of impostor p50 and
   buys −1.85 / −5.75 MiB of `gpu_unnamed_steady`); the guard row
   `gem_pp_jobs_over_bound` fails a zone above 2.5. A split's GPU-side cost
   (the tile-based PP pass re-walks the framebuffer per job) is **not
   separated** from the CPU wait, which already accounts for the delta. The
   count and the wait come from the census shim's job-stream counters
   (`scripts/pi/lima_bo_census.c`, the `gem_pp_jobs_per_frame`,
   `gem_waits_per_frame` and `gem_wait_ms_per_frame` columns of
   `scripts/pi/rendered_zone_summarize.py`).
8. **No timer queries.** `lima` exposes no `GL_EXT_disjoint_timer_query`; the
   P52 GPU sync probe (`docker/irrlicht-drm/gpu_sync_probe.h`) therefore has no
   per-phase GPU timing on the Pi, only its CPU-side swap, flip and finish
   attribution, and it is wired into the benchmark tool, not the client. A
   diagnostic plan built on it decides nothing on the Pi. Measure CPU-side:
   the shim's wait clock, or a wall clock around the GL call.

## Memory: what the kernel counts

9. **GEM buffers live outside RssAnon** (shmem in the process's cgroup); the
   `[memgpu]` row sees them only in the client's own cgroup, which the Pi rigs
   provide (B955, `refactor/B955/README.md`).
10. **Each ETC1 texture holds two GEM buffers** on this driver (B962,
    `refactor/B962/README.md`).
11. **The fragment shader runs at FP16**: a LUT or comparison that needs more
    precision clamps (B965, `refactor/B965/README.md`).
12. **A `discard` whose comparison operand depends on a derivative is
    inverted** on this driver (B958, `refactor/B958/README.md`).

## Using this sheet

A perf or memory finding card names which facts above its mechanism rests on,
cites the Mesa function for any fact not yet here, and states the prediction
those facts make for the measured delta (fact 5 predicts no delta from a
re-buffering that keeps the draw count; fact 1 predicts a cost proportional to
writes per frame, not bytes). A new fact earns a numbered entry with its
measurement; a fact that turns out wrong is corrected here, not in a card.
