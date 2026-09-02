#include "client/eq.h"
#include "client/willeq_database.h"
#include "client/pathfinder_interface.h"
#ifdef EQT_HAS_NAVMESH
#include "client/pathfinder_nav_mesh.h"
#endif
#include "client/hc_map.h"
#include "client/combat.h"
#include "client/corpse_predicate.h"  // T185: player-corpse rename on the death path
#include "client/trade_manager.h"
#include "client/world_object.h"
#include "client/spell/spell_manager.h"
#include "client/spell/buff_manager.h"
#include "client/spell/spell_effects.h"
#include "client/skill/skill_manager.h"
#include "client/zone_lines.h"
#include "client/formatted_message.h"
#include "common/logging.h"
#include "common/packet_structs.h"
#include "common/name_utils.h"
#include "common/performance_metrics.h"

// P28: inventory/chat/hotbar/command-registry are plain data-model headers
// with no renderer dependency (used unconditionally below: m_inventory_manager,
// m_hotbar_model, m_command_registry are complete-type-required here). Only
// hotkey_manager.h (pulls in <irrlicht.h>) and json_config.h/loading_thread.h
// (renderer-only helpers) stay gated.
#include "client/inventory/inventory_manager.h"
#include "client/inventory/inventory_constants.h"
#include "client/inventory/item_instance.h"
#include "client/chat/chat_message_buffer.h"
#include "client/hotbar/hotbar_model.h"
#include "client/graphics/ui/command_registry.h"
#ifdef EQT_HAS_GRAPHICS
#include "client/input/hotkey_manager.h"
#include "common/util/json_config.h"
#endif

#include "client/bridge/game_state_bridge.h"
#ifdef EQT_HAS_GRAPHICS
#include "client/graphics/loading_thread.h"
#endif
#include "client/bridge/irrlicht_bridge.h"

#ifdef WITH_AUDIO
#include "client/audio/audio_manager.h"
#include "client/audio/zone_audio_manager.h"
#include "client/audio/sfx_manager.h"
#include "client/audio/sound_assets.h"
#include "client/audio/door_sounds.h"
#include "client/audio/water_sounds.h"
#endif

#include "common/net/posix_udp_transport.h"
#include "client/animation_constants.h"
#include <array>
#include <iomanip>
#include <sstream>
#include <ctime>
#include <algorithm>
#include <vector>
#include <cmath>
#include <cstring>
#include <cstdio>
#include <thread>
#include <chrono>
#include <map>
#include <tuple>
#include <fstream>
#ifdef __linux__
#include <unistd.h>
#endif

#ifndef M_PI
#define M_PI 3.14159265358979323846
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

