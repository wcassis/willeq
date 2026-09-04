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
