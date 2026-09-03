# L02 — `Eqe1EffectsComponent` (family: `renderer_effects.cpp` — EQE1 archive resolution + fire-glow icospheres)

Batch: `refactor/L_renderer_component_extraction.md` (§L01 corrections are
the template). Wave 1, three-wide with L03/L04. Measured at `cfbbde57`.

The family name "effects" is misleading: 11 of the 14 bodies are EQE1
archive plumbing (`getOrOpenEQE1`, `getOrOpenEQE1FromDir`,
`resolveEQE1Archives`, `openArchiveContaining`, `openCompositeSheetEQE1`,
`isPersistentEQE1Archive`, `loadEQE1ArchiveConfig`, `logEQE1ArchiveUsage`,
`getEQE1MappedBytesTotal`, `addSpawnedRaceEQE1Candidate`) and 3 are the
fire-glow icosphere (`buildIcosphereMesh`, `compileIcosphereShader`,
`renderFireGlowIcospheres`, `updateFireGlowLighting`). Name the component
for what it is: `Eqe1ArchiveComponent`, and put the icosphere trio in the
same TU under its own banner (they share the `icoLoc*` uniforms, all 9
single-family). 697 lines; single-family members: the 9 `icoLoc*` uniform
locations (measure; the batch survey says 9).

Callers (word grep; the compiler confirms): `renderer_zone_load.cpp` (6
methods), `renderer_zone_stream.cpp`, `renderer_zone_load_env.cpp` (2),
`renderer_init.cpp` (2), `renderer_zone_lifecycle.cpp`, `renderer_diagnostics.cpp`,
`renderer_frame_render.cpp` (2), `constrained_texture_cache.h`
(`getOrOpenEQE1` — read it: a callback type or a comment). All in-renderer;
retarget as `eqe1_->X()` inside `IrrlichtRenderer` bodies and
`ctx_.Eqe1().X()` inside other components (none yet besides rdp).
`getOrOpenEQE1FromDir` has no external caller — in-family or dead (§5 with
the in-family rule; check for `pushEvent` first, K12 ff).

Text readers: `tests/cmake/commands.cmake` defines
`RENDERER_EFFECTS_SOURCE=…/renderer_effects.cpp` — repoint to the new path;
`tests/test_eqe1_archive_config.cpp` names `renderer_effects.cpp` in a
message and a comment and reads the built-in fallback list — read it; if
it parses the TU as text, the repoint is territory (K03 c).

Predicted header delta: −14 declarations −9 members −banners +(4..6
scaffolding at L01 cost) ≈ **−15 or better**.

Territory: `include/client/graphics/eqe1/eqe1_archive_component.h` (new),
`src/client/graphics/eqe1/eqe1_archive_component.cpp` (git mv of
`renderer_effects.cpp`), `irrlicht_renderer.h`, `renderer_init.cpp` (ctor
construction line only), root `CMakeLists.txt`, `tests/cmake/commands.cmake`
(the one define), `tests/test_eqe1_archive_config.cpp` (repoint only),
`tests/test_eqe1_archive_component.cpp` (new) + cmake, allowlist,
`docs/module_map.md`, CALL-SITE-ONLY edits in the renderer TUs named. L03
and L04 edit `irrlicht_renderer.h`, `renderer_init.cpp`, `renderer_frame_render.cpp`,
`renderer_frame_input.cpp` at their own lines. Files under
`src/client/graphics` change: visual gate + ARM are NOT skippable.
