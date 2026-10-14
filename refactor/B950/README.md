# B950 (GitLab #268): make `EQT_HAS_TEXTURE_ATLAS` PUBLIC on `willeq_core`

**Fixed by option (a)** — the define becomes PUBLIC on `willeq_core`, and every
per-target PRIVATE copy in TESTS is deleted as now-redundant. Not option (b)
(pimpl): the atlas members are two `irr::s32` shader-type ids read/written
inline by `setAtlasShaderMaterialTypes()` at two renderer call sites, and a
pimpl would add a heap indirection to the zone build for no memory or
behaviour win; a two-line visibility change is the root fix.

## Why (a) is safe: the layout is controlled by the DEFINE, not by the atlas TUs

The two `irr::s32` atlas members in `zone_geometry.h` are guarded by
`EQT_HAS_TEXTURE_ATLAS`, but the atlas TUs — `zone_geometry_atlased.cpp`
(CMakeLists.txt:596) and `texture_atlas.cpp` (:749) — are gated separately,
inside `IF(EQT_GRAPHICS)` (the `GRAPHICS_SOURCES` at :576-577). The
`PUBLIC` define at :965 is OUTSIDE that gate. So:

- `EQT_GRAPHICS=ON` (the default; mandatory whenever a patched Irrlicht is
  found — the configure gate at CMakeLists.txt:134-152): `willeq_core`
  compiles `zone_geometry.cpp` (:596) and both atlas TUs, the macro is
  PUBLIC, every link consumer sees it.
- `EQT_GRAPHICS=OFF` (the CI hygiene job, `.gitlab-ci.yml:143`, builds it
  that way): `willeq_core` exists WITHOUT the atlas TUs but WITH the define,
  so its `ZoneMeshBuilder` keeps the two-member layout — and nothing in the
  core calls the atlas API there (the only call sites,
  `renderer_zone_draw.cpp` / `renderer_zone_load_objects.cpp`, are graphics
  TUs). The MR !129 description's "no configuration builds `willeq_core`
  without the atlas" was wrong in that one respect (note posted on the MR);
  the safety argument holds on the define, which is what (a) changes.

- Every TEST that links `willeq_core` now receives the define through the
  link. The per-test PRIVATE copies (`test_texture_eviction_residency`,
  `test_texture_eviction_character_restore`, `test_texture_lru_draw_order` in
  `tests/cmake/renderer_guards.cmake`) and their explaining comments are
  deleted as now-redundant.
- `test_placed_object_meshes.cpp` (tests/cmake/world_geometry.cmake) — the
  consumer the issue names, which constructed a `ZoneMeshBuilder` without the
  define — now gets it through the link. No change needed at its CMake site.
- The three gles2 tools (`gles2_zone_benchmark`, `gles2_shader_diagnostic`,
  `gles2_composite_audit`, CMakeLists.txt:2330/:2507/:2632) **keep**
  their PRIVATE copy: they do NOT link `willeq_core` (they link
  `${ZONE_BENCH_LIBS}`, CMakeLists.txt:2354) and compile
  `zone_geometry.cpp` / `zone_geometry_atlased.cpp` / `texture_atlas.cpp`
  themselves — for them the PRIVATE define is still the only one, and the
  acceptance criterion deletes only copies that the PUBLIC define makes
  redundant.
- `test_graphics`, `test_asset_loading`, `test_door_loading` (standalone-TU
  tests that compile `zone_geometry*` themselves, link no `willeq_core`) and
  the standalone tools `model_viewer` / `s3d_dump` / `entity_composite_builder`
  / `generate_surface_map` / `zone_line_editor` are unaffected: they never
  linked `willeq_core`, so the PUBLIC define cannot reach them, and none of
  them calls the atlas API (the atlas methods are only called from
  `renderer_zone_draw.cpp:1094` and
  `renderer_zone_load_objects.cpp:150`, both in `willeq_core`;
  `tools/zone_atlas_builder.cpp` mentions the name in a comment only).

## RED (pre-fix, on the unmodified head)

The pre-B1019 net shape (two probe TUs in the same target: CONSUMER with no
explicit define, CORE with `EQT_HAS_TEXTURE_ATLAS` per source) was what the
original RED was captured with:

````
[ RUN      ] ZoneGeometryAtlasLayout.ConsumerAndCoreSeeTheSameLayout
.../tests/test_zone_geometry_atlas_layout.cpp:51: Failure
Expected equality of these values:
  asPlainConsumer
    Which is: 272
  asCore
    Which is: 280
ZoneMeshBuilder's layout depends on EQT_HAS_TEXTURE_ATLAS and the two probe TUs
disagree (plain consumer=272 core=280): a TU that links willeq_core without the
define constructs an object smaller than the core's constructor writes (B944's
ASan stack-buffer-overflow). The define must be PUBLIC on willeq_core so every
consumer of the header sees the same class.

.../tests/test_zone_geometry_atlas_layout.cpp:63: Failure
Value of: consumerSawMacro
  Actual: false
Expected: true
a bare consumer of zone_geometry.h still does NOT see EQT_HAS_TEXTURE_ATLAS: the
define is not PUBLIC on willeq_core (pre-B950 shape). ...

[  FAILED  ] ZoneGeometryAtlasLayout.ConsumerAndCoreSeeTheSameLayout (0 ms)
  1 FAILED TEST
````

2 × `irr::s32` members = 8 bytes total [measured: 280-272].

B1019 (#340) tightened the net to measure the CORE side INSIDE `willeq_core`
(`ZoneMeshBuilder::layoutSize()`, zone_geometry.cpp) instead of a sibling
probe TU with a hand-placed define, and re-captured the same RED with the
tightened test:

````
[ RUN      ] ZoneGeometryAtlasLayout.ConsumerAndCoreSeeTheSameLayout
.../tests/test_zone_geometry_atlas_layout.cpp:52: Failure
Expected equality of these values:
  asConsumer
    Which is: 272
  asCore
    Which is: 280
...
.../tests/test_zone_geometry_atlas_layout.cpp:64: Failure
Value of: thisTuSawTheAtlasMacro
  Actual: false
Expected: true
a bare consumer of zone_geometry.h still does NOT see EQT_HAS_TEXTURE_ATLAS:
the define is not PUBLIC on willeq_core (pre-B950 shape). ...
[  FAILED  ] ZoneGeometryAtlasLayout.ConsumerAndCoreSeeTheSameLayout (0 ms)
````
(refactor/B1019/README.md carries the full run; this block is the B950-era
capture, re-pasted at the current line numbers of that era's test.)

## GREEN (post-fix)

Reconfigured and rebuilt after the one-word CMake change. B1019 re-ran the
TIGHTENED test (consumer's own `sizeof` vs `willeq_core`'s
`ZoneMeshBuilder::layoutSize()`): `test_zone_geometry_atlas_layout` 1/1
(both sides report 280; `thisTuSawTheAtlasMacro` true — the PUBLIC define
flows into the consumer through the `willeq_core` link),
`test_placed_object_meshes` 7/7, `test_texture_eviction_residency` 4/4,
`test_texture_lru_draw_order` 4/4, `test_texture_eviction_character_restore`
6/6 — all under the CI configuration (Release, GLES2,
`EQT_GLES2_IRRLICHT_DIR` pointed at the local patched Irrlicht). The full
suite + `visual_gate` run on the CI pipeline for this branch (the gate is
the no-behaviour-change check for the acceptance criterion).

## Not verified / left open

- The ASan reproduction (B944's original `stack-buffer-overflow` in
  `ZoneMeshBuilder::ZoneMeshBuilder`) was NOT re-run under ASan here: an ASan
  build of `willeq_core` is not worth the 4 GiB budget for this unit, and the
  layout-equality test is the direct net for the same violation (unequal
  `sizeof` across a constructor boundary is exactly what ASan reported). The
  fix removes the size disagreement by construction.
- Gate frames: not run in this session — the CI pipeline for this branch runs
  `visual_gate` (all five manifest zones), which is the no-behaviour-change
  check for the acceptance criterion. The change touches no pixel path
  (compile-definition visibility only); CI's gate is the verification.
- Pi (ARM) byte sizes are not measured; the test asserts equality, not a
  value, so it holds on any ABI.
- Local `willeq` link verification required a rebuild of the shared
  gitignored `third_party/irrlicht-gles2-x86` (the copy in /home/user/src/willeq
  predated fable's `gles2GetDrawCensusTotals`; CI does exactly this
  rebuild via its input-stamp check — `.gitlab-ci.yml` build stage). No repo
  file outside this unit's scope changed.
