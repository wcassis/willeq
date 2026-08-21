// Static name/string lookup tables and dump helpers for EverQuest.
// Split out of eq.cpp (F02) — pure move, no behavior change.

#include "client/eq.h"
#include "common/logging.h"
#include "common/packet_structs.h"
#include <iostream>
#include <iomanip>
#include <sstream>
#include <algorithm>
#include <cctype>

std::string EverQuest::FormatPrice(int32_t copperAmount) {
	if (copperAmount <= 0) return "Free";
	std::string result;
	int32_t pp = copperAmount / 1000; copperAmount %= 1000;
	int32_t gp = copperAmount / 100;  copperAmount %= 100;
	int32_t sp = copperAmount / 10;
	int32_t cp = copperAmount % 10;
	if (pp > 0) result += fmt::format("{}p", pp);
	if (gp > 0) { if (!result.empty()) result += ' '; result += fmt::format("{}g", gp); }
	if (sp > 0) { if (!result.empty()) result += ' '; result += fmt::format("{}s", sp); }
	if (cp > 0 || result.empty()) { if (!result.empty()) result += ' '; result += fmt::format("{}c", cp); }
	return result;
}

std::string EverQuest::GetOpcodeName(uint16_t opcode) {
	// Login opcodes
	switch (opcode) {
		case HC_OP_SessionReady: return "HC_OP_SessionReady";
		case HC_OP_Login: return "HC_OP_Login";
		case HC_OP_ServerListRequest: return "HC_OP_ServerListRequest";
		case HC_OP_PlayEverquestRequest: return "HC_OP_PlayEverquestRequest";
		case HC_OP_ChatMessage: return "HC_OP_ChatMessage";
		case HC_OP_LoginAccepted: return "HC_OP_LoginAccepted";
		case HC_OP_ServerListResponse: return "HC_OP_ServerListResponse";
		case HC_OP_PlayEverquestResponse: return "HC_OP_PlayEverquestResponse";
	}

	// World opcodes
	switch (opcode) {
		case HC_OP_SendLoginInfo: return "HC_OP_SendLoginInfo";
		case HC_OP_GuildsList: return "HC_OP_GuildsList";
		case HC_OP_LogServer: return "HC_OP_LogServer";
		case HC_OP_ApproveWorld: return "HC_OP_ApproveWorld";
		case HC_OP_EnterWorld: return "HC_OP_EnterWorld";
		case HC_OP_PostEnterWorld: return "HC_OP_PostEnterWorld";
		case HC_OP_ExpansionInfo: return "HC_OP_ExpansionInfo";
		case HC_OP_SendCharInfo: return "HC_OP_SendCharInfo";
		case HC_OP_World_Client_CRC1: return "HC_OP_World_Client_CRC1";
		case HC_OP_World_Client_CRC2: return "HC_OP_World_Client_CRC2";
		case HC_OP_AckPacket: return "HC_OP_AckPacket";
		case HC_OP_WorldClientReady: return "HC_OP_WorldClientReady";
		case HC_OP_MOTD: return "HC_OP_MOTD";
		case HC_OP_SetChatServer: return "HC_OP_SetChatServer";
		case HC_OP_SetChatServer2: return "HC_OP_SetChatServer2";
		case HC_OP_ZoneServerInfo: return "HC_OP_ZoneServerInfo";
		case HC_OP_WorldComplete: return "HC_OP_WorldComplete";
	}

	// Zone opcodes
	switch (opcode) {
		case HC_OP_ZoneEntry: return "HC_OP_ZoneEntry";
		case HC_OP_NewZone: return "HC_OP_NewZone";
		case HC_OP_ReqClientSpawn: return "HC_OP_ReqClientSpawn";
		case HC_OP_ZoneSpawns: return "HC_OP_ZoneSpawns";
		case HC_OP_SendZonepoints: return "HC_OP_SendZonepoints";
		case HC_OP_ReqNewZone: return "HC_OP_ReqNewZone";
		case HC_OP_PlayerProfile: return "HC_OP_PlayerProfile";
		case HC_OP_CharInventory: return "HC_OP_CharInventory";
		case HC_OP_TimeOfDay: return "HC_OP_TimeOfDay";
		case HC_OP_SpawnDoor: return "HC_OP_SpawnDoor";
		case HC_OP_ClientReady: return "HC_OP_ClientReady";
		case HC_OP_ZoneChange: return "HC_OP_ZoneChange";
		case HC_OP_RequestClientZoneChange: return "HC_OP_RequestClientZoneChange";
		case HC_OP_SetServerFilter: return "HC_OP_SetServerFilter";
		case HC_OP_GroundSpawn: return "HC_OP_GroundSpawn";
		case HC_OP_ClickObject: return "HC_OP_ClickObject";
		case HC_OP_ClickObjectAction: return "HC_OP_ClickObjectAction";
		case HC_OP_TradeSkillCombine: return "HC_OP_TradeSkillCombine";
		case HC_OP_Weather: return "HC_OP_Weather";
		case HC_OP_ClientUpdate: return "HC_OP_ClientUpdate";
		case HC_OP_SpawnAppearance: return "HC_OP_SpawnAppearance";
		case HC_OP_NewSpawn: return "HC_OP_NewSpawn";
		case HC_OP_DeleteSpawn: return "HC_OP_DeleteSpawn";
		case HC_OP_MobHealth: return "HC_OP_MobHealth";
		case HC_OP_HPUpdate: return "HC_OP_HPUpdate";
		case HC_OP_TributeUpdate: return "HC_OP_TributeUpdate";
		case HC_OP_TributeTimer: return "HC_OP_TributeTimer";
		case HC_OP_SendAATable: return "HC_OP_SendAATable";
		case HC_OP_UpdateAA: return "HC_OP_UpdateAA";
		case HC_OP_RespondAA: return "HC_OP_RespondAA";
		case HC_OP_SendTributes: return "HC_OP_SendTributes";
		case HC_OP_TributeInfo: return "HC_OP_TributeInfo";
		case HC_OP_RequestGuildTributes: return "HC_OP_RequestGuildTributes";
		case HC_OP_SendGuildTributes: return "HC_OP_SendGuildTributes";
		case HC_OP_SendAAStats: return "HC_OP_SendAAStats";
		case HC_OP_SendExpZonein: return "HC_OP_SendExpZonein";
		case HC_OP_WorldObjectsSent: return "HC_OP_WorldObjectsSent";
		case HC_OP_ExpUpdate: return "HC_OP_ExpUpdate";
		case HC_OP_RaidUpdate: return "HC_OP_RaidUpdate";
		case HC_OP_GuildMOTD: return "HC_OP_GuildMOTD";
		case HC_OP_ChannelMessage: return "HC_OP_ChannelMessage";
		case HC_OP_WearChange: return "HC_OP_WearChange";
		case HC_OP_Illusion: return "HC_OP_Illusion";
		case HC_OP_MoveDoor: return "HC_OP_MoveDoor";
		case HC_OP_ClickDoor: return "HC_OP_ClickDoor";
		case HC_OP_CompletedTasks: return "HC_OP_CompletedTasks";
		case HC_OP_TaskDescription: return "HC_OP_TaskDescription";
		case HC_OP_TaskActivity: return "HC_OP_TaskActivity";
		case HC_OP_ClearLeadershipAbilities: return "HC_OP_ClearLeadershipAbilities";
		case HC_OP_ClearAA: return "HC_OP_ClearAA";
		case HC_OP_AAExpUpdate: return "HC_OP_AAExpUpdate";
		case HC_OP_DzCompass: return "HC_OP_DzCompass";
		case HC_OP_DzExpeditionLockoutTimers: return "HC_OP_DzExpeditionLockoutTimers";
		case HC_OP_BeginCast: return "HC_OP_BeginCast";
		case HC_OP_ManaChange: return "HC_OP_ManaChange";
		case HC_OP_Buff: return "HC_OP_Buff";
		case HC_OP_FormattedMessage: return "HC_OP_FormattedMessage";
		case HC_OP_PlayerStateAdd: return "HC_OP_PlayerStateAdd";
		case HC_OP_Death: return "HC_OP_Death";
		case HC_OP_PlayerStateRemove: return "HC_OP_PlayerStateRemove";
		case HC_OP_Stamina: return "HC_OP_Stamina";
		case HC_OP_Emote: return "HC_OP_Emote";
		case HC_OP_BecomeCorpse: return "HC_OP_BecomeCorpse";
		case HC_OP_ZonePlayerToBind: return "HC_OP_ZonePlayerToBind";
		case HC_OP_LevelUpdate: return "HC_OP_LevelUpdate";
		case HC_OP_SimpleMessage: return "HC_OP_SimpleMessage";
		case HC_OP_TargetHoTT: return "HC_OP_TargetHoTT";
		case HC_OP_SkillUpdate: return "HC_OP_SkillUpdate";
		case HC_OP_CancelTrade: return "HC_OP_CancelTrade";
		case HC_OP_TradeRequest: return "HC_OP_TradeRequest";
		case HC_OP_TradeRequestAck: return "HC_OP_TradeRequestAck";
		case HC_OP_TradeCoins: return "HC_OP_TradeCoins";
		case HC_OP_MoveCoin: return "HC_OP_MoveCoin";
		case HC_OP_TradeAcceptClick: return "HC_OP_TradeAcceptClick";
		case HC_OP_FinishTrade: return "HC_OP_FinishTrade";
		case HC_OP_PreLogoutReply: return "HC_OP_PreLogoutReply";
		case HC_OP_Camp: return "HC_OP_Camp";
		case HC_OP_Logout: return "HC_OP_Logout";
		case HC_OP_LogoutReply: return "HC_OP_LogoutReply";
		case HC_OP_RezzRequest: return "HC_OP_RezzRequest";
		case HC_OP_RezzAnswer: return "HC_OP_RezzAnswer";
		case HC_OP_RezzComplete: return "HC_OP_RezzComplete";
		case HC_OP_WhoAllRequest: return "HC_OP_WhoAllRequest";
		case HC_OP_WhoAllResponse: return "HC_OP_WhoAllResponse";
		case HC_OP_InspectRequest: return "HC_OP_InspectRequest";
		case HC_OP_InspectAnswer: return "HC_OP_InspectAnswer";
		case HC_OP_GuildInvite: return "HC_OP_GuildInvite";
		case HC_OP_GuildInviteAccept: return "HC_OP_GuildInviteAccept";
		case HC_OP_GuildRemove: return "HC_OP_GuildRemove";
		case HC_OP_GuildDelete: return "HC_OP_GuildDelete";
		case HC_OP_GuildLeader: return "HC_OP_GuildLeader";
		case HC_OP_GuildDemote: return "HC_OP_GuildDemote";
		case HC_OP_GuildPublicNote: return "HC_OP_GuildPublicNote";
		case HC_OP_SetGuildMOTD: return "HC_OP_SetGuildMOTD";
		case HC_OP_GetGuildMOTD: return "HC_OP_GetGuildMOTD";
		case HC_OP_GuildMemberList: return "HC_OP_GuildMemberList";
		case HC_OP_GuildMemberUpdate: return "HC_OP_GuildMemberUpdate";
		case HC_OP_GetGuildMOTDReply: return "HC_OP_GetGuildMOTDReply";
		case HC_OP_SetGuildRank: return "HC_OP_SetGuildRank";
		case HC_OP_GuildMemberAdd: return "HC_OP_GuildMemberAdd";
		// Phase 3 opcodes
		case HC_OP_CorpseDrag: return "HC_OP_CorpseDrag";
		case HC_OP_CorpseDrop: return "HC_OP_CorpseDrop";
		case HC_OP_ConsiderCorpse: return "HC_OP_ConsiderCorpse";
		case HC_OP_ConfirmDelete: return "HC_OP_ConfirmDelete";
		case HC_OP_Consent: return "HC_OP_Consent";
		case HC_OP_ConsentDeny: return "HC_OP_ConsentDeny";
		case HC_OP_ConsentResponse: return "HC_OP_ConsentResponse";
		case HC_OP_DenyResponse: return "HC_OP_DenyResponse";
		case HC_OP_Assist: return "HC_OP_Assist";
		case HC_OP_AssistGroup: return "HC_OP_AssistGroup";
		case HC_OP_BoardBoat: return "HC_OP_BoardBoat";
		case HC_OP_LeaveBoat: return "HC_OP_LeaveBoat";
		case HC_OP_ControlBoat: return "HC_OP_ControlBoat";
		case HC_OP_Split: return "HC_OP_Split";
		case HC_OP_LFGCommand: return "HC_OP_LFGCommand";
		case HC_OP_MarkRaidNPC: return "HC_OP_MarkRaidNPC";
		case HC_OP_Shielding: return "HC_OP_Shielding";
		case HC_OP_EnvDamage: return "HC_OP_EnvDamage";
		case HC_OP_DisciplineUpdate: return "HC_OP_DisciplineUpdate";
		case HC_OP_DisciplineTimer: return "HC_OP_DisciplineTimer";
		case HC_OP_BankerChange: return "HC_OP_BankerChange";
		case HC_OP_Save: return "HC_OP_Save";
		case HC_OP_SaveOnZoneReq: return "HC_OP_SaveOnZoneReq";
		case HC_OP_PopupResponse: return "HC_OP_PopupResponse";
		case HC_OP_ClearObject: return "HC_OP_ClearObject";
		// Phase 4 opcodes
		case HC_OP_RequestDuel: return "HC_OP_RequestDuel";
		case HC_OP_DuelAccept: return "HC_OP_DuelAccept";
		case HC_OP_DuelDecline: return "HC_OP_DuelDecline";
		case HC_OP_BindWound: return "HC_OP_BindWound";
		case HC_OP_TrackTarget: return "HC_OP_TrackTarget";
		case HC_OP_TrackUnknown: return "HC_OP_TrackUnknown";
		case HC_OP_RecipesFavorite: return "HC_OP_RecipesFavorite";
		case HC_OP_RecipesSearch: return "HC_OP_RecipesSearch";
		case HC_OP_RecipeDetails: return "HC_OP_RecipeDetails";
		case HC_OP_RecipeAutoCombine: return "HC_OP_RecipeAutoCombine";
		case HC_OP_RecipeReply: return "HC_OP_RecipeReply";
		case HC_OP_Surname: return "HC_OP_Surname";
		case HC_OP_FaceChange: return "HC_OP_FaceChange";
		case HC_OP_Dye: return "HC_OP_Dye";
		case HC_OP_PlayMP3: return "HC_OP_PlayMP3";
		case HC_OP_Sound: return "HC_OP_Sound";
		case HC_OP_RandomReq: return "HC_OP_RandomReq";
		case HC_OP_RandomReply: return "HC_OP_RandomReply";
		case HC_OP_FindPersonRequest: return "HC_OP_FindPersonRequest";
		case HC_OP_FindPersonReply: return "HC_OP_FindPersonReply";
		case HC_OP_CameraEffect: return "HC_OP_CameraEffect";
		case HC_OP_Rewind: return "HC_OP_Rewind";
		case HC_OP_YellForHelp: return "HC_OP_YellForHelp";
		case HC_OP_Report: return "HC_OP_Report";
		case HC_OP_FriendsWho: return "HC_OP_FriendsWho";
		case HC_OP_GMZoneRequest: return "HC_OP_GMZoneRequest";
		case HC_OP_GMSummon: return "HC_OP_GMSummon";
		case HC_OP_GMGoto: return "HC_OP_GMGoto";
		case HC_OP_GMFind: return "HC_OP_GMFind";
		case HC_OP_GMKick: return "HC_OP_GMKick";
		case HC_OP_GMKill: return "HC_OP_GMKill";
		case HC_OP_GMHideMe: return "HC_OP_GMHideMe";
		case HC_OP_GMToggle: return "HC_OP_GMToggle";
		case HC_OP_GMEmoteZone: return "HC_OP_GMEmoteZone";
		case HC_OP_GMBecomeNPC: return "HC_OP_GMBecomeNPC";
		case HC_OP_GMSearchCorpse: return "HC_OP_GMSearchCorpse";
		case HC_OP_GMLastName: return "HC_OP_GMLastName";
		case HC_OP_GMApproval: return "HC_OP_GMApproval";
		case HC_OP_GMServers: return "HC_OP_GMServers";
		case HC_OP_Petition: return "HC_OP_Petition";
		case HC_OP_PetitionQue: return "HC_OP_PetitionQue";
		case HC_OP_PetitionDelete: return "HC_OP_PetitionDelete";
		case HC_OP_FloatListThing: return "HC_OP_FloatListThing";
		case HC_OP_MobRename: return "HC_OP_MobRename";
		case HC_OP_Stun: return "HC_OP_Stun";
		case HC_OP_Consider: return "HC_OP_Consider";
		case HC_OP_TargetMouse: return "HC_OP_TargetMouse";
		case HC_OP_AutoAttack: return "HC_OP_AutoAttack";
		case HC_OP_AutoAttack2: return "HC_OP_AutoAttack2";
		case HC_OP_CastSpell: return "HC_OP_CastSpell";
		case HC_OP_InterruptCast: return "HC_OP_InterruptCast";
		case HC_OP_ColoredText: return "HC_OP_ColoredText";
		case HC_OP_LootRequest: return "HC_OP_LootRequest";
		case HC_OP_LootItem: return "HC_OP_LootItem";
		case HC_OP_EndLootRequest: return "HC_OP_EndLootRequest";
		case HC_OP_LootComplete: return "HC_OP_LootComplete";
		case HC_OP_ItemPacket: return "HC_OP_ItemPacket";
		case HC_OP_MoneyOnCorpse: return "HC_OP_MoneyOnCorpse";
		case HC_OP_Damage: return "HC_OP_Damage";
		case HC_OP_MoveItem: return "HC_OP_MoveItem";
		case HC_OP_DeleteItem: return "HC_OP_DeleteItem";
		case HC_OP_SetGroupTarget: return "HC_OP_SetGroupTarget";
		case HC_OP_LFGAppearance: return "HC_OP_LFGAppearance";
		case HC_OP_LinkedReuse: return "HC_OP_LinkedReuse";
		case HC_OP_MemorizeSpell: return "HC_OP_MemorizeSpell";
		case HC_OP_SpecialMesg: return "HC_OP_SpecialMesg";
		case HC_OP_ShopRequest: return "HC_OP_ShopRequest";
		case HC_OP_ShopPlayerBuy: return "HC_OP_ShopPlayerBuy";
		case HC_OP_ShopPlayerSell: return "HC_OP_ShopPlayerSell";
		case HC_OP_ShopEnd: return "HC_OP_ShopEnd";
		case HC_OP_ShopEndConfirm: return "HC_OP_ShopEndConfirm";
		case HC_OP_MoneyUpdate: return "HC_OP_MoneyUpdate";
		case HC_OP_GMTraining: return "HC_OP_GMTraining";
		case HC_OP_GMTrainSkill: return "HC_OP_GMTrainSkill";
		case HC_OP_GMEndTraining: return "HC_OP_GMEndTraining";
		default: return fmt::format("OP_Unknown_{:#06x}", opcode);
	}
}

std::string EverQuest::GetStringMessage(uint32_t string_id) {
	// Use StringDatabase if loaded
	if (m_string_db.isLoaded()) {
		std::string msg = m_string_db.getString(string_id);
		if (!msg.empty()) {
			return msg;
		}
		// Fall through to return unknown message
		return fmt::format("[Unknown message #{}]", string_id);
	}

	// Fallback to hardcoded messages (if string files not loaded)
	switch (string_id) {
		case 100: return "Your target is out of range, get closer!";
		case 101: return "Target player not found.";
		case 104: return "Trade cancelled, duplicated Lore Items would result.";
		case 105: return "You cannot form an affinity with this area. Try a city.";
		case 106: return "This spell does not work here.";
		case 107: return "This spell does not work on this plane.";
		case 108: return "You cannot see your target.";
		case 113: return "The next group buff you cast will hit all targets in range.";
		case 114: return "You escape from combat, hiding yourself from view.";
		case 116: return "Your ability failed. Timer has been reset.";
		case 119: return "Alternate Experience is *OFF*.";
		case 121: return "Alternate Experience is *ON*.";
		case 124: return "Your target is too far away, get closer!";
		case 126: return "Your will is not sufficient to command this weapon.";
		case 127: return "Your pet's will is not sufficient to command its weapon.";
		case 128: return "You unleash a flurry of attacks.";
		case 129: return "You failed to disarm the trap.";
		case 130: return "It's locked and you're not holding the key.";
		case 131: return "This lock cannot be picked.";
		case 132: return "You are not sufficiently skilled to pick this lock.";
		case 133: return "You opened the locked door with your magic GM key.";
		case 136: return "You are not sufficient level to use this item.";
		case 138: return "You gain experience!!";
		case 139: return "You gain party experience!!";
		case 143: return "Your bow shot did double dmg.";
		case 147: return "Someone is bandaging you.";
		case 150: return "You have scrounged up some fishing grubs.";
		case 151: return "You have scrounged up some water.";
		case 152: return "You have scrounged up some food.";
		case 153: return "You have scrounged up some drink.";
		case 154: return "You have scrounged up something that doesn't look edible.";
		case 155: return "You fail to locate any food nearby.";
		case 156: return "You are already fishing!";
		case 160: return "You can't fish without a fishing pole, go buy one.";
		case 161: return "You need to put your fishing pole in your primary hand.";
		case 162: return "You can't fish without fishing bait, go buy some.";
		case 163: return "You cast your line.";
		case 164: return "You're not scaring anyone.";
		case 165: return "You stop fishing and go on your way.";
		case 166: return "Trying to catch land sharks perhaps?";
		case 167: return "Trying to catch a fire elemental or something?";
		case 168: return "You didn't catch anything.";
		case 169: return "Your fishing pole broke!";
		case 170: return "You caught, something...";
		case 171: return "You spill your beer while bringing in your line.";
		case 172: return "You lost your bait!";
		case 173: return "Your spell fizzles!";
		case 179: return "You cannot use this item unless it is equipped.";
		case 180: return "You miss a note, bringing your song to a close!";
		case 181: return "Your race, class, or deity cannot use this item.";
		case 182: return "Item is out of charges.";
		case 189: return "You are already on a mount.";
		case 191: return "Your target has no mana to affect";
		case 196: return "You must first target a group member.";
		case 197: return "Your spell is too powerful for your intended target.";
		case 199: return "Insufficient Mana to cast this spell!";
		case 203: return "This being is not a worthy sacrifice.";
		case 204: return "This being is too powerful to be a sacrifice.";
		case 205: return "You cannot sacrifice yourself.";
		case 207: return "You *CANNOT* cast spells, you have been silenced!";
		case 208: return "Spell can only be cast during the day.";
		case 209: return "Spell can only be cast during the night.";
		case 210: return "That spell can not affect this target PC.";
		case 214: return "You must first select a target for this spell!";
		case 215: return "You must first target a living group member whose corpse you wish to summon.";
		case 221: return "This spell only works on corpses.";
		case 224: return "You can't drain yourself!";
		case 230: return "This corpse is not valid.";
		case 231: return "This player cannot be resurrected. The corpse is too old.";
		case 234: return "You can only cast this spell in the outdoors.";
		case 236: return "Spell recast time not yet met.";
		case 237: return "Spell recovery time not yet met.";
		case 239: return "Your target cannot be mesmerized.";
		case 240: return "Your target cannot be mesmerized (with this spell).";
		case 241: return "Your target is immune to the stun portion of this effect.";
		case 242: return "Your target is immune to changes in its attack speed.";
		case 243: return "Your target is immune to fear spells.";
		case 244: return "Your target is immune to changes in its run speed.";
		case 246: return "You cannot have more than one pet at a time.";
		case 248: return "Your target is too high of a level for your charm spell.";
		case 251: return "That spell can not affect this target NPC.";
		case 254: return "You are no longer feigning death, because a spell hit you.";
		case 255: return "You do not have a pet.";
		case 256: return "Your pet is the focus of something's attention.";
		case 260: return "Your gate is too unstable, and collapses.";
		case 262: return "You cannot sense any corpses for this PC in this zone.";
		case 263: return "Your spell did not take hold.";
		case 267: return "This NPC cannot be charmed.";
		case 268: return "Your target looks unaffected.";
		case 269: return "Stick to singing until you learn to play this instrument.";
		case 270: return "You regain your concentration and continue your casting.";
		case 271: return "Your spell would not have taken hold on your target.";
		case 272: return "You are missing some required spell components.";
		case 275: return "You feel yourself starting to appear.";
		case 303: return "You have slain %1!";
		case 334: return "You gained experience!";
		case 335: return "You gained raid experience!";
		case 336: return "You gained group leadership experience!";
		case 337: return "You gained raid leadership experience!";
		default: return fmt::format("[Unknown message #{}]", string_id);
	}
}

std::string EverQuest::GetFormattedStringMessage(uint32_t string_id, const std::vector<std::string>& args) {
	// Use StringDatabase if loaded
	if (m_string_db.isLoaded()) {
		std::string msg = m_string_db.formatString(string_id, args);
		if (!msg.empty()) {
			return msg;
		}
		return fmt::format("[Unknown message #{}]", string_id);
	}

	// Fallback: get template and do basic substitution
	std::string tmpl = GetStringMessage(string_id);
	if (tmpl.empty() || tmpl[0] == '[') {
		return tmpl;  // Unknown message
	}

	// Basic %1, %2, etc. substitution
	std::string result = tmpl;
	for (size_t i = 0; i < args.size() && i < 9; i++) {
		std::string placeholder = "%" + std::to_string(i + 1);
		size_t pos = result.find(placeholder);
		if (pos != std::string::npos) {
			result.replace(pos, placeholder.length(), args[i]);
		}
	}
	return result;
}

bool EverQuest::LoadStringFiles(const std::string& eqClientPath) {
	(void)eqClientPath;
	if (!m_string_db.initFromDatabase()) {
		LOG_ERROR(MOD_MAIN, "Failed to initialize string database from SQLite");
		return false;
	}
	LOG_INFO(MOD_MAIN, "Loaded strings from SQLite ({} eqstr, {} dbstr)",
			 m_string_db.getEqStrCount(), m_string_db.getDbStrCount());
	return true;
}

std::string EverQuest::GetChatTypeName(uint32_t chat_type) {
	// Common chat types
	switch (chat_type) {
		case 0: return "Say";
		case 1: return "Tell";
		case 2: return "Group";
		case 3: return "Guild";
		case 4: return "OOC";
		case 5: return "Auction";
		case 6: return "Shout";
		case 7: return "Emote";
		case 8: return "Spells";
		case 11: return "GM";
		case 13: return "Skills";
		case 14: return "Chat";
		case 15: return "White";
		case 20: return "DarkGray";
		case 124: return "YouSlain";
		case 138: return "ExpGain";
		case 254: return "Yellow";
		case 257: return "LightGray";
		case 258: return "Red";
		case 259: return "Green";
		case 260: return "Blue";
		case 261: return "DarkBlue";
		case 262: return "Purple";
		case 263: return "LightBlue";
		case 264: return "Black";
		case 265: return "TooFarAway";
		case 269: return "NPCRampage";
		case 270: return "NPCFlurry";
		case 271: return "NPCEnrage";
		case 273: return "EchoSay";
		case 274: return "EchoTell";
		case 275: return "EchoGroup";
		case 276: return "EchoGuild";
		case 283: return "NonMelee";
		case 284: return "SpellWornOff";
		case 289: return "MeleeCrit";
		case 294: return "SpellCrit";
		case 304: return "DamageShield";
		case 305: return "Experience";
		case 313: return "Faction";
		case 315: return "Loot";
		case 316: return "Dice";
		case 317: return "ItemLink";
		case 319: return "RaidSay";
		case 320: return "MyPet";
		case 322: return "OthersPet";
		case 330: return "FocusEffect";
		case 337: return "ItemBenefit";
		case 342: return "Strikethrough";
		case 343: return "StunResist";
		default: return fmt::format("Type{}", chat_type);
	}
}

std::string EverQuest::GetClassName(uint32_t class_id) {
	switch (class_id) {
		case 1: return "Warrior";
		case 2: return "Cleric";
		case 3: return "Paladin";
		case 4: return "Ranger";
		case 5: return "Shadow Knight";
		case 6: return "Druid";
		case 7: return "Monk";
		case 8: return "Bard";
		case 9: return "Rogue";
		case 10: return "Shaman";
		case 11: return "Necromancer";
		case 12: return "Wizard";
		case 13: return "Magician";
		case 14: return "Enchanter";
		case 15: return "Beastlord";
		case 16: return "Berserker";
		default: return fmt::format("Class{}", class_id);
	}
}

std::string EverQuest::GetRaceName(uint32_t race_id) {
	switch (race_id) {
		case 1: return "Human";
		case 2: return "Barbarian";
		case 3: return "Erudite";
		case 4: return "Wood Elf";
		case 5: return "High Elf";
		case 6: return "Dark Elf";
		case 7: return "Half Elf";
		case 8: return "Dwarf";
		case 9: return "Troll";
		case 10: return "Ogre";
		case 11: return "Halfling";
		case 12: return "Gnome";
		case 14: return "Iksar";
		case 128: return "Vah Shir";
		case 130: return "Froglok";
		case 330: return "Drakkin";
		default: return fmt::format("Race{}", race_id);
	}
}

std::string EverQuest::GetDeityName(uint32_t deity_id) {
	switch (deity_id) {
		case 0: return "Agnostic";
		case 140: return "Agnostic";
		case 201: return "Bertoxxulous";
		case 202: return "Brell Serilis";
		case 203: return "Cazic-Thule";
		case 204: return "Erollisi Marr";
		case 205: return "Bristlebane";
		case 206: return "Innoruuk";
		case 207: return "Karana";
		case 208: return "Mithaniel Marr";
		case 209: return "Prexus";
		case 210: return "Quellious";
		case 211: return "Rallos Zek";
		case 212: return "Rodcet Nife";
		case 213: return "Solusek Ro";
		case 214: return "The Tribunal";
		case 215: return "Tunare";
		case 216: return "Veeshan";
		case 396: return "Agnostic";
		default: return fmt::format("Deity{}", deity_id);
	}
}

std::string EverQuest::GetBodyTypeName(uint8_t bodytype) {
	switch (bodytype) {
		case 0: return "Humanoid";
		case 1: return "Lycanthrope";
		case 2: return "Undead";
		case 3: return "Giant";
		case 4: return "Construct";
		case 5: return "Extraplanar";
		case 6: return "Magical";
		case 7: return "Summoned Undead";
		case 8: return "BaneGiant";
		case 9: return "Dain";
		case 10: return "NoTarget";
		case 11: return "Vampire";
		case 12: return "Atenha Ra";
		case 13: return "Greater Akheva";
		case 14: return "Khati Sha";
		case 15: return "Seru";
		case 16: return "Grieg Veneficus";
		case 17: return "Draz Nurakk";
		case 18: return "Zek";
		case 19: return "Luggald";
		case 20: return "Animal";
		case 21: return "Insect";
		case 22: return "Monster";
		case 23: return "Summoned";
		case 24: return "Plant";
		case 25: return "Dragon";
		case 26: return "Summoned2";
		case 27: return "Summoned3";
		case 28: return "Dragon2";
		case 29: return "VeliousDragon";
		case 30: return "Familiar";
		case 31: return "Dragon3";
		case 32: return "Boxes";
		case 33: return "Muramite";
		case 34: return "NoTarget2";
		case 60: return "Untargetable";
		case 63: return "SwarmPet";
		case 64: return "MonsterSummon";
		case 66: return "InvisibleMan";
		case 67: return "Special";
		default: return fmt::format("BodyType{}", bodytype);
	}
}

std::string EverQuest::GetEquipSlotName(int slot) {
	switch (slot) {
		case 0: return "Head";
		case 1: return "Chest";
		case 2: return "Arms";
		case 3: return "Wrist";
		case 4: return "Hands";
		case 5: return "Legs";
		case 6: return "Feet";
		case 7: return "Primary";
		case 8: return "Secondary";
		default: return fmt::format("Slot{}", slot);
	}
}

std::string EverQuest::GetNpcTypeName(uint8_t npc_type) {
	switch (npc_type) {
		case 0: return "Player";
		case 1: return "NPC";
		case 2: return "PC Corpse";
		case 3: return "NPC Corpse";
		default: return fmt::format("Type{}", npc_type);
	}
}

void EverQuest::DumpEntityAppearance(const std::string& name) const {
	// Find entity by name (case-insensitive, underscore-tolerant)
	std::string search_lower = name;
	std::transform(search_lower.begin(), search_lower.end(), search_lower.begin(), ::tolower);
	std::replace(search_lower.begin(), search_lower.end(), ' ', '_');

	for (const auto& [id, entity] : m_entities) {
		std::string entity_name_lower = entity.name;
		std::transform(entity_name_lower.begin(), entity_name_lower.end(), entity_name_lower.begin(), ::tolower);
		if (entity_name_lower.find(search_lower) != std::string::npos) {
			DumpEntityAppearance(id);
			return;
		}
	}
	std::cout << fmt::format("Entity '{}' not found", name) << std::endl;
}

void EverQuest::DumpEntityAppearance(uint16_t spawn_id) const {
	auto it = m_entities.find(spawn_id);
	if (it == m_entities.end()) {
		std::cout << fmt::format("Entity with spawn_id {} not found", spawn_id) << std::endl;
		return;
	}

	const Entity& e = it->second;

	std::cout << "========================================" << std::endl;
	std::cout << fmt::format("Entity Appearance Dump: {} (ID: {})", e.name, e.spawn_id) << std::endl;
	std::cout << "========================================" << std::endl;

	// Basic info
	std::cout << fmt::format("Type:      {} ({})", GetNpcTypeName(e.npc_type), e.npc_type) << std::endl;
	std::cout << fmt::format("Race:      {} (ID: {})", GetRaceName(e.race_id), e.race_id) << std::endl;
	std::cout << fmt::format("Class:     {} (ID: {})", GetClassName(e.class_id), e.class_id) << std::endl;
	std::cout << fmt::format("Gender:    {} ({})", e.gender == 0 ? "Male" : (e.gender == 1 ? "Female" : "Neutral"), e.gender) << std::endl;
	std::cout << fmt::format("Level:     {}", e.level) << std::endl;
	std::cout << fmt::format("Body Type: {} (ID: {})", GetBodyTypeName(e.bodytype), e.bodytype) << std::endl;
	std::cout << fmt::format("Size:      {:.2f}", e.size) << std::endl;
	std::cout << fmt::format("Light:     {}", e.light) << std::endl;

	// Appearance details
	std::cout << std::endl << "--- Facial Features ---" << std::endl;
	std::cout << fmt::format("Face:       {}", e.face) << std::endl;
	std::cout << fmt::format("Hair Color: {}", e.haircolor) << std::endl;
	std::cout << fmt::format("Hair Style: {}", e.hairstyle) << std::endl;
	std::cout << fmt::format("Beard:      {}", e.beard) << std::endl;
	std::cout << fmt::format("Beard Color:{}", e.beardcolor) << std::endl;

	// Equipment textures
	std::cout << std::endl << "--- Equipment Textures (Material IDs) ---" << std::endl;
	std::cout << fmt::format("Helm Texture:   {} (Show: {})", e.helm, e.showhelm ? "Yes" : "No") << std::endl;
	std::cout << fmt::format("Chest Texture2: {} (equip_chest2 / mount color)", e.equip_chest2) << std::endl;

	for (int i = 0; i < 9; i++) {
		uint32_t mat = e.equipment[i];
		uint32_t tint = e.equipment_tint[i];

		// Parse tint components (BGRA format)
		uint8_t tint_blue = (tint >> 0) & 0xFF;
		uint8_t tint_green = (tint >> 8) & 0xFF;
		uint8_t tint_red = (tint >> 16) & 0xFF;
		uint8_t tint_use = (tint >> 24) & 0xFF;

		std::string slot_name = GetEquipSlotName(i);

		if (mat != 0 || tint != 0) {
			std::cout << fmt::format("  [{:>9}] Material: {:>5}", slot_name, mat);
			if (tint != 0) {
				std::cout << fmt::format("  Tint: RGB({:>3},{:>3},{:>3}) Use: 0x{:02X}",
					tint_red, tint_green, tint_blue, tint_use);
			}
			std::cout << std::endl;
		} else {
			std::cout << fmt::format("  [{:>9}] (none)", slot_name) << std::endl;
		}
	}

	// Position
	std::cout << std::endl << "--- Position ---" << std::endl;
	std::cout << fmt::format("Location: ({:.2f}, {:.2f}, {:.2f}) Heading: {:.1f}",
		e.x, e.y, e.z, e.heading) << std::endl;
	std::cout << fmt::format("HP:       {}%", e.hp_percent) << std::endl;

	std::cout << "========================================" << std::endl;
}

void EverQuest::DumpPacket(const std::string &prefix, uint16_t opcode, const EQ::Net::Packet &p) {
	DumpPacket(prefix, opcode, (const uint8_t*)p.Data(), p.Length());
}

void EverQuest::DumpPacket(const std::string &prefix, uint16_t opcode, const uint8_t *data, size_t size) {
	if (s_debug_level < 6) return;

	LOG_TRACE(MOD_NET, "[Packet {}] [{}] [{:#06x}] Size [{}]",
		prefix, GetOpcodeName(opcode), opcode, size);

	// Hex dump
	std::stringstream ss;
	for (size_t i = 0; i < size; i += 16) {
		ss << std::setfill(' ') << std::setw(5) << i << ": ";

		// Hex bytes
		for (size_t j = 0; j < 16; j++) {
			if (i + j < size) {
				ss << std::setfill('0') << std::setw(2) << std::hex << (int)data[i + j] << " ";
			} else {
				ss << "   ";
			}
			if (j == 7) ss << "- ";
		}

		ss << " | ";

		// ASCII representation
		for (size_t j = 0; j < 16 && i + j < size; j++) {
			char c = data[i + j];
			ss << (isprint(c) ? c : '.');
		}

		if (i + 16 < size) {
			ss << "\n";
		}
	}
	LOG_TRACE(MOD_NET, "{}", ss.str());
}
