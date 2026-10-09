# B972-FT — per-page/per-batch VBO ring: remove Lima's per-frame write-wait (issue #326)

**Class B (memory / frame-time).** **Status: implemented; Pi acceptance A/B
complete — frame gate MET, memory gate NOT MET (flush-coupled; see
Measurement).** Companion card to B972 (the option-2 memory cut, MR !94) and B1006
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

Measured on the Pi crowd tour (`imp200far`, 800×600, Mesa nollvm, VM lane,
census shim) across four builds. `gpu_unnamed_steady` is the memory gate;
`impostor-frames` p50 (median over all frames) + `gem_pp_jobs_per_frame` +
`gem_wait_ms_per_frame` are the frame gate.

| build | zone | fps | pp jobs/f | wait ms/f | imp p50 (ms) | unnamed (MiB) |
|---|---|---|---|---|---|---|
| no-cut `c7abab9c` | qeynos2 | 30 | 1.24 | 0.015 | 33.62 | 19.29 |
| no-cut `c7abab9c` | gfaydark | 25 | 1.31 | 0.016 | 41.55 | 27.89 |
| cut `a893b467` | qeynos2 | 21 | 3.14 | 16.88 | 49.21 | **14.58** |
| cut `a893b467` | gfaydark | 17 | 3.20 | 21.57 | 62.35 | **23.06** |
| ringed `e5f69cd6` (per-page ring) | qeynos2 | 30 | 1.24 | 0.068 | 33.86 | 19.50 |
| ringed `e5f69cd6` | gfaydark | 25 | 1.31 | 0.067 | 41.51 | 28.14 |
| packed `054fa115` (alt, one VBO/frame) | qeynos2 | 31 | 1.23 | 0.053 | 33.27 | 18.92 |
| packed `054fa115` | gfaydark | 25 | 1.32 | 0.053 | 41.36 | 27.89 |

**Verdict.** The frame gate is **met** by BOTH ring shapes: p50 is at no-cut
parity (qeynos2 ~33, gfaydark ~41), `gem_pp_jobs_per_frame` ≤ 2.0 (1.23–1.32),
and `gem_wait_ms_per_frame` ~0.05 (within ~1 ms of no-cut's 0.015). The
`lima_bo_wait` is gone, exactly as the ring was meant to remove.

The memory gate is **NOT met** by either: `gpu_unnamed_steady` is back in the
no-cut band (18.9–19.5 / 27.9–28.1), not the cut's 14.6 / 23.1. The 4–7 MiB
cut is lost. The packed alternative is marginally *better* than the per-page
ring (18.92 vs 19.50 on qeynos2) but still 4.3 MiB above the cut's 14.58.

**Mechanism (named, per B1006/#327).** The `gem_owners.tsv` per-owner table at
`steady_60s` (qeynos2) isolates the cut to the Mesa/Lima driver-retained rows —
NOT a client-owned buffer — and the deciding factor is the per-frame FLUSH
(cost-sheet fact 1: a partial `glBufferSubData` into a buffer the current
frame's job references flushes that job and blocks in `lima_bo_wait`), NOT the
number of VBOs:

| owner row | no-cut | cut | ringed | packed |
|---|---|---|---|---|
| `lima: draw-time BOs` (varyings in flight, PP streams) | 2025 / 9.10 M | 1493 / 6.92 M | 1914 / 8.78 M | 1876 / 8.61 M |
| `gem_wait_ms_per_frame` | 0.015 | **16.88** | 0.068 | 0.053 |
| `untagged (irrlicht_scene)` | 78 / 3.45 M | 25 / 0.96 M | 74 / 2.81 M | 71 / 2.62 M |
| `mesa: stream uploader (1 MiB)` | 6 / 6.00 M | 5 / 5.00 M | 7 / 7.00 M | 6 / 6.00 M |
| `vbo:impostor` (client's own) | 0 / 0.00 M | 9 / 0.04 M | 10 / 0.04 M | 4 / 0.05 M |
| **TOTAL regular BOs** | 2477 / 50.13 M | 2000 / 44.82 M | 2443 / 50.41 M | 2411 / 50.25 M |

The row that tracks the memory gate is `lima: draw-time BOs`, and it is LOW
ONLY on the one leg that flushes (cut, 6.92 M). The three flush-free legs are
all high and clustered (8.6–9.1 M) — and **packed, which issues the same
single VBO/frame as the cut, sits at 8.61 M, i.e. near no-cut, not near the
cut.** So the scratch does NOT scale with distinct-VBO count; it tracks the
per-frame flush. The cut's ~5.3 MiB total reduction (50.13→44.82) is the
`draw-time BOs` row (−2.18 M) plus `untagged irrlicht_scene` (−2.48 M) plus the
stream-uploader and nametag rows, all of which are released when the frame's
job is flushed and torn down. The client's own `vbo:impostor` adds only 0.04 M.

**Why the ring loses it.** The cut's memory win and frame loss are the SAME
event: the cut wrote its shared VBO while a page's draw was still in the
frame's job, so every write flushed the job — the flush both (a) blocked in
`lima_bo_wait` (17–21 ms/frame, the frame regression) and (b) tore down Mesa's
draw-time BOs (the memory win). The ring's entire purpose is to STOP that
flush (rotate buffers so no write touches an in-flight job) — and stopping the
flush is exactly what lets Mesa's `draw-time BOs` accumulate back to the
no-cut level, with or without packing the frame into one VBO.

**Consequence.** The memory cut (B972 option-2) and the frame fix (B972-FT
ring) are FLUSH-COUPLED in the current Mesa/Lima: the low `gpu_unnamed_steady`
is a by-product of the per-frame flush, and the ring removes that flush by
definition. A shape that keeps the wait AND keeps the memory would need to
flush without waiting — not available in the driver, and not shown to exist in
the two shapes measured here (per-page ring and packed one-VBO/frame both land
in the no-cut memory band). This is a **needs-ruling** (Class C) for Will:
either (a) accept the frame fix and let `gpu_unnamed_steady` return to the
no-cut band (the 4–7 MiB cut is not achievable together with the frame fix at
the client level), (b) re-scope the cut so the frame fix stands on its own
(frame parity + `gem_pp_jobs_per_frame` ≤ 2.0 + `lima_bo_wait` gone, memory at
no-cut level), or (c) hold both gates — which, on this evidence, requires a
driver-side change to flush without waiting, out of the client's reach.

The two ring shapes both pass the pixel-identity test (`test_b972_stream_buffers`
2/2: VBO path byte-identical to client-array, bounded ring). x86 full suite
5668/5668 and the CI `build`/`test`/`gate` (visual_gate) jobs are all green on
the packed tip `054fa115` (pipeline 670).

## The glFlush leg (note 2938 / ruling option c) — commit `cf56196a`, MEASURED 2026-10-09

The last untested client-side shape: ONE explicit `glFlush()` immediately after
the impostor pass (`ImpostorBatchNode::render()`, after the packed VBO's
unbind/re-sync, before the frame's remaining non-impostor draws). It submits
the impostor-pass job early. It does NO buffer write, so it never enters
`lima_transfer_map`'s `lima_bo_wait` (cost-sheet fact 1 is write-triggered) —
unlike the pre-ring cut, whose shared-VBO `glBufferSubData` flushes AND waits.
If the cut's 4–7 MiB reduction is the per-draw varying BOs being freed by that
early submit, this recovers the memory gate at ring frame parity (both gates
hold); if not, `gpu_unnamed_steady` stays in the no-cut band and the cut is not
client-achievable with the frame fix.

**x86 (this commit):** `test_b972_stream_buffers` 2/2 (pixel-identity still
byte-identical — the flush is a no-op for pixels), hygiene 25/25, pipeline 683
green (hygiene/build/test/gate; the `test` job runs the full suite).

**Pi A/B (2026-10-09 ~01:46 UTC, build `ffecf7d3` = `cf56196a` + the README
update commit; pipeline 685, `build-arm` job 3277).** Same rig as the
four-build table (qeynos2 + gfaydark, imp200far, 800×600, Mesa nollvm, VM
lane, census shim). `impostor-frames` p50 is the median of the per-120-frame
report windows in the steady window (frame ≥ 3000); `gpu_unnamed_steady` is
`steady_60s` from `results.tsv`:

| build | zone | imp p50 (ms) | pp jobs/f | wait ms/f | unnamed (MiB) |
|---|---|---|---|---|---|
| no-cut `c7abab9c` (fresh, this A/B) | qeynos2 | 33.48 | 1.24 | 0.014 | 18.92 |
| no-cut `c7abab9c` (fresh, this A/B) | gfaydark | 41.31 | 1.31 | 0.016 | 27.76 |
| packed `054fa115` (ring, no flush) | qeynos2 | 33.21 | 1.23 | 0.053 | 18.92 |
| packed `054fa115` (ring, no flush) | gfaydark | 41.30 | 1.32 | 0.053 | 27.89 |
| **glFlush `ffecf7d3` (this leg)** | **qeynos2** | **34.70** | **2.23** | **0.054** | **17.07** |
| **glFlush `ffecf7d3` (this leg)** | **gfaydark** | **41.79** | **2.29** | **0.052** | **22.14** |
| (reference) cut `a893b467` | qeynos2 | 49.21 | 3.14 | 16.88 | 14.58 |
| (reference) cut `a893b467` | gfaydark | 62.35 | 3.20 | 21.57 | 23.06 |

**Verdict (corrected by B1011, 2026-10-09 — the first write-up had the
gfaydark sign wrong): the memory gate is MET on gfaydark and missed on
qeynos2; the frame gate fails on its job-count proxy while the frame time
itself stays near parity. This is a trade for Will, not a failed
prediction.**

1. **Memory gate: met on gfaydark, 2.49 MiB short on qeynos2.**
   `gpu_unnamed_steady` drops below the ring's no-cut band — qeynos2 17.07
   vs 18.92 (−1.85 MiB vs the ring) and gfaydark 22.14 vs 27.89 (−5.75
   MiB) — so the early submit DOES free part of the per-draw varying pool,
   exactly as the hypothesis predicted. Against the cut's 14.58 / 23.06:
   qeynos2 is still 2.49 MiB above the cut, but **gfaydark at 22.14 is 0.92
   MiB BELOW the cut** (the earlier text read this as "0.96 short"; it is
   the heavier zone, and the one where the memory matters). (Mechanism note: the pre-ring cut
   flushed on every `glBufferSubData` — ~10 flushes/frame across impostor,
   particle and nametag writes; this leg flushes exactly once, after the
   impostor pass. The freed share scales with how many writes' BOs are in
   flight, so a single flush frees only the impostor pass's slice of the
   pool.)
2. **Frame gate: the job-count column FAILS.** `gem_pp_jobs_per_frame`
   rises to 2.23 / 2.29 — above the ≤ 2.0 gate — because the explicit
   flush splits the frame's render job into two (the impostor pass, then
   the rest of the frame) plus the tail of the previous frame's job still
   draining. The CPU wait stays at ring parity (0.052–0.054 ms vs the
   ring's 0.053) and the p50 stays within noise of no-cut (34.70 / 41.79
   vs 33.48 / 41.31), so the flush costs frame rate through the job split,
   not through a wait — the exact pattern the pre-ring cut showed, at a
   much smaller dose (2.2 jobs vs 3.1–3.2).
3. **`gem_wait_ms_per_frame` passes** in all three ring shapes (0.052–
   0.068, no-cut 0.014–0.016) — the write-triggered `lima_bo_wait` stays
   gone; the flush never enters `lima_transfer_map`, as fact 1 predicted.

**Consequence for the ruling (B1011 framing).** ring + glFlush buys
−1.85 / −5.75 MiB of `gpu_unnamed_steady` (all of the cut on gfaydark,
60 % of it on qeynos2) for +1.2 / +0.5 ms of impostor p50 (34.70 vs 33.48;
41.79 vs 41.31 — qeynos2's +3.6 % is above the no-cut run-to-run spread of
~0.1 ms, so it is a small real cost, not noise) and a job count of 2.2–2.3
against the 2.0 proxy bound B1006 pinned. `gem_wait_ms` is at ring parity,
so the cost is the job split only. Will's standing order is memory first,
frame rate second, so this is a legitimate third option: **(d) ship ring +
glFlush and re-pin the jobs/frame bound at 2.5**. The full cut on qeynos2
would have needed the cut's ~10 per-write flushes, which is precisely the
per-page write-wait the ring was built to remove. **The worker's recommendation
stands as (a): ship the frame fix; `gpu_unnamed_steady` returns to the
no-cut band (the 4–7 MiB cut is not client-achievable with the frame fix
in current Mesa 25.2.8 Lima).** No budget, golden or ratchet is touched by
`cf56196a`; the 10 MiB `gpu_unnamed_over_bound` guard row stays RED as the
B988/#307 known condition (closes with B997).

**Evidence:** `~/kb-evidence/b972ft-vbo-ring/{nocut-c7abab9c,prering-a893b467,
ringed-e5f69cd6,packed-054fa115,glflush-cf56196a/{nocut,glflush}}/
{qeynos2,gfaydark}/imp200far/` (results.tsv, gem_owners.tsv,
zones/<zone>/client.log — the p50 windows).

## Not verified / left open

- The GPU-side cost of a job split (the tile-based PP pass re-walking the
  framebuffer per job) was not separated from the CPU wait by B1006; the wait
  sum already accounted for the whole delta, so the split's own cost is small or
  overlapped. The ring removes the waits; whether the split's residual cost
  shows up is what the `gem_pp_jobs_per_frame` ≤ 2.0 gate checks.
