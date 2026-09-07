# T431 — first-person eye placement matched to the official client (B018)

Class B; eqemu lock (atomic; must be ABSENT). Read B018,
`refactor/T301_first_person_eye_placement.md` (written, never dispatched),
T307/T309 (the measured attack-animation draw rule — do not revive the
refuted near-plane/viewmodel ideas; see the memory note), T405's rig
(`#goto x y z h` pins position and heading on BOTH clients), and
`src/client/graphics/entity_first_person_draw.cpp` + the camera code.

1. Oracle: first-person view (the official client's zoom-in-to-first-
   person key) at T405's spot, heading pinned, day: a still + a slow
   turn video; the same on fable. Compare the eye height (where the
   horizon and a known landmark sit in the frame), the pitch, and the
   near geometry visible at the bottom of the frame.
2. A/B fable's eye height (currently the static bbox crown) and any
   offset against the oracle frame until the landmark rows match within
   the noise floor; every value carries its probe. Race matters (eye
   height differs per model) — measure two races.
3. Tests pin the solved placement per race (pre-fix failing). Strike B018.
   Restore server values (world DOWN); `down.sh`; delete the lock.

STOP: lock present; the official client's first-person key cannot be
driven under Wine (report; capture third-person as a control).
