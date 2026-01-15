# Pre-Luclin Opcodes Implementation Plan

This plan covers implementing all missing and partially implemented opcodes for Classic, Kunark, and Velious expansions in WillEQ. All opcodes are required for a complete implementation.

## Overview

| Category | Count | Status |
|----------|-------|--------|
| Partially Implemented | 12 | **COMPLETE** |
| High Priority | ~25 | **COMPLETE** |
| Medium Priority | ~30 | **COMPLETE** |
| Standard Priority | ~40 | **COMPLETE** |

---

## Phase 1: Complete Partially Implemented Opcodes ✓ COMPLETE

**Completed: 2026-01-13**

All 12 partially implemented opcodes have been completed:

| Opcode | Implementation |
|--------|---------------|
| OP_SpawnAppearance | Added handlers for AT_INVISIBLE, AT_SNEAK, AT_LINKDEAD, AT_FLYMODE, AT_AFK, AT_ANONYMOUS, AT_PET, AT_SIZE, AT_LIGHT. Added Entity fields for tracking state. |
| OP_Taunt | Implemented CombatManager::Taunt() with 4-byte Target_Struct. Added /taunt command. |
| OP_CombatAbility | Already complete (verified implementation in CombatManager::UseAbility) |
| OP_AutoFire | Implemented EnableAutoFire/DisableAutoFire/ToggleAutoFire. Added /autofire command. |
| OP_ApplyPoison | Added ApplyPoison_Struct, SendApplyPoison(), ZoneProcessApplyPoison() handler with success/failure messages. |
| OP_Track | Added Track_Struct, ZoneProcessTrack() parses entries and displays tracking results in chat. |
| OP_TradeSkillCombine | Clarified server sends empty ack, feedback via OP_FormattedMessage (already handled). |
| OP_CloseContainer | Already complete (verified uses OP_ClickObjectAction with open=0). |
| OP_PetCommands | Clarified UI button state updates via PetWindow::update() polling. |
| OP_RaidUpdate | Added OP_RaidInvite opcode, SendRaidInvite(), ZoneProcessRaidUpdate() parses actions and shows messages. |
| OP_Consider | Already complete (verified ConsiderTarget() and ProcessConsiderResponse()). |
| Follow Movement | Fixed /follow command to call Follow(target_name) for actual movement. |

---

### 1.1 Movement/Appearance (Reference)

**OP_SpawnAppearance (0x7c32)**
- Location: `src/client/eq.cpp:5226`
- Issue: Only handles sitting/standing; missing invisible, levitate, etc.
- Task: Add handlers for all appearance types (see eqemu `zone/client_packet.cpp`)
- Appearance types needed:
  - `AT_Invisibility` (3)
  - `AT_Levitate` (19)
  - `AT_InvisVsUndead` (4)
  - `AT_InvisVsAnimals` (5)
  - `AT_Sneaking` (15)
  - `AT_Size` (12)
  - `AT_Light` (23)

### 1.2 Combat

**OP_Taunt (0x5e48)**
- Status: Opcode defined, no handler
- Task: Implement send and receive handlers
- Reference: `zone/combat.cpp` (eqemu)
- Struct: `TauntStruct` with target_id and skill

**OP_CombatAbility (0x5ee8)**
- Status: Handler incomplete
- Task: Complete discipline activation logic
- Reference: `zone/melee.cpp` (eqemu)
- Struct: `CombatAbility_Struct`

**OP_AutoFire (0x6c53)**
- Status: Opcode defined, no handler
- Task: Implement ranged auto-attack toggle
- Simple toggle packet, similar to OP_AutoAttack

### 1.3 Skills

**OP_ApplyPoison (0x0c2c)**
- Status: Opcode defined, no handler
- Task: Implement poison application to weapons
- Reference: `zone/client_packet.cpp` (eqemu)
- Struct: `ApplyPoison_Struct` with inventorySlot and success

**OP_Track (0x5d11)**
- Status: Handler exists but result processing minimal
- Task: Parse tracking results and display in UI
- Reference: `zone/client_packet.cpp` (eqemu)
- Struct: `Tracking_Struct` with array of TrackingEntry

### 1.4 Tradeskills

**OP_TradeSkillCombine (0x0b40)**
- Location: `src/client/eq.cpp:11779`
- Issue: TODO for parsing combine result and showing success/failure
- Task: Parse `Combine_Struct` result and display message
- Add success/failure notification to chat

### 1.5 Objects

**OP_CloseContainer (N/A)**
- Status: Handler exists, may not send packet
- Task: Verify packet is sent when closing containers
- Test with tradeskill containers

### 1.6 Pets

**OP_PetCommands (0x10a1)**
- Location: `src/client/eq.cpp:14306`
- Issue: TODO for `m_renderer->getWindowManager()->updatePetButtonState`
- Task: Implement pet button state updates in UI

### 1.7 Raids

**OP_RaidUpdate (0x1f21)**
- Status: Receives only, no client-initiated actions
- Task: Add support for sending raid join/invite packets
- This ties into OP_RaidJoin and OP_RaidInvite implementation

### 1.8 Misc

**OP_Consider (0x65ca)**
- Status: Receives response, sending may be incomplete
- Task: Verify consider packet is sent correctly
- Test with various NPC types

**Follow Movement**
- Location: `src/client/eq.cpp:6707`
- Issue: TODO for actual follow movement
- Task: Implement follow pathfinding/movement logic
- Use existing pathfinder interface

---

## Phase 2: High Priority Opcodes ✓ COMPLETE

**Completed: 2026-01-13**

Core gameplay features that significantly impact the user experience.

| System | Opcodes | Implementation |
|--------|---------|---------------|
| Logout | OP_Camp, OP_Logout, OP_LogoutReply | SendCamp(), SendLogout(), ZoneProcessLogoutReply(), /camp command |
| Resurrection | OP_RezzRequest, OP_RezzAnswer, OP_RezzComplete | ZoneProcessRezzRequest(), SendRezzAnswer(), /rezaccept, /rezdecline |
| Who | OP_WhoAllRequest, OP_WhoAllResponse | SendWhoAllRequest(), ZoneProcessWhoAllResponse(), /who command |
| Inspect | OP_InspectRequest, OP_InspectAnswer | SendInspectRequest(), ZoneProcessInspectRequest(), ZoneProcessInspectAnswer(), /inspect command |
| Guild | OP_GuildInvite, OP_GuildInviteAccept, OP_GuildRemove, OP_GuildDemote, OP_GuildLeader, OP_GetGuildMOTD, OP_SetGuildMOTD, OP_GuildMemberUpdate, OP_GuildMemberAdd, OP_GetGuildMOTDReply | Full guild management with commands: /guildinvite, /guildaccept, /guilddecline, /guildremove, /guilddemote, /guildleader, /guildmotd |

### 2.1 Logout System (CRITICAL)

**OP_Camp (0x78c1) - C->S**
- Sends camp request to server
- Struct: `Camp_Struct` or empty packet
- Triggers 30-second camp timer

**OP_Logout (0x61ff) - C->S**
- Immediate logout request
- Sent when camp timer completes

**OP_LogoutReply (0x3cdc) - S->C**
- Server confirmation of logout
- Clean up connection and return to character select

**Implementation:**
```cpp
// Add to TitaniumZoneOpcodes enum
OP_Camp = 0x78c1,
OP_Logout = 0x61ff,
OP_LogoutReply = 0x3cdc,

// Add handler
void EverQuest::HandleLogoutReply(const Packet& packet);

// Add slash command /camp
void EverQuest::SendCamp();
```

### 2.2 Resurrection System (CRITICAL)

**OP_RezzRequest (0x1035) - S->C**
- Server sends rez offer popup
- Struct: `Resurrect_Struct` with zone, rez_zone_id, exp_percentage

**OP_RezzAnswer (0x6219) - C->S**
- Client accepts/declines rez
- Struct: `Resurrect_Struct` with action (0=decline, 1=accept)

**OP_RezzComplete (0x4b05) - S->C**
- Server confirms rez completion
- Triggers zone change if needed

**Implementation:**
- Add popup dialog for rez offers
- Track pending rez state
- Handle zone change after acceptance

### 2.3 Who System

**OP_WhoAllRequest (0x5cdd) - C->S**
- Send who request with optional filters
- Struct: `Who_All_Struct` with name, guild, level_min, level_max, class, race, zone

**OP_WhoAllResponse (0x757b) - S->C**
- Server returns player list
- Struct: `WhoAllReturnStruct` with array of players

**Implementation:**
- Add `/who` command handler
- Parse and display results in chat
- Add who window UI

### 2.4 Inspect System

**OP_InspectRequest (0x775d) - C->S**
- Request to inspect player
- Struct: `Inspect_Struct` with target_id

**OP_InspectRequest (0x775d) - S->C**
- Someone is inspecting you (popup)
- Contains inspector's name

**OP_InspectAnswer (0x2403) - C->S/S->C**
- Bidirectional: Send/receive inspect data
- Struct: `InspectResponse_Struct` with equipment slots

**OP_InspectBuffs (0x4FB6) - S->C**
- Target's buff info
- Struct: array of buff spell IDs

**Implementation:**
- Add `/inspect` command
- Create inspect window UI
- Handle both sending and receiving

### 2.5 Guild System (Core)

**C->S Packets:**

| Opcode | Wire Code | Description |
|--------|-----------|-------------|
| OP_GuildInvite | 0x18b7 | Invite player to guild |
| OP_GuildInviteAccept | 0x61d0 | Accept guild invite |
| OP_GuildRemove | 0x0179 | Remove member |
| OP_GuildDelete | 0x6cce | Delete guild |
| OP_GuildLeader | 0x12b1 | Transfer leadership |
| OP_GuildDemote | 0x4eb9 | Demote member |
| OP_GuildPublicNote | 0x17a2 | Set public note |
| OP_SetGuildMOTD | 0x591c | Set MOTD |
| OP_GetGuildMOTD | 0x7fec | Get MOTD |

**S->C Packets:**

| Opcode | Wire Code | Description |
|--------|-----------|-------------|
| OP_GuildMemberList | 0x147d | Full member list |
| OP_GuildMemberUpdate | 0x0f4d | Member status change |
| OP_GetGuildMOTDReply | 0x3246 | MOTD response |
| OP_SetGuildRank | 0x6966 | Rank change |
| OP_GuildMemberAdd | 0x754e | New member joined |

**Implementation Order:**
1. Define all opcodes in enum
2. Add packet structures to `packet_structs.h`
3. Implement MOTD get/set first (simplest)
4. Add invite/accept flow
5. Add remove/demote/leader transfer
6. Create guild window UI

---

## Phase 3: Medium Priority Opcodes ✓ COMPLETE

**Completed: 2026-01-13**

Enhanced gameplay features.

| System | Opcodes | Implementation |
|--------|---------|---------------|
| Corpse Management | OP_CorpseDrag, OP_CorpseDrop, OP_ConsiderCorpse, OP_ConfirmDelete | SendCorpseDrag(), SendCorpseDrop(), SendConsiderCorpse(), SendConfirmDelete(), /corpsedrag, /corpsedrop commands |
| Consent System | OP_Consent, OP_ConsentDeny, OP_ConsentResponse, OP_DenyResponse | SendConsent(), SendConsentDeny(), ZoneProcessConsentResponse(), ZoneProcessDenyResponse(), /consent, /deny commands |
| Combat Targeting | OP_Assist, OP_AssistGroup | SendAssist(), SendAssistGroup(), /assist command |
| Travel System | OP_BoardBoat, OP_LeaveBoat, OP_ControlBoat | SendBoardBoat(), SendLeaveBoat(), SendControlBoat() with boat tracking state |
| Group Split | OP_Split | SendSplit() with validation, /split command |
| LFG System | OP_LFGCommand, OP_LFGAppearance | SendLFGCommand(), ZoneProcessLFGAppearance(), /lfg command |
| Raid Additions | OP_RaidJoin, OP_MarkRaidNPC | Opcodes added (builds on existing raid system) |
| Combat Abilities | OP_Shielding, OP_EnvDamage | SendShielding(), ZoneProcessEnvDamage() with damage type messages, /shield command |
| Discipline System | OP_DisciplineUpdate, OP_DisciplineTimer | ZoneProcessDisciplineUpdate(), ZoneProcessDisciplineTimer() |
| Banking | OP_BankerChange | ZoneProcessBankerChange() updates bank currency in GameState |
| Misc | OP_Save, OP_SaveOnZoneReq, OP_PopupResponse, OP_ClearObject | SendSave(), SendSaveOnZoneReq(), SendPopupResponse(), ZoneProcessClearObject(), /save command |

### 3.1 Corpse Management

**OP_CorpseDrag (0x50c0) - C->S**
- Start dragging corpse
- Struct: `CorpseDrag_Struct` with corpse_name

**OP_CorpseDrop (0x7c7c) - C->S**
- Stop dragging corpse
- Empty packet or corpse_id

**OP_ConsiderCorpse (0x773f) - C->S**
- Get corpse info
- Similar to OP_Consider

**OP_ConfirmDelete (0x3838) - C->S**
- Confirm item deletion dialog
- Struct: with slot_id and confirmed flag

### 3.2 Consent System

**OP_Consent (0x1081) - C->S**
- Grant corpse loot permission
- Struct: `Consent_Struct` with name

**OP_ConsentDeny (0x4e8c) - C->S**
- Revoke consent
- Struct: `Consent_Struct` with name

**OP_ConsentResponse (0x6380) - S->C**
- Server confirms consent granted

**OP_DenyResponse (0x7c66) - S->C**
- Server confirms consent denied

### 3.3 Combat Targeting

**OP_Assist (0x7709) - C->S**
- Assist target's target
- Struct: `EntityId_Struct` with entity_id

**OP_AssistGroup (0x5104) - C->S**
- Assist group member
- Similar struct

### 3.4 Travel

**OP_BoardBoat (0x4298) - C->S**
- Board boat/transport
- Struct: with boat entity_id

**OP_LeaveBoat (0x67c9) - C->S**
- Leave boat
- Empty or boat_id

**OP_ControlBoat (0x2c81) - C->S**
- Control boat movement
- For boat pilots

### 3.5 Group Features

**OP_Split (0x4848) - C->S**
- Split money with group
- Struct: `Split_Struct` with copper, silver, gold, platinum

### 3.6 LFG System

**OP_LFGCommand (0x68ac) - C->S**
- Set LFG status
- Struct: `LFG_Struct` with message, level range

**OP_LFGResponse (N/A) - S->C**
- LFG list response

**OP_LFGAppearance (0x1a85) - S->C**
- Update LFG flag on player

### 3.7 Raid System

**OP_RaidJoin (0x1f21) - C->S**
- Request to join raid
- Struct: with raid_id

**OP_RaidInvite (0x5891) - C->S**
- Invite player to raid
- Struct: with player_name

**OP_MarkRaidNPC (N/A) - S->C**
- Mark NPC for raid

### 3.8 Combat Abilities

**OP_Shielding (0x3fe6) - C->S**
- Warrior shield ability
- Struct: with target_id

**OP_EnvDamage (0x31b3) - C->S**
- Environmental damage report (fall, lava)
- Struct: with damage_amount, damage_type

### 3.9 Discipline System

**OP_DisciplineUpdate (0x7180) - S->C**
- Available disciplines
- Struct: array of discipline IDs

**OP_DisciplineTimer (0x53df) - S->C**
- Discipline cooldown
- Struct: disc_id, remaining_time

### 3.10 Banking

**OP_BankerChange (0x6a5b) - S->C**
- Bank contents update
- After deposit/withdrawal

### 3.11 Misc Medium Priority

**OP_Save (0x736b) - C->S**
- Request character save

**OP_SaveOnZoneReq (0x1540) - C->S**
- Save before zoning

**OP_PopupResponse (0x3816) - C->S**
- Response to popup dialog
- Struct: popup_id, button_clicked

**OP_ClearObject (0x21ed) - S->C**
- Clear ground spawn object

---

## Phase 4: Standard Priority Opcodes ✓ COMPLETE

**Completed: 2026-01-13**

Required features implemented after core and enhanced gameplay.

| System | Opcodes | Implementation |
|--------|---------|---------------|
| Dueling | OP_RequestDuel, OP_DuelAccept, OP_DuelDecline | SendDuelRequest(), SendDuelAccept(), SendDuelDecline(), ZoneProcessDuelRequest(), /duel, /duelaccept, /dueldecline commands |
| Skills | OP_BindWound, OP_TrackTarget, OP_TrackUnknown | SendBindWound(), SendTrackTarget(), SendTrackUnknown(), /bandage command |
| Tradeskills | OP_RecipesFavorite, OP_RecipesSearch, OP_RecipeDetails, OP_RecipeAutoCombine, OP_RecipeReply | Send functions for all, ZoneProcessRecipeReply(), ZoneProcessRecipeAutoCombine() |
| Cosmetic | OP_Surname, OP_FaceChange, OP_Dye | SendSurname(), SendFaceChange(), SendDye(), ZoneProcessDye(), /surname command |
| Audio | OP_PlayMP3, OP_Sound | ZoneProcessPlayMP3(), ZoneProcessSound() with logging |
| Misc | OP_RandomReq, OP_RandomReply, OP_FindPersonRequest, OP_FindPersonReply, OP_CameraEffect, OP_Rewind, OP_YellForHelp, OP_Report, OP_FriendsWho | SendRandom(), SendRewind(), SendYellForHelp(), SendReport(), SendFriendsWho(), ZoneProcessRandomReply(), ZoneProcessFindPersonReply(), ZoneProcessCameraEffect(), /random, /rewind, /yell commands |
| GM Commands | OP_GMZoneRequest, OP_GMSummon, OP_GMGoto, OP_GMFind, OP_GMKick, OP_GMKill, OP_GMHideMe, OP_GMToggle, OP_GMEmoteZone, OP_GMBecomeNPC, OP_GMDelCorpse, OP_GMSearchCorpse, OP_GMLastName, OP_GMApproval, OP_GMServers, OP_GMNameChange | Send functions for all GM commands |
| Petitions | OP_Petition, OP_PetitionQue, OP_PetitionDelete, OP_PetitionBug, OP_PetitionCheckIn, OP_PetitionResolve, OP_PetitionUnCheckout, OP_PetitionRefresh, OP_PDeletePetition | SendPetition(), SendPetitionDelete(), ZoneProcessPetitionUpdate(), /bug, /petition commands |

### 4.1 Dueling

**OP_RequestDuel (0x28e1) - C->S/S->C**
- Duel challenge

**OP_DuelAccept (0x1b09) - C->S**
- Accept duel

**OP_DuelDecline (0x3bad) - C->S**
- Decline duel

### 4.2 Skills

**OP_Bind_Wound (0x601d) - C->S**
- Bind wound skill
- Struct: with target_id

**OP_TrackTarget (0x7085) - C->S**
- Select tracking target

**OP_TrackUnknown (0x6177) - C->S**
- Unknown tracking packet

### 4.3 Tradeskills

**OP_RecipesFavorite (0x23f0) - C->S**
- Favorite recipes list

**OP_RecipesSearch (0x164d) - C->S**
- Search recipes

**OP_RecipeDetails (0x4ea2) - C->S**
- Get recipe details

**OP_RecipeAutoCombine (0x0353) - C->S/S->C**
- Auto-combine recipe

**OP_RecipeReply (0x31f8) - S->C**
- Recipe search results

### 4.4 Cosmetic

**OP_Surname (0x4668) - C->S**
- Set surname

**OP_FaceChange (0x0f8e) - C->S**
- Change face

**OP_Dye (0x00dd) - C->S**
- Dye armor

### 4.5 Audio

**OP_PlayMP3 (0x26ab) - S->C**
- Play music

**OP_Sound (0x541e) - S->C**
- Play sound effect

### 4.6 Miscellaneous

**OP_RandomReq (0x5534) - C->S**
- Random roll (/random)

**OP_RandomReply (0x6cd5) - S->C**
- Random roll result

**OP_FindPersonRequest (0x3c41) - C->S**
- Find person path

**OP_FindPersonReply (0x5711) - S->C**
- Find person path result

**OP_CameraEffect (0x0937) - S->C**
- Camera shake, etc.

**OP_Rewind (0x4cfa) - C->S**
- Rewind command (stuck recovery)

**OP_YellForHelp (0x61ef) - C->S**
- Yell for help

**OP_Report (0x7f9d) - C->S**
- Report player

**OP_FriendsWho (0x48fe) - C->S**
- Friends list who

### 4.7 GM Commands

20 opcodes for GM/admin functionality.

| Opcode | Wire Code | Description |
|--------|-----------|-------------|
| OP_GMZoneRequest | 0x1306 | GM zone request |
| OP_GMZoneRequest2 | 0x244c | GM zone request 2 |
| OP_GMSummon | 0x1edc | GM summon player |
| OP_GMGoto | 0x1cee | GM goto player |
| OP_GMFind | 0x5930 | GM find player |
| OP_GMKick | 0x692c | GM kick player |
| OP_GMKill | 0x6980 | GM kill target |
| OP_GMHideMe | 0x15b2 | GM invisibility |
| OP_GMToggle | 0x7fea | GM toggle |
| OP_GMEmoteZone | 0x39f2 | GM zone emote |
| OP_GMBecomeNPC | 0x7864 | GM become NPC |
| OP_GMDelCorpse | 0x0b2f | GM delete corpse |
| OP_GMSearchCorpse | 0x3c32 | GM search corpse |
| OP_GMLastName | 0x23a1 | GM set last name |
| OP_GMApproval | 0x0c0f | GM approval |
| OP_GMServers | 0x3387 | GM servers |
| OP_GMNameChange | N/A | GM name change |

### 4.8 Petitions

9 opcodes for the petition/support system.

| Opcode | Wire Code | Description |
|--------|-----------|-------------|
| OP_Petition | 0x251f | Submit petition |
| OP_PetitionQue | 0x33c3 | Petition queue |
| OP_PetitionDelete | 0x5692 | Delete petition |
| OP_PetitionBug | N/A | Petition bug report |
| OP_PetitionCheckIn | N/A | Petition check in |
| OP_PetitionResolve | N/A | Petition resolve |
| OP_PetitionUnCheckout | N/A | Petition un-checkout |
| OP_PetitionRefresh | N/A | Petition refresh |
| OP_PDeletePetition | N/A | Delete petition |
| OP_PetitionUpdate | N/A | Petition update (S->C) |
| OP_PetitionCheckout | N/A | Petition checkout (S->C) |

---

## Implementation Strategy

### Per-Opcode Implementation Steps

For each opcode:

1. **Define opcode constant** in `include/client/eq.h`
   - Add to `TitaniumZoneOpcodes` enum

2. **Define packet structure** in `include/common/packet_structs.h`
   - Match eqemu struct layout exactly
   - Add static_assert for size verification

3. **Implement handler** in `src/client/eq.cpp`
   - C->S: Add `Send<Opcode>()` method
   - S->C: Add `Handle<Opcode>()` method
   - Register handler in `SetupZonePacketHandlers()`

4. **Add UI integration** if applicable
   - Slash command in `CommandRegistry`
   - Window/dialog if needed

5. **Test** with eqemu server
   - Verify packet structure matches
   - Test edge cases

### Reference Files

**EQEmu Server (for packet structure reference):**
- `/home/user/projects/claude/akk-stack/code/zone/client_packet.cpp`
- `/home/user/projects/claude/akk-stack/code/common/patches/titanium_structs.h`
- `/home/user/projects/claude/akk-stack/code/common/patches/titanium.cpp`

**WillEQ Client:**
- `include/client/eq.h` - Opcode enums
- `src/client/eq.cpp` - Handlers
- `include/common/packet_structs.h` - Structures

---

## Estimated Work by Phase

| Phase | Opcodes | Complexity |
|-------|---------|------------|
| Phase 1 | 12 | Low-Medium (partial code exists) |
| Phase 2 | ~25 | Medium-High (core features) |
| Phase 3 | ~30 | Medium (enhanced features) |
| Phase 4 | ~40 | Medium (additional required features) |

---

## Sub-Agent Usage Guidelines

Use parallel sub-agents for:

1. **Research tasks** - Looking up eqemu structs/handlers
2. **Multiple independent opcodes** - Different categories in parallel
3. **UI components** - Window implementation separate from packet handlers
4. **Testing** - Build and test while implementing next feature

Example parallel work:
- Agent 1: Implement logout system (Camp/Logout/LogoutReply)
- Agent 2: Implement resurrection system (RezzRequest/Answer/Complete)
- Agent 3: Research guild packet structures

---

## Success Criteria

Phase 1 complete when:
- All 12 partially implemented opcodes have TODOs resolved
- No remaining TODO comments for these opcodes

Phase 2 complete when:
- /camp and /logout work correctly
- Resurrection offers appear and can be accepted
- /who returns player list
- /inspect shows player equipment
- Basic guild operations work (invite, leave, MOTD)

Phase 3 complete when: ✓ COMPLETE
- Corpse dragging works ✓
- Consent system functional ✓
- /assist works ✓
- Boat travel works ✓
- LFG system functional ✓
- Raid invites work ✓

Phase 4 complete when: ✓ COMPLETE
- Dueling system fully functional ✓
- All skill opcodes implemented (Bind_Wound, TrackTarget, TrackUnknown) ✓
- All tradeskill recipe opcodes working ✓
- Cosmetic features work (Surname, FaceChange, Dye) ✓
- Audio opcodes implemented (PlayMP3, Sound) ✓
- All misc opcodes working (Random, FindPerson, Camera, Rewind, YellForHelp, Report, FriendsWho) ✓
- All GM commands implemented ✓
- All petition opcodes implemented ✓

---

## Notes

1. Focus on packet correctness first, UI polish second
2. Test each opcode with eqemu server before moving on
3. Some opcodes share structures - implement together
4. Guild system is largest single feature (~17 opcodes)
5. Many S->C handlers may already work via generic handlers

---

## Appendix: Quick Reference

### Wire Codes for Phase 1 (Partial)
```
OP_SpawnAppearance = 0x7c32
OP_Taunt = 0x5e48
OP_CombatAbility = 0x5ee8
OP_AutoFire = 0x6c53
OP_ApplyPoison = 0x0c2c
OP_Track = 0x5d11
OP_TradeSkillCombine = 0x0b40
OP_PetCommands = 0x10a1
OP_RaidUpdate = 0x1f21
OP_Consider = 0x65ca
```

### Wire Codes for Phase 2 (High Priority)
```
OP_Camp = 0x78c1
OP_Logout = 0x61ff
OP_LogoutReply = 0x3cdc
OP_RezzRequest = 0x1035
OP_RezzAnswer = 0x6219
OP_RezzComplete = 0x4b05
OP_WhoAllRequest = 0x5cdd
OP_WhoAllResponse = 0x757b
OP_InspectRequest = 0x775d
OP_InspectAnswer = 0x2403
OP_GuildInvite = 0x18b7
OP_GuildMemberList = 0x147d
```
