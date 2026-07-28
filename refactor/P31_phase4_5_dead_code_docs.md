# P31 — Desktop-GL elimination Phases 4+5: dead-source deletion + doc sweep

**Slot: slot1. x86 only — no Pi, no eqemu. Sole agent this dispatch.**

Executes the final phases of `refactor/desktop_gl_elimination_plan.md`
§6 (read §5 for the inventory it validated). Prereqs: Phases 0-3
executed; GLES2 tree certified; P30's configure gate live.

## Phase 4 — delete dead source paths

Per the roadmap inventory (verify each is genuinely unreachable before
deleting — cite the gate that makes it dead):

1. `EDT_BURNINGSVIDEO` branches — including `loadZoneStep11_Env()`'s
   software-renderer gate and the `AnimatedTreeManager` it guards.
   Delete `animated_tree_manager.{h,cpp}` entirely (P20's dormant H02d
   mirror goes with it — that history lives in the P20/P20b plan docs),
   its wiring in `irrlicht_renderer.cpp` / `renderer_debug.cpp`, and
   its CMake source entry.
2. `RenderingBackend::Software` / `RenderingBackend::OpenGL` enum
   values and their ~10 call sites; the `EDT_OPENGL` loading-screen
   branch. Where a switch/if collapses to a single live case, collapse
   it cleanly — no empty shells left behind.
3. The patched device forks: do NOT delete driver-internal
   `EDT_SOFTWARE`/`EDT_OPENGL` switch cases in docker/irrlicht-drm*/
   (they're the device's own API surface and P27 just touched that
   switch; out of scope).
4. `docker/Dockerfile` (non-ARM image — orphaned by P30, fails
   configure by design): read `docker/CLAUDE.md` + grep for what
   references the image. If any workflow still uses it, UPDATE it to
   the GLES2 recipe (apt: EGL/GLESv2/mesa instead of libirrlicht-dev;
   bootstrap the patched Irrlicht in-image). If nothing references it,
   retire it (move to `docker/retired/` with a README note) — state
   which you did and the evidence.

## Phase 5 — doc sweep

- `src/client/graphics/CLAUDE.md`: driver-tier table — collapse the
  Desktop-GL row; update the "Legacy platforms" note; remove
  references to deleted classes (AnimatedTreeManager etc.).
- Root `CLAUDE.md`: sweep for now-stale desktop-GL references beyond
  what P30 already fixed (Display Options table, VNC/X11 notes that
  assume desktop GL — the X11-EGL windowed mode (P25,
  `WILLEQ_EGL_X11`) is the interactive x86 path to mention).
- `docs/decisions.md`: append a completion note under the 2026-07-28
  GLES2-only entry (phases executed, dates, headline commits) — do
  not rewrite the original entry.
- `scripts/build-gles2-x86.sh`: update the "sidesteps system
  libirrlicht" framing comment (there is no other kind now).
- `refactor/desktop_gl_elimination_plan.md`: mark Phases 4+5 executed;
  the elimination is COMPLETE.
- Do NOT touch historical docs (refactor/completed/, docs/reference/,
  P-unit plan files) — they're banner-historical by policy.

## Rules

- Deletion must be provably dead code: for each deletion, name the
  gate that made it unreachable (e.g. "driver type EDT_BURNINGSVIDEO
  is never constructed — P30's configure requires EQT_GLES2 or
  EQT_DRM, both of which request EDT_OGLES2"). If reachability is
  ambiguous, KEEP and flag — same discipline as P30's APPLE branches.
- Behavior-preserving on all live paths: GLES2 rendering identical,
  goldens untouched.
- Territory: the dead-path source files + their call sites, CMake
  entries, docker/Dockerfile (per 4.4), the named docs. Nothing else.
- STOP: a "dead" path turns out reachable, or deletion forces
  signature changes that cascade beyond call-site cleanup.

## Verification

- Full current bar: GLES2-config tree build + ctest at baseline
  (1234/1235, ALSA exception), `build-gles2-x86.sh` clean, visual
  gate PASS goldens untouched, `build-arm-noble.sh` clean,
  `check_file_sizes.sh` no new FAILs.
- Headless tree (`-DEQT_GRAPHICS=OFF`) still builds + suite at its
  812/813 baseline (P28's deliverable must not regress).
- If the Dockerfile was updated: prove it (build the image and run
  the in-image configure at least through the Irrlicht check, or
  document precisely why a full image build isn't feasible on this
  host and what was verified instead).
- Grep-proof: `EDT_BURNINGSVIDEO`, `AnimatedTreeManager`,
  `RenderingBackend::Software`, `RenderingBackend::OpenGL` appear
  nowhere in src/ or include/ after the unit (report the residual
  hits in docker/irrlicht-drm*/ and historical docs as expected).

## Review

**Deviation (major): Phase 4 deletions NOT made.** Verification per the
plan's own "name the gate" rule found all three targets reachable/live, not
provably dead — see the per-item table below and `docs/decisions.md`'s
2026-07-28 entry for full evidence. Per the plan's ambiguous-reachability
rule ("KEEP and flag — same discipline as P30's APPLE branches") and the
explicit STOP condition ("a 'dead' path turns out reachable"), no source was
deleted. Phase 4 item 4 (Dockerfile) and Phase 5 (doc sweep) were executed.

### Per-deletion table (attempted, not executed)

| Item | Plan's claimed gate | What verification found | Disposition |
|---|---|---|---|
| `EDT_BURNINGSVIDEO` / `RenderingBackend::Software` | "BURNINGSVIDEO can never be selected" | Directly reachable via documented `--renderer software` / `--constrained voodoo1\|voodoo2` on the exact GLES2-config `willeq` the verification bar builds (`src/client/application.cpp:353-357`, CLI override always wins over preset, no forcing-back-to-GLES2 safety net). `docker/irrlicht-drm-x86/CIrrDeviceFB.cpp::createDriver()` calls `createBurningVideoDriver()` with no `#ifdef` gate — compiled in unconditionally. | KEPT, flagged |
| `AnimatedTreeManager` (+ `animated_tree_manager.{h,cpp}`) | "guarded by the software-renderer gate" | `treeManager_` is unconditionally constructed in `IrrlichtRenderer::InitGraphics` (`irrlicht_renderer.cpp:830`, no driver-type check). Its wind-shader classification (`isTreeObject`) and `getWindController()` are used on the live GLES2 path: `renderer_zone_stream.cpp:426-432,572-577` (fixup-pass wind material assignment), `renderer_frame.cpp:1068-1070,1158` (per-frame wind update, weather multiplier), `renderer_sim_bridge.cpp:190,315-316,865,1054` (wind snapshot, tree shadows, diagnostics), `renderer_diagnostics.cpp:105-106,221-222`. Only the CPU-instancing branch (`beginInitialize`/`initializeNextBatch`/`assignBspRegions`) inside `loadZoneStep11_Env()` (`renderer_zone_load.cpp:1988-2004`) is runtime-gated on `driver_->getDriverType() == EDT_BURNINGSVIDEO`. Deleting the class would break the live wind path — the plan's own "deletion forces signature changes that cascade beyond call-site cleanup" STOP condition. | KEPT, flagged |
| `EDT_OPENGL` / `RenderingBackend::OpenGL` | Part of the same "never selected" claim | Dead end specifically on x86: `scripts/build-gles2-x86.sh` disables `_IRR_COMPILE_WITH_OPENGL_` in the patched Irrlicht, so `CIrrDeviceFB::createDriver()`'s `EDT_OPENGL` case doesn't exist in that static lib — falls through to the code's own `EDT_SOFTWARE` retry (`irrlicht_renderer.cpp:611-616`). But it is the live, compiled driver for the still-buildable ARM `--no-gles2` config (`scripts/build-arm-noble.sh --no-gles2` → `EQT_DRM=ON EQT_GLES2=OFF`, CMakeLists.txt:610-613 "Desktop GL mode via Lima" branch, untouched by P30's Phase 3 and explicitly named in root `CLAUDE.md`'s "Legacy platforms" note as present, if superseded) — this build was not part of this unit's verification bar but is documented as a current, supported (if legacy) configuration outside this executor's authority to silently break. | KEPT, flagged |
| Driver-internal `EDT_SOFTWARE`/`EDT_OPENGL` switch cases in `docker/irrlicht-drm*/` | N/A — explicitly out of scope | Confirmed out of scope, not touched | N/A |

### Dockerfile decision: UPDATED (not retired)

Evidence: `docker/docker-compose.yml` (3 services: `builder`, `willeq`,
`willeq-vnc`, all `dockerfile: docker/Dockerfile`) and `docker/README.md`
(current, non-historical doc — build/run instructions for all 3 stages) both
actively reference it — a live workflow, not orphaned.

Change: apt packages `libirrlicht-dev libxxf86vm-dev libx11-dev
libgl1-mesa-dev` → `libegl1-mesa-dev libgles2-mesa-dev libgbm-dev
libdrm-dev libx11-dev` (builder stage) / `libirrlicht1.8 libxxf86vm1 libx11-6
libgl1 libglx-mesa0` → `libegl1 libgles2 libgbm1 libdrm2 libx11-6` (runtime
stage). Builder stage now bootstraps the same patched GLES2 Irrlicht
`scripts/build-gles2-x86.sh`/`docker/Dockerfile.arm-noble` build (wget
pristine 1.8.5 instead of the host-local reference checkout those scripts
use, apply `irrlicht-drm-patch.py` + `docker/irrlicht-drm(-x86)/` sources,
disable `_IRR_COMPILE_WITH_OPENGL_`, `make staticlib` natively), then
configures with `-DEQT_GLES2=ON -DEQT_DRM=OFF
-DEQT_GLES2_IRRLICHT_DIR=/opt/irrlicht-gles2`.

Verification: full local `docker build` through all three stages
(`builder` → `runtime` → `vnc`) succeeded. CMake logged `Irrlicht:
/opt/irrlicht-gles2/lib/libIrrlicht.a` and `Display backend: EGL + OpenGL ES
2.0 (surfaceless llvmpipe, or windowed via --egl-x11; no desktop GL)`;
`willeq` built and linked; the final `vnc`-stage image's
`/usr/local/bin/willeq --help` ran successfully and printed the expected
usage text. Added root `.dockerignore` (none existed) — without it `docker
build`'s context copied `build/`, `build-gles2/`, `build-arm-noble/`, and
`third_party/irrlicht-gles2-x86/` (~12GB) into every build, which is what
made the first build attempt exceed the 10-minute timeout; it excludes
`build*/`, `.git`, `third_party/irrlicht-gles2-x86/` only (the rest of
`third_party/`, e.g. vendored sqlite3/miniaudio, is checked into git and
still needed).

### Doc sweep

- `scripts/build-gles2-x86.sh`: fixed the stale "the default `build/` tree
  (desktop GL, EQT_GLES2=OFF) is untouched by this script" comment — P30
  already made `build/` GLES2-mandatory (no desktop-GL tree exists to be
  "untouched"). The plan's literal quoted phrase ("sidesteps system
  libirrlicht") was not found verbatim in the script (may already have been
  reworded in an earlier pass); this was the closest actual stale framing.
- Root `CLAUDE.md`: "Running" section examples and the "Display Options
  Comparison" table updated — previously implied `./build/bin/willeq`
  needs "a display or VNC" and that "Direct X11" is the local-GPU option;
  corrected to describe the actual default (surfaceless EGL, no display
  needed) and the opt-in `--egl-x11` windowed path (P25).
- `src/client/graphics/CLAUDE.md`: Renderer Tiers table rewritten with the
  Phase-4 reachability findings (per-tier "Reachable via" / "Status"
  columns) instead of the plan's "collapse Desktop-GL row" instruction,
  since the row describes still-live (if narrow/legacy) code.
  `AnimatedTreeManager` was not previously listed in the Components section
  at all, so no "remove references to deleted class" edit was needed there.
- `docs/decisions.md`: appended a completion note under the 2026-07-28
  entry — Phase 0/2/3 executed dates, Phase 4 attempted-not-executed with
  the reachability evidence, Phase 4 item 4 + Phase 5 executed.
- `refactor/desktop_gl_elimination_plan.md`: Phase 4 marked
  ATTEMPTED/NOT EXECUTED with evidence; Phase 4 item 4 + Phase 5 marked
  EXECUTED. The elimination as a whole is explicitly NOT marked COMPLETE.
- Root `CLAUDE.md`'s "Legacy platforms" note and `src/client/graphics/
  CLAUDE.md`'s other sections were left as-is (already accurate, no
  deletions to reconcile against).

### Verification numbers

- `cmake --build build -j24` (GLES2-config tree): clean, no errors.
- `ctest --test-dir build --output-on-failure`: **1361/1362** (1362 total
  registered; 41 skipped by design [pcap/graphics-integration/golden-baseline
  tests requiring external fixtures — unchanged from prior baselines]; sole
  failure `AudioManagerTest.PreloadCommonSounds`, the documented ALSA host
  exception). `visual_gate` test: **Passed**. Note: the documented baseline
  is 1234/1235 (P27) — the higher count here reflects tests added by units
  since; the pass/fail *pattern* (single known ALSA exception) matches.
- `bash scripts/build-gles2-x86.sh`: clean (Irrlicht rebuild + `build-gles2/`
  `willeq` + `gles2_zone_benchmark`).
- Headless tree (`build-nogfx/`, `-DEQT_GRAPHICS=OFF -DEQT_BUILD_TESTS=ON`,
  freshly configured — none existed in the worktree): build clean, ctest
  **939/940** (1 failure, same ALSA exception; documented baseline is
  812/813 — again a higher absolute count from unrelated unit growth, same
  pass/fail pattern, P28's deliverable not regressed).
- `bash scripts/build-arm-noble.sh` (default, GLES2 ON): clean, all binaries
  built including `willeq`.
- `bash scripts/refactor/check_file_sizes.sh fable`: exit 0, no FAILs
  (no source touched this unit, so trivially unaffected, but included as
  a positive confirmation).
- Grep-proof (plan's requirement): **NOT satisfied, by design** —
  `EDT_BURNINGSVIDEO` (4 files), `AnimatedTreeManager` (6 files),
  `RenderingBackend::Software` (4 files), `RenderingBackend::OpenGL` (4
  files) all still present under `src/`/`include/`, because no deletions
  were made (see table above). This is the flagged deviation, not an
  oversight.

### Deviations summary

1. Phase 4 (items 1-2) not executed — reachability evidence contradicts the
   plan's premise (detailed above). No STOP was needed in the "abort the
   unit" sense since Phase 4 item 4 and Phase 5 remained independently
   executable and low-risk; this is reported as narrowed evidence per
   protocol ("report narrowed evidence, don't thrash") rather than a hard
   STOP.
2. Grep-proof requirement not met, as a direct consequence of (1).
3. `src/client/graphics/CLAUDE.md`'s driver-tier table was rewritten rather
   than having its "Desktop GL" row collapsed, to stay accurate given (1).
4. A root `.dockerignore` was added — not named in the plan's territory list
   but required to make the Dockerfile verification (full `docker build`)
   feasible on this host within the protocol's timeout discipline; scoped
   tightly (only excludes local build trees + the one gitignored
   bootstrapped third-party dir).
5. `build-nogfx/` was newly created in this worktree (none existed) to run
   the headless-tree verification step; left in place (gitignored, matches
   the persistent `build/`/`build-gles2/` pattern).

### Final commit

See commit on branch `task/P31-phase4-5` (hash reported in the executor's
final message).
