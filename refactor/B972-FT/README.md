# B972-FT — per-page/per-batch VBO ring: remove Lima's per-frame write-wait (issue #326)

**Class B (memory / frame-time).** **Status: implemented; Pi acceptance A/B
running.** Companion card to B972 (the option-2 memory cut, MR !94) and B1006
(the census that measured the mechanism).

## Why

B972's option-2 cut replaced the impostor and particle client-array draws with
bounded reused VBOs (the B975 `NameTagBatchNode` slack pattern). On the Pi it
was **memory-positive, frame-time-negative**: it cut 4–7 MiB of
`gpu_unnamed_steady` but cost ~30 % frame time (qeynos2 31→21 fps, gfaydark
25→17, order-independent, `refactor/B972/README.md`). MR !94 therefore stayed
draft and this card was boarded to fix the frame-time leg while keeping the
memory cut.

B1006 measured the mechanism (`refactor/B1006/README.md`, `docs/reference/lima_cost_sheet.md`
fact 1): a partial `glBufferSubData` into a buffer the current frame's Lima job
references flushes that job and blocks in `lima_bo_wait`, **once per write,
independent of bytes**. The cut build spent 17–21 ms/frame inside
`lima_bo_wait` across ~10 waits (vs 0.015 ms no-cut) and split into 3.1–3.2 PP
jobs/frame (vs 1.2–1.3). The impostor path rewrote ONE shared VBO once per page
after the previous page's draw was already in the job, so every page ≥ 2
blocked AND fragmented into a separate render job (a tile load/store per page).

## The fix

Cost-sheet fact 4 names the shape for a per-frame dynamic buffer: build the pass
into bounded buffers, write each once, and rotate **two or three buffers across
frames so no write touches a buffer any pending job references**. A write that
finds no matching in-flight job makes `lima_flush_job_accessing_bo` a no-op and
`lima_bo_wait` return immediately, so the wait disappears and the frame stays
one job. This is applied to all three dynamic-upload passes, each ring 3
frame-rows deep (outlives a two-in-flight-job tail):

- **`ImpostorBatchNode`** — a ring of VBO/EBO pairs, one per (frame row, page).
  Frame N page p writes pair `(N % depth) * pages + p`, so same-frame pages use
  distinct pairs (no within-frame wait) and the in-flight frames N-1/N-2 use
  other rows. `B972FT_RING_DEPTH` (1..3, default 3) configures the depth; **1 is
  the pre-B972-FT single shared pair — the A/C control** for the measurement.
- **`UnifiedParticleRenderer`** — 12 slots = 3 frame rows × 4 batches (the two
  procedural-atlas and two spell-sprite batches). Frame N batch b writes slot
  `(N % 3) * 4 + b`. `frameCount_` advances only on a frame that actually
  renders particles, so the depth counts write-frames.
- **`NameTagBatchNode`** — 12 slots = 3 frame rows × 4 chunk slots (one run per
  frame; the single font-atlas texture makes a frame one run). Chunk k of run r
  writes slot `(r % 3) * 4 + k`. The one index EBO stays static (written only
  at growth), so only the per-frame vertex write rotates.

Each slot grows at high-water + **one slack vertex**, so every `glBufferSubData`
stays strictly smaller than the allocation — the B975 partial-write path (Lima
reuses the bounded storage) rather than the whole-resource write (which renames
a fresh BO). Memory stays named (`vbo:impostor` / `vbo:particles` /
`vbo:nametags`), so the `gpu_unnamed_steady` cut is held; the rings add a
handful of ~30 KB named VBOs, trivial against the 13 MiB `u_upload` pool the
cut removed.

## Acceptance (issue #326 + the 2026-10-08 addendum)

Measured on the Pi crowd tour (`imp200far`, 800×600, cache-on, Mesa nollvm, VM
lane), A/B against the no-cut build `c7abab9c`, via the B1006 census
(`scripts/b964/pi_crowd.sh --bo-census`):

1. **Frame time back to no-cut parity:** `impostor-frames` p50 for qeynos2
   returns to ~33 ms (2× vsync), gfaydark to ~40 ms; the +1 vsync-period delta
   is gone. Not "lower than fable" — **parity**.
2. **Census mechanism gate (2026-10-08 addendum, #326 note 2888):** the
   `gem_pp_jobs_per_frame` column returns to ≤ 2.0 (no-cut measures 1.2–1.3) and
   `gem_wait_ms_per_frame` is within ~1 ms of no-cut (0.015 ms).
3. **Memory cut held:** `gpu_unnamed_steady` still shows the 4–7 MiB reduction;
   the buffers stay named (not regressed to the 13 MiB `u_upload` pool). The
   10 MiB `gpu_unnamed_over_bound` guard stays RED as the B988/#307 known
   condition (closes with B997) — out of scope here.
4. **Pixel-identical:** `test_b972_stream_buffers` green (the VBO path renders
   byte-identically to the client-array path, and the impostor ring is bounded:
   it rotates across frames, allocates a new row once, and does not grow on
   wrap).

## Defects found in CI at the ring head (fixed before the Pi A/B)

Pipeline 663 (head `ce56510e`) ran red: all six `NameTagBatchTest` cases
SEGFAULTED inside libgallium (`glDrawElements`, read of 0x8). Reproduced
locally on llvmpipe and root-caused with ASan; two independent defects, both
in `NameTagBatchNode`, both masked until the ring head:

1. **Attribute pointers captured client memory.** `glVertexAttribPointer`
   captures the buffer bound AT CALL TIME (no VAO). The ring commit removed
   the `glBindBuffer(GL_ARRAY_BUFFER, gpuVertex_)` that used to precede the
   pointer setup and moved the bind into the per-chunk loop AFTER the
   pointers, so the pointers captured client address 0 and the draw read
   it. Fixed by setting the pointers inside the loop, after each chunk's
   slot bind — the impostor and particle rings both already did this,
   which is why only the nametag path crashed. This also fixes a second,
   latent consequence: a run of MORE THAN 4 chunks would have drawn every
   chunk through the first chunk's slot, not its own.
2. **Release under-subtracted the driver tally.** `releaseGpuBuffers`
   zeroed `gpuVertexRingBytes_[i]` in its delete loop BEFORE computing
   `gpuBufferBytes()` for `gles2AdjustRawBufferBytes(-…)`, so the ring's
   vertex bytes stayed in the driver's raw-buffer tally (measured residual
   500,700 B = 3 grown slots; only the static index EBO, 25,032 B, was
   subtracted). Fixed by tallying before zeroing — the impostor and
   particle releases both already compute their total first.
   `test_name_tag_batch`'s `GpuStorageIsBoundedAccountedAndReleasedWithTheBatch`
   now renders a second frame first so it reads the steady-state total
   (ring slots grow lazily, one per frame row, first use), and its
   `before + bytes` expectation pins the exact release.

Both fixed on this branch (the commit after `ce56510e`); ASan Debug tree
and Release tree both green on `test_name_tag_batch` (6/6) and
`test_b972_stream_buffers` (2/2) before the Pi A/B.

## Measurement

*(A/B running; results land below.)*

## Not verified / left open

- The GPU-side cost of a job split (the tile-based PP pass re-walking the
  framebuffer per job) was not separated from the CPU wait by B1006; the wait
  sum already accounted for the whole delta, so the split's own cost is small or
  overlapped. The ring removes the waits; whether the split's residual cost
  shows up is what the `gem_pp_jobs_per_frame` ≤ 2.0 gate checks.
