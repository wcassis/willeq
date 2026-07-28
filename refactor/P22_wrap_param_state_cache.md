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

(executor fills in)
