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

(executor fills in)
