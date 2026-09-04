# G05 — `processFrameRender`: the render passes become one private pass per method

Batch: `refactor/G_oversize_methods.md` (corrections 1–17). **Dispatch
after L13 merges** (L13 rewrites receivers inside `src/client/graphics/frame/`);
two-wide with G04 is fine once it does. Measured at `dfa3738f`:
`FrameComponent::processFrameRender` in `src/client/graphics/frame/renderer_frame_render.cpp`
(~546 lines, sequential render passes with `measureSection` timing
brackets). Re-measure mechanically: list the passes the timing brackets
and banners already delimit.

## Shape

1. Each pass becomes `void FrameComponent::renderX()` (sky, zone geometry,
   entities, effects, overlays, HUD, RDP capture, …) — verbatim slice
   INCLUDING its `measureSection` bracket, so the timing categories are
   unchanged; `processFrameRender` becomes the ordered list. Locals that
   cross passes (the frame's camera/frustum/visibility results) become a
   `FrameRenderState` struct passed by reference or members of the
   component — record each (STOP on a lifetime change; STOP on anything
   that touches the render/game thread boundary).
2. Order is load-bearing (depth, blending, overlay-on-top): an order test
   parses the TU for the pass-call sequence against a golden extracted
   BEFORE the change; size asserted; faults planted in code (swap two
   pass calls; drop one).
3. ⚠ Text readers: `tests/test_ui_hide_command.cpp` and
   `tests/test_static_ui_scroll.cpp` read this TU by path and locate
   bodies by spelling (L11 widened them) — re-read them; if they locate
   `processFrameRender`'s body or a pass inside it, widen + re-prove
   (correction 21); `test_debug_hotkey_arms` reads the sibling input TU.
4. Proof: line multiset; the driving tests pass with assertions
   untouched; **the visual gate standalone is the behavioural proof**
   (goldens untouched).

Predicted: `processFrameRender` 546 → ≈ 25–35 lines; `frame_component.h`
gains ≈ 12 declarations.

STOP: a cross-pass local whose lifetime changes; a thread-boundary
change; the visual gate diffs a golden; a pinned test's assertion.

Territory: `src/client/graphics/frame/renderer_frame_render.cpp`,
`include/client/graphics/frame/frame_component.h`, the new test + golden
+ cmake, the two text-reader tests (widening only), `docs/module_map.md`.
Graphics files change: visual gate + ARM NOT skippable.
