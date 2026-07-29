# P38 — Frame time-budget audit: the truth, not the KPI margin

Executed 2026-07-29 on `wt/slot1`, branch `task/P38-time-budget`, cut from `fable`
at `b74aca9`. Orange Pi One (Allwinner H3, Cortex-A7, Mali 400 via Lima),
`gles2_zone_benchmark`, befallen zone, `--entities 5` (Will's steering: a
realistic NPC count, not the P11-era stress calibration of N=205).

**Will's question**: pre-optimization e205 ran 54ms, and even now p95 GPU
sync is ~17ms and CPU submission ~9ms for ~8 draws. Why does a Voodoo1-era
zone with 5 NPCs cost that much on a Mali 400? This unit answers with
measurement, not more margin-shaving.

## Executive summary

**The realistic-load frame is cheap on average (11.1ms avg / 25.8ms p95 of
a 33.3ms budget — 66% headroom) and GPU-bound (54% of the frame), not
draw-call-bound.** The `~8 draws` / `~9ms submission` numbers Will quoted
are reproduced almost exactly here (draws_avg 8.7, submission avg 2.44ms /
p95 6.83ms) — this IS the already-optimized (P09-P14) shipped
configuration. The "why does it cost so much" answer has three parts:

1. **A dead config lie, found and fixed as a bounded local prototype,
   measures to ~0% on this replay** — `enableMipmaps: true` ships in the
   orangepi preset but is neutralized by two independent bugs (detailed
   below); fixing both, mipmapping genuinely doesn't help THIS scene at
   THIS camera distance. Honest null, not the fundamental cost.
2. **GPU sync (draw+rasterize+shade) is 54% of the average frame and 78%
   of the isolated fixed-pose frame** — for only 1,380 triangles and 8
   draws, a trivial load for a Mali 400 on paper. Per-pixel lighting
   accounts for a measured 22% of that GPU cost (lightweight lighting
   dropped GPU avg 6.02ms → 4.69ms). The remainder is unexplained by
   anything this audit could isolate cleanly (see the resolution-probe
   confound below) — **named as the single biggest open question**, not
   closed here.
3. **The tail (p95/max), not the average, is what's close to the 33.3ms
   budget** — driven by one already-identified dense-region camera pose
   (bf472-class, ~700+ merged regions / ~5,400 triangles vs the ~1,380
   average) that P14 already measured and already has a recommended,
   unlanded fix (`occlusionMaxOccluderRegions` 16→256).

## Budget table (befallen, `--entities 5`, shipped orangepi preset:
`--merge-zone-draws --composite-scenegraph --eqe1-path`, production
lighting config, 800x600, 1000-frame canonical replay unless noted)

| variant | total avg/p95 (ms) | submission avg/p95 | GPU-sync avg/p95 | collect/cull avg | swap avg/p95 | draws avg | tris avg | tex binds avg | texParam calls avg |
|---|---|---|---|---|---|---|---|---|---|
| **control (moving replay)** | **11.108 / 25.836** | 2.440 / 6.829 | 6.021 / 13.831 | 2.243 | 3.873 / 9.250 | 8.7 | 1,379.6 | 17.3 | 34.7 |
| fixed-pose (300f, camera held still) | 12.476 / 13.267 | 1.045 / 1.294 | 9.770 / 9.967 | 1.316 | 1.047 / 1.405 | 6.0 | 996.0 | 12.0 | 24.0 |

**30fps KPI line = 33.3ms.** Control sits at 33% of budget (avg) / 78% of
budget (p95) — real headroom on average, tight only at the tail.
`total_ms` does NOT include `swap_ms` (buffer-swap/page-flip, measured
separately because it happens after `total_ms` is stamped — see
`zone_bench_metrics.h`); true wall-clock pacing is `total_ms + swap_ms` ≈
**14.98ms avg** for the control.

**Fixed-pose is the cleanest single number for "what does this scene
fundamentally cost, with zero camera-path variance"**: 12.5ms avg, p95
barely above avg (13.267 vs 12.476 — a real scene has almost no run-to-run
noise once the camera stops moving), GPU sync alone is 78% of the frame
(9.77 of 12.48ms) for only 6 draws / 996 triangles. **This is the number
Will's question is really asking about** — a near-static, trivial scene
still spends 4-5x more time in GPU rasterize/shade than in CPU submission.

### Driver counters (glBindTexture / glTexParameteri per frame, `--gl-counters`,
new instrumentation this unit, kept committed — flag-gated, zero cost off)

At 8.7 draws/frame: 17.3 texture binds (2 per draw — diffuse unit 0 +
alpha-companion unit 1, `applyMaterialRenderState()`) and 34.7
`glTexParameteri` calls (4 per bound-texture-0 draw — mag/min filter +
wrap S/T, **unconditional every draw**, confirming P22's finding still
holds). At only ~35 calls/frame this is cheap in absolute CPU terms under
the shipped (merged) config — see the state-dedup result below for why
this matters far more in a regressed/unmerged scenario.

## Per-suspect measured deltas (all vs the control row above, same replay,
same Pi session unless noted; Δ% = (variant − control) / control)

### (a) Mipmaps — THE BANDWIDTH ELEPHANT CLAIM: quantified, and it is a NULL at this scene

**Two real, independent bugs found, both fixed together in one local
prototype (`WILLEQ_MIPMAP_PROTOTYPE=1`, reverted before commit):**

1. `COGLES2Texture::uploadImage()` (`docker/irrlicht-drm/COGLES2Texture.cpp`)
   never calls `glGenerateMipmap`, ever — P22's finding, reconfirmed by
   this unit's code read. Every texture has exactly mip level 0.
2. **New finding, not in P22's territory** (P22 examined the driver's own
   texture class; this lives in the higher-level cache):
   `ConstrainedTextureCache`'s three non-compressed upload sites
   (`src/client/graphics/constrained_texture_cache.cpp` ~L273, ~L737,
   ~L865, all `#ifdef EQT_HAS_DRM` — **ARM/Pi only, invisible on the x86
   dev tree**) set `GL_TEXTURE_MIN_FILTER` to `GL_LINEAR_MIPMAP_LINEAR`
   whenever `config_.enableMipmaps` is true (true for the shipped orangepi
   preset) — **but never call `glGenerateMipmap` either** (only the
   DXT-compressed upload path at L1392 does, and that path is dead in
   production: orangepi's `enableCompressedTextures` is `false`, the
   codebase uses native ETC1 instead). This requests mipmap-filtered
   sampling on textures with an **incomplete mipmap chain** — undefined
   per the GL ES2 spec, driver-dependent fallback behavior. Not just
   "missing mipmaps" (P22's framing) — actively requesting an invalid
   sampling mode.
3. Even that setting is moot in practice: `COpenGLES2Driver::applyMaterialRenderState()`
   (P22's territory) sets `MIN_FILTER` **unconditionally on every single
   draw call**, always to plain `GL_LINEAR`/`GL_NEAREST` (never a mipmap
   variant) — so whatever `ConstrainedTextureCache` set at upload time is
   stomped back to non-mipmapped before the very first draw. Two
   independent no-ops compound into a total no-op.
4. `calculateTextureSize()` still adds the 33%-larger mip-chain estimate
   to the texture memory *budget accounting* when `enableMipmaps` is true
   — the config flag has a real (if secondary) side effect: **phantom
   memory pressure that could trigger unnecessary LRU eviction**, even
   though no mip bytes are ever actually uploaded. Not measured
   separately in this unit (would need its own eviction-count probe);
   named as a candidate follow-up.

**The fix** (prototype only, reverted): (1) generate mips in
`COGLES2Texture::uploadImage()`, set `HasMipMaps=true`; (2) make
`applyMaterialRenderState()` respect `tex->hasMipMaps()` instead of always
overwriting to non-mipmap. Correctly closes both bugs as one fix (the
`ConstrainedTextureCache` override becomes correct once mips actually
exist).

**Measured (shipped-preset moving replay, Pi, `mipmap_on_shipped` vs control):**

| | total avg | total p95 | GPU avg | GPU p95 |
|---|---|---|---|---|
| control | 11.108 | 25.836 | 6.021 | 13.831 |
| mipmap fix | 11.143 | 26.017 | 6.029 | 13.891 |
| **Δ** | **+0.35%** | **+0.70%** | **+0.13%** | **+0.43%** |

**Honest null.** Within run-to-run noise, zero measured effect — worse if
anything (noise-level). At this replay's camera distances, texture
minification isn't severe enough to matter, and/or Lima's driver already
handles the incomplete-mipmap case gracefully (no crash, no visible glitch
in spot checks) rather than paying a penalty for it. **This is real
evidence against "mipmaps are the bandwidth elephant" as a claim about
THIS zone's THIS replay** — it does not rule out a win on zones/poses with
more oblique or distant texture viewing (e.g. long sightlines, terrain at
grazing angles), which this replay's indoor/near-camera path doesn't
exercise. Flagging as the honest scope limit, not overclaiming a universal
null.

### (b) Per-draw state churn (glTexParameteri dedup)

**A real bug was caught and fixed mid-unit, not silently absorbed**: the
first ARM binary build happened before this prototype's code was added
(a background-build-timing mistake), so the first two Pi measurements of
this suspect were run against a binary that didn't contain the fix —
`WILLEQ_STATE_DEDUP_PROTOTYPE=1` produced byte-identical `tex_param_calls`
counts to the control on that stale binary. Caught via a direct manual A/B
on the Pi (not accepted as a "0% effect" finding), ARM rebuilt, both
variants re-measured (`statededup_on_v2` / `statededup_on_shipped_v2`).

Prototype: per-GL-texture-object cache of the last-applied
mag/min-filter+wrap combo in `COpenGLES2Driver::applyMaterialRenderState()`,
skip the 4 `glTexParameteri` calls when unchanged (P22 already proved,
via GL-readback instrumentation, that every texture requests exactly ONE
wrap mode for its whole lifetime in the traced paths — this dedup is safe
by that same evidence, just never landed as an optimization).

**Measured (shipped-preset moving replay, Pi, corrected binary):**

| | total avg | submission avg | texParam calls avg |
|---|---|---|---|
| control | 11.108 | 2.440 | 34.7 |
| dedup fix | 11.056 | 2.397 | **0.03** |
| **Δ** | **−0.47%** | **−1.76%** | **−99.9%** |

**Real but small under the shipped/merged config** — call count drops
~1000x but the absolute count was already tiny (34.7/frame), so the CPU
win is marginal. **Measured separately under the unmerged/high-churn
counterfactual** (see below) where the same fix is far more valuable —
this is a defense-in-depth fix for a regression scenario, not today's
bottleneck.

### (c) Per-pixel vs lightweight lighting (pre-existing `--lighting-model` flag, no new code)

| | total avg/p95 | GPU avg/p95 | draws avg | swap avg |
|---|---|---|---|---|
| control (perpixel, default) | 11.108 / 25.836 | 6.021 / 13.831 | 8.7 | 3.873 |
| lightweight | 9.429 / 21.577 | 4.692 / 10.138 | 5.5 | 4.980 |
| **Δ** | **−15.1% / −16.5%** | **−22.1% / −26.7%** | −37% | +28.6% |

**The single largest measured lever in this audit.** Per-pixel player
lighting costs a genuine, substantial share of GPU time — consistent with
the project's own documented Mali 400 rule ("FS branches / normalize()
are extremely expensive... even trivial geometry"): the per-pixel path's
extra fragment-shader math (inversesqrt light distance, quadratic
attenuation, texture sampling) is real, measurable GPU cost even at only
~1,380 triangles.

**Notable, unplanned finding: `swap_ms` INCREASES when GPU work drops.**
`total_ms + swap_ms` is nearly constant across variants regardless of
internal split (control 14.98ms, mipmap 14.98ms, dedup 14.95ms,
lightweight **14.41ms** — only a partial, not full, floor). This is
consistent with vsync/DRM-page-flip frame pacing partially absorbing
compute headroom: finishing GPU work faster doesn't fully translate to a
faster *observed* frame once close to a page-flip cadence. Not conclusively
proven here (the floor moves slightly, doesn't hard-clamp) but named as a
real risk: **shaving GPU/CPU time below whatever this floor is may not
show up as real wall-clock wins**, which is exactly Will's "stop
optimizing the margin" instinct, evidenced rather than asserted.

### (d) Overdraw / fill-rate (resolution-scaling probe): CONFOUNDED, not a clean result

**Planned**: 400x300 vs 800x600, GPU-time ratio ≈ fill-bound share.
**Actual result**: GPU time got *5x worse*, not ~4x better, at the lower
resolution — and **draw count and triangle count also increased**
(draws 8.7→10.6, triangles 1,379.6→1,917.8), which is impossible if
resolution only affected fill/fragment cost. Confirmed reproducible under
both the unmerged and shipped-preset configs (same qualitative pattern
both times).

| | total avg/p95 | GPU avg/p95 | draws avg | tris avg |
|---|---|---|---|---|
| 800x600 (control) | 11.108 / 25.836 | 6.021 / 13.831 | 8.7 | 1,379.6 |
| 400x300 | 36.509 / 74.372 | 29.584 / 65.153 | 10.6 | 1,917.8 |

**This is a genuine, reproducible, but UNCONFOUNDED-nothing result — the
naive "lower resolution isolates fill-rate" assumption is simply wrong for
this codebase.** Something in the visibility pipeline (PVS/frustum culling
or the software occlusion culler) makes MORE geometry survive as
"visible" at lower target resolution, not less. `SoftwareOcclusionCuller`'s
own offscreen buffer is a fixed 64x32 (`occlusionBufferWidth/Height`,
independent of `--res`), so the mechanism isn't obviously that buffer —
root cause not identified in this audit's budget. **Reported as an honest
null-with-a-twist, not a fill-bound percentage** — a real, separate bug
worth its own follow-up unit before this probe can answer the fill-bound
question cleanly.

### (e) Swap/vsync stall isolation

No new experiment needed — `swap_ms` was already split out by P08-era
instrumentation (`zone_bench_metrics.h`, captured after `endScene()`,
separately from `total_ms`). Control: swap avg 3.873ms / p95 9.250ms — a
**real, non-trivial cost comparable in magnitude to submission** (2.440ms
avg), not included in the commonly-quoted `total_ms` figure. See finding
(c) above for the more interesting result: swap time appears to partially
absorb GPU/CPU savings from other optimizations, suggesting a display
frame-pacing floor somewhere near ~14.4-15.0ms wall-clock for this
hardware/config, independent of compute.

### (f) Counters reveal (draw-call breakdown at the dense-region tail)

The shipped-preset control's own tail (frames 478-491, the same bf472-class
dense pose P14 already characterized) shows the real story behind the
p95/max gap, even with `--merge-zone-draws` already engaged:

| | draws | triangles | regions drawn | GPU ms |
|---|---|---|---|---|
| whole-run avg | 8.7 | 1,379.6 | ~60-70 | 6.021 |
| bf472-class tail (frame 487) | 13 | 5,414 | 760 | 16.843 |

**Merge already collapsed the draw-call count at this pose (13, not the
~700+ it would be unmerged — see the counterfactual below) — what remains
is genuine triangle/fill load**: 4x the average triangle count, driving
GPU time to ~2.8x the average. This is squarely **P14's already-identified,
already-quantified, already-recommended, still-unlanded fix**
(`occlusionMaxOccluderRegions` 16→256 in the orangepi preset — P14 showed
this closes the whole-path p95 KPI gap on the N=205 stress replay; not
re-verified at N=5 in this unit, but the mechanism — more occluder budget
prunes exactly this dense-region tail — applies identically).

## Counterfactual: what an UNMERGED zone costs (not what ships — a
regression/magnitude sanity-check, kept for context)

**Deviation flagged plainly**: the first 5 Pi invocations of this unit
(`budget_moving_e5`, `budget_fixedpose_e5`, `mipmap_on`, `statededup_on`,
`both_protos_on`, `lightweight_lighting`, `res_400x300`) were run WITHOUT
`--merge-zone-draws --composite-scenegraph --eqe1-path` — mirroring the
`constrained_renderer_config.cpp` hardcoded-preset fallback function
(`mergeZoneDraws = false` there) instead of the actual SHIPPED runtime
preset (`config/constrained_presets.json`'s `"orangepi"` block, which sets
`"mergeZoneDraws": true` and `"enableCompositeEntities": true` — the JSON
file the client actually loads). Discovered only after these runs landed,
by tracing why a "realistic 5-NPC load" was producing 38ms averages and a
**231ms submission spike** (frame 478, 1,459 draws, 775 regions) — the
unmerged, uncomposited baseline reproduces the classic P09/P10-era
draw-call explosion at dense-region poses (each of ~700+ zone regions
becomes its own draw + full state resend), which the shipped config
already fixes via `mergeZoneDraws`. **These numbers do NOT represent
current production** — they are kept below as a magnitude sanity-check
(showing what regressing `mergeZoneDraws` would cost) and because they
usefully re-confirm the mipmap/dedup suspects hold the same sign at a much
higher-churn operating point.

| variant (unmerged, uncomposited) | total avg/p95 | submission avg/p95 | draws avg | texParam calls avg |
|---|---|---|---|---|
| control | 38.497 / 170.691 | 22.910 / 103.906 | 263.0 | 1,052.1 |
| mipmap fix | 37.969 / 179.668 | — | 263.0 | 1,052.1 |
| state-dedup fix (corrected binary) | 37.847 / 174.622 | 22.401 / 102.701 | 263.0 | **0.2** |
| lightweight lighting | 37.082 / 170.296 | 23.035 / 106.611 | 263.0 | 1,052.1 |

Mipmap: −1.4% total (small, same-signed null as the shipped-config
result). State-dedup: −1.7% total / −2.2% submission despite eliminating
99.98% of `glTexParameteri` calls — confirms the GL call itself is cheap
on this driver; the value of dedup is in avoiding a much larger number of
calls at scale, not in each call being expensive. Lightweight lighting:
−3.7% total (smaller relative win than under the shipped/merged config —
consistent with GPU time being a smaller share of a submission-dominated
frame).

## Ranked fundamental fixes (sized as follow-up units)

1. **Land `occlusionMaxOccluderRegions` 16→256 in the shipped orangepi
   preset** (P14's already-measured, already-recommended, still-unlanded
   fix). Directly attacks finding (f) — the dense-region tail that pushes
   p95 toward the KPI line even under the current merged config. P14
   measured this closing the whole-path p95 gap on the N=205 stress
   replay; this unit's N=5 tail (bf472-class pose, frame 478-491 class)
   is the exact same mechanism. **Sizing: trivial (one config value
   change, already validated, zero new code)** — the only reason it
   hasn't shipped is that it needed a report, not more code. Highest
   confidence, lowest cost, most directly evidenced.
2. **Fix or remove the `enableMipmaps` config lie.** Two options: (a) land
   the P38 mipmap prototype properly (generate mips, respect
   `hasMipMaps()` in `applyMaterialRenderState()`) even though this unit's
   replay showed ~0% win — the config flag currently does nothing but
   request undefined-behavior sampling and inflate the texture memory
   budget by 33% for zero benefit, so fixing it is a correctness/hygiene
   win independent of performance, and may matter on zones/poses with
   more distant/oblique texture viewing than this replay exercised; or
   (b) if a broader survey confirms no zone benefits, flip
   `"enableMipmaps": false` in the shipped preset and delete the dead
   `ConstrainedTextureCache` mip-filter-setting code, recovering the
   phantom 33% memory-budget overhead (could reduce unnecessary texture
   LRU eviction). **Sizing: small** (surgical driver/cache change,
   already prototyped in this unit, needs only a wider-replay validation
   pass before landing either direction) — a good next unit precisely
   because the code and the measurement method already exist.
3. **Root-cause the resolution-dependent visibility-count anomaly (finding
   d).** A 400x300 render showed MORE draws and triangles than 800x600 —
   structurally wrong regardless of any performance question, and it
   blocks ever cleanly measuring fill/overdraw cost via resolution
   scaling. **Sizing: medium** (needs its own investigation — likely in
   `zone_bench_loader.cpp`'s PVS/frustum setup or the occlusion culler's
   screen-space math — this unit only detected and reproduced it, did not
   root-cause it).
4. **Land the state-dedup glTexParameteri fix (finding b) as defense in
   depth**, bundled with #1 or #2 since it touches the same
   `applyMaterialRenderState()` function region. Small measured win today
   (−0.47% total under the shipped config) but protects against a much
   larger regression if `mergeZoneDraws` or `enableCompositeEntities` were
   ever disabled/regressed (the unmerged counterfactual shows the same
   fix eliminating 99.98% of calls at a 30x-higher operating point).
   **Sizing: trivial** (already prototyped, ~10 lines, P22 already proved
   the safety precondition).
5. **Investigate the swap_ms floor (finding c/e).** If real, it means
   further GPU/CPU optimization below ~14-15ms wall-clock buys nothing
   observable — worth confirming with `eglSwapInterval(0)` (currently
   never called, so EGL's default vsync-on behavior applies) as a
   diagnostic-only experiment (never shippable with tearing, but would
   prove/disprove the floor decisively) before spending more budget
   chasing GPU-side wins below that line. **Sizing: small, diagnostic
   only** — directly tests Will's "stop optimizing the margin" hypothesis.
6. **Explain the residual GPU-sync cost** (the still-open ~5-6ms avg /
   ~10-14ms p95 GPU time for a genuinely trivial 1,380-tri / 8-draw
   scene, after accounting for lighting-model cost via #2's sibling
   finding (c)). Lightweight lighting explains 22% of it; the remainder
   is unexplained by anything in this audit's scope (mipmaps: null;
   state churn: negligible CPU-side, doesn't touch GPU; resolution probe:
   confounded). Candidates not yet tested: shader program complexity
   beyond the lighting model (fog computation, vertex counts per draw),
   texture format/bandwidth (ETC1 decode cost on Lima), or Mali
   400-specific tiler/binning overhead independent of triangle count.
   **Sizing: medium-large** — needs its own instrumented investigation
   (e.g. a stripped-down shader A/B beyond just lighting-model, following
   the existing `gles2_shader_perpixel_benchmark` pattern) — the single
   biggest named unknown this audit leaves open, and the most likely
   candidate for "the fundamental cost" Will is asking about, but not
   closed here.

## Pi session discipline

15 timed data-producing Pi invocations landed (+ 2 small untimed
governor-neutral debug verification runs used to root-cause the
state-dedup binary-staleness bug, not counted as measurement data) across
10 script launches, 2 SessionAbort recoveries handled cleanly per protocol
(`sync && drop_caches`, no reboot, governor restored each time — CmaFree
recovered 3,632→80,040 kB, 20,304→82,004 kB, 32→79,796 kB, 136→82,004 kB
across the 4 abort events) — total well under the 24-invocation
(2-session × 12) ceiling. Pi left pristine: governor `ondemand` on all 4
cores (verified), `/tmp` clean of all staged binaries/CSVs/logs/path files
(verified via `ls /tmp` showing only unrelated `systemd-private-*` dirs)
at final exit.

## Deviations from the plan

- **A real methodology bug, caught and corrected mid-unit, not silently
  absorbed**: the first 7 Pi invocations used the wrong "orangepi preset"
  reference (the `constrained_renderer_config.cpp` hardcoded-preset
  fallback, `mergeZoneDraws=false`) instead of the actual shipped runtime
  preset (`config/constrained_presets.json`, `mergeZoneDraws=true`).
  Caught by investigating an implausible 231ms submission spike, not
  accepted as a headline number. Corrected with 8 additional invocations
  under the true shipped-preset flags; the wrong-config numbers are kept
  as a labeled counterfactual (magnitude sanity-check for a
  merge-regression scenario), never presented as production numbers.
- **A second real bug, also caught and corrected**: the ARM binary used
  for the first state-dedup measurements (both unmerged and shipped
  variants) was built before the state-dedup prototype code was added to
  `COpenGLES2Driver.cpp/.h` (a background-build-timing mistake — the ARM
  build was launched, then more prototype code was added while it was
  still running, and the binary was never rebuilt before those two Pi
  invocations). Caught via a direct manual on-Pi A/B showing
  byte-identical `tex_param_calls` with and without the env var — not
  accepted as a "0% effect" finding. ARM rebuilt, both variants
  re-measured with `_v2` labels; the stale-binary numbers are not used in
  any reported delta.
- The resolution-scaling probe (suspect d) did not produce the planned
  fill-bound percentage — a genuine, reproducible confound (lower
  resolution increases visible geometry, not just fill cost) made the
  naive interpretation invalid. Reported honestly as an unresolved,
  separate finding rather than forcing a number.
- The mipmap prototype's ETC1-compressed upload path
  (`COGLES2Texture::uploadETC1()`) was intentionally left unmodified —
  `glGenerateMipmap` on an ETC1-compressed base texture is unsupported by
  the driver's own decode path (no software mip-level generation exists
  for compressed formats here); scoped out rather than risking an
  undefined-behavior experiment. The RGBA upload path
  (`uploadImage()`) is the one actually exercised by this replay's
  textures (orangepi's `enableCompressedTextures: false`), so this scoping
  does not affect the measured result.
- The fixed-pose replay uses a new synthetic path fixture
  (`benchmarks/paths/befallen_fixedpose_300f.path`, committed) rather than
  the benchmark's native `--camera-pose` static-capture mode — that mode
  is zone-geometry-only (no entities, no `--gl-counters` wiring), so it
  couldn't isolate "camera-path variance vs. GPU/driver cost" the way
  this unit needed; reusing the `--path-file --animate` pipeline with a
  repeated pose was the smallest change that kept entities and counters
  in the loop.
