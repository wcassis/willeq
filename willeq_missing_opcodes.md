# WillEQ Missing and Partial Opcode Implementation Report

This document compares the Classic/Kunark/Velious packet requirements against
the current WillEQ implementation to identify missing and partially implemented
opcodes.

## Summary

| Category | Fully Implemented | Partially Implemented | Not Implemented |
|----------|-------------------|----------------------|-----------------|
| Zone Entry/Connection | 14 | 0 | 2 |
| World Server | 12 | 0 | 0 |
| Movement/Position | 4 | 1 | 1 |
| Combat | 8 | 2 | 3 |
| Spells/Casting | 7 | 0 | 0 |
| Inventory/Items | 5 | 0 | 1 |
| Looting/Corpses | 5 | 0 | 4 |
| Groups | 6 | 0 | 2 |
| Raids | 1 | 1 | 1 |
| Guilds | 2 | 0 | 15 |
| Trading | 7 | 0 | 0 |
| Merchants | 6 | 0 | 0 |
| Objects/Doors | 5 | 1 | 1 |
| Tradeskills | 1 | 1 | 4 |
| Communication | 5 | 0 | 1 |
| Social/Info | 0 | 0 | 6 |
| Dueling | 0 | 0 | 3 |
| Skills/Abilities | 13 | 3 | 3 |
| Pets | 1 | 1 | 0 |
| Books | 1 | 0 | 0 |
| GM Commands | 3 | 0 | 17 |
| Petitions | 0 | 0 | 9 |
| Resurrection | 0 | 0 | 3 |
| Misc | 9 | 2 | 18 |
| **TOTAL** | **~115** | **~12** | **~94** |

---

## NOT IMPLEMENTED - Client-to-Server (C->S)

### Guilds (Priority: HIGH)

| Opcode | Wire Code | Description | Notes |
|--------|-----------|-------------|-------|
| OP_GuildInvite | 0x18b7 | Invite player to guild | Core guild functionality |
| OP_GuildInviteAccept | 0x61d0 | Accept guild invite | Core guild functionality |
| OP_GuildRemove | 0x0179 | Remove member from guild | Officer/leader function |
| OP_GuildDelete | 0x6cce | Delete guild | Leader function |
| OP_GuildLeader | 0x12b1 | Make new guild leader | Leader function |
| OP_GuildDemote | 0x4eb9 | Demote guild member | Officer function |
| OP_GuildPublicNote | 0x17a2 | Set public note | Member function |
| OP_SetGuildMOTD | 0x591c | Set guild MOTD | Officer function |
| OP_GetGuildMOTD | 0x7fec | Get guild MOTD | Core guild functionality |
| OP_GetGuildsList | N/A | Get guilds list | Zone list |
| OP_GuildPeace | 0x215a | Guild peace | PvP servers |
| OP_GuildWar | 0x0c81 | Guild war | PvP servers |

### Raids (Priority: MEDIUM)

| Opcode | Wire Code | Description | Notes |
|--------|-----------|-------------|-------|
| OP_RaidJoin | 0x1f21 | Join raid | Core raid function |
| OP_RaidInvite | 0x5891 | Invite to raid | Core raid function |

### Dueling (Priority: LOW)

| Opcode | Wire Code | Description | Notes |
|--------|-----------|-------------|-------|
| OP_RequestDuel | 0x28e1 | Request duel | PvP feature |
| OP_DuelAccept | 0x1b09 | Accept duel | PvP feature |
| OP_DuelDecline | 0x3bad | Decline duel | PvP feature |

### Looting/Corpses (Priority: MEDIUM)

| Opcode | Wire Code | Description | Notes |
|--------|-----------|-------------|-------|
| OP_CorpseDrag | 0x50c0 | Drag corpse | Corpse retrieval |
| OP_CorpseDrop | 0x7c7c | Drop dragged corpse | Corpse retrieval |
| OP_ConfirmDelete | 0x3838 | Confirm item deletion | Safety dialog |
| OP_ConsiderCorpse | 0x773f | Consider corpse | Corpse info |

### Social/Info (Priority: MEDIUM)

| Opcode | Wire Code | Description | Notes |
|--------|-----------|-------------|-------|
| OP_WhoAllRequest | 0x5cdd | Who request | Player lookup |
| OP_InspectRequest | 0x775d | Inspect player request | Player info |
| OP_InspectAnswer | 0x2403 | Inspect answer (C->S) | Player info response |
| OP_FriendsWho | 0x48fe | Friends who | Friend list |
| OP_Bug | 0x7ac2 | Bug report | Reporting |
| OP_Feedback | 0x5306 | Feedback | Reporting |

### Consent (Priority: MEDIUM)

| Opcode | Wire Code | Description | Notes |
|--------|-----------|-------------|-------|
| OP_Consent | 0x1081 | Corpse consent | Corpse permission |
| OP_ConsentDeny | 0x4e8c | Deny consent | Corpse permission |

### GM Commands (Priority: LOW)

| Opcode | Wire Code | Description | Notes |
|--------|-----------|-------------|-------|
| OP_GMZoneRequest | 0x1306 | GM zone request | Admin |
| OP_GMZoneRequest2 | 0x244c | GM zone request 2 | Admin |
| OP_GMSummon | 0x1edc | GM summon | Admin |
| OP_GMGoto | 0x1cee | GM goto | Admin |
| OP_GMFind | 0x5930 | GM find | Admin |
| OP_GMKick | 0x692c | GM kick | Admin |
| OP_GMKill | 0x6980 | GM kill | Admin |
| OP_GMHideMe | 0x15b2 | GM hide | Admin |
| OP_GMToggle | 0x7fea | GM toggle | Admin |
| OP_GMEmoteZone | 0x39f2 | GM emote zone | Admin |
| OP_GMBecomeNPC | 0x7864 | GM become NPC | Admin |
| OP_GMDelCorpse | 0x0b2f | GM delete corpse | Admin |
| OP_GMSearchCorpse | 0x3c32 | GM search corpse | Admin |
| OP_GMLastName | 0x23a1 | GM last name | Admin |
| OP_GMApproval | 0x0c0f | GM approval | Admin |
| OP_GMServers | 0x3387 | GM servers | Admin |
| OP_GMNameChange | N/A | GM name change | Admin |

### Petitions (Priority: LOW)

| Opcode | Wire Code | Description | Notes |
|--------|-----------|-------------|-------|
| OP_Petition | 0x251f | Submit petition | Support |
| OP_PetitionQue | 0x33c3 | Petition queue | Support |
| OP_PetitionDelete | 0x5692 | Delete petition | Support |
| OP_PetitionBug | N/A | Petition bug | Support |
| OP_PetitionCheckIn | N/A | Petition check in | Support |
| OP_PetitionResolve | N/A | Petition resolve | Support |
| OP_PetitionUnCheckout | N/A | Petition un-checkout | Support |
| OP_PetitionRefresh | N/A | Petition refresh | Support |
| OP_PDeletePetition | N/A | Delete petition | Support |

### Resurrection (Priority: HIGH)

| Opcode | Wire Code | Description | Notes |
|--------|-----------|-------------|-------|
| OP_RezzAnswer | 0x6219 | Resurrection answer | Core death/rez |

### Targeting (Priority: MEDIUM)

| Opcode | Wire Code | Description | Notes |
|--------|-----------|-------------|-------|
| OP_Assist | 0x7709 | Assist target | Combat assist |
| OP_AssistGroup | 0x5104 | Assist group member | Group assist |

### Combat (Priority: MEDIUM)

| Opcode | Wire Code | Description | Notes |
|--------|-----------|-------------|-------|
| OP_Shielding | 0x3fe6 | Shield ability | Warrior ability |
| OP_EnvDamage | 0x31b3 | Environmental damage | Fall/lava damage |

### Skills (Priority: LOW)

| Opcode | Wire Code | Description | Notes |
|--------|-----------|-------------|-------|
| OP_Bind_Wound | 0x601d | Bind wound | Healing skill |
| OP_TrackTarget | 0x7085 | Track target | Tracking |
| OP_TrackUnknown | 0x6177 | Track unknown | Tracking |

### Tradeskills (Priority: LOW)

| Opcode | Wire Code | Description | Notes |
|--------|-----------|-------------|-------|
| OP_RecipesFavorite | 0x23f0 | Favorite recipes | Recipe UI |
| OP_RecipesSearch | 0x164d | Search recipes | Recipe UI |
| OP_RecipeDetails | 0x4ea2 | Recipe details | Recipe UI |
| OP_RecipeAutoCombine | 0x0353 | Auto combine | Convenience |

### Miscellaneous (Priority: VARIES)

| Opcode | Wire Code | Description | Notes |
|--------|-----------|-------------|-------|
| OP_Camp | 0x78c1 | Camp/logout | HIGH - Core logout |
| OP_Logout | 0x61ff | Logout | HIGH - Core logout |
| OP_Save | 0x736b | Save character | MEDIUM - Data safety |
| OP_SaveOnZoneReq | 0x1540 | Save on zone request | MEDIUM |
| OP_Surname | 0x4668 | Set surname | LOW |
| OP_FaceChange | 0x0f8e | Face change | LOW |
| OP_Dye | 0x00dd | Dye armor | LOW |
| OP_BoardBoat | 0x4298 | Board boat | MEDIUM - Travel |
| OP_LeaveBoat | 0x67c9 | Leave boat | MEDIUM - Travel |
| OP_ControlBoat | 0x2c81 | Control boat | LOW |
| OP_RandomReq | 0x5534 | Random roll request | LOW |
| OP_Split | 0x4848 | Split money | MEDIUM - Group loot |
| OP_FindPersonRequest | 0x3c41 | Find person request | LOW |
| OP_LFGCommand | 0x68ac | LFG command | MEDIUM |
| OP_Report | 0x7f9d | Report | LOW |
| OP_Rewind | 0x4cfa | Rewind command | LOW |
| OP_YellForHelp | 0x61ef | Yell for help | LOW |
| OP_PopupResponse | 0x3816 | Popup response | MEDIUM |

### Groups (Priority: MEDIUM)

| Opcode | Wire Code | Description | Notes |
|--------|-----------|-------------|-------|
| OP_GroupAcknowledge | N/A | Group acknowledgment | Edge case |
| OP_GroupDelete | N/A | Delete group | Edge case |

---

## NOT IMPLEMENTED - Server-to-Client (S->C)

### Guilds (Priority: HIGH)

| Opcode | Wire Code | Description | Notes |
|--------|-----------|-------------|-------|
| OP_GuildMemberList | 0x147d | Guild member list | Core guild UI |
| OP_GuildMemberUpdate | 0x0f4d | Guild member update | Member status |
| OP_GetGuildMOTDReply | 0x3246 | Guild MOTD reply | MOTD display |
| OP_SetGuildRank | 0x6966 | Set guild rank | Rank display |
| OP_ZoneGuildList | 0x6957 | Zone guild list | Zone guild info |
| OP_GuildMemberAdd | 0x754e | Guild member add | Member join |

### Raids (Priority: MEDIUM)

| Opcode | Wire Code | Description | Notes |
|--------|-----------|-------------|-------|
| OP_MarkRaidNPC | N/A | Mark raid NPC | Raid targeting |

### Social/Info (Priority: MEDIUM)

| Opcode | Wire Code | Description | Notes |
|--------|-----------|-------------|-------|
| OP_WhoAllResponse | 0x757b | Who response | Who display |
| OP_InspectRequest | 0x775d | Inspect request (S->C) | Inspect popup |
| OP_InspectAnswer | 0x2403 | Inspect answer | Inspect display |
| OP_InspectBuffs | 0x4FB6 | Inspect buffs | Buff display |

### Dueling (Priority: LOW)

| Opcode | Wire Code | Description | Notes |
|--------|-----------|-------------|-------|
| OP_RequestDuel | 0x28e1 | Duel request | PvP |

### Consent (Priority: MEDIUM)

| Opcode | Wire Code | Description | Notes |
|--------|-----------|-------------|-------|
| OP_ConsentResponse | 0x6380 | Consent response | Consent confirm |
| OP_DenyResponse | 0x7c66 | Deny response | Consent deny |

### Resurrection (Priority: HIGH)

| Opcode | Wire Code | Description | Notes |
|--------|-----------|-------------|-------|
| OP_RezzRequest | 0x1035 | Resurrection request | Rez popup |
| OP_RezzComplete | 0x4b05 | Resurrection complete | Rez confirm |

### Groups (Priority: LOW)

| Opcode | Wire Code | Description | Notes |
|--------|-----------|-------------|-------|
| OP_GroupLeaderChange | N/A | Leader change | Leader transfer |

### Tradeskills (Priority: LOW)

| Opcode | Wire Code | Description | Notes |
|--------|-----------|-------------|-------|
| OP_RecipeReply | 0x31f8 | Recipe reply | Recipe UI |
| OP_RecipeAutoCombine | 0x0353 | Auto combine result | Convenience |

### Targeting (Priority: LOW)

| Opcode | Wire Code | Description | Notes |
|--------|-----------|-------------|-------|
| OP_TargetReject | N/A | Target reject | Invalid target |

### Petitions (Priority: LOW)

| Opcode | Wire Code | Description | Notes |
|--------|-----------|-------------|-------|
| OP_PetitionUpdate | N/A | Petition update | Support |
| OP_PetitionCheckout | N/A | Petition checkout | Support |

### Pets (Priority: MEDIUM)

| Opcode | Wire Code | Description | Notes |
|--------|-----------|-------------|-------|
| OP_Charm | 0x12e5 | Charm | Charm break msg |

### Disciplines (Priority: MEDIUM)

| Opcode | Wire Code | Description | Notes |
|--------|-----------|-------------|-------|
| OP_DisciplineUpdate | 0x7180 | Discipline update | Disc availability |
| OP_DisciplineTimer | 0x53df | Discipline timer | Disc cooldown |

### Miscellaneous (Priority: VARIES)

| Opcode | Wire Code | Description | Notes |
|--------|-----------|-------------|-------|
| OP_LogoutReply | 0x3cdc | Logout reply | HIGH - Logout confirm |
| OP_RandomReply | 0x6cd5 | Random roll result | LOW |
| OP_BankerChange | 0x6a5b | Banker change | MEDIUM - Banking |
| OP_CashReward | 0x4c8c | Cash reward | LOW |
| OP_SetFace | N/A | Set face | LOW |
| OP_PlayMP3 | 0x26ab | Play MP3 | LOW - Audio |
| OP_ClearObject | 0x21ed | Clear object | MEDIUM |
| OP_RemoveAllDoors | 0x77d0 | Remove all doors | LOW - Zoning |
| OP_Sound | 0x541e | Sound | LOW - Audio |
| OP_CameraEffect | 0x0937 | Camera effect | LOW |
| OP_DynamicWall | N/A | Dynamic wall | LOW |
| OP_FindPersonReply | 0x5711 | Find person reply | LOW |
| OP_ForceFindPerson | N/A | Force find person | LOW |
| OP_LFGResponse | N/A | LFG response | MEDIUM |

---

## PARTIALLY IMPLEMENTED

These opcodes have handlers but are incomplete or missing functionality.

### Movement/Position

| Opcode | Wire Code | Description | Issue |
|--------|-----------|-------------|-------|
| OP_SpawnAppearance | 0x7c32 | Appearance change | Line 5226: "Other appearance types (invisible, levitate, etc.) - not implemented yet" |

### Combat

| Opcode | Wire Code | Description | Issue |
|--------|-----------|-------------|-------|
| OP_Taunt | 0x5e48 | Taunt ability | Opcode defined, no handler |
| OP_CombatAbility | 0x5ee8 | Combat discipline | Opcode defined, handler incomplete |

### Skills

| Opcode | Wire Code | Description | Issue |
|--------|-----------|-------------|-------|
| OP_ApplyPoison | 0x0c2c | Apply poison | Opcode defined, no handler |
| OP_Track | 0x5d11 | Tracking | Handler exists but result processing minimal |
| OP_AutoFire | 0x6c53 | Auto fire (ranged) | Opcode defined, no handler |

### Tradeskills

| Opcode | Wire Code | Description | Issue |
|--------|-----------|-------------|-------|
| OP_TradeSkillCombine | 0x0b40 | Tradeskill combine | Line 11779: "TODO: Parse combine result and show success/failure message" |

### Objects

| Opcode | Wire Code | Description | Issue |
|--------|-----------|-------------|-------|
| OP_CloseContainer | N/A | Close container | Handler exists, may not send packet |

### Pets

| Opcode | Wire Code | Description | Issue |
|--------|-----------|-------------|-------|
| OP_PetCommands | 0x10a1 | Pet commands | Line 14306: "TODO: m_renderer->getWindowManager()->updatePetButtonState" |

### Raids

| Opcode | Wire Code | Description | Issue |
|--------|-----------|-------------|-------|
| OP_RaidUpdate | 0x1f21 | Raid update | Receives only, no client-initiated actions |

### Misc

| Opcode | Wire Code | Description | Issue |
|--------|-----------|-------------|-------|
| OP_Consider | 0x65ca | Consider | Receives response, sending may be incomplete |
| Follow | N/A | Follow movement | Line 6707: "TODO: Implement actual follow movement" |

---

## IMPLEMENTATION PRIORITY RECOMMENDATIONS

### High Priority (Core Gameplay)

1. **OP_Camp / OP_Logout / OP_LogoutReply** - Players need to properly log out
2. **OP_RezzRequest / OP_RezzAnswer / OP_RezzComplete** - Death recovery is essential
3. **Guild Operations** - Social system is core to EQ gameplay
4. **OP_WhoAllRequest / OP_WhoAllResponse** - Finding players
5. **OP_InspectRequest / OP_InspectAnswer** - Player info

### Medium Priority (Enhanced Gameplay)

1. **Raid Operations** - Endgame content
2. **OP_CorpseDrag / OP_CorpseDrop** - Corpse retrieval
3. **OP_Consent / OP_ConsentDeny** - Corpse permissions
4. **OP_Assist / OP_AssistGroup** - Combat targeting
5. **OP_BoardBoat / OP_LeaveBoat** - Travel
6. **OP_Split** - Group loot distribution
7. **OP_LFGCommand** - Group finding
8. **Combat abilities** (Taunt, CombatAbility, Shielding)
9. **Discipline system** - Warrior/Monk abilities

### Low Priority (Nice to Have)

1. **Dueling** - PvP feature
2. **GM Commands** - Admin tools
3. **Petitions** - Support system
4. **Recipe UI** - Convenience features
5. **Audio** (PlayMP3, Sound)
6. **Cosmetic** (Dye, FaceChange, Surname)

---

## STATISTICS

### By Implementation Status (Classic/Kunark/Velious only)

- **Fully Implemented**: ~115 opcodes (~55%)
- **Partially Implemented**: ~12 opcodes (~6%)
- **Not Implemented**: ~94 opcodes (~39%)

### By Direction

**C->S (Client to Server):**
- Implemented: ~65
- Not Implemented: ~65

**S->C (Server to Client):**
- Implemented: ~80
- Not Implemented: ~40

### By Feature Category (Not Implemented)

| Category | Count | Priority |
|----------|-------|----------|
| Guilds | 17 | HIGH |
| GM Commands | 17 | LOW |
| Miscellaneous | 15 | VARIES |
| Petitions | 9 | LOW |
| Social/Info | 6 | MEDIUM |
| Looting/Corpses | 4 | MEDIUM |
| Tradeskills | 4 | LOW |
| Resurrection | 3 | HIGH |
| Dueling | 3 | LOW |
| Skills | 3 | LOW |
| Combat | 3 | MEDIUM |
| Raids | 3 | MEDIUM |
| Consent | 2 | MEDIUM |
| Targeting | 2 | MEDIUM |
| Groups | 2 | LOW |
| Disciplines | 2 | MEDIUM |

---

## FILES ANALYZED

### WillEQ Source Files

- `include/client/eq.h` - Opcode enums (TitaniumLoginOpcodes, TitaniumWorldOpcodes, TitaniumZoneOpcodes)
- `src/client/eq.cpp` - Packet handlers (~14,765 lines, 71+ zone handlers)
- `include/common/packet_structs.h` - Packet structures (50+)

### Reference Documents

- `titanium_packets_classic_kunark_velious.md` - Target feature set

---

## NOTES

1. WillEQ is approximately 55% complete for Classic/Kunark/Velious packet support.

2. The client excels at core gameplay (movement, combat, spells, inventory, trading, merchants) but lacks social features (guilds, raids, inspect, who).

3. Many "missing" packets are for features less critical in a headless/automated client (GM commands, petitions, audio).

4. Some packets listed as "not implemented" may work via generic handlers or be unnecessary for client-side implementation.

5. The partial implementations mostly involve TODO comments for UI integration or complete result parsing.
