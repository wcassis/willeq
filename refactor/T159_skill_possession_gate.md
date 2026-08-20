# T159 — A real possession gate for skills, and a real cap table

**Slot: slot1. x86. NO eqemu, NO lock, NO Pi.** T160 (corpse naming) runs in
parallel in slot2 and touches `combat.cpp`/`eq.cpp` — **you touch neither.**

**Territory: `src/client/skill/`, `include/client/skill/`, `config/` (one new
data file), `tests/`, `refactor/`. Do NOT touch `src/client/combat.cpp` or
`src/client/eq.cpp` — those are T160's.**

T158 proved on a live server that **`activateSkill` has no possession gate at
all**: `Oraclerogue` (Rogue with Taunt 0) sent `OP_Taunt`, our client printed
**"You use Taunt!"**, and the server discarded the packet in silence. `Testmelee`
got a reply in 52 ms for the identical command. Read T158's Review first.

## The two defects

1. **`SkillData::hasSkill()` (`skill_data.h:101`) is
   `skill_id < 75 && name != "Unknown"`** — it never looks at the value, the
   class, or anything else. Its comment says it exists to decide what to
   *display*, and `canUseSkill` (`skill_manager.cpp:158`) uses it as a
   *possession* test. One predicate doing two jobs, and it is wrong for one.
2. **`SkillManager::loadClassSkillCaps()` (`skill_manager.cpp:48`) is a stub**
   that zeroes every cap and then invents eight of them (`player_level * 5` for
   Defense/Dodge/Offense/BindWound, flat 200 for Swimming/SenseHeading/
   AlcoholTolerance/Begging) **for every class alike**. `updateSkill()` then
   patches `max_value = current_value` whenever a nonzero value arrives, so the
   caps are part invention, part echo.

## ⚠ THE TRAP — gate on CLASS, not on LEVEL

The obvious gate ("cap at my level > 0") is **wrong and T158 has the
counter-example**: Apply Poison's rogue cap starts at level **18**, yet
`Oraclerogue` at level **10** with skill **0** applied poison successfully —
`Handle_OP_ApplyPoison` never consults `HasSkill`, it checks
`IsPoison && GetClass() == Rogue`. A level-gated client check would have blocked
a send that demonstrably works, trading a false positive for a false negative.

**The correct predicate is "can my class EVER have this skill".** Taunt has no
row for class 9 at any level (that is why the server binned it); Apply Poison
has one from level 18. Gate on presence, not on the level threshold. If you
believe a level gate is also warranted somewhere, say so in the Review and leave
it out of this unit.

## The data — already exported for you

`/mnt/projects/fable/review/T159/skill_caps_by_class.tsv`, **603 rows**, one per
`(class_id, skill_id)` with `first_level` and `max_cap`, collapsed by the
orchestrator from this server's `peq.skill_caps` (58,359 raw rows, 16 classes,
76 skills, levels 1–105). **A pair absent from that file means the class can
never have that skill.** Its README states the provenance; carry that into
whatever you ship so the next reader knows what the numbers are and are not.

Ship it as data under `config/` in the style of the existing files there
(`race_models.json`, `spell_effects.json` are the precedents) and load it at
init. **Do not hand-type 603 rows into a header.** If the file is missing or
unparseable at runtime, fail loud in the log and fall back to today's
permissive behaviour rather than silently blocking every skill — a data-file
regression must not brick the skill system.

## The work

1. Replace the `loadClassSkillCaps` stub with a real load from that data.
2. Split the predicate: keep a display test (all valid skills) and add a real
   possession test (this class can have this skill), and make `canUseSkill` use
   the possession one. Fix `updateSkill`'s `max_value = current_value` patch-up
   now that a real cap exists.
3. When the gate refuses, **tell the player something true** — the current path
   prints "You use Taunt!" for a skill that went nowhere. `getSkillUseError()`
   already exists; note that it returns "You must be behind your target"
   unconditionally for `requires_behind`, which is a latent bug of the same
   family (Backstab only). Fix it if it is one line; report it if not.

## Acceptance

- A test proving the live T158 case: a **Rogue cannot activate Taunt**, and the
  packet is not sent.
- A test proving the counter-example still works: a **Rogue CAN activate Apply
  Poison at level 10 with skill value 0**.
- A test that a Warrior CAN taunt.
- The gate's data comes from the shipped file, not from a fixture invented in
  the test.

## Verification

Full protocol bar. Baseline **2570/2571** at tip `4f69fb76`, sole expected
failure the documented ALSA `AudioManagerTest.PreloadCommonSounds`.
`tests/test_command_processor.cpp` is over the 3000-line ceiling — new tests go
in a new file.

**Not claimed: any wire behaviour.** A live re-check is owed and cheap (the
T158 script re-run should now refuse Taunt on the rogue before sending).

## STOP conditions

- The data file cannot express something the gate needs (report what is missing).
- The fix would require touching the packet layer or the action layer.

## Review

Executed in `/mnt/projects/fable/wt/slot1`, task branch
`task/T159-skill-possession-gate` off `fable` tip `4f69fb76`. No eqemu, no
lock, no Pi; `main` and the sibling slots untouched. **`src/client/combat.cpp`
and `src/client/eq.cpp` were not touched** — T160's territory, and it turned
out nothing needed to change there (see "Telling the player something true").

**Build** `cmake --build build -j24` clean, unpiped. **Suite**
`ctest --output-on-failure` **2577/2578** — baseline 2570/2571 plus this
unit's 7 new tests, sole failure the documented ALSA
`AudioManagerTest.PreloadCommonSounds`. **Sizes**
`scripts/refactor/check_file_sizes.sh fable`: 6 changed files, 6 ok, 0 warn,
0 fail (largest: `skill_manager.cpp` 778).

### The possession predicate

**"Does `config/skill_caps.json` carry a row for (my class, this skill) at ANY
level?"** — presence, not the level threshold, exactly as the plan's trap
demands. Nothing in the activation path reads `first_level`.

New `EQ::SkillCapTable` (`include/client/skill/skill_cap_table.h`,
`src/client/skill/skill_cap_table.cpp`, 90 + 133 lines) is a process-wide
singleton that parses the shipped file with jsoncpp — the same library and the
same "load a file under `config/` at init" shape as `spell_effects.json`. It
exposes `classCanEverHaveSkill(class, skill)` and a `getEntry()` returning
`{first_level, max_cap}`.

`config/skill_caps.json` is the 603 input rows verbatim, as
`caps[class_id][skill_id] = [first_level, max_cap]`, 38 lines, one line per
class. Its `_comment` block carries the README's provenance in full — the
58,359 raw rows, the 16 classes / 76 skills / levels 1–105, what `first_level`
and `max_cap` mean, that absence means "never", and *why* `first_level` is not
a gate (the Apply Poison counter-example, named). Nothing was hand-typed: the
file was generated from the TSV.

`SkillManager::loadClassSkillCaps()` is no longer a stub. It calls
`SkillCapTable::ensureLoaded()` and then, per skill id, sets
`class_can_have`, `class_first_level` and `max_value` from the table. The
eight invented caps (`player_level * 5` for Defense/Dodge/Offense/BindWound,
flat 200 for Swimming/SenseHeading/AlcoholTolerance/Begging, identical for all
sixteen classes) are gone. `player_level` is now unused by the possession
flag, deliberately, and the comment says so.

**Failure mode as specified**: a missing, unparseable, or empty file leaves
`isLoaded()` false, logs at ERROR naming the path and stating that the gate is
disabled, and every skill stays permitted — pre-T159 behaviour. A second
WARN fires from `loadClassSkillCaps` naming the class. A data regression
degrades the client to "no gate", never to "no skills".

### How the display test and the possession test were split

`SkillData::hasSkill()` is gone; in its place two named predicates:

* `isDisplayableSkill()` — the old body verbatim (`skill_id < 75 && name !=
  "Unknown"`). Every UI listing kept it: `getAllSkills`,
  `getSkillsByCategory`, `getActivatableSkills`, `getSkillCount`. The skills
  window is unchanged, and test 5 pins that (a Rogue still *lists* Taunt, at
  75 rows, while not possessing it).
* `canClassHaveSkill()` — reads the new `SkillData::class_can_have` flag set
  by the cap load. Defaults `true`, so a `SkillData` that never went through a
  cap load behaves exactly as it did before T159.

`SkillManager::hasSkill()` — whose declaration always said "Check if player
has a specific skill" while forwarding to the display test — is now
`isDisplayableSkill() && canClassHaveSkill()`. `canUseSkill()` checks both in
sequence, and `getSkillUseError()` gained the matching branch. Renaming rather
than overloading was deliberate: the whole defect was one name answering two
questions, and a reader who now types `hasSkill` on a `SkillData` gets a
compile error instead of the wrong answer.

`SkillData` also gained `class_first_level` (loaded, exposed, gates nothing)
and two new fields' worth of comments recording that `max_value` is the class
**lifetime** cap, not the cap at the current level.

### `updateSkill`'s `max_value = current_value` patch-up

It survives, but **only as the fallback**. Both copies (`updateSkill` and
`updateAllSkills`) now carry `&& !SkillCapTable::instance().isLoaded()`. With
a real table loaded, a cap is a cap: an arriving skill value can no longer
manufacture one, and in particular a stray value for a skill the class cannot
have can no longer conjure a nonzero `max_value` for it. With no table loaded
the echo is the only cap information the client has, so it comes back. Test 6
pins both directions (Rogue + Taunt stays 0 with the file; Rogue + Taunt
becomes 77 without it).

### `getSkillUseError`'s `requires_behind` — T158 defect 3

**Confirmed, and it was a one-liner, so it is fixed.** The branch
unconditionally returned `"You must be behind your target"` for any
`requires_behind` skill, *ahead of* the resource check, while
`checkPositionRequirement()` unconditionally returns `true` — so
`canUseSkill()` can never fail for that reason and reporting it is always a
lie. Concretely: a Backstab refused for lack of endurance blamed the player's
facing. The `return` is removed; the branch is now an empty `TODO` paired with
the one already in `checkPositionRequirement()`, with a comment saying the two
go back in together when facing is actually implemented. Only Backstab sets
the flag, so nothing user-visible changes today — it stops being a trap for
whoever implements facing. Not otherwise touched: implementing the facing
check is a real piece of work (position + heading math) and is out of scope
here.

### Telling the player something true

The gate returns `"Your class cannot use Taunt"` from `getSkillUseError()` —
naming the class as the reason, which is what is true. The generic
`"You do not have this skill"` (which before T159 was *unreachable*, since the
predicate above it was the display test) stays for invalid ids.

**No change to `eq.cpp` was needed**, which is fortunate given T160 owns it:
its `setOnSkillActivated` handler already had the honest else-branch,
`"Cannot use {skill}: {message}"`. The bug was never the message — it was that
the failure branch could not be reached. So the live T158 line
`"You use Taunt!"` becomes
`"Cannot use Taunt: Your class cannot use Taunt"`, and test 2 asserts that
string off the real bridge `SystemMessage` event, not off the callback.

### The tests

New file `tests/test_skill_possession_gate.cpp` (312 lines), seven tests, on
the `test_combat_manager.cpp` seam — `MakeEq()` + live `CombatManager` and
`SkillManager` + `TestBridge`, no network, no graphics device, no server.
`tests/test_command_processor.cpp` was not grown (it is over the 3000-line
ceiling). **The gate's data is the shipped file**: the fixture loads
`WILLEQ_REPO_ROOT "/config/skill_caps.json"` by path and asserts 603 rows —
no synthetic cap table anywhere in the file, so a broken shipped file fails
these tests instead of passing them.

The three required ones:

1. **`RogueCannotActivateTaunt_AndNothingIsSent`** — the live T158 case. Rogue
   (class 9) level 10, Taunt 0, with a real NPC spawned and targeted through
   `CombatManager` so nothing *else* can be the refusal reason.
   `activateSkill(73, target)` returns false, and the truthful refusal appears
   on the bridge while `"You use Taunt!"` does not. **The packet-not-sent
   proof is the cold cooldown**: `activateSkill` calls `markUsed()` only on
   the branch that has already called `sendSkillUsePacket()`, so Taunt's 5 s
   recast still reading 0 ms is a witness that the send branch never ran. (A
   direct wire assertion is not available on this seam —
   `EverQuest::QueuePacket` silently no-ops with a null zone connection and
   increments no counter; T23's Review recorded the same limitation.)
2. **`RogueCanActivateApplyPoisonAtLevel10WithSkillZero`** — the
   counter-example. It first asserts the configuration that a level gate would
   have refused (`class_first_level == 18`, player level 10, skill value 0)
   and *then* that `canUseSkill`/`activateSkill` both say yes. If anyone later
   "improves" the gate into a level check, this test is what fails.
3. **`WarriorCanActivateTaunt`** — Warrior (class 1), same target, activation
   succeeds, cooldown is now running, and the bridge carries `"You use
   Taunt!"`.

Plus four supporting: `ShippedCapTableCarriesTheThreeT158Facts` (the file
itself says (9,73) absent, (9,6) = [18,200], (1,73) = [1,230]),
`DisplayListingStillShowsSkillsTheClassCannotHave`,
`CapsAreRealPerClassValuesNotTheStubsInventions`, and
`MissingCapFileFallsBackToPermissiveBehaviour` (the fixture reloads the real
file in both SetUp and TearDown, since the table is a singleton).

### Deviations

1. **`SkillData::hasSkill()` was renamed, not kept as an alias.** The plan said
   "keep a display test"; the test is kept, the *name* is not. Reason above.
   All six call sites moved with it; no caller outside
   `src/client/skill/` used it (`eq.cpp` reads `getAllSkills()` and
   `skill->max_value`, both unaffected).
2. **`config/skill_caps.json` is not registered in
   `src/client/application.cpp`'s `checkFile()` startup manifest**, where
   `config/race_models.json` is. `application.cpp` is outside this unit's
   territory. Worth one line in a later unit — the fallback already logs at
   ERROR, so the loss is only the early warning.
3. **One planned assertion was wrong about the data and was rewritten.** The
   draft of test 6 assumed the stub's flat-200 Begging was invented *existence*
   for classes that cannot beg. It is not: all sixteen classes have rows for
   all eight of the stub's skills. Only the *values* were invented (Warrior
   Bind Wound is 525, not level*5 = 50). The test now pins four value
   mismatches instead. Recorded because "the stub was wrong" is true in a
   narrower way than the plan's phrasing implies.
4. **One transient build failure, not reproducible.** The first
   `cmake --build` after adding the new TU failed inside
   `GoogleTestAddTests.cmake` discovery for `test_eq_action_handler_enum_guards`
   — an unrelated target. An immediate rebuild with no source change succeeded,
   and every subsequent full build and the full ctest run were clean. Logged in
   case it recurs; it looks like a parallel-discovery race, not this unit.

### Two things the data file cannot express (reported, no STOP hit)

Neither blocked the gate, so neither is a STOP condition — but both are real
limits of this export and the next reader should know:

1. **There is no per-level cap curve.** `max_cap` is `MAX(cap)` over levels
   1–105, i.e. the class's lifetime cap. `SkillData::max_value` therefore now
   means "class lifetime cap", not "maximum skill at current level" as its old
   comment claimed. Both the header and the JSON say so explicitly. The skills
   window shows `current / max` and will now show, e.g., `9 / 525` for a
   level-10 Warrior's Bind Wound where it used to show `9 / 50`. **That number
   is more honest than the old one** (which was invented) but it is not the
   number a real EQ client shows. Fixing it needs a level-indexed export, which
   is a different (and much larger) data file — a decision for Will, not a
   silent one for this unit.
2. **`skill_caps` carries ids 75, 76 and 77**, above the client's
   `isDisplayableSkill()` ceiling of 75. They load into the table (the array is
   `MAX_PP_SKILL` = 100 wide) but no display or activation path can reach them.
   Harmless; noted so nobody reads the 603 vs "75 skills" mismatch as a bug.

### Should there also be a level gate?

The plan asked. **Recommendation: no — not without wire evidence per skill.**
T158 established that eqemu's handlers do not agree with each other about
`HasSkill`: Taunt consults it, Apply Poison does not. A client-side level gate
would be right for the first family and wrong for the second, and we currently
know which family a skill is in only by reading
`/mnt/projects/claude/EQEmu/zone/client_packet.cpp` handler by handler. The
cheap, honest version is a per-skill "the server checks HasSkill for this one"
flag derived from that audit — which is a source-reading unit, not this one.
Until then, refusing only what the class can *never* have is the form that
cannot produce a false negative.

### Owed

1. **A live re-check.** Not claimed here: no wire behaviour was observed by
   this unit. The T158 script re-run should now refuse Taunt on `Oraclerogue`
   before sending — expect `Cannot use Taunt: Your class cannot use Taunt` in
   the client log and **no** `Sent Taunt packet:` line — while
   `/useskill apply poison` on the same character still produces the server
   echo and the item deletion, and `Testmelee` still taunts. Cheap: three
   commands, one session, and it needs no `#level`/`#setskill`.
2. **The per-level cap curve** (limit 1 above), if Will wants the skills window
   to show a real level cap.
3. **`config/skill_caps.json` in `application.cpp`'s startup manifest**
   (deviation 2).
4. **The facing check** for Backstab, both halves together
   (`checkPositionRequirement()` and the `requires_behind` branch of
   `getSkillUseError()`).
