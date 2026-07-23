# H02c — Fix foliage transparency in the GLES2 golden path

Will's blessing review rejected the H02b golden set: in gfaydark, tree
foliage renders as full solid rectangles where the masked texture should be
transparent (visible as pale rectangles in gfaydark_f1/f2 backgrounds).
Automation could not catch this — the frames are deterministic and
geometry-rich; they're just WRONG. Root-cause and fix, then re-bless.

## Diagnosis guidance (verify, don't assume)

The benchmark loader already wires the alpha-test shader
(zone_bench_loader.cpp uses getActiveAlphaTest at several sites), so
suspect one of:
1. **Texture decode drops alpha** — EQ masked textures (foliage/leaves) get
   alpha from the palette-index-0 / color-key convention or DDS alpha; check
   whether the decoded texture for a foliage surface actually has an alpha
   channel in this path.
2. **Material classification misses foliage** — the WLD material flags for
   masked surfaces may not be mapped to the alpha-test material in the
   benchmark's mesh build (compare against production's mesh_building.cpp /
   zone_geometry.cpp classification).
3. **ETC1/EQE1 interplay** — ETC1 is RGB-only; alpha requires the separate
   alpha-page mechanism. Confirm which texture path the x86 benchmark uses
   (S3D/BMP-DDS decode, presumably, since no --eqe1-path is passed) and rule
   this in/out explicitly.
Useful cross-check: does the DESKTOP-GL x86 build (build/, model_viewer or
production path) render gfaydark foliage correctly? That isolates
benchmark-loader vs GLES2-driver vs decode-path.

## Rules

- Fix at the true root cause. If the fix lands in SHARED production code
  (src/client/graphics/eq/*, constrained_texture_cache), that's in scope but
  demands the full regression bar (complete ctest + visual gate) and a
  precise explanation of why production behavior change is correct — the Pi
  presumably renders foliage correctly today, so a shared-code root cause
  needs to explain that too (e.g., a path only the benchmark exercises).
- After the fix: re-bless ALL 9 goldens (`visual_gate.sh --bless`), verify
  plain-run pass + determinism (two runs byte-identical), and re-check the
  OTHER zones' frames for the same class of bug (qeynos2 ivy walls
  especially). Regenerate the contact sheet — still NOT approved; Will
  re-reviews.

## Verification

- Foliage in gfaydark frames shows transparent-masked leaves (visual + a
  before/after crop pair saved to the review artifacts dir).
- visual_gate bless → verify pass; determinism confirmed.
- Default build + full ctest unregressed (mandatory if shared code touched).

## Review

(executor fills in: root cause with evidence, fix location + why correct
for production too, before/after evidence, re-bless results)
