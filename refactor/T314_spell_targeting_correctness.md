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

---

## Review (slot2, 2026-09-02)

Both defects closed. No eqemu: `/mnt/projects/fable/.eqemu-lock` was held by
`T311-legB 2026-09-02 slot1` for the whole unit, so **every piece of evidence below
is headless (ctest on this x86 host). Nothing here is a live-session observation.**

### The range decision, and why

**Membership AND range.** The membership check was added; the range check was kept,
and a second defect in the radius itself was fixed on the way.

The oracle settles it. eqemu `Group::CastGroupSpell()` (`zone/groups.cpp:813-852`):

```
range = caster->GetAOERange(spell_id);
float range2 = range*range;
for(z=0; z < MAX_GROUP_MEMBERS; z++) {
    if(members[z] == caster) { caster->SpellOnTarget(spell_id, caster); }
    else if(members[z] != nullptr) {
        distance = DistanceSquared(caster->GetPosition(), members[z]->GetPosition());
        if(distance <= range2 && distance >= min_range2) { ...SpellOnTarget... }
        else LogSpells("Group spell: [{}] is out of range [{}] ...");
    }
}
```

Three things fall out of that, all of them now implemented:

1. **The caster is not range checked.** He is cast on from his own group slot before
   any distance is measured. `findGroupTargets()` pushes `caster_id` unconditionally,
   and `SpellGroupTargeting.CasterIsAlwaysAffectedRegardlessOfRadius` pins it at
   radius 0.
2. **Every other member is range checked.** So "in the group" does not replace
   "in range".
3. **The radius is `Mob::GetAOERange()`, not the cast range** —
   `zone/spells.cpp:5666`: `aoe_range`, else `range`, else `10`.
   `processGroupSpell()` was passing `spell.range` unconditionally. **That is a
   second live defect, found while settling this question**: for any group buff whose
   row carries an `aoe_range` (the normal case for group spells) the client was
   measuring against the wrong radius. Extracted as
   `SpellTypeProcessor::groupSpellRadius()` and pinned by three tests.

`min_range` is *not* implemented here — `SpellData` has no `min_range` field, so
there is nothing to read. Named under "deliberately not fixed" below.

**Membership matching:** spawn id first (`GroupState::findMemberBySpawnId`), name as
a fallback. Not belt-and-braces — `eq_group.cpp:414-428` only resolves a member's
`spawnId` if that member was already in `m_entities` when the `OP_GroupUpdate`
arrived. A group mate who zones in afterwards keeps `spawnId == 0`, so spawn-id
matching alone would silently drop them, which is the "misses group mates" half of
the original defect wearing a different hat.
`SpellGroupTargeting.MemberWhoZonedInAfterTheGroupUpdateIsStillTargeted` asserts the
precondition (`spawnId == 0`) before it asserts the fix.

**Ungrouped caster:** `inGroup() == false` returns the caster alone. A group spell
cast solo is a self spell, not a spell on the nearest five strangers.

### Does the canonical header disagree with the server anywhere?

**No.** Every constant in `include/client/eq_body_types.h` was compared value by value
against `/mnt/projects/akk-stack/code/common/bodytypes.h` (the server this project
runs). All 39 agree, including the sparse tail (60, 63, 64, 66, 67). The header was
not changed except to update its own provenance comment. T308 transcribed it
correctly.

### What the two dead copies were actually doing

Worth recording, because the board's description of them was itself slightly wrong:

| copy | board said | actually |
|---|---|---|
| `SpellManager` Animal set | "`BT_ANIMAL = 20`, includes insects" | the set was `{Luggald(20), Animal(21)}`. It accepted **Luggald**, got Animal right by accident under the name "insect", and **Insect(22) was never in it at all** |
| `SpellManager` Giant set | "`BT_GIANT = 3`" | `{Undead(3), SummonedUndead(8)}` — an undead was a legal target for a giant-slaying spell, a giant was not |
| `SpellTypeProcessor` Plant/Giant | "`21` for Plant (Insect), `22` for Giant (Monster)" | 21 is **Animal** and 22 is **Insect** (the board's numbers were themselves off by one) |
| `SpellTypeProcessor` Undead / Summoned / Animal | "not checked" | `// Would need body type check - placeholder` → `return true`: these three restrictions **accepted every creature in the game** |

### Accept sets now, and two deliberate narrowings

Transcribed from `Mob::DetermineSpellTargets()` (`zone/spells.cpp` ~1936-1995):

| restriction | accepts |
|---|---|
| Undead, UndeadAE | Undead(3), SummonedUndead(8), Vampire(12) |
| Summoned, SummonedAE | Summoned(24), Summoned2(27), Summoned3(28) |
| Animal | Animal(21) — **only** |
| Plant | Plant(25) |
| Giant | Giant(4), RaidGiant(9) folded in |
| Dragon | Dragon(26), VeliousDragon(30) folded in |
| Corpse | `is_corpse` (not a body type) |

Two sets are **narrower** than the old client, deliberately, because the server is the
oracle: **Insect is no longer an animal target**, and **Dragon2(29) is no longer a
dragon target** (the server folds only RaidGiant and VeliousDragon; `Dragon2` and
`Dragon3` are not folded). Both are called out in
`src/client/spell/spell_body_type_target.cpp`'s comments.

### Changes

| file | what |
|---|---|
| `include/client/spell/spell_body_type_target.h` (new, 47 lines) | the one predicate: `BodyTypeSatisfiesTargetType()` + `TargetTypeRestrictsCreature()` |
| `src/client/spell/spell_body_type_target.cpp` (new, 89 lines) | its implementation, constants from `client/eq_body_types.h` |
| `src/client/spell/spell_manager.cpp` | `isValidCreatureTarget()`: 47 lines of `BT_*` and a switch → one call |
| `src/client/spell/spell_type_processor.cpp` | `isValidTarget()` → one call; `findGroupTargets()` gains the membership gate + `isGroupMember()`; `processGroupSpell()` uses `groupSpellRadius()` |
| `include/client/spell/spell_type_processor.h` | `isGroupMember()`, `groupSpellRadius()`, `DEFAULT_GROUP_RADIUS` |
| `tests/test_spell_group_targeting.cpp` (new, 277 lines) | 10 tests |
| `tests/test_spell_body_type_targeting.cpp` (new, 358 lines) | 11 tests incl. the 37-row table and the source guard |
| `tests/test_spell_manager.cpp` | four existing cases carried the OLD numbering as their fixture data (bodytype 0 for "Humanoid", 20 for "Animal", 11 for "Vampire") — corrected to 1 / 21 / 12 with the reason inline |
| `refactor/deferred.md`, `refactor/T308_*.md`, `include/client/eq_body_types.h` | board + provenance struck |

### Test evidence — including the proof the negatives are load-bearing

A negative assertion is only worth anything if it fails on the broken code. So the
old behaviour was re-injected into the new function bodies (patch exported to
scratch, per protocol — **no `git stash`**), the two binaries rebuilt, and the suites
re-run. Results:

| suite | on the fix | on the old behaviour re-injected |
|---|---|---|
| `test_spell_group_targeting` | 10/10 pass | **4 FAIL** |
| `test_spell_body_type_targeting` | 11/11 pass | **7 FAIL** |

Which ones caught it:

- `SpellGroupTargeting.AdjacentNonMembersAreNotTargeted` — **FAILED on old code.**
  The two strangers standing 3 and 4 units from the caster were in the target set.
  This is the defect, stated as an assertion.
- `SpellGroupTargeting.WideRadiusReachesEveryMemberAndStillNoStrangers` — **FAILED on
  old code.** This is the one that separates "fixed membership" from "dropped range":
  widening the radius must pull in the distant *member* and still leave the adjacent
  strangers out.
- `SpellGroupTargeting.UngroupedCasterAffectsOnlySelfEvenInACrowd` — **FAILED on old
  code** (returned 5 targets for a caster with no group).
- `SpellGroupTargeting.MemberWhoZonedInAfterTheGroupUpdateIsStillTargeted` — **FAILED
  on old code** (the stranger came along for the ride).
- `SpellGroupTargeting.MemberOutOfRadiusIsSkippedButMemberInRadiusIsNot` — passes both
  ways; the old code did honour the radius. Kept as the explicit pin that the fix did
  not drop it.
- `SpellManagerBodyType.{GiantSpellAcceptsGiantAndRejectsMonster,
  PlantSpellAcceptsPlantAndRejectsAnimal,
  ValidateCastRefusesUndeadSpellOnALivingHumanoid}` — **all FAILED on old code.**
- `SpellTypeProcessorBodyType.{UndeadRestrictionNoLongerAcceptsEverything,
  AnimalRestrictionNoLongerAcceptsEverything,
  PlantAndGiantUseTheServerNumbersNotTheOldOnes}` — **all FAILED on old code.**
- `SpellBodyTypeGuard.NoBTConstantOrBareBodyTypeIntegerSurvivesInSpellCode` —
  **FAILED on old code**, naming the exact `BT_*` and bare-integer lines. It scans
  `src/client/spell/` and `include/client/spell/` on every ctest, strips `//`
  comments (so the defect stays documented in prose without tripping the guard), and
  fires on any `BT_*` identifier or any `body_type`/`bodytype` compared to an integer
  literal in either operand order.

One test-writing trap worth recording: the first draft placed the out-of-range group
mate at `server_y = 400` and the assertion failed. Spawn_Struct positions are
**eighths of a world unit** (`eq_entities.cpp:275-283` divides the 19-bit field by 8),
so "400" landed 50 units away — comfortably inside a 100-unit radius, and the
out-of-range case would have proved nothing while looking green. `PlayerSpawnAt()`
now takes world units and multiplies, and `SpawnScene()` asserts the **decoded**
positions rather than trusting the encoding.

The 37-row accept/reject table pairs every accepted value with a neighbour that must
be refused, including the two pairs the old code actually confused —
Plant(25)/Animal(21) and Giant(4)/Insect(22) — plus Plant(25)/Insect(22) and
Giant(4)/Monster(23) as named in the plan.
`SpellBodyTypeTarget.RestrictedTargetTypesAreExactlyTheOnesThatCanRefuse` brute-forces
target types 0-50 against body types 0-70 and asserts
`TargetTypeRestrictsCreature()` agrees with the predicate's own behaviour, so the two
functions cannot drift.

### Verification bar

| step | result |
|---|---|
| 1. `cmake --build build -j24` (GLES2 tree) | **PASS**, EXIT=0 (reconfigure needed for the new TUs) |
| 2. `ctest --test-dir build -j6 --output-on-failure` | **PASS** — 3347/3348, the only failure is the documented ALSA host quirk `AudioManagerTest.PreloadCommonSounds`. Zero new failures vs `refactor/test_baseline.txt`. Suite grew by 21 (10 + 11), 3327 → 3348 (`ctest -N` confirms the total). |
| 3. `bash scripts/build-gles2-x86.sh` | **PASS**, EXIT=0 |
| 4. Visual gate | **PASS** (inside the ctest run), goldens untouched |
| 5. `bash scripts/build-arm-noble.sh` | **PASS**, EXIT=0 |
| 6. `check_file_sizes.sh --files` (the 8 files this unit touches) | **8 ok, 0 warn, 0 fail** |

Note on step 6: a bare `check_file_sizes.sh fable` from this slot also reports
`FAIL src/client/graphics/irrlicht_renderer.cpp` and two WARNs — those files belong to
work merged onto `fable` after this branch was cut, not to this unit. The `--files`
run above is the one that scopes to T314.

Territory respected: `src/client/spell/spell_cast_checks.cpp` (slot1 T311 Leg B) and
`src/client/graphics/ui/` (slot3 T313) untouched.

### Found and deliberately NOT fixed

1. **`min_range` is not honoured for group spells.** eqemu range-checks
   `distance >= min_range2` as well as `<= range2`. `SpellData` has no `min_range`
   field and `spell_database.cpp` does not read one, so implementing it means a
   schema/loader change — out of this unit's territory. The practical exposure is
   small (group buffs with a minimum range are rare), but it is a real divergence.
2. **Pets are never group-spell targets.** eqemu's `GROUP_BUFF_PETS` arm buffs each
   member's pet for target types other than `GroupNoPets`. fable's
   `findGroupTargets()` returns players only. `SpellTargetType::GroupClientsPets` and
   `GroupNoPets` are therefore treated identically — the enum distinction exists and
   means nothing.
3. **`findGroupTargets()` cannot see a group mate who is out of the entity list.**
   That is correct (you cannot buff someone the zone has not sent you), but it is
   worth noting that `GroupState` knows about out-of-zone members and this path
   silently ignores them rather than reporting "out of range".
4. **`processGroupSpell()` still passes `from_server = false`** to
   `SpellEffects::processSpell()` for every member, i.e. the client predicts the
   whole group's buff locally. Untouched — pre-existing, orthogonal, and changing it
   is a prediction-policy decision, not a targeting fix.
5. **⚠ THE WHOLE OF `SpellTypeProcessor` IS UNREACHED FROM PRODUCTION — the T264
   reachability pattern again, and it applies to HALF THIS UNIT.** Verified by grep
   over `src/` and `include/`: `m_spell_type_processor` appears at exactly three
   sites — the `make_unique` in `eq.cpp:264`, the `GetSpellTypeProcessor()` accessor
   in `eq.h:1023`, and the member declaration. **`GetSpellTypeProcessor()` has zero
   callers**, `update()` is never ticked, and `processSpell()` is never invoked. So
   `processGroupSpell()`, `findGroupTargets()`, `findPBAETargets()`,
   `findTargetsInRadius()`, `isValidTarget()` and the whole rain-spell machine are
   constructed at startup and then never touched. `isValidTarget()` additionally has
   no callers even *inside* its own TU.

   This does not make the fixes wrong — the code is real, its contract is real, and
   these tests are the first thing that has ever reached it — but it **changes the
   impact claim on the board.** The plan (and tier 2 item 5) said "a group heal lands
   on strangers standing nearby". As the tree stands, no group heal reaches this code
   at all: the user-facing cast path is `EqActionHandler`/`eq_bridge` →
   `SpellManager::beginCastFromGem()` → `validateCast()` →
   `isValidCreatureTarget()`, which is the copy that *is* live. **So of the two
   defects this unit closed, only the `SpellManager` body-type table was reachable by
   a player; the group-targeting defect and the `SpellTypeProcessor` body-type table
   were latent.** Stated plainly rather than left for someone to discover after
   quoting the impact line. Wiring `SpellTypeProcessor` in (or deleting it) is a
   separate decision and is out of this unit's territory — recommend it be boarded as
   its own class-A reachability item alongside T264's family.
