# T337 — The G06 movement-order golden learns T332's five-line insertion

Merge interaction: T332 (branched before G06 merged) added five lines to
`src/client/graphics/player/renderer_player_physics.cpp` (the
`seededPosition` handling inside the position-publish phase), and the G06
order golden `tests/goldens/movement_order_g06.txt` does not contain
them — `MovementOrder.ComposedMovementOrderMatchesThePreG06Golden` fails
on the tip at statement 410. Both changes are correct; the golden must
learn the insertion. Slot3. No production code change.

1. Diff `e9f67119..HEAD` for the TU; list the added statements and the
   phase they sit in.
2. Update the golden by INSERTING those statements at their position (do
   not regenerate from the tip — that would accept anything; the golden's
   value is that it was derived from the pre-G06 body plus known,
   reviewed insertions). Record the insertion in the golden's header
   comment (or a sibling `.md`) as "T332 insertion at 410–414".
3. Re-plant: drop one of the inserted lines in the code → the test must
   fail; restore; swap two phase calls → fail; restore. Paste both.
4. `test_movement_order` 4/4; the frame text-reader tests; full ctest -j6
   (ALSA quirk only). Skip gles2/gate/ARM with evidence (no source file
   changes).
5. Add to `refactor/G_oversize_methods.md` as correction 31: "an order
   golden is a merge-time hazard — any branch that edits a golden-pinned
   TU and was cut before the golden's unit merged will fail on the tip;
   the merge step must run the golden tests for every TU the branch
   touched."

Territory: the golden file, `tests/test_movement_order.cpp` (only if the
insertion needs a normalisation note), `refactor/G_oversize_methods.md`.

## Review

Executed 2026-09-04 in `wt/slot3`, branch `task/T337-movement-golden` off
`fable` at `429716b5`. **No production source file changed** — the unit is the
golden plus its guard's size constant. **No STOP condition hit.**

### 1. The insertion, measured from the diff

`git diff e9f67119 HEAD -- src/client/graphics/player/renderer_player_physics.cpp`
is **two hunks, five added lines, of which THREE are comments**. As the order
test's normaliser sees the TU (comments dropped), T332 added exactly **two
statements**:

| statement | phase | sits immediately after |
|---|---|---|
| `playerPositionIsSeed_ = false;` | 6, `publishPositionAndCamera()`, inside `if (positionChanged)` | `ctx_.playerZ = newZ;` |
| `intent.seededPosition = playerPositionIsSeed_;` | 7, `sendPositionUpdateToServer()` | `intent.heading = heading;  // EQ format (0-512)` |

Neither is a `MovementFrame` field, so normalisation rules 3 and 4 do not touch
them; both are emitted verbatim (the second's predecessor keeps its trailing
comment, which is why the golden entry carries it too).

### 2. INSERTED, not regenerated

`tests/goldens/movement_order_g06.txt` was rewritten by inserting the two
statements at their positions and renumbering — **the 471 pre-existing entries
are unchanged in content and in relative order**; the diff is 63/61 lines only
because every ordinal from 410 on shifts by one, then again from 440 on. The
golden is now **473 entries**:

- **410** `playerPositionIsSeed_ = false;` (was: 410 `if (headingChanged) {`)
- **440** `intent.seededPosition = playerPositionIsSeed_;`

The golden's line format is `<ordinal> <statement>` and `goldenOrder()` rejects
a line with no space, so it cannot carry a header comment. The insertion is
therefore recorded **in the guard**, in the comment block above `kGoldenSize`
(471 -> **473**), naming both statements, both positions and the reason. The
`GoldenIsPresentAndTheRightSize` failure message gained one clause: a genuine
change is INSERTED at its position and named there, **never regenerated from
the tip** — a regenerated golden accepts whatever the tip says, which is the one
property this golden exists to deny.

`tests/test_movement_order.cpp` is the only other file touched, and only in the
constant and two message strings; the parser, the normaliser and all four cases
are untouched.

### 3. Re-plant: both faults caught, TU byte-identical afterwards

The TU was copied aside first, and `git status --porcelain` after each restore
lists **no source file** — proof of byte-identity without `git checkout --`.

| plant | outcome |
|---|---|
| **drop one of the inserted lines** — delete `playerPositionIsSeed_ = false;` from `publishPositionAndCamera()`. This is the real regression T332 exists to prevent: the client would keep claiming every position it sends is a seed echo, forever | **FAILS**, exit 1: `movement order diverges at statement 410: got: if (headingChanged) { / exp: playerPositionIsSeed_ = false;` — 3 passed, 1 failed |
| **swap two phase calls** — `publishPositionAndCamera(frame);` ⇄ `sendPositionUpdateToServer(frame);` in the driver, no phase body edited | **FAILS ×2**, exit 1: `updatePlayerMovement() no longer calls the eight movement phases in order` (the `called` vector printed with 6 and 7 transposed) **and** `movement order diverges at statement 405` — 2 passed, 2 failed |

Note the second plant still diverges at **405**, three statements before the
insertion — the phase-order fault is caught upstream of it, so the insertion
did not blunt the guard.

### 4. Verification

| step | result |
|---|---|
| `test_movement_order` | **4/4 PASSED**, exit 0 |
| `cmake --build build -j24` | **EXIT 0**, no exit-143, no post-143 sweep needed |
| frame text-reader tests — `ctest -R "MovementOrder\|FrameRenderOrder\|FrameComponent\|FirstPerson"` | **56/56 passed, 0 failed** |
| full `ctest --test-dir build -j6` | **3775 passed / 1 failed out of 3776** (141 s). The sole failure is the known host quirk `AudioManagerTest.PreloadCommonSounds` — the ALSA-only exception the bar allows. Test count is unchanged from the tip's 3776: this unit adds no case |
| `git diff --name-only` | `tests/goldens/movement_order_g06.txt`, `tests/test_movement_order.cpp`, `refactor/G_oversize_methods.md`, `refactor/T337_movement_golden_learns_t332.md` — **no file under `src/` or `include/`** |
| gles2 build / visual gate / ARM cross-build | **SKIPPED, with the `git diff --name-only` above as the evidence**: no production source or header changed, so no binary this unit produces differs from the tip's. Re-running them could only re-measure `fable` |

### 5. Correction 31

Appended to `refactor/G_oversize_methods.md` under a `## T337 correction`
heading: an order golden is a **merge-time** hazard. Both changes here were
correct and neither conflicted textually — git merged the code cleanly, and the
golden, living in a different file, silently stopped describing it. The merge
step must run the golden tests for **every TU the incoming branch touched**, not
just the branch's own tests; a green pre-merge branch proves nothing about a
golden it never saw.
