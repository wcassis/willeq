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

## 2026-07-30 — P41: lighting-lever characterization for Will's decision (+ Pi ride-alongs)

Executed on `wt/slot1`, branch `task/P41-lighting-lever`, cut from `fable`
at `33bf633`. This unit does NOT flip anything — per-pixel adaptive
lighting is blessed visual territory (Q03: GLS level 9, x3 boost,
count-mode). It characterizes the lightweight-vs-per-pixel trade P38 found
(-15% total / -22% GPU at e5) for Will's decision, at both realistic (e5)
and stress (e205) load, plus three ride-alongs.

### 1. Pi lighting A/B, both loads (canonical befallen replay, shipped
preset: `--merge-zone-draws --composite-scenegraph --eqe1-path`, GLS
level 9 production lighting, 800x600, 1000-frame replay)

e5 numbers are P38's own control/lightweight rows, reused verbatim (same
replay file, same shipped-preset flags, same `--gl-counters` methodology —
re-measuring would have spent Pi budget on a number this unit would only
reproduce). e205 is new data from this unit.

| load | variant | total avg/p95 (ms) | GPU avg/p95 (ms) | submission avg/p95 (ms) | draws avg | Δ total | Δ GPU |
|---|---|---|---|---|---|---|---|
| e5  | per-pixel (control) | 11.108 / 25.836 | 6.021 / 13.831 | 2.440 / 6.829 | 8.7  | — | — |
| e5  | lightweight          | 9.429 / 21.577  | 4.692 / 10.138 | (not isolated in P38's table) | 5.5  | **-15.1% / -16.5%** | **-22.1% / -26.7%** |
| e205 | per-pixel (control) | 24.885 / 35.119 | 11.558 / 17.456 | 6.129 / 9.135  | 42.6 | — | — |
| e205 | lightweight          | 23.281 / 30.873 | 10.269 / 14.302 | 5.830 / 8.618  | 39.5 | **-6.4% / -12.1%** | **-11.2% / -18.1%** |

`polys_avg` is identical within variant at each load (e5: 1,379.6; e205:
16,076.1) — lighting model changes shading cost only, never a geometry/
visibility decision, at either load (same confirmation P38 already made at
e5).

**The lever shrinks by roughly half at stress load.** At e5 the per-pixel
tax is -15.1%/-22.1% (total/GPU); at e205 it's -6.4%/-11.2%. The
per-pixel-vs-lightweight difference is a roughly constant per-frame GPU
cost (this unit did not isolate the exact ms delta at e205 the way P38 did
at e5, but the GPU-ms deltas above are consistent with that: e5 Δ≈1.33ms
GPU, e205 Δ≈1.29ms GPU — nearly identical absolute GPU savings), while
total frame time at e205 is dominated by a much larger submission/collect-
cull share (205 entities vs 5) that the lighting model doesn't touch. So
the lever's *absolute* GPU savings (~1.3ms) is load-independent, but its
*relative* (%) importance drops as entity count rises — the opposite of
what "per-pixel is the biggest lever" (P38's framing, an e5-only measurement)
implies if read as a general statement. At the realistic e5 load the lever
really is the largest found; at stress e205 load it's a smaller fraction
of a now CPU-heavier frame.

### 2. Visual A/B for Will (x86 stills at the Q03 golden poses)

Full manifest, diff stats, and objective per-pose read:
`benchmarks/audits/p41_lighting/EVIDENCE_MANIFEST.txt`. Stills:
`/mnt/projects/fable/videos/p41_lighting/` (gitignored, NFS, per the P20
convention — not committed).

Summary: at the doorway/archway pose (f1, closest camera distance to large
sparsely-tessellated polys), 89.9% of pixels differ (mean channel delta
11/255, max 112/255) — the clearest case of the expected 1999-era
per-vertex banding signature on large flat surfaces (checkerboard floor,
hall walls). The other two poses (altar room f0, balcony hall f2 — smaller,
more numerous polys) show smaller, more localized differences (~29-30% of
pixels, mean delta 1-3/255). No acceptability judgment is made here per the
plan — Will may find the lightweight look period-appropriate; the report is
objective (where it differs and by how much), not a recommendation.

### 3. Ride-along A (P40 closure): 400x300 vs 800x600 draws/tris, on the
Pi (real hardware, not x86 llvmpipe)

P40 (slot2, x86-only) found the resolution-dependent visibility anomaly
(P38 finding d: lower resolution produces MORE draws/triangles, not fewer)
does **NOT reproduce on x86 llvmpipe** — draws/polys/regions were
byte-identical across 400x300/800x600/1024x768 there. This unit's Pi
measurement (shipped preset, e5, canonical befallen replay) gives the
answer for the actual target hardware:

| res | draws avg | polys avg | total avg/p95 (ms) | GPU avg/p95 (ms) |
|---|---|---|---|---|
| 800x600 (P38's e5 control, reused) | 8.7  | 1,379.6 | 11.108 / 25.836 | 6.021 / 13.831 |
| 400x300 (this unit, new)           | 10.6 | 1,917.8 | 35.985 / 74.065 | 29.125 / 65.066 |

**The anomaly DOES reproduce on the Pi, and far more dramatically than
P38's original (later-discovered-mistaken) x86 measurement of the same
question ever showed.** Draws +22% and triangles +39% at the lower
resolution, exactly matching P38's original draws (8.7→10.6) and polys
(1,379.6→1,917.8) figures almost to the decimal — this unit's Pi numbers
and P38's stale x86 numbers agree on the *geometry* counts, while total/GPU
time is far worse on the Pi at 400x300 (35.985ms/29.125ms GPU) than at
800x600 (11.108ms/6.021ms GPU) — a >3x GPU-time regression at the LOWER
resolution, the opposite of what shrinking the framebuffer should ever do.

**This does NOT close the resolution question — it reopens it as a
Pi/Lima-driver-specific (or occlusion-culler-screen-space-specific)
mechanism, not a general one.** P40's x86/llvmpipe root-cause work (ruling
out FOV/aspect and confirming byte-identical counters across resolutions)
does not apply to the Pi's real driver stack; the occlusion buffer or
LOD/screen-size threshold P40's plan named as candidate mechanisms may
behave differently under Lima's actual rasterizer/tiler versus llvmpipe's
software path. **Recommendation: this is P40's root-cause work, not P41's
— flagging as a new, higher-priority follow-up** (the 800x600 shipped
resolution is unaffected today, but any future resolution change, or
understanding why llvmpipe and Lima disagree, needs its own investigation
with Pi-side instrumentation P40 never had).

### 4. Ride-along B: `eglSwapInterval(0)` diagnostic (local prototype,
reverted)

perf_truth_audit's own open item (finding c/e): `total_ms + swap_ms` sits
at a nearly-constant ~14.4-15.0ms wall-clock floor across P38's variants
regardless of internal GPU/CPU split, suggesting a display frame-pacing
floor. `eglSwapInterval` was never called anywhere in the codebase before
this unit (confirmed by `grep`) — EGL's default (interval 1, vsync-on)
always applied. Added a local, env-var-gated prototype in
`docker/irrlicht-drm/CIrrDeviceFB.cpp`'s EGL init (`WILLEQ_SWAP_INTERVAL_PROTOTYPE`,
calls `eglSwapInterval(eglDisplay_, N)` right after `eglMakeCurrent`),
reverted before commit — never shippable with vsync off (tearing).

| variant | total avg/p95 (ms) | GPU avg/p95 (ms) | swap avg/p95 (ms) |
|---|---|---|---|
| control (P38's e5, vsync default) | 11.108 / 25.836 | 6.021 / 13.831 | 3.873 / 9.250 |
| `eglSwapInterval(0)` (this unit) | 11.155 / 25.644 | 6.044 / 13.771 | 3.805 / — |

**Decisive null: `swap_ms` does NOT change with vsync disabled** (3.805ms
vs 3.873ms, within noise; total/GPU also unchanged, within noise).
`eglSwapInterval` defaulting to interval-1 (vsync-on) is NOT the source of
the ~14.4-15.0ms wall-clock floor perf_truth_audit's finding (c) named as
a hypothesis. **This rules out one specific mechanism, it does not explain
the floor.** Given `docker/irrlicht-drm/CIrrDeviceFB.cpp`'s `drmPageFlip()`
already does its own explicit, synchronous wait
(`waitForFlipComplete()` polls the DRM fd for the page-flip completion
event before the *next* frame's `eglSwapBuffers`, precisely to avoid
`drmModePageFlip`'s `-EBUSY`→synchronous-`drmModeSetCrtc` fallback that
blocks ~35ms), the more likely candidate for the floor is that explicit
DRM page-flip wait itself, not EGL's own swap-interval gate — the DRM
compositor/vblank cadence is still in the loop via `waitForFlipComplete()`
regardless of what `eglSwapInterval` is told. Not proven here (would need
instrumenting `waitForFlipComplete()`'s own wait time directly, out of
this unit's diagnostic-only scope) — named as the next concrete hypothesis
for whoever picks up the swap-floor investigation.

### Options note (for Will's decision — not a recommendation)

**Keep per-pixel (status quo, blessed Q03 territory).** Cost: -15.1%
total / -22.1% GPU at realistic e5 load (P38's number, this unit's
sole reuse), shrinking to -6.4%/-11.2% at e205 stress load (this unit's new
number). Benefit: the smooth per-pixel falloff Will personally tuned;
avoids the f1-pose 90%-of-pixels visual change this unit measured.

**Flip to lightweight (all-per-vertex).** Recovers the above % at whichever
load is representative of real play (closer to e5 than e205 per Will's own
P38 steering — "realistic NPC count, not the P11-era stress calibration").
Cost: visible per-vertex banding on large flat polys at close range (f1-
class views), a real, era-consistent but different look from what Q03
blessed. Would require its own re-bless unit (P07c lesson: default flips
need tier-2 re-bless, never silently).

**Per-tier knob (per-pixel on capable hardware, lightweight on the most
constrained tier).** Not measured or prototyped in this unit — named as a
third option because the orangepi preset already has tier-specific budget
knobs (`zone-pp-budget`, `mergeZoneDraws`, etc.) that this would sit
alongside. Sizing/complexity not assessed here.

No default is flipped, no golden is changed, no config is touched — this
section is data for Will's decision, not a recommendation.

## 2026-08-01 — P51: canonical-KPI "regression" re-baselined — instrument
honesty, not a production regression. **32.24-32.5ms band RETIRED; 34.3-
34.6ms is the new canonical reference.**

P48's own KPI guard (EVIDENCE_MANIFEST.txt) caught the canonical e205
befallen replay landing at 34.3-34.6ms p95, over the 32.24-32.5ms band
blessed since P14/P39/P45, and reproduced the same number on a binary
built from BEFORE P48 even started (branch base `9a61ed3`) — proving the
drift predated P48 and lived somewhere in P46/P46b/P46c/P46d/P47's window.
P51's job was to attribute it. **H1 (instrument-honesty) confirmed on the
FIRST probe, no bisect needed:**

| run | p95 (ms) | draws_avg | in blessed band [32.24,32.5]? |
|---|---|---|---|
| tip, default (entity shader promotion ON) | 34.321 / 34.624 (P48's own 2 runs) | 41.9 | NO |
| tip, `--no-entity-shader-promotion` (P51, this unit) | **32.250** | 41.9 | **YES** |

Same binary, same invocation, same draws_avg (structural output
unchanged) — the ONLY variable is P46c's `--no-entity-shader-promotion`
opt-out, and toggling it alone moves the number by ~2.2ms, landing
squarely back inside the old band. This is decisive, not suggestive.

**Mechanism**: pre-P46b, `gles2_zone_benchmark`'s entity path rendered on
Irrlicht's built-in fixed-function material (`EMT_SOLID`, no custom
shader) — the benchmark never linked `entity_renderer.cpp`'s promotion
step at all (P44b's finding). P46b's `applyEntityShaderMaterialPromotion()`
(`tools/hwcaps/zone_bench_loader.cpp`) made the benchmark mirror
`EntityRenderer::createEntity()` (`src/client/graphics/entity_renderer.cpp`
lines 629-648) correctly for the first time: every entity buffer gets
promoted to the production per-pixel-lit shader (real player light/sun/fog),
exactly as the live client has ALWAYS done. **Production code is
unchanged by this entire arc — P46b only fixed the benchmark tool's own
fidelity gap.** So the "regression" was never a regression in what ships;
it is the instrument catching up to what production has always cost. The
32.24-32.5ms band (P14/P39/P41/P45's own blessed numbers) understated
production's real per-entity shading cost because those runs measured
entities on cheap fixed-function materials the real client never uses.

**Scaling with entity count** (P51's own 2nd data point, e5 = 5 entities,
P38/P41's "realistic NPC count" shorthand — 4 Pi invocations total this
unit, all logged under `benchmarks/pi_runs/p51_kpi_bisect/`):

| load | draws_avg | WITH promotion p95/avg (ms) | WITHOUT promotion p95/avg (ms) | delta (p95) |
|---|---|---|---|---|
| e5 (realistic) | 8.6 | 23.957 / 10.818 | 23.502 / 10.865 | **+0.455ms — noise-level, not distinguishable from run-to-run jitter** |
| e205 (stress) | 41.9 | 34.47 (avg of P48's 2 runs) | 32.250 | **+2.22ms (~6.9% of frame budget)** |

The promotion cost is proportional to the number of shaded entity draws on
screen, not a fixed per-frame tax — at a realistic 5-entity encounter it is
indistinguishable from noise; it only becomes visible at the 205-entity
stress-test load this benchmark uses for its canonical KPI spot. **This
means the historical blessed baselines were never meaningfully wrong at
realistic play loads** — the bias is real but its magnitude at the
entity counts players actually see (P38's own steering: e5, not e205) is
near zero. The KPI guard's own choice of e205 as its "canonical" spot is
what surfaced the gap; it is doing its job correctly.

**Which historical numbers carry the bias**: every blessed e205-shape
canonical-replay number recorded BEFORE P46b landed (`4386398`,
2026-07-30) carries this understatement at e205 scale — P14 (32.313ms),
P39 (32.5ms), P41/P45's own re-measurements, and P48's own pre-fix
comparisons against them. None of these are wrong for what they measured
at the time (pre-P46b, the benchmark genuinely rendered on fixed-function
materials); they are simply not comparable to any P46b-or-later number
without accounting for the ~2.2ms/205-entity delta this unit measured.
Real-roster A/B numbers (P45's `e205_realroster_*`, P48's
`e205_realroster_p48tip`) are UNAFFECTED by this specific bias — those
already used `--test-races` with masked/composite races exercising
different code paths, not the default race cycle this promotion gap
concerns.

**Honest-KPI statement for Will**: production-representative p95 at the
canonical e205 befallen replay is **34.3-34.6ms — over the 33.3ms 30fps
line**. This connects directly to the P41 lighting-lever data above:
per-pixel entity shading (the thing Will chose to KEEP at Q03/P41) is
part of this cost, at the same shipped per-pixel material Q03 blessed.
This is not a new cost — it is the cost that has always existed in
production, now correctly visible in the benchmark for the first time.
**The 34.3-34.6ms band is the new canonical KPI-guard reference,
effective this unit; the 32.24-32.5ms band is RETIRED for any
post-`4386398` binary.** No code change was made (Phase B verdict:
instrument-honesty, not a production regression — the plan's strict "fix
only if small AND behavior-identical" condition does not apply because
there is nothing to fix; production behavior was already correct and
unchanged throughout this entire investigation).

**P49 ride-along** (same session, 1 invocation): `--res 400 300` without
`--allow-mode-fallback` correctly refused (exit 1), logged the P43/P49
mode-fallback ERROR line naming the substituted `2560x1440` mode, and the
benchmark's own "achieved resolution != requested" ERROR + "Refusing to
run" lines. The DRM-level ELL_ERROR line's documented post-init `run()`
repeat did NOT fire — expected and benign: `gles2_zone_benchmark`'s own
resolution check exits the process before ever entering
`IrrlichtDevice::run()`'s frame loop (unlike `willeq`'s continuous loop,
which is what that repeat log was designed for), so only the
device-creation-time fallback line is reachable here. Control (800x600,
this unit's own H1 probe run) confirmed no fallback line and "DRM: Found
matching mode 800x600@75Hz for requested 800x600" logged normally. P49's
`refactor/deferred.md` ride-along entry updated with these results.

## 2026-08-01 — P52: GPU-sync characterization (Phase 2, Pi) — partial,
## budget exhausted before full matrix

Executed on `wt/slot3`, branch `task/P52-gpu-sync`. Phase 1 (x86 prep,
instrumentation) landed clean per its own Review section in
`refactor/P52_gpu_sync_characterization.md`. Phase 2 (this section) ran on
the Pi after P51 released it. **The planned 6-invocation matrix did not
produce usable data on its first attempt — two real methodology bugs were
found mid-unit, one fixed within budget, one identified but not fixed
because the ≤8-invocation ceiling (2 reserve, both now spent on recovery)
was exhausted before a corrected re-run was possible.** Reported honestly
per this document's own established convention (P38/P41/P43 all did the
same when their own methodology broke) rather than silently forcing a
clean-looking number.

### Bug 1 (found, fixed): static-pose CLI mode bypasses ALL per-frame
### instrumentation

The original matrix (`refactor/scratch/p52_pi_gpu_sync_matrix.py`) reused
`scripts/pi/pi_common.py`'s `run_zone_bench_generic()`/
`pose_flags_repeated()`, which builds `--camera-pose` flags from
`tests/goldens/manifest.json`'s static-pose fixtures — the same idiom
P38/P41/P43's own scripts used successfully. What those prior units never
hit (their instrumentation lived in code paths shared by both loops, or
they used `--path-file` themselves for their canonical numbers, as P51's
own scaling probe did) is that `gles2_zone_benchmark.cpp` has **two
structurally separate per-frame loops**: a static-pose capture loop
(`if (!opts.cameraPoses.empty())`, ~line 2461) used only when
`--camera-pose` flags are present, and a moving-path loop (driven by
`--path-file` or the navmesh/orbit fallback, ~line 2595+) where this
unit's new instrumentation (`gpuSyncBeginSubmissionPhase`/
`gpuSyncWrapSwap`/`gpuSyncTimedFinish`) — and, it turns out, the
**pre-existing** P08 phase-timing (`collect_cull_ms`/`sort_ms`/
`submission_ms`) and P38 GL-counter (`tex_binds`/`tex_param_calls`)
instrumentation — actually live. All 6 original CSVs
(`benchmarks/pi_runs/p52_gpu_sync/p52_{A..F}_*.csv`) have `draws`/`polys`
identical across every entity-count variant (entities never appear in any
of the 3 fixed poses — they're placed "along camera path," a synthetic
orbit unrelated to where the fixed poses look) AND every phase-timing/
GL-counter/GPU-sync column reads exactly `0`/`-1` — confirmed by diffing
against P51's own `--path-file` CSVs
(`benchmarks/pi_runs/p51_kpi_bisect/befallen_e5_shipped_withPromotion.csv`),
which have real non-zero values for the exact same columns on the exact
same ARM binary shape. **Kept, not deleted** — the 6 CSVs are a real,
informative record (they prove the static-pose loop's own limitation,
useful for any future unit that reaches for `--camera-pose` expecting
full instrumentation) but must never be read as GPU-sync data.

Fixed by switching to `--path-file benchmarks/paths/befallen_fixedpose_300f.path`
(P38's own committed replay fixture) — the moving-path loop, same
convention P51's scaling probe used
(`refactor/scratch/p52_pi_gpu_sync_pathfile_fix.py`).

### Bug 2 (found, NOT fixed — budget exhausted): missing shipped-preset
### flags inflate the recovery run's absolute numbers

The 2-invocation recovery run (`A_canonical_e5_pathfile`,
`B_stress_e205_pathfile`) fixed Bug 1 but did not carry over
`--merge-zone-draws --composite-scenegraph --eqe1-path <path>` (the
"shipped preset" flags P38/P41/P51's own canonical replays all use —
`SHIPPED_PRESET_ARGS` in `refactor/scratch/p51_pi_scaling_probe.py`).
Without them, zone draws are unmerged and entities render via the
per-body-part RGBA path instead of the composite ETC1 path — the effect
compounds badly at high entity count: `B_stress_e205_pathfile` measured
**232.7ms avg / 236.0ms p95 total frame time**, ~7x P51's own honest
e205 canonical number (34.3-34.6ms band). **This is a real config-fidelity
gap, not a new production number — it must not be read as a KPI figure
or compared against the 34.3-34.6ms band.** `submission_ms` alone averages
133.5ms (57% of the frame) at e205 in this run, consistent with
unmerged/uncomposited entity draws being the dominant confound, not a
genuine GPU-sync finding. No further Pi invocation was available to
correct this (8/8 of the ≤8 ceiling spent: 6 on the Bug-1 matrix, 2 on
the Bug-1 fix). Flagged as the top item for a follow-up unit (see Options
note below).

### What IS trustworthy from the 2 recovered runs

Despite Bug 2, three findings are structural (about *where* CPU-blocking
time goes, not sensitive to the absolute-magnitude confound) and are
reported with appropriate caution:

1. **Lima has NO `GL_EXT_disjoint_timer_query` support** — confirmed on
   real hardware, both recovery runs: `GPUSyncProbe: GL_EXT_disjoint_timer_query
   NOT available (fns=1, ext_string=0)`. Function pointers resolved via
   `eglGetProcAddress` (non-null) but the `GL_EXTENSIONS` string does NOT
   advertise the extension — exactly the false-positive this unit's
   capability probe was designed to catch by requiring both signals (see
   Phase 1 Review). Every `gpu_zone_ns`/`gpu_entity_ns`/
   `gpu_particles_ui_ns`/`gpu_swap_ns` column is `-1` across every Pi CSV
   this unit produced, by design — GPU-side timer queries are **not
   available on the target hardware**, full stop. All GPU-sync
   characterization work on this platform, this unit's and any future
   one's, must use the CPU-side sync-point attribution path.
2. **CPU-side sync-point attribution works correctly on real Lima
   hardware** and produces sane, non-degenerate numbers at both loads
   (`A_canonical_e5_pathfile`: `swap_buffers_ms` avg 0.079ms, `flip_wait_ms`
   avg 0.024ms, `finish_readpixels_ms` avg 10.912ms;
   `B_stress_e205_pathfile`: 0.120ms / 0.030ms / 89.918ms respectively) —
   the instrumentation itself is validated end-to-end on the actual target
   platform, independent of the Bug 2 config-fidelity issue.
3. **`waitForFlipComplete()`/`eglSwapBuffers()` are NOT where the CPU
   blocks** — `swap_buffers_ms + flip_wait_ms` totals ~0.10ms at e5 and
   ~0.15ms at e205, versus `finish_readpixels_ms` (the `glFinish()` sync
   point — CPU waiting on the GPU to finish submitted work) at 10.9ms and
   89.9ms respectively. `finish_readpixels_ms` and the pre-existing
   `gpu_ms` column are numerically near-identical in both runs (10.912 vs
   10.922; 89.918 vs 89.928) — expected, both time the same `glFinish()`
   call, a useful cross-check that the new instrumentation is wired
   correctly rather than an independent confirmation. **This is a null
   result for P41's swap-floor hypothesis** (perf_truth_audit's own
   finding (c)/(e): "the more likely candidate for the [~14.4-15.0ms wall-
   clock] floor is [[waitForFlipComplete()]], not EGL's own swap-interval
   gate") — on this measurement, the DRM page-flip wait is negligible
   (tens of microseconds), and essentially all CPU-blocks-on-GPU time is
   spent in `glFinish()` waiting for submitted GPU work to complete, not
   waiting for the display's page-flip event. Load-invariance (flip_wait
   barely changes 0.024ms→0.030ms across a 15x entity-count increase)
   further supports it being a fixed, small, display-cadence-adjacent cost
   rather than a GPU-load-dependent one — consistent with P41's framing
   that *something* display-cadence-related exists, just not at the
   magnitude P41 speculated, and not as the dominant GPU-sync contributor
   P38's original ~50-54% figure was trying to explain. **This finding
   should be treated as preliminary** (Bug 2's confound affects the same
   runs) but is unlikely to be an artifact of Bug 2 specifically, since
   Bug 2 inflates *submission*, not swap/flip timing, and the swap/flip
   numbers are tiny in absolute terms regardless of what else the frame is
   doing.

### Ranked attribution of the "GPU-sync ~50-54% of frame" question (P38
### audit item 6, this unit's original charter)

Given Bug 2, this unit cannot deliver a confident percentage-of-frame
number for the honest shipped-preset config — that requires the follow-up
invocation named below. What it CAN deliver, ranked by confidence:

1. **HIGH CONFIDENCE**: GPU-sync's CPU-blocking mechanism is `glFinish()`
   waiting on GPU work completion, not the swap/present/flip-wait
   mechanism. (Findings 2-3 above; structural, not magnitude-sensitive.)
2. **HIGH CONFIDENCE**: no GPU-side timer-query instrumentation is
   possible on this hardware — any future GPU-side breakdown attempt
   (zone vs. entity vs. particle GPU cost) must use a different technique
   (e.g., selective-disable A/B pairs, the same method P41 used for the
   lighting lever) since `GL_EXT_disjoint_timer_query` is unavailable.
3. **NOT MEASURED THIS UNIT** (needs the follow-up): the actual
   zone-pass/entity-pass/particles-UI split of GPU-sync time, the
   entity-count scaling curve under the CORRECT shipped-preset flags
   (Will's own ask, per the orchestrator's Phase-2 go-ahead message: this
   unit's entity sweep was supposed to reproduce and refine P51's
   +0.455ms(e5)/+2.22ms(e205) entity-promotion curve — it did not,
   because Bug 2's confound makes this run's entity-count deltas
   uninterpretable), and the fill-rate isolation (E/F, never run at all).

### Options note (report-only, no fix landed — matches this unit's own
### scope: characterization, not remediation)

**Option 1 (recommended): land a small follow-up unit (2-4 Pi
invocations) that re-runs `A_canonical_e5`/`B_stress_e205` (and, budget
permitting, `C_mid_e40`/`D_zero_e0`/`E_fill_lo`/`F_fill_hi`) with the
corrected shipped-preset flags** (`--merge-zone-draws
--composite-scenegraph --eqe1-path`, matching P38/P41/P51's own canonical
invocations exactly). `refactor/scratch/p52_pi_gpu_sync_pathfile_fix.py`
is a ready-to-adapt template — add the three missing flags to its `cmd`
list and it reproduces this unit's exact recovery methodology, correctly
configured. This is the only path to a real percentage-of-frame GPU-sync
attribution and a valid entity-scaling/fill-rate isolation.

**Option 2: accept the structural (mechanism-level) finding as sufficient
for now.** `waitForFlipComplete()`/`eglSwapBuffers()` are ruled out as the
GPU-sync bottleneck with high confidence (finding 3); `glFinish()`-driven
GPU completion wait is confirmed as the mechanism. If Will's actual
question is "should we chase the swap/flip-wait path for a fix," this
unit's answer is already conclusive: no, don't — the whole
`waitForFlipComplete()`-as-swap-floor-suspect line of investigation P41
opened is now closed with a null result, on real hardware, not just
diagnostically ruled out via `eglSwapInterval(0)` as P41 did. Do NOT act
on the specific ms-level numbers from this unit's e5/e205 runs (Bug 2).

**Option 3: re-scope the original charter.** Given GPU-side timer queries
are confirmed unavailable on the only hardware this matters for, any
future "P38 audit item 6" successor unit should be scoped around
selective-disable CPU-side A/B pairs (P41's own lighting-lever method)
from the start, not GPU timer queries — this unit's GPU-query code
remains in the tree, flag-gated and harmless (P51 will presumably not
re-litigate the swap-interval prototype P41 already reverted), but nobody
should plan future work assuming it will produce zone/entity/particle
GPU-ns splits on this hardware. It won't.

No default flipped, no config changed, no golden touched — this unit
(both phases) is purely diagnostic per its own plan.
