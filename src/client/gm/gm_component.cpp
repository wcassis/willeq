// EverQuest GM command cluster.
//
// Split out of eq.cpp (Batch F, unit F04): GM zone request/summon/goto/find/
// kick/kill/hideme/emotezone/lastname senders plus their Process* packet
// handlers. Moved verbatim, no behavior change. See
// refactor/F04_ucs_guild_gm.md.
//
// K02 (Batch K, refactor/K_component_extraction.md): moved again, from
// src/client/eq_gm.cpp to here, and the bodies rebased from EverQuest onto
// eqt::gm::GmComponent. Still verbatim -- only the substitutions §Mechanism 4
// allows (m_zone_connection/m_game_state/m_character -> ctx_.*;
// PosX()/PosY()/PosZ()/MySpawnId()/AddChatSystemMessage() -> owner_.*, they
// stay on EverQuest). m_test_last_gm_hideme/m_test_last_gm_summon moved WITH
// this family, so those two references need no prefix at all. See the K02
// Review.

#include "client/eq.h"
#include "client/gm/gm_component.h"
#include "common/logging.h"
#include "common/packet_structs.h"
#include <cstring>

void eqt::gm::GmComponent::SendGMZoneRequest(const std::string& charname, uint16_t zone_id)
{
	EQ::Net::DynamicPacket packet;
	packet.PutUInt16(0, HC_OP_GMZoneRequest);

	EQT::GMZoneRequest_Struct gm;
	memset(&gm, 0, sizeof(gm));
	strncpy(gm.charname, charname.c_str(), 63);
	gm.zone_id = zone_id;
	packet.PutData(2, &gm, sizeof(gm));

	if (ctx_.zone_connection && ctx_.game_state.world().isZoneConnected()) {
		ctx_.net.SafeQueueZone(packet);
		LOG_INFO(MOD_MAIN, "GM zone request: {} to zone {}", charname, zone_id);
	}
}

// T229: eqemu READS the coordinates in this body. Client::OPGMSummon
// (zone/client_process.cpp:1877) does
//     MovePC(zone, inst, gms->x, gms->y, gms->z, GetHeading(), true)
// for an in-zone player target and copies x/y/z into ServerZonePlayer_Struct
// for a cross-zone one; only a non-client mob (GMMove to the GM's own spot)
// and a corpse ignore them. Until T229 this sender memset the body and filled
// only the names, so every /summon relocated the target to (0, 0, 0) -- the
// server's own log in T148's live run reads `ZonePC Client [Testbuddy] ...
// x [0] y [0] z [0]` (review/T148/eqemu_zone_arena_server_log.txt). The
// official client sends the GM's current position, which is what a summon
// means; we do the same.
//
// The fields are FLOATS (eqemu common/eq_packet_structs.h:1949 -- `float y;
// float x; float z;`), and they are in SERVER coordinates. Our internal
// client x = server y, client y = server x (the T52 convention SendPositionUpdate and
// SendZoneChange both invert on the way out), so the wire's y-field gets our
// client x and the wire's x-field gets our client y.
void eqt::gm::GmComponent::SendGMSummon(const std::string& charname)
{
	EQ::Net::DynamicPacket packet;
	packet.PutUInt16(0, HC_OP_GMSummon);

	EQT::GMSummon_Struct gm;
	memset(&gm, 0, sizeof(gm));
	strncpy(gm.charname, charname.c_str(), 63);
	strncpy(gm.gmname, ctx_.character.c_str(), 63);
	gm.y = ctx_.self.PosX();  // server's y = our x
	gm.x = ctx_.self.PosY();  // server's x = our y
	gm.z = ctx_.self.PosZ();
	packet.PutData(2, &gm, sizeof(gm));

	// T229 test seam (same shape as T217's hideme one): the bytes we actually
	// built, captured before the connection gate.
	m_test_last_gm_summon.sent = true;
	m_test_last_gm_summon.bytes.assign(
		static_cast<const uint8_t*>(packet.Data()),
		static_cast<const uint8_t*>(packet.Data()) + packet.Length());

	if (ctx_.zone_connection && ctx_.game_state.world().isZoneConnected()) {
		ctx_.net.SafeQueueZone(packet);
		LOG_INFO(MOD_MAIN, "GM summon: {} to ({:.1f}, {:.1f}, {:.1f})", charname, ctx_.self.PosX(), ctx_.self.PosY(), ctx_.self.PosZ());
	}
}

void eqt::gm::GmComponent::SendGMGoto(const std::string& charname)
{
	EQ::Net::DynamicPacket packet;
	packet.PutUInt16(0, HC_OP_GMGoto);

	EQT::GMSummon_Struct gm;
	memset(&gm, 0, sizeof(gm));
	strncpy(gm.charname, charname.c_str(), 63);
	strncpy(gm.gmname, ctx_.character.c_str(), 63);
	packet.PutData(2, &gm, sizeof(gm));

	if (ctx_.zone_connection && ctx_.game_state.world().isZoneConnected()) {
		ctx_.net.SafeQueueZone(packet);
		LOG_INFO(MOD_MAIN, "GM goto: {}", charname);
	}
}

void eqt::gm::GmComponent::SendGMFind(const std::string& charname)
{
	EQ::Net::DynamicPacket packet;
	packet.PutUInt16(0, HC_OP_GMFind);

	EQT::GMSummon_Struct gm;
	memset(&gm, 0, sizeof(gm));
	strncpy(gm.charname, charname.c_str(), 63);
	strncpy(gm.gmname, ctx_.character.c_str(), 63);
	packet.PutData(2, &gm, sizeof(gm));

	if (ctx_.zone_connection && ctx_.game_state.world().isZoneConnected()) {
		ctx_.net.SafeQueueZone(packet);
		LOG_DEBUG(MOD_MAIN, "GM find: {}", charname);
	}
}

// GMKick_Struct is 129 bytes and serves both OP_GMKick and OP_GMKill (eqemu's
// GMKill_Struct has the identical three fields). Until T145 ours was 132 --
// an invented `padding[3]` -- and `Handle_OP_GMKick`
// (zone/client_packet.cpp:6729) does a BARE `return` on a size mismatch with
// no log line, so /kick left zero server-side trace while logging local
// success here. These two senders use the correct 1-arg send convention (the
// packet carries its own opcode); they needed no change beyond the struct fix.
// T378 moved the send itself from ctx_.zone_connection->QueuePacket() to
// ctx_.net.SafeQueueZone(), which is what the recorder and the T268 seam see.
void eqt::gm::GmComponent::SendGMKick(const std::string& charname)
{
	EQ::Net::DynamicPacket packet;
	packet.PutUInt16(0, HC_OP_GMKick);

	EQT::GMKick_Struct gm;
	memset(&gm, 0, sizeof(gm));
	strncpy(gm.name, charname.c_str(), 63);
	strncpy(gm.gmname, ctx_.character.c_str(), 63);
	packet.PutData(2, &gm, sizeof(gm));

	if (ctx_.zone_connection && ctx_.game_state.world().isZoneConnected()) {
		ctx_.net.SafeQueueZone(packet);
		LOG_INFO(MOD_MAIN, "GM kick: {}", charname);
	}
}

void eqt::gm::GmComponent::SendGMKill(const std::string& charname)
{
	EQ::Net::DynamicPacket packet;
	packet.PutUInt16(0, HC_OP_GMKill);

	EQT::GMKick_Struct gm;  // Same struct as kick
	memset(&gm, 0, sizeof(gm));
	strncpy(gm.name, charname.c_str(), 63);
	strncpy(gm.gmname, ctx_.character.c_str(), 63);
	packet.PutData(2, &gm, sizeof(gm));

	if (ctx_.zone_connection && ctx_.game_state.world().isZoneConnected()) {
		ctx_.net.SafeQueueZone(packet);
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
void eqt::gm::GmComponent::SendGMHideMe(bool hide)
{
	EQ::Net::DynamicPacket packet;
	packet.PutUInt16(0, HC_OP_GMHideMe);

	EQT::SpawnAppearance_Struct sa;
	memset(&sa, 0, sizeof(sa));
	sa.spawn_id = ctx_.self.MySpawnId();
	sa.type = AT_GM;
	sa.parameter = hide ? 0 : 1;
	packet.PutData(2, &sa, sizeof(sa));

	// T217 test seam (same shape as T167's face-change one): the bytes we
	// actually built, captured before the connection gate.
	m_test_last_gm_hideme.sent = true;
	m_test_last_gm_hideme.bytes.assign(
		static_cast<const uint8_t*>(packet.Data()),
		static_cast<const uint8_t*>(packet.Data()) + packet.Length());

	if (ctx_.zone_connection && ctx_.game_state.world().isZoneConnected()) {
		ctx_.net.SafeQueueZone(packet);
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
void eqt::gm::GmComponent::SendGMEmoteZone(const std::string& text)
{
	EQ::Net::DynamicPacket packet;
	packet.PutUInt16(0, HC_OP_GMEmoteZone);

	char body[512];
	memset(body, 0, sizeof(body));
	// 511 + the NUL the memset already guarantees. eqemu strtok()s and
	// entity_list.Message()es this buffer, so it must be NUL-terminated.
	strncpy(body, text.c_str(), sizeof(body) - 1);
	packet.PutData(2, body, sizeof(body));

	if (ctx_.zone_connection && ctx_.game_state.world().isZoneConnected()) {
		ctx_.net.SafeQueueZone(packet);
		LOG_INFO(MOD_MAIN, "GM zone emote: {}", text);
	}
}

void eqt::gm::GmComponent::SendGMLastName(const std::string& charname, const std::string& lastname)
{
	EQ::Net::DynamicPacket packet;
	packet.PutUInt16(0, HC_OP_GMLastName);

	EQT::GMLastName_Struct gm;
	memset(&gm, 0, sizeof(gm));
	strncpy(gm.name, charname.c_str(), 63);
	strncpy(gm.gmname, ctx_.character.c_str(), 63);
	strncpy(gm.lastname, lastname.c_str(), 63);
	packet.PutData(2, &gm, sizeof(gm));

	if (ctx_.zone_connection && ctx_.game_state.world().isZoneConnected()) {
		ctx_.net.SafeQueueZone(packet);
		LOG_INFO(MOD_MAIN, "GM set lastname for {}: {}", charname, lastname);
	}
}

void eqt::gm::GmComponent::ZoneProcessGMZoneRequest(const EQ::Net::Packet &p)
{
	if (p.Length() < 2 + sizeof(EQT::GMZoneRequest_Struct)) {
		return;
	}

	const EQT::GMZoneRequest_Struct* gm = reinterpret_cast<const EQT::GMZoneRequest_Struct*>(
		static_cast<const uint8_t*>(p.Data()) + 2);

	if (gm->success) {
		LOG_INFO(MOD_MAIN, "GM zone request approved for zone {}", gm->zone_id);

		// B1088 (#408): an APPROVED GM zone request is the official client's
		// cue to initiate the transition itself -- eqemu's
		// Handle_OP_GMZoneRequest (akk-stack code/zone/client_packet.cpp:7066)
		// only replies success plus the destination zone's safe coordinates
		// and never moves the character; the reply IS the approval the
		// client acts on. The client answers with OP_ZoneChange (the same
		// handshake death-to-bind performs, player_state.cpp:464); the
		// server's OP_ZoneChange approval (ZoneProcessZoneChange) then
		// stores the pending zone, refreshes the loading screen's
		// "Entering <long name>..." line, and tears the old zone down.
		//
		// The reply's x/y/z are the TARGET zone's safe coords in WIRE order
		// (server x first) -- swap x/y into our internal order, exactly as
		// ZonePlayerToBind does (player_state.cpp:401-405). The server
		// decides the GM zone's landing itself, so these carry wire
		// fidelity only (T52's lesson); the swap is the consistent choice
		// anyway.
		{
			float dest_x = gm->y;   // our x = server y
			float dest_y = gm->x;   // our y = server x
			float dest_z = gm->z;
			float dest_heading = gm->heading;
			ctx_.game_state.world().setPendingZone(
				static_cast<uint32_t>(gm->zone_id), dest_x, dest_y, dest_z, dest_heading);
			ctx_.Zone().RequestZoneChange(
				static_cast<uint16_t>(gm->zone_id), dest_x, dest_y, dest_z, dest_heading);
			ctx_.out.System("Zoning to zone " + std::to_string(gm->zone_id) + "...");
		}
	} else {
		LOG_WARN(MOD_MAIN, "GM zone request denied");
		ctx_.out.System("Zone request denied.");
	}
}

// T229: the reply is a GMFind_Struct (eqemu common/eq_packet_structs.h:1960
// -- `float x; float y; float z;`, x FIRST), not the GMSummon_Struct this
// used to decode it through. Handle_OP_GMFind (zone/client_packet.cpp:6640)
// echoes charname/gmname and, when the target is in the GM's zone, sets
// success=1, zoneID and the target's GetX()/GetY()/GetZ(); an out-of-zone or
// unknown name comes back success=0 with zeroed coords (it does NOT ask
// world). T148's live run printed `at (-998490112, 1125908480, 1112276992)`
// -- 0xC47C4000 / 0x431C0000 / 0x424C0000, i.e. server y=-1009 x=156 z=51,
// read as int32 and in the wrong slots.
//
// The chat line is printed in the same convention /loc uses for the GM's own
// position (our internal x = server y, y = server x), so a GM can compare the
// two directly.
void eqt::gm::GmComponent::ZoneProcessGMFind(const EQ::Net::Packet &p)
{
	if (p.Length() < 2 + sizeof(EQT::GMFind_Struct)) {
		return;
	}

	const EQT::GMFind_Struct* gm = reinterpret_cast<const EQT::GMFind_Struct*>(
		static_cast<const uint8_t*>(p.Data()) + 2);

	std::string charname(gm->charname, strnlen(gm->charname, 64));
	if (!gm->success) {
		LOG_INFO(MOD_MAIN, "GM find: {} not found in this zone", charname);
		ctx_.out.System(fmt::format("{} is not in this zone.", charname));
		return;
	}
	LOG_INFO(MOD_MAIN, "GM find result: {} in zone {} at ({:.1f}, {:.1f}, {:.1f}) [server x={:.1f} y={:.1f}]",
		charname, gm->zoneID, gm->y, gm->x, gm->z, gm->x, gm->y);
	ctx_.out.System(fmt::format("{} is in zone {} at {:.1f}, {:.1f}, {:.1f}",
		charname, gm->zoneID, gm->y, gm->x, gm->z));
}

void eqt::gm::GmComponent::ZoneProcessGMSummon(const EQ::Net::Packet &p)
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
		ctx_.out.System("Summon failed.");
	}
}
