# P26 — Desktop-GL elimination Phase 0: link fixes + tool retirement

**Slot: slot1. x86 only — no Pi, no eqemu.** Sibling: P25 (slot2,
X11-EGL backend, touches docker/irrlicht-drm-x86 + its own build
wiring) — if you both touch `CMakeLists.txt`, keep to your own target
blocks; the orchestrator resolves any merge overlap.

Executes Phase 0 of `refactor/desktop_gl_elimination_plan.md` (read it
first — §4 Phase 0 and the §3 tools table), amended by Will's decision
(2026-07-28): the six retire-candidate tools are RETIRED, not fixed —
"can be ignored/archived".

## Work

1. **Retire six tools**: `uv_analysis`, `analyze_hum_model`,
   `test_animation`, `bsp_coordinate_finder`, `extract_zone_lines`,
   `generate_textures`. Remove their CMake targets; move their
   top-level sources from `tools/` to `tools/retired/` with a short
   `tools/retired/README.md` (what each did, retired per decisions.md
   2026-07-28, git history has the buildable versions). Do not delete
   shared sources other targets still compile.
2. **Fix the remaining 8 Class-B targets** (5 kept tools:
   `model_viewer`, `generate_textures`→retired so NOT it —
   the kept five per the plan: `model_viewer`, `entity_composite_builder`,
   `generate_surface_map`, `zone_line_editor`, `s3d_dump`; 3 tests:
   `test_asset_loading`, `test_door_loading`, `test_graphics`): add the
   explicit backend link set the plan prescribes so each links in BOTH
   the desktop tree and the GLES2 tree (`EQT_GLES2_IRRLICHT_DIR` +
   `EQT_BUILD_TESTS=ON`). Follow the plan's prescription; where it
   matches the existing conditional idiom (APPLE/EQT_DRM/EQT_GLES2
   branches used by model_viewer etc.), keep that idiom.
3. **Update the roadmap doc**: mark Phase 0 executed, record Will's
   retirement decision in the §3 table, adjust the ctest-registration
   expectations (the 3 test binaries now build in the GLES2 tree; the
   retired tools drop out of both trees' target counts).
4. Also update the two stale references the retirement creates, if
   any: grep scripts/, docs/, CLAUDE.md files for the six retired tool
   names; fix or annotate (do not rewrite historical refactor/ docs —
   they're banner-historical).

## Verification

- Desktop tree: full 6-step bar (known ctest exceptions per protocol).
- GLES2 tree: configure `EQT_GLES2_IRRLICHT_DIR` + tests ON, build
  `-k`, and run ctest — the 3 previously-NOT_BUILT test binaries now
  register and their ~120 cases run. Expected remaining GLES2-tree
  failures: the known host exceptions plus the DOCUMENTED Phase-1
  blocker `RaceModelAnimationHeadlessTest.ModelAnimatesProperly_Headless`
  (segfault — do NOT fix or investigate it, it's the next unit; just
  confirm it's still the only genuine failure).
- Report both trees' final ctest numbers explicitly.

## Rules

- Territory: `CMakeLists.txt` (tool/test target blocks only),
  `tools/` moves, the roadmap doc, small doc-reference fixes. No
  production src/ changes, no driver files, no goldens.
- STOP: a "mechanical" link fix turns out to need source changes in a
  target (report which and why instead).

## Review

**Retirement moves** (`git mv tools/<x>.cpp tools/retired/<x>.cpp`, CMake
targets removed from `CMakeLists.txt`): `uv_analysis`, `analyze_hum_model`,
`test_animation`, `bsp_coordinate_finder`, `extract_zone_lines` (all in the
`IF(EQT_GRAPHICS)` block that also holds `s3d_dump`/`entity_composite_builder`
etc.), `generate_textures` (its own `IF(EQT_GRAPHICS AND IRRLICHT_LIBRARY)`
block). `tools/retired/README.md` added documenting what each did and why.
Shared sources these tools used (`constrained_texture_cache.cpp`,
`background_thread_pool.cpp`, etc.) were left untouched — still compiled by
other targets/willeq_core.

**Per-target link additions** (all in `CMakeLists.txt` unless noted):

- `model_viewer`: bug fix, not addition — `ELSEIF(EQT_DRM)` → `ELSEIF(EQT_DRM
  OR EQT_GLES2)`. The target already had the full APPLE/DRM+GLES2/desktop
  idiom; it was gated on `EQT_DRM` alone, so the x86 GLES2 dev config
  (`EQT_DRM=OFF, EQT_GLES2=ON`) fell into the desktop `X11/GL` `ELSE()` branch
  and failed to link (missing `eglGetError`/`gbm_device_destroy` etc. against
  the static GLES2-patched Irrlicht archive).
- `s3d_dump`, `entity_composite_builder`, `generate_surface_map`,
  `zone_line_editor`: added the same `IF(EQT_DRM OR EQT_GLES2) → EGL GLESv2
  drm gbm ELSEIF(NOT APPLE) → X11 Xxf86vm GL` block (these targets had no
  conditional branch at all before — relied on the desktop shared
  `libIrrlicht.so` transitively pulling in GL/X11). Each of these four also
  compiles `constrained_texture_cache.cpp`, which references
  `gpu_upload_thread.h` under `EQT_HAS_GLES2`; added `TARGET_SOURCES(<target>
  PRIVATE src/client/graphics/gpu_upload_thread.cpp)` gated on `EQT_GLES2` to
  each (matching `model_viewer`'s existing pattern for the same reason).
- `test_asset_loading`, `test_door_loading`, `test_graphics`
  (`tests/CMakeLists.txt`): same `EGL GLESv2 drm gbm` / `X11 Xxf86vm GL` link
  block plus the same conditional `gpu_upload_thread.cpp` source addition
  (all three compile `constrained_texture_cache.cpp`).

**Doc updates**: `refactor/desktop_gl_elimination_plan.md` — Phase 0 marked
EXECUTED (intro + §6), §3 tools table updated (5 Port→Ported, 6 Retire
candidates→RETIRED with the decision date), `src/client/graphics/CLAUDE.md`
— removed the stale `generate_textures` tools-list bullet (only stale
reference found; grep of `scripts/`, `docs/`, all `CLAUDE.md` files for the
six retired names turned up nothing else live — `refactor/F04_ucs_guild_gm.md`
is historical and correctly left untouched).

**Both trees' final ctest numbers** (identical 1235 registered, name-diff
confirmed byte-identical test lists between trees):

| Tree | Registered | Passed | Failed |
|---|---|---|---|
| Desktop (`build/`) | 1235 | 1233 | 2 — `AudioManagerTest.PreloadCommonSounds` (known host exception, ALSA), `RDPServerFrameTest.UpdateFrameMultipleTimes` (known intermittent, SEGFAULT) |
| GLES2 (`build-gles2-tests/`, configured `EQT_GLES2=ON EQT_DRM=OFF EQT_GLES2_IRRLICHT_DIR=... EQT_BUILD_TESTS=ON WITH_RDP=ON`) | 1235 | 1233 | 2 — `AudioManagerTest.PreloadCommonSounds` (same known host exception), `RaceModelAnimationHeadlessTest.ModelAnimatesProperly_Headless` (SEGFAULT — **confirmed** still the sole genuine GLES2-tree failure, the documented Phase-1 blocker; not investigated per this unit's rules) |

(Note: `WITH_RDP` must be explicitly set `ON` when configuring the GLES2
tests tree — `scripts/build-gles2-x86.sh`'s own default config uses
`WITH_RDP=OFF` and `EQT_BUILD_TESTS=OFF`, which is correct for its own
purpose (`willeq` + `gles2_zone_benchmark` only) but would under-report the
ctest delta if reused verbatim for full-suite comparison; this unit
configured a separate `build-gles2-tests/` tree with `EQT_BUILD_TESTS=ON
WITH_RDP=ON` for the apples-to-apples count above.)

**Verification bar**: desktop `cmake --build -j24` — 0 errors, retired
targets confirmed absent from `cmake --build build --target help`; GLES2
tree `cmake --build build-gles2-tests -j24 -- -k` — 0 errors, all 5 kept
tools + 3 test binaries present in `bin/`; `scripts/build-gles2-x86.sh`
(default `build-gles2/`, `willeq` + `gles2_zone_benchmark`) — success;
`scripts/build-arm-noble.sh` — success, `model_viewer` present in ARM
output (exercises the `EQT_DRM=ON EQT_GLES2=ON` branch of the fix);
`scripts/refactor/check_file_sizes.sh fable` — no new FAILs (2 pre-existing
WARNs, `application.cpp`/`irrlicht_renderer.cpp`, both pre-existing and
unrelated to this unit's diff).

**Deviations**:
1. `check_file_sizes.sh fable` reported one ERROR (not FAIL):
   `docker/irrlicht-drm-x86/CIrrDeviceFB_X11.cpp: file not found`. Root
   cause: `fable` advanced past this task branch's fork point during this
   unit's execution (P25 merged to `fable` mid-session, adding that file);
   the script's default base diffs against the *current* `fable` tip, so it
   sees the file as "deleted" relative to a branch that predates P25's
   merge. Confirmed via `git merge-base --is-ancestor` that this branch's
   fork point is a strict ancestor of current `fable`. Not caused by any
   change in this unit (nothing under `docker/` touched); a rebase would
   clear it, left to the orchestrator's merge process rather than rebasing
   mid-unit.
2. Built a second GLES2 CMake tree (`build-gles2-tests/`) alongside the
   `scripts/build-gles2-x86.sh` default (`build-gles2/`) rather than
   reconfiguring the latter, since the script's own config
   (`EQT_BUILD_TESTS=OFF`) can't produce the full ctest run the plan asks
   for; both point at the same `third_party/irrlicht-gles2-x86` static
   Irrlicht build, so no duplicate Irrlicht compile.

**Final commit**: see `git log -1` on `task/P26-phase0-links` after this
Review section is committed.
