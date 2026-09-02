# T314 — Spell targeting picks the wrong targets: group membership, and two off-by-one body-type tables

**Slot: slot2. Branch `task/T314` cut from `fable`. x86 only. No Pi.
NO eqemu for the fix — slots 1 and 3 are contending for the server. Design the
acceptance around headless tests; take a live leg ONLY if the lock is free
when you get there, and say plainly which evidence is live and which is not.**

Closes **tier 2 item 5** and the **tier 2 addendum (body-type numbering)** of
`refactor/deferred.md`'s class-A list. One unit because both defects are the
same sentence — *a spell lands on the wrong creatures* — and both live in
`src/client/spell/`.

## Defect 1 — group spells target by PROXIMITY, not group membership

`SpellTypeProcessor`'s group-target loop
(`src/client/spell/spell_type_processor.cpp:246-281`) adds the caster, then
walks **every non-corpse player entity inside `range`**:

```
// TODO: When group system is implemented, get actual group member IDs
// For now, find nearby players within range as a placeholder
...
    // TODO: Check if actually in group
    // For now, just check range
    targets.push_back(entity_id);
```

*Impact:* a group heal or buff **lands on strangers standing nearby and misses
group mates who are out of range** — wrong on both sides, in a game where
standing next to strangers is the normal case.

**The premise of the TODO is stale: the group system EXISTS.**
`eqt::state::GroupState` (`include/client/state/group_state.h:49`) is a live
domain in the single `GameState` (T286/T288 swept it), with `inGroup()`,
`memberCount()`, `getMember(index)`, `findMemberByName()` and a
find-by-spawn-id sibling. Use it.

**Decide and state the range question explicitly.** Real EverQuest group spells
still have a radius; "in the group" and "in range" are an AND, not a
replacement. Read what the spell data actually provides before choosing, say
which you implemented, and pin it. Do not silently drop the range check while
fixing the membership check.

## Defect 2 — two live copies of the body-type numbering, both wrong

The canonical copy already exists: `include/client/eq_body_types.h`, transcribed
from eqemu's `common/bodytypes.h` (the server that actually stamps the value),
added by T308. Two consumers still hand-roll their own:

- **`SpellManager::CheckTargetBodyType()`** — `src/client/spell/spell_manager.cpp:456-469`:
  local `BT_*` constants **off by one for every value** (`BT_HUMANOID = 0`,
  `BT_UNDEAD = 2`, `BT_GIANT = 3`, `BT_ANIMAL = 20`, `BT_PLANT = 24`,
  `BT_DRAGON = 25`, …) against the canonical `Humanoid = 1`, `Undead = 3`,
  `Giant = 4`.
- **`SpellTypeProcessor`** — `src/client/spell/spell_type_processor.cpp:584-591`:
  a THIRD, differently-wrong set — `21` for Plant (21 is Insect; Plant is 25)
  and `22` for Giant (22 is Monster; Giant is 4). Only its `26` for Dragon is
  right. Note the `Animal` arm immediately above it
  (`// Would need body type check - placeholder` → `return true`) accepts
  **everything**; fix that with the rest.

*Impact:* every undead / animal / summoned / plant / giant / dragon restricted
spell tests the wrong body type — refused against a legitimate target, accepted
against an illegitimate one.

**Repoint both at `client/eq_body_types.h`. Do not transcribe the numbers a
fourth time.** T308 fixed the display-only copy and deliberately left these two
because they change live behaviour; the fourth and worst copy
(`CombatManager::IsHumanoid()`'s race-id table) is already closed by T308 Leg 1
— read `tests/test_hunt_target_body_type.cpp` for the shape it used.

## Acceptance

- **Group targeting:** a test with a caster, two real group members (one
  deliberately OUT of range), and two non-members standing adjacent. Assert the
  target set is exactly the members that qualify — and assert the **negative**,
  that the adjacent strangers are NOT in it. The negative is the whole defect.
- **Body types:** a table test over the canonical values, asserting each
  restricted target type accepts its own body type and rejects at least one
  neighbouring value. Include Plant(25)/Insect(21) and Giant(4)/Monster(22) —
  the two the old code actually confused.
- **A grep-style guard** that no `BT_*` literal or bare body-type integer
  survives in `src/client/spell/` — the point of the unit is that the numbering
  has ONE home. (`tests/test_duplicate_state_stores.cpp`'s `kKnownRemaining`
  pattern is the precedent for a test that cannot drift from its doc.)
- Full `ctest -j6` (**NOT -j24**), zero new failures vs `refactor/test_baseline.txt`.
- `check_file_sizes.sh --files` no new FAILs.

## Territory

`src/client/spell/spell_type_processor.cpp`, `src/client/spell/spell_manager.cpp`,
`include/client/eq_body_types.h` (only if a value is genuinely missing — if you
change one, say why the server's header disagrees), plus your tests.
⚠ Concurrent: slot1 T311 Leg B touches `src/client/spell/spell_cast_checks.cpp`
— **stay out of that file**. slot3 T313 is in `graphics/ui/`.
