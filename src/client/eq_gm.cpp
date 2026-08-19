// EverQuest GM command cluster.
//
// Split out of eq.cpp (Batch F, unit F04): GM zone request/summon/goto/find/
// kick/kill/hideme/emotezone/lastname senders plus their Process* packet
// handlers. Moved verbatim, no behavior change. See
// refactor/F04_ucs_guild_gm.md.

#include "client/eq.h"
#include "common/logging.h"
#include "common/packet_structs.h"
#include <cstring>

void EverQuest::SendGMZoneRequest(const std::string& charname, uint16_t zone_id)
{
	EQ::Net::DynamicPacket packet;
	packet.PutUInt16(0, HC_OP_GMZoneRequest);

	EQT::GMZoneRequest_Struct gm;
	memset(&gm, 0, sizeof(gm));
	strncpy(gm.charname, charname.c_str(), 63);
	gm.zone_id = zone_id;
	packet.PutData(2, &gm, sizeof(gm));

	if (m_zone_connection && m_zone_connected) {
		m_zone_connection->QueuePacket(packet);
		LOG_INFO(MOD_MAIN, "GM zone request: {} to zone {}", charname, zone_id);
	}
}

void EverQuest::SendGMSummon(const std::string& charname)
{
	EQ::Net::DynamicPacket packet;
	packet.PutUInt16(0, HC_OP_GMSummon);

	EQT::GMSummon_Struct gm;
	memset(&gm, 0, sizeof(gm));
	strncpy(gm.charname, charname.c_str(), 63);
	strncpy(gm.gmname, m_character.c_str(), 63);
	packet.PutData(2, &gm, sizeof(gm));

	if (m_zone_connection && m_zone_connected) {
		m_zone_connection->QueuePacket(packet);
		LOG_INFO(MOD_MAIN, "GM summon: {}", charname);
	}
}

void EverQuest::SendGMGoto(const std::string& charname)
{
	EQ::Net::DynamicPacket packet;
	packet.PutUInt16(0, HC_OP_GMGoto);

	EQT::GMSummon_Struct gm;
	memset(&gm, 0, sizeof(gm));
	strncpy(gm.charname, charname.c_str(), 63);
	strncpy(gm.gmname, m_character.c_str(), 63);
	packet.PutData(2, &gm, sizeof(gm));

	if (m_zone_connection && m_zone_connected) {
		m_zone_connection->QueuePacket(packet);
		LOG_INFO(MOD_MAIN, "GM goto: {}", charname);
	}
}

void EverQuest::SendGMFind(const std::string& charname)
{
	EQ::Net::DynamicPacket packet;
	packet.PutUInt16(0, HC_OP_GMFind);

	EQT::GMSummon_Struct gm;
	memset(&gm, 0, sizeof(gm));
	strncpy(gm.charname, charname.c_str(), 63);
	strncpy(gm.gmname, m_character.c_str(), 63);
	packet.PutData(2, &gm, sizeof(gm));

	if (m_zone_connection && m_zone_connected) {
		m_zone_connection->QueuePacket(packet);
		LOG_DEBUG(MOD_MAIN, "GM find: {}", charname);
	}
}

// GMKick_Struct is 129 bytes and serves both OP_GMKick and OP_GMKill (eqemu's
// GMKill_Struct has the identical three fields). Until T145 ours was 132 --
// an invented `padding[3]` -- and `Handle_OP_GMKick`
// (zone/client_packet.cpp:6729) does a BARE `return` on a size mismatch with
// no log line, so /kick left zero server-side trace while logging local
// success here. These two senders use the correct 1-arg
// `m_zone_connection->QueuePacket(packet)` convention (the packet carries its
// own opcode); they needed no change beyond the struct fix.
void EverQuest::SendGMKick(const std::string& charname)
{
	EQ::Net::DynamicPacket packet;
	packet.PutUInt16(0, HC_OP_GMKick);

	EQT::GMKick_Struct gm;
	memset(&gm, 0, sizeof(gm));
	strncpy(gm.name, charname.c_str(), 63);
	strncpy(gm.gmname, m_character.c_str(), 63);
	packet.PutData(2, &gm, sizeof(gm));

	if (m_zone_connection && m_zone_connected) {
		m_zone_connection->QueuePacket(packet);
		LOG_INFO(MOD_MAIN, "GM kick: {}", charname);
	}
}

void EverQuest::SendGMKill(const std::string& charname)
{
	EQ::Net::DynamicPacket packet;
	packet.PutUInt16(0, HC_OP_GMKill);

	EQT::GMKick_Struct gm;  // Same struct as kick
	memset(&gm, 0, sizeof(gm));
	strncpy(gm.name, charname.c_str(), 63);
	strncpy(gm.gmname, m_character.c_str(), 63);
	packet.PutData(2, &gm, sizeof(gm));

	if (m_zone_connection && m_zone_connected) {
		m_zone_connection->QueuePacket(packet);
		LOG_INFO(MOD_MAIN, "GM kill: {}", charname);
	}
}

void EverQuest::SendGMHideMe(bool hide)
{
	// Uses SpawnAppearance
	SendSpawnAppearance(AT_GM, hide ? 1 : 0);
	LOG_INFO(MOD_MAIN, "GM hide: {}", hide ? "ON" : "OFF");
}

void EverQuest::SendGMEmoteZone(const std::string& text)
{
	EQ::Net::DynamicPacket packet;
	packet.PutUInt16(0, HC_OP_GMEmoteZone);
	packet.PutString(2, text);

	if (m_zone_connection && m_zone_connected) {
		m_zone_connection->QueuePacket(packet);
		LOG_INFO(MOD_MAIN, "GM zone emote: {}", text);
	}
}

void EverQuest::SendGMLastName(const std::string& charname, const std::string& lastname)
{
	EQ::Net::DynamicPacket packet;
	packet.PutUInt16(0, HC_OP_GMLastName);

	EQT::GMLastName_Struct gm;
	memset(&gm, 0, sizeof(gm));
	strncpy(gm.name, charname.c_str(), 63);
	strncpy(gm.gmname, m_character.c_str(), 63);
	strncpy(gm.lastname, lastname.c_str(), 63);
	packet.PutData(2, &gm, sizeof(gm));

	if (m_zone_connection && m_zone_connected) {
		m_zone_connection->QueuePacket(packet);
		LOG_INFO(MOD_MAIN, "GM set lastname for {}: {}", charname, lastname);
	}
}

void EverQuest::ZoneProcessGMZoneRequest(const EQ::Net::Packet &p)
{
	if (p.Length() < 2 + sizeof(EQT::GMZoneRequest_Struct)) {
		return;
	}

	const EQT::GMZoneRequest_Struct* gm = reinterpret_cast<const EQT::GMZoneRequest_Struct*>(
		static_cast<const uint8_t*>(p.Data()) + 2);

	if (gm->success) {
		LOG_INFO(MOD_MAIN, "GM zone request approved for zone {}", gm->zone_id);
	} else {
		LOG_WARN(MOD_MAIN, "GM zone request denied");
		AddChatSystemMessage("Zone request denied.");
	}
}

void EverQuest::ZoneProcessGMFind(const EQ::Net::Packet &p)
{
	if (p.Length() < 2 + sizeof(EQT::GMSummon_Struct)) {
		return;
	}

	const EQT::GMSummon_Struct* gm = reinterpret_cast<const EQT::GMSummon_Struct*>(
		static_cast<const uint8_t*>(p.Data()) + 2);

	std::string charname(gm->charname, strnlen(gm->charname, 64));
	LOG_INFO(MOD_MAIN, "GM find result: {} in zone {} at ({}, {}, {})",
		charname, gm->zoneID, gm->x, gm->y, gm->z);
	AddChatSystemMessage(fmt::format("{} is in zone {} at ({}, {}, {})",
		charname, gm->zoneID, gm->x, gm->y, gm->z));
}

void EverQuest::ZoneProcessGMSummon(const EQ::Net::Packet &p)
{
	if (p.Length() < 2 + sizeof(EQT::GMSummon_Struct)) {
		return;
	}

	const EQT::GMSummon_Struct* gm = reinterpret_cast<const EQT::GMSummon_Struct*>(
		static_cast<const uint8_t*>(p.Data()) + 2);

	if (gm->success) {
		LOG_INFO(MOD_MAIN, "GM summon successful");
	} else {
		LOG_WARN(MOD_MAIN, "GM summon failed");
		AddChatSystemMessage("Summon failed.");
	}
}
