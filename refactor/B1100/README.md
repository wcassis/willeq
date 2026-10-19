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

The frame passes the event receiver's physical held-button state to
`StaticUIInput`. A track click remains an immediate jump to its pointer step.
The three legacy sliders use the same control-kind path; no per-slider gesture
state is added.

## Clip cap display

The requested 0..20 slider step remains persisted as set. The apply arm asks
the renderer for its effective render distance after `setRenderDistance()` and
feeds that into `OptionsPanelState` for the value label. The label prints the
effective world-unit distance with ` (max)` only when it is below the
requested distance. The renderer logs a clamp once when the requested/capped
pair or cap source changes, naming `preset` or `zone`.

## Not verified

No live SBC/device gesture or visual appearance is claimed by this offline
card. Device-free input and label tests, remote builds, hygiene, and CI are
the verification bar.

## CLAUDE.md EDITS

The card's protected-file rule forbids editing any `CLAUDE.md`; the proposed
Options paragraph sentence is: "The Music, Sound and Clip Plane sliders drag
while held; a capped clip plane shows its effective distance as `300 (max)`."
