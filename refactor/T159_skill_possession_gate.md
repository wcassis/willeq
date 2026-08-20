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

(executor fills this in)
