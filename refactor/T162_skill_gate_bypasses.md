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

Executed in `/mnt/projects/fable/wt/slot1`, branch `task/T162-skill-gate-bypasses`
off `fable` tip **`9c401324`**. No eqemu, no lock, no Pi. `main` and the sibling
slots were never touched.

**Build** `cmake --build build -j24` clean, unpiped. **Suite**
`ctest --output-on-failure` **2595/2596** — the plan's 2585 baseline plus this
unit's 10 new tests, sole failure the documented ALSA
`AudioManagerTest.PreloadCommonSounds`. **Sizes**
`scripts/refactor/check_file_sizes.sh fable`: 4 changed source files, 3 ok,
1 warn, 0 fail — `src/client/eq_action_handler.cpp` 1768 lines, a pre-existing
WARN (it was 1689 on `fable`, already past the 1500 warn line; this unit added
79 lines, almost all of them comment).

**Deviation — the gtest-discovery race, third sighting.** The first `-j24` build
failed inside `GoogleTestAddTests.cmake` ("Error running test executable …
`test_login_world_seam` … Process terminated due to timeout") with no compiler
error anywhere. An immediate rebuild with no source change was clean, and every
build after it was clean. This is T159's deviation 4 and T161's, verbatim, now
**seen three times in three different units** — the plan said a third sighting
makes it worth its own unit.

---

### The complete inventory of paths that emit a skill opcode

Swept by grepping every `HC_OP_*` that `SkillManager::getSkillOpcode()` can
return (the seventeen dedicated opcodes plus `HC_OP_CombatAbility`) across
`src/` and `include/`, then chasing the callers of every sender found. The
matches in `src/client/eq.cpp:1570-1634` are the **inbound** zone-packet switch
(`ZoneProcess*` responses) and emit nothing; `src/client/eq_strings.cpp` is
opcode-name lookup. What is left is the whole outbound set:

| # | emitter | opcode(s) | reached from | before T162 | after |
|---|---|---|---|---|---|
| 1 | `SkillManager::sendSkillUsePacket` (incl. `sendBodiedSkillPacket`) | all 17 dedicated + `HC_OP_CombatAbility` | `activateSkill` | **gated** (T159) | unchanged |
| 2 | `SkillManager::sendSkillUsePacket` | same | `SkillManager::toggleSkill` | **ungated** | **left ungated — see below** |
| 3 | `CombatManager::Taunt` | `HC_OP_Taunt` | `sendBodiedSkillPacket` case 73 | gated | unchanged |
| 4 | `CombatManager::Taunt` | `HC_OP_Taunt` | `EqActionHandler::taunt()` (`/taunt`) | **BYPASS** | **routed through `activateSkill`** |
| 5 | `CombatManager::UseAbility` | `HC_OP_CombatAbility` | `sendSkillUsePacket` only — no other caller in the client | gated | unchanged |
| 6 | `EverQuest::SendApplyPoison` | `HC_OP_ApplyPoison` | `sendBodiedSkillPacket` case 6 only | gated | unchanged |
| 7 | `EverQuest::SendBindWound` | `HC_OP_BindWound` | `sendBodiedSkillPacket` case 9 | gated | unchanged |
| 8 | `EverQuest::SendBindWound` | `HC_OP_BindWound` | `EqActionHandler::bandage()` (`/bandage`) | **BYPASS** | **routed through `activateSkill`** |
| 9 | `EverQuest::SendTrackTarget` | `HC_OP_TrackTarget` | `EqActionHandler::trackTarget()` (`/track`) | **BYPASS** | **gated by calling the gate's predicate — cannot be routed** |
| 10 | `SkillManager` direct `QueuePacket` | `HC_OP_Disarm`, `HC_OP_PickPocket` | `sendBodiedSkillPacket` cases 16/48 | gated | unchanged |
| 11 | `eq_bridge.cpp:1056`, `:1371` (hotbar / skills-window activation) | — | `activateSkill` | gated | unchanged |

Nothing else emits one. In particular `/sneak`, `/feign`, `/hide` and `/run`
are **not** on this list: they go to `setMovementMode()` / `setPositionState()`
and send appearance/state packets, not `HC_OP_Sneak` / `HC_OP_FeignDeath` /
`HC_OP_Hide`. Those three opcodes are outbound-only from
`sendSkillUsePacket`, i.e. only via `/useskill`.

**Left ungated, with the reason: `SkillManager::toggleSkill` (row 2).** It calls
`sendSkillUsePacket` without ever consulting `canUseSkill`, so on paper it is a
second bypass — but it has **zero callers anywhere in the client** (the
reachability class this board tracks; nothing in `src/`, `include/` or `tests/`
names it). Nothing can reach it, so nothing can bypass the gate through it.
Routing it through `activateSkill` would be wrong anyway: `canUseSkill` refuses
on cooldown and on resources, which is not the right question for switching a
toggle *off*. It is annotated in place with a comment naming this unit, so
whoever wires a toggle UI to it has to make the call deliberately.

---

### What each bypass became, and what `activateSkill`'s side effects meant

**`/taunt` — `EqActionHandler::taunt()` now calls `useSkill(EQ::kSkillTaunt)`.**
`useSkill()` is the existing wrapper T156 built around `activateSkill`: it
resolves the current target and reports `activateSkill`'s own refusal. Delegating
to it rather than calling `activateSkill` inline means the file gained **no**
second copy of anything — not the predicate, not the error reporting. The
"no target" pre-check and its `"No target to taunt"` message are kept ahead of
it, unchanged.

* **Double-send: no.** T152 deliberately made skill 73 route *to*
  `CombatManager::Taunt()` and added no second sender, so the old direct call
  and the new gated one end at the same single line (`combat.cpp:770`). One
  `HC_OP_Taunt`, as before.
* **Animation: new, but proven.** `activateSkill` calls `playSkillAnimation`,
  and skill 73 has `getSkillAnimationId == ANIM_ID_KICK`, so `/taunt` now also
  sends `SendAnimation` and pushes the kick gesture. That is exactly what
  `/useskill taunt` already did, and T161 watched a Warrior do it live with the
  server answering the taunt in 31 ms. `/taunt` and `/useskill taunt` are now
  the same action, which is the point.
* **Cooldown: new, and correct.** `/taunt` had no client-side recast; it now
  takes Taunt's 5 s (`getSkillBaseRecastTime(73) == 5000`), shared with
  `/useskill taunt` instead of one command dodging the other's timer. Taunt has
  a real reuse timer in EQ, so this narrows nothing a player could legitimately
  do.
* **Message:** `"Taunting target"` is **deleted**, not printed alongside
  anything. Success is now `activateSkill`'s own `"You use Taunt!"` on the
  bridge; refusal is T159's `"Cannot use Taunt: Your class cannot use Taunt"`.
  No new wording was invented. Grep confirms no test asserted
  `"Taunting target"`.

**`/bandage` — `EqActionHandler::bandage()` now calls
`useSkill(EQ::kSkillBindWound)`.**

* **Double-send: no.** The old body was three lines of target resolution plus
  `SendBindWound`. `sendBodiedSkillPacket`'s case 9 carries the *same three
  lines* (T152 copied them from here) plus the same `SendBindWound`. The
  duplicate is now deleted and case 9 is the only copy — a duplicated-then-
  drifted pair removed rather than added.
* **Animation: none.** `getSkillAnimationId(9) == 0` and
  `getSkillAnimationCode(9) == nullptr`, so `playSkillAnimation` sends nothing
  and pushes nothing. `/bandage` gains no packet.
* **Cooldown: new, 1 s.** Bind Wound's base recast is 1000 ms. This is the
  shortest in the table and matches what `/useskill bind wound` already did.
* **Targeting is unchanged for every production caller.** Bind Wound has
  `requires_target == false` (you may bandage yourself), so `activateSkill`
  zeroes the target before `sendSkillUsePacket` and case 9 re-resolves
  "current target, else self" — bit-identical to the old function for
  `bandage(0)`, which is the only call the client makes
  (`CommandProcessor::cmdBandage` → `m_dispatcher.bandage(0)`; `/bandage` and
  the `/bindwound` alias both).
  **Narrowing, reported not hidden:** an *explicit non-zero* `targetId` handed
  to `ActionDispatcher::bandage()` is no longer honoured — it is resolved
  inside the skill path instead. No caller passes one (the two
  `test_command_processor` tests assert `bandage(0)` precisely because that is
  the only form). The alternative was a new `SkillData` flag to stop
  `activateSkill` zeroing the target for this one skill, and this seam has **no
  packet observation point** (`EverQuest::QueuePacket` no-ops with a null zone
  connection and counts nothing — T23's and T159's finding), so that flag would
  have been untestable machinery guarding an unreachable case. The function
  logs at DEBUG when it is given one.

**`/track` — gated, not routed. This is STOP condition 1, reported rather than
forced.** `EqActionHandler::trackTarget()` sends `HC_OP_TrackTarget`, which is
**not skill 53's opcode**: `getSkillOpcode(53)` is `HC_OP_Track`. They are two
different actions in eqemu — `Handle_OP_Track` bumps the Tracking skill and
returns the track *list*, `Handle_OP_TrackTarget` just does
`SetTrackingID(t->EntityID)` on a target you already picked. `activateSkill(53)`
would therefore send a different packet and do a different thing, which the plan
says not to force.

It is still the same defect: `Handle_OP_TrackTarget`'s **first statement** is
`if (!CanThisClassTrack()) return;` — so before this unit a Rogue's `/track`
put a packet on the wire that the server dropped in silence, with the client
saying nothing at all. It is now gated by a new one-line private helper
`EqActionHandler::skillGateAllows(skillId)` that **calls** T159's predicate,
`SkillManager::hasSkill()` (= `isDisplayableSkill() && canClassHaveSkill()`),
and formats the refusal from `getSkillUseError()` — the same sentence
`activateSkill` produces, so a player refused by `/track` reads exactly what a
player refused by `/taunt` reads. That is a call to the single predicate, not a
copy of it; there is no `class_can_have` test written anywhere in
`eq_action_handler.cpp`. The helper fails **open** (no `SkillManager`, or an
unloaded cap table) exactly as T159's gate does. It is checked *before* target
resolution, matching the server's own order.

---

### The cap-data check, per command

Read out of the shipped `config/skill_caps.json` and **pinned as a test**
(`CapDataSaysWhoMayTauntBandageAndTrack`), so a data change fails loudly instead
of silently changing who can do what:

| skill | command | classes with a row at ANY level | verdict |
|---|---|---|---|
| 9 Bind Wound | `/bandage` | **all sixteen** (1–16) | gating it can refuse nobody; the T158/T161 live `/bandage` cannot regress |
| 73 Taunt | `/taunt` | 1 Warrior, 3 Paladin, 4 Ranger, 5 Shadowknight | matches EQ; Rogue (9) absent, which is T161's case |
| 53 Tracking | `/track` | 4 Ranger, 6 Druid, 8 Bard | **exactly** eqemu's `CanThisClassTrack()` |

Nothing surprising, and nothing that would have been a false negative. The one
worth naming: the `/taunt` help text still reads
"warrior/paladin/shadowknight ability" and omits **Ranger**, which the cap data
says can taunt. Cosmetic, in `command_processor.cpp`'s registry, left alone.

---

### The tests

New file `tests/test_skill_gate_bypasses.cpp` (10 tests, ~290 lines), registered
in `tests/CMakeLists.txt`. `tests/test_command_processor.cpp` was not grown (it
is past the 3000-line FAIL ceiling). The tests drive **`EqActionHandler`
directly** — the layer where the bypass lived — against a real `EverQuest`, a
real `CombatManager` and a real `SkillManager`, on `test_skill_possession_gate`'s
seam. The cap data is the **shipped** `config/skill_caps.json` loaded by path
from `WILLEQ_REPO_ROOT`; there is no synthetic cap table in the file.

1. `CapDataSaysWhoMayTauntBandageAndTrack` — the table above, all sixteen
   classes checked for each of the three skills.
2. **`RogueTauntCommandIsRefused_AndNothingIsSent` — the headline**, T161's
   exact live case: a Rogue with a live target runs `/taunt`, nothing is sent,
   and the player is told why. The packet-not-sent witness is the **cold
   cooldown** (`activateSkill` starts Taunt's 5 s recast only on the branch that
   has already reached `sendSkillUsePacket`, and that is the only surviving path
   to `CombatManager::Taunt`). It also asserts the two lines T161 recorded as
   lies — `"Taunting target"` and `"You use Taunt!"` — are both absent.
3. `WarriorTauntCommandStillSends` — the control: cooldown running,
   `"You use Taunt!"` on the bridge.
4. `TauntWithNoTargetStillSaysNoTargetToTaunt` — the message that did not change.
5. `RogueBandageStillWorks` — the regression that matters. Cooldown running, so
   `SendBindWound` was reached.
6. `BandageWithNoTargetStillWorks_SelfBandage` — the self-fallback survived the
   move into case 9.
7. `RogueTrackCommandIsRefusedWithTheSameWording` — asserts the full string
   `"Cannot use Tracking: Your class cannot use Tracking"`.
8. `RangerTrackCommandStillSends` — no refusal, no target complaint.
9. `TrackGateRunsBeforeTargetResolution` — a Rogue with no target gets the
   **class** reason, not the target reason, the way the server orders it.
10. `MissingCapFileLeavesEveryCommandPermitted` — with the cap file gone, both
    `/taunt` and `/track` behave as they did before the gate existed. Wrong, but
    wrong in the direction that leaves the client playable.

**Confirmed to fail against today's code.** With `eq_action_handler.cpp` reverted
to `fable` and the test file unchanged, **7 of the 10 fail**, headline included
(`RogueTauntCommandIsRefused_AndNothingIsSent`,
`WarriorTauntCommandStillSends`, `RogueBandageStillWorks`,
`BandageWithNoTargetStillWorks_SelfBandage`,
`RogueTrackCommandIsRefusedWithTheSameWording`,
`TrackGateRunsBeforeTargetResolution`,
`MissingCapFileLeavesEveryCommandPermitted`). The three that pass either way are
the cap-data pin, the unchanged "no target" message, and the Ranger `/track`
control — which is what those three are for.

---

### Deviations

1. **The gtest-discovery race, third sighting** (see above).
2. **`/track` was not routed through `activateSkill`** — STOP condition 1,
   reported not forced, with the opcode evidence above. It is gated by calling
   the gate's predicate instead.
3. **One new named-constant block** (`EQ::kSkillBindWound` / `kSkillTracking` /
   `kSkillTaunt` in `skill_constants.h`) so the command handlers do not carry
   bare `9` / `53` / `73`.
4. **`ActionDispatcher::bandage()`'s explicit-target parameter is now advisory**
   (see above). No caller passes one; the function logs at DEBUG if one arrives.
5. `src/client/eq_action_handler.cpp` remains a size **WARN** (1689 → 1768). It
   was already over the 1500 warn line before this unit; splitting it is not
   this unit's territory.

### Not claimed — the owed live re-check

**No wire behaviour is claimed.** The x86 test seam cannot observe a packet
(`QueuePacket` no-ops with a null zone connection), so every "nothing was sent"
assertion here is the cooldown witness, not the wire. The re-check is cheap and
is exactly T161's method: run T161's driver, whose script already contains the
`/taunt`-after-refusal sequence, with server log category **84 (`Packet C->S`)**
at level 3 to file, and confirm that

* `Oraclerogue`'s `/taunt` produces **no** `OP_Taunt` line (T161 has one at
  `17:24:44` — that line must disappear), while `Testmelee`'s `/taunt` still
  produces one;
* `Oraclerogue`'s `/track` produces **no** `OP_TrackTarget` line;
* `Oraclerogue`'s `/bandage` still produces `OP_BindWound` — the one that must
  not regress.

