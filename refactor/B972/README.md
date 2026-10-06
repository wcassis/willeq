# B972 — Mesa stream-upload buffers + Lima draw-time BOs (issue #290)

**Class B (memory).** **Status: MEASURED — lever analysis complete; acceptance scope
needs a decision (boarded, not closed).**

## Task

Reduce the crowd-tour **unnamed** GEM/lima BO memory on the Pi below the
**10 MiB `gpu_unnamed_steady` guard** on **qeynos2** and **gfaydark**
(impostor-200-far, cache-on default policy) with **no frame-time regression**.
The B986 baseline measured **24.82 MiB** unnamed at the same point
(gfaydark-imp200far, cache-on, run-277).

## What was done (this run, `task/B972-mesa-stream-buffers` @ fable tip `85fcf0fd`)

1. Re-merged fable, rebuilt the x86 GLES2 Irrlicht, pushed, CI `build-arm`
   (pipeline 557, job 2662) green; staged the ARM build **locally** on the Pi
   (`~/ci-arm/85fcf0fd…/`, `bin/willeq-nollvm` absolute DT_RPATH + Mesa prefix
   under the binary) — the Pi rigs read the build dir from local ext4, not NFS.
2. **Reproduced the B986 baseline, RED on both zones** (cache-on, imp200far,
   800x600, the byte-identical run-277 crowd tour):

   | zone | rig `gpu_unnamed_steady` | fps_median | verdict |
   |---|---|---|---|
   | qeynos2 | **18.90 MiB** | 32 | over_bound |
   | gfaydark | **24.5 MiB** | 26 | over_bound |

   Matches B986's 24.82 within run-to-run noise.

3. **Decisive control — `off200far` (impostors OFF, 200 full models, same
   tip/rig):**

   | zone | imp200far | off200far | impostor Δ |
   |---|---|---|---|
   | qeynos2 | 18.90 | 17.64 | +1.3 MiB |
   | gfaydark | 24.5 | 24.54 | ~0 |

   **The impostor is NOT the lever.** Its ~12 MiB is its *named* impostor
   texture (already counted in `logical` and subtracted by the guard); its
   *unnamed* contribution is only ~0–1.3 MiB. Entities (full models) already
   draw through the hardware-buffer/VBO path, so 200 models occupy the same
   1 MiB `u_upload` slot pool the impostor does.

## Census anatomy (gfaydark imp200far, final crowd `steady_60s`, cache-on)

`unnamed = gpu_buffers − logical` (frame PNGs and `logical_textures` are
subtracted by the summarizer; `logical_buffers` is 0 in every run because the
zone-merge/entity draws are client-array, not VBOs).

- **13 × 1 MiB `u_upload` stream slots (13 MiB), all cache-retained**, tagged:
  - 6 × `draw:zone` — the zone-merge 3D passes (`renderer_zone_merge.cpp`,
    ~38 client-array `drawCalls`/frame across the merged pool).
  - 6 × `draw3d:client` — 3D client-array draws (impostor pages, doors, aux).
  - 1 × untagged — a raw-GL 3D draw (particles / `unified_particle_renderer`).
- **~7.4 MiB of per-draw varying BOs** — ~2043 × 4 KiB
  (`lima_update_varying` allocates a fresh BO per client-array draw).
- **None of the 13 slots are `draw2d:*`** → the 2D UI is NOT the lever; T462
  already batches the UI into `UiQuadBatch` (though that batch still streams
  client-side vertices — a small, separate lever).

## Why the 10 MiB guard is out of reach in this card's budget/shape

- The guard needs a **~16–19 MiB** cut on gfaydark (from 24.5 → <10).
- **No impostor/2D lever does that** (measured above: impostor ≤ 1.3 MiB).
- The bulk is the **zone-merge client-array draws (6 slots) + entity/aux
  client-array draws (6 slots) + ~7.4 MiB varying BOs.** Converting those to
  bounded VBOs (the merged B975 `NameTagBatchNode` pattern — bytes enter
  `logical`, the 1 MiB slot is freed) is the mechanism, but:
  - **B962 already measured a static VBO for the zone pools and it was worse**
    (+4.3 MiB, ~half frame rate). The zone pool is dynamic (generation gather
    per frame), so a VBO there is a large per-frame upload — B962's
    "2D UI and entities keep the stream-upload buffers busy" is the core
    problem, not a per-draw bug.
  - Doing it safely (bounded reused VBOs, partial `glBufferSubData`, the B975
    slack-buffer pattern) across the zone-merge + entity-aux draws is a
    multi-week effort, not a one-card one.

## Reachability (from B986, same zone/rig)

Cache-**off** (`--no-gpu-cache`) measured a floor of **6.87 MiB** unnamed
(stream-uploader 12.6 → 1.0, draw-time 10.0 → 0.7). So **the excess above the
floor is purely Mesa cache retention of the client-array 1 MiB slots**, and a
cache-on run *can* get under 10 MiB — but only by reducing the *number* of
concurrent client-array draws that keep a slot warm, i.e. the zone-merge /
entity-aux VBO conversion above. Turning the cache off is not an option (the
guard is defined cache-on; and it just moves the cost to per-frame uploads).

## Recommendation (needs Will)

The 10 MiB **crowd-tour** bound on gfaydark is set against a measurement that is
dominated by the zone-merge + entity-aux client-array slot pool that **B962
already rejected a static-VBO fix for**. Two coherent options:

1. **Re-scope the guard** to a point where the lever is the thing being
   measured — e.g. keep the 10 MiB bound but judge it at a **no-crowd** or
   **qeynos2-only** point, or set the bound to what a cache-on steady
   measurement can reach after a bounded-VBO conversion of the *convertible*
   draws (impostor pages, the untagged raw-GL particle draw, the T462 UI batch)
   — a realistic ~3–6 MiB cut, matching the board entry's own "3-6 MiB"
   framing. The remaining zone-merge work goes to its own card (it overlaps
   B962's rejected path and needs a bounded-VBO design, not a one-shot).
2. **Keep the 10 MiB crowd-tour bound as-is** and split the work: this card
   ships the *self-contained, pixel-identical, measurable* cuts (impostor
   pages → bounded VBO via B975 pattern; the untagged raw-GL particle draw →
   VBO; optionally the T462 `UiQuadBatch` vertices → VBO), and a follow-up
   card (larger, overlaps B962) owns the zone-merge + entity-aux bounded-VBO
   conversion needed to close the rest.

**No code was changed on this branch yet.** The branch is at fable tip
`85fcf0fd` (CI 557/2662 green); it holds no lever change. Nothing to revert.

## Measurement evidence (on the Pi)

- `~/kb-evidence/b972-stream-buffers/baseline-crowd-20261006T071713Z/{gfaydark,qeynos2}/imp200far/`
  — reproduced baseline (results.tsv, client.log, census).
- `~/kb-evidence/b972-stream-buffers/control-off200far-…/{gfaydark,qeynos2}/off200far/`
  — the `off200far` control (results.tsv: qeynos2 17.64 MiB, gfaydark 24.54 MiB).
- `~/kb-evidence/b986-pi-gpu-guard/run-277/census-gfaydark-imp200far/…/gem_owners.tsv`
  — B986 per-tag census (the 13-slot anatomy).
- Pi census shim `lima_bo_census.so` (md5 `af7610db74fc0b55f23a7900c953f5df`)
  staged at `~/kb-evidence/b972-stream-buffers/tools/` (attribution only —
  the guard itself reads the client's own `[memgpu]` row).
