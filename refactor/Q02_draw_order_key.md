# Q02 — Deterministic production draw-order key (texture-name hash)

Approved by Will 2026-07-24 ("go ahead with the hygiene pair").
Slot: wt/slot2, branch task/Q02-draw-order-key. Runs in parallel with Q01
(slot1) — file sets are disjoint.

## Motivation (deferred.md, found by H02b 2026-07-23)

`src/client/graphics/renderer_zone_draw.cpp` builds the material sort key
from the low 16 bits of the texture POINTER at two sites (zone regions
~line 339-346, zone objects ~line 551-557):

    texId = (uintptr_t)mat.getTexture(0) & 0xFFFF;
    matKey = (effectiveType << 16) | texId;

Same bug H02b fixed in the benchmark loader (zone_bench_loader.cpp:733-750,
`textureGroupId()`): under ASLR the production draw order of equal-material
geometry varies run-to-run — wobbling state-change counts, nondeterministic
z-fighting on coplanar faces — and it would break any future full-app
golden testing. Small fix: apply the textureGroupId() pattern to both
sites. Prerequisite for the full-app llvmpipe test tier.

## Design

1. Add a file-local `textureGroupId(const irr::video::SMaterial&)` helper
   to renderer_zone_draw.cpp (anonymous namespace): FNV-1a hash of the
   texture-0 name path, `& 0xFFFF` — byte-for-byte the same body as
   zone_bench_loader.cpp:742 (H02b), with the same rationale comment. The
   benchmark deliberately mirrors production (established mirror-pattern
   precedent, e.g. P07d's radius solve) — duplicate the 8-line helper and
   add a "keep in sync" cross-reference comment at BOTH copies rather than
   inventing a shared header for it.
2. Replace both pointer-key sites with the helper. No other behavior
   change: the key stays 16-bit (hash collisions merge two textures into
   one state-change group — harmless, exactly as pointer low-bits collided
   before), grouping semantics and front-to-back order within a material
   group are unchanged.
3. Sweep for stragglers: grep src/ and include/ for any other
   pointer-derived sort/group keys (`uintptr_t.*getTexture`,
   `& 0xFFFF` near material keys). Known non-issues: regionIdx-based
   callback keys (renderer_zone_draw.cpp:1002, renderer_zone_stream.cpp,
   renderer_gpu_upload.cpp — index-based, fine) and flicker RNG uses.
   docker/irrlicht-drm driver internals are out of scope.
4. Confirm `matKey` is consumed only for intra-frame sorting (grep its
   uses) — it must not be persisted or compared across frames/runs
   anywhere.
5. Touch-up: src/client/graphics/CLAUDE.md "material key
   `(MaterialType << 16) | TextureID`" mention — note the ID is a stable
   texture-name hash (H02b/Q02), not a pointer.

## Verification

- Standard bar: `cmake --build build -j24`; ctest (known host exceptions:
  AudioManagerTest.PreloadCommonSounds,
  RDPServerFrameTest.UpdateFrameMultipleTimes); scripts/build-gles2-x86.sh;
  scripts/build-arm-noble.sh; scripts/refactor/check_file_sizes.sh fable.
- **Visual gate PASS with goldens UNTOUCHED** — the gate exercises the
  benchmark loader (already name-hash keyed since H02b), not this code
  path; a golden diff from this unit means something unexpected happened —
  stop and report.
- **Helper parity**: diff the new helper body against
  zone_bench_loader.cpp's `textureGroupId` — byte-for-byte identical modulo
  the comment; state so explicitly in the review.
- **Straggler sweep results**: the grep evidence that no pointer-derived
  draw keys remain in src/ or include/.
- No runtime demonstration is required (the production sorted-draw path
  needs a live zone; the identical fix was proven run-stable by H02b on the
  benchmark loader, and the acceptance here is code-level equivalence).
  If you find a cheap existing harness that exercises
  drawZoneGeometrySorted deterministically, a before/after run is welcome
  but not required.

## Out of scope

- The full-app golden harness itself (this unit only unblocks it).
- Any perf tuning, key-width changes, or sort-order redesign.
- docker/irrlicht-drm driver internals.
- Q01's files (tools/hwcaps, scripts/ci, scripts/pi, tests/goldens,
  benchmarks/, .gitignore).

## Review

(executor fills in)
