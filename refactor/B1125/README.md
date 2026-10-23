# B1125 (#445): OP_DeleteSpawn(0) is ignored

Defect B1118 M9. Evidence: `/mnt/projects/fable/review/B1125/` (index: its `README.md`).

## 0. Result

`EntitiesComponent::ZoneProcessDeleteSpawn` used to read spawn id 0 as "delete the current
target". With the player self-targeted, the DeleteSpawn(0) that follows an NPC death erased the
local player (B1118 run A1). The handler now ignores id 0 (decision (a)).

- **Test.** `tests/test_delete_spawn_zero.cpp` replays the A1 packet bytes. It was RED at
  4a5246c1 (3 of 6 cases) and is GREEN with the fix.
- **Negative cases.** A real-id DeleteSpawn still deletes the entity and clears that target.
  Deleting another entity leaves a self target alone.
- **Live leg.** The lane-5 A1 replay on the fixed build keeps the player: no `despawned` line,
  the camera stays on the player, and the next cast resolves both hand bones.

## 1. Why the server sends 0, and why (a)

- **eqemu source** (lane 5, `/opt/akk-stack/code`).
  - `NPC::Death` (`zone/attack.cpp`) runs `entity_list.AddCorpse(corpse, GetID())` and then
    `SetID(0)` on the NPC.
  - When the NPC object is destroyed, `Mob::~Mob` (`zone/mob.cpp`) sends
    `CreateDespawnPacket()` with `GetID()`, which is now 0.
  - So id 0 is the despawn of an object whose id now belongs to its corpse. It never names a live
    entity. A player death does the same `AddCorpse` + `SetID(0)` (`attack.cpp`).
- **Pcaps.** The census covers all 10 B1118 captures, oracle and fable:
  - 23 OP_Death and 18 DeleteSpawn(0).
  - Every DeleteSpawn(0) comes 32-61 ms after the death of a corpse-leaving victim the player
    killed. None is orphaned.
  - 17 of the 18 are followed by a real-id DeleteSpawn of the corpse about 1.03 s later, when the
    empty corpse decays. Run A1's rodent kept its corpse and got none.
  - Deaths caused by an NPC send only the real id.
- **Rejecting (b).** Keeping the target mapping but excluding the player would still erase any
  other live target. One example is an AE kill while targeting an NPC that survives (the test
  `LiveNpcTargeted_TargetSurvives` fails at the base). It would also act on a packet that refers
  to no entity. The real removal always comes under the real id.

## 2. Changes

- `src/client/entities/entities_component_appearance.cpp`:
  - id 0 returns at once and logs at TRACE.
  - The corpse guard's comment no longer gives DeleteSpawn(0) as its reason.
- `tests/test_delete_spawn_zero.cpp` and `tests/cmake/action_handlers.cmake`: a new target that
  links `willeq_core` only. It is registered in the headless (`EQT_GRAPHICS=OFF`) build too, and
  is green there.
- `refactor/T33_loot_death_event_wiring.md`: a note on the stale "interpreted as delete current
  target" sentence.

## 3. Verification

- Headless Release tree, built for named targets:
  - test_delete_spawn_zero: RED log first, then GREEN.
  - test_zone_entities, test_game_state_source_writes, test_zone_connection_flow: green.
- Whole-tree Release build with the CI flags, `-j 4 -- -k`: log
  `review/B1125/tests/ci_flags_build.log`.
- Lane 5 live leg, `review/B1125/live_A1/`. The x11grab capture runs at 30 fps.

## FOLLOW-UP

- **Empty corpses never leave.** The corpse guard in `ZoneProcessDeleteSpawn` deletes a corpse
  only after looting completes. It therefore refuses the server's real-id DeleteSpawn that
  removes an empty corpse about 1 s after death. The pcaps show that packet after 17 of 18 kills,
  so by the code fable keeps every such corpse (not measured on screen in this unit). The guard was written while id 0
  still mapped to the target; it is now worth re-checking against the oracle (does the official
  client remove the corpse on that packet?).
