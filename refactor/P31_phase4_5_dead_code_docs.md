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

(executor fills in: per-deletion table [item, gate that made it dead,
files touched], Dockerfile decision + evidence, doc-sweep list, all
verification numbers, deviations)
