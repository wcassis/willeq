# T275 — UIRenderer::drawRect() draws nothing visible in the gem panel (T272 finding)

Slot: slot2. x86. OWNS eqemu only if a live capture is needed (the static
UI panels may be reachable pre-connection — try that first; noclobber if
not). Class A/B. Territory: src/client/graphics/ui/ui_renderer.*,
ui_atlas.*, the gem-panel draw path in static_panels.*, tests. Do NOT
touch race data/getters (T274 slot1), read-book (T273 slot3).

## Finding (T272 Review, review/T272/t272_5_gemcolumn_zoom.png)
Neither T272's new gold destination marker nor the PRE-EXISTING
Ready-state green borders appear in a real capture, though both use the
same `UIRenderer::drawRect()` idiom that other panels apparently rely on.
So either drawRect is a no-op in this configuration, its geometry lands
off-panel, or it is drawn and then overdrawn.

## Scope
1. Determine which: instrument the call (log rect + clip + the texture/
   atlas state), capture a frame with `--dump-frames-dir` and
   `--eqe1-path`, and check the pixels at the logged rect. Establish
   whether ANY drawRect anywhere is visible (grep its call sites — if some
   are visible and some not, the difference is the finding).
2. Fix the cause (missing atlas binding, wrong blend/depth state for
   untextured quads, coordinate space, draw order vs the panel background,
   or an early-out). T205's UI-atlas history and T193/T204's texture
   lifetime notes are the nearest prior art.
3. Test: a static-panel pixel test (T54 added the first ones — see
   tests/ for the panel harness) asserting the border pixels are the
   expected colour after a draw. Red/green measured.
4. Capture before/after crops for Will.

## Verification
Full ctest; visual gate PASS with goldens untouched (UI is not in the
goldens — confirm); Review; evidence review/T275/. Commit:
`T275: UIRenderer rectangles are visible again -- <cause>`.
