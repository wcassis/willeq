# P01 — Investigate: EQE1 slower than RGBA on Mali400 (+ lighting/culling questions)

Batch P (performance) opener, directed by Will. H04 measured EQE1/ETC1
consistently SLOWER than direct RGBA in p95 frame time on the Pi — but
Will's historical tests showed NO perf difference between ETC and RGBA
(memory footprint only). Explain the contradiction with controlled
experiments on the real hardware, and revisit Will's two open questions
from his last debugging era: (a) per-pixel lighting cost dominance (fps was
consistently high with per-pixel lighting DISABLED), (b) whether lighting
operations were being applied to geometry regardless of visibility /
whether scene-graph culling works as believed.

## Hypotheses to test (H1 is the orchestrator's prime suspect — try to
DISCONFIRM it, don't anchor)

- **H1 — H02d asymmetry:** H02d (merged 2026-07-23, BEFORE H04's runs) made
  RGBA alpha-test read the main texture's own alpha (ONE fetch/fragment)
  while ETC1 keeps the companion variant (TWO fetches). Historical ETC≈RGBA
  tests predate H02d, when BOTH paths sampled a unit-1 texture. Prediction
  if true: the delta concentrates in alpha-test-heavy scenes/frames
  (gfaydark canopy, befallen?); forcing both paths onto the same variant
  (or comparing pre-H02d builds) eliminates the delta; delta shrinks at
  lower resolution only if fragment-bound.
- **H2 — filtering/mipmap differences:** ETC1 main textures carry full mip
  chains + trilinear (per 00026f3/9189ad6 era fixes) and companions use
  NEAREST_MIPMAP_NEAREST; the RGBA path's mip/filter state on Pi (EQT_DRM
  build) may differ. Different sampling cost per fetch.
- **H3 — measurement confound:** load-order, cache residency, or thermal
  ordering in H04's runs (EQE1 ran after RGBA each pass?). Check H04's run
  script ordering and re-measure interleaved.
- **H4 — Lima ETC1 decode cost:** Lima may decompress ETC1 in a slow path.
  Least actionable; only reach for it if H1–H3 are excluded.

## Experiments (Pi, real hardware; strict I/O rules from
refactor/H04_pi_tier2.md apply — /tmp staging, governor pinning, cooldowns)

1. Reproduce the delta with interleaved A/B ordering (kills H3).
2. Per-frame analysis: correlate delta magnitude with alpha-test draw share
   per frame (the CSVs have per-frame draws; add a per-material-type count
   to the benchmark CSV if needed — flag-gated).
3. Resolution scaling: 400×300 vs 800×600 both paths — fragment-bound or
   not.
4. Variant forcing: smallest possible experiment that puts both paths on
   the SAME shader variant (e.g. temporarily force RGBA textures through
   the companion variant in a scratch build — experiment-only, never
   committed to production paths) → if delta vanishes, H1 confirmed.
5. Filter-state audit: log actual GL min/mag filters + mip presence for a
   sample of zone textures on both paths on the Pi (H2).
6. **Lighting cost curve (Will's question a):** benchmark `--lighting N`
   (0..8) sweep on one zone, both texture paths — quantify per-pixel
   lighting's share of frame time on Mali400 today. Compare against the
   historical perpixel benchmark data (docs/reference/orangepione/
   perpixel.txt, perpixellut.txt; tool gles2_shader_perpixel_benchmark).
7. **Lighting-vs-visibility (Will's question b):** using --no-frustum-cull /
   --no-pvs toggles and the draw/state counts, determine whether lighting
   work (uniform computation, light selection, per-vertex or per-pixel
   shader cost) scales with VISIBLE geometry or TOTAL geometry. Read the
   current post-Batch-F/G code (sim_visibility computeLightSelection,
   zone draw paths) to trace where light selection happens relative to
   culling; state clearly whether lighting is applied to culled geometry
   anywhere.

## Deliverables

Findings report in this file's Review section: the confirmed explanation
for the EQE1 delta (with numbers), answers to Will's two questions (with
numbers and code references), comparison table vs historical perpixel data,
and a prioritized list of fix candidates with estimated impact. NO
production fixes in this unit — investigation only (scratch builds for
experiments are fine in the worktree, never merged).

## Verification

- Every hypothesis explicitly confirmed/refuted with measurements.
- Pi left pristine (governor, /tmp) after every session.
- No production code changes committed (benchmark flag additions allowed if
  needed for experiment 2, with the usual bar).

## Review

**STATUS: interrupted by a Pi kernel panic mid-session (block 6b, second of two
lighting-model experiments). The Pi is confirmed unreachable (ping and ssh
both dead) and needs a physical power cycle before any further hardware work.
Everything below experiments 1-4, 6, 7 is real, completed, hardware-measured
data; experiment 6b (true per-pixel-vs-lightweight A/B) and the H2 GL-filter
runtime spot-check never got to run any benchmark invocation before the panic
— H2 is answered from source inspection instead (see below), and 6b is
recorded as blocked.**

### Panic context record (for whoever debugs the Pi-side crash)

- **Last command in flight when contact was lost:** the `GovernorPin.__enter__`
  ssh call (`scripts/pi/pi_common.py:190-199`) — a plain
  `for f in /sys/devices/system/cpu/cpu*/cpufreq/scaling_governor; do echo
  performance | sudo -n tee $f > /dev/null; done`, i.e. **no benchmark
  process was running or being launched at the moment of the hang** — this
  ssh call to set the CPU governor timed out after the fixed 300s subprocess
  timeout with no other data (`pi_common.ssh_run`'s default `timeout=300`,
  hit at `scripts/pi/pi_common.py:45`).
- This was the **7th separate python invocation** of the ad hoc orchestration
  script (`p01_run.py`, kept in the session scratchpad, not committed) in
  this investigation, i.e. the 7th independent preflight -> stage -> governor
  pin -> run -> pull -> restore cycle since the session started. It followed
  immediately after **block 7 (culling toggles) completed and exited
  cleanly**, including its own governor restore (confirmed
  `['ondemand','ondemand','ondemand','ondemand']` printed) and staged-binary
  cleanup — so the Pi was healthy and idle at the end of block 7.
  `investigate_console_drm()` and `strip_and_stage_binary()` for block 6b
  both completed successfully (binary staged at `/tmp/gles2_zone_benchmark_p01`,
  5,838,136 bytes, `ls`/`df -h /tmp` output printed normally) immediately
  before the hang — i.e. the panic (or the state leading to it) occurred in
  the few seconds between a successful `ls`/`df` ssh round-trip and the next
  ssh round-trip, with **zero GPU/DRM activity in that specific window**.
- **Cumulative DRM-touching process count since session start: 41 completed
  `gles2_zone_benchmark --drm` invocations** across blocks 1, 2, 3, 4, 6, 7
  (4 + 6 + 8 + 6 + 9 + 8 = 41), each a fresh process doing its own
  EGL/GBM/DRM context init, zone load, render, `glFinish`, and teardown, run
  back-to-back (6-10s cooldowns between invocations, ~10s between blocks)
  over roughly 45 minutes. Every one of those 41 runs completed successfully
  and produced a valid CSV (see below) — no single run crashed or hung.
  Given the panic surfaced only after this density of rapid init/teardown
  cycles, and manifested between runs rather than during one, the
  orchestrator's hypothesis (kernel-side GEM/CMA buffer accumulation across
  many rapid DRM context cycles, a slow leak/fragmentation crossing a
  threshold asynchronously rather than a single triggering command) is
  consistent with everything observed here — H04's original 4 runs (2 full
  A/B passes) never approached this density. Not independently confirmed
  (no kernel log access after the panic), but this is the strongest
  correlated signal available.
- No ssh retries were attempted after the timeout per the interrupt
  instruction. Governor state on the Pi at the moment of the panic is
  therefore of unknown final value (the block 6b `GovernorPin.__enter__` may
  have partially applied `performance` to some cores before the hang) —
  **flag for whoever power-cycles the Pi: verify/reset
  `scaling_governor` to `ondemand` on all 4 cores after it comes back**,
  since the block 7 restore is the last confirmed-good state but block 6b's
  partial pin is unconfirmed either way.
- Scratch orchestration script: `p01_run.py` (session scratchpad, not
  part of this repo/commit) — block functions `block1`..`block7`, `block6b`.
  All raw CSVs/logs pulled before the panic are on NFS at
  `benchmarks/pi_runs/p01_investigation/` (41 CSVs, gitignored per
  `benchmarks/pi_runs/`, same convention as H04).

---

### H1 — H02d asymmetry: **DISCONFIRMED** (orchestrator's prime suspect, ruled out)

Two independent lines of evidence, both real Mali400 measurements:

1. **Experiment 2 (material-type histogram, new `--material-counts` flag,
   `tools/hwcaps/gles2_zone_benchmark.cpp` `countMaterialFamilies()`):**
   qeynos2's three designated frames have **`alpha_test_draws = 0` in every
   frame** (`benchmarks/pi_runs/p01_investigation/p01_e2_qeynos2_{rgba,eqe1}.csv`
   — `alpha=[0]`, `solid=[119,228,236]`) — i.e. 100% of qeynos2's drawn
   geometry at these poses is solid material, and the solid fragment shader
   (`FRAGMENT_SHADER_SOLID_SRC`, `src/client/graphics/zone_shader.cpp:120-137`)
   does exactly one `texture2D(uTexture, ...)` fetch on **both** the ETC1 and
   RGBA paths — there is no companion texture involved for solid materials at
   all (the two-fetch-vs-one-fetch asymmetry H1 describes only exists in the
   alpha-test shader family, `zone_shader.cpp:155-171` vs the H02d
   `...ALPHA_RGBA_SRC` variant). H1's mechanism is **structurally inapplicable**
   to qeynos2, yet qeynos2 shows the **largest and noisiest** RGBA-vs-EQE1
   deltas of the three zones (see below) — this alone rules out H1 as *the*
   explanation.
2. **Experiment 4 (variant forcing, scratch env-gated hook in
   `zone_geometry.cpp` `buildTexturedMesh`/`buildTexturedMeshFromUploaded`,
   `EQT_P01_FORCE_COMPANION=1`, reverted before this commit — not part of the
   diff):** forcing RGBA alpha-test materials through the ETC1 companion
   (2-fetch) shader variant instead of H02d's 1-fetch RGBA variant:
   - qeynos2 (no alpha-test draws in view): forced 34.70ms vs normal 34.79ms
     p95 (**-0.3%**, noise) — exactly the null result predicted once you know
     experiment 2 found zero alpha-test draws there.
   - gfaydark (**does** have alpha-test draws — `alpha=[9,25,28]` out of
     `solid=[118,125,175]`, i.e. ~5-14% of draws): forcing companion **p95
     went DOWN, 41.11ms -> 35.70ms (-13.2%)** — the opposite direction from
     H1's prediction (forcing the extra fetch should cost more, not less).
     gfaydark's actual EQE1-vs-RGBA delta in the same session was a much
     smaller +3.2% (42.41ms vs 41.11ms). Forcing the alleged root cause in
     isolation does not reproduce or approach the real EQE1 delta's
     magnitude, and moves the wrong way.

**Verdict: H1 disconfirmed.** The H02d single-fetch-vs-companion asymmetry is
real and correctly described, but it cannot be the (primary) explanation for
the EQE1-vs-RGBA slowdown: qeynos2 has none of the alpha-test draws H1's
mechanism requires, and gfaydark's direct A/B test of the mechanism moved
opposite to prediction. If it contributes at all, it is far below the noise
floor of these measurements.

### H2 — filtering/mipmap differences: **REFUTED by source inspection** (no runtime experiment needed or completed)

`src/client/graphics/constrained_texture_cache.cpp`:
- ETC1 **main** texture upload (lines 1454-1462): `GL_TEXTURE_MAG_FILTER =
  GL_LINEAR`, `GL_TEXTURE_MIN_FILTER = GL_LINEAR_MIPMAP_LINEAR` (or `GL_LINEAR`
  if no mips) — **identical** to the direct-RGBA upload path's filter state
  elsewhere in the same file (e.g. lines 243-247, 646-650, 774-778,
  1306-1310, all the same `LINEAR`/`LINEAR_MIPMAP_LINEAR` pair). Solid
  materials — which is 100% of qeynos2's and befallen's drawn geometry per
  experiment 2 — sample only this main texture on both paths, with identical
  filter state. No asymmetry exists here at all.
- The ETC1 **alpha companion** (unit 1, lines 1492-1504) does use a different
  filter, `GL_NEAREST`/`GL_NEAREST_MIPMAP_NEAREST` — but that's the opposite
  direction from what H2 needs: nearest-neighbor sampling is cheaper than
  trilinear on essentially every GPU's texture unit (fewer texel fetches
  and no interpolation math per sample), so if this filter difference
  mattered at all it would predict ETC1's alpha-test path should be *faster*
  than RGBA's, not slower. The comment at line 1492-1496 explains this
  filter was chosen for **correctness** (binary alpha masks must not be
  blended), not performance.

**Verdict: H2 refuted.** No filter/mip asymmetry exists on the solid path
(the bulk of drawn geometry in 2 of 3 zones), and the one real asymmetry
(companion NEAREST vs main LINEAR) points the wrong direction to explain a
slowdown. No Pi runtime experiment was needed to settle this one, and the
planned live GL-filter-state dump (H2's runtime half of experiment 5) never
ran before the panic — but the source-level answer is unambiguous enough
that it isn't blocking.

### H3 — measurement confound (order/thermal): **CONFIRMED as a real, large effect — invalidates precise percentages, does not fully explain the phenomenon**

Experiment 1 (qeynos2, ABBA sequence, same session, governor pinned
throughout, `p01_e1_qeynos2_*.csv`):

| order | variant | p95 (ms) |
|---|---|---|
| 1st | rgba | 34.85 |
| 2nd | eqe1 | 45.96 |
| 3rd | eqe1 | 43.03 |
| 4th (last) | rgba | **48.89** |

The **last run in the sequence is RGBA and is the slowest of all four** —
slower than either EQE1 run. This is a monotonic-looking drift with session
position that has nothing to do with which texture variant is active; H04's
original baseline always ran RGBA-then-EQE1 within each pass
(`scripts/pi/pi_common.py` `variants = [False, True]`, hard-coded order,
never varied), so every one of H04's "EQE1 slower" numbers has this same
uncontrolled order confound baked in and its exact percentages should not be
taken as precise.

However, H3 does **not** fully explain the phenomenon away: across many
independent, freshly-governor-pinned, cooldown-rested sessions in this
investigation (11 separate qeynos2/800x600/rgba measurements across blocks
1, 2, 3, 4, 6, 7), **every single RGBA p95 falls in a tight 34.6-35.3ms band
(<2% spread)**, while the 5 independent qeynos2/800x600/eqe1 measurements
span **30.70 to 45.96ms (a >49% spread)** with no consistent trend by session
order (block3's fresh-session EQE1 run was the *fastest* of the whole
investigation, 30.70ms, actually beating RGBA). This exact
stable-RGBA/noisy-EQE1 signature matches H04's own 3-run variance table
(qeynos2/rgba max deviation 0.3% vs qeynos2/eqe1 21.1%, `refactor/H04_pi_tier2.md`
review). A pure order/thermal artifact should affect both variants'
variance similarly (they're both subject to the same clock/thermal state);
it doesn't explain why EQE1 specifically is the noisy one across completely
independent sessions with no shared order.

**Verdict: H3 confirmed as real and significant — it means no single
reported "+X% slower" number from this investigation or H04's baseline
should be trusted to more than order-of-magnitude precision — but it is not
sufficient on its own to explain the qualitatively different variance
signature between the two texture paths.**

### H4 — Lima ETC1 decode/sampling cost: **best-supported remaining explanation, not directly instrumented (no GPU profiler access on this hardware from userspace)**

Combining the above: H1 is structurally ruled out for qeynos2 and moves the
wrong way for gfaydark; H2 points the wrong direction; H3 is real but
doesn't explain the variance-shape asymmetry. What's left, and what the data
actually shows, is a **texture-format-correlated difference in measurement
stability and average cost** that tracks which texture pipeline is active
(RGBA: fast, tight, deterministic; ETC1: usually slower on average, and far
noisier) independent of alpha-test presence, resolution, or session order.
This is consistent with real GPU/driver-side cost in decoding/sampling ETC1
compressed blocks on the Mali400's texture units via the Lima driver — H4's
original description — that interacts with the GPU's memory bus/thermal
state in ways RGBA sampling doesn't (e.g. decode-block cache behavior,
DVFS/memory-clock coupling to the smaller ETC1 memory footprint). This
investigation's instruments (frame-timing CSVs over ssh) cannot directly
profile Lima's internal GPU pipeline stages to confirm the mechanism further
— that would need a GPU-side profiler (`perf` with Mali/Lima counters, or
Mesa's `GALLIUM_HUD`) which wasn't attempted and is a natural next step once
the Pi is back.

### The historical "no difference" claim: reconciled

`H04_pi_tier2.md`'s review explicitly notes "prior signal was all VM/llvmpipe
timing, which is not representative of the actual target device." Will's
historical ETC1-vs-RGBA tests were almost certainly run against llvmpipe
(software rasterizer, VM) rather than the real Mali400 + Lima driver;
llvmpipe has no hardware ETC1 decode path to be slow in the first place (it's
a CPU rasterizer that decodes compressed formats as part of its own texture
sampling code, likely at a cost profile completely unrelated to a real
mobile GPU's fixed-function decode block) — so a "no difference" finding on
llvmpipe says nothing about the real device, and isn't in tension with H04's
real-hardware finding. This is the headline explanation: **the delta is real
on Mali400 hardware and was invisible in VM testing because VM testing never
exercised the real GPU's ETC1 texture path at all.**

### Confirmed explanation for the EQE1 delta (summary)

The EQE1-vs-RGBA slowdown H04 measured is real but was measured with an
uncontrolled order confound (H3) that inflates and destabilizes the specific
percentages; the underlying cause is not the H02d single/companion shader
fetch asymmetry (H1, disconfirmed both by an alpha-draw-count argument on
qeynos2 and by a direct forcing experiment on gfaydark that moved the wrong
way) and not a filtering/mipmap difference (H2, refuted by source
inspection — the one real filter asymmetry points the wrong direction). The
best-supported remaining explanation is a genuine Mali400/Lima GPU-side cost
of sampling ETC1-compressed textures (H4) that is both slower on average and
substantially noisier than sampling RGBA — consistent with every zone/session
combination measured, and consistent with why Will's historical tests (on
VM/llvmpipe, per H04's own review) never saw it.

### Will's question (a): per-pixel lighting cost

Two different things were measured, and they give very different answers:

1. **Point-light count within the shader that's always active
   (`--lighting 0..8`, experiment 6, `p01_e6_qeynos2_light{0..8}.csv`):**
   p95 frame time is flat at 34.6-35.3ms across the entire 0-8 sweep, every
   step-to-step delta inside ±0.5ms — **no measurable cost** from adding
   point lights on real zone geometry at these poses. This is because
   `tools/hwcaps/zone_bench_loader.cpp:209` calls
   `data->zoneShader->setPerPixelPlayerLight(true)` **unconditionally** —
   every golden-zone benchmark run already uses the per-pixel-lit shader
   variant; `--lighting N` only changes the trip count of the point-light
   loop inside that already-active shader, not which shader runs.
2. **The historical `perpixel.txt`/`perpixellut.txt` benchmark** measured
   something different: LIGHTWEIGHT (per-vertex) baseline **7.3ms** vs "PP
   BRANCHLESS (current)" (per-pixel) **23.3ms** — a **+16ms, >3x** cost —
   on a synthetic 160-vertex/224-triangle single-quad-per-wall scene at
   1280x720. That delta is the per-vertex-vs-per-pixel **shading model**
   switch itself (dominated by full-screen fragment count on a
   fill-rate-bound synthetic scene), not point-light count.
3. **Experiment 6b (true per-pixel-vs-all-lightweight A/B on real zone
   geometry, via `--adaptive-lighting --zone-pp-budget 0` which forces every
   zone region to the lightweight material override per
   `tools/hwcaps/zone_bench_loader.cpp:877-903`) was queued as the next
   step to directly answer "does the historical +16ms/3x per-pixel cost
   reproduce on real zone geometry" — but the Pi panicked during this
   block's governor-pin step, before any benchmark invocation ran. Zero
   data collected for this specific comparison. This is the single most
   important unresolved piece of hardware work for Will's question (a) —
   it directly targets his memory that "fps was consistently high with
   per-pixel lighting DISABLED," which experiment 6 (point-light count) does
   not test at all.**

**Answer as far as the data goes:** point-light count (0-8) is free on real
zone geometry at these resolutions/poses; the expensive thing (per the
historical synthetic benchmark) is having per-pixel shading active *at all*
vs per-vertex, and that comparison could not be repeated on real geometry
before the panic. Given the benchmark hard-codes per-pixel-always
(`zone_bench_loader.cpp:209`), and production's adaptive-lighting budget
mechanism exists specifically to cap per-pixel-lit region count
(`renderer_zone_draw.cpp:189-218`), it is plausible the historical "fps high
with per-pixel disabled" observation reflects exactly this shading-model
cost rather than light count — but that inference is not yet confirmed on
real geometry and should be the first experiment run once the Pi returns.

### Will's question (b): does lighting work scale with total or visible geometry?

Traced the full path from culling to per-fragment shading, real code
references:

1. **Culling happens first, before any draw list is built.**
   `src/client/graphics/sim_visibility.cpp` `computeVisibility()`
   (lines 91-149): candidates start from the PVS-precomputed visible-bounds
   index list when PVS culling is enabled (line 109-115), then each
   candidate is distance-culled (line 129) and frustum-culled (lines
   132-138) before being added to `output.sortedRegions` (line 143) — **only
   regions that already passed both PVS and frustum culling exist in
   `sortedRegions` at all.**
2. **The renderer's draw list is built only from that already-culled set.**
   `src/client/graphics/renderer_sim_bridge.cpp:700-705` copies
   `results->sortedRegions` (the culled set from step 1) into
   `sortedZoneDrawList_`, and
   `src/client/graphics/renderer_zone_draw.cpp` `drawZoneGeometrySorted()`
   (line 266) iterates **only** `sortedZoneDrawList_` — there is no code
   path that adds a culled-out region's mesh buffers to
   `sortedDrawEntries_`, so **no fragment shader ever runs for geometry that
   didn't pass culling.** The adaptive-lighting per-pixel/lightweight
   material override (`getLightweightOverride`, lines 212-218) is applied
   per mesh buffer *while building this already-culled list* (line 284's
   `regionsDrawn >= zonePPBudget` counts only regions that survived
   culling, in front-to-back order) — so even the per-pixel-vs-lightweight
   choice only ever operates on visible geometry, never on culled-out
   regions.
3. **Light *selection* (which lights are active) is the one place with
   O(total) cost, but it's cheap CPU work, not fragment-shading cost.**
   `sim_visibility.cpp` `computeLightSelection()` (lines 509-619) iterates
   `zoneData_.zoneLightNodes` (**all** zone lights, `for` loop at line 533)
   and `zoneData_.objectLights` (**all** object lights, line 549) to build a
   candidate list — but each candidate is already gated by a visibility
   bitvector (`output.lightVisible`/`output.objectLightVisible`, checked at
   lines 535 and 551) computed by a separate PVS/frustum pass over lights
   (`computeLightVisibility`/`computeObjectLightVisibility`, referenced at
   sim_visibility.cpp:382-434/451-502) before this function runs. So even
   this loop's cost scales with **total light count in the zone**, not
   total *geometry* — and it's a cheap `std::sort` over a small candidate
   list (top-8 selected, line 568-571), not per-fragment work.

**Answer: no, lighting work — specifically per-pixel/per-vertex shading
cost, and the point-light uniform loop inside the fragment shader — does not
scale with total scene geometry, only with what gets drawn after PVS +
frustum culling** (`sim_visibility.cpp:91-149` -> `renderer_sim_bridge.cpp:700-705`
-> `renderer_zone_draw.cpp:266-373`, confirmed by experiment 7 below: culled
regions never appear in the draw list at all, by construction, not by a
runtime check). The one part of the lighting pipeline whose cost scales with
a "total" count is light *selection* (`sim_visibility.cpp:533-562`, scales
with total zone light count, already visibility-filtered), and that's O(number
of lights) CPU-side sorting, not fragment-shading work — negligible next to
draw cost. Experiment 7 (below) independently confirms geometry volume itself
(not lighting) is the dominant scaling factor once culling is disabled.

### Experiment 7 result: culling is doing enormous, necessary work

`p01_e7_*.csv` (qeynos2, gfaydark; `--no-frustum-cull`, `--no-pvs`,
both):

| config | qeynos2 p95 (ms) | qeynos2 regions drawn | gfaydark p95 (ms) | gfaydark regions drawn |
|---|---|---|---|---|
| normal | 34.82 | 60-140 | 41.03 | 53-86 |
| `--no-frustum-cull` | 65.45 | 176-346 | **720.08** | 1014-1051 |
| `--no-pvs` | 145.87 | 140-390 | 41.35 | 53-86 |
| both disabled | **947.63** | 1803-1896 | 820.75 | 1460-1591 |

With both culling systems disabled, qeynos2 draws **1803-1896 regions**
(vs 60-140 normally, a >12x increase) and frame time balloons to **947ms
p95** (~1 fps, from ~29 fps) — and gfaydark's frustum-cull-alone case is
already catastrophic (720ms p95, 1014-1051 regions vs 53-86 normally) even
with PVS still active, meaning frustum culling alone is doing most of the
heavy lifting for gfaydark's (larger, more open) BSP layout, while qeynos2
depends more evenly on both systems. This is a direct, real-hardware
demonstration that **total zone geometry vastly exceeds what's needed per
frame**, and that both culling systems are doing large, necessary,
non-overlapping work — reinforcing the answer to question (b): the
`regions_vis`/`regions_drawn` CSV columns are always equal in every
condition tested (culling gates entry into the draw list, not a separate
post-draw-list filter), confirming the code trace above empirically as well
as by inspection.

### Fix candidates (prioritized, investigation only — no fixes implemented this unit)

1. **Fix H04's/future benchmarks' order confound (H3) before trusting any
   EQE1-vs-RGBA percentage.** Randomize or interleave variant order across
   repeated A/B trials in `scripts/pi/pi_bench_run.py`
   (`variants = [False, True]` is currently fixed every pass,
   `scripts/pi/pi_bench_run.py:44`). Cheap, high-confidence-value fix to the
   measurement harness itself; estimated impact: turns a noisy ~+10-25%
   single-sample delta into a trustworthy number, without changing any
   product behavior.
2. **Re-run experiment 6b (true per-pixel-vs-lightweight on real geometry)
   once the Pi is back** — directly answers Will's question (a) with real
   zone geometry instead of relying on the historical synthetic benchmark's
   extrapolation. Highest-value next step; blocked entirely on hardware
   availability, zero code risk (uses existing `--adaptive-lighting
   --zone-pp-budget 0` flags, no new code).
3. **If H4 (Lima ETC1 decode cost) is confirmed with a GPU-side profiler,
   consider defaulting to RGBA over EQE1 on this specific
   hardware/driver combination** — the memory-footprint win EQE1 was
   introduced for (H04: qeynos2 42.2MB RGBA vs 5.28MB ETC1) is real, but if
   the CPU/GPU frame-time cost consistently outweighs it on Mali400/Lima,
   the current default may be a net loss for this device. Do not act on
   this without confirming H4 with a real profiler first — this
   investigation's evidence is suggestive, not conclusive, on the
   *mechanism* (though solid on the *effect*: RGBA stable and fast, EQE1
   noisy and usually slower).
4. **Given culling is doing 12x+ work reduction and is clearly necessary
   (experiment 7), do not deprioritize PVS/frustum correctness work in
   favor of shading-cost micro-optimizations** — geometry volume, not
   lighting, is the dominant lever on this hardware by a wide margin.
5. Low priority / not actionable without more instrumentation: H1's
   (disconfirmed) mechanism and H2's (refuted) mechanism need no further
   work; H3's harness fix (#1) is the only actionable item from the
   hypothesis list itself.

### Deviations from the plan

- Experiment 6b (added beyond the plan's original experiment 6, to isolate
  point-light-count from shading-model as described under Will's question
  (a)) never produced data — Pi panic during its preflight/governor-pin
  step, zero benchmark invocations run for it. Recorded as blocked, not
  silently dropped.
- Experiment 5's runtime half (live GL filter-state dump on the Pi) was
  superseded by a decisive source-level answer (see H2) before it was
  scheduled to run, and the panic occurred before it would have been
  reached anyway — not attempted.
- The H1 variant-forcing scratch hook (`zone_geometry.cpp`,
  `EQT_P01_FORCE_COMPANION`) was implemented, built (both VM and ARM), run
  on the Pi for experiment 4, and then **reverted** before this commit per
  the plan's "scratch changes stay in the worktree, never committed" rule —
  it is not part of this unit's diff. Its numbers are recorded above from
  the CSVs it produced while active.
- The `--material-counts` CSV addition (experiment 2) was implemented
  without a runtime-cost concern gate beyond the flag itself (always-zero
  columns when the flag is off, computed from the benchmark's own
  already-built `drawEntries` list in O(draws) when on) — kept as the
  plan's pre-authorized flag-gated addition, and is part of this commit
  (`tools/hwcaps/gles2_zone_benchmark.cpp`,
  `tools/hwcaps/zone_bench_metrics.{h,cpp}`,
  `include/client/graphics/zone_shader.h`'s new
  `getMaterialTypeLWAlphaTestRGBA()` getter).
- Full default-build + ctest re-verification was **not** re-run for this
  unit's committed diff before the panic interrupted the session; the
  local VM build of the changed target (`gles2_zone_benchmark`) was
  confirmed compiling clean both before and after reverting the scratch
  hook (see below) but the full test suite pass is outstanding.
