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

## Review

Executed 2026-09-04 in `wt/slot2`, branch `task/G06-update-player-movement` off
`fable` at `8bde97cb`. Structural, move-only, zero behaviour change.
**No STOP condition hit.**

### Correction 22 first: the exit census, with loop depth

Every `return`/`continue`/`break` between the signature (line 32) and the
closing brace (line 837) of the pre-change body, enumerated mechanically with
its enclosing brace depth:

| line | depth | exit | belongs to |
|---|---|---|---|
| 42 | 2 | `if (chatHasFocus) return false;` | the `isActionHeld` lambda (declared 41) |
| 48 | 3 | `if (!ctx_.eventReceiver->isKeyDown(binding.keyCode)) continue;` | the lambda's `for` at 47, loop depth 1 |
| 55 | 4 | `return true;` | the lambda |
| 58 | 2 | `return false;` | the lambda |

**All four exits belong to the lambda; `updatePlayerMovement` itself has ZERO
exits.** The body runs top to bottom, unconditionally, every frame. So it is a
PIPELINE, not G07's search: **no phase carries a `bool` result, no early return
had to be reproduced, and the plan's "STOP if a `return` skipped later phases in
a way a bool cannot reproduce exactly" is discharged vacuously** — there is no
such return. The only other control flow that could have constrained a cut is
the pair of `while` normalisation loops at 154-155, both wholly inside phase 2.

### Re-measurement at the tip (correction 1: mechanical, not the plan's numbers)

At `8bde97cb`, `PlayerCameraComponent::updatePlayerMovement` spans **lines
32-837 of an 840-line `src/client/graphics/player/renderer_player_physics.cpp`**
— signature 32, closing brace 837, body **804 lines**. The plan said ~806 (the
figure the file's own header comment carried); it is 806 counting the signature
and closing brace, 804 of body.

### The cut: fourteen bodies at the file's own blank-line seams

This file carries no timing brackets (G05's rule 24 does not apply) and no
banner block structure (G04's rule 18 does not fit either — its `//` comments
annotate individual statements). **Its seams are blank lines between top-level
statement regions**, and every one of the eight top-level cuts lands on one:
97/99, 158/160, 214/216, 230/232, 700/702, 746/748, 803/805. Phase 5's three
sub-cuts are the file's own `else if` arms (265 / 511 / 667), and phase 5a's two
are its own `if (!blocked)` / `} else {` at 314 / 409.

| # | body | orig lines | new len | what it is |
|---|---|---|---|---|
| — | `updatePlayerMovement` (driver) | 32-837 | **14** | the ordered list |
| 1 | `readMovementInput` | 33-97 | 70 | chat focus, hotkey read, movement-state write, jump initiation |
| 2 | `updateHeadingAndPitch` | 99-158 | 70 | turning, mouse-look, the pitch clamps (incl. the first-person floor), `headingRad` |
| 3 | `computeMovementVector` | 160-214 | 62 | swim vs ground `moveX/Y/Z` |
| 4 | `applyProposedPositionAndJumpPhysics` | 216-230 | 25 | `newX/Y/Z` and the gravity integration |
| 5 | `resolveMovementCollision` | 232-700 | 50 | the `isMoving` gate, system selection, `modelYOffset`, and the three-way dispatch |
| 5a | `resolveIrrlichtCollision` | 266-510 | 58 | the horizontal ray, its debug lines, the clear/blocked split |
| 5b | `resolveIrrlichtClearPath` | 315-408 | 102 | land / swim ceiling+floor / step height |
| 5c | `resolveIrrlichtBlockedPath` | 410-509 | 111 | vertical-only jump/swim, X-then-Y wall slide |
| 5d | `resolveHCMapCollision` | 512-666 | 163 | the whole server-map-file path |
| 5e | `resolveUncollidedMovement` | 668-698 | 38 | no map / collision disabled |
| 6 | `publishPositionAndCamera` | 702-746 | 57 | `headingChanged`, the `ctx_.player*` writes, entity position, follow + first-person camera |
| 7 | `sendPositionUpdateToServer` | 748-803 | 63 | stop detection, the 250 ms throttle, the `PlayerPositionChanged` intent |
| 8 | `updatePlayerMovementAnimation` | 805-836 | 36 | jump / swim / walk / run / idle animation selection |

- **`updatePlayerMovement`: 806 lines -> 14** (a comment, the frame, one
  assignment, eight calls, a blank, braces). Predicted 30-40: **beaten**, because
  no phase needed a `bool` result and no early return had to be honoured.
- `renderer_player_physics.cpp`: 840 -> **990** (+150, every line attributed
  below). `player_camera_component.h`: 229 -> **294** (+65: the 17-field
  `MovementFrame`, 13 declarations, and their comment blocks). Predicted
  "≈ 8 declarations + the struct" — **13**, because phase 5 dispatches the
  file's three collision systems and the Irrlicht one splits again at its own
  `if (!blocked)`.
- `check_file_sizes.sh --all`: **866 ok, 0 warn, 0 fail.**

### Cross-phase locals — the locals table, and why there IS a struct this time

Every declaration in the pre-change body was enumerated with its declaration
line and the line of its last use, **before** the struct was designed
(corrections 19/25). Unlike G04 (0 of 13) and G05 (0 of ~50), this body has real
crossers, and exactly seventeen values cross a cut:

| local | declared | last use | crosses |
|---|---|---|---|
| `turnLeft`, `turnRight` | 63, 64 | 106 | 1 → 2 |
| `heading` | 100 | 782 | 2 → 3, 6, 7 |
| `oldHeading` | 101 | 703 | 2 → 6 |
| `oldPitch` | 122 | 717 | 2 → 6 |
| `headingRad` | 158 | 210 | 2 → 3 |
| `moveX`, `moveY`, `moveZ` | 161 | 241 | 3 → 4, 5 |
| `newX`, `newY`, `newZ` | 217-219 | 707-709 | 4 → 5, 5a-e, 6 |
| `positionChanged` | 233 | 771 | 5 → 5a-e, 6, 7 |
| `modelYOffset` | 260 | 645 | 5 → 5a-e |
| `headingChanged` | 703 | 771 | 6 → 7 |
| `hasMovementInput` | 750 | 824 | 7 → 8 |
| `deltaTime` (the parameter) | — | 730 | driver → 2, 4, 5, 6 |

Those seventeen, and **only** those, are the fields of the private nested
`PlayerCameraComponent::MovementFrame`. Everything else stays inside the phase
that declares it — including the names that a naive `\b` grep reports as
crossers and are not: `speed`, `groundZ`, `feetZ`, `currentFeetZ`, `stepHeight`,
`headHeight`, `headStart`/`headEnd`/`ceilingHit`/`maxZ`, `losCheck` and
`ctrlHeld` are each **re-declared** in several sibling scopes, and `strafeLeft`/
`strafeRight`'s apparent use at line 751 is the *member* `playerMovement_.strafeLeft`,
not the local. **No local's lifetime changed**: every field is written by the
phase that used to declare it, before any later phase reads it, in the same
statement order as before — which the order golden proves position by position.

`useBspFiltered`, `checkHeight`, `rayStart`, `hitPoint` and `hitTriangle` cross
only phase 5a's *internal* cut, so they are **parameters of 5b/5c**, not frame
fields — the same choice G05 made for its `float deltaTime` passes. The struct
carries frame state; it does not become a bag for collision scratch.

**How the slices stay verbatim.** Each phase opens by re-binding the frame
fields it needs to references *under their original names*
(`float& newX = frame.newX;`). The 804 sliced lines are then byte-identical to
the pre-change body except for **fourteen declaration lines whose type-specifier
moved into the struct** (`float heading = ctx_.playerHeading;` → `heading = ctx_.playerHeading;`),
each of which is listed individually in the multiset below.

**No thread-boundary change.** All thirteen phases are private and called only
(transitively) from `updatePlayerMovement()`, which runs on the render thread as
it always did. The three **function-local statics** in phase 7
(`hadMovementInput`, `prevX/prevY/prevZ`, `lastCallbackTime`) moved with their
code into `sendPositionUpdateToServer()`, which is called exactly once per
`updatePlayerMovement()` call, unconditionally, at the same point in the order —
so first-call initialisation and per-process sharing are unchanged. The single
`ctx_.bridge->pushIntent()` is still the only cross-thread write.

**T332 was not touched.** Nothing in this TU converts a seeded wire z; the only
z arithmetic here is `groundZ + modelYOffset` ground snapping, which is
unchanged line for line.

### Proof (a): the line multiset

`git show fable:...renderer_player_physics.cpp` vs the new file, every line
`strip()`ed, `Counter` difference both ways:

```
REMOVED: 22 lines
    8  file-header comment lines: the "updatePlayerMovement() is 806 lines /
       673 lines of ceiling headroom" note and the DO-NOT-MOVE block, rewritten
       in place for the new shape and the widened guard
   14  declaration lines whose STORAGE moved into MovementFrame; each reappears
       verbatim minus its type-specifier, in the same phase, at the same point:
         bool turnLeft = ... / bool turnRight = ...
         float heading = ... / float oldHeading = ... / float oldPitch = ...
         float headingRad = ... / float moveX = 0.0f, moveY = ..., moveZ = ...
         float newX = ... / float newY = ... / float newZ = ...
         bool positionChanged = false / float modelYOffset = 0.0f
         bool headingChanged = ... / bool hasMovementInput = ...
ADDED:   172 lines  (bucketed mechanically; nothing left over)
   61  reference re-bindings `<type>[&] X = frame.X;` across the 13 phases
   33  comment lines: the rewritten file-header block + one banner per phase
   20  blank separators
   14  the de-typed declarations (each pairing with a REMOVED line above)
   13  the 13 new phase signatures (first line)
   13  }                                       (one per new body)
   13  phase and sub-phase calls (8 in the driver, 3 in phase 5, 2 in phase 5a)
    3  phase 5c's signature continuation lines
    2  the driver's `MovementFrame frame;` / `frame.deltaTime = deltaTime;`
```

**Zero CODE lines removed.** Every one of the 804 original body lines is present
exactly once — 790 byte-identical, 14 with only their leading type-specifier
gone. Residual outside those buckets: **0**.

### Proof (b): the order golden and the eight planted faults

`tests/goldens/movement_order_g06.txt`: **471 entries**,
`<ordinal> <normalised source line>`, extracted mechanically from
`git show fable:src/client/graphics/player/renderer_player_physics.cpp` **before
a line of the TU was edited**.

Normalisation (mirrored exactly in the test, and stated in its header comment):
strip whitespace; drop blank, `//`-comment, preprocessor and brace-only lines;
drop the split's own plumbing (`MovementFrame frame;`,
`frame.deltaTime = deltaTime;`, and a re-binding `<type>[&] X = frame.X;` **whose
alias name equals the field name** — a binding that renames a field is *not*
dropped and shows as a diff); and drop a leading `float `/`bool ` from a
declaration **of a MovementFrame field only**, which is the one difference this
golden deliberately does not police. The field list is parsed out of the header
at runtime, not hard-coded, so adding or dropping a field moves it.

**Why a text parse and not a runtime observation** (G03 correction 9): there is
no container, and `updatePlayerMovement()` cannot be run in a unit test — it
needs a live Irrlicht device, an event receiver with real key state, a loaded
zone with collision geometry or an HCMap, an entity renderer and a bridge. The
TU **is** the order.

`tests/test_movement_order.cpp` (registered in `tests/cmake/core_utils.cmake`
beside G05's; no link deps, `WILLEQ_REPO_ROOT` is global) parses every
`void|bool PlayerCameraComponent::<name>(` at column 0 — collecting the body
only *after* the opening brace, because 5c's signature spans four lines — then
**composes**: walk `updatePlayerMovement()`'s body in order; a statement that is
a call to a method defined in this TU expands *recursively*; every other line is
emitted as itself. Four cases:

1. `GoldenIsPresentAndTheRightSize` — non-empty and **== 471**, in its own case
   (correction 5 / K11 ee).
2. `EveryPhaseIsCalledExactlyOnceAndNoneIsOrphaned` — the driver's eight
   `X(frame);` calls equal `kPhases` exactly, in order; and every other
   definition in the TU is called from some other body.
3. `UpdatePlayerMovementIsOnlyTheOrderedList` — the anti-regrowth clause (G04
   correction 16): the driver may contain only the frame plumbing and phase
   calls, and fewer than 20 statements.
4. `ComposedMovementOrderMatchesThePreG06Golden` — position by position, with
   three statements of context either side of a divergence.

**Eight plants, seven in the CODE plus the empty-golden trap, eight catches;
TU, golden and guard byte-identical afterwards (verified by `diff` after every
plant):**

| plant | outcome |
|---|---|
| **the call-order fault** (correction 12) — swap `publishPositionAndCamera` ⇄ `sendPositionUpdateToServer` in the driver; no phase body edited. The real regression: the server would be told the *previous* frame's position and the throttle would key off a stale `headingChanged` | **FAILS** ×2: `no longer calls the eight movement phases in order`, `movement order diverges at statement 405` |
| **drop a phase call** — delete `computeMovementVector(frame);` | **FAILS** ×3: the phase-list equality, `computeMovementVector() is defined ... but is never called from another body`, `diverges at statement 64` |
| **delete a real statement** inside a phase — `playerMovement_.turnLeft = turnLeft;` | **FAILS**: `diverges at statement 31` |
| **add an invented statement** inside a phase — `playerPitch_ = 0.0f;` after the `pitchChanged` computation | **FAILS**: `diverges at statement 413` |
| **move a statement across a cut** (G04 correction 20) — hoist the `if (headingChanged) ctx_.playerHeading = heading;` block out of phase 6 into the top of phase 7, i.e. *after* the camera has been repositioned from the old heading. Statement count unchanged; only the position moves | **FAILS**: `diverges at statement 410` |
| (the empty-file trap) truncate the golden to 0 bytes | **FAILS** ×2 including case 1 — nothing passes vacuously |
| **the text guard, reachability half** — delete `updateHeadingAndPitch(frame);` from the driver, leaving the first-person floor clamp spelled correctly inside a phase nothing calls | **FAILS** in `test_first_person_camera_single_formula`: `updateHeadingAndPitch() applies the first-person down-pitch floor ... but NOTHING IN THAT FILE CALLS IT` |
| **the text guard, spelling half** — delete the `if (cameraMode_ == CameraMode::FirstPerson) { ... firstPersonMinLookPitchDegrees() ... }` block outright | **FAILS** in the same test: `firstPersonMinLookPitchDegrees is no longer read anywhere in ...` |

### Text-reader check (corrections 7 and 13: run the grep even when the plan's list looks complete)

`grep` for `renderer_player_physics` / `updatePlayerMovement` /
`player_camera_component` across `tests/`, `scripts/`, `tools/` and `docs/`. The
plan named five guards; the grep found **twelve** mentions, of which **exactly
one test parses this TU**:

| file | reads | disposition |
|---|---|---|
| `tests/test_first_person_camera_single_formula.cpp` | **this TU as text** — Part 1 scans it for a `CameraMode::FirstPerson` + camera-write pair; Part 2 asserts it is the sanctioned second reader of `firstPersonMinLookPitchDegrees` | **WIDENED** (below); assertions untouched, **15 cases green** |
| `tests/test_entity_collision_z_offset.cpp:9` | prose only (a comment naming the snap formula) | untouched, green |
| `tests/test_first_person_body_draw_rule.cpp:291` | prose only ("renderer_player_physics asks for p01 every frame") — and it is still true: that line is now in phase 8 | untouched, green |
| `tests/test_bridge_queue_race.cpp:149` | prose only (names the mouse-delta consumer) | untouched, green |
| `tests/test_frame_component.cpp:109` | prose only (`chatInputFocused()` reader) | untouched, **green** |
| `tests/test_player_camera_component.cpp`, `tests/cmake/renderer_guards.cmake` | include/build the header | untouched, green |
| `tests/test_irrlicht_camera_command.cpp:64`, `test_sim_bridge_component`, `test_slash_commands_component`, `test_zone_stream_component` | prose naming the header/idiom | untouched |
| `docs/module_map.md:190,199` | lists the component and the TU | row 199 rewritten |

Part 1 of the single-formula test is unaffected by construction: this TU mentions
`CameraMode::FirstPerson` (now in phases 2 and 6) but still never writes
`camera_`/`ctx_.camera` — it delegates to `repositionFirstPersonCamera()`, and
that call moved into phase 6 verbatim.

**The widening (correction 26, and correction 16 — name the matcher that would
have weakened it).** `TunablesAreReferencedFromOneTUOnly` allowed
`firstPersonMinLookPitchDegrees` a second reader, identified **by filename**:
"the accessor is spelled somewhere in `renderer_player_physics.cpp`". That was
sufficient while the accumulator was one 806-line function — the spelling and
the function were the same object. After G06 the clamp lives inside
`updateHeadingAndPitch()`, and **the old matcher goes green over a phase the
driver no longer calls**: the file is right, the spelling is right, and vertical
mouse-look has silently lost its floor. The new clause resolves the enclosing
`void PlayerCameraComponent::<name>(` above *every* match and asserts a line
starting `<name>(` exists elsewhere in the same TU. Both halves were proved by
planting the break each exists to catch (the last two rows above); the
reachability plant leaves the file-level matcher **passing**, which is the whole
point, and neither plant is caught by the other's clause.

### Verification

| step | result |
|---|---|
| `cmake --build wt/slot2/build -j24` | **EXIT 0**, first attempt, no exit-143 (no post-143 sweep needed) |
| `ctest --test-dir wt/slot2/build -j6 --output-on-failure` | **3759/3760 passed**; sole failure the known host quirk `AudioManagerTest.PreloadCommonSounds`. 3760 tests (was 3756 — the 4 new order cases) |
| `bash scripts/build-gles2-x86.sh` | **EXIT 0** |
| **visual gate standalone** (`bash scripts/ci/visual_gate.sh`) | **EXIT 0, `=== VISUAL GATE: PASS ===`** — qeynos2, gfaydark, befallen plus the three memory-coverage runs. `git status --porcelain` afterwards lists **no golden**: only the six files this unit owns. **This is the behavioural proof.** |
| `bash scripts/build-arm-noble.sh` | **EXIT 0** |
| `check_file_sizes.sh --all` | **checked 866 files: 866 ok, 0 warn, 0 fail** |
| T234/T237 transform suites | `test_eq_action_handler_transforms_real`, `test_movement_component`, `test_eq_action_handler_movement_utility` — all green in the suite run, **zero assertion edits** |
| line multiset | 22 removed (8 header comments + 14 de-typed declarations), 172 added, every line attributed; **zero code lines removed**; residual **0** |
| planted faults | 7 plants in the code + the empty-golden trap, **8/8 caught**; TU, golden and guard restored byte-identical after each |

### Deviations from the plan

1. **Fourteen bodies, not eight.** The driver is the ordered list of eight
   phases, as planned; phase 5 then dispatches five more, because the collision
   region is 469 of the body's 804 lines and its own `else if` chain is the
   file's clearest seam. Leaving it as one 469-line phase would have replaced an
   806-line method with a 469-line one. Header declarations: **13**, not "≈ 8".
2. **The cut is the file's blank-line seams**, not banners (G04's rule) and not
   timing brackets (G05's) — this file has neither. Every top-level cut lands on
   a blank line that already separated two statement regions.
3. **There IS a struct** — the first G unit to need one. G04 and G05 found zero
   cross-cut locals; this body has seventeen. It is a private *nested* struct
   (K05 j, like `CollisionDebugLine` beside it), not a header-level type: nothing
   outside this family names it.
4. **The driver is 14 lines, not the predicted 30-40** — a direct consequence of
   the exit census: with no early returns to honour, the list needs no `if`s.

### Corrections to the Batch G template

- **(27) When the slices need cross-phase locals, re-bind them by REFERENCE
  under their original names rather than rewriting the body to `frame.X`.** The
  choice is invisible to the compiler and decisive for the proof: aliasing kept
  804 of 804 body lines byte-identical and left a 22-line multiset delta that
  fits in a table, where a `frame.` rewrite would have touched several hundred
  lines and buried a real edit among them. The cost is one stated normalisation
  rule in the order test (drop `<type>[&] X = frame.X;` **only when the alias
  name equals the field name**, so a binding that silently renames a field still
  shows as a diff).
- **(28) The golden's normalisation must be told, explicitly and narrowly, about
  the one text change the refactor makes.** Here that is fourteen declarations
  becoming assignments as their storage moved into the struct. Stripping a
  leading type-specifier *for frame fields only* — with the field list parsed
  from the header at test time, never hard-coded — keeps the rule auditable and
  keeps `static` (and every non-field local) fully policed. A blanket
  "strip leading type keywords" rule would have hidden phase 7's three
  function-local statics turning into ordinary locals, which is a real bug.
- **(29) A body-parsing text guard must skip the signature, not just count
  braces from the definition line.** G05's parser collected everything after the
  definition line; G06's phase 5c has a four-line signature, and that parser
  swallowed three parameter lines into the body and produced 474 statements
  against a 471-entry golden. Start collecting only after the brace depth first
  goes positive. Cheap to get wrong, and it fails as a confusing order diff
  rather than as a parse error.
