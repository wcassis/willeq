# G06 — `updatePlayerMovement`: the 806-line per-frame movement body becomes phases over a `MovementFrame`

Batch: `refactor/G_oversize_methods.md` (corrections 1–17). **Runs ALONE
after G05 merges** (same component neighbourhood; the first-person camera
guards). Measured at `ad97818d`: `PlayerCameraComponent::updatePlayerMovement`
in `src/client/graphics/player/renderer_player_physics.cpp` (starts ~line
32, ~806 lines). Re-measure mechanically: the phases the body's own
banners delimit (input read → intent → collision → water/swim → ground
snap → first-person camera → position publish/send), and every local that
crosses a phase.

## Shape

1. A private `struct MovementFrame` holds the cross-phase locals (dt,
   input state, desired delta, collision results, ground z, …) — declared
   in the component header (or a private nested struct — note K05 j
   consequences for the text guards). Each phase becomes
   `void PlayerCameraComponent::phaseX(MovementFrame&)`, verbatim slice;
   `updatePlayerMovement` becomes the ordered phase list. Early
   `return`s inside a phase become a `bool` phase result the list honours
   (record each; STOP if a `return` skipped later phases in a way a bool
   cannot reproduce exactly).
2. ⚠ The guards: `tests/test_first_person_camera_single_formula.cpp`
   (Part 1 greps the graphics tree for the first-person formula spelling;
   Part 3 locates a body by name — L05 widened both), `test_first_person_body_draw_rule`,
   `test_entity_collision_z_offset`, `test_bridge_queue_race` name this TU
   or method — read every one first; widen + re-prove with planted breaks
   (correction 21); assertions untouched. ⚠ The T234/T237 transform tests
   pin movement against the official client — they must pass unchanged.
3. Order test as G04/G05: golden of the phase sequence, size asserted,
   planted swap/drop in code.
4. Proof: line multiset; **the visual gate standalone** (first-person
   goldens) is the behavioural proof; plus the T234/T237 suites.

Predicted: `updatePlayerMovement` 806 → ≈ 30–40 lines; the header gains
≈ 8 declarations + the struct.

STOP: a cross-phase local with a lifetime change; an early return a bool
cannot reproduce; any guard assertion; a golden diff.

Territory: `src/client/graphics/player/renderer_player_physics.cpp`,
`include/client/graphics/player/player_camera_component.h`, the new test
+ golden + cmake, the guard tests (widening only), `docs/module_map.md`.
Visual gate + ARM NOT skippable.
