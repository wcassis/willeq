// eqt::chat::ChatComponent -- the EverQuest chat and text-message cluster.
//
// Split out of eq.cpp (Batch F, unit F06) into src/client/eq_chat.cpp, and
// K03 (Batch K, refactor/K_component_extraction.md) moved it again, from
// there to here, rebasing the bodies from EverQuest onto
// eqt::chat::ChatComponent. Still verbatim -- only the substitutions
// §Mechanism 4 allows (m_character/m_bridge/... -> ctx_..., m_last_tell_sender
// and m_string_db stay on EverQuest so they are owner_..., AddChatSystemMessage
// and GetStringMessage/GetFormattedStringMessage stay behind so they are
// owner_.Foo(), and EverQuest's statics s_debug_level and DumpPacket are
// qualified as EverQuest:: -- friendship grants access, not scope).
//
// The three AddChat* output primitives did NOT move: they stay on EverQuest,
// in src/client/eq_chat.cpp. See the K03 Review.
//
// ChatChannelName() (T320's single channel-name table, declared in
// eq_enums.h) came with the bodies that call it; it is a free function and
// changed not at all.
//
// T341 (board B038): the "stay on EverQuest" clauses above are HISTORY, not
// the current shape. This family now reaches EverQuest not at all -- the
// `owner_` handle and the `friend class` line are both gone; see the T341
// note in this family's component header.

#include "client/chat/chat_component.h"
#include "client/say_link_text.h"

#include "client/eq.h"
#include "client/combat.h"
#include "client/formatted_message.h"
#include "client/skill/skill_manager.h"
#include "common/logging.h"
#include <algorithm>
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

// T320: the single channel-name table (declared in eq.h). Every consumer that
// turns a wire channel number into a display name goes through here -- the
// console [CHAT] line and the bridge's ChatMessageData::channelName both did
// it themselves, from two ternary chains forty lines apart in
// ZoneProcessChannelMessage below, and the two had drifted apart: the console
// chain had no CHAT_CHANNEL_AUCTION case (auction printed as "chan4" for
// T297's whole live run) while the bridge chain did, and NEITHER knew channel
// 14, the server's echo of an outgoing tell ("chan14").
//
// Unnamed channels deliberately fall through to "chanN" rather than a flat
// "other" so an unhandled number stays diagnosable in a log -- that fallback
// is how channel 14 was spotted at all.
std::string ChatChannelName(uint32_t channel)
{
	switch (channel) {
	case CHAT_CHANNEL_GUILD:     return "guild";
	case CHAT_CHANNEL_GROUP:     return "group";
	case CHAT_CHANNEL_SHOUT:     return "shout";
	case CHAT_CHANNEL_AUCTION:   return "auction";
	case CHAT_CHANNEL_OOC:       return "ooc";
	case CHAT_CHANNEL_BROADCAST: return "broadcast";
	case CHAT_CHANNEL_TELL:      return "tell";
	case CHAT_CHANNEL_SAY:       return "say";
	case CHAT_CHANNEL_PETITION:  return "petition";
	case CHAT_CHANNEL_GMSAY:     return "gmsay";
	case CHAT_CHANNEL_TELL_ECHO: return "tellecho";
	case CHAT_CHANNEL_RAID:      return "raid";
	case CHAT_CHANNEL_EMOTE:     return "emote";
	default:                     return fmt::format("chan{}", channel);
	}
}

void eqt::chat::ChatComponent::ZoneSendChannelMessage(const std::string &message, ChatChannelType channel, const std::string &target)
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
	size_t name_len = ctx_.character.length();
	if (name_len > 63) name_len = 63;
	memcpy((char*)p.Data() + 66, ctx_.character.c_str(), name_len);
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

	if (EverQuest::GetDebugLevel() >= 1) {
		// T320: this was a THIRD copy of the channel-name table, on the SEND
		// side, and drifted differently again from the two receive-side
		// chains -- it had auction but no emote, petition, gmsay, raid or
		// broadcast, and fell back to a bare "unknown" that threw the channel
		// number away entirely. Found by the guard test written for the other
		// two (tests/test_chat_channel_names.cpp), not by reading.
		std::cout << fmt::format("Sending {} message: '{}'",
			ChatChannelName(channel), message) << std::endl;
	}

	eqt::strings::DumpPacket("C->S", HC_OP_ChannelMessage, p);
	ctx_.net.SafeQueueZone(p);
}

void eqt::chat::ChatComponent::ZoneProcessChannelMessage(const EQ::Net::Packet &p)
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
	// T395 (board B205): 0x12-delimited EQ links arrive in chat text too --
	// an item link a player posts is the same wire shape as the saylink
	// eqemu wrapped a spell name in. Render the display text, not the body.
	std::string message = eqt::StripSayLinks(p.GetCString(150));

	// Track last tell sender for /reply command
	if (channel == CHAT_CHANNEL_TELL && !sender.empty() && sender != ctx_.character) {
		m_last_tell_sender = sender;
		LOG_DEBUG(MOD_MAIN, "Last tell sender updated to: {}", m_last_tell_sender);
	}

	if (EverQuest::GetDebugLevel() >= 1) {
		std::cout << fmt::format("[CHAT] {} ({}): {}",
			sender,
			ChatChannelName(channel),   // T320: one table, see top of file
			message) << std::endl;

		if (!target.empty() && channel == CHAT_CHANNEL_TELL) {
			std::cout << fmt::format("  (Tell to: {})", target) << std::endl;
		}
	}

	// Publish ChatMessage event to bridge
	if (ctx_.bridge) {
		eqt::state::ChatMessageData data;
		data.sender = sender;
		data.message = message;
		data.channelType = channel;
		data.channelName = ChatChannelName(channel);  // T320: one table
		ctx_.bridge->pushEvent(eqt::state::GameEvent(
			eqt::state::GameEventType::ChatMessage, std::move(data)));
	}
}

void eqt::chat::ChatComponent::SendChatMessage(const std::string &message, const std::string &channel_name, const std::string &target)
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
	if (!ctx_.zone_connection || !ctx_.game_state.world().isZoneConnected()) {
		LOG_WARN(MOD_MAIN, "Not connected to zone server");
		return;
	}

	// T320: say so when there is no group to say it to. The measured
	// complaint was not only that group state went stale across a zone
	// transition, but that /gsay into a group that no longer existed
	// produced NO error -- the message just vanished. The server drops a
	// group ChannelMessage from an ungrouped client silently
	// (eqemu zone/client.cpp ChannelMessageReceived: no group, no
	// Group::GroupMessage, no reply), so the client has to be the one to
	// tell the player.
	if (channel == CHAT_CHANNEL_GROUP && !ctx_.game_state.group().inGroup()) {
		ctx_.out.System("You must be in a group to use that command.");
		return;
	}

	// T243: /emote is not a ChannelMessage on Titanium -- it is OP_Emote with
	// Emote_Struct. eqemu does accept ChannelMessage channel 22 (zone/client.cpp
	// ChatChannel_Emotes, "Underfoot and later") and rebroadcasts it as
	// OP_Emote, but the Titanium client never sends that form, and until T243
	// this client could not decode the OP_Emote reply either.
	if (channel == CHAT_CHANNEL_EMOTE) {
		ZoneSendEmote(message);
		return;
	}

	// Route messages based on channel type
	// For now, route all messages through zone server since UCS implementation needs fixing
	ZoneSendChannelMessage(message, channel, target);
}

void eqt::chat::ChatComponent::ZoneSendEmote(const std::string &message)
{
	// Titanium Emote_Struct: u32 type (0 = custom text) + char message[1024].
	// eqemu's Handle_OP_Emote size-checks against sizeof(Emote_Struct) and
	// drops anything else, so the packet is always 2 + 1028 bytes. The server
	// truncates the text at 512 characters itself; we only guarantee the NUL.
	//
	// The text goes out with a LEADING SPACE: Handle_OP_Emote builds the
	// broadcast as memcpy(GetName()) + memcpy(in->message) with no separator
	// (zone/client_packet.cpp), so the official client's wire form is
	// " waves" and the observer sees "Testchar waves". T243 live run 2 (no
	// space) produced "Testcharwaves" on the observer.
	std::string wire = " " + message;
	EQ::Net::DynamicPacket p;
	p.Resize(2 + sizeof(EQT::Emote_Struct));
	p.PutUInt16(0, HC_OP_Emote);
	p.PutUInt32(2, 0);
	size_t message_len = std::min(wire.length(), sizeof(EQT::Emote_Struct::message) - 1);
	memcpy((char*)p.Data() + 6, wire.c_str(), message_len);

	if (EverQuest::GetDebugLevel() >= 1) {
		std::cout << fmt::format("Sending emote: '{}'", message) << std::endl;
	}

	eqt::strings::DumpPacket("C->S", HC_OP_Emote, p);
	ctx_.net.SafeQueueZone(p);

	// Local echo. eqemu broadcasts the emote with ignore_sender=true
	// (QueueCloseClients in Handle_OP_Emote), so the actor's own line comes
	// from the client -- as the official client does ("Testchar waves").
	if (ctx_.bridge) {
		eqt::state::ChatMessageData data;
		data.message = ctx_.character + wire.substr(0, message_len);
		data.channelType = CHAT_CHANNEL_EMOTE;
		data.channelName = "emote";
		ctx_.bridge->pushEvent(eqt::state::GameEvent(
			eqt::state::GameEventType::ChatMessage, std::move(data)));
	}
}

void eqt::chat::ChatComponent::ZoneProcessEmote(const EQ::Net::Packet &p)
{
	// T243: inbound OP_Emote. eqemu's Titanium ENCODE(OP_Emote) writes u32
	// type + NUL-terminated message, sized to the text (not 1028), and the
	// message already carries the actor's name ("Testchar waves") --
	// Handle_OP_Emote / ChatChannel_Emotes prepend GetName() server-side.
	// Layout: opcode@0, type u32@2, message cstring@6. Guard is > 6 so the
	// bounds-checked GetCString (T08) always has at least the NUL to read.
	if (p.Length() <= 6) {
		LOG_WARN(MOD_ZONE, "Emote packet too small: {} bytes", p.Length());
		return;
	}

	uint32_t type = p.GetUInt32(2);
	std::string message = p.GetCString(6);

	if (EverQuest::GetDebugLevel() >= 1) {
		std::cout << fmt::format("[CHAT] (emote type {}) {}", type, message) << std::endl;
	}

	// Empty sender on purpose: the text is complete as sent, and the emote
	// formatter (chat_message_buffer.cpp) prints it verbatim in that case.
	if (ctx_.bridge) {
		eqt::state::ChatMessageData data;
		data.message = message;
		data.channelType = CHAT_CHANNEL_EMOTE;
		data.channelName = "emote";
		ctx_.bridge->pushEvent(eqt::state::GameEvent(
			eqt::state::GameEventType::ChatMessage, std::move(data)));
	}
}

bool eqt::chat::ChatComponent::DispatchSlashCommand(const std::string &cmd_line)
{
	// S04: command_registry is pre-allocated in EverQuest constructor,
	// unconditionally as of T29 (see eq.cpp) -- no EQT_HAS_GRAPHICS guard
	// needed here any more. Individual commands that still genuinely need
	// graphics report so themselves (see eq_commands_movement.cpp /
	// eq_commands_spells.cpp T29 notes) rather than being unreachable.
	if (!ctx_.command_registry) {
		return false;
	}
	return ctx_.command_registry->executeCommand(cmd_line);
}

void eqt::chat::ChatComponent::ProcessChatInput(const std::string &input)
{
	// T29: this used to be wrapped entirely in #ifdef EQT_HAS_GRAPHICS, so a
	// headless-compiled build had NO slash commands at all -- slash input
	// was silently dropped and non-slash input still went to /say. That was
	// the "headless hole" named in docs/decisions.md 2026-08-02: the
	// registry itself (and RegisterCommands()) no longer requires graphics,
	// so this is the live chat window's half of the single unified dispatch
	// entry point -- the other half is Application::DispatchSlashCommand,
	// reached from stdin/--script. Both ultimately go through
	// DispatchSlashCommand() above (registry 1) first, so a command typed
	// here and the same command piped to stdin take the same first step.
	if (input.empty()) {
		return;
	}

	// Check if it's a command (starts with /)
	if (input[0] == '/') {
		std::string cmd_line = input.substr(1);  // Remove leading /

		// Try to execute via command registry (registry 1). T29: if that
		// doesn't recognize it, give Application's registries 2/3 (action::
		// CommandProcessor, then the renderer command bridge) the same shot
		// stdin/--script get, via the fallback Application wires in at
		// startup -- see SetUnrecognizedCommandFallback()'s doc comment.
		if (!DispatchSlashCommand(cmd_line) &&
		    !(m_unrecognized_command_fallback && m_unrecognized_command_fallback(cmd_line))) {
			// Command not found anywhere - extract command name for error message
			size_t space_pos = cmd_line.find(' ');
			std::string command = (space_pos != std::string::npos) ?
				cmd_line.substr(0, space_pos) : cmd_line;
			ctx_.out.System(fmt::format("Unknown command: /{}. Type /help for a list of commands.", command));
		}
	} else {
		// No slash - treat as /say
		SendChatMessage(input, "say");
	}
}

void eqt::chat::ChatComponent::ZoneProcessColoredText(const EQ::Net::Packet &p)
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
		if (ctx_.bridge) {
			eqt::state::ChatMessageData data;
			data.message = message;
			data.channelType = static_cast<uint32_t>(eqt::ui::ChatChannel::Spell);
			data.channelName = "spell";
			ctx_.bridge->pushEvent(eqt::state::GameEvent(
				eqt::state::GameEventType::SystemMessage, std::move(data)));
		}
		if (EverQuest::GetDebugLevel() >= 1) {
			std::cout << message << std::endl;
		}
	}
}

void eqt::chat::ChatComponent::ZoneProcessFormattedMessage(const EQ::Net::Packet &p)
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
	std::string tmpl = ctx_.strings.Db().getString(string_id);

	// Format the message using the template and arguments
	std::string formatted_text;
	if (!tmpl.empty()) {
		formatted_text = ctx_.strings.GetFormattedStringMessage(string_id, args);
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
	if (ctx_.bridge) {
		eqt::state::ChatMessageData data;
		data.sender = sender;
		data.message = formatted_text;
		data.channelType = static_cast<uint32_t>(channel);
		data.channelName = isSystemMessage ? "system" : "npc";
		ctx_.bridge->pushEvent(eqt::state::GameEvent(
			eqt::state::GameEventType::ChatMessage, std::move(data)));
	}

	LOG_DEBUG(MOD_ZONE, "[FormattedMessage] channel={}, sender='{}', text='{}'",
		static_cast<int>(channel), sender, formatted_text);
}

void eqt::chat::ChatComponent::ZoneProcessSimpleMessage(const EQ::Net::Packet &p)
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
	std::string tmpl = ctx_.strings.GetStringMessage(string_id);

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
		if ((string_id == 303 || string_id == 12113) && !ctx_.game_state.combat().lastSlainEntityName().empty()) {
			target_name = ctx_.game_state.combat().lastSlainEntityName();
		}
		// Otherwise try current combat target
		else if (ctx_.combat_manager && ctx_.combat_manager->GetTargetId() != 0) {
			auto it = ctx_.entities.find(ctx_.combat_manager->GetTargetId());
			if (it != ctx_.entities.end()) {
				target_name = EQT::toDisplayName(it->second.name);
			}
		}

		args.push_back(target_name);
	}

	// Format the message with arguments
	std::string message_text;
	if (!args.empty()) {
		message_text = ctx_.strings.GetFormattedStringMessage(string_id, args);
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
	if (ctx_.bridge) {
		eqt::state::ChatMessageData data;
		data.message = message_text;
		data.channelType = static_cast<uint32_t>(channel);
		data.channelName = "system";
		ctx_.bridge->pushEvent(eqt::state::GameEvent(
			eqt::state::GameEventType::ChatMessage, std::move(data)));
	}

	LOG_DEBUG(MOD_ZONE, "[SimpleMessage] channel={}, text='{}'",
		static_cast<int>(channel), message_text);
}
