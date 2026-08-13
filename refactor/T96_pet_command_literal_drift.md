# T96 — `/pet attack` dismisses your pet: fix the drifted literals

**Slot: whichever is free. Headless fix + tests, then a LIVE leg — this one
genuinely needs the server. eqemu lock normally.**

**Territory: `src/client/action/action_dispatcher.cpp`, `tests/`, `refactor/`,
`/mnt/projects/fable/review/T96/`.**

Read T95's Review and its `/pet` table in `refactor/deferred.md` first.

## The defect

Commit `b1c722b0` renumbered `EQT::PetCommand` (`include/client/pet_constants.h`)
and never touched `action_dispatcher.cpp`'s hardcoded literals. Valid
enumerators are **1-14 and 16-21**; 0, 15 and 22+ have none.

| wrapper | sends | actually means | should send |
|---|---|---|---|
| `petAttack` | 2 | **`PET_GETLOST` — dismisses the pet** | 7 `PET_ATTACK` |
| `petBackOff` | 28 | invalid | 1 `PET_BACKOFF` |
| `petFollow` | 4 | `PET_HEALTHREPORT` | 8 `PET_FOLLOWME` |
| `petGuard` | 5 | `PET_GUARDHERE` — **correct** | 5 |
| `petSit` | 6 | `PET_GUARDME` | 9 `PET_SITDOWN` |
| `petTaunt` | 12 | `PET_HOLD` | 11 `PET_TAUNT` |
| `petHold` | 15 | invalid (target cmd, sends no packet) | 12 `PET_HOLD` |
| `petFocus` | 24 | invalid | 19 `PET_FOCUS` |
| `petHealth` | 0 | invalid | 4 `PET_HEALTHREPORT` |

**Verify every row against `pet_constants.h` yourself before changing
anything.** The table above is the orchestrator's reading; treat it as a
starting point, not gospel.

## The comments drifted too — and that is why this survived

`action_dispatcher.cpp:800` reads `// PET_ATTACK = 2 (from pet_constants.h)`
while the header says `PET_ATTACK = 7`. A reader checking the literal found a
comment confirming it and moved on.

**Delete these comments rather than correcting them.** Once the call sites use
named enumerators the comment is redundant, and a redundant comment is exactly
what drifted. Do not replace one stale assertion with a fresher one.

## The fix

Replace every literal with the named `EQT::PetCommand` enumerator. That is the
actual defence: a future renumbering is then picked up automatically instead of
silently diverging.

**Check `dismissPet()` separately** — it routes through
`m_handler->dismissPet()` rather than `sendPetCommand()`, so it may be fine, or
may have its own drift. Report either way.

Note the interaction with T95, which just landed: its guard already rejects the
four *invalid* values with an in-game message, so `petBackOff`, `petHold`,
`petFocus` and `petHealth` currently fail loudly. The four wrong-but-*valid*
ones (`petAttack`, `petFollow`, `petSit`, `petTaunt`) still go out silently.
After this unit all nine should be both valid and correct.

## Tests

Pin each wrapper to its expected enumerator, so a future renumbering breaks a
test instead of a player's pet.

- Assert on the **value passed to the handler**, per wrapper — this is exactly
  what `RecordingActionHandler` is good for, and unlike T90's case the value
  under test is the literal itself, so the fake is the right seam here.
- **Prove the tests can fail in both directions**, the standard T95 set: break
  a mapping and show red, then restore and show green. Report that you did it.

## Live leg — required, and the reason is specific

**Do not infer correctness from the packet.** This project has twice concluded
success from a well-formed message that did the wrong thing. Confirm the pet
actually obeys.

- Summon a pet (a caster character — `Testcaster` exists on `willeq_test`).
- Exercise each of the nine commands and record what the pet does.
- **`/pet attack` needs care**: before this fix it dismisses the pet, so
  verify it *attacks* afterwards and note that the old behaviour is gone.
- `/pet hold`, `/pet focus`, `/pet taunt` are toggles — confirm state changes
  rather than just absence of an error.
- Some commands produce a server chat response (`/pet health` reports status);
  capture those, they are the clearest evidence.

Session discipline: verify binary freshness against HEAD first; force daylight
`/say #set time 11 0`; pad scripts far longer than you think; `-d 4` **plus**
`--log-level=DEBUG` where wire-level lines are needed; restore DB state and
confirm by `SELECT` **after logout**; clean teardown and delete the lock.

## What you may not do

- Do not renumber `PetCommand` or change `pet_constants.h`. The header matches
  the Titanium wire protocol; the call sites are what is wrong.
- Do not "fix" the four commands T95's guard now rejects by loosening the
  guard. They are rejected because their literals are wrong — fix the literals.
- Do not extend to other command families. If you notice the same
  literal-versus-enumerator pattern elsewhere, **note it for follow-up** rather
  than expanding scope.

## Verification

Full build and suite (expect 2095/2096 plus your new tests; sole pre-existing
failure the documented ALSA `AudioManagerTest.PreloadCommonSounds`). Visual
gate — goldens should not move.

Findings and the live evidence to `/mnt/projects/fable/review/T96/` with a
README. Open any image you produce and describe what is actually visible.

## Review

Executed in `/mnt/projects/fable/wt/slot2`, task branch `task/T96-pet-literals`,
base `fable` tip `d081939d`. Slot2, no other unit's territory touched.
eqemu leg used the lock: T94(slot1) held it initially, waited ~13 minutes in
bounded polling (per protocol), acquired cleanly once freed, tore down and
deleted the lock at the end. Full findings and live-run logs are in
`/mnt/projects/fable/review/T96/README.md` — this section summarizes.

### Verified mapping table

Cross-checked every row against `include/client/pet_constants.h` directly
(not just the orchestrator's table). The plan's table was accurate as
written:

| wrapper | was sending (literal) | now sends | value |
|---|---|---|---|
| `petAttack` | 2 (`PET_GETLOST` — dismisses) | `EQT::PET_ATTACK` | 7 |
| `petBackOff` | 28 (invalid) | `EQT::PET_BACKOFF` | 1 |
| `petFollow` | 4 (`PET_HEALTHREPORT`) | `EQT::PET_FOLLOWME` | 8 |
| `petGuard` | 5 (`PET_GUARDHERE` — already correct) | `EQT::PET_GUARDHERE` | 5 |
| `petSit` | 6 (`PET_GUARDME`) | `EQT::PET_SITDOWN` | 9 |
| `petTaunt` | 12 (`PET_HOLD`) | `EQT::PET_TAUNT` | 11 |
| `petHold` | 15 (invalid gap) | `EQT::PET_HOLD` | 12 |
| `petFocus` | 24 (invalid) | `EQT::PET_FOCUS` | 19 |
| `petHealth` | 0 (invalid) | `EQT::PET_HEALTHREPORT` | 4 |

### `dismissPet()` — checked separately, NOT drifted

`ActionDispatcher::dismissPet()` routes to `m_handler->dismissPet()` →
`EqActionHandler::dismissPet()` (`src/client/eq_action_handler.cpp`), which
already calls `m_eq.SendPetCommand(EQT::PET_GETLOST, 0)` — the named
enumerator, correct value. No change needed. (Its adjacent comment, `//
PET_GETLOST = 29 from pet_constants.h`, IS stale — the real value is 2 —
but the code itself uses the enumerator, not the literal, so behavior is
unaffected; that comment is in `eq_action_handler.cpp`, outside this unit's
territory, so left alone and just noted here.)

### Comments removed, not corrected

All nine `// PET_X = N (from pet_constants.h)` comments in
`action_dispatcher.cpp` were deleted (not fixed to the new number) —
several were already stale before this unit (e.g. `petAttack`'s claimed
`PET_ATTACK = 2` vs. the header's actual `PET_ATTACK = 7`), and correcting
them in place would just reset the clock on the same failure mode per the
plan's explicit instruction. The call sites are now self-documenting
(`EQT::PET_ATTACK` needs no comment repeating what it says).

### Tests: pinned, proven able to fail in both directions

`tests/test_action_dispatcher.cpp`'s existing
`PetFamily_EachConvenienceMethodUsesItsOwnCommandId` test previously pinned
the OLD DRIFTED literals as if they were correct (it was itself part of the
problem — a green test asserting the wrong number). Updated to assert the
corrected `EQT::PetCommand` enumerator per wrapper (both a symbolic
`std::to_string(static_cast<int>(EQT::PET_X))` form and, in a new second
test `PetFamily_EachConvenienceMethodSendsTheDocumentedWireValue`, the raw
expected wire number directly, so a future accidental change to
`pet_constants.h` itself would also be caught, not just a drift back in
`action_dispatcher.cpp`). `RecordingActionHandler` was the seam (asserts on
the exact value passed to the handler, per wrapper) — correct per the
plan, since the value under test IS the literal being sent, unlike T90's
case.

**Proof of both-directions failure**: backed up `action_dispatcher.cpp`,
changed `petAttack()` to send `EQT::PET_GETLOST` instead of
`EQT::PET_ATTACK` (reintroducing the exact historical bug), rebuilt just
`test_action_dispatcher`, ran it: both new/updated pet tests went red with
the exact expected-vs-actual mismatch (`"sendPetCommand(2, 0)"` vs.
`"sendPetCommand(7, 0)"`). Restored from the backup, `diff` confirmed
byte-identical, rebuilt, ran again: all pet tests green.

### Live result per command

See `/mnt/projects/fable/review/T96/README.md` for the full write-up; the
key evidence table is there. **Headline: the live leg found that
`ActionDispatcher::petAttack()`/etc. — the exact code this unit fixed —
has zero live callers.** `Application::DispatchSlashCommand()` tries
`EverQuest::m_command_registry` first; that registry's own, independent
`/pet` implementation (`src/client/eq_commands_spells.cpp`) was already
using correct named enumerators and always wins before
`action::CommandProcessor::cmdPet()` (the sole caller of the code fixed
here) is ever reached — confirmed by reading `DispatchSlashCommand()` and
empirically (every `/pet` line in the live logs shows `registry=game`,
never `registry=action`). This is at least a tenth instance of the
project's "implemented-but-unreached" pattern (see `refactor/deferred.md`'s
`## T96` entry for the full writeup and the "3 competing command systems"
follow-up note).

Given that, the live leg was re-purposed to do what it honestly could:
confirm the pet actually obeys the **live** `/pet` implementation
end-to-end (real GM-summoned pet, `#makepet skel_pet_11_`, a
petcontrol=2/"Normal" pet type that listens to all commands, verified
against eqemu server source), which is what a real player experiences
today regardless of which client file handles it:

- `/pet attack` (with `a_rat` targeted): pet says *"Attacking a rat
  Master."*, hits and kills the rat, `You gain experience!!` — attacks,
  does not dismiss.
- `/pet back`: *"Sorry, Master..calming down."*
- `/pet follow`: *"Following you, Master."*
- `/pet guard`: *"Guarding with my life..oh splendid one."*
- `/pet sit` / `/pet stand`: *"Changing position, Master."* both times
  (distinct sit/stand state changes).
- `/pet taunt` (toggle): *"No longer taunting attackers, Master."* — flips
  the pet's taunt state off (it defaulted on for this NPC).
- `/pet notaunt` (explicit off): same message, confirming the explicit-off
  path also works and the state stayed off.
- `/pet hold` / `/pet nohold` / `/pet focus`: packet sent
  (`"Sent pet command: hold/focus"`), but **no server response and no
  observable state change** — root-caused to an eqemu AA gate
  (`aabonuses.PetCommands[PetCommand]`) neither available test character's
  class can purchase; not a T96 defect, documented in
  `refactor/deferred.md`'s `## T96` entry.
- `/pet health`: *"I have 100.00 percent of my hit points left."*
- `/pet dismiss`: *"As you wish, oh great one."*, pet despawns cleanly.

Testchar's DB position after logout: `zone_id=36, x=-77.875, y=21.875, z=3,
heading=190` — matches the documented restore baseline (small float
rounding only; character never moved meaningfully during the session).
Verified via `SELECT` after logout, no restore needed.

### Other literal-drift-shaped findings

- The "3 competing command systems" duplication itself (`/pet` implemented
  independently in `eq_commands_spells.cpp` AND `action_dispatcher.cpp` AND
  potentially the renderer bridge) — noted for a follow-up dedup, not
  fixed (explicitly out of scope: "do not extend to other command
  families").
- No other literal-vs-enumerator drift was noticed in the touched files
  during this unit; the sweep was scoped to the nine `/pet` wrappers named
  in the plan, per the plan's own scope limit.

### Build/test verification

1. `cmake --build build -j24` — clean, no new warnings.
2. `ctest --test-dir build --output-on-failure` — **2101/2102 passed**.
   This unit added one net-new test function
   (`PetFamily_EachConvenienceMethodSendsTheDocumentedWireValue`; the
   other pet test, `PetFamily_EachConvenienceMethodUsesItsOwnCommandId`,
   was updated in place, not added). The 2102 total vs. the plan's stated
   2095/2096 baseline reflects that plus normal suite growth from other
   units merged to `fable` since T95's baseline was written. Sole failure
   the documented ALSA `AudioManagerTest.PreloadCommonSounds`.
3. `bash scripts/build-gles2-x86.sh` — clean.
4. `scripts/ci/visual_gate.sh` — **PASS** (qeynos2/gfaydark/befallen);
   goldens untouched (this unit made no rendering changes — confirmed by
   the touched-files list: `action_dispatcher.cpp`,
   `test_action_dispatcher.cpp`, `deferred.md`, this plan file).
5. `bash scripts/build-arm-noble.sh` — clean cross-compile.
6. `scripts/refactor/check_file_sizes.sh fable` — 0 WARN, 0 FAIL
   (`action_dispatcher.cpp` 1019 lines, `test_action_dispatcher.cpp` 778
   lines).

### Deviations from the plan

- **The live leg's evidentiary value is different from what the plan
  anticipated**, discovered only by running it: the plan (following T95's
  static-analysis-only finding) expected the live leg to prove this unit's
  `action_dispatcher.cpp` diff changed live `/pet` behavior. It instead
  proved the code fixed here has no live caller at all — a separate,
  already-correct implementation handles `/pet` live. Flagged prominently
  per protocol rather than silently writing up the live results as if they
  validated the diff. The fix itself (verified via unit tests, both-
  directions-provable) is still correct and complete against the plan's
  literal scope (`action_dispatcher.cpp`'s nine wrappers).
- **`/pet hold` and `/pet focus` toggle states could not be behaviorally
  confirmed live**, unlike the plan's expectation — root-caused to an
  eqemu AA gate unrelated to this unit (see above), not silently passed
  over.
- **Subcommand vocabulary mismatch on the first live attempt**: assumed
  `CommandProcessor::cmdPet()`'s vocabulary (`backoff`, bare `/target`
  targeting nearest) would apply live; the actually-live
  `eq_commands_spells.cpp` handler wants `back` (not `backoff`) and
  `/target <name>` (bare `/target` is a different, also-live
  `eq_commands_movement.cpp` handler requiring a name). Both logs kept in
  `review/T96/` for traceability; the corrected second run is the primary
  evidence.
- No STOP condition was hit. `pet_constants.h` was not touched. No
  scope extension beyond the nine named wrappers.

## Board

Close T95's `/pet` drift entry with the result, and record whether the same
pattern exists in any other command family.
