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


// Zone packet senders
void eqt::zone::ZoneComponent::ZoneSendZoneEntry()
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
	size_t name_len = ctx_.character.length();
	if (name_len > 63) name_len = 63;
	memcpy((char*)p.Data() + name_offset, ctx_.character.c_str(), name_len);
	
	// Fill rest of name field with zeros (64 bytes total for name)
	for (size_t i = name_offset + name_len; i < name_offset + 64; i++) {
		p.PutUInt8(i, 0);
	}
	
	EverQuest::DumpPacket("C->S", HC_OP_ZoneEntry, p);
	if (ctx_.zone_connection) {
		ctx_.zone_connection->QueuePacket(p);
	} else {
		++owner_.m_replay_suppressed_sends;
	}
	m_zone_entry_sent = true;
	
	// Clear movement history on zone entry
	m_movement_history.clear();
	m_last_movement_history_send = 0;  // Reset the timer to prevent immediate sends
	
	// Add zone entry to movement history
	MovementHistoryEntry zone_entry;
	zone_entry.x = owner_.PosX();
	zone_entry.y = owner_.PosY();
	zone_entry.z = owner_.PosZ();
	zone_entry.type = 4; // ZoneLine type
	zone_entry.timestamp = static_cast<uint32_t>(std::time(nullptr));
	m_movement_history.push_back(zone_entry);
}

void eqt::zone::ZoneComponent::ZoneSendReqClientSpawn()
{
	EQ::Net::DynamicPacket p;
	p.Resize(2);
	
	p.PutUInt16(0, HC_OP_ReqClientSpawn);
	
	EverQuest::DumpPacket("C->S", HC_OP_ReqClientSpawn, p);
	if (ctx_.zone_connection) {
		ctx_.zone_connection->QueuePacket(p);
	} else {
		++owner_.m_replay_suppressed_sends;
	}
}

void eqt::zone::ZoneComponent::ZoneSendClientReady()
{
	EQ::Net::DynamicPacket p;
	p.Resize(2);
	
	p.PutUInt16(0, HC_OP_ClientReady);
	
	LOG_DEBUG(MOD_ZONE, "Sending OP_ClientReady");

	EverQuest::DumpPacket("C->S", HC_OP_ClientReady, p);
	if (ctx_.zone_connection) {
		ctx_.zone_connection->QueuePacket(p);
	} else {
		++owner_.m_replay_suppressed_sends;
	}
	ctx_.game_state.world().setClientReady(true);  // T286: only store
}

void eqt::zone::ZoneComponent::ZoneSendSetServerFilter()
{
	// Set server filter for what types of messages we want
	// SetServerFilter_Struct is uint32 filters[29] = 116 bytes
	EQ::Net::DynamicPacket p;
	p.Resize(118);  // 2 bytes opcode + 116 bytes data
	
	p.PutUInt16(0, HC_OP_SetServerFilter);
	
	// T243: show everything. The slot conventions are NOT uniform (see
	// EQT::ServerFilterSlot): eleven slots are "0=hide, 1=show" and eqemu's
	// Filter0 macro shows ONLY for the exact value 1; the rest are "0=show".
	// The previous payload (0xFFFFFFFF in every slot, "all enabled") hid all
	// 28 filters server-side -- FilterSocials among them, which is why the
	// OP_Emote broadcast never reached an observing willeq (T243 live run 1).
	static const bool kShowIsOne[EQT::FILTER_COUNT] = {
		false,                                            // 0  none
		true, true, true, true, true, true, true,         // 1-7  guild/socials/group/shouts/auctions/ooc/badwords
		false, false, false, false, false, false,         // 8-13 pc spells .. spell damage
		true, true, true, true,                           // 14-17 my misses / others miss / others hit / missed me
		false, false, false, false, false, false,         // 18-23 damage shields .. pet spells
		false, false, false, false, false                 // 24-28 heal over time .. bard songs on pets
	};
	for (size_t i = 0; i < EQT::FILTER_COUNT; i++) {
		p.PutUInt32(2 + (i * 4), kShowIsOne[i] ? 1u : 0u);
	}
	
	LOG_DEBUG(MOD_ZONE, "Sending OP_SetServerFilter");

	EverQuest::DumpPacket("C->S", HC_OP_SetServerFilter, p);
	if (ctx_.zone_connection) {
		ctx_.zone_connection->QueuePacket(p);
	} else {
		++owner_.m_replay_suppressed_sends;
	}
	m_server_filter_sent = true;
}

void eqt::zone::ZoneComponent::ZoneSendStreamIdentify()
{
	// Send stream identify with Titanium zone opcode
	EQ::Net::DynamicPacket p;
	p.Resize(2);
	
	p.PutUInt16(0, HC_OP_ZoneEntry);  // 0x7213 - TitaniumZone
	
	LOG_DEBUG(MOD_ZONE, "Sending stream identify with opcode {:#06x} (Titanium_zone)", 0x7213);
	
	ctx_.zone_connection->QueuePacket(p, 0, false);  // Send unreliable
}

void eqt::zone::ZoneComponent::ZoneSendAckPacket()
{
	EQ::Net::DynamicPacket p;
	p.Resize(6);
	
	p.PutUInt16(0, HC_OP_AckPacket);
	p.PutUInt32(2, 0);  // sequence or something
	
	EverQuest::DumpPacket("C->S", HC_OP_AckPacket, p);
	if (ctx_.zone_connection) {
		ctx_.zone_connection->QueuePacket(p);
	} else {
		++owner_.m_replay_suppressed_sends;
	}
}

void eqt::zone::ZoneComponent::ZoneSendReqNewZone()
{
	EQ::Net::DynamicPacket p;
	p.Resize(2);
	
	p.PutUInt16(0, HC_OP_ReqNewZone);
	
	EverQuest::DumpPacket("C->S", HC_OP_ReqNewZone, p);
	if (ctx_.zone_connection) {
		ctx_.zone_connection->QueuePacket(p);
	} else {
		++owner_.m_replay_suppressed_sends;
	}
	m_req_new_zone_sent = true;
}

void eqt::zone::ZoneComponent::ZoneSendSendAATable()
{
	EQ::Net::DynamicPacket p;
	p.Resize(2);
	
	p.PutUInt16(0, HC_OP_SendAATable);
	
	EverQuest::DumpPacket("C->S", HC_OP_SendAATable, p);
	if (ctx_.zone_connection) {
		ctx_.zone_connection->QueuePacket(p);
	} else {
		++owner_.m_replay_suppressed_sends;
	}
	m_aa_table_sent = true;
}

void eqt::zone::ZoneComponent::ZoneSendUpdateAA()
{
	EQ::Net::DynamicPacket p;
	p.Resize(12);  // Typical UpdateAA size
	
	p.PutUInt16(0, HC_OP_UpdateAA);
	// Rest is AA data - zeros for now
	
	EverQuest::DumpPacket("C->S", HC_OP_UpdateAA, p);
	if (ctx_.zone_connection) {
		ctx_.zone_connection->QueuePacket(p);
	} else {
		++owner_.m_replay_suppressed_sends;
	}
	m_update_aa_sent = true;
}

void eqt::zone::ZoneComponent::ZoneSendSendTributes()
{
	EQ::Net::DynamicPacket p;
	p.Resize(2);
	
	p.PutUInt16(0, HC_OP_SendTributes);
	
	EverQuest::DumpPacket("C->S", HC_OP_SendTributes, p);
	if (ctx_.zone_connection) {
		ctx_.zone_connection->QueuePacket(p);
	} else {
		++owner_.m_replay_suppressed_sends;
	}
	m_tributes_sent = true;
}

void eqt::zone::ZoneComponent::ZoneSendRequestGuildTributes()
{
	EQ::Net::DynamicPacket p;
	p.Resize(2);
	
	p.PutUInt16(0, HC_OP_RequestGuildTributes);
	
	EverQuest::DumpPacket("C->S", HC_OP_RequestGuildTributes, p);
	if (ctx_.zone_connection) {
		ctx_.zone_connection->QueuePacket(p);
	} else {
		++owner_.m_replay_suppressed_sends;
	}
	m_guild_tributes_sent = true;
}

void eqt::zone::ZoneComponent::ZoneSendSendExpZonein()
{
	EQ::Net::DynamicPacket p;
	p.Resize(2);
	
	p.PutUInt16(0, HC_OP_SendExpZonein);
	
	EverQuest::DumpPacket("C->S", HC_OP_SendExpZonein, p);
	if (ctx_.zone_connection) {
		ctx_.zone_connection->QueuePacket(p);
	} else {
		++owner_.m_replay_suppressed_sends;
	}
	m_exp_zonein_sent = true;
}

