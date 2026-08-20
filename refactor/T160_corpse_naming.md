# T160 — Player corpses are not recognised as corpses

**Slot: slot2. x86. NO eqemu, NO lock, NO Pi.** T159 (skill gate) runs in
parallel in slot1 and owns `src/client/skill/` — **you touch neither it nor
`config/`.**

**Territory: `src/client/combat.cpp` (`IsCorpse` and its callers),
`src/client/eq.cpp` (the spawn-name corpse detection at `:1408`), `tests/`,
`refactor/`.**

T155 probed T153's corpse-consider route on a live server and it **FAILED** — but
the defect is not in the sender T153 fixed. Read T155's Review first.

## The defect

eqemu names corpses two different ways:

| corpse of | name form |
|---|---|
| an NPC | `Probemob's_corpse123` — **underscore** |
| a player | `Testmelee's corpse456` — **space** |

`CombatManager::IsCorpse` (`combat.cpp:1681`) is
`entity.is_corpse || entity.name.find("'s_corpse") != std::string::npos` — the
underscore form only. `eq.cpp:1408` does the same test when a spawn arrives.
And `is_corpse` is otherwise set **only by the death path** (`eq.cpp:3311`,
when we watch something die).

**Consequence: any player corpse, and any corpse that already existed when we
zoned in, is treated as a live entity.** `/consider` cons it as if alive;
T153's corpse route never fires. Confirm the two name forms against eqemu source
before you fix anything — T155 read them from live captures, and this unit
should be able to name the server-side site that builds each.

## The work

Fix the recognition, in **both** places, so it covers the space form as well as
the underscore form. Prefer one shared helper over two literals that can drift —
this is the board's duplicated-then-drifted class and there are already two
copies of the same test.

**Check every consumer of `is_corpse` before you widen the predicate.** There
are around a dozen (`combat.cpp:432`, `:2058`, `eq_bridge.cpp:650`,
`spell_type_processor.cpp` ×4, `spell_manager.cpp:483`, `eq_entities.cpp:1638`,
`eq_player_info.cpp:281`). Widening what counts as a corpse changes targeting,
spell validity and auto-attack. **Say in the Review which consumers you checked
and what each does differently now** — a silent behaviour change in combat
targeting is a worse defect than the one being fixed.

Watch for the false positive: a living NPC legitimately named something like
`a_corpse_beetle` must not become a corpse. State how your predicate avoids it.

## Acceptance

- A test with a **player-corpse name** (space form) recognised as a corpse.
- A test with the **NPC form** still recognised (regression).
- A test that a living entity with "corpse" in its name is **not** matched.
- A test covering the zone-in path, not just the death path — the death path
  already works and is not the defect.

## Verification

Full protocol bar. Baseline **2570/2571** at tip `4f69fb76`, sole expected
failure the documented ALSA `AudioManagerTest.PreloadCommonSounds`.

**Not claimed: any wire behaviour.** The live re-probe of `/consider` on a
corpse is owed and is what closes T153. T155 also recorded a second obstacle for
that probe — `NPC:EmptyNPCCorpseDecayTime = 0` makes a fresh NPC corpse
unresolvable server-side almost immediately — so name a **player** corpse as the
better subject for the follow-up.

## STOP conditions

- A consumer of `is_corpse` would change behaviour in a way you cannot justify
  from the plan (report it, do not guess).

## Review

Executed in `/mnt/projects/fable/wt/slot2`, branch `task/T160-corpse-naming`,
off `fable` tip **`606adfdc`** (the tip at dispatch; `fable` advanced to
`3c3e6228` with T159's merge while this unit was running — this branch was NOT
rebased onto it). No eqemu, no lock, no Pi. `src/client/skill/` and `config/`
were never touched.

### The two eqemu name-building sites (read, not taken from the plan)

1. **`Corpse::CalcCorpseName`**, `zone/corpse.cpp:612-624` — the *only* place a
   corpse name is composed. It calls `EntityList::RemoveNumbers(name)` on the
   base name, then:

   * player corpse (`m_is_player_corpse`): `snprintf(tmp, ..., "'s corpse%d", GetID())`
     — apostrophe + **SPACE** (`:617`);
   * NPC corpse: ``snprintf(tmp, ..., "`s_corpse%d", GetID())`` —
     **BACKTICK** + underscore (`:620`).

   Both end in the corpse's own entity id, and the base is truncated to fit
   (`name[(sizeof(name)-1) - strlen(tmp)] = 0; strcat(name, tmp);`).

2. **`NPC::Death`**, `zone/attack.cpp:2850-2854` — the apostrophe+underscore
   form the client used to be the only one matching. eqemu's own comment says
   it plainly: *"The client sees NPC corpses as name's_corpse. The server uses
   name\`s_corpse so that %T works on corpses (client workaround)"*. It builds
   `Strings::Replace(corpse->GetName(), "\`s_corpse", "'s_corpse")` and ships it
   via `Mob::SendRename` → `OP_MobRename` (`zone/mob.cpp:1678`).

   **This is a rename broadcast at the instant of death only.** A corpse that
   was already on the ground when we zoned in is never renamed for us, so the
   name we receive for it is the raw **backtick** form. That is a third name
   form, and neither the plan nor T155 named it. All three are handled.

**A third, better-than-a-name signal was found and is now the primary test:**
`Corpse::FillSpawnStruct` (`zone/corpse.cpp:1787-1792`) sets
`ns->spawn.NPC = 2` for **every** corpse it spawns, player or NPC — the
`Spawn_Struct` field at offset 83 that `titanium_structs.h:269` documents as
`0=player,1=npc,2=pc corpse,3=npc corpse`. **This client already parses that
field** into `Entity::npc_type` (`eq_entities.cpp:289` and `:1101`, used for the
heading transform) and has been throwing away the corpse meaning ever since.
That is the reachability/parsed-but-unread pattern again, in a receive path.

### The predicate shipped

Two free functions declared in `include/client/combat.h`, defined in
`src/client/combat.cpp`, replacing **both** copies of the old
`name.find("'s_corpse")` literal:

```cpp
bool ParseCorpseName(const std::string& name,
                     std::string* base_name = nullptr,
                     uint32_t* corpse_id = nullptr);
bool IsCorpseEntity(const Entity& entity);
```

`ParseCorpseName` walks three separators — `"'s corpse"`, `"'s_corpse"`,
`` "`s_corpse" `` — with `rfind`, and **requires a non-empty run of 1–10 decimal
digits after the separator and nothing else**. It hands back the base name and
the id so the rename handler needs no second copy of the separator.

`IsCorpseEntity` is, in order: the `is_corpse` flag → `npc_type == 2 || == 3` →
`ParseCorpseName(name)`.

**The false positive is guarded by the mandatory trailing id, not by the
separator.** `a_corpse_beetle`, `a_corpse_beetle001` (no separator at all),
`Corpsemaker`, and even `Bob's corpse` and `Bob's corpse12x` (separator, no
clean id) are all rejected. eqemu can never emit a corpse name without the id,
because `CalcCorpseName` always `snprintf`s `GetID()`. A ten-digit cap keeps
`std::stoul` from throwing on a hostile name rather than catching after the
fact.

`CombatManager::IsCorpse(const Entity&)` is now a one-line delegate to
`IsCorpseEntity`, so every existing caller (`FindNearbyCorpse`,
`CorpseStillPresent`, `GetConsiderOpcodeForTarget`, the loot machine) is widened
in one place.

### Consumer audit — all eleven sites, and what each does differently now

The decisive scoping fact: **`Entity::is_corpse` (the raw field) is still
written only by the death handler (`eq.cpp:3311`) and the rename handler
(`eq.cpp:~1420`). This unit widened the *predicate*, not the *field*.** Spawn
parsing lives in `src/client/eq_entities.cpp`, which is outside this unit's
territory, so nothing was set there. That is why eight of the eleven sites are
provably unchanged — the field they read never changed value.

| # | site | reads | changed? | what it does now |
|---|---|---|---|---|
| 1 | `combat.cpp:432` `CycleTargets` "skip corpses" | was flag → **now `IsCorpse()`** | **YES — deliberate** | A player corpse, or any corpse that predates our zone-in, is no longer tab-targetable. Before, it sat in the cycle list as a live mob. This is the one targeting change, it is in the direction the plan asks for, and it is pinned by `PlayerCorpseIsExcludedFromTargetCycling` |
| 2 | `combat.cpp:2058` `TickAwaitingCorpse` | was flag → **now `IsCorpse()`** | **YES — deliberate, no widening of the match** | Still an O(1) `find(m_pending_corpse_id)` — T33's "the specific victim, not any corpse" rule is untouched. The only difference is that if the corpse arrives as a fresh spawn instead of via the death we watched, we now loot it instead of timing out at the deadline. Cannot mis-fire on a living entity: the id must already be our pending victim |
| 3 | `combat.cpp:1681` `IsCorpse` | the predicate itself | **YES — the fix** | Delegates to `IsCorpseEntity` |
| 4 | `eq_bridge.cpp:650` corpse-decay fade on despawn | raw flag | no | Unchanged. A visual nicety (fade vs vanish); if it were widened, corpses we did not watch die would also fade. Purely cosmetic either way — deliberately left alone rather than reached outside territory |
| 5 | `spell_type_processor.cpp:164` AE-radius target collection | raw flag | no | Unchanged. Would only ever *remove* targets from an AE, never add |
| 6 | `spell_type_processor.cpp:205` group/PB target collection | raw flag | no | Unchanged — **and note it is already partly protected**: the very next lines gate on `entity.npc_type != 0` for beneficial and `== 0` for detrimental, so a player corpse (`npc_type == 2`) is already excluded from beneficial group targeting by the npc_type test, and an NPC corpse from the detrimental one |
| 7 | `spell_type_processor.cpp:267` group-member scan | raw flag | no | Unchanged; also already gated on `npc_type != 0` immediately above |
| 8 | `spell_type_processor.cpp:566` `IsValidTarget` | raw flag | no | **Unchanged, and this is the one that most matters.** `entity.is_corpse && target_type != Corpse → false`. Had the field been widened here, every corpse would have become an invalid target for every non-corpse spell — correct in principle, but a real targeting/validity change on a path with no live coverage. Left for the follow-up, stated rather than smuggled |
| 9 | `spell_manager.cpp:483-484` `SpellTargetType::Corpse` validity | raw flag | no | Unchanged. This is the *false-negative* side of the same coin: a rez cast at a player corpse we did not watch die is still rejected client-side. Owed, out of territory (`src/client/spell/`, and T159 was live in that tree) |
| 10 | `eq_entities.cpp:1638` `DeleteSpawn` corpse-deletion path | raw flag | no | Unchanged. Closes the loot window if the corpse we are looting is deleted; we only ever loot a corpse we reached through the loot machine |
| 11 | `eq_player_info.cpp:281` `stateEntity.isCorpse` (bridge snapshot) | raw flag | no | Unchanged. Renderer-facing snapshot field |

(The plan said "around a dozen"; the exact count is eleven sites including
`IsCorpse` itself. Three changed, all three inside `src/client/combat.cpp`.)

**No STOP condition was hit.** No consumer changed in a way that could not be
justified: the two behaviour changes are both in `combat.cpp`, both express the
same intent the old code already had ("skip corpses" / "is my victim a corpse
yet"), and both are covered by tests.

### `eq.cpp` — the plan's `:1408` is the rename handler, not the spawn parser

Worth recording because it changes what the fix could reach. `eq.cpp:1408` is
inside `case HC_OP_MobRename:`, not a spawn path. There is **no** corpse test in
spawn parsing anywhere — `ZoneProcessZoneSpawns` / `ZoneProcessNewSpawn` live in
`eq_entities.cpp` and never look at the name or at `npc_type == 2`. So the
"zone-in" leg is fixed at the predicate, not by setting the flag.

The rename handler was also rewritten to use `ParseCorpseName`'s returned
`base_name`/`corpse_id` instead of its own `find` + `substr(pos + 9)` +
`stoi`-in-a-try. Behaviour preserved: id-from-name first, base-name fallback if
that id is not in `m_entities`, and the pre-existing `entity_id > 0` guard is
kept explicitly.

### The four required tests (plus four more), `tests/test_combat_manager.cpp`

| required by plan | test |
|---|---|
| player-corpse name (space form) recognised | `CorpseNaming.PlayerCorpseSpaceFormIsParsed`, and end-to-end in `CombatManagerCorpseRecognition.PlayerCorpseSpawnedAtZoneInIsACorpse` |
| NPC form still recognised (regression) | `CorpseNaming.NpcCorpseBothFormsAreParsed` (both the post-rename apostrophe form and the raw backtick form), `CombatManagerCorpseRecognition.NpcCorpseNameAloneIsACorpse` (with `npc_type=1`, so the **name alone** carries the verdict) |
| living entity with "corpse" in its name not matched | `CorpseNaming.LivingNamesWithCorpseInThemAreNotCorpses` (9 negatives), `CombatManagerCorpseRecognition.LivingCorpseBeetleIsNotACorpse` (`a_corpse_beetle001` still cons as `HC_OP_Consider`) |
| the zone-in path, not the death path | `CombatManagerCorpseRecognition.PlayerCorpseSpawnedAtZoneInIsACorpse` — a bulk **`HC_OP_ZoneSpawns`** batch with `npc_type=2` and the space-form name, **no `HC_OP_Death`, no `HC_OP_MobRename`**. It asserts `is_corpse == false` (the field really is untouched) *and* `GetConsiderOpcodeForTarget() == HC_OP_ConsiderCorpse`, i.e. T153's route now fires for exactly the entity T155's probe 2 failed on |

Plus `PlayerCorpseIsExcludedFromTargetCycling` (pins consumer 1's change) and
`MobRenameToPlayerCorpseFormSetsTheFlag` (the second literal's site, via a real
`HC_OP_MobRename` replay). Eight new tests total.

### Verification

| step | result |
|---|---|
| 1. `cmake --build build -j24` | **PASS** — clean, unpiped, no new warnings |
| 2. `ctest --output-on-failure` | **2578/2579 PASS**; sole failure the documented ALSA `AudioManagerTest.PreloadCommonSounds`. Baseline 2570/2571; +8 = the eight tests above |
| 3. `check_file_sizes.sh` | **1 FAIL, pre-existing and net-reduced by this unit** — see below |

**The size FAIL is `src/client/eq.cpp`, and it was already failing.** At `fable`
tip `606adfdc` the file is **3908** lines; after this unit it is **3904** (the
rename-handler rewrite is four lines shorter). The ceiling is 3000. This unit
did not create the failure and could not avoid surfacing it, because the plan's
territory requires editing `eq.cpp`. `src/client/combat.cpp` WARNs at 2213
(2134 before, also already over the 1500 warn line). `include/client/combat.h`
(428) and `tests/test_combat_manager.cpp` (1167) are ok.

Aside: `CLAUDE.md` describes `eq.cpp` as "~3,600 lines". It is 3908. Stale, not
touched here.

### Deviations

1. **`include/client/combat.h` was edited**, which is not literally in the
   stated territory (`combat.cpp`, `eq.cpp`, `tests/`, `refactor/`). It is
   unavoidable: the plan asks for **one shared helper** used from two TUs, so a
   declaration has to live in a header. `combat.h` was chosen over `eq.h`
   because `eq.cpp` already includes it, it forward-declares `Entity` already,
   and it keeps the corpse logic next to `CombatManager::IsCorpse`. 29 lines
   added, all declaration + comment, no class-body change, no ABI-relevant edit
   to `CombatManager`.
2. **Tests went into the existing `tests/test_combat_manager.cpp`** (1025 →
   1167 lines) rather than a new file. The 3000-line prohibition named
   `test_command_processor.cpp`; this file is well under both ceilings and is
   where the T153 corpse-consider tests already live, so splitting would have
   scattered the same subject across two files.
3. **A third name form was added beyond the plan's two.** The plan's table lists
   the apostrophe forms only; eqemu's *server-side* NPC corpse name uses a
   backtick, and that is what a pre-existing corpse actually arrives as. Adding
   it is strictly more coverage, and it is tested.
4. **`npc_type == 2 || == 3` was made the primary test, ahead of the name.**
   The plan only asked for a name fix. The spawn field is authoritative,
   immune to name games, and already parsed — using it is what makes the
   zone-in case robust rather than string-dependent. `3` is included because
   `titanium_structs.h` documents it even though eqemu only ever sends `2`.
5. **`fable` advanced to `3c3e6228` (T159's merge) mid-unit.** This branch was
   left on `606adfdc` and not rebased. `check_file_sizes.sh fable` therefore
   reports seven files (three of them T159's, in reverse); the table above is
   from a `--files` run scoped to this unit's four.

### What is owed

1. **The live re-probe that closes T153.** Nothing here has been seen on the
   wire. One session: find or make a corpse, `/target` it, `/consider`, and
   confirm a `CORPSE_DECAY_TIME_*` / `CORPSE_REZ_TIME_*` chat line instead of a
   normal con — and confirm `/consider` on a *living* target still cons
   normally (the regression risk of consumer 1 and 2's change).
   **Use a PLAYER corpse, not an NPC corpse.** T155 measured
   `NPC:EmptyNPCCorpseDecayTime = 0` on this server, which makes a fresh NPC
   corpse unresolvable within ~0.4 s of death (probe 3, BLOCKED); a player
   corpse persists for days and is also the exact case that FAILED. Note T155
   deliberately did not kill a player character to make one — that is a
   character change and needs sanctioning before the probe is dispatched.
   Best evidence would be a **zone-out/zone-in with the corpse already on the
   ground**, which exercises the `npc_type == 2` route with no rename in play.
2. **Widen the field, not just the predicate — a follow-up unit in
   `src/client/eq_entities.cpp`.** Setting `entity.is_corpse` from
   `npc_type == 2 || == 3` (and from `ParseCorpseName`) at spawn-parse time
   would move consumers 4–11 in one stroke. The two that matter are
   `spell_type_processor.cpp:566` (a corpse would stop being a valid target for
   ordinary spells) and `spell_manager.cpp:483` (a rez would start being valid
   on a corpse we did not watch die). Both are genuine behaviour changes on
   paths with no live coverage, which is exactly why they were not made from
   inside this unit's territory.
3. **`Entity::npc_type`'s corpse meaning is still unused elsewhere.** It has
   been parsed since the spawn parser was written and read only for the heading
   transform. Worth a sweep for other places that ask "is this a corpse" the
   long way.
