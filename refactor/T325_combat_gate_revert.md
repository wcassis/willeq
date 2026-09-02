# T325 — revert the client-side combat gate so the skill gate agrees with the server

**Slot: slot2. Branch `task/T325` cut from `fable` @ `bcbf2ddc`. x86 only. No Pi,
no eqemu lock (source-derived server evidence only).**

Closes **board item 36** of `refactor/deferred.md`.

## Ruling

Will, 2026-09-02: **"revert the combat gate."** T317 implemented
`SkillManager::checkCombatRequirement()` against a real in-combat signal and
flagged in its own Review that the result is *deliberately stricter than the
server*. It is. The practical cost is that the client refuses an **opening
Backstab from stealth** and an opening Kick — the rogue's standard opener —
both of which eqemu accepts.

## Shape of the revert (as T317's Review named it)

Clear `requires_combat` in `include/client/skill/skill_data.h` rather than
re-hollowing `checkCombatRequirement()`. That keeps the mechanism — field,
predicate, refusal message — intact and correct for anything that ever
genuinely needs it, and removes the false refusals. Re-hollowing the function
would recreate exactly the no-op gate T317 was dispatched to fix.

## What must be re-derived, not assumed

1. The eleven flagged skills, from `skill_data.h` itself.
2. For each, the **opcode fable actually sends** (`getSkillOpcode()`) and
   **that opcode's eqemu handler**. `OPCombatAbility` is not the only one:
   Disarm and Intimidation have dedicated handlers.
3. Whether any handler carries an in-combat requirement. Keep the flag on any
   skill that does, and say why.

## Out of scope, must keep working

⚠ **The facing check.** `checkPositionRequirement()` is server-derived
(eqemu `Mob::MobAngle`) and correct. T159's standing instruction pairs it with
`"You must be behind your target"` — that pairing is not to be disturbed.

## Acceptance

- A test that FAILS before this change and passes after: an opening Backstab
  (and Kick) with auto-attack off and no aggro is permitted.
- `tests/test_skill_gates.cpp` updated; for each changed assertion, say whether
  it was pinning real behaviour or the over-strict gate.
- Full `ctest -j6` (NOT -j24), zero new failures vs `refactor/test_baseline.txt`;
  `scripts/build-gles2-x86.sh` BEFORE ctest; visual gate PASS, goldens
  untouched; ARM build; `check_file_sizes.sh --files`.

## Territory

`include/client/skill/skill_data.h`, `src/client/skill/skill_manager.cpp`,
`tests/test_skill_gates.cpp`, `refactor/deferred.md`, this plan.

---

## Review

**Executed in slot2 on `task/T325`, cut from `fable` @ `bcbf2ddc`. x86 only, no
Pi, no eqemu lock. All server evidence is source-derived from the eqemu tree at
`/mnt/projects/akk-stack/code`; no live leg was run and none is claimed.**

### Verification bar

| step | result |
|---|---|
| `cmake --build build -j24` | PASS (EXIT=0) |
| `scripts/build-gles2-x86.sh` | PASS (EXIT=0), run BEFORE ctest |
| `ctest --test-dir build -j6` | **3511 tests, 1 failed** — `AudioManagerTest.PreloadCommonSounds`, the documented ALSA host quirk. Zero new failures vs `refactor/test_baseline.txt`. |
| Visual gate | **PASS** (in-suite `visual_gate`, 21.40 s), goldens untouched (`git status -- refactor/goldens` empty) |
| `scripts/build-arm-noble.sh` | PASS (EXIT=0). ⚠ The FIRST attempt hit the 600 s tool ceiling (exit 143); per the protocol trap the rebuild's clean EXIT=0 was NOT trusted on its own — every artifact under `build-arm-noble/` (`bin/`, `*.so*`, and `lib*.a` including the 2.6 GB `libwilleq_core.a`) was scanned with `readelf -h` / `ar t`. **0 bad, 0 empty.** |
| `check_file_sizes.sh --files` | 3 ok, 0 warn, 0 fail |

### The eleven: all cleared, none kept

Re-derived per skill through `SkillManager::getSkillOpcode()` — which is NOT
uniform, and that is why the check had to be per skill rather than per
`OPCombatAbility`:

| skills | opcode | eqemu handler | in-combat requirement? |
|---|---|---|---|
| 8 Backstab, 10 Bash, 21 DragonPunch, 23 EagleStrike, 26 FlyingKick, 30 Kick, 38 RoundKick, 52 TigerClaw | `OP_CombatAbility` | `Client::OPCombatAbility`, `zone/special_attacks.cpp:325` | **NO** — target exists; not casting (non-bard) / feared / stunned / mezzed / DivineAura / dead; `MaxSkill()`; target id matches; `IsAttackAllowed`; `CombatRange`; reuse timer. |
| 16 Disarm | `OP_Disarm` | `Handle_OP_Disarm`, `zone/client_packet.cpp:5749` | **NO** — not dead/zoning; `HasSkill`; timer; source-id sanity; no corpse; has target; targets match; distance ≤ 400; not feigned either side; `IsAttackAllowed`. |
| 71 Intimidation | `OP_InstillDoubt` | `Handle_OP_InstillDoubt` `:9095` → `Mob::InstillDoubt`, `special_attacks.cpp:2390` | **NO** — timer; target is an NPC; no DivineAura; `CombatRange`. |

Backstab's extra rules live in `Mob::TryBackstab` (`:711`) — a 1H piercing
weapon, and `BehindMob` — and neither is an in-combat test. So **the opening
Backstab from stealth was the client refusing something the server accepts**,
which is what Will's ruling is about.

`requires_target` was left on all eleven: every one of those handlers really
does need a target, and clearing it would have swapped one wrong gate for
another.

### Shape of the revert

`requires_combat = true` deleted from the five `switch` arms in
`initializeSkillData()`. `checkCombatRequirement()` is **not** touched beyond
its comment — re-hollowing it to `return true` would have recreated exactly
the no-op gate T317 was dispatched to fix, and the "clear the data, keep the
mechanism" shape is the one T317's own Review named. Nothing outside
`skill_data.h`, `skill_manager.cpp` and `test_skill_gates.cpp` reads the flag
(grepped across `include/`, `src/`, `tests/`).

### The now-dormant message

`getSkillUseError()`'s `"You must be in combat"` is unreachable **from shipped
data** once no skill sets the flag. It was kept, not deleted, and the call is
argued at the site:

- **It is not T159's defect class.** T159's message *lied* — it blamed facing
  for an endurance failure, and could be produced when facing was fine. This
  one is produced only when `checkCombatRequirement()` actually refused, so it
  can never name the wrong reason.
- Deleting it would have broken T159's other standing rule by halves — a check
  with no message — and left `checkCombatRequirement()` a predicate whose
  refusal is silent, which is the T317 defect again.
- Its exact shape is board item 35's: a real message behind data nothing
  populates. Item 35 is left standing for the same reason.
- **It is not untested.** Three `MechanismIsIntact_*` tests set the flag by
  hand through the public mutable `getSkill()` and assert the refusal, the
  message, the aggro branch and the not-my-target negative — so the mechanism
  cannot silently rot back into a no-op.

### Tests — what changed, and what each assertion was pinning

`tests/test_skill_gates.cpp`, section B1 rewritten; **20 tests (was 16)**.

| T317 test | verdict | now |
|---|---|---|
| `NEGATIVE_KickIsRefusedWithATargetButNoCombat` | pinned the **over-strict gate** | inverted into `T325_AnOpeningKickIsPermittedWithATargetAndNoCombat` — the acceptance case |
| `DisengagingPutsTheCombatGateBack` | pinned the **over-strict gate** | folded into `EngagingAutoAttackDoesNotChangeTheAnswerEither`, which now asserts the answer is TRUE on both sides of the toggle |
| `EngagingAutoAttackSatisfiesTheCombatGate` | pinned **real behaviour** (Kick allowed in combat) — still true | same test, widened as above |
| `BeingAttackedCountsAsCombatEvenWithAutoAttackOff` | its `ASSERT_FALSE` precondition pinned the **over-strict gate**; the aggro branch itself is real mechanism | `MechanismIsIntact_BeingAttackedByTheTargetSatisfiesIt`, flag set by hand |
| `NEGATIVE_AggroFromSomeoneElseDoesNotUnlockTheGate` | pinned the **over-strict gate** (though the scoping it checks is a real property of the predicate) | `MechanismIsIntact_AggroFromSomeoneElseDoesNotSatisfyIt` |
| `SkillsWithoutTheCombatFlagAreUntouched` | pinned **real behaviour** | unchanged |
| all of section A, B2 (facing) and B3 (T159) | pinned **real behaviour** | **unchanged, all still pass** |

New beyond the inversions: `T325_AnOpeningBackstabFromStealthIsPermitted`
(the rogue opener, behind the target with auto-attack off and no aggro),
`T325_NoShippedSkillCarriesTheCombatFlag` (sweeps all 100 skill slots, so
re-adding the flag anywhere is a deliberate act with a test to answer to),
`T325_TheElevenT317FlaggedStillRequireATarget`, `ATargetIsStillRequired`
(the gate that DOES have a server counterpart, unchanged), and
`MechanismIsIntact_TheFlagStillRefusesAndStillSaysWhy`.

`FindOffsets()`/`Offsets` moved above section B1 (B1 now uses them for the
Backstab-opener case). Move-only.

### Failing-before evidence

`include/client/skill/skill_data.h` was reverted alone, via an exported patch
(`git diff > x.patch; git apply -R` — **never `git stash`**, which is
cross-slot shared state), the test binary rebuilt against the new tests, and
run:

```
[  FAILED  ] SkillGateTest.T325_AnOpeningKickIsPermittedWithATargetAndNoCombat
[  FAILED  ] SkillGateTest.T325_AnOpeningBackstabFromStealthIsPermitted
[  FAILED  ] SkillGateTest.T325_NoShippedSkillCarriesTheCombatFlag
[  FAILED  ] SkillGateTest.EngagingAutoAttackDoesNotChangeTheAnswerEither
[  PASSED  ] 16 tests.
```

The patch was then re-applied and the file verified back to 0 occurrences of
`requires_combat = true`. With T325's `skill_data.h` in place: **20/20 pass**.

### The facing check still works

Untouched, and confirmed by its own tests passing unchanged in the same run:
`NEGATIVE_BackstabIsRefusedFromTheFrontOfTheTarget` (with its no-cooldown
assertion, so `sendSkillUsePacket()` demonstrably never ran),
`BackstabIsAllowedFromBehindTheTarget`,
`TheGateFollowsTheTargetsHeading_NotThePlayers`,
`AnUnknownTargetDoesNotProduceAFacingComplaint`, all four
`BehindTargetMathTest` cases, and `T159_ABackstabShortOfEnduranceBlamesEnduranceNotFacing`.
T159's check-and-message pairing is intact; nothing in
`checkPositionRequirement()` or `behind_target.h` was edited. The new
`T325_AnOpeningBackstabFromStealthIsPermitted` strengthens it slightly: facing
is now proven to gate Backstab **outside** combat as well, where before every
facing test ran with auto-attack forced on.

### Deviations

None. Territory held exactly: `include/client/skill/skill_data.h`,
`src/client/skill/skill_manager.cpp`, `tests/test_skill_gates.cpp`,
`refactor/deferred.md`, this plan.
