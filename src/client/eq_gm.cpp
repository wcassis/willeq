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

// T217: this used to call SendSpawnAppearance(AT_GM, ...), i.e. it went out
// as OP_SpawnAppearance type 20. eqemu has no case for that type and dropped
// it -- `Unknown SpawnAppearance type: 0x0014` in the zone log (T148 live
// evidence, review/T148/) -- so /hideme could never reach the server's
// handler. The handler, Client::Handle_OP_GMHideMe (zone/client_packet.cpp),
// listens on OP_GMHideMe = 0x15b2 (patch_Titanium.conf:132) and demands an
// 8-byte SpawnAppearance_Struct body, of which it reads ONLY `parameter`:
//
//     SetHideMe(!sa->parameter);
//
// So parameter is a VISIBILITY flag, not a hide flag: 0 = hide me, 1 = show
// me. spawn_id/type are filled the way the SpawnAppearance path fills them
// (ours, AT_GM) for the reader's benefit; the server ignores both.
void EverQuest::SendGMHideMe(bool hide)
{
	EQ::Net::DynamicPacket packet;
	packet.PutUInt16(0, HC_OP_GMHideMe);

	EQT::SpawnAppearance_Struct sa;
	memset(&sa, 0, sizeof(sa));
	sa.spawn_id = m_my_spawn_id;
	sa.type = AT_GM;
	sa.parameter = hide ? 0 : 1;
	packet.PutData(2, &sa, sizeof(sa));

	// T217 test seam (same shape as T167's face-change one): the bytes we
	// actually built, captured before the connection gate.
	m_test_last_gm_hideme.sent = true;
	m_test_last_gm_hideme.bytes.assign(
		static_cast<const uint8_t*>(packet.Data()),
		static_cast<const uint8_t*>(packet.Data()) + packet.Length());

	if (m_zone_connection && m_zone_connected) {
		m_zone_connection->QueuePacket(packet);
		LOG_INFO(MOD_MAIN, "GM hide: {}", hide ? "ON" : "OFF");
	}
}

// T148: this sender wrote a bare variable-length string where the server
// demands a FIXED 512-byte body. `Handle_OP_GMEmoteZone`
// (zone/client_packet.cpp:6605) checks `app->size != sizeof(GMEmoteZone_Struct)`
// -- and GMEmoteZone_Struct is a single `char text[512]`
// (common/eq_packet_structs.h:2779) -- so every /emotezone was rejected
// outright. Measured, not inferred: T148's first live run produced
//   Zone | Error | Handle_OP_GMEmoteZone Wrong size: OP_GMEmoteZone,
//                  [size=32,] expected [512]
// in the zone server's own log, and no client in the zone (including the
// sender) saw the emote. This was invisible before T148 because the sender
// had zero callers -- exactly the shape T146 warned about, where a local
// "sent OK" log certifies a dead path.
//
// The 512-byte body is built inline rather than by adding a struct to
// packet_structs.h: it is one fixed-size char array with no field layout to
// get wrong, and keeping it local avoids touching a header the parallel
// struct-audit units share.
void EverQuest::SendGMEmoteZone(const std::string& text)
{
	EQ::Net::DynamicPacket packet;
	packet.PutUInt16(0, HC_OP_GMEmoteZone);

	char body[512];
	memset(body, 0, sizeof(body));
	// 511 + the NUL the memset already guarantees. eqemu strtok()s and
	// entity_list.Message()es this buffer, so it must be NUL-terminated.
	strncpy(body, text.c_str(), sizeof(body) - 1);
	packet.PutData(2, body, sizeof(body));

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
