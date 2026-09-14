# T484 — fable's sky is a tan/orange texture with pale white trees where the official client's is blue (B726)

Class A (visible, match the oracle), **Leg A offline (no lock)**, slot1. Found
by T478 (read its Review `refactor/T478_oracle_regrab_chase_and_held_items.md`
§4 and the stills under `review/T478/`, which show both clients at the same
qeynos2 spot at 11:00 game time, weather 0). Prior sky/lighting work to read
first: T428 (night base, vision floors), B445/B446 (ambient uploaded twice,
chromaticity squared), B231/T418 (rain darkening), B360 (fog model) —
grep `refactor/BOARD.md` and `refactor/completed/` for "sky".

## Tasks

1. **Describe the difference from the frames, precisely.** Crop the sky region
   of both clients' T478 stills, and report the mean and per-row RGB. State
   whether the whole dome is wrong, or only a layer (clouds, base colour).
   Report the same for the "pale white trees": is that tree texture colour, or
   the sky tint bleeding into foliage through fog, ambient or the alpha
   companion?
2. **Trace fable's sky for qeynos2.** Cover:
   - which sky asset (`sky.s3d` layer / dome index for the zone,
     `zoneinfo`/`sky` type from the server's zone data);
   - which textures bind, and whether they come through EQE1 with their ETC1
     `_alpha` companion;
   - the time-of-day tint;
   - fog colour;
   - the shader path.

   Compare with what the official client's zone data implies. The eqemu `zone`
   table's sky column and the sky type fable receives are in the session logs
   under `review/T478/`.
3. **Leading suspects, confirm or refute each.**
   - (a) The wrong sky type/layer index: an off-by-one or a mis-mapped sky id.
   - (b) An ETC1 alpha-companion or channel-order issue on the sky textures:
     BGR/RGB swap, or blue lost through the T411 companion path.
   - (c) The B445 double-ambient upload squaring chromaticity on sky and foliage.
   - (d) The time-of-day tint table applied at the wrong hour.

   Check each with a headless render or a unit seam and real assets. Do not
   eyeball.
4. Fix the root cause with a test that fails first. Capture before/after x86
   stills at the T478 pose, if possible through an offline path (the gate's
   `gles2_zone_benchmark` renders qeynos2). Put them in `review/T484/` with a
   README; never self-assess. If the fix needs a live A/B against the oracle,
   board it as Leg B (lock).

## Proof

Standard verification bar, builds strictly in sequence. Update the G-batch
goldens for touched TUs in place per G-correction 32, and say so. **Visual gate:
the gate zones render sky, so an image golden WILL likely move if the sky
changes. STOP and report with before/after, because Will blesses; never
self-bless.** ARM; file sizes. Board B780–B789; strike or narrow B726.

File territory: sky / environment rendering (`src/client/graphics/environment/`
sky and time-of-day, the sky shader uniforms), sky asset loading, related
tests, `review/T484/`. **Not** spell code / the gem panel (T483, slot3), and not
the object/character loaders or the T479 branch (slot2).

STOP: an image golden moves (expected if fixed — report before/after for Will's
bless); the cause is B445's ambient double-upload (report it; that re-solve is
a known lock-queue item with measured baselines, not a drive-by).

## Review

Slot1, `task/T484-sky-colour`, base `fable` `18bb8e55`. Offline (no lock, no Pi). Media: `review/T484/README.md`.
**Root cause found and fixed: the sky dome's time-of-day VERTEX TINT reached SOLID3D_FS with red and blue
exchanged.** It is not any of the four planned suspects, and not B445. No image golden moved (the gate renders no
sky; see §5).

### 1. The difference, from the frames

T478 sheet `T478_chase_h160_oracle_vs_fable_default_vs_fable_minimum.png` (tiles 512 px wide, so root coordinates /2):

| crop | official | fable |
|---|---|---|
| sky, mean | (93.1, 135.1, 172.4) | (126.8, 117.8, 104.1) |
| sky, per row (y 22..112, step 10) | flat (87..109, 132..143, 171..176) | flat (113..138, 112..123, 101..107) |
| tree canopy | (47.5, 62.6, 53.6) | (138.5, 152.7, 160.1) |

- **The whole dome is wrong, uniformly.** Every row is shifted the same way, and the cloud structure is intact
  (visible as blue gaps in the tan). So this is a colour transform of a correctly mapped texture, not a wrong layer.
- **The number that named it.** `normalsky.bmp` (decoded from `sky.s3d`) averages (147,172,190). At 11:00 the code's
  tint is (140,175,220) = (0.549,0.686,0.863).
  - Texture × tint with red and blue EXCHANGED, (0.863,0.686,0.549), gives (127,118,104). fable measured (127,118,104).
  - Texture × tint in the right order gives (81,118,164).
  - The texture itself swapped, times the tint, gives (104,118,127). That does not match, so the texture is not the
    swapped part.
- **The trees are not the sky.**
  - Fog starts at 250 units (T478 `run.log`: `Fog: start=250, end=300`) and these trees are much nearer.
  - The dome draws behind with depth write off, so it cannot bleed into foliage.
  - Their colour (138,153,160) is not the tan sky's.
  - Boarded as **B782**, not investigated (tree/object rendering is outside this territory).

### 2. The sky path for qeynos2 (citations)

- **Sky type.** `ZoneLoadComponent::loadZoneStep10_Sky` (`zone_load/renderer_zone_load_env.cpp`) loads `sky.s3d` and
  `sky.ini`. `SkyRenderer::prepareSkyType` (`sky_renderer.cpp`) maps the zone's weather through
  `SkyConfig::getSkyTypeIdForWeather`: qeynos2 → **0**, meaning "use the server's id". The server sent **1**
  (T478 `run.log:715`, "Prepared sky type 1 (weather: DefaultClear, category: 0)"). Category Normal.
- **Layer and texture.** `SkyLoader::buildSkyTypes` (`eq/sky_loader.cpp`): types 0 and 1 → background layer **11**,
  cloud layer 13. `wld_dump` of `sky.wld`: `LAYER11_DMSPRITEDEF` uses material `NORMALSKY`. The texture is
  `normalsky.bmp`, 128x128.
- **EQE1.** `sky.eqe1` carries `normalsky` 128x128, 8 mips, ETC1, **no `_alpha` companion** (`verify_eqe1 --verbose`).
  It uploads through `ConstrainedTextureCache::getOrLoad` / `uploadTexture`, and
  `COpenGLES2Driver::bindAlphaCompanionUnit` binds the opaque-white fallback on unit 1.
- **Dome.** `SkyDomeMode::Original` → `createSkyDomeFromWldGeometry` (run.log "Created sky dome from WLD geometry: 145
  vertices, 264 triangles"). Material `EMT_TRANSPARENT_VERTEX_ALPHA`, fog off, ZWRITE off.
- **Shader path.** A textured built-in material → `EOGLES2SP_SOLID3D` (`COpenGLES2Driver.cpp`, built-in shader
  selection). The driver uploads ambient = tint = 1 for built-ins, so `vColor = aColor`, and SOLID3D_FS computes
  `texColor * vColor` (`COGLES2Shaders.cpp`).
- **Time-of-day tint.** `SkyRenderer::calculateSkyColors` (`sky_renderer_colors.cpp`) at 11:00 returns `DAY`: zenith
  (100,150,220), horizon (180,200,220), brightness 1.0. `updateSkyLayerColors` averages them to (140,175,220) and
  **writes the tint into every dome vertex's `Color`**, keeping the per-vertex fade alpha.
- **The defect.** `drawVertexPrimitiveList` and the `drawMeshBuffer` VBO path in `COpenGLES2Driver.cpp` both call
  `glVertexAttribPointer(EOGLES2VA_COLOR, 4, GL_UNSIGNED_BYTE, GL_TRUE, stride, base+24)` on the raw `S3DVertex`
  bytes. `SColor` is a u32 0xAARRGGBB, stored B,G,R,A on little-endian, so `aColor.r` is the tint's BLUE.
- **Fog colour.** `OverlaysComponent::setupFog` (`overlays/overlays_hud_env.cpp`) uses
  `SkyRenderer::getRecommendedFogColor` = `DAY.fog` (160,180,200) at 11:00. It goes through uniforms with correct
  order and does not touch the dome, which has fog disabled.
- **Official client side.** Its sky is a blue cloud texture consistent with `normalsky` (the same layer).
  - fable's zone table value is not in T478's logs.
  - `data/oracle_eqclient_ini_before.ini` has `Sky=2`, the client's sky-detail option, not a sky type.

### 3. Suspects

All measured headless on the real assets (`tests/test_sky_dome_tint_channel_order.cpp`), not argued.

| suspect | verdict | evidence |
|---|---|---|
| (a) wrong sky type / layer index | **refuted** | `sky.ini` gives 0 and the server gives 1. Both select layer 11 = `NORMALSKY` (the test asserts the mapping and the texture name). The dome's texture structure is intact in both T478's frame and the headless frame. |
| (b) ETC1 companion / channel order | **refuted** | `normalsky` through `sky.eqe1` and the real ETC1 upload, drawn with a white vertex colour, reads (147.9,172.1,189.9) against the BMP's (147,172,190). No companion exists for this entry. |
| (c) B445 double ambient | **refuted** | The dome draws through built-in SOLID3D, not a zone shader. The driver sets the built-in ambient and tint uniforms to 1, and `updateTimeOfDay`'s zone-shader uploads never reach it. The before/after change is achieved with no ambient term touched. |
| (d) tint table at the wrong hour | **refuted as the cause** | 11:00 → `DAY` exactly. With the channel order fixed, the dome/reference ratio is (0.542,0.682,0.861), the table's own (0.549,0.686,0.863). The table is invented rather than oracle-fitted, which is a separate question: **B781**. |
| **(e) vertex-colour byte order** (found) | **confirmed** | Headless dome at 11:00 before the fix: **(126.0,117.3,104.0)**, ratios (0.852,0.682,0.548), against T478 live (127,118,104). A pure-red vertex colour reads (0,0,190). |

### 4. The fix

- **Change.** `include/client/graphics/gles2_vertex_color.h` (new, 38 lines) adds `toGles2VertexColor()`: an `SColor`
  whose stored bytes are R,G,B,A. `SkyRenderer::updateSkyLayerColors` writes the dome tint through it and keeps each
  vertex's alpha.
- **Not the driver, on purpose.** The zone shaders read `aColor.rgb` as baked light through the same upload, and the
  lighting constants T405/T428 solved live were solved with that byte order in place. Fixing the driver would re-open
  them silently. Boarded as a decision, **B780**. The test pins the driver convention so that a driver fix trips it.

**Red before, green after** (`build/bin/test_sky_dome_tint_channel_order`):

| run | Driver... | Etc... | DaylightDome... |
|---|---|---|---|
| first run, unfixed (`review/T484/before/test_output_before_fix.txt`) | OK | FAILED on a wrong expectation (asserted the `sky.ini` type was 1; pixel asserts passed) | **FAILED**, (0.852,0.682,0.548) |
| corrected test, fix reverted by `git apply -R` (`before/test_output_corrected_test_on_reverted_fix.txt`) | OK | OK | **FAILED**, (0.852,0.682,0.548) |
| corrected test, fix applied (`after/test_output_after_fix.txt`) | OK | OK | **OK**, (0.542,0.682,0.861) |

**Stills.** `review/T484/T484_sky_dome_headless_before_after_320x240.png` shows the zenith and horizon views, before
and after, from the test harness (`T484_DUMP_DIR`).
- They are not at T478's pose: no offline path draws the sky. `gles2_zone_benchmark` (the gate) constructs no
  `SkyRenderer`; a grep of `tools/` and `benchmarks/` finds none.
- The live frame at the pose is **B783** (Leg B, lock).
- Not self-assessed.

### 5. Verification

Strictly in sequence. Every step was run in the background with a unit-private exit file
(`scratchpad/T484/exit/*.exit`), each deleted before launch.

| step | result |
|---|---|
| 1. `cmake --build build -j24` (RelWithDebInfo, slot's own Irrlicht) | **PASS**, exit 0 |
| 2. `ctest --test-dir build -j6` | **4,806 / 4,807 → PASS after one fix.** The red was `ZeroCallers`, caused by this unit: the new test called `SkyRenderer::getCurrentSkyType()`, an EXEMPT zero-caller, which made it tests-only. The assertion was dropped (the pixels prove the type). The re-run of `ZeroCallers` plus the three `SkyDomeTintChannelOrderTest` cases passed 4/4. `visual_gate` inside the suite passed, but ran against the pre-rebuild `build-gles2`, so step 4 is the gate of record. |
| 3. `scripts/build-gles2-x86.sh` | **PASS**, exit 0 |
| 4. `scripts/ci/visual_gate.sh` (standalone, fresh `build-gles2`) | **PASS** (`=== VISUAL GATE: PASS ===`), all three zones plus memory coverage. **Goldens untouched** (`git status`: nothing under `tests/goldens`). As predicted in §4, the gate renders no sky. |
| 5. `scripts/build-arm-noble.sh` | **PASS**, exit 0 |
| 6. `check_file_sizes.sh fable` + `--files` for the new files | **PASS**. `sky_renderer_colors.cpp` 644, test 293, header 38, `ui_rendering.cmake` 657; 0 warn, 0 fail. |

**G-batch goldens.** The branch touches `sky_renderer_colors.cpp`, a test, a CMake file and a new header. None is a TU
pinned by a G-batch order/coverage golden (`zone_dispatch`, `bridge_component_intents`,
`command_processor_registration`, `application`, `renderer_frame_render`, `renderer_player_physics`), so there was
nothing to update in place.

### 6. Board

- **B726** struck: the sky half is fixed here; the tree half moved to B782.
- **New:**
  - **B780** (C): driver vertex-colour byte order, and the zone baked-light consequence.
  - **B781** (A, unconfirmed): tint table not fitted to the oracle.
  - **B782** (A, unconfirmed): pale distant trees.
  - **B783** (P): Leg B live frame at T478's pose.
- B784–B789 unused.

### 7. Deviations

- **The cause was none of the four planned suspects.** A fifth, found by pixel arithmetic, is confirmed; all four
  planned ones are refuted with measurements.
- **The fix compensates at the consumer, not in the driver** (B780 explains why). This leaves a known-wrong driver
  convention in place for Will's decision.
- **The first red run carried a wrong test expectation** (sky type 1 vs 0). It was corrected and re-proven red
  against the reverted fix; both runs' output is kept.
- **The before/after stills are headless harness renders, not frames at T478's pose** (no offline path draws the sky).
- **The full suite caught this unit's own hygiene red (`ZeroCallers`).** It was fixed by dropping a test assertion,
  not by editing the exemption table (§5).
- **`cmake --build --target` on the Unix Makefiles tree did not regenerate** for the new target; it needed a plain
  `cmake -S -B` with the existing cache first. No option changed.
