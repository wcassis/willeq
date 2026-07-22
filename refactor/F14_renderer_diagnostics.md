# F14 — Split RDP and diagnostics out of irrlicht_renderer.cpp

First renderer-side unit: lift the two cleanest clusters out of
`src/client/graphics/irrlicht_renderer.cpp` (16,394 lines). Pure move — no
behavior change, no interface change, no reformatting of moved code.

## Scope (line refs @ commit a0c02cd — irrlicht_renderer.cpp untouched since;
verify by method name)

Two new files under `src/client/graphics/`:

**renderer_rdp.cpp** — the RDP remote-desktop cluster (~15726–15936, ~210
lines): `initRDP`, `startRDPServer`, `stopRDPServer`, `captureFrameForRDP`,
`handleRDPKeyboard`, `handleRDPMouse` (confirm exact set by grep — every
`IrrlichtRenderer::` method in that range).

**renderer_diagnostics.cpp** — the diagnostics clusters:
- dumpScene / runSceneProfile / profileSceneBreakdown / measureSection
  (~9055–9341, ~285 lines)
- frame-timing profiling: logFrameTimings and neighbors (~11533–11834, ~300)
- `getMemoryReport` (~15365–15725, ~360)

## Steps

1. Locate exact method boundaries by grep (`IrrlichtRenderer::` definitions
   in the listed ranges). List the full method set in the review.
2. Watch for `#ifdef` guards spanning the clusters (RDP code is likely inside
   an RDP feature guard) — preserve guards exactly in the new files.
3. Create both files: brief header comment + include block (start from
   irrlicht_renderer.cpp's includes, prune compiler-driven).
4. File-local `static` helpers / anonymous-namespace symbols referenced by
   moved code: move if used only by moved code; otherwise leave and document
   (de-static via a shared internal header only if unavoidable).
5. Add both files to the same source list as irrlicht_renderer.cpp in
   CMakeLists.txt (respect any graphics/RDP conditional grouping).

## Verification

- `cmake --build build -j24` clean.
- `ctest --test-dir build` — fully green. NOTE: this unit touches RDP code and
  RDPServerFrameTest.UpdateFrameMultipleTimes is a known rare flake
  (pre-existing segfault under parallel load). If it fails, re-run it in
  isolation several times; it must pass consistently in isolation.
- `python3 scripts/refactor/verify_move_only.py fable..HEAD` — PASS, or
  substantive lines limited to documented de-static-ing.
- `bash scripts/refactor/check_file_sizes.sh --files src/client/graphics/renderer_rdp.cpp src/client/graphics/renderer_diagnostics.cpp` — ok.
- irrlicht_renderer.cpp shrinks by ~1,100+ lines.

## Review

(executor fills in)
