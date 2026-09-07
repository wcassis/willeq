# T453 — the next TUs over 1,200 (structural)

Class B (structural), slot1, no server. Split, by the T446/T447/T451
recipe (contiguous ranges, shared header, no behaviour change, outputs
byte-identical, goldens/pins unmoved, G05/G06 zero insertions/reorders):
`src/client/graphics/door_manager.cpp` (1,365 — T438 already extracted
`door_manager_animation.cpp`; split the rest by create/mesh/state/
collision — T435's `door_manager_collision.cpp` exists) and every other
TU currently over 1,200 lines (measure at checkout with
`scripts/check_file_sizes.sh --all` or `wc -l`; list them in the
Review; T431 in slot3 may be editing camera TUs — skip any TU in
`src/client/graphics/player/` and say so). Largest resulting TU under
1,000.

Docs/nets: module_map TU table + COVERAGE rows, CLAUDE.md census,
DocClaims/DocCitations green. No board strikes; new items B470+
(T452/T431 allocate from B469+ — say which you used).

## Proof

Build (background), `ctest -j6` (no expected reds), visual gate
standalone + ARM (report host load), file sizes, doc nets.

STOP: an image golden or a memory pin moves; a G05/G06 reorder.

## Review

**Branch** `task/T453-next-splits`, base `a714738c` (fable tip, verified at
checkout). Slot1, `build/` RelWithDebInfo, GLES2 Irrlicht from the slot's own
`third_party/irrlicht-gles2-x86` (cache checked). No server, no eqemu lock
taken — T431 holds it. **No TU under `src/client/graphics/player/` was
touched**, as the plan required; none of the five targets lived there.

Full evidence — the five multiset-diff residue files, the two before/after
stdout pairs and the test-case name sets — is in
`/mnt/projects/fable/review/T453/`, with a README that reproduces the tables
below.

### The band above 1,200 was exactly five TUs, and it is now empty

Measured at checkout over `src/ include/ tests/ tools/ benchmarks/`, matching
the dispatch exactly:

| TU | lines at checkout |
|---|---|
| `src/client/graphics/door_manager.cpp` | 1,365 |
| `tools/hwcaps/gles2_fog_volume_benchmark.cpp` | 1,220 |
| `src/client/graphics/detail/detail_manager.cpp` | 1,219 |
| `tests/test_action_dispatcher.cpp` | 1,217 |
| `tools/test_gl_points.cpp` | 1,206 |

Re-measured on the branch, **nothing is over 1,200**. The largest file
anywhere under those five directories is now
`tests/test_eq_action_handler_cast.cpp` at 1,193, followed by
`src/client/graphics/zone_shader_sources_gles2.h` and
`include/client/graphics/irrlicht_renderer.h` at 1,191.

### The split table

All five follow T446/T447/T451's recipe: bodies are contiguous line ranges of
the original, moved verbatim; the only edits are the ones a TU boundary forces.

| old file | lines | new files | lines | cases |
|---|---|---|---|---|
| `src/client/graphics/door_manager.cpp` | 1,365 | `door_manager.cpp` — ctor/dtor, `setZone`, the two PVS-visibility predicates, `calculateOpenHeading`, `isDoorMeshBuilt`/`getUnbuiltDoors`, `setDoorState`, `getDoorAtScreenPos`/`getNearestDoor`, `hasDoor`/`getDoor`, `clearDoors` | 368 | — |
| | | `door_manager_mesh.cpp` — `findDoorMesh`, `invalidateMeshCache`, `createPlaceholderMesh` (+ `colorFromName`, `createOctagonalPrism`) | 352 | — |
| | | `door_manager_create.cpp` — `createDoor`, `registerDoor`, `buildDoorMesh` (+ `INVISIBLE_OPENTYPES`) | 486 | — |
| | | `door_manager_rebuild.cpp` — `recomputeAllBspRegions`, `rebuildSingleDoor`, `rebuildPlaceholderDoors`, `setAllDoorsVisible`, `getDoorSceneNodes` | 235 | — |
| `tools/hwcaps/gles2_fog_volume_benchmark.cpp` | 1,220 | `gles2_fog_volume_benchmark.cpp` — `main` | 375 | — |
| | | `fog_volume/fog_volume_egl.cpp` — DRM/GBM and X11 EGL bring-up, page flip, teardown, shader compile/link | 296 | — |
| | | `fog_volume/fog_volume_scene.cpp` — FP16 sqrt/exp LUTs, the subdivided alley, the fog billboard quad, the checker texture, the mat4 helpers, the eight fog spheres | 273 | — |
| | | `fog_volume/fog_volume_passes.cpp` — `drawZonePass`, `drawFogPass`, `benchFogOnly`, `benchZonePlusFog`, `benchZoneOnly` | 321 | — |
| | | `fog_volume/fog_volume_bench.h` (seam) | 136 | — |
| `src/client/graphics/detail/detail_manager.cpp` | 1,219 | `detail_manager.cpp` — lifecycle, the texture-cache eviction hook, zone enter/exit and config, `update`, density, per-category enables, the season override, foliage disturbance, footprints, the census accessors, `isExcluded`/`isExcludedByBsp` | 547 | — |
| | | `detail_manager_chunks.cpp` — `updateVisibleChunks`, `loadChunk`, `unloadDistantChunks`, `rebuildAllChunkMeshes` | 169 | — |
| | | `detail_manager_surface.cpp` — `getGroundInfo`, `getTextureFromMeshNode`, `getSurfaceTypeAtPosition`, `classifyTexture` | 554 | — |
| `tests/test_action_dispatcher.cpp` | 1,217 | `tests/action_dispatcher_test_fixture.h` (shared fixture) | 52 | 63 |
| | | `test_action_dispatcher_routing.cpp` — movement, heading, facing, warp, move-to/follow, targeting, auto-attack, cast-from-gem, doors and corpses, chat, group, character state, inventory use, hotbar | 556 | 30 |
| | | `test_action_dispatcher_families.cpp` — spellbook, trade, pets, real spells, tradeskill, extended movement/character-state/combat, entity queries, animation | 323 | 18 |
| | | `test_action_dispatcher_guards.cpp` — `isReady`, the no-handler sweep, the handler-vs-zone split, reply-to-tell, nearest world object, the T148 GM gate | 328 | 15 |
| `tools/test_gl_points.cpp` | 1,206 | `test_gl_points.cpp` — the six-test `main` | 772 | — |
| | | `gl_points/gl_points_egl.cpp` — DRM/GBM and X11 EGL bring-up, teardown, GL error helpers, shader compile/link | 267 | — |
| | | `gl_points/gl_points_fbo.cpp` — the FBO, the pixel readback analyser, the four GLSL ES sources, the pass/fail tally | 187 | — |
| | | `gl_points/gl_points.h` (seam) | 103 | — |

63 = 30 + 18 + 15. **Largest resulting file 772 lines**, against the plan's
1,000 bar. 6,227 ceiling-riding lines became 20 files.
`check_file_sizes.sh --all`: **1,196 files, 0 warn, 0 fail** (1,181 at T451 —
+15 is exactly this unit's net new files).

**No public header moved**, so no include site outside the five families
changed: `include/client/graphics/door_manager.h`,
`include/client/graphics/detail/detail_manager.h` and the action-dispatcher
production headers are untouched. The two new seam headers live beside the
tools that use them, T447's convention;
`tools/hwcaps/fog_volume/fog_volume_bench.h` sits next to T385's
`fog_volume_shaders.h`, created for the same reason.

**`main()` was left whole in both tools, deliberately.** `test_gl_points`'s is
728 lines of six test blocks that share locals across the block boundaries;
cutting it would have been a rewrite, not a move. Extracting everything around
it lands the residual at 772, comfortably inside the bar.

### What the boundaries forced, and nothing else

A sorted line-multiset diff of each original at `a714738c` against the union of
its replacements. Residue files in
`review/T453/*_lines_only_in_original.txt`.

- **`door_manager.cpp`: ZERO lines lost.** Every line survives, `static`
  keywords included — nothing gained external linkage. `colorFromName` and
  `createOctagonalPrism` went with their only caller `createPlaceholderMesh`,
  and `INVISIBLE_OPENTYPES` with its only two readers `createDoor` and
  `registerDoor`; each caller set was checked by grep before the cut.
- **`detail_manager.cpp`: ZERO lines lost.** The TU had no file-statics at all.
- **`test_action_dispatcher.cpp`: two lines** — `namespace {` and
  `} // namespace`. The fixture had to move into a NAMED namespace
  (`action_dispatcher_test`), T446's finding: GTest requires every case in a
  suite to share one fixture TYPE, and an anonymous-namespace fixture in a
  header gives each TU its own type and aborts at startup. The three
  using-directives stayed at global scope in the header, so every moved body
  resolves exactly the names it resolved before, and the fixture body itself is
  byte-verbatim.
- **`gles2_fog_volume_benchmark.cpp`: 28 lines** — 25 `static` signature lines
  (each is now a declaration in the seam header plus an external-linkage
  definition) and 3 default-argument lines, which may be stated once and are
  now stated on the declarations of `drawFogPass`, `benchFogOnly` and
  `benchZonePlusFog`. `floatToHalf` and `buildSubdividedQuad` stayed
  file-static: their callers are all inside `fog_volume_scene.cpp`.
- **`tools/test_gl_points.cpp`: 19 lines** — 18 `static` signature lines and
  the one default argument `int tolerance = 20`. `glErrorString` stayed
  file-static: its only caller is `checkGLError`, in the same TU.

Five structs (`EGLState`, `Vertex`, `FogVertex`, `FogSphere`, `BenchResult`)
and four (`EGLState`, `FBO`, `PixelStats`, `TestResult`) moved to the two seam
headers, because a type used on both sides of a TU boundary may be defined
once. Their definitions moved verbatim.

### cmake changes

No target was added, removed, renamed or relinked; no test registration
changed; no `GTEST_DISCOVER_TESTS` call moved.

- `CMakeLists.txt` — `willeq_core` gained the three new door TUs and the two
  new detail TUs; `gles2_fog_volume_benchmark` gained its three; `test_gl_points`
  gained its two and `${CMAKE_SOURCE_DIR}/tools` on its include path, so its
  seam header resolves as `"gl_points/gl_points.h"` (T447's convention). The
  fog benchmark already carried `${CMAKE_SOURCE_DIR}/tools/hwcaps`.
- `tests/cmake/zone_integration.cmake` — the door-manager source list, which
  `test_zone_*` binaries compile directly, gained the same three.
- `tests/cmake/commands.cmake` — `test_action_dispatcher` takes three sources
  instead of one. Still ONE target: a ctest test name is `Suite.Case`, so three
  binaries all carrying suite `ActionDispatcherTest` would register the same 63
  names three times.

### The behaviour proof

| target | comparison | result |
|---|---|---|
| door + detail managers | `scripts/ci/visual_gate.sh` standalone (3 zones + 3 memory-coverage passes) | **=== VISUAL GATE: PASS ===**; no golden pixel and no memory pin moved |
| door + detail managers | `TexturesReproducible`, `PackReproducible`, `DbReproducible` | **PASS**; no `.sha256` and no `.eqe1` file is in the diff |
| `test_action_dispatcher` | the 63 `Suite.Case` names extracted from the deleted file at `a714738c`, sorted, vs `--gtest_list_tests` on the split binary | **63 = 63, NAME SETS IDENTICAL** |
| `test_gl_points` | full stdout, base binary vs split binary, under Xvfb | **BYTE-IDENTICAL**, 100 lines, both exit 0 |
| `gles2_fog_volume_benchmark` | full stdout, base binary vs split binary, `--frames 20 --res 320 240`, under Xvfb | **IDENTICAL with every run of digits masked**, 204 lines, both exit 0 |

The two "before" binaries are real binaries, not an argument: `git archive
a714738c` into a scratch tree, configured against this slot's Irrlicht and
built `--target gles2_fog_volume_benchmark test_gl_points`.

**Flagged, and it is a property of the tool rather than of the split: the fog
benchmark cannot have a byte-identical stdout proof.** Every number it prints
is a measured millisecond, an FPS derived from one, or a percentage of a frame
budget, so two runs of the SAME binary do not match either. Masking every run
of digits leaves 204 lines of labels, section headers, table structure and
prose, and those compare exactly — which is the strongest claim available and
covers the whole control flow (which variants ran, in which order, with which
sphere counts and radii, and which summary rows were emitted). The timings also
sit in the same band on both sides (8 spheres, r=6: 5.61 ms base vs 5.92 ms
split, at host load 20-40). `test_gl_points` prints no timings, so it was held
to the strict bar and met it byte for byte.

### Verification

Host load average was **16-48** throughout (three slots working); every number
below carries that.

| step | result |
|---|---|
| `cmake --build <slot>/build -j24` (background, exit file) | **exit 0**, ~31 min, zero `error:` lines. No exit 143, so no truncation scan owed |
| `ctest --test-dir <slot>/build -j6 --output-on-failure` | **100% tests passed, 0 failed out of 4672**, 321.9 s. No reds at all — not even the T360 `HeadlessShutdownTest` load artefact |
| `ctest -L hygiene` after the doc edits | **9/9 PASS** (`FileSizeCeiling`, `DocCitations`, `DocClaims`, `DocCounts`, `CommentClaims`, `TexturesReproducible`, `PackReproducible`, `DbReproducible`, `WriteOnlyFields`) |
| `scripts/build-gles2-x86.sh` | **exit 0** |
| `scripts/ci/visual_gate.sh` (standalone) | **PASS** |
| `scripts/build-arm-noble.sh` | **exit 0** |
| `check_file_sizes.sh fable` | 20 checked, **20 ok, 0 warn, 0 fail** |
| `check_file_sizes.sh --all` | **1,196 files, 0 warn, 0 fail** |
| goldens | untouched — no golden, `.sha256` or `.eqe1` file is in the diff |
| G05/G06 | untouched — neither `renderer_frame_render.cpp` nor `renderer_player_physics` is in the diff; zero insertions, zero reorders |
| memory pins | untouched — the gate's three memory-coverage passes are inside its PASS |

`DocCitations` went red once, on this Review's own doc edits, and was fixed in
place: the new `CLAUDE.md` census sentence named `tests/test_action_dispatcher.cpp`
(which this unit deletes), and two `docs/module_map.md` tool rows named the new
sources by a path relative to `tools/` rather than repo-relative. The same
class T451 hit. Green on re-run, and green in the full `-L hygiene` pass above.

### Docs and nets

- **`docs/module_map.md`** — nine new rows in the renderer TU table for the
  door and detail families; the `gles2_fog_volume_benchmark` row in the tools
  table now names all four TUs and both seam headers; a new `test_gl_points`
  row; and the "Test build" section now names the five ONE-target-several-sources
  test targets, this unit's included, with its per-TU case counts.
- **`CLAUDE.md`** — the M00 census paragraph re-measured (1,196 files) and
  given a sentence recording that the band above 1,200 is now empty and what
  the largest remaining file is.
- **`include/client/graphics/renderer_zone_merge.h`** — a T124 comment cited
  "door_manager.cpp: setDoorState()/update()/rebuildSingleDoor()". It was
  already half-stale (T438 moved `update()`); it now names the TU each of the
  three bodies actually lives in.
- **One net reads a split TU as TEXT, and it did NOT go vacuous — checked, not
  assumed.** `tests/test_data_path_routing.cpp`'s
  `DataPathRouting.TextureLoadsAreRouted` scans
  `src/client/graphics/detail/detail_manager.cpp` for a bare `"data/…"` /
  `"config/…"` literal that does not go through `ResolveDataPath()`. Had the
  split moved that TU's two data-path sites into `detail_manager_surface.cpp`,
  the test would still have PASSED while asserting over nothing. Both sites are
  in the residual `detail_manager.cpp` (2 hits there, 0 in each new TU), so the
  assertion still bites. This is the class T451 flagged for
  `test_movement_component.cpp`.
- No other script or test reads any of the five basenames as TEXT, so nothing
  in the "do not rename" family was disturbed. Checked by grep over
  `src/ include/ tests/ tools/ benchmarks/ scripts/` for each basename.

### Board

**No board strikes and no new board items.** Nothing was discovered that
outlives the unit: every deviation is recorded above (the fog benchmark's
digit-masked proof, and `main()` left whole in both tools), and neither is an
open question. No item from the B470+ allocation was used.

### What surprised me

The full suite came back **completely green — 4672/4672**. Both of the load
artefacts the protocol warns about (`visual_gate` CPU-p95 and
`HeadlessShutdownTest` under `-j6`) stayed quiet even at load 16-48, so nothing
had to be re-run standalone to be believed.
