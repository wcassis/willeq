# T162 — Close the `/taunt` bypass, and every other one like it

**Slot: slot1. x86. NO eqemu, NO lock, NO Pi.**

**Territory: `src/client/eq_action_handler.{h,cpp}`, `src/client/action/`,
`include/client/action/`, `src/client/skill/`, `include/client/skill/`,
`src/client/combat.cpp` (only if a skill opcode is emitted there), `tests/`,
`refactor/`.**

T161 proved live that **`/taunt` bypasses T159's gate entirely.** 45 seconds
after the gate correctly refused `/useskill taunt` on a Rogue, `/taunt` on the
same character sent `OP_Taunt` to the server, printed **"Taunting target"**, and
the server discarded it in silence. Read T161's Review (defect 1) and T159's
Review before starting.

**The general defect: T159's gate lives in `SkillManager::activateSkill`, so it
only protects `activateSkill`'s callers.** Every other path that emits a skill
opcode is ungated and still lies to the player. This unit closes the class, not
just the one command.

## Known bypasses — confirmed by the orchestrator, not a guess

| command | path | opcode |
|---|---|---|
| `/taunt` | `cmdTaunt` → `ActionDispatcher::taunt()` → `EqActionHandler::taunt()` (`:392`) → `CombatManager::Taunt()` (`combat.cpp:746`) | `HC_OP_Taunt` at `combat.cpp:770` |
| `/bandage` | `ActionDispatcher::bandage()` → `EqActionHandler::bandage()` (`:1430`) → `EverQuest::SendBindWound` | `HC_OP_BindWound` at `eq_player_info.cpp:1775` |
| `/track` | `EqActionHandler::trackTarget()` (`:1623`) → `EverQuest::SendTrackTarget` | `HC_OP_TrackTarget` at `eq_player_info.cpp:1793` |

**That table is a starting point, not the answer. Sweep for the rest yourself** —
every `HC_OP_*` that `SkillManager::getSkillOpcode()` knows about, reached from
anywhere other than `sendSkillUsePacket`. Report the complete inventory in the
Review, including any you decide to leave ungated and why.

## The shape of the fix

**Route them through the one gated entry point, `SkillManager::activateSkill`.**
For Taunt that is already where they meet: T152 deliberately made skill 73 call
`CombatManager::Taunt()` and added **no second sender**. So `/taunt` should
become `activateSkill(73, target)` — **do not add a parallel gate check inside
`EqActionHandler::taunt()`**, which would be a second copy of the predicate and
the board's duplicated-then-drifted class all over again.

Watch what `activateSkill` does beyond sending: it plays an animation, starts a
cooldown, and fires `m_on_skill_activated`. **Make sure routing a command through
it does not double-send, double-animate, or start a cooldown the command never
had.** If a command genuinely must not take the cooldown, say so and explain.

**Before gating any command, check `config/skill_caps.json` for the classes that
use it.** Gating a command whose skill has no cap row for a class that can
legitimately use it would break working behaviour — the exact false-negative
T159 was designed to avoid. Bind Wound in particular is near-universal; verify
rather than assume. Report any command where the data says something surprising.

## Tell the player the truth

The current message is "Taunting target" for a packet the server throws away.
When the gate refuses, the player gets the real reason — T159 already produces
`Cannot use Taunt: Your class cannot use Taunt`. Reuse it; do not invent a
second wording.

## Acceptance

- A test reproducing T161's exact live case: **a Rogue running `/taunt` sends no
  packet** and is told why. This is the unit's headline and it must fail against
  today's code.
- A Warrior running `/taunt` still sends.
- `/bandage` still works for a class that has Bind Wound — T158 and T161 both
  verified it live, so a regression here is a real regression.
- One test per additional bypass you close.

## Verification

Full protocol bar. Baseline **2585/2586** at tip `9c401324`, sole expected
failure the documented ALSA `AudioManagerTest.PreloadCommonSounds`.
`tests/test_command_processor.cpp` is over the 3000-line ceiling — new tests go
in a new file.

⚠ A **gtest-discovery build race** has now hit two units (T159, T161): the first
`-j24` build fails inside `GoogleTestAddTests.cmake` with no compiler error and
an immediate rebuild is clean. If you see it, rebuild once and **say so** — a
third sighting makes it worth its own unit.

**Not claimed: any wire behaviour.** The live re-check is owed and is cheap —
T161's driver already has the exact `/taunt`-after-refusal sequence, and
category 84 (`Packet C->S`) is how it is proven.

## STOP conditions

- A bypass cannot be routed through `activateSkill` without changing what the
  command does to the player (report it; do not force it).
- The cap data says a class cannot have a skill that a working command sends.

## Review

(executor fills this in)
