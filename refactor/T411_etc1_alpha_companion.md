# T411 — alpha through the pack's own companion channel, not RGBA8888 (B287, B261)

Class B (the Pi's GPU texture budget: 56 of 59 textures at 256² cost
349,528 B as RGBA8888 vs 43,688 B as ETC1+mips; the pack grew +606% and
every zone maps +17.2 MB), slot1, no server. Read B287, B261, T400's and
T406's §Reviews (the consumers and their single-sampler shaders:
`SOLID3D_FS`, `UI2D_FS`, the particle shader), the EQE1 format's
`<name>_alpha` companion (`tools/eqe1_builder`, the loader in
`constrained_texture_cache`), and the alpha-TEST shaders that already
sample the companion on unit 1.

1. Give the three alpha-BLEND shaders a companion sampler on unit 1
   (`uAlphaTex`, `hasAlpha` uniform): `texColor.a = texture2D(uAlphaTex,
   uv).r` when the entry carries a companion. Bind the companion at the
   same sites the alpha-test path binds it (read how; one code path, not
   a second). Materials for storm clouds, rain/snow overlays, cursor,
   billboards, particle atlas switch to it.
2. Drop the `--rgba8888-texture` overrides for every entry whose consumer
   now samples the companion; keep only the ones that provably cannot
   (say which). Rebuild the pack via the script (atomic; copy the old pack
   under `review/T411/`); the pack should return close to 2.8 MB (+ the
   companions, which are ETC1 too — measure). Update the sha256 pins in
   the same commit.
3. Tests: per consumer, a headless render with the companion bound yields
   the same alpha as T406's RGBA8888 path (pre-fix: the companion is
   ignored → opaque); the pack census asserts companions present and no
   RGBA8888 payload except the documented exceptions.
4. Visual gate standalone: no image golden should move (alpha identical
   by construction — if one moves, the companion is not equivalent: STOP
   with the contact sheet). `eqe1MappedBytes` WILL drop; report the new
   three numbers for a B240-style bless (Will blessed the up-move; the
   down-move is put to him the same way — do not bless).
5. GPU texture memory on the Pi: the P13 ledger shape — report the
   qeynos2 upload total before/after (the coverage run's
   `gpuTextureBytes` if the gate emits it).
6. Strike B287, B261.

## Proof

Build (all trees), `ctest -j6`, pre-fix/post-fix, gate standalone + ARM,
file sizes.

STOP: a consumer's shader cannot take a second sampler on the DRM/Pi
path (unit count); report it.

## Review

Branch `task/T411-etc1-alpha-companion`, base `b274ec2f`. Slot 1, no server,
no eqemu lock taken (T405 held it).

### 0. The headline

The plan asked whether alpha could travel in the pack's own companion channel
instead of an in-band RGBA8888 payload. It can, for **60 of the 61 entries
that carry alpha**: `scripts/build_willeq_pack.sh`'s `--rgba8888-texture` list
went from 60 names to one, and the survivor (`ui_atlas`) was never on the list
-- it is there for a different reason, measured in section 4, and it is pinned
by `kInBandAlpha` in the census test so a future addition needs the same kind
of reason.

| | pre-T411 (T406) | post-T411 | delta |
|---|---:|---:|---:|
| `willeq.eqe1` | 19,994,910 B | 5,532,254 B | **-14,462,656 (-72.3%)** |
| pack sha256 | `2f76087a…` | `aca03e7f…` | pinned in the same commit |
| entries | 67 | 126 (65 tex + 60 companions + 1 raw) | the companions are the growth |
| `eqe1MappedBytes` (each of 3 zones) | see B295 table | | **-14,462,656** |
| a 256x256 weather entry, pack | 349,524 B `[rgba8888]` | 43,704 B `[etc1]` + 43,704 B companion | -73.5% |
| the same entry, GPU upload | 349,524 B | 87,376 B | **-75%** |

It does NOT return to the pre-T406 2.83 MB, and that is correct: the
companions are bytes that never existed before T406 (the old BMPs were 24-bit
and carried no alpha at all). 5.53 MB is 2.83 MB of RGB, ~2.4 MB of
one-channel alpha, and ui_atlas's in-band payload -- which is what the format
was designed to charge.

### 1. Which shaders gained the sampler, and how the companion is bound

Three alpha-BLEND programs, one binding mechanism.

| program | where | consumers |
|---|---|---|
| `SOLID3D_FS` | `docker/irrlicht-drm/COGLES2Shaders.cpp` | `EMT_TRANSPARENT_ALPHA_CHANNEL` -- the storm cloud dome, the boids and tumbleweed billboards (also the FS of AtlasSolid3D, where blending is off so it cannot matter) |
| `UI2D_FS` | same file | every `draw2DImage(..., useAlphaChannelOfTexture=true)`: the rain and snow overlays, the software cursor, and the whole static UI's `ui_atlas` |
| `POINT_SPRITE_FS` | `src/client/graphics/environment/unified_particle_renderer.cpp` | `UnifiedParticleRenderer` -- the particle atlas |

Each gained `uniform sampler2D uAlphaTexture;` and one line:

    texColor.a *= texture2D(uAlphaTexture, uv).r;

**There is no `hasAlpha` uniform.** The plan proposed one; an unconditional
MULTIPLY is exact for both payload shapes and needs no per-draw state:

* ETC1 payload -- `texColor.a` is 1.0 (ETC1 is RGB-only), the companion holds
  the alpha, so `alpha = companion.r`;
* in-band alpha (RGBA8888, a raw BMP decode, a DDS) -- no companion exists,
  the driver binds a 1x1 opaque-white texture, so `alpha = texColor.a`,
  bit-identical to before.

That second case is not an argument, it is three of the six cases in
`tests/test_alpha_blend_companion.cpp`.

**How the companion is bound.** Not at the consumer sites. Before this unit,
seven call sites did `mat.setTexture(1, cache->getAlphaTexture(tex))` by hand
(entity_renderer, zone_geometry x4, zone_geometry_atlased,
renderer_zone_stream, renderer_zone_load_env x2) and every alpha-BLEND
consumer had simply never been written to do it -- while `draw2DImage` and
`draw2DImageBatch` bound **nothing at all** on unit 1, so `UI2D_FS` would have
sampled whatever the previous draw left there. The companion is a property of
the TEXTURE, so it now lives on the texture:

1. `COGLES2Texture` gains `setAlphaCompanion()` / `getAlphaCompanion()`
   (a raw, non-grabbed pointer; safe because `evictTexture()` removes only the
   main texture, so the companion always OUTLIVES the object pointing at it).
2. `gles2SetAlphaCompanion(void*, void*)` in `COpenGLES2Driver.cpp` -- the
   sibling of the existing `gles2WrapTexture()` free function, and the same
   `void*` bridge the app already uses to reach driver internals.
3. `ConstrainedTextureCache::uploadCompressedETC1()` calls it once, at the one
   point where both GL handles are wrapped.
4. `COpenGLES2Driver::bindAlphaCompanionUnit(tex0, explicitUnit1)` -- ONE
   function, called from `applyMaterialRenderState()` **and** both
   `draw2DImage` overloads. Preference order: the material's explicit unit-1
   texture (the atlas alpha page and the seven hand-bound sites keep working
   unchanged), then `tex0`'s own companion, then the white fallback.
5. The particle renderer bypasses the driver entirely (raw GL), so it takes
   the companion's GL handle as a new `render()` argument that
   `ParticleManager` fills from `constrainedCache_->getAlphaTexture()`, and
   creates its own 1x1 white fallback. It also now saves and restores texture
   unit 1 alongside unit 0.

No consumer file was edited. The storm dome, the billboards, the rain/snow
overlays and the cursor all got the companion without knowing it exists.

### 2. Which entries kept RGBA8888, and why

**One: `ui_atlas`, and it was never on T400/T406's list.** All 60 of theirs
were dropped. `TextureAlphaCensus.NoPackEntryNeedsTheRgba8888Payload` asserts
that every non-ETC1 payload in the pack appears in `kInBandAlpha` with a
reason, and that every `kInBandAlpha` entry really is in-band.

`ui_atlas`'s reason is not "single sampler" -- `UI2D_FS` samples the companion
now. It is **mip selection**, found by a red test rather than by argument, and
this is the most interesting thing the unit turned up:

* `UIRenderer::drawRect()` paints solid colour by tinting the atlas's 4x4
  `white` sprite. A spell-gem border is a **32x1** destination rect, so v
  minifies 4:1 and GL selects mip level 2 of texture unit 1.
* The companion's mip chain is ETC1-encoded **per level**, and by level 2 the
  `white` sprite has shrunk to a single bright texel alone in a 4x4 block --
  which ETC1 cannot represent. Decoding `ui_atlas_alpha` straight out of the
  pack: level 0 = 255, level 1 = 255, **level 2 = 217**, level 3 = 68.
* 217/255 x 200 = 170. `UIAtlasPixelTest.SpellGemReadyBorderIsGreen` read
  **172** against a wanted 200 (tolerance 24) -- a 15% dimming of every 1px UI
  rect. Its 2px-tall sibling `SpellGemMemorizeTargetMarkerIsGold` selects mip
  level 1 and stayed green, which is what pointed at the mip level.
* Unit 0 carries no mip filter at all (T275 dropped it to plain `GL_LINEAR`
  because these chains are not always mipmap-complete), so an in-band alpha has
  no such artifact. With `ui_atlas` in-band all 14 `test_ui_atlas_pixels` cases
  are green.

Cost of the exception: `ui_atlas` 43,704 + 43,704 -> 349,524, i.e. **+262,116
bytes** against ~14.7 MB saved on the other 60. The 3D alpha-test and
alpha-blend paths never minify a 4x4 sprite that hard, which is why only this
one entry is affected.

The four 24-bit files are unaffected -- they have no alpha to carry, so they
were never in the list: `spell_particle_fallback` (B286, alpha provably not
sampled on an additive blend), `detail_atlas` (B265), `rain_streaks` and
`snow_flakes` (no consumer).

The one quality caveat, stated plainly: for the 60, alpha is now
**ETC1-compressed (4 bpp, lossy)** rather than an exact 8-bit channel. For the
32 storm clouds and the rain/snow sheets that is a smooth gradient going
through a block codec. No image golden moved, but no gate golden photographs
weather either.

### 3. Tests, pre and post

`tests/test_alpha_blend_companion.cpp` (new, 385 lines, 6 cases, registered in
`tests/cmake/ui_rendering.cmake` beside `test_eqe1_alpha_companion_mip`, whose
headless-GLES2 idiom and self-skip it reuses). It builds a synthetic 2-entry
EQE1 pack -- an ETC1 payload that decodes to solid WHITE plus a companion that
decodes to solid BLACK (alpha 0) -- uploads it through the real
`ConstrainedTextureCache`, draws it over a magenta clear through each of the
three paths, and reads the centre pixel back.

| case | pre-fix | post-fix |
|---|---|---|
| `TwoDCompanionMakesTheDrawTransparent` | **FAIL**, read back (255,255,255) | PASS |
| `ThreeDCompanionMakesTheDrawTransparent` | **FAIL**, read back (255,255,255) | PASS |
| `ParticleCompanionMakesTheSpriteTransparent` | **FAIL**, read back (255,255,255) | PASS |
| `TwoDWithoutACompanionStaysOpaque` | PASS | PASS |
| `ThreeDWithoutACompanionStaysOpaque` | PASS | PASS |
| `ParticleWithoutACompanionStaysOpaque` | PASS | PASS |

The pre-fix column is a real run: the three `texColor.a *= …` lines were
deleted, the patched Irrlicht and the test binary rebuilt, the binary run, and
the sources restored from a copy (never `git stash` / `git checkout --`). The
three controls staying green in the SAME run is the half that proves the
multiply is a no-op without a companion.

`tests/test_texture_alpha_census.cpp` -- assertion (3) inverted. The three
`…PackEntriesCarryAlphaInBand` cases became `…CarryAlphaInACompanion`
(ETC1 payload + `alphaData != nullptr` + a `<name>_alpha` entry), a
`particleSet()` and its three cases were added (the particle atlas had a
census row but no pack assertion), and
`NoPackEntryNeedsTheRgba8888Payload` was added.

| | pre-fix (against the T406 pack) | post-fix (against the rebuilt pack) |
|---|---|---|
| `TextureAlphaCensus` | **13 of 18 pass, 5 FAIL** | **18 of 18** |

The five: `WeatherPackEntriesCarryAlphaInACompanion`,
`SoftwareCursorPackEntryCarriesAlphaInACompanion`,
`BillboardPackEntriesCarryAlphaInACompanion`,
`ParticleAtlasPackEntryCarriesAlphaInACompanion`,
`NoPackEntryNeedsTheRgba8888Payload` (which named 60 offenders).

### 4. The gate, and the thing it does not photograph

Standalone `scripts/ci/visual_gate.sh`: **FAIL on exactly the three
`eqe1MappedBytes` lines and nothing else.**

| zone | manifest (T406-blessed) | measured | delta |
|---|---:|---:|---:|
| qeynos2 | 137,128,478 | 122,665,822 | -14,462,656 |
| gfaydark | 135,794,570 | 121,331,914 | -14,462,656 |
| befallen | 130,279,066 | 115,816,410 | -14,462,656 |

No image golden moved (`git status tests/goldens/` empty after the run) and
all three CPU p95 numbers are inside their ceilings (7.041 / 5.620 / 6.081 vs
10.108 / 10.672 / 10.948). **Not blessed** -- board **B295**, same rule as
B240's up-move.

**But the clean image result deserves suspicion, and section 4 is where it
gets it.** `ui_atlas` has always been ETC1 with a `ui_atlas_alpha` companion,
and the whole static UI draws it through `draw2DImage(..., useAlpha=true)` ->
`UI2D_FS`. So this unit did not only give the weather textures their alpha --
it gave the UI its alpha, on a path that has been drawing OPAQUE since the
program was written. Measured on the generated atlas: **77.1% of its 65,536
texels are not opaque and 64.0% are fully transparent** (42 levels, min 0 max
255). That no golden moved therefore proves the gate's three zones capture no
UI, not that nothing changed. One piece of it WAS pinned and did go red --
`UIAtlasPixelTest.SpellGemReadyBorderIsGreen`, section 2 -- and chasing that
red is what produced the `ui_atlas` exception; with it in place all 14
`test_ui_atlas_pixels` cases are green, so no pinned UI pixel changed. The
unpinned rest of the UI is boarded as **B296** with the measurement rather
than declared fine.

### 5. GPU texture memory (plan step 5)

Measured properly rather than asserted: the memory-coverage benchmark was run
twice for qeynos2 with identical arguments, once against a directory of
symlinks whose `willeq.eqe1` is the saved pre-T411 pack and once against the
installed one (the shared pack was NOT mutated for this).

| | pre-T411 pack | post-T411 pack |
|---|---:|---:|
| `eqe1MappedBytes` | 137,128,478 | 122,665,822 |
| `gpuTextureBytes` | 9,682,104 | **9,682,104** |
| `textureCacheBytesUsed` | 9,156,008 | 9,156,008 |
| `textureCount` | 273 | 273 |

**The GPU upload is unchanged, and that is the honest answer**: the coverage
run activates no weather and spawns no particles, so not one of the 60 changed
entries is resident in it. B287 cited a P13 ledger showing all 32
`storm_cloud_blend_*` uploaded in qeynos2; that is a different (weather-active)
run and this gate configuration does not reproduce it. What IS measured is the
per-entry cost: a 256x256 weather texture uploads 349,524 B as RGBA8888 and
87,376 B as ETC1 + companion, so when the storm set is resident the saving is
32 x 262,148 = 8,388,736 B on the Orange Pi's texture budget. The mapped-bytes
saving (-14.7 MB per zone) is unconditional.

### 6. Proof

| step | result |
|---|---|
| `bash scripts/build-gles2-x86.sh --irrlicht-only` (x3: green, red, green) | EXIT=0 each |
| `cmake --build build -j24` | **EXIT=0** (no exit-143 in this unit; no post-143 sweep needed) |
| `cmake --build build-gles2 -j24 --target willeq gles2_zone_benchmark` | EXIT=0 |
| `bash scripts/build-arm-noble.sh` | EXIT=0, `build-arm-noble/bin/willeq` ELF 32-bit ARM EABI5 |
| `ctest --test-dir build -j6` | see the final report / section 7 |
| `TextureAlphaCensus` | 13/18 pre-fix -> **18/18** post-fix |
| `ParticleAtlasAlpha` | 5/5 (its pack case inverted with the census) |
| `UIAtlasPixelTest` + siblings | **14/14** (one red on the way, section 2) |
| `AlphaBlendCompanion` | 3/6 pre-fix -> **6/6** post-fix |
| `PackReproducible` (`build_willeq_pack.sh --check`) | PASS: rebuild byte-identical to the pinned hash AND the installed pack |
| `visual_gate` standalone | FAIL on the three `eqe1MappedBytes` lines only |
| goldens | untouched, no image moved |
| `check_file_sizes.sh fable` | see the final report |

The pack was installed only through `scripts/build_willeq_pack.sh` (temp file
in the destination directory + `mv`), after checking no `willeq`, `ctest` or
gate process was running -- slot2's T405 client and slot3's T409 ctest were
both live earlier and were waited out. The pre-change pack is at
`/mnt/projects/fable/review/T411/willeq.eqe1.pre-T411` and the post-change one
beside it, with a `README.md`.

### 7. Deviations from the plan

- **No `hasAlpha` uniform** (plan step 1). An unconditional multiply against a
  white fallback is exact for both payload shapes, needs no per-draw uniform,
  and keeps the "one code path" the same step asks for. Proven by the three
  no-companion control cases.
- **The binding is NOT "at the same sites the alpha-test path binds it"**
  (plan step 1). Following that literally would have added a ninth, tenth and
  eleventh hand-written `setTexture(1, …)` -- the duplicated-then-drifted
  pattern that caused B261 in the first place -- and it is impossible for
  `draw2DImage`, which has no material at all. The companion moved onto the
  texture and the binding into one driver function instead. The seven existing
  hand-bound sites still take precedence and were not touched.
- **The pack did not return "close to 2.8 MB"** (plan step 2). 5.53 MB, and
  the reason is arithmetic, not a defect: 60 ETC1 companions that did not
  exist before T406, plus ui_atlas in band.
- **Exactly one entry kept RGBA8888, and it is not one the plan had in mind**
  (`ui_atlas`, section 2). Found by a red pixel test, root-caused by decoding
  the companion's mip level 2 out of the pack, and pinned in `kInBandAlpha`.
- **`gpuTextureBytes` did not move** (plan step 5). Measured both ways rather
  than reported as a saving that the gate cannot see -- section 5.
- **No STOP fired.** No image golden moved, so no contact sheet. The UI-alpha
  change in section 4 is NOT a golden move; it is a change on a path with no
  golden, and it is boarded (B296) rather than absorbed.
- **New scope discovered by measurement**: B296 (the UI atlas alpha), and the
  bless request B295.
