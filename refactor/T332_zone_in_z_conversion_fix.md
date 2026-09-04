# T332 — Fix the login z-compounding on the graphical path (B049, writer named by T331)

Class A. Slot1. `bridge_component.cpp` only touches the bridge family —
disjoint from G05 (`graphics/frame/`).

## The defect (T331, `review/T331/README.md`)

`BridgeComponent::OnGraphicsMovement` (`src/client/bridge/bridge_component.cpp`,
~line 717) clears `player.zAwaitingFrameConversion` on the premise that a
z coming from the renderer is already in the render frame. At zone-in
that is false: the renderer is SEEDED with the raw wire z
(`setPlayerPosition` from the zone-load snapshot, and again after the
model loads) and echoes it back through `PlayerPositionChanged`. The
correct deferred conversion (`bridge_component_intents.cpp:132-146`,
`wire → render: z − 0.625·size`) never runs, so the next
`SendPositionUpdate` re-adds `+0.625·size` to a z that was never
converted. Headless there is no renderer echo, so the flag survives and
the conversion runs — which is why T311's three-login test is green and
wrong.

## The fix (smallest correct change; measure the alternatives in the Review)

Preferred: `OnGraphicsMovement` must NOT clear the flag when the
renderer's z is the seeded wire z. Discriminate by provenance, not by
value: the seed path (`setPlayerPosition` from the snapshot) marks the
renderer-side position as "seeded from wire" (a bool alongside the
position in the intent, or a generation counter the snapshot bumps), and
`OnGraphicsMovement` runs the conversion (the same code the intents path
has at :132-146 — factor it into one function called from both) when the
flag is set AND the echo is the seed. After the first genuine renderer
movement (physics ran), the flag is cleared as today. Alternative, if
provenance cannot be carried: run the conversion in `OnGraphicsMovement`
whenever the flag is set (never just clear it) — say why the preferred
form was or was not possible.

## The regression test — MUST have a renderer present

`tests/test_zone_in_z_conversion.cpp`: construct the graphical harness
the existing renderer tests use (`test_position_frame_roundtrip.cpp` is
the closest — read it), seed a zone-in position with a known wire z for a
size-6 race, let the renderer echo it (drive one `PlayerPositionChanged`
through the bridge with the seeded value), then read the z the next
`SendPositionUpdate` would pack (the T234/T237 transform seam) and assert
it equals the wire z exactly (no +0.625). Second case: after a genuine
movement (a different z from the renderer), the flag is clear and the
packed z is `renderZ + 0.625·size`. Third: three consecutive zone-ins in
one process do not accumulate. **Prove the test catches the bug:** run it
against the pre-fix code first (`git stash` is forbidden — export a
patch, apply -R, run, re-apply) and paste the failing output.

Also: T311's headless three-login test stays; annotate it as the
headless arm.

## Live proof (owed unless the lock is free — it is, T331 released it)

Three graphical login/camp cycles of `Testchar` exactly as T331 ran them
(reuse `review/T331/` scripts); `character_data.z` must read 3.751 after
each. Table to `review/T332/README.md`. Restore the character as found;
`down.sh`; delete the lock.

## Territory

`src/client/bridge/bridge_component.cpp`, `src/client/bridge/bridge_component_intents.cpp`
(the factored conversion), `include/client/bridge/bridge_component.h`,
`include/client/events/renderer_intents.h` (only if provenance is carried
in the intent — one field), the renderer's seed call site
(`src/client/graphics/zone_load/…` or `application_zone_loading.cpp` —
one line to mark the seed; if it is under `src/client/graphics`, the
visual gate + ARM run), the new test + cmake, `refactor/BOARD.md` (strike
B049 with the live table), `review/T332/`.

STOP: the fix needs a thread-ownership change; the pre-fix test does not
fail (then the test is wrong, not the fix).
