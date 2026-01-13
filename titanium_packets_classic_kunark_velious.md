# Titanium Packets for Classic/Kunark/Velious Features

This document contains C->S and S->C packets for the Titanium Edition EverQuest client
filtered to include only features that existed in the original EverQuest through the
Scars of Velious expansion (March 1999 - December 2000).

## Expansion Timeline Reference

- **Classic EverQuest**: March 16, 1999
- **Ruins of Kunark**: April 24, 2000
- **Scars of Velious**: December 5, 2000
- ~~Shadows of Luclin: December 4, 2001~~ (EXCLUDED)
- ~~Later expansions~~ (EXCLUDED)

## Features EXCLUDED from this list

The following features were added after Velious and are NOT included:

- **Alternate Advancement (AA)** - Added in Shadows of Luclin
- **Bazaar/Trader System** - Added in Shadows of Luclin
- **Leadership AAs** - Added in Omens of War
- **Tribute System** - Added in Gates of Discord
- **Tasks System** - Added in Lost Dungeons of Norrath
- **Adventures/LDoN Content** - Added in Lost Dungeons of Norrath
- **Dynamic Zones/Expeditions** - Added in Gates of Discord
- **Bandolier** - Added post-Velious
- **Potion Belt** - Added post-Velious
- **Veteran Rewards** - Added post-Velious
- **Guild Tribute** - Added in Gates of Discord
- **XTargeting** - Added in later expansions
- **Voice Macros** - Added post-Velious
- **PVP Leaderboards** - Server-side feature, post-classic
- **Crystals (Radiant/Ebon)** - Added post-Velious
- **Title System** - Added post-Velious
- **LFGuild** - Added post-Velious

---

## Client-to-Server (C->S) Packets - Classic/Kunark/Velious

### Zone Entry / Connection

| Opcode | Wire Code | Description |
|--------|-----------|-------------|
| OP_ZoneEntry | 0x7213 | Client entering zone (contains character name) |
| OP_SetServerFilter | 0x6563 | Set message filtering options |
| OP_ReqClientSpawn | 0x0322 | Request spawn data |
| OP_ReqNewZone | 0x7ac5 | Request zone information |
| OP_SendExpZonein | 0x0587 | Request experience on zone-in |
| OP_ClientReady | 0x5e20 | Client ready signal |
| OP_ClientError | N/A | Client error report |
| OP_ApproveZone | N/A | Zone approval response |
| OP_TGB | 0x0c11 | Target Group Buff toggle |
| OP_AckPacket | 0x7752 | Packet acknowledgment |

### World Server

| Opcode | Wire Code | Description |
|--------|-----------|-------------|
| OP_SendLoginInfo | 0x4dd0 | Send login credentials |
| OP_EnterWorld | 0x7cba | Enter world request |
| OP_DeleteCharacter | 0x26c9 | Delete character request |
| OP_CharacterCreate | 0x10b2 | Create new character |
| OP_RandomNameGenerator | 0x23d4 | Request random name |
| OP_ApproveName | 0x3ea6 | Name approval request |
| OP_WorldClientReady | 0x5e99 | World client ready |
| OP_WorldLogout | 0x7718 | World logout request |
| OP_World_Client_CRC1 | 0x5072 | Client CRC check 1 |
| OP_World_Client_CRC2 | 0x5b18 | Client CRC check 2 |

### Movement / Position

| Opcode | Wire Code | Description |
|--------|-----------|-------------|
| OP_ClientUpdate | 0x14cb | Player position update |
| OP_SpawnAppearance | 0x7c32 | Appearance change (sitting, standing, etc.) |
| OP_WearChange | 0x7441 | Equipment appearance change |
| OP_Jump | 0x0797 | Player jump |
| OP_SetRunMode | 0x4aba | Toggle run/walk mode |

### Combat

| Opcode | Wire Code | Description |
|--------|-----------|-------------|
| OP_AutoAttack | 0x5e55 | Toggle auto-attack |
| OP_AutoAttack2 | 0x0701 | Secondary auto-attack toggle |
| OP_AutoFire | 0x6c53 | Toggle auto-fire (ranged) |
| OP_TargetMouse | 0x6c47 | Target with mouse click |
| OP_TargetCommand | 0x1477 | Target command |
| OP_Taunt | 0x5e48 | Taunt ability |
| OP_CombatAbility | 0x5ee8 | Combat discipline/ability |
| OP_Damage | 0x5c78 | Damage packet (client report) |
| OP_Death | 0x6160 | Death notification |
| OP_EnvDamage | 0x31b3 | Environmental damage |
| OP_Shielding | 0x3fe6 | Shield ability |

### Spells / Casting

| Opcode | Wire Code | Description |
|--------|-----------|-------------|
| OP_CastSpell | 0x304b | Cast a spell |
| OP_MemorizeSpell | 0x308e | Memorize/scribe spell |
| OP_SwapSpell | 0x2126 | Swap spell slots |
| OP_DeleteSpell | 0x4f37 | Delete spell from book |
| OP_LoadSpellSet | 0x403e | Load spell set |
| OP_ManaChange | 0x4839 | Mana change notification |
| OP_Buff | 0x6a53 | Buff packet (click-off) |

### Inventory / Items

| Opcode | Wire Code | Description |
|--------|-----------|-------------|
| OP_MoveItem | 0x420f | Move item in inventory |
| OP_DeleteItem | 0x4d81 | Delete item |
| OP_Consume | 0x77d6 | Consume food/drink |
| OP_ItemLinkClick | 0x53e5 | Click item link |
| OP_ApplyPoison | 0x0c2c | Apply poison to weapon |

### Targeting / Consider

| Opcode | Wire Code | Description |
|--------|-----------|-------------|
| OP_Consider | 0x65ca | Consider target |
| OP_ConsiderCorpse | 0x773f | Consider corpse |
| OP_Assist | 0x7709 | Assist target |
| OP_AssistGroup | 0x5104 | Assist group member |
| OP_TargetHoTT | 0x6a12 | Target of target |

### Looting / Corpses

| Opcode | Wire Code | Description |
|--------|-----------|-------------|
| OP_LootRequest | 0x6f90 | Request to loot corpse |
| OP_EndLootRequest | 0x2316 | End loot session |
| OP_LootItem | 0x7081 | Loot specific item |
| OP_CorpseDrag | 0x50c0 | Drag corpse |
| OP_CorpseDrop | 0x7c7c | Drop dragged corpse |
| OP_ConfirmDelete | 0x3838 | Confirm item deletion |

### Groups

| Opcode | Wire Code | Description |
|--------|-----------|-------------|
| OP_GroupInvite | 0x1b48 | Invite to group |
| OP_GroupInvite2 | 0x12d6 | Alternate group invite |
| OP_GroupFollow | 0x7bc7 | Accept group invite |
| OP_GroupFollow2 | N/A | Alternate accept |
| OP_GroupDisband | 0x0e76 | Disband/leave group |
| OP_GroupCancelInvite | 0x1f27 | Cancel group invite |
| OP_GroupAcknowledge | N/A | Group acknowledgment |
| OP_GroupDelete | N/A | Delete group |

### Raids

| Opcode | Wire Code | Description |
|--------|-----------|-------------|
| OP_RaidJoin | 0x1f21 | Join raid |
| OP_RaidInvite | 0x5891 | Invite to raid |

### Guilds (Basic)

| Opcode | Wire Code | Description |
|--------|-----------|-------------|
| OP_GuildInvite | 0x18b7 | Guild invite |
| OP_GuildInviteAccept | 0x61d0 | Accept guild invite |
| OP_GuildRemove | 0x0179 | Remove from guild |
| OP_GuildDelete | 0x6cce | Delete guild |
| OP_GuildLeader | 0x12b1 | Make guild leader |
| OP_GuildDemote | 0x4eb9 | Demote guild member |
| OP_GuildPublicNote | 0x17a2 | Set public note |
| OP_SetGuildMOTD | 0x591c | Set guild MOTD |
| OP_GetGuildMOTD | 0x7fec | Get guild MOTD |
| OP_GetGuildsList | N/A | Get guilds list |
| OP_GuildPeace | 0x215a | Guild peace |
| OP_GuildWar | 0x0c81 | Guild war |

### Trading (Player to Player)

| Opcode | Wire Code | Description |
|--------|-----------|-------------|
| OP_TradeRequest | 0x372f | Request trade |
| OP_TradeRequestAck | 0x4048 | Acknowledge trade request |
| OP_TradeAcceptClick | 0x0065 | Accept trade |
| OP_CancelTrade | 0x2dc1 | Cancel trade |
| OP_TradeBusy | 0x6839 | Trade busy |
| OP_MoveCoin | 0x7657 | Move coin in trade |

### Merchants

| Opcode | Wire Code | Description |
|--------|-----------|-------------|
| OP_ShopRequest | 0x45f9 | Open merchant window |
| OP_ShopPlayerBuy | 0x221e | Buy from merchant |
| OP_ShopPlayerSell | 0x0e13 | Sell to merchant |
| OP_ShopEnd | 0x7e03 | Close merchant window |

### Objects / Doors

| Opcode | Wire Code | Description |
|--------|-----------|-------------|
| OP_ClickDoor | 0x043b | Click on door |
| OP_ClickObject | 0x3bc2 | Click on object |
| OP_ClickObjectAction | 0x6937 | Object action |
| OP_CloseContainer | N/A | Close container |

### Tradeskills

| Opcode | Wire Code | Description |
|--------|-----------|-------------|
| OP_TradeSkillCombine | 0x0b40 | Combine tradeskill |
| OP_RecipesFavorite | 0x23f0 | Favorite recipes |
| OP_RecipesSearch | 0x164d | Search recipes |
| OP_RecipeDetails | 0x4ea2 | Recipe details request |
| OP_RecipeAutoCombine | 0x0353 | Auto combine recipe |

### Communication

| Opcode | Wire Code | Description |
|--------|-----------|-------------|
| OP_ChannelMessage | 0x1004 | Chat channel message |
| OP_Emote | 0x547a | Emote |
| OP_Consent | 0x1081 | Corpse consent |
| OP_ConsentDeny | 0x4e8c | Deny consent |

### Social / Info

| Opcode | Wire Code | Description |
|--------|-----------|-------------|
| OP_WhoAllRequest | 0x5cdd | Who request |
| OP_InspectRequest | 0x775d | Inspect player request |
| OP_InspectAnswer | 0x2403 | Inspect answer |
| OP_Bug | 0x7ac2 | Bug report |
| OP_Feedback | 0x5306 | Feedback |

### Dueling

| Opcode | Wire Code | Description |
|--------|-----------|-------------|
| OP_RequestDuel | 0x28e1 | Request duel |
| OP_DuelAccept | 0x1b09 | Accept duel |
| OP_DuelDecline | 0x3bad | Decline duel |

### Skills / Abilities

| Opcode | Wire Code | Description |
|--------|-----------|-------------|
| OP_Fishing | 0x0b36 | Fishing |
| OP_Forage | 0x4796 | Forage |
| OP_SenseHeading | 0x05ac | Sense heading |
| OP_Track | 0x5d11 | Tracking |
| OP_TrackTarget | 0x7085 | Track target |
| OP_TrackUnknown | 0x6177 | Track unknown |
| OP_Hide | 0x4312 | Hide |
| OP_Sneak | 0x74e1 | Sneak |
| OP_FeignDeath | 0x7489 | Feign death |
| OP_Mend | 0x14ef | Mend |
| OP_SenseTraps | 0x5666 | Sense traps |
| OP_DisarmTraps | 0x1241 | Disarm traps |
| OP_InstillDoubt | 0x389e | Intimidation |
| OP_PickPocket | 0x2ad8 | Pick pocket |
| OP_Bind_Wound | 0x601d | Bind wound |
| OP_Begging | 0x13e7 | Begging |

### Pets

| Opcode | Wire Code | Description |
|--------|-----------|-------------|
| OP_PetCommands | 0x10a1 | Pet commands |

### Books

| Opcode | Wire Code | Description |
|--------|-----------|-------------|
| OP_ReadBook | 0x1496 | Read book |

### GM Commands

| Opcode | Wire Code | Description |
|--------|-----------|-------------|
| OP_GMTraining | 0x238f | GM training window |
| OP_GMEndTraining | 0x613d | End GM training |
| OP_GMTrainSkill | 0x11d2 | GM train skill |
| OP_GMZoneRequest | 0x1306 | GM zone request |
| OP_GMZoneRequest2 | 0x244c | GM zone request 2 |
| OP_GMSummon | 0x1edc | GM summon |
| OP_GMGoto | 0x1cee | GM goto |
| OP_GMFind | 0x5930 | GM find |
| OP_GMKick | 0x692c | GM kick |
| OP_GMKill | 0x6980 | GM kill |
| OP_GMHideMe | 0x15b2 | GM hide |
| OP_GMToggle | 0x7fea | GM toggle |
| OP_GMEmoteZone | 0x39f2 | GM emote zone |
| OP_GMBecomeNPC | 0x7864 | GM become NPC |
| OP_GMDelCorpse | 0x0b2f | GM delete corpse |
| OP_GMSearchCorpse | 0x3c32 | GM search corpse |
| OP_GMLastName | 0x23a1 | GM last name |
| OP_GMApproval | 0x0c0f | GM approval |
| OP_GMServers | 0x3387 | GM servers |
| OP_GMNameChange | N/A | GM name change |

### Petitions

| Opcode | Wire Code | Description |
|--------|-----------|-------------|
| OP_Petition | 0x251f | Submit petition |
| OP_PetitionQue | 0x33c3 | Petition queue |
| OP_PetitionDelete | 0x5692 | Delete petition |
| OP_PetitionBug | N/A | Petition bug |
| OP_PetitionCheckIn | N/A | Petition check in |
| OP_PetitionResolve | N/A | Petition resolve |
| OP_PetitionUnCheckout | N/A | Petition un-checkout |
| OP_PetitionRefresh | N/A | Petition refresh |
| OP_PDeletePetition | N/A | Delete petition |

### Miscellaneous

| Opcode | Wire Code | Description |
|--------|-----------|-------------|
| OP_Camp | 0x78c1 | Camp/logout |
| OP_Logout | 0x61ff | Logout |
| OP_Save | 0x736b | Save character |
| OP_SaveOnZoneReq | 0x1540 | Save on zone request |
| OP_Surname | 0x4668 | Set surname |
| OP_FaceChange | 0x0f8e | Face change |
| OP_Dye | 0x00dd | Dye armor |
| OP_BoardBoat | 0x4298 | Board boat |
| OP_LeaveBoat | 0x67c9 | Leave boat |
| OP_ControlBoat | 0x2c81 | Control boat |
| OP_ZoneChange | 0x5dd8 | Zone change |
| OP_RandomReq | 0x5534 | Random roll request |
| OP_Split | 0x4848 | Split money |
| OP_FindPersonRequest | 0x3c41 | Find person request |
| OP_LFGCommand | 0x68ac | LFG command |
| OP_Report | 0x7f9d | Report |
| OP_CrashDump | 0x7825 | Crash dump |
| OP_Rewind | 0x4cfa | Rewind command |
| OP_YellForHelp | 0x61ef | Yell for help |
| OP_TestBuff | 0x6ab0 | Test buff |
| OP_SafeFallSuccess | 0x3b21 | Safe fall success |
| OP_PopupResponse | 0x3816 | Popup response |
| OP_RezzAnswer | 0x6219 | Resurrection answer |
| OP_Sacrifice | 0x727a | Sacrifice |
| OP_Translocate | 0x8258 | Translocate |
| OP_KeyRing | 0x68c4 | Key ring |
| OP_CancelSneakHide | 0x48C2 | Cancel sneak/hide |
| OP_DeleteSpawn | 0x55bc | Delete spawn (from client) |
| OP_Illusion | 0x448d | Illusion packet |

---

## Server-to-Client (S->C) Packets - Classic/Kunark/Velious

### Zone Entry / Connection

| Opcode | Wire Code | Description |
|--------|-----------|-------------|
| OP_ZoneEntry | 0x7213 | Player zone entry spawn data |
| OP_PlayerProfile | 0x75df | Full player profile data |
| OP_NewZone | 0x0920 | Zone information |
| OP_ZoneSpawns | 0x2e78 | Zone spawn list |
| OP_SendZonepoints | 0x3eba | Zone points list |
| OP_SpawnDoor | 0x4c24 | Door spawns |
| OP_CharInventory | 0x5394 | Character inventory |
| OP_TimeOfDay | 0x1580 | Time of day |
| OP_ZoneServerReady | N/A | Zone server ready |
| OP_WorldObjectsSent | N/A | World objects sent |

### World Server

| Opcode | Wire Code | Description |
|--------|-----------|-------------|
| OP_SendCharInfo | 0x4513 | Character selection info |
| OP_LogServer | 0x0fa6 | Log server info |
| OP_ApproveWorld | 0x3c25 | World approval |
| OP_MOTD | 0x024d | Message of the day |
| OP_ExpansionInfo | 0x04ec | Expansion info |
| OP_ZoneServerInfo | 0x61b6 | Zone server info |
| OP_SetChatServer | 0x00d7 | Chat server info |
| OP_SetChatServer2 | 0x6536 | Chat server info 2 |
| OP_WorldComplete | 0x509d | World complete |
| OP_PostEnterWorld | 0x52A4 | Post enter world |
| OP_GuildsList | 0x6957 | Guilds list |
| OP_SendMaxCharacters | N/A | Max characters |
| OP_ZoneUnavail | 0x407C | Zone unavailable |
| OP_CharacterStillInZone | 0x60fa | Character still in zone |
| OP_WorldChecksumFailure | 0x7D37 | Checksum failure |
| OP_WorldLoginFailed | 0x8DA7 | Login failed |
| OP_WorldLevelTooHigh | 0x583b | Level too high |
| OP_CharInacessable | 0x436A | Character inaccessible |

### Spawns / NPCs

| Opcode | Wire Code | Description |
|--------|-----------|-------------|
| OP_NewSpawn | 0x1860 | New spawn in zone |
| OP_DeleteSpawn | 0x55bc | Delete spawn |
| OP_SpawnAppearance | 0x7c32 | Spawn appearance change |
| OP_WearChange | 0x7441 | Equipment change |
| OP_Animation | 0x2acf | Animation |
| OP_Illusion | 0x448d | Illusion effect |
| OP_MobRename | 0x0498 | Mob rename |
| OP_BecomeCorpse | 0x4DBC | Become corpse |
| OP_GroundSpawn | 0x0f47 | Ground spawn object |

### Position / Movement

| Opcode | Wire Code | Description |
|--------|-----------|-------------|
| OP_ClientUpdate | 0x14cb | Server position update |
| OP_SpawnPositionUpdate | N/A | Spawn position update |
| OP_RequestClientZoneChange | 0x7834 | Zone change request |
| OP_ZoneChange | 0x5dd8 | Zone change |
| OP_ZonePlayerToBind | 0x385e | Zone player to bind |

### Combat / Damage

| Opcode | Wire Code | Description |
|--------|-----------|-------------|
| OP_Action | 0x497c | Combat action |
| OP_Damage | 0x5c78 | Damage message |
| OP_Death | 0x6160 | Death message |
| OP_SpellEffect | 0x22C5 | Spell effect |

### Health / Stats

| Opcode | Wire Code | Description |
|--------|-----------|-------------|
| OP_HPUpdate | 0x3bcf | HP update |
| OP_MobHealth | 0x0695 | Mob health |
| OP_InitialMobHealth | 0x3d2d | Initial mob health |
| OP_ManaChange | 0x4839 | Mana change |
| OP_ManaUpdate | N/A | Mana update |
| OP_EnduranceUpdate | N/A | Endurance update |
| OP_Stamina | 0x7a83 | Stamina |
| OP_MobEnduranceUpdate | N/A | Mob endurance |
| OP_MobManaUpdate | N/A | Mob mana |
| OP_LevelUpdate | 0x6d44 | Level update |
| OP_LevelAppearance | 0x358e | Level appearance |
| OP_ExpUpdate | 0x5ecd | Experience update |
| OP_SkillUpdate | 0x6a93 | Skill update |
| OP_IncreaseStats | 0x5b7b | Stat increase |

### Spells

| Opcode | Wire Code | Description |
|--------|-----------|-------------|
| OP_BeginCast | 0x3990 | Begin casting |
| OP_InterruptCast | 0x0b97 | Interrupt cast |
| OP_MemorizeSpell | 0x308e | Memorize spell response |
| OP_Buff | 0x6a53 | Buff data |
| OP_LinkedReuse | 0x6a00 | Linked spell reuse |

### Items / Inventory

| Opcode | Wire Code | Description |
|--------|-----------|-------------|
| OP_ItemPacket | 0x3397 | Item data packet |
| OP_ItemLinkResponse | 0x667c | Item link response |
| OP_MoveItem | 0x420f | Move item response |
| OP_DeleteItem | 0x4d81 | Delete item |
| OP_DeleteCharge | 0x1c4a | Delete charge |
| OP_SomeItemPacketMaybe | 0x4033 | Arrow packet |

### Looting

| Opcode | Wire Code | Description |
|--------|-----------|-------------|
| OP_MoneyOnCorpse | 0x7fe4 | Money on corpse |
| OP_LootComplete | 0x0a94 | Loot complete |
| OP_LootItem | 0x7081 | Loot item response |

### Messages

| Opcode | Wire Code | Description |
|--------|-----------|-------------|
| OP_ChannelMessage | 0x1004 | Chat message |
| OP_Emote | 0x547a | Emote |
| OP_FormattedMessage | 0x5a48 | Formatted message |
| OP_SpecialMesg | 0x2372 | Special message |
| OP_SimpleMessage | 0x673c | Simple message |
| OP_ColoredText | 0x0b2d | Colored text |
| OP_ConsentResponse | 0x6380 | Consent response |
| OP_DenyResponse | 0x7c66 | Deny response |

### Groups

| Opcode | Wire Code | Description |
|--------|-----------|-------------|
| OP_GroupUpdate | 0x2dd6 | Group update |
| OP_GroupLeaderChange | N/A | Leader change |

### Raids

| Opcode | Wire Code | Description |
|--------|-----------|-------------|
| OP_RaidUpdate | 0x1f21 | Raid update |

### Guilds (Basic)

| Opcode | Wire Code | Description |
|--------|-----------|-------------|
| OP_GuildMemberList | 0x147d | Guild member list |
| OP_GuildMemberUpdate | 0x0f4d | Guild member update |
| OP_GuildMOTD | 0x475a | Guild MOTD |
| OP_GetGuildMOTDReply | 0x3246 | Guild MOTD reply |
| OP_SetGuildRank | 0x6966 | Set guild rank |
| OP_ZoneGuildList | 0x6957 | Zone guild list |
| OP_GuildMemberAdd | 0x754e | Guild member add |

### Trading

| Opcode | Wire Code | Description |
|--------|-----------|-------------|
| OP_TradeRequestAck | 0x4048 | Trade request ack |
| OP_TradeCoins | 0x34c1 | Trade coins |
| OP_TradeMoneyUpdate | N/A | Trade money update |
| OP_FinishTrade | 0x6014 | Finish trade |
| OP_TradeBusy | 0x6839 | Trade busy |

### Merchants

| Opcode | Wire Code | Description |
|--------|-----------|-------------|
| OP_ShopRequest | 0x45f9 | Merchant data |
| OP_ShopPlayerSell | 0x0e13 | Sell response |
| OP_ShopDelItem | 0x0da9 | Delete merchant item |
| OP_ShopEndConfirm | 0x20b2 | Shop end confirm |

### Objects / Environment

| Opcode | Wire Code | Description |
|--------|-----------|-------------|
| OP_ClickObjectAction | 0x6937 | Click object action |
| OP_ClearObject | 0x21ed | Clear object |
| OP_MoveDoor | 0x700d | Move door |
| OP_RemoveAllDoors | 0x77d0 | Remove all doors |
| OP_Weather | 0x254d | Weather |
| OP_Sound | 0x541e | Sound |
| OP_CameraEffect | 0x0937 | Camera effect |
| OP_DynamicWall | N/A | Dynamic wall |

### Tradeskills

| Opcode | Wire Code | Description |
|--------|-----------|-------------|
| OP_RecipeReply | 0x31f8 | Recipe reply |
| OP_RecipeAutoCombine | 0x0353 | Auto combine result |

### Consider / Target

| Opcode | Wire Code | Description |
|--------|-----------|-------------|
| OP_Consider | 0x65ca | Consider response |
| OP_TargetReject | N/A | Target reject |

### Social / Info

| Opcode | Wire Code | Description |
|--------|-----------|-------------|
| OP_WhoAllResponse | 0x757b | Who response |
| OP_InspectRequest | 0x775d | Inspect request |
| OP_InspectAnswer | 0x2403 | Inspect answer |
| OP_InspectBuffs | 0x4FB6 | Inspect buffs |

### Dueling

| Opcode | Wire Code | Description |
|--------|-----------|-------------|
| OP_RequestDuel | 0x28e1 | Duel request |

### Tracking

| Opcode | Wire Code | Description |
|--------|-----------|-------------|
| OP_Track | 0x5d11 | Track results |

### Petitions

| Opcode | Wire Code | Description |
|--------|-----------|-------------|
| OP_PetitionUpdate | N/A | Petition update |
| OP_PetitionCheckout | N/A | Petition checkout |

### Books

| Opcode | Wire Code | Description |
|--------|-----------|-------------|
| OP_ReadBook | 0x1496 | Book text |

### Pets

| Opcode | Wire Code | Description |
|--------|-----------|-------------|
| OP_PetBuffWindow | 0x4e31 | Pet buff window |
| OP_Charm | 0x12e5 | Charm |

### Resurrection

| Opcode | Wire Code | Description |
|--------|-----------|-------------|
| OP_RezzRequest | 0x1035 | Resurrection request |
| OP_RezzComplete | 0x4b05 | Resurrection complete |

### Disciplines / Abilities

| Opcode | Wire Code | Description |
|--------|-----------|-------------|
| OP_DisciplineUpdate | 0x7180 | Discipline update |
| OP_DisciplineTimer | 0x53df | Discipline timer |

### LFG

| Opcode | Wire Code | Description |
|--------|-----------|-------------|
| OP_LFGAppearance | 0x1a85 | LFG appearance |
| OP_LFGResponse | N/A | LFG response |

### Path Finding

| Opcode | Wire Code | Description |
|--------|-----------|-------------|
| OP_FindPersonReply | 0x5711 | Find person reply |
| OP_ForceFindPerson | N/A | Force find person |

### Miscellaneous

| Opcode | Wire Code | Description |
|--------|-----------|-------------|
| OP_Stun | 0x1E51 | Stun |
| OP_MoneyUpdate | 0x267c | Money update |
| OP_RandomReply | 0x6cd5 | Random roll result |
| OP_LogoutReply | 0x3cdc | Logout reply |
| OP_PreLogoutReply | 0x711e | Pre-logout reply |
| OP_BankerChange | 0x6a5b | Banker change |
| OP_CashReward | 0x4c8c | Cash reward |
| OP_SetFace | N/A | Set face |
| OP_PlayMP3 | 0x26ab | Play MP3 |
| OP_AnnoyingZoneUnknown | 0x729c | Zone unknown |
| OP_FloatListThing | 0x6a1b | Float list |
| OP_QueryResponseThing | 0x1974 | Query response |
| OP_FinishWindow | N/A | Finish window |
| OP_FinishWindow2 | N/A | Finish window 2 |

---

## Summary Statistics

### Client-to-Server Packets (Classic/Kunark/Velious)

| Category | Count |
|----------|-------|
| Zone Entry / Connection | 10 |
| World Server | 10 |
| Movement / Position | 5 |
| Combat | 11 |
| Spells / Casting | 7 |
| Inventory / Items | 5 |
| Targeting / Consider | 5 |
| Looting / Corpses | 6 |
| Groups | 8 |
| Raids | 2 |
| Guilds (Basic) | 12 |
| Trading | 6 |
| Merchants | 4 |
| Objects / Doors | 4 |
| Tradeskills | 5 |
| Communication | 4 |
| Social / Info | 5 |
| Dueling | 3 |
| Skills / Abilities | 16 |
| Pets | 1 |
| Books | 1 |
| GM Commands | 20 |
| Petitions | 9 |
| Miscellaneous | 22 |
| **TOTAL C->S** | **~176** |

### Server-to-Client Packets (Classic/Kunark/Velious)

| Category | Count |
|----------|-------|
| Zone Entry / Connection | 10 |
| World Server | 17 |
| Spawns / NPCs | 9 |
| Position / Movement | 5 |
| Combat / Damage | 4 |
| Health / Stats | 15 |
| Spells | 5 |
| Items / Inventory | 6 |
| Looting | 3 |
| Messages | 8 |
| Groups | 2 |
| Raids | 1 |
| Guilds (Basic) | 7 |
| Trading | 5 |
| Merchants | 4 |
| Objects / Environment | 8 |
| Tradeskills | 2 |
| Consider / Target | 2 |
| Social / Info | 4 |
| Dueling | 1 |
| Tracking | 1 |
| Petitions | 2 |
| Books | 1 |
| Pets | 2 |
| Resurrection | 2 |
| Disciplines | 2 |
| LFG | 2 |
| Path Finding | 2 |
| Miscellaneous | 12 |
| **TOTAL S->C** | **~144** |

---

## Notes

1. This list uses Titanium wire protocol opcodes but restricts functionality to
   Classic/Kunark/Velious era features.

2. The Titanium client (2005) supports many features not available in the original
   game. This document filters to only core features that existed in 1999-2000.

3. Some packets may have slight implementation differences in the original client
   versus the Titanium client's backward-compatible mode.

4. Features like boat travel, tradeskills, and tracking were present in Classic but
   may have been enhanced in later expansions. The core packets remain the same.

## References

- EverQuest expansion history for feature verification
- Source: `common/patches/titanium.cpp`, `patch_Titanium.conf`
- Core structures: `common/patches/titanium_structs.h`
