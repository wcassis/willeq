# T254 — auto-defence engages the LATEST attacker; does a stun gate movement? (T247 leftovers)

Slot: slot2. x86. OWNS eqemu briefly (noclobber) for the two-beetle leg.
Class A + B. Territory: src/client/combat.cpp `CheckForAggro` /
`NoteAttackedBy` / `GetNearbyHostiles`, src/client/eq_movement.cpp stun
gate (if the answer is yes), tests. Do NOT touch eq_bridge.cpp hotbar arm
(T253 slot1), inventory (T252 slot3).

## 1. Aggro selection
`CheckForAggro` engages the FIRST aggro entry in scan order, not the most
recent attacker. Keep a timestamp in `NoteAttackedBy`; engage the latest
(ties → nearest). Test: two attackers, older one first in scan order →
latest engaged. Red/green. Live: two beetles `#attack` in sequence →
engages the second.

## 2. Stun vs movement (class B)
eqemu: does a stunned client's OP_ClientUpdate get rejected/ignored, and
does the official client freeze movement while stunned? Read
`Client::Handle_OP_ClientUpdate` / `Mob::IsStunned` usage in
/mnt/projects/akk-stack/code/zone. If the server ignores movement while
stunned, gate `UpdateMovement`'s position sends on `!IsStunned()` (so the
client doesn't desync) and test it; if the server accepts movement while
stunned, do NOT gate — record the finding and close the question.

## Verification
Full ctest; Review; evidence review/T254/. Commit:
`T254: auto-defence targets the latest attacker; stun-vs-movement <settled>`.
