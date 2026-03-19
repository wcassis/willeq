# D07: Publish Door, Group, Pet, Spell, Skill Events

## Plan

### Overview

Add event publishing alongside existing direct renderer calls for door spawns/state,
group updates, pet window, spell casting/buffs, vision changes, and skill updates.

### Steps — Doors

1. Publish DoorSpawned in `ZoneProcessSpawnDoor()` (line ~5879):
   - After `m_renderer->createDoor(...)`, push DoorSpawned for each door
   - Include: doorId, name, x/y/z, heading, state (isOpen)

2. Publish DoorStateChanged in `ZoneProcessMoveDoor()` (line ~12819):
   - After `m_renderer->setDoorState(...)`, push DoorStateChanged
   - Include: doorId, isOpen

### Steps — Groups

3. Publish GroupInviteReceived in `ZoneProcessGroupInvite()` (line ~15313):
   - After group window shows pending invite, push GroupInviteReceived
   - Use GroupChangedData with inviter info

4. Publish GroupChanged in `ZoneProcessGroupUpdate()` GROUP_ACT_UPDATE/JOIN (line ~15407):
   - After group state is fully updated, push GroupChanged
   - Include: inGroup, isLeader, leaderName, memberCount

5. Publish GroupMemberUpdated for each member in same handler:
   - After each member is added, push GroupMemberUpdated

6. Publish GroupChanged in `ZoneProcessGroupUpdate()` GROUP_ACT_LEAVE (line ~15435):
   - After member removed, push GroupChanged with updated count

7. Publish GroupChanged in `ZoneProcessGroupUpdate()` GROUP_ACT_DISBAND (line ~15441):
   - After ClearGroup(), push GroupChanged with inGroup=false

8. Publish GroupChanged in `ZoneProcessGroupUpdate()` GROUP_ACT_MAKE_LEADER (line ~15461):
   - After leader change, push GroupChanged with new leader

9. Publish GroupChanged in `ZoneProcessGroupDisband()` (line ~15473):
   - After ClearGroup(), push GroupChanged with inGroup=false

### Steps — Pets

10. Publish PetCreated in `OnPetCreated()` (line ~19921):
    - After `openPetWindow()`, push PetCreated with spawnId, name, level

11. Publish PetRemoved in `OnPetRemoved()` (line ~19934):
    - After `closePetWindow()`, push PetRemoved

12. Publish PetButtonStateChanged in `OnPetButtonStateChanged()` (line ~19938):
    - Push PetButtonStateChanged with button and state

### Steps — Spells/Buffs

13. Publish SpellCastStarted in `ZoneProcessBeginCast()` (line ~12900):
    - After `m_spell_manager->handleBeginCast(...)`, push SpellCastStarted
    - Include: casterId, spellId, castTimeMs, targetId (0 if unknown)

14. Publish BuffUpdated in `ZoneProcessBuff()` (line ~13030):
    - When buff is applied (is_self path), push BuffUpdated
    - Include: slot, spellId, ticksLeft

15. Publish BuffRemoved in `ZoneProcessBuff()` (line ~13023):
    - When buff fades (is_self path), push BuffRemoved with slot

16. Publish VisionChanged in buff fade callback (line ~18933/18936):
    - After `m_renderer->setVisionType(...)` or `resetVisionToBase()`, push VisionChanged

### Steps — Skills

17. Publish SkillValueChanged in HC_OP_SkillUpdate handler (line ~2304):
    - After `m_skill_manager->updateSkill(...)`, push SkillValueChanged
    - Include: skillId, value

18. Publish TrainerWindowOpened in `ZoneProcessGMTraining()` (line ~5404):
    - After `openSkillTrainerWindow(...)`, push TrainerWindowOpened
    - Include: npcId, npcName

19. Publish TrainerWindowClosed in `CloseSkillTrainer()`:
    - After `closeSkillTrainerWindow()`, push TrainerWindowClosed

20. Build and verify compilation

## Acceptance Criteria

- All existing direct renderer calls remain unchanged
- Bridge receives door, group, pet, spell, and skill events
- All push calls guarded by `if (m_bridge)`
- No new warnings or errors in build
- Zone-loading-time door registrations (registerDoor) do NOT publish events

## Review

All steps completed as planned. No deviations.

Call sites covered:
- Door: ZoneProcessSpawnDoor (DoorSpawned per door), ZoneProcessMoveDoor
  (DoorStateChanged)
- Group: ZoneProcessGroupInvite (GroupInviteReceived),
  ZoneProcessGroupUpdate JOIN/UPDATE (GroupChanged + GroupMemberUpdated × N),
  ZoneProcessGroupUpdate LEAVE (GroupChanged),
  ZoneProcessGroupUpdate DISBAND (GroupChanged inGroup=false),
  ZoneProcessGroupUpdate MAKE_LEADER (GroupChanged),
  ZoneProcessGroupDisband (GroupChanged inGroup=false)
- Pet: OnPetCreated (PetCreated), OnPetRemoved (PetRemoved),
  OnPetButtonStateChanged (PetButtonStateChanged)
- Spell: ZoneProcessBeginCast (SpellCastStarted),
  ZoneProcessBuff fade+is_self (BuffRemoved),
  ZoneProcessBuff apply+is_self (BuffUpdated),
  buff fade callback (VisionChanged with final computed vision type)
- Skill: HC_OP_SkillUpdate (SkillValueChanged),
  ZoneProcessGMTraining (TrainerWindowOpened),
  CloseTrainerWindow (TrainerWindowClosed)

GroupInviteReceived uses GroupChangedData as carrier (inviter name in leaderName).
PetRemoved spawnId is 0 because m_pet_spawn_id is cleared before OnPetRemoved().
TrainerWindowClosed published before clearing m_trainer_npc_id.
VisionChanged tracks final vision type after re-scanning remaining buffs.
All 19 push sites guarded by `if (m_bridge)`.

Build succeeds, 73 relevant tests pass. All acceptance criteria met.
