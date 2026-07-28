# Desktop-GL elimination plan

Survey + phased migration plan for the standing decision
(`docs/decisions.md` 2026-07-28): GLES2 (`COpenGLES2Driver`) is the sole 3D
renderer going forward; the desktop-GL path (system `libirrlicht`, X11/GLX,
`EDT_OPENGL`/`EDT_BURNINGSVIDEO`) is to be eliminated, not maintained.

Produced by P24 (`refactor/P24_desktop_gl_survey.md`). x86 only. Every claim
below is backed by an actual configure/build/ldd/nm observation on this host,
cited inline — not by reading `CMakeLists.txt` text alone.

Sibling unit **P25** (slot2) is building an X11-EGL window backend in
`docker/irrlicht-drm-x86/` for interactive dev use once desktop GL is gone;
this doc flags where phases must wait on it landing.

**Phase 0 — EXECUTED (P26, `refactor/P26_phase0_gles2_links.md`).** The 6
near-zero-reference Class-B tools were RETIRED (not ported) per Will's
2026-07-28 decision; the remaining 8 Class-B targets (`model_viewer`,
`entity_composite_builder`, `generate_surface_map`, `zone_line_editor`,
`s3d_dump`, `test_asset_loading`, `test_door_loading`, `test_graphics`) got
the explicit `EGL GLESv2 drm gbm` (+ `gpu_upload_thread.cpp` where the
target touches `constrained_texture_cache`) link fix, gated the same way as
`willeq`'s `LINK_LIBS`. Both trees now build 100% of their (reduced) target
sets; `ctest` registers the identical 1235 test cases in both trees
(0 net delta), with the sole remaining GLES2-tree failure being the
documented Phase 1 blocker below (confirmed still the only genuine
failure — not investigated, per this unit's rules). See the P26 plan's
Review section for full evidence.

## 1. Evidence base — the two trees actually built

| Tree | Configure | Irrlicht | Result |
|---|---|---|---|
| **Desktop** | `build/` (pre-existing, `EQT_GRAPHICS=ON EQT_GLES2=OFF EQT_BUILD_TESTS=ON WITH_RDP=ON`) | system `/usr/lib/x86_64-linux-gnu/libIrrlicht.so.1.8` (shared) | 103 binaries, already built |
| **GLES2 + tests** | `build-gles2-tests/` (new scratch tree this unit) — `EQT_GRAPHICS=ON EQT_GLES2=ON EQT_DRM=OFF EQT_GLES2_IRRLICHT_DIR=third_party/irrlicht-gles2-x86 EQT_BUILD_TESTS=ON WITH_RDP=ON` | patched static `third_party/irrlicht-gles2-x86/lib/libIrrlicht.a` (built via `scripts/build-gles2-x86.sh --irrlicht-only`, already present in the worktree) | 90 binaries link clean, **14 fail to link** (see §3) |

`third_party/irrlicht-gles2-x86/` and `build-gles2-tests/` are scratch —
gitignored, not committed, removed at the end of this unit (see Cleanup).

**Cosmetic bug found while configuring**: the final `MESSAGE(STATUS ...)`
summary block (CMakeLists.txt ~1717-1723) prints `Display backend: X11/GLX`
whenever `EQT_DRM=OFF`, even with `EQT_GLES2=ON` (it only branches on
`EQT_DRM AND EQT_GLES2` / `EQT_DRM`, never bare `EQT_GLES2`). The actual
`LINK_LIBS` logic (line 526, `ELSEIF(EQT_DRM OR EQT_GLES2)`) is correct — this
is a log-message-only bug, not a functional defect. Worth a one-line fix in
whichever phase touches that block.

## 2. Target inventory (evidence: `cmake --build build --target help` vs
   `cmake --build build-gles2-tests --target help`, `bin/` diff, `ldd`/`nm`)

The CMake target *list* is identical between configurations (both configure
the same `CMakeLists.txt`) — one cosmetic diff: `particle_race_stress` (P23)
was absent from the pre-existing `build/` cache only because that tree
predates P23 and hasn't been reconfigured; it's gated by the same
`EQT_GRAPHICS AND IRRLICHT_LIBRARY` condition in both. Not a GL-backend
difference.

What differs is which targets **link successfully**. Classified by root
mechanism, not by eyeballing CMakeLists:

### Class A — GLES2-clean already (build clean in both trees, verified)
`willeq`, `willeq_core`, `gles2_zone_benchmark`, `gles2_shader_diagnostic`,
`gles2_composite_audit`, `particle_race_stress`, `test_gl_points`, all
`hwcaps/gles2_*` and `hwcaps/egl_image_sharing_test`/`gpu_texture_formats`
probes (already GLESv2/EGL-only by design), and every tool that never links
`IRRLICHT_LIBRARY` at all: `s3d_extract`, `pfs_verify`, `wld_dump`,
`bsp_region_finder`, `zone_region_classifier`, `region_poly_audit`,
`atlas_uv_audit`, `willeq_db_builder`, `equipment_index_builder`,
`s3d_index_builder`, `zone_atlas_builder`, `eqe1_builder`, `verify_eqe1`,
`ui_atlas_builder`, `query_surface_map` — **~76 targets**, no work needed.
Plus all ~63 non-graphics test binaries and `test_rdp_server` /
`test_rdp_input_handler` (verified building clean with `WITH_RDP=ON` in the
GLES2 tree too — RDP is orthogonal to the 3D backend, confirmed by
reconfigure+rebuild in this unit).

### Class B — broken in the GLES2 tree, mechanical fix (11 tools + 3 tests = 14 targets)

Verified failure (full build log, `cmake --build build-gles2-tests -j24 -- -k`):

```
analyze_hum_model, bsp_coordinate_finder, entity_composite_builder,
extract_zone_lines, generate_surface_map, generate_textures, model_viewer,
s3d_dump, test_animation, uv_analysis, zone_line_editor,
test_asset_loading, test_door_loading, test_graphics
```

Root cause (confirmed via `ldd`/`nm`, not guessed): these targets link
`${IRRLICHT_LIBRARY}` (+ zlib/jsoncpp/fmt) but never list explicit
GL/EGL/GLESv2/drm/gbm libraries. Against the **desktop** system Irrlicht —
`ldd build/bin/model_viewer` shows `libIrrlicht.so.1.8` pulling in
`libGL.so.1`, `libX11.so.6`, `libXxf86vm.so.1` transitively as its own
`DT_NEEDED` entries — so the missing symbols resolve for free. Against the
**GLES2** patched static archive (`libIrrlicht.a`, no baked-in deps), the
linker needs the app to supply `glGetError`/`eglGetError`/`gbm_device_destroy`
etc. explicitly — link fails:
```
/usr/bin/ld: COGLES2Texture.cpp:(.text+0x1ac8): undefined reference to `glGetError'
/usr/bin/ld: CIrrDeviceFB.cpp:(.text+0x4f9): undefined reference to `gbm_device_destroy'
/usr/bin/ld: gpu_upload_thread.cpp:(.text+0x544): undefined reference to `eglGetError'
```
`willeq`, `gles2_zone_benchmark`, `gles2_shader_diagnostic`, and
`gles2_composite_audit` already carry the correct explicit
`EGL GLESv2 drm gbm` (+ `gpu_upload_thread.cpp`) set gated on `EQT_GLES2` —
that's the exact pattern the fix copies to the 14 Class B targets.

**This is the second time this exact target list has hit an incomplete-link
bug.** `refactor/deferred.md` records an earlier ~identical-membership
failure (7 of these 11 tools) against the *desktop* Release build: missing
`${IRRLICHT_LIBRARY}` itself caused an `undefined reference to
irr::video::IdentityMaterial` at `-O3` (fixed `55b101e`/`b68c32a`, silent at
`-O0`). Both incidents share a root pattern — these tool targets never
declare a complete, backend-agnostic link-dependency set for Irrlicht, so
they "work" only by accident of whichever implicit symbol resolution the
current build happens to provide. **Recommendation: make this the first
migration-plan unit** (§4, Phase 0) — low-risk, mechanical, and it's the
only thing standing between "today" and "the GLES2 tree builds 100% of what
the desktop tree builds."

### Class C — genuine new test behavior divergence (NOT a link problem)

`ctest --test-dir build-gles2-tests` (full run, `-j4`, 1118 registered vs
1235 desktop — delta is exactly the 3 Class-B test binaries'
`GTEST_DISCOVER_TESTS` `_NOT_BUILT` placeholders, 14+12+94=120 desktop-only
test *cases*, cross-checked test-name-diff): **6 failures**, of which:

- `test_graphics_NOT_BUILT`, `test_asset_loading_NOT_BUILT`,
  `test_door_loading_NOT_BUILT` — expected placeholders for Class B.
- `AudioManagerTest.PreloadCommonSounds` — pre-existing documented host
  exception (ALSA availability), listed in `EXECUTOR_PROTOCOL.md` and
  `refactor/test_baseline.txt`. Not new.
- `RDPServerFrameTest.UpdateFrameAfterStart` (SEGFAULT in the `-j4` run) —
  re-ran in isolation 3x on **both** trees: passes every time
  (`ctest -R RDPServerFrameTest.UpdateFrameAfterStart`). Consistent with the
  already-documented `RDPServerFrameTest.*` parallel-execution intermittent
  (protocol lists `UpdateFrameMultipleTimes`; this is a sibling test in the
  same flaky family, not a new regression).
- **`RaceModelAnimationHeadlessTest.ModelAnimatesProperly_Headless`
  (SEGFAULT) — genuinely new, 100% reproducible on the GLES2 tree, 100%
  passing on desktop.** Re-ran 3x each tree to confirm (not flaky either
  way). `gdb -batch -ex run -ex bt` shows the crash directly inside
  `TestBody()` (not in a render thread, not in EGL init — the fixture uses
  `EDT_NULL`, no GPU/window at all):
  ```
  Thread 1 received signal SIGSEGV
  #0 RaceModelAnimationHeadlessTest_..._Test::TestBody()
  ```
  Likely cause (not fixed — report only, per this unit's rules): the test
  dereferences `node->getInstanceMesh()->getMeshBuffer(0)->getVertices()`
  after `ASSERT_NE(..., nullptr)` on the mesh pointer, but doesn't guard
  against `getMeshBuffer(0)` returning a buffer with zero vertices; against
  the patched-Irrlicht build, `RaceModelLoader::createAnimatedNode` may be
  constructing an empty/different mesh even under `EDT_NULL`. This means
  **`libEGL warning: failed to open /dev/dri/... Permission denied`
  appears even for this EDT_NULL-only test** (unexplained — worth the
  investigating unit checking whether the patched Irrlicht build now probes
  EGL unconditionally at some static-init point, independent of
  `DriverType`). **This is a blocking finding for Phase 1** (§4) — the
  GLES2 tree cannot yet be certified to "carry the full test suite" until
  this is root-caused; it needs its own small investigation unit before
  Phase 1 closes out.

No other tests differ between trees.

## 3. Tools verdict table

| Tool | Class | External refs (scripts/docs/refactor grep) | Verdict |
|---|---|---|---|
| model_viewer | B | 8 files (active — spell-fx debugging, model QA) | **Ported** (P26: `EGL GLESv2 drm gbm` link set added — the target already had the conditional idiom but gated on `EQT_DRM` alone, missing the `EQT_GLES2`-without-`EQT_DRM` x86 dev case; fixed to `EQT_DRM OR EQT_GLES2`) |
| entity_composite_builder | B | 9 files (active — composite atlas pipeline) | **Ported** (P26) |
| generate_surface_map | B | 2 files, incl. `docs/plans/surface_map_expansion_plan.md` (active feature history) | **Ported** (P26) |
| zone_line_editor | B | 2 files (`refactor/deferred.md`, this plan) | **Ported** (P26, 2D editor, low GL surface — cheap to fix) |
| s3d_dump | B | 6 files (active — archive analysis) | **Ported** (P26) |
| uv_analysis, analyze_hum_model, test_animation, bsp_coordinate_finder, extract_zone_lines | B | 1 file each — only `refactor/F04_ucs_guild_gm.md`'s "these fail to link, unrelated to F04" note, i.e. **no live usage instructions found**, just a prior bug report mentioning them | **RETIRED** (Will's decision 2026-07-28, executed P26) — CMake targets removed, sources moved to `tools/retired/` |
| generate_textures | B | 0 external refs (only this plan mentions it) | **RETIRED** (Will's decision 2026-07-28, executed P26) — offline procedural texture generator, no found consumer; CMake target removed, source moved to `tools/retired/` |
| All Class A tools | A | — | Already GLES2-clean, no action |

None of the Class B tools "genuinely test desktop-GL behavior" — every one
fails for the identical mechanical reason (§2 Class B), confirmed by
`ldd`/link-log, not by inspection. Phase 0 fixes all 14 with the same
one-line-per-target change; the retire/port split above is about whether to
bother at all for the 6 nearly-unreferenced ones, not about feasibility.

## 4. External consumer: `/mnt/projects/willeq-benchmark`

Confirmed (its `origin` remote is `/mnt/projects/fable/main`, pinned at
`b68c32a`, several commits behind current `fable` tip). Its own
`build_errors_response.md` (2026-07-27, from a prior orchestration session)
tells its operator explicitly:

> "the desktop-GL build you already have gives you the full test suite and
> is the right default for a patch-and-test harness... Only add the GLES2
> build if your benchmark tasks specifically target the GLES2 renderer."

**This is now in direct tension with the 2026-07-28 decision.** The
benchmark harness's documented default (`cmake ..` with no flags → desktop
GL, `ctest` reference number 1234/1235) is exactly the config this plan
eliminates. Its README (`| EQT_GLES2 | OFF | ... |`) and setup guidance both
assume desktop GL survives. **Risk register item, not a code risk**:
whichever phase removes the desktop-GL CMake path must ship an update to
that guidance (or a message to the benchmark session) *before or
simultaneously with* landing, or the external harness's next `git pull`
breaks its default configure line with no warning. Recommend this be an
explicit checklist line in whichever phase does the CMake removal (§4 Phase
3), not left implicit.

## 5. Source-level desktop-GL reference sweep

- **`EDT_BURNINGSVIDEO`** (software driver): 4 sites —
  `renderer_zone_stream.cpp:433`, `renderer_zone_load.cpp:700,1988`,
  `irrlicht_renderer.cpp:564` (the last is the *only* place it's
  constructed — a `RenderingBackend::Software` loading-screen driver
  choice; the other 3 are `driver_->getDriverType() != EDT_BURNINGSVIDEO`
  checks gating GPU-only code paths, i.e. dead once BURNINGSVIDEO can never
  be selected). `AnimatedTreeManager` is confirmed-dead code on GLES2 (P20
  finding, cited in decisions.md) — deletable alongside.
- **`EDT_OPENGL`** (desktop GL 2.1): loading-screen driver selection in
  `irrlicht_renderer.cpp` (~line 559), gated on
  `RenderingBackend::OpenGL` — a config enum
  (`include/client/graphics/constrained_renderer_config.h`) with call sites
  in `application.cpp:396`, `constrained_renderer_config.cpp` (4 sites:
  string-parsing `"opengl"`/`"gl"` and two hardware-tier defaults), plus
  `tools/model_viewer.cpp` and `tools/hwcaps/{gles2_composite_audit,
  zone_bench_loader}.cpp` (all guarded, need individual review — likely
  fallback/comparison code, not load-bearing).
- **`EMT_*` (Irrlicht material-type enum)** — 29 files reference
  `EMT_SOLID`/`EMT_TRANSPARENT_ALPHA_CHANNEL`/`EMT_LIGHTMAP`/etc. **Not
  desktop-only**: these are Irrlicht's abstract material-type constants,
  consumed identically by both `COGLES2MaterialRenderer` and the desktop
  driver (`src/client/graphics/CLAUDE.md` confirms `zone_shader.cpp`
  already emits GLES-ES-1.0-vs-desktop shader variants gated on
  `EQT_HAS_GLES2`, sharing the same `EMT_*` material-type IDs). No deletion
  candidate here — false positive for "desktop GL reliance."
- **Raw `gl*()` calls outside the driver**: none are fixed-function
  (`glBegin`/`glMatrixMode`/`glLoadIdentity`/`glVertex3f` all absent from
  the whole tree — zero grep hits). The `gl*(` hits that do exist
  (`renderer_zone_draw.cpp`, `zone_shader.cpp`, `renderer_effects.cpp`,
  `constrained_texture_cache.cpp`, portal stencil calls per
  `src/client/graphics/CLAUDE.md`, etc.) are core GL/GLES-ES-shared calls
  (`glStencilFunc`/`glColorMask`/texture/VBO functions) valid under GLES2 —
  no fixed-function-pipeline reliance found anywhere in the client itself.
- **CMake desktop-only branches**: system-Irrlicht `FIND_PATH`/
  `FIND_LIBRARY` fallback (lines 84-94), the `X11 Xxf86vm GL` link branch
  (line 549, taken whenever `EQT_DRM` and `EQT_GLES2` are both off — i.e.
  the only way to reach desktop GL at all), `model_viewer`'s own X11/GL
  `ELSE()` branch (690-700), `generate_textures`'s (733-741), and the
  desktop-`GL`-only `ELSE()` in `gles2_zone_benchmark`'s `ZONE_BENCH_LIBS`
  (1499-1502).
- **Goldens / visual gate**: `scripts/ci/visual_gate.sh` targets
  `build-gles2/` explicitly (hardcoded in the script's own header comment
  and its SKIP-if-absent logic) regardless of which tree's `ctest` invokes
  it — confirmed by reading the script. Goldens are unaffected by this
  elimination; they already never touch the desktop build.

## 6. Phased migration plan

Validated against evidence above — the plan bends to the finding, not the
other way round. Each phase is its own executor unit, its own PASS bar,
tree stays green throughout.

**Phase 0 — EXECUTED (P26).** Originally scoped as "fix all 14 Class-B link
failures"; landed as 6 RETIRED (Will's 2026-07-28 decision — CMake targets
removed, sources moved to `tools/retired/`) + 8 PORTED (`model_viewer`,
`entity_composite_builder`, `generate_surface_map`, `zone_line_editor`,
`s3d_dump`, `test_asset_loading`, `test_door_loading`, `test_graphics`),
each getting the explicit `EGL GLESv2 drm gbm` link set (+
`gpu_upload_thread.cpp` source where the target touches
`constrained_texture_cache`, matching `gles2_zone_benchmark`'s pattern),
gated the same way `willeq`'s own `LINK_LIBS` already is (`EQT_DRM OR
EQT_GLES2` → EGL/GLESv2/drm/gbm; else desktop `X11 Xxf86vm GL`) so **both**
configs keep building. `model_viewer` had a latent bug in its existing
idiom (gated on `EQT_DRM` alone, so the x86 GLES2 dev config — `EQT_DRM=OFF,
EQT_GLES2=ON` — fell through to the desktop `X11/GL` branch and failed to
link against the static GLES2 archive); fixed to `EQT_DRM OR EQT_GLES2`.
Zero behavior change otherwise, mechanical, low risk. Bar met: both trees
build 100% of their (now-reduced) target sets; `ctest` registers the
identical 1235 test cases in both trees (delta 0, net of the
RaceModelAnimation finding below, which is the only genuine remaining
GLES2-tree failure — confirmed, not investigated).

**Phase 1 — Root-cause `RaceModelAnimationHeadlessTest.ModelAnimatesProperly_Headless`.**
Small, focused investigation unit (not a fix-everything unit): why does an
`EDT_NULL`-only test crash against the patched Irrlicht, and why does it
print an EGL/DRI permission warning at all under `EDT_NULL`. **Blocks** the
claim "the GLES2 tree carries the full test suite" — must close before
Phase 2 can declare the GLES2 tree the sole CI target.

**Phase 2 — EXECUTED (P30, `refactor/P30_phase2_3_desktop_removal.md`).**
Root `CLAUDE.md` "Build Commands" now documents the GLES2-config tree
(`-DEQT_GRAPHICS=ON -DEQT_GLES2=ON -DEQT_DRM=OFF
-DEQT_GLES2_IRRLICHT_DIR=third_party/irrlicht-gles2-x86
-DEQT_BUILD_TESTS=ON`) as the primary `mkdir build && cmake ..` guidance,
including the one-time `scripts/build-gles2-x86.sh --irrlicht-only`
bootstrap. `EXECUTOR_PROTOCOL.md`'s verification-bar steps 1-2 now spell out
the same GLES2-config tree for `<slot>/build`. No script in `scripts/` or
`scripts/ci/` configured its own desktop-GL tree from scratch (grepped both
dirs) — the ones referencing `build/bin/...` (e.g. `run-model-viewer.sh`,
`generate_s3d_reference.sh`) just run whatever binary is there and need no
change now that `build/`'s semantics have flipped to GLES2.
`scripts/ci/visual_gate.sh` already hardcoded `build-gles2/` and is
unaffected. `willeq-benchmark` guidance is the ORCHESTRATOR's to update at
merge time (§4), noted not touched.

**Phase 3 — EXECUTED (P30).** Deleted the system-Irrlicht
`FIND_PATH`/`FIND_LIBRARY` fallback (CMakeLists.txt's Irrlicht-detection
block is now: use `EQT_GLES2_IRRLICHT_DIR` if set, else use whatever
`IRRLICHT_INCLUDE_DIR`/`IRRLICHT_LIBRARY` were already supplied as cache
variables — which is how the ARM Docker build injects its own patched
Irrlicht — else `FATAL_ERROR` naming the bootstrap command). Added a second
configure-time gate: `EQT_GRAPHICS=ON` with a resolved Irrlicht now also
requires `EQT_DRM` or `EQT_GLES2`, else `FATAL_ERROR` (desktop GL is not
reachable any other way). Removed every `X11 Xxf86vm GL` desktop link
branch: main `LINK_LIBS`, `model_viewer`, `s3d_dump`,
`entity_composite_builder`, `generate_surface_map`, `zone_line_editor`, the
3 equivalent test binaries in `tests/CMakeLists.txt`
(`test_graphics`/`test_asset_loading`/`test_door_loading`), and
`gles2_zone_benchmark`'s desktop-`GL`-only `ELSE()`. (`generate_textures`
was already retired in P26 — no branch left to remove.) `APPLE` branches
kept as-is (ambiguous/never a supported target — per this unit's rules, not
touched). Fixed the cosmetic `Display backend: X11/GLX` bug (§1) in the same
pass: added an explicit `EQT_GLES2`-without-`EQT_DRM` branch reporting "EGL +
OpenGL ES 2.0", and the final bare `ELSE()` (now dead — the mandatory-Irrlicht
gate guarantees `EQT_DRM OR EQT_GLES2`) reports "UNKNOWN (unreachable...)"
rather than the misleading "X11/GLX". P25's X11-EGL window backend
(confirmed already landed — `CIrrDeviceFB_X11.cpp`, `--egl-x11`, in the
worktree before this unit started) remains the interactive dev-use
replacement, unaffected by this phase.

**Known casualty (flagged, not fixed — out of this unit's territory):**
`docker/Dockerfile` (the non-ARM, non-`build-arm-noble.sh` Docker image) apt-
installs `libirrlicht-dev libxxf86vm-dev libx11-dev libgl1-mesa-dev` and
configures with plain `-DEQT_GRAPHICS=ON` — no `EQT_GLES2_IRRLICHT_DIR`, no
`EQT_GLES2`/`EQT_DRM`. This now fails at configure with the new
mandatory-Irrlicht `FATAL_ERROR`, by design (that was exactly a desktop-GL
build). `docker/` is not in this unit's assigned territory (CMakeLists.txt /
tests/CMakeLists.txt / scripts/ / CLAUDE.md / EXECUTOR_PROTOCOL.md /
this roadmap doc only) — orchestrator should schedule a fix or retirement of
that Dockerfile alongside the willeq-benchmark guidance update (§4).

**Phase 4 — Delete dead source paths.** `EDT_BURNINGSVIDEO`/
`AnimatedTreeManager`/`RenderingBackend::Software`/`RenderingBackend::OpenGL`
and their ~10 call sites (§5), the `EDT_OPENGL` loading-screen branch. Each
deletion individually small; batch by file for review-ability.

**Phase 5 — Docs.** `CLAUDE.md` (top-level and
`src/client/graphics/CLAUDE.md`'s driver-tier table — collapse "Desktop GL"
row, update "Legacy platforms" note), `docs/decisions.md` (mark this
decision's elimination complete), remove/update
`scripts/build-gles2-x86.sh`'s "sidesteps system libirrlicht" framing since
there will be no other kind left.

**Phase 4 — ATTEMPTED, NOT EXECUTED (P31, 2026-07-28,
`refactor/P31_phase4_5_dead_code_docs.md`).** Verification (the plan's own
"name the gate" requirement) found none of the three deletions are provably
dead: `RenderingBackend::Software`/`EDT_BURNINGSVIDEO` is directly reachable
via the documented `--renderer software` / `--constrained voodoo1|voodoo2`
flags on the exact tree the verification bar builds, and
`docker/irrlicht-drm-x86/CIrrDeviceFB.cpp::createDriver()` compiles
`createBurningVideoDriver()` unconditionally; `AnimatedTreeManager` is
unconditionally constructed and its wind-shader classification/
`getWindController()` are live on the GLES2 path (only its CPU-instancing
branch is runtime-gated on `EDT_BURNINGSVIDEO`) — deleting the class cascades
well beyond call-site cleanup; `RenderingBackend::OpenGL`/`EDT_OPENGL` is a
dead end on x86 specifically (`_IRR_COMPILE_WITH_OPENGL_` disabled in the
patched Irrlicht) but is the live compiled driver for the still-buildable ARM
`--no-gles2` Lima configuration (unaffected by Phase 3, still named in root
CLAUDE.md's "Legacy platforms" note). Per this unit's ambiguous-reachability
rule, all three were KEPT and flagged rather than deleted — full evidence in
`docs/decisions.md`'s 2026-07-28 entry and `src/client/graphics/CLAUDE.md`'s
Renderer Tiers table. **The elimination is therefore NOT complete** — Phase 4
is open, blocked on a product decision (not an executor call) about whether
to also retire the CLI surface (`--renderer software|opengl`, `--constrained
voodoo1|voodoo2|tnt`) and the ARM `--no-gles2` build, which is the actual
precondition for these deletions to be safe.

**Phase 4 — EXECUTED (P32, 2026-07-28, `refactor/P32_phase4_final.md`).**
Will's 2026-07-28 scope-ruling addendum (`docs/decisions.md`) resolved the
precondition P31 identified: `--renderer` limited to `gles2` (software/opengl
removed, CLI fails fast on them), constrained presets made
renderer-independent, ARM `--no-gles2` Lima build retired. With entry points
closed first, all three deletions became provably dead and were executed:
`RenderingBackend::Software`/`::OpenGL` and every call site
(`EDT_BURNINGSVIDEO`/`EDT_OPENGL` client branches, the `EDT_SOFTWARE` retry
fallback) removed; `AnimatedTreeManager` deleted — its two live GLES2
consumers (tree classification via `isTreeObject`, wind config/weather
multiplier via `getWindController()`) were extracted onto
`TreeIdentifier`/`TreeWindController` directly (pre-existing separate TUs;
`AnimatedTreeManager` was a thin facade over them plus dead CPU-instancing
machinery, confirmed via full call-graph trace that the CPU-instancing tree
list was populated only by the now-removed `EDT_BURNINGSVIDEO` branch, so
every "if (treeManager_)" consumer was safe to convert to unconditional
direct calls). **The elimination is now COMPLETE.** Full verification
evidence, the extraction's consumer-by-consumer mapping, and the grep-proof
table are in `refactor/P32_phase4_final.md`'s Review.

**Phase 4 item 4 (docker/Dockerfile) and Phase 5 — EXECUTED (P31).**
`docker/Dockerfile` (non-ARM image) had a live, currently-documented workflow
(`docker/docker-compose.yml`'s `builder`/`willeq`/`willeq-vnc` services,
`docker/README.md`) so it was UPDATED, not retired: apt packages swapped from
`libirrlicht-dev libxxf86vm-dev libx11-dev libgl1-mesa-dev` to
`libegl1-mesa-dev libgles2-mesa-dev libgbm-dev libdrm-dev libx11-dev`
(build stage) / `libegl1 libgles2 libgbm1 libdrm2 libx11-6` (runtime stage),
and the builder stage now bootstraps the same patched GLES2 Irrlicht
`scripts/build-gles2-x86.sh` builds (wget the pristine 1.8.5 source instead
of reading the host's `/mnt/projects/irrlicht-1.8.5` reference checkout,
apply `irrlicht-drm-patch.py` + the `docker/irrlicht-drm(-x86)/` sources,
disable `_IRR_COMPILE_WITH_OPENGL_`, `make staticlib` natively) before
configuring with `-DEQT_GLES2=ON -DEQT_DRM=OFF
-DEQT_GLES2_IRRLICHT_DIR=/opt/irrlicht-gles2`. Verified with a full local
`docker build` through all three stages (builder → runtime → vnc) — CMake
correctly logged `Display backend: EGL + OpenGL ES 2.0` and linked against
the bootstrapped Irrlicht, `willeq` built, and the final image's `willeq
--help` ran successfully. A root `.dockerignore` was added (none existed) —
without it the build context copied `build*/`/`third_party/` (~12GB of local
build trees) into every `docker build`, which is what caused the first
build attempt to time out; `third_party/irrlicht-gles2-x86/` (gitignored,
rebuilt in-image) is excluded but the rest of `third_party/` (vendored
sqlite3/miniaudio/etc., checked into git) is not. Doc sweep also done:
`scripts/build-gles2-x86.sh`'s stale "default `build/` tree is desktop GL"
comment corrected (P30 already made `build/` GLES2-mandatory), root
`CLAUDE.md`'s Running section and Display Options table updated to describe
the actual default (surfaceless EGL, no display needed) and `--egl-x11`
instead of implying plain "Direct X11" GPU rendering, and
`src/client/graphics/CLAUDE.md`'s Renderer Tiers table rewritten with the
Phase-4 reachability findings above (see `docs/decisions.md` for the full
version). The "collapse Desktop-GL row" instruction was NOT done — see Phase
4 above.

## 7. Risk register

- **RaceModelAnimationHeadlessTest regression** (§2 Class C) — could
  silently mask a real per-instance-mesh bug in the patched Irrlicht build
  if not root-caused before Phase 2; flagged as a hard blocker above, not
  merely a note.
- **willeq-benchmark external harness** (§4) — breaks its documented default
  build path with no warning unless its guidance is updated in lockstep
  with Phase 2/3. Low code risk, high external-friction risk if missed.
- **6 near-zero-reference tools** (§3) — retiring them destroys their build
  history/utility if anyone was relying on them without leaving a doc trail;
  Phase 0 should get an explicit Will go/no-go rather than defaulting to
  delete.
- **Goldens/visual gate** — verified unaffected (§5); re-confirm after
  Phase 3's CMake surgery in case a refactor accidentally changes which
  tree `visual_gate.sh` resolves `build-gles2/` against.
- **P25 sequencing** — Phase 3 must not land before P25's X11-EGL backend;
  if P25 slips, Phase 3 slips with it (Phase 0-2 have no such dependency and
  can proceed independently).
- **Test-suite parallel-execution flakiness** (`RDPServerFrameTest.*`
  family) — pre-existing, unrelated to this elimination, but the ctest
  delta-diffing methodology in Phase 0's bar should run tests serially or
  accept this known family as noise, not chase it as a regression.

## Cleanup

`build-gles2-tests/` (scratch ctest tree) and the `-DWITH_RDP` reconfigure
were removed from the worktree at the end of this unit; `third_party/
irrlicht-gles2-x86/` was already present before this unit (built by an
earlier session's `scripts/build-gles2-x86.sh` run) and is left as-is
(gitignored, reusable by the Phase 0 executor rather than rebuilding it
again from scratch).
