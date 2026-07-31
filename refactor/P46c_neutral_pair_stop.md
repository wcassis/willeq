# P46c — Lighting-neutral bless pair: STOP, root cause found (the RGBA cutout cannot be demonstrated by this instrument yet)

**Slot: slot2. x86 ONLY — no Pi, no eqemu.** Round 5 of the P37 visual-evidence
saga. Round 4 (P46b) was accepted-in-spirit but Will's implicit ask (per the
orchestrator's framing of this errand) was for a pair where BEFORE/AFTER are
pixel-identical everywhere except the genuine alpha cutout — P46b's pair
still carries a real, disclosed lighting-model delta (fixed-function BEFORE
vs promoted-per-pixel-lit AFTER) on top of the cutout. This unit's job was to
build an instrument that isolates the cutout alone.

## Work done

1. **Benchmark-only opt-out flag added and verified**:
   `--no-entity-shader-promotion` (CLI, `tools/hwcaps/gles2_zone_benchmark.cpp`)
   → `BenchConfig::noEntityShaderPromotion` (`tools/hwcaps/zone_bench_loader.h`)
   → gates the `applyEntityShaderMaterialPromotion()` call site in
   `tools/hwcaps/zone_bench_loader.cpp` (~line 792). Default unchanged
   (promotion on). Verified functioning correctly: with the flag OFF
   (default), ETC1-leg AE vs `fda2e34` at ske/f26/r8h6 = 6900, an exact
   match to P46b's own documented promoted-leg number
   (`benchmarks/audits/p46b_bless/EVIDENCE_MANIFEST.txt` line ~191). With
   the flag ON (promotion skipped), the same comparison gives AE = 0. The
   flag does exactly what it says.

2. **Pre-capture discard verification (per this unit's own mandate) — FAILED,
   root-caused.** Before running the full 12/26/42 × ske/zom capture set, a
   single quick RGBA-mode render was checked per the plan's explicit
   instruction ("verify the discard actually fires... before the full set").
   Result: **AE = 0** between `fda2e34` (BEFORE, build-preP37/, verified
   non-stub: `gles2_zone_benchmark` 14,383,392 bytes / `willeq` 79,261,280
   bytes, exact match to every prior unit's documented size) and tip with
   `--no-entity-shader-promotion` (AFTER), RGBA mode (no `--eqe1-path`), ske
   race 21, orbit-radius 8/height-offset 6, frames 12/26/42 — pixel-for-pixel
   identical, not just diff-confined-to-cutout. Re-checked at orbit-radius 3
   (closer framing): still AE = 0. **The built-in alpha-test discard does not
   fire anywhere in this framing.** Also checked the ETC1 leg with the same
   flag: also AE = 0 (expected once understood — see below — the ETC1 alpha
   *companion-texture binding* on unit 1 lives entirely inside
   `applyEntityShaderMaterialPromotion()`, so skipping that function for a
   neutral capture also removes the only mechanism that puts real alpha data
   on unit 1 for the ETC1 path; ETC1 was never a viable route for this
   particular ask).

## Root cause (why RGBA-mode discard doesn't fire even though it should, per the plan's own reasoning)

The plan's premise — "P46 Part A supplies unit-0 alpha, which the built-in
alpha-test shader reads" — is correct in isolation but doesn't survive this
benchmark's actual call ordering. Added a temporary diagnostic (reverted
before this commit, `git diff` confirmed empty on both touched files) in
`src/client/graphics/eq/mesh_building.cpp`'s `buildMeshFromGeometry()`
(P44b/P46b's own confirmed sole path to P37's masked-material marker) that
printed, for every masked buffer, whether `ZoneMeshBuilder`'s
`texturesWithAlpha_` set had an entry for that texture name at the moment
this function assigned `EMT_TRANSPARENT_ALPHA_CHANNEL_REF`. Result for SKE
race 21: **`skefa0001.bmp`, `skeft0001.bmp`, `skeft0002.bmp`,
`skech0001.bmp`, `skelg0001.bmp` — all `hasAlphaInCache=0`** (opaque),
despite being on P37's ground-truth masked list.

Traced to a call-ordering / cache-key collision, confirming P46b's own
hypothesis 2 ("Cache-key collision in ConstrainedTextureCache") — which
P46b's bisect matrix treated as exonerated, but only tested whether
reverting the *decode fix itself* changed the AE (it didn't, because the
decode fix's effect was already being neutralized by this ordering issue,
independent of whether the fix was present):

1. `ConstrainedTextureCache::getOrLoad()` (`src/client/graphics/constrained_texture_cache.cpp:111-196`)
   is keyed by **texture name only**. On a cache hit (`cache_.find(name)`,
   line 119-124) it returns the already-uploaded texture unconditionally —
   the `applyPaletteMask` parameter is only consulted on the *first* decode
   of a given name (cache miss, line 180). Whichever caller decodes a name
   first wins for the lifetime of the process.
2. `tools/hwcaps/zone_bench_loader.cpp`'s "Pre-load meshes for each race"
   loop (~line 741) calls `RaceModelLoader::getMeshForRace()`
   **unconditionally, before the `--animate` block**, for every race in
   `raceIds` — including race 21 even when only an animated node is
   ultimately rendered.
3. `getMeshForRace()` (`src/client/graphics/eq/race_model_loader.cpp:405-454`)
   builds the STATIC mesh via `meshBuilder_->buildTexturedMesh(...)`
   (`src/client/graphics/eq/zone_geometry.cpp:270+`), which loads every
   per-part texture via `ZoneMeshBuilder::loadTextureFromBMP()`
   (`zone_geometry.cpp:236-268`) → `constrainedCache_->getOrLoad(name, data,
   uploadCategory_, "loadTextureFromBMP")` at **line 249 — no
   `applyPaletteMask` argument, defaults to `false`.** This is the FIRST
   decode of `skeft0001.bmp` etc. in the process, and it decodes them
   opaque, permanently occupying that cache slot.
4. Only afterward does the masked-aware decode run — either
   `RaceModelLoader::preloadModelData()`'s T05 loop
   (`race_model_loader.cpp:999-1008`, passes `isMaskedMaterial=true`) or
   `mesh_building.cpp::buildMeshFromGeometry()`'s own `getOrLoadTexture()`
   call — but by then `cache_[name]` already holds the opaque texture from
   step 3, so every subsequent lookup for that name (masked-aware or not)
   returns the same stale, alpha=255 object. `mat.setTexture(0, texture)`
   in `mesh_building.cpp:259` therefore always binds an opaque texture for
   masked entity buffers in this benchmark tool, regardless of the
   `--no-entity-shader-promotion` flag, regardless of P46 Part A being
   present, and regardless of ETC1 vs RGBA leg (RGBA is the one this
   mechanism actually degrades — ETC1's real alpha lives in a wholly
   separate companion-texture path, untouched by `decodeBMP`).

This explains, retroactively, P46b's own bisect-matrix finding that
"reverting Part A changes NOTHING" (bisect matrix, `refactor/P46b_wrong_texture_root_cause.md`
lines 103-105) — not because Part A's decode fix is inert or wrong in
isolation, but because this benchmark's call ordering means Part A's
`applyPaletteMask=true` decode almost never gets to be the *first* decode of
a masked entity texture's name. **P46's/P46b's committed RGBA-leg AE numbers
(1937–9013 depending on frame/race) were never demonstrating an alpha
cutout** — they were demonstrating the same lighting-model delta the ETC1
leg and this unit's own promotion on/off test isolate. The ETC1 leg's
numbers are real (companion-texture alpha bypasses this cache entirely), but
the RGBA "secondary" pair's interpretive claim ("P37 + P46 Part A jointly")
was not supported by what the pixels actually show.

## Scope boundary — why this is a STOP, not a fix-in-place

The mechanism above lives entirely in production source
(`constrained_texture_cache.cpp`, `zone_geometry.cpp`, `race_model_loader.cpp`)
and in this benchmark tool's own load-ordering (`zone_bench_loader.cpp`'s
unconditional static-mesh preload loop, which predates this unit and exists
for reasons unrelated to P46c). P46c's rules are explicit: *benchmark-only
source changes (the flag); no production source, no goldens, no presets.*
Fixing the cache-key collision (e.g., keying on `(name, applyPaletteMask)`,
or having `getOrLoad` refuse a mismatched-mask cache hit) is a production
change with implications beyond this benchmark — it needs its own errand,
scoped and reviewed on its own, not folded into a bless-capture unit. This
also has a real (not yet measured) production question attached: does any
live-client code path load a masked entity's textures via an unmasked
route before the masked-aware route runs, the same way this benchmark's
static-preload-then-animate ordering does? Not established either way here
— flagging as an open question for whoever picks this up, not asserting a
production bug.

Given this, delivering an RGBA "neutral" pair right now would either (a)
show AE=0 everywhere — actively misleading, since it would read as "P37+P46A
have zero visual effect" rather than "this instrument can't show it," or (b)
require silently reverting to the promoted-shader path, which reintroduces
exactly the lighting-model delta this unit exists to eliminate. Per the
errand's own rule — "STOP-with-explanation beats a pair with any unexplained
delta" — the mirror case (an unexplained *absence* of the delta being
tested) is the same STOP.

## What was and wasn't verified

- ctest: 1614/1615, sole exception `AudioManagerTest.PreloadCommonSounds`
  (documented ALSA host quirk) — full baseline clean, no regressions from the
  flag.
- Build: `cmake --build build -j24` clean.
- Binary sizes: `build/bin/gles2_zone_benchmark` 14,392,704 bytes /
  `build/bin/willeq` non-stub; `build-preP37/` reused, verified byte-matching
  every prior unit's documented BEFORE size.
- ARM cross-build and full visual gate were **not** run — no bless pair was
  produced to gate, and no production source changed, so there is nothing
  for `visual_gate.sh`/`build-arm-noble.sh` to validate beyond what P46b
  already ran on the same tip. Will run them as part of whichever follow-up
  unit fixes the cache-key issue and completes this capture.
- Temporary diagnostic (`mesh_building.cpp`, `zone_geometry.h`) added, used
  once, reverted via `git checkout --` — confirmed empty diff on both files
  before this commit (same precedent as P44b/P46b's own temporary
  diagnostics).

## Recommendation for the follow-up unit

1. Root-cause-fix the cache-key collision in `ConstrainedTextureCache`
   (production, its own errand — likely keying decoded/cached entries on
   `(name, appliedMask)` or asserting on a mismatched re-request, plus an
   audit of every `getOrLoad()` call site's ordering assumptions).
2. Once fixed, this unit's `--no-entity-shader-promotion` flag should work
   as designed for the RGBA leg — re-run the verification render first,
   confirm non-zero AE confined to mask regions, then proceed with the full
   12/26/42 × ske/zom capture per this plan's original Work section.
3. Investigate whether the same static-preload-before-masked-decode ordering
   exists anywhere in production's own entity load path (not just this
   benchmark) — open question, not yet investigated.

## Deliverables (this unit)

- Flag: `tools/hwcaps/gles2_zone_benchmark.cpp`,
  `tools/hwcaps/zone_bench_loader.{h,cpp}` — commit "P46c flag".
- This STOP writeup — commit "P46c evidence/STOP".
- `refactor/deferred.md` P46b bless entry annotated with a pointer to this
  STOP (round 5 blocked, root cause identified, no new pair delivered).
- No stills produced under `/mnt/projects/fable/videos/p46c_bless/` beyond
  this session's own scratch verification renders (not delivered as
  evidence — they show the AE=0 null result, not a bless pair, and were not
  moved into a committed-adjacent location).
