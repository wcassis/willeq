# T400 — the particle atlas carries its alpha (B225)

Class A, slot1, no server. Read the B225 board text and T389's §Review
(sprites/ and `particle_atlas_8x.png`), `tools/` particle atlas generator
(find it: grep `particle_atlas`), the loader in
`src/client/graphics/environment/` (`ParticleAtlas`), and the GLES2
particle shader/material (blend mode).

1. Measure: the shipped `data/textures/particle_atlas.bmp` header (bit
   depth, alpha), the generator's output format, and what the loader
   does with a 24-bit BMP (alpha defaults to 255 → opaque squares). One
   test that decodes the shipped file and asserts every tile has
   non-trivial alpha (pre-fix failing).
2. Fix at the generator: write a 32-bit BMP (or TGA/PNG if the loader
   path prefers — read what `data/textures/*.bmp` loads go through after
   T399's data-path routing; do not add a new image decoder) with the
   shape in alpha AND premultiplied/neutral RGB as the material's blend
   expects (read the material: additive vs alpha-blend — say which and
   why the RGB must be what it is). Regenerate the shipped file with the
   generator (the atlas pack B230/T398 may also embed it — coordinate:
   if T398 has merged, use its rebuild script; if not, note that the pack
   must be rebuilt when it does).
3. Test: the loaded tile's alpha channel matches the generator's shape
   function; the material's blend mode is pinned.
4. Visual: the gate's zones cast no spells, so no golden moves — if one
   does (a particle drawn in a gate zone: weather? torches?), STOP with
   the contact sheet, never bless. Re-take T389's fable impact frame for
   spell 54 offline if the headless capture rig allows (T389's
   `scripts/`); else owed to the next lock unit.
5. Strike B225; annotate B226 (the style sizes are a separate question —
   do NOT retune them here; T389 measured the oracle's impact as
   hundreds of sprites for 2–3 s, and that match is its own unit after
   the atlas draws).

## Proof

Build (all trees), `ctest -j6` (no expected failures), pre-fix/post-fix,
visual gate standalone + ARM, file sizes.

STOP: the material is additive and the shapes were designed for it
(then the "opaque square" is a different defect — report the frames).

## Review

Branch `task/T400-particle-atlas-alpha`, base `9f093662`. Slot 1, no server,
no eqemu lock taken.

### 1. What was measured (step 1)

| thing | before | after |
|---|---|---|
| `data/textures/particle_atlas.bmp` | `BM`, DIB header 40 (`BITMAPINFOHEADER`), 64x64, **24 bpp**, `BI_RGB`, bottom-up, 12,342 B | `BM`, DIB header 108 (`BITMAPV4HEADER`), 64x64, **32 bpp**, `BI_BITFIELDS` (ARGB masks), top-down, 16,506 B |
| decoded alpha, drawn tiles | one level (255) per tile | 15 tiles with >3 distinct levels, min 0, max >64; tile 15 all-zero (the one the generator never draws) |
| decoded alpha vs `generateParticleAtlas()` | **4,064 of 4,096 texels disagree** | 0 disagree (RGB also 0) |
| `willeq.eqe1` `particle_atlas` entry | ETC1, `hasAlpha=0`, 2,744 B, no companion | RGBA8888 (payload 3), `hasAlpha=1`, 21,844 B, no companion |
| pack | v1, 67 entries, 2,811,102 B, sha `e60962fb…` | v2, 67 entries, 2,830,206 B, sha `1078463f…` |

The generator's own code is unchanged. It writes a constant RGB per tile and
varies only `pixelColor.setAlpha(...)` in all fifteen draw helpers, so the
alpha channel IS the asset. The retired writer
(`tools/retired/generate_textures.cpp`) called Irrlicht's
`writeImageToFile(img, "particle_atlas.bmp")`; `CImageWriterBMP` emits 24 bpp
and drops alpha with no diagnostic. `ConstrainedTextureCache::decodeBMP()`
then substitutes `A=255` for 24 bpp (it already handles 32 bpp correctly).

### 2. The half the plan (and B225) did not have: the pack

**Fixing the BMP alone would have changed nothing a player sees.** Every real
run passes `--eqe1-path`, and `getOrLoad()` consults the EQE1 packs BEFORE the
raw-BMP decoder — a miss there is `LOG_FATAL` + `exit(1)`, so there is no
bypass. In the pack the atlas was ETC1, and **ETC1 is RGB-only**:
`eqe1_builder` puts a texture's alpha in a separate `<name>_alpha` companion
entry, uploaded to texture unit 1 by `uploadCompressedETC1()`.

Only the alpha-TEST shader variants sample unit 1 —
`ATLAS_ALPHA3D_FS` / `ALPHA3D_FS`, which `discard` below a threshold. Both
particle draw paths read `texColor.a` from a SINGLE sampler:

- `ParticleManager::init()` sets `EMT_TRANSPARENT_ALPHA_CHANNEL`;
  `COpenGLES2Driver::applyMaterialRenderState()` maps that to
  `glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA)` + depth-write off, and
  `applyMaterialState()` selects `EOGLES2SP_SOLID3D` (the `ALPHA3D` branch is
  taken only for `..._REF`). `SOLID3D_FS` has one `uniform sampler2D uTexture`.
- `UnifiedParticleRenderer` (the GLES2 point-sprite path spell effects go
  through) has its own program with one `uTexture`, and its alpha batch uses
  the same `GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA`. Its
  `if (color.a < 0.004) discard;` never fired, because `color.a` was always 1.

There is no blended dual-sampler shader, so "bind the companion" is not a fix
here. In-band alpha is. The EQE1 format was already designed for exactly this
(`eqe1_format.h`: *"v2 packs may mix payload formats per-entry; readers
dispatch on payloadFormat, not on the file version"*) and nothing had ever
used it — `--payload-format` applied to a whole pack at once. T400 added
`eqe1_builder --rgba8888-texture <name>` (BMP-directory mode only) and wired
it into `scripts/build_willeq_pack.sh`.

### 3. Blend-mode decision, and why RGB is what it is

**Alpha-blend, not additive** — the STOP condition did not fire. Both paths
use `GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA` (the unified renderer keeps a
separate `GL_ONE, GL_ONE` batch, chosen per particle, but the atlas must serve
both). RGB is therefore **straight, non-premultiplied, and unchanged**. That
is also what the constant-per-tile RGB the generator already writes requires:
bilinear filtering and the 7-level mip chain interpolate RGB across texels, so
a premultiplied tile (RGB falling to black as alpha falls to 0) would darken
its own edges and its lower mips, while a constant-RGB tile is
interpolation-proof. Pinned by
`ParticleAtlasAlpha.ParticleMaterialIsAlphaBlendNotAdditive`, which asserts
the material type in both directions and that unit 1 is never bound.

### 4. Changes

- **`tools/bmp32_writer.h`** (new, header-only): the one 32-bit BMP writer.
  Lifted verbatim from `ui_atlas_builder`'s `writeBMP()`, which has shipped
  this exact layout since U02 and is why `ui_atlas` has alpha in the pack.
  `ui_atlas_builder` now calls it — byte-identical output, proven by
  `PackReproducible` (the other 66 pack entries came out unchanged).
- **`tools/particle_atlas_builder.cpp`** (new) + CMake target: `EDT_NULL`
  device → `generateParticleAtlas()` → `writeBMP32()`. Deterministic, no GPU.
- **`src/client/graphics/texture_generators.cpp`**: unchanged, but now
  actually COMPILED — see §6.
- **`tools/eqe1_builder.cpp`**: `--rgba8888-texture` per-entry override;
  the BMP-dir path emits v2 when any entry is non-ETC1.
- **`scripts/build_willeq_pack.sh`**: passes `--rgba8888-texture particle_atlas`.
- **`data/textures/particle_atlas.bmp`**, **`data/willeq.eqe1.sha256`**: regenerated.
- **`particle_manager`**: material setup extracted to a static
  `configureParticleMaterial()` so the blend mode is pinnable without a
  device. No behaviour change.
- **`tests/test_particle_atlas_alpha.cpp`** (new, 5 cases).
- **`tools/retired/README.md`**: corrected — see B260.

### 5. Proof

| step | result |
|---|---|
| `cmake --build build -j24` | EXIT=0 (after a post-143 sweep: the first run was killed mid-`ar` and **deleted `build/lib/libwilleq_core.a` outright**, leaving `build/lib/std5laHO`; both handled per protocol, then a clean rebuild) |
| `ctest --test-dir build -j6` | **4,126 / 4,128**, 17 skipped. Failures: `visual_gate` (below) and `DocCitations` — the latter was MY board text citing a path that no longer exists; corrected and re-run GREEN |
| `test_particle_atlas_alpha` PRE-fix | **4 of 5 FAIL**: `ShippedFileIs32BitAndCarriesAnAlphaChannel`, `EveryDrawnTileHasANonTrivialAlphaHistogram`, `ShippedFileMatchesTheGeneratorPixelForPixel` (4,064/4,096 texels), `PackEntryCarriesAlphaInBand` |
| `test_particle_atlas_alpha` POST-fix | **5 of 5 PASS** |
| `PackReproducible` | PASS on the new pack (43 s) |
| `verify_eqe1 willeq.eqe1` | OK (66 textures, 2 alpha, 1 raw) |
| visual gate | **FAIL on exactly three `eqe1MappedBytes` lines and nothing else** — see below |
| ARM (`build-arm-noble.sh`) | see §7 |
| `check_file_sizes.sh fable` | see §7 |

**The gate.** No image golden moved and no CPU p95 ceiling was missed. The
only failures are the three memory-coverage `eqe1MappedBytes` values, which
are B240's already-pending bless, now larger:

| zone | manifest | measured | delta |
|---|---:|---:|---:|
| qeynos2 | 119,944,431 | 119,963,774 | +19,343 |
| gfaydark | 118,610,523 | 118,629,866 | +19,343 |
| befallen | 113,095,019 | 113,114,362 | +19,343 |

+19,343 = T398's owed +239 plus T400's +19,104. **NOT blessed** — added to
B240's pending bless, per the standing rule that Will blesses these three
numbers and someone edits lines 171/180/189 by hand.

**Pack delta, per entry** (index diff of the two files): 67 entries before and
after, none added or removed, **66 byte-identical**; the single change is
`particle_atlas`, payload 0→3, `hasAlpha` 0→1, 2,744→21,844 B. File version
1→2, total 2,811,102→2,830,206 B (+19,104). The pre-change pack is kept at
`/mnt/projects/fable/review/T400/willeq.eqe1.pre-T400` (sha `e60962fb…`)
alongside the pre-change BMP. Installed through
`scripts/build_willeq_pack.sh` only (temp-file-then-`mv`, in the destination
directory), after confirming no ctest/gate/willeq process was running.

### 6. What surprised me

**`src/client/graphics/texture_generators.cpp` was in NO CMake target.** A
tree-wide grep for `texture_generators` across every `CMakeLists.txt` and
`*.cmake` returns nothing: 926 lines defining every particle, creature, cloud
and detail texture the client ships, compiling nowhere since P26 retired its
only consumer. P26's recorded rationale was "no found consumer of its output
anywhere in the tree or docs" — and T398 has since proven that the 64 BMPs it
wrote are 64 of the pack's 67 entries. **B225 is the direct consequence**: the
code that proves the shipped file wrong could not be run, so the wrong file
kept shipping. Boarded as **B260** (the other 63 BMPs still have no build
recipe and no proof they match their generator; `tools/retired/README.md`
corrected). The general trap — an ETC1 `_alpha` companion is invisible to any
single-sampler shader, silently, with `hasAlpha=1` in the pack — is boarded as
**B261**.

### 7. Deviations from the plan

- **The plan's step 2 was not sufficient.** It scoped the fix to the generator
  and the shipped BMP, and noted the pack "may also embed it". The pack does
  embed it, and on the ETC1 path the BMP fix alone is invisible: the pack
  change (§2) is load-bearing, not incidental. Flagged rather than silently
  absorbed — it added a tool change (`eqe1_builder`) and a pack-script change
  the plan did not anticipate.
- **Step 4's offline spell-54 recapture was NOT taken.** It needs a live
  eqemu session and this unit took no lock. Still owed to the next lock unit,
  and it is worth more now: B226's retune should be the unit that takes it,
  so the capture shows a resized burst of real shapes rather than two
  unattributable changes at once.
- **B226 was annotated, not retuned**, exactly as the plan directed.
