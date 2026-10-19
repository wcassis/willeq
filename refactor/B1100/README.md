# B1100 — Options slider drag

## Gesture design

`OptionsPanelState` owns the armed slider control index. A press edge on an
enabled slider track or knob resolves its initial step using the existing
`resolveOptionsClick()` path and arms that control. Each later frame with the
left button held resolves the current pointer X against that *same* control,
even when the pointer leaves its rectangle. `setOptionsValue()` reports only
real step transitions, so `FrameComponent::applyOptionsSetting()` receives one
change per crossed step. Release, window closure, or a new non-slider press
disarms it. Other controls keep their press-edge behaviour. The frame still
reports the initial press as UI-owned to `PressOwnership`, which holds that
owner until release; a slider drag cannot turn into camera look.

The input frame samples the event receiver's physical held-button state into
`OptionsPanelState` before `StaticUIInput` runs. A track click remains an
immediate jump to its pointer step.
The three legacy sliders use the same control-kind path; no per-slider gesture
state is added.

## Clip cap display

The requested 0..20 slider step remains persisted as set. The apply path asks
the renderer for its effective render distance each UI frame and after
`setRenderDistance()`, feeding `OptionsPanelState` for the value label. It prints the
effective world-unit distance with ` (max)` only when it is below the
requested distance. The renderer logs a clamp once when the requested or
effective value changes, naming `preset` or `zone`.

## Not verified

No live SBC/device gesture or visual appearance is claimed by this offline
card. Device-free input and label tests, remote builds, hygiene, and CI are
the verification bar.

## Verification

- The new `OptionsWindowB1100.HeldClipDragAppliesEachNewStep` failed before
  the input change: the clip step remained 5 on held motion to steps 6 and 7,
  and `changed` remained `None`. The actual remote-test output is retained at
  `/home/user/kb-evidence/kb-b1100-options-slider-drag/negative-drag.log`.
- The rebuilt `test_options_window` target passed `rbuild ... test -R 'Options'`,
  including the new release, outside-window drag, music/sound and
  cap-label checks.
- `rbuild ... hygiene` passed; its environment skips are retained as skips.
- The first MR pipeline's `build-headless` job failed
  `FrameRenderOrder.ComposedDrawOrderMatchesThePreG05Golden` because the initial
  implementation added bookkeeping to `renderer_frame_render.cpp`. The trace
  is retained in `ci-headless-failure.log` beside the negative log. The
  bookkeeping moved into `renderer_frame_input.cpp` and the existing
  `applyOptionsSetting()` call, leaving the golden-covered render TU unchanged.
  The rebuilt `test_frame_render_order` and Options tests pass together via
  `rbuild ... test -R 'Options|FrameRenderOrder'`; the golden was not edited.

## CLAUDE.md EDITS

The card's protected-file rule forbids editing any `CLAUDE.md`; the proposed
Options paragraph sentence is: "The Music, Sound and Clip Plane sliders drag
while held; a capped clip plane shows its effective distance as `300 (max)`."
