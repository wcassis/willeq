# H02 — CI gates: golden frames, metric assertions, memory ceilings

Build the automated visual/performance/memory verification gate on top of
H01's x86 GLES2 build (`build-gles2/`, real `COpenGLES2Driver` under
llvmpipe via EGL surfaceless — no Xvfb even needed for the benchmark path;
verify whether the benchmark still wants an X display or runs fully
headless with the surfaceless fallback, and prefer fully headless).

## Design

1. **Golden set**: 3 zones × ~3 designated frames each along the fixed
   camera path, chosen for coverage: `qeynos2` (city/outdoor, proven in
   H01), `gfaydark` (forest/organic), and one dungeon interior (pick
   `blackburrow` or similar — confirm asset presence). Use the benchmark's
   `--dump-png` (H01) extended if needed to dump specific frame indices
   (e.g. `--dump-frames 0,30,59`).
2. **Golden storage**: `tests/goldens/<zone>_f<N>.png`, committed to the
   repo. Alongside: `tests/goldens/manifest.json` recording zone, frame
   index, benchmark flags, resolution, and the mesa/llvmpipe version string
   (from `glGetString(GL_VERSION)`/`GL_RENDERER`) the goldens were blessed
   under.
3. **Comparison**: decoded-pixel comparison, not PNG byte compare (encoder
   drift immunity). Python3 + PIL if available, else ImageMagick `compare
   -metric AE`; exact-zero-diff is the pass bar (llvmpipe is
   deterministic). On mismatch, write a diff image + both frames to an
   artifacts dir and fail with a clear message including the mesa version
   check (if mesa version != manifest version, say "mesa upgraded —
   re-bless" instead of "regression").
4. **Metric assertions**: from the benchmark CSV per zone —
   - exact-match assertions: total draw calls, state changes, polygon
     counts per designated frame (deterministic on a fixed path);
   - threshold assertions: p95 CPU frame ms ≤ recorded baseline × 2.0
     (llvmpipe timing varies with load; generous ceiling catches
     blocking-the-render-loop regressions, not noise).
   Baselines live in the same manifest.
5. **Memory assertions**: ConstrainedTextureCache / mesh cache byte totals
   after zone load (the benchmark logs/exposes these — extend its CSV or
   summary output minimally if needed) asserted ≤ the constrained preset's
   budget, and exact-match against manifest (deterministic loads).
6. **Entry points**:
   - `scripts/ci/visual_gate.sh` — runs all zones, all assertions; exit
     nonzero on any failure. `--bless` regenerates goldens + manifest.
   - Register a ctest entry (`visual_gate`) that invokes the script and
     SKIPs when `build-gles2/` is absent, so the default suite stays
     independent.
7. **Contact sheet**: `--bless` also emits
   `tests/goldens/contact_sheet.html` — all goldens embedded (base64 data
   URIs), labeled with zone/frame/what-to-check. The orchestrator publishes
   this for Will's blessing; do not mark anything "approved" yourself.

## Verification

- `scripts/ci/visual_gate.sh --bless` then `scripts/ci/visual_gate.sh`:
  bless → verify passes end-to-end on this VM.
- Deliberately perturb one golden (temporarily) → gate FAILS with diff
  artifacts → restore. Document this negative test in the review.
- Default build + full ctest unregressed; new ctest entry SKIPs cleanly
  when build-gles2 is absent.

## Review

(executor fills in: zone/frame choices with rationale, headless findings,
comparison mechanism, baseline numbers, negative-test evidence, deviations)
