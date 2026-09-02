# T313 — The static UI has no scroll input at all: chat, skills and loot are pinned to page one

**Slot: slot3. Branch `task/T313` cut from `fable`. x86 only. No Pi.
One live session for the acceptance leg.**

Closes **tier 2 item 12** of `refactor/deferred.md`'s "THE ONE CLASS-A LIST".

## The defect

Three panels carry a `scrollOffset` that **both render and hit-test honour**, and
**nothing but a clamp or a reset ever writes one**:

| panel | field | honoured at | only writes |
|---|---|---|---|
| chat | `include/client/graphics/ui/static_panels.h:67` | `static_panels.cpp:173,199` | `:169-170` clamp |
| skills | `:340` | `static_panels.cpp:846`, `static_ui_input.cpp:269` | — |
| loot | `:386` (+ `maxScrollOffset()` at `:390`) | `loot_panel.cpp:67`, `static_ui_input.cpp:233` | `loot_panel.cpp:192` reset, `:233` clamp |

`StaticUIInput` (`include/client/graphics/ui/static_ui_input.h`) has **no wheel
handler at all** — `processInput()` takes only `mouseX/mouseY/leftClicked` — and
no PageUp/PageDown binding exists (`hotkey_manager.cpp:457` binds `KEY_NEXT` to
StrafeRight).

*Impact:* chat scrollback is unreachable, the skills window shows only its first
page, and **a corpse with more items than fit is partly unlootable**.

## The shape of the fix — one input path, not three

This is the whole point of the unit. The wheel has to arrive once, be routed to
whichever panel is under the cursor (or focused), and move that panel's existing
`scrollOffset` within its existing clamp. Do not add three bespoke handlers, and
do not invent a second scrolling model — the clamps and the `maxScrollOffset()`
helper already define the semantics; you are supplying the missing writer.

1. **Get the wheel event to `StaticUIInput`.** Irrlicht delivers it as
   `EMIE_MOUSE_WHEEL` in `RendererEventReceiver::OnEvent`. There is an existing
   accumulator convention right there for exactly this shape —
   `getCameraZoomDelta()` / `getMusicVolumeDelta()` (`irrlicht_renderer.h:396-399`)
   are read-and-reset floats. Follow it rather than inventing one, and make sure
   a UI-consumed wheel does NOT also zoom the camera (that is a live regression
   if you get it wrong — check what currently consumes the wheel BEFORE you
   change routing, and say what you found).
2. **Route by cursor position**, using the same hit-testing the panels already do.
3. **Also bind PageUp/PageDown** for the focused panel — a keyboard path matters
   because `--script` runs and headless harnesses cannot generate a wheel, which
   is the same gap `/hideui` closed for the UI-hide feature in T301 Leg D. If a
   command is the cheaper reachable path, add one and say why.
4. **Chat's offset is measured from the BOTTOM** (`:67`: "0 = at bottom") and the
   other two from the top. Respect each panel's own convention; do not harmonise
   them.

⚠ **Slot2 is in `RendererEventReceiver` this dispatch (T312 — the bridgeQueue_
thread-safety unit) and will be editing `irrlicht_renderer.h`/`.cpp` and
`renderer_frame.cpp`.** Your step 1 touches the same header. Keep your edit
there to the minimum (the accumulator + its accessor), do it in ONE small hunk,
and flag it in your Review as sibling territory so the orchestrator can order
the merges. Everything else lives in `src/client/graphics/ui/`.

## Acceptance

- **A test per panel** that scrolls past the first page and asserts the rendered
  row set changed — the panels are testable headlessly (`tests/test_ui_atlas_pixels.cpp`
  and the T290 loot tests are the precedent; read them first).
- **Live, with a real wheel**: `xdotool` click 4/5 over each panel. The loot panel
  is the one that matters most — put more items on a corpse than fit, scroll, and
  loot a row that was **not** reachable before this unit. That is the user-visible
  proof; a screenshot pair goes to `/mnt/projects/fable/review/T313/` with a README.
- **Full `ctest -j6`** (NOT -j24), zero new failures vs `refactor/test_baseline.txt`.
- If a golden re-bless looks necessary, **STOP and report** — do not re-bless.
