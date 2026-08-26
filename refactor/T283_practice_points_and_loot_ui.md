# T283 — one practice-point store; retire the loot-window UI-gesture leg (T280 + T268 hand-backs)

Slot: slot1. x86. NO eqemu (unit tests + a static-panel check). Class A.
Territory: `EverQuest::m_practice_points` / `PlayerState::m_practicePoints`
and the PlayerProfile parse that feeds both, the loot popup question below
(static_panels / static_ui_input — T272's click plumbing is the model),
tests. Do NOT touch the EQE1 index (T281 slot2), world containers (T282
slot3).

## 1. Two practice-point stores (T280)
Both are written from the same PlayerProfile so they cannot drift in
production, but a test seeding one silently gets the wrong answer from the
other. Per the 2026-08-25 decision (one GameState, state written at the
source), keep the `PlayerState` one and delete the duplicate — or, if the
duplicate is what the wire path needs, make the other a read-through. Grep
every reader first and say which way you went.

## 2. Loot window: the last UI-gesture leg (T268)
T268 closed F1 by command and left the three loot INTENTS producer-less
because there is no loot popup. Decide by looking: does the static UI have
a panel that lists corpse contents (T264/T268 said no)? If a panel exists
or is a small addition on the T272 pattern, wire the clicks to the same
handler bodies `/loot`, `/lootall`, `/lootclose` already use (one body).
If building a panel is genuinely a UI feature rather than a wiring job,
STOP and report it as a feature request — do not build a new window
speculatively.

## Verification
Full ctest; visual gate if a panel changed; Review; evidence review/T283/.
Commit: `T283: one practice-point store; loot UI leg <wired|reported>`.
