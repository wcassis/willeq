#include "client/eq.h"
#include "client/pathfinder_interface.h"
#include "client/hc_map.h"
#include "client/combat.h"
#include "client/trade_manager.h"
#include "client/world_object.h"
#include "client/spell/spell_manager.h"
#include "client/spell/buff_manager.h"
#include "client/spell/spell_effects.h"
#include "client/spell/spell_type_processor.h"
#include "client/skill/skill_manager.h"
#include "client/zone_lines.h"
#include "client/formatted_message.h"
#include "common/logging.h"
#include "common/packet_structs.h"
#include "common/name_utils.h"

#ifdef EQT_HAS_GRAPHICS
#include "client/graphics/irrlicht_renderer.h"
#include "client/graphics/ui/inventory_manager.h"
#include "client/graphics/ui/inventory_constants.h"
#include "client/graphics/ui/window_manager.h"
#include "client/graphics/ui/hotbar_window.h"
#include "client/graphics/ui/item_instance.h"
#include "client/graphics/ui/chat_message_buffer.h"
#include "client/graphics/ui/command_registry.h"
#include "client/graphics/ui/hotbar_types.h"
#include "client/graphics/ui/spell_book_window.h"
#include "common/util/json_config.h"
#endif
#include "client/animation_constants.h"
#include <array>
#include <iomanip>
#include <sstream>
#include <ctime>
#include <algorithm>
#include <vector>
#include <cmath>
#include <cstring>
#include <thread>
#include <chrono>
#include <map>
#include <tuple>
#include <fstream>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// Movement constants
const float DEFAULT_RUN_SPEED = 6.0f;     // Real EQ run speed based on good client testing
const float DEFAULT_WALK_SPEED = 3.0f;    // Normal EQ walk speed (half of run)
const float WALK_SPEED_THRESHOLD = 4.5f;  // Speed threshold for walk vs run animation
const float POSITION_UPDATE_INTERVAL_MS = 200.0f;  // Real clients update every 200ms

// Following constants
const float FOLLOW_CLOSE_DISTANCE = 10.0f;   // Stop following when this close
const float FOLLOW_FAR_DISTANCE = 30.0f;     // Full speed when this far
const float FOLLOW_MIN_SPEED_MULT = 0.5f;    // Minimum speed multiplier when close
const float FOLLOW_MAX_SPEED_MULT = 1.5f;    // Maximum speed multiplier when far

// Static member definition
int EverQuest::s_debug_level = 0;

const char* eqcrypt_block(const char *buffer_in, size_t buffer_in_sz, char* buffer_out, bool enc) {
	DES_key_schedule k;
	DES_cblock v;

	memset(&k, 0, sizeof(DES_key_schedule));
	memset(&v, 0, sizeof(DES_cblock));

	if (!enc && buffer_in_sz && buffer_in_sz % 8 != 0) {
		return nullptr;
	}

	DES_ncbc_encrypt((const unsigned char*)buffer_in, (unsigned char*)buffer_out, (long)buffer_in_sz, &k, &v, enc);
	return buffer_out;
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
	// Common string messages from string_ids.h
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
	if (s_debug_level < 3) return;

	std::cout << fmt::format("[Packet {}] [{}] [{:#06x}] Size [{}]",  
		prefix, GetOpcodeName(opcode), opcode, size) << std::endl;

	if (s_debug_level >= 3) {
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
		std::cout << ss.str() << std::endl;
	}
}

EverQuest::EverQuest(const std::string &host, int port, const std::string &user, const std::string &pass, const std::string &server, const std::string &character)
{
	m_host = host;
	m_port = port;
	m_user = user;
	m_pass = pass;
	m_server = server;
	m_character = character;
	m_dbid = 0;
	
	// Initialize combat manager
	m_combat_manager = std::make_unique<CombatManager>(this);

	// Initialize trade manager
	m_trade_manager = std::make_unique<TradeManager>();
	SetupTradeManagerCallbacks();

	// Initialize spell manager
	m_spell_manager = std::make_unique<EQ::SpellManager>(this);

	// Initialize skill manager
	m_skill_manager = std::make_unique<EQ::SkillManager>(this);

	// Direct connection without DNS lookup (assume host is already IP or will be resolved by OS)
	m_login_connection_manager.reset(new EQ::Net::DaybreakConnectionManager());

	m_login_connection_manager->OnNewConnection(std::bind(&EverQuest::LoginOnNewConnection, this, std::placeholders::_1));
	m_login_connection_manager->OnConnectionStateChange(std::bind(&EverQuest::LoginOnStatusChangeReconnectEnabled, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3));
	m_login_connection_manager->OnPacketRecv(std::bind(&EverQuest::LoginOnPacketRecv, this, std::placeholders::_1, std::placeholders::_2));

	m_login_connection_manager->Connect(m_host, m_port);
}

EverQuest::~EverQuest()
{
	// Stop the update loop before destroying
	StopUpdateLoop();
	
	// Clean up connections properly to avoid pure virtual method calls
	if (m_zone_connection) {
		m_zone_connection.reset();
	}
	if (m_zone_connection_manager) {
		m_zone_connection_manager.reset();
	}
	if (m_login_connection) {
		m_login_connection.reset();
	}
	if (m_login_connection_manager) {
		m_login_connection_manager.reset();
	}
	
	// Clear movement history
	m_movement_history.clear();
}

void EverQuest::LoginOnNewConnection(std::shared_ptr<EQ::Net::DaybreakConnection> connection)
{
	m_login_connection = connection;
	LOG_INFO(MOD_WORLD, "Connecting...");
}

void EverQuest::LoginOnStatusChangeReconnectEnabled(std::shared_ptr<EQ::Net::DaybreakConnection> conn, EQ::Net::DbProtocolStatus from, EQ::Net::DbProtocolStatus to)
{
	if (to == EQ::Net::StatusConnected) {
		LOG_INFO(MOD_WORLD, "Login connected.");
#ifdef EQT_HAS_GRAPHICS
		if (m_renderer) {
			m_renderer->setLoadingProgress(0.02f, L"Login server connected...");
		}
#endif
		LoginSendSessionReady();
	}

	if (to == EQ::Net::StatusDisconnected) {
		LOG_INFO(MOD_WORLD, "Login connection lost before we got to world, reconnecting.");
		m_key.clear();
		m_dbid = 0;
		m_login_connection.reset();
		m_login_connection_manager->Connect(m_host, m_port);
	}
}

void EverQuest::LoginOnStatusChangeReconnectDisabled(std::shared_ptr<EQ::Net::DaybreakConnection> conn, EQ::Net::DbProtocolStatus from, EQ::Net::DbProtocolStatus to)
{
	if (to == EQ::Net::StatusDisconnected) {
		m_login_connection.reset();
	}
}

void EverQuest::LoginOnPacketRecv(std::shared_ptr<EQ::Net::DaybreakConnection> conn, const EQ::Net::Packet & p)
{
	auto opcode = p.GetUInt16(0);
	DumpPacket("S->C", opcode, p);
	
	switch (opcode) {
	case HC_OP_ChatMessage:
		if (s_debug_level >= 1) {
			std::cout << "Received HC_OP_ChatMessage, sending login" << std::endl;
		}
		LoginSendLogin();
		break;
	case HC_OP_LoginAccepted:
		LoginProcessLoginResponse(p);
		break;
	case HC_OP_ServerListResponse:
		LoginProcessServerPacketList(p);
		break;
	case HC_OP_PlayEverquestResponse:
		LoginProcessServerPlayResponse(p);
		break;
	default:
		if (s_debug_level >= 1) {
			std::cout << fmt::format("Unhandled login opcode: {:#06x}",  opcode) << std::endl;
		}
		break;
	}
}

void EverQuest::LoginSendSessionReady()
{
	EQ::Net::DynamicPacket p;
	p.Resize(14);
	p.PutUInt16(0, HC_OP_SessionReady);
	p.PutUInt32(2, m_login_sequence++);  // sequence
	p.PutUInt32(6, 0);  // unknown
	p.PutUInt32(10, 2048);  // max length
	
	DumpPacket("C->S", HC_OP_SessionReady, p);
	m_login_connection->QueuePacket(p);
}

void EverQuest::LoginSendLogin()
{
#ifdef EQT_HAS_GRAPHICS
	if (m_renderer) {
		m_renderer->setLoadingProgress(0.05f, L"Authenticating...");
	}
#endif
	size_t buffer_len = m_user.length() + m_pass.length() + 2;
	std::unique_ptr<char[]> buffer(new char[buffer_len]);
	memset(buffer.get(), 0, buffer_len);

	strcpy(&buffer[0], m_user.c_str());
	strcpy(&buffer[m_user.length() + 1], m_pass.c_str());

	size_t encrypted_len = buffer_len;

	if (encrypted_len % 8 > 0) {
		encrypted_len = ((encrypted_len / 8) + 1) * 8;
	}

	EQ::Net::DynamicPacket p;
	p.Resize(12 + encrypted_len);
	p.PutUInt16(0, HC_OP_Login);
	p.PutUInt32(2, m_login_sequence++);  // sequence
	p.PutUInt32(6, 0x00020000);  // unknown - matches good log
	
	// Clear the encrypted area first
	memset((char*)p.Data() + 12, 0, encrypted_len);
	eqcrypt_block(&buffer[0], buffer_len, (char*)p.Data() + 12, true);

	DumpPacket("C->S", HC_OP_Login, p);
	m_login_connection->QueuePacket(p);
}

void EverQuest::LoginSendServerRequest()
{
	EQ::Net::DynamicPacket p;
	p.Resize(12);
	p.PutUInt16(0, HC_OP_ServerListRequest);
	p.PutUInt32(2, m_login_sequence++);  // sequence
	p.PutUInt32(6, 0);  // unknown
	p.PutUInt16(10, 0);  // unknown
	
	DumpPacket("C->S", HC_OP_ServerListRequest, p);
	m_login_connection->QueuePacket(p);
}

void EverQuest::LoginSendPlayRequest(uint32_t id)
{
	EQ::Net::DynamicPacket p;
	p.Resize(16);
	p.PutUInt16(0, HC_OP_PlayEverquestRequest);
	p.PutUInt32(2, m_login_sequence++);  // sequence
	p.PutUInt32(6, 0);  // unknown
	p.PutUInt16(10, 0);  // unknown
	p.PutUInt32(12, id);  // server id
	
	DumpPacket("C->S", HC_OP_PlayEverquestRequest, p);
	m_login_connection->QueuePacket(p);
}

void EverQuest::LoginProcessLoginResponse(const EQ::Net::Packet & p)
{
	auto encrypt_size = p.Length() - 12;
	if (encrypt_size % 8 > 0) {
		encrypt_size = (encrypt_size / 8) * 8;
	}

	std::unique_ptr<char[]> decrypted(new char[encrypt_size]);

	eqcrypt_block((char*)p.Data() + 12, encrypt_size, &decrypted[0], false);

	EQ::Net::StaticPacket sp(&decrypted[0], encrypt_size);
	auto response_error = sp.GetUInt16(1);

	if (response_error > 101) {
		LOG_ERROR(MOD_WORLD, "Error logging in response code: {}", response_error);
		LoginDisableReconnect();
	}
	else {
		m_key = sp.GetCString(12);
		m_dbid = sp.GetUInt32(8);

		LOG_INFO(MOD_WORLD, "Logged in successfully with dbid {} and key {}", m_dbid, m_key);
#ifdef EQT_HAS_GRAPHICS
		if (m_renderer) {
			m_renderer->setLoadingProgress(0.08f, L"Login successful, requesting server list...");
		}
#endif
		LoginSendServerRequest();
	}
}

void EverQuest::LoginProcessServerPacketList(const EQ::Net::Packet & p)
{
	m_world_servers.clear();
	auto number_of_servers = p.GetUInt32(18);
	size_t idx = 22;

	for (auto i = 0U; i < number_of_servers; ++i) {
		WorldServer ws;
		ws.address = p.GetCString(idx);
		idx += (ws.address.length() + 1);

		ws.type = p.GetInt32(idx);
		idx += 4;

		auto id = p.GetUInt32(idx);
		idx += 4;

		ws.long_name = p.GetCString(idx);
		idx += (ws.long_name.length() + 1);

		ws.lang = p.GetCString(idx);
		idx += (ws.lang.length() + 1);

		ws.region = p.GetCString(idx);
		idx += (ws.region.length() + 1);

		ws.status = p.GetInt32(idx);
		idx += 4;

		ws.players = p.GetInt32(idx);
		idx += 4;

		m_world_servers[id] = ws;
	}

	for (auto server : m_world_servers) {
		if (server.second.long_name.compare(m_server) == 0) {
			LOG_INFO(MOD_WORLD, "Found world server {}, attempting to login.", m_server);
#ifdef EQT_HAS_GRAPHICS
			if (m_renderer) {
				std::wstring serverName(server.second.long_name.begin(), server.second.long_name.end());
				m_renderer->setLoadingProgress(0.12f, L"Selecting server: " + serverName);
			}
#endif
			LoginSendPlayRequest(server.first);
			return;
		}
	}

	LOG_ERROR(MOD_WORLD, "Got response from login server but could not find world server {} disconnecting.", m_server);
	LoginDisableReconnect();
}

void EverQuest::LoginProcessServerPlayResponse(const EQ::Net::Packet &p)
{
	auto allowed = p.GetUInt8(12);

	LOG_DEBUG(MOD_WORLD, "PlayEverquestResponse: allowed={}, server_id={}",
		allowed, p.GetUInt32(18));

	if (allowed) {
		auto server = p.GetUInt32(18);
		auto ws = m_world_servers.find(server);
		if (ws != m_world_servers.end()) {
			LOG_INFO(MOD_WORLD, "Connecting to world server {} at {}:9000",
				ws->second.long_name, ws->second.address);
#ifdef EQT_HAS_GRAPHICS
			if (m_renderer) {
				m_renderer->setLoadingProgress(0.18f, L"Connecting to world server...");
			}
#endif
			ConnectToWorld(ws->second.address);
			LoginDisableReconnect();
		} else {
			LOG_WARN(MOD_WORLD, "Server ID {} not found in world servers list", server);
		}
	}
	else {
		auto message = p.GetUInt16(13);
		LOG_ERROR(MOD_WORLD, "Failed to login to server with message {}", message);
		LoginDisableReconnect();
	}
}

void EverQuest::LoginDisableReconnect()
{
	m_login_connection_manager->OnConnectionStateChange(std::bind(&EverQuest::LoginOnStatusChangeReconnectDisabled, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3));
	m_login_connection->Close();
}

void EverQuest::ConnectToWorld(const std::string& world_address)
{
	LOG_DEBUG(MOD_WORLD, "Creating new world connection manager for {}:9000", world_address);

	// Store the world server address for reconnection
	m_world_server_host = world_address;

	m_world_connection_manager.reset(new EQ::Net::DaybreakConnectionManager());
	m_world_connection_manager->OnNewConnection(std::bind(&EverQuest::WorldOnNewConnection, this, std::placeholders::_1));
	m_world_connection_manager->OnConnectionStateChange(std::bind(&EverQuest::WorldOnStatusChangeReconnectEnabled, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3));
	m_world_connection_manager->OnPacketRecv(std::bind(&EverQuest::WorldOnPacketRecv, this, std::placeholders::_1, std::placeholders::_2));
	m_world_connection_manager->Connect(world_address, 9000);
}

void EverQuest::WorldOnNewConnection(std::shared_ptr<EQ::Net::DaybreakConnection> connection)
{
	m_world_connection = connection;
	LOG_DEBUG(MOD_WORLD, "World connection created: {}", connection ? "valid" : "null");
}

void EverQuest::WorldOnStatusChangeReconnectEnabled(std::shared_ptr<EQ::Net::DaybreakConnection> conn, EQ::Net::DbProtocolStatus from, EQ::Net::DbProtocolStatus to)
{
	LOG_TRACE(MOD_WORLD, "WorldOnStatusChangeReconnectEnabled: from={} to={}", static_cast<int>(from), static_cast<int>(to));

	if (to == EQ::Net::StatusConnected) {
		LOG_DEBUG(MOD_WORLD, "World connected, sending client auth");
#ifdef EQT_HAS_GRAPHICS
		if (m_renderer) {
			m_renderer->setLoadingTitle(L"Connecting...");
			m_renderer->setLoadingProgress(0.25f, L"Authenticating with world server...");
		}
#endif
		// Send login info immediately
		WorldSendClientAuth();
		LOG_DEBUG(MOD_WORLD, "Client auth sent");
	}

	if (to == EQ::Net::StatusDisconnected) {
		LOG_DEBUG(MOD_WORLD, "World connection lost");
		m_world_connection.reset();

		// Reconnect to world if we're in a zone (needed for zone transfers)
		if (m_zone_connected && !m_world_server_host.empty()) {
			LOG_DEBUG(MOD_WORLD, "Reconnecting to world server for zone transfer support");
			m_world_connection_manager->Connect(m_world_server_host, 9000);
		}
	}

	if (to == EQ::Net::StatusConnecting) {
		LOG_TRACE(MOD_WORLD, "World connection status: Connecting");
	}

	if (to == EQ::Net::StatusDisconnecting) {
		LOG_TRACE(MOD_WORLD, "World connection status: Disconnecting");
	}
}

void EverQuest::WorldOnStatusChangeReconnectDisabled(std::shared_ptr<EQ::Net::DaybreakConnection> conn, EQ::Net::DbProtocolStatus from, EQ::Net::DbProtocolStatus to)
{
	if (to == EQ::Net::StatusDisconnected) {
		m_world_connection.reset();
	}
}

void EverQuest::WorldOnPacketRecv(std::shared_ptr<EQ::Net::DaybreakConnection> conn, const EQ::Net::Packet & p)
{
	LOG_TRACE(MOD_WORLD, "WorldOnPacketRecv called");
	auto opcode = p.GetUInt16(0);
	DumpPacket("S->C", opcode, p);
	
	switch (opcode) {
	case HC_OP_ChatMessage:
		LOG_DEBUG(MOD_WORLD, "Received HC_OP_ChatMessage, sending login info");
		WorldSendClientAuth();
		break;
	case HC_OP_SessionReady:
		LOG_DEBUG(MOD_WORLD, "Received HC_OP_SessionReady");
		// Server is ready, now send login info
		WorldSendClientAuth();
		break;
	case HC_OP_GuildsList:
		WorldProcessGuildsList(p);
		break;
	case HC_OP_LogServer:
		WorldProcessLogServer(p);
		break;
	case HC_OP_ApproveWorld:
		WorldProcessApproveWorld(p);
		break;
	case HC_OP_EnterWorld:
		WorldProcessEnterWorld(p);
		break;
	case HC_OP_PostEnterWorld:
		WorldProcessPostEnterWorld(p);
		break;
	case HC_OP_ExpansionInfo:
		WorldProcessExpansionInfo(p);
		break;
	case HC_OP_SendCharInfo:
		WorldProcessCharacterSelect(p);
		break;
	case HC_OP_MOTD:
		WorldProcessMOTD(p);
		break;
	case HC_OP_SetChatServer:
	case HC_OP_SetChatServer2:
		WorldProcessSetChatServer(p);
		break;
	case HC_OP_ZoneServerInfo:
		WorldProcessZoneServerInfo(p);
		break;
	default:
		if (s_debug_level >= 1) {
			std::cout << fmt::format("Unhandled world opcode: {}",  GetOpcodeName(opcode)) << std::endl;
		}
		break;
	}
}

void EverQuest::WorldSendSessionReady()
{
	// Don't send SessionReady - world server doesn't expect it
	// The issue must be something else
	return;
}

void EverQuest::WorldSendClientAuth()
{
	EQ::Net::DynamicPacket p;
	p.Resize(466);  // 2 bytes opcode + 464 bytes LoginInfo struct

	p.PutUInt16(0, HC_OP_SendLoginInfo);
	
	// Clear the entire packet
	memset((char*)p.Data() + 2, 0, 464);
	
	// login_info field starts at offset 2 (after opcode)
	char* login_info = (char*)p.Data() + 2;
	
	// First put the account ID (converted from dbid)
	std::string dbid_str = std::to_string(m_dbid);
	size_t dbid_len = dbid_str.length();
	if (dbid_len > 18) dbid_len = 18;
	memcpy(login_info, dbid_str.c_str(), dbid_len);
	
	// Add null terminator after account ID
	login_info[dbid_len] = 0;
	
	// Copy session key after the null terminator
	size_t key_len = m_key.length();
	if (key_len > 15) key_len = 15;
	memcpy(login_info + dbid_len + 1, m_key.c_str(), key_len);

	// Set zoning flag at offset 188 of LoginInfo
	// If we're already in a zone (reconnecting for zone-to-zone), set zoning = 1
	// Otherwise, set zoning = 0 (fresh login from character select)
	uint8_t zoning_flag = (m_zone_connected || m_zone_change_requested) ? 1 : 0;
	p.PutUInt8(2 + 188, zoning_flag);

	LOG_INFO(MOD_WORLD, "Sending login info: dbid={}, key={}, zoning={}", dbid_str, m_key, zoning_flag);

	DumpPacket("C->S", HC_OP_SendLoginInfo, p);
	m_world_connection->QueuePacket(p);
}

void EverQuest::WorldSendEnterWorld(const std::string &character)
{
#ifdef EQT_HAS_GRAPHICS
	if (m_renderer) {
		m_renderer->setLoadingProgress(0.28f, L"Entering world...");
	}
#endif
	EQ::Net::DynamicPacket p;
	p.Resize(74);  // Matching good log size

	p.PutUInt16(0, HC_OP_EnterWorld);
	
	// Character name at offset 2, null-terminated, 64 bytes
	size_t name_len = character.length();
	if (name_len > 63) name_len = 63;
	memcpy((char*)p.Data() + 2, character.c_str(), name_len);
	p.PutUInt8(2 + name_len, 0);  // null terminator
	
	// Fill rest with zeros
	for (size_t i = 2 + name_len + 1; i < 66; i++) {
		p.PutUInt8(i, 0);
	}
	
	p.PutUInt32(66, 0);  // unknown
	p.PutUInt32(70, 0);  // unknown

	DumpPacket("C->S", HC_OP_EnterWorld, p);
	m_world_connection->QueuePacket(p);
	m_enter_world_sent = true;
}

void EverQuest::WorldProcessCharacterSelect(const EQ::Net::Packet &p)
{
	// For Titanium client, the packet is just the raw CharacterSelect_Struct
	// Let's verify packet size
	if (p.Length() < 1706) {
		std::cout << fmt::format("[ERROR] Character select packet too small: {} bytes", p.Length()) << std::endl;
		return;
	}
	
	// Debug: Let's check names array at offset 1024 + 2 (for opcode)
	LOG_DEBUG(MOD_MAIN, "Checking character names in Titanium format:");
	
	// Titanium CharacterSelect_Struct has names at offset 1024
	const size_t names_offset = 1024 + 2; // +2 for opcode
	
	// Check all 10 character slots
	for (int i = 0; i < 10; i++) {
		size_t name_offset = names_offset + (i * 64);
		
		// Get character name
		char name_buf[65] = {0};
		for (int j = 0; j < 64 && name_offset + j < p.Length(); j++) {
			name_buf[j] = p.GetUInt8(name_offset + j);
			if (name_buf[j] == 0) break;
		}
		std::string name(name_buf);
		
		if (name.length() > 0) {
			// Get other character data using Titanium offsets
			uint8_t level = p.GetUInt8(1694 + 2 + i);  // Level array at offset 1694
			uint8_t pclass = p.GetUInt8(1004 + 2 + i); // Class array at offset 1004
			uint32_t race = p.GetUInt32(0 + 2 + (i * 4)); // Race array at offset 0
			uint32_t zone = p.GetUInt32(964 + 2 + (i * 4)); // Zone array at offset 964
			
			LOG_DEBUG(MOD_MAIN, "Character {}: name='{}', level={}, class={}, race={}, zone={}", 
				i, name, level, pclass, race, zone);
			
			if (m_character.compare(name) == 0) {
				LOG_DEBUG(MOD_MAIN, "Found our character '{}' at index {}", m_character, i);
#ifdef EQT_HAS_GRAPHICS
				if (m_renderer) {
					std::wstring charName(m_character.begin(), m_character.end());
					m_renderer->setLoadingProgress(0.32f, L"Character found: " + charName);
				}
#endif
				m_character_select_index = i;
				return;
			}
		}
	}

	std::cout << fmt::format("Could not find {}, cannot continue to login.",  m_character) << std::endl;
}

void EverQuest::WorldSendApproveWorld()
{
	EQ::Net::DynamicPacket p;
	p.Resize(274);  // Matching good log response size
	
	p.PutUInt16(0, HC_OP_ApproveWorld);
	// The rest is character-specific data which we'll leave as zeros for now
	
	DumpPacket("C->S", HC_OP_ApproveWorld, p);
	m_world_connection->QueuePacket(p);
}

void EverQuest::WorldSendWorldClientCRC()
{
	EQ::Net::DynamicPacket p;
	p.Resize(2058);  // Matching good log size
	
	// Send CRC1
	p.PutUInt16(0, HC_OP_World_Client_CRC1);
	// Fill with dummy CRC data
	for (size_t i = 2; i < 2058; i++) {
		p.PutUInt8(i, 0);
	}
	
	DumpPacket("C->S", HC_OP_World_Client_CRC1, p);
	m_world_connection->QueuePacket(p);
	
	// Send CRC2
	p.PutUInt16(0, HC_OP_World_Client_CRC2);
	DumpPacket("C->S", HC_OP_World_Client_CRC2, p);
	m_world_connection->QueuePacket(p);
}

void EverQuest::WorldSendWorldClientReady()
{
	EQ::Net::DynamicPacket p;
	p.Resize(2);
	
	p.PutUInt16(0, HC_OP_WorldClientReady);
	
	DumpPacket("C->S", HC_OP_WorldClientReady, p);
	m_world_connection->QueuePacket(p);
	m_world_ready = true;
}

void EverQuest::WorldSendWorldComplete()
{
	EQ::Net::DynamicPacket p;
	p.Resize(2);
	
	p.PutUInt16(0, HC_OP_WorldComplete);
	
	DumpPacket("C->S", HC_OP_WorldComplete, p);
	m_world_connection->QueuePacket(p);
}

void EverQuest::WorldProcessGuildsList(const EQ::Net::Packet &p)
{
	LOG_DEBUG(MOD_WORLD, "Received guilds list");
}

void EverQuest::WorldProcessLogServer(const EQ::Net::Packet &p)
{
	LOG_DEBUG(MOD_WORLD, "Received log server info");
}

void EverQuest::WorldProcessApproveWorld(const EQ::Net::Packet &p)
{
	LOG_DEBUG(MOD_WORLD, "World approved, sending response");
	WorldSendApproveWorld();
	
	// After approval, send CRC packets
	WorldSendWorldClientCRC();
}

void EverQuest::WorldProcessEnterWorld(const EQ::Net::Packet &p)
{
	LOG_DEBUG(MOD_WORLD, "Server acknowledged enter world");
}

void EverQuest::WorldProcessPostEnterWorld(const EQ::Net::Packet &p)
{
	LOG_DEBUG(MOD_WORLD, "Post enter world received");
}

void EverQuest::WorldProcessExpansionInfo(const EQ::Net::Packet &p)
{
	uint32_t expansions = p.GetUInt32(2);
	LOG_DEBUG(MOD_WORLD, "Expansion info: {:#x}", expansions);
	
	// After receiving initial packets, send WorldClientReady
	if (!m_world_ready) {
		// Send ACK packet first
		EQ::Net::DynamicPacket ack;
		ack.Resize(6);
		ack.PutUInt16(0, HC_OP_AckPacket);
		ack.PutUInt32(2, 0);  // sequence or something
		DumpPacket("C->S", HC_OP_AckPacket, ack);
		m_world_connection->QueuePacket(ack);
		
		// Then send WorldClientReady
		WorldSendWorldClientReady();
		
		// Now send EnterWorld
		if (!m_enter_world_sent) {
			WorldSendEnterWorld(m_character);
		}
	}
}

void EverQuest::WorldProcessMOTD(const EQ::Net::Packet &p)
{
	LOG_DEBUG(MOD_WORLD, "Received MOTD");
}

void EverQuest::WorldProcessSetChatServer(const EQ::Net::Packet &p)
{
	// Parse the chat server info from world server
	// Format: "host,port,server.character,connection_type,mail_key"
	std::string chat_info = p.GetCString(2);
	
	if (s_debug_level >= 2) {
		LOG_DEBUG(MOD_NET, "Received chat server info: {}", chat_info);
	}
	
	// Parse the comma-separated values
	std::vector<std::string> parts;
	std::stringstream ss(chat_info);
	std::string part;
	while (std::getline(ss, part, ',')) {
		parts.push_back(part);
	}
	
	if (parts.size() >= 5) {
		m_ucs_host = parts[0];
		m_ucs_port = std::stoi(parts[1]);
		// parts[2] is server.character
		// parts[3] is connection type (we'll use for authentication)
		m_mail_key = parts[4];

		LOG_DEBUG(MOD_WORLD, "UCS connection info: {}:{}, mail_key: {}",
			m_ucs_host, m_ucs_port, m_mail_key);

		// Connect to UCS server
		// TODO: Fix UCS implementation to use correct EQStream API
		// ConnectToUCS(m_ucs_host, m_ucs_port);
	} else {
		LOG_WARN(MOD_WORLD, "Invalid chat server info format");
	}
}

void EverQuest::WorldProcessZoneServerInfo(const EQ::Net::Packet &p)
{
	LOG_DEBUG(MOD_WORLD, "Received ZoneServerInfo packet");

#ifdef EQT_HAS_GRAPHICS
	if (m_renderer) {
		m_renderer->setLoadingTitle(L"Loading Zone...");
		m_renderer->setLoadingProgress(0.38f, L"Received zone server info...");
	}
#endif

	// Extract zone server info
	std::string new_zone_host = p.GetCString(2);
	uint16_t new_zone_port = p.GetUInt16(130);

	LOG_DEBUG(MOD_ZONE, "Zone server info received: {}:{}", new_zone_host, new_zone_port);

	// Check if this is a zone transition (we're already connected to a zone)
	bool is_zone_transition = m_zone_connected || m_zone_connection != nullptr;

	if (is_zone_transition) {
		LOG_DEBUG(MOD_ZONE, "Zone transition detected - disconnecting from current zone");

		// Set player position to pending zone coordinates if we have them
		if (m_pending_zone_id != 0) {
			LOG_DEBUG(MOD_ZONE, "Setting spawn position to ({:.1f}, {:.1f}, {:.1f})",
				m_pending_zone_x, m_pending_zone_y, m_pending_zone_z);
			m_x = m_pending_zone_x;
			m_y = m_pending_zone_y;
			m_z = m_pending_zone_z;
			if (m_pending_zone_heading > 0) {
				m_heading = m_pending_zone_heading;
			}
		}

		// Disconnect from current zone (this also calls CleanupZone)
		DisconnectFromZone();
	}

	// Store new zone server info
	m_zone_server_host = new_zone_host;
	m_zone_server_port = new_zone_port;

	// Clear pending zone info
	m_pending_zone_id = 0;
	m_pending_zone_x = 0.0f;
	m_pending_zone_y = 0.0f;
	m_pending_zone_z = 0.0f;
	m_pending_zone_heading = 0.0f;

	// Send WorldComplete to acknowledge
	WorldSendWorldComplete();

	// Connect to the zone server
	ConnectToZone();
}

// Zone server connection functions
void EverQuest::ConnectToZone()
{
	LOG_DEBUG(MOD_ZONE, "Connecting to zone server at {}:{}", m_zone_server_host, m_zone_server_port);

#ifdef EQT_HAS_GRAPHICS
	if (m_renderer) {
		m_renderer->setLoadingProgress(0.48f, L"Connecting to zone server...");
	}
#endif

	// Initialize inventory manager before zone connection so CharInventory packet can be processed
	// This must exist before packets arrive, independent of graphics initialization
	if (!m_inventory_manager) {
		m_inventory_manager = std::make_unique<eqt::inventory::InventoryManager>();
		SetupInventoryCallbacks();
		LOG_DEBUG(MOD_INVENTORY, "Inventory manager initialized");
	}

	// Initialize spell manager and load spell database before zone packets arrive
	// This is needed so buff info in PlayerProfile can be processed
	if (m_spell_manager && !m_spell_manager->isInitialized()) {
		if (m_spell_manager->initialize(m_eq_client_path)) {
			LOG_DEBUG(MOD_SPELL, "Spell database loaded");
		} else {
			LOG_WARN(MOD_SPELL, "Could not load spell database - spell system will be limited");
		}
	}

	// Initialize buff manager with spell database (needed for PlayerProfile buff processing)
	if (m_spell_manager && m_spell_manager->isInitialized() && !m_buff_manager) {
		m_buff_manager = std::make_unique<EQ::BuffManager>(&m_spell_manager->getDatabase());
		LOG_DEBUG(MOD_SPELL, "Buff manager initialized");
	}

	m_zone_connection_manager.reset(new EQ::Net::DaybreakConnectionManager());
	m_zone_connection_manager->OnNewConnection(std::bind(&EverQuest::ZoneOnNewConnection, this, std::placeholders::_1));
	m_zone_connection_manager->OnConnectionStateChange(std::bind(&EverQuest::ZoneOnStatusChangeReconnectEnabled, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3));
	m_zone_connection_manager->OnPacketRecv(std::bind(&EverQuest::ZoneOnPacketRecv, this, std::placeholders::_1, std::placeholders::_2));
	m_zone_connection_manager->Connect(m_zone_server_host, m_zone_server_port);
}

void EverQuest::ZoneOnNewConnection(std::shared_ptr<EQ::Net::DaybreakConnection> connection)
{
	m_zone_connection = connection;
	LOG_DEBUG(MOD_ZONE, "Zone connection created");
}

void EverQuest::ZoneOnStatusChangeReconnectEnabled(std::shared_ptr<EQ::Net::DaybreakConnection> conn, EQ::Net::DbProtocolStatus from, EQ::Net::DbProtocolStatus to)
{
	if (to == EQ::Net::StatusConnected) {
		LOG_DEBUG(MOD_ZONE, "Zone connected");
		m_zone_connected = true;
#ifdef EQT_HAS_GRAPHICS
		if (m_renderer) {
			m_renderer->setLoadingProgress(0.58f, L"Zone connected, requesting entry...");
		}
#endif
		// Send stream identify immediately after connection
		ZoneSendStreamIdentify();
		m_zone_session_established = true;

		// Send ack packet and zone entry immediately after session established
		ZoneSendAckPacket();
		ZoneSendZoneEntry();
	}

	if (to == EQ::Net::StatusDisconnected) {
		LOG_INFO(MOD_ZONE, "Zone connection lost, reconnecting.");
		m_zone_connected = false;
		m_zone_session_established = false;
		m_zone_entry_sent = false;
		// m_client_spawned = false; // Deprecated
		m_zone_connection.reset();
		m_zone_connection_manager->Connect(m_zone_server_host, m_zone_server_port);
	}
}

void EverQuest::ZoneOnStatusChangeReconnectDisabled(std::shared_ptr<EQ::Net::DaybreakConnection> conn, EQ::Net::DbProtocolStatus from, EQ::Net::DbProtocolStatus to)
{
	if (to == EQ::Net::StatusDisconnected) {
		m_zone_connection.reset();
	}
}

void EverQuest::ZoneOnPacketRecv(std::shared_ptr<EQ::Net::DaybreakConnection> conn, const EQ::Net::Packet & p)
{
	auto opcode = p.GetUInt16(0);

	LOG_TRACE(MOD_ZONE, "[ZONE RECV] opcode=0x{:04x} len={}", opcode, p.Length());

	DumpPacket("S->C", opcode, p);

	switch (opcode) {
	case HC_OP_SessionReady:
		LOG_DEBUG(MOD_ZONE, "Zone session established, sending ack and zone entry");
		// Send ack packet
		ZoneSendAckPacket();
		// Send zone entry
		ZoneSendZoneEntry();
		break;
	case HC_OP_PlayerProfile:
		ZoneProcessPlayerProfile(p);
		break;
	case HC_OP_ZoneEntry:
		// Server echoes our zone entry back with ServerZoneEntry_Struct
		LOG_DEBUG(MOD_MAIN, "Zone entry response, size: {}", p.Length());
		
		// ServerZoneEntry_Struct contains NewSpawn_Struct which contains Spawn_Struct
		// Let's check for spawn ID at various offsets
		if (p.Length() > 10) {
			// Try to find spawn ID - it might be early in the spawn struct
			LOG_DEBUG(MOD_MAIN, "Potential spawn IDs in ZoneEntry response:");
			LOG_DEBUG(MOD_MAIN, "  uint16 at offset 2: {}", p.GetUInt16(2));
			LOG_DEBUG(MOD_MAIN, "  uint16 at offset 4: {}", p.GetUInt16(4));
			LOG_DEBUG(MOD_MAIN, "  uint32 at offset 2: {}", p.GetUInt32(2));
			LOG_DEBUG(MOD_MAIN, "  uint32 at offset 6: {}", p.GetUInt32(6));
			
			// The spawn struct has name at offset 7, let's check before that
			if (p.Length() > 71) {
				auto spawn_name = p.GetCString(9); // offset 2 + 7
				LOG_DEBUG(MOD_MAIN, "  Spawn name at offset 7: '{}'", spawn_name);
			}
		}
		break;
	case HC_OP_ZoneSpawns:
		ZoneProcessZoneSpawns(p);
		break;
	case HC_OP_TimeOfDay:
		ZoneProcessTimeOfDay(p);
		break;
	case HC_OP_TributeUpdate:
		ZoneProcessTributeUpdate(p);
		break;
	case HC_OP_TributeTimer:
		ZoneProcessTributeTimer(p);
		break;
	case HC_OP_CharInventory:
		ZoneProcessCharInventory(p);
		break;
	case HC_OP_Weather:
		ZoneProcessWeather(p);
		break;
	case HC_OP_NewZone:
		ZoneProcessNewZone(p);
		break;
	case HC_OP_SendAATable:
		ZoneProcessSendAATable(p);
		break;
	case HC_OP_RespondAA:
		ZoneProcessRespondAA(p);
		break;
	case HC_OP_TributeInfo:
		ZoneProcessTributeInfo(p);
		break;
	case HC_OP_SendGuildTributes:
		ZoneProcessSendGuildTributes(p);
		break;
	case HC_OP_SpawnDoor:
		ZoneProcessSpawnDoor(p);
		break;
	case HC_OP_GroundSpawn:
		ZoneProcessGroundSpawn(p);
		break;
	case HC_OP_ClickObjectAction:
		ZoneProcessClickObjectAction(p);
		break;
	case HC_OP_TradeSkillCombine:
		ZoneProcessTradeSkillCombine(p);
		break;
	case HC_OP_SendZonepoints:
		ZoneProcessSendZonepoints(p);
		break;
	case HC_OP_SendAAStats:
		ZoneProcessSendAAStats(p);
		break;
	case HC_OP_SendExpZonein:
		ZoneProcessSendExpZonein(p);
		break;
	case HC_OP_WorldObjectsSent:
		ZoneProcessWorldObjectsSent(p);
		break;
	case HC_OP_SpawnAppearance:
		ZoneProcessSpawnAppearance(p);
		break;
	case HC_OP_ExpUpdate:
		ZoneProcessExpUpdate(p);
		break;
	case HC_OP_RaidUpdate:
		ZoneProcessRaidUpdate(p);
		break;
	case HC_OP_GuildMOTD:
		ZoneProcessGuildMOTD(p);
		break;
	case HC_OP_NewSpawn:
		ZoneProcessNewSpawn(p);
		break;
	case HC_OP_ClientUpdate:
		ZoneProcessClientUpdate(p);
		break;
	case HC_OP_DeleteSpawn:
		ZoneProcessDeleteSpawn(p);
		break;
	case HC_OP_MobHealth:
		ZoneProcessMobHealth(p);
		break;
	case HC_OP_HPUpdate:
		ZoneProcessHPUpdate(p);
		break;
	case HC_OP_ChannelMessage:
		ZoneProcessChannelMessage(p);
		break;
	case HC_OP_WearChange:
		ZoneProcessWearChange(p);
		break;
	case HC_OP_Illusion:
		ZoneProcessIllusion(p);
		break;
	case HC_OP_MoveDoor:
		ZoneProcessMoveDoor(p);
		break;
	case HC_OP_CompletedTasks:
		ZoneProcessCompletedTasks(p);
		break;
	case HC_OP_DzCompass:
		ZoneProcessDzCompass(p);
		break;
	case HC_OP_DzExpeditionLockoutTimers:
		ZoneProcessDzExpeditionLockoutTimers(p);
		break;
	case HC_OP_BeginCast:
		ZoneProcessBeginCast(p);
		break;
	case HC_OP_ManaChange:
		ZoneProcessManaChange(p);
		break;
	case HC_OP_Buff:
		ZoneProcessBuff(p);
		break;
	case HC_OP_ColoredText:
		ZoneProcessColoredText(p);
		break;
	case HC_OP_FormattedMessage:
		ZoneProcessFormattedMessage(p);
		break;
	case HC_OP_PlayerStateAdd:
		ZoneProcessPlayerStateAdd(p);
		break;
	case HC_OP_Death:
		ZoneProcessDeath(p);
		break;
	case HC_OP_PlayerStateRemove:
		ZoneProcessPlayerStateRemove(p);
		break;
	case HC_OP_Stamina:
		ZoneProcessStamina(p);
		break;
	case HC_OP_Emote:
		// Animation packet - applies to all entities (not just self)
		// This is used for combat animations, emotes, and other action animations
		ZoneProcessEmote(p);
		break;
	
	// Combat-related opcodes
	case 0x65ca: // OP_Consider response
		ZoneProcessConsider(p);
		break;
	case HC_OP_Action: // OP_Action (combat/spell actions)
		ZoneProcessAction(p);
		break;
	case HC_OP_Damage:
		ZoneProcessDamage(p);
		break;
	case HC_OP_MoneyOnCorpse:
		ZoneProcessMoneyOnCorpse(p);
		break;
	case HC_OP_LootRequest:
		// Server responds to our loot request - confirms we can loot the corpse
		// The actual loot items come via ItemPacket (type 102) BEFORE this response
		if (p.Length() >= 6) {
			uint32_t corpse_id = p.GetUInt32(2);
			LOG_DEBUG(MOD_INVENTORY, "LootRequest response: corpseId={}", corpse_id);

			// Check if loot window is empty (no items to loot)
			// If empty, auto-complete the loot session immediately
#ifdef EQT_HAS_GRAPHICS
			if (m_player_looting_corpse_id != 0 && m_renderer && m_renderer->getWindowManager()) {
				auto* lootWindow = m_renderer->getWindowManager()->getLootWindow();
				if (lootWindow && lootWindow->getLootItems().empty()) {
					LOG_DEBUG(MOD_INVENTORY, "LootRequest response: No items on corpse, auto-completing loot");
					CloseLootWindow(static_cast<uint16_t>(corpse_id));
				}
			}
#endif
		}
		break;
	case HC_OP_LootItem:
		// Server echoes OP_LootItem back as acknowledgment
		if (p.Length() >= 16) {
			uint32_t lootee = p.GetUInt32(2);
			uint32_t looter = p.GetUInt32(6);
			uint16_t slot_id = p.GetUInt16(10);
			int32_t auto_loot = static_cast<int32_t>(p.GetUInt32(12));
			LOG_DEBUG(MOD_INVENTORY, "LootItem ACK: lootee={} looter={} slot={} auto_loot={}",
				lootee, looter, slot_id, auto_loot);
			// auto_loot == -1 means the loot was denied
			if (auto_loot == -1) {
				LOG_WARN(MOD_INVENTORY, "Loot DENIED by server!");
				// Remove from pending slots
				auto it = std::find(m_pending_loot_slots.begin(), m_pending_loot_slots.end(), slot_id);
				if (it != m_pending_loot_slots.end()) {
					m_pending_loot_slots.erase(it);
				}
			}
		} else {
			LOG_DEBUG(MOD_INVENTORY, "LootItem ACK: short packet, len={}", p.Length());
		}
		break;
	case HC_OP_LootComplete:
		// Server signals end of loot session (rarely sent by P1999/Titanium servers)
		LOG_DEBUG(MOD_INVENTORY, "LootComplete received from server");
		m_pending_loot_slots.clear();
		break;
	case HC_OP_ItemPacket:
		// Item packets can be: vendor items (100), inventory items (103), or loot items
		// Check the packet type in the first 4 bytes after the header
		{
			uint32_t item_packet_type = 0;
			if (p.Length() >= 6) {
				item_packet_type = p.GetUInt32(2);
			}
			LOG_DEBUG(MOD_INVENTORY, "ItemPacket received, type={} m_player_looting_corpse_id={} m_vendor_npc_id={} pending_slots={}",
				item_packet_type, m_player_looting_corpse_id, m_vendor_npc_id, m_pending_loot_slots.size());
#ifdef EQT_HAS_GRAPHICS
			// Check if this is a vendor item (type 100)
			if (item_packet_type == EQT::ITEM_PACKET_MERCHANT && m_vendor_npc_id != 0 && m_renderer && m_renderer->getWindowManager()) {
				// Vendor item - add to vendor window
				ZoneProcessVendorItemToUI(p);
			} else if (m_player_looting_corpse_id != 0 && m_renderer && m_renderer->getWindowManager()) {
				// Player mode looting - check if this is a looted item or loot window population
				if (!m_pending_loot_slots.empty()) {
					// We have pending loot requests - this item is going to inventory
					ZoneProcessLootedItemToInventory(p);
				} else {
					// No pending requests - this is loot window population
					ZoneProcessLootItemToUI(p);
				}
			} else
#endif
			if (m_combat_manager && m_combat_manager->IsLooting()) {
				// Headless mode looting
				ZoneProcessLootItem(p);
			} else {
				// Check if this is a trade partner item
#ifdef EQT_HAS_GRAPHICS
				bool handled_as_trade = false;
				if (m_trade_manager && m_trade_manager->isTrading()) {
					handled_as_trade = ZoneProcessTradePartnerItem(p);
				}
				if (!handled_as_trade) {
					// Regular inventory item packet - pass to inventory manager
					if (m_inventory_manager) {
						m_inventory_manager->processItemPacket(p);
						// Refresh sellable items if vendor window is open
						if (m_renderer && m_renderer->getWindowManager() &&
						    m_renderer->getWindowManager()->isVendorWindowOpen()) {
							m_renderer->getWindowManager()->refreshVendorSellableItems();
						}
					}
				}
#endif
				if (s_debug_level >= 2) {
					LOG_DEBUG(MOD_MAIN, "Received ItemPacket type={} while not looting/trading", item_packet_type);
				}
			}
		}
		break;
#ifdef EQT_HAS_GRAPHICS
	case HC_OP_MoveItem:
		ZoneProcessMoveItem(p);
		break;
	case HC_OP_DeleteItem:
		ZoneProcessDeleteItem(p);
		break;
	case HC_OP_ShopRequest:
		ZoneProcessShopRequest(p);
		break;
	case HC_OP_ShopPlayerBuy:
		ZoneProcessShopPlayerBuy(p);
		break;
	case HC_OP_ShopPlayerSell:
		ZoneProcessShopPlayerSell(p);
		break;
	case HC_OP_ShopEndConfirm:
		ZoneProcessShopEndConfirm(p);
		break;
	case HC_OP_MoneyUpdate:
		ZoneProcessMoneyUpdate(p);
		break;
	case HC_OP_GMTraining:
		ZoneProcessGMTraining(p);
		break;
#endif
	case 0x61f9: // OP_TargetCommand response
		// Server may reject our target
		if (p.Length() >= 4) {
			uint16_t result = p.GetUInt16(2);
			if (result != 0 && s_debug_level >= 1) {
				LOG_DEBUG(MOD_MAIN, "Target rejected: {}", result);
			}
		}
		break;
	
	// Additional opcodes
	case HC_OP_BecomeCorpse:
		// Someone died and became a corpse
		if (s_debug_level >= 2) {
			LOG_DEBUG(MOD_MAIN, "Received BecomeCorpse packet");
		}
		break;
	case HC_OP_ZonePlayerToBind:
		ZoneProcessZonePlayerToBind(p);
		break;
	case HC_OP_ZoneChange:
		ZoneProcessZoneChange(p);
		break;
	case HC_OP_SimpleMessage:
		ZoneProcessSimpleMessage(p);
		break;
	case HC_OP_TargetHoTT:
		// Target's target information
		if (p.Length() >= 6) {
			uint16_t target_id = p.GetUInt16(2);
			uint16_t hott_id = p.GetUInt16(4);
			if (s_debug_level >= 2) {
				LOG_DEBUG(MOD_MAIN, "Target {} has target {}", target_id, hott_id);
			}
		}
		break;
	case HC_OP_SkillUpdate:
		// Skill level update
		// SkillUpdate_Struct: skillId (uint32) + value (uint32) = 8 bytes + 2 byte header
		if (p.Length() >= 10) {
			uint32_t skill_id_32 = p.GetUInt32(2);  // 4-byte skillId at offset 2
			uint32_t value = p.GetUInt32(6);        // 4-byte value at offset 6
			uint8_t skill_id = static_cast<uint8_t>(skill_id_32);  // Cast to uint8_t for skill manager

			// Update skill manager
			if (m_skill_manager) {
				m_skill_manager->updateSkill(skill_id, value);
			}

#ifdef EQT_HAS_GRAPHICS
			// Update trainer window if open
			if (m_renderer && m_renderer->getWindowManager() &&
				m_renderer->getWindowManager()->isSkillTrainerWindowOpen()) {
				m_renderer->getWindowManager()->updateSkillTrainerSkill(skill_id, value);
				// Decrement practice points since training succeeded
				m_renderer->getWindowManager()->decrementSkillTrainerPracticePoints();
				if (m_practice_points > 0) {
					--m_practice_points;
				}
			}
#endif

			if (s_debug_level >= 2) {
				LOG_DEBUG(MOD_MAIN, "Skill {} updated to {}", static_cast<int>(skill_id), value);
			}
		}
		break;
	case HC_OP_CancelTrade:
		// Trade cancelled
		if (p.Length() >= 2 + sizeof(EQT::CancelTrade_Struct)) {
			const EQT::CancelTrade_Struct* cancel = reinterpret_cast<const EQT::CancelTrade_Struct*>(p.Data() + 2);
			if (m_trade_manager) {
				m_trade_manager->handleCancelTrade(*cancel);
			}
			if (s_debug_level >= 2) {
				LOG_DEBUG(MOD_MAIN, "Trade cancelled by spawn {}", cancel->spawn_id);
			}
		}
		break;
	case HC_OP_TradeRequest:
		// Trade request from another player
		if (p.Length() >= 2 + sizeof(EQT::TradeRequest_Struct)) {
			const EQT::TradeRequest_Struct* req = reinterpret_cast<const EQT::TradeRequest_Struct*>(p.Data() + 2);
			if (m_trade_manager) {
				// Look up the name of the requesting player
				std::string from_name = "Unknown";
				auto it = m_entities.find(static_cast<uint16_t>(req->from_spawn_id));
				if (it != m_entities.end()) {
					from_name = it->second.name;
				}
				m_trade_manager->handleTradeRequest(*req, from_name);
			}
			if (s_debug_level >= 2) {
				LOG_DEBUG(MOD_MAIN, "Trade request from spawn {} to spawn {}",
				         req->from_spawn_id, req->target_spawn_id);
			}
		}
		break;
	case HC_OP_TradeRequestAck:
		// Trade request accepted
		if (p.Length() >= 2 + sizeof(EQT::TradeRequestAck_Struct)) {
			const EQT::TradeRequestAck_Struct* ack = reinterpret_cast<const EQT::TradeRequestAck_Struct*>(p.Data() + 2);
			if (m_trade_manager) {
				m_trade_manager->handleTradeRequestAck(*ack);
			}
			if (s_debug_level >= 2) {
				LOG_DEBUG(MOD_MAIN, "Trade request ack from spawn {} to spawn {}",
				         ack->from_spawn_id, ack->target_spawn_id);
			}
		}
		break;
	case HC_OP_TradeCoins:
		// Trade partner changed money
		if (p.Length() >= 2 + sizeof(EQT::TradeCoins_Struct)) {
			const EQT::TradeCoins_Struct* coins = reinterpret_cast<const EQT::TradeCoins_Struct*>(p.Data() + 2);
			if (m_trade_manager) {
				m_trade_manager->handleTradeCoins(*coins);
			}
			if (s_debug_level >= 2) {
				LOG_DEBUG(MOD_MAIN, "Trade coins from spawn {}: slot {} amount {}",
				         coins->spawn_id, coins->slot, coins->amount);
			}
		}
		break;
	case HC_OP_TradeAcceptClick:
		// Trade partner clicked accept
		if (p.Length() >= 2 + sizeof(EQT::TradeAcceptClick_Struct)) {
			const EQT::TradeAcceptClick_Struct* accept = reinterpret_cast<const EQT::TradeAcceptClick_Struct*>(p.Data() + 2);
			if (m_trade_manager) {
				m_trade_manager->handleTradeAcceptClick(*accept);
			}
			if (s_debug_level >= 2) {
				LOG_DEBUG(MOD_MAIN, "Trade accept click from spawn {}: accepted={}",
				         accept->spawn_id, accept->accepted);
			}
		}
		break;
	case HC_OP_FinishTrade:
		// Trade completed - server may send with or without struct data
		{
			EQT::FinishTrade_Struct finish = {};
			if (p.Length() >= 2 + sizeof(EQT::FinishTrade_Struct)) {
				finish = *reinterpret_cast<const EQT::FinishTrade_Struct*>(p.Data() + 2);
			}
			if (m_trade_manager) {
				m_trade_manager->handleFinishTrade(finish);
			}
			LOG_INFO(MOD_MAIN, "Trade finished");
		}
		break;
	case HC_OP_PreLogoutReply:
		// Response to logout request
		if (s_debug_level >= 2) {
			LOG_DEBUG(MOD_MAIN, "Logout reply received");
		}
		break;
	case HC_OP_MobRename:
		// Mob name change - typically when mob becomes corpse
		// The packet contains 3 name fields (each 64 bytes)
		if (p.Length() >= 4) {  // Need at least opcode + 2 bytes for parsing
			// Parse the packet to find the entity ID
			// From the log, we see the mob name appears multiple times with "'s_corpse" appended
			const char* data = (const char*)p.Data() + 2;  // Skip opcode
			int data_len = p.Length() - 2;
			
			// Extract the new name from the first occurrence
			char new_name[65] = {0};
			if (data_len >= 64) {
				memcpy(new_name, data, 64);
				new_name[64] = '\0';
			}
			
			// The mob ID isn't directly in the packet, but we can find the entity by matching the base name
			// Extract base name without "'s_corpse" suffix
			std::string name_str(new_name);
			bool is_corpse = name_str.find("'s_corpse") != std::string::npos;
			
			if (is_corpse) {
				// Extract the base name and corpse ID from name like "a_gopher_snake's_corpse126"
				size_t corpse_pos = name_str.find("'s_corpse");
				if (corpse_pos != std::string::npos) {
					std::string base_name = name_str.substr(0, corpse_pos);
					std::string corpse_suffix = name_str.substr(corpse_pos + 9);  // Skip "'s_corpse"
					
					// The number at the end is the entity ID
					uint16_t entity_id = 0;
					try {
						entity_id = std::stoi(corpse_suffix);
					} catch (...) {
						// If we can't parse the ID, try to find by matching base name
						for (auto& [id, entity] : m_entities) {
							if (entity.name == base_name + "001" || entity.name == base_name) {
								entity_id = id;
								break;
							}
						}
					}
					
					if (entity_id > 0) {
						auto it = m_entities.find(entity_id);
						if (it != m_entities.end()) {
							std::string old_name = it->second.name;
							it->second.name = new_name;
							it->second.is_corpse = true;
							if (s_debug_level >= 1) {
								LOG_INFO(MOD_ENTITY, "Entity {} became corpse: '{}' -> '{}'",
									entity_id, old_name, new_name);
							}
						}
					}
				}
			}
		}
		break;
	case HC_OP_Stun:
		// Stun effect notification
		if (p.Length() >= 6) {
			uint16_t target_id = p.GetUInt16(2);
			uint16_t duration = p.GetUInt16(4);  // Duration in 10ths of a second
			float duration_sec = duration / 10.0f;

			if (target_id == m_my_spawn_id) {
				if (s_debug_level >= 1) {
					std::cout << fmt::format("[WARNING] You have been stunned for {:.1f} seconds!", duration_sec) << std::endl;
				}
			} else if (s_debug_level >= 2) {
				LOG_DEBUG(MOD_MAIN, "Entity {} stunned for {:.1f} seconds", target_id, duration_sec);
			}
		}
		break;

	// Group packet handlers
	case HC_OP_GroupInvite:
		ZoneProcessGroupInvite(p);
		break;
	case HC_OP_GroupInvite2:
		// Alternate invite opcode - same handler
		ZoneProcessGroupInvite(p);
		break;
	case HC_OP_GroupFollow:
		ZoneProcessGroupFollow(p);
		break;
	case HC_OP_GroupUpdate:
		ZoneProcessGroupUpdate(p);
		break;
	case HC_OP_GroupDisband:
		ZoneProcessGroupDisband(p);
		break;
	case HC_OP_GroupCancelInvite:
		ZoneProcessGroupCancelInvite(p);
		break;
	case HC_OP_SetGroupTarget:
		// Group target indicator - ignore for now
		break;
	case HC_OP_LFGAppearance:
		// LFG appearance update - ignore
		break;
	case HC_OP_LinkedReuse:
		// Linked spell reuse timer - ignore for now
		break;
	case HC_OP_MemorizeSpell:
		{
			// MemorizeSpell_Struct: slot(4), spell_id(4), scribing(4), unknown0(4)
			// Total: 16 bytes after opcode (2 bytes)
			if (p.Length() < 18) {
				LOG_WARN(MOD_SPELL, "MemorizeSpell packet too short: {} bytes", p.Length());
				break;
			}

			uint32_t slot = p.GetUInt32(2);
			uint32_t spell_id = p.GetUInt32(6);
			uint32_t scribing = p.GetUInt32(10);

			LOG_DEBUG(MOD_SPELL, "MemorizeSpell response: slot={} spell_id={} scribing={}",
				slot, spell_id, scribing);

			// Check if this is a scribe confirmation (scribing flag cleared to 0)
			if (m_pending_scribe_spell_id != 0 &&
				m_pending_scribe_spell_id == spell_id &&
				scribing == 0) {

				// Add spell to spellbook
				if (m_spell_manager->scribeSpell(spell_id, static_cast<uint16_t>(slot))) {
					// Get spell name for message
					const EQ::SpellData* spellData = m_spell_manager->getSpell(spell_id);
					std::string spellName = spellData ? spellData->name : "spell";

					AddChatSystemMessage(fmt::format("You have learned {}!", spellName));
					LOG_INFO(MOD_SPELL, "Successfully scribed spell {} ({}) to slot {}",
						spellName, spell_id, slot);

					// Refresh spellbook UI if open
					if (m_renderer) {
						auto* windowManager = m_renderer->getWindowManager();
						if (windowManager) {
							auto* spellBookWindow = windowManager->getSpellBookWindow();
							if (spellBookWindow) {
								spellBookWindow->refresh();
							}
						}
					}
				} else {
					LOG_WARN(MOD_SPELL, "Failed to add spell {} to spellbook slot {}",
						spell_id, slot);
				}

				// Clear pending scribe state
				m_pending_scribe_spell_id = 0;
				m_pending_scribe_book_slot = 0;
				m_pending_scribe_source_slot = -1;
			}
			// Handle gem slot memorization confirmations (slot < MAX_SPELL_GEMS)
			else if (slot < EQ::MAX_SPELL_GEMS && scribing == 0) {
				// Existing gem memorization - SpellManager handles this via other mechanisms
				LOG_DEBUG(MOD_SPELL, "Gem memorization confirmation: gem={} spell={}", slot, spell_id);
			}
		}
		break;
	case HC_OP_SpecialMesg:
		{
			// Special messages (trade notifications, skill-ups, etc.)
			// Format: 2 bytes opcode + 24 bytes header + null-terminated message
			constexpr size_t HEADER_SIZE = 24;
			constexpr size_t MSG_OFFSET = 2 + HEADER_SIZE;  // After opcode and header

			if (p.Length() > MSG_OFFSET) {
				const char* msgStart = reinterpret_cast<const char*>(p.Data()) + MSG_OFFSET;
				size_t maxLen = p.Length() - MSG_OFFSET;
				std::string message(msgStart, strnlen(msgStart, maxLen));

				if (!message.empty()) {
					AddChatSystemMessage(message);
				}
			}
		}
		break;
	case HC_OP_InterruptCast:
		// Cast interrupted - spell manager handles this via other packets
		break;

	// ========================================================================
	// Skill Response Handlers
	// ========================================================================
	case HC_OP_Begging:
		// Begging response - BeggingResponse_Struct (20 bytes after opcode)
		if (p.Length() >= 22) {
			uint32_t result = p.GetUInt32(14);  // offset 12 in struct + 2 for opcode
			uint32_t amount = p.GetUInt32(18);  // offset 16 in struct + 2 for opcode
			// Result: 0=Fail, 1=Plat, 2=Gold, 3=Silver, 4=Copper
			if (result == 0) {
				AddChatSystemMessage("You have been unable to convince your target to give you money.");
			} else {
				const char* coin_types[] = {"", "platinum", "gold", "silver", "copper"};
				if (result <= 4) {
					AddChatSystemMessage(fmt::format("You receive {} {}.", amount, coin_types[result]));
				}
			}
		}
		break;
	case HC_OP_Hide:
		// Hide skill response - typically no data, success indicated by SpawnAppearance
		LOG_DEBUG(MOD_MAIN, "Hide response received: {} bytes", p.Length());
		break;
	case HC_OP_Sneak:
		// Sneak skill response
		LOG_DEBUG(MOD_MAIN, "Sneak response received: {} bytes", p.Length());
		break;
	case HC_OP_SenseHeading:
		// Sense Heading response - may contain heading info
		LOG_DEBUG(MOD_MAIN, "SenseHeading response received: {} bytes", p.Length());
		break;
	case HC_OP_Forage:
		// Forage response - contains item found or failure
		LOG_DEBUG(MOD_MAIN, "Forage response received: {} bytes", p.Length());
		break;
	case HC_OP_Fishing:
		// Fishing response
		LOG_DEBUG(MOD_MAIN, "Fishing response received: {} bytes", p.Length());
		break;
	case HC_OP_Mend:
		// Mend response
		LOG_DEBUG(MOD_MAIN, "Mend response received: {} bytes", p.Length());
		break;
	case HC_OP_FeignDeath:
		// Feign Death response
		LOG_DEBUG(MOD_MAIN, "FeignDeath response received: {} bytes", p.Length());
		break;
	case HC_OP_Track:
		// Track response - contains list of trackable entities
		LOG_DEBUG(MOD_MAIN, "Track response received: {} bytes", p.Length());
		break;
	case HC_OP_PickPocket:
		// Pick Pocket response
		LOG_DEBUG(MOD_MAIN, "PickPocket response received: {} bytes", p.Length());
		break;
	case HC_OP_SenseTraps:
		// Sense Traps response
		LOG_DEBUG(MOD_MAIN, "SenseTraps response received: {} bytes", p.Length());
		break;
	case HC_OP_DisarmTraps:
		// Disarm Traps response
		LOG_DEBUG(MOD_MAIN, "DisarmTraps response received: {} bytes", p.Length());
		break;
	case HC_OP_InstillDoubt:
		// Intimidation response
		LOG_DEBUG(MOD_MAIN, "InstillDoubt response received: {} bytes", p.Length());
		break;
	case HC_OP_ReadBook:
		ZoneProcessReadBook(p);
		break;

	default:
		if (s_debug_level >= 1) {
			std::cout << fmt::format("Unhandled zone opcode: {}", GetOpcodeName(opcode)) << std::endl;
		}
		break;
	}
}

// Zone packet senders
void EverQuest::ZoneSendSessionReady()
{
	EQ::Net::DynamicPacket p;
	p.Resize(14);
	p.PutUInt16(0, HC_OP_SessionReady);
	p.PutUInt32(2, m_zone_sequence++);  // sequence
	p.PutUInt32(6, 0);  // unknown
	p.PutUInt32(10, 2048);  // max length
	
	DumpPacket("C->S", HC_OP_SessionReady, p);
	m_zone_connection->QueuePacket(p);
}

void EverQuest::ZoneSendZoneEntry()
{
	// Zone entry packet - ClientZoneEntry_Struct
	EQ::Net::DynamicPacket p;
	p.Resize(70);  // 2 bytes opcode + 68 bytes data
	
	p.PutUInt16(0, HC_OP_ZoneEntry);
	
	// ClientZoneEntry_Struct starts at offset 2
	// unknown00 - 32-bit value (0xFFF67726 in good log)
	p.PutUInt32(2, 0xFFF67726);  // This might need to be calculated or could be a checksum
	
	// char_name[64] at offset 6
	size_t name_offset = 6;
	size_t name_len = m_character.length();
	if (name_len > 63) name_len = 63;
	memcpy((char*)p.Data() + name_offset, m_character.c_str(), name_len);
	
	// Fill rest of name field with zeros (64 bytes total for name)
	for (size_t i = name_offset + name_len; i < name_offset + 64; i++) {
		p.PutUInt8(i, 0);
	}
	
	DumpPacket("C->S", HC_OP_ZoneEntry, p);
	m_zone_connection->QueuePacket(p);
	m_zone_entry_sent = true;
	
	// Clear movement history on zone entry
	m_movement_history.clear();
	m_last_movement_history_send = 0;  // Reset the timer to prevent immediate sends
	
	// Add zone entry to movement history
	MovementHistoryEntry zone_entry;
	zone_entry.x = m_x;
	zone_entry.y = m_y;
	zone_entry.z = m_z;
	zone_entry.type = 4; // ZoneLine type
	zone_entry.timestamp = static_cast<uint32_t>(std::time(nullptr));
	m_movement_history.push_back(zone_entry);
}

void EverQuest::ZoneSendReqClientSpawn()
{
	EQ::Net::DynamicPacket p;
	p.Resize(2);
	
	p.PutUInt16(0, HC_OP_ReqClientSpawn);
	
	DumpPacket("C->S", HC_OP_ReqClientSpawn, p);
	m_zone_connection->QueuePacket(p);
}

void EverQuest::ZoneSendClientReady()
{
	EQ::Net::DynamicPacket p;
	p.Resize(2);
	
	p.PutUInt16(0, HC_OP_ClientReady);
	
	LOG_DEBUG(MOD_ZONE, "Sending OP_ClientReady");

	DumpPacket("C->S", HC_OP_ClientReady, p);
	m_zone_connection->QueuePacket(p);
	m_client_ready_sent = true;
}

void EverQuest::ZoneSendSetServerFilter()
{
	// Set server filter for what types of messages we want
	// SetServerFilter_Struct is uint32 filters[29] = 116 bytes
	EQ::Net::DynamicPacket p;
	p.Resize(118);  // 2 bytes opcode + 116 bytes data
	
	p.PutUInt16(0, HC_OP_SetServerFilter);
	
	// Set all filters to 0xFFFFFFFF (all enabled) for now
	for (size_t i = 0; i < 29; i++) {
		p.PutUInt32(2 + (i * 4), 0xFFFFFFFF);
	}
	
	LOG_DEBUG(MOD_ZONE, "Sending OP_SetServerFilter");

	DumpPacket("C->S", HC_OP_SetServerFilter, p);
	m_zone_connection->QueuePacket(p);
	m_server_filter_sent = true;
}

void EverQuest::ZoneSendStreamIdentify()
{
	// Send stream identify with Titanium zone opcode
	EQ::Net::DynamicPacket p;
	p.Resize(2);
	
	p.PutUInt16(0, HC_OP_ZoneEntry);  // 0x7213 - Titanium_zone
	
	LOG_DEBUG(MOD_ZONE, "Sending stream identify with opcode {:#06x} (Titanium_zone)", 0x7213);
	
	m_zone_connection->QueuePacket(p, 0, false);  // Send unreliable
}

void EverQuest::ZoneSendAckPacket()
{
	EQ::Net::DynamicPacket p;
	p.Resize(6);
	
	p.PutUInt16(0, HC_OP_AckPacket);
	p.PutUInt32(2, 0);  // sequence or something
	
	DumpPacket("C->S", HC_OP_AckPacket, p);
	m_zone_connection->QueuePacket(p);
}

void EverQuest::ZoneSendReqNewZone()
{
	EQ::Net::DynamicPacket p;
	p.Resize(2);
	
	p.PutUInt16(0, HC_OP_ReqNewZone);
	
	DumpPacket("C->S", HC_OP_ReqNewZone, p);
	m_zone_connection->QueuePacket(p);
	m_req_new_zone_sent = true;
}

void EverQuest::ZoneSendSendAATable()
{
	EQ::Net::DynamicPacket p;
	p.Resize(2);
	
	p.PutUInt16(0, HC_OP_SendAATable);
	
	DumpPacket("C->S", HC_OP_SendAATable, p);
	m_zone_connection->QueuePacket(p);
	m_aa_table_sent = true;
}

void EverQuest::ZoneSendUpdateAA()
{
	EQ::Net::DynamicPacket p;
	p.Resize(12);  // Typical UpdateAA size
	
	p.PutUInt16(0, HC_OP_UpdateAA);
	// Rest is AA data - zeros for now
	
	DumpPacket("C->S", HC_OP_UpdateAA, p);
	m_zone_connection->QueuePacket(p);
	m_update_aa_sent = true;
}

void EverQuest::ZoneSendSendTributes()
{
	EQ::Net::DynamicPacket p;
	p.Resize(2);
	
	p.PutUInt16(0, HC_OP_SendTributes);
	
	DumpPacket("C->S", HC_OP_SendTributes, p);
	m_zone_connection->QueuePacket(p);
	m_tributes_sent = true;
}

void EverQuest::ZoneSendRequestGuildTributes()
{
	EQ::Net::DynamicPacket p;
	p.Resize(2);
	
	p.PutUInt16(0, HC_OP_RequestGuildTributes);
	
	DumpPacket("C->S", HC_OP_RequestGuildTributes, p);
	m_zone_connection->QueuePacket(p);
	m_guild_tributes_sent = true;
}

void EverQuest::ZoneSendSpawnAppearance()
{
	EQ::Net::DynamicPacket p;
	p.Resize(14);  // Typical spawn appearance size
	
	p.PutUInt16(0, HC_OP_SpawnAppearance);
	p.PutUInt16(2, 0);  // spawn id (0 = self)
	p.PutUInt16(4, 14);  // type (14 = animation)
	p.PutUInt32(6, 0);  // value
	p.PutUInt32(10, 0);  // unknown
	
	DumpPacket("C->S", HC_OP_SpawnAppearance, p);
	m_zone_connection->QueuePacket(p);
	m_spawn_appearance_sent = true;
}

void EverQuest::ZoneSendSendExpZonein()
{
	EQ::Net::DynamicPacket p;
	p.Resize(2);
	
	p.PutUInt16(0, HC_OP_SendExpZonein);
	
	DumpPacket("C->S", HC_OP_SendExpZonein, p);
	m_zone_connection->QueuePacket(p);
	m_exp_zonein_sent = true;
}

// Zone packet processors
void EverQuest::ZoneProcessNewZone(const EQ::Net::Packet &p)
{
	// NewZone packet contains zone information
	// NewZone_Struct: char_name[64], zone_short_name[32], zone_long_name[278], ...
	// zone_id at offset 684, total struct size 700 bytes
	if (p.Length() >= 96) {  // Need at least 64 + 32 bytes for zone name
		// Skip opcode (2 bytes) and char_name (64 bytes) to get to zone_short_name
		m_current_zone_name = p.GetCString(66);  // 2 (opcode) + 64 (char_name)

		// Extract zone_id if packet is large enough (offset 684 in struct, 686 in packet)
		if (p.Length() >= 688) {  // 2 (opcode) + 684 + 2 (zone_id)
			m_current_zone_id = p.GetUInt16(686);
		}

		if (s_debug_level >= 2) {
			LOG_DEBUG(MOD_MAIN, "Received new zone data for: {} (zone_id={})",
				m_current_zone_name, m_current_zone_id);
		}

		// Load pathfinder for this zone
		LoadPathfinder(m_current_zone_name);

		// Load zone map for terrain height calculations
		LoadZoneMap(m_current_zone_name);

		// Load zone lines for zone transitions
		LoadZoneLines(m_current_zone_name);
	} else {
		if (s_debug_level >= 2) {
			LOG_DEBUG(MOD_MAIN, "Received new zone data");
		}
	}
	
	m_new_zone_received = true;

#ifdef EQT_HAS_GRAPHICS
	// Load zone graphics
	OnZoneLoadedGraphics();
#endif

	// After NewZone, send all parallel requests
	// 1. AA table request
	if (!m_aa_table_sent) {
		ZoneSendSendAATable();
	}
	
	// 2. Update AA
	if (!m_update_aa_sent) {
		ZoneSendUpdateAA();
	}
	
	// 3. Send tributes request
	if (!m_tributes_sent) {
		ZoneSendSendTributes();
	}
	
	// 4. Request guild tributes
	if (!m_guild_tributes_sent) {
		ZoneSendRequestGuildTributes();
	}
}

void EverQuest::ZoneProcessPlayerProfile(const EQ::Net::Packet &p)
{
	if (s_debug_level >= 2) {
		LOG_DEBUG(MOD_MAIN, "PlayerProfile received, size={} bytes", p.Length());
	}

	// PlayerProfile_Struct from titanium_structs.h
	// All offsets need +2 for the opcode
	const size_t base = 2;

	// Extract all player data from the profile
	if (p.Length() < base + 19588) {  // PlayerProfile_Struct is 19588 bytes
		LOG_WARN(MOD_MAIN, "PlayerProfile packet too small: {} bytes (expected at least {})",
			p.Length(), base + 19588);
	}
	
	// Basic attributes (offsets from struct)
	uint32_t gender = p.GetUInt32(base + 4);     // offset 4
	uint32_t race = p.GetUInt32(base + 8);       // offset 8
	uint32_t class_ = p.GetUInt32(base + 12);    // offset 12
	uint8_t level = p.GetUInt8(base + 20);       // offset 20
	uint32_t deity = p.GetUInt32(base + 124);    // offset 124

	// Bind points - array of 5 BindStruct at offset 24, each 20 bytes
	// BindStruct: zone_id (4), x (4), y (4), z (4), heading (4)
	// First bind point (index 0) is the main bind location
	uint32_t bind_zone_id = p.GetUInt32(base + 24);       // offset 24
	float bind_x = p.GetFloat(base + 28);                  // offset 28
	float bind_y = p.GetFloat(base + 32);                  // offset 32
	float bind_z = p.GetFloat(base + 36);                  // offset 36
	float bind_heading = p.GetFloat(base + 40);            // offset 40

	// Appearance
	uint8_t haircolor = p.GetUInt8(base + 172);  // offset 172
	uint8_t beardcolor = p.GetUInt8(base + 173); // offset 173
	uint8_t eyecolor1 = p.GetUInt8(base + 174);  // offset 174
	uint8_t eyecolor2 = p.GetUInt8(base + 175);  // offset 175
	uint8_t hairstyle = p.GetUInt8(base + 176);  // offset 176
	uint8_t beard = p.GetUInt8(base + 177);      // offset 177
	uint8_t face = p.GetUInt8(base + 2264);      // offset 2264
	
	// Stats - these are base stats without equipment
	uint32_t cur_hp = p.GetUInt32(base + 2232);  // offset 2232
	uint32_t mana = p.GetUInt32(base + 2228);    // offset 2228
	uint32_t endurance = p.GetUInt32(base + 6148); // offset 6148
	uint32_t STR = p.GetUInt32(base + 2236);     // offset 2236
	uint32_t STA = p.GetUInt32(base + 2240);     // offset 2240
	uint32_t CHA = p.GetUInt32(base + 2244);     // offset 2244
	uint32_t DEX = p.GetUInt32(base + 2248);     // offset 2248
	uint32_t INT = p.GetUInt32(base + 2252);     // offset 2252
	uint32_t AGI = p.GetUInt32(base + 2256);     // offset 2256
	uint32_t WIS = p.GetUInt32(base + 2260);     // offset 2260

	// Practice points (unspent training points)
	uint32_t practice_points = p.GetUInt32(base + 2224);  // offset 2224

	// Currency - player's on-person money
	uint32_t platinum = p.GetUInt32(base + 4428);  // offset 4428
	uint32_t gold = p.GetUInt32(base + 4432);      // offset 4432
	uint32_t silver = p.GetUInt32(base + 4436);    // offset 4436
	uint32_t copper = p.GetUInt32(base + 4440);    // offset 4440

	// Bank currency - money stored in bank
	// From titanium_structs.h: platinum_bank at 13136, gold_bank at 13140, etc.
	uint32_t bank_platinum = p.GetUInt32(base + 13136);  // offset 13136
	uint32_t bank_gold = p.GetUInt32(base + 13140);      // offset 13140
	uint32_t bank_silver = p.GetUInt32(base + 13144);    // offset 13144
	uint32_t bank_copper = p.GetUInt32(base + 13148);    // offset 13148

	// Character identity
	char name[64] = {0};
	char last_name[32] = {0};
	if (p.Length() >= base + 13036) {
		memcpy(name, static_cast<const char*>(p.Data()) + base + 12940, 64);      // offset 12940
		memcpy(last_name, static_cast<const char*>(p.Data()) + base + 13004, 32); // offset 13004
	}
	uint32_t guild_id = p.GetUInt32(base + 13036); // offset 13036
	
	// Position data is stored differently - it's in bind points at offset 24
	// But the current position is at offset 13116-13128
	// Note: Server x/y are swapped relative to zone geometry
	float server_x = p.GetFloat(base + 13116);      // offset 13116
	float server_y = p.GetFloat(base + 13120);      // offset 13120
	float z = p.GetFloat(base + 13124);      // offset 13124
	float heading = p.GetFloat(base + 13128); // offset 13128

	// Update our position (swap x and y from server)
	m_x = server_y;
	m_y = server_x;
	m_z = z;
	// PlayerProfile heading is stored as (12-bit scaled value / 4)
	// Convert back: server_heading_deg = heading * 360 / 512, then m_heading = 90 - server_heading_deg
	float server_heading_deg = heading * 360.0f / 512.0f;
	m_heading = 90.0f - server_heading_deg;
	if (m_heading < 0.0f) m_heading += 360.0f;
	if (m_heading >= 360.0f) m_heading -= 360.0f;

	// Debug: Zone-in position tracking
	if (s_debug_level >= 1) {
		LOG_INFO(MOD_ZONE, "[ZONE-IN] PlayerProfile position: server=({:.2f},{:.2f},{:.2f}) heading={:.2f} -> client=({:.2f},{:.2f},{:.2f}) heading={:.2f}",
		         server_x, server_y, z, heading, m_x, m_y, m_z, m_heading);
		// Additional logging in consistent format for position debugging
		LOG_DEBUG(MOD_MOVEMENT, "POS S->C PlayerProfile [SELF] profile_heading={:.2f} -> server_heading_deg={:.2f} -> m_heading={:.2f}deg",
			heading, server_heading_deg, m_heading);
		LOG_DEBUG(MOD_MOVEMENT, "POS S->C PlayerProfile [SELF] server_pos=({:.2f},{:.2f},{:.2f}) -> client_pos=({:.2f},{:.2f},{:.2f})",
			server_x, server_y, z, m_x, m_y, m_z);
	}

#ifdef EQT_HAS_GRAPHICS
	// Sync initial position to renderer (important for Player mode)
	// Convert m_heading from degrees (0-360) to EQ format (0-512)
	if (m_renderer) {
		float heading512 = m_heading * 512.0f / 360.0f;
		if (s_debug_level >= 1) {
			LOG_INFO(MOD_ZONE, "[ZONE-IN] Calling setPlayerPosition from PlayerProfile: ({:.2f},{:.2f},{:.2f}) heading={:.2f}deg -> {:.2f} (512 fmt)",
			         m_x, m_y, m_z, m_heading, heading512);
		}
		m_renderer->setPlayerPosition(m_x, m_y, m_z, heading512);
	}
#endif

	// Store character stats
	m_level = level;
	m_class = class_;
	m_race = race;
	m_gender = gender;
	m_deity = deity;
	m_cur_hp = cur_hp;
	m_max_hp = cur_hp;  // Will be updated when we get buffs/equipment
	m_mana = mana;
	m_max_mana = mana;
	m_endurance = endurance;
	m_max_endurance = endurance;
	m_STR = STR;
	m_STA = STA;
	m_CHA = CHA;
	m_DEX = DEX;
	m_INT = INT;
	m_AGI = AGI;
	m_WIS = WIS;
	m_platinum = platinum;
	m_gold = gold;
	m_silver = silver;
	m_copper = copper;
	m_bank_platinum = bank_platinum;
	m_bank_gold = bank_gold;
	m_bank_silver = bank_silver;
	m_bank_copper = bank_copper;
	m_practice_points = practice_points;
	m_last_name = last_name;

	// Initialize skill manager with player info first
	if (m_skill_manager) {
		m_skill_manager->initialize(static_cast<uint8_t>(class_), static_cast<uint8_t>(race), level);

		// Skills are stored as uint32_t[100] starting at offset 4460
		// Reference: EQEmu titanium_structs.h PlayerProfile_Struct
		const size_t skill_offset = 4460;
		if (p.Length() >= base + skill_offset + (EQT::MAX_PP_SKILL * 4)) {
			uint32_t skills[EQT::MAX_PP_SKILL];
			int nonzero_count = 0;
			for (size_t i = 0; i < EQT::MAX_PP_SKILL; ++i) {
				skills[i] = p.GetUInt32(base + skill_offset + (i * 4));
				if (skills[i] > 0) {
					nonzero_count++;
					if (s_debug_level >= 2) {
						LOG_DEBUG(MOD_MAIN, "Skill[{}] = {}", i, skills[i]);
					}
				}
			}
			m_skill_manager->updateAllSkills(skills, EQT::MAX_PP_SKILL);
			LOG_INFO(MOD_MAIN, "Loaded {} skills from PlayerProfile ({} non-zero)", EQT::MAX_PP_SKILL, nonzero_count);
		} else {
			LOG_WARN(MOD_MAIN, "PlayerProfile too small for skills: {} < {}",
				p.Length(), base + skill_offset + (EQT::MAX_PP_SKILL * 4));
		}
	}

#ifdef EQT_HAS_GRAPHICS
	// Update inventory manager with player info for item restriction validation
	if (m_inventory_manager) {
		m_inventory_manager->setPlayerInfo(race, class_, level);
	}
#endif

	// Store bind point (swap x/y like we do for position)
	m_bind_zone_id = bind_zone_id;
	m_bind_x = bind_y;  // Server x/y are swapped
	m_bind_y = bind_x;
	m_bind_z = bind_z;
	m_bind_heading = bind_heading;

	// The entity ID appears to be at offset 14384 based on previous debugging
	uint32_t entity_id = 0;
	if (p.Length() > base + 14384) {
		entity_id = p.GetUInt32(base + 14384);
		m_my_character_id = static_cast<uint16_t>(entity_id);
	}
	
	// For size, we need to check if it's sent in the profile or if we get it from spawn
	// It's not in the standard PlayerProfile_Struct, so we'll use default
	float size = 6.0f;  // Default player size
	
	if (s_debug_level >= 2) {
		LOG_DEBUG(MOD_MAIN, "=== CHARACTER DATA ===");
		LOG_DEBUG(MOD_MAIN, "Name: {} {} (Level {} {} {})",
			name, last_name, level, GetClassName(class_), GetRaceName(race));
		LOG_DEBUG(MOD_MAIN, "Gender: {} Race: {} Class: {} Deity: {}",
			gender, race, class_, deity);
		LOG_DEBUG(MOD_MAIN, "=== POSITION DATA ===");
		LOG_DEBUG(MOD_MAIN, "Server raw: x={:.2f} y={:.2f} z={:.2f} heading={:.2f}",
			server_x, server_y, z, heading);
		LOG_DEBUG(MOD_MAIN, "Internal (swapped): m_x={:.2f} m_y={:.2f} m_z={:.2f} m_heading={:.2f}",
			m_x, m_y, m_z, m_heading);
		LOG_DEBUG(MOD_MAIN, "Size: {:.2f} (default, will be updated from spawn)", size);
		LOG_DEBUG(MOD_MAIN, "=== BIND POINT ===");
		LOG_DEBUG(MOD_MAIN, "Bind raw: zone={} x={:.2f} y={:.2f} z={:.2f} heading={:.2f}",
			bind_zone_id, bind_x, bind_y, bind_z, bind_heading);
		LOG_DEBUG(MOD_MAIN, "Bind internal: zone={} m_bind_x={:.2f} m_bind_y={:.2f} m_bind_z={:.2f}",
			m_bind_zone_id, m_bind_x, m_bind_y, m_bind_z);
		LOG_DEBUG(MOD_MAIN, "=== STATS ===");
		LOG_DEBUG(MOD_MAIN, "HP: {} Mana: {} End: {}", cur_hp, mana, endurance);
		LOG_DEBUG(MOD_MAIN, "STR:{} STA:{} CHA:{} DEX:{} INT:{} AGI:{} WIS:{}",
			STR, STA, CHA, DEX, INT, AGI, WIS);
		LOG_DEBUG(MOD_MAIN, "Currency: {}pp {}gp {}sp {}cp", platinum, gold, silver, copper);
		LOG_DEBUG(MOD_MAIN, "Bank Currency: {}pp {}gp {}sp {}cp", bank_platinum, bank_gold, bank_silver, bank_copper);
		LOG_DEBUG(MOD_MAIN, "Practice points (training sessions): {}", practice_points);
		LOG_DEBUG(MOD_MAIN, "Entity ID: {} (from offset 14384)", entity_id);
		LOG_DEBUG(MOD_MAIN, "===========================");
	}
	
	// Add ourselves to the entity list
	Entity self_entity;
	self_entity.spawn_id = m_my_character_id;
	self_entity.name = std::string(name);
	// Use already-swapped m_x and m_y, and transformed heading
	self_entity.x = m_x;
	self_entity.y = m_y;
	self_entity.z = m_z;
	self_entity.heading = m_heading;  // Use transformed heading (360 - raw_heading)
	self_entity.level = level;
	self_entity.class_id = static_cast<uint8_t>(class_);
	self_entity.race_id = static_cast<uint16_t>(race);
	self_entity.gender = static_cast<uint8_t>(gender);
	self_entity.guild_id = guild_id;
	self_entity.hp_percent = 100;  // We'll get actual HP from spawn or HP update packets
	self_entity.cur_mana = static_cast<uint16_t>(mana);
	self_entity.max_mana = static_cast<uint16_t>(mana);  // Will be updated from spawn
	self_entity.size = size;
	self_entity.animation = 0;
	self_entity.delta_x = 0;
	self_entity.delta_y = 0;
	self_entity.delta_z = 0;
	self_entity.delta_heading = 0;
	self_entity.last_update_time = std::time(nullptr);
	
	// Update our own size
	m_size = size;
	
	// Store in entity list
	m_entities[m_my_character_id] = self_entity;

	if (s_debug_level >= 1) {
		LOG_DEBUG(MOD_MAIN, "Added self to entity list: {} (ID: {})", self_entity.name, m_my_character_id);
	}

	// Extract spell gems and spellbook from PlayerProfile
	// Titanium offsets: spell_book at 2312 (400 x uint32), mem_spells at 4360 (8 x uint32)
	if (m_spell_manager) {
		constexpr size_t SPELLBOOK_OFFSET = 2312;
		constexpr size_t MEM_SPELLS_OFFSET = 4360;
		constexpr size_t SPELLBOOK_SIZE = 400;
		constexpr size_t SPELL_GEM_COUNT = 8;

		// Extract spellbook (400 spell IDs)
		if (p.Length() >= base + SPELLBOOK_OFFSET + SPELLBOOK_SIZE * 4) {
			std::array<uint32_t, 400> spellbook;
			for (size_t i = 0; i < SPELLBOOK_SIZE; ++i) {
				spellbook[i] = p.GetUInt32(base + SPELLBOOK_OFFSET + i * 4);
			}
			m_spell_manager->setSpellbook(spellbook.data(), SPELLBOOK_SIZE);

			// Count non-empty spellbook entries for debug
			size_t scribed_count = 0;
			for (size_t i = 0; i < SPELLBOOK_SIZE; ++i) {
				if (spellbook[i] != 0xFFFFFFFF && spellbook[i] != 0) {
					scribed_count++;
				}
			}
			if (s_debug_level >= 1) {
				LOG_DEBUG(MOD_SPELL, "Loaded {} spells from spellbook", scribed_count);
			}
		}

		// Extract memorized spells (8 spell gem slots)
		if (p.Length() >= base + MEM_SPELLS_OFFSET + SPELL_GEM_COUNT * 4) {
			std::array<uint32_t, 8> mem_spells;
			for (size_t i = 0; i < SPELL_GEM_COUNT; ++i) {
				mem_spells[i] = p.GetUInt32(base + MEM_SPELLS_OFFSET + i * 4);
			}
			m_spell_manager->setSpellGems(mem_spells.data(), SPELL_GEM_COUNT);

			if (s_debug_level >= 1) {
				LOG_DEBUG(MOD_SPELL, "Memorized spells:");
				for (size_t i = 0; i < SPELL_GEM_COUNT; ++i) {
					if (mem_spells[i] != 0xFFFFFFFF && mem_spells[i] != 0) {
						const EQ::SpellData* spell = m_spell_manager->getSpell(mem_spells[i]);
						std::string name = spell ? spell->name : "Unknown";
						LOG_DEBUG(MOD_SPELL, "  Gem {}: {} (ID {})", i + 1, name, mem_spells[i]);
					}
				}
			}
		}
	}

	// Extract player buffs from PlayerProfile
	// Titanium offsets: buffs at 5008, 25 SpellBuff_Struct entries (20 bytes each)
	if (m_buff_manager) {
		constexpr size_t BUFF_OFFSET = 5008;
		constexpr size_t BUFF_COUNT = 25;
		constexpr size_t BUFF_STRUCT_SIZE = 20;

		if (p.Length() >= base + BUFF_OFFSET + BUFF_COUNT * BUFF_STRUCT_SIZE) {
			std::array<EQT::SpellBuff_Struct, 25> buffs;
			for (size_t i = 0; i < BUFF_COUNT; ++i) {
				size_t offset = base + BUFF_OFFSET + i * BUFF_STRUCT_SIZE;
				buffs[i].effect_type = p.GetUInt8(offset + 0);
				buffs[i].level = p.GetUInt8(offset + 1);
				buffs[i].bard_modifier = p.GetUInt8(offset + 2);
				buffs[i].unknown003 = p.GetUInt8(offset + 3);
				buffs[i].spellid = p.GetUInt32(offset + 4);
				buffs[i].duration = p.GetInt32(offset + 8);
				buffs[i].counters = p.GetUInt32(offset + 12);
				buffs[i].player_id = p.GetUInt32(offset + 16);
			}
			m_buff_manager->setPlayerBuffs(buffs.data(), BUFF_COUNT);

			if (s_debug_level >= 1) {
				size_t active_count = 0;
				for (size_t i = 0; i < BUFF_COUNT; ++i) {
					if (buffs[i].spellid != 0 && buffs[i].spellid != 0xFFFFFFFF && buffs[i].effect_type != 0) {
						active_count++;
						if (m_spell_manager) {
							const EQ::SpellData* spell = m_spell_manager->getSpell(buffs[i].spellid);
							std::string name = spell ? spell->name : "Unknown";
							LOG_DEBUG(MOD_SPELL, "  Buff slot {}: {} (ID {}, duration={})",
								i, name, buffs[i].spellid, buffs[i].duration);
						}
					}
				}
				LOG_DEBUG(MOD_SPELL, "Loaded {} active buffs from profile", active_count);
			}
		}
	}

#ifdef EQT_HAS_GRAPHICS
	// Update renderer with character info and stats
	if (m_renderer) {
		// Convert name and class to wstring for UI
		std::wstring wname;
		for (char c : m_character) {
			wname += static_cast<wchar_t>(c);
		}
		std::wstring wclass;
		std::string classStr = GetClassName(m_class);
		for (char c : classStr) {
			wclass += static_cast<wchar_t>(c);
		}
		std::wstring wdeity;
		std::string deityName = GetDeityName(m_deity);
		for (char c : deityName) {
			wdeity += static_cast<wchar_t>(c);
		}

		m_renderer->setCharacterInfo(wname, m_level, wclass);
		m_renderer->setCharacterDeity(wdeity);

		// Calculate stats from equipment and update
		UpdateInventoryStats();
	}
#endif
}

void EverQuest::ZoneProcessCharInventory(const EQ::Net::Packet &p)
{
	LOG_DEBUG(MOD_INVENTORY, "Received character inventory packet, size={}", p.Length());

	if (m_inventory_manager) {
		LOG_TRACE(MOD_INVENTORY, "Processing with inventory manager");
		m_inventory_manager->processCharInventory(p);
	} else {
		LOG_ERROR(MOD_INVENTORY, "No inventory manager!");
	}

#ifdef EQT_HAS_GRAPHICS
	// Update stats after inventory changes
	UpdateInventoryStats();
#endif
}

#ifdef EQT_HAS_GRAPHICS
void EverQuest::ZoneProcessMoveItem(const EQ::Net::Packet &p)
{
	// Server response to our MoveItem request
	// Format: MoveItem_Struct { from_slot, to_slot, number_in_stack }
	if (s_debug_level >= 1) {
		std::cout << "Received MoveItem response" << std::endl;
	}

	if (m_inventory_manager) {
		m_inventory_manager->processMoveItemResponse(p);
	}

	// Refresh sellable items if vendor window is open (items may have moved in/out of sellable slots)
	if (m_renderer && m_renderer->getWindowManager() &&
	    m_renderer->getWindowManager()->isVendorWindowOpen()) {
		m_renderer->getWindowManager()->refreshVendorSellableItems();
	}

	// Update stats after item movement (may affect equipped items)
	UpdateInventoryStats();
}

void EverQuest::ZoneProcessDeleteItem(const EQ::Net::Packet &p)
{
	// Server response to our DeleteItem request
	if (s_debug_level >= 1) {
		std::cout << "Received DeleteItem response" << std::endl;
	}

	if (m_inventory_manager) {
		m_inventory_manager->processDeleteItemResponse(p);
	}

	// Refresh sellable items if vendor window is open (item may have been deleted from sellable slots)
	if (m_renderer && m_renderer->getWindowManager() &&
	    m_renderer->getWindowManager()->isVendorWindowOpen()) {
		m_renderer->getWindowManager()->refreshVendorSellableItems();
	}
}

void EverQuest::SetupInventoryCallbacks()
{
	if (!m_inventory_manager) {
		return;
	}

	// Set callback for when player moves an item
	m_inventory_manager->setMoveItemCallback(
		[this](int16_t fromSlot, int16_t toSlot, uint32_t quantity) {
			SendMoveItem(fromSlot, toSlot, quantity);
		}
	);

	// Set callback for when player destroys an item
	m_inventory_manager->setDeleteItemCallback(
		[this](int16_t slot) {
			SendDeleteItem(slot);
		}
	);
}

void EverQuest::SendMoveItem(int16_t fromSlot, int16_t toSlot, uint32_t quantity)
{
	LOG_DEBUG(MOD_INVENTORY, "SendMoveItem called: fromSlot={} toSlot={} qty={}", fromSlot, toSlot, quantity);

	if (!m_zone_connection) {
		LOG_WARN(MOD_INVENTORY, "No zone connection, not sending MoveItem");
		return;
	}

	// MoveItem_Struct is 12 bytes: from_slot(4), to_slot(4), number_in_stack(4)
	// Packet format: opcode(2) + data(12) = 14 bytes
	//
	// When quantity is 0, the entire item/stack is moved.
	// When quantity > 0, only that many items are split from the stack.
	EQ::Net::DynamicPacket packet;
	packet.Resize(14);

	packet.PutUInt16(0, HC_OP_MoveItem);                   // opcode
	packet.PutUInt32(2, static_cast<uint32_t>(fromSlot));  // from_slot
	packet.PutUInt32(6, static_cast<uint32_t>(toSlot));    // to_slot
	packet.PutUInt32(10, quantity);                        // number_in_stack (0 = move entire item)

	LOG_DEBUG(MOD_INVENTORY, "Sending MoveItem packet: {} -> {} (qty: {})", fromSlot, toSlot, quantity);

	DumpPacket("C->S", HC_OP_MoveItem, packet);
	m_zone_connection->QueuePacket(packet);
}

void EverQuest::SendDeleteItem(int16_t slot)
{
	if (!m_zone_connection) {
		return;
	}

	// DeleteItem_Struct is 12 bytes: from_slot(4), to_slot(4), number_in_stack(4)
	// to_slot is 0xFFFFFFFF for deletion
	// Packet format: opcode(2) + data(12) = 14 bytes
	EQ::Net::DynamicPacket packet;
	packet.Resize(14);

	packet.PutUInt16(0, HC_OP_DeleteItem);                 // opcode
	packet.PutUInt32(2, static_cast<uint32_t>(slot));      // from_slot
	packet.PutUInt32(6, 0xFFFFFFFF);                       // to_slot = -1 (delete)
	packet.PutUInt32(10, 0);                               // number_in_stack

	if (s_debug_level >= 1) {
		std::cout << fmt::format("[Inventory] Sending DeleteItem: slot {}", slot) << std::endl;
	}

	DumpPacket("C->S", HC_OP_DeleteItem, packet);
	m_zone_connection->QueuePacket(packet);
}

void EverQuest::ScribeSpellFromScroll(uint32_t spellId, uint16_t bookSlot, int16_t sourceSlot)
{
	if (!m_zone_connection) {
		LOG_WARN(MOD_SPELL, "No zone connection, cannot scribe spell");
		return;
	}

	if (!m_spell_manager) {
		LOG_WARN(MOD_SPELL, "No spell manager, cannot scribe spell");
		return;
	}

	// Check if spell is already scribed
	if (m_spell_manager->hasSpellScribed(spellId)) {
		AddChatSystemMessage("You already have this spell scribed in your spellbook.");
		LOG_DEBUG(MOD_SPELL, "Spell {} already scribed, not sending packets", spellId);
		return;
	}

	// Check level/class requirements
	const EQ::SpellData* spellData = m_spell_manager->getSpell(spellId);
	if (spellData) {
		uint8_t reqLevel = spellData->getClassLevel(static_cast<EQ::PlayerClass>(m_class));
		if (reqLevel == 255) {
			AddChatSystemMessage("Your class cannot use this spell.");
			LOG_DEBUG(MOD_SPELL, "Class {} cannot use spell {}", m_class, spellId);
			return;
		}
		if (m_level < reqLevel) {
			AddChatSystemMessage(fmt::format("You must be level {} to scribe this spell.", reqLevel));
			LOG_DEBUG(MOD_SPELL, "Level {} too low to scribe spell {} (requires {})",
				m_level, spellId, reqLevel);
			return;
		}
	}

	LOG_INFO(MOD_SPELL, "Scribing spell {} to spellbook slot {} (scroll already on cursor)",
		spellId, bookSlot);

	// Note: The scroll was already moved to cursor slot when Ctrl+clicked
	// (InventoryManager::setSpellForScribe sends MoveItem to cursor)
	// We only need to send OP_MemorizeSpell here

	// Send OP_MemorizeSpell with scribing action
	// MemorizeSpell_Struct:
	// - slot (4 bytes): spellbook slot (0-399) when scribing
	// - spell_id (4 bytes): spell ID
	// - scribing (4 bytes): action type (0=scribe from scroll, 1=memorize, 2=forget, 3=spellbar)
	// - reduction (4 bytes): reuse timer reduction
	{
		EQ::Net::DynamicPacket memPacket;
		memPacket.Resize(18);  // opcode(2) + struct(16)

		constexpr uint32_t SCRIBING_FROM_SCROLL = 0;

		memPacket.PutUInt16(0, HC_OP_MemorizeSpell);
		memPacket.PutUInt32(2, bookSlot);    // spellbook slot (0-399)
		memPacket.PutUInt32(6, spellId);     // spell ID
		memPacket.PutUInt32(10, SCRIBING_FROM_SCROLL);  // scribing action = 0
		memPacket.PutUInt32(14, 0);          // reduction

		LOG_DEBUG(MOD_SPELL, "Sending MemorizeSpell: spell={} book_slot={} scribing=0 (scribe from scroll)",
			spellId, bookSlot);
		DumpPacket("C->S", HC_OP_MemorizeSpell, memPacket);
		m_zone_connection->QueuePacket(memPacket);
	}

	// Store pending scribe state for confirmation handling
	m_pending_scribe_spell_id = spellId;
	m_pending_scribe_book_slot = bookSlot;
	m_pending_scribe_source_slot = sourceSlot;

	AddChatSystemMessage(fmt::format("Scribing {}...",
		spellData ? spellData->name : "spell"));
}

// Trade send functions

void EverQuest::SendTradeRequest(const EQT::TradeRequest_Struct& req)
{
	if (!m_zone_connection) {
		LOG_WARN(MOD_MAIN, "No zone connection, not sending TradeRequest");
		return;
	}

	EQ::Net::DynamicPacket packet;
	packet.Resize(2 + sizeof(EQT::TradeRequest_Struct));
	packet.PutUInt16(0, HC_OP_TradeRequest);
	memcpy(packet.Data() + 2, &req, sizeof(EQT::TradeRequest_Struct));

	DumpPacket("C->S", HC_OP_TradeRequest, packet);
	m_zone_connection->QueuePacket(packet);
}

void EverQuest::SendTradeRequestAck(const EQT::TradeRequestAck_Struct& ack)
{
	if (!m_zone_connection) {
		LOG_WARN(MOD_MAIN, "No zone connection, not sending TradeRequestAck");
		return;
	}

	EQ::Net::DynamicPacket packet;
	packet.Resize(2 + sizeof(EQT::TradeRequestAck_Struct));
	packet.PutUInt16(0, HC_OP_TradeRequestAck);
	memcpy(packet.Data() + 2, &ack, sizeof(EQT::TradeRequestAck_Struct));

	DumpPacket("C->S", HC_OP_TradeRequestAck, packet);
	m_zone_connection->QueuePacket(packet);
}

void EverQuest::SendTradeCoins(const EQT::TradeCoins_Struct& coins)
{
	LOG_DEBUG(MOD_MAIN, "SendTradeCoins called: spawn_id={} slot={} amount={} (m_my_spawn_id={} m_my_character_id={})",
		coins.spawn_id, coins.slot, coins.amount, m_my_spawn_id, m_my_character_id);

	if (!m_zone_connection) {
		LOG_WARN(MOD_MAIN, "No zone connection, not sending TradeCoins");
		return;
	}

	EQ::Net::DynamicPacket packet;
	packet.Resize(2 + sizeof(EQT::TradeCoins_Struct));
	packet.PutUInt16(0, HC_OP_TradeCoins);
	memcpy(packet.Data() + 2, &coins, sizeof(EQT::TradeCoins_Struct));

	LOG_DEBUG(MOD_MAIN, "Queueing TradeCoins packet, size={}", packet.Length());
	DumpPacket("C->S", HC_OP_TradeCoins, packet);
	m_zone_connection->QueuePacket(packet);
}

void EverQuest::SendMoveCoin(const EQT::MoveCoin_Struct& move)
{
	LOG_DEBUG(MOD_MAIN, "SendMoveCoin called: from_slot={} to_slot={} cointype1={} cointype2={} amount={}",
		move.from_slot, move.to_slot, move.cointype1, move.cointype2, move.amount);

	if (!m_zone_connection) {
		LOG_WARN(MOD_MAIN, "No zone connection, not sending MoveCoin");
		return;
	}

	EQ::Net::DynamicPacket packet;
	packet.Resize(2 + sizeof(EQT::MoveCoin_Struct));
	packet.PutUInt16(0, HC_OP_MoveCoin);
	memcpy(packet.Data() + 2, &move, sizeof(EQT::MoveCoin_Struct));

	LOG_DEBUG(MOD_MAIN, "Queueing MoveCoin packet, size={}", packet.Length());
	DumpPacket("C->S", HC_OP_MoveCoin, packet);
	m_zone_connection->QueuePacket(packet);
}

void EverQuest::SendTradeAcceptClick(const EQT::TradeAcceptClick_Struct& accept)
{
	if (!m_zone_connection) {
		LOG_WARN(MOD_MAIN, "No zone connection, not sending TradeAcceptClick");
		return;
	}

	EQ::Net::DynamicPacket packet;
	packet.Resize(2 + sizeof(EQT::TradeAcceptClick_Struct));
	packet.PutUInt16(0, HC_OP_TradeAcceptClick);
	memcpy(packet.Data() + 2, &accept, sizeof(EQT::TradeAcceptClick_Struct));

	DumpPacket("C->S", HC_OP_TradeAcceptClick, packet);
	m_zone_connection->QueuePacket(packet);
}

void EverQuest::SendCancelTrade(const EQT::CancelTrade_Struct& cancel)
{
	if (!m_zone_connection) {
		LOG_WARN(MOD_MAIN, "No zone connection, not sending CancelTrade");
		return;
	}

	EQ::Net::DynamicPacket packet;
	packet.Resize(2 + sizeof(EQT::CancelTrade_Struct));
	packet.PutUInt16(0, HC_OP_CancelTrade);
	memcpy(packet.Data() + 2, &cancel, sizeof(EQT::CancelTrade_Struct));

	DumpPacket("C->S", HC_OP_CancelTrade, packet);
	m_zone_connection->QueuePacket(packet);
}

// Book/Note reading methods

void EverQuest::SendReadBookRequest(uint8_t window, uint8_t type, const std::string& filename)
{
	if (!m_zone_connection) {
		LOG_WARN(MOD_MAIN, "No zone connection, not sending ReadBook request");
		return;
	}

	// Packet format from server titanium_structs.h BookRequest_Struct:
	// Offset 0-1: opcode (2 bytes)
	// Offset 2: window (1 byte) - 0xFF = new window
	// Offset 3: type (1 byte) - 0=scroll, 1=book, 2=item info
	// Offset 4+: txtfile (null-terminated string)
	size_t packetSize = 2 + 2 + filename.length() + 1;  // opcode + window/type + filename + null

	EQ::Net::DynamicPacket packet;
	packet.Resize(packetSize);

	packet.PutUInt16(0, HC_OP_ReadBook);    // opcode
	packet.PutUInt8(2, window);              // window
	packet.PutUInt8(3, type);                // type
	// txtfile at offset 4
	memcpy((char*)packet.Data() + 4, filename.c_str(), filename.length() + 1);

	LOG_DEBUG(MOD_MAIN, "Sending ReadBook request: window={} type={} filename='{}'", window, type, filename);

	DumpPacket("C->S", HC_OP_ReadBook, packet);
	m_zone_connection->QueuePacket(packet);
}

void EverQuest::ZoneProcessReadBook(const EQ::Net::Packet& p)
{
	// Response packet format:
	// Offset 0-1: opcode (2 bytes)
	// Offset 2: window (1 byte)
	// Offset 3: type (1 byte)
	// Offset 4+: booktext (null-terminated string with '^' as newline)

	if (p.Length() < 4) {
		LOG_WARN(MOD_MAIN, "ReadBook response too short: {} bytes", p.Length());
		return;
	}

	uint8_t window = p.GetUInt8(2);
	uint8_t type = p.GetUInt8(3);

	// Extract the book text (starts at offset 4)
	std::string bookText;
	if (p.Length() > 4) {
		bookText = p.GetCString(4);
	}

	LOG_DEBUG(MOD_MAIN, "ReadBook response: window={} type={} textLen={}", window, type, bookText.length());
	LOG_TRACE(MOD_MAIN, "Book text: '{}'", bookText);

#ifdef EQT_HAS_GRAPHICS
	// Show the note window in the renderer
	if (m_renderer) {
		m_renderer->showNoteWindow(bookText, type);
	}
#endif
}

#ifdef EQT_HAS_GRAPHICS
void EverQuest::RequestReadBook(const std::string& filename, uint8_t type)
{
	// Send read book request with new window flag (0xFF)
	SendReadBookRequest(0xFF, type, filename);
}
#endif

bool EverQuest::ZoneProcessTradePartnerItem(const EQ::Net::Packet& p)
{
	// Check if this is a partner trade item (ItemPacketTradeView = 101)
	// Partner items are sent with packet type 101 and slot 0-3 (relative)
	// Returns true if handled as trade item, false if should be processed normally

	if (p.Length() < 10) {
		return false;
	}

	// Get packet type (4 bytes after 2-byte opcode)
	uint32_t packetType = p.GetUInt32(2);

	// Only handle ItemPacketTradeView (101) packets
	if (packetType != EQT::ITEM_PACKET_TRADE_VIEW) {
		return false;
	}

	// Skip opcode (2 bytes) and packet type (4 bytes)
	const char* itemData = reinterpret_cast<const char*>(p.Data()) + 2 + 4;
	size_t itemLen = p.Length() - 2 - 4;

	// Remove trailing nulls
	while (itemLen > 0 && itemData[itemLen - 1] == '\0') {
		itemLen--;
	}

	if (itemLen == 0) {
		return false;
	}

	// Quick parse to get slot ID - it's the first pipe-delimited field
	// For trade view packets, slot is 0-3 (relative)
	int16_t slotId = eqt::inventory::SLOT_INVALID;
	std::string slotStr;
	for (size_t i = 0; i < itemLen && itemData[i] != '|'; i++) {
		slotStr += itemData[i];
	}
	if (!slotStr.empty()) {
		try {
			slotId = static_cast<int16_t>(std::stoi(slotStr));
		} catch (...) {
			return false;
		}
	}

	// Validate slot is 0-7 (partner trade slots)
	if (slotId < 0 || slotId > 7) {
		LOG_WARN(MOD_MAIN, "Invalid partner trade slot: {}", slotId);
		return true;  // Still consumed - don't pass to inventory
	}

	// Parse the full item
	std::map<int16_t, std::unique_ptr<eqt::inventory::ItemInstance>> subItems;
	auto item = eqt::inventory::TitaniumItemParser::parseItem(itemData, itemLen, slotId, &subItems);

	if (!item) {
		LOG_WARN(MOD_MAIN, "Failed to parse trade partner item for slot {}", slotId);
		return true;  // Still consumed - don't pass to inventory
	}

	LOG_INFO(MOD_MAIN, "Received trade partner item: {} in slot {}", item->name, slotId);

	// Show chat message about the offered item
	if (m_trade_manager) {
		std::string partnerName = m_trade_manager->getPartnerName();
		std::string itemName = item->name;
		AddChatSystemMessage(fmt::format("{} has offered you a {}.",
			EQT::toDisplayName(partnerName), itemName));
	}

	// Pass to trade manager
	if (m_trade_manager) {
		m_trade_manager->handlePartnerItemPacket(slotId, std::move(item));
	}

	return true;  // Handled as trade item
}

void EverQuest::SetupTradeManagerCallbacks()
{
	if (!m_trade_manager) {
		return;
	}

	// Set up network callbacks
	m_trade_manager->setSendTradeRequest([this](const EQT::TradeRequest_Struct& req) {
		SendTradeRequest(req);
	});

	m_trade_manager->setSendTradeRequestAck([this](const EQT::TradeRequestAck_Struct& ack) {
		SendTradeRequestAck(ack);
	});

	m_trade_manager->setSendMoveCoin([this](const EQT::MoveCoin_Struct& move) {
		SendMoveCoin(move);
	});

	m_trade_manager->setSendTradeAcceptClick([this](const EQT::TradeAcceptClick_Struct& accept) {
		SendTradeAcceptClick(accept);
	});

	m_trade_manager->setSendCancelTrade([this](const EQT::CancelTrade_Struct& cancel) {
		SendCancelTrade(cancel);
	});

#ifdef EQT_HAS_GRAPHICS
	// Set up UI callbacks
	m_trade_manager->setOnRequestReceived([this](uint32_t spawnId, const std::string& name) {
		if (m_renderer && m_renderer->getWindowManager()) {
			m_renderer->getWindowManager()->showTradeRequest(spawnId, name);
		}
	});

	m_trade_manager->setOnStateChanged([this](TradeState state) {
		if (!m_renderer || !m_renderer->getWindowManager()) {
			return;
		}
		auto* wm = m_renderer->getWindowManager();
		if (state == TradeState::Active) {
			wm->openTradeWindow(m_trade_manager->getPartnerSpawnId(),
			                    m_trade_manager->getPartnerName(),
			                    m_trade_manager->isNpcTrade());
		} else if (state == TradeState::None) {
			// Only close on None state (cancel) - Completed is handled by m_on_completed
			wm->closeTradeWindow();
		}
	});

	m_trade_manager->setOnItemUpdated([this](bool isOwn, int slot) {
		if (!m_renderer || !m_renderer->getWindowManager()) {
			return;
		}
		auto* wm = m_renderer->getWindowManager();
		if (!isOwn) {
			// Partner item updated - get from trade manager
			const auto* item = m_trade_manager->getPartnerItem(slot);
			if (item) {
				// Make a copy for the UI
				auto itemCopy = std::make_unique<eqt::inventory::ItemInstance>(*item);
				wm->setTradePartnerItem(slot, std::move(itemCopy));
			} else {
				wm->clearTradePartnerItem(slot);
			}
		}
		// Own items are displayed from inventory trade slots
	});

	m_trade_manager->setOnMoneyUpdated([this](bool isOwn) {
		if (!m_renderer || !m_renderer->getWindowManager()) {
			return;
		}
		auto* wm = m_renderer->getWindowManager();
		if (isOwn) {
			wm->setTradeOwnMoney(m_trade_manager->getOwnMoney().platinum,
			                     m_trade_manager->getOwnMoney().gold,
			                     m_trade_manager->getOwnMoney().silver,
			                     m_trade_manager->getOwnMoney().copper);
		} else {
			wm->setTradePartnerMoney(m_trade_manager->getPartnerMoney().platinum,
			                         m_trade_manager->getPartnerMoney().gold,
			                         m_trade_manager->getPartnerMoney().silver,
			                         m_trade_manager->getPartnerMoney().copper);
		}
	});

	m_trade_manager->setOnAcceptStateChanged([this](bool ownAccepted, bool partnerAccepted) {
		if (m_renderer && m_renderer->getWindowManager()) {
			m_renderer->getWindowManager()->setTradeOwnAccepted(ownAccepted);
			m_renderer->getWindowManager()->setTradePartnerAccepted(partnerAccepted);
		}
	});

	m_trade_manager->setOnCompleted([this]() {
		// Generate chat messages for what was exchanged
		std::string partnerName = EQT::toDisplayName(m_trade_manager->getPartnerName());
		const auto& partnerMoney = m_trade_manager->getPartnerMoney();
		const auto& ownMoney = m_trade_manager->getOwnMoney();

		// Report money received from partner
		if (partnerMoney.platinum > 0) {
			AddChatSystemMessage(fmt::format("You receive {} platinum from {}.", partnerMoney.platinum, partnerName));
		}
		if (partnerMoney.gold > 0) {
			AddChatSystemMessage(fmt::format("You receive {} gold from {}.", partnerMoney.gold, partnerName));
		}
		if (partnerMoney.silver > 0) {
			AddChatSystemMessage(fmt::format("You receive {} silver from {}.", partnerMoney.silver, partnerName));
		}
		if (partnerMoney.copper > 0) {
			AddChatSystemMessage(fmt::format("You receive {} copper from {}.", partnerMoney.copper, partnerName));
		}

		// Report items received from partner
		for (int i = 0; i < 8; i++) {
			const auto* item = m_trade_manager->getPartnerItem(i);
			if (item) {
				AddChatSystemMessage(fmt::format("You receive {} from {}.", item->name, partnerName));
			}
		}

		// Report money given to partner
		if (ownMoney.platinum > 0) {
			AddChatSystemMessage(fmt::format("You give {} platinum to {}.", ownMoney.platinum, partnerName));
		}
		if (ownMoney.gold > 0) {
			AddChatSystemMessage(fmt::format("You give {} gold to {}.", ownMoney.gold, partnerName));
		}
		if (ownMoney.silver > 0) {
			AddChatSystemMessage(fmt::format("You give {} silver to {}.", ownMoney.silver, partnerName));
		}
		if (ownMoney.copper > 0) {
			AddChatSystemMessage(fmt::format("You give {} copper to {}.", ownMoney.copper, partnerName));
		}

		// Report items given to partner
		for (int i = 0; i < 8; i++) {
			const auto* item = m_trade_manager->getOwnItem(i);
			if (item) {
				AddChatSystemMessage(fmt::format("You give {} to {}.", item->name, partnerName));
			}
		}

		if (m_renderer && m_renderer->getWindowManager()) {
			m_renderer->getWindowManager()->closeTradeWindow(false);  // Don't send cancel on completion
		}
	});

	m_trade_manager->setOnCancelled([this]() {
		AddChatSystemMessage("Trade cancelled.");
		if (m_renderer && m_renderer->getWindowManager()) {
			m_renderer->getWindowManager()->closeTradeWindow(true);  // Send cancel
		}
	});
#endif
}

void EverQuest::ZoneProcessLootItemToUI(const EQ::Net::Packet &p)
{
	LOG_DEBUG(MOD_INVENTORY, "ZoneProcessLootItemToUI called, packet length={}", p.Length());

	// Parse the serialized item data for the loot window
	// The packet contains pipe-delimited item data
	if (p.Length() < 10) {
		LOG_DEBUG(MOD_INVENTORY, "ZoneProcessLootItemToUI: Packet too short, ignoring");
		return;
	}

	// Skip the first 2 bytes (opcode is already removed, but there's a header)
	const char* data = (const char*)p.Data() + 2;
	int data_len = p.Length() - 2;

	// Skip the first 4 bytes of the data (unknown header)
	if (data_len > 4) {
		data += 4;
		data_len -= 4;
	}

	std::string item_data(data, data_len);

	if (s_debug_level >= 3) {
		LOG_TRACE(MOD_INVENTORY, "Item data length={}", item_data.length());
		LOG_TRACE(MOD_INVENTORY, "Item data preview: {}", item_data.substr(0, std::min(size_t(200), item_data.length())));
	}

	// Parse the pipe-delimited fields to extract item info
	std::vector<std::string> fields;
	std::stringstream ss(item_data);
	std::string field;
	while (std::getline(ss, field, '|')) {
		fields.push_back(field);
	}

	if (s_debug_level >= 3) {
		LOG_TRACE(MOD_INVENTORY, "Parsed {} fields", fields.size());
		for (size_t i = 0; i < fields.size() && i < 20; i++) {
			LOG_TRACE(MOD_INVENTORY, "  field[{}] = '{}'", i, fields[i].substr(0, 50));
		}
	}

	// The format is: 1|0|slot|0|1|0|icon|0|0|0|0|"0|item_name|lore_name|model|item_id|...
	// field[2] = slot number
	// field[6] = icon ID
	// field[12] = item name
	if (fields.size() > 15) {
		try {
			int16_t slot_num = static_cast<int16_t>(std::stoi(fields[2]));  // Slot number in loot window
			std::string item_name = fields[12];         // Item name (was incorrectly field[11])

			LOG_DEBUG(MOD_INVENTORY, "Creating loot item: slot={} name='{}'", slot_num, item_name);

			// Create an ItemInstance for the loot window
			if (m_renderer && m_renderer->getWindowManager() && m_inventory_manager) {
				auto item = std::make_unique<eqt::inventory::ItemInstance>();
				item->name = item_name;

				// Parse icon from static data field[11] (absolute field[22])
				// Loot packet format: 11 instance fields (0-10), then static data starting at field[11]
				// Static field indices: [0]=ItemClass, [1]=name, ..., [11]=icon
				try {
					if (fields.size() > 22) {
						item->icon = static_cast<uint32_t>(std::stoi(fields[22]));
						LOG_TRACE(MOD_INVENTORY, "  icon={} (from field[22])", item->icon);
					}
				} catch (...) {
					LOG_DEBUG(MOD_INVENTORY, "  icon parsing failed for field[22]='{}'",
						(fields.size() > 22 ? fields[22] : "N/A"));
				}

				// Parse item ID from static data field[4] (absolute field[15])
				try {
					item->itemId = static_cast<uint32_t>(std::stoi(fields[15]));
					LOG_TRACE(MOD_INVENTORY, "  itemId={} (from field[15])", item->itemId);
				} catch (...) {}

				// Parse stackSize from static data field[131] (absolute field[142])
				// and stackable from static data field[133] (absolute field[144])
				try {
					if (fields.size() > 144) {
						item->stackSize = std::stoi(fields[142]);
						item->stackable = std::stoi(fields[144]) != 0;
						LOG_TRACE(MOD_INVENTORY, "  stackSize={} stackable={} (from fields[142], [144])",
							item->stackSize, item->stackable);
					}
				} catch (...) {
					LOG_DEBUG(MOD_INVENTORY, "  stackSize/stackable parsing failed");
				}

				m_renderer->getWindowManager()->addLootItem(slot_num, std::move(item));
			} else {
				LOG_WARN(MOD_INVENTORY, "Cannot add loot item: renderer={} windowManager={} invManager={}",
					(m_renderer ? "ok" : "null"),
					(m_renderer && m_renderer->getWindowManager() ? "ok" : "null"),
					(m_inventory_manager ? "ok" : "null"));
			}
		} catch (const std::exception& e) {
			LOG_WARN(MOD_INVENTORY, "Failed to parse loot item data: {}", e.what());
		}
	} else {
		LOG_DEBUG(MOD_INVENTORY, "Not enough fields ({}), need > 15", fields.size());
	}
}

void EverQuest::ZoneProcessLootedItemToInventory(const EQ::Net::Packet &p)
{
	LOG_DEBUG(MOD_INVENTORY, "ZoneProcessLootedItemToInventory called, pending_slots={}", m_pending_loot_slots.size());

	// This is called when an item is being looted from a corpse to inventory
	// The item packet format is the same as ZoneProcessLootItemToUI

	if (m_pending_loot_slots.empty()) {
		// No pending loot - shouldn't happen, but handle gracefully
		LOG_DEBUG(MOD_INVENTORY, "ZoneProcessLootedItemToInventory: No pending loot slots! Treating as inventory item.");
		if (m_inventory_manager) {
			m_inventory_manager->processItemPacket(p);
			// Refresh sellable items if vendor window is open
			if (m_renderer && m_renderer->getWindowManager() &&
			    m_renderer->getWindowManager()->isVendorWindowOpen()) {
				m_renderer->getWindowManager()->refreshVendorSellableItems();
			}
		}
		return;
	}

	// Get the slot we're expecting to receive
	int16_t expected_slot = m_pending_loot_slots.front();
	m_pending_loot_slots.erase(m_pending_loot_slots.begin());

	LOG_DEBUG(MOD_INVENTORY, "ZoneProcessLootedItemToInventory: Processing looted item for corpse slot {}, remaining pending={}",
		expected_slot, m_pending_loot_slots.size());

	// Add item to player's inventory
	if (m_inventory_manager) {
		LOG_TRACE(MOD_INVENTORY, "Adding item to inventory via processItemPacket");
		m_inventory_manager->processItemPacket(p);
	}

	// Remove the item from the loot window and refresh sellable items
	if (m_renderer && m_renderer->getWindowManager()) {
		m_renderer->getWindowManager()->removeLootItem(expected_slot);
		LOG_TRACE(MOD_INVENTORY, "Removed item from loot window slot {}", expected_slot);

		// Refresh sellable items if vendor window is open
		if (m_renderer->getWindowManager()->isVendorWindowOpen()) {
			m_renderer->getWindowManager()->refreshVendorSellableItems();
		}
	}

	// Continue loot-all operation if there are more items to loot
	if (m_loot_all_in_progress && !m_loot_all_remaining_slots.empty() && m_player_looting_corpse_id != 0) {
		uint16_t corpseId = m_player_looting_corpse_id;
		int16_t nextSlot = m_loot_all_remaining_slots.front();
		m_loot_all_remaining_slots.erase(m_loot_all_remaining_slots.begin());
		LOG_DEBUG(MOD_INVENTORY, "LootAll continuation: next slot={} remaining={}", nextSlot, m_loot_all_remaining_slots.size());
		LootItemFromCorpse(corpseId, nextSlot, true);
	} else if (m_loot_all_in_progress && m_loot_all_remaining_slots.empty()) {
		// Finished looting all items
		LOG_DEBUG(MOD_INVENTORY, "LootAll complete, all items looted");
		m_loot_all_in_progress = false;
	}
}

void EverQuest::SetupLootCallbacks()
{
	if (!m_renderer || !m_renderer->getWindowManager()) {
		return;
	}

	auto* windowManager = m_renderer->getWindowManager();

	// Set callback for when player clicks Loot on a single item
	windowManager->setOnLootItem([this](uint16_t corpseId, int16_t slot) {
		LootItemFromCorpse(corpseId, slot);
	});

	// Set callback for when player clicks Loot All
	windowManager->setOnLootAll([this](uint16_t corpseId) {
		LootAllFromCorpse(corpseId);
	});

	// Set callback for when player clicks Destroy All
	windowManager->setOnDestroyAll([this](uint16_t corpseId) {
		DestroyAllCorpseLoot(corpseId);
	});

	// Set callback for when player closes the loot window
	windowManager->setOnLootClose([this](uint16_t corpseId) {
		CloseLootWindow(corpseId);
	});

	if (s_debug_level >= 2) {
		LOG_DEBUG(MOD_INVENTORY, "Loot callbacks set up");
	}
}

void EverQuest::SetupVendorCallbacks()
{
	if (!m_renderer || !m_renderer->getWindowManager()) {
		return;
	}

	auto* windowManager = m_renderer->getWindowManager();

	// Set callback for when player clicks Buy on an item
	windowManager->setOnVendorBuy([this](uint16_t npcId, uint32_t itemSlot, uint32_t quantity) {
		BuyFromVendor(npcId, itemSlot, quantity);
	});

	// Set callback for when player clicks Sell on an item
	windowManager->setOnVendorSell([this](uint16_t npcId, uint32_t itemSlot, uint32_t quantity) {
		SellToVendor(npcId, itemSlot, quantity);
	});

	// Set callback for when player closes the vendor window
	windowManager->setOnVendorClose([this](uint16_t npcId) {
		CloseVendorWindow();
	});

	if (s_debug_level >= 2) {
		LOG_DEBUG(MOD_INVENTORY, "Vendor callbacks set up");
	}
}

void EverQuest::SetupBankCallbacks()
{
	if (!m_renderer || !m_renderer->getWindowManager()) {
		return;
	}

	auto* windowManager = m_renderer->getWindowManager();

	// Set callback for when player closes the bank window
	windowManager->setOnBankClose([this]() {
		CloseBankWindow();
	});

	// Set callback for currency movement between bank and inventory
	windowManager->setOnBankCurrencyMove([this, windowManager](int32_t coinType, int32_t amount, bool fromBank) {
		// Build and send MoveCoin packet
		EQT::MoveCoin_Struct move;
		if (fromBank) {
			// Moving from bank to inventory
			move.from_slot = EQT::COINSLOT_BANK;
			move.to_slot = EQT::COINSLOT_INVENTORY;
		} else {
			// Moving from inventory to bank
			move.from_slot = EQT::COINSLOT_INVENTORY;
			move.to_slot = EQT::COINSLOT_BANK;
		}
		move.cointype1 = coinType;
		move.cointype2 = coinType;
		move.amount = amount;

		SendMoveCoin(move);

		// Update local currency values (server will confirm, but update locally for responsiveness)
		uint32_t* srcPlatinum = fromBank ? &m_bank_platinum : &m_platinum;
		uint32_t* srcGold = fromBank ? &m_bank_gold : &m_gold;
		uint32_t* srcSilver = fromBank ? &m_bank_silver : &m_silver;
		uint32_t* srcCopper = fromBank ? &m_bank_copper : &m_copper;

		uint32_t* dstPlatinum = fromBank ? &m_platinum : &m_bank_platinum;
		uint32_t* dstGold = fromBank ? &m_gold : &m_bank_gold;
		uint32_t* dstSilver = fromBank ? &m_silver : &m_bank_silver;
		uint32_t* dstCopper = fromBank ? &m_copper : &m_bank_copper;

		switch (coinType) {
			case EQT::COINTYPE_PP:
				if (*srcPlatinum >= static_cast<uint32_t>(amount)) {
					*srcPlatinum -= amount;
					*dstPlatinum += amount;
				}
				break;
			case EQT::COINTYPE_GP:
				if (*srcGold >= static_cast<uint32_t>(amount)) {
					*srcGold -= amount;
					*dstGold += amount;
				}
				break;
			case EQT::COINTYPE_SP:
				if (*srcSilver >= static_cast<uint32_t>(amount)) {
					*srcSilver -= amount;
					*dstSilver += amount;
				}
				break;
			case EQT::COINTYPE_CP:
				if (*srcCopper >= static_cast<uint32_t>(amount)) {
					*srcCopper -= amount;
					*dstCopper += amount;
				}
				break;
		}

		// Update UI displays
		windowManager->updateBaseCurrency(m_platinum, m_gold, m_silver, m_copper);
		windowManager->updateBankCurrency(m_bank_platinum, m_bank_gold, m_bank_silver, m_bank_copper);

		LOG_DEBUG(MOD_INVENTORY, "Bank currency move: type={} amount={} fromBank={}, bank now: {}pp {}gp {}sp {}cp, inv now: {}pp {}gp {}sp {}cp",
			coinType, amount, fromBank,
			m_bank_platinum, m_bank_gold, m_bank_silver, m_bank_copper,
			m_platinum, m_gold, m_silver, m_copper);
	});

	// Set callback for currency conversion in bank (cp->sp->gp->pp)
	windowManager->setOnBankCurrencyConvert([this, windowManager](int32_t fromCoinType, int32_t amount) {
		// Conversion is always 10:1 ratio: 10 cp -> 1 sp, 10 sp -> 1 gp, 10 gp -> 1 pp
		// fromCoinType: 0=copper, 1=silver, 2=gold (platinum can't be converted further)
		// amount: number of source coins to convert (must be multiple of 10)

		if (amount < 10 || (amount % 10) != 0) {
			LOG_WARN(MOD_INVENTORY, "Invalid conversion amount: {} (must be multiple of 10)", amount);
			return;
		}

		// Determine destination coin type (one tier higher)
		int32_t toCoinType = fromCoinType + 1;
		if (toCoinType > EQT::COINTYPE_PP) {
			LOG_WARN(MOD_INVENTORY, "Cannot convert platinum further");
			return;
		}

		// Build and send MoveCoin packet for conversion
		// Server handles conversion automatically when cointype1 != cointype2
		EQT::MoveCoin_Struct move;
		move.from_slot = EQT::COINSLOT_BANK;
		move.to_slot = EQT::COINSLOT_BANK;
		move.cointype1 = fromCoinType;   // Source coin type
		move.cointype2 = toCoinType;     // Destination coin type (one tier higher)
		move.amount = amount;

		SendMoveCoin(move);

		LOG_DEBUG(MOD_INVENTORY, "Sent bank currency conversion: {} {} -> {} (cointype {} -> {})",
			amount, (fromCoinType == EQT::COINTYPE_CP ? "copper" :
			         fromCoinType == EQT::COINTYPE_SP ? "silver" : "gold"),
			(toCoinType == EQT::COINTYPE_SP ? "silver" :
			 toCoinType == EQT::COINTYPE_GP ? "gold" : "platinum"),
			fromCoinType, toCoinType);

		// Update local values for UI responsiveness (server will confirm)
		uint32_t convertedAmount = amount / 10;
		switch (fromCoinType) {
			case EQT::COINTYPE_CP:  // Copper -> Silver
				if (m_bank_copper >= static_cast<uint32_t>(amount)) {
					m_bank_copper -= amount;
					m_bank_silver += convertedAmount;
				}
				break;
			case EQT::COINTYPE_SP:  // Silver -> Gold
				if (m_bank_silver >= static_cast<uint32_t>(amount)) {
					m_bank_silver -= amount;
					m_bank_gold += convertedAmount;
				}
				break;
			case EQT::COINTYPE_GP:  // Gold -> Platinum
				if (m_bank_gold >= static_cast<uint32_t>(amount)) {
					m_bank_gold -= amount;
					m_bank_platinum += convertedAmount;
				}
				break;
		}

		// Update UI
		windowManager->updateBankCurrency(m_bank_platinum, m_bank_gold, m_bank_silver, m_bank_copper);

		LOG_DEBUG(MOD_INVENTORY, "Bank currency after conversion: {}pp {}gp {}sp {}cp",
			m_bank_platinum, m_bank_gold, m_bank_silver, m_bank_copper);
	});

	if (s_debug_level >= 2) {
		LOG_DEBUG(MOD_INVENTORY, "Bank callbacks set up");
	}
}

void EverQuest::SetupTradeWindowCallbacks()
{
	if (!m_renderer || !m_renderer->getWindowManager()) {
		return;
	}

	auto* windowManager = m_renderer->getWindowManager();

	// Initialize trade window with trade manager
	windowManager->initTradeWindow(m_trade_manager.get());

	// Set callback for when player clicks Accept in trade window
	windowManager->setOnTradeAccept([this]() {
		if (m_trade_manager) {
			m_trade_manager->clickAccept();
		}
	});

	// Set callback for when player clicks Cancel in trade window
	windowManager->setOnTradeCancel([this]() {
		if (m_trade_manager) {
			m_trade_manager->cancelTrade();
		}
	});

	// Set callback for when player accepts trade request
	windowManager->setOnTradeRequestAccept([this]() {
		if (m_trade_manager) {
			m_trade_manager->acceptTradeRequest();
		}
	});

	// Set callback for when player declines trade request
	windowManager->setOnTradeRequestDecline([this]() {
		if (m_trade_manager) {
			m_trade_manager->rejectTradeRequest();
		}
	});

	// Set callback for trade error messages
	windowManager->setOnTradeError([this](const std::string& message) {
		AddChatSystemMessage(message);
	});

	LOG_DEBUG(MOD_MAIN, "Trade window callbacks set up");
}

void EverQuest::SetupTradeskillCallbacks()
{
	auto* windowManager = m_renderer ? m_renderer->getWindowManager() : nullptr;
	if (!windowManager) {
		LOG_WARN(MOD_MAIN, "Cannot set up tradeskill callbacks - window manager not available");
		return;
	}

	// Set callback for when player clicks Combine in tradeskill container
	windowManager->setOnTradeskillCombine([this]() {
		// Get the tradeskill window to determine if it's a world container or inventory container
		auto* windowManager = m_renderer ? m_renderer->getWindowManager() : nullptr;
		if (!windowManager) return;

		auto* tradeskillWindow = windowManager->getTradeskillContainerWindow();
		if (!tradeskillWindow || !tradeskillWindow->isOpen()) return;

		if (tradeskillWindow->isWorldContainer()) {
			// For world containers, send combine to the world container slot
			SendTradeSkillCombine(eqt::inventory::SLOT_TRADESKILL_EXPERIMENT_COMBINE);
			LOG_DEBUG(MOD_INVENTORY, "Sent tradeskill combine for world container");
		} else {
			// For inventory containers, send combine to the container's slot
			int16_t containerSlot = tradeskillWindow->getContainerSlot();
			SendTradeSkillCombine(containerSlot);
			LOG_DEBUG(MOD_INVENTORY, "Sent tradeskill combine for inventory container at slot {}", containerSlot);
		}
	});

	// Set callback for when tradeskill container window is closed
	windowManager->setOnTradeskillClose([this]() {
		auto* windowManager = m_renderer ? m_renderer->getWindowManager() : nullptr;
		if (!windowManager) return;

		auto* tradeskillWindow = windowManager->getTradeskillContainerWindow();
		if (!tradeskillWindow) return;

		if (tradeskillWindow->isWorldContainer()) {
			// For world containers, notify server that we're closing
			uint32_t dropId = tradeskillWindow->getWorldObjectId();
			if (dropId != 0) {
				SendCloseContainer(dropId);
				LOG_DEBUG(MOD_INVENTORY, "Sent close container for world object dropId={}", dropId);
			}
			m_active_tradeskill_object_id = 0;
		}
		// For inventory containers, nothing special needed - items are already in inventory
	});

	LOG_DEBUG(MOD_MAIN, "Tradeskill container callbacks set up");
}

void EverQuest::ZoneProcessShopRequest(const EQ::Net::Packet &p)
{
	// Server response to our shop request
	// Merchant_Open_Struct: npc_id(4), unknown04(4), action(4), sell_rate(4)
	if (p.Length() < 18) {
		LOG_WARN(MOD_INVENTORY, "ShopRequest packet too short: {} bytes", p.Length());
		return;
	}

	uint32_t npc_id = p.GetUInt32(2);
	uint32_t action = p.GetUInt32(10);
	float sell_rate = 1.0f;
	// Read sell_rate as float from bytes 14-17
	if (p.Length() >= 18) {
		uint32_t sell_rate_bits = p.GetUInt32(14);
		memcpy(&sell_rate, &sell_rate_bits, sizeof(float));
	}

	LOG_DEBUG(MOD_INVENTORY, "ShopRequest response: npc_id={} action={} sell_rate={:.4f}", npc_id, action, sell_rate);

	if (action == 1) {
		// Success - vendor is open
		m_vendor_npc_id = static_cast<uint16_t>(npc_id);
		m_vendor_sell_rate = sell_rate;

		// Get vendor name from entity if available (converted to display format)
		auto it = m_entities.find(m_vendor_npc_id);
		if (it != m_entities.end()) {
			m_vendor_name = EQT::toDisplayName(it->second.name);
		} else {
			m_vendor_name = "Merchant";
		}

		// Open the vendor window
		if (m_renderer && m_renderer->getWindowManager()) {
			m_renderer->getWindowManager()->openVendorWindow(m_vendor_npc_id, m_vendor_name, m_vendor_sell_rate);

			// Update player money in vendor window
			int32_t pp = static_cast<int32_t>(GetPlatinum());
			int32_t gp = static_cast<int32_t>(GetGold());
			int32_t sp = static_cast<int32_t>(GetSilver());
			int32_t cp = static_cast<int32_t>(GetCopper());
			m_renderer->getWindowManager()->getVendorWindow()->setPlayerMoney(pp, gp, sp, cp);
		}

		LOG_INFO(MOD_INVENTORY, "Opened vendor window for {} (id={})", m_vendor_name, m_vendor_npc_id);
	} else {
		LOG_WARN(MOD_INVENTORY, "Vendor open failed: action={}", action);
	}
}

void EverQuest::ZoneProcessShopPlayerBuy(const EQ::Net::Packet &p)
{
	// Server confirmation of purchase
	// Merchant_Purchase_Struct: npc_id(4), player_id(4), itemslot(4), unknown12(4), quantity(4), action(4)
	if (p.Length() < 26) {
		LOG_WARN(MOD_INVENTORY, "ShopPlayerBuy packet too short: {} bytes", p.Length());
		return;
	}

	uint32_t npc_id = p.GetUInt32(2);
	uint32_t player_id = p.GetUInt32(6);
	uint32_t itemslot = p.GetUInt32(10);
	uint32_t quantity = p.GetUInt32(18);
	uint32_t action = p.GetUInt32(22);

	LOG_DEBUG(MOD_INVENTORY, "ShopPlayerBuy: npc_id={} player_id={} slot={} qty={} action={}",
		npc_id, player_id, itemslot, quantity, action);

	// Display purchase confirmation in chat and update money
#ifdef EQT_HAS_GRAPHICS
	if (m_renderer && m_renderer->getWindowManager()) {
		auto* vendorWindow = m_renderer->getWindowManager()->getVendorWindow();
		if (vendorWindow && vendorWindow->isOpen()) {
			const auto* item = vendorWindow->getItem(itemslot);
			if (item) {
				int32_t price = vendorWindow->getItemPrice(itemslot);
				int32_t totalPrice = price * static_cast<int32_t>(quantity);
				std::string priceStr = vendorWindow->formatPrice(totalPrice);
				if (quantity > 1) {
					AddChatSystemMessage(fmt::format("You purchased {} x{} for {}", item->name, quantity, priceStr));
				} else {
					AddChatSystemMessage(fmt::format("You purchased {} for {}", item->name, priceStr));
				}

				// Deduct money from local currency (server confirms purchase = money spent)
				int64_t totalCopper = static_cast<int64_t>(m_platinum) * 1000 +
				                      static_cast<int64_t>(m_gold) * 100 +
				                      static_cast<int64_t>(m_silver) * 10 +
				                      static_cast<int64_t>(m_copper);
				totalCopper -= totalPrice;
				if (totalCopper < 0) totalCopper = 0;

				// Convert back to currency
				m_platinum = static_cast<uint32_t>(totalCopper / 1000);
				totalCopper %= 1000;
				m_gold = static_cast<uint32_t>(totalCopper / 100);
				totalCopper %= 100;
				m_silver = static_cast<uint32_t>(totalCopper / 10);
				m_copper = static_cast<uint32_t>(totalCopper % 10);

				// Update vendor window with new money
				vendorWindow->setPlayerMoney(m_platinum, m_gold, m_silver, m_copper);
				LOG_DEBUG(MOD_INVENTORY, "Updated money: {}pp {}gp {}sp {}cp", m_platinum, m_gold, m_silver, m_copper);
			}
		}
	}
#endif

	// ItemPacket with type 103 (inventory) handles the actual item placement
	// and stack quantity updates via InventoryManager::setItem()
}

void EverQuest::ZoneProcessShopPlayerSell(const EQ::Net::Packet &p)
{
	// Server confirmation of sale
	// Merchant_Sell_Response_Struct: npc_id(4), itemslot(4), quantity(4), price(4)
	if (p.Length() < sizeof(EQT::Merchant_Sell_Response_Struct) + 2) {
		LOG_WARN(MOD_INVENTORY, "ShopPlayerSell packet too short: {} bytes", p.Length());
		return;
	}

	auto* resp = reinterpret_cast<const EQT::Merchant_Sell_Response_Struct*>(
		static_cast<const char*>(p.Data()) + 2);

	LOG_INFO(MOD_INVENTORY, "Sold {} items from slot {} for {} copper",
		resp->quantity, resp->itemslot, resp->price);

#ifdef EQT_HAS_GRAPHICS
	if (m_renderer && m_renderer->getWindowManager()) {
		auto* vendorWindow = m_renderer->getWindowManager()->getVendorWindow();
		if (vendorWindow && vendorWindow->isOpen()) {
			// Format and display sale confirmation
			std::string priceStr = vendorWindow->formatPrice(static_cast<int32_t>(resp->price));
			if (resp->quantity > 1) {
				AddChatSystemMessage(fmt::format("You sold {} items for {}", resp->quantity, priceStr));
			} else {
				AddChatSystemMessage(fmt::format("You sold item for {}", priceStr));
			}

			// Remove item from local inventory
			int16_t invSlot = static_cast<int16_t>(resp->itemslot);
			if (m_inventory_manager) {
				const auto* item = m_inventory_manager->getItem(invSlot);
				if (item) {
					if (item->stackable && item->quantity > static_cast<int32_t>(resp->quantity)) {
						// Reduce stack quantity
						auto* mutableItem = m_inventory_manager->getItemMutable(invSlot);
						if (mutableItem) {
							mutableItem->quantity -= static_cast<int32_t>(resp->quantity);
							LOG_DEBUG(MOD_INVENTORY, "Reduced stack at slot {} to {} items",
								invSlot, mutableItem->quantity);
						}
					} else {
						// Remove entire item
						m_inventory_manager->removeItem(invSlot);
						LOG_DEBUG(MOD_INVENTORY, "Removed item from slot {}", invSlot);
					}
				}
			}

			// Refresh sellable items list
			m_renderer->getWindowManager()->refreshVendorSellableItems();
		}
	}
#endif
}

void EverQuest::ZoneProcessMoneyUpdate(const EQ::Net::Packet &p)
{
	// Money update after transaction
	// MoneyUpdate_Struct: platinum(4), gold(4), silver(4), copper(4)
	if (p.Length() < sizeof(EQT::MoneyUpdate_Struct) + 2) {
		LOG_WARN(MOD_INVENTORY, "MoneyUpdate packet too short: {} bytes", p.Length());
		return;
	}

	auto* money = reinterpret_cast<const EQT::MoneyUpdate_Struct*>(
		static_cast<const char*>(p.Data()) + 2);

	// Update player money
	m_platinum = static_cast<uint32_t>(money->platinum);
	m_gold = static_cast<uint32_t>(money->gold);
	m_silver = static_cast<uint32_t>(money->silver);
	m_copper = static_cast<uint32_t>(money->copper);

	LOG_DEBUG(MOD_INVENTORY, "Money updated: {}pp {}gp {}sp {}cp",
		m_platinum, m_gold, m_silver, m_copper);

#ifdef EQT_HAS_GRAPHICS
	if (m_renderer && m_renderer->getWindowManager()) {
		// Update base currency in window manager (for inventory display)
		m_renderer->getWindowManager()->updateBaseCurrency(
			m_platinum, m_gold, m_silver, m_copper);

		// Update vendor window money display
		auto* vendorWindow = m_renderer->getWindowManager()->getVendorWindow();
		if (vendorWindow && vendorWindow->isOpen()) {
			vendorWindow->setPlayerMoney(
				static_cast<int32_t>(m_platinum),
				static_cast<int32_t>(m_gold),
				static_cast<int32_t>(m_silver),
				static_cast<int32_t>(m_copper));
		}

		// Update trainer window money display
		if (m_renderer->getWindowManager()->isSkillTrainerWindowOpen()) {
			m_renderer->getWindowManager()->updateSkillTrainerMoney(
				m_platinum, m_gold, m_silver, m_copper);
		}
	}
#endif
}

void EverQuest::ZoneProcessShopEndConfirm(const EQ::Net::Packet &p)
{
	// Server confirms vendor session ended
	LOG_DEBUG(MOD_INVENTORY, "ShopEndConfirm received, closing vendor window");

	// Close the vendor window
	if (m_renderer && m_renderer->getWindowManager()) {
		m_renderer->getWindowManager()->closeVendorWindow();
	}

	// Clear vendor state
	m_vendor_npc_id = 0;
	m_vendor_sell_rate = 1.0f;
	m_vendor_name.clear();
}

void EverQuest::ZoneProcessVendorItemToUI(const EQ::Net::Packet &p)
{
	LOG_DEBUG(MOD_INVENTORY, "ZoneProcessVendorItemToUI called, packet length={}", p.Length());

	// Parse the serialized item data for the vendor window using TitaniumItemParser
	if (p.Length() < 10) {
		LOG_DEBUG(MOD_INVENTORY, "ZoneProcessVendorItemToUI: Packet too short, ignoring");
		return;
	}

	// Skip the first 2 bytes (opcode header) and 4 bytes (item packet type = 100)
	const char* data = (const char*)p.Data() + 2 + 4;
	size_t data_len = p.Length() - 2 - 4;

	if (data_len == 0) {
		LOG_DEBUG(MOD_INVENTORY, "ZoneProcessVendorItemToUI: No item data");
		return;
	}

	// Use TitaniumItemParser to properly parse the item data
	int16_t slotId = eqt::inventory::SLOT_INVALID;
	auto item = eqt::inventory::TitaniumItemParser::parseItem(data, data_len, slotId);

	if (!item) {
		LOG_WARN(MOD_INVENTORY, "Failed to parse vendor item");
		return;
	}

	// slotId from parser is the vendor slot number
	uint32_t vendorSlot = static_cast<uint32_t>(slotId);
	LOG_DEBUG(MOD_INVENTORY, "Creating vendor item: slot={} name='{}'", vendorSlot, item->name);

	// Add to vendor window
	if (m_renderer && m_renderer->getWindowManager()) {
		m_renderer->getWindowManager()->addVendorItem(vendorSlot, std::move(item));
	}
}

void EverQuest::RequestOpenVendor(uint16_t npcId)
{
	if (m_vendor_npc_id != 0) {
		LOG_DEBUG(MOD_INVENTORY, "Already in vendor session with NPC {}", m_vendor_npc_id);
		return;
	}

	LOG_DEBUG(MOD_INVENTORY, "Requesting vendor open for NPC {}", npcId);

	// Send Merchant_Click_Struct
	EQ::Net::DynamicPacket pkt;
	EQT::Merchant_Click_Struct req;
	memset(&req, 0, sizeof(req));
	req.npc_id = npcId;
	req.player_id = m_my_spawn_id;
	pkt.PutData(0, &req, sizeof(req));
	QueuePacket(HC_OP_ShopRequest, &pkt);
}

void EverQuest::BuyFromVendor(uint16_t npcId, uint32_t itemSlot, uint32_t quantity)
{
	if (m_vendor_npc_id == 0 || m_vendor_npc_id != npcId) {
		LOG_WARN(MOD_INVENTORY, "BuyFromVendor: Not in vendor session with NPC {}", npcId);
		return;
	}

	LOG_DEBUG(MOD_INVENTORY, "Buying from vendor {}: slot={} quantity={}", npcId, itemSlot, quantity);

	// Send Merchant_Purchase_Struct
	EQ::Net::DynamicPacket pkt;
	EQT::Merchant_Purchase_Struct buy;
	buy.npc_id = npcId;
	buy.player_id = m_my_spawn_id;
	buy.itemslot = itemSlot;
	buy.unknown12 = 0;
	buy.quantity = quantity;
	buy.action = EQT::MERCHANT_BUY;
	pkt.PutData(0, &buy, sizeof(buy));
	QueuePacket(HC_OP_ShopPlayerBuy, &pkt);
}

void EverQuest::SellToVendor(uint16_t npcId, uint32_t itemSlot, uint32_t quantity)
{
	if (m_vendor_npc_id == 0 || m_vendor_npc_id != npcId) {
		LOG_WARN(MOD_INVENTORY, "SellToVendor: Not in vendor session with NPC {}", npcId);
		AddChatSystemMessage("You are not interacting with a vendor.");
		return;
	}

	// Validate the item exists and can be sold
	if (m_inventory_manager) {
		const auto* item = m_inventory_manager->getItem(static_cast<int16_t>(itemSlot));
		if (!item) {
			LOG_WARN(MOD_INVENTORY, "SellToVendor: No item at slot {}", itemSlot);
			AddChatSystemMessage("That item is no longer in your inventory.");
			return;
		}
		if (item->noDrop) {
			LOG_WARN(MOD_INVENTORY, "SellToVendor: Item '{}' is NO_DROP", item->name);
			AddChatSystemMessage(fmt::format("You cannot sell {}.", item->name));
			return;
		}
		if (item->stackable && static_cast<int32_t>(quantity) > item->quantity) {
			LOG_WARN(MOD_INVENTORY, "SellToVendor: Requested quantity {} exceeds stack size {}",
				quantity, item->quantity);
			quantity = static_cast<uint32_t>(item->quantity);
		}
	}

	LOG_DEBUG(MOD_INVENTORY, "Selling to vendor {}: slot={} quantity={}", npcId, itemSlot, quantity);

	// Send Merchant_Sell_Struct
	EQ::Net::DynamicPacket pkt;
	EQT::Merchant_Sell_Struct sell;
	sell.npc_id = npcId;
	sell.itemslot = itemSlot;
	sell.quantity = quantity;
	sell.unknown12 = 0;  // Server ignores this field
	pkt.PutData(0, &sell, sizeof(sell));
	QueuePacket(HC_OP_ShopPlayerSell, &pkt);
}

void EverQuest::CloseVendorWindow()
{
	if (m_vendor_npc_id == 0) {
		return;
	}

	LOG_DEBUG(MOD_INVENTORY, "Closing vendor window for NPC {}", m_vendor_npc_id);

	// Send Merchant_End_Struct
	EQ::Net::DynamicPacket pkt;
	EQT::Merchant_End_Struct end;
	end.npc_id = m_vendor_npc_id;
	end.player_id = m_my_spawn_id;
	pkt.PutData(0, &end, sizeof(end));
	QueuePacket(HC_OP_ShopEnd, &pkt);

	// Close the UI immediately (don't wait for server confirmation)
	if (m_renderer && m_renderer->getWindowManager()) {
		m_renderer->getWindowManager()->closeVendorWindow();
	}

	// Clear vendor state
	m_vendor_npc_id = 0;
	m_vendor_sell_rate = 1.0f;
	m_vendor_name.clear();
}

void EverQuest::OpenBankWindow(uint16_t bankerNpcId)
{
	if (m_banker_npc_id != 0) {
		// Already have bank open, close it first
		CloseBankWindow();
	}

	// Use a dummy NPC ID if none provided (for /bank command testing)
	// In real use, this would be the banker NPC's spawn ID
	m_banker_npc_id = (bankerNpcId != 0) ? bankerNpcId : 1;

	LOG_DEBUG(MOD_INVENTORY, "Opening bank window (banker NPC: {})", m_banker_npc_id);

	// Open the bank window UI
	if (m_renderer && m_renderer->getWindowManager()) {
		// Update bank currency before opening window
		m_renderer->getWindowManager()->updateBankCurrency(
			m_bank_platinum, m_bank_gold, m_bank_silver, m_bank_copper);
		m_renderer->getWindowManager()->openBankWindow();
	}

	AddChatSystemMessage("Bank window opened");
}

void EverQuest::CloseBankWindow()
{
	if (m_banker_npc_id == 0) {
		return;
	}

	LOG_DEBUG(MOD_INVENTORY, "Closing bank window (banker NPC: {})", m_banker_npc_id);

	// Close the bank window UI and all bank bag windows
	if (m_renderer && m_renderer->getWindowManager()) {
		m_renderer->getWindowManager()->closeBankWindow();
	}

	// Clear bank state
	m_banker_npc_id = 0;

	AddChatSystemMessage("Bank window closed");
}
// ============================================================================
// Skill Trainer Window Methods
// ============================================================================

void EverQuest::SetupTrainerCallbacks()
{
	if (!m_renderer || !m_renderer->getWindowManager()) {
		return;
	}

	auto* windowManager = m_renderer->getWindowManager();

	// Set callback for when player clicks Train button
	windowManager->setSkillTrainCallback([this](uint8_t skillId) {
		TrainSkill(skillId);
	});

	// Set callback for when player closes the trainer window
	windowManager->setTrainerCloseCallback([this]() {
		CloseTrainerWindow();
	});

	if (s_debug_level >= 2) {
		LOG_DEBUG(MOD_MAIN, "Trainer callbacks set up");
	}
}

void EverQuest::ZoneProcessGMTraining(const EQ::Net::Packet& p)
{
	// Server response to our training request
	// GMTrainee_Struct: npcid(4), playerid(4), skills[100](400), unknown408[40](40) = 448 bytes
	if (p.Length() < sizeof(EQT::GMTrainee_Struct) + 2) {
		LOG_WARN(MOD_MAIN, "GMTraining packet too short: {} bytes", p.Length());
		return;
	}

	const auto* trainee = reinterpret_cast<const EQT::GMTrainee_Struct*>(
		static_cast<const char*>(p.Data()) + 2);

	uint32_t npc_id = trainee->npcid;
	uint32_t player_id = trainee->playerid;

	LOG_DEBUG(MOD_MAIN, "GMTraining response: npc_id={} player_id={}", npc_id, player_id);

	// Store trainer state
	m_trainer_npc_id = static_cast<uint16_t>(npc_id);

	// Get trainer name from entity if available
	auto it = m_entities.find(m_trainer_npc_id);
	if (it != m_entities.end()) {
		m_trainer_name = EQT::toDisplayName(it->second.name);
	} else {
		m_trainer_name = "Trainer";
	}

	// Build list of trainable skills
	std::vector<eqt::ui::TrainerSkillEntry> skillEntries;

	for (uint8_t skill_id = 0; skill_id < EQT::MAX_PP_SKILL; ++skill_id) {
		uint32_t max_trainable = trainee->skills[skill_id];

		// Skip skills with max_trainable = 0 (not trainable at this trainer)
		if (max_trainable == 0) {
			continue;
		}

		// Get current skill value
		uint32_t current_value = 0;
		if (m_skill_manager) {
			current_value = m_skill_manager->getSkillValue(skill_id);
		}

		// Skip skills already at or above max
		if (current_value >= max_trainable) {
			continue;
		}

		// Get skill name
		const char* skill_name = EQ::getSkillName(skill_id);
		std::wstring name_wstr(skill_name, skill_name + strlen(skill_name));

		// Calculate training cost
		// EQ training cost formula: (current_value + 1) * 10 copper per point
		// For simplicity, we'll use the cost for training one point
		uint32_t cost = (current_value + 1) * 10;

		eqt::ui::TrainerSkillEntry entry;
		entry.skill_id = skill_id;
		entry.name = name_wstr;
		entry.current_value = current_value;
		entry.max_trainable = max_trainable;
		entry.cost = cost;

		skillEntries.push_back(entry);

		if (s_debug_level >= 2) {
			LOG_DEBUG(MOD_MAIN, "  Skill {}: {} cur={} max={} cost={}",
				skill_id, skill_name, current_value, max_trainable, cost);
		}
	}

	LOG_INFO(MOD_MAIN, "Trainer window opened for {} (id={}) with {} trainable skills",
		m_trainer_name, m_trainer_npc_id, skillEntries.size());

	// Open the trainer window
	if (m_renderer && m_renderer->getWindowManager()) {
		std::wstring trainer_name_wstr(m_trainer_name.begin(), m_trainer_name.end());
		m_renderer->getWindowManager()->openSkillTrainerWindow(
			m_trainer_npc_id, trainer_name_wstr, skillEntries);

		// Update player money in trainer window
		m_renderer->getWindowManager()->updateSkillTrainerMoney(
			GetPlatinum(), GetGold(), GetSilver(), GetCopper());

		// Update practice points (free training sessions)
		m_renderer->getWindowManager()->updateSkillTrainerPracticePoints(
			GetPracticePoints());
	}
}

void EverQuest::RequestTrainerWindow(uint16_t npcId)
{
	if (m_trainer_npc_id != 0) {
		LOG_DEBUG(MOD_MAIN, "Already in trainer session with NPC {}", m_trainer_npc_id);
		return;
	}

	LOG_DEBUG(MOD_MAIN, "Requesting trainer window for NPC {}", npcId);

	// Send GMTrainee_Struct
	EQ::Net::DynamicPacket pkt;
	EQT::GMTrainee_Struct req;
	memset(&req, 0, sizeof(req));
	req.npcid = npcId;
	req.playerid = m_my_spawn_id;
	pkt.PutData(0, &req, sizeof(req));
	QueuePacket(HC_OP_GMTraining, &pkt);
}

void EverQuest::TrainSkill(uint8_t skillId)
{
	if (m_trainer_npc_id == 0) {
		LOG_WARN(MOD_MAIN, "TrainSkill: Not in trainer session");
		AddChatSystemMessage("You are not interacting with a trainer.");
		return;
	}

	LOG_DEBUG(MOD_MAIN, "Training skill {} with trainer {}", skillId, m_trainer_npc_id);

	// Send GMSkillChange_Struct
	EQ::Net::DynamicPacket pkt;
	EQT::GMSkillChange_Struct train;
	memset(&train, 0, sizeof(train));
	train.npcid = static_cast<uint16_t>(m_trainer_npc_id);
	train.skillbank = 0;  // 0 = normal skills, 1 = languages
	train.skill_id = skillId;
	pkt.PutData(0, &train, sizeof(train));
	QueuePacket(HC_OP_GMTrainSkill, &pkt);
}

void EverQuest::CloseTrainerWindow()
{
	if (m_trainer_npc_id == 0) {
		return;
	}

	LOG_DEBUG(MOD_MAIN, "Closing trainer window for NPC {}", m_trainer_npc_id);

	// Send GMTrainEnd_Struct
	EQ::Net::DynamicPacket pkt;
	EQT::GMTrainEnd_Struct end;
	end.npcid = m_trainer_npc_id;
	end.playerid = m_my_spawn_id;
	pkt.PutData(0, &end, sizeof(end));
	QueuePacket(HC_OP_GMEndTraining, &pkt);

	// Close the UI immediately (don't wait for server confirmation)
	if (m_renderer && m_renderer->getWindowManager()) {
		m_renderer->getWindowManager()->closeSkillTrainerWindow();
	}

	// Clear trainer state
	m_trainer_npc_id = 0;
	m_trainer_name.clear();
}
#endif

void EverQuest::ZoneProcessZoneSpawns(const EQ::Net::Packet &p)
{
	// ZoneSpawns contains multiple Spawn_Struct entries
	// Based on titanium_structs.h, Spawn_Struct is 385 bytes
	
	if (s_debug_level >= 2) {
		LOG_DEBUG(MOD_ENTITY, "Received zone spawns packet, size: {} bytes", p.Length());
	}
	
	// Don't clear existing entities - ZoneSpawns packets can come in multiple batches
	// Only add/update spawns, don't remove existing ones
	
	// The ZoneSpawns packet appears to have a header before the spawn data
	size_t offset = 2;
	int spawn_count = 0;
	
	/*	
	// Debug: Dump the beginning of the packet to understand structure
	if (s_debug_level >= 1) {
		std::cout << "First 50 bytes of ZoneSpawns packet:" << std::endl;
		for (int i = 0; i < 50 && i < p.Length(); i++) {
			std::cout << fmt::format("{:02x} ", p.GetUInt8(i));
			if ((i + 1) % 16 == 0) std::cout << std::endl;
		}
		std::cout << std::endl;
		
		// Let's analyze the hex dump more carefully:
		// At offset 9: "Renux_Herkanor000" - but this is the middle of a name
		// The name field in Spawn_Struct is at offset 7
		// If "Renux_Herkanor000" is at packet offset 9, and name is at struct offset 7,
		// then the spawn struct starts at packet offset 9 - 7 = 2
		
		// But wait - we're seeing truncated names like "e_rat005" instead of "a_large_rat005"
		// This means we're starting too late. Let's check different offsets
		
		// First, let's look for a complete name pattern
		for (int test_offset = 0; test_offset < 20 && test_offset + 70 < p.Length(); test_offset++) {
			char c = (char)p.GetUInt8(test_offset);
			if (c >= 'A' && c <= 'Z' || c >= 'a' && c <= 'z') {
				std::string test_name = p.GetCString(test_offset);
				if (test_name.length() > 3 && test_name.length() < 64) {
					std::cout << fmt::format("  Possible name at offset {}: '{}'", test_offset, test_name) << std::endl;
				}
			}
		}
		
		// If we're getting "e_rat005" instead of "a_large_rat005", we're missing 6 characters
		// Current offset is 8, name field is at +7, so we're reading from packet offset 15
		// We need to start 6 bytes earlier: 8 - 6 = 2
		offset = 2;
		
		// Calculate expected spawn count based on packet size
		size_t data_size = p.Length() - offset;
		size_t expected_spawns = data_size / 385;
		std::cout << fmt::format("Packet has {} bytes of data, expecting approximately {} spawns", 
			data_size, expected_spawns) << std::endl;
	}
	*/

	// Parse Spawn_Struct entries based on titanium_structs.h
	while (offset + 385 <= p.Length()) { // Spawn_Struct is 385 bytes
		Entity entity;
		
		// Based on Spawn_Struct from titanium_structs.h:
		// name is at offset 7 (64 bytes)
		entity.name = p.GetCString(offset + 7);
		
		// If name is empty, we've likely reached the end of spawn data
		if (entity.name.empty()) {
			if (s_debug_level >= 2) {
				std::cout << fmt::format("Found empty name at offset {}, ending spawn parsing", offset) << std::endl;
			}
			break;
		}
		
		// spawnId is at offset 340 in the struct (uint32)
		entity.spawn_id = p.GetUInt32(offset + 340);
		
		// Debug: Check what's around the spawn_id location for first few spawns
		if (spawn_count < 3 && ShouldLog(MOD_ENTITY, LOG_TRACE)) {
			LOG_TRACE(MOD_ENTITY, "Spawn at offset {}: Name='{}', checking spawn_id area:", offset, entity.name);
			for (int i = 330; i < 350 && offset + i + 4 < p.Length(); i += 4) {
				uint32_t val = p.GetUInt32(offset + i);
				if (val > 0 && val < 100000) {
					LOG_TRACE(MOD_ENTITY, "  Offset +{}: u32={} ({:#010x})", i, val, val);
				}
			}
		}
		
		// level is at offset 151
		entity.level = p.GetUInt8(offset + 151);
		
		// Position data from the bitfield structure:
		// x is at offset 94 (19 bits within a bitfield)
		// y is at offset 98 (19 bits)
		// z is at offset 102 (19 bits)
		// heading is at offset 106 (12 bits)
		
		// Read the bitfields
		uint32_t field1 = p.GetUInt32(offset + 94);  // contains deltaHeading and x
		uint32_t field2 = p.GetUInt32(offset + 98);  // contains y and animation
		uint32_t field3 = p.GetUInt32(offset + 102); // contains z and deltaY
		uint32_t field4 = p.GetUInt32(offset + 106); // contains deltaX and heading
		
		// Extract x, y, z (19 bits each, signed)
		// Positions are stored as fixed-point values (multiplied by 8)
		// Note: Server x/y are swapped relative to zone geometry, so we swap them here
		int32_t x_raw = (field1 >> 10) & 0x7FFFF;
		if (x_raw & 0x40000) x_raw |= 0xFFF80000; // Sign extend
		float server_x = static_cast<float>(x_raw) / 8.0f;

		int32_t y_raw = field2 & 0x7FFFF;
		if (y_raw & 0x40000) y_raw |= 0xFFF80000; // Sign extend
		float server_y = static_cast<float>(y_raw) / 8.0f;

		int32_t z_raw = field3 & 0x7FFFF;
		if (z_raw & 0x40000) z_raw |= 0xFFF80000; // Sign extend
		entity.z = static_cast<float>(z_raw) / 8.0f;

		// Swap x and y from server
		entity.x = server_y;
		entity.y = server_x;

		// Read npc_type early - needed for heading transformation
		entity.npc_type = p.GetUInt8(offset + 83);
		bool isNPC = (entity.npc_type == 1 || entity.npc_type == 3);

		// Extract heading (11 bits to match C->S format)
		// Server uses 512 EQ units = 360 degrees, so multiply by 360/512
		uint32_t raw_heading = (field4 >> 13) & 0x7FF;
		float server_heading = (static_cast<float>(raw_heading) / 4.0f) * 360.0f / 512.0f;
		// Use heading directly for all entities (NPCs and players)
		entity.heading = server_heading;
		if (entity.heading >= 360.0f) entity.heading -= 360.0f;

		// Debug logging for current player at level 1+
		if (s_debug_level >= 1 && entity.name == m_character) {
			LOG_DEBUG(MOD_MOVEMENT, "POS S->C ZoneSpawns [SELF] spawn_id={} name='{}'", entity.spawn_id, entity.name);
			LOG_DEBUG(MOD_MOVEMENT, "POS S->C ZoneSpawns [SELF] raw_heading={} -> server_heading={:.2f}deg -> entity.heading={:.2f}deg (isNPC={})",
				raw_heading, server_heading, entity.heading, isNPC);
			LOG_DEBUG(MOD_MOVEMENT, "POS S->C ZoneSpawns [SELF] server_pos=({:.2f},{:.2f},{:.2f}) -> entity_pos=({:.2f},{:.2f},{:.2f})",
				server_x, server_y, entity.z, entity.x, entity.y, entity.z);
		}

		// race is at offset 284 (uint32)
		entity.race_id = p.GetUInt32(offset + 284);

		// class_ is at offset 331
		entity.class_id = p.GetUInt8(offset + 331);

		// gender is at offset 334
		entity.gender = p.GetUInt8(offset + 334);

		// guildID is at offset 238
		entity.guild_id = p.GetUInt32(offset + 238);

		// curHp is at offset 86 (hp percentage)
		entity.hp_percent = p.GetUInt8(offset + 86);

		// animation is in the bitfield at offset 98
		entity.animation = (field2 >> 19) & 0x3FF;

		// size is at offset 75 (float)
		entity.size = *reinterpret_cast<const float*>(static_cast<const char*>(p.Data()) + offset + 75);

		// Appearance data from Spawn_Struct
		entity.face = p.GetUInt8(offset + 6);           // Face/head variant
		// npc_type already read above for heading transformation
		entity.haircolor = p.GetUInt8(offset + 85);     // Hair color
		entity.showhelm = p.GetUInt8(offset + 139);     // Show helm
		entity.hairstyle = p.GetUInt8(offset + 145);    // Hair style
		entity.beardcolor = p.GetUInt8(offset + 146);   // Beard color
		entity.beard = p.GetUInt8(offset + 156);        // Beard style
		entity.helm = p.GetUInt8(offset + 275);         // Helm texture
		entity.light = p.GetUInt8(offset + 330);        // Light source
		entity.bodytype = p.GetUInt8(offset + 335);     // Body type
		entity.equip_chest2 = p.GetUInt8(offset + 339); // Body texture variant

		// Equipment textures (TextureProfile at offset 197, 9 slots * 4 bytes)
		for (int i = 0; i < 9; i++) {
			entity.equipment[i] = p.GetUInt32(offset + 197 + i * 4);
		}

		// Equipment tint colors (TintProfile at offset 348, 9 slots * 4 bytes)
		for (int i = 0; i < 9; i++) {
			entity.equipment_tint[i] = p.GetUInt32(offset + 348 + i * 4);
		}

		// Initialize movement fields
		entity.delta_x = 0;
		entity.delta_y = 0;
		entity.delta_z = 0;
		entity.delta_heading = 0;
		entity.last_update_time = std::time(nullptr);

		// Validate the spawn data before adding
		if (entity.spawn_id > 0 && entity.spawn_id < 100000 && !entity.name.empty()) {
			// Check if this is our own character and update our position
			if (entity.name == m_character) {
				m_my_spawn_id = entity.spawn_id;
				if (m_trade_manager) {
					m_trade_manager->setMySpawnId(m_my_spawn_id);
				}

				// Debug: Zone-in position tracking at level 1
				if (s_debug_level >= 1) {
					LOG_INFO(MOD_ZONE, "[ZONE-IN] Found our spawn in ZoneSpawns: ID={} Name='{}'", m_my_spawn_id, m_character);
					LOG_INFO(MOD_ZONE, "[ZONE-IN] Spawn entity: pos=({:.2f},{:.2f},{:.2f}) heading={:.2f} size={:.2f}",
					         entity.x, entity.y, entity.z, entity.heading, entity.size);
					LOG_INFO(MOD_ZONE, "[ZONE-IN] Previous client pos: ({:.2f},{:.2f},{:.2f}) heading={:.2f}",
					         m_x, m_y, m_z, m_heading);
				}

				if (s_debug_level >= 2) {
					LOG_DEBUG(MOD_ENTITY, "=== FOUND OUR CHARACTER ===");
					LOG_DEBUG(MOD_ENTITY, "Name: '{}' Spawn ID: {}", m_character, m_my_spawn_id);
					LOG_DEBUG(MOD_ENTITY, "Position: x={:.2f} y={:.2f} z={:.2f} heading={:.2f}",
						entity.x, entity.y, entity.z, entity.heading);
					LOG_DEBUG(MOD_ENTITY, "Size: {:.2f} (THIS IS THE ACTUAL SIZE)", entity.size);
					LOG_DEBUG(MOD_ENTITY, "Previous m_z={:.2f}, updating to entity.z={:.2f}", m_z, entity.z);
					LOG_DEBUG(MOD_ENTITY, "Previous m_size={:.2f}, updating to entity.size={:.2f}", m_size, entity.size);
					LOG_DEBUG(MOD_ENTITY, "==============================");
				}
#ifdef EQT_HAS_GRAPHICS
				// Notify renderer of player spawn ID
				if (m_graphics_initialized && m_renderer) {
					if (s_debug_level >= 1) {
						LOG_INFO(MOD_ZONE, "[ZONE-IN] Calling setPlayerSpawnId({}) from ZoneSpawns", m_my_spawn_id);
					}
					m_renderer->setPlayerSpawnId(m_my_spawn_id);
				}
#endif
				// Update our position to match server spawn position
				m_x = entity.x;
				m_y = entity.y;
				m_size = entity.size;
				// Server Z is model center. Convert to feet position for internal use.
				// SendPositionUpdate will add m_size/2 to convert back to model center.
				m_z = entity.z - (entity.size / 2.0f);
				// Don't update heading from spawn data as it may be scaled differently

				if (s_debug_level >= 1) {
					LOG_INFO(MOD_ZONE, "[ZONE-IN] Updated client pos from spawn: ({:.2f},{:.2f},{:.2f}) (feet Z, server Z was {:.2f}) heading unchanged={:.2f}",
					         m_x, m_y, m_z, entity.z, m_heading);
				}

#ifdef EQT_HAS_GRAPHICS
				// Update renderer with corrected feet Z position
				if (m_renderer) {
					float heading512 = m_heading * 512.0f / 360.0f;
					m_renderer->setPlayerPosition(m_x, m_y, m_z, heading512);
				}
#endif
			}
			
			// Add to entity list
			m_entities[entity.spawn_id] = entity;
			spawn_count++;

#ifdef EQT_HAS_GRAPHICS
			OnSpawnAddedGraphics(entity);
#endif

			if (spawn_count <= 5) {
				LOG_DEBUG(MOD_ENTITY, "Loaded spawn {}: {} (ID: {}) Level {} Race {} Size {:.2f} at ({:.2f}, {:.2f}, {:.2f})",
					spawn_count, entity.name, entity.spawn_id, entity.level,
					entity.race_id, entity.size, entity.x, entity.y, entity.z);
			}
			// Log all spawns with position and heading at debug level 3+
			LOG_WARN(MOD_ENTITY, "ZoneSpawn: {} (ID:{}) pos=({:.2f},{:.2f},{:.2f}) heading={:.2f} npc_type={}",
				entity.name, entity.spawn_id, entity.x, entity.y, entity.z, entity.heading, entity.npc_type);
		} else {
			LOG_DEBUG(MOD_ENTITY, "Skipping invalid spawn at offset {}: ID={}, Name='{}'",
				offset, entity.spawn_id, entity.name);
		}
		
		// Move to next spawn struct
		offset += 385;
	}
	
	LOG_INFO(MOD_ZONE, "Loaded {} spawns in zone", spawn_count);
}

void EverQuest::ZoneProcessTimeOfDay(const EQ::Net::Packet &p)
{
	m_time_hour = p.GetUInt8(2);
	m_time_minute = p.GetUInt8(3);
	m_time_day = p.GetUInt8(4);
	m_time_month = p.GetUInt8(5);
	m_time_year = p.GetUInt16(6);

	LOG_DEBUG(MOD_ZONE, "Time of day: {:02d}:{:02d} {:02d}/{:02d}{}",
		m_time_hour, m_time_minute, m_time_day, m_time_month, m_time_year);
}

void EverQuest::ZoneProcessSpawnDoor(const EQ::Net::Packet &p)
{
	// Packet format: opcode(2) + array of Door_Struct(80)
	// No count field - calculate from packet size
	if (p.Length() < 2) {
		LOG_WARN(MOD_ENTITY, "SpawnDoor packet too small: {} bytes", p.Length());
		return;
	}

	size_t data_size = p.Length() - 2;  // Subtract opcode
	if (data_size % sizeof(EQT::Door_Struct) != 0) {
		LOG_WARN(MOD_ENTITY, "SpawnDoor packet size {} not divisible by Door_Struct size {}",
		         data_size, sizeof(EQT::Door_Struct));
		// Try to parse what we can
	}

	uint32_t count = static_cast<uint32_t>(data_size / sizeof(EQT::Door_Struct));
	LOG_INFO(MOD_ENTITY, "Received {} doors ({} bytes)", count, p.Length());

	// Parse each door
	for (uint32_t i = 0; i < count; ++i) {
		size_t offset = 2 + i * sizeof(EQT::Door_Struct);  // Start after opcode
		const uint8_t* data = static_cast<const uint8_t*>(p.Data()) + offset;
		const EQT::Door_Struct* door_data = reinterpret_cast<const EQT::Door_Struct*>(data);

		Door door;
		door.door_id = door_data->doorId;

		// Extract name (null-terminated, max 32 chars)
		char name_buf[33] = {0};
		std::memcpy(name_buf, door_data->name, 32);
		door.name = name_buf;

		// Position - swap x and y from server (same as entity spawns)
		door.x = door_data->yPos;
		door.y = door_data->xPos;
		door.z = door_data->zPos;
		door.heading = door_data->heading;
		door.incline = door_data->incline;
		door.size = door_data->size;
		door.opentype = door_data->opentype;
		door.state = door_data->state_at_spawn;
		door.invert_state = (door_data->invert_state != 0);
		door.door_param = door_data->door_param;

		// Store in door map
		m_doors[door.door_id] = door;

		// Debug: dump raw bytes at critical offsets (60-68)
		LOG_DEBUG(MOD_ENTITY, "Door {} raw bytes @60-67: {:02x} {:02x} {:02x} {:02x} {:02x} {:02x} {:02x} {:02x}",
		         i, data[60], data[61], data[62], data[63], data[64], data[65], data[66], data[67]);

		LOG_DEBUG(MOD_ENTITY, "Door {}: '{}' at ({:.1f}, {:.1f}, {:.1f}) heading={:.1f} incline={} size={} type={} state={} invert={}",
		         door.door_id, door.name, door.x, door.y, door.z, door.heading,
		         door.incline, door.size, door.opentype, door.state, door.invert_state ? 1 : 0);

#ifdef EQT_HAS_GRAPHICS
		// Notify renderer to create door visual
		// state_at_spawn: 0=closed, 1=open
		// invert_state: if true, the meaning of state is inverted
		// XOR logic: initiallyOpen = (state != 0) XOR invert_state
		bool initiallyOpen = (door.state != 0) != door.invert_state;
		if (m_renderer) {
			m_renderer->createDoor(door.door_id, door.name, door.x, door.y, door.z,
			                       door.heading, door.incline, door.size, door.opentype,
			                       initiallyOpen);
		}
#endif
	}
}

void EverQuest::ZoneProcessSendZonepoints(const EQ::Net::Packet &p)
{
	// Packet format: opcode(2) + count(4) + count * ZonePoint_Entry(24)
	// Minimum size: 2 + 4 = 6 bytes
	if (p.Length() < 6) {
		return;
	}

	uint32_t count = p.GetUInt32(2);

	LOG_DEBUG(MOD_ZONE, "Received zone points: {} entries", count);

	// Parse zone points and store them in ZoneLines
	if (m_zone_lines && count > 0) {
		std::vector<EQT::ZonePoint> zonePoints;
		zonePoints.reserve(count);

		// Each ZonePoint_Entry is 24 bytes, starting at offset 6
		const size_t entrySize = 24;
		const size_t dataStart = 6;

		for (uint32_t i = 0; i < count; ++i) {
			size_t offset = dataStart + i * entrySize;
			if (offset + entrySize > p.Length()) {
				break;
			}

			EQT::ZonePoint zp;
			zp.number = p.GetUInt32(offset);       // iterator/zone point ID
			zp.targetY = p.GetFloat(offset + 4);   // y
			zp.targetX = p.GetFloat(offset + 8);   // x
			zp.targetZ = p.GetFloat(offset + 12);  // z
			zp.heading = p.GetFloat(offset + 16);  // heading
			zp.targetZoneId = p.GetUInt16(offset + 20);  // zoneid
			// zoneinstance at offset + 22 is ignored for now

			zonePoints.push_back(zp);

			if (s_debug_level >= 3) {
				LOG_TRACE(MOD_ZONE, "  Zone point {}: zone={} pos=({},{},{}) heading={}",
					zp.number, zp.targetZoneId, zp.targetX, zp.targetY, zp.targetZ, zp.heading);
			}
		}

		m_zone_lines->setServerZonePoints(zonePoints);

		LOG_DEBUG(MOD_ZONE, "Stored {} zone points in ZoneLines", zonePoints.size());
	}

	// After zone points, check if we need to send server filter
	// This is old logic - server filter is now sent after GuildMOTD
	// But keeping it here for compatibility if the flow is different
	if (m_zone_entry_sent && !m_client_ready_sent) {
		// This path shouldn't normally be taken anymore
		if (!m_server_filter_sent && m_send_exp_zonein_received) {
			if (s_debug_level >= 1) {
				std::cout << "ZoneProcessSendZonepoints calling ZoneSendSetServerFilter (fallback)" << std::endl;
			}
			ZoneSendSetServerFilter();
		}

		// Send client ready - This shouldn't happen here anymore
		if (!m_client_ready_sent && m_server_filter_sent) {
			std::cout << "ZoneProcessSendZonepoints calling ZoneSendClientReady" << std::endl;
			ZoneSendClientReady();
		}
	}
}

void EverQuest::ZoneProcessSpawnAppearance(const EQ::Net::Packet &p)
{
	// SpawnAppearance_Struct: spawn_id(2), type(2), parameter(4) = 8 bytes + 2 opcode = 10 bytes
	if (p.Length() < 10) {
		return;
	}

	uint16_t spawn_id = p.GetUInt16(2);
	uint16_t type = p.GetUInt16(4);
	uint32_t parameter = p.GetUInt32(6);

	if (s_debug_level >= 2 || IsTrackedTarget(spawn_id)) {
		std::cout << fmt::format("[SpawnAppearance] spawn_id={}, type={}, parameter={}",
			spawn_id, type, parameter) << std::endl;
	}

	switch (type) {
	case AT_ANIMATION:
		// Animation update - set the entity's animation
		{
			auto it = m_entities.find(spawn_id);
			if (it != m_entities.end()) {
				it->second.animation = static_cast<uint8_t>(parameter);
			}

#ifdef EQT_HAS_GRAPHICS
			if (m_graphics_initialized && m_renderer) {
				// Map animation parameter to EQ model animation code
				std::string animCode;
				bool loop = true;
				bool playThrough = false;

				// Zone server animation values (100+) are pose/state animations
				// Lower values are action animations
				EQT::Graphics::IrrlichtRenderer::EntityPoseState poseState =
					EQT::Graphics::IrrlichtRenderer::EntityPoseState::Standing;
				bool setPose = false;

				switch (parameter) {
				case ANIM_STANDING:
				case ANIM_STAND:
					animCode = "o01";  // Idle/standing
					poseState = EQT::Graphics::IrrlichtRenderer::EntityPoseState::Standing;
					setPose = true;
					break;
				case ANIM_SITTING:
					animCode = "p02";  // Sitting idle pose
					loop = false;      // Don't loop (holds on last frame automatically)
					poseState = EQT::Graphics::IrrlichtRenderer::EntityPoseState::Sitting;
					setPose = true;
					break;
				case ANIM_CROUCHING:
					animCode = "l08";  // Crouching
					loop = false;      // Don't loop (holds on last frame automatically)
					poseState = EQT::Graphics::IrrlichtRenderer::EntityPoseState::Crouching;
					setPose = true;
					break;
				case ANIM_LYING:
					animCode = "d05";  // Lying down (death pose)
					loop = false;      // Don't loop (holds on last frame automatically)
					poseState = EQT::Graphics::IrrlichtRenderer::EntityPoseState::Lying;
					setPose = true;
					break;
				case ANIM_FREEZE:
					animCode = "o01";  // Freeze in place (use idle)
					break;
				case ANIM_LOOT:
					animCode = "t07";  // Looting animation
					loop = false;
					playThrough = true;
					break;
				default:
					// For other animations, don't change current animation
					// (movement animations come through ClientUpdate)
					break;
				}

				if (!animCode.empty()) {
					// Set pose state BEFORE animation to prevent updateEntity from overriding
					if (setPose) {
						m_renderer->setEntityPoseState(spawn_id, poseState);
					}
					m_renderer->setEntityAnimation(spawn_id, animCode, loop, playThrough);
					if (s_debug_level >= 2 || IsTrackedTarget(spawn_id)) {
						std::cout << fmt::format("[SpawnAppearance] Set animation '{}' pose={} on spawn_id={}",
							animCode, static_cast<int>(poseState), spawn_id) << std::endl;
					}
				}
			}
#endif
		}
		break;

	case AT_DIE:
		// Entity died - trigger death animation
#ifdef EQT_HAS_GRAPHICS
		if (m_graphics_initialized && m_renderer) {
			m_renderer->playEntityDeathAnimation(spawn_id);
		}
#endif
		break;

	case AT_HP_UPDATE:
		// HP update
		{
			auto it = m_entities.find(spawn_id);
			if (it != m_entities.end()) {
				it->second.hp_percent = static_cast<uint8_t>(parameter);
			}
		}
		break;

	default:
		// Other appearance types (invisible, levitate, etc.) - not implemented yet
		break;
	}
}

void EverQuest::ZoneProcessEmote(const EQ::Net::Packet &p)
{
	// Animation/Emote packet format:
	// Offset 0-1: Opcode
	// Offset 2-3: spawn_id (uint16)
	// Offset 4: animation_speed (uint8)
	// Offset 5: animation_id (uint8)
	if (p.Length() < 6) {
		return;
	}

	uint16_t spawn_id = p.GetUInt16(2);
	uint8_t anim_speed = p.GetUInt8(4);
	uint8_t anim_id = p.GetUInt8(5);

	if (s_debug_level >= 2 || IsTrackedTarget(spawn_id)) {
		LOG_DEBUG(MOD_ENTITY, "[EMOTE] spawn_id={}, speed={}, anim_id={}", spawn_id, anim_speed, anim_id);
	}

	// Update entity animation state
	auto it = m_entities.find(spawn_id);
	if (it != m_entities.end()) {
		it->second.animation = anim_id;
	}

#ifdef EQT_HAS_GRAPHICS
	if (m_graphics_initialized && m_renderer) {
		// Map animation ID to EQ model animation code
		std::string animCode;
		bool loop = false;
		bool playThrough = true;  // Most emotes/attacks should play through

		// Get entity's weapon skill types for weapon-based attack animations
		uint8_t primaryWeaponSkill = m_renderer->getEntityPrimaryWeaponSkill(spawn_id);
		uint8_t secondaryWeaponSkill = m_renderer->getEntitySecondaryWeaponSkill(spawn_id);

		// Map common animation IDs to EQ model animation codes
		// Combat animations (these are the key ones for NPC combat)
		switch (anim_id) {
		// Combat attack animations - use weapon-based animation
		case 1:  // Primary hand attack
		case 5:  // Attack
		case 6:  // Attack2
			// Use weapon skill type to determine correct animation
			animCode = EQ::getWeaponAttackAnimation(primaryWeaponSkill, false, false);
			break;
		case 2:  // Secondary hand attack
			// Use secondary weapon skill type
			animCode = EQ::getWeaponAttackAnimation(secondaryWeaponSkill, true, false);
			break;

		// Combat skill animations (use correct animation codes from reference)
		case 10: // Round kick
			animCode = EQ::ANIM_ROUND_KICK;  // c11
			break;
		case 11: // ANIM_KICK
			animCode = EQ::ANIM_KICK;  // c01 - basic kick
			break;
		case 12: // ANIM_BASH
			animCode = EQ::ANIM_BASH;  // c07 - Shield bash
			break;
		case 14: // Flying kick
			animCode = EQ::ANIM_FLYING_KICK;  // t07
			break;

		// Damage/hit reaction
		case 3:  // Damage from front
		case 4:  // Damage from back
			animCode = EQ::ANIM_DAMAGE_MINOR;  // d01
			break;

		// Death
		case 16: // ANIM_DEATH
			animCode = EQ::ANIM_DEATH;  // d05
			break;

		// Social/Emote animations (s01-s28)
		// EQ Titanium animation IDs mapped to pre-Luclin animation codes
		case 18: // ANIM_CHEER
			animCode = EQ::ANIM_EMOTE_CHEER;  // s01 - Cheer
			break;
		case 19: // ANIM_KNEEL
			animCode = EQ::ANIM_CROUCHING;  // l08 - Crouching
			loop = true;
			playThrough = false;
			break;
		case 20: // ANIM_JUMP
			animCode = EQ::ANIM_FALLING;  // l05 - Falling/jump
			break;
		case 21: // ANIM_CRY / ANIM_MOURN
			animCode = EQ::ANIM_EMOTE_MOURN;  // s02 - Mourn/Disappointed
			break;
		case 23: // ANIM_RUDE
			animCode = EQ::ANIM_EMOTE_RUDE;  // s04 - Rude
			break;
		case 24: // ANIM_YAWN
			animCode = EQ::ANIM_EMOTE_YAWN;  // s05 - Yawn
			break;
		case 26: // ANIM_NOD
			animCode = EQ::ANIM_EMOTE_NOD;  // s06 - Nod
			break;
		case 27: // ANIM_AMAZED
			animCode = EQ::ANIM_EMOTE_AMAZED;  // s07 - Amazed
			break;
		case 28: // ANIM_PLEAD
			animCode = EQ::ANIM_EMOTE_PLEAD;  // s08 - Plead
			break;
		case 29: // ANIM_WAVE
			animCode = EQ::ANIM_EMOTE_WAVE;  // s03 - Wave
			break;
		case 30: // ANIM_CLAP
			animCode = EQ::ANIM_EMOTE_CLAP;  // s09 - Clap
			break;
		case 31: // ANIM_DISTRESS
			animCode = EQ::ANIM_EMOTE_DISTRESS;  // s10 - Distress/Hungry
			break;
		case 32: // ANIM_BLUSH
			animCode = EQ::ANIM_EMOTE_BLUSH;  // s11 - Blush
			break;
		case 33: // ANIM_CHUCKLE
			animCode = EQ::ANIM_EMOTE_CHUCKLE;  // s12 - Chuckle
			break;
		case 34: // ANIM_COUGH / ANIM_BURP
			animCode = EQ::ANIM_EMOTE_BURP;  // s13 - Burp/Cough
			break;
		case 35: // ANIM_DUCK
			animCode = EQ::ANIM_EMOTE_DUCK;  // s14 - Duck
			break;
		case 36: // ANIM_PUZZLE / ANIM_LOOK_AROUND
			animCode = EQ::ANIM_EMOTE_PUZZLE;  // s15 - Look Around/Puzzle
			break;
		case 58: // ANIM_DANCE
			animCode = EQ::ANIM_EMOTE_DANCE;  // s16 - Dance
			loop = true;
			playThrough = false;
			break;
		case 59: // ANIM_BLINK
			animCode = EQ::ANIM_EMOTE_BLINK;  // s17 - Blink
			break;
		case 60: // ANIM_GLARE
			animCode = EQ::ANIM_EMOTE_GLARE;  // s18 - Glare
			break;
		case 61: // ANIM_DROOL
			animCode = EQ::ANIM_EMOTE_DROOL;  // s19 - Drool
			break;
		case 62: // ANIM_KNEEL_EMOTE
			animCode = EQ::ANIM_EMOTE_KNEEL;  // s20 - Kneel (emote)
			break;
		case 63: // ANIM_LAUGH
			animCode = EQ::ANIM_EMOTE_LAUGH;  // s21 - Laugh
			break;
		case 64: // ANIM_POINT
			animCode = EQ::ANIM_EMOTE_POINT;  // s22 - Point
			break;
		case 65: // ANIM_SHRUG / ANIM_PONDER
			animCode = EQ::ANIM_EMOTE_SHRUG;  // s23 - Ponder/Shrug
			break;
		case 66: // ANIM_READY
			animCode = EQ::ANIM_EMOTE_READY;  // s24 - Ready
			break;
		case 67: // ANIM_SALUTE
			animCode = EQ::ANIM_EMOTE_SALUTE;  // s25 - Salute
			break;
		case 68: // ANIM_SHIVER
			animCode = EQ::ANIM_EMOTE_SHIVER;  // s26 - Shiver
			break;
		case 69: // ANIM_TAP_FOOT
			animCode = EQ::ANIM_EMOTE_TAP_FOOT;  // s27 - Tap Foot
			break;
		case 70: // ANIM_BOW
			animCode = EQ::ANIM_EMOTE_BOW;  // s28 - Bow
			break;

		// Bard instrument animations
		case 43: // ANIM_STRINGED_INSTRUMENT
			animCode = EQ::ANIM_STRINGED_INST;  // t02 - Stringed Instrument
			loop = true;
			playThrough = false;
			break;
		case 44: // ANIM_WIND_INSTRUMENT
			animCode = EQ::ANIM_WIND_INST;  // t03 - Wind Instrument
			loop = true;
			playThrough = false;
			break;

		// Casting animations (when triggered via emote packet)
		case 42: // ANIM_CAST
			animCode = EQ::ANIM_CAST_PULLBACK;  // t04 - Cast Pull Back
			break;

		// Special animations
		case 105: // ANIM_LOOT
			animCode = EQ::ANIM_POSE_KNEEL;  // p05 - kneeling/looting
			break;

		default:
			// Unknown animation - use weapon-based attack for combat-range IDs
			if (anim_id >= 1 && anim_id <= 15) {
				animCode = EQ::getWeaponAttackAnimation(primaryWeaponSkill, false, false);
			}
			break;
		}

		if (!animCode.empty()) {
			m_renderer->setEntityAnimation(spawn_id, animCode, loop, playThrough);
			if (s_debug_level >= 2 || IsTrackedTarget(spawn_id)) {
				LOG_DEBUG(MOD_ENTITY, "[EMOTE] Set animation '{}' on spawn_id={} (anim_id={}, weaponSkill={})",
					animCode, spawn_id, anim_id, primaryWeaponSkill);
			}
		}
	}
#endif
}

void EverQuest::ZoneProcessGroundSpawn(const EQ::Net::Packet &p)
{
	// Parse Object_Struct from packet (skip 2-byte opcode)
	if (p.Length() < 2 + sizeof(EQT::Object_Struct)) {
		LOG_WARN(MOD_ENTITY, "GroundSpawn packet too small: {} bytes (need {})",
			p.Length(), 2 + sizeof(EQT::Object_Struct));
		return;
	}

	const auto* obj = reinterpret_cast<const EQT::Object_Struct*>(p.Data() + 2);

	// Create WorldObject from parsed data
	eqt::WorldObject worldObj;
	worldObj.drop_id = obj->drop_id;
	worldObj.name = std::string(obj->object_name, strnlen(obj->object_name, sizeof(obj->object_name)));
	worldObj.x = obj->x;
	worldObj.y = obj->y;
	worldObj.z = obj->z;
	worldObj.heading = obj->heading;
	worldObj.size = obj->size;
	worldObj.object_type = obj->object_type;
	worldObj.zone_id = obj->zone_id;
	worldObj.zone_instance = obj->zone_instance;
	worldObj.incline = obj->incline;
	worldObj.tilt_x = obj->tilt_x;
	worldObj.tilt_y = obj->tilt_y;
	worldObj.solid_type = obj->solid_type;

	// Store the world object
	m_world_objects[worldObj.drop_id] = worldObj;

	// Add tradeskill containers to renderer for click detection
#ifdef EQT_HAS_GRAPHICS
	if (worldObj.isTradeskillContainer() && m_renderer) {
		m_renderer->addWorldObject(worldObj.drop_id, worldObj.x, worldObj.y, worldObj.z,
			worldObj.object_type, worldObj.name);
	}
#endif

	if (worldObj.isTradeskillContainer()) {
		LOG_DEBUG(MOD_ENTITY, "Tradeskill object spawned: id={} name='{}' type={} ({}) at ({:.1f}, {:.1f}, {:.1f})",
			worldObj.drop_id, worldObj.name, worldObj.object_type,
			worldObj.getTradeskillName(), worldObj.x, worldObj.y, worldObj.z);
	} else {
		LOG_TRACE(MOD_ENTITY, "Ground object spawned: id={} name='{}' type={} at ({:.1f}, {:.1f}, {:.1f})",
			worldObj.drop_id, worldObj.name, worldObj.object_type,
			worldObj.x, worldObj.y, worldObj.z);
	}
}

void EverQuest::ZoneProcessWeather(const EQ::Net::Packet &p)
{
	LOG_DEBUG(MOD_ZONE, "Weather update received");
	
	m_weather_received = true;

#ifdef EQT_HAS_GRAPHICS
	if (m_renderer) {
		m_renderer->setLoadingProgress(0.68f, L"Receiving zone data...");
	}
#endif

	// Weather is the last packet in Zone Entry phase
	// Now we can send ReqNewZone
	if (!m_req_new_zone_sent) {
		ZoneSendReqNewZone();
	}
}

void EverQuest::ZoneProcessNewSpawn(const EQ::Net::Packet &p)
{
	// NewSpawn packet is 387 bytes total: opcode (2 bytes) + Spawn_Struct (385 bytes)
	// The spawn_id is embedded within the Spawn_Struct, not separate
	
	if (p.Length() < 387) {
		if (s_debug_level >= 1) {
			std::cout << fmt::format("NewSpawn packet too small: {} bytes (expected 387)", p.Length()) << std::endl;
		}
		return;
	}
	
	Entity entity;
	
	// Spawn_Struct starts at offset 2 (after opcode)
	size_t offset = 2;
	
	// Use the same parsing logic as ZoneSpawns
	// spawnId is at offset 340 in the struct (uint32)
	entity.spawn_id = p.GetUInt32(offset + 340);
	
	// Name is at offset 7 in Spawn_Struct
	entity.name = p.GetCString(offset + 7);
	
	// Level at offset 151
	entity.level = p.GetUInt8(offset + 151);
	
	// Position data from the bitfield structure (same as ZoneSpawns):
	uint32_t field1 = p.GetUInt32(offset + 94);  // contains deltaHeading and x
	uint32_t field2 = p.GetUInt32(offset + 98);  // contains y and animation
	uint32_t field3 = p.GetUInt32(offset + 102); // contains z and deltaY
	uint32_t field4 = p.GetUInt32(offset + 106); // contains deltaX and heading
	
	// Extract x, y, z (19 bits each, signed) - positions are in 10ths
	// Note: Server x/y are swapped relative to zone geometry, so we swap them here
	int32_t x_raw = (field1 >> 10) & 0x7FFFF;
	if (x_raw & 0x40000) x_raw |= 0xFFF80000; // Sign extend
	float server_x = static_cast<float>(x_raw);

	int32_t y_raw = field2 & 0x7FFFF;
	if (y_raw & 0x40000) y_raw |= 0xFFF80000; // Sign extend
	float server_y = static_cast<float>(y_raw);

	int32_t z_raw = field3 & 0x7FFFF;
	if (z_raw & 0x40000) z_raw |= 0xFFF80000; // Sign extend
	entity.z = static_cast<float>(z_raw);

	// Swap x and y from server
	entity.x = server_y;
	entity.y = server_x;

	// Read npc_type early - needed to determine heading transformation
	// NPC type: 0=player, 1=npc, 2=pc_corpse, 3=npc_corpse
	entity.npc_type = p.GetUInt8(offset + 83);
	bool isNPC = (entity.npc_type == 1 || entity.npc_type == 3);

	// Extract heading (11 bits to match C->S format)
	// Server uses 512 EQ units = 360 degrees, so multiply by 360/2048
	uint32_t raw_heading = (field4 >> 13) & 0x7FF;
	float server_heading = static_cast<float>(raw_heading) * 360.0f / 2048.0f;
	// Use heading directly for all entities (NPCs and players)
	entity.heading = server_heading;
	if (entity.heading >= 360.0f) entity.heading -= 360.0f;

	// Debug logging for current player at level 1+
	if (s_debug_level >= 1 && entity.name == m_character) {
		LOG_DEBUG(MOD_MOVEMENT, "POS S->C NewSpawn [SELF] spawn_id={} name='{}'", entity.spawn_id, entity.name);
		LOG_DEBUG(MOD_MOVEMENT, "POS S->C NewSpawn [SELF] raw_heading={} -> server_heading={:.2f}deg -> entity.heading={:.2f}deg (isNPC={})",
			raw_heading, server_heading, entity.heading, isNPC);
		LOG_DEBUG(MOD_MOVEMENT, "POS S->C NewSpawn [SELF] server_pos=({:.2f},{:.2f},{:.2f}) -> entity_pos=({:.2f},{:.2f},{:.2f})",
			server_x, server_y, static_cast<float>(z_raw), entity.x, entity.y, entity.z);
	}

	// race is at offset 284 (uint32)
	entity.race_id = p.GetUInt32(offset + 284);

	// class_ is at offset 331
	entity.class_id = p.GetUInt8(offset + 331);

	// gender is at offset 334
	entity.gender = p.GetUInt8(offset + 334);

	// guildID is at offset 238
	entity.guild_id = p.GetUInt32(offset + 238);

	// curHp is at offset 86 (hp percentage)
	entity.hp_percent = p.GetUInt8(offset + 86);
	
	// animation is in the bitfield at offset 98
	entity.animation = (field2 >> 19) & 0x3FF;

	// size is at offset 75 (float)
	entity.size = *reinterpret_cast<const float*>(static_cast<const char*>(p.Data()) + offset + 75);

	// Appearance data from Spawn_Struct
	entity.face = p.GetUInt8(offset + 6);           // Face/head variant
	// npc_type already read above for heading transformation
	entity.haircolor = p.GetUInt8(offset + 85);     // Hair color
	entity.showhelm = p.GetUInt8(offset + 139);     // Show helm
	entity.hairstyle = p.GetUInt8(offset + 145);    // Hair style
	entity.beardcolor = p.GetUInt8(offset + 146);   // Beard color
	entity.beard = p.GetUInt8(offset + 156);        // Beard style
	entity.helm = p.GetUInt8(offset + 275);         // Helm texture
	entity.light = p.GetUInt8(offset + 330);        // Light source
	entity.bodytype = p.GetUInt8(offset + 335);     // Body type
	entity.equip_chest2 = p.GetUInt8(offset + 339); // Body texture variant

	// Equipment textures (TextureProfile at offset 197, 9 slots * 4 bytes)
	for (int i = 0; i < 9; i++) {
		entity.equipment[i] = p.GetUInt32(offset + 197 + i * 4);
	}

	// Equipment tint colors (TintProfile at offset 348, 9 slots * 4 bytes)
	for (int i = 0; i < 9; i++) {
		entity.equipment_tint[i] = p.GetUInt32(offset + 348 + i * 4);
	}

	// Initialize movement fields
	entity.delta_x = 0;
	entity.delta_y = 0;
	entity.delta_z = 0;
	entity.delta_heading = 0;
	entity.last_update_time = std::time(nullptr);

	// Validate the spawn data before adding
	if (entity.spawn_id > 0 && entity.spawn_id < 100000 && !entity.name.empty()) {
		// Check if this is our own spawn by comparing name
		if (entity.name == m_character) {
			m_my_spawn_id = entity.spawn_id;
			if (m_trade_manager) {
				m_trade_manager->setMySpawnId(m_my_spawn_id);
			}
			if (s_debug_level >= 1) {
				LOG_DEBUG(MOD_MAIN, "Found our own spawn in NewSpawn! Name: {}, Spawn ID: {}, server pos=({:.2f}, {:.2f}, {:.2f}) size={:.2f}",
					entity.name, m_my_spawn_id, entity.x, entity.y, entity.z, entity.size);
			}
			// Update our position to match server spawn position
			m_x = entity.x;
			m_y = entity.y;
			m_size = entity.size;
			// Server Z is model center. Convert to feet position for internal use.
			// SendPositionUpdate will add m_size/2 to convert back to model center.
			m_z = entity.z - (entity.size / 2.0f);

			if (s_debug_level >= 1) {
				LOG_INFO(MOD_ZONE, "[ZONE-IN] Updated client pos from NewSpawn: ({:.2f},{:.2f},{:.2f}) (feet Z, server Z was {:.2f})",
					m_x, m_y, m_z, entity.z);
			}

#ifdef EQT_HAS_GRAPHICS
			// Update renderer with corrected feet Z position
			if (m_renderer) {
				float heading512 = m_heading * 512.0f / 360.0f;
				m_renderer->setPlayerPosition(m_x, m_y, m_z, heading512);
			}
#endif
		}

		// Add to entity list
		m_entities[entity.spawn_id] = entity;

#ifdef EQT_HAS_GRAPHICS
		OnSpawnAddedGraphics(entity);
		// Notify renderer of player spawn ID (after entity is created)
		if (entity.name == m_character && m_graphics_initialized && m_renderer) {
			m_renderer->setPlayerSpawnId(m_my_spawn_id);
		}
#endif

		if (s_debug_level >= 2) {
			LOG_DEBUG(MOD_ENTITY, "New spawn: {} (ID: {}) Level {} {} Race {} at ({:.2f}, {:.2f}, {:.2f})",
				entity.name, entity.spawn_id, entity.level,
				entity.class_id, entity.race_id, entity.x, entity.y, entity.z);
		}
	} else {
		LOG_WARN(MOD_ENTITY, "Invalid spawn data in NewSpawn: ID={}, Name='{}'",
			entity.spawn_id, entity.name);
	}
}

void EverQuest::ZoneProcessTributeUpdate(const EQ::Net::Packet &p)
{
	LOG_DEBUG(MOD_ZONE, "Received tribute update");
}

void EverQuest::ZoneProcessTributeTimer(const EQ::Net::Packet &p)
{
	LOG_DEBUG(MOD_ZONE, "Received tribute timer");
}

void EverQuest::ZoneProcessSendAATable(const EQ::Net::Packet &p)
{
	LOG_DEBUG(MOD_ZONE, "Received AA table data");
	m_aa_table_count++;
	
	// Check if all Zone Request phase responses received
	CheckZoneRequestPhaseComplete();
}

void EverQuest::ZoneProcessRespondAA(const EQ::Net::Packet &p)
{
	LOG_DEBUG(MOD_ZONE, "Received AA response");
	
	// Check if all Zone Request phase responses received
	CheckZoneRequestPhaseComplete();
}

void EverQuest::ZoneProcessTributeInfo(const EQ::Net::Packet &p)
{
	LOG_DEBUG(MOD_ZONE, "Received tribute info");
	m_tribute_count++;
	
	// Check if all Zone Request phase responses received
	CheckZoneRequestPhaseComplete();
}

void EverQuest::ZoneProcessSendGuildTributes(const EQ::Net::Packet &p)
{
	LOG_DEBUG(MOD_ZONE, "Received guild tributes");
	m_guild_tribute_count++;
	
	// Check if all Zone Request phase responses received
	CheckZoneRequestPhaseComplete();
}

void EverQuest::ZoneProcessSendAAStats(const EQ::Net::Packet &p)
{
	LOG_DEBUG(MOD_ZONE, "Received AA stats");
}

void EverQuest::ZoneProcessSendExpZonein(const EQ::Net::Packet &p)
{
	LOG_DEBUG(MOD_ZONE, "Received exp zone in - this triggers SendZoneInPackets()");

	m_send_exp_zonein_received = true;

	// ExpZonein triggers the server to send ExpUpdate, RaidUpdate, and GuildMOTD
	// We'll wait for GuildMOTD before sending final packets
}

void EverQuest::ZoneProcessWorldObjectsSent(const EQ::Net::Packet &p)
{
	LOG_DEBUG(MOD_ZONE, "Received world objects sent");

	// After world objects, send exp zonein if not already sent
	if (!m_exp_zonein_sent) {
		ZoneSendSendExpZonein();
	}
}

void EverQuest::ZoneProcessExpUpdate(const EQ::Net::Packet &p)
{
	LOG_DEBUG(MOD_ZONE, "Received exp update");
}

void EverQuest::ZoneProcessRaidUpdate(const EQ::Net::Packet &p)
{
	LOG_DEBUG(MOD_ZONE, "Received raid update");
}

void EverQuest::ZoneProcessGuildMOTD(const EQ::Net::Packet &p)
{
	LOG_DEBUG(MOD_ZONE, "Received guild MOTD");

	// GuildMOTD is the last packet from SendZoneInPackets()
	// Now we send SetServerFilter and ClientReady
	if (!m_server_filter_sent) {
		ZoneSendSetServerFilter();
		m_server_filter_sent = true;
	}
	
	if (!m_client_ready_sent) {
#ifdef EQT_HAS_GRAPHICS
		if (m_renderer) {
			m_renderer->setLoadingProgress(0.92f, L"Finalizing zone entry...");
		}
#endif
		ZoneSendClientReady();
		m_client_ready_sent = true;

		LOG_INFO(MOD_ZONE, "Zone connection complete! Client is now in the zone.");

#ifdef EQT_HAS_GRAPHICS
		// Signal renderer that zone is ready for display
		if (m_renderer) {
			m_renderer->setLoadingProgress(1.0f, L"Zone ready!");
			m_renderer->setZoneReady(true);
		}
#endif

		// Start the update loop now that we're fully zoned in
		if (!m_update_running) {
			StartUpdateLoop();
		}
	}
}

void EverQuest::ZoneProcessClientUpdate(const EQ::Net::Packet &p)
{
	// ClientUpdate packets contain position updates from other players
	// Format based on PlayerPositionUpdateServer_Struct (bit-packed):
	// spawn_id (2 bytes) + 20 bytes of bit-packed data

	if (p.Length() < 24) { // 2 (opcode) + 2 (spawn_id) + 20 (bit-packed data)
		LOG_WARN(MOD_ZONE, "ClientUpdate packet too small: {} bytes", p.Length());
		return;
	}
	
	// Read spawn_id (first 2 bytes after opcode)
	uint16_t spawn_id = p.GetUInt16(2);
	
	// Read the bit-packed data as 5 32-bit integers
	uint32_t field1 = p.GetUInt32(4);  // delta_heading:10, x_pos:19, padding:3
	uint32_t field2 = p.GetUInt32(8);  // y_pos:19, animation:10, padding:3
	uint32_t field3 = p.GetUInt32(12); // z_pos:19, delta_y:13
	uint32_t field4 = p.GetUInt32(16); // delta_x:13, heading:12, padding:7
	uint32_t field5 = p.GetUInt32(20); // delta_z:13, padding:19
	
	// Extract values from bit fields
	int32_t delta_heading = (field1 & 0x3FF);          // bits 0-9 (10 bits)
	int32_t x_pos_raw = (field1 >> 10) & 0x7FFFF;      // bits 10-28 (19 bits)
	
	int32_t y_pos_raw = (field2 & 0x7FFFF);            // bits 0-18 (19 bits)
	int32_t animation = (field2 >> 19) & 0x3FF;        // bits 19-28 (10 bits, signed)
	
	int32_t z_pos_raw = (field3 & 0x7FFFF);            // bits 0-18 (19 bits)
	int32_t delta_y = (field3 >> 19) & 0x1FFF;         // bits 19-31 (13 bits)
	
	int32_t delta_x = (field4 & 0x1FFF);               // bits 0-12 (13 bits)
	uint32_t heading = (field4 >> 13) & 0xFFF;         // bits 13-24 (12 bits)
	
	int32_t delta_z = (field5 & 0x1FFF);               // bits 0-12 (13 bits)
	
	// Sign extend delta values (they are signed)
	if (delta_heading & 0x200) delta_heading |= 0xFFFFFC00; // sign extend from 10 bits
	if (delta_x & 0x1000) delta_x |= 0xFFFFE000;           // sign extend from 13 bits
	if (delta_y & 0x1000) delta_y |= 0xFFFFE000;           // sign extend from 13 bits
	if (delta_z & 0x1000) delta_z |= 0xFFFFE000;           // sign extend from 13 bits

	// Sign extend animation (negative = play animation in reverse)
	// abs(animation) = animation ID, sign = playback direction
	if (animation & 0x200) animation |= 0xFFFFFC00;        // sign extend from 10 bits
	
	// Sign extend position values (they are signed 19-bit integers)
	if (x_pos_raw & 0x40000) x_pos_raw |= 0xFFF80000;      // sign extend from 19 bits
	if (y_pos_raw & 0x40000) y_pos_raw |= 0xFFF80000;      // sign extend from 19 bits
	if (z_pos_raw & 0x40000) z_pos_raw |= 0xFFF80000;      // sign extend from 19 bits
	
	// Convert raw position values to floats (divided by 8 for fixed-point)
	// Note: Server x/y are swapped relative to zone geometry
	float server_x = static_cast<float>(x_pos_raw) / 8.0f;
	float server_y = static_cast<float>(y_pos_raw) / 8.0f;
	float x = server_y;  // Swap
	float y = server_x;  // Swap
	float z = static_cast<float>(z_pos_raw) / 8.0f;
	// Delta values must also be swapped to match position coordinate system
	float server_dx = static_cast<float>(delta_x) / 8.0f;
	float server_dy = static_cast<float>(delta_y) / 8.0f;
	float dx = server_dy;  // Swap to match position swap
	float dy = server_dx;  // Swap to match position swap
	float dz = static_cast<float>(delta_z) / 8.0f;
	float dh = static_cast<float>(delta_heading);
	// Convert heading to degrees: 512 EQ units = 360 degrees
	uint32_t raw_heading = heading & 0x7FF;  // Lower 11 bits
	float server_h = static_cast<float>(raw_heading) * 360.0f / 2048.0f;
	// Use heading directly for all entities (no mirroring)
	float h_player = server_h;
	if (h_player >= 360.0f) h_player -= 360.0f;
	float h_npc = server_h;
	if (h_npc >= 360.0f) h_npc -= 360.0f;

	// Debug: log position updates
	// Level 1: current player, current target, or tracked targets
	// Level 2+: all entities
	uint16_t currentTargetId = m_combat_manager ? m_combat_manager->GetTargetId() : 0;
	bool isCurrentPlayer = (spawn_id == m_my_character_id);
	bool shouldLogPosition = (s_debug_level >= 2) ||
	                         (s_debug_level >= 1 && (isCurrentPlayer || spawn_id == currentTargetId || IsTrackedTarget(spawn_id)));
	if (shouldLogPosition) {
		LOG_DEBUG(MOD_MOVEMENT, "POS S->C spawn_id={} pos=({:.2f}, {:.2f}, {:.2f}) heading={:.1f} anim={} delta=({:.2f}, {:.2f}, {:.2f}) (my_id={})",
			spawn_id, x, y, z, server_h, animation, dx, dy, dz, m_my_character_id);
	}
	// Additional detailed logging for current player at debug level 1+
	if (s_debug_level >= 1 && isCurrentPlayer) {
		LOG_DEBUG(MOD_MOVEMENT, "POS S->C [SELF] raw_heading={} (12-bit field={}) -> server_h={:.2f}deg -> h_player={:.2f}deg",
			raw_heading, heading, server_h, h_player);
		LOG_DEBUG(MOD_MOVEMENT, "POS S->C [SELF] server_pos=({:.2f},{:.2f},{:.2f}) -> client_pos=({:.2f},{:.2f},{:.2f})",
			server_x, server_y, z, x, y, z);
	}

	// Check if this is an update for our own character
	if (spawn_id == m_my_character_id) {
		// Update our position to match what the server says
		m_x = x;
		m_y = y;
		m_z = z;
		// S->C transformation for our player: inverse of C->S (server_heading = 90 - m_heading)
		m_heading = 90.0f - server_h;
		if (m_heading < 0.0f) m_heading += 360.0f;
		if (m_heading >= 360.0f) m_heading -= 360.0f;
		// Also update our spawn ID if not set
		if (m_my_spawn_id == 0) {
			m_my_spawn_id = spawn_id;
			if (m_trade_manager) {
				m_trade_manager->setMySpawnId(m_my_spawn_id);
			}
			LOG_INFO(MOD_MOVEMENT, "Set our spawn ID to {} from ClientUpdate", m_my_spawn_id);

#ifdef EQT_HAS_GRAPHICS
			// Notify renderer of player spawn ID
			if (m_graphics_initialized && m_renderer) {
				m_renderer->setPlayerSpawnId(m_my_spawn_id);

				// Also update the inventory model view with player appearance
				auto playerIt = m_entities.find(m_my_spawn_id);
				if (playerIt != m_entities.end()) {
					const Entity& entity = playerIt->second;
					EQT::Graphics::EntityAppearance appearance;
					appearance.face = entity.face;
					appearance.haircolor = entity.haircolor;
					appearance.hairstyle = entity.hairstyle;
					appearance.beardcolor = entity.beardcolor;
					appearance.beard = entity.beard;
					appearance.texture = entity.equip_chest2;
					appearance.helm = entity.helm;
					for (int i = 0; i < 9; i++) {
						appearance.equipment[i] = entity.equipment[i];
						appearance.equipment_tint[i] = entity.equipment_tint[i];
					}
					m_renderer->updatePlayerAppearance(entity.race_id, entity.gender, appearance);
				}
			}
#endif

			// Start the update loop when we first receive our ClientUpdate
			// This indicates we're fully in the game world
			if (!m_update_running) {
				StartUpdateLoop();
			}
		}
		
		// Update our entity in the entity list
		auto it = m_entities.find(m_my_spawn_id);
		if (it != m_entities.end()) {
			it->second.x = x;
			it->second.y = y;
			it->second.z = z;
			it->second.heading = h_player;
			it->second.animation = animation;
			it->second.delta_x = dx;
			it->second.delta_y = dy;
			it->second.delta_z = dz;
			it->second.delta_heading = dh;
			it->second.last_update_time = std::time(nullptr);

#ifdef EQT_HAS_GRAPHICS
			OnSpawnMovedGraphics(m_my_spawn_id, x, y, z, h_player, dx, dy, dz, animation);
#endif
		}
		return;
	}

	// Update entity position in our list
	auto it = m_entities.find(spawn_id);
	if (it != m_entities.end()) {
		// Use heading directly for all entity types
		bool isNPC = (it->second.npc_type == 1 || it->second.npc_type == 3);
		float entity_heading = isNPC ? h_npc : h_player;

		it->second.x = x;
		it->second.y = y;
		it->second.z = z;
		it->second.heading = entity_heading;
		it->second.animation = animation;
		it->second.delta_x = dx;
		it->second.delta_y = dy;
		it->second.delta_z = dz;
		it->second.delta_heading = dh;
		it->second.last_update_time = std::time(nullptr);

#ifdef EQT_HAS_GRAPHICS
		OnSpawnMovedGraphics(spawn_id, x, y, z, entity_heading, dx, dy, dz, animation);
#endif

	}
}

void EverQuest::ZoneProcessDeleteSpawn(const EQ::Net::Packet &p)
{
	// DeleteSpawn is sent when an entity despawns
	// Format: spawn_id (2 bytes)
	
	if (p.Length() < 4) {
		if (s_debug_level >= 1) {
			std::cout << fmt::format("DeleteSpawn packet too small: {} bytes", p.Length()) << std::endl;
		}
		return;
	}
	
	uint16_t spawn_id = p.GetUInt16(2);
	
	// Special case: spawn_id 0 might mean "delete current target"
	if (spawn_id == 0 && m_combat_manager && m_combat_manager->HasTarget()) {
		spawn_id = m_combat_manager->GetTargetId();
		if (s_debug_level >= 1) {
			LOG_DEBUG(MOD_MAIN, "DeleteSpawn with ID 0 interpreted as current target: {}", spawn_id);
		}
	}
	
	auto it = m_entities.find(spawn_id);
	if (it != m_entities.end()) {
		// Handle corpse deletion - only delete corpses we've finished looting
		if (it->second.is_corpse) {
			// If we're actively looting this corpse, close the loot window first
			if (m_player_looting_corpse_id == spawn_id) {
				LOG_DEBUG(MOD_ENTITY, "Corpse {} ({}) being deleted while looting, closing loot window", spawn_id, it->second.name);
#ifdef EQT_HAS_GRAPHICS
				if (m_renderer && m_renderer->getWindowManager()) {
					m_renderer->getWindowManager()->closeLootWindow();
				}
#endif
				m_player_looting_corpse_id = 0;
				m_loot_all_in_progress = false;
				m_loot_all_remaining_slots.clear();
				// Mark as ready for deletion
				m_loot_complete_corpse_id = spawn_id;
			}

			// Only delete corpses we've completed looting
			// This prevents corpses from being deleted when NPCs die (server sends DeleteSpawn(0))
			if (m_loot_complete_corpse_id != spawn_id) {
				LOG_TRACE(MOD_ENTITY, "Ignoring DeleteSpawn for corpse {} ({}) - not finished looting", spawn_id, it->second.name);
				return;
			}

			LOG_DEBUG(MOD_ENTITY, "Corpse {} ({}) removed by server after looting", spawn_id, it->second.name);
			m_loot_complete_corpse_id = 0;  // Clear the flag
		}

		LOG_DEBUG(MOD_ENTITY, "Entity {} ({}) despawned", spawn_id, it->second.name);

		// Cancel trade if partner despawned
		if (m_trade_manager && m_trade_manager->isTrading() &&
			m_trade_manager->getPartnerSpawnId() == spawn_id) {
			LOG_DEBUG(MOD_MAIN, "Trade partner despawned, canceling trade");
			m_trade_manager->cancelTrade();
			AddChatSystemMessage("Trade cancelled - partner is no longer available");
		}

#ifdef EQT_HAS_GRAPHICS
		OnSpawnRemovedGraphics(spawn_id);
#endif

		m_entities.erase(it);
	} else {
		if (s_debug_level >= 2) {
			std::cout << fmt::format("DeleteSpawn for unknown spawn_id: {}", spawn_id) << std::endl;
		}
	}
}

void EverQuest::ZoneProcessMobHealth(const EQ::Net::Packet &p)
{
	// MobHealth updates an entity's health percentage
	// Format: spawn_id (2 bytes), hp_percent (1 byte)
	
	if (p.Length() < 5) {
		if (s_debug_level >= 1) {
			std::cout << fmt::format("MobHealth packet too small: {} bytes", p.Length()) << std::endl;
		}
		return;
	}
	
	uint16_t spawn_id = p.GetUInt16(2);
	uint8_t hp_percent = p.GetUInt8(4);
	
	auto it = m_entities.find(spawn_id);
	if (it != m_entities.end()) {
		uint8_t old_hp = it->second.hp_percent;
		it->second.hp_percent = hp_percent;
		if (s_debug_level >= 2 || IsTrackedTarget(spawn_id)) {
			LOG_DEBUG(MOD_ENTITY, "[HP] Entity {} ({}) health: {}% -> {}%", spawn_id, it->second.name, old_hp, hp_percent);
		}
#ifdef EQT_HAS_GRAPHICS
		// Update renderer target HP if this is our current target
		if (m_renderer && m_renderer->getCurrentTargetId() == spawn_id) {
			m_renderer->updateCurrentTargetHP(hp_percent);
		}
#endif
	}
}

void EverQuest::ZoneProcessHPUpdate(const EQ::Net::Packet &p)
{
	// HPUpdate is sent for an entity's HP
	// SpawnHPUpdate_Struct format: cur_hp (4 bytes), max_hp (4 bytes), spawn_id (2 bytes)
	// Total: 10 bytes + 2 byte opcode = 12 bytes
	// Note: Mana is sent separately via HC_OP_ManaChange

	if (p.Length() < 12) {
		if (s_debug_level >= 1) {
			std::cout << fmt::format("HPUpdate packet too small: {} bytes", p.Length()) << std::endl;
		}
		return;
	}

	uint32_t cur_hp = p.GetUInt32(2);
	int32_t max_hp = p.GetInt32(6);
	uint16_t spawn_id = p.GetUInt16(10);

	// Check if this is our own HP update
	bool is_self = (spawn_id == m_my_spawn_id);

	if (is_self) {
		// Update player HP tracking
		m_cur_hp = cur_hp;
		m_max_hp = static_cast<uint32_t>(max_hp > 0 ? max_hp : 0);
	}

	if (is_self) {
		LOG_DEBUG(MOD_ENTITY, "Player HP: {}/{} (spawn_id={})", cur_hp, max_hp, spawn_id);
	} else {
		LOG_TRACE(MOD_ENTITY, "Entity {} HP: {}/{}", spawn_id, cur_hp, max_hp);
	}

	// Update entity HP if we're tracking it
	auto it = m_entities.find(spawn_id);
	if (it != m_entities.end()) {
		it->second.hp_percent = (max_hp > 0) ? static_cast<uint8_t>(cur_hp * 100 / max_hp) : 100;
	}

	// Update combat stats for our player
	if (is_self && m_combat_manager) {
		CombatStats stats;
		stats.current_hp = cur_hp;
		stats.max_hp = max_hp > 0 ? static_cast<uint32_t>(max_hp) : 0;
		// Don't update mana here - it comes from ManaChange packet
		stats.current_mana = m_mana;
		stats.max_mana = m_max_mana;
		stats.current_endurance = 0;
		stats.max_endurance = 0;
		stats.hp_percent = (max_hp > 0) ? (static_cast<float>(cur_hp) * 100.0f / static_cast<float>(max_hp)) : 100.0f;
		stats.mana_percent = (m_max_mana > 0) ? (static_cast<float>(m_mana) * 100.0f / static_cast<float>(m_max_mana)) : 100.0f;
		stats.endurance_percent = 100.0f;
		m_combat_manager->UpdateCombatStats(stats);
	}

#ifdef EQT_HAS_GRAPHICS
	// Update inventory window stats when player HP changes
	if (is_self) {
		UpdateInventoryStats();
	}
#endif
}

void EverQuest::CheckZoneRequestPhaseComplete()
{
	// Check if we've received all expected responses from Zone Request phase
	// We need to have received:
	// 1. At least one AA table (m_aa_table_count > 0)
	// 2. RespondAA packet
	// 3. At least one tribute info (m_tribute_count > 0) 
	// 4. At least one guild tribute (m_guild_tribute_count > 0)
	
	// For simplicity, we'll check if we've received at least one of each
	if (m_new_zone_received && 
	    m_aa_table_count > 0 && 
	    m_tribute_count > 0 && 
	    m_guild_tribute_count > 0 &&
	    !m_req_client_spawn_sent) {
		
		LOG_INFO(MOD_ZONE, "Zone Request phase complete, sending ReqClientSpawn");
		
		// Move to Player Spawn Request phase
		ZoneSendReqClientSpawn();
		m_req_client_spawn_sent = true;
	}
}

void EverQuest::ZoneSendChannelMessage(const std::string &message, ChatChannelType channel, const std::string &target)
{
	// Based on ChannelMessage_Struct from titanium_structs.h:
	// targetname[64], sender[64], language(4), chan_num(4), unknown(8), skill(4), message[]
	
	// For channel messages, we need to include the null terminator
	size_t message_len = message.length();
	size_t packet_size = 150 + message_len + 1; // +1 for null terminator
	
	EQ::Net::DynamicPacket p;
	p.Resize(packet_size);
	
	p.PutUInt16(0, HC_OP_ChannelMessage);
	
	// Clear the entire packet to ensure null terminators
	memset((char*)p.Data() + 2, 0, packet_size - 2);
	
	// Target name (64 bytes) - used for tells
	if (!target.empty()) {
		size_t target_len = target.length();
		if (target_len > 63) target_len = 63;
		memcpy((char*)p.Data() + 2, target.c_str(), target_len);
		// Null terminator already set by memset
	}
	
	// Sender name (64 bytes) - this is our character name
	size_t name_len = m_character.length();
	if (name_len > 63) name_len = 63;
	memcpy((char*)p.Data() + 66, m_character.c_str(), name_len);
	// Null terminator already set by memset
	
	// Language (0 = common tongue)
	p.PutUInt32(130, 0);
	
	// Channel number
	p.PutUInt32(134, static_cast<uint32_t>(channel));
	
	// Unknown fields (8 bytes) - already 0 from memset
	
	// Skill in language (100 = perfect)
	p.PutUInt32(146, 100);
	
	// Message - copy only the characters, null terminator already set
	memcpy((char*)p.Data() + 150, message.c_str(), message_len);
	
	if (s_debug_level >= 1) {
		std::cout << fmt::format("Sending {} message: '{}'", 
			channel == CHAT_CHANNEL_SAY ? "say" :
			channel == CHAT_CHANNEL_TELL ? "tell" :
			channel == CHAT_CHANNEL_SHOUT ? "shout" :
			channel == CHAT_CHANNEL_OOC ? "ooc" :
			channel == CHAT_CHANNEL_AUCTION ? "auction" :
			channel == CHAT_CHANNEL_GROUP ? "group" :
			channel == CHAT_CHANNEL_GUILD ? "guild" : "unknown",
			message) << std::endl;
	}
	
	DumpPacket("C->S", HC_OP_ChannelMessage, p);
	m_zone_connection->QueuePacket(p);
}

void EverQuest::ZoneProcessChannelMessage(const EQ::Net::Packet &p)
{
	// Process incoming channel messages
	if (p.Length() < 150) { // Minimum size for ChannelMessage_Struct
		if (s_debug_level >= 1) {
			std::cout << fmt::format("ChannelMessage packet too small: {} bytes", p.Length()) << std::endl;
		}
		return;
	}

	std::string target = p.GetCString(2);
	std::string sender = p.GetCString(66);
	uint32_t language = p.GetUInt32(130);
	uint32_t channel = p.GetUInt32(134);
	uint32_t skill = p.GetUInt32(146);
	std::string message = p.GetCString(150);

	if (s_debug_level >= 1) {
		std::cout << fmt::format("[CHAT] {} ({}): {}",
			sender,
			channel == CHAT_CHANNEL_SAY ? "say" :
			channel == CHAT_CHANNEL_TELL ? "tell" :
			channel == CHAT_CHANNEL_SHOUT ? "shout" :
			channel == CHAT_CHANNEL_OOC ? "ooc" :
			channel == CHAT_CHANNEL_GROUP ? "group" :
			channel == CHAT_CHANNEL_GUILD ? "guild" :
			channel == CHAT_CHANNEL_EMOTE ? "emote" :
			fmt::format("chan{}", channel),
			message) << std::endl;

		if (!target.empty() && channel == CHAT_CHANNEL_TELL) {
			std::cout << fmt::format("  (Tell to: {})", target) << std::endl;
		}
	}

#ifdef EQT_HAS_GRAPHICS
	// Route message to chat window
	if (m_renderer) {
		auto* windowManager = m_renderer->getWindowManager();
		if (windowManager) {
			auto* chatWindow = windowManager->getChatWindow();
			if (chatWindow) {
				// Convert EQ channel to ChatChannel enum
				eqt::ui::ChatChannel chatChannel;
				switch (channel) {
					case CHAT_CHANNEL_GUILD:   chatChannel = eqt::ui::ChatChannel::Guild; break;
					case CHAT_CHANNEL_GROUP:   chatChannel = eqt::ui::ChatChannel::Group; break;
					case CHAT_CHANNEL_SHOUT:   chatChannel = eqt::ui::ChatChannel::Shout; break;
					case CHAT_CHANNEL_AUCTION: chatChannel = eqt::ui::ChatChannel::Auction; break;
					case CHAT_CHANNEL_OOC:     chatChannel = eqt::ui::ChatChannel::OOC; break;
					case CHAT_CHANNEL_TELL:    chatChannel = eqt::ui::ChatChannel::Tell; break;
					case CHAT_CHANNEL_SAY:     chatChannel = eqt::ui::ChatChannel::Say; break;
					case CHAT_CHANNEL_EMOTE:   chatChannel = eqt::ui::ChatChannel::Emote; break;
					default:                   chatChannel = eqt::ui::ChatChannel::System; break;
				}

				// Create and add message
				eqt::ui::ChatMessage chatMsg;
				chatMsg.sender = sender;
				chatMsg.text = message;
				chatMsg.channel = chatChannel;
				chatMsg.timestamp = static_cast<uint32_t>(std::time(nullptr));
				chatMsg.color = eqt::ui::getChannelColor(chatChannel);

				chatWindow->addMessage(std::move(chatMsg));
			}
		}
	}
#endif
}

void EverQuest::SendChatMessage(const std::string &message, const std::string &channel_name, const std::string &target)
{
	// Convert channel name to ChatChannelType
	ChatChannelType channel;
	
	// Convert to lowercase for comparison
	std::string channel_lower = channel_name;
	std::transform(channel_lower.begin(), channel_lower.end(), channel_lower.begin(), ::tolower);
	
	if (channel_lower == "say") {
		channel = CHAT_CHANNEL_SAY;
	} else if (channel_lower == "tell") {
		channel = CHAT_CHANNEL_TELL;
		if (target.empty()) {
			std::cout << "Error: Tell requires a target player name" << std::endl;
			return;
		}
	} else if (channel_lower == "shout") {
		channel = CHAT_CHANNEL_SHOUT;
	} else if (channel_lower == "ooc") {
		channel = CHAT_CHANNEL_OOC;
	} else if (channel_lower == "group") {
		channel = CHAT_CHANNEL_GROUP;
	} else if (channel_lower == "guild") {
		channel = CHAT_CHANNEL_GUILD;
	} else if (channel_lower == "auction") {
		channel = CHAT_CHANNEL_AUCTION;
	} else if (channel_lower == "emote") {
		channel = CHAT_CHANNEL_EMOTE;
	} else {
		LOG_WARN(MOD_MAIN, "Unknown channel: '{}'. Valid channels: say, tell, shout, ooc, group, guild, auction, emote", channel_name);
		return;
	}

	// Check if we're connected to the zone
	if (!m_zone_connection || !m_zone_connected) {
		LOG_WARN(MOD_MAIN, "Not connected to zone server");
		return;
	}
	
	// Route messages based on channel type
	// For now, route all messages through zone server since UCS implementation needs fixing
	ZoneSendChannelMessage(message, channel, target);
}

void EverQuest::AddChatSystemMessage(const std::string &text)
{
#ifdef EQT_HAS_GRAPHICS
	if (m_renderer) {
		auto* windowManager = m_renderer->getWindowManager();
		if (windowManager) {
			auto* chatWindow = windowManager->getChatWindow();
			if (chatWindow) {
				chatWindow->addSystemMessage(text);
			}
		}
	}
#endif
	// Also print to console
	LOG_INFO(MOD_MAIN, "{}", text);
}

// ============================================================================
// Hotbar Button Management (Stub for future implementation)
// ============================================================================

void EverQuest::AddPendingHotbarButton(uint8_t skill_id)
{
#ifdef EQT_HAS_GRAPHICS
	const char* skill_name = EQ::getSkillName(skill_id);
	m_pending_hotbar_buttons.emplace_back(
		eqt::ui::HotbarButtonType::Skill,
		static_cast<uint32_t>(skill_id),
		skill_name ? skill_name : "Unknown Skill"
	);
	LOG_INFO(MOD_MAIN, "Queued skill {} ({}) for hotbar (total pending: {})",
		skill_id, skill_name ? skill_name : "Unknown", m_pending_hotbar_buttons.size());
#endif
}

const std::vector<eqt::ui::PendingHotbarButton>& EverQuest::GetPendingHotbarButtons() const
{
	return m_pending_hotbar_buttons;
}

void EverQuest::ClearPendingHotbarButtons()
{
	m_pending_hotbar_buttons.clear();
	LOG_DEBUG(MOD_MAIN, "Cleared pending hotbar buttons");
}

size_t EverQuest::GetPendingHotbarButtonCount() const
{
	return m_pending_hotbar_buttons.size();
}

void EverQuest::ProcessChatInput(const std::string &input)
{
	if (input.empty()) {
		return;
	}

#ifdef EQT_HAS_GRAPHICS
	// Ensure command registry is initialized
	if (!m_command_registry) {
		m_command_registry = std::make_unique<eqt::ui::CommandRegistry>();
		RegisterCommands();
	}

	// Check if it's a command (starts with /)
	if (input[0] == '/') {
		std::string cmd_line = input.substr(1);  // Remove leading /

		// Try to execute via command registry
		if (!m_command_registry->executeCommand(cmd_line)) {
			// Command not found - extract command name for error message
			size_t space_pos = cmd_line.find(' ');
			std::string command = (space_pos != std::string::npos) ?
				cmd_line.substr(0, space_pos) : cmd_line;
			AddChatSystemMessage(fmt::format("Unknown command: /{}. Type /help for a list of commands.", command));
		}
	} else {
		// No slash - treat as /say
		SendChatMessage(input, "say");
	}
#else
	// Without graphics, just send as say
	if (input[0] != '/') {
		SendChatMessage(input, "say");
	}
#endif
}

#ifdef EQT_HAS_GRAPHICS
void EverQuest::RegisterCommands()
{
	using namespace eqt::ui;

	// === Chat Commands ===

	Command say;
	say.name = "say";
	say.aliases = {"s"};
	say.usage = "/say <message>";
	say.description = "Say message to nearby players";
	say.category = "Chat";
	say.handler = [this](const std::string& args) {
		SendChatMessage(args, "say");
	};
	m_command_registry->registerCommand(say);

	Command shout;
	shout.name = "shout";
	shout.aliases = {"sho"};
	shout.usage = "/shout <message>";
	shout.description = "Shout message to entire zone";
	shout.category = "Chat";
	shout.handler = [this](const std::string& args) {
		SendChatMessage(args, "shout");
	};
	m_command_registry->registerCommand(shout);

	Command ooc;
	ooc.name = "ooc";
	ooc.aliases = {"o"};
	ooc.usage = "/ooc <message>";
	ooc.description = "Out of character message";
	ooc.category = "Chat";
	ooc.handler = [this](const std::string& args) {
		SendChatMessage(args, "ooc");
	};
	m_command_registry->registerCommand(ooc);

	Command auction;
	auction.name = "auction";
	auction.aliases = {"auc"};
	auction.usage = "/auction <message>";
	auction.description = "Auction channel message";
	auction.category = "Chat";
	auction.handler = [this](const std::string& args) {
		SendChatMessage(args, "auction");
	};
	m_command_registry->registerCommand(auction);

	Command gsay;
	gsay.name = "gsay";
	gsay.aliases = {"g"};
	gsay.usage = "/gsay <message>";
	gsay.description = "Group chat message";
	gsay.category = "Chat";
	gsay.handler = [this](const std::string& args) {
		SendChatMessage(args, "group");
	};
	m_command_registry->registerCommand(gsay);

	Command gu;
	gu.name = "gu";
	gu.aliases = {"guildsay"};
	gu.usage = "/gu <message>";
	gu.description = "Guild chat message";
	gu.category = "Chat";
	gu.handler = [this](const std::string& args) {
		SendChatMessage(args, "guild");
	};
	m_command_registry->registerCommand(gu);

	Command tell;
	tell.name = "tell";
	tell.aliases = {"t", "msg"};
	tell.usage = "/tell <player> <message>";
	tell.description = "Send private message to player";
	tell.category = "Chat";
	tell.requiresArgs = true;
	tell.handler = [this](const std::string& args) {
		size_t space_pos = args.find(' ');
		if (space_pos != std::string::npos) {
			std::string target = args.substr(0, space_pos);
			std::string message = args.substr(space_pos + 1);
			SendChatMessage(message, "tell", target);
		} else if (!args.empty()) {
			AddChatSystemMessage("Usage: /tell <player> <message>");
		}
	};
	m_command_registry->registerCommand(tell);

	Command emote;
	emote.name = "emote";
	emote.aliases = {"em", "me"};
	emote.usage = "/emote <action>";
	emote.description = "Perform custom emote";
	emote.category = "Chat";
	emote.handler = [this](const std::string& args) {
		SendChatMessage(args, "emote");
	};
	m_command_registry->registerCommand(emote);

	Command reply;
	reply.name = "reply";
	reply.aliases = {"r"};
	reply.usage = "/reply <message>";
	reply.description = "Reply to last tell";
	reply.category = "Chat";
	reply.handler = [this](const std::string& args) {
		AddChatSystemMessage("Reply not yet implemented");
	};
	m_command_registry->registerCommand(reply);

	// === Group Commands ===

	Command invite_cmd;
	invite_cmd.name = "invite";
	invite_cmd.aliases = {"inv"};
	invite_cmd.usage = "/invite [name]";
	invite_cmd.description = "Invite player to group";
	invite_cmd.category = "Group";
	invite_cmd.handler = [this](const std::string& args) {
		std::string target_name = args;

		// If no name provided, use current target
		if (target_name.empty()) {
			if (m_combat_manager && m_combat_manager->HasTarget()) {
				uint16_t target_id = m_combat_manager->GetTargetId();
				auto it = m_entities.find(target_id);
				if (it != m_entities.end()) {
					target_name = it->second.name;
				}
			}
		}

		if (target_name.empty()) {
			AddChatSystemMessage("Usage: /invite <name> or target a player");
			return;
		}

		SendGroupInvite(target_name);
		AddChatSystemMessage(fmt::format("Inviting {} to group", EQT::toDisplayName(target_name)));
	};
	m_command_registry->registerCommand(invite_cmd);

	Command follow_cmd;
	follow_cmd.name = "follow";
	follow_cmd.aliases = {"fol"};
	follow_cmd.usage = "/follow [name]";
	follow_cmd.description = "Accept group invite or follow player";
	follow_cmd.category = "Group";
	follow_cmd.handler = [this](const std::string& args) {
		// If we have a pending invite, accept it
		if (m_has_pending_invite) {
			AcceptGroupInvite();
			return;
		}

		// Otherwise, try to follow a player
		std::string target_name = args;
		if (target_name.empty() && m_combat_manager && m_combat_manager->HasTarget()) {
			uint16_t target_id = m_combat_manager->GetTargetId();
			auto it = m_entities.find(target_id);
			if (it != m_entities.end()) {
				target_name = it->second.name;
			}
		}

		if (!target_name.empty()) {
			// Implement follow behavior (movement following)
			AddChatSystemMessage(fmt::format("Following {}", EQT::toDisplayName(target_name)));
			// TODO: Implement actual follow movement
		} else {
			AddChatSystemMessage("No pending invite. Use /follow <name> to follow a player.");
		}
	};
	m_command_registry->registerCommand(follow_cmd);

	Command disband_cmd;
	disband_cmd.name = "disband";
	disband_cmd.usage = "/disband";
	disband_cmd.description = "Leave or disband group";
	disband_cmd.category = "Group";
	disband_cmd.handler = [this](const std::string& args) {
		if (!m_in_group) {
			AddChatSystemMessage("You are not in a group");
			return;
		}

		if (m_is_group_leader) {
			SendGroupDisband();
			AddChatSystemMessage("Group disbanded");
		} else {
			SendLeaveGroup();
			AddChatSystemMessage("You have left the group");
		}
	};
	m_command_registry->registerCommand(disband_cmd);

	Command decline_cmd;
	decline_cmd.name = "decline";
	decline_cmd.usage = "/decline";
	decline_cmd.description = "Decline pending group invite";
	decline_cmd.category = "Group";
	decline_cmd.handler = [this](const std::string& args) {
		if (!m_has_pending_invite) {
			AddChatSystemMessage("No pending group invite");
			return;
		}
		DeclineGroupInvite();
	};
	m_command_registry->registerCommand(decline_cmd);

	// === Trade Commands ===

	Command trade_cmd;
	trade_cmd.name = "trade";
	trade_cmd.usage = "/trade [name]";
	trade_cmd.description = "Initiate trade with target or named player";
	trade_cmd.category = "Social";
	trade_cmd.handler = [this](const std::string& args) {
		if (!m_trade_manager) {
			AddChatSystemMessage("Trade not available");
			return;
		}
		if (m_trade_manager->isTrading()) {
			AddChatSystemMessage("You are already trading");
			return;
		}
		// Cannot trade while in combat
		if (m_combat_manager && m_combat_manager->GetCombatState() == COMBAT_STATE_ENGAGED) {
			AddChatSystemMessage("You cannot trade while in combat");
			return;
		}

		uint32_t target_spawn_id = 0;
		std::string target_name;

		if (args.empty()) {
			// Use current target
			if (m_combat_manager && m_combat_manager->GetTargetId() != 0) {
				target_spawn_id = m_combat_manager->GetTargetId();
				auto it = m_entities.find(target_spawn_id);
				if (it != m_entities.end()) {
					target_name = it->second.name;
				}
			}
			if (target_spawn_id == 0) {
				AddChatSystemMessage("No target - use /trade <name> or target a player");
				return;
			}
		} else {
			// Find player by name
			std::string searchName = args;
			std::transform(searchName.begin(), searchName.end(), searchName.begin(), ::tolower);

			for (const auto& [id, entity] : m_entities) {
				// Only trade with players (not corpses, class_id > 0 indicates player class)
				if (entity.class_id == 0 || entity.is_corpse) continue;

				std::string entityName = entity.name;
				std::transform(entityName.begin(), entityName.end(), entityName.begin(), ::tolower);
				if (entityName.find(searchName) != std::string::npos) {
					target_spawn_id = entity.spawn_id;
					target_name = entity.name;
					break;
				}
			}
			if (target_spawn_id == 0) {
				AddChatSystemMessage(fmt::format("No player found matching '{}'", args));
				return;
			}
		}

		// Cannot trade with self
		if (target_spawn_id == m_my_spawn_id) {
			AddChatSystemMessage("You cannot trade with yourself");
			return;
		}

		// Check distance to target (max trade range ~150 units)
		constexpr float MAX_TRADE_DISTANCE = 150.0f;
		bool isNpc = false;
		auto target_it = m_entities.find(target_spawn_id);
		if (target_it != m_entities.end()) {
			const auto& target_entity = target_it->second;
			isNpc = (target_entity.npc_type == 1);
			float dx = m_x - target_entity.x;
			float dy = m_y - target_entity.y;
			float dz = m_z - target_entity.z;
			float distance = std::sqrt(dx*dx + dy*dy + dz*dz);
			if (distance > MAX_TRADE_DISTANCE) {
				AddChatSystemMessage("You are too far away to trade");
				return;
			}
		}

		m_trade_manager->requestTrade(target_spawn_id, target_name, isNpc);
		AddChatSystemMessage(fmt::format("Requesting trade with {}", EQT::toDisplayName(target_name)));
	};
	m_command_registry->registerCommand(trade_cmd);

	Command accept_cmd;
	accept_cmd.name = "accept";
	accept_cmd.usage = "/accept";
	accept_cmd.description = "Accept pending trade request";
	accept_cmd.category = "Social";
	accept_cmd.handler = [this](const std::string& args) {
		if (!m_trade_manager) {
			AddChatSystemMessage("Trade not available");
			return;
		}
		if (m_trade_manager->getState() != TradeState::PendingAccept) {
			AddChatSystemMessage("No pending trade request");
			return;
		}
		// Cannot accept trade while in combat
		if (m_combat_manager && m_combat_manager->GetCombatState() == COMBAT_STATE_ENGAGED) {
			AddChatSystemMessage("You cannot trade while in combat");
			return;
		}
		m_trade_manager->acceptTradeRequest();
		AddChatSystemMessage(fmt::format("Accepting trade with {}", EQT::toDisplayName(m_trade_manager->getPartnerName())));
	};
	m_command_registry->registerCommand(accept_cmd);

	Command tradedecline_cmd;
	tradedecline_cmd.name = "tradedecline";
	tradedecline_cmd.aliases = {"rejecttrade"};
	tradedecline_cmd.usage = "/tradedecline";
	tradedecline_cmd.description = "Decline pending trade request";
	tradedecline_cmd.category = "Social";
	tradedecline_cmd.handler = [this](const std::string& args) {
		if (!m_trade_manager) {
			AddChatSystemMessage("Trade not available");
			return;
		}
		if (m_trade_manager->getState() != TradeState::PendingAccept) {
			AddChatSystemMessage("No pending trade request");
			return;
		}
		std::string partner_name = m_trade_manager->getPartnerName();
		m_trade_manager->rejectTradeRequest();
		AddChatSystemMessage(fmt::format("Declined trade request from {}", EQT::toDisplayName(partner_name)));
	};
	m_command_registry->registerCommand(tradedecline_cmd);

	Command canceltrade_cmd;
	canceltrade_cmd.name = "canceltrade";
	canceltrade_cmd.aliases = {"stoptrade"};
	canceltrade_cmd.usage = "/canceltrade";
	canceltrade_cmd.description = "Cancel active trade";
	canceltrade_cmd.category = "Social";
	canceltrade_cmd.handler = [this](const std::string& args) {
		if (!m_trade_manager) {
			AddChatSystemMessage("Trade not available");
			return;
		}
		if (m_trade_manager->getState() != TradeState::Active) {
			AddChatSystemMessage("You are not trading");
			return;
		}
		m_trade_manager->cancelTrade();
		AddChatSystemMessage("Trade cancelled");
	};
	m_command_registry->registerCommand(canceltrade_cmd);

	// === Movement Commands ===

	Command loc;
	loc.name = "loc";
	loc.aliases = {"location"};
	loc.usage = "/loc";
	loc.description = "Show current coordinates";
	loc.category = "Movement";
	loc.handler = [this](const std::string& args) {
		AddChatSystemMessage(fmt::format("Your location is {:.1f}, {:.1f}, {:.1f}", m_x, m_y, m_z));
	};
	m_command_registry->registerCommand(loc);

	Command sit;
	sit.name = "sit";
	sit.usage = "/sit";
	sit.description = "Sit down";
	sit.category = "Movement";
	sit.handler = [this](const std::string& args) {
		SetPositionState(POS_SITTING);
		AddChatSystemMessage("You sit down.");
	};
	m_command_registry->registerCommand(sit);

	Command stand;
	stand.name = "stand";
	stand.usage = "/stand";
	stand.description = "Stand up";
	stand.category = "Movement";
	stand.handler = [this](const std::string& args) {
		SetPositionState(POS_STANDING);
		AddChatSystemMessage("You stand up.");
	};
	m_command_registry->registerCommand(stand);

	Command move_cmd;
	move_cmd.name = "move";
	move_cmd.usage = "/move <x> <y> <z>";
	move_cmd.description = "Move to coordinates";
	move_cmd.category = "Movement";
	move_cmd.handler = [this](const std::string& args) {
		std::istringstream iss(args);
		float x, y, z;
		if (iss >> x >> y >> z) {
			Move(x, y, z);
			AddChatSystemMessage(fmt::format("Moving to ({:.1f}, {:.1f}, {:.1f})", x, y, z));
		} else {
			AddChatSystemMessage("Usage: /move <x> <y> <z>");
		}
	};
	m_command_registry->registerCommand(move_cmd);

	Command moveto;
	moveto.name = "moveto";
	moveto.usage = "/moveto <entity_name>";
	moveto.description = "Move to named entity";
	moveto.category = "Movement";
	moveto.handler = [this](const std::string& args) {
		std::string entity = args;
		// Trim leading whitespace
		while (!entity.empty() && entity[0] == ' ') {
			entity = entity.substr(1);
		}
		if (!entity.empty()) {
			MoveToEntity(entity);
			AddChatSystemMessage(fmt::format("Moving to entity: {}", entity));
		} else {
			AddChatSystemMessage("Usage: /moveto <entity_name>");
		}
	};
	m_command_registry->registerCommand(moveto);

	Command follow;
	follow.name = "follow";
	follow.usage = "/follow <entity_name>";
	follow.description = "Follow an entity";
	follow.category = "Movement";
	follow.handler = [this](const std::string& args) {
		std::string entity = args;
		// Trim leading whitespace
		while (!entity.empty() && entity[0] == ' ') {
			entity = entity.substr(1);
		}
		if (!entity.empty()) {
			Follow(entity);
			AddChatSystemMessage(fmt::format("Following: {}", entity));
		} else {
			AddChatSystemMessage("Usage: /follow <entity_name>");
		}
	};
	m_command_registry->registerCommand(follow);

	Command stopfollow;
	stopfollow.name = "stopfollow";
	stopfollow.aliases = {"stop"};
	stopfollow.usage = "/stopfollow";
	stopfollow.description = "Stop following";
	stopfollow.category = "Movement";
	stopfollow.handler = [this](const std::string& args) {
		StopFollow();
		AddChatSystemMessage("Stopped following");
	};
	m_command_registry->registerCommand(stopfollow);

	// === Combat Commands ===

	Command target;
	target.name = "target";
	target.aliases = {"tar"};
	target.usage = "/target <name>";
	target.description = "Target entity by name";
	target.category = "Combat";
	target.handler = [this](const std::string& args) {
		if (args.empty()) {
			AddChatSystemMessage("Usage: /target <name>");
			return;
		}
		// Find entity by name (partial match)
		std::string searchName = args;
		std::transform(searchName.begin(), searchName.end(), searchName.begin(), ::tolower);

		for (const auto& [id, entity] : m_entities) {
			std::string entityName = entity.name;
			std::transform(entityName.begin(), entityName.end(), entityName.begin(), ::tolower);
			if (entityName.find(searchName) != std::string::npos) {
				if (m_combat_manager) {
					m_combat_manager->SetTarget(entity.spawn_id);
					AddChatSystemMessage(fmt::format("Targeting: {}", EQT::toDisplayName(entity.name)));
				}
				return;
			}
		}
		AddChatSystemMessage(fmt::format("No target found matching '{}'", args));
	};
	m_command_registry->registerCommand(target);

	Command attack;
	attack.name = "attack";
	attack.aliases = {"att"};
	attack.usage = "/attack";
	attack.description = "Begin auto-attack";
	attack.category = "Combat";
	attack.handler = [this](const std::string& args) {
		if (m_combat_manager && m_combat_manager->GetTargetId() != 0) {
			m_combat_manager->EnableAutoAttack();
			AddChatSystemMessage("Auto attack ON");
		} else {
			AddChatSystemMessage("No target - auto attack not enabled");
		}
	};
	m_command_registry->registerCommand(attack);

	Command stopattack;
	stopattack.name = "stopattack";
	stopattack.usage = "/stopattack";
	stopattack.description = "Stop auto-attack";
	stopattack.category = "Combat";
	stopattack.handler = [this](const std::string& args) {
		if (m_combat_manager) {
			m_combat_manager->DisableAutoAttack();
			AddChatSystemMessage("Auto attack OFF");
		}
	};
	m_command_registry->registerCommand(stopattack);

	Command aa;
	aa.name = "aa";
	aa.aliases = {"~"};
	aa.usage = "/aa";
	aa.description = "Toggle auto-attack on/off";
	aa.category = "Combat";
	aa.handler = [this](const std::string& args) {
		if (m_combat_manager) {
			if (m_combat_manager->IsAutoAttackEnabled()) {
				m_combat_manager->DisableAutoAttack();
				AddChatSystemMessage("Auto attack OFF");
			} else if (m_combat_manager->GetTargetId() != 0) {
				m_combat_manager->EnableAutoAttack();
				AddChatSystemMessage("Auto attack ON");
			} else {
				AddChatSystemMessage("No target - auto attack not enabled");
			}
		}
	};
	m_command_registry->registerCommand(aa);

	Command combat;
	combat.name = "combat";
	combat.aliases = {};
	combat.usage = "/combat [on|off]";
	combat.description = "Enable/disable combat manager (auto-attack, hunting, etc.)";
	combat.category = "Combat";
	combat.handler = [this](const std::string& args) {
		if (!m_combat_manager) return;

		std::string arg = args;
		// Trim whitespace
		while (!arg.empty() && arg[0] == ' ') arg = arg.substr(1);
		while (!arg.empty() && arg.back() == ' ') arg.pop_back();

		if (arg.empty()) {
			// Toggle
			if (m_combat_manager->IsEnabled()) {
				m_combat_manager->Disable();
				AddChatSystemMessage("Combat manager disabled");
			} else {
				m_combat_manager->Enable();
				AddChatSystemMessage("Combat manager enabled");
			}
		} else if (arg == "on" || arg == "1" || arg == "true") {
			m_combat_manager->Enable();
			AddChatSystemMessage("Combat manager enabled");
		} else if (arg == "off" || arg == "0" || arg == "false") {
			m_combat_manager->Disable();
			AddChatSystemMessage("Combat manager disabled");
		} else {
			AddChatSystemMessage("Usage: /combat [on|off]");
		}
	};
	m_command_registry->registerCommand(combat);

	// === Utility Commands ===

	Command help;
	help.name = "help";
	help.aliases = {"h", "?"};
	help.usage = "/help [command]";
	help.description = "Show help for commands";
	help.category = "Utility";
	help.handler = [this](const std::string& args) {
		if (args.empty()) {
			// Show all categories and commands
			AddChatSystemMessage("=== Available Commands ===");
			for (const auto& category : m_command_registry->getCategories()) {
				auto cmds = m_command_registry->getCommandsByCategory(category);
				if (!cmds.empty()) {
					std::string cmdList;
					for (const auto* cmd : cmds) {
						if (!cmdList.empty()) cmdList += ", ";
						cmdList += cmd->name;
					}
					AddChatSystemMessage(fmt::format("{}: {}", category, cmdList));
				}
			}
			AddChatSystemMessage("Type /help <command> for detailed help.");
		} else {
			// Show help for specific command
			const auto* cmd = m_command_registry->findCommand(args);
			if (cmd) {
				AddChatSystemMessage(fmt::format("=== {} ===", cmd->name));
				AddChatSystemMessage(cmd->description);
				AddChatSystemMessage(fmt::format("Usage: {}", cmd->usage));
				if (!cmd->aliases.empty()) {
					std::string aliases;
					for (const auto& a : cmd->aliases) {
						if (!aliases.empty()) aliases += ", ";
						aliases += "/" + a;
					}
					AddChatSystemMessage(fmt::format("Aliases: {}", aliases));
				}
			} else {
				AddChatSystemMessage(fmt::format("Unknown command: {}", args));
			}
		}
	};
	m_command_registry->registerCommand(help);

	Command quit;
	quit.name = "quit";
	quit.aliases = {"exit"};
	quit.usage = "/quit";
	quit.description = "Exit the game";
	quit.category = "Utility";
	quit.handler = [this](const std::string& args) {
		AddChatSystemMessage("Use /camp to logout safely, or /q to exit immediately.");
	};
	m_command_registry->registerCommand(quit);

	Command q;
	q.name = "q";
	q.usage = "/q";
	q.description = "Exit the game immediately";
	q.category = "Utility";
	q.handler = [this](const std::string& args) {
#ifdef EQT_HAS_GRAPHICS
		if (m_renderer) {
			m_renderer->requestQuit();
		}
#endif
	};
	m_command_registry->registerCommand(q);

	Command camp;
	camp.name = "camp";
	camp.usage = "/camp";
	camp.description = "Sit down and camp out (logout)";
	camp.category = "Utility";
	camp.handler = [this](const std::string& args) {
		// Sit down
		SetPositionState(POS_SITTING);
		AddChatSystemMessage("You have begun to camp. You will log out in 30 seconds.");
		// Start camp timer
		StartCampTimer();
	};
	m_command_registry->registerCommand(camp);

	Command debug;
	debug.name = "debug";
	debug.usage = "/debug <0-6>";
	debug.description = "Set debug level";
	debug.category = "Utility";
	debug.handler = [this](const std::string& args) {
		if (args.empty()) {
			AddChatSystemMessage(fmt::format("Debug level: {}", s_debug_level));
		} else {
			try {
				int level = std::stoi(args);
				SetDebugLevel(level);
				AddChatSystemMessage(fmt::format("Debug level set to {}", level));
			} catch (...) {
				AddChatSystemMessage("Usage: /debug <0-6>");
			}
		}
	};
	m_command_registry->registerCommand(debug);

	Command timestamp;
	timestamp.name = "timestamp";
	timestamp.aliases = {"timestamps", "time"};
	timestamp.usage = "/timestamp";
	timestamp.description = "Toggle timestamps in chat";
	timestamp.category = "Utility";
	timestamp.handler = [this](const std::string& args) {
		if (m_renderer) {
			if (auto* chatWindow = m_renderer->getWindowManager()->getChatWindow()) {
				chatWindow->toggleTimestamps();
				chatWindow->saveSettings();  // Persist the setting
				if (chatWindow->getShowTimestamps()) {
					AddChatSystemMessage("Timestamps enabled");
				} else {
					AddChatSystemMessage("Timestamps disabled");
				}
			}
		}
	};
	m_command_registry->registerCommand(timestamp);

	Command filter;
	filter.name = "filter";
	filter.usage = "/filter [channel]";
	filter.description = "Toggle chat channel filter";
	filter.detailedHelp = "Channels: say, tell, group, guild, shout, auction, ooc, emote, combat, exp, loot, npc, all";
	filter.category = "Utility";
	filter.handler = [this](const std::string& args) {
		if (!m_renderer) return;
		auto* chatWindow = m_renderer->getWindowManager()->getChatWindow();
		if (!chatWindow) return;

		if (args.empty()) {
			// Show current filter status
			AddChatSystemMessage("=== Chat Filter Status ===");
			AddChatSystemMessage(fmt::format("Say: {}", chatWindow->isChannelEnabled(eqt::ui::ChatChannel::Say) ? "ON" : "OFF"));
			AddChatSystemMessage(fmt::format("Tell: {}", chatWindow->isChannelEnabled(eqt::ui::ChatChannel::Tell) ? "ON" : "OFF"));
			AddChatSystemMessage(fmt::format("Group: {}", chatWindow->isChannelEnabled(eqt::ui::ChatChannel::Group) ? "ON" : "OFF"));
			AddChatSystemMessage(fmt::format("Guild: {}", chatWindow->isChannelEnabled(eqt::ui::ChatChannel::Guild) ? "ON" : "OFF"));
			AddChatSystemMessage(fmt::format("Shout: {}", chatWindow->isChannelEnabled(eqt::ui::ChatChannel::Shout) ? "ON" : "OFF"));
			AddChatSystemMessage(fmt::format("Auction: {}", chatWindow->isChannelEnabled(eqt::ui::ChatChannel::Auction) ? "ON" : "OFF"));
			AddChatSystemMessage(fmt::format("OOC: {}", chatWindow->isChannelEnabled(eqt::ui::ChatChannel::OOC) ? "ON" : "OFF"));
			AddChatSystemMessage(fmt::format("Emote: {}", chatWindow->isChannelEnabled(eqt::ui::ChatChannel::Emote) ? "ON" : "OFF"));
			AddChatSystemMessage(fmt::format("Combat: {}", chatWindow->isChannelEnabled(eqt::ui::ChatChannel::Combat) ? "ON" : "OFF"));
			AddChatSystemMessage(fmt::format("Exp: {}", chatWindow->isChannelEnabled(eqt::ui::ChatChannel::Experience) ? "ON" : "OFF"));
			AddChatSystemMessage(fmt::format("Loot: {}", chatWindow->isChannelEnabled(eqt::ui::ChatChannel::Loot) ? "ON" : "OFF"));
			AddChatSystemMessage(fmt::format("NPC: {}", chatWindow->isChannelEnabled(eqt::ui::ChatChannel::NPCDialogue) ? "ON" : "OFF"));
			AddChatSystemMessage("Type /filter <channel> to toggle.");
			return;
		}

		std::string channel = args;
		std::transform(channel.begin(), channel.end(), channel.begin(), ::tolower);

		eqt::ui::ChatChannel ch;
		bool found = true;
		if (channel == "say" || channel == "s") {
			ch = eqt::ui::ChatChannel::Say;
		} else if (channel == "tell" || channel == "t") {
			ch = eqt::ui::ChatChannel::Tell;
		} else if (channel == "group" || channel == "g") {
			ch = eqt::ui::ChatChannel::Group;
		} else if (channel == "guild" || channel == "gu") {
			ch = eqt::ui::ChatChannel::Guild;
		} else if (channel == "shout" || channel == "sho") {
			ch = eqt::ui::ChatChannel::Shout;
		} else if (channel == "auction" || channel == "auc") {
			ch = eqt::ui::ChatChannel::Auction;
		} else if (channel == "ooc" || channel == "o") {
			ch = eqt::ui::ChatChannel::OOC;
		} else if (channel == "emote" || channel == "em") {
			ch = eqt::ui::ChatChannel::Emote;
		} else if (channel == "combat") {
			ch = eqt::ui::ChatChannel::Combat;
		} else if (channel == "exp" || channel == "experience") {
			ch = eqt::ui::ChatChannel::Experience;
		} else if (channel == "loot") {
			ch = eqt::ui::ChatChannel::Loot;
		} else if (channel == "npc") {
			ch = eqt::ui::ChatChannel::NPCDialogue;
		} else if (channel == "all") {
			chatWindow->enableAllChannels();
			chatWindow->saveSettings();
			AddChatSystemMessage("All channels enabled");
			return;
		} else if (channel == "none") {
			chatWindow->disableAllChannels();
			chatWindow->saveSettings();
			AddChatSystemMessage("All channels disabled (except system)");
			return;
		} else {
			found = false;
		}

		if (found) {
			chatWindow->toggleChannel(ch);
			chatWindow->saveSettings();
			const char* chName = eqt::ui::getChannelName(ch);
			AddChatSystemMessage(fmt::format("{} filter: {}", chName, chatWindow->isChannelEnabled(ch) ? "ON" : "OFF"));
		} else {
			AddChatSystemMessage(fmt::format("Unknown channel: {}. Use /filter for list.", args));
		}
	};
	m_command_registry->registerCommand(filter);

	Command who;
	who.name = "who";
	who.usage = "/who [filter]";
	who.description = "List nearby entities";
	who.category = "Social";
	who.handler = [this](const std::string& args) {
		std::string filter = args;
		std::transform(filter.begin(), filter.end(), filter.begin(), ::tolower);

		int count = 0;
		for (const auto& [id, entity] : m_entities) {
			std::string name = entity.name;
			std::transform(name.begin(), name.end(), name.begin(), ::tolower);
			if (filter.empty() || name.find(filter) != std::string::npos) {
				AddChatSystemMessage(fmt::format("[{}] {} (Lvl {})", entity.spawn_id, EQT::toDisplayName(entity.name), entity.level));
				count++;
				if (count >= 20) {
					AddChatSystemMessage(fmt::format("... and {} more", m_entities.size() - count));
					break;
				}
			}
		}
		if (count == 0) {
			AddChatSystemMessage("No entities found.");
		}
	};
	m_command_registry->registerCommand(who);

	// === Spell Commands ===

	Command cast;
	cast.name = "cast";
	cast.usage = "/cast <gem#>";
	cast.description = "Cast spell from gem slot (1-8)";
	cast.category = "Spells";
	cast.handler = [this](const std::string& args) {
		if (!m_spell_manager) {
			AddChatSystemMessage("Spell system not initialized");
			return;
		}
		if (args.empty()) {
			AddChatSystemMessage("Usage: /cast <gem# 1-8>");
			return;
		}
		try {
			int gem = std::stoi(args);
			if (gem < 1 || gem > 8) {
				AddChatSystemMessage("Gem slot must be 1-8");
				return;
			}
			uint16_t target_id = m_combat_manager ? m_combat_manager->GetTargetId() : 0;
			EQ::CastResult result = m_spell_manager->beginCastFromGem(static_cast<uint8_t>(gem - 1), target_id);
			switch (result) {
				case EQ::CastResult::Success:
					AddChatSystemMessage(fmt::format("Casting from gem {}", gem));
					break;
				case EQ::CastResult::NotMemorized:
					AddChatSystemMessage(fmt::format("No spell in gem {}", gem));
					break;
				case EQ::CastResult::NotEnoughMana:
					AddChatSystemMessage("Insufficient mana");
					break;
				case EQ::CastResult::SpellNotReady:
					AddChatSystemMessage("Spell not ready");
					break;
				case EQ::CastResult::AlreadyCasting:
					AddChatSystemMessage("Already casting");
					break;
				default:
					AddChatSystemMessage("Cannot cast spell");
					break;
			}
		} catch (...) {
			AddChatSystemMessage("Usage: /cast <gem# 1-8>");
		}
	};
	m_command_registry->registerCommand(cast);

	Command mem;
	mem.name = "mem";
	mem.aliases = {"memspell", "memorize"};
	mem.usage = "/mem <gem#> <spell_name>";
	mem.description = "Memorize spell to gem slot";
	mem.category = "Spells";
	mem.handler = [this](const std::string& args) {
		if (!m_spell_manager) {
			AddChatSystemMessage("Spell system not initialized");
			return;
		}
		// Parse: gem# spell_name
		std::istringstream iss(args);
		int gem;
		std::string spell_name;
		if (!(iss >> gem)) {
			AddChatSystemMessage("Usage: /mem <gem# 1-8> <spell_name>");
			return;
		}
		std::getline(iss >> std::ws, spell_name);
		if (spell_name.empty()) {
			AddChatSystemMessage("Usage: /mem <gem# 1-8> <spell_name>");
			return;
		}
		if (gem < 1 || gem > 8) {
			AddChatSystemMessage("Gem slot must be 1-8");
			return;
		}
		// Find spell by name
		const EQ::SpellData* spell = m_spell_manager->getDatabase().getSpellByName(spell_name);
		if (!spell) {
			// Try partial match
			auto matches = m_spell_manager->getDatabase().findSpellsByName(spell_name);
			if (matches.empty()) {
				AddChatSystemMessage(fmt::format("Spell '{}' not found", spell_name));
				return;
			}
			if (matches.size() == 1) {
				spell = matches[0];
			} else {
				// Limit to first 5 matches
				AddChatSystemMessage("Multiple matches found:");
				size_t count = 0;
				for (const auto* s : matches) {
					AddChatSystemMessage(fmt::format("  {} (ID: {})", s->name, s->id));
					if (++count >= 5) {
						if (matches.size() > 5) {
							AddChatSystemMessage(fmt::format("  ... and {} more", matches.size() - 5));
						}
						break;
					}
				}
				return;
			}
		}
		if (m_spell_manager->memorizeSpell(spell->id, static_cast<uint8_t>(gem - 1))) {
			AddChatSystemMessage(fmt::format("Memorizing {} in gem {}", spell->name, gem));
		} else {
			AddChatSystemMessage(fmt::format("Cannot memorize {} - check if scribed and level requirement", spell->name));
		}
	};
	m_command_registry->registerCommand(mem);

	Command forget;
	forget.name = "forget";
	forget.usage = "/forget <gem#>";
	forget.description = "Forget spell from gem slot";
	forget.category = "Spells";
	forget.handler = [this](const std::string& args) {
		if (!m_spell_manager) {
			AddChatSystemMessage("Spell system not initialized");
			return;
		}
		if (args.empty()) {
			AddChatSystemMessage("Usage: /forget <gem# 1-8>");
			return;
		}
		try {
			int gem = std::stoi(args);
			if (gem < 1 || gem > 8) {
				AddChatSystemMessage("Gem slot must be 1-8");
				return;
			}
			if (m_spell_manager->forgetSpell(static_cast<uint8_t>(gem - 1))) {
				AddChatSystemMessage(fmt::format("Forgot spell in gem {}", gem));
			} else {
				AddChatSystemMessage(fmt::format("No spell in gem {}", gem));
			}
		} catch (...) {
			AddChatSystemMessage("Usage: /forget <gem# 1-8>");
		}
	};
	m_command_registry->registerCommand(forget);

	Command spellbook;
	spellbook.name = "spellbook";
	spellbook.aliases = {"book"};
	spellbook.usage = "/spellbook";
	spellbook.description = "Open spellbook window";
	spellbook.category = "Spells";
	spellbook.handler = [this](const std::string& args) {
		AddChatSystemMessage("Spellbook window not yet implemented in UI");
		// TODO: Open spellbook window when UI is implemented
	};
	m_command_registry->registerCommand(spellbook);

	Command skills;
	skills.name = "skills";
	skills.usage = "/skills";
	skills.description = "Toggle skills window";
	skills.category = "Utility";
	skills.handler = [this](const std::string& args) {
#ifdef EQT_HAS_GRAPHICS
		if (m_renderer && m_renderer->getWindowManager()) {
			m_renderer->getWindowManager()->toggleSkillsWindow();
		} else {
			AddChatSystemMessage("Skills window requires graphics mode");
		}
#else
		AddChatSystemMessage("Skills window requires graphics mode");
#endif
	};
	m_command_registry->registerCommand(skills);

	Command bank;
	bank.name = "bank";
	bank.usage = "/bank";
	bank.description = "Toggle bank window";
	bank.category = "Utility";
	bank.handler = [this](const std::string& args) {
#ifdef EQT_HAS_GRAPHICS
		if (IsBankWindowOpen()) {
			CloseBankWindow();
		} else {
			OpenBankWindow();
		}
#else
		AddChatSystemMessage("Bank window requires graphics mode");
#endif
	};
	m_command_registry->registerCommand(bank);

	Command train;
	train.name = "train";
	train.usage = "/train";
	train.description = "Open trainer window with current target";
	train.category = "Skills";
	train.handler = [this](const std::string& args) {
#ifdef EQT_HAS_GRAPHICS
		// Check if we have a target
		uint16_t target_id = m_combat_manager ? m_combat_manager->GetTargetId() : 0;
		if (target_id == 0) {
			AddChatSystemMessage("You must have a trainer targeted.");
			return;
		}

		// Check if target exists
		auto it = m_entities.find(target_id);
		if (it == m_entities.end()) {
			AddChatSystemMessage("Target not found.");
			return;
		}

		const Entity& target = it->second;

		// Check if target is an NPC (not player, not corpse)
		if (target.npc_type != 1) {
			AddChatSystemMessage("That is not an NPC.");
			return;
		}

		// Check if target is a guildmaster trainer (class 20-35)
		// Reference: EQEmu classes.h - WarriorGM=20 through BerserkerGM=35
		constexpr uint8_t CLASS_WARRIOR_GM = 20;
		constexpr uint8_t CLASS_BERSERKER_GM = 35;
		if (target.class_id < CLASS_WARRIOR_GM || target.class_id > CLASS_BERSERKER_GM) {
			AddChatSystemMessage("That is not a trainer.");
			return;
		}

		// Request trainer window from server
		RequestTrainerWindow(target_id);
#else
		AddChatSystemMessage("Training requires graphics mode.");
#endif
	};
	m_command_registry->registerCommand(train);

	Command gems;
	gems.name = "gems";
	gems.usage = "/gems";
	gems.description = "Show memorized spells";
	gems.category = "Spells";
	gems.handler = [this](const std::string& args) {
		if (!m_spell_manager) {
			AddChatSystemMessage("Spell system not initialized");
			return;
		}
		AddChatSystemMessage("=== Spell Gems ===");
		for (int i = 0; i < 8; i++) {
			uint32_t spell_id = m_spell_manager->getMemorizedSpell(static_cast<uint8_t>(i));
			if (spell_id != EQ::SPELL_UNKNOWN && spell_id != 0xFFFFFFFF) {
				const EQ::SpellData* spell = m_spell_manager->getSpell(spell_id);
				std::string state_str;
				EQ::GemState state = m_spell_manager->getGemState(static_cast<uint8_t>(i));
				switch (state) {
					case EQ::GemState::Ready: state_str = "Ready"; break;
					case EQ::GemState::Refresh:
						state_str = fmt::format("Refresh ({}s)",
							m_spell_manager->getGemCooldownRemaining(static_cast<uint8_t>(i)) / 1000);
						break;
					case EQ::GemState::MemorizeProgress:
						state_str = fmt::format("Memorizing ({:.0f}%)",
							m_spell_manager->getMemorizeProgress(static_cast<uint8_t>(i)) * 100);
						break;
					case EQ::GemState::Casting: state_str = "Casting"; break;
					default: state_str = ""; break;
				}
				AddChatSystemMessage(fmt::format("[{}] {} {}", i + 1,
					spell ? spell->name : fmt::format("Unknown({})", spell_id),
					state_str));
			} else {
				AddChatSystemMessage(fmt::format("[{}] <empty>", i + 1));
			}
		}
	};
	m_command_registry->registerCommand(gems);

	Command interrupt;
	interrupt.name = "interrupt";
	interrupt.aliases = {"ducking"};
	interrupt.usage = "/interrupt";
	interrupt.description = "Interrupt current spell cast";
	interrupt.category = "Spells";
	interrupt.handler = [this](const std::string& args) {
		if (!m_spell_manager) {
			AddChatSystemMessage("Spell system not initialized");
			return;
		}
		if (m_spell_manager->isCasting()) {
			m_spell_manager->interruptCast();
			AddChatSystemMessage("Casting interrupted");
		} else {
			AddChatSystemMessage("Not casting");
		}
	};
	m_command_registry->registerCommand(interrupt);
}
#endif

// UCS (Universal Chat Service) connection implementation
// TODO: Fix UCS implementation to use correct EQStream API
/*
void EverQuest::ConnectToUCS(const std::string& host, uint16_t port)
{
	if (s_debug_level >= 1) {
		std::cout << fmt::format("Connecting to UCS at {}:{}", host, port) << std::endl;
	}
	
	// UCS uses EQStream with legacy options (1-byte opcodes)
	EQ::Net::EQStreamManagerInterfaceOptions ucs_opts(port, false, false);
	ucs_opts.opcode_size = 1;  // Legacy 1-byte opcodes
	ucs_opts.daybreak_options.stale_connection_ms = 600000;
	
	m_ucs_stream_manager = std::make_unique<EQ::Net::EQStreamManager>(ucs_opts);
	
	m_ucs_stream_manager->OnNewConnection([this](std::shared_ptr<EQ::Net::EQStream> stream) {
		UCSOnNewConnection(stream);
	});
	
	m_ucs_stream_manager->OnConnectionLost([this](std::shared_ptr<EQ::Net::EQStream> stream) {
		UCSOnConnectionLost();
	});
	
	// Connect to UCS server
	m_ucs_stream = m_ucs_stream_manager->CreateConnection(host, port);
	m_ucs_stream->SetStreamType(EQ::Net::EQStreamType::Chat);
}

void EverQuest::UCSOnNewConnection(std::shared_ptr<EQ::Net::EQStream> stream)
{
	if (s_debug_level >= 1) {
		std::cout << "UCS connection established" << std::endl;
	}
	
	m_ucs_stream = stream;
	m_ucs_connected = true;
	
	// Set packet callback
	m_ucs_stream->SetPacketCallback([this](const EQ::Net::Packet &p) {
		UCSOnPacketRecv(p);
	});
	
	// Send mail login packet to authenticate
	UCSSendMailLogin();
}

void EverQuest::UCSOnConnectionLost()
{
	if (s_debug_level >= 1) {
		std::cout << "UCS connection lost" << std::endl;
	}
	
	m_ucs_connected = false;
	m_ucs_stream.reset();
}

void EverQuest::UCSOnPacketRecv(const EQ::Net::Packet &p)
{
	if (p.Length() < 1) return;
	
	uint8_t opcode = p.GetUInt8(0);
	
	if (s_debug_level >= 2) {
		std::cout << fmt::format("UCS packet received: opcode={:#04x}, size={}", opcode, p.Length()) << std::endl;
	}
	
	switch (opcode) {
		case HC_OP_ChatMessage:
			UCSProcessChatMessage(p);
			break;
		case HC_OP_UCS_MailNew:
			UCSProcessMailNew(p);
			break;
		case HC_OP_UCS_Buddy:
			UCSProcessBuddy(p);
			break;
		default:
			if (s_debug_level >= 1) {
				std::cout << fmt::format("Unhandled UCS opcode: {:#04x}", opcode) << std::endl;
			}
			break;
	}
}

void EverQuest::UCSSendMailLogin()
{
	if (!m_ucs_stream || !m_ucs_connected) {
		return;
	}
	
	// MailLogin packet format: opcode(1) + mail_key(null-terminated string)
	size_t key_len = m_mail_key.length() + 1; // Include null terminator
	size_t packet_size = 1 + key_len;
	
	EQ::Net::DynamicPacket p;
	p.Resize(packet_size);
	
	p.PutUInt8(0, HC_OP_UCS_MailLogin);
	memcpy((char*)p.Data() + 1, m_mail_key.c_str(), key_len);
	
	if (s_debug_level >= 1) {
		std::cout << fmt::format("Sending UCS MailLogin with key: {}", m_mail_key) << std::endl;
	}
	
	m_ucs_stream->QueuePacket(p);
}

void EverQuest::UCSSendTell(const std::string &to, const std::string &message)
{
	if (!m_ucs_stream || !m_ucs_connected) {
		if (s_debug_level >= 1) {
			std::cout << "UCS not connected, cannot send tell" << std::endl;
		}
		return;
	}
	
	// Tell format via UCS: opcode(1) + target(null-terminated) + sender(null-terminated) + message(null-terminated)
	size_t to_len = to.length() + 1;
	size_t from_len = m_character.length() + 1;
	size_t msg_len = message.length() + 1;
	size_t packet_size = 1 + to_len + from_len + msg_len;
	
	EQ::Net::DynamicPacket p;
	p.Resize(packet_size);
	
	size_t offset = 0;
	p.PutUInt8(offset++, HC_OP_ChatMessage);
	
	memcpy((char*)p.Data() + offset, to.c_str(), to_len);
	offset += to_len;
	
	memcpy((char*)p.Data() + offset, m_character.c_str(), from_len);
	offset += from_len;
	
	memcpy((char*)p.Data() + offset, message.c_str(), msg_len);
	
	if (s_debug_level >= 1) {
		std::cout << fmt::format("Sending UCS tell to {}: {}", to, message) << std::endl;
	}
	
	m_ucs_stream->QueuePacket(p);
}

void EverQuest::UCSSendJoinChannel(const std::string &channel)
{
	if (!m_ucs_stream || !m_ucs_connected) {
		return;
	}
	
	// Join channel format: opcode(1) + channel_name(null-terminated) + character_name(null-terminated)
	size_t ch_len = channel.length() + 1;
	size_t name_len = m_character.length() + 1;
	size_t packet_size = 1 + ch_len + name_len;
	
	EQ::Net::DynamicPacket p;
	p.Resize(packet_size);
	
	size_t offset = 0;
	p.PutUInt8(offset++, HC_OP_UCS_ChatJoin);
	
	memcpy((char*)p.Data() + offset, channel.c_str(), ch_len);
	offset += ch_len;
	
	memcpy((char*)p.Data() + offset, m_character.c_str(), name_len);
	
	if (s_debug_level >= 1) {
		std::cout << fmt::format("Joining UCS channel: {}", channel) << std::endl;
	}
	
	m_ucs_stream->QueuePacket(p);
}

void EverQuest::UCSSendLeaveChannel(const std::string &channel)
{
	if (!m_ucs_stream || !m_ucs_connected) {
		return;
	}
	
	// Leave channel format: opcode(1) + channel_name(null-terminated) + character_name(null-terminated)
	size_t ch_len = channel.length() + 1;
	size_t name_len = m_character.length() + 1;
	size_t packet_size = 1 + ch_len + name_len;
	
	EQ::Net::DynamicPacket p;
	p.Resize(packet_size);
	
	size_t offset = 0;
	p.PutUInt8(offset++, HC_OP_UCS_ChatLeave);
	
	memcpy((char*)p.Data() + offset, channel.c_str(), ch_len);
	offset += ch_len;
	
	memcpy((char*)p.Data() + offset, m_character.c_str(), name_len);
	
	if (s_debug_level >= 1) {
		std::cout << fmt::format("Leaving UCS channel: {}", channel) << std::endl;
	}
	
	m_ucs_stream->QueuePacket(p);
}

void EverQuest::UCSProcessChatMessage(const EQ::Net::Packet &p)
{
	if (p.Length() < 2) return;
	
	// UCS chat message format: opcode(1) + channel(null-terminated) + sender(null-terminated) + message(null-terminated)
	size_t offset = 1; // Skip opcode
	
	std::string channel = p.GetCString(offset);
	offset += channel.length() + 1;
	
	if (offset >= p.Length()) return;
	
	std::string sender = p.GetCString(offset);
	offset += sender.length() + 1;
	
	if (offset >= p.Length()) return;
	
	std::string message = p.GetCString(offset);
	
	if (s_debug_level >= 1) {
		std::cout << fmt::format("[UCS] {}.{}: {}", channel, sender, message) << std::endl;
	}
}

void EverQuest::UCSProcessMailNew(const EQ::Net::Packet &p)
{
	if (s_debug_level >= 1) {
		std::cout << "New mail notification received" << std::endl;
	}
}

void EverQuest::UCSProcessBuddy(const EQ::Net::Packet &p)
{
	if (s_debug_level >= 1) {
		std::cout << "Buddy list update received" << std::endl;
	}
}
*/

// Movement implementation
void EverQuest::Move(float x, float y, float z)
{
	LOG_DEBUG(MOD_MAIN, "Move called: target=({:.2f}, {:.2f}, {:.2f})", x, y, z);
	// Use pathfinding by default
	MoveToWithPath(x, y, z);
	
	// Non-blocking - movement will be handled by the update system
}

void EverQuest::MoveToEntity(const std::string &name)
{
	Entity* entity = FindEntityByName(name);
	if (entity) {
		if (s_debug_level >= 1) {
			std::cout << fmt::format("Found entity '{}' at ({:.2f}, {:.2f}, {:.2f})", 
				entity->name, entity->x, entity->y, entity->z) << std::endl;
		}
		LOG_DEBUG(MOD_MAIN, "MoveToEntity: Moving to {} at ({:.2f}, {:.2f}, {:.2f})", 
			entity->name, entity->x, entity->y, entity->z);
		Move(entity->x, entity->y, entity->z);  // This will block until complete
	} else {
		std::cout << fmt::format("Entity '{}' not found", name) << std::endl;
	}
}

void EverQuest::MoveToEntityWithinRange(const std::string &name, float stop_distance)
{
	Entity* entity = FindEntityByName(name);
	if (entity) {
		if (s_debug_level >= 1) {
			std::cout << fmt::format("Found entity '{}' at ({:.2f}, {:.2f}, {:.2f}), stopping within {:.1f} units", 
				entity->name, entity->x, entity->y, entity->z, stop_distance) << std::endl;
		}
		
		// Calculate current distance
		float dx = entity->x - m_x;
		float dy = entity->y - m_y;
		float dz = entity->z - m_z;
		float dist = std::sqrt(dx*dx + dy*dy + dz*dz);
		
		// If we're already within range, don't move
		if (dist <= stop_distance) {
			if (s_debug_level >= 1) {
				std::cout << fmt::format("Already within range ({:.1f} <= {:.1f})", dist, stop_distance) << std::endl;
			}
			return;
		}
		
		// Predict where the target will be based on its movement
		// Use a prediction time based on how long it would take us to reach them
		float our_speed = 4.3f;  // Approximate run speed
		float time_to_reach = dist / our_speed;
		
		// Limit prediction time to avoid overshooting
		if (time_to_reach > 2.0f) time_to_reach = 2.0f;
		
		// Calculate predicted position
		float predicted_x = entity->x + entity->delta_x * time_to_reach;
		float predicted_y = entity->y + entity->delta_y * time_to_reach;
		float predicted_z = entity->z + entity->delta_z * time_to_reach;
		
		// Calculate direction to predicted position
		dx = predicted_x - m_x;
		dy = predicted_y - m_y;
		dz = predicted_z - m_z;
		dist = std::sqrt(dx*dx + dy*dy + dz*dz);
		
		// Calculate position to stop at (stop_distance units away from predicted position)
		// We want to move to 80% of the stop distance to ensure we're well within range
		float move_ratio = (dist - stop_distance * 0.8f) / dist;
		float target_x = m_x + dx * move_ratio;
		float target_y = m_y + dy * move_ratio;
		float target_z = m_z + dz * move_ratio;
		
		if (s_debug_level >= 2) {
			LOG_DEBUG(MOD_MAIN, "Predictive targeting: entity at ({:.1f},{:.1f},{:.1f}), velocity ({:.1f},{:.1f},{:.1f})", entity->x, entity->y, entity->z, entity->delta_x, entity->delta_y, entity->delta_z);
			LOG_DEBUG(MOD_MAIN, "Predicted position in {:.1f}s: ({:.1f},{:.1f},{:.1f})", time_to_reach, predicted_x, predicted_y, predicted_z);
		}
		
		LOG_DEBUG(MOD_MAIN, "MoveToEntityWithinRange: Moving to ({:.2f}, {:.2f}, {:.2f}) to be within {:.1f} of {}", 
			target_x, target_y, target_z, stop_distance, entity->name);
		
		// For combat movement, don't block - let UpdateCombatMovement handle continuous tracking
		if (m_in_combat_movement) {
			// Just set the target and let the movement system handle it
			m_target_x = target_x;
			m_target_y = target_y;
			m_target_z = target_z;
			m_is_moving = true;
			m_heading = CalculateHeading(m_x, m_y, predicted_x, predicted_y);
			
			if (s_debug_level >= 1) {
				LOG_DEBUG(MOD_MAIN, "Non-blocking combat movement initiated");
			}
		} else {
			Move(target_x, target_y, target_z);  // This will block until complete
		}
	} else {
		std::cout << fmt::format("Entity '{}' not found", name) << std::endl;
	}
}

void EverQuest::Follow(const std::string &name)
{
	Entity* entity = FindEntityByName(name);
	if (entity) {
		m_follow_target = entity->name;  // Store the actual entity name
		std::cout << fmt::format("Following {}", entity->name) << std::endl;
		
		// Calculate initial path if pathfinding is enabled
		if (m_use_pathfinding && m_pathfinder) {
			LOG_DEBUG(MOD_MAIN, "Follow: Pathfinding enabled, m_pathfinder={}", 
				m_pathfinder ? "valid" : "null");
			float dist = CalculateDistance2D(m_x, m_y, entity->x, entity->y);
			LOG_DEBUG(MOD_MAIN, "Follow: Distance to target: {:.2f}, follow_distance: {:.2f}", 
				dist, m_follow_distance);
			if (dist > m_follow_distance) {
				// Find path from current position to target
				LOG_DEBUG(MOD_MAIN, "Follow: Calculating path from ({:.2f},{:.2f},{:.2f}) to ({:.2f},{:.2f},{:.2f})", m_x, m_y, m_z, entity->x, entity->y, entity->z);
				if (FindPath(m_x, m_y, m_z, entity->x, entity->y, entity->z)) {
					// Start following the path
					m_current_path_index = 0;
					m_is_moving = true;
					if (IsDebugEnabled()) std::cout << fmt::format("[DEBUG] Follow: Path calculated successfully with {} waypoints", 
						m_current_path.size()) << std::endl;
					for (size_t i = 0; i < std::min(m_current_path.size(), size_t(5)); i++) {
						std::cout << fmt::format("  Waypoint {}: ({:.2f},{:.2f},{:.2f})", 
							i, m_current_path[i].x, m_current_path[i].y, m_current_path[i].z) << std::endl;
					}
				} else {
					// Pathfinding failed, use direct movement
					LOG_DEBUG(MOD_MAIN, "Follow: Pathfinding failed, using direct movement");
					m_target_x = entity->x;
					m_target_y = entity->y;
					m_target_z = entity->z;
					m_is_moving = true;
				}
			}
		} else {
			LOG_DEBUG(MOD_MAIN, "Follow: Pathfinding disabled (m_use_pathfinding={}, m_pathfinder={})", m_use_pathfinding, m_pathfinder ? "valid" : "null");
		}
	} else {
		std::cout << fmt::format("Entity '{}' not found", name) << std::endl;
	}
}

void EverQuest::StopFollow()
{
	if (!m_follow_target.empty()) {
		std::cout << fmt::format("Stopped following {}", m_follow_target) << std::endl;
		m_follow_target.clear();
	}
	StopMovement();
}

void EverQuest::StartCombatMovement(const std::string &name, float stop_distance)
{
	Entity* entity = FindEntityByName(name);
	if (entity) {
		m_combat_target = entity->name;
		m_combat_stop_distance = stop_distance;
		m_in_combat_movement = true;
		m_last_combat_movement_update = std::chrono::steady_clock::now();
		
		if (s_debug_level >= 1) {
			std::cout << fmt::format("Starting combat movement to '{}' with stop distance {:.1f}", 
				entity->name, stop_distance) << std::endl;
		}
		
		// Start initial movement immediately
		UpdateCombatMovement();
	} else {
		std::cout << fmt::format("Combat target '{}' not found", name) << std::endl;
	}
}

void EverQuest::UpdateCombatMovement()
{
	if (!m_in_combat_movement || m_combat_target.empty()) {
		return;
	}
	
	Entity* entity = FindEntityByName(m_combat_target);
	if (!entity) {
		// Target disappeared
		m_in_combat_movement = false;
		StopMovement();
		return;
	}
	
	// Calculate current distance
	float dx = entity->x - m_x;
	float dy = entity->y - m_y;
	float dz = entity->z - m_z;
	float dist = std::sqrt(dx*dx + dy*dy + dz*dz);
	
	// Check if we're within range
	if (dist <= m_combat_stop_distance) {
		// We're in range, stop if moving
		if (m_is_moving) {
			StopMovement();
		}
		// Keep combat movement active to track if target moves away
		return;
	}
	
	// We're out of range, need to move closer
	// Only update movement if target has moved significantly or enough time has passed
	auto now = std::chrono::steady_clock::now();
	auto time_since_update = std::chrono::duration_cast<std::chrono::milliseconds>(
		now - m_last_combat_movement_update).count();
	
	// For hunting, always update to track the moving target
	bool should_update = false;
	bool is_hunting = m_combat_manager && m_combat_manager->IsAutoHunting();
	
	if (is_hunting) {
		// When hunting, always update position to track moving targets
		should_update = true;
	} else {
		// Normal combat movement - update less frequently
		int update_interval = 500;
		float movement_threshold = 5.0f;
		
		if (time_since_update >= update_interval) {
			should_update = true;
		} else if (m_is_moving) {
			// Check if target moved significantly from where we're heading
			if (!m_current_path.empty()) {
				// Check distance from path endpoint to target
				const auto& endpoint = m_current_path.back();
				float path_dx = std::abs(entity->x - endpoint.x);
				float path_dy = std::abs(entity->y - endpoint.y);
				if (path_dx > movement_threshold || path_dy > movement_threshold) {
					should_update = true;
					if (s_debug_level >= 1) {
						if (IsDebugEnabled()) std::cout << fmt::format("[DEBUG] Target moved {:.1f} units from path endpoint, updating",
							std::sqrt(path_dx*path_dx + path_dy*path_dy)) << std::endl;
					}
				}
			} else {
				// Direct movement - check against target position
				float target_dx = std::abs(entity->x - m_target_x);
				float target_dy = std::abs(entity->y - m_target_y);
				if (target_dx > movement_threshold || target_dy > movement_threshold) {
					should_update = true;
				}
			}
		}
	}
	
	if (should_update || !m_is_moving) {
		// Only update direct target if we're not following a path
		bool following_path = !m_current_path.empty() && m_current_path_index < m_current_path.size();
		
		if (!following_path) {
			// Use predictive targeting for hunting
			if (is_hunting) {
				// Predict where the target will be
				float our_speed = 4.3f;
				float time_to_reach = dist / our_speed;
				if (time_to_reach > 1.0f) time_to_reach = 1.0f; // Shorter prediction for updates
				
				float predicted_x = entity->x + entity->delta_x * time_to_reach;
				float predicted_y = entity->y + entity->delta_y * time_to_reach;
				float predicted_z = entity->z + entity->delta_z * time_to_reach;
				
				// Recalculate direction to predicted position
				dx = predicted_x - m_x;
				dy = predicted_y - m_y;
				dz = predicted_z - m_z;
				dist = std::sqrt(dx*dx + dy*dy + dz*dz);
			}
			
			// Calculate new position to move to
			float move_ratio = (dist - m_combat_stop_distance * 0.8f) / dist;
			m_target_x = m_x + dx * move_ratio;
			m_target_y = m_y + dy * move_ratio;
			m_target_z = m_z + dz * move_ratio;
			
			// Update heading and start moving
			m_heading = CalculateHeading(m_x, m_y, m_target_x, m_target_y);
			
			if (s_debug_level >= 1) {
				LOG_DEBUG(MOD_MAIN, "Combat movement (direct): Set target=({:.1f},{:.1f},{:.1f})", m_target_x, m_target_y, m_target_z);
			}
		} else {
			if (s_debug_level >= 1) {
				if (IsDebugEnabled()) std::cout << fmt::format("[DEBUG] Combat movement (path): Keeping target=({:.1f},{:.1f},{:.1f}), path_index={}/{}",
					m_target_x, m_target_y, m_target_z, m_current_path_index, m_current_path.size()) << std::endl;
			}
		}
		
		m_is_moving = true;
		m_last_combat_movement_update = now;
		
		if (s_debug_level >= 1) {
			LOG_DEBUG(MOD_MAIN, "Combat movement: from ({:.1f},{:.1f},{:.1f}) to target {} at ({:.1f},{:.1f},{:.1f}), dist={:.1f}, stop_dist={:.1f}", m_x, m_y, m_z, entity->name, entity->x, entity->y, entity->z, dist, m_combat_stop_distance);
			LOG_DEBUG(MOD_MAIN, "Combat movement: moving to ({:.1f},{:.1f},{:.1f}), m_is_moving set to {}", 
				m_target_x, m_target_y, m_target_z, m_is_moving);
		}
		
		// For now, use direct movement for combat (like the working attack command)
		// We can add pathfinding later once basic movement works
		m_current_path.clear();
		m_current_path_index = 0;
		
		if (s_debug_level >= 1) {
			LOG_DEBUG(MOD_MAIN, "Combat movement using direct approach (no pathfinding)");
		}
	}
}

void EverQuest::Face(float x, float y, float z)
{
	// Calculate heading to face the given position
	float new_heading = CalculateHeading(m_x, m_y, x, y);
	
	if (s_debug_level >= 2) {
		LOG_DEBUG(MOD_MAIN, "Face: current pos ({:.1f},{:.1f}), target ({:.1f},{:.1f}), old heading {:.1f}°, new heading {:.1f}°", m_x, m_y, x, y, m_heading, new_heading);
	}
	
	// Always update heading - even small changes should be sent
	// The previous 0.1f threshold was preventing the face command from working
	m_heading = new_heading;
	SendPositionUpdate();
}

void EverQuest::FaceEntity(const std::string &name)
{
	Entity* entity = FindEntityByName(name);
	if (entity) {
		if (s_debug_level >= 2) {
			std::cout << fmt::format("Facing entity '{}'", entity->name) << std::endl;
		}
		Face(entity->x, entity->y, entity->z);
	} else {
		std::cout << fmt::format("Entity '{}' not found", name) << std::endl;
	}
}

void EverQuest::MoveTo(float x, float y, float z)
{
	// Trace who's calling MoveTo
	if (s_debug_level >= 1 && std::abs(x - m_x) < 0.1f && std::abs(y - m_y) < 0.1f) {
		LOG_DEBUG(MOD_MAIN, "WARNING: MoveTo called with current position! target=({:.2f},{:.2f},{:.2f}) current=({:.2f},{:.2f},{:.2f})", x, y, z, m_x, m_y, m_z);
	}
	
	m_target_x = x;
	m_target_y = y;
	m_target_z = z;
	m_is_moving = true;
	
	// Calculate initial heading towards target
	float new_heading = CalculateHeading(m_x, m_y, x, y);
	m_heading = new_heading;
	// Send immediate position update to show the heading change
	SendPositionUpdate();
	
	if (s_debug_level >= 1) {
		LOG_DEBUG(MOD_MAIN, "MoveTo: Setting target=({:.2f}, {:.2f}, {:.2f}) from current=({:.2f}, {:.2f}, {:.2f}), heading={:.1f}, m_is_moving={}", 
			x, y, z, m_x, m_y, m_z, m_heading, m_is_moving);
	}
}

void EverQuest::StopMovement()
{
	if (m_is_moving) {
		m_is_moving = false;
		m_animation = ANIM_STAND;
		
		// Clear any active path
		m_current_path.clear();
		m_current_path_index = 0;
		
		SendPositionUpdate();
		
		if (s_debug_level >= 1) {
			std::cout << "Movement stopped" << std::endl;
		}
	}
}

void EverQuest::UpdateMovement()
{
	// Process deferred zone change if approved
	// This MUST be at the very start of UpdateMovement to process outside of any callback context
	if (m_zone_change_approved) {
		m_zone_change_approved = false;
		LOG_DEBUG(MOD_ZONE, "Processing deferred zone change...");
		ProcessDeferredZoneChange();
		LOG_DEBUG(MOD_ZONE, "Deferred zone change processed");
		return;  // Don't process movement while zone changing
	}

	// Update camp timer
	UpdateCampTimer();

	// Check distance to banker NPC - close bank if player moved too far
	if (IsBankWindowOpen() && m_banker_npc_id != 0) {
		auto it = m_entities.find(m_banker_npc_id);
		if (it == m_entities.end()) {
			// Banker NPC no longer exists (despawned, etc.)
			LOG_DEBUG(MOD_INVENTORY, "Banker NPC {} no longer exists, closing bank", m_banker_npc_id);
			CloseBankWindow();
		} else {
			float dist = CalculateDistance2D(m_x, m_y, it->second.x, it->second.y);
			if (dist > NPC_INTERACTION_DISTANCE) {
				LOG_DEBUG(MOD_INVENTORY, "Player moved too far from banker ({:.1f} > {:.1f}), closing bank",
					dist, NPC_INTERACTION_DISTANCE);
				CloseBankWindow();
				AddChatSystemMessage("You have moved too far from the banker.");
			}
		}
	}

	// Debug: Log target at start of UpdateMovement
	static auto last_update_debug = std::chrono::steady_clock::now();
	auto now = std::chrono::steady_clock::now();
	if (s_debug_level >= 1 && m_in_combat_movement &&
	    std::chrono::duration_cast<std::chrono::seconds>(now - last_update_debug).count() >= 1) {
		if (IsDebugEnabled()) std::cout << fmt::format("[DEBUG] UpdateMovement START: target=({:.1f},{:.1f},{:.1f}) is_moving={} path_size={}",
			m_target_x, m_target_y, m_target_z, m_is_moving, m_current_path.size()) << std::endl;
		last_update_debug = now;
	}
	
	// Update keyboard-based movement first
	if (m_move_forward || m_move_backward || m_turn_left || m_turn_right) {
		UpdateKeyboardMovement();
		// Check for zone line collision
		CheckZoneLine();
		// SendPositionUpdate has internal 250ms throttling
		SendPositionUpdate();
		// Don't process other movement if using keyboard
		return;
	}
	
	// Update combat movement if active
	if (m_in_combat_movement) {
		UpdateCombatMovement();
	}
	
	// Check if we're following someone
	if (!m_follow_target.empty()) {
		// Find the entity we're following
		for (const auto& pair : m_entities) {
			const Entity& entity = pair.second;
			if (entity.name == m_follow_target) {
				// Check if we're close enough
				float dist = CalculateDistance2D(m_x, m_y, entity.x, entity.y);
				float z_diff = std::abs(m_z - entity.z);
				
				// Log follow status periodically
				static std::chrono::steady_clock::time_point last_follow_log = std::chrono::steady_clock::now();
				auto now_follow = std::chrono::steady_clock::now();
				auto elapsed_follow = std::chrono::duration_cast<std::chrono::milliseconds>(now_follow - last_follow_log);
				
				if (elapsed_follow.count() >= 1000 && s_debug_level >= 1) {
					LOG_DEBUG(MOD_MAIN, "Following {}: Distance={:.1f} (stop at {:.1f}), Z-diff={:.1f}, Speed={:.1f}", m_follow_target, dist, m_follow_distance, z_diff, m_move_speed);
					last_follow_log = now_follow;
				}
				
				if (dist < m_follow_distance) {
					// We're close enough, stop if we were moving
					if (m_is_moving) {
						if (s_debug_level >= 1) {
							LOG_DEBUG(MOD_MAIN, "Reached follow distance ({:.1f}), stopping", dist);
						}
						StopMovement();
					}
					return;
				} else if (dist > m_follow_distance * 1.5f) {
					// Target has moved significantly, recalculate path
					// Check if final destination changed significantly
					float final_dest_dist = 0.0f;
					if (!m_current_path.empty() && m_current_path.size() > 0) {
						// Compare with the final waypoint in our path
						const auto& final_waypoint = m_current_path.back();
						final_dest_dist = CalculateDistance2D(final_waypoint.x, final_waypoint.y, entity.x, entity.y);
					} else {
						// No path, compare with our immediate target
						final_dest_dist = CalculateDistance2D(m_target_x, m_target_y, entity.x, entity.y);
					}
					
					if (final_dest_dist > 5.0f || m_current_path.empty()) {
						// Recalculate path using pathfinding
						LOG_DEBUG(MOD_MAIN, "UpdateMovement: Target moved significantly (dist={:.2f})", final_dest_dist);
						if (m_use_pathfinding && m_pathfinder) {
							// Find path from current position to target
							LOG_DEBUG(MOD_MAIN, "UpdateMovement: Recalculating path from ({:.2f},{:.2f},{:.2f}) to ({:.2f},{:.2f},{:.2f})", m_x, m_y, m_z, entity.x, entity.y, entity.z);
							if (FindPath(m_x, m_y, m_z, entity.x, entity.y, entity.z)) {
								// Start following the path
								m_current_path_index = 0;
								m_is_moving = true;
								if (IsDebugEnabled()) std::cout << fmt::format("[DEBUG] UpdateMovement: Path recalculated with {} waypoints", 
									m_current_path.size()) << std::endl;
								for (size_t i = 0; i < std::min(m_current_path.size(), size_t(3)); i++) {
									std::cout << fmt::format("  Waypoint {}: ({:.2f},{:.2f},{:.2f})", 
										i, m_current_path[i].x, m_current_path[i].y, m_current_path[i].z) << std::endl;
								}
							} else {
								// Pathfinding failed, use direct movement
								LOG_DEBUG(MOD_MAIN, "UpdateMovement: Pathfinding failed, using direct movement");
								m_target_x = entity.x;
								m_target_y = entity.y;
								m_target_z = entity.z;
								m_is_moving = true;
							}
						} else {
							// Fallback to direct movement
							LOG_DEBUG(MOD_MAIN, "UpdateMovement: Pathfinding disabled, using direct movement");
							m_target_x = entity.x;
							m_target_y = entity.y;
							m_target_z = entity.z;
							m_is_moving = true;
						}
					}
				}
				break;
			}
		}
	}
	
	// Always update jump if jumping, even when not moving
	if (m_is_jumping) {
		float old_z = m_z;
		UpdateJump();
		
		// If Z changed, send position update immediately
		if (m_z != old_z) {
			SendPositionUpdate();
		}
	}
	
	if (!m_is_moving) {
		// Still check for zone lines when stopped (player might have stopped in one)
		CheckZoneLine();

		// Send idle position updates like real clients (every 1-2 seconds)
		static std::map<EverQuest*, std::chrono::steady_clock::time_point> last_idle_updates;
		if (last_idle_updates.find(this) == last_idle_updates.end()) {
			last_idle_updates[this] = std::chrono::steady_clock::now();
		}

		auto now = std::chrono::steady_clock::now();
		auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - last_idle_updates[this]);

		if (elapsed.count() >= 1500) {  // 1.5 seconds average
			SendPositionUpdate();
			last_idle_updates[this] = now;
		}
		return;
	}
	
	// Check if we're following a path
	if (!m_current_path.empty() && m_current_path_index < m_current_path.size()) {
		// Check if we've reached the current waypoint
		const auto& waypoint = m_current_path[m_current_path_index];
		float dist_to_waypoint = CalculateDistance2D(m_x, m_y, waypoint.x, waypoint.y);
		
		if (s_debug_level >= 2) {
			if (IsDebugEnabled()) std::cout << fmt::format("[DEBUG] Following path: waypoint {}/{}, dist to waypoint: {:.2f}", 
				m_current_path_index, m_current_path.size()-1, dist_to_waypoint) << std::endl;
		}
		
		// Stuck detection - track if we're not making progress
		static std::map<EverQuest*, std::pair<float, std::chrono::steady_clock::time_point>> stuck_detection;
		auto now = std::chrono::steady_clock::now();
		
		if (stuck_detection.find(this) == stuck_detection.end()) {
			stuck_detection[this] = {dist_to_waypoint, now};
		} else {
			auto& [last_dist, last_time] = stuck_detection[this];
			auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - last_time).count();
			
			// If we haven't moved closer to the waypoint in 3 seconds, we're stuck
			if (elapsed >= 3 && std::abs(dist_to_waypoint - last_dist) < 1.0f) {
				std::cout << fmt::format("[WARNING] Stuck at waypoint {} - distance hasn't changed in {} seconds", 
					m_current_path_index, elapsed) << std::endl;
				
				// Skip to next waypoint or stop
				if (m_current_path_index < m_current_path.size() - 1) {
					m_current_path_index++;
					std::cout << "Skipping to next waypoint due to being stuck" << std::endl;
					// Reset stuck detection for the new waypoint
					stuck_detection.erase(this);
				} else {
					std::cout << "Stuck on final waypoint, stopping movement" << std::endl;
					StopMovement();
					stuck_detection.erase(this);
					return;
				}
			} else if (elapsed >= 1) {
				// Update every second if we've made progress
				if (std::abs(dist_to_waypoint - last_dist) > 0.5f) {
					stuck_detection[this] = {dist_to_waypoint, now};
				}
			}
		}
		
		// Check both 2D distance and Z difference for waypoint reached
		float z_diff_to_waypoint = std::abs(m_z - waypoint.z);
		bool waypoint_reached = (dist_to_waypoint < 5.0f && z_diff_to_waypoint < 10.0f) || dist_to_waypoint < 3.0f;
		
		if (waypoint_reached) { // Within range of waypoint
			// Move to next waypoint
			m_current_path_index++;
			stuck_detection.erase(this); // Clear stuck detection when reaching waypoint
			
			if (m_current_path_index >= m_current_path.size()) {
				// Reached the end of the path
				LOG_DEBUG(MOD_MAIN, "Reached end of path");
				StopMovement();
				return;
			} else {
				// Move to next waypoint
				const auto& next_waypoint = m_current_path[m_current_path_index];
				
				// Check if the next waypoint is different enough from current position
				float dist_to_next = CalculateDistance2D(m_x, m_y, next_waypoint.x, next_waypoint.y);
				if (dist_to_next > 2.0f) {  // Only move if it's far enough away
					MoveTo(next_waypoint.x, next_waypoint.y, next_waypoint.z);
					
					if (s_debug_level >= 2) {
						std::cout << fmt::format("Reached waypoint {}, moving to waypoint {} of {}",
							m_current_path_index - 1, m_current_path_index, m_current_path.size() - 1) << std::endl;
					}
				} else {
					// Skip this waypoint, it's too close
					if (s_debug_level >= 2) {
						std::cout << fmt::format("Skipping waypoint {} (too close: {:.2f} units)",
							m_current_path_index, dist_to_next) << std::endl;
					}
					// Continue to check the next waypoint in the next update
				}
			}
		}
	}
	
	// Only process console-based target movement if we have an active target or path
	// Skip this if we're in graphics Player mode without a console movement command
	bool has_console_movement = !m_follow_target.empty() || !m_current_path.empty() ||
	                            m_in_combat_movement ||
	                            (m_target_x != 0.0f || m_target_y != 0.0f || m_target_z != 0.0f);

	if (!has_console_movement) {
		// No console-based movement active, skip target processing
		return;
	}

	// Debug: Check what target we're moving to
	static auto last_target_debug = std::chrono::steady_clock::now();
	auto now_debug = std::chrono::steady_clock::now();
	if (s_debug_level >= 1 && std::chrono::duration_cast<std::chrono::seconds>(now_debug - last_target_debug).count() >= 1) {
		std::cerr << fmt::format("[DEBUG] Movement target check: current=({:.1f},{:.1f},{:.1f}) target=({:.1f},{:.1f},{:.1f}) path_size={} path_index={}",
			m_x, m_y, m_z, m_target_x, m_target_y, m_target_z, m_current_path.size(), m_current_path_index) << std::endl;
		last_target_debug = now_debug;
	}

	// Calculate distance to target
	float dx = m_target_x - m_x;
	float dy = m_target_y - m_y;
	float dz = m_target_z - m_z;
	float distance = std::sqrt(dx * dx + dy * dy + dz * dz);

	// Check if we've reached the target
	if (distance < 2.0f) {  // Stop within 2 units of target
		if (s_debug_level >= 1) {
			std::cerr << fmt::format("[DEBUG] Reached target at distance {:.2f}, target=({:.1f},{:.1f},{:.1f})",
				distance, m_target_x, m_target_y, m_target_z) << std::endl;
		}

		m_x = m_target_x;
		m_y = m_target_y;
		m_z = m_target_z;

		// If following a path, check for next waypoint
		if (!m_current_path.empty() && m_current_path_index < m_current_path.size() - 1) {
			// Continue to next waypoint instead of stopping
			return;
		}

		StopMovement();
		return;
	}
	
	// Calculate time delta based on actual elapsed time
	static std::map<EverQuest*, std::chrono::steady_clock::time_point> last_move_times;
	if (last_move_times.find(this) == last_move_times.end()) {
		last_move_times[this] = std::chrono::steady_clock::now();
	}
	auto now_movement = std::chrono::steady_clock::now();
	auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now_movement - last_move_times[this]);
	last_move_times[this] = now_movement;
	
	float delta_time = elapsed.count() / 1000.0f;  // Convert to seconds
	if (delta_time > 0.1f) delta_time = 0.1f;  // Cap at 100ms to prevent large jumps
	
	// Debug delta time issues
	if (s_debug_level >= 1 && m_in_combat_movement && delta_time < 0.01f) {
		if (IsDebugEnabled()) std::cout << fmt::format("[DEBUG] WARNING: Very small delta_time: {:.6f}s ({}ms)", 
			delta_time, elapsed.count()) << std::endl;
	}
	
	// Calculate movement step using current movement speed
	float current_speed = m_move_speed;
	
	// Adjust speed when following
	if (!m_follow_target.empty()) {
		// Speed up when far, slow down when close
		if (distance > FOLLOW_FAR_DISTANCE) {
			current_speed *= FOLLOW_MAX_SPEED_MULT;
		} else if (distance < FOLLOW_CLOSE_DISTANCE) {
			current_speed *= FOLLOW_MIN_SPEED_MULT;
		} else {
			// Linear interpolation between min and max speed
			float speed_factor = (distance - FOLLOW_CLOSE_DISTANCE) / (FOLLOW_FAR_DISTANCE - FOLLOW_CLOSE_DISTANCE);
			current_speed *= FOLLOW_MIN_SPEED_MULT + (FOLLOW_MAX_SPEED_MULT - FOLLOW_MIN_SPEED_MULT) * speed_factor;
		}
	}
	
	float step = current_speed * delta_time;
	if (step > distance) {
		step = distance;
	}
	
	// Debug output for combat movement
	static auto last_move_debug = std::chrono::steady_clock::now();
	if (s_debug_level >= 1 && m_in_combat_movement && 
	    std::chrono::duration_cast<std::chrono::seconds>(now_movement - last_move_debug).count() >= 1) {
		LOG_DEBUG(MOD_MAIN, "Movement update: pos=({:.1f},{:.1f},{:.1f}) target=({:.1f},{:.1f},{:.1f}) dist={:.1f} speed={:.1f} step={:.1f}", m_x, m_y, m_z, m_target_x, m_target_y, m_target_z, distance, current_speed, step);
		last_move_debug = now_movement;
	}
	
	// Normalize direction and move
	float factor = step / distance;
	float old_x = m_x;
	float old_y = m_y;
	m_x += dx * factor;
	m_y += dy * factor;
	m_z += dz * factor;
	
	// Debug: verify position actually changed
	if (s_debug_level >= 1 && m_in_combat_movement) {
		LOG_DEBUG(MOD_MAIN, "Position update: ({:.1f},{:.1f}) -> ({:.1f},{:.1f}) delta=({:.3f},{:.3f})", old_x, old_y, m_x, m_y, m_x - old_x, m_y - old_y);
	}
	
	// Update heading
	m_heading = CalculateHeading(m_x - dx * factor, m_y - dy * factor, m_x, m_y);
	
	// Set animation based on actual movement distance
	// Don't change animation if we're jumping
	if (!m_is_jumping) {
		// Calculate movement distance in this update
		float move_dist = std::sqrt(dx * dx + dy * dy);
		
		// Determine animation based on movement distance
		uint32_t new_animation = ANIM_STAND;
		
		if (move_dist < 0.1f) {
			// Very small movement (< 0.1 units) - no animation change
			new_animation = ANIM_STAND;
		} else if (move_dist < 2.0f) {
			// Small movement - walk animation
			new_animation = ANIM_WALK;
		} else {
			// Larger movement - run animation
			new_animation = ANIM_RUN;
		}
		
		// Only change animation if it's different
		if (m_animation != new_animation) {
			m_animation = new_animation;
		}
	}
	
	// Update jump if jumping
	bool was_jumping = m_is_jumping;
	float old_z = m_z;
	UpdateJump();
	
	// If jumping and Z changed, send position update immediately
	if (m_is_jumping && m_z != old_z) {
		SendPositionUpdate();
		m_last_position_update_time = now;
	}
	
	// Fix Z position periodically to follow terrain (but not while jumping)
	if (!m_is_jumping) {
		static std::map<EverQuest*, std::chrono::steady_clock::time_point> last_z_fix_times;
		if (last_z_fix_times.find(this) == last_z_fix_times.end()) {
			last_z_fix_times[this] = now;
		}
		
		auto z_fix_elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - last_z_fix_times[this]);
		// Fix Z every 1000ms while moving (once per second to reduce adjustments)
		if (z_fix_elapsed.count() >= 1000) {
			FixZ();
			last_z_fix_times[this] = now;
		}
	}
	
	// Check for zone line collision
	CheckZoneLine();

	// Send position update with proper throttling per instance
	if (!m_last_position_update_time.time_since_epoch().count()) {
		m_last_position_update_time = std::chrono::steady_clock::now();
	}

	auto update_elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now_movement - m_last_position_update_time);

	// Send updates at regular intervals when moving
	if (update_elapsed.count() >= POSITION_UPDATE_INTERVAL_MS) {
		SendPositionUpdate();
		m_last_position_update_time = now_movement;
	}
}

void EverQuest::SendPositionUpdate()
{
	if (!IsFullyZonedIn() || !m_zone_connection) {
		if (s_debug_level >= 2) {
			auto now = std::chrono::system_clock::now();
			auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()) % 1000;
			auto time_t_now = std::chrono::system_clock::to_time_t(now);
			std::tm* tm_now = std::localtime(&time_t_now);
			std::cout << fmt::format("[{:02d}:{:02d}:{:02d}.{:03d}] [POS] SendPositionUpdate skipped: zoned_in={} zone_conn={}",
				tm_now->tm_hour, tm_now->tm_min, tm_now->tm_sec, static_cast<int>(ms.count()),
				IsFullyZonedIn(), m_zone_connection != nullptr) << std::endl;
		}
		return;
	}

	// Throttle position updates to ~250ms minimum (working client averages ~275ms)
	// This prevents jerky motion when viewed by other players
	static auto lastSendTime = std::chrono::steady_clock::now();
	static int16_t lastSentAnimation = 0;
	auto now = std::chrono::steady_clock::now();
	auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - lastSendTime);

	// Allow immediate send when stopping (animation goes to 0) for responsiveness
	bool isStoppingNow = (m_animation == 0 && lastSentAnimation != 0);

	if (!isStoppingNow && elapsed.count() < 250) {
		// Throttled - skip this update
		return;
	}

	lastSendTime = now;
	lastSentAnimation = m_animation;
	
	// Keep track of last position for delta calculations (per instance, not static)
	static std::map<EverQuest*, std::tuple<float, float, float, float>> last_positions;
	
	// Initialize last position if this is the first update
	if (last_positions.find(this) == last_positions.end()) {
		last_positions[this] = std::make_tuple(m_x, m_y, m_z, m_heading);
	}
	
	auto& [last_x, last_y, last_z, last_heading] = last_positions[this];
	
	// Calculate deltas
	float delta_x = m_x - last_x;
	float delta_y = m_y - last_y;
	float delta_z = m_z - last_z;
	float delta_heading_deg = m_heading - last_heading;
	
	// Normalize delta heading to -180 to 180
	while (delta_heading_deg > 180.0f) delta_heading_deg -= 360.0f;
	while (delta_heading_deg < -180.0f) delta_heading_deg += 360.0f;
	
	// Convert heading values for the packet
	// EQ uses a different heading system where values might need adjustment
	int delta_heading_scaled = static_cast<int>(delta_heading_deg * 512.0f / 360.0f) & 0x3FF; // 10 bits
	
	// Convert heading to 12-bit value for packet
	// Server uses EQ12toFloat which divides by 4, then converts: degrees = (raw/4) * 360/512
	// This simplifies to: degrees = raw * 360 / 2048
	// So to convert degrees to raw: raw = degrees * 2048 / 360
	//
	// C->S transformation: -m_heading + 90 to match server convention
	// Inverse of S->C transformation (m_heading = 90 - server_h)
	float server_heading = 90.0f - m_heading;
	if (server_heading < 0.0f) server_heading += 360.0f;
	if (server_heading >= 360.0f) server_heading -= 360.0f;
	int heading_raw = static_cast<int>(server_heading * 2048.0f / 360.0f);
	// FloatToEQ12 does modulo 2048, so values wrap
	uint16_t heading_scaled = heading_raw % 2048;
	
	if (s_debug_level >= 2) {
		LOG_DEBUG(MOD_MOVEMENT, "POS C->S [SELF] m_heading={:.2f}deg -> server_heading={:.2f}deg -> heading_scaled={} (12-bit)",
			m_heading, server_heading, heading_scaled);
	}
	
	// Build PlayerPositionUpdateClient_Struct
	// NOTE: Field order must match server's expected binary layout exactly
	// Server struct has: spawn_id, sequence, y_pos, delta_z, delta_x, delta_y, ...
	struct {
		uint16_t spawn_id;
		uint16_t sequence;
		float y_pos;
		float delta_z;
		float delta_x;   // Server expects delta_x at offset 12
		float delta_y;   // Server expects delta_y at offset 16
		uint32_t anim_and_delta_heading;
		float x_pos;
		float z_pos;
		uint16_t heading_and_padding;
		uint8_t unknown[2];
	} update;
	
	memset(&update, 0, sizeof(update));
	
	// Use the entity ID we extracted from PlayerProfile
	// This is what the server expects in ClientUpdate packets
	update.spawn_id = m_my_character_id;
	update.sequence = ++m_movement_sequence;
	
	// Debug: Log what we're actually sending
	// LOG_DEBUG(MOD_MAIN, "SendMovementUpdate: Using spawn_id {} (m_my_character_id={})", 
	// 	update.spawn_id, m_my_character_id);
	
	// Validate that we have a valid character ID
	if (m_my_character_id == 0) {
		LOG_ERROR(MOD_MOVEMENT, "SendMovementUpdate called with m_my_character_id = 0! Not sending update.");
		return;
	}

	// Check if we're accidentally using another player's ID
	auto it = m_entities.find(m_my_character_id);
	if (it != m_entities.end() && it->second.name != m_character) {
		LOG_WARN(MOD_MOVEMENT, "m_my_character_id {} belongs to '{}', not our character '{}'!",
			m_my_character_id, it->second.name, m_character);
	}
	
	// Convert from internal coordinate system back to server coordinate system
	// When receiving from server: m_x = server_y, m_y = server_x (see ZoneProcessPlayerProfile)
	// So to send back: server's y_pos = m_x, server's x_pos = m_y
	update.y_pos = m_x;  // m_x holds server's original y
	update.x_pos = m_y;  // m_y holds server's original x
	// Send Z position directly without offset - server stores the actual position
	update.z_pos = m_z;
	// Deltas follow the same transformation:
	// Internal delta_x (change in m_x) = change in server's y = server's delta_y
	// Internal delta_y (change in m_y) = change in server's x = server's delta_x
	update.delta_x = delta_y;  // server's delta_x = change in m_y (server's x)
	update.delta_y = delta_x;  // server's delta_y = change in m_x (server's y)
	update.delta_z = delta_z;
	
	// Pack animation and delta_heading into bitfield
	// Use current animation state (which includes jump animation)
	uint32_t anim_value = m_animation;
	update.anim_and_delta_heading = (anim_value & 0x3FF) | (0 << 10) | (1 << 20);
	
	// Pack heading into bitfield (12 bits)
	update.heading_and_padding = heading_scaled;
	
	// Build packet
	EQ::Net::DynamicPacket p;
	p.Resize(sizeof(update) + 2);
	p.PutUInt16(0, HC_OP_ClientUpdate);
	memcpy((char*)p.Data() + 2, &update, sizeof(update));
	
	// Debug: Verify what's in the packet
	if (s_debug_level >= 1) {
		uint16_t packet_spawn_id = p.GetUInt16(2); // spawn_id is first field after opcode
		// LOG_DEBUG(MOD_MAIN, "ClientUpdate packet: spawn_id in struct = {}, spawn_id in packet = {}", 
		// 	update.spawn_id, packet_spawn_id);
	}
	
	// Log C->S position updates at debug level 2+
	if (s_debug_level >= 2) {
		// Log server coordinates (x_pos, y_pos) = (m_y, m_x) due to coordinate swap
		LOG_DEBUG(MOD_MOVEMENT, "POS C->S spawn_id={} server_pos=({:.2f}, {:.2f}, {:.2f}) heading={:.1f} anim={} server_delta=({:.2f}, {:.2f}, {:.2f})",
			m_my_character_id, update.x_pos, update.y_pos, update.z_pos, m_heading, m_animation, update.delta_x, update.delta_y, update.delta_z);
		LOG_DEBUG(MOD_MOVEMENT, "POS C->S [SELF] client_pos=({:.2f},{:.2f},{:.2f}) -> server_pos=({:.2f},{:.2f},{:.2f})",
			m_x, m_y, m_z, update.x_pos, update.y_pos, update.z_pos);
	}

	DumpPacket("C->S", HC_OP_ClientUpdate, p);
	SafeQueueZonePacket(p, 0, false);  // Send unreliable
	
	// Update last position
	last_x = m_x;
	last_y = m_y;
	last_z = m_z;
	last_heading = m_heading;
	
	// Track movement history for anti-cheat
	MovementHistoryEntry history_entry;
	history_entry.x = m_x;
	history_entry.y = m_y;
	history_entry.z = m_z;
	history_entry.type = 1; // Normal movement (Collision type)
	history_entry.timestamp = static_cast<uint32_t>(std::time(nullptr));
	
	m_movement_history.push_back(history_entry);
	
	// Send movement history every second (matching client behavior)
	uint32_t current_time = static_cast<uint32_t>(std::time(nullptr));
	if (current_time - m_last_movement_history_send >= 1) {
		m_last_movement_history_send = current_time;  // Update timestamp BEFORE sending to prevent duplicates
		try {
			SendMovementHistory();
		}
		catch (const std::exception& e) {
			std::cerr << "[ERROR] Exception in SendMovementHistory: " << e.what() << std::endl;
		}
		catch (...) {
			std::cerr << "[ERROR] Unknown exception in SendMovementHistory" << std::endl;
		}
	}
}

float EverQuest::CalculateHeading(float x1, float y1, float x2, float y2)
{
	float dx = x2 - x1;
	float dy = y2 - y1;
	
	// If we're not moving, preserve current heading
	if (std::abs(dx) < 0.001f && std::abs(dy) < 0.001f) {
		return m_heading;
	}
	
	// Calculate angle in radians
	// In EQ's coordinate system: +X is East, +Y is North (verified)
	// We want: 0° = North, 90° = East, 180° = South, 270° = West
	// Standard atan2(y,x) gives: 0° = East, 90° = North, 180° = West, 270° = South
	// So we need to use atan2(x,y) and then adjust by 90 degrees
	float angle = std::atan2(dx, dy);  // This gives 0° = North, 90° = East
	
	// Convert radians to degrees
	float degrees = angle * 180.0f / M_PI;
	
	// atan2 returns -180 to 180, convert to 0-360
	if (degrees < 0.0f) {
		degrees += 360.0f;
	}
	
	// No 180° adjustment needed - the known good client shows:
	// North ≈ 3.5°, East ≈ 137.7°, South ≈ 89.5°, West ≈ 45.8°
	// (converted from raw values 14, 1567, 1018, 521)
	
	// Debug output with more detail
	if (s_debug_level >= 2) {
		LOG_DEBUG(MOD_MAIN, "CalculateHeading: from ({:.1f},{:.1f}) to ({:.1f},{:.1f}), dx={:.1f}, dy={:.1f}, raw angle={:.1f}°, adjusted={:.1f}°", 
			x1, y1, x2, y2, dx, dy, angle * 180.0f / M_PI, degrees);
	}
	
	return degrees;
}

float EverQuest::CalculateDistance(float x1, float y1, float z1, float x2, float y2, float z2) const
{
	float dx = x2 - x1;
	float dy = y2 - y1;
	float dz = z2 - z1;
	return std::sqrt(dx * dx + dy * dy + dz * dz);
}

float EverQuest::CalculateDistance2D(float x1, float y1, float x2, float y2) const
{
	float dx = x2 - x1;
	float dy = y2 - y1;
	return std::sqrt(dx * dx + dy * dy);
}

bool EverQuest::HasReachedDestination() const
{
	if (!m_is_moving) {
		return true;
	}
	
	// Check if we're close enough to the target (within 2 units)
	float dist = CalculateDistance2D(m_x, m_y, m_target_x, m_target_y);
	return dist < 2.0f;
}

Entity* EverQuest::FindEntityByName(const std::string& name)
{
	// Find entity by name (case-insensitive prefix match)
	// Replace spaces with underscores in search term
	std::string name_lower = name;
	std::transform(name_lower.begin(), name_lower.end(), name_lower.begin(), ::tolower);
	std::replace(name_lower.begin(), name_lower.end(), ' ', '_');
	
	for (auto& pair : m_entities) {
		Entity& entity = pair.second;
		std::string entity_name_lower = entity.name;
		std::transform(entity_name_lower.begin(), entity_name_lower.end(), entity_name_lower.begin(), ::tolower);
		
		if (entity_name_lower.find(name_lower) == 0) {  // Starts with
			return &entity;
		}
	}
	
	return nullptr;
}

void EverQuest::ListEntities(const std::string& search) const
{
	if (!IsFullyZonedIn()) {
		std::cout << "Not in zone yet" << std::endl;
		return;
	}
	
	if (m_entities.empty()) {
		std::cout << "No entities in zone" << std::endl;
		return;
	}
	
	// Prepare search string
	std::string search_lower = search;
	if (!search.empty()) {
		std::transform(search_lower.begin(), search_lower.end(), search_lower.begin(), ::tolower);
		std::replace(search_lower.begin(), search_lower.end(), ' ', '_');
	}
	
	if (search.empty()) {
		std::cout << fmt::format("Entities in zone ({} total):", m_entities.size()) << std::endl;
	} else {
		std::cout << fmt::format("Entities matching '{}' in zone:", search) << std::endl;
	}
	
	// Sort entities by distance from player
	std::vector<std::pair<float, const Entity*>> sorted_entities;
	for (const auto& pair : m_entities) {
		const Entity& entity = pair.second;
		
		// Filter by search term if provided
		if (!search.empty()) {
			std::string entity_name_lower = entity.name;
			std::transform(entity_name_lower.begin(), entity_name_lower.end(), entity_name_lower.begin(), ::tolower);
			if (entity_name_lower.find(search_lower) == std::string::npos) {
				continue;  // Skip entities that don't match search
			}
		}
		
		float dist = CalculateDistance(m_x, m_y, m_z, entity.x, entity.y, entity.z);
		sorted_entities.push_back({dist, &entity});
	}
	
	std::sort(sorted_entities.begin(), sorted_entities.end(), 
		[](const auto& a, const auto& b) { return a.first < b.first; });
	
	// Check if any entities matched
	if (sorted_entities.empty() && !search.empty()) {
		std::cout << fmt::format("  No entities found matching '{}'", search) << std::endl;
		return;
	}
	
	// Display up to 20 nearest entities
	int count = 0;
	for (const auto& [dist, entity] : sorted_entities) {
		if (count++ >= 20) {
			std::cout << "  ... and more" << std::endl;
			break;
		}
		
		std::cout << fmt::format("  {} (ID: {}) - Level {} {} - {:.1f} units away at ({:.0f}, {:.0f}, {:.0f})",
			entity->name, entity->spawn_id, entity->level,
			entity->class_id == 0 ? "NPC" : fmt::format("Class {}", entity->class_id),
			dist, entity->x, entity->y, entity->z) << std::endl;
			
		if (entity->hp_percent < 100) {
			std::cout << fmt::format("    HP: {}%", entity->hp_percent) << std::endl;
		}
	}
}

void EverQuest::ZoneProcessWearChange(const EQ::Net::Packet &p)
{
	// WearChange indicates equipment changes on entities
	// Titanium Format: spawn_id (2 bytes), material (2 bytes), color (4 bytes), wear_slot_id (1 byte)
	// Total: 9 bytes + 2 byte opcode = 11 bytes

	if (p.Length() != 11) {
		if (s_debug_level >= 1) {
			std::cout << fmt::format("WearChange packet wrong size: {} bytes (expected 11)", p.Length()) << std::endl;
		}
		return;
	}

	// Skip opcode (2 bytes), then parse struct
	uint16_t spawn_id = p.GetUInt16(2);      // offset 0 in struct, 2 in packet
	uint16_t material = p.GetUInt16(4);      // offset 2 in struct, 4 in packet
	uint32_t color = p.GetUInt32(6);         // offset 4 in struct, 6 in packet
	uint8_t wear_slot = p.GetUInt8(10);      // offset 8 in struct, 10 in packet

	if (s_debug_level >= 2) {
		auto it = m_entities.find(spawn_id);
		std::string name = (it != m_entities.end()) ? it->second.name : "Unknown";
		std::cout << fmt::format("Equipment change for {} (ID: {}): slot {} material {} color {:08X}",
			name, spawn_id, wear_slot, material, color) << std::endl;
	}

	// Update the entity's equipment data
	auto it = m_entities.find(spawn_id);
	if (it != m_entities.end()) {
		Entity& entity = it->second;

		// Update equipment slot if valid (slots 0-8 map to equipment array)
		if (wear_slot < 9) {
			entity.equipment[wear_slot] = material;
			entity.equipment_tint[wear_slot] = color;
		}

#ifdef EQT_HAS_GRAPHICS
		// If this is our player, update the inventory model view
		if (spawn_id == m_my_spawn_id && m_graphics_initialized && m_renderer) {
			// Build appearance from updated entity data
			EQT::Graphics::EntityAppearance appearance;
			appearance.face = entity.face;
			appearance.haircolor = entity.haircolor;
			appearance.hairstyle = entity.hairstyle;
			appearance.beardcolor = entity.beardcolor;
			appearance.beard = entity.beard;
			appearance.texture = entity.equip_chest2;
			appearance.helm = entity.helm;
			for (int i = 0; i < 9; i++) {
				appearance.equipment[i] = entity.equipment[i];
				appearance.equipment_tint[i] = entity.equipment_tint[i];
			}
			m_renderer->updatePlayerAppearance(entity.race_id, entity.gender, appearance);
		}
#endif
	}
}

void EverQuest::ZoneProcessIllusion(const EQ::Net::Packet &p)
{
	// Illusion packet indicates race/appearance changes from illusion spells
	// Packet format: opcode(2) + Illusion_Struct(168)

	if (p.Length() < 2 + sizeof(EQT::Illusion_Struct)) {
		LOG_WARN(MOD_ENTITY, "Illusion packet too small: {} bytes (expected {})",
		         p.Length(), 2 + sizeof(EQT::Illusion_Struct));
		return;
	}

	const EQT::Illusion_Struct* illusion = reinterpret_cast<const EQT::Illusion_Struct*>(
		static_cast<const char*>(p.Data()) + 2);

	uint32_t spawn_id = illusion->spawnid;
	int32_t new_race = illusion->race;
	uint8_t new_gender = illusion->gender;
	uint8_t new_texture = illusion->texture;
	uint8_t new_helm = illusion->helmtexture;
	float new_size = illusion->size;

	auto it = m_entities.find(static_cast<uint16_t>(spawn_id));
	if (it == m_entities.end()) {
		LOG_DEBUG(MOD_ENTITY, "Illusion for unknown entity {}", spawn_id);
		return;
	}

	Entity& entity = it->second;
	std::string entity_name = entity.name;

	LOG_INFO(MOD_ENTITY, "{} (ID: {}) illusioned to race {} gender {} texture {} helm {} size {:.2f}",
	         entity_name, spawn_id, new_race, new_gender, new_texture, new_helm, new_size);

	// Update entity data
	entity.race_id = static_cast<uint16_t>(new_race);
	entity.gender = new_gender;
	entity.equip_chest2 = new_texture;
	entity.helm = new_helm;
	entity.size = new_size;

	// Copy additional appearance fields if present
	entity.face = illusion->face;
	entity.hairstyle = illusion->hairstyle;
	entity.haircolor = illusion->haircolor;
	entity.beard = illusion->beard;
	entity.beardcolor = illusion->beardcolor;

#ifdef EQT_HAS_GRAPHICS
	if (m_renderer) {
		// Build new appearance from updated entity data
		EQT::Graphics::EntityAppearance appearance;
		appearance.face = entity.face;
		appearance.haircolor = entity.haircolor;
		appearance.hairstyle = entity.hairstyle;
		appearance.beardcolor = entity.beardcolor;
		appearance.beard = entity.beard;
		appearance.texture = entity.equip_chest2;
		appearance.helm = entity.helm;
		for (int i = 0; i < 9; i++) {
			appearance.equipment[i] = entity.equipment[i];
			appearance.equipment_tint[i] = entity.equipment_tint[i];
		}

		// If this is our player, update player appearance
		if (spawn_id == m_my_spawn_id) {
			m_renderer->updatePlayerAppearance(entity.race_id, entity.gender, appearance);
		}

		// Update the entity's model in the renderer
		m_renderer->updateEntityAppearance(static_cast<uint16_t>(spawn_id),
		                                    entity.race_id, entity.gender, appearance);
	}
#endif
}

void EverQuest::ZoneProcessMoveDoor(const EQ::Net::Packet &p)
{
	// MoveDoor indicates door animations (opening/closing)
	// Packet format: opcode(2) + MoveDoor_Struct(2)
	// action: 0x02 = close (go to closed visual state)
	//         0x03 = open (go to open visual state)

	if (p.Length() < 4) {
		LOG_WARN(MOD_ENTITY, "MoveDoor packet too small: {} bytes", p.Length());
		return;
	}

	uint8_t door_id = p.GetUInt8(2);
	uint8_t action = p.GetUInt8(3);

	// Update door state in our tracking
	auto it = m_doors.find(door_id);
	if (it != m_doors.end()) {
		bool is_open = (action == 0x03);
		it->second.state = is_open ? 1 : 0;

		// Check if this was a user-initiated door click
		bool user_initiated = (m_pending_door_clicks.erase(door_id) > 0);

		// Log at debug level 2+ for all doors, or level 1+ for user-initiated
		if (s_debug_level >= 2 || (user_initiated && s_debug_level >= 1)) {
			LOG_DEBUG(MOD_ENTITY, "Door {} {}{}", door_id,
			          is_open ? "opened" : "closed",
			          user_initiated ? " (user)" : "");
		}

#ifdef EQT_HAS_GRAPHICS
		// Notify renderer to animate the door
		if (m_renderer) {
			m_renderer->setDoorState(door_id, is_open, user_initiated);
		}
#endif
	} else {
		if (s_debug_level >= 2) {
			LOG_DEBUG(MOD_ENTITY, "MoveDoor for unknown door {}", door_id);
		}
	}
}

void EverQuest::ZoneProcessCompletedTasks(const EQ::Net::Packet &p)
{
	// CompletedTasks sends the list of completed tasks
	// This is informational - we can track it if needed
	
	if (s_debug_level >= 2) {
		LOG_DEBUG(MOD_MAIN, "Received completed tasks list ({} bytes)", p.Length());
	}
}

void EverQuest::ZoneProcessDzCompass(const EQ::Net::Packet &p)
{
	// DzCompass updates the expedition/DZ compass direction
	// Format: heading (4 bytes float), x (4 bytes float), y (4 bytes float), z (4 bytes float)
	
	if (s_debug_level >= 2) {
		if (p.Length() >= 18) {
			float heading = p.GetFloat(2);
			float x = p.GetFloat(6);
			float y = p.GetFloat(10);
			float z = p.GetFloat(14);
			LOG_DEBUG(MOD_MAIN, "DZ compass update: heading {:.1f} to ({:.2f}, {:.2f}, {:.2f})",
				heading, x, y, z);
		}
	}
}

void EverQuest::ZoneProcessDzExpeditionLockoutTimers(const EQ::Net::Packet &p)
{
	// DzExpeditionLockoutTimers sends lockout timer information
	// This is informational for expeditions/DZs
	
	if (s_debug_level >= 2) {
		LOG_DEBUG(MOD_MAIN, "Received DZ expedition lockout timers ({} bytes)", p.Length());
	}
}

void EverQuest::ZoneProcessBeginCast(const EQ::Net::Packet &p)
{
	// BeginCast indicates an entity is starting to cast a spell
	// Format: spawn_id (2 bytes), spell_id (2 bytes), cast_time (4 bytes)

	if (p.Length() < 10) {
		if (s_debug_level >= 1) {
			std::cout << fmt::format("BeginCast packet too small: {} bytes", p.Length()) << std::endl;
		}
		return;
	}

	uint16_t spawn_id = p.GetUInt16(2);
	uint16_t spell_id = p.GetUInt16(4);
	uint32_t cast_time = p.GetUInt32(6);

	if (s_debug_level >= 2) {
		auto it = m_entities.find(spawn_id);
		std::string name = (it != m_entities.end()) ? EQT::toDisplayName(it->second.name) : "Unknown";
		std::cout << fmt::format("{} (ID: {}) begins casting spell {} ({}ms)",
			name, spawn_id, spell_id, cast_time) << std::endl;
	}

	// Notify spell manager about the cast
	if (m_spell_manager) {
		m_spell_manager->handleBeginCast(spawn_id, spell_id, cast_time);
	}
}

void EverQuest::ZoneProcessManaChange(const EQ::Net::Packet &p)
{
	// ManaChange updates the player's own mana (sent after casting spells, etc.)
	// ManaChange_Struct format:
	//   new_mana (4 bytes), stamina (4 bytes), spell_id (4 bytes),
	//   keepcasting (1 byte), padding (3 bytes)
	// Total: 16 bytes + 2 byte opcode = 18 bytes

	if (p.Length() < 18) {
		if (s_debug_level >= 1) {
			std::cout << fmt::format("ManaChange packet too small: {} bytes", p.Length()) << std::endl;
		}
		return;
	}

	// Skip opcode (2 bytes), then parse ManaChange_Struct
	uint32_t new_mana = p.GetUInt32(2);    // offset 0 in struct
	uint32_t stamina = p.GetUInt32(6);     // offset 4 in struct (endurance)
	uint32_t spell_id = p.GetUInt32(10);   // offset 8 in struct
	uint8_t keepcasting = p.GetUInt8(14);  // offset 12 in struct

	// Update player mana tracking
	m_mana = new_mana;

	// Update our entity if we're tracking it
	if (m_my_spawn_id != 0) {
		auto it = m_entities.find(m_my_spawn_id);
		if (it != m_entities.end()) {
			it->second.cur_mana = static_cast<uint16_t>(std::min(new_mana, static_cast<uint32_t>(65535)));
		}
	}

	if (s_debug_level >= 2) {
		LOG_DEBUG(MOD_SPELL, "ManaChange: mana={}, stamina={}, spell_id={}, keepcasting={}",
			new_mana, stamina, spell_id, keepcasting);
	}

	// Notify spell manager if available
	if (m_spell_manager) {
		m_spell_manager->handleManaChange(new_mana, stamina, spell_id);
	}

	// Also update endurance tracking
	m_endurance = stamina;

	// Update combat stats with new mana value
	if (m_combat_manager) {
		CombatStats stats;
		stats.current_hp = m_cur_hp;
		stats.max_hp = m_max_hp;
		stats.current_mana = new_mana;
		stats.max_mana = m_max_mana;
		stats.current_endurance = stamina;
		stats.max_endurance = 0;
		stats.hp_percent = (m_max_hp > 0) ? (static_cast<float>(m_cur_hp) * 100.0f / static_cast<float>(m_max_hp)) : 100.0f;
		stats.mana_percent = (m_max_mana > 0) ? (static_cast<float>(new_mana) * 100.0f / static_cast<float>(m_max_mana)) : 100.0f;
		stats.endurance_percent = 100.0f;
		m_combat_manager->UpdateCombatStats(stats);
	}

#ifdef EQT_HAS_GRAPHICS
	// Update inventory window stats when player mana/endurance changes
	UpdateInventoryStats();
#endif
}

void EverQuest::ZoneProcessBuff(const EQ::Net::Packet &p)
{
	// SpellBuffPacket_Struct format:
	//   entityid (4 bytes), SpellBuff_Struct (20 bytes), slotid (4 bytes), bufffade (4 bytes)
	// Total: 32 bytes + 2 byte opcode = 34 bytes minimum

	if (p.Length() < 34) {
		if (s_debug_level >= 1) {
			std::cout << fmt::format("Buff packet too small: {} bytes", p.Length()) << std::endl;
		}
		return;
	}

	// Parse SpellBuffPacket_Struct
	uint32_t entity_id = p.GetUInt32(2);

	// SpellBuff_Struct at offset 6
	EQT::SpellBuff_Struct buff;
	buff.effect_type = p.GetUInt8(6);
	buff.level = p.GetUInt8(7);
	buff.bard_modifier = p.GetUInt8(8);
	buff.unknown003 = p.GetUInt8(9);
	buff.spellid = p.GetUInt32(10);
	buff.duration = p.GetInt32(14);
	buff.counters = p.GetUInt32(18);
	buff.player_id = p.GetUInt32(22);

	uint32_t slot_id = p.GetUInt32(26);
	uint32_t buff_fade = p.GetUInt32(30);

	// Check if this is our buff
	bool is_self = (entity_id == m_my_spawn_id);

	if (s_debug_level >= 1) {
		std::cout << fmt::format("Buff: entity={}, spell={}, slot={}, duration={}, fade={}, effect_type={}",
			entity_id, buff.spellid, slot_id, buff.duration, buff_fade, buff.effect_type) << std::endl;
	}

	// Update buff manager
	if (m_buff_manager) {
		if (buff_fade == 1) {
			// Buff is fading
			if (is_self) {
				m_buff_manager->removePlayerBuffBySlot(static_cast<uint8_t>(slot_id));
			} else {
				m_buff_manager->removeBuffBySlot(static_cast<uint16_t>(entity_id), static_cast<uint8_t>(slot_id));
			}
		} else if (buff.effect_type == 2 && buff.spellid != 0 && buff.spellid != 0xFFFFFFFF) {
			// Buff is being applied
			if (is_self) {
				m_buff_manager->setPlayerBuff(static_cast<uint8_t>(slot_id), buff);
			} else {
				m_buff_manager->setEntityBuff(static_cast<uint16_t>(entity_id),
					static_cast<uint8_t>(slot_id), buff);
			}
		}
	}
}

void EverQuest::ZoneProcessColoredText(const EQ::Net::Packet &p)
{
	// ColoredText_Struct: uint32 color, char msg[variable]
	// Used for spell fade messages and other colored text
	if (p.Length() < 6) {  // 2 byte opcode + 4 byte color minimum
		LOG_WARN(MOD_ZONE, "ColoredText packet too small: {} bytes", p.Length());
		return;
	}

	uint32_t color = p.GetUInt32(2);

	// Extract message starting at offset 6
	std::string message;
	if (p.Length() > 6) {
		const char* msg_ptr = reinterpret_cast<const char*>(static_cast<const uint8_t*>(p.Data()) + 6);
		size_t max_len = p.Length() - 6;
		// Find null terminator or use full length
		size_t len = strnlen(msg_ptr, max_len);
		message = std::string(msg_ptr, len);
	}

	LOG_DEBUG(MOD_ZONE, "ColoredText: color={}, message='{}'", color, message);

	// Display in chat window
	if (!message.empty()) {
#ifdef EQT_HAS_GRAPHICS
		if (m_renderer && m_renderer->getWindowManager()) {
			auto* chatWindow = m_renderer->getWindowManager()->getChatWindow();
			if (chatWindow) {
				// Create a ChatMessage for the spell fade text
				eqt::ui::ChatMessage msg;
				msg.text = message;
				msg.channel = eqt::ui::ChatChannel::Spell;
				msg.isSystemMessage = true;
				msg.timestamp = static_cast<uint32_t>(std::time(nullptr));
				msg.color = eqt::ui::getChannelColor(eqt::ui::ChatChannel::Spell);
				chatWindow->addMessage(msg);
			}
		}
#endif
		if (s_debug_level >= 1) {
			std::cout << message << std::endl;
		}
	}
}

void EverQuest::ZoneProcessFormattedMessage(const EQ::Net::Packet &p)
{
	// FormattedMessage is used for various system messages
	// Titanium Format: unknown0 (4 bytes), string_id (4 bytes), type (4 bytes), message (variable)
	// Total header: 12 bytes + 2 byte opcode = 14 bytes minimum

	if (p.Length() < 14) {
		LOG_WARN(MOD_ZONE, "FormattedMessage packet too small: {} bytes", p.Length());
		return;
	}

	// Skip opcode (2 bytes) and header (12 bytes), then get message string
	uint32_t unknown0 = p.GetUInt32(2);
	uint32_t string_id = p.GetUInt32(6);
	uint32_t type = p.GetUInt32(10);

	LOG_DEBUG(MOD_ZONE, "[FormattedMessage] Packet length: {}, unknown0={}, string_id={}, type={}",
		p.Length(), unknown0, string_id, type);

	// Message starts at offset 14 (2 byte opcode + 12 byte header)
	if (p.Length() > 14) {
		const uint8_t* data = static_cast<const uint8_t*>(p.Data()) + 14;
		size_t msg_len = p.Length() - 14;

		// Parse the message with full link preservation
		eqt::ParsedFormattedMessage parsed = eqt::parseFormattedMessage(data, msg_len);

		LOG_DEBUG(MOD_ZONE, "[FormattedMessage] string_id={}, type={}, message='{}', links={}",
			string_id, type, parsed.displayText, parsed.links.size());

		// Display in chat window if we have content
		if (!parsed.displayText.empty()) {
#ifdef EQT_HAS_GRAPHICS
			if (!m_renderer) {
				LOG_DEBUG(MOD_ZONE, "[FormattedMessage] No renderer available");
			} else {
				auto* windowManager = m_renderer->getWindowManager();
				if (!windowManager) {
					LOG_DEBUG(MOD_ZONE, "[FormattedMessage] No window manager available");
				} else {
					auto* chatWindow = windowManager->getChatWindow();
					if (!chatWindow) {
						LOG_DEBUG(MOD_ZONE, "[FormattedMessage] No chat window available");
					} else {
						eqt::ui::ChatMessage chatMsg;
						chatMsg.links = std::move(parsed.links);

						// Handle different message types based on string_id
						// string_id=467 with type=286 = Loot message (item link)
						// string_id=339 = Tradeskill success (item created)
						// string_id=1032 = NPC says
						// string_id=1034 = NPC shouts
						if (string_id == 467) {
							// Loot message - display as system message with item link
							chatMsg.channel = eqt::ui::ChatChannel::Loot;
							chatMsg.sender = "";
							chatMsg.text = "You have looted " + parsed.displayText;
							LOG_DEBUG(MOD_ZONE, "[FormattedMessage] Adding to chat: channel={}, sender='{}', text='{}'",
								static_cast<int>(chatMsg.channel), chatMsg.sender, chatMsg.text);
							chatWindow->addMessage(chatMsg);
							return;
						}

						if (string_id == 339) {
							// Tradeskill success message - item name is in displayText
							chatMsg.channel = eqt::ui::ChatChannel::System;
							chatMsg.sender = "";
							chatMsg.text = "You create a " + parsed.displayText + "!";
							LOG_DEBUG(MOD_ZONE, "[FormattedMessage] Adding to chat: channel={}, sender='{}', text='{}'",
								static_cast<int>(chatMsg.channel), chatMsg.sender, chatMsg.text);
							chatWindow->addMessage(chatMsg);
							return;
						}

						// Parse NPC name (first two words) and message from displayText
						// Format: "Firstname Lastname message text here"
						std::string npcName;
						std::string messageText;
						const std::string& fullText = parsed.displayText;

						// Find first two spaces to extract two-word NPC name
						size_t firstSpace = fullText.find(' ');
						if (firstSpace != std::string::npos) {
							size_t secondSpace = fullText.find(' ', firstSpace + 1);
							if (secondSpace != std::string::npos) {
								npcName = fullText.substr(0, secondSpace);
								messageText = fullText.substr(secondSpace + 1);
							} else {
								// Only one space, treat first word as name
								npcName = fullText.substr(0, firstSpace);
								messageText = fullText.substr(firstSpace + 1);
							}
						} else {
							// No space, entire text is name (unlikely)
							npcName = fullText;
						}

						// Determine format based on string_id
						// string_id=1032 → "says" (NPC dialogue)
						// string_id=1034 → "shouts" (NPC shout)
						std::string verb;
						if (string_id == 1034) {
							verb = "shouts";
							chatMsg.channel = eqt::ui::ChatChannel::Shout;
						} else {
							// Default to "says" for string_id=1032 and others
							verb = "says";
							chatMsg.channel = eqt::ui::ChatChannel::NPCDialogue;
						}

						// Set sender and message text
						chatMsg.sender = npcName;
						if (!messageText.empty()) {
							if (chatMsg.channel == eqt::ui::ChatChannel::Shout) {
								// For Shout: chat_message_buffer.cpp handles formatting
								// like "NPC Name shouts, 'message'", so just set the raw message
								chatMsg.text = messageText;

								// Adjust link positions for the message text
								// Original displayText: "NPC Name message..." - message starts after npcName + " "
								// New text: just "message..." - starts at 0
								// So we need to shift link positions left by (npcName.length() + 1)
								size_t originalMessageStart = npcName.length() + 1;  // After "NPC Name "

								for (auto& link : chatMsg.links) {
									if (link.startPos >= originalMessageStart) {
										link.startPos -= originalMessageStart;
									} else {
										// Link is in the NPC name, which we've removed - mark as invalid
										link.startPos = 0;
									}
									if (link.endPos >= originalMessageStart) {
										link.endPos -= originalMessageStart;
									} else {
										link.endPos = 0;
									}
								}
							} else {
								// For NPCDialogue: chat_message_buffer displays text as-is,
								// so include the full formatted message
								chatMsg.text = npcName + " " + verb + ", '" + messageText + "'";

								// Adjust link positions for the reformatted text
								// Original: "NPC Name message..." - message starts after npcName + " "
								// New: "NPC Name says, 'message...'" - message starts after npcName + " " + verb + ", '"
								// The offset is the difference: verb.length() + 3 (" " + ", '")
								size_t originalMessageStart = npcName.length() + 1;  // After "NPC Name "
								size_t newMessageStart = npcName.length() + 1 + verb.length() + 3;  // After "NPC Name says, '"
								int offset = static_cast<int>(newMessageStart) - static_cast<int>(originalMessageStart);

								for (auto& link : chatMsg.links) {
									if (link.startPos >= originalMessageStart) {
										link.startPos += offset;
									}
									if (link.endPos >= originalMessageStart) {
										link.endPos += offset;
									}
								}
							}
						} else {
							chatMsg.text = chatMsg.channel == eqt::ui::ChatChannel::Shout ?
								"something" : npcName + " " + verb + " something";
						}

						chatMsg.color = eqt::ui::getChannelColor(chatMsg.channel);
						chatMsg.isSystemMessage = false;  // Has a sender now
						LOG_DEBUG(MOD_ZONE, "[FormattedMessage] Adding to chat: channel={}, sender='{}', text='{}'",
							static_cast<int>(chatMsg.channel), npcName, chatMsg.text);
						chatWindow->addMessage(std::move(chatMsg));
					}
				}
			}
#endif
		}
	} else {
		// No message content, just the header
		LOG_DEBUG(MOD_ZONE, "[FormattedMessage] string_id={}, type={}, no message content",
			string_id, type);
	}
}

void EverQuest::ZoneProcessSimpleMessage(const EQ::Net::Packet &p)
{
	// SimpleMessage is used for system messages that use string templates
	// SimpleMessage_Struct: color(4), string_id(4), unknown8(4)
	// Total: 12 bytes + 2 byte opcode = 14 bytes minimum

	if (p.Length() < 14) {
		LOG_WARN(MOD_ZONE, "SimpleMessage packet too small: {} bytes", p.Length());
		return;
	}

	uint32_t color_type = p.GetUInt32(2);   // color/type
	uint32_t string_id = p.GetUInt32(6);    // string_id

	// Get the message template for this string ID
	std::string message_text = GetStringMessage(string_id);

	LOG_DEBUG(MOD_ZONE, "[SimpleMessage] color_type={}, string_id={}, template='{}'",
		color_type, string_id, message_text);

	// Substitute placeholders (%1, %2, etc.) with entity names
	// %1 is typically the target (for "You have slain %1!")
	if (message_text.find("%1") != std::string::npos) {
		std::string target_name = "something";

		// For "slain" messages (string_id 303), use the last slain entity name
		if (string_id == 303 && !m_last_slain_entity_name.empty()) {
			target_name = m_last_slain_entity_name;
		}
		// Otherwise try current combat target
		else if (m_combat_manager && m_combat_manager->GetTargetId() != 0) {
			auto it = m_entities.find(m_combat_manager->GetTargetId());
			if (it != m_entities.end()) {
				target_name = EQT::toDisplayName(it->second.name);
			}
		}

		size_t pos = message_text.find("%1");
		if (pos != std::string::npos) {
			message_text.replace(pos, 2, target_name);
		}
	}

	// Map color_type to chat channel
	eqt::ui::ChatChannel channel = eqt::ui::ChatChannel::System;
	switch (color_type) {
		case 124:  // YouSlain
			channel = eqt::ui::ChatChannel::Combat;
			break;
		case 138:  // ExpGain
			channel = eqt::ui::ChatChannel::Experience;
			break;
		default:
			channel = eqt::ui::ChatChannel::System;
			break;
	}

#ifdef EQT_HAS_GRAPHICS
	if (m_renderer) {
		auto* windowManager = m_renderer->getWindowManager();
		if (windowManager) {
			auto* chatWindow = windowManager->getChatWindow();
			if (chatWindow) {
				eqt::ui::ChatMessage chatMsg;
				chatMsg.channel = channel;
				chatMsg.sender = "";
				chatMsg.text = message_text;
				chatMsg.color = eqt::ui::getChannelColor(channel);
				chatMsg.isSystemMessage = true;

				LOG_DEBUG(MOD_ZONE, "[SimpleMessage] Adding to chat: channel={}, text='{}'",
					static_cast<int>(channel), message_text);
				chatWindow->addMessage(std::move(chatMsg));
				return;
			}
		}
	}
#endif

	// Fallback: print to console if no chat window
	std::string type_name = GetChatTypeName(color_type);
	std::cout << fmt::format("[{}] {}", type_name, message_text) << std::endl;
}

void EverQuest::ZoneProcessPlayerStateAdd(const EQ::Net::Packet &p)
{
	// PlayerStateAdd adds a state/buff to a player
	// This could be buffs, debuffs, or other state changes

	if (p.Length() < 4) {
		if (s_debug_level >= 1) {
			std::cout << fmt::format("PlayerStateAdd packet too small: {} bytes", p.Length()) << std::endl;
		}
		return;
	}
	
	// The exact structure depends on the state being added (buff/debuff/state change)
	// Parse what we can from the packet
	if (s_debug_level >= 2 && p.Length() >= 6) {
		uint16_t stateType = p.GetUInt16(2);
		uint16_t stateValue = p.GetUInt16(4);
		LOG_DEBUG(MOD_ZONE, "[PlayerStateAdd] State added: type={:#06x}, value={:#06x}, size={} bytes",
			stateType, stateValue, p.Length());
	}
}

void EverQuest::ZoneProcessDeath(const EQ::Net::Packet &p)
{
	// Death packet indicates an entity has died
	// Death_Struct layout (all uint32):
	//   spawn_id, killer_id, corpseid, bindzoneid, spell_id, attack_skill, damage, unknown

	if (p.Length() < 30) {  // Need at least through damage field
		if (s_debug_level >= 1) {
			std::cout << fmt::format("Death packet too small: {} bytes", p.Length()) << std::endl;
		}
		return;
	}

	// Parse death information - Death_Struct uses uint32 fields
	// Offsets include 2-byte opcode prefix
	uint32_t victim_id = p.GetUInt32(2);    // spawn_id - Who died
	uint32_t killer_id = p.GetUInt32(6);    // killer_id - Who killed them
	// uint32_t corpse_id = p.GetUInt32(10);  // corpseid (unused)
	// uint32_t bind_zone = p.GetUInt32(14);  // bindzoneid (unused)
	uint32_t spell_id = p.GetUInt32(18);    // spell_id - Spell that killed (0 if melee)
	// uint32_t attack_skill = p.GetUInt32(22);  // attack_skill (unused)
	uint32_t damage = p.GetUInt32(26);      // damage - Final damage amount
	
	// Get entity names if we have them
	std::string victim_name = "Unknown";
	std::string killer_name = "Unknown";
	
	// Special case: killer_id 0 means the player character killed it
	if (killer_id == 0) {
		killer_id = m_my_spawn_id;
		killer_name = m_character;
	} else {
		auto killer_it = m_entities.find(killer_id);
		if (killer_it != m_entities.end()) {
			killer_name = killer_it->second.name;
		}
	}
	
	auto victim_it = m_entities.find(victim_id);
	if (victim_it != m_entities.end()) {
		victim_name = victim_it->second.name;
		// Mark entity as dead (0 HP)
		victim_it->second.hp_percent = 0;
		victim_it->second.is_corpse = true;  // Mark as corpse

#ifdef EQT_HAS_GRAPHICS
		// Trigger death animation in graphics
		if (m_graphics_initialized && m_renderer) {
			m_renderer->playEntityDeathAnimation(victim_id);
		}
#endif
	}
	
	if (s_debug_level >= 1) {
		// spell_id of 0, 0xFFFF (65535), or 0xFFFFFFFF means no spell was used
		bool hasValidSpell = spell_id > 0 && spell_id != 0xFFFF && spell_id != 0xFFFFFFFF;
		if (hasValidSpell) {
			LOG_INFO(MOD_COMBAT, "{} ({}) was killed by {} ({}) for {} damage (spell: {})",
				victim_name, victim_id, killer_name, killer_id, damage, spell_id);
		} else {
			LOG_INFO(MOD_COMBAT, "{} ({}) was killed by {} ({}) for {} damage",
				victim_name, victim_id, killer_name, killer_id, damage);
		}
	}
	
	// Track the last entity we killed for SimpleMessage substitution
	// Store the display name now before it gets modified to include "'s corpse"
	if (killer_id == m_my_spawn_id) {
		m_last_slain_entity_name = EQT::toDisplayName(victim_name);
	}

	// Notify combat manager if this was our target
	if (m_combat_manager && victim_id == m_combat_manager->GetTargetId()) {
		// Target died - combat manager should handle looting if enabled
		if (s_debug_level >= 1) {
			LOG_DEBUG(MOD_MAIN, "Our combat target died, checking for loot...");
		}
	}
	
	// If we died, log it prominently
	if (victim_id == m_my_spawn_id) {
		std::cout << "YOU HAVE BEEN SLAIN!" << std::endl;
#ifdef EQT_HAS_GRAPHICS
		// Close vendor window if open (can't shop while dead)
		if (m_renderer && m_renderer->getWindowManager()) {
			if (m_renderer->getWindowManager()->isVendorWindowOpen()) {
				m_renderer->getWindowManager()->closeVendorWindow();
				LOG_DEBUG(MOD_INVENTORY, "Closed vendor window due to player death");
			}
		}
#endif
		// Cancel trade if we died
		if (m_trade_manager && m_trade_manager->isTrading()) {
			LOG_DEBUG(MOD_MAIN, "Player died during trade, canceling trade");
			m_trade_manager->cancelTrade();
		}
	}

	// Cancel trade if trade partner died
	if (m_trade_manager && m_trade_manager->isTrading() &&
		m_trade_manager->getPartnerSpawnId() == victim_id) {
		LOG_DEBUG(MOD_MAIN, "Trade partner died, canceling trade");
		m_trade_manager->cancelTrade();
		AddChatSystemMessage("Trade cancelled - partner died");
	}

	// If the vendor NPC died, close vendor window
#ifdef EQT_HAS_GRAPHICS
	if (m_renderer && m_renderer->getWindowManager()) {
		auto* vendorWindow = m_renderer->getWindowManager()->getVendorWindow();
		if (vendorWindow && vendorWindow->isOpen() && vendorWindow->getNpcId() == victim_id) {
			m_renderer->getWindowManager()->closeVendorWindow();
			LOG_DEBUG(MOD_INVENTORY, "Closed vendor window due to vendor death");
		}
	}
#endif
}

void EverQuest::ZoneProcessPlayerStateRemove(const EQ::Net::Packet &p)
{
	// PlayerStateRemove removes a state/buff from a player
	// This is the counterpart to PlayerStateAdd
	
	if (p.Length() < 4) {
		if (s_debug_level >= 1) {
			std::cout << fmt::format("PlayerStateRemove packet too small: {} bytes", p.Length()) << std::endl;
		}
		return;
	}
	
	// The exact structure depends on the state being removed (buff/debuff/state change)
	// Parse what we can from the packet
	if (s_debug_level >= 2 && p.Length() >= 6) {
		uint16_t stateType = p.GetUInt16(2);
		uint16_t stateValue = p.GetUInt16(4);
		LOG_DEBUG(MOD_ZONE, "[PlayerStateRemove] State removed: type={:#06x}, value={:#06x}, size={} bytes",
			stateType, stateValue, p.Length());
	}
}

void EverQuest::ZoneProcessStamina(const EQ::Net::Packet &p)
{
	// Stamina (endurance) update packet
	// Format: spawn_id (2 bytes), current_stamina (4 bytes), max_stamina (4 bytes)
	
	if (p.Length() < 10) {
		if (s_debug_level >= 1) {
			std::cout << fmt::format("Stamina packet too small: {} bytes", p.Length()) << std::endl;
		}
		return;
	}
	
	uint16_t spawn_id = p.GetUInt16(2);
	uint32_t current_stamina = p.GetUInt32(4);
	uint32_t max_stamina = p.GetUInt32(8);
	
	if (s_debug_level >= 2) {
		std::cout << fmt::format("Stamina update: spawn_id={}, current={}, max={}", 
			spawn_id, current_stamina, max_stamina) << std::endl;
	}
	
	// Store stamina values if this is for our character
	if (spawn_id == m_my_character_id) {
		// TODO: Store stamina values as member variables if needed
	}
}

void EverQuest::ZoneProcessZonePlayerToBind(const EQ::Net::Packet &p)
{
	// ZonePlayerToBind is sent when the player dies and needs to respawn at bind point
	// The packet uses ZoneChange_Struct format (88 bytes):
	// char_name[64], zone_id (2), instance_id (2), y (4), x (4), z (4), zone_reason (4), success (4)

	LOG_INFO(MOD_MAIN, "You are being sent to your bind point...");

	if (p.Length() < 90) {  // 2 bytes opcode + 88 bytes struct
		LOG_WARN(MOD_MAIN, "ZonePlayerToBind packet too small: {} bytes", p.Length());
		// Fall back to stored bind point
		LOG_DEBUG(MOD_MAIN, "Using stored bind point: zone {} at ({:.2f}, {:.2f}, {:.2f})",
			m_bind_zone_id, m_bind_x, m_bind_y, m_bind_z);
		return;
	}

	// Parse ZoneChange_Struct
	char char_name[65] = {0};
	memcpy(char_name, static_cast<const char*>(p.Data()) + 2, 64);
	uint16_t target_zone_id = p.GetUInt16(66);
	uint16_t instance_id = p.GetUInt16(68);
	float zone_y = p.GetFloat(70);  // Server sends y first
	float zone_x = p.GetFloat(74);  // Then x
	float zone_z = p.GetFloat(78);
	uint32_t zone_reason = p.GetUInt32(82);
	int32_t success = p.GetInt32(86);

	// Swap x/y for our coordinate system
	float bind_x = zone_y;
	float bind_y = zone_x;
	float bind_z = zone_z;

	LOG_DEBUG(MOD_MAIN, "Zone change to zone {} (instance {}): ({:.2f}, {:.2f}, {:.2f}) reason={} success={}",
		target_zone_id, instance_id, bind_x, bind_y, bind_z, zone_reason, success);

	if (success != 1) {
		LOG_WARN(MOD_MAIN, "Zone change failed (success != 1)");
		return;
	}

	// Check if we're staying in the same zone or going to a different zone
	bool is_same_zone = (target_zone_id == m_current_zone_id) || (m_current_zone_id == 0);

	if (is_same_zone) {
		// Same zone death - just teleport player locally
		LOG_DEBUG(MOD_MAIN, "Same-zone respawn to ({:.2f}, {:.2f}, {:.2f})",
			bind_x, bind_y, bind_z);

		// Update our position to the bind point
		m_x = bind_x;
		m_y = bind_y;
		m_z = bind_z;
		// Use bind heading if available
		if (m_bind_heading > 0) {
			m_heading = m_bind_heading;
		}

		// Reset HP to full (server will send actual values)
		m_cur_hp = m_max_hp;

		// Update our entity position
		if (m_my_spawn_id != 0) {
			auto it = m_entities.find(m_my_spawn_id);
			if (it != m_entities.end()) {
				it->second.x = m_x;
				it->second.y = m_y;
				it->second.z = m_z;
				it->second.hp_percent = 100;
				it->second.is_corpse = false;
			}
		}

#ifdef EQT_HAS_GRAPHICS
		// Update renderer with new position
		// Convert m_heading from degrees (0-360) to EQ format (0-512)
		if (m_graphics_initialized && m_renderer) {
			float heading512 = m_heading * 512.0f / 360.0f;
			m_renderer->setPlayerPosition(m_x, m_y, m_z, heading512);

			// Clear corpse state for player entity
			if (m_my_spawn_id != 0) {
				auto* entityRenderer = m_renderer->getEntityRenderer();
				if (entityRenderer) {
					entityRenderer->setPlayerEntityVisible(true);
				}
			}
		}
#endif

		// Send position update to server to confirm we're at bind point
		SendPositionUpdate();

		LOG_INFO(MOD_MAIN, "Respawned at bind point ({:.2f}, {:.2f}, {:.2f})",
			m_x, m_y, m_z);
	} else {
		// Different zone death - need full zone transition
		LOG_DEBUG(MOD_MAIN, "Cross-zone respawn: current zone {} -> bind zone {}",
			m_current_zone_id, target_zone_id);

		// Store pending zone info for the transition
		// The server will send us ZoneServerInfo through the world connection
		m_pending_zone_id = target_zone_id;
		m_pending_zone_x = bind_x;
		m_pending_zone_y = bind_y;
		m_pending_zone_z = bind_z;
		m_pending_zone_heading = m_bind_heading;

		// Mark that we're waiting for a zone change
		m_zone_change_requested = true;

		LOG_DEBUG(MOD_MAIN, "Waiting for world server to provide new zone server info...");

		// The server will coordinate the transition:
		// 1. Zone server notifies world server of death respawn
		// 2. World server finds/boots target zone
		// 3. World server sends ZoneServerInfo to client
		// 4. WorldProcessZoneServerInfo() will detect this and handle the transition
	}
}

void EverQuest::ZoneProcessZoneChange(const EQ::Net::Packet &p)
{
	// Server response to our zone change request (or server-initiated zone change)
	// Uses ZoneChange_Struct (88 bytes)

	if (p.Length() < 90) {  // 2 bytes opcode + 88 bytes struct
		std::cout << fmt::format("[WARNING] ZoneChange packet too small: {} bytes", p.Length()) << std::endl;
		return;
	}

	// Parse ZoneChange_Struct
	char char_name[65] = {0};
	memcpy(char_name, static_cast<const char*>(p.Data()) + 2, 64);
	uint16_t zone_id = p.GetUInt16(66);
	uint16_t instance_id = p.GetUInt16(68);
	float zone_y = p.GetFloat(70);  // Server sends y first
	float zone_x = p.GetFloat(74);  // Then x
	float zone_z = p.GetFloat(78);
	uint32_t zone_reason = p.GetUInt32(82);
	int32_t success = p.GetInt32(86);

	// Swap x/y for our coordinate system
	float target_x = zone_y;
	float target_y = zone_x;
	float target_z = zone_z;

	LOG_DEBUG(MOD_ZONE, "Zone change response: zone={} instance={} pos=({:.1f}, {:.1f}, {:.1f}) reason={} success={}",
		zone_id, instance_id, target_x, target_y, target_z, zone_reason, success);

	// Check success code
	// success == 1 means approved
	// success != 1 means denied (various error codes)
	if (success != 1) {
		LOG_WARN(MOD_ZONE, "Zone change DENIED (success={})", success);
		m_zone_change_requested = false;
		m_zone_line_triggered = false;
		m_pending_zone_id = 0;
		return;
	}

	LOG_DEBUG(MOD_ZONE, "Zone change APPROVED to zone {} at ({:.1f}, {:.1f}, {:.1f})",
		zone_id, target_x, target_y, target_z);

	// Store the target coordinates for the new zone
	m_pending_zone_id = zone_id;
	m_pending_zone_x = target_x;
	m_pending_zone_y = target_y;
	m_pending_zone_z = target_z;

	// Zone-to-zone transition flow:
	// 1. Disconnect from current zone server
	// 2. Reconnect to world server (with zoning=1 flag)
	// 3. World server sends ZoneServerInfo for new zone
	// 4. Connect to new zone server
	// 5. Send WorldComplete, world disconnects (normal)

	// Close vendor window if open (we're leaving the zone)
#ifdef EQT_HAS_GRAPHICS
	if (m_renderer && m_renderer->getWindowManager()) {
		if (m_renderer->getWindowManager()->isVendorWindowOpen()) {
			m_renderer->getWindowManager()->closeVendorWindow();
			LOG_DEBUG(MOD_INVENTORY, "Closed vendor window due to zone change");
		}
	}
#endif

	// IMPORTANT: We cannot directly call DisconnectFromZone() here because we're
	// inside a packet callback. Destroying the connection manager from within its
	// own callback causes issues with libuv handle cleanup.
	// Instead, we set a flag and let UpdateMovement() handle the disconnect safely.
	m_zone_change_approved = true;
	LOG_TRACE(MOD_ZONE, "Zone change approved, will process disconnect on next update");
}

void EverQuest::RequestZoneChange(uint16_t zone_id, float x, float y, float z, float heading)
{
	if (!IsFullyZonedIn() || !m_zone_connection) {
		LOG_WARN(MOD_ZONE, "Cannot request zone change - not fully zoned in");
		return;
	}

	if (m_zone_change_requested) {
		LOG_DEBUG(MOD_ZONE, "Zone change already pending, ignoring duplicate request");
		return;
	}

	LOG_DEBUG(MOD_ZONE, "Requesting zone change to zone {} at ({:.1f}, {:.1f}, {:.1f})",
		zone_id, x, y, z);

	// Build ZoneChange_Struct packet
	// The packet is 90 bytes (2 opcode + 88 struct)
	EQ::Net::DynamicPacket p;
	p.Resize(90);  // 2 bytes opcode + 88 bytes ZoneChange_Struct
	memset(p.Data(), 0, 90);

	p.PutUInt16(0, HC_OP_ZoneChange);

	// char_name[64] at offset 2
	size_t name_len = m_character.length();
	if (name_len > 63) name_len = 63;
	memcpy((char*)p.Data() + 2, m_character.c_str(), name_len);

	// zone_id at offset 66
	p.PutUInt16(66, zone_id);

	// instance_id at offset 68 (0 for non-instanced zones)
	p.PutUInt16(68, 0);

	// Server expects Y, X, Z order (opposite of our internal order)
	// Our internal: m_x = server_y, m_y = server_x
	// So to send to server: offset 70 = x (our y), offset 74 = y (our x)
	p.PutFloat(70, y);  // Server's y_pos = our x
	p.PutFloat(74, x);  // Server's x_pos = our y
	p.PutFloat(78, z);

	// zone_reason at offset 82
	// 0 = ZoneSolicited (server-requested)
	// 1 = ZoneUnsolicited (client-initiated, like zone line)
	p.PutUInt32(82, 1);  // ZoneUnsolicited - we're initiating this

	// success at offset 86
	// 0 = request (we're asking)
	// 1 = response (server's reply)
	p.PutInt32(86, 0);  // This is a request

	DumpPacket("C->S", HC_OP_ZoneChange, p);

	if (SafeQueueZonePacket(p)) {
		m_zone_change_requested = true;
		LOG_DEBUG(MOD_ZONE, "Zone change request sent to server");
	} else {
		LOG_ERROR(MOD_ZONE, "Failed to send zone change request");
	}
}

void EverQuest::CleanupZone()
{
	LOG_DEBUG(MOD_ZONE, "Cleaning up current zone state");

#ifdef EQT_HAS_GRAPHICS
	// Zone is no longer ready - show loading screen during transition
	if (m_renderer) {
		m_renderer->setZoneReady(false);
		m_renderer->setLoadingProgress(0.0f, L"Disconnecting from zone...");
	}
#endif

	// Stop any movement
	StopMovement();
	m_is_moving = false;
	m_current_path.clear();
	m_current_path_index = 0;
	m_follow_target.clear();
	m_in_combat_movement = false;

	// Clear entity map (keep only self for now - will be re-populated by new zone)
	uint16_t my_id = m_my_spawn_id;
	Entity my_entity;
	bool have_self = false;
	auto it = m_entities.find(my_id);
	if (it != m_entities.end()) {
		my_entity = it->second;
		have_self = true;
	}
	m_entities.clear();
	if (have_self) {
		m_entities[my_id] = my_entity;
	}

	// Clear door data
	m_doors.clear();

	// Clear world objects (forges, looms, groundspawns)
	ClearWorldObjects();

	// Clear pathfinding data
	m_pathfinder.reset();

	// Clear zone map - but first clear renderer's reference to avoid dangling pointer
#ifdef EQT_HAS_GRAPHICS
	if (m_renderer) {
		m_renderer->setCollisionMap(nullptr);
	}
#endif
	m_zone_map.reset();

	// Clear zone lines
	m_zone_lines.reset();

	// Reset zone line detection state
	m_zone_line_triggered = false;
	m_zone_change_requested = false;

#ifdef EQT_HAS_GRAPHICS
	// Unload zone graphics
	if (m_renderer) {
		m_renderer->unloadZone();
	}
#endif

	// Reset zone connection state flags
	m_zone_connected = false;
	m_zone_session_established = false;
	m_zone_entry_sent = false;
	m_weather_received = false;
	m_req_new_zone_sent = false;
	m_new_zone_received = false;
	m_aa_table_sent = false;
	m_update_aa_sent = false;
	m_tributes_sent = false;
	m_guild_tributes_sent = false;
	m_req_client_spawn_sent = false;
	m_spawn_appearance_sent = false;
	m_exp_zonein_sent = false;
	m_send_exp_zonein_received = false;
	m_server_filter_sent = false;
	m_client_ready_sent = false;
	m_client_spawned = false;

	// Reset AA/tribute tracking
	m_aa_table_count = 0;
	m_tribute_count = 0;
	m_guild_tribute_count = 0;

	// Clear movement history
	m_movement_history.clear();
	m_last_movement_history_send = 0;

	// Reset zone name and ID
	m_current_zone_name.clear();
	m_current_zone_id = 0;

	LOG_DEBUG(MOD_ZONE, "Zone cleanup complete");
}

void EverQuest::DisconnectFromZone()
{
	LOG_DEBUG(MOD_ZONE, "Disconnecting from current zone server");

	// Cleanup zone state first
	CleanupZone();

	// Disable reconnect on the zone connection before disconnecting
	if (m_zone_connection_manager) {
		m_zone_connection_manager->OnConnectionStateChange(
			std::bind(&EverQuest::ZoneOnStatusChangeReconnectDisabled, this,
				std::placeholders::_1, std::placeholders::_2, std::placeholders::_3));
	}

	// Close zone connection
	if (m_zone_connection) {
		m_zone_connection->Close();
		m_zone_connection.reset();
	}

	// Reset zone connection manager (will be recreated when connecting to new zone)
	m_zone_connection_manager.reset();

	LOG_DEBUG(MOD_ZONE, "Disconnected from zone server");
}

void EverQuest::ProcessDeferredZoneChange()
{
	// This method is called from UpdateMovement, which is outside of any callback context.
	// This ensures safe destruction of connection managers.
	LOG_DEBUG(MOD_ZONE, "Processing deferred zone change");

	LOG_TRACE(MOD_ZONE, "Step 1: Disconnecting from current zone");
#ifdef EQT_HAS_GRAPHICS
	if (m_renderer) {
		m_renderer->setLoadingProgress(0.1f, L"Disconnecting from zone...");
	}
#endif
	DisconnectFromZone();
	LOG_TRACE(MOD_ZONE, "Step 1 complete: Zone disconnected");

	// Reconnect to world server to get new zone info
	if (!m_world_server_host.empty()) {
		LOG_TRACE(MOD_ZONE, "Step 2: Creating new world connection manager");
#ifdef EQT_HAS_GRAPHICS
		if (m_renderer) {
			m_renderer->setLoadingProgress(0.15f, L"Connecting to world server...");
		}
#endif

		// Reset world connection state flags so we go through the full handshake again
		m_world_ready = false;
		m_enter_world_sent = false;

		// Reset old connections first
		m_world_connection.reset();
		LOG_TRACE(MOD_ZONE, "Step 2a: Old world connection reset");

		m_world_connection_manager.reset(new EQ::Net::DaybreakConnectionManager());
		LOG_TRACE(MOD_ZONE, "Step 2b: New world connection manager created");

		m_world_connection_manager->OnNewConnection(std::bind(&EverQuest::WorldOnNewConnection, this, std::placeholders::_1));
		m_world_connection_manager->OnConnectionStateChange(std::bind(&EverQuest::WorldOnStatusChangeReconnectEnabled, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3));
		m_world_connection_manager->OnPacketRecv(std::bind(&EverQuest::WorldOnPacketRecv, this, std::placeholders::_1, std::placeholders::_2));
		LOG_TRACE(MOD_ZONE, "Step 2c: Callbacks registered");

		LOG_DEBUG(MOD_ZONE, "Step 3: Connecting to world server at {}:9000", m_world_server_host);
		m_world_connection_manager->Connect(m_world_server_host, 9000);
		LOG_TRACE(MOD_ZONE, "Step 3 complete: Connect() called, waiting for callbacks");
	} else {
		LOG_ERROR(MOD_ZONE, "No world server address stored for reconnection!");
	}

	LOG_TRACE(MOD_ZONE, "ProcessDeferredZoneChange returning");
}

// Pathfinding implementation
void EverQuest::LoadPathfinder(const std::string& zone_name)
{
	if (zone_name.empty()) {
		LOG_DEBUG(MOD_MAIN, "LoadPathfinder: Zone name is empty, skipping");
		return;
	}
	
	LOG_DEBUG(MOD_MAIN, "LoadPathfinder: Loading pathfinder for zone '{}'", zone_name);
	
	// Release previous pathfinder if any
	m_pathfinder.reset();
	m_current_path.clear();
	m_current_path_index = 0;
	
	try {
		// Use the IPathfinder factory method to load the navigation mesh
		m_pathfinder.reset(IPathfinder::Load(zone_name, m_navmesh_path));
		
		if (m_pathfinder) {
			// Check if it's actually a PathfinderNavmesh vs PathfinderNull
			// This is a bit of a hack but helps us understand what type we got
			PathfinderOptions test_opts;
			bool partial = false, stuck = false;
			auto test_path = m_pathfinder->FindPath(
				glm::vec3(0, 0, 0), glm::vec3(1, 1, 1), partial, stuck, test_opts);
			
			if (s_debug_level >= 2) {
				LOG_DEBUG(MOD_MAIN, "Loaded pathfinder for zone: {} (type: {})",
					zone_name, test_path.empty() ? "NavMesh" : "Null");
			}
		} else {
			if (s_debug_level >= 1) {
				LOG_DEBUG(MOD_MAIN, "No navigation mesh available for zone: {}", zone_name);
			}
		}
	} catch (const std::exception& e) {
		if (s_debug_level >= 1) {
			LOG_DEBUG(MOD_MAIN, "Failed to load navigation mesh for {}: {}", zone_name, e.what());
		}
	}
}

bool EverQuest::FindPath(float start_x, float start_y, float start_z, float end_x, float end_y, float end_z)
{
	LOG_DEBUG(MOD_MAIN, "FindPath called: from ({:.2f},{:.2f},{:.2f}) to ({:.2f},{:.2f},{:.2f})", start_x, start_y, start_z, end_x, end_y, end_z);
	
	if (!m_pathfinder) {
		LOG_DEBUG(MOD_MAIN, "FindPath: No pathfinder object available (m_pathfinder is null)");
		return false;
	}
	
	// Clear previous path
	m_current_path.clear();
	m_current_path_index = 0;
	
	// Set up pathfinding options
	PathfinderOptions opts;
	opts.smooth_path = true;
	opts.step_size = 10.0f; // Distance between waypoints
	opts.offset = 5.0f; // Height offset for character
	
	// Find the path
	bool partial = false;
	bool stuck = false;
	LOG_DEBUG(MOD_MAIN, "FindPath: Calling m_pathfinder->FindPath()...");
	IPathfinder::IPath path = m_pathfinder->FindPath(
		glm::vec3(start_x, start_y, start_z),
		glm::vec3(end_x, end_y, end_z),
		partial,
		stuck,
		opts
	);
	if (IsDebugEnabled()) std::cout << fmt::format("[DEBUG] FindPath: Result - path size: {}, partial: {}, stuck: {}", 
		path.size(), partial, stuck) << std::endl;
	
	if (path.empty()) {
		if (s_debug_level >= 1) {
			std::cout << fmt::format("No path found from ({:.1f}, {:.1f}, {:.1f}) to ({:.1f}, {:.1f}, {:.1f})",
				start_x, start_y, start_z, end_x, end_y, end_z) << std::endl;
			if (partial) {
				std::cout << "  (Partial path available)" << std::endl;
			}
			if (stuck) {
				std::cout << "  (Path leads back to start - stuck)" << std::endl;
			}
		}
		return false;
	}
	
	// Convert path nodes to vec3 waypoints
	for (const auto& node : path) {
		// Skip teleport nodes for now
		if (!node.teleport) {
			m_current_path.push_back(node.pos);
		}
	}
	
	if (s_debug_level >= 1) {
		std::cout << fmt::format("Found path with {} waypoints", m_current_path.size()) << std::endl;
		if (s_debug_level >= 2) {
			for (size_t i = 0; i < m_current_path.size() && i < 5; i++) {
				const auto& pos = m_current_path[i];
				std::cout << fmt::format("  Waypoint {}: ({:.1f}, {:.1f}, {:.1f})", 
					i, pos.x, pos.y, pos.z) << std::endl;
			}
			if (m_current_path.size() > 5) {
				std::cout << "  ..." << std::endl;
			}
		}
	}
	
	return true;
}

void EverQuest::MoveToWithPath(float x, float y, float z)
{
	if (!IsFullyZonedIn()) {
		std::cout << "Error: Not in zone yet" << std::endl;
		return;
	}
	
	// If pathfinding is disabled or not available, use direct movement
	if (!m_use_pathfinding || !m_pathfinder) {
		MoveTo(x, y, z);
		return;
	}
	
	// Find path from current position to target
	if (FindPath(m_x, m_y, m_z, x, y, z)) {
		// Face the final destination immediately
		float new_heading = CalculateHeading(m_x, m_y, x, y);
		if (std::abs(new_heading - m_heading) > 0.1f) {
			m_heading = new_heading;
			SendPositionUpdate();
		}
		
		// Start following the path
		m_current_path_index = 0;
		FollowPath();
	} else {
		// Fallback to direct movement if pathfinding fails
		if (s_debug_level >= 1) {
			std::cout << "Pathfinding failed, using direct movement" << std::endl;
		}
		MoveTo(x, y, z);
	}
}

void EverQuest::FollowPath()
{
	if (m_current_path.empty() || m_current_path_index >= m_current_path.size()) {
		StopMovement();
		return;
	}
	
	// Skip waypoints that are too close to current position (within 1 unit)
	while (m_current_path_index < m_current_path.size()) {
		const auto& waypoint = m_current_path[m_current_path_index];
		float dist = CalculateDistance2D(m_x, m_y, waypoint.x, waypoint.y);
		
		if (dist > 1.0f) {
			// This waypoint is far enough, move to it
			MoveTo(waypoint.x, waypoint.y, waypoint.z);
			break;
		} else {
			// Skip this waypoint, it's too close
			if (s_debug_level >= 2) {
				LOG_DEBUG(MOD_MAIN, "Skipping waypoint {} at ({:.1f},{:.1f},{:.1f}) - too close (dist={:.1f})", m_current_path_index, waypoint.x, waypoint.y, waypoint.z, dist);
			}
			m_current_path_index++;
		}
	}
	
	// If we've gone through all waypoints, stop
	if (m_current_path_index >= m_current_path.size()) {
		StopMovement();
		return;
	}
	
	// The UpdateMovement function will handle checking when we reach waypoints
}

void EverQuest::LoadZoneMap(const std::string& zone_name)
{
	if (zone_name.empty()) {
		LOG_DEBUG(MOD_MAIN, "LoadZoneMap: Zone name is empty, skipping");
		return;
	}
	
	LOG_DEBUG(MOD_MAIN, "LoadZoneMap: Loading map for zone '{}'", zone_name);

	// Release previous map if any - first clear renderer's reference to avoid dangling pointer
#ifdef EQT_HAS_GRAPHICS
	if (m_renderer) {
		m_renderer->setCollisionMap(nullptr);
	}
#endif
	m_zone_map.reset();
	
	// Use custom maps path if set, otherwise try common locations
	std::string maps_path = m_maps_path;
	if (maps_path.empty()) {
		// Try to use the same path as navmesh
		if (!m_navmesh_path.empty()) {
			// Navigate from nav directory to maps directory
			size_t nav_pos = m_navmesh_path.find("/nav");
			if (nav_pos != std::string::npos) {
				maps_path = m_navmesh_path.substr(0, nav_pos);
			}
		}
		
		// Fallback to default location (current directory)
		if (maps_path.empty()) {
			maps_path = "./maps";
		}
	}
	
	LOG_DEBUG(MOD_MAIN, "LoadZoneMap: Using maps path: {}", maps_path);
	
	m_zone_map.reset(HCMap::LoadMapFile(zone_name, maps_path));
	
	if (!m_zone_map) {
		std::cout << fmt::format("[WARNING] Failed to load map for zone: {}", zone_name) << std::endl;
	}
}

void EverQuest::LoadZoneLines(const std::string& zone_name)
{
	if (zone_name.empty()) {
		LOG_DEBUG(MOD_MAIN, "LoadZoneLines: Zone name is empty, skipping");
		return;
	}

	LOG_DEBUG(MOD_MAIN, "LoadZoneLines: Loading zone lines for '{}'", zone_name);

	// Create new ZoneLines instance
	m_zone_lines = std::make_unique<EQT::ZoneLines>();

	// Reset zone line detection state
	m_zone_line_triggered = false;
	m_zone_change_requested = false;
	m_pending_zone_id = 0;
	m_last_zone_check_x = m_x;
	m_last_zone_check_y = m_y;
	m_last_zone_check_z = m_z;

	// Try to load zone lines from the zone's S3D file
	if (!m_eq_client_path.empty()) {
		if (m_zone_lines->loadFromZone(zone_name, m_eq_client_path)) {
			LOG_DEBUG(MOD_MAP, "LoadZoneLines: Loaded {} zone lines from WLD", m_zone_lines->getZoneLineCount());
		} else {
			LOG_DEBUG(MOD_MAP, "LoadZoneLines: No zone lines found in WLD for '{}'", zone_name);
		}
	} else {
		LOG_DEBUG(MOD_MAP, "LoadZoneLines: No EQ client path set, zone lines from WLD unavailable");
	}
}

void EverQuest::CheckZoneLine()
{
	// TODO: Automatic zoning disabled - needs fixing
	return;

	// Skip if not fully zoned in or no zone lines loaded
	if (!IsFullyZonedIn() || !m_zone_lines || !m_zone_lines->hasZoneLines()) {
#ifdef EQT_HAS_GRAPHICS
		// Clear zone line visual indicator
		if (m_renderer) {
			m_renderer->setZoneLineDebug(false);
		}
#endif
		return;
	}

	// Skip if a zone change is already in progress
	if (m_zone_change_requested) {
		return;
	}

	// Debug: periodically log zone line check status
	static int checkCount = 0;
	checkCount++;
	if (checkCount % 100 == 0) {
		LOG_TRACE(MOD_ZONE, "Zone line check #{}: pos=({:.1f}, {:.1f}, {:.1f}) zone_lines={} regions={}",
			checkCount, m_x, m_y, m_z,
			m_zone_lines->hasZoneLines() ? "yes" : "no",
			m_zone_lines->getZoneLineCount());
	}

	// Skip if we haven't moved since last check
	constexpr float MIN_MOVE_THRESHOLD = 0.1f;
	float dx = m_x - m_last_zone_check_x;
	float dy = m_y - m_last_zone_check_y;
	float dz = m_z - m_last_zone_check_z;
	float dist_sq = dx * dx + dy * dy + dz * dz;

	if (dist_sq < MIN_MOVE_THRESHOLD * MIN_MOVE_THRESHOLD) {
		return;
	}

	// Update last checked position
	float old_x = m_last_zone_check_x;
	float old_y = m_last_zone_check_y;
	float old_z = m_last_zone_check_z;
	m_last_zone_check_x = m_x;
	m_last_zone_check_y = m_y;
	m_last_zone_check_z = m_z;

	// Run coordinate mapping test ONCE to find correct transformation
	static bool ranCoordTest = false;
	if (!ranCoordTest) {
		ranCoordTest = true;
		// m_x = server_y, m_y = server_x (client swaps coords)
		// So server coords are (m_y, m_x, m_z)
		m_zone_lines->debugTestCoordinateMappings(m_y, m_x, m_z);
	}

	// Check if currently in a zone line region
	// BSP tree uses (server_y, server_x, server_z) based on testing with bsp_region_finder
	// m_x = server_y, m_y = server_x (from spawn struct)
	// Zone lines at BSP x >= 1400 correspond to high server_y (north direction)
	float bsp_x = m_x;   // server_y (north/south)
	float bsp_y = m_y;   // server_x (east/west)
	float bsp_z = m_z;
	EQT::ZoneLineResult result = m_zone_lines->checkPosition(bsp_x, bsp_y, bsp_z, bsp_x, bsp_y, bsp_z);

	// Debug output for every position check (throttled)
	if (checkCount % 50 == 0) {
		LOG_TRACE(MOD_ZONE, "Zone line check: bsp=({:.1f}, {:.1f}, {:.1f}) server=({:.1f}, {:.1f}, {:.1f}) -> isZoneLine={}",
			bsp_x, bsp_y, bsp_z, m_y, m_x, m_z, result.isZoneLine ? "YES" : "no");
	}

	if (result.isZoneLine) {
		// We're in a zone line
		auto now = std::chrono::steady_clock::now();

#ifdef EQT_HAS_GRAPHICS
		// Set zone line visual indicator
		if (m_renderer) {
			std::string debugText = fmt::format("pos=({:.1f}, {:.1f}, {:.1f})", m_x, m_y, m_z);
			m_renderer->setZoneLineDebug(true, result.targetZoneId, debugText);
		}
#endif

		// Debounce: require 500ms cooldown after a trigger before allowing another
		if (m_zone_line_triggered) {
			auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - m_zone_line_trigger_time);
			if (elapsed.count() < 500) {
				// Still in cooldown period, ignore
				return;
			}
		}

		// Check if this is a reference-type zone line that needs server lookup
		if (result.needsServerLookup) {
			// Log warning - the server should have sent us the zone point data
			LOG_WARN(MOD_ZONE, "Zone line triggered but missing zone point data for index {}", result.zonePointIndex);
			// Still trigger the zone line, the server will handle it
		}

		// Trigger zone line!
		m_zone_line_triggered = true;
		m_zone_line_trigger_time = now;
		m_pending_zone_id = result.targetZoneId;
		m_pending_zone_x = result.targetX;
		m_pending_zone_y = result.targetY;
		m_pending_zone_z = result.targetZ;
		m_pending_zone_heading = result.heading;

		LOG_INFO(MOD_ZONE, "Zone line triggered! Target zone: {}, coords: ({:.1f}, {:.1f}, {:.1f}), heading: {:.1f}",
		        m_pending_zone_id, m_pending_zone_x, m_pending_zone_y, m_pending_zone_z, m_pending_zone_heading);

		// Send zone change request to server
		RequestZoneChange(m_pending_zone_id, m_pending_zone_x, m_pending_zone_y, m_pending_zone_z, m_pending_zone_heading);
	} else {
#ifdef EQT_HAS_GRAPHICS
		// Clear zone line visual indicator
		if (m_renderer) {
			m_renderer->setZoneLineDebug(false);
		}
#endif

		// Not in a zone line - clear the triggered flag if we were previously in one
		if (m_zone_line_triggered) {
			// Check if enough time has passed and we're no longer in a zone line
			auto now = std::chrono::steady_clock::now();
			auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - m_zone_line_trigger_time);
			if (elapsed.count() >= 500) {
				// Cooldown expired and we're not in a zone line, reset state
				m_zone_line_triggered = false;
				m_pending_zone_id = 0;
			}
		}
	}
}

float EverQuest::GetBestZ(float x, float y, float z)
{
	if (!m_zone_map) {
		return z; // Return current Z if no map loaded
	}
	
	glm::vec3 pos(x, y, z);
	glm::vec3 result;
	float best_z = m_zone_map->FindBestZ(pos, &result);

	// BEST_Z_INVALID from hc_map.h is a sentinel value indicating no valid ground found
	if (best_z == (float)BEST_Z_INVALID) {
		return z; // Return current Z if no valid ground found
	}
	
	return best_z;
}

void EverQuest::FixZ()
{
	if (!m_zone_map) {
		// Log once when following starts
		static bool logged_no_map = false;
		if (!logged_no_map && !m_follow_target.empty()) {
			LOG_DEBUG(MOD_MAIN, "FixZ: No zone map loaded - Z-height fixing disabled");
			logged_no_map = true;
		}
		return;
	}
	
	// Get the best Z for our current position
	float new_z = GetBestZ(m_x, m_y, m_z);
	float z_diff = new_z - m_z;
	float abs_diff = std::abs(z_diff);
	
	// Always log Z-height info when following for debugging
	if (!m_follow_target.empty() && s_debug_level >= 1) {
		// Find the entity we're following to compare Z values
		Entity* target = nullptr;
		for (const auto& pair : m_entities) {
			if (pair.second.name == m_follow_target) {
				target = const_cast<Entity*>(&pair.second);
				break;
			}
		}
		
		if (target) {
			LOG_DEBUG(MOD_MAIN, "FixZ: Following {} - My Z: {:.2f}, Target Z: {:.2f}, Map thinks Z should be: {:.2f}, Diff: {:.2f}", m_follow_target, m_z, target->z, new_z, z_diff);
		}
	}
	
	// Only update if the difference is significant
	if (abs_diff > 1.5f && abs_diff < 20.0f) {
		// Much more conservative Z adjustment to prevent bouncing
		float adjustment;
		
		if (abs_diff < 3.0f) {
			// Small differences: move only 20% of the way
			adjustment = z_diff * 0.2f;
		} else if (abs_diff < 5.0f) {
			// Medium differences: move 15% of the way
			adjustment = z_diff * 0.15f;
		} else {
			// Large differences: move 10% of the way, capped at 1.0 units
			adjustment = z_diff * 0.1f;
			if (std::abs(adjustment) > 1.0f) {
				adjustment = (adjustment > 0) ? 1.0f : -1.0f;
			}
		}
		
		if (s_debug_level >= 1) {
			LOG_DEBUG(MOD_MAIN, "FixZ: Adjusting Z from {:.2f} to {:.2f} (adjustment: {:.2f}, diff was: {:.2f})", 
				m_z, m_z + adjustment, adjustment, z_diff);
		}
		
		m_z += adjustment;
	} else if (s_debug_level >= 2 && abs_diff > 0.01f) {
		LOG_DEBUG(MOD_MAIN, "FixZ: Z difference too small to adjust: {:.2f} (current: {:.2f}, best: {:.2f})", 
			z_diff, m_z, new_z);
	}
}

void EverQuest::SetMovementMode(MovementMode mode)
{
	if (m_movement_mode == mode) {
		return;
	}
	
	m_movement_mode = mode;
	
	// Update movement speed based on mode
	switch (mode) {
		case MOVE_MODE_RUN:
			m_move_speed = DEFAULT_RUN_SPEED;
			break;
		case MOVE_MODE_WALK:
			m_move_speed = DEFAULT_WALK_SPEED;
			break;
		case MOVE_MODE_SNEAK:
			m_move_speed = DEFAULT_WALK_SPEED * 0.6f;  // Sneak is 60% of walk speed
			SetSneak(true);
			break;
	}
	
	if (s_debug_level >= 1) {
		LOG_DEBUG(MOD_MAIN, "Movement mode changed to {} (speed: {:.1f})", 
			mode == MOVE_MODE_RUN ? "RUN" : mode == MOVE_MODE_WALK ? "WALK" : "SNEAK", 
			m_move_speed);
	}
}

void EverQuest::SetPositionState(PositionState state)
{
	if (m_position_state == state) {
		return;
	}
	
	m_position_state = state;
	
	switch (state) {
		case POS_STANDING:
			SendSpawnAppearance(AT_ANIMATION, 100);  // Animation::Standing
			if (m_is_moving) {
				StopMovement();
			}
			// Cancel camp timer if standing up
			if (m_is_camping) {
				CancelCamp();
			}
			break;
		case POS_SITTING:
			SendSpawnAppearance(AT_ANIMATION, 110);  // Animation::Sitting
			if (m_is_moving) {
				StopMovement();
			}
			break;
		case POS_CROUCHING:
			SendSpawnAppearance(AT_ANIMATION, 111);  // Animation::Crouching
			break;
		case POS_FEIGN_DEATH:
			SendSpawnAppearance(AT_ANIMATION, ANIM_LYING);  // Use lying animation for feign death
			if (m_is_moving) {
				StopMovement();
			}
			break;
		case POS_DEAD:
			SendSpawnAppearance(AT_DIE, 0);  // Use AT_DIE for actual death
			if (m_is_moving) {
				StopMovement();
			}
			break;
	}
	
	if (s_debug_level >= 1) {
		const char* state_names[] = {"STANDING", "SITTING", "CROUCHING", "FEIGN_DEATH", "DEAD"};
		LOG_DEBUG(MOD_MAIN, "Position state changed to {}", state_names[state]);
	}
}

void EverQuest::SendSpawnAppearance(uint16_t type, uint32_t value)
{
	if (!IsFullyZonedIn()) {
		return;
	}
	
	EQ::Net::DynamicPacket p;
	p.Resize(10);  // SpawnAppearance packet size: 2 bytes opcode + 8 bytes struct
	
	p.PutUInt16(0, HC_OP_SpawnAppearance);
	p.PutUInt16(2, m_my_spawn_id);  // spawn_id
	p.PutUInt16(4, type);            // type
	p.PutUInt32(6, value);           // parameter
	// Removed the extra 4 bytes that were at offset 10
	
	m_zone_connection->QueuePacket(p);
	
	if (s_debug_level >= 1) {
		LOG_DEBUG(MOD_MAIN, "Sent SpawnAppearance: spawn_id={}, type={}, value={}", 
			m_my_spawn_id, type, value);
	}
}

void EverQuest::SendAnimation(uint8_t animation_id, uint8_t animation_speed)
{
	if (!IsFullyZonedIn()) {
		return;
	}
	
	EQ::Net::DynamicPacket p;
	p.Resize(6);  // Animation packet size: 2 bytes opcode + 4 bytes data
	
	p.PutUInt16(0, HC_OP_Emote);          // opcode (OP_Animation)
	p.PutUInt16(2, m_my_spawn_id);        // spawn_id
	p.PutUInt8(4, animation_speed);       // animation speed (usually 10)
	p.PutUInt8(5, animation_id);          // animation ID
	
	m_zone_connection->QueuePacket(p, 0, false);  // Send unreliable
	
	if (s_debug_level >= 1) {
		LOG_DEBUG(MOD_MAIN, "Sent Animation: spawn_id={}, speed={}, animation_id={}",
			m_my_spawn_id, animation_speed, animation_id);
	}
}

void EverQuest::SendClickDoor(uint8_t door_id, uint32_t item_id)
{
	if (!IsFullyZonedIn()) {
		LOG_DEBUG(MOD_ENTITY, "Cannot click door - not fully zoned in");
		return;
	}

	// Check if door exists
	auto it = m_doors.find(door_id);
	if (it == m_doors.end()) {
		LOG_WARN(MOD_ENTITY, "Attempted to click unknown door {}", door_id);
		return;
	}

	LOG_INFO(MOD_ENTITY, "Clicking door {} ('{}')", door_id, it->second.name);

	// Track this as a user-initiated door click
	m_pending_door_clicks.insert(door_id);

	// Build ClickDoor packet
	EQ::Net::DynamicPacket p;
	p.Resize(2 + sizeof(EQT::ClickDoor_Struct));

	p.PutUInt16(0, HC_OP_ClickDoor);
	p.PutUInt8(2, door_id);           // doorid
	p.PutUInt8(3, 0);                 // unknown001
	p.PutUInt8(4, 0);                 // unknown002
	p.PutUInt8(5, 0);                 // unknown003
	p.PutUInt8(6, 0);                 // picklockskill
	p.PutUInt8(7, 0);                 // unknown005[0]
	p.PutUInt8(8, 0);                 // unknown005[1]
	p.PutUInt8(9, 0);                 // unknown005[2]
	p.PutUInt32(10, item_id);         // item_id (key)
	p.PutUInt16(14, m_my_spawn_id);   // player_id
	p.PutUInt16(16, 0);               // unknown014

	m_zone_connection->QueuePacket(p, 0, true);
}

// ============================================================================
// World Object / Tradeskill Methods
// ============================================================================

void EverQuest::SendClickObject(uint32_t drop_id)
{
	if (!IsFullyZonedIn()) {
		LOG_DEBUG(MOD_ENTITY, "Cannot click object - not fully zoned in");
		return;
	}

	// Check if object exists
	auto it = m_world_objects.find(drop_id);
	if (it == m_world_objects.end()) {
		LOG_WARN(MOD_ENTITY, "Attempted to click unknown object {}", drop_id);
		return;
	}

	LOG_INFO(MOD_ENTITY, "Clicking object {} ('{}') type={} ({})",
		drop_id, it->second.name, it->second.object_type,
		it->second.getTradeskillName());

	// Build ClickObject packet
	EQ::Net::DynamicPacket p;
	p.Resize(2 + sizeof(EQT::ClickObject_Struct));

	p.PutUInt16(0, HC_OP_ClickObject);
	p.PutUInt32(2, drop_id);           // drop_id
	p.PutUInt32(6, m_my_spawn_id);     // player_id

	m_zone_connection->QueuePacket(p, 0, true);
}

void EverQuest::SendTradeSkillCombine(int16_t container_slot)
{
	if (!IsFullyZonedIn()) {
		LOG_DEBUG(MOD_ENTITY, "Cannot combine - not fully zoned in");
		return;
	}

	LOG_INFO(MOD_ENTITY, "Sending tradeskill combine for container slot {}", container_slot);

	// Build TradeSkillCombine packet
	EQ::Net::DynamicPacket p;
	p.Resize(2 + sizeof(EQT::NewCombine_Struct));

	p.PutUInt16(0, HC_OP_TradeSkillCombine);
	p.PutInt16(2, container_slot);     // container_slot
	p.PutInt16(4, 0);                  // guildtribute_slot (usually 0)

	m_zone_connection->QueuePacket(p, 0, true);
}

void EverQuest::SendCloseContainer(uint32_t drop_id)
{
	if (!IsFullyZonedIn()) {
		LOG_DEBUG(MOD_ENTITY, "Cannot close container - not fully zoned in");
		return;
	}

	LOG_DEBUG(MOD_ENTITY, "Closing tradeskill container {}", drop_id);

	// Build ClickObjectAction packet to close the container
	EQ::Net::DynamicPacket p;
	p.Resize(2 + sizeof(EQT::ClickObjectAction_Struct));

	p.PutUInt16(0, HC_OP_ClickObjectAction);
	p.PutUInt32(2, m_my_spawn_id);     // player_id
	p.PutUInt32(6, drop_id);           // drop_id
	p.PutUInt32(10, 0);                // open = 0 (closing)
	p.PutUInt32(14, 0);                // type
	p.PutUInt32(18, 0);                // unknown16
	p.PutUInt32(22, 0);                // icon
	p.PutUInt32(26, 0);                // unknown24
	// object_name is 64 bytes of zeros (not needed for close)
	for (int i = 0; i < 64; i++) {
		p.PutUInt8(30 + i, 0);
	}

	m_zone_connection->QueuePacket(p, 0, true);

	// Clear active tradeskill object
	m_active_tradeskill_object_id = 0;
}

const eqt::WorldObject* EverQuest::GetWorldObject(uint32_t drop_id) const
{
	auto it = m_world_objects.find(drop_id);
	if (it != m_world_objects.end()) {
		return &it->second;
	}
	return nullptr;
}

void EverQuest::ClearWorldObjects()
{
	LOG_DEBUG(MOD_ENTITY, "Clearing {} world objects", m_world_objects.size());
	m_world_objects.clear();
	m_active_tradeskill_object_id = 0;
}

void EverQuest::ZoneProcessClickObjectAction(const EQ::Net::Packet &p)
{
	// Parse ClickObjectAction_Struct from packet (skip 2-byte opcode)
	if (p.Length() < 2 + sizeof(EQT::ClickObjectAction_Struct)) {
		LOG_WARN(MOD_ENTITY, "ClickObjectAction packet too small: {} bytes (need {})",
			p.Length(), 2 + sizeof(EQT::ClickObjectAction_Struct));
		return;
	}

	const auto* action = reinterpret_cast<const EQT::ClickObjectAction_Struct*>(p.Data() + 2);

	std::string objectName(action->object_name, strnlen(action->object_name, sizeof(action->object_name)));

	if (action->open == 1) {
		// Server is telling us to open a tradeskill container
		LOG_INFO(MOD_ENTITY, "Opening tradeskill container: drop_id={} type={} icon={} name='{}'",
			action->drop_id, action->type, action->icon, objectName);

		// Store the active tradeskill object ID
		m_active_tradeskill_object_id = action->drop_id;

		// Tell WindowManager to open the tradeskill window
		auto* windowManager = m_renderer ? m_renderer->getWindowManager() : nullptr;
		if (windowManager) {
			// World containers have 10 slots (WORLD_BEGIN to WORLD_END)
			windowManager->openTradeskillContainer(action->drop_id, objectName,
				static_cast<uint8_t>(action->type), eqt::inventory::WORLD_COUNT);
		}
	} else {
		// Server is confirming container is closed
		LOG_DEBUG(MOD_ENTITY, "Tradeskill container closed: drop_id={}", action->drop_id);
		m_active_tradeskill_object_id = 0;

		// Close the window if it's open
		auto* windowManager = m_renderer ? m_renderer->getWindowManager() : nullptr;
		if (windowManager) {
			windowManager->closeTradeskillContainer();
		}
	}
}

void EverQuest::ZoneProcessTradeSkillCombine(const EQ::Net::Packet &p)
{
	// This is the server response to a combine attempt
	// The server will send item updates via OP_ItemPacket for the result
	// and skill updates via OP_SkillUpdate for skill-ups

	LOG_DEBUG(MOD_ENTITY, "TradeSkillCombine response received ({} bytes)", p.Length());

	// The packet format for the result may vary - typically the server
	// handles the item consumption and creation, then sends inventory updates.
	// For now, just log that we received a response.

	// TODO: Parse combine result and show success/failure message
	// The result information may come via OP_SpecialMesg or OP_FormattedMessage
}

// ============================================================================
// Group Methods
// ============================================================================

const GroupMember* EverQuest::GetGroupMember(int index) const
{
	if (index >= 0 && index < m_group_member_count) {
		return &m_group_members[index];
	}
	return nullptr;
}

void EverQuest::SendGroupInvite(const std::string& target_name)
{
	if (!m_zone_connection_manager || !IsFullyZonedIn()) {
		LOG_WARN(MOD_MAIN, "Cannot send group invite - not connected to zone");
		return;
	}

	EQT::GroupInvite_Struct packet = {};
	strncpy(packet.inviter_name, m_character.c_str(), 63);
	strncpy(packet.invitee_name, target_name.c_str(), 63);

	EQ::Net::DynamicPacket p;
	p.Resize(2 + sizeof(EQT::GroupInvite_Struct));
	p.PutUInt16(0, HC_OP_GroupInvite);
	p.PutData(2, &packet, sizeof(packet));

	m_zone_connection->QueuePacket(p, 0, true);
	LOG_INFO(MOD_MAIN, "Sent group invite to {}", target_name);
}

void EverQuest::SendGroupFollow(const std::string& inviter_name)
{
	if (!m_zone_connection_manager || !IsFullyZonedIn()) {
		LOG_WARN(MOD_MAIN, "Cannot accept group invite - not connected to zone");
		return;
	}

	EQT::GroupFollow_Struct packet = {};
	strncpy(packet.name1, inviter_name.c_str(), 63);
	strncpy(packet.name2, m_character.c_str(), 63);

	EQ::Net::DynamicPacket p;
	p.Resize(2 + sizeof(EQT::GroupFollow_Struct));
	p.PutUInt16(0, HC_OP_GroupFollow);
	p.PutData(2, &packet, sizeof(packet));

	m_zone_connection->QueuePacket(p, 0, true);

	m_has_pending_invite = false;
	m_pending_inviter_name.clear();

	LOG_INFO(MOD_MAIN, "Accepted group invite from {}", inviter_name);
}

void EverQuest::SendGroupDecline(const std::string& inviter_name)
{
	if (!m_zone_connection_manager || !IsFullyZonedIn()) {
		LOG_WARN(MOD_MAIN, "Cannot decline group invite - not connected to zone");
		return;
	}

	EQT::GroupCancel_Struct packet = {};
	strncpy(packet.name1, inviter_name.c_str(), 63);
	strncpy(packet.name2, m_character.c_str(), 63);
	packet.toggle = 0;

	EQ::Net::DynamicPacket p;
	p.Resize(2 + sizeof(EQT::GroupCancel_Struct));
	p.PutUInt16(0, HC_OP_GroupCancelInvite);
	p.PutData(2, &packet, sizeof(packet));

	m_zone_connection->QueuePacket(p, 0, true);

	m_has_pending_invite = false;
	m_pending_inviter_name.clear();

	LOG_INFO(MOD_MAIN, "Declined group invite from {}", inviter_name);
}

void EverQuest::SendGroupDisband()
{
	if (!m_zone_connection_manager || !IsFullyZonedIn() || !m_in_group) {
		LOG_WARN(MOD_MAIN, "Cannot disband group - not in group or not connected");
		return;
	}

	EQT::GroupDisband_Struct packet = {};
	strncpy(packet.name1, m_character.c_str(), 63);
	strncpy(packet.name2, m_character.c_str(), 63);

	EQ::Net::DynamicPacket p;
	p.Resize(2 + sizeof(EQT::GroupDisband_Struct));
	p.PutUInt16(0, HC_OP_GroupDisband);
	p.PutData(2, &packet, sizeof(packet));

	m_zone_connection->QueuePacket(p, 0, true);
	LOG_INFO(MOD_MAIN, "Sent group disband");
}

void EverQuest::SendLeaveGroup()
{
	// Same packet as disband for non-leader
	SendGroupDisband();
}

void EverQuest::AcceptGroupInvite()
{
	if (m_has_pending_invite) {
		SendGroupFollow(m_pending_inviter_name);
		AddChatSystemMessage("You have joined " + m_pending_inviter_name + "'s group");
	}
}

void EverQuest::DeclineGroupInvite()
{
	if (m_has_pending_invite) {
		SendGroupDecline(m_pending_inviter_name);
		AddChatSystemMessage("Group invite declined");
	}
}

void EverQuest::ClearGroup()
{
	m_in_group = false;
	m_is_group_leader = false;
	m_group_leader_name.clear();
	m_group_member_count = 0;
	for (auto& member : m_group_members) {
		member = GroupMember();
	}
}

int EverQuest::FindGroupMemberByName(const std::string& name) const
{
	for (int i = 0; i < m_group_member_count; i++) {
		if (m_group_members[i].name == name) {
			return i;
		}
	}
	return -1;
}

void EverQuest::UpdateGroupMemberFromEntity(int index)
{
	if (index < 0 || index >= m_group_member_count) {
		return;
	}

	GroupMember& member = m_group_members[index];

	// Find entity by name
	for (const auto& [id, entity] : m_entities) {
		if (entity.name == member.name) {
			member.spawn_id = id;
			member.level = entity.level;
			member.class_id = entity.class_id;
			member.hp_percent = entity.hp_percent;
			member.in_zone = true;
			return;
		}
	}

	// Entity not found - mark as out of zone
	member.spawn_id = 0;
	member.in_zone = false;
}

// Group packet handlers
void EverQuest::ZoneProcessGroupInvite(const EQ::Net::Packet& p)
{
	if (p.Length() < 2 + sizeof(EQT::GroupInvite_Struct)) {
		LOG_WARN(MOD_MAIN, "GroupInvite packet too small: {} bytes", p.Length());
		return;
	}

	auto* data = reinterpret_cast<const EQT::GroupInvite_Struct*>(static_cast<const char*>(p.Data()) + 2);
	std::string inviter_name(data->inviter_name);
	std::string invitee_name(data->invitee_name);

	LOG_INFO(MOD_MAIN, "Group invite received: {} invited {}", inviter_name, invitee_name);

	// Check if this invite is for us
	if (invitee_name == m_character) {
		m_has_pending_invite = true;
		m_pending_inviter_name = inviter_name;
		AddChatSystemMessage(inviter_name + " has invited you to join a group");

#ifdef EQT_HAS_GRAPHICS
		// Show pending invite in group window
		if (m_renderer) {
			auto* windowMgr = m_renderer->getWindowManager();
			if (windowMgr && windowMgr->getGroupWindow()) {
				windowMgr->getGroupWindow()->showPendingInvite(inviter_name);
				windowMgr->openGroupWindow();
			}
		}
#endif
	}
}

void EverQuest::ZoneProcessGroupFollow(const EQ::Net::Packet& p)
{
	// This is sent as confirmation that someone joined the group
	LOG_DEBUG(MOD_MAIN, "GroupFollow packet received ({} bytes)", p.Length());
}

void EverQuest::ZoneProcessGroupUpdate(const EQ::Net::Packet& p)
{
	if (p.Length() < 2 + sizeof(EQT::GroupUpdate_Struct)) {
		LOG_WARN(MOD_MAIN, "GroupUpdate packet too small: {} bytes", p.Length());
		return;
	}

	auto* data = reinterpret_cast<const EQT::GroupUpdate_Struct*>(static_cast<const char*>(p.Data()) + 2);

	switch (data->action) {
		case EQT::GROUP_ACT_UPDATE:
		case EQT::GROUP_ACT_JOIN:
		{
			// Full group update
			ClearGroup();
			m_in_group = true;
			m_group_leader_name = std::string(data->leadersname);
			m_is_group_leader = (m_group_leader_name == m_character);

			// Hide pending invite since we joined
			m_has_pending_invite = false;
			m_pending_inviter_name.clear();
#ifdef EQT_HAS_GRAPHICS
			if (m_renderer) {
				auto* windowMgr = m_renderer->getWindowManager();
				if (windowMgr && windowMgr->getGroupWindow()) {
					windowMgr->getGroupWindow()->hidePendingInvite();
				}
			}
#endif

			// Add self first
			GroupMember self;
			self.name = m_character;
			self.spawn_id = m_my_spawn_id;
			self.level = m_level;
			self.class_id = static_cast<uint8_t>(m_class);
			self.hp_percent = (m_max_hp > 0) ? static_cast<uint8_t>(m_cur_hp * 100 / m_max_hp) : 100;
			self.mana_percent = (m_max_mana > 0) ? static_cast<uint8_t>(m_mana * 100 / m_max_mana) : 100;
			self.is_leader = m_is_group_leader;
			self.in_zone = true;
			m_group_members[0] = self;
			m_group_member_count = 1;

			// Add other members
			for (int i = 0; i < 5 && m_group_member_count < MAX_GROUP_MEMBERS; i++) {
				std::string member_name(data->membername[i]);
				if (!member_name.empty() && member_name != m_character) {
					GroupMember member;
					member.name = member_name;
					member.is_leader = (member_name == m_group_leader_name);

					// Try to find entity for HP/mana
					for (const auto& [id, entity] : m_entities) {
						if (entity.name == member_name) {
							member.spawn_id = id;
							member.level = entity.level;
							member.class_id = entity.class_id;
							member.hp_percent = entity.hp_percent;
							member.in_zone = true;
							break;
						}
					}

					m_group_members[m_group_member_count++] = member;
				}
			}

			AddChatSystemMessage("Group updated");
			LOG_INFO(MOD_MAIN, "Group update: {} members, leader: {}",
					 m_group_member_count, m_group_leader_name);
			break;
		}

		case EQT::GROUP_ACT_LEAVE:
		{
			// Someone left the group
			std::string member_name(data->membername[0]);
			int idx = FindGroupMemberByName(member_name);
			if (idx >= 0) {
				// Remove member by shifting
				for (int i = idx; i < m_group_member_count - 1; i++) {
					m_group_members[i] = m_group_members[i + 1];
				}
				m_group_member_count--;
				m_group_members[m_group_member_count] = GroupMember();
				AddChatSystemMessage(member_name + " has left the group");
			}
			break;
		}

		case EQT::GROUP_ACT_DISBAND:
			ClearGroup();
			AddChatSystemMessage("Your group has been disbanded");
			break;

		case EQT::GROUP_ACT_MAKE_LEADER:
		{
			std::string new_leader(data->leadersname);
			m_group_leader_name = new_leader;
			m_is_group_leader = (new_leader == m_character);

			// Update leader flags
			for (int i = 0; i < m_group_member_count; i++) {
				m_group_members[i].is_leader = (m_group_members[i].name == new_leader);
			}

			AddChatSystemMessage(new_leader + " is now the group leader");
			break;
		}

		default:
			LOG_DEBUG(MOD_MAIN, "Unhandled group action: {}", data->action);
			break;
	}
}

void EverQuest::ZoneProcessGroupDisband(const EQ::Net::Packet& p)
{
	ClearGroup();
	AddChatSystemMessage("Your group has been disbanded");
	LOG_INFO(MOD_MAIN, "Group disbanded");

#ifdef EQT_HAS_GRAPHICS
	// Hide pending invite UI if shown
	if (m_renderer) {
		auto* windowMgr = m_renderer->getWindowManager();
		if (windowMgr && windowMgr->getGroupWindow()) {
			windowMgr->getGroupWindow()->hidePendingInvite();
		}
	}
#endif
}

void EverQuest::ZoneProcessGroupCancelInvite(const EQ::Net::Packet& p)
{
	m_has_pending_invite = false;
	m_pending_inviter_name.clear();
	AddChatSystemMessage("Group invite cancelled");
	LOG_INFO(MOD_MAIN, "Group invite cancelled");

#ifdef EQT_HAS_GRAPHICS
	// Hide pending invite in group window
	if (m_renderer) {
		auto* windowMgr = m_renderer->getWindowManager();
		if (windowMgr && windowMgr->getGroupWindow()) {
			windowMgr->getGroupWindow()->hidePendingInvite();
		}
	}
#endif
}

void EverQuest::SendJump()
{
	if (!IsFullyZonedIn()) {
		return;
	}
	
	EQ::Net::DynamicPacket p;
	p.Resize(2);  // Jump packet size: 2 bytes opcode only (no data)
	
	p.PutUInt16(0, HC_OP_Jump);  // opcode
	
	m_zone_connection->QueuePacket(p, 0, false);
	
	if (s_debug_level >= 1) {
		LOG_DEBUG(MOD_MAIN, "Sent OP_Jump packet (2 bytes)");
	}
}

void EverQuest::SendMovementHistory()
{
	if (!IsFullyZonedIn() || m_movement_history.empty() || !m_zone_connection) {
		return;
	}

	// Additional safety check
	if (!m_zone_connected || !m_zone_connection_manager) {
		LOG_WARN(MOD_ZONE, "SendMovementHistory called with invalid connection state");
		return;
	}

	// Add debugging info about connection state
	LOG_TRACE(MOD_ZONE, "SendMovementHistory called at time {} - connection ptr: {}, connected: {}, history size: {}",
		static_cast<uint32_t>(std::time(nullptr)),
		static_cast<void*>(m_zone_connection.get()),
		m_zone_connected,
		m_movement_history.size());

	// Limit history to last 70 entries (similar to what client sends)
	while (m_movement_history.size() > 70) {
		m_movement_history.pop_front();
	}

	// For unreliable packets, we have a max of ~250 bytes (255 - CRC)
	// Each entry is 17 bytes, plus 2 bytes for opcode
	// So we can fit about 14 entries per unreliable packet: (250 - 2) / 17 = 14.5
	const size_t MAX_ENTRIES_PER_PACKET = 14;
	(void)MAX_ENTRIES_PER_PACKET; // Unused but kept for documentation

	// Real clients send all entries in one packet, so we must send as reliable
	// to match their behavior (they send 67-70 entries = 1139-1190 bytes + 2 = 1141-1192 total)
	size_t packet_size = 2 + (m_movement_history.size() * sizeof(MovementHistoryEntry));

	LOG_TRACE(MOD_ZONE, "Creating OP_FloatListThing packet: {} entries, {} bytes total",
		m_movement_history.size(), packet_size);

	EQ::Net::DynamicPacket p;
	p.Resize(packet_size);

	p.PutUInt16(0, HC_OP_FloatListThing);

	// Pack movement entries
	size_t offset = 2;
	try {
		for (const auto& entry : m_movement_history) {
			// UpdateMovementEntry struct layout:
			// float Y, float X, float Z, uint8 type, uint32 timestamp
			p.PutFloat(offset, entry.y);      // Y first
			p.PutFloat(offset + 4, entry.x);  // then X
			p.PutFloat(offset + 8, entry.z);  // then Z
			p.PutUInt8(offset + 12, entry.type);
			p.PutUInt32(offset + 13, entry.timestamp);
			offset += 17;
		}

		if (s_debug_level >= 2) {
			LOG_DEBUG(MOD_ZONE, "About to queue OP_FloatListThing as reliable packet");
		}

		// Send as reliable since it exceeds unreliable packet size limit
		if (!SafeQueueZonePacket(p, 0, true)) {  // Send reliable
			LOG_ERROR(MOD_ZONE, "Failed to queue OP_FloatListThing packet - SafeQueueZonePacket returned false");
		} else {
			LOG_TRACE(MOD_ZONE, "Successfully queued OP_FloatListThing with {} movement entries ({} bytes) as reliable",
				m_movement_history.size(), packet_size);
		}
	}
	catch (const std::exception& e) {
		LOG_ERROR(MOD_ZONE, "Exception packing movement history: {}", e.what());
	}
	catch (...) {
		LOG_ERROR(MOD_ZONE, "Unknown exception packing movement history");
	}
}

bool EverQuest::SafeQueueZonePacket(EQ::Net::Packet &p, int stream, bool reliable)
{
	if (!m_zone_connection) {
		LOG_WARN(MOD_ZONE, "Attempted to send packet with null zone connection");
		return false;
	}

	// Add debugging for large reliable packets
	if (reliable && p.Length() > 400 && s_debug_level >= 2) {
		uint16_t opcode = p.GetUInt16(0);
		LOG_TRACE(MOD_ZONE, "SafeQueueZonePacket: Large reliable packet - opcode={:#06x} ({}), length={}, stream={}",
			opcode, GetOpcodeName(opcode), p.Length(), stream);

		// Check connection state before sending large packets
		try {
			auto stats = m_zone_connection->GetStats();
			LOG_DEBUG(MOD_ZONE, "Connection stats before large packet: sent={}, recv={}, resent={}", stats.sent_packets, stats.recv_packets, stats.resent_packets);
		} catch (...) {
			LOG_WARN(MOD_ZONE, "Failed to get connection stats");
		}
	}

	// Check if connection is still valid
	try {
		// Get connection stats to verify the connection is still valid
		auto stats = m_zone_connection->GetStats();

		// Additional check - if stats look corrupted, don't send
		if (stats.recv_packets == 0 && stats.sent_packets == 0 && m_zone_connected) {
			LOG_WARN(MOD_ZONE, "Zone connection appears invalid (zero stats)");
			return false;
		}

		m_zone_connection->QueuePacket(p, stream, reliable);

		// Log successful queueing of large reliable packets
		if (reliable && p.Length() > 400 && s_debug_level >= 2) {
			LOG_DEBUG(MOD_ZONE, "Large reliable packet queued successfully");
		}

		return true;
	}
	catch (const std::exception& e) {
		LOG_ERROR(MOD_ZONE, "Exception in SafeQueueZonePacket: {} (opcode={:#06x}, len={}, reliable={})",
			e.what(), p.GetUInt16(0), p.Length(), reliable);
		m_zone_connection.reset();  // Clear the corrupted connection
		m_zone_connected = false;
		return false;
	}
	catch (...) {
		LOG_ERROR(MOD_ZONE, "Unknown exception in SafeQueueZonePacket (opcode={:#06x}, len={}, reliable={})",
			p.GetUInt16(0), p.Length(), reliable);
		m_zone_connection.reset();  // Clear the corrupted connection
		m_zone_connected = false;
		return false;
	}
}

void EverQuest::Jump()
{
	if (m_is_jumping) {
		if (s_debug_level >= 1) {
			LOG_DEBUG(MOD_MAIN, "Jump blocked - already jumping");
		}
		return;  // Can't jump while already jumping
	}
	
	// Allow jumping while moving or standing
	m_is_jumping = true;
	m_jump_start_z = m_z;
	m_jump_start_time = std::chrono::steady_clock::now();
	
	// Real clients have a tiny position shift when jumping (to trigger server broadcasts)
	// Add a small random X/Y movement like real clients do
	static int jump_count = 0;
	jump_count++;
	float tiny_shift = (jump_count % 2 == 0) ? -0.04f : 0.04f;  // Alternate direction
	
	// Apply tiny shift to X or Y position (alternate between them)
	if (jump_count % 4 < 2) {
		m_x += tiny_shift;
	} else {
		m_y += tiny_shift;
	}
	
	// Real clients send standing animation before jumping
	SendSpawnAppearance(AT_ANIMATION, ANIM_STANDING);   // 100
	
	// Send OP_Jump packet like real clients do
	SendJump();
	
	// Don't set animation to jump - real clients keep animation at 0 in ClientUpdate
	
	if (s_debug_level >= 1) {
		LOG_DEBUG(MOD_MAIN, "Jump initiated from z={}", m_z);
	}
	
	// Don't block - let the update loop handle the jump
	// The jump will be processed by UpdateMovement() calls
}

void EverQuest::StartUpdateLoop()
{
	if (m_update_running) {
		return;  // Already running
	}
	
	m_update_running = true;
	m_update_thread = std::thread([this]() {
		if (s_debug_level >= 2) {
			LOG_DEBUG(MOD_MAIN, "Update loop started");
		}

		while (m_update_running && IsFullyZonedIn()) {

			// Note: Network events are processed by the main thread's EventLoop
			// We only handle game logic updates here

			// Update all game systems
			UpdateMovement();

			// Update combat system
			if (m_combat_manager) {
				m_combat_manager->Update();
			}

			// Sleep to maintain ~60 FPS
			// std::this_thread::sleep_for(std::chrono::milliseconds(16));
			std::this_thread::sleep_for(std::chrono::milliseconds(500));
		}

		if (s_debug_level >= 2) {
			LOG_DEBUG(MOD_MAIN, "Update loop stopped");
		}
	});
}

void EverQuest::StopUpdateLoop()
{
	if (!m_update_running) {
		return;
	}
	
	m_update_running = false;
	if (m_update_thread.joinable()) {
		m_update_thread.join();
	}
}

void EverQuest::UpdateJump()
{
	if (!m_is_jumping) {
		return;
	}
	
	auto now = std::chrono::steady_clock::now();
	auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - m_jump_start_time).count();
	
	// Jump physics: parabolic arc
	// Jump lasts about 1 second, peaks at 0.5 seconds
	const float jump_duration_ms = 1000.0f;
	const float jump_height = 25.0f;  // Maximum height of jump (increased for visibility)
	
	if (elapsed >= jump_duration_ms) {
		// Jump complete
		m_is_jumping = false;
		m_z = m_jump_start_z;  // Return to original height (or use FixZ)
		
		// Return to appropriate animation
		if (m_is_moving) {
			m_animation = (m_move_speed >= WALK_SPEED_THRESHOLD) ? ANIM_RUN : ANIM_WALK;
		} else {
			m_animation = ANIM_STAND;
		}
		
		if (s_debug_level >= 1) {
			LOG_DEBUG(MOD_MAIN, "Jump completed");
		}
	} else {
		// Calculate jump height using parabola
		float t = elapsed / jump_duration_ms;  // 0 to 1
		float height = 4.0f * jump_height * t * (1.0f - t);  // Parabola peaking at t=0.5
		m_z = m_jump_start_z + height;
		
		if (s_debug_level >= 2) {
			static int jump_frame = 0;
			if (jump_frame % 10 == 0) {  // Log every 10th frame
				LOG_DEBUG(MOD_MAIN, "Jump progress: t={:.2f}, height={:.2f}, z={:.2f}", 
					t, height, m_z);
			}
			jump_frame++;
		}
	}
}

void EverQuest::PerformEmote(uint32_t animation)
{
	if (!IsFullyZonedIn()) {
		return;
	}
	
	// Send the emote animation using the proper Animation packet
	SendAnimation(static_cast<uint8_t>(animation));
	
	// Some emotes should stop movement
	if (animation == ANIM_KNEEL || animation == ANIM_DEATH) {
		if (m_is_moving) {
			StopMovement();
		}
	}
	
	if (s_debug_level >= 1) {
		LOG_DEBUG(MOD_MAIN, "Performing emote animation {}", animation);
	}
}

void EverQuest::SetAFK(bool afk)
{
	if (m_is_afk == afk) {
		return;
	}
	
	m_is_afk = afk;
	SendSpawnAppearance(AT_AFK, afk ? 1 : 0);
	
	if (s_debug_level >= 1) {
		LOG_DEBUG(MOD_MAIN, "AFK status: {}", afk ? "ON" : "OFF");
	}
}

void EverQuest::SetAnonymous(bool anon)
{
	if (m_is_anonymous == anon) {
		return;
	}
	
	m_is_anonymous = anon;
	SendSpawnAppearance(AT_ANONYMOUS, anon ? 1 : 0);
	
	if (s_debug_level >= 1) {
		LOG_DEBUG(MOD_MAIN, "Anonymous status: {}", anon ? "ON" : "OFF");
	}
}

void EverQuest::SetRoleplay(bool rp)
{
	if (m_is_roleplay == rp) {
		return;
	}

	m_is_roleplay = rp;
	SendSpawnAppearance(AT_ANONYMOUS, rp ? 2 : 0);  // 2 = roleplay

	if (s_debug_level >= 1) {
		LOG_DEBUG(MOD_MAIN, "Roleplay status: {}", rp ? "ON" : "OFF");
	}
}

void EverQuest::StartCampTimer()
{
	if (m_is_camping) {
		return;  // Already camping
	}

	m_is_camping = true;
	m_camp_start_time = std::chrono::steady_clock::now();

	if (s_debug_level >= 1) {
		LOG_DEBUG(MOD_MAIN, "Camp timer started, will log out in {} seconds", CAMP_TIMER_SECONDS);
	}
}

void EverQuest::CancelCamp()
{
	if (!m_is_camping) {
		return;
	}

	m_is_camping = false;
	AddChatSystemMessage("You are no longer camping.");

	if (s_debug_level >= 1) {
		LOG_DEBUG(MOD_MAIN, "Camp timer cancelled");
	}
}

void EverQuest::UpdateCampTimer()
{
	if (!m_is_camping) {
		return;
	}

	auto now = std::chrono::steady_clock::now();
	auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - m_camp_start_time).count();

	if (elapsed >= CAMP_TIMER_SECONDS) {
		// Camp timer complete, logout
		m_is_camping = false;
		AddChatSystemMessage("You have camped.");

#ifdef EQT_HAS_GRAPHICS
		if (m_renderer) {
			m_renderer->requestQuit();
		}
#endif

		if (s_debug_level >= 1) {
			LOG_DEBUG(MOD_MAIN, "Camp timer complete, logging out");
		}
	}
}

void EverQuest::SetSneak(bool sneak)
{
	if (m_is_sneaking == sneak) {
		return;
	}
	
	m_is_sneaking = sneak;
	SendSpawnAppearance(AT_SNEAK, sneak ? 1 : 0);
	
	if (s_debug_level >= 1) {
		LOG_DEBUG(MOD_MAIN, "Sneak status: {}", sneak ? "ON" : "OFF");
	}
}

float EverQuest::GetMovementSpeed() const
{
	// Return current movement speed, which may be modified by buffs, encumbrance, etc.
	return m_move_speed;
}

// Combat-related packet handlers
void EverQuest::ZoneProcessConsider(const EQ::Net::Packet &p)
{
	// Consider response from server
	if (p.Length() < 30) { // 2 opcode + 28 Consider_Struct
		return;
	}
	
	struct Consider_Struct {
		uint32_t playerid;
		uint32_t targetid;
		uint32_t faction;
		uint32_t level;
		int32_t  cur_hp;
		int32_t  max_hp;
		uint8_t  pvpcon;
		uint8_t  unknown3[3];
	} *con = (Consider_Struct*)((uint8_t*)p.Data() + 2);
	
	if (s_debug_level >= 1) {
		LOG_DEBUG(MOD_MAIN, "Consider: target={}, faction={}, level={}, hp={}/{}", 
			con->targetid, con->faction, con->level, con->cur_hp, con->max_hp);
	}
	
	// Update combat manager with consider info
	if (m_combat_manager) {
		m_combat_manager->ProcessConsiderResponse(con->targetid, con->faction, con->level, 
			con->cur_hp, con->max_hp);
	}
}

void EverQuest::ZoneProcessAction(const EQ::Net::Packet &p)
{
	// Combat action (attack, spell cast, etc)
	if (p.Length() < 33) { // 2 opcode + 31 Action_Struct
		return;
	}

	// Local struct matching wire format (31 bytes)
	struct ActionPacket_Struct {
		uint16_t target;
		uint16_t source;
		uint16_t level;
		uint32_t instrument_mod;
		float    force;
		float    hit_heading;
		float    hit_pitch;
		uint8_t  type;
		uint16_t unknown23;
		uint16_t unknown25;
		uint16_t spell;
		uint8_t  spell_level;
		uint8_t  effect_flag;
	} __attribute__((packed));

	const ActionPacket_Struct* action = (const ActionPacket_Struct*)((uint8_t*)p.Data() + 2);

	if (s_debug_level >= 1) {
		LOG_DEBUG(MOD_MAIN, "Action: source={} -> target={}, type={}, spell={}", action->source, action->target, action->type, action->spell);
	}

	// Convert to EQT::Action_Struct for spell manager
	EQT::Action_Struct spellAction;
	spellAction.target = action->target;
	spellAction.source = action->source;
	spellAction.level = action->level;
	spellAction.instrument_mod = action->instrument_mod;
	spellAction.force = action->force;
	spellAction.hit_heading = action->hit_heading;
	spellAction.hit_pitch = action->hit_pitch;
	spellAction.type = action->type;
	spellAction.spell = action->spell;
	spellAction.level2 = action->spell_level;
	spellAction.effect_flag = action->effect_flag;

	// Pass to spell manager for spell effect handling
	if (m_spell_manager) {
		m_spell_manager->handleAction(spellAction);
	}

	// Note: Attack animations are NOT triggered here - they come from animation updates
	// when the entity initiates an attack. The Action packet is for spell/ability effects.
}

void EverQuest::ZoneProcessDamage(const EQ::Net::Packet &p)
{
	// Damage notification
	if (p.Length() < 25) { // 2 opcode + 23 CombatDamage_Struct
		return;
	}
	
	struct CombatDamage_Struct {
		uint16_t target;
		uint16_t source;
		uint8_t  type;
		uint16_t spellid;
		uint32_t damage;
		float    force;
		float    hit_heading;
		float    hit_pitch;
		uint8_t  unknown06;
	} __attribute__((packed)) *dmg = (CombatDamage_Struct*)((uint8_t*)p.Data() + 2);
	
	// Debug: show raw bytes for damage field
	// Copy packed struct fields to local variables for fmt::format
	uint16_t target_id = dmg->target;
	uint16_t source_id = dmg->source;
	uint8_t damage_type = dmg->type;
	uint16_t spell_id = dmg->spellid;
	int32_t damage_amount = dmg->damage;

	// Cancel trade if player takes damage (entering combat)
	if (target_id == m_my_spawn_id && damage_amount > 0 &&
		m_trade_manager && m_trade_manager->isTrading()) {
		LOG_DEBUG(MOD_MAIN, "Player took damage during trade, canceling trade");
		m_trade_manager->cancelTrade();
		AddChatSystemMessage("Trade cancelled - you entered combat");
	}

	if (s_debug_level >= 3) {
		const uint8_t* data = static_cast<const uint8_t*>(p.Data()) + 2;
		std::string hexBytes;
		for (int i = 0; i < 12 && i < static_cast<int>(p.Length()) - 2; ++i) {
			hexBytes += fmt::format("{:02x} ", data[i]);
		}
		LOG_TRACE(MOD_COMBAT, "Damage packet raw bytes (first 12): {}", hexBytes);
		LOG_DEBUG(MOD_COMBAT, "Damage target={} ({:04x}), source={} ({:04x}), type={} ({:02x}), spell={} ({:04x})", target_id, target_id, source_id, source_id, damage_type, damage_type, spell_id, spell_id);
		LOG_TRACE(MOD_COMBAT, "Damage bytes at offset 7: {:02x} {:02x} {:02x} {:02x}", data[7], data[8], data[9], data[10]);
	}

	if (s_debug_level >= 2 || IsTrackedTarget(target_id) || IsTrackedTarget(source_id)) {
		// Get entity names if available
		std::string target_name = std::to_string(target_id);
		std::string source_name = std::to_string(source_id);
		auto target_it = m_entities.find(target_id);
		auto source_it = m_entities.find(source_id);
		if (target_it != m_entities.end()) target_name = target_it->second.name;
		if (source_it != m_entities.end()) source_name = source_it->second.name;

		LOG_DEBUG(MOD_COMBAT, "{} -> {} for {} damage (type {})",
			source_name, target_name, damage_amount, damage_type);
	}

#ifdef EQT_HAS_GRAPHICS
	// Trigger damage reaction animation on target (if damage > 0 and not a miss)
	// Note: Attack animations on source are triggered via ZoneProcessAction
	if (m_graphics_initialized && m_renderer && damage_amount > 0) {
		// Play damage reaction animation on target
		// Don't play on dead entities
		auto it = m_entities.find(target_id);
		if (it != m_entities.end() && it->second.hp_percent > 0) {
			// Calculate damage percentage to determine animation type
			float damagePercent = 0.0f;

			// For player, use actual max HP
			if (target_id == m_my_spawn_id && m_max_hp > 0) {
				damagePercent = (static_cast<float>(damage_amount) / static_cast<float>(m_max_hp)) * 100.0f;
			} else {
				// For NPCs, estimate based on level
				// Rough HP estimate: level * 10-20 for most NPCs
				// Use conservative estimate (level * 15) for damage percentage calc
				uint8_t level = it->second.level > 0 ? it->second.level : 1;
				float estimatedMaxHP = static_cast<float>(level) * 15.0f;
				damagePercent = (static_cast<float>(damage_amount) / estimatedMaxHP) * 100.0f;
			}

			// Determine damage animation based on damage type and percentage
			// EQ damage types:
			// - 0-79: Melee damage (type is skill ID)
			// - 231: Spell damage (lifetap)
			// - 252: DoT damage
			// - 253: Environmental damage (lava, drowning)
			// - 254: Trap damage
			// - 255: Fall damage
			const char* damageAnim = nullptr;
			bool isDrowning = (damage_type == 253);  // Environmental
			bool isTrap = (damage_type == 254);      // Trap damage

			damageAnim = EQ::getDamageAnimation(damagePercent, isDrowning, isTrap);

			m_renderer->setEntityAnimation(target_id, damageAnim, false, true);
			if (s_debug_level >= 2 || IsTrackedTarget(target_id)) {
				LOG_DEBUG(MOD_COMBAT, "Damage reaction '{}' on target={} (dmg={}, pct={:.1f}%, type={})",
					damageAnim, target_id, damage_amount, damagePercent, damage_type);
			}
		}

		// Trigger first-person attack animation when player deals damage
		if (source_id == m_my_spawn_id && m_renderer->isFirstPersonMode()) {
			m_renderer->triggerFirstPersonAttack();
		}
	}
#endif

	// Check if target died
	if (damage_amount > 0 && target_id != 0) {
		auto it = m_entities.find(target_id);
		if (it != m_entities.end() && it->second.hp_percent == 0) {
			// Target died, might want to loot
			if (m_combat_manager && m_combat_manager->IsAutoAttackEnabled() &&
				target_id == m_combat_manager->GetTargetId()) {
				// Our target died
				if (s_debug_level >= 2 || IsTrackedTarget(target_id)) {
					LOG_DEBUG(MOD_COMBAT, "Target {} ({}) died", target_id, it->second.name);
				}
			}
		}
	}
}

void EverQuest::ZoneProcessMoneyOnCorpse(const EQ::Net::Packet &p)
{
	// Money on corpse packet
	if (p.Length() < 22) {
		return;
	}
	
	// Parse money amounts
	struct MoneyOnCorpse_Struct {
		uint8_t response;   // 0x01 = success
		uint8_t unknown1[3];
		uint32_t platinum;
		uint32_t gold;
		uint32_t silver;
		uint32_t copper;
	} __attribute__((packed)) *money = (MoneyOnCorpse_Struct*)((uint8_t*)p.Data() + 2);

	// Copy packed struct fields to local variables for fmt::format
	uint32_t plat = money->platinum;
	uint32_t gold = money->gold;
	uint32_t silver = money->silver;
	uint32_t copper = money->copper;

	if (s_debug_level >= 1) {
		LOG_DEBUG(MOD_MAIN, "MoneyOnCorpse: {} platinum, {} gold, {} silver, {} copper\n", plat, gold, silver, copper);
	}
	
	// For now, just log it - money is auto-looted when we loot any item
}

void EverQuest::ZoneProcessLootItem(const EQ::Net::Packet &p)
{
	// Parse the serialized item data
	// The packet contains pipe-delimited item data
	if (p.Length() < 10) {
		return;
	}
	
	// Skip the first 4 bytes (opcode is already removed, but there's a header)
	const char* data = (const char*)p.Data() + 2;
	int data_len = p.Length() - 2;
	
	// Skip the first 4 bytes of the data (unknown header)
	if (data_len > 4) {
		data += 4;
		data_len -= 4;
	}
	
	std::string item_data(data, data_len);
	
	if (s_debug_level >= 1) {
		if (IsDebugEnabled()) std::cout << fmt::format("[DEBUG] ProcessLootItem: Received item data, length={}\n", item_data.length());
		if (s_debug_level >= 2) {
			// Show first part of the data
			if (IsDebugEnabled()) std::cout << fmt::format("[DEBUG] Item data preview: {}\n", 
				item_data.substr(0, std::min(size_t(100), item_data.length())));
		}
	}
	
	// Parse the pipe-delimited fields to extract item slot and name
	std::vector<std::string> fields;
	std::stringstream ss(item_data);
	std::string field;
	while (std::getline(ss, field, '|')) {
		fields.push_back(field);
	}
	
	// The format appears to be: slot|unknown|slot_number|....|item_name|...
	if (fields.size() > 12) {
		try {
			uint32_t slot_num = std::stoi(fields[2]);  // Slot number in loot window
			std::string item_name = fields[11];         // Item name
			
			if (s_debug_level >= 1) {
				if (IsDebugEnabled()) std::cout << fmt::format("[DEBUG] Loot window item: slot {} = '{}'\n", slot_num, item_name);
			}
			
			// Add to combat manager's loot list
			if (m_combat_manager) {
				m_combat_manager->AddLootItem(slot_num);
			}
		} catch (...) {
			if (s_debug_level >= 1) {
				LOG_DEBUG(MOD_MAIN, "Failed to parse loot item data");
			}
		}
	}
	
	// If auto-loot is enabled and we have items, start looting after a delay
	if (m_combat_manager && m_combat_manager->IsAutoLootEnabled() && 
	    m_combat_manager->GetCombatState() == COMBAT_STATE_LOOTING) {
		// The combat manager will handle the actual looting
		m_combat_manager->CheckAutoLoot();
	}
}

void EverQuest::QueuePacket(uint16_t opcode, EQ::Net::DynamicPacket* packet)
{
	if (!m_zone_connection || !packet) {
		return;
	}
	
	EQ::Net::DynamicPacket p;
	p.Resize(packet->Length() + 2); // Add space for opcode
	p.PutUInt16(0, opcode);
	if (packet->Length() > 0) {
		memcpy(static_cast<uint8_t*>(p.Data()) + 2, packet->Data(), packet->Length());
	}
	
	m_zone_connection->QueuePacket(p);
}

void EverQuest::StartCombatMovement(uint16_t entity_id)
{
	// Find the entity
	auto it = m_entities.find(entity_id);
	if (it == m_entities.end()) {
		if (s_debug_level >= 1) {
			LOG_DEBUG(MOD_MAIN, "StartCombatMovement: Entity {} not found", entity_id);
		}
		return;
	}
	
	const auto& entity = it->second;
	m_combat_target = entity.name;
	// Use previously set combat stop distance, or default to 5.0f
	if (m_combat_stop_distance <= 0.0f) {
		m_combat_stop_distance = 5.0f;
	}
	m_in_combat_movement = true;
	
	if (s_debug_level >= 1) {
		LOG_DEBUG(MOD_MAIN, "Starting combat movement to {} (ID: {})", entity.name, entity_id);
	}
	
	// Start moving to the target
	MoveToEntityWithinRange(entity.name, m_combat_stop_distance);
}

// Keyboard control methods
void EverQuest::StartMoveForward()
{
	if (!IsFullyZonedIn()) return;
	m_move_forward = true;
	if (s_debug_level >= 2) {
		LOG_DEBUG(MOD_MAIN, "Starting forward movement");
	}
}

void EverQuest::StartMoveBackward()
{
	if (!IsFullyZonedIn()) return;
	m_move_backward = true;
	if (s_debug_level >= 2) {
		LOG_DEBUG(MOD_MAIN, "Starting backward movement");
	}
}

void EverQuest::StartTurnLeft()
{
	if (!IsFullyZonedIn()) return;
	m_turn_left = true;
	if (s_debug_level >= 2) {
		LOG_DEBUG(MOD_MAIN, "Starting left turn");
	}
}

void EverQuest::StartTurnRight()
{
	if (!IsFullyZonedIn()) return;
	m_turn_right = true;
	if (s_debug_level >= 2) {
		LOG_DEBUG(MOD_MAIN, "Starting right turn");
	}
}

void EverQuest::StopMoveForward()
{
	m_move_forward = false;
	if (!m_move_backward && !m_turn_left && !m_turn_right) {
		m_is_moving = false;
		m_animation = ANIM_STAND;
		SendPositionUpdate();  // Send immediate update when stopping
		if (s_debug_level >= 2) {
			LOG_DEBUG(MOD_MAIN, "Stopping all movement");
		}
	}
}

void EverQuest::StopMoveBackward()
{
	m_move_backward = false;
	if (!m_move_forward && !m_turn_left && !m_turn_right) {
		m_is_moving = false;
		m_animation = ANIM_STAND;
		SendPositionUpdate();  // Send immediate update when stopping
		if (s_debug_level >= 2) {
			LOG_DEBUG(MOD_MAIN, "Stopping all movement");
		}
	}
}

void EverQuest::StopTurnLeft()
{
	m_turn_left = false;
	if (!m_move_forward && !m_move_backward && !m_turn_right) {
		m_is_moving = false;
		m_animation = ANIM_STAND;
		SendPositionUpdate();  // Send immediate update when stopping
	}
}

void EverQuest::StopTurnRight()
{
	m_turn_right = false;
	if (!m_move_forward && !m_move_backward && !m_turn_left) {
		m_is_moving = false;
		m_animation = ANIM_STAND;
		SendPositionUpdate();  // Send immediate update when stopping
	}
}

void EverQuest::UpdateKeyboardMovement()
{
	if (!IsFullyZonedIn()) return;
	
	// Handle turning
	const float turn_speed = 90.0f; // degrees per second (4 seconds for full rotation)
	float delta_heading = 0.0f;
	
	if (m_turn_left && !m_turn_right) {
		delta_heading = -turn_speed * 0.5f; // 500ms tick rate
	} else if (m_turn_right && !m_turn_left) {
		delta_heading = turn_speed * 0.5f;
	}
	
	if (delta_heading != 0.0f) {
		m_heading += delta_heading;
		// Normalize heading to 0-360
		while (m_heading < 0.0f) m_heading += 360.0f;
		while (m_heading >= 360.0f) m_heading -= 360.0f;
	}
	
	// Handle forward/backward movement
	float move_distance = 0.0f;
	
	if (m_move_forward && !m_move_backward) {
		move_distance = GetMovementSpeed() * 0.5f; // 500ms tick rate
		m_is_moving = true;
		m_animation = (m_move_speed >= WALK_SPEED_THRESHOLD) ? ANIM_RUN : ANIM_WALK;
	} else if (m_move_backward && !m_move_forward) {
		move_distance = -DEFAULT_WALK_SPEED * 0.5f; // Backward uses walk speed, 500ms tick
		m_is_moving = true;
		m_animation = ANIM_WALK; // Always walk when moving backward
	} else if (!m_move_forward && !m_move_backward && !m_turn_left && !m_turn_right) {
		// Not moving
		if (m_is_moving) {
			m_is_moving = false;
			m_animation = ANIM_STAND;
		}
	}
	
	// Apply movement
	if (move_distance != 0.0f) {
		// Convert heading to radians for calculations
		float heading_rad = m_heading * M_PI / 180.0f;

		// Calculate new position
		// In EQ coordinates: +X = East, +Y = North
		// Heading 0 = North, 90 = East, 180 = South, 270 = West
		float new_x = m_x + move_distance * sin(heading_rad);
		float new_y = m_y + move_distance * cos(heading_rad);

		// Update position
		m_x = new_x;
		m_y = new_y;

		// Fix Z position if we have map data
		FixZ();
	}
}

#ifdef EQT_HAS_GRAPHICS
// Graphics renderer integration methods

bool EverQuest::InitGraphics(int width, int height) {
	if (m_graphics_initialized) {
		return true;
	}

	if (m_eq_client_path.empty()) {
		LOG_ERROR(MOD_GRAPHICS, "EQ client path not set. Call SetEQClientPath() first.");
		return false;
	}

	m_renderer = std::make_unique<EQT::Graphics::IrrlichtRenderer>();

	EQT::Graphics::RendererConfig config;
	config.width = width;
	config.height = height;
	config.softwareRenderer = true;  // Use software renderer (no GPU required)
	config.eqClientPath = m_eq_client_path;
	config.windowTitle = "WillEQ - " + m_character;
	config.fog = true;
	config.lighting = false;  // Fullbright mode
	config.showNameTags = true;

	if (!m_renderer->init(config)) {
		LOG_ERROR(MOD_GRAPHICS, "Failed to initialize renderer");
		m_renderer.reset();
		return false;
	}

	// Preload global character models for faster entity rendering
	auto* entityRenderer = m_renderer->getEntityRenderer();
	if (entityRenderer) {
		if (entityRenderer->loadGlobalCharacters()) {
			LOG_DEBUG(MOD_GRAPHICS, "Global character models loaded");
		} else {
			LOG_WARN(MOD_GRAPHICS, "Could not load global character models (will use placeholders)");
		}
	}

	// Set up HUD callback to display player stats (HP/Mana bars)
	// Zone, location, entities are displayed by the renderer HUD
	m_renderer->setHUDCallback([this]() -> std::wstring {
		std::wostringstream ss;
		ss << L"--- PLAYER ---\n";
		std::wstring char_w(m_character.begin(), m_character.end());
		ss << char_w << L" (Lvl " << (int)m_level << L")\n";

		// HP bar
		ss << L"HP: [";
		int barLen = 20;
		int hpPercent = (m_max_hp > 0) ? (m_cur_hp * 100 / m_max_hp) : 100;
		int filled = (hpPercent * barLen) / 100;
		for (int i = 0; i < barLen; ++i) {
			ss << (i < filled ? L"|" : L" ");
		}
		ss << L"] " << m_cur_hp << L"/" << m_max_hp << L" (" << hpPercent << L"%)\n";

		// Mana bar (only for casters)
		if (m_max_mana > 0) {
			ss << L"MP: [";
			int manaPercent = (m_max_mana > 0) ? (m_mana * 100 / m_max_mana) : 100;
			filled = (manaPercent * barLen) / 100;
			for (int i = 0; i < barLen; ++i) {
				ss << (i < filled ? L"|" : L" ");
			}
			ss << L"] " << m_mana << L"/" << m_max_mana << L" (" << manaPercent << L"%)\n";
		}

		return ss.str();
	});

	// Set up save entities callback (F10 key)
	m_renderer->setSaveEntitiesCallback([this]() {
		std::string filename = "entities_" + m_current_zone_name + ".json";
		SaveEntityDataToFile(filename);
	});

	// Set up movement callback for Player Mode server sync
	m_renderer->setMovementCallback([this](const EQT::Graphics::PlayerPositionUpdate& update) {
		OnGraphicsMovement(update);
	});

	// Set up target selection callback for mouse click targeting
	m_renderer->setTargetCallback([this](uint16_t spawnId) {
		// Check if we have a cursor item or cursor money and clicked on a player - initiate trade
		if (m_inventory_manager && m_trade_manager) {
			const auto* cursorItem = m_inventory_manager->getItem(eqt::inventory::CURSOR_SLOT);
			bool hasCursorMoney = m_inventory_manager->hasCursorMoney();
			if (cursorItem || hasCursorMoney) {
				// We have an item or money on cursor - check if target is a player or NPC
				auto it = m_entities.find(spawnId);
				if (it != m_entities.end() && (it->second.npc_type == 0 || it->second.npc_type == 1)) {
					// Target is a player (0) or NPC (1) - initiate trade request
					bool isNpc = (it->second.npc_type == 1);
					if (cursorItem) {
						LOG_INFO(MOD_MAIN, "Initiating trade with {} (cursor item: {}, isNpc={})",
						         it->second.name, cursorItem->name, isNpc);
					} else {
						LOG_INFO(MOD_MAIN, "Initiating trade with {} (cursor money, isNpc={})",
						         it->second.name, isNpc);
					}
					m_trade_manager->requestTrade(spawnId, it->second.name, isNpc);
					return;  // Don't target, we're initiating trade
				}
			}
		}

		if (m_combat_manager) {
			if (m_combat_manager->SetTarget(spawnId)) {
				// Set tracked target for debug logging
				SetTrackedTargetId(spawnId);

				// Update renderer with full target info
				auto it = m_entities.find(spawnId);
				if (it != m_entities.end()) {
					const Entity& e = it->second;
					EQT::Graphics::TargetInfo info;
					info.spawnId = e.spawn_id;
					info.name = e.name;
					info.level = e.level;
					info.hpPercent = e.hp_percent;
					info.raceId = e.race_id;
					info.gender = e.gender;
					info.classId = e.class_id;
					info.bodyType = e.bodytype;
					info.helm = e.helm;
					info.showHelm = e.showhelm;
					info.texture = e.equip_chest2;
					info.npcType = e.npc_type;
					info.x = e.x;
					info.y = e.y;
					info.z = e.z;
					info.heading = e.heading;
					for (int i = 0; i < 9; ++i) {
						info.equipment[i] = e.equipment[i];
						info.equipmentTint[i] = e.equipment_tint[i];
					}
					m_renderer->setCurrentTargetInfo(info);

					LOG_DEBUG(MOD_ENTITY, "=== TARGET SELECTED: {} ===", e.name);
					LOG_DEBUG(MOD_ENTITY, "  spawn_id={} race_id={} gender={} level={} class_id={}",
						spawnId, e.race_id, (int)e.gender, (int)e.level, (int)e.class_id);
					LOG_DEBUG(MOD_ENTITY, "  npc_type={} (0=player,1=npc,2=pc_corpse,3=npc_corpse) bodytype={}",
						(int)e.npc_type, (int)e.bodytype);
					LOG_DEBUG(MOD_ENTITY, "  face={} haircolor={} hairstyle={} beardcolor={} beard={}",
						(int)e.face, (int)e.haircolor, (int)e.hairstyle, (int)e.beardcolor, (int)e.beard);
					LOG_DEBUG(MOD_ENTITY, "  texture(equip_chest2)={} helm={} showhelm={} light={}",
						(int)e.equip_chest2, (int)e.helm, (int)e.showhelm, (int)e.light);
					LOG_DEBUG(MOD_ENTITY, "  equipment[0-8]: head={} chest={} arms={} wrist={} hands={} legs={} feet={} primary={} secondary={}",
						e.equipment[0], e.equipment[1], e.equipment[2], e.equipment[3],
						e.equipment[4], e.equipment[5], e.equipment[6], e.equipment[7], e.equipment[8]);
					LOG_DEBUG(MOD_ENTITY, "  equipment_tint[0-8]: {:08X} {:08X} {:08X} {:08X} {:08X} {:08X} {:08X} {:08X} {:08X}",
						e.equipment_tint[0], e.equipment_tint[1], e.equipment_tint[2], e.equipment_tint[3],
						e.equipment_tint[4], e.equipment_tint[5], e.equipment_tint[6], e.equipment_tint[7], e.equipment_tint[8]);
				}
			}
		}
	});

	// Set up clear target callback (ESC key clears target)
	m_renderer->setClearTargetCallback([this]() {
		if (m_combat_manager) {
			m_combat_manager->ClearTarget();
			SetTrackedTargetId(0);
			LOG_DEBUG(MOD_COMBAT, "Target cleared via ESC key");
		}
	});

	// Set up loot corpse callback (shift+click on corpse)
	m_renderer->setLootCorpseCallback([this](uint16_t corpseId) {
		RequestLootCorpse(corpseId);
	});

	// Set up auto attack toggle callback (` key in Player Mode)
	m_renderer->setAutoAttackCallback([this]() {
		if (m_combat_manager) {
			if (m_combat_manager->IsAutoAttackEnabled()) {
				m_combat_manager->DisableAutoAttack();
				LOG_DEBUG(MOD_COMBAT, "Auto attack OFF");
			} else {
				// Only enable if we have a target
				if (m_combat_manager->GetTargetId() != 0) {
					m_combat_manager->EnableAutoAttack();
					LOG_DEBUG(MOD_COMBAT, "Auto attack ON");
				} else {
					LOG_DEBUG(MOD_COMBAT, "No target - auto attack not enabled");
				}
			}
		}
	});

	// Set up auto attack status callback for HUD display
	m_renderer->setAutoAttackStatusCallback([this]() -> bool {
		return m_combat_manager && m_combat_manager->IsAutoAttackEnabled();
	});

	// Set up hail callback (H key in Player Mode)
	m_renderer->setHailCallback([this]() {
		std::string message = "Hail";
		// If we have a target, append the target name
		if (m_combat_manager && m_combat_manager->HasTarget()) {
			uint16_t target_id = m_combat_manager->GetTargetId();
			auto it = m_entities.find(target_id);
			if (it != m_entities.end()) {
				message += ", " + it->second.name;
			}
		}
		ZoneSendChannelMessage(message, CHAT_CHANNEL_SAY, "");
	});

	// Set up vendor toggle callback (V key in Player Mode)
	m_renderer->setVendorToggleCallback([this]() {
		// If vendor window is open, close it
		if (IsVendorWindowOpen()) {
			CloseVendorWindow();
			return;
		}

		// Check if we have a target
		if (!m_combat_manager || !m_combat_manager->HasTarget()) {
			LOG_DEBUG(MOD_INVENTORY, "Vendor toggle: No target selected");
			return;
		}

		uint16_t target_id = m_combat_manager->GetTargetId();
		auto it = m_entities.find(target_id);
		if (it == m_entities.end()) {
			LOG_DEBUG(MOD_INVENTORY, "Vendor toggle: Target {} not found in entities", target_id);
			return;
		}

		const Entity& target = it->second;

		// Check if target is an NPC (not player, not corpse)
		if (target.npc_type != 1) {
			LOG_DEBUG(MOD_INVENTORY, "Vendor toggle: Target {} is not an NPC (type={})", target.name, target.npc_type);
			return;
		}

		// Check if target is a merchant (class 41)
		// Class 41 is the MERCHANT class in EQ
		constexpr uint8_t CLASS_MERCHANT = 41;
		if (target.class_id != CLASS_MERCHANT) {
			LOG_DEBUG(MOD_INVENTORY, "Vendor toggle: Target {} is not a merchant (class={})", target.name, target.class_id);
			return;
		}

		// Try to open vendor window
		LOG_DEBUG(MOD_INVENTORY, "Vendor toggle: Opening vendor {} (id={})", target.name, target_id);
		RequestOpenVendor(target_id);
	});

	// Set up banker interact callback (Ctrl+click on NPC in Player Mode)
	m_renderer->setBankerInteractCallback([this](uint16_t npcId) {
		// If bank window is already open, ignore
		if (IsBankWindowOpen()) {
			LOG_DEBUG(MOD_INVENTORY, "Banker interact: Bank already open");
			return;
		}

		auto it = m_entities.find(npcId);
		if (it == m_entities.end()) {
			LOG_DEBUG(MOD_INVENTORY, "Banker interact: NPC {} not found in entities", npcId);
			return;
		}

		const Entity& target = it->second;

		// Check if target is an NPC (not player, not corpse)
		if (target.npc_type != 1) {
			LOG_DEBUG(MOD_INVENTORY, "Banker interact: Target {} is not an NPC (type={})", target.name, target.npc_type);
			return;
		}

		// Check distance to NPC
		float distSq = CalculateDistance2D(m_x, m_y, target.x, target.y);
		distSq = distSq * distSq;  // CalculateDistance2D returns distance, not squared
		if (distSq > NPC_INTERACTION_DISTANCE_SQUARED) {
			LOG_DEBUG(MOD_INVENTORY, "Banker interact: Target {} is too far away (dist={})", target.name, std::sqrt(distSq));
			AddChatSystemMessage("You are too far away to interact with this NPC.");
			return;
		}

		// Check if target is a banker (class 40 = GM_Banker in EQ)
		constexpr uint8_t CLASS_BANKER = 40;
		if (target.class_id != CLASS_BANKER) {
			LOG_DEBUG(MOD_INVENTORY, "Banker interact: Target {} is not a banker (class={})", target.name, target.class_id);
			AddChatSystemMessage("This NPC is not a banker.");
			return;
		}

		// Open the bank window
		LOG_INFO(MOD_INVENTORY, "Opening bank window for {} (id={})", target.name, npcId);
		OpenBankWindow(npcId);
	});

	// Set up trainer toggle callback (T key in Player Mode)
	m_renderer->setTrainerToggleCallback([this]() {
		// If trainer window is open, close it
		if (IsTrainerWindowOpen()) {
			CloseTrainerWindow();
			return;
		}

		// Check if we have a target
		if (!m_combat_manager || !m_combat_manager->HasTarget()) {
			LOG_DEBUG(MOD_MAIN, "Trainer toggle: No target selected");
			return;
		}

		uint16_t target_id = m_combat_manager->GetTargetId();
		auto it = m_entities.find(target_id);
		if (it == m_entities.end()) {
			LOG_DEBUG(MOD_MAIN, "Trainer toggle: Target {} not found in entities", target_id);
			return;
		}

		const Entity& target = it->second;

		// Check if target is an NPC (not player, not corpse)
		if (target.npc_type != 1) {
			LOG_DEBUG(MOD_MAIN, "Trainer toggle: Target {} is not an NPC (type={})", target.name, target.npc_type);
			return;
		}

		// Check if target is a guildmaster trainer (class 20-35)
		// Reference: EQEmu classes.h - WarriorGM=20 through BerserkerGM=35
		constexpr uint8_t CLASS_WARRIOR_GM = 20;
		constexpr uint8_t CLASS_BERSERKER_GM = 35;
		if (target.class_id < CLASS_WARRIOR_GM || target.class_id > CLASS_BERSERKER_GM) {
			LOG_DEBUG(MOD_MAIN, "Trainer toggle: Target {} is not a trainer (class={})", target.name, target.class_id);
			AddChatSystemMessage("That is not a trainer.");
			return;
		}

		// Request trainer window from server
		LOG_DEBUG(MOD_MAIN, "Trainer toggle: Requesting trainer {} (id={}, class={})", target.name, target_id, target.class_id);
		RequestTrainerWindow(target_id);
	});

	// Set up door interaction callback (left-click on door or U key in Player Mode)
	m_renderer->setDoorInteractCallback([this](uint8_t doorId) {
		SendClickDoor(doorId);
	});

	// Set up world object (tradeskill container) interaction callback (left-click on object or O key)
	m_renderer->setWorldObjectInteractCallback([this](uint32_t dropId) {
		// Find the world object to check if it's a tradeskill container
		auto it = m_world_objects.find(dropId);
		if (it != m_world_objects.end() && it->second.isTradeskillContainer()) {
			LOG_INFO(MOD_INVENTORY, "Clicking tradeskill container: dropId={} name='{}' type={}",
				dropId, it->second.name, it->second.object_type);
			SendClickObject(dropId);
		} else if (it != m_world_objects.end()) {
			LOG_DEBUG(MOD_ENTITY, "World object {} is not a tradeskill container (type={})",
				dropId, it->second.object_type);
		}
	});

	// Set up spell gem cast callback (1-8 keys in Player Mode)
	m_renderer->setSpellGemCastCallback([this](uint8_t gemSlot) {
		if (m_spell_manager) {
			// Get current target for spell casting
			uint16_t targetId = m_combat_manager ? m_combat_manager->GetTargetId() : 0;
			EQ::CastResult result = m_spell_manager->beginCastFromGem(gemSlot, targetId);
			if (result != EQ::CastResult::Success) {
				// Log or display error message
				LOG_DEBUG(MOD_SPELL, "Spell gem {} cast failed: {}", gemSlot + 1, static_cast<int>(result));
			}
		}
	});

	// Set collision map for Player Mode (will be updated when zone loads)
	if (m_zone_map) {
		m_renderer->setCollisionMap(m_zone_map.get());
	}

	// Initialize inventory manager if not already done (normally created in ConnectToZone)
	if (!m_inventory_manager) {
		m_inventory_manager = std::make_unique<eqt::inventory::InventoryManager>();
		SetupInventoryCallbacks();
		LOG_DEBUG(MOD_INVENTORY, "Inventory manager initialized for graphics");
	}

	// Connect inventory manager to renderer
	if (m_renderer && m_inventory_manager) {
		m_renderer->setInventoryManager(m_inventory_manager.get());
		LOG_TRACE(MOD_GRAPHICS, "Inventory manager connected to renderer");
	}

	// Set up loot window callbacks
	SetupLootCallbacks();

	// Set up vendor window callbacks
	SetupVendorCallbacks();

	// Set up bank window callbacks
	SetupBankCallbacks();

	// Set up trainer window callbacks
	SetupTrainerCallbacks();

	// Set up trade window callbacks
	SetupTradeWindowCallbacks();

	// Set up tradeskill container callbacks
	SetupTradeskillCallbacks();

	// Initialize spell database if not already done
	if (m_spell_manager && !m_spell_manager->isInitialized()) {
		if (!m_spell_manager->initialize(m_eq_client_path)) {
			LOG_WARN(MOD_SPELL, "Could not load spell database - spell system will be limited");
		}
	}

	// Initialize buff manager with spell database
	if (m_spell_manager && m_spell_manager->isInitialized() && !m_buff_manager) {
		m_buff_manager = std::make_unique<EQ::BuffManager>(&m_spell_manager->getDatabase());
		LOG_DEBUG(MOD_SPELL, "Buff manager initialized");
	}

	// Initialize spell effects processor
	if (m_spell_manager && m_spell_manager->isInitialized() && m_buff_manager && !m_spell_effects) {
		m_spell_effects = std::make_unique<EQ::SpellEffects>(
			this, &m_spell_manager->getDatabase(), m_buff_manager.get());
		LOG_DEBUG(MOD_SPELL, "Spell effects processor initialized");
	}

	// Initialize spell type processor (handles targeting and multi-target spells)
	if (m_spell_manager && m_spell_manager->isInitialized() && m_spell_effects && !m_spell_type_processor) {
		m_spell_type_processor = std::make_unique<EQ::SpellTypeProcessor>(
			this, &m_spell_manager->getDatabase(), m_spell_effects.get());
		LOG_DEBUG(MOD_SPELL, "Spell type processor initialized");
	}

	// Set up spell gem panel
	if (m_spell_manager && m_renderer && m_renderer->getWindowManager()) {
		auto* windowManager = m_renderer->getWindowManager();
		windowManager->initSpellGemPanel(m_spell_manager.get());

		// Set up gem cast callback
		windowManager->setGemCastCallback([this](uint8_t gemSlot) {
			if (!m_spell_manager) return;
			uint16_t targetId = m_combat_manager ? m_combat_manager->GetTargetId() : 0;
			EQ::CastResult result = m_spell_manager->beginCastFromGem(gemSlot, targetId);
			if (result == EQ::CastResult::Success) {
				uint32_t spellId = m_spell_manager->getMemorizedSpell(gemSlot);
				const EQ::SpellData* spell = m_spell_manager->getSpell(spellId);
				if (spell) {
					AddChatSystemMessage(fmt::format("Casting {}", spell->name));
				}
			} else if (result == EQ::CastResult::NotMemorized) {
				AddChatSystemMessage(fmt::format("No spell in gem {}", gemSlot + 1));
			} else if (result == EQ::CastResult::NotEnoughMana) {
				AddChatSystemMessage("Insufficient mana");
			} else if (result == EQ::CastResult::GemCooldown) {
				AddChatSystemMessage("Spell not ready");
			} else if (result == EQ::CastResult::AlreadyCasting) {
				AddChatSystemMessage("Already casting");
			}
		});

		// Set up gem forget callback (right-click)
		windowManager->setGemForgetCallback([this](uint8_t gemSlot) {
			if (!m_spell_manager) return;
			if (m_spell_manager->forgetSpell(gemSlot)) {
				AddChatSystemMessage(fmt::format("Forgot spell in gem {}", gemSlot + 1));
			}
		});

		// Set up spellbook open/close callback to send appearance animation
		windowManager->setSpellbookStateCallback([this](bool isOpen) {
			// Send spawn appearance to server: animation 110 = sitting/spellbook, 100 = standing
			SendSpawnAppearance(AT_ANIMATION, isOpen ? 110 : 100);
			LOG_DEBUG(MOD_SPELL, "Spellbook {} - sent appearance animation {}",
				isOpen ? "opened" : "closed", isOpen ? 110 : 100);
		});

		// Set up scribe spell request callback
		windowManager->setScribeSpellRequestCallback(
			[this](uint32_t spellId, uint16_t bookSlot, int16_t sourceSlot) {
				ScribeSpellFromScroll(spellId, bookSlot, sourceSlot);
			}
		);

		LOG_DEBUG(MOD_SPELL, "Spell gem panel initialized");
	}

	// Set up buff window
	if (m_buff_manager && m_renderer && m_renderer->getWindowManager()) {
		auto* windowManager = m_renderer->getWindowManager();
		windowManager->initBuffWindow(m_buff_manager.get());

		// Set up buff cancel callback (right-click to remove buff)
		windowManager->setBuffCancelCallback([this](uint8_t slot) {
			// In EQ, right-clicking a buff removes it (for player's own buffs)
			if (m_buff_manager) {
				m_buff_manager->removeBuffBySlot(0, slot);  // 0 = player
				AddChatSystemMessage(fmt::format("Buff in slot {} cancelled", slot + 1));
			}
		});

		LOG_DEBUG(MOD_SPELL, "Buff window initialized");
	}

	// Set up group window
	if (m_renderer && m_renderer->getWindowManager()) {
		auto* windowManager = m_renderer->getWindowManager();
		windowManager->initGroupWindow(this);
		windowManager->initPlayerStatusWindow(this);
		windowManager->initSkillsWindow(m_skill_manager.get());

		// Set up skills window callbacks
		windowManager->setSkillActivateCallback([this](uint8_t skill_id) {
			if (m_skill_manager) {
				m_skill_manager->activateSkill(skill_id);
			}
		});

		windowManager->setHotbarCreateCallback([this, windowManager](uint8_t skill_id) {
			// Put skill on cursor for placement in hotbar
			const char* skill_name = EQ::getSkillName(skill_id);
			windowManager->setHotbarCursor(
				eqt::ui::HotbarButtonType::Skill,
				static_cast<uint32_t>(skill_id),
				skill_name ? skill_name : "Unknown Skill",
				0  // No icon ID for skills - will use text label
			);
			AddChatSystemMessage(fmt::format("Drag {} to a hotbar slot", skill_name ? skill_name : "skill"));
		});

		// Set up skill activation feedback callback
		if (m_skill_manager) {
			m_skill_manager->setOnSkillActivated([this](uint8_t skill_id, bool success, const std::string& message) {
				const char* skill_name = EQ::getSkillName(skill_id);
				if (success) {
					AddChatSystemMessage(fmt::format("You use {}!", skill_name));
				} else {
					AddChatSystemMessage(fmt::format("Cannot use {}: {}", skill_name, message));
				}
			});

			// Set up skill-up notification callback
			m_skill_manager->setOnSkillUpdate([this](uint8_t skill_id, uint32_t old_value, uint32_t new_value) {
				if (new_value > old_value) {
					const char* skill_name = EQ::getSkillName(skill_id);
					AddChatSystemMessage(fmt::format("You have become better at {}! ({})", skill_name, new_value));
				}
			});
		}

		// Set up group window callbacks
		windowManager->setGroupInviteCallback([this]() {
			// Invite current target to group
			if (m_combat_manager && m_combat_manager->HasTarget()) {
				uint16_t target_id = m_combat_manager->GetTargetId();
				auto it = m_entities.find(target_id);
				if (it != m_entities.end()) {
					SendGroupInvite(it->second.name);
					AddChatSystemMessage(fmt::format("Inviting {} to group", it->second.name));
				}
			} else {
				AddChatSystemMessage("No target selected");
			}
		});

		windowManager->setGroupDisbandCallback([this]() {
			if (m_in_group) {
				if (m_is_group_leader) {
					SendGroupDisband();
				} else {
					SendLeaveGroup();
				}
			}
		});

		windowManager->setGroupAcceptCallback([this]() {
			AcceptGroupInvite();
		});

		windowManager->setGroupDeclineCallback([this]() {
			DeclineGroupInvite();
		});

		LOG_DEBUG(MOD_MAIN, "Group window initialized");
	}

	// Set up hotbar activate callback
	if (m_renderer && m_renderer->getWindowManager()) {
		auto* windowManager = m_renderer->getWindowManager();

		windowManager->setHotbarActivateCallback([this, windowManager](int index, const eqt::ui::HotbarButton& button) {
			switch (button.type) {
				case eqt::ui::HotbarButtonType::Spell: {
					// Cast spell by ID (find gem slot and cast)
					if (m_spell_manager) {
						// Find which gem slot has this spell
						for (uint8_t gem = 0; gem < EQ::MAX_SPELL_GEMS; gem++) {
							if (m_spell_manager->getMemorizedSpell(gem) == button.id) {
								uint16_t targetId = m_combat_manager ? m_combat_manager->GetTargetId() : 0;
								EQ::CastResult result = m_spell_manager->beginCastFromGem(gem, targetId);
								if (result == EQ::CastResult::Success) {
									const EQ::SpellData* spell = m_spell_manager->getSpell(button.id);
									if (spell) {
										AddChatSystemMessage(fmt::format("Casting {}", spell->name));
									}
								} else if (result == EQ::CastResult::NotEnoughMana) {
									AddChatSystemMessage("Insufficient mana");
								} else if (result == EQ::CastResult::GemCooldown) {
									AddChatSystemMessage("Spell not ready");
								} else if (result == EQ::CastResult::AlreadyCasting) {
									AddChatSystemMessage("Already casting");
								}
								return;
							}
						}
						AddChatSystemMessage("Spell not memorized");
					}
					break;
				}
				case eqt::ui::HotbarButtonType::Item: {
					// Use item by ID - find in inventory and cast click effect
					if (m_inventory_manager) {
						int16_t slot = m_inventory_manager->findItemSlotByItemId(button.id);
						if (slot == eqt::inventory::SLOT_INVALID) {
							AddChatSystemMessage("Item not found in inventory");
							break;
						}

						const eqt::inventory::ItemInstance* item = m_inventory_manager->getItem(slot);
						if (!item) {
							AddChatSystemMessage("Item not found");
							break;
						}

						// Check if item has a click effect
						if (item->clickEffect.effectId == 0) {
							AddChatSystemMessage(fmt::format("{} has no click effect", item->name));
							break;
						}

						// Check if item must be equipped for click effect (type 5 = must be equipped)
						if (item->clickEffect.type == 5 && slot >= eqt::inventory::GENERAL_BEGIN) {
							AddChatSystemMessage("You must equip this item to use its effect");
							break;
						}

						// Send CastSpell packet with item slot
						EQ::Net::DynamicPacket packet;
						packet.Resize(20);  // CastSpell_Struct size
						packet.PutUInt32(0, 10);  // slot = 10 for item clicks
						packet.PutUInt32(4, static_cast<uint32_t>(item->clickEffect.effectId));  // spell_id
						packet.PutUInt32(8, static_cast<uint32_t>(slot));  // inventoryslot
						uint16_t targetId = m_combat_manager ? m_combat_manager->GetTargetId() : 0;
						packet.PutUInt32(12, targetId);  // target_id
						packet.PutUInt32(16, 0);  // cs_unknown

						QueuePacket(HC_OP_CastSpell, &packet);
						AddChatSystemMessage(fmt::format("Using {}", item->name));

						// Start cooldown on the hotbar button
						if (item->clickEffect.recastDelay > 0) {
							windowManager->startHotbarCooldown(index, item->clickEffect.recastDelay);
						}
					}
					break;
				}
				case eqt::ui::HotbarButtonType::Emote: {
					// Send emote text
					if (!button.emoteText.empty()) {
						ProcessChatInput(button.emoteText);
					}
					break;
				}
				case eqt::ui::HotbarButtonType::Skill: {
					// Activate skill by ID
					if (m_skill_manager) {
						m_skill_manager->activateSkill(static_cast<uint8_t>(button.id));
					}
					break;
				}
				default:
					break;
			}
		});

		LOG_DEBUG(MOD_MAIN, "Hotbar window callbacks initialized");
	}

	// Set up chat submit callback
	m_renderer->setChatSubmitCallback([this](const std::string& text) {
		ProcessChatInput(text);
	});

	// Set up read item callback for book/note reading
	m_renderer->setReadItemCallback([this](const std::string& bookText, uint8_t bookType) {
		RequestReadBook(bookText, bookType);
	});

	// Set up auto-completion for chat window
	if (auto* chatWindow = m_renderer->getWindowManager()->getChatWindow()) {
		// Provide command registry for command completion
		chatWindow->setCommandRegistry(m_command_registry.get());

		// Provide entity names for player name completion
		chatWindow->setEntityNameProvider([this]() -> std::vector<std::string> {
			std::vector<std::string> names;
			for (const auto& [id, entity] : m_entities) {
				if (!entity.name.empty()) {
					names.push_back(entity.name);
				}
			}
			// Also add our own name
			if (!m_character.empty()) {
				names.push_back(m_character);
			}
			return names;
		});

		// Handle link clicks in chat messages
		chatWindow->setLinkClickCallback([this](const eqt::MessageLink& link) {
			if (link.type == eqt::LinkType::NPCName) {
				// Say the NPC name/keyword to trigger dialogue
				LOG_DEBUG(MOD_ENTITY, "Clicked NPC link: '{}' - sending say", link.displayText);
				ZoneSendChannelMessage(link.displayText, CHAT_CHANNEL_SAY, "");
			} else if (link.type == eqt::LinkType::Item) {
				// Look up item from cache and show tooltip
				LOG_DEBUG(MOD_ENTITY, "Clicked item link: '{}' (ID: {})", link.displayText, link.itemId);
				const eqt::inventory::ItemInstance* item = nullptr;
				if (m_inventory_manager) {
					item = m_inventory_manager->getItemById(link.itemId);
				}
				if (item && m_renderer && m_renderer->getWindowManager()) {
					// Show tooltip at current mouse position
					int mouseX = m_renderer->getMouseX();
					int mouseY = m_renderer->getMouseY();
					m_renderer->getWindowManager()->showItemTooltip(item, mouseX, mouseY);
				} else {
					// Item not in cache - show message with item name
					AddChatSystemMessage(fmt::format("Item: {} (ID: {} - not in cache)", link.displayText, link.itemId));
				}
			}
		});
	}

	m_graphics_initialized = true;
	LOG_INFO(MOD_GRAPHICS, "Renderer initialized successfully ({}x{})", width, height);

	// If zone is already fully connected when graphics init, set zone ready
	if (IsFullyZonedIn() && m_renderer) {
		m_renderer->setZoneReady(true);
		LOG_DEBUG(MOD_GRAPHICS, "Zone already ready, enabling rendering");
	}

	return true;
}

void EverQuest::ShutdownGraphics() {
	if (m_inventory_manager) {
		m_inventory_manager->clearAll();
		m_inventory_manager.reset();
	}
	if (m_renderer) {
		m_renderer->shutdown();
		m_renderer.reset();
	}
	m_graphics_initialized = false;
	LOG_DEBUG(MOD_GRAPHICS, "Renderer shut down");
}

bool EverQuest::UpdateGraphics(float deltaTime) {
	static int graphics_frame_counter = 0;
	graphics_frame_counter++;

	// Extra logging when zone is not connected (during zone transition)
	bool zone_transition_logging = !m_zone_connected;
	if (zone_transition_logging) {
		LOG_TRACE(MOD_GRAPHICS, "UpdateGraphics (zone transition) frame {}", graphics_frame_counter);
	}

	if (!m_graphics_initialized || !m_renderer) {
		if (graphics_frame_counter % 100 == 0 || zone_transition_logging) {
			LOG_TRACE(MOD_GRAPHICS, "UpdateGraphics: no renderer (initialized={})", m_graphics_initialized);
		}
		return true;  // No renderer, just return success
	}

	if (zone_transition_logging) {
		LOG_TRACE(MOD_GRAPHICS, "Renderer exists, checking mode...");
	}

	// Log periodically
	if (graphics_frame_counter % 500 == 0) {
		LOG_TRACE(MOD_GRAPHICS, "UpdateGraphics frame {} zone_connected={}", graphics_frame_counter, m_zone_connected);
	}

	try {
		// Update player position in renderer (only in Admin mode)
		// In Player/Repair mode, the renderer drives position via OnGraphicsMovement callback
		// Convert m_heading from degrees (0-360) to EQ format (0-512)
		if (m_renderer->getRendererMode() == EQT::Graphics::RendererMode::Admin) {
			float heading512 = m_heading * 512.0f / 360.0f;
			m_renderer->setPlayerPosition(m_x, m_y, m_z, heading512);
		}

		if (zone_transition_logging) {
			LOG_TRACE(MOD_GRAPHICS, "Position set, updating target...");
		}

		// Periodic target HP update (~1 second interval)
		m_target_update_timer += deltaTime;
		if (m_target_update_timer >= 1.0f) {
			m_target_update_timer = 0.0f;
			uint16_t targetId = m_renderer->getCurrentTargetId();
			if (targetId != 0) {
				auto it = m_entities.find(targetId);
				if (it != m_entities.end()) {
					// Update the target HP from current entity data
					m_renderer->updateCurrentTargetHP(it->second.hp_percent);
				}
			}
		}

		if (zone_transition_logging) {
			LOG_TRACE(MOD_GRAPHICS, "Target updated, updating time...");
		}

		// Update time of day lighting
		m_renderer->updateTimeOfDay(m_time_hour, m_time_minute);

		// Update spell manager (cooldowns, memorization progress, cast timeouts)
		if (m_spell_manager) {
			m_spell_manager->update(deltaTime);
		}

		// Update buff manager (buff durations, expirations)
		if (m_buff_manager) {
			m_buff_manager->update(deltaTime);
		}

		if (zone_transition_logging) {
			LOG_TRACE(MOD_GRAPHICS, "Time updated, calling processFrame...");
		}

		// Process a frame
		bool result = m_renderer->processFrame(deltaTime);

		if (zone_transition_logging) {
			LOG_TRACE(MOD_GRAPHICS, "processFrame returned {}", result);
		}

		if (!result) {
			LOG_DEBUG(MOD_GRAPHICS, "processFrame returned false - window may have been closed");
		}
		return result;
	}
	catch (const std::exception& e) {
		LOG_ERROR(MOD_GRAPHICS, "Exception in UpdateGraphics: {}", e.what());
		return false;
	}
	catch (...) {
		LOG_ERROR(MOD_GRAPHICS, "Unknown exception in UpdateGraphics");
		return false;
	}
}

void EverQuest::OnZoneLoadedGraphics() {
	if (!m_graphics_initialized || !m_renderer) {
		return;
	}

	// Prevent double loading - check if this zone is already loaded
	if (m_renderer->getCurrentZoneName() == m_current_zone_name && !m_current_zone_name.empty()) {
		LOG_DEBUG(MOD_GRAPHICS, "Zone {} already loaded, skipping", m_current_zone_name);
		return;
	}

	LOG_DEBUG(MOD_GRAPHICS, "Loading zone: {}", m_current_zone_name);

	// Load the zone in the renderer
	// Progress range: 0.70 to 0.88 - fits between "Receiving zone data..." (0.68)
	// and "Finalizing zone entry..." (0.92)
	if (m_renderer->loadZone(m_current_zone_name, 0.70f, 0.88f)) {
		LOG_DEBUG(MOD_GRAPHICS, "Zone loaded successfully");

		// Update progress after zone geometry loaded (loadZone uses drawLoadingScreen directly,
		// so we need to sync the member variable to prevent progress from dropping back)
		m_renderer->setLoadingProgress(0.88f, L"Zone geometry loaded...");

		// Update collision map for Player Mode movement
		if (m_zone_map) {
			m_renderer->setCollisionMap(m_zone_map.get());
			LOG_TRACE(MOD_GRAPHICS, "Collision map set for Player Mode");
		}

		// Set camera mode based on renderer mode
		if (m_renderer->getRendererMode() == EQT::Graphics::RendererMode::Player) {
			// Player mode: use Follow camera (third-person behind player)
			m_renderer->setCameraMode(EQT::Graphics::IrrlichtRenderer::CameraMode::Follow);
		} else {
			// Admin mode: use Free camera for debugging
			m_renderer->setCameraMode(EQT::Graphics::IrrlichtRenderer::CameraMode::Free);
		}
		// Convert m_heading from degrees (0-360) to EQ format (0-512)
		float heading512 = m_heading * 512.0f / 360.0f;
		m_renderer->setPlayerPosition(m_x, m_y, m_z, heading512);
		// Note: Server Z is the middle of the character model, not ground level.
		// The renderer should adjust model positioning internally - don't sync Z back.

		// Create entities for all current spawns
		for (const auto& [spawn_id, entity] : m_entities) {
			// npc_type: 0=player, 1=npc, 2=pc_corpse, 3=npc_corpse
			bool isNPC = (entity.npc_type == 1 || entity.npc_type == 3);
			bool isCorpse = (entity.npc_type == 2 || entity.npc_type == 3);

			// Fallback: Also detect corpse by name (server adds "'s corpse" or "_corpse" suffix)
			if (!isCorpse && entity.name.find("corpse") != std::string::npos) {
				isCorpse = true;
				LOG_TRACE(MOD_ENTITY, "Entity {} ({}) detected as corpse by name (npc_type={})", spawn_id, entity.name, (int)entity.npc_type);
			}

			// Build appearance from entity data
			EQT::Graphics::EntityAppearance appearance;
			appearance.face = entity.face;
			appearance.haircolor = entity.haircolor;
			appearance.hairstyle = entity.hairstyle;
			appearance.beardcolor = entity.beardcolor;
			appearance.beard = entity.beard;
			appearance.texture = entity.equip_chest2;
			appearance.helm = entity.helm;
			for (int i = 0; i < 9; i++) {
				appearance.equipment[i] = entity.equipment[i];
				appearance.equipment_tint[i] = entity.equipment_tint[i];
			}

			m_renderer->createEntity(spawn_id, entity.race_id, entity.name,
			                         entity.x, entity.y, entity.z, entity.heading,
			                         spawn_id == m_my_spawn_id, entity.gender, appearance, isNPC, isCorpse);

			// If this is our player, also update the inventory model view
			if (spawn_id == m_my_spawn_id) {
				m_renderer->updatePlayerAppearance(entity.race_id, entity.gender, appearance);
			}
		}

		// Recreate doors from stored data (they were cleared during zone load)
		for (const auto& [door_id, door] : m_doors) {
			bool initiallyOpen = (door.state != 0) != door.invert_state;
			m_renderer->createDoor(door.door_id, door.name, door.x, door.y, door.z,
			                       door.heading, door.incline, door.size, door.opentype,
			                       initiallyOpen);
		}
		LOG_DEBUG(MOD_GRAPHICS, "Recreated {} doors after zone load", m_doors.size());

		// Set up hotbar changed callback to auto-save
		auto* windowMgr = m_renderer->getWindowManager();
		if (windowMgr) {
			windowMgr->setHotbarChangedCallback([this]() {
				SaveHotbarConfig();
			});
			LOG_DEBUG(MOD_UI, "Hotbar changed callback registered");
		}
	} else {
		LOG_ERROR(MOD_GRAPHICS, "Failed to load zone: {}", m_current_zone_name);
	}
}

void EverQuest::OnSpawnAddedGraphics(const Entity& entity) {
	if (!m_graphics_initialized || !m_renderer) {
		return;
	}

	// npc_type: 0=player, 1=npc, 2=pc_corpse, 3=npc_corpse
	bool isNPC = (entity.npc_type == 1 || entity.npc_type == 3);
	bool isCorpse = (entity.npc_type == 2 || entity.npc_type == 3);

	// Fallback: Also detect corpse by name (server adds "'s corpse" or "_corpse" suffix)
	if (!isCorpse && entity.name.find("corpse") != std::string::npos) {
		isCorpse = true;
		LOG_TRACE(MOD_ENTITY, "Entity {} ({}) detected as corpse by name (npc_type={})", entity.spawn_id, entity.name, (int)entity.npc_type);
	}

	// Build appearance from entity data
	EQT::Graphics::EntityAppearance appearance;
	appearance.face = entity.face;
	appearance.haircolor = entity.haircolor;
	appearance.hairstyle = entity.hairstyle;
	appearance.beardcolor = entity.beardcolor;
	appearance.beard = entity.beard;
	appearance.texture = entity.equip_chest2;
	appearance.helm = entity.helm;
	for (int i = 0; i < 9; i++) {
		appearance.equipment[i] = entity.equipment[i];
		appearance.equipment_tint[i] = entity.equipment_tint[i];
	}

	m_renderer->createEntity(entity.spawn_id, entity.race_id, entity.name,
	                         entity.x, entity.y, entity.z, entity.heading,
	                         entity.spawn_id == m_my_spawn_id, entity.gender, appearance, isNPC, isCorpse);

	// Set initial pose state from spawn animation value
	// The animation field in spawn data may indicate sitting/standing/etc.
	if (!isCorpse && entity.animation != 0) {
		EQT::Graphics::IrrlichtRenderer::EntityPoseState poseState =
			EQT::Graphics::IrrlichtRenderer::EntityPoseState::Standing;
		std::string animCode;
		bool setPose = false;

		if (entity.animation == ANIM_SITTING) {
			poseState = EQT::Graphics::IrrlichtRenderer::EntityPoseState::Sitting;
			animCode = "p02";  // Sitting idle
			setPose = true;
		} else if (entity.animation == ANIM_CROUCHING) {
			poseState = EQT::Graphics::IrrlichtRenderer::EntityPoseState::Crouching;
			animCode = "l08";  // Crouching
			setPose = true;
		} else if (entity.animation == ANIM_LYING) {
			poseState = EQT::Graphics::IrrlichtRenderer::EntityPoseState::Lying;
			animCode = "d05";  // Lying down
			setPose = true;
		}

		if (setPose) {
			m_renderer->setEntityPoseState(entity.spawn_id, poseState);
			m_renderer->setEntityAnimation(entity.spawn_id, animCode, true, false);
			LOG_DEBUG(MOD_ENTITY, "Set initial pose for {} (ID: {}) to {} (anim={})",
			          entity.name, entity.spawn_id, animCode, entity.animation);
		}
	}

	// If this is our player, also update the inventory model view
	if (entity.spawn_id == m_my_spawn_id) {
		m_renderer->updatePlayerAppearance(entity.race_id, entity.gender, appearance);
	}
}

void EverQuest::SaveEntityDataToFile(const std::string& filename) {
	std::ofstream file(filename);
	if (!file.is_open()) {
		std::cerr << "Failed to open " << filename << " for writing" << std::endl;
		return;
	}

	file << "{\n  \"zone\": \"" << m_current_zone_name << "\",\n  \"entities\": [\n";

	bool first = true;
	for (const auto& [spawn_id, entity] : m_entities) {
		if (!first) file << ",\n";
		first = false;

		file << "    {\n";
		file << "      \"spawn_id\": " << entity.spawn_id << ",\n";
		file << "      \"name\": \"" << entity.name << "\",\n";
		file << "      \"race_id\": " << entity.race_id << ",\n";
		file << "      \"gender\": " << (int)entity.gender << ",\n";
		file << "      \"face\": " << (int)entity.face << ",\n";
		file << "      \"haircolor\": " << (int)entity.haircolor << ",\n";
		file << "      \"hairstyle\": " << (int)entity.hairstyle << ",\n";
		file << "      \"beardcolor\": " << (int)entity.beardcolor << ",\n";
		file << "      \"beard\": " << (int)entity.beard << ",\n";
		file << "      \"texture\": " << (int)entity.equip_chest2 << ",\n";
		file << "      \"equipment\": [";
		for (int i = 0; i < 9; i++) {
			if (i > 0) file << ", ";
			file << entity.equipment[i];
		}
		file << "],\n";
		file << "      \"equipment_tint\": [";
		for (int i = 0; i < 9; i++) {
			if (i > 0) file << ", ";
			file << entity.equipment_tint[i];
		}
		file << "],\n";
		file << "      \"position\": [" << entity.x << ", " << entity.y << ", " << entity.z << "],\n";
		file << "      \"heading\": " << entity.heading << "\n";
		file << "    }";
	}

	file << "\n  ]\n}\n";
	file.close();

	LOG_INFO(MOD_ENTITY, "Saved {} entities to {}", m_entities.size(), filename);
}

void EverQuest::OnSpawnRemovedGraphics(uint16_t spawn_id) {
	if (!m_graphics_initialized || !m_renderer) {
		return;
	}

	// Clear target if this entity was targeted
	if (m_renderer->getCurrentTargetId() == spawn_id) {
		m_renderer->clearCurrentTarget();
		if (m_combat_manager) {
			m_combat_manager->ClearTarget();
		}
	}

	// Check if this is a corpse - corpses should fade out instead of vanishing instantly
	auto it = m_entities.find(spawn_id);
	if (it != m_entities.end() && it->second.is_corpse) {
		m_renderer->startCorpseDecay(spawn_id);
	} else {
		m_renderer->removeEntity(spawn_id);
	}
}

void EverQuest::OnSpawnMovedGraphics(uint16_t spawn_id, float x, float y, float z, float heading,
                                      float dx, float dy, float dz, int32_t animation) {
	if (!m_graphics_initialized || !m_renderer) {
		return;
	}

	// If this is our player entity, update the renderer's local player position/heading
	// so the camera follows the server-authoritative position
	if (spawn_id == m_my_spawn_id) {
		// Convert heading from degrees (0-360) to EQ format (0-512) for setPlayerPosition
		float heading_512 = heading * 512.0f / 360.0f;
		m_renderer->setPlayerPosition(x, y, z, heading_512);
	}

	m_renderer->updateEntity(spawn_id, x, y, z, heading, dx, dy, dz, animation);
}

void EverQuest::OnGraphicsMovement(const EQT::Graphics::PlayerPositionUpdate& update) {
	// Called by the renderer when the player moves in Player Mode
	// This syncs the player's position with the server

	// Only accept graphics movement when in Player mode
	if (!m_renderer || m_renderer->getRendererMode() != EQT::Graphics::RendererMode::Player) {
		return;
	}

	// Update position from graphics input
	// The renderer provides ground-level Z (where player's feet are)
	// SendMovementUpdate adds m_size/2 to convert to model-center Z for the server
	m_x = update.x;
	m_y = update.y;
	m_z = update.z;  // Ground-level Z from renderer
	// Convert heading from EQ format (0-512) to degrees (0-360) for internal use
	m_heading = update.heading * 360.0f / 512.0f;

	// Derive movement state from velocity
	float speed2D = std::sqrt(update.dx * update.dx + update.dy * update.dy);
	bool isMoving = speed2D > 0.01f;
	m_is_moving = isMoving;

	// Determine if moving backward by comparing velocity direction to heading
	// Convert heading to radians and get unit vector in heading direction
	float headingRad = update.heading / 512.0f * 2.0f * 3.14159265f;
	float headingX = std::sin(headingRad);
	float headingY = std::cos(headingRad);

	// Dot product of velocity and heading direction
	// Positive = forward, Negative = backward
	float dotProduct = update.dx * headingX + update.dy * headingY;
	bool isMovingBackward = isMoving && dotProduct < -0.01f;

	// Determine animation based on movement state
	// Negative animation = play in reverse (e.g., walking backward)
	if (!isMoving) {
		m_animation = ANIM_STAND;
	} else if (speed2D > 5.0f) {  // Running threshold
		m_animation = isMovingBackward ? -ANIM_RUN : ANIM_RUN;
	} else {
		m_animation = isMovingBackward ? -ANIM_WALK : ANIM_WALK;
	}

	if (s_debug_level >= 2) {
		LOG_DEBUG(MOD_MOVEMENT, "OnGraphicsMovement: pos=({:.2f},{:.2f},{:.2f}) heading={:.1f} vel=({:.2f},{:.2f},{:.2f}) anim={}",
			update.x, update.y, update.z, update.heading, update.dx, update.dy, update.dz, m_animation);
	}

	// Check for zone line collision
	CheckZoneLine();

	// SendPositionUpdate has internal 250ms throttling
	SendPositionUpdate();
}

void EverQuest::UpdateInventoryStats() {
	if (!m_renderer) {
		return;
	}

	// Calculate equipment bonuses from inventory
	eqt::inventory::EquipmentStats equipStats;
	float totalWeight = 0.0f;

	// Track weapon skill types for combat animations
	uint8_t primaryWeaponSkill = EQ::WEAPON_HAND_TO_HAND;  // Default to H2H (unarmed)
	uint8_t secondaryWeaponSkill = EQ::WEAPON_NONE;

	if (m_inventory_manager) {
		equipStats = m_inventory_manager->calculateEquipmentStats();
		totalWeight = m_inventory_manager->calculateTotalWeight();

		// Get weapon skill types from equipped items
		using namespace eqt::inventory;
		const eqt::inventory::ItemInstance* primaryItem = m_inventory_manager->getItem(SLOT_PRIMARY);
		const eqt::inventory::ItemInstance* secondaryItem = m_inventory_manager->getItem(SLOT_SECONDARY);

		if (primaryItem && primaryItem->itemId != 0) {
			primaryWeaponSkill = primaryItem->skillType;
		}
		if (secondaryItem && secondaryItem->itemId != 0) {
			secondaryWeaponSkill = secondaryItem->skillType;
		}

		// Update our entity's weapon skill types
		auto it = m_entities.find(m_my_spawn_id);
		if (it != m_entities.end()) {
			it->second.primary_weapon_skill = primaryWeaponSkill;
			it->second.secondary_weapon_skill = secondaryWeaponSkill;
		}

		// Propagate weapon skill types to renderer
		m_renderer->setEntityWeaponSkills(m_my_spawn_id, primaryWeaponSkill, secondaryWeaponSkill);
	}

	// Calculate total stats (base + equipment)
	int totalStr = static_cast<int>(m_STR) + equipStats.str;
	int totalSta = static_cast<int>(m_STA) + equipStats.sta;
	int totalAgi = static_cast<int>(m_AGI) + equipStats.agi;
	int totalDex = static_cast<int>(m_DEX) + equipStats.dex;
	int totalWis = static_cast<int>(m_WIS) + equipStats.wis;
	int totalInt = static_cast<int>(m_INT) + equipStats.int_;
	int totalCha = static_cast<int>(m_CHA) + equipStats.cha;

	// Max weight capacity is equal to total STR
	float maxWeight = static_cast<float>(totalStr);

	// Update weight tracking
	m_weight = totalWeight;
	m_max_weight = maxWeight;

	// Update renderer with all stats
	m_renderer->updateCharacterStats(
		m_cur_hp, m_max_hp,
		m_mana, m_max_mana,
		m_endurance, m_max_endurance,
		0, 0,  // AC/ATK: not yet implemented
		totalStr, totalSta, totalAgi, totalDex, totalWis, totalInt, totalCha,
		equipStats.poisonResist, equipStats.magicResist, equipStats.diseaseResist,
		equipStats.fireResist, equipStats.coldResist,
		m_weight, m_max_weight,
		m_platinum, m_gold, m_silver, m_copper);

	// Update bonus stats (haste, regen)
	if (auto* wm = m_renderer->getWindowManager()) {
		if (auto* inv = wm->getInventoryWindow()) {
			inv->setHaste(equipStats.haste);
			inv->setRegenHP(equipStats.hpRegen);
			inv->setRegenMana(equipStats.manaRegen);
		}
	}
}

// ============================================================================
// Player Mode Loot Window Methods
// ============================================================================

void EverQuest::RequestLootCorpse(uint16_t corpseId) {
	LOG_DEBUG(MOD_INVENTORY, "RequestLootCorpse: corpseId={}", corpseId);

	// Store the corpse being looted
	m_player_looting_corpse_id = corpseId;
	LOG_TRACE(MOD_INVENTORY, "Set m_player_looting_corpse_id={}", m_player_looting_corpse_id);

	// Send loot request packet - server expects only the corpse ID (4 bytes)
	EQ::Net::DynamicPacket packet;
	packet.Resize(4);
	packet.PutUInt32(0, corpseId);
	QueuePacket(HC_OP_LootRequest, &packet);

	// Open the loot window in the UI
	if (m_renderer && m_renderer->getWindowManager()) {
		// Find the corpse entity to get its name (converted to display format)
		std::string corpseName = "Corpse";
		auto it = m_entities.find(corpseId);
		if (it != m_entities.end()) {
			corpseName = EQT::toDisplayName(it->second.name);
		}
		m_renderer->getWindowManager()->openLootWindow(corpseId, corpseName);
	}
}

void EverQuest::LootItemFromCorpse(uint16_t corpseId, int16_t slot, bool autoLoot) {
	LOG_DEBUG(MOD_INVENTORY, "LootItemFromCorpse: corpseId={} slot={} autoLoot={} m_player_looting_corpse_id={} m_my_spawn_id={}",
		corpseId, slot, autoLoot, m_player_looting_corpse_id, m_my_spawn_id);

	// Track this slot as pending loot (waiting for server confirmation)
	m_pending_loot_slots.push_back(slot);
	LOG_TRACE(MOD_INVENTORY, "Added slot {} to pending_loot_slots, size={}", slot, m_pending_loot_slots.size());

	// Send loot item packet
	#pragma pack(1)
	struct LootingItem_Struct {
		uint32_t lootee;
		uint32_t looter;
		uint16_t slot_id;
		uint8_t  unknown3[2];
		uint32_t auto_loot;
	};
	#pragma pack()
	static_assert(sizeof(LootingItem_Struct) == 16, "LootingItem_Struct must be 16 bytes");

	EQ::Net::DynamicPacket packet;
	packet.Resize(sizeof(LootingItem_Struct));
	LootingItem_Struct* li = (LootingItem_Struct*)packet.Data();
	li->lootee = corpseId;
	li->looter = m_my_spawn_id;
	li->slot_id = static_cast<uint16_t>(slot);
	li->unknown3[0] = 0;
	li->unknown3[1] = 0;
	li->auto_loot = autoLoot ? 1 : 0;

	// Debug: print packet bytes (only at high debug level)
	if (s_debug_level >= 3) {
		std::string hex_str;
		const uint8_t* data = (const uint8_t*)packet.Data();
		for (size_t i = 0; i < packet.Length(); i++) {
			char buf[4];
			snprintf(buf, sizeof(buf), "%02x ", data[i]);
			hex_str += buf;
		}
		LOG_TRACE(MOD_INVENTORY, "Sending OP_LootItem packet ({} bytes): {}", packet.Length(), hex_str);
	}
	LOG_DEBUG(MOD_INVENTORY, "OP_LootItem: lootee={} looter={} slot_id={} auto_loot={}",
		li->lootee, li->looter, li->slot_id, li->auto_loot);

	QueuePacket(HC_OP_LootItem, &packet);
}

void EverQuest::LootAllFromCorpse(uint16_t corpseId) {
	LOG_DEBUG(MOD_INVENTORY, "LootAllFromCorpse: corpseId={}", corpseId);

	// Get the loot window and validate looting is possible
	if (!m_renderer || !m_renderer->getWindowManager()) {
		LOG_WARN(MOD_INVENTORY, "LootAllFromCorpse: No renderer or window manager");
		return;
	}

	auto* lootWindow = m_renderer->getWindowManager()->getLootWindow();
	if (!lootWindow || !lootWindow->isOpen()) {
		LOG_DEBUG(MOD_INVENTORY, "LootAllFromCorpse: Loot window not open");
		return;
	}

	// Check if we can loot all
	if (!lootWindow->canLootAll()) {
		std::string error = lootWindow->getLootAllError();
		LOG_DEBUG(MOD_INVENTORY, "LootAllFromCorpse: Cannot loot all - {}", error);
		return;
	}

	// Get items to loot - in Titanium, server closes loot session after each item,
	// so we must loot one item at a time and re-request loot for remaining items
	const auto& items = lootWindow->getLootItems();
	if (items.empty()) {
		LOG_DEBUG(MOD_INVENTORY, "LootAllFromCorpse: No items to loot");
		return;
	}

	// Store remaining slots for sequential looting
	m_loot_all_remaining_slots.clear();
	for (const auto& [slot, item] : items) {
		m_loot_all_remaining_slots.push_back(slot);
	}

	LOG_DEBUG(MOD_INVENTORY, "LootAllFromCorpse: Queued {} items for sequential looting", m_loot_all_remaining_slots.size());

	// Start loot-all operation - loot first item only
	m_loot_all_in_progress = true;
	int16_t firstSlot = m_loot_all_remaining_slots.front();
	m_loot_all_remaining_slots.erase(m_loot_all_remaining_slots.begin());
	LootItemFromCorpse(corpseId, firstSlot, true);  // autoLoot=true for Loot All
}

void EverQuest::DestroyAllCorpseLoot(uint16_t corpseId) {
	LOG_DEBUG(MOD_INVENTORY, "DestroyAllCorpseLoot: corpseId={}", corpseId);

	// For now, just close the loot window - items stay on corpse until despawn
	// The Titanium protocol doesn't have explicit destroy packets for loot items
	CloseLootWindow(corpseId);
}

void EverQuest::CloseLootWindow(uint16_t corpseId) {
	LOG_DEBUG(MOD_INVENTORY, "CloseLootWindow: corpseId={} m_player_looting_corpse_id={}",
		corpseId, m_player_looting_corpse_id);

	// Use passed corpseId, or fall back to m_player_looting_corpse_id if 0
	uint16_t targetCorpseId = (corpseId != 0) ? corpseId : m_player_looting_corpse_id;

	// Clear the looting state
	m_player_looting_corpse_id = 0;
	m_pending_loot_slots.clear();
	m_loot_all_in_progress = false;
	m_loot_all_remaining_slots.clear();

	// Mark this corpse as ready for deletion (server will send DeleteSpawn after EndLootRequest)
	m_loot_complete_corpse_id = targetCorpseId;

	// Send end loot request to server (OP_EndLootRequest, not OP_LootComplete)
	// Server expects 4 bytes with corpse entity ID as uint16 at offset 0
	EQ::Net::DynamicPacket packet;
	packet.Resize(4);
	packet.PutUInt16(0, targetCorpseId);
	packet.PutUInt16(2, 0);  // padding
	QueuePacket(HC_OP_EndLootRequest, &packet);
	LOG_DEBUG(MOD_INVENTORY, "CloseLootWindow: Sent HC_OP_EndLootRequest with corpseId={}", targetCorpseId);

	// Close the loot window in the UI
	if (m_renderer && m_renderer->getWindowManager()) {
		m_renderer->getWindowManager()->closeLootWindow();
	}
}

void EverQuest::SaveHotbarConfig() {
	if (m_config_path.empty() || !m_renderer) return;
	auto* windowMgr = m_renderer->getWindowManager();
	if (!windowMgr) return;

	auto config = EQ::JsonConfigFile::Load(m_config_path);
	Json::Value& root = config.RawHandle();

	// Migrate old config format to new format if needed
	// Old format: array of clients at top level
	// New format: object with "clients" array
	if (root.isArray()) {
		Json::Value newRoot;
		newRoot["clients"] = root;
		root = newRoot;
		LOG_INFO(MOD_CONFIG, "Migrated config from legacy array format to new object format");
	}

	// Now root should be an object - find or create the client config
	Json::Value* clientConfig = nullptr;
	if (root.isObject()) {
		if (root.isMember("clients") && root["clients"].isArray() && root["clients"].size() > 0) {
			clientConfig = &root["clients"][0];
		} else if (!root.isMember("clients")) {
			// Single client object at top level - wrap it in clients array
			Json::Value clientsCopy = root;
			root = Json::Value(Json::objectValue);
			root["clients"] = Json::Value(Json::arrayValue);
			root["clients"].append(clientsCopy);
			clientConfig = &root["clients"][0];
			LOG_INFO(MOD_CONFIG, "Migrated single client config to clients array format");
		}
	}

	if (clientConfig && clientConfig->isObject()) {
		(*clientConfig)["hotbar"] = windowMgr->collectHotbarData();
		config.Save(m_config_path);
		LOG_DEBUG(MOD_CONFIG, "Saved hotbar config to {}", m_config_path);
	} else {
		LOG_WARN(MOD_CONFIG, "Could not save hotbar config - invalid config format");
	}
}

void EverQuest::LoadHotbarConfig() {
	if (m_config_path.empty() || !m_renderer) return;
	auto* windowMgr = m_renderer->getWindowManager();
	if (!windowMgr) return;

	auto config = EQ::JsonConfigFile::Load(m_config_path);
	Json::Value& root = config.RawHandle();

	// Find the client config object to load hotbar from
	const Json::Value* clientConfig = nullptr;
	if (root.isArray() && root.size() > 0) {
		clientConfig = &root[0];
	} else if (root.isObject()) {
		if (root.isMember("clients") && root["clients"].isArray() && root["clients"].size() > 0) {
			clientConfig = &root["clients"][0];
		} else {
			clientConfig = &root;
		}
	}

	if (clientConfig && clientConfig->isObject() && clientConfig->isMember("hotbar")) {
		windowMgr->loadHotbarData((*clientConfig)["hotbar"]);
		LOG_DEBUG(MOD_CONFIG, "Loaded hotbar config from {}", m_config_path);
	} else {
		LOG_DEBUG(MOD_CONFIG, "No hotbar config found in {}", m_config_path);
	}
}

#endif // EQT_HAS_GRAPHICS
 
