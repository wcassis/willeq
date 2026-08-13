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

(executor fills: the verified mapping table; whether `dismissPet()` had drifted;
the comments removed; proof the tests fail in both directions; the live result
per command, with what the pet actually did; anything else found with the same
literal-drift shape; deviations)

## Board

Close T95's `/pet` drift entry with the result, and record whether the same
pattern exists in any other command family.
