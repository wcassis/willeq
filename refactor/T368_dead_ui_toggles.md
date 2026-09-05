# T368 — the three dead UI toggle keys get real toggles (B101); consumer-less InputActions (B100)

Class B, slot2. Two legs, one commit each. Read the B101 and B100 board
text and T365's §Review. The eqemu lock is held by T367 — this unit does
NOT take it.

## Leg A — B101

`RA::ToggleGroup` (G, Alt+P alternate), `RA::TogglePet` and
`RA::ToggleBuffWindow` reach EMPTY arms in `renderer_frame_input.cpp`
("visibility is driven by inGroup state"). A bound key that does nothing
is a defect; the official client's windows toggle open/closed on their
key regardless of state.
1. Oracle first, without the server: the official client's default key
   bindings live in its own files (`/mnt/projects/EverQuestP1999` —
   `eqclient.ini` [KeyMaps]/keyboard sections, `defaults/`, or the UI XML
   for the options window; find them and QUOTE the lines) — record the
   official default keys for the group, pet and buff windows and compare
   with `hotkey_manager.cpp`'s bindings. If they differ, fable's binding
   changes to the official one (a binding picked by argument is the
   smell). If the files do not record defaults, say so and leave the
   bindings; a screenshot of the options window is owed when the lock
   frees (board it).
2. Give each of the three the `toggleSkillsWindow()` shape: an `isOpen`
   the panel honours, toggled by the key. Read how each panel decides to
   draw today (`inGroup`, pet exists, buffs present) — the toggle must
   compose with it the way the official client does: a closed window
   stays closed when you join a group; an open window with no group draws
   its empty frame (check what the oracle draws for an empty group window
   — if unknown, the empty frame, and say it is unverified).
3. Tests, pre-fix failing: each key flips its panel's `isOpen`; the panel
   draw honours it (the static-panel UI test seam, T54–T56 shape).
4. Strike B101.

## Leg B — B100

`InputAction::ToggleVendor`, `TogglePetWindow`, `ToggleTrainer`,
`ToggleSpellbook` — no producer, no consumer. Delete them; restate or
delete `UIToggleActions_AreNotBridgeTerritory` (with all seven gone the
case has no subject — delete it and say so). Compiler confirms no reader.
Strike B100.

## Proof

Build, `ctest -j6`, the new tests with pre-fix/post-fix runs, file sizes;
`src/client/graphics/frame` changes → visual gate standalone + ARM
(slot2's trees are at willeq.db v6).

STOP: the official default binding for one of the three is a key fable
already binds to something else (report the collision; do not rebind by
argument).
