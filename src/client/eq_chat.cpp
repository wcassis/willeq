// EverQuest chat and text-message cluster.
//
// Split out of eq.cpp (Batch F, unit F06): channel-message send/process
// (ZoneSendChannelMessage, ZoneProcessChannelMessage), SendChatMessage, the
// AddChat* family (system/combat/miss messages into the chat buffer), the
// pending-hotbar-button helpers, ProcessChatInput, and the colored/formatted/
// simple text processors (ZoneProcessColoredText, ZoneProcessFormattedMessage,
// ZoneProcessSimpleMessage). Moved verbatim, no behavior change.
// See refactor/F06_chat.md.

#include "client/eq.h"
#include "client/combat.h"
#include "client/formatted_message.h"
#include "client/skill/skill_manager.h"
#include "common/logging.h"
#include "common/packet_structs.h"
#include "common/name_utils.h"
#include "client/bridge/game_state_bridge.h"
// P28: these are plain data-model headers (chat channel enum/buffer, chat
// command registry, hotbar model) with no renderer dependency — used
// unconditionally below (ChatChannel, m_command_registry). Un-guarded to
// match their declassified eq.h forward decls.
#include "client/chat/chat_message_buffer.h"
#include "client/graphics/ui/command_registry.h"
#include "client/hotbar/hotbar_model.h"

#include <cstring>

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
	// Process incoming channel messages.
	// T20: the guard is > 150, not >= 150. The message cstring starts at
	// offset 150, so an exactly-150-byte packet passed the old check and then
	// read at the packet end, where the bounds-checked GetCString (T08) throws
	// std::out_of_range on this receive path with nothing to catch it. Real
	// payloads are always at least 151 bytes (message + NUL).
	if (p.Length() <= 150) { // ChannelMessage_Struct + at least the message NUL
		LOG_WARN(MOD_ZONE, "ChannelMessage packet too small: {} bytes", p.Length());
		return;
	}

	std::string target = p.GetCString(2);
	std::string sender = p.GetCString(66);
	uint32_t language = p.GetUInt32(130);
	uint32_t channel = p.GetUInt32(134);
	uint32_t skill = p.GetUInt32(146);
	std::string message = p.GetCString(150);

	// Track last tell sender for /reply command
	if (channel == CHAT_CHANNEL_TELL && !sender.empty() && sender != m_character) {
		m_last_tell_sender = sender;
		LOG_DEBUG(MOD_MAIN, "Last tell sender updated to: {}", m_last_tell_sender);
	}

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

	// Publish ChatMessage event to bridge
	if (m_bridge) {
		eqt::state::ChatMessageData data;
		data.sender = sender;
		data.message = message;
		data.channelType = channel;
		data.channelName =
			channel == CHAT_CHANNEL_SAY ? "say" :
			channel == CHAT_CHANNEL_TELL ? "tell" :
			channel == CHAT_CHANNEL_SHOUT ? "shout" :
			channel == CHAT_CHANNEL_OOC ? "ooc" :
			channel == CHAT_CHANNEL_GROUP ? "group" :
			channel == CHAT_CHANNEL_GUILD ? "guild" :
			channel == CHAT_CHANNEL_AUCTION ? "auction" :
			channel == CHAT_CHANNEL_EMOTE ? "emote" : "other";
		m_bridge->pushEvent(eqt::state::GameEvent(
			eqt::state::GameEventType::ChatMessage, std::move(data)));
	}
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
	// Publish SystemMessage event to bridge
	if (m_bridge) {
		eqt::state::ChatMessageData data;
		data.message = text;
		data.channelType = static_cast<uint32_t>(eqt::ui::ChatChannel::System);
		data.channelName = "system";
		m_bridge->pushEvent(eqt::state::GameEvent(
			eqt::state::GameEventType::SystemMessage, std::move(data)));
	}
	// Also print to console
	LOG_INFO(MOD_MAIN, "{}", text);
}

void EverQuest::AddChatCombatMessage(const std::string &text, bool is_self)
{
	// Publish ChatMessage event to bridge (combat channel)
	if (m_bridge) {
		eqt::state::ChatMessageData data;
		data.message = text;
		data.channelType = static_cast<uint32_t>(is_self ? eqt::ui::ChatChannel::CombatSelf : eqt::ui::ChatChannel::Combat);
		data.channelName = is_self ? "combat_self" : "combat";
		m_bridge->pushEvent(eqt::state::GameEvent(
			eqt::state::GameEventType::ChatMessage, std::move(data)));
	}
	// Also print to console
	LOG_DEBUG(MOD_COMBAT, "{}", text);
}

void EverQuest::AddChatMissMessage(const std::string &text)
{
	// D20e1: Bridge already handles ChatMessage → chat window display.
	// Publish ChatMessage event to bridge (combat miss channel)
	if (m_bridge) {
		eqt::state::ChatMessageData data;
		data.message = text;
		data.channelType = static_cast<uint32_t>(eqt::ui::ChatChannel::CombatMiss);
		data.channelName = "combat_miss";
		m_bridge->pushEvent(eqt::state::GameEvent(
			eqt::state::GameEventType::ChatMessage, std::move(data)));
	}
	LOG_DEBUG(MOD_COMBAT, "{}", text);
}

// ============================================================================
// Hotbar Button Management (Stub for future implementation)
// ============================================================================

void EverQuest::AddPendingHotbarButton(uint8_t skill_id)
{
	const char* skill_name = EQ::getSkillName(skill_id);
	m_pending_hotbar_buttons.emplace_back(
		eqt::ui::HotbarButtonType::Skill,
		static_cast<uint32_t>(skill_id),
		skill_name ? skill_name : "Unknown Skill"
	);
	LOG_INFO(MOD_MAIN, "Queued skill {} ({}) for hotbar (total pending: {})",
		skill_id, skill_name ? skill_name : "Unknown", m_pending_hotbar_buttons.size());
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
	// S04: command_registry is pre-allocated in EverQuest constructor

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

	// Display in chat window via bridge
	if (!message.empty()) {
		if (m_bridge) {
			eqt::state::ChatMessageData data;
			data.message = message;
			data.channelType = static_cast<uint32_t>(eqt::ui::ChatChannel::Spell);
			data.channelName = "spell";
			m_bridge->pushEvent(eqt::state::GameEvent(
				eqt::state::GameEventType::SystemMessage, std::move(data)));
		}
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

	// Parse message arguments (null-byte delimited in packet)
	std::vector<std::string> args;
	eqt::ParsedFormattedMessageWithArgs parsedArgs;

	if (p.Length() > 14) {
		const uint8_t* data = static_cast<const uint8_t*>(p.Data()) + 14;
		size_t msg_len = p.Length() - 14;

		// Parse with argument preservation
		parsedArgs = eqt::parseFormattedMessageArgs(data, msg_len);
		args = parsedArgs.args;

		LOG_DEBUG(MOD_ZONE, "[FormattedMessage] string_id={}, type={}, args_count={}, links={}",
			string_id, type, args.size(), parsedArgs.links.size());
		for (size_t i = 0; i < args.size(); i++) {
			LOG_DEBUG(MOD_ZONE, "[FormattedMessage]   arg[{}]='{}'", i, args[i]);
		}
	}

	// Look up the format template from StringDatabase
	std::string tmpl = m_string_db.getString(string_id);

	// Format the message using the template and arguments
	std::string formatted_text;
	if (!tmpl.empty()) {
		formatted_text = GetFormattedStringMessage(string_id, args);
		LOG_DEBUG(MOD_ZONE, "[FormattedMessage] template='{}', formatted='{}'", tmpl, formatted_text);
	} else {
		// Fallback: join args with spaces if template not found
		for (size_t i = 0; i < args.size(); i++) {
			if (i > 0) formatted_text += " ";
			formatted_text += args[i];
		}
		LOG_DEBUG(MOD_ZONE, "[FormattedMessage] No template for string_id={}, fallback text='{}'",
			string_id, formatted_text);
	}

	if (formatted_text.empty()) {
		LOG_DEBUG(MOD_ZONE, "[FormattedMessage] string_id={}, type={}, empty result",
			string_id, type);
		return;
	}

	// Determine chat channel based on template content
	eqt::ui::ChatChannel channel = eqt::ui::ChatChannel::System;
	std::string sender;
	bool isSystemMessage = true;

	if (!tmpl.empty()) {
		// Check template patterns to determine message type
		if (tmpl.find("looted") != std::string::npos) {
			channel = eqt::ui::ChatChannel::Loot;
		} else if (tmpl.find("fashioned") != std::string::npos ||
		           tmpl.find("create") != std::string::npos) {
			channel = eqt::ui::ChatChannel::System;
		} else if (tmpl.find("shouts") != std::string::npos) {
			channel = eqt::ui::ChatChannel::Shout;
			// For shout messages, first arg is usually the NPC name
			if (!args.empty()) {
				sender = args[0];
				isSystemMessage = false;
			}
		} else if (tmpl.find("says") != std::string::npos ||
		           tmpl.find("tells you") != std::string::npos) {
			channel = eqt::ui::ChatChannel::NPCDialogue;
			// For say/tell messages, first arg is usually the NPC name
			if (!args.empty()) {
				sender = args[0];
				isSystemMessage = false;
			}
		} else if (tmpl.find("experience") != std::string::npos) {
			channel = eqt::ui::ChatChannel::Experience;
		} else if (tmpl.find("spell") != std::string::npos && tmpl.find("worn off") != std::string::npos) {
			channel = eqt::ui::ChatChannel::Spell;
		}
	}

	// Publish ChatMessage event to bridge
	if (m_bridge) {
		eqt::state::ChatMessageData data;
		data.sender = sender;
		data.message = formatted_text;
		data.channelType = static_cast<uint32_t>(channel);
		data.channelName = isSystemMessage ? "system" : "npc";
		m_bridge->pushEvent(eqt::state::GameEvent(
			eqt::state::GameEventType::ChatMessage, std::move(data)));
	}

	LOG_DEBUG(MOD_ZONE, "[FormattedMessage] channel={}, sender='{}', text='{}'",
		static_cast<int>(channel), sender, formatted_text);
}

void EverQuest::ZoneProcessSimpleMessage(const EQ::Net::Packet &p)
{
	// SimpleMessage is used for system messages that use string templates
	// SimpleMessage_Struct: string_id(4), color(4), unknown8(4)
	// Total: 12 bytes + 2 byte opcode = 14 bytes minimum
	// Note: SimpleMessage has no argument data - placeholders must be filled from context

	if (p.Length() < 14) {
		LOG_WARN(MOD_ZONE, "SimpleMessage packet too small: {} bytes", p.Length());
		return;
	}

	// SimpleMessage_Struct: string_id (4 bytes), color (4 bytes), unknown8 (4 bytes)
	// Offsets include 2-byte opcode prefix
	uint32_t string_id = p.GetUInt32(2);    // string_id at offset 0
	uint32_t color_type = p.GetUInt32(6);   // color at offset 4

	// Get the message template for this string ID
	std::string tmpl = GetStringMessage(string_id);

	LOG_DEBUG(MOD_ZONE, "[SimpleMessage] color_type={}, string_id={}, template='{}'",
		color_type, string_id, tmpl);

	// Build arguments from context for placeholder substitution
	// SimpleMessage doesn't include args in packet - they must be inferred
	std::vector<std::string> args;

	// Determine context-based arguments
	// %1 is typically the target entity name
	if (tmpl.find("%1") != std::string::npos || tmpl.find("#1") != std::string::npos) {
		std::string target_name = "something";

		// For "slain" messages, use the last slain entity name
		// Note: String 12113 is "You have slain %1!" in eqstr_us.txt
		// Server may send 303 or 12113 depending on context
		if ((string_id == 303 || string_id == 12113) && !m_last_slain_entity_name.empty()) {
			target_name = m_last_slain_entity_name;
		}
		// Otherwise try current combat target
		else if (m_combat_manager && m_combat_manager->GetTargetId() != 0) {
			auto it = m_entities.find(m_combat_manager->GetTargetId());
			if (it != m_entities.end()) {
				target_name = EQT::toDisplayName(it->second.name);
			}
		}

		args.push_back(target_name);
	}

	// Format the message with arguments
	std::string message_text;
	if (!args.empty()) {
		message_text = GetFormattedStringMessage(string_id, args);
	} else {
		message_text = tmpl;
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

	// Publish ChatMessage event to bridge
	if (m_bridge) {
		eqt::state::ChatMessageData data;
		data.message = message_text;
		data.channelType = static_cast<uint32_t>(channel);
		data.channelName = "system";
		m_bridge->pushEvent(eqt::state::GameEvent(
			eqt::state::GameEventType::ChatMessage, std::move(data)));
	}

	LOG_DEBUG(MOD_ZONE, "[SimpleMessage] channel={}, text='{}'",
		static_cast<int>(channel), message_text);
}
