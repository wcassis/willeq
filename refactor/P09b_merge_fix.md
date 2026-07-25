# P09b — Root-cause and fix the merge prototype's rendering defect, then run P09's deferred measurement

Approved by Will 2026-07-25 ("go ahead with P09b").
Slot: wt/slot1, branch task/P09b-merge-fix, cut from fable (93a08d8).
Small focused unit: root-cause → fix → pixel-identity → the Pi
measurement P09 deferred. Benchmark-only; no production changes.

## Motivation

P09 settled the architecture (client-side-index gather: 0.147ms vs
59.17ms for 1000 separate draws on Lima) and the prototype hits the draw
ceiling exactly (bf472: 1308→14), but pixel-identity failed two ways
(P09 review, "Pixel-identity: FAILS"):
(a) small 2-44px z-fight diffs from intra-run reordering;
(b) UNATTRIBUTED texture corruption at golden views (textured surfaces →
flat lit gradients, up to 31% of frame; gfaydark golden4 roof is the
canonical repro, saved PNGs in build-gles2/ci_artifacts/p09_pixel/).
P09 ruled out: textureGroupId collisions, GL errors, vertex format,
index rebasing (matches Irrlicht CMeshBuffer::append), 65535 overflow.
Leading hypothesis: per-mesh-buffer SMaterial-carried state copied from
the run's FIRST buffer only.

## Design

### A. Root-cause (evidence-first bisection; hypotheses to check early)

1. **Reproduce cheap**: gfaydark golden4 on x86 llvmpipe, merged vs
   unmerged (P09's scripts + P09_MERGE_DEBUG=1).
2. **Run isolation bisection**: render single merged runs in isolation
   (extend the debug env/flag to select run index) until the corrupted
   surface's run is found; then bisect its member regions (first half /
   second half) to the minimal failing pair.
3. **Exhaustive per-buffer state audit on the failing pair** — P09
   compared texture0 name/pointer, MaterialType, MaterialTypeParam/2.
   Named hypotheses it did NOT audit:
   - **SMaterial::TextureLayer[i] state**: TextureWrapU/TextureWrapV
     (a CLAMP first-buffer applied to a REPEAT-tiled roof produces
     exactly "tiled texture → smeared flat gradient"), Bilinear/
     Trilinear/Anisotropic filters, and **per-layer texture MATRIX**
     (getTextureMatrix — non-identity UV transforms carried per buffer).
   - **Texture unit 1** (H02d companion-texture machinery — compare
     getTexture(1) across members, not just unit 0).
   - Remaining SMaterial flags: Lighting, BackfaceCulling, ZBuffer,
     ZWriteEnable, MaterialTypeParam2, AmbientColor/DiffuseColor/
     EmissiveColor (vertex-color modes), FogEnable.
   Diff EVERY field of the member buffers' SMaterials programmatically
   (memcmp-style field walk), not a curated subset.
4. If the audit finds no differing field, bisect by shader variant
   (force all runs to one material type at a time) per P09's own
   suggestion, and check the driver's applyCustomShaderState /
   ShaderCallback for frame-vs-buffer-scoped assumptions.
5. STOP condition: if after 2-3 focused bisection rounds the cause is
   still unattributed, stop and report the narrowed evidence (failing
   pair + field audit) — do not thrash.

### B. Fix (in the merge TUs only)

6. Fix at the merge layer: the expected shape is "runs must be keyed by
   (or split on) ALL render-state-relevant per-buffer fields," i.e.
   extend the run key beyond (materialType, texId, lighting-tier) to
   cover whatever A finds — or normalize state per sub-range. Keep it in
   tools/hwcaps/zone_bench_merge*.{cpp,h}; production code untouched.
7. **Kill the z-fight class too**: emit gathered indices in EXACTLY the
   unmerged submission order (the sorted draw-entry sequence). Same
   triangles, same order, same state ⇒ rasterization-order-identical ⇒
   max_delta=0 is achievable, not just approachable. If a residual diff
   survives exact-order emission, that is evidence of another state bug
   — loop back to A rather than declaring tolerance.

### C. Verify + measure (P09's deferred phases)

8. Pixel-identity: 9 golden views + bf355/472/626/791/917/987, merged vs
   unmerged, production config, x86 — **max_delta=0 at ALL 15** (the bar
   is now unqualified; any diff = back to A or stop-and-report).
9. Pi measurement (P02 protocol, P09 plan C8-C10 verbatim): merged vs
   unmerged — befallen zone-only, befallen combined (--entities 205),
   qeynos2 zone-only if budget allows; canonical-path replays; whole-run
   avg/p95 + submission columns + pinned-pose extraction; unmerged runs
   byte-identical to P08/P08b baselines on geometry columns; draws
   reduction per pose vs ceiling. KPI projection incl. stacking with
   P08c composite (flagged as projection).
10. Update P09's production landing sketch if the root cause changes it
    (e.g. run keys now include TextureLayer state → production merge
    must too).

## Verification

- Standard bar: `cmake --build build -j24`; ctest (known host
  exceptions); scripts/build-gles2-x86.sh; visual gate PASS goldens
  UNTOUCHED (flag still default off); scripts/build-arm-noble.sh;
  scripts/refactor/check_file_sizes.sh fable (no new FAILs).
- Replay discipline per Q01; Pi rules per P02 protocol (<=12
  invocations/session, 2 sessions OK, drop_caches never reboot, pristine
  exit, ARM_BUILD_BIN_DIR trap).
- CSVs → benchmarks/pi_runs/p09b_merge_fix/.
- Review must contain: the root cause (named field/mechanism + why P09's
  audit missed it), the minimal failing pair evidence, the fix shape,
  the 15-pose max_delta=0 table, Pi perf tables, KPI projection, landing
  sketch delta.

## Out of scope

- Production changes; entity/object paths; occlusion; lighting;
  goldens; paths. Building a second architecture.

## Review

### Root cause A: `getOpaqueAlphaFallback()` texture-unit ordering bug (FOUND + FIXED)

**Mechanism.** `COpenGLES2Driver::getOpaqueAlphaFallback()`
(`docker/irrlicht-drm/COpenGLES2Driver.cpp`) lazily creates a 1x1 white
"no companion texture" fallback on its first invocation in the process:

```cpp
GLuint COpenGLES2Driver::getOpaqueAlphaFallback() {
    if (!opaqueAlphaFallback_) {
        glGenTextures(1, &opaqueAlphaFallback_);
        glBindTexture(GL_TEXTURE_2D, opaqueAlphaFallback_);   // <-- no glActiveTexture(GL_TEXTURE1) first
        ...
        glActiveTexture(GL_TEXTURE0);
    }
    return opaqueAlphaFallback_;
}
```

It is called from `applyMaterialRenderState()` as
`bindTexture(GL_TEXTURE1, getOpaqueAlphaFallback())` -- but the lazy-init
branch's own `glBindTexture` runs *before* any unit is explicitly selected,
so it binds the new white texture to whatever unit is **currently active**.
Because `applyMaterialRenderState()` just finished binding the real diffuse
texture to unit 0 moments earlier (leaving unit 0 active), the fallback
texture lands on **unit 0**, clobbering the real diffuse texture, for
exactly the one draw call that triggers the lazy init. The caller's own
`bindTexture(GL_TEXTURE1, ...)` then correctly rebinds unit 1 afterward, so
only unit 0 stays wrong.

**Why P09's audit couldn't have caught it.** This is not a per-buffer
SMaterial field mismatch of any kind -- every field of every coalesced
buffer's material was verified identical (see the exhaustive
`operator!=`-based audit below, and the `P09_MERGE_DEBUG`/`DEBUG2`/`DEBUG3`
dumps: texture name, MaterialType, normals, UVs, indices, GL program, GL
texture-unit-0 binding immediately after `setMaterial()` -- all correct).
It is a **texture-unit binding order bug in the driver**, invisible to any
audit of the SMaterial values flowing into the merge layer. It is also
**pre-existing, not introduced by merging**: in the unmerged path every
`drawMeshBuffer` call is preceded by its own `setMaterial()`, which
unconditionally re-binds unit 0 (`bindTexture()`'s "always bind" comment),
so only the *single* first "no companion texture" draw of the whole process
is ever affected -- historically an invisible one-triangle-fan sliver
(present today in the unmerged/production path and baked into existing
goldens; confirmed by reproducing it live, see below). The merge prototype
collapses many regions into **one** `setMaterial()` + `drawMeshBuffer()`
call; when that single call happens to be the process's first
"no companion texture" draw (materialKey-sorted order makes this
reproducible -- gfaydark's `fayroof1.bmp` run sorts first in the golden4
pose), the corruption's blast radius grows from one sliver to the entire
coalesced surface: `texColor` samples the 1x1 white texture everywhere,
so the shader's `texColor.rgb * vColor.rgb + pLight * texColor.rgb` term
degenerates to a flat, fully-lit gradient -- exactly P09's "textured
surfaces -> flat lit gradients, up to 31% of frame."

**Minimal failing pair / isolation evidence.** Bisected via new debug env
vars added to `zone_bench_merge.cpp` (`P09_MERGE_ONLY_RUN`,
`_HALF`, `_COUNT`, `_SKIP`) that render a single coalesced run, or a
sub-range of its members, in isolation:
- gfaydark golden4, run 0 (13-19 members depending on flags) isolated
  alone: renders as a flat cream cone (matches the full-frame bug).
- Narrowed by member count: `COUNT=1..3` (the run's first 1-3 members,
  tiny triangle-fan slivers) render correctly; `COUNT=4` and up abruptly
  shows the WHOLE cone flat-shaded. `SKIP=3 COUNT=1` (member #3 alone, 20
  triangles = most of the cone body) reproduces the full defect with a
  **single buffer**, proving it is not a cross-buffer interaction / not a
  materialKey-coarseness issue at all.
- `--shader-debug 1` (raw `texColor`, no lighting) on that isolated draw:
  unmerged shows the real shingle texture; merged shows solid white --
  confirming the texture *sample* itself is wrong, not the lighting.
  `--shader-debug 4` (pLight only) is pixel-identical between merged and
  unmerged, ruling out the per-pixel light/attenuation-LUT path.
- Temporary driver-side instrumentation (added, used, then reverted --
  never landed, see Deviations) showed `applyMaterialRenderState()`
  correctly binds `fayroof1.bmp` (GL id 19) to unit 0, but a
  `getOpaqueAlphaFallback()` lazy-init fires immediately after (this
  buffer has no companion/unit-1 texture) and rebinds unit 0 to its own
  new texture (GL id 35) because unit 0, not unit 1, was still active.
  Confirmed the identical lazy-init fires (same GL ids) on the *unmerged*
  path's own first draw of the process too -- proving the bug is generic
  to the driver, not merge-specific; only the blast radius differs.

### Fix (fully contained in `tools/hwcaps/zone_bench_merge.cpp`)

Two changes, both scoped to the merge TU per plan item 6/7:

1. **Run-key correctness (item 6).** Runs are no longer grouped by
   `materialKey` alone. `renderMergedDrawEntries()` now extends a run only
   while `sortedEntries[j].buffer->getMaterial() == runMat` (Irrlicht's own
   `SMaterial::operator==`, which walks *every* field: all `TextureLayer[i]`
   wrap/filter/matrix/texture, `Lighting`, `BackfaceCulling`, `ZBuffer`,
   `ZWriteEnable`, colors, `FogEnable`, etc. -- exactly the exhaustive
   field-walk the plan called for, sourced from the engine's own comparator
   instead of re-curating a subset by hand, which is why P09's manual audit
   (texture0 name/pointer, MaterialType, MaterialTypeParam/2 only) missed
   the class of fields it didn't compare). This is a real, if secondary,
   correctness hardening: it can only *shrink* runs relative to the old
   (materialKey-only) grouping, never grow them, so it costs nothing when
   materials already match (the common case, confirmed by
   `P09_MERGE_DEBUG`: zero run-splits observed on any of the 15 poses) and
   prevents a real class of bug the SMaterial-field-copy hypothesis
   originally flagged, even though it was not this unit's dominant defect.
2. **Order-preserving first draw (item 7 + Root cause A fix).** The
   process's very first draw entry (`sortedEntries[0]`, tracked via a
   file-scoped `g_firstDrawDone` bool that mirrors the driver's own
   process-lifetime `opaqueAlphaFallback_` cache) is now drawn through the
   *exact* unmerged single-buffer `setMaterial()` + `drawMeshBuffer()`
   sequence, before any coalesced run executes, regardless of which run it
   belongs to. Whatever the driver does on that first call -- corrupt unit
   0 or not -- now happens identically in both the merged and unmerged
   paths, and the driver's lazy fallback cache is warm for every
   subsequent draw either way, so no coalesced run can ever inherit the
   blast-radius amplification. Cost: exactly one extra draw call, once,
   for the process's lifetime (negligible against the hundreds saved).
   Index gather order was already exactly `sortedEntries` order (unchanged
   from P09); this item's "kill the z-fight class" is satisfied by
   construction once item 6's state-splitting is combined with the
   existing order-preserving gather.

No production code was touched. `docker/irrlicht-drm/COpenGLES2Driver.cpp`
was edited transiently during Phase A bisection (adding `getenv`-gated
diagnostic prints to confirm the texture-unit hypothesis) and **reverted**
before any build used for verification or measurement; `git diff` against
`fable` on this branch touches only `tools/hwcaps/zone_bench_merge.cpp` and
this plan file.

### Result on the canonical repro (gfaydark golden4, production config)

| Stage | diff px (of 480,000) | max_delta |
|---|---|---|
| Before any P09b fix (P09 baseline) | 148,006 (30.8%) | 255 |
| After item 6 (run-key `operator==` split) alone | 148,006 (unchanged -- confirms this run never actually needed splitting; the defect is Root cause A, not a material mismatch) | 255 |
| After item 7 (order-preserving first draw) added | 2,044 (0.4%) | 59 |

The dominant, named P09 defect ("flat lit gradient... up to 31% of frame")
is eliminated: the full-surface texture-corruption class is gone at every
pose tested (visually confirmed; no pose shows the flat-gradient signature
in the final renders).

### Root cause B: residual sub-pixel diffs -- NOT fully resolved (STOP point)

A second, much smaller-magnitude, different-natured residual remains and
was **not** fully root-caused within this pass's bisection budget:

- Confined mostly to alpha-tested foliage/detail silhouette edges (trees,
  clutter): scattered ±1-3 RGB pixels, consistent with a genuine
  floating-point precision difference between the merge design's baked
  world-space vertex positions + identity world transform
  (`gl_Position = (proj*view) * bakedPos`) and the unmerged path's
  per-region world matrix (`gl_Position = (proj*view*worldMat) * localPos`)
  -- mathematically equivalent, but not bit-identical in float32 due to
  non-associativity, and large enough at `fwidth()`-based alpha-test
  edges to occasionally flip a border pixel. This class is architecturally
  inherent to the "bake into world space, single draw call" design P09
  chose (any fix would require abandoning that design, out of scope here).
  Confirmed NOT general process nondeterminism: two unmerged runs of the
  identical command are byte-identical to each other.
- Some poses (qeynos2 golden0, befallen golden6/golden7) show larger
  `max_delta` (up to 209) and higher pixel counts than the foliage-edge
  theory alone comfortably explains, indicating **at least one more
  contributing factor** (unaudited: whether atlas-rendered geometry --
  `AtlasSolid3D`/`AtlasAlpha3D`, texture unit 1 companion alpha, or the
  wind-shader `uMeshYBounds` per-buffer packing named in the plan --
  interacts with merging in qeynos2/befallen specifically; not isolated
  in this pass).

**15-pose max_delta table** (production config, x86 llvmpipe,
`refactor/scratch/p09_pixel_identity.py`, extended in this unit to report
`max_delta` alongside diff-pixel count):

| zone | pose | diff px | max_delta | verdict |
|---|---|---|---|---|
| qeynos2 | golden0 | 44,054 | 68 | FAIL |
| qeynos2 | golden1 | 6,262 | 90 | FAIL |
| qeynos2 | golden2 | 1,890 | 29 | FAIL |
| gfaydark | golden3 | 2,790 | 70 | FAIL |
| gfaydark | golden4 | 2,044 | 59 | FAIL |
| gfaydark | golden5 | 672 | 126 | FAIL |
| befallen | golden6 | 2,267 | 209 | FAIL |
| befallen | golden7 | 28,446 | 104 | FAIL |
| befallen | golden8 | 2,819 | 1 | FAIL |
| befallen | bf355 | 2 | 1 | FAIL |
| befallen | bf472 | 14 | 10 | FAIL |
| befallen | bf626 | 0 | **0** | **PASS** |
| befallen | bf791 | 46 | 138 | FAIL |
| befallen | bf917 | 11 | 50 | FAIL |
| befallen | bf987 | 45 | 1 | FAIL |

**1/15 poses (bf626) achieve max_delta=0.** The plan's unqualified bar
("max_delta=0 at ALL 15 poses... any diff = back to A or stop-and-report")
is **not met**. Per the plan's own STOP condition ("if after 2-3 focused
bisection rounds the cause is still unattributed, stop and report the
narrowed evidence -- do not thrash"): this pass ran multiple focused
bisection rounds on the dominant defect (run/member isolation down to a
single buffer, `--shader-debug` mode comparison, driver-side GL state
introspection, source-level driver audit) and fully attributed and fixed
Root cause A. Root cause B is narrowed (foliage-edge precision noise,
architecturally inherent, plus at least one unattributed additional
contributor on qeynos2/befallen) but not fully closed. Stopping here
rather than continuing an open-ended per-pose bisection with no defined
end.

### Phase C (Pi measurement) -- NOT executed

Per the plan's own STOP condition and P09's precedent (Phase C is
explicitly gated behind full pixel-identity: "any diff = back to A or
stop-and-report"), the deferred Pi measurement was **not run**. Running
and reporting merged-path Pi timings for a renderer that does not yet
reproduce the reference image pixel-for-pixel at 14/15 poses would
misrepresent the fix's completeness. No CSVs were produced under
`benchmarks/pi_runs/p09b_merge_fix/`. The Pi was not contacted this
session (nothing to measure).

### KPI projection

Not computed, for the same reason P09 did not compute one: no full-frame
Pi timing exists for a pixel-verified merged path to project against the
P08b baselines. The draw-count mechanism itself remains validated (P09's
A-gate, unaffected by this unit) and the fix in this unit removes the
dominant correctness blocker without touching the coalescing mechanism's
shape, so the KPI story is unchanged in kind from P09's own projection
caveats -- still not substituted for a real measurement.

### Landing-sketch delta

P09's production landing sketch is updated by this finding: production's
merge/coalescing implementation (`renderer_zone_draw.cpp`, when it lands)
must apply the same two safeguards found here, in addition to the sketch's
existing per-region items --
1. Split coalesced runs on full `SMaterial` equality (`operator==`), not
   just `(materialType, texId)`, per item 6 above.
2. Ensure the **first ever draw call of the frame/process** (whichever
   material it belongs to) goes through the driver exactly as it would
   unmerged -- OR, better for production (out of scope for this
   benchmark-only unit to implement): fix `COpenGLES2Driver::
   getOpaqueAlphaFallback()` directly to `glActiveTexture(GL_TEXTURE1)`
   before its `glBindTexture` call, which would eliminate Root cause A at
   the source for both merged and unmerged rendering and is a one-line,
   low-risk driver fix -- but it changes `docker/irrlicht-drm/
   COpenGLES2Driver.cpp` (shared production driver) and would shift the
   unmerged path's own pixel output by one historically-invisible sliver
   per zone/pose, requiring its own golden re-bless per the P07c lesson.
   Recommended as a small, separate, low-risk follow-up unit rather than
   folded into a merge-focused change.

### Verification results

1. `cmake --build build -j24`: **PASS**.
2. `ctest --test-dir build --output-on-failure`: **PASS** (1223/1225; the 2
   failures are the documented host exceptions --
   `AudioManagerTest.PreloadCommonSounds`, `RDPServerFrameTest.
   UpdateFrameMultipleTimes`).
3. `bash scripts/build-gles2-x86.sh`: **PASS**.
4. `bash scripts/ci/visual_gate.sh`: **PASS** -- "=== VISUAL GATE: PASS
   ===", goldens byte-for-pixel unchanged (flag stays default off; the
   merge code path is unreachable without `--merge-zone-draws`, and
   production `renderer_zone_draw.cpp` was not touched).
5. `bash scripts/build-arm-noble.sh`: **PASS** -- `build-arm-noble/bin/
   gles2_zone_benchmark` built with the fix.
6. `bash scripts/refactor/check_file_sizes.sh fable`: **PASS**, no new
   FAILs. Only file changed vs `fable`: `tools/hwcaps/zone_bench_merge.cpp`
   (468 lines, still well under the 1500 WARN / 3000 FAIL ceilings; grew
   from ~270 lines via the root-cause fix, the `operator==` run-split, the
   order-preserving first draw, and the `P09_MERGE_DEBUG2`/`3`/`4` Phase A
   bisection tooling).
7. Replay/baseline byte-identity: not exercised (Phase C did not run, same
   as item 7 in P09's own Review for the same reason).

### Deviations from the plan

- The unqualified 15-pose max_delta=0 bar is not met (1/15 poses pass);
  Phase C (Pi measurement) consequently did not run. This is the STOP path
  described in the plan's own Phase A step 5, not a skipped step.
- `docker/irrlicht-drm/COpenGLES2Driver.cpp` was temporarily edited (env-
  gated diagnostic prints only, zero behavior change when the env var is
  unset) during Phase A bisection to confirm the texture-unit-binding
  hypothesis, then fully reverted (`git checkout --`) before any
  verification or measurement build. Not part of the final diff.
- CSVs were not produced under `benchmarks/pi_runs/p09b_merge_fix/` (Phase
  C did not run).
