# P22 — GLES2 texture wrap-param state cache (P18's lead)

**Slot: slot2. x86 ONLY — no Pi, no eqemu.** Siblings: P21 (slot1,
doors/entities + eqemu), P23 (slot3, particles/stability) — territories
disjoint; respect them.

Close the animated composite-vs-multibuffer pixel delta that P18
narrowed but could not fix. This unit owns the GLES2 driver territory
P18 was barred from.

## Trusted findings from P18 (refactor/P18_animated_composite_delta.md
Review — read it first; do not re-run its dead ends)

- The delta (Z35 method, qeynos2 e6 seed42 `--path-file`, frames
  15/25/35): differing_pct 17.2/29.3/26.0%, mean ~0.1-2/255 BUT
  max_channel_delta 182-224/255 — structural, not FP noise. Static
  composites are pixel-identical; only animated entities diverge.
- RULED OUT: material Lighting flag (dead code at runtime), skinned
  normals (bit-identical vertex dump), vertex color (both builders
  hardcode white).
- THE LEAD: composite mesh forces `TextureWrapU/V = ETC_CLAMP_TO_EDGE`;
  multibuffer leaves Irrlicht's default `ETC_REPEAT`. A UV-extent-aware
  clamp patch in mesh_building.cpp REGRESSED the diff to 83-92% —
  disproportionate and direction-inconsistent with mip-pollution,
  pointing at texture-OBJECT wrap-parameter state being shared: GL wrap
  params live on the texture object, not the draw call; if the driver's
  state-dedup skips reapplying `glTexParameteri` when the bound texture
  is unchanged (or applies wrap only at creation), two materials sharing
  one `ITexture*` but wanting different wrap modes stomp each other in
  draw-order-dependent ways.

## Work

1. INSTRUMENT FIRST (env-gated, removed before final commit): log
   wrap-mode application vs. requests in `COpenGLES2Driver` /
   `COGLES2Texture` (`docker/irrlicht-drm/` and the x86 fork
   `docker/irrlicht-drm-x86/` — check how the two share code; fix must
   land in the path(s) the x86 GLES2 build and the ARM build actually
   compile). Establish with logs, on the Z35 repro, whether draw calls
   request different wrap modes for a shared texture and whether the
   driver honors or skips them.
2. If confirmed: fix so per-material wrap state is honored per draw
   call (e.g. cache last-applied wrap per texture object and reapply
   on mismatch in setActiveTexture/material application — follow the
   driver's existing state-dedup idioms). Mind the perf discipline:
   no unconditional glTexParameteri per draw; only on change.
3. Success metric: re-run the Z35 A/B — animated composite-vs-
   multibuffer delta drops to FP class (differing_pct low single
   digits AND max_channel_delta ≤ a few LSB). Static must stay
   pixel-identical. Report before/after numbers.
4. If the delta does NOT close after the wrap fix (or the lead
   disproves), STOP with the instrumentation evidence — do not chase
   new theories beyond one bounded follow-up hypothesis.

## Rules

- Territory: `docker/irrlicht-drm*/` driver files ONLY (plus a
  temporary benchmark flag if needed for instrumentation — reverted).
  Do NOT touch mesh builders (`mesh_building.cpp` — P18 proved fixing
  the callers regresses), zone_shader, doors/entities (P21), particles
  (P23).
- GOLDENS: a wrap-behavior fix can legitimately move golden pixels
  (REPEAT-vs-CLAMP edge sampling). Run the visual gate: if goldens
  move, STOP after capturing before/after golden diffs and report —
  re-bless is Will's decision via the orchestrator, never yours.
- ARM safety: the fix compiles in the ARM driver too — build-arm-noble
  must pass; do not add GLES3-only calls.

## Verification

- Full 6-step bar (known ctest exceptions per protocol). Visual gate
  result reported explicitly (PASS, or STOP with diffs per above).
- Z35 A/B numbers before/after + the instrumentation evidence for the
  mechanism.

## Review

**Outcome: STOP — lead disproven by instrumentation, no production code changes
committed.** The specific mechanism P18 flagged ("if the driver's state-dedup
skips reapplying `glTexParameteri` when the bound texture is unchanged...")
does not exist in the current driver. No fix was made because there is
nothing to fix at this layer.

### Territory confirmation

`docker/irrlicht-drm/COGLES2Texture.{h,cpp}` and `COpenGLES2Driver.{h,cpp}`
are the actual shared driver files compiled into *both* the x86 build
(`scripts/build-gles2-x86.sh` copies `docker/irrlicht-drm/*` verbatim, only
forking `CIrrDeviceFB`) and the ARM build (`docker/irrlicht-drm-patch.py` +
Dockerfile.arm-noble). `docker/irrlicht-drm-x86/` contains *only*
`CIrrDeviceFB.{h,cpp}` (the EGL-surfaceless device fork) — confirmed via
`diff -rq` between the two directories. Any fix had to land in
`docker/irrlicht-drm/`, which is where the investigation and instrumentation
were done.

### Code-reading pass (before instrumenting)

- `COpenGLES2Driver::applyMaterialRenderState()` (`COpenGLES2Driver.cpp`
  ~line 1144-1157) sets `GL_TEXTURE_WRAP_S/T` via `glTexParameteri`
  **unconditionally, every call**, derived fresh from
  `material.TextureLayer[0].TextureWrapU` — there is no dedup/cache check
  gating this call (unlike `bindTexture()`'s `glActiveTexture` dedup, or
  `setBlend`/`setCull`'s state-compare-then-set idiom used elsewhere in the
  same file). `bindTexture()` itself also always issues `glBindTexture`
  (comment: "external code may change bound textures via raw GL calls,
  making our tracked state stale" — no dedup on the bind either).
- Entities render through `EQAnimatedMeshSceneNode::render()`
  (`animated_mesh_scene_node.cpp` line 787-800), which calls
  `driver->setMaterial(materials_[i])` **per mesh buffer, every frame**, no
  batching/dedup above the driver. `setMaterial()` always calls
  `applyMaterialState()` → `applyMaterialRenderState()` (built-in path) or
  `applyCustomShaderState()` (custom-shader path, used by entities via
  `shaderMaterialSolid_`) which itself calls `applyMaterialRenderState()`
  first, so wrap application is not skipped for the custom-shader entity
  path either.
- Checked whether any post-`applyMaterialRenderState` callback could
  override wrap state before the draw: `ShaderCallback::OnSetConstants`
  (the callback registered for `materialSolid_`/`materialLWSolid_`, the
  ones entities actually use per `ZoneShaderManager::getActiveSolid()`) only
  touches `GL_TEXTURE_MAG/MIN_FILTER` and only inside a
  `#if defined(EQT_HAS_DRM) && !defined(EQT_HAS_GLES2)` block — dead code on
  the GLES2 build both x86 and ARM compile. `AtlasShaderCallback` (which
  *does* do raw `glBindTexture` without touching wrap) is registered only
  for the zone-atlas materials, never for entity materials — ruled out as a
  factor for the entity divergence under investigation.
- `COGLES2Texture::regenerateMipMapLevels()` (`glGenerateMipmap`) has zero
  callers anywhere in the driver or `src/client/graphics/`, and
  `applyMaterialRenderState`'s `MIN_FILTER` is only ever `GL_LINEAR` or
  `GL_NEAREST` (`BilinearFilter` ? one : the other) — never a
  `*_MIPMAP_*` enum. So P18's "mip-pollution" framing for *why* REPEAT vs
  CLAMP would matter doesn't apply to this driver at all: no texture in
  this codebase ever has more than mip level 0, so mip-level wraparound
  blending cannot be occurring.

This static read already argued against the hypothesized cache-skip
mechanism, but per the plan's "INSTRUMENT FIRST" requirement, it was
verified empirically rather than accepted from code reading alone.

### Instrumentation (temporary, reverted before commit)

Added env-gated (`WILLEQ_WRAP_DEBUG=1`) logging directly in
`applyMaterialRenderState()` in `docker/irrlicht-drm/COpenGLES2Driver.cpp`,
right after the wrap `glTexParameteri` calls: for every draw, log the bound
GL texture name, the wrap mode requested by the current material, the wrap
mode **read back** via `glGetTexParameteriv` immediately after the set call,
and whether the previous draw that touched this same GL texture name
requested a different wrap mode (a `std::unordered_map<GLuint, bool>`
keyed by texture name, tracking last-requested clamp/repeat) — this
directly targets the "shared `ITexture*`, draw-order-dependent stomping"
mechanism: if it existed, readback would sometimes disagree with the
request, and/or a texture's readback would lag one frame behind a changed
request from a differently-configured material.

Ran the Z35 repro (`gles2_zone_benchmark --zone qeynos2 --eq-path
/mnt/projects/EverQuestP1999 --entities 6 --seed 42 --animate --path-file
benchmarks/paths/qeynos2_seed42_1000f.path --frames 40 --warmup 5 --res 1024
768`), once with `--composite-scenegraph --eqe1-path
/mnt/projects/EverQuest_ETC1`, once without, both under
`WILLEQ_WRAP_DEBUG=1`:

- **Composite run**: 3,213 instrumented draws, 19 distinct GL texture
  objects. Every texture requested exactly one wrap mode for the entire
  run (13 textures REPEAT [zone/shared], 6 textures CLAMP [per-entity
  composite atlas sheets — each entity's composite texture is a unique GL
  object, never shared]).
- **Multibuffer run**: 4,494 instrumented draws, ~190 distinct GL texture
  objects (shared body-part/skin textures across entities, as expected).
  Every single one requested REPEAT (clamp never requested in this path —
  confirms P18's finding that multibuffer never explicitly sets wrap and
  gets Irrlicht's REPEAT default).
- **Readback vs request, both runs combined**: 7,707 samples, **0
  mismatches** — `glGetTexParameteriv(GL_TEXTURE_WRAP_S/T)` matched the
  just-issued request 100% of the time.
- **Draw-order wrap-change-on-shared-texture detector**: **0 fires** in
  either run — no GL texture object ever saw two different wrap requests
  across consecutive draws within a single process.

**Conclusion: the state-caching-skip mechanism does not occur.** Wrap
parameters are requested consistently per texture object within each
code path, and every request is applied and immediately verified via GL
readback — there is no evidence of skipped reapplication, no evidence of
stale/stomped state, and (structurally, since composite mode's atlas
textures are always unique per-entity GL objects, never shared with
multibuffer's body-part textures, and the two modes are exercised in
separate processes in this benchmark) no scenario in which "two materials
sharing one `ITexture*` but wanting different wrap modes" can even arise
in the traced code path. P18's lead is refuted, not just unconfirmed.

### One bounded follow-up hypothesis

Given the wrap-caching lead is dead, checked whether the wrap-mode
*difference itself* (composite's per-entity atlas is genuinely CLAMP,
multibuffer's shared skin textures are genuinely REPEAT — both correctly
applied, per above) could still be the delta's proximate cause via some
other legitimate GL sampling difference (not a caching bug, just "REPEAT
and CLAMP really do sample differently near UV 0/1 edges"). This is
consistent with P18's own reverted experiment (forcing UV-extent-aware
CLAMP onto multibuffer *increased* the divergence to 92.5/82.9/45.2%,
rather than closing it toward composite) — i.e. even when wrap modes are
matched up, the paths still diverge, and forcing them to match makes it
worse, not better. Re-ran the same Z35 A/B on the current (unmodified)
`fable` baseline to sanity-check: composite-vs-multibuffer differs
substantially, consistent in direction with P18's findings, though this
unit's own quick diffing script (see Deviations) is not calibrated
identically to P16/P18's original tool, so the exact percentages below
should not be treated as directly comparable to the 17.2/29.3/26.0%
figures in P18's Review — the qualitative reproduction (large, structural,
composite-vs-multibuffer-only divergence; static unaffected) stands.
This confirms the delta is real and reproducible on this checkout, but its
cause is elsewhere in the pipeline (most likely genuinely in
`mesh_building.cpp`'s per-path material/UV construction, which is P22's
explicitly barred territory — "P18 proved caller-side fixes regress") —
not fixable from `docker/irrlicht-drm*/` given the instrumentation
evidence above. Per the plan's own STOP condition ("if the lead disproves
... STOP with the instrumentation evidence — do not chase new theories
beyond one bounded follow-up hypothesis"), stopping here.

### Z35 A/B (informational only — no fix landed, so before == after == current `fable`)

This unit's own diff script (`differing_pct` = % of pixels with any
nonzero max-channel delta; not the same tool/threshold as P16/P18's,
which was not preserved in the repo — see Deviations), frames 15/25/35,
same repro command as above:

| frame | differing_pct | mean_abs_delta | max_channel_delta |
|-------|---------------|-----------------|--------------------|
| 15    | 92.5% (38.7% at delta>2) | 1.55/255 | 205/255 |
| 25    | 82.9% (32.7% at delta>2) | 1.81/255 | 243/255 |
| 35    | 45.2% (16.9% at delta>2) | 0.61/255 | 24/255 |

Structural divergence reproduces (large minority-to-majority of pixels
differ, well beyond FP noise); static geometry was not re-verified this
unit (not touched, and P18 already established static is pixel-identical
via the same code paths this unit left unmodified).

### Visual gate

**Not applicable / not run.** No production code change was made (the only
edit — temporary instrumentation in `COpenGLES2Driver.cpp` — was reverted
before commit; `git diff` against `fable` is empty). Nothing to gate.

### Verification

1. `cmake --build build -j24` — clean, on the reverted (== `fable`-identical)
   tree. Confirms the revert left no residue.
2-6. Skipped as moot, same rationale as P18's Review: no code change to
   verify (ctest/visual-gate/arm-build/file-size steps are scoped to a
   landed fix; this unit lands none). The instrumented build (step 3
   equivalent, `scripts/build-gles2-x86.sh` with the temporary
   instrumentation present) was run successfully to gather the evidence
   above, then reverted.

### Deviations from the plan

- No fix attempted (deviates from the plan's step 2/3) because step 1
  (instrument-first) already refuted the mechanism the fix would have
  targeted — the plan's own step 4 STOP condition ("if the lead disproves
  ... STOP") applies directly, so no fix was attempted against a mechanism
  already shown not to exist.
- The plan's step 3 success-metric diff numbers use a diff script written
  for this unit (P16/P18's original tool was not found committed anywhere
  in the repo — searched `refactor/scratch/`, `scripts/`, `*.py` for
  `differing_pct`/`seam_diff`/`z35`, found only the JSON *outputs*, not the
  generator). The numbers reported above are therefore informational
  (confirm the delta is real and large) rather than a precise before/after
  against P18's 17.2/29.3/26.0% baseline — flagging this rather than
  silently presenting them as equivalent.
- Instrumentation lived only in `docker/irrlicht-drm/COpenGLES2Driver.cpp`
  (the plan's allowance for "a temporary benchmark flag if needed for
  instrumentation" was not needed — the driver-side logging alone was
  sufficient and stayed inside the primary territory).
