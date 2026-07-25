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

(executor fills in)
