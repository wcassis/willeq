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

**Implementation notes.** Added a file-local `textureGroupId(const
irr::video::SMaterial&)` helper to the anonymous namespace in
`src/client/graphics/renderer_zone_draw.cpp` (right before the closing
`} // namespace` at what was line 124), body copied verbatim from
`tools/hwcaps/zone_bench_loader.cpp`'s `textureGroupId()` (H02b): FNV-1a hash
of the texture-0 name path, `& 0xFFFF`, with a null-texture guard returning
0. Replaced both pointer-key sites (zone regions, formerly ~339-346; zone
objects, formerly ~551-557) with `uint32_t texId = textureGroupId(mat);`,
removing the inline `reinterpret_cast<uintptr_t>(mat.getTexture(0)) &
0xFFFF` and its guarding `if`. No other logic changed: `matKey` is still
`(effectiveType << 16) | texId`, still 16-bit, still consumed only by
`sortedDrawEntries_`. Also updated
`src/client/graphics/CLAUDE.md`'s "material key `(MaterialType << 16) |
TextureID`" line to note `TextureID` is a stable texture-name hash
(H02b/Q02), not a pointer.

**Deviation from the plan's literal wording**: design point 1 says to "add
a 'keep in sync' cross-reference comment at BOTH copies." The executor
instructions' Rules section explicitly lists `tools/hwcaps` as one of Q01's
files, off-limits for this unit (parallel-unit file-boundary discipline).
I resolved the conflict in favor of the stricter, more specific rule:
`zone_bench_loader.cpp` is untouched by this unit. The cross-reference is
one-directional — the new `renderer_zone_draw.cpp` copy's rationale comment
explicitly names `tools/hwcaps/zone_bench_loader.cpp`'s `textureGroupId()`
(H02b) and says "keep the two copies in sync." The existing H02b comment in
`zone_bench_loader.cpp` is unmodified and does not point back at this new
production copy. If a bidirectional cross-reference is wanted, it should be
a trivial follow-up touching only `tools/hwcaps/zone_bench_loader.cpp` (Q01
territory) once that unit is clear.

**Helper parity**: byte-for-byte diff of the two `textureGroupId` function
bodies (signature through closing brace) — identical, confirmed
programmatically. The only difference between the two files is the
rationale/cross-reference comment text above each definition, as intended.

**Straggler sweep results**: `grep -rn "uintptr_t.*getTexture"` across
`src/` and `include/` — no matches (the two sites fixed here were the only
ones). `grep -rn "0xFFFF"` near graphics code turns up only known
non-issues: `regionIdx`-based callback keys in
`renderer_zone_stream.cpp`/`renderer_gpu_upload.cpp`/`renderer_zone_draw.cpp:1013`
(index-based, not pointer-derived), flicker RNG state in
`renderer_zone_load.cpp`/`renderer_effects.cpp` (not a sort/group key), a
checker-texture placeholder color constant in `constrained_texture_cache.cpp`,
sentinel index values (`0xFFFFFFFF`) in `mesh_building.cpp`,
`gpu_upload_thread.h`, `background_work_queue.h`, `detail_types.h`, and a
WLD fragment flags mask in `wld_loader.cpp`. None are pointer-derived
draw/sort keys.

**matKey consumption**: `grep -rn "matKey\|materialKey\|sortedDrawEntries_"`
in `src/client/graphics` and `include/client/graphics` confirms `matKey`
(struct field `ZoneDrawEntry::materialKey`,
`include/client/graphics/irrlicht_renderer.h:460`) is populated only in the
two collection sites, sorted via `std::sort` on `sortedDrawEntries_`, and
consumed only in the immediately-following draw loop within the same
function call (`drawZoneGeometrySorted`, `renderer_zone_draw.cpp`).
`sortedDrawEntries_` is cleared at the top of that function and also in
`renderer_debug.cpp`/`renderer_zone_lifecycle.cpp` (zone-teardown/debug
paths) — never persisted or compared across frames or runs.

**Verification outcomes**:
- `cmake --build build -j24` — PASS, clean build.
- `ctest --test-dir build --output-on-failure` — PASS modulo the two known
  host exceptions (`AudioManagerTest.PreloadCommonSounds`,
  `RDPServerFrameTest.UpdateFrameMultipleTimes`, latter segfaulted on the
  second run as documented). First full run also showed `visual_gate`
  failing on a CPU-time perf ceiling (p95 120ms vs 33.8ms ceiling) — this is
  the perf gate, not a pixel/golden diff; re-running `visual_gate` alone
  immediately after passed cleanly (6.4s, no other CPU-heavy process
  running), confirming host scheduling noise from the parallel Q01 unit's
  concurrent build, not a regression from this change. A subsequent full
  ctest re-run confirmed `visual_gate` PASS with goldens untouched.
- `scripts/build-gles2-x86.sh` — PASS.
- `scripts/build-arm-noble.sh` — PASS, ARM ELF binaries produced.
- `scripts/refactor/check_file_sizes.sh fable` — PASS (exit 0, no
  violations).

**Deviations**: one, documented above (did not touch
`tools/hwcaps/zone_bench_loader.cpp` per the executor Rules' explicit Q01
file boundary, despite design point 1's "both copies" phrasing). No other
deviations from the plan; no scope creep.
