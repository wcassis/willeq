# T406 — the other 63 textures keep their alpha (B266)

Class A, slot1, no server. Read B266, T400's §Review (the 32-bit BMP
writer and the pack's `--rgba8888-texture` override), T403's §Review
(`generate_textures --report`, `scripts/build_textures.sh`,
`data/textures.sha256`, the one-commit cascade rule in CLAUDE.md), and
the consumers per file (rain/snow/storm-cloud renderers, the cursor, the
spell fallback, tumbleweed).

1. Census from `--report`: per file, the alpha levels the generator
   draws and the consumer's material/blend (alpha-blend, alpha-test,
   additive, or opaque). A file whose consumer never samples alpha is
   NOT changed (say which).
2. For every file whose consumer samples alpha: write 32-bit (T400's
   writer), regenerate, and give the pack entry the rgba8888 override
   where the ETC1 companion path cannot be sampled (T400/B261 — read the
   consumer's shader: single sampler → override needed; alpha-test dual
   sampler → the companion already works, say so). One commit per
   consumer family (weather, cursor, spell fallback, detail/tumbleweed).
3. The one-commit cascade: regenerate → `--write-pins` → rebuild the pack
   via the script (atomic; copy the old pack under `review/T406/`; no
   ctest/gate/willeq running) → `data/willeq.eqe1.sha256` — in the SAME
   commit per family so `TexturesReproducible` and `PackReproducible`
   never go red on a tip.
4. Tests: per changed file, the shipped BMP carries a non-trivial alpha
   histogram (pre-fix failing); per consumer, the material is pinned.
5. Visual: storm clouds / rain / snow / cursor may appear in the gate's
   zones — run the gate standalone after EACH family; if a pixel golden
   moves, that is the alpha becoming visible: STOP with the contact sheet
   for Will, never bless, and leave that family's commit in place with
   the STOP recorded. Report the `eqe1MappedBytes` delta per family for
   B240's pending bless (do not bless).
6. Strike B266 (or annotate per family).

## Proof

Build (all trees), `ctest -j6` (expected reds: B240's gate delta only —
the atlas determinism test is green after T403), pre-fix/post-fix, gate
standalone per family, ARM, file sizes.

STOP: a pixel golden moves (per family, as above).

## Review

Branch `task/T406-universal-alpha`, base `68bb624d`. Slot 1, no server, no
eqemu lock taken. Three commits, one per consumer family:

| commit | family | files |
|---|---|---:|
| `75de2288` | weather | 56 |
| `d5dd61fc` | cursor | 1 |
| `93429d28` | billboards (detail/tumbleweed) | 2 |

### 1. The census (step 1)

From `generate_textures --report`, plus the consumer and its blend traced to
the GLES2 driver. "Sampler" is what the fragment shader actually reads: the
ETC1 `_alpha` companion lives on unit 1 and ONLY the alpha-TEST variants
(`ALPHA3D_FS`, `ATLAS_ALPHA3D_FS`) sample it, so a single-sampler consumer
needs the alpha in-band (B261).

| file(s) | n | alpha levels (min..max) | consumer | material / blend | shader | changed? |
|---|--:|---|---|---|---|---|
| `storm_cloud_blend_0..31` | 32 | 56..64+ (min 11..37, **max 87..107**) | `StormCloudLayer` dome | `EMT_TRANSPARENT_ALPHA_CHANNEL` -> `GL_SRC_ALPHA/GL_ONE_MINUS_SRC_ALPHA` | `SOLID3D_FS`, 1 sampler | **YES** |
| `rain_intensity_01..10` | 10 | 64+ (0..~219) | `RainOverlay` foreground | `draw2DImage(..., useAlpha=true)` -> same blend | `UI2D_FS`, 1 sampler | **YES** |
| `rain_layer_mid`, `rain_layer_far` | 2 | 64+ (0..220) | `RainOverlay` mid/far | as above | `UI2D_FS` | **YES** |
| `snow_intensity_01..10` | 10 | 64+ (0..134..255) | `SnowOverlay` foreground | as above | `UI2D_FS` | **YES** |
| `snow_layer_mid`, `snow_layer_far` | 2 | 64+ (0..226 / 0..113) | `SnowOverlay` mid/far | as above | `UI2D_FS` | **YES** |
| `software_cursor` | 1 | 2 (0, 255) | `FrameComponent::renderFrame()`, DRM only | `draw2DImage(..., useAlpha=true)` | `UI2D_FS` | **YES** |
| `creature_atlas` | 1 | 23 (0..255) | `BoidsManager::creatureMaterial_` | `EMT_TRANSPARENT_ALPHA_CHANNEL` | `SOLID3D_FS` | **YES** |
| `tumbleweed` | 1 | 2 (0, 255) | `TumbleweedManager` mesh buffer | `EMT_TRANSPARENT_ALPHA_CHANNEL` | `SOLID3D_FS` | **YES** |
| `particle_atlas` | 1 | 64+ | `ParticleManager`, `UnifiedParticleRenderer` | alpha blend | 1 sampler | already 32-bit (T400) |
| `spell_particle_fallback` | 1 | 64+ (0..254) | `SpellVisualFX` particle system | **`EMT_TRANSPARENT_ADD_COLOR` -> `GL_ONE, GL_ONE`** | `SOLID3D_FS` | **NO** — see §3 |
| `detail_atlas` | 1 | 64+ (0..255) | `DetailManager` / `DetailChunk` | `EMT_TRANSPARENT_ALPHA_CHANNEL` | `SOLID3D_FS` | **NO** — B265 |
| `rain_streaks`, `snow_flakes` | 2 | 64+ | **none** | — | — | **NO** — no consumer |

59 converted, 4 left 24-bit, 1 already done: 64.

**Not one consumer is alpha-test.** Every changed entry therefore takes
`--rgba8888-texture`; the ETC1 companion would have been invisible to all of
them. `scripts/build_willeq_pack.sh` now builds that list instead of passing
one name.

### 2. The two "no consumer" files are really unreferenced

`RainOverlay::initialize()` loads `rain_intensity_01..10` plus
`rain_layer_mid`/`rain_layer_far` and nothing else; `SnowOverlay` mirrors it.
A tree-wide grep for `rain_streaks` / `snow_flakes` over `src/ include/
tests/ tools/ benchmarks/` returns only `tools/generate_textures.cpp`,
`tests/test_data_paths.cpp`'s path list, and a P13 audit transcript. They are
still generated and still pinned (deleting a shipped asset is a different
decision), just not converted.

### 3. `spell_particle_fallback`: alpha is provably not sampled (new B286)

`generateSpellParticleFallback()` writes `SColor(alpha, 255, 255, 255)` — a
cosine falloff entirely in alpha over constant white. Its consumer sets
`EMT_TRANSPARENT_ADD_COLOR`, which
`COpenGLES2Driver::applyMaterialRenderState()` maps to `glBlendFunc(GL_ONE,
GL_ONE)`, and `SOLID3D_FS` computes `mix(uFogColor, texColor * vColor,
fog)` **without premultiplying RGB by alpha**. So `gl_FragColor.a` reaches no
visible channel and converting the file would have grown it 33% for zero
pixels. That the falloff is invisible on that path at all is a real defect —
the fallback draws as a flat white square — but it is a shader/blend defect,
not a texture-format one, so it is boarded as **B286** rather than "fixed" by
a file rewrite. Reached only when all three official-client DDS flares are
missing.

### 4. The cascade, per family

Each commit contains, together: the `BmpFormat::Bmp32` flip in
`tools/generate_textures.cpp`, the regenerated BMPs, `data/textures.sha256`
via `--write-pins`, the `--rgba8888-texture` additions in
`scripts/build_willeq_pack.sh`, the rebuilt-and-installed pack, and
`data/willeq.eqe1.sha256`. `TexturesReproducible` and `PackReproducible` were
run green at every commit (`63 byte-identical, 0 drifted, 1 exempt`; `PASS:
rebuild is byte-identical to the pinned hash and the installed pack`).

Files were regenerated into a temp directory and copied per family, never
`build_textures.sh` in place — an in-place run would also rewrite
`detail_atlas.bmp`, which no build reproduces (B265) and which is
RECIPE-exempt precisely so it cannot drift.

The pack was installed only through `scripts/build_willeq_pack.sh` (temp file
in the destination directory + `mv`), each time after checking no `willeq`,
`ctest` or gate process was running. The pre-change pack is at
`/mnt/projects/fable/review/T406/willeq.eqe1.pre-T406` (`1078463f…`,
2,830,206 B).

| after family | pack sha256 | bytes | delta |
|---|---|---:|---:|
| (before) | `1078463f…` | 2,830,206 | — |
| weather | `920d01bf…` | 19,956,350 | +17,126,144 |
| cursor | `0f084675…` | 19,956,702 | +352 |
| billboards | `2f76087a…` | 19,994,910 | +38,208 |

### 5. Tests

`tests/test_texture_alpha_census.cpp` (new, 459 lines, 14 cases, registered
in `tests/cmake/graphics.cmake`). Reads files only — the BMPs, the pack, and
the consumer sources — so it needs no device. Four kinds of assertion: the
shipped bit depth per file, a non-trivial alpha histogram, the pack entry's
payload format (`EQE1_PAYLOAD_RGBA8888`, `hasAlpha`, and NO `_alpha`
companion), and a source-level pin on each consumer's blend, because the
format follows from the blend. `EveryShippedBmpIsAccountedFor` cross-checks
the 64 pinned names against the census lists so a new generator has to
declare which side it is on.

| family | pre-fix | post-fix |
|---|---|---|
| weather | **3 of 6 FAIL** (`WeatherTexturesAre32Bit`, `WeatherTexturesHaveANonTrivialAlphaHistogram`, `WeatherPackEntriesCarryAlphaInBand`) | 6/6 PASS |
| cursor | **3 of 4 FAIL** (`SoftwareCursorIs32Bit`, `…HasANonTrivialAlphaHistogram`, `…PackEntryCarriesAlphaInBand`) | 10/10 PASS (whole binary) |
| billboards | **3 of 14 FAIL** (`BillboardCutoutsAre32Bit`, `…HaveANonTrivialAlphaHistogram`, `BillboardPackEntriesCarryAlphaInBand`) | 14/14 PASS |

The material pins are source-level deliberately: all three materials are
configured inside methods that need a live `IVideoDriver`, and the point is
not to re-run the setter but to fail loudly if the BLEND changes.

### 6. Proof

| step | result |
|---|---|
| `cmake --build build -j24` | **EXIT=0** (two exit-143 kills; sweeps below) |
| `ctest --test-dir build -j6` | **4,163 / 4,164**, 49 skipped. Only failure: `visual_gate` |
| `TexturesReproducible` | PASS (3.5 s) |
| `PackReproducible` | PASS (1.1 s) |
| `ParticleAtlasAlpha` (5) | PASS — T400's pins survived the rebuild |
| `TextureAlphaCensus` (14) | PASS |
| `visual_gate` standalone, after EACH family | FAIL on exactly the three `eqe1MappedBytes` lines and nothing else, all three times |
| goldens | untouched (`git status tests/goldens/` empty after every gate run); **no image moved** |
| CPU p95 (final run) | qeynos2 6.750 / gfaydark 8.602 / befallen 6.913 vs ceilings 10.108 / 10.672 / 10.948 |
| `build-gles2` (willeq + gles2_zone_benchmark) | EXIT=0 |
| `build-arm-noble.sh` | EXIT=0 |
| `check_file_sizes.sh fable` | 10 checked, 10 ok, 0 warn, 0 fail (`test_texture_alpha_census.cpp` 459, `generate_textures.cpp` 353) |

**The gate.** Identical three lines in all three runs, growing by the family's
pack delta each time. Final:

| zone | manifest | measured | delta |
|---|---:|---:|---:|
| qeynos2 | 119,944,431 | 137,128,478 | +17,184,047 |
| gfaydark | 118,610,523 | 135,794,570 | +17,184,047 |
| befallen | 113,095,019 | 130,279,066 | +17,184,047 |

That is B240's pending bless (+239 from T398, +19,104 from T400, +17,164,704
from T406). **Not blessed**, per the standing rule.

**Post-143 sweeps.** Two builds of `build/` hit the 600 s ceiling under heavy
contention (main and slot3 were both building). (a) First: `libwilleq_core.a`
present with 324 members, no `st??????` temps, 295 binaries all valid ELF,
recent `.o` files all valid ELF — nothing damaged, resumed. (b) Second: five
test binaries (`test_commands_component`, `test_forced_pose_no_echo`,
`test_gm_component`, `test_position_frame_roundtrip`,
`test_self_death_sequence`) were **non-empty but not ELF** — the T353 shape —
deleted and rebuilt; final state 296 binaries, 0 bad. A third 143 hit
`build-gles2`, where `scripts/build-gles2-x86.sh` rebuilds the entire patched
Irrlicht (~8 minutes) on every invocation and left only ~2 minutes for the
`willeq` link, which was killed twice in a row. Worked around by calling
`cmake --build build-gles2 --target willeq` / `gles2_zone_benchmark`
directly — the script's Irrlicht stage had already produced an intact
`libIrrlicht.a`.

### 7. What surprised me

1. **The cost.** 2.83 MB -> 19.99 MB of shared pack, +17.2 MB mapped per
   zone, and the same multiple on GPU texture memory (an ETC1 256x256 with
   mips is 43,688 bytes; RGBA8888 is 349,528, and 56 of the 59 are 256x256).
   The P13 ledger shows all 32 cloud frames uploaded in qeynos2, so this is
   not theoretical. And every one of the 59 is a CONSTANT-RGB image carrying
   one 8-bit channel — an `A8` payload would do it in ~4.4 MB. Boarded as
   **B287** (class C) rather than invented mid-unit.
2. **`createSoftwareCursor()` survives on a fourteen-line ordering
   accident.** It asks the cache for `"softwareCursor"`, but the pack entry
   is `software_cursor` and `findCompressedEntry()` only lowercases and strips
   the extension. It works because the cursor is loaded at
   `renderer_init.cpp:484` and `willeq.eqe1` is not opened until line 498, so
   the EQE1 branch is skipped and the raw BMP is decoded. Move either line
   and the DRM path gets `LOG_FATAL` + `exit(1)`. Boarded **B285**. It also
   means the cursor's pack override is defensive, not load-bearing, today.
3. **The additive path cannot show alpha at all** (§3). B266 was framed as a
   file-format defect; for one of the 64 it is a blend defect wearing the
   same clothes, and converting the file would have hidden that rather than
   fixed it.
4. **`main`'s tip-verify ctest was collateral.** It finished at 03:07 with
   `visual_gate` and `PackReproducible` red — caused by T406's family-1 pack
   install at ~02:52, not by anything on the tip. Nothing was running when
   each install happened (checked each time), so no run was torn; but a
   *later* run in another worktree reads the new pack against its old pin.
   This is exactly the B228/B230/B240 shared-asset hazard, and it will hold
   until T406 merges.

### 8. Deviations from the plan

- **The plan says "the other 63 textures"; 59 were converted.** Four are
  excluded with reasons, each recorded in `kNeverAlpha` and in §1/§3:
  `spell_particle_fallback` (alpha not sampled — the plan's own "NOT changed
  (say which)" rule), `detail_atlas` (B265), `rain_streaks` and `snow_flakes`
  (no consumer).
- **No STOP fired.** No pixel golden moved after any family, so no contact
  sheet was produced and no family commit is held.
- **The plan's four families were regrouped as three commits.** "Spell
  fallback" became a no-change finding (§3), and "detail/tumbleweed" became
  tumbleweed + creature_atlas, since `detail_atlas` is B265's and the boids
  atlas has the same material as the tumbleweed.
- **B287 is new scope discovered by measurement** and is a class-C question,
  not something this unit decided.
