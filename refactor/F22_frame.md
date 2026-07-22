# F22 — Split processFrame and its sub-passes into renderer_frame.cpp

Move the per-frame spine out of `src/client/graphics/irrlicht_renderer.cpp`
(7,596 lines). This is the main render-loop cluster — HIGH sensitivity, but
still a pure TU move. No behavior change, no interface change, no
reformatting.

## Scope (orig 9342–10900 @ a0c02cd; heavily shifted. Enumerate by NAME.)

Move to new `src/client/graphics/renderer_frame.cpp` the contiguous
frame-processing cluster: `processFrame` (~495 lines) and its sub-pass
methods — the `processFrame*` family (input, common input, player input,
input deltas, chat input, visibility, simulation, render — enumerate the
actual set by grep for `processFrame` and read the cluster boundaries), plus
the render-pass body methods in the same contiguous region.

In-range non-frame methods (judgment): leave + document. `run()` and
`saveScreenshot()` were explicitly excluded from F15 and remain out of scope
here too UNLESS `run()` is merely a thin wrapper that sits contiguously and
is inseparable — default: leave `run()` in irrlicht_renderer.cpp.

## Steps

1. Locate the cluster; produce the mapping in the review BEFORE moving.
2. Replicate namespace wrapper + include patterns (F14–F21 reviews; expect
   forward-declaration include gaps like F19/F20 hit).
3. Move verbatim; preserve #ifdef guards exactly.
4. File-local statics: usual rule.
5. Add to `GRAPHICS_SOURCES` in CMakeLists.txt.

## Verification

- `cmake --build build -j24` clean.
- `ctest --test-dir build` — green modulo documented exceptions in
  refactor/test_baseline.txt (ALSA expected-fail; RDP flake → re-run
  isolated, and note it reproduces on unmodified fable per deferred.md).
- `python3 scripts/refactor/verify_move_only.py BASE..HEAD` — PASS or
  documented deviations only.
- `bash scripts/refactor/check_file_sizes.sh --files <abs>/renderer_frame.cpp`
  — WARN accepted, must not FAIL.
- irrlicht_renderer.cpp shrinks by ~1,000+ lines.

## Review

(executor fills in)
