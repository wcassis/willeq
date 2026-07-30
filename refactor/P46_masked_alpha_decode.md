# P46 — Masked-entity alpha: fix the RGBA decode gap + benchmark companion binding + bless evidence

**Slot: slot2. x86 ONLY — no Pi, no eqemu.** Production change
authorized by Will 2026-07-30 ("go ahead with P46").

## Background (read first)

- refactor/P44b_bless_retake.md + its P44c addendum, and
  benchmarks/audits/p44b_bless_retake/EVIDENCE_MANIFEST.txt — the
  double-null story and both root causes.
- refactor/P37_*.md — the masked-texture ground truth table.
- The two gaps this unit closes:
  (A) `ConstrainedTextureCache::decodeBMP()` (~line 1151) hardcodes
  alpha=255 for 8bpp paletted BMPs — EQ's palette-index-0
  transparency convention is implemented exactly once, in
  `tools/eqe1_builder.cpp` (~line 88), offline only. Consequence: the
  live x86 RGBA path cannot render masked-entity alpha and never
  could.
  (B) `gles2_zone_benchmark` never links/exercises
  `entity_renderer.cpp`'s companion-alpha binding, so the ETC1 path's
  alpha is invisible to the benchmark.

## Work

A. **Production decode fix**: implement palette-index-0 → alpha=0 in
   the runtime BMP decode, **gated on the texture actually being
   WLD-masked** — palette slot 0 is a legitimate color in unmasked
   8bpp textures and MUST remain opaque byte-identical. Investigate
   before coding: map every decodeBMP consumer (zone textures?
   entity per-part? equipment?) and how/whether the textureMasked
   flag can reach the cache (plumb a flag through the load call, or
   key off the WLD-derived masked-texture set — choose the least
   invasive route that cannot misfire on unmasked textures; mirror
   eqe1_builder's decode logic itself). STOP-with-memo if the
   plumbing cascades beyond ~6 files (P35 precedent) or if masked
   ZONE textures would change behavior on this path (evidence needed
   either way: state which categories of texture can hit the new
   code).
   Evidence: unmasked textures byte-identical (hash or ledger
   comparison on a representative set); ground-truth masked SKE/ZOM
   textures now decode hasAlpha=1 with mask-shaped alpha.
B. **Benchmark companion binding**: wire the entity companion-alpha
   binding (mirroring entity_renderer.cpp's production behavior) into
   the benchmark's entity path so the ETC1 (--eqe1-path) leg renders
   masked alpha the way production does. Benchmark-only code.
C. **The bless evidence, at last** (P44b's proven method verbatim:
   deterministic --animate at a fixed frame index, orbit-radius 8 /
   height-offset 6 sky-silhouette framing, SKE race 21 primary, ZOM
   race 70 legs secondary, 400% crops, diff-must-be-confined-to-mask
   assertion):
   - PRIMARY pair (isolates P37, production-representative for the
     Pi): ETC1 mode with Part B active — tip vs fda2e34, identical
     invocation.
   - SECONDARY pair (x86-live-representative): RGBA mode with Part A
     active — tip vs fda2e34. Manifest must state this pair's visual
     delta = P37 + P46 jointly (the before-binary lacks both), i.e.
     "what x86 players see before vs after this pair of fixes".
   - Expected null stays null: zomch0001 (citywlivy-class) — state it
     up front.
   Stills → /mnt/projects/fable/videos/p46_bless/ (NFS, gitignored);
   committed manifest + diff stats → benchmarks/audits/p46_bless/.

## Rules

- Separate commits: A, B, C(+manifest/Review). Any one revertible.
- Goldens untouched — visual gate PASS mandatory after A; ANY golden
  diff = STOP and report (do not re-bless yourself).
- No preset/config changes. No Pi runs (ETC1 leg runs on x86 llvmpipe
  with --eqe1-path, same as CI).
- Build-tree caution: verify non-stub binary sizes (07-30 crash
  hazard); the fda2e34 scratch build from P44b/c may be reusable at
  wt/slot2/build-preP37 — verify commit + sizes before trusting.

## Verification

- Full bar: GLES2 ctest baseline (current ~1495/1497 class; known
  exceptions ALSA + RDP race), visual gate PASS, ARM cross-build
  clean, file-size check.
- Per-part evidence as listed in A/B/C. Plan Review filled in.

## Review

**Both root causes closed, in production for (A) and tool-only for (B); bless
evidence delivered and, for the first time across P37/P44/P44b/P44c/P46, is
a real (non-null) visual demonstration.**

### Part A — production decode fix (commit e55ba17)

Consumer map (all ~17 `ConstrainedTextureCache::getOrLoad()` call sites,
found via grep across `src/` and `tools/`): zone geometry
(`zone_geometry.cpp`), entity per-part textures (`race_model_loader.cpp`
T05 loop), equipment per-triangle-group textures
(`equipment_model_loader.cpp`), composite-sheet EQE1 lookups
(`race_model_loader.cpp`, empty-data EQE1-only, decodeBMP never fires),
spell FX (`spell_visual_fx.cpp`), animated zone textures
(`animated_texture_manager.cpp`), UI cursor/atlas (`irrlicht_renderer.cpp`,
`ui_atlas.cpp`), detail/creature/particle atlases (`detail_manager.cpp`,
`boids_manager.cpp`, `particle_manager.cpp`), weather overlays
(`snow_overlay.cpp`, `rain_overlay.cpp`, `storm_cloud_layer.cpp`,
`tumbleweed_manager.cpp`).

Chosen route: a defaulted `bool applyPaletteMask = false` parameter threaded
through `getOrLoad() → processTextureData() → decodeBMP()` (header +
implementation, `constrained_texture_cache.h`/`.cpp`). Only the two call
sites that already compute a WLD-derived `isMaskedMaterial` flag (the same
`textureMasked()` signal P37 wired) pass `true`:
- `race_model_loader.cpp`'s T05 per-part decode loop — converted from a
  range-for over `textureNames()` to an indexed loop so `textureMasked()`
  (a parallel array, confirmed via `mesh_building.cpp`'s identical indexing
  pattern) can be looked up per texture.
- `equipment_model_loader.cpp`'s per-triangle-group texture load — the
  `isMaskedMaterial` bool was already computed just above this call site
  for P37's `MaterialType` assignment; reused as-is.
`zone_geometry.cpp`'s call site is untouched (stays at the `false` default)
— required by the plan's STOP condition (masked ZONE textures must not
change on this path; zone_geometry.cpp has its own independent WLD-masked
handling via H02d that must not be perturbed). Cascade: 4 files touched
(2 header/impl + 2 call sites), well under the ~6-file P35 precedent — no
STOP needed.

`decodeBMP()`'s 8bpp branch now sets `rgba[dstIdx+3] = (applyPaletteMask &&
idx == 0) ? 0 : 255`, mirroring `tools/eqe1_builder.cpp`'s decode logic
(including its identical out-of-range-index clamp-to-0 behavior) rather than
reinventing it.

Evidence (`benchmarks/audits/p46_bless/EVIDENCE_MANIFEST.txt`, "Part A
gating evidence" section): zone geometry (befallen, no `--eqe1-path`) and an
unmasked entity race (Human) render byte-identical (md5-identical, AE=0)
before/after. The primary/secondary bless pairs below show WLD-masked
entity textures now decoding with mask-shaped alpha.

### Part B — benchmark companion binding (commit d19e325)

Added `applyEntityAlphaCompanionBinding()` in `zone_bench_loader.cpp`, an
intentional line-for-line mirror of `entity_renderer.cpp`'s
`applyAlphaTestMaterialType()` (same ETC1-companion vs RGBA-variant branch,
same fallback order) — ported, not reinvented. Wired in immediately after
`createAnimatedNode()` in the `--animate` entity path (the same call site
P44b traced as the only path reaching `mesh_building.cpp`'s P37 fix in this
tool). Iterates the created node's instance-mesh buffers, and for any buffer
mesh_building.cpp already flagged `EMT_TRANSPARENT_ALPHA_CHANNEL_REF`
(P37's marker), binds the ETC1 alpha companion on unit 1 (ETC1 texture) or
switches to the RGBA alpha-test shader variant (direct RGBA/DDS texture) —
matching production exactly. Benchmark-only file; no production source
touched.

### Part C — bless evidence (commit — this Review + manifest)

Method: P44b's method verbatim (deterministic `--animate` at fixed frame
indices 12/26/42, `--orbit-radius 8 --orbit-height-offset 6`, SKE race 21
primary, ZOM race 70 secondary, 400% crops, diff-confined-to-entity
assertion). BEFORE = fda2e34 (reused P44b/P44c's `build-preP37/` scratch
build without rebuilding — verified reusable by exact byte-for-byte size
match to P44b's own documented sizes, ruling out a stale/stub 07-30
artifact). AFTER = tip (this branch, Part A+B).

**PRIMARY (ETC1, Part B active) — isolates P37 alone**: real, non-null
visual delta for the first time on any instrument. AE 1937–6939 px/480000
per pair (0.4%–1.5%), diff bounding boxes tight rectangles sitting exactly
over the entity silhouette (105x242 @ ske f26, 44x238 @ zom f26) — not
scattered zone-wide noise, no STOP triggered. 400% torso crops visually
confirm BEFORE renders solid, AFTER renders alpha-tested.

**SECONDARY (RGBA, Part A active) — P37 + Part A jointly, x86-live-
representative**: same AE magnitude and near-identical diff bounding boxes
as the ETC1 leg (expected: both legs alpha-test the same mask shape at the
same pose via different delivery mechanisms) — corroborating, not
duplicative, evidence. BEFORE still's md5 matches P44c's own documented
RGBA-null-result md5 exactly, confirming this session's baseline reproduces
P44c's own recorded starting point.

**zomch0001 (chest)**: stayed null as expected, stated up front per the
plan — citywlivy-class (WLD-masked, decode-opaque), unchanged by design
under both P37 and P46. Visible zom-pair diffs attributed to zomlg0001
(legs, real alpha), consistent with P37's own ground-truth table.

Full data (per-frame AE table, diff-bounding-box method, md5s, the zone/
unmasked-race byte-identity check) in
`benchmarks/audits/p46_bless/EVIDENCE_MANIFEST.txt`. Stills at
`/mnt/projects/fable/videos/p46_bless/{primary_etc1,secondary_rgba,parta_evidence}/`
(NFS, gitignored).

### Verification bar

| Step | Result |
|---|---|
| `cmake --build build -j24` | PASS, clean |
| `ctest --test-dir build --output-on-failure` | 1529/1530 (sole exception: `AudioManagerTest.PreloadCommonSounds`, documented ALSA host quirk) |
| `bash scripts/build-gles2-x86.sh` | PASS, clean |
| `bash scripts/ci/visual_gate.sh` | PASS (qeynos2/gfaydark/befallen), goldens untouched, no re-bless |
| `bash scripts/build-arm-noble.sh` | PASS, clean, all binaries non-stub |
| `bash scripts/refactor/check_file_sizes.sh 5420886` | `constrained_texture_cache.cpp` WARN (1885 lines) — pre-existing (1871 at merge-base), this unit added 14 net lines, no state change (WARN→WARN); `zone_bench_loader.cpp` FAIL (3098 lines) — **pre-existing FAIL** (already 3031, over the 3000 threshold, at merge-base 5420886, before this unit's +67 lines) — not a regression, flagged per deviation 1 below |

### Deviations

1. `tools/hwcaps/zone_bench_loader.cpp` was already over the file-size
   check's FAIL threshold before this unit (3031 lines at merge-base);
   Part B's addition pushed it to 3098. Not split out — out of P46's
   territory (a focused benchmark companion-alpha fix), would be its own
   errand. Flagged, not silently absorbed.
2. zomch0001 chest-specific 400% crop not produced separately — occluded by
   equipped gear at this race/variant/framing (an already-established P44b
   finding); would not add information over the existing numeric trace.

### Which bless pairs are genuinely human-judgeable

Lead with the **PRIMARY ETC1 pair** (production-representative for the
Orange Pi tier, isolates P37 alone): `primary_etc1/crops/ske_{BEFORE,AFTER}_f26_torso_zoom.png`
and the equivalent zom crops. This is the first pixel-level visual proof
of P37's effect on any instrument. The **SECONDARY RGBA pair**
(`secondary_rgba/crops/ske_{BEFORE,AFTER}_f26_torso_zoom.png`) is the
x86-live-client-representative claim (P37+P46 jointly) and corroborates the
primary pair rather than standing alone. Both are genuinely judgeable —
same crop framing/scale as P44b's (rejected-then-fixed) live-client attempt,
now on a pose-guaranteed-identical instrument with a confirmed non-zero,
entity-confined diff.
