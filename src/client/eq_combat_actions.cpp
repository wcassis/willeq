// EverQuest combat-resolution packet cluster.
//
// Split out of eq.cpp (Batch F, unit F13): the Consider handler
// (ZoneProcessConsider), the Action handler (ZoneProcessAction), the Damage
// handler (ZoneProcessDamage, ~237 lines, plus its file-local getDamageVerb
// helper), the MoneyOnCorpse handler (ZoneProcessMoneyOnCorpse), and the
// loot-item handler (ZoneProcessLootItem). Moved verbatim, no behavior
// change. See refactor/F13_combat_actions.md.
//
// Note: the loot-corpse family (RequestLootCorpse, LootItemFromCorpse,
// LootAllFromCorpse, DestroyAllCorpseLoot) is NOT contiguous with this
// cluster in eq.cpp (separated by ~1,600 lines of unrelated methods) and is
// deliberately left in eq.cpp for a later unit.

#include "client/eq.h"
#include "client/combat.h"
#include "client/spell/spell_manager.h"
#include "client/trade_manager.h"
#include "client/animation_constants.h"
#include "common/logging.h"
#include "common/packet_structs.h"
#include "common/name_utils.h"
#include "client/bridge/game_state_bridge.h"

// P28: renderer-free chat data model, used unconditionally below
// (eqt::ui::ChatChannel).
#include "client/chat/chat_message_buffer.h"

#ifdef WITH_AUDIO
#include "client/audio/audio_manager.h"
#include "client/audio/sound_assets.h"
#endif

#include <sstream>

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

	// Display consider message to chat
	std::string target_name = "Unknown";
	auto it = m_entities.find(static_cast<uint16_t>(con->targetid));
	if (it != m_entities.end()) {
		target_name = EQT::toDisplayName(it->second.name);
	}

	// Determine faction standing message
	// Faction values: 1=ally, 2=warmly, 3=kindly, 4=amiably, 5=indifferent,
	//                 6=apprehensive, 7=dubious, 8=threatening, 9=scowling
	std::string faction_msg;
	switch (con->faction) {
		case 1: faction_msg = "regards you as an ally"; break;
		case 2: faction_msg = "looks upon you warmly"; break;
		case 3: faction_msg = "kindly considers you"; break;
		case 4: faction_msg = "judges you amiably"; break;
		case 5: faction_msg = "regards you indifferently"; break;
		case 6: faction_msg = "looks your way apprehensively"; break;
		case 7: faction_msg = "glowers at you dubiously"; break;
		case 8: faction_msg = "threatens you"; break;
		case 9: faction_msg = "scowls at you, ready to attack"; break;
		default: faction_msg = fmt::format("regards you (faction {})", con->faction); break;
	}

	// Determine con color name and display color based on server level value
	std::string con_color;
	switch (con->level) {
		case 2:  con_color = "green"; break;
		case 4:  con_color = "blue"; break;
		case 6:  con_color = "gray"; break;
		case 10: con_color = "light blue"; break;
		case 13: con_color = "red"; break;
		case 15: con_color = "yellow"; break;
		case 18: con_color = "light blue"; break;
		case 20: con_color = "white"; break;
		default: con_color = "white"; break;
	}

	// Format: "a gnoll scout regards you indifferently -- cons green"
	std::string message = fmt::format("{} {} -- cons {}", target_name, faction_msg, con_color);
	// Publish SystemMessage event to bridge
	if (m_bridge) {
		eqt::state::ChatMessageData data;
		data.message = message;
		data.channelType = static_cast<uint32_t>(eqt::ui::ChatChannel::System);
		data.channelName = "system";
		m_bridge->pushEvent(eqt::state::GameEvent(
			eqt::state::GameEventType::SystemMessage, std::move(data)));
	}
	LOG_INFO(MOD_MAIN, "{}", message);
}

void EverQuest::ZoneProcessAction(const EQ::Net::Packet &p)
{
	// Combat action (attack, spell cast, etc)
	if (p.Length() < 33) { // 2 opcode + the 31-byte wire Action struct below
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

	// Convert to the spell manager's in-memory DTO (NOT a wire struct -- the
	// wire layout is the 31-byte ActionPacket_Struct above)
	EQT::SpellActionInfo spellAction;
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

// Helper: Get damage verb based on damage type
// Returns pair: {second_person, third_person} e.g. {"crush", "crushes"}
static std::pair<const char*, const char*> getDamageVerb(uint8_t damage_type) {
	switch (damage_type) {
		case 0:  // Blunt (1HBlunt, 2HBlunt)
			return {"crush", "crushes"};
		case 1:  // Slashing (1HSlashing, 2HSlashing)
			return {"slash", "slashes"};
		case 4:  // Hand-to-hand, Feign Death
			return {"hit", "hits"};
		case 7:  // Archery
			return {"hit", "hits"};
		case 8:  // Backstab
			return {"backstab", "backstabs"};
		case 10: // Bash
			return {"bash", "bashes"};
		case 21: // Dragon Punch
			return {"strike", "strikes"};
		case 23: // Eagle Strike, Tiger Claw
			return {"strike", "strikes"};
		case 30: // Kick, Flying Kick, Round Kick
			return {"kick", "kicks"};
		case 36: // Piercing (1HPiercing, 2HPiercing)
			return {"pierce", "pierces"};
		case 51: // Throwing
			return {"hit", "hits"};
		case 74: // Frenzy
			return {"hit", "hits"};
		default:
			return {"hit", "hits"};
	}
}

void EverQuest::ZoneProcessDamage(const EQ::Net::Packet &p)
{
	LOG_INFO(MOD_COMBAT, "ZoneProcessDamage called, packet length={}", p.Length());

	// Damage notification
	if (p.Length() < 25) { // 2 opcode + 23 CombatDamage_Struct
		LOG_INFO(MOD_COMBAT, "Damage packet too short: {} < 25", p.Length());
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

	// Get entity names for combat messages
	std::string target_name;
	std::string source_name;
	auto target_it = m_entities.find(target_id);
	auto source_it = m_entities.find(source_id);
	if (target_it != m_entities.end()) {
		target_name = EQT::toDisplayName(target_it->second.name);
	}
	if (source_it != m_entities.end()) {
		source_name = EQT::toDisplayName(source_it->second.name);
	}

	if (s_debug_level >= 2 || IsTrackedTarget(target_id) || IsTrackedTarget(source_id)) {
		LOG_DEBUG(MOD_COMBAT, "{} -> {} for {} damage (type {})",
			source_name.empty() ? std::to_string(source_id) : source_name,
			target_name.empty() ? std::to_string(target_id) : target_name,
			damage_amount, damage_type);
	}

	// Add combat chat messages
	if (damage_amount > 0 && !target_name.empty()) {
		bool player_is_target = (target_id == m_my_spawn_id);
		bool player_is_source = (source_id == m_my_spawn_id);

		// Get verb based on damage type (crush, slash, pierce, kick, etc.)
		auto [verb_second, verb_third] = getDamageVerb(damage_type);

		std::string msg;
		if (player_is_source) {
			// Player dealt damage
			if (spell_id > 0 && spell_id != 0xFFFF) {
				msg = fmt::format("You hit {} for {} points of non-melee damage.", target_name, damage_amount);
			} else {
				msg = fmt::format("You {} {} for {} points of damage.", verb_second, target_name, damage_amount);
			}
			AddChatCombatMessage(msg, true);
		} else if (player_is_target) {
			// Player took damage
			if (spell_id > 0 && spell_id != 0xFFFF) {
				msg = fmt::format("{} hit you for {} points of non-melee damage.", source_name.empty() ? "Unknown" : source_name, damage_amount);
			} else {
				msg = fmt::format("{} {} YOU for {} points of damage.", source_name.empty() ? "Unknown" : source_name, verb_third, damage_amount);
			}
			AddChatCombatMessage(msg, true);
		} else if (!source_name.empty()) {
			// Observing combat between others
			if (spell_id > 0 && spell_id != 0xFFFF) {
				msg = fmt::format("{} hit {} for {} points of non-melee damage.", source_name, target_name, damage_amount);
			} else {
				msg = fmt::format("{} {} {} for {} points of damage.", source_name, verb_third, target_name, damage_amount);
			}
			AddChatCombatMessage(msg, false);
		}
	} else if (damage_amount == 0 && !target_name.empty()) {
		// Miss - use CombatMiss channel (filterable separately from hits)
		bool player_is_source = (source_id == m_my_spawn_id);
		bool player_is_target = (target_id == m_my_spawn_id);

		// Get verb for miss messages too
		auto [verb_second, verb_third] = getDamageVerb(damage_type);

		std::string msg;
		if (player_is_source) {
			msg = fmt::format("You try to {} {} but miss!", verb_second, target_name);
			AddChatMissMessage(msg);
		} else if (player_is_target && !source_name.empty()) {
			msg = fmt::format("{} tries to {} YOU but misses!", source_name, verb_second);
			AddChatMissMessage(msg);
		} else if (!source_name.empty()) {
			// Observing combat between others - also show misses
			msg = fmt::format("{} tries to {} {} but misses!", source_name, verb_second, target_name);
			AddChatMissMessage(msg);
		}
	}

#ifdef WITH_AUDIO
	// Play combat sounds at target position
	if (m_audio_manager && target_it != m_entities.end()) {
		const Entity& target = target_it->second;
		bool isMelee = (spell_id == 0 || spell_id == 0xFFFF);

		if (damage_amount > 0) {
			if (isMelee) {
				// Melee hit sound
				m_audio_manager->playSound(EQT::Audio::SoundId::MELEE_HIT,
					glm::vec3(target.x, target.y, target.z));
			} else {
				// Spell damage - play spell impact sound
				// TODO: Map specific spells to specific impact sounds
				m_audio_manager->playSound(EQT::Audio::SoundId::SPELL_CAST,
					glm::vec3(target.x, target.y, target.z));
			}
		} else if (isMelee) {
			// Miss - play swing sound
			m_audio_manager->playSound(EQT::Audio::SoundId::MELEE_MISS,
				glm::vec3(target.x, target.y, target.z));
		}
	}
#endif

	// Publish DamageEvent to bridge
	if (m_bridge && damage_amount > 0) {
		eqt::state::DamageEventData data;
		data.sourceId = source_id;
		data.targetId = target_id;
		data.amount = damage_amount;
		data.type = damage_type;
		m_bridge->pushEvent(eqt::state::GameEvent(
			eqt::state::GameEventType::DamageEvent, std::move(data)));
	}

	// Publish combat animation events via bridge (D17b: bridge is sole path to renderer)
	if (m_bridge) {
		bool isMeleeDamage = (damage_type < 80);

		// Don't animate dead entities
		auto target_entity_it = m_entities.find(target_id);
		bool targetAlive = (target_entity_it != m_entities.end() && target_entity_it->second.hp_percent > 0);

		if (isMeleeDamage && targetAlive) {
			// Calculate damage percentage for animation selection
			float damagePercent = 0.0f;
			if (target_id == m_my_spawn_id && m_max_hp > 0) {
				damagePercent = (static_cast<float>(damage_amount) / static_cast<float>(m_max_hp)) * 100.0f;
			} else if (target_entity_it != m_entities.end()) {
				uint8_t level = target_entity_it->second.level > 0 ? target_entity_it->second.level : 1;
				float estimatedMaxHP = static_cast<float>(level) * 15.0f;
				damagePercent = (static_cast<float>(damage_amount) / estimatedMaxHP) * 100.0f;
			}

			eqt::state::CombatAnimationData data;
			data.sourceId = source_id;
			data.targetId = target_id;
			data.damageType = damage_type;
			data.damageAmount = damage_amount;
			data.damagePercent = static_cast<uint8_t>(damagePercent);
			m_bridge->pushEvent(eqt::state::GameEvent(
				eqt::state::GameEventType::CombatAnimation, std::move(data)));

			if (s_debug_level >= 2 || IsTrackedTarget(target_id) || IsTrackedTarget(source_id)) {
				LOG_DEBUG(MOD_COMBAT, "Queued melee anim: source={} target={} skill={} dmg={} pct={:.1f}%",
					source_id, target_id, damage_type, damage_amount, damagePercent);
			}
		} else if (damage_amount > 0 && targetAlive && !isMeleeDamage) {
			// Non-melee damage (spells, DoT, environmental) - play immediately
			float damagePercent = 0.0f;
			if (target_id == m_my_spawn_id && m_max_hp > 0) {
				damagePercent = (static_cast<float>(damage_amount) / static_cast<float>(m_max_hp)) * 100.0f;
			} else if (target_entity_it != m_entities.end()) {
				uint8_t level = target_entity_it->second.level > 0 ? target_entity_it->second.level : 1;
				float estimatedMaxHP = static_cast<float>(level) * 15.0f;
				damagePercent = (static_cast<float>(damage_amount) / estimatedMaxHP) * 100.0f;
			}

			bool isDrowning = (damage_type == 253);
			bool isTrap = (damage_type == 254);
			const char* damageAnim = EQ::getDamageAnimation(damagePercent, isDrowning, isTrap);

			eqt::state::EntityAnimationEventData data;
			data.spawnId = target_id;
			data.animCode = 0;
			data.animName = damageAnim;
			data.loop = false;
			data.playThrough = true;
			m_bridge->pushEvent(eqt::state::GameEvent(
				eqt::state::GameEventType::EntityAnimationEvent, std::move(data)));
			if (s_debug_level >= 2 || IsTrackedTarget(target_id)) {
				LOG_DEBUG(MOD_COMBAT, "Damage reaction '{}' on target={} (dmg={}, pct={:.1f}%, type={})",
					damageAnim, target_id, damage_amount, damagePercent, damage_type);
			}
		}
	}

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

	LOG_DEBUG(MOD_INVENTORY, "ProcessLootItem: Received item data, length={}", item_data.length());
	LOG_TRACE(MOD_INVENTORY, "Item data preview: {}",
		item_data.substr(0, std::min(size_t(100), item_data.length())));

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

			LOG_DEBUG(MOD_INVENTORY, "Loot window item: slot {} = '{}'", slot_num, item_name);

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
