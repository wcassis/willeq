# T440 — a total ray miss is NoFloor (B438); the diagnostic ground stub and two dead skill fields (B415, B416)

Class A/B, slot2, no server. Read B438, B415, B416; T437's §Review
(`GroundZ` type, `ShouldLandJump()`, the `findGroundZIrrlicht()` stub
with five ternary call sites that only warns), T429 Leg B's §Review
(the skill fields), `skill_data.h`, `skill_manager.cpp`,
`renderer_player_collision.cpp` / `renderer_player_physics.cpp`.

1. B438: `findGroundZBspFiltered()` answers `GroundZ::NoFloor()` on a
   total miss and when there is no BSP tree; every consumer already
   handles `NoFloor` since T437 — verify each (a falling player keeps
   falling; the walking step-height path and wall slides use
   `zOr(currentFeetZ)`), and say what the player does over a real hole
   (falls until the next floor / zone geometry; never lands in mid-air).
   Pre-fix failing test in the device-free `ground_query` seam and, if
   reachable, on the BSP filter with a hand-built tree.
2. `findGroundZIrrlicht()`: a `LOG_WARN` + return-feet stub referenced by
   five ternaries. Delete it and the ternaries (the BSP path is the only
   path); if a non-BSP zone exists in the era assets, say which and what
   happens there now (NoFloor). Census pin if one covers that TU.
3. B415: delete `SkillData::animation_code` (the one consumer calls the
   free function) — or make `SkillManager` read it; pick by which
   removes a duplicate store. B416: `usable_while_sitting` — read
   eqemu's per-opcode handlers (`/mnt/projects/akk-stack/code/zone/…`,
   quoted absolutely) for a sitting refusal as T325 read them for
   in-combat; wire the predicate where eqemu refuses, delete the field
   where it does not. No guessing: the handler text decides.

## Proof

Build (background), `ctest -j6` (no expected reds), pre-fix/post-fix,
G05/G06 in place (G06 pins the movement TU), visual gate standalone +
ARM (no image golden should move), file sizes, WriteOnlyFields/census
pins learn deletions. Strike B438/B415/B416; new items B445+ (T439 in
slot1 also allocates from B445 — say which you used).

STOP: an image golden moves.
