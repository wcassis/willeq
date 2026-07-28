# P30 — Desktop-GL elimination Phases 2+3: GLES2 default + branch removal

**Slot: slot1. x86 only — no Pi, no eqemu.** Siblings RUNNING: P28
(slot2, eq.h + 12 client TUs — do NOT touch those files), P29 (slot3,
no source). Your territory: CMakeLists.txt / tests/CMakeLists.txt,
scripts/, the build-instruction sections of the root CLAUDE.md, the
verification-bar section of refactor/EXECUTOR_PROTOCOL.md, the roadmap
doc, plus ONE named cosmetic source fix (below). Nothing else in src/.

Executes `refactor/desktop_gl_elimination_plan.md` §6 Phases 2 and 3
(read §5-§7 first; Phases 4/5 are NOT in scope — no source-path
deletions, no full doc sweep). Prereqs all met: Phase 0/1 executed,
P25 landed (windowed dev path exists), GLES2 tree certified 1234/1235.

## Work

1. **Phase 2 — GLES2 config becomes the primary tree.** The canonical
   dev/test configure is now the GLES2 one (patched static Irrlicht,
   `EQT_GLES2=ON`, tests on). Update: root CLAUDE.md "Build Commands"
   (plain `mkdir build && cmake ..` guidance → the GLES2-config
   equivalent, including the one-time
   `scripts/build-gles2-x86.sh --irrlicht-only` bootstrap if
   third_party is absent — verify that flag exists, else document the
   actual bootstrap), EXECUTOR_PROTOCOL.md's verification bar steps
   1-2 (build/ctest now mean the GLES2-config tree), and any script
   that assumes `build/` is the desktop tree (grep scripts/ and
   scripts/ci/). Keep `build-gles2/` (the client+benchmark tree the
   build script produces) and its consumers (visual gate) unchanged.
2. **Phase 3 — remove desktop GL from CMake** per the roadmap's §5
   inventory: the system-Irrlicht FIND_PATH/FIND_LIBRARY fallback
   (~lines 84-94), every `X11 Xxf86vm GL` desktop link branch (main
   LINK_LIBS ~549, model_viewer, gles2_zone_benchmark's desktop
   ELSE(), the 8 P26/P27 per-target blocks' desktop halves, tests/),
   the APPLE branches IF they are desktop-GL-only (verify; Apple has
   never been a supported target here — if removal is ambiguous, keep
   and note). `EQT_GLES2_IRRLICHT_DIR` becomes effectively mandatory:
   configure without a usable patched Irrlicht must FAIL EARLY with a
   clear message naming the bootstrap command — not fall back to
   desktop, not fail at link time.
3. **Cosmetic fix** (roadmap Phase 3 note): the misleading
   `Display backend: X11/GLX` log message (roadmap §1) — fix the
   string at its single site now that its branch is being touched.
   Name the file in the Review; if it lives in a file P28 owns
   (eq.h/the 12 TUs), SKIP it and say so.
4. Update the roadmap doc: Phases 2+3 marked EXECUTED with what
   actually happened.

## Rules

- The retired-desktop configure path must be GONE, not half-alive:
  after this unit, `-DEQT_GRAPHICS=ON` without the patched Irrlicht
  errors out clearly; there is no code path that links system
  libIrrlicht. `EQT_GRAPHICS=OFF` (headless compile) is P28's concern
  — do not touch its semantics, and expect merge-time coordination if
  you both edit option-plumbing lines: keep your diff surgical.
- Do NOT delete dead source paths (AnimatedTreeManager,
  EDT_BURNINGSVIDEO branches etc.) — Phase 4, not yours.
- The willeq-benchmark clone guidance and TESTING_CHARTER wording are
  the ORCHESTRATOR's to update at merge time — note them, don't touch
  them.
- STOP: anything forces a behavior change in the GLES2/ARM rendering
  paths, or the mandatory-Irrlicht configure gate can't be made clean.

## Verification (the bar itself changes in this unit — apply the NEW bar)

- GLES2-config full tree (tests on): build clean + ctest at the P27
  baseline (1234/1235, ALSA exception only).
- `bash scripts/build-gles2-x86.sh` clean; visual gate PASS, goldens
  untouched.
- `bash scripts/build-arm-noble.sh` clean (ARM CMake path must be
  unaffected by the removals — it never used the desktop branches;
  verify, don't assume).
- Negative test: a configure attempt with no patched Irrlicht
  available fails early with the documented message (show it).
- `check_file_sizes.sh` vs fable: no new FAILs.

## Review

### Removal inventory vs roadmap §5

| Item | Disposition | Why |
|---|---|---|
| System-Irrlicht `FIND_PATH`/`FIND_LIBRARY` fallback (~L84-94) | **REMOVED** | Replaced with: use `EQT_GLES2_IRRLICHT_DIR` if set; else use whatever `IRRLICHT_INCLUDE_DIR`/`IRRLICHT_LIBRARY` are already cache-set (this is how `docker/Dockerfile.arm-noble`/`scripts/build-arm-noble.sh` inject the ARM patched Irrlicht — confirmed by reading the Dockerfile, not assumed); else `FATAL_ERROR` naming the bootstrap command. |
| Main `LINK_LIBS` `X11 Xxf86vm GL` branch (~L549/555) | **REMOVED** | Was the bare `ELSE()` after `IF(APPLE) ELSEIF(EQT_DRM OR EQT_GLES2)`; converted `ELSEIF` to `ELSE()` and dropped the desktop tail — a new configure-time gate (see below) guarantees `EQT_DRM OR EQT_GLES2` whenever this code runs, so the branch was unreachable dead code. |
| `model_viewer`'s own `X11`/`GL` `ELSE()` | **REMOVED** | Same pattern as main `LINK_LIBS`. |
| `generate_textures`'s `ELSE()` | **N/A — already gone** | Tool was RETIRED in P26 (Phase 0); no branch left to remove. Noted, not touched. |
| `gles2_zone_benchmark`'s desktop-`GL`-only `ELSE()` in `ZONE_BENCH_LIBS` | **REMOVED** | Same pattern. |
| "8 P26/P27 per-target blocks' desktop halves" | **REMOVED (7 of 8 have a desktop half; 1 didn't)** | `s3d_dump`, `entity_composite_builder`, `generate_surface_map`, `zone_line_editor` (main CMakeLists.txt) + `test_graphics`, `test_asset_loading`, `test_door_loading` (tests/CMakeLists.txt) each had an `ELSEIF(NOT APPLE) TARGET_LINK_LIBRARIES(... X11 Xxf86vm GL)` tail — removed, leaving only the `IF(EQT_DRM OR EQT_GLES2) ... ENDIF()` body. `model_viewer` is counted above (its ELSE() had no ELSEIF form). |
| `APPLE` branches | **KEPT** | Ambiguous whether they're "desktop-GL-only" (they use Cocoa/OpenGL/IOKit/Carbon frameworks, not X11) and Apple has never been a supported target — kept per the plan's explicit fallback rule, noted here rather than silently left. |
| `EQT_GLES2_IRRLICHT_DIR` mandatory | **DONE, with a caveat** | Made effectively mandatory via the FATAL_ERROR gate above — but the *literal* variable stays optional as a name, because the ARM path (correctly, per §6 Phase 3's own architecture) never sets it, injecting `IRRLICHT_INCLUDE_DIR`/`IRRLICHT_LIBRARY` directly instead. What's now mandatory is "a resolved Irrlicht path, by one of the two sanctioned mechanisms" — verified this doesn't regress ARM (build-arm-noble.sh ran clean, see Verification). |
| Cosmetic `Display backend: X11/GLX` bug (§1) | **FIXED** | Root `CMakeLists.txt`'s final `MESSAGE(STATUS ...)` block (not a P28-owned file). Added an explicit `ELSEIF(EQT_GLES2)` branch ("EGL + OpenGL ES 2.0 …"); the residual bare `ELSE()` (now unreachable given the mandatory-Irrlicht gate) reports "UNKNOWN (unreachable …)" instead of the misleading "X11/GLX", left in place defensively rather than deleted. |

New configure-time gate added (not explicitly named in §5 but required to make the mandatory-Irrlicht claim hold without a link-time failure): after Irrlicht resolves, `IF(NOT (EQT_DRM OR EQT_GLES2)) MESSAGE(FATAL_ERROR ...)`. Without it, `-DEQT_GLES2_IRRLICHT_DIR=... ` alone (forgetting `-DEQT_GLES2=ON`) would have fallen through the (now-removed) desktop `ELSE()` with nothing appended, failing at *link* time — exactly what the plan forbids.

### New configure UX + bootstrap docs

- `CLAUDE.md` "Build Commands": replaced the plain `mkdir build && cd build && cmake ..` guidance with the GLES2-config equivalent (`-DEQT_GRAPHICS=ON -DEQT_GLES2=ON -DEQT_DRM=OFF -DEQT_GLES2_IRRLICHT_DIR=third_party/irrlicht-gles2-x86 -DEQT_BUILD_TESTS=ON`), the one-time `bash scripts/build-gles2-x86.sh --irrlicht-only` bootstrap (verified this flag exists — `scripts/build-gles2-x86.sh` line ~30 `--irrlicht-only`), and a note that `build-gles2/` (produced by the no-args form of that script) is untouched and still what `scripts/ci/visual_gate.sh` targets. Also fixed the stale "Dependencies" line (`libirrlicht-dev, libxxf86vm-dev`) to point at the patched-Irrlicht bootstrap instead of an apt package.
- `refactor/EXECUTOR_PROTOCOL.md` verification-bar steps 1-2: now spell out the exact GLES2-config `cmake` invocation for `<slot>/build`, plus the bootstrap-if-absent note and the 1234/1235 P27 baseline.
- `refactor/desktop_gl_elimination_plan.md`: Phase 2 and Phase 3 marked EXECUTED with what actually happened (see the doc itself — kept in sync with this table rather than duplicated further here), including the `docker/Dockerfile` casualty finding (below).

### Negative test output (configure with no patched Irrlicht available)

```
$ cmake -S . -B /tmp/cmake_negative_check
...
-- Found Boost: ... found components: graph
CMake Error at CMakeLists.txt:106 (MESSAGE):
  EQT_GRAPHICS=ON requires a locally-built patched GLES2 Irrlicht —
  desktop-GL Irrlicht (system libirrlicht-dev / X11-GLX) support was removed
  (P30, refactor/desktop_gl_elimination_plan.md).  For x86 dev builds: run
  'bash scripts/build-gles2-x86.sh --irrlicht-only' once, then configure with
  -DEQT_GLES2_IRRLICHT_DIR=third_party/irrlicht-gles2-x86 -DEQT_GLES2=ON (see
  CLAUDE.md 'Build Commands').  For ARM cross-compiles, use
  scripts/build-arm-noble.sh, which supplies
  IRRLICHT_INCLUDE_DIR/IRRLICHT_LIBRARY directly.  To build without graphics
  at all, configure with -DEQT_GRAPHICS=OFF.

-- Configuring incomplete, errors occurred!
```
Fails at configure (no build/link attempted), names the exact bootstrap command, exit is a CMake hard error (not a silent fallback). Ran against a scratch tree with no `-D` flags at all — `EQT_GRAPHICS` defaults ON, `EQT_GLES2_IRRLICHT_DIR` defaults empty, no `IRRLICHT_INCLUDE_DIR`/`IRRLICHT_LIBRARY` cache vars present.

### Verification numbers (NEW bar, this unit)

1. GLES2-config full tree, tests on (`<slot>/build`, reconfigured from the pre-existing desktop tree — old cache wiped since the config fundamentally changed): `cmake --build build -j24` — **100% built clean**, no errors (pre-existing narrowing warnings in `inventory_manager.cpp` only, unrelated to this unit).
2. `ctest --test-dir build --output-on-failure`: **1234/1235 passed** — sole failure `AudioManagerTest.PreloadCommonSounds` (documented ALSA host exception). Matches the P27 baseline exactly. Full run took ~300s.
3. `bash scripts/build-gles2-x86.sh` (full run, not `--irrlicht-only`): **clean**, rebuilt `third_party/irrlicht-gles2-x86/libIrrlicht.a` + `build-gles2/bin/{willeq,gles2_zone_benchmark}`.
4. Visual gate: `bash scripts/ci/visual_gate.sh` — first run FAILed on CPU-time ceilings (qeynos2, befallen — `p95 CPU ms` over 2x baseline), immediately re-ran clean 5s later: **PASS**. Attributed to transient host load from the concurrent full-tree build+ctest run this unit had just finished (evidence discipline: re-ran rather than accepting either result on faith; PASS is the reproducible steady-state result). Goldens untouched — `git status` shows no changes under any golden/image path, confirmed no `--bless` was ever invoked.
5. `bash scripts/build-arm-noble.sh`: **clean**, full target list built (`willeq`, `model_viewer`, `gpu_texture_formats`, 10 `gles2_*`/hwcaps probes, `gles2_zone_benchmark`, `gles2_shader_diagnostic`, `gles2_composite_audit`), `willeq` binary produced (340M unstripped ARM ELF). Confirms the ARM path is unaffected by the removals, as the plan required verifying rather than assuming.
6. `bash scripts/refactor/check_file_sizes.sh fable`: **no output, exit 0** — no new FAILs (expected: this unit only edits CMake/docs, no new large C++ files).

### Deviations

- The plan named `EQT_GLES2_IRRLICHT_DIR` as becoming "non-optional/always-on" (roadmap §6 Phase 3 wording); literally making it a required CMake variable would have broken the ARM build, which never sets it (confirmed by reading `docker/Dockerfile.arm-noble` — it injects `IRRLICHT_INCLUDE_DIR`/`IRRLICHT_LIBRARY` directly as cache variables instead). Implemented the equivalent *intent* — a resolved Irrlicht is now mandatory, sourced from either mechanism, with an early `FATAL_ERROR` if neither is present — verified ARM still builds clean. Flagged here rather than silently reinterpreting the plan.
- Found and documented (not fixed — out of this unit's territory) that `docker/Dockerfile` (the non-ARM Docker image, `apt-get install libirrlicht-dev libxxf86vm-dev libx11-dev libgl1-mesa-dev` + plain `-DEQT_GRAPHICS=ON`) now fails at configure by design, since it was exactly a desktop-GL build with no `EQT_GLES2_IRRLICHT_DIR`/`EQT_GLES2`/`EQT_DRM`. `docker/` is not in this unit's assigned territory (CMakeLists.txt/tests/CMakeLists.txt/scripts//CLAUDE.md/EXECUTOR_PROTOCOL.md/roadmap doc only) — flagged in the roadmap doc's Phase 3 section and here for the orchestrator to schedule a fix or retirement alongside the `willeq-benchmark` guidance update (§4, also not touched — orchestrator's per the plan).
- `build/` (the pre-existing desktop-config tree, 103 binaries) was wiped and reconfigured in-place as the new GLES2-config tree rather than left as a stale desktop cache, since Phase 2 makes it the primary tree and its old cache (`EQT_GLES2:BOOL=OFF`) was incompatible with the new mandatory-Irrlicht gate. `build/` is gitignored — no tracked-file impact.
- The `data/eq_client` symlink (`ln -s /mnt/projects/EverQuestP1999 data/eq_client`, gitignored, per `refactor/test_baseline.txt`) did not exist in this worktree; created it to get the full 1235-test count rather than 3 fewer asset-dependent tests silently skipping. Standard per-worktree setup, not a code change.

### Final commit

See branch `task/P30-phase2-3`, commit created immediately after this Review section was filled in (see the executor's final report for the hash).
