// Titanium login, world and zone opcode tables, plus the UCS client table.
// Lifted verbatim out of the umbrella client header by
// refactor/J16_eq_header_type_extraction.md. Move-only; no value changed.

#pragma once
// Titanium login opcodes
enum TitaniumLoginOpcodes {
	HC_OP_SessionReady = 0x0001,
	HC_OP_Login = 0x0002,
	HC_OP_ServerListRequest = 0x0004,
	HC_OP_PlayEverquestRequest = 0x000d,
	HC_OP_ChatMessage = 0x0016,
	HC_OP_LoginAccepted = 0x0017,
	HC_OP_ServerListResponse = 0x0018,
	HC_OP_PlayEverquestResponse = 0x0021
};

// Titanium world opcodes
enum TitaniumWorldOpcodes {
	HC_OP_SendLoginInfo = 0x4dd0,
	HC_OP_GuildsList = 0x6957,
	HC_OP_LogServer = 0x0fa6,
	HC_OP_ApproveWorld = 0x3c25,
	HC_OP_EnterWorld = 0x7cba,
	HC_OP_PostEnterWorld = 0x52a4,
	HC_OP_ExpansionInfo = 0x04ec,
	HC_OP_SendCharInfo = 0x4513,
	HC_OP_World_Client_CRC1 = 0x5072,
	HC_OP_World_Client_CRC2 = 0x5b18,
	HC_OP_AckPacket = 0x7752,
	HC_OP_WorldClientReady = 0x5e99,
	HC_OP_MOTD = 0x024d,
	HC_OP_SetChatServer = 0x00d7,
	HC_OP_SetChatServer2 = 0x6536,
	HC_OP_ZoneServerInfo = 0x61b6,
	HC_OP_WorldComplete = 0x509d,
	// B888 (#204): character creation and deletion,
	// akk-stack/code/utils/patches/patch_Titanium.conf:21-27.
	HC_OP_DeleteCharacter = 0x26c9,
	HC_OP_CharacterCreate = 0x10b2,
	HC_OP_RandomNameGenerator = 0x23d4,
	HC_OP_ApproveName = 0x3ea6
};

// Titanium zone opcodes
enum TitaniumZoneOpcodes {
	HC_OP_ZoneEntry = 0x7213,
	HC_OP_NewZone = 0x0920,
	HC_OP_ReqClientSpawn = 0x0322,
	HC_OP_ZoneSpawns = 0x2e78,
	HC_OP_SendZonepoints = 0x3eba,
	HC_OP_ReqNewZone = 0x7ac5,
	HC_OP_PlayerProfile = 0x75df,
	HC_OP_CharInventory = 0x5394,
	HC_OP_TimeOfDay = 0x1580,
	HC_OP_SpawnDoor = 0x4c24,
	HC_OP_ClientReady = 0x5e20,
	HC_OP_ZoneChange = 0x5dd8,
	HC_OP_RequestClientZoneChange = 0x7834,  // Server requests client to zone
	HC_OP_SetServerFilter = 0x6563,
	HC_OP_GroundSpawn = 0x0f47,
	HC_OP_Weather = 0x254d,
	HC_OP_ClientUpdate = 0x14cb,
	HC_OP_SpawnAppearance = 0x7c32,
	HC_OP_NewSpawn = 0x1860,
	HC_OP_DeleteSpawn = 0x55bc,
	HC_OP_MobHealth = 0x0695,
	HC_OP_HPUpdate = 0x3bcf,
	HC_OP_TributeUpdate = 0x5639,
	HC_OP_TributeTimer = 0x4665,
	HC_OP_SendAATable = 0x367d,
	HC_OP_UpdateAA = 0x5966,
	HC_OP_RespondAA = 0x3af4,
	HC_OP_SendTributes = 0x067a,
	HC_OP_TributeInfo = 0x152d,
	HC_OP_RequestGuildTributes = 0x5e3a,
	HC_OP_SendGuildTributes = 0x5e3d,
	HC_OP_SendAAStats = 0x5996,
	HC_OP_SendExpZonein = 0x0587,
	HC_OP_WorldObjectsSent = 0x0000,
	HC_OP_ExpUpdate = 0x5ecd,
	// T169: OP_RaidJoin and OP_RaidUpdate are the SAME Titanium wire value,
	// 0x1f21 (patch_Titanium.conf:441 and :443). Everything raid-related
	// that the server sends arrives here, including raidCreate (action 8),
	// which carries a DIFFERENT body -- see raid_update.h's shape table.
	// A separate HC_OP_RaidJoin = 0x3c24 used to sit further down this
	// enum; 0x3c24 is not a Titanium opcode at all (it maps to nothing in
	// patch_Titanium.conf), it had no sender and no dispatch arm, and it
	// cannot be given its real value without colliding with this one.
	HC_OP_RaidUpdate = 0x1f21,
	// C->S OP_RaidCommand. T179 wired a real sender (EQT::encodeRaidCommand):
	// the body is the TITANIUM 136-byte RaidGeneral_Struct (action@0,
	// player_name@4[64], leader_name@68[64], parameter@132) -- eqemu's
	// DECODE(OP_RaidInvite) converts it to the 140-byte internal struct before
	// Handle_OP_RaidCommand. (T168's note here said 140-byte internal; that was
	// wrong -- it predated checking the DECODE, and sending it made the handler
	// read leader_name as empty and drop the command. Corrected live in T179.)
	HC_OP_RaidInvite = 0x5891,   // Invite player to raid
	HC_OP_GuildMOTD = 0x475a,
	HC_OP_ChannelMessage = 0x1004,
	HC_OP_WearChange = 0x7441,
	HC_OP_MoveDoor = 0x700d,
	HC_OP_ClickDoor = 0x043b,
	HC_OP_CompletedTasks = 0x76a2,
	HC_OP_TaskDescription = 0x5ef7,    // S->C: Task description data
	HC_OP_TaskActivity = 0x682d,       // S->C: Task activity update
	HC_OP_ClearLeadershipAbilities = 0x7416,  // S->C: Clear leadership AA
	HC_OP_ClearAA = 0x5918,            // S->C: Clear AA on zone-in
	HC_OP_AAExpUpdate = 0x5f58,        // S->C: AA experience update
	HC_OP_DzCompass = 0x28aa,
	HC_OP_DzExpeditionLockoutTimers = 0x7c12,
	HC_OP_BeginCast = 0x3990,
	HC_OP_ManaChange = 0x4839,
	HC_OP_FormattedMessage = 0x5a48,
	HC_OP_PlayerStateAdd = 0x63da,
	HC_OP_Death = 0x6160,
	HC_OP_PlayerStateRemove = 0x381d,
	HC_OP_Stamina = 0x7a83,
	// T243: 0x2acf is OP_Animation (patch_Titanium.conf:151) -- spawn_id +
	// speed + anim id, both directions. Until T243 it was misnamed HC_OP_Emote
	// and the real text-emote opcode was absent, so /emote went out as
	// OP_ChannelMessage channel 22 and inbound emotes fell through the
	// dispatcher (T239 live: observer saw nothing, actor got no echo).
	HC_OP_Animation = 0x2acf,
	// OP_Emote = 0x547a (patch_Titanium.conf:189): text emote, Emote_Struct
	// {u32 type; char message[1024]} C->S (exactly 1028 bytes or eqemu's
	// Handle_OP_Emote drops it), u32 type + NUL-terminated text S->C.
	HC_OP_Emote = 0x547a,
	HC_OP_Jump = 0x0797,
	// Combat opcodes
	HC_OP_AutoAttack = 0x5e55,
	HC_OP_AutoAttack2 = 0x0701,
	HC_OP_TargetCommand = 0x1477,
	// T374 (BOARD B137): the server's reply to OP_TargetCommand. G01 carried
	// it into kZoneOpcodeTable as the bare literal 0x61f9 -- the only key in
	// either table that was not an enumerator -- because no enumerator had
	// ever been written for it. Value unchanged; the G01 golden
	// (tests/goldens/zone_opcodes_g01.txt) pins it and is byte-identical
	// across the change.
	HC_OP_TargetCommandReply = 0x61f9,
	HC_OP_TargetMouse = 0x6c47,
	HC_OP_Consider = 0x65ca,
	HC_OP_Action = 0x497c,
	HC_OP_CastSpell = 0x304b,
	HC_OP_InterruptCast = 0x0b97,
	HC_OP_ColoredText = 0x0b2d,
	HC_OP_Buff = 0x6a53,
	HC_OP_Damage = 0x5c78,
	HC_OP_LootRequest = 0x6f90,
	HC_OP_LootItem = 0x7081,
	HC_OP_EndLootRequest = 0x2316,  // Client sends to end loot session
	HC_OP_LootComplete = 0x0a94,    // Server sends to client
	HC_OP_ItemPacket = 0x3397,
	HC_OP_MoneyOnCorpse = 0x7fe4,
	HC_OP_FloatListThing = 0x6a1b,
	// Additional opcodes
	HC_OP_BecomeCorpse = 0x4dbc,
	HC_OP_ZonePlayerToBind = 0x385e,
	HC_OP_LevelUpdate = 0x6d44,
	HC_OP_SimpleMessage = 0x673c,
	HC_OP_TargetHoTT = 0x6a12,
	HC_OP_SkillUpdate = 0x6a93,
	HC_OP_CancelTrade = 0x2dc1,
	// Trade opcodes
	HC_OP_TradeRequest = 0x372f,      // Initiate trade request
	HC_OP_TradeRequestAck = 0x4048,   // Accept trade request
	// B1170 (#489): the missile object message (eqemu OP_SomeItemPacketMaybe,
	// patch_Titanium.conf); eqgame's handler builds the object at 00460737.
	HC_OP_SomeItemPacketMaybe = 0x4033,
	// T168: SERVER->CLIENT ONLY. eqemu has no ConnectedOpcodes entry for
	// this opcode at all -- it EMITS it from inside Handle_OP_MoveCoin to
	// tell the other trader coins were added. To put coins into a trade,
	// send OP_MoveCoin (SendMoveCoin). Anything sent on this opcode is
	// discarded unhandled.
	HC_OP_TradeCoins = 0x34c1,        // Server->Client: coin amounts in trade
	HC_OP_MoveCoin = 0x7657,          // Client->Server: move coins to/from trade
	HC_OP_TradeAcceptClick = 0x0065,  // Click accept button in trade
	HC_OP_FinishTrade = 0x6014,       // Trade completed
	HC_OP_PreLogoutReply = 0x711e,
	HC_OP_MobRename = 0x0498,
	HC_OP_Stun = 0x1e51,
	// Inventory opcodes
	HC_OP_MoveItem = 0x420f,
	HC_OP_DeleteItem = 0x4d81,
	// Group opcodes (from patch_Titanium.conf)
	HC_OP_GroupInvite = 0x1b48,
	HC_OP_GroupInvite2 = 0x12d6,
	HC_OP_GroupFollow = 0x7bc7,
	HC_OP_GroupUpdate = 0x2dd6,
	HC_OP_GroupDisband = 0x0e76,
	HC_OP_GroupCancelInvite = 0x1f27,
	HC_OP_SetGroupTarget = 0x3eec,
	// LFG opcodes
	HC_OP_LFGAppearance = 0x1a85,
	// Spell-related opcodes
	HC_OP_LinkedReuse = 0x6a00,
	HC_OP_MemorizeSpell = 0x308e,
	// B1076 fix-2 (UIGAP-ORACLE 205, plan5_swap.tsv step 10, MEASURED lane 1
	// 2026-10-18): the spellbook's committing right-click. Payload is two
	// uint32 spellbook-slot indices (captured 0x00000000 0x00000001); the
	// server acks with the same opcode and the same payload, and it is the
	// server that re-sends the book, so the ack carries no data the client
	// must read. ShowEQ 10/27/05. The scribe's MemorizeSpell above is the
	// scroll->book write; this one exchanges two BOOK slots.
	HC_OP_SwapSpell = 0x2126,
	HC_OP_Illusion = 0x448d,     // Illusion/disguise spell effect
	// Combat ability opcodes
	HC_OP_CombatAbility = 0x5ee8,  // Combat ability use (kick, bash, round kick, etc.)
	HC_OP_Taunt = 0x5e48,          // Taunt skill
	HC_OP_Disarm = 0x17d9,         // Disarm skill
	HC_OP_FeignDeath = 0x7489,     // Feign Death
	HC_OP_Mend = 0x14ef,           // Mend (monk)
	HC_OP_InstillDoubt = 0x389e,   // Intimidation
	// Rogue skill opcodes
	HC_OP_Hide = 0x4312,           // Hide
	HC_OP_Sneak = 0x74e1,          // Sneak
	HC_OP_PickPocket = 0x2ad8,     // Pick Pocket
	HC_OP_SenseTraps = 0x5666,     // Sense Traps
	HC_OP_DisarmTraps = 0x1241,    // Disarm Traps
	HC_OP_ApplyPoison = 0x0c2c,    // Apply Poison
	HC_OP_CancelSneakHide = 0x48c2, // Cancel Sneak/Hide
	// Utility/Trade skill opcodes
	HC_OP_SenseHeading = 0x05ac,   // Sense Heading
	HC_OP_Begging = 0x13e7,        // Begging
	HC_OP_Forage = 0x4796,         // Forage
	HC_OP_Fishing = 0x0b36,        // Fishing
	HC_OP_BindWound = 0x601d,      // Bind Wound
	HC_OP_Track = 0x5d11,          // Track
	HC_OP_TrackTarget = 0x7085,    // Track Target
	// Auto Fire opcode (AutoAttack/AutoAttack2 already defined above)
	HC_OP_AutoFire = 0x6c53,       // Auto Fire (ranged)
	// Misc opcodes
	HC_OP_SpecialMesg = 0x2372,   // Special message (skill-ups, NPC reactions, etc.)
	// Vendor/Merchant opcodes
	HC_OP_ShopRequest = 0x45f9,      // Open vendor window / vendor acknowledgement
	HC_OP_ShopPlayerBuy = 0x221e,    // Buy item from vendor / purchase confirmation
	HC_OP_ShopPlayerSell = 0x0e13,   // Sell item to vendor / sell confirmation
	HC_OP_ShopEnd = 0x7e03,          // Close vendor window
	HC_OP_ShopEndConfirm = 0x20b2,   // Server confirms vendor session ended
	HC_OP_MoneyUpdate = 0x267c,      // Update player money after transaction
	// Book/Note reading opcode
	HC_OP_ReadBook = 0x1496,         // Read note/book item text
	// Tradeskill/Object opcodes
	HC_OP_ClickObject = 0x3bc2,          // Client->Server: click world object (forge, groundspawn, etc.)
	HC_OP_ClickObjectAction = 0x6937,    // Server->Client: object action response (open tradeskill container)
	HC_OP_TradeSkillCombine = 0x0b40,    // Bidirectional: tradeskill combine request/result
// Skill training opcodes
	HC_OP_GMTraining = 0x238f,           // Skill training initiation/data (bidirectional)
	HC_OP_GMTrainSkill = 0x11d2,         // Request to train a specific skill (client -> server)
	HC_OP_GMEndTraining = 0x613d,        // End training session (client -> server)
	HC_OP_GMEndTrainingResponse = 0x0000, // Server ack for end training (unused in Titanium)
	// Pet opcodes
	HC_OP_PetCommands = 0x10a1,          // C->S: Pet command from client
	HC_OP_PetBuffWindow = 0x4e31,        // S->C: Pet buff window data
	// Logout/Camp opcodes
	HC_OP_Camp = 0x78c1,                 // C->S: Camp request (starts 30s timer)
	HC_OP_Logout = 0x61ff,               // C->S: Logout request (after camp timer)
	HC_OP_LogoutReply = 0x3cdc,          // S->C: Server confirmation of logout
	// Resurrection opcodes
	HC_OP_RezzRequest = 0x1035,          // S->C: Resurrection offer
	HC_OP_RezzAnswer = 0x6219,           // C->S: Accept/decline resurrection
	HC_OP_RezzComplete = 0x4b05,         // S->C: Resurrection complete
	// Who opcodes
	HC_OP_WhoAllRequest = 0x5cdd,        // C->S: Request /who results
	HC_OP_WhoAllResponse = 0x757b,       // S->C: Who results
	// Inspect opcodes
	HC_OP_InspectRequest = 0x775d,       // Bidirectional: Request inspect / being inspected
	HC_OP_InspectAnswer = 0x2403,        // Bidirectional: Inspect response data
	// Guild opcodes (C->S)
	HC_OP_GuildInvite = 0x18b7,          // C->S: Invite player to guild
	HC_OP_GuildInviteAccept = 0x61d0,    // C->S: Accept/decline guild invite
	HC_OP_GuildRemove = 0x0179,          // C->S: Remove member from guild
	HC_OP_GuildDelete = 0x6cce,          // C->S: Delete guild
	HC_OP_GuildLeader = 0x12b1,          // C->S: Transfer leadership
	HC_OP_GuildDemote = 0x4eb9,          // C->S: Demote member
	HC_OP_GuildPublicNote = 0x17a2,      // C->S: Set public note
	HC_OP_SetGuildMOTD = 0x591c,         // C->S: Set guild MOTD
	HC_OP_GetGuildMOTD = 0x7fec,         // C->S: Request guild MOTD
	// Guild opcodes (S->C)
	HC_OP_GuildMemberList = 0x147d,      // S->C: Full member list
	HC_OP_GuildMemberUpdate = 0x0f4d,    // S->C: Member status change
	HC_OP_GetGuildMOTDReply = 0x3246,    // S->C: MOTD response
	HC_OP_SetGuildRank = 0x6966,         // S->C: Rank change
	HC_OP_GuildMemberAdd = 0x754e,       // S->C: New member joined
	// Phase 3: Corpse Management
	HC_OP_CorpseDrag = 0x50c0,           // C->S: Start dragging corpse
	HC_OP_CorpseDrop = 0x7c7c,           // C->S: Drop dragged corpse
	HC_OP_ConsiderCorpse = 0x773f,       // C->S: Consider corpse (like /con)
	HC_OP_ConfirmDelete = 0x3838,        // C->S: Confirm corpse deletion
	// Phase 3: Consent System
	HC_OP_Consent = 0x1081,              // C->S: Grant consent to loot corpse
	HC_OP_ConsentDeny = 0x4e8c,          // C->S: Deny consent
	HC_OP_ConsentResponse = 0x6380,      // S->C: Consent granted response
	HC_OP_DenyResponse = 0x7c66,         // S->C: Consent denied response
	// Phase 3: Combat Targeting
	HC_OP_Assist = 0x7709,               // C->S: Assist target (acquire their target)
	HC_OP_AssistGroup = 0x5104,          // C->S: Group assist
	// Phase 3: Travel System
	// T168: the three boat opcodes have NO senders any more, deliberately.
	// BoardBoat's body is the boat's NAME (eqemu requires 5 < size <= 64 and
	// resolves it with entity_list.GetMob(boatname)), not an entity id;
	// ControlBoat files a POSSIBLE_HACK player-event against our own account
	// for any id that is not a controllable boat; and steering is not this
	// opcode at all, it is OP_ClientUpdate with a foreign spawn_id. Boats
	// need a ride/entity model before any of this can be written correctly --
	// refactor/T168_sender_triage.md.
	HC_OP_BoardBoat = 0x4298,            // C->S: Board boat
	HC_OP_LeaveBoat = 0x67c9,            // C->S: Leave boat
	HC_OP_ControlBoat = 0x2c81,          // C->S: Control boat direction
	// Phase 3: Group Split
	HC_OP_Split = 0x4848,                // C->S: Split money with group
	// Phase 3: LFG System
	HC_OP_LFGCommand = 0x68ac,           // C->S: LFG toggle command
	// Phase 3: Raid additions
	// T176: corrected from the wrong 0x5191 (which maps to no opcode in
	// patch_Titanium.conf) to eqemu's OP_MarkNPC = 0x5483 (patch_Titanium.conf
	// :528, the source of truth). Renamed from HC_OP_MarkRaidNPC: Titanium has
	// exactly one mark opcode (OP_MarkNPC); there is no distinct raid-mark
	// opcode, so the "Raid" in the old name was a misnomer. Still no sender and
	// no dispatch arm -- referenced only by eq_strings.cpp.
	HC_OP_MarkNPC = 0x5483,              // C->S: Mark NPC (raid/group target marking)
	// Phase 3: Combat Abilities
	HC_OP_Shielding = 0x3fe6,            // C->S: Shield another player
	HC_OP_EnvDamage = 0x31b3,            // S->C: Environmental damage
	// Phase 3: Discipline System
	HC_OP_DisciplineUpdate = 0x7180,     // S->C: Discipline list update
	HC_OP_DisciplineTimer = 0x53df,      // S->C: Discipline cooldown timer
	// Phase 3: Banking
	HC_OP_BankerChange = 0x6a5b,         // S->C: Bank contents changed
	// Phase 3: Misc opcodes
	HC_OP_Save = 0x736b,                 // C->S: Save character request
	HC_OP_SaveOnZoneReq = 0x1540,        // C->S: Save before zone
	HC_OP_PopupResponse = 0x3816,        // C->S: Response to popup dialog
	HC_OP_ClearObject = 0x21ed,          // S->C: Clear world object
	// Phase 4: Dueling
	HC_OP_RequestDuel = 0x28e1,          // Bidirectional: Duel request
	HC_OP_DuelAccept = 0x1b09,           // C->S: Accept duel
	HC_OP_DuelDecline = 0x3bad,          // C->S: Decline duel
	// Phase 4: Skills (Note: HC_OP_BindWound and HC_OP_TrackTarget already defined above)
	HC_OP_TrackUnknown = 0x6177,         // C->S: Track related
	// Phase 4: Tradeskills
	HC_OP_RecipesFavorite = 0x23f0,      // C->S: Favorite recipes list
	HC_OP_RecipesSearch = 0x164d,        // C->S: Search recipes
	HC_OP_RecipeDetails = 0x4ea2,        // C->S: Get recipe details
	HC_OP_RecipeAutoCombine = 0x0353,    // Bidirectional: Auto-combine
	HC_OP_RecipeReply = 0x31f8,          // S->C: Recipe search results
	// Phase 4: Cosmetic
	HC_OP_Surname = 0x4668,              // C->S: Set surname
	HC_OP_FaceChange = 0x0f8e,           // C->S: Change face
	HC_OP_Dye = 0x00dd,                  // Bidirectional: Dye armor
	// Phase 4: Audio
	HC_OP_PlayMP3 = 0x26ab,              // S->C: Play music
	HC_OP_Sound = 0x541e,                // S->C: Play sound effect
	// Phase 4: Misc
	HC_OP_RandomReq = 0x5534,            // C->S: Random roll request
	HC_OP_RandomReply = 0x6cd5,          // S->C: Random roll result
	HC_OP_FindPersonRequest = 0x3c41,    // C->S: Find person path
	HC_OP_FindPersonReply = 0x5711,      // S->C: Find person result
	HC_OP_CameraEffect = 0x0937,         // S->C: Camera shake/effect
	HC_OP_Rewind = 0x4cfa,               // C->S: Rewind (stuck recovery)
	HC_OP_YellForHelp = 0x61ef,          // C->S: Yell for help
	HC_OP_Report = 0x7f9d,               // C->S: Report/bug
	HC_OP_FriendsWho = 0x48fe,           // C->S: Friends list who
	// Phase 4: GM Commands
	// B1081 (#401): the two OP_GMZoneRequest packets are a QUERY + a CONFIRM,
	// not one round trip. The oracle (patch_Titanium.conf: OP_GMZoneRequest2
	// = 0x244c) queries the server with charname+zone (0x1306), the server
	// replies with the target's safe coords and a success flag, and only
	// THEN the client sends GMZoneRequest2 (a uint32 zone id) -- eqemu's
	// Handle_OP_GMZoneRequest2 (zone/client_packet.cpp:7142) is the one that
	// performs the change, GoToSafeCoords(zonereq, 0) (zoning.cpp:1074).
	// Sending only the first half (what fable did) makes /gmzone a silent
	// no-op for every cross-zone move.
	HC_OP_GMZoneRequest = 0x1306,        // Bidirectional: GM zone query (charname+zone -> safe coords)
	HC_OP_GMZoneRequest2 = 0x244c,       // C->S: GM zone confirm (uint32 zone id -> the change)
	HC_OP_GMSummon = 0x1edc,             // Bidirectional: GM summon player
	HC_OP_GMGoto = 0x1cee,               // C->S: GM goto player
	HC_OP_GMFind = 0x5930,               // Bidirectional: GM find player
	HC_OP_GMKick = 0x692c,               // Bidirectional: GM kick player
	HC_OP_GMKill = 0x6980,               // Bidirectional: GM kill target
	HC_OP_GMHideMe = 0x15b2,             // C->S: GM invisibility
	HC_OP_GMToggle = 0x7fea,             // C->S: GM toggle
	HC_OP_GMEmoteZone = 0x39f2,          // C->S: GM zone emote
	HC_OP_GMBecomeNPC = 0x7864,          // C->S: GM become NPC
	HC_OP_GMSearchCorpse = 0x3c32,       // C->S: GM search corpse
	HC_OP_GMLastName = 0x23a1,           // Bidirectional: GM set last name
	HC_OP_GMApproval = 0x0c0f,           // C->S: GM approval
	HC_OP_GMServers = 0x3387,            // C->S: GM servers
	// Phase 4: Petitions
	HC_OP_Petition = 0x251f,             // C->S: Submit petition
	HC_OP_PetitionQue = 0x33c3,          // C->S: Petition queue
	HC_OP_PetitionDelete = 0x5692        // C->S: Delete petition
};

// UCS (Universal Chat Service) opcodes
enum UCSClientOpcodes {
	HC_OP_UCS_MailLogin = 0x00,
	HC_OP_UCS_ChatMessage = 0x01,
	HC_OP_UCS_ChatJoin = 0x02,
	HC_OP_UCS_ChatLeave = 0x03,
	HC_OP_UCS_ChatWho = 0x04,
	HC_OP_UCS_ChatInvite = 0x05,
	HC_OP_UCS_ChatModerate = 0x06,
	HC_OP_UCS_ChatGrant = 0x07,
	HC_OP_UCS_ChatVoice = 0x08,
	HC_OP_UCS_ChatKick = 0x09,
	HC_OP_UCS_ChatSetOwner = 0x0a,
	HC_OP_UCS_ChatOPList = 0x0b,
	HC_OP_UCS_ChatList = 0x0c,
	HC_OP_UCS_MailHeaderCount = 0x20,
	HC_OP_UCS_MailHeader = 0x21,
	HC_OP_UCS_MailGetBody = 0x22,
	HC_OP_UCS_MailSendBody = 0x23,
	HC_OP_UCS_MailDeleteMsg = 0x24,
	HC_OP_UCS_MailNew = 0x25,
	HC_OP_UCS_Buddy = 0x40,
	HC_OP_UCS_Ignore = 0x41
};
