// F03: Login-server and world-server connection clusters, split out of eq.cpp.
// Pure move — see refactor/F03_login_world.md for scope and verification.
//
// Covers:
//   - Login cluster: EverQuest::Login{On,Send,Process}* (session handshake,
//     credential exchange via eqcrypt_block, server list, play request)
//   - World cluster: EverQuest::World{On,Send,Process}* (character select,
//     zone handoff)
//
// eqcrypt_block is a free-function DES helper used exclusively by the login
// cluster (LoginSendLogin / LoginProcessLoginResponse); it has no header
// declaration and no other callers, so it moved with its callers unchanged.

#include "client/eq.h"
#include "common/logging.h"
#include "common/packet_structs.h"
#include <cstring>
#include <functional>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

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

void EverQuest::LoginOnNewConnection(std::shared_ptr<EQ::Net::DaybreakConnection> connection)
{
	m_login_connection = connection;
	LOG_INFO(MOD_WORLD, "Connecting...");
}

void EverQuest::LoginOnStatusChangeReconnectEnabled(std::shared_ptr<EQ::Net::DaybreakConnection> conn, EQ::Net::DbProtocolStatus from, EQ::Net::DbProtocolStatus to)
{
	if (to == EQ::Net::StatusConnected) {
		LOG_INFO(MOD_WORLD, "Login connected.");
		// Phase 1 complete - connected to login server, now sending session ready
		LoginSendSessionReady();
	}

	if (to == EQ::Net::StatusDisconnected) {
		LOG_INFO(MOD_WORLD, "Login connection lost before we got to world, reconnecting.");
		m_key.clear();
		m_dbid = 0;
		m_login_connection.reset();
		SetLoadingPhase(LoadingPhase::LOGIN_CONNECTING);
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

	// T05: record real network traffic only (replayed packets enter via
	// ReplayLoginPacket -> DispatchLoginPacket and are never re-recorded) --
	// mirrors R01's rule for zone (see eq.cpp:725).
	if (m_session_recorder && p.Length() >= 2) {
		m_session_recorder->record(EQT::Net::SessionStream::Login, EQT::Net::SessionDirection::ServerToClient,
			opcode, (const uint8_t*)p.Data() + 2, static_cast<uint32_t>(p.Length() - 2));
	}

	DispatchLoginPacket(p);
}

// T05 replay/test seam (mirrors Batch R's ReplayZonePacket): build a
// synthetic app packet (2-byte opcode prefix + payload, matching
// session_log.h's on-disk layout) and run it through the same opcode switch
// LoginOnPacketRecv uses.
void EverQuest::ReplayLoginPacket(uint16_t opcode, const uint8_t* data, size_t size)
{
	EQ::Net::DynamicPacket p;
	p.Resize(2 + size);
	p.PutUInt16(0, opcode);
	if (size > 0 && data != nullptr) {
		memcpy(static_cast<char*>(p.Data()) + 2, data, size);
	}
	DispatchLoginPacket(p);
}

void EverQuest::DispatchLoginPacket(const EQ::Net::Packet & p)
{
	auto opcode = p.GetUInt16(0);

	switch (opcode) {
	case HC_OP_ChatMessage:
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
		LOG_WARN(MOD_LOGIN, "Unhandled login opcode: {:#06x}", opcode);
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
	if (m_login_connection) {
		m_login_connection->QueuePacket(p);
	} else {
		++m_replay_suppressed_sends;
	}
}

void EverQuest::LoginSendLogin()
{
	SetLoadingPhase(LoadingPhase::LOGIN_AUTHENTICATING);
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
	if (m_login_connection) {
		m_login_connection->QueuePacket(p);
	} else {
		++m_replay_suppressed_sends;
	}
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
	if (m_login_connection) {
		m_login_connection->QueuePacket(p);
	} else {
		++m_replay_suppressed_sends;
	}
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
	if (m_login_connection) {
		m_login_connection->QueuePacket(p);
	} else {
		++m_replay_suppressed_sends;
	}
}

void EverQuest::LoginProcessLoginResponse(const EQ::Net::Packet & p)
{
	// T07: Length() is size_t, so a packet shorter than the 12-byte header
	// would underflow this subtraction to ~1.8e19 and feed it straight to
	// new char[] (T06 pinned that as a std::bad_alloc). Guard the underflow
	// before it happens.
	if (p.Length() < 12) {
		LOG_WARN(MOD_WORLD, "LoginAccepted packet too small: {} bytes (expected at least {})",
			p.Length(), 12);
		return;
	}

	auto encrypt_size = p.Length() - 12;
	if (encrypt_size % 8 > 0) {
		encrypt_size = (encrypt_size / 8) * 8;
	}

	// T07: the decrypted buffer is read at offsets 1 (uint16), 8 (uint32)
	// and 12 (cstring), so it needs at least 13 bytes. Without this, a
	// packet of exactly 12 bytes yields a zero-length buffer and the reads
	// below throw std::out_of_range out of the network callback. Real
	// LoginAccepted packets decrypt to 80 bytes, so this cannot reject
	// legitimate traffic.
	if (encrypt_size < 13) {
		LOG_WARN(MOD_WORLD, "LoginAccepted payload too small after decrypt sizing: {} bytes (expected at least {})",
			encrypt_size, 13);
		return;
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
	// T07: both members are always set in production (LoginOnNewConnection /
	// ConnectToLogin) before any packet can arrive, but this is reachable
	// from HC_OP_PlayEverquestResponse via the replay seam, where they are
	// null — T06 pinned that as a SIGSEGV. Guard both, matching the pattern
	// used by every other connection use in this file.
	if (m_login_connection_manager) {
		m_login_connection_manager->OnConnectionStateChange(std::bind(&EverQuest::LoginOnStatusChangeReconnectDisabled, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3));
	}
	if (m_login_connection) {
		m_login_connection->Close();
	}
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

	// T05: record real network traffic only (replayed packets enter via
	// ReplayWorldPacket -> DispatchWorldPacket and are never re-recorded) --
	// mirrors R01's rule for zone (see eq.cpp:725).
	if (m_session_recorder && p.Length() >= 2) {
		m_session_recorder->record(EQT::Net::SessionStream::World, EQT::Net::SessionDirection::ServerToClient,
			opcode, (const uint8_t*)p.Data() + 2, static_cast<uint32_t>(p.Length() - 2));
	}

	DispatchWorldPacket(p);
}

// T05 replay/test seam (mirrors Batch R's ReplayZonePacket): build a
// synthetic app packet (2-byte opcode prefix + payload, matching
// session_log.h's on-disk layout) and run it through the same opcode switch
// WorldOnPacketRecv uses.
void EverQuest::ReplayWorldPacket(uint16_t opcode, const uint8_t* data, size_t size)
{
	EQ::Net::DynamicPacket p;
	p.Resize(2 + size);
	p.PutUInt16(0, opcode);
	if (size > 0 && data != nullptr) {
		memcpy(static_cast<char*>(p.Data()) + 2, data, size);
	}
	DispatchWorldPacket(p);
}

void EverQuest::DispatchWorldPacket(const EQ::Net::Packet & p)
{
	auto opcode = p.GetUInt16(0);

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
		LOG_WARN(MOD_WORLD, "Unhandled world opcode: {}", GetOpcodeName(opcode));
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
	if (m_world_connection) {
		m_world_connection->QueuePacket(p);
	} else {
		++m_replay_suppressed_sends;
	}
}

void EverQuest::WorldSendEnterWorld(const std::string &character)
{
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
	if (m_world_connection) {
		m_world_connection->QueuePacket(p);
	} else {
		++m_replay_suppressed_sends;
	}
	m_enter_world_sent = true;
}

void EverQuest::WorldProcessCharacterSelect(const EQ::Net::Packet &p)
{
	SetLoadingPhase(LoadingPhase::WORLD_CHARACTER_SELECT);

	// For Titanium client, the packet is just the raw CharacterSelect_Struct
	// Let's verify packet size
	if (p.Length() < 1706) {
		LOG_WARN(MOD_WORLD, "Character select packet too small: {} bytes", p.Length());
		return;
	}

	// Debug: Let's check names array at offset 1024 + 2 (for opcode)
	LOG_DEBUG(MOD_MAIN, "Checking character names in Titanium format:");

	// Titanium CharacterSelect_Struct has names at offset 1024
	const size_t names_offset = 1024 + 2; // +2 for opcode

	// T14: keep the occupied slots instead of discarding them after logging,
	// so a character-select UI (and tests) can read the list. Rebuilt on
	// every delivery; previously the loop returned at the first name match,
	// leaving later slots unparsed.
	m_character_select_list.clear();
	bool found = false;

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

		// eqemu fills empty Titanium slots with the literal name "<none>"
		// (all other fields zero) — verified against the real capture, where
		// slots 3-9 carry it. '<' is not a legal character-name character,
		// so this cannot shadow a real character.
		if (name.length() > 0 && name != "<none>") {
			// Get other character data using Titanium offsets
			uint8_t level = p.GetUInt8(1694 + 2 + i);  // Level array at offset 1694
			uint8_t pclass = p.GetUInt8(1004 + 2 + i); // Class array at offset 1004
			uint32_t race = p.GetUInt32(0 + 2 + (i * 4)); // Race array at offset 0
			uint32_t zone = p.GetUInt32(964 + 2 + (i * 4)); // Zone array at offset 964

			LOG_DEBUG(MOD_MAIN, "Character {}: name='{}', level={}, class={}, race={}, zone={}",
				i, name, level, pclass, race, zone);

			m_character_select_list.push_back({i, name, level, pclass, race, zone});

			if (!found && m_character.compare(name) == 0) {
				LOG_DEBUG(MOD_MAIN, "Found our character '{}' at index {}", m_character, i);
				m_character_select_index = i;
				found = true;
			}
		}
	}

	if (!found) {
		LOG_ERROR(MOD_WORLD, "Could not find {}, cannot continue to login.", m_character);
	}
}

void EverQuest::WorldSendApproveWorld()
{
	EQ::Net::DynamicPacket p;
	p.Resize(274);  // Matching good log response size

	p.PutUInt16(0, HC_OP_ApproveWorld);
	// The rest is character-specific data which we'll leave as zeros for now

	DumpPacket("C->S", HC_OP_ApproveWorld, p);
	if (m_world_connection) {
		m_world_connection->QueuePacket(p);
	} else {
		++m_replay_suppressed_sends;
	}
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
	if (m_world_connection) {
		m_world_connection->QueuePacket(p);
	} else {
		++m_replay_suppressed_sends;
	}

	// Send CRC2
	p.PutUInt16(0, HC_OP_World_Client_CRC2);
	DumpPacket("C->S", HC_OP_World_Client_CRC2, p);
	if (m_world_connection) {
		m_world_connection->QueuePacket(p);
	} else {
		++m_replay_suppressed_sends;
	}
}

void EverQuest::WorldSendWorldClientReady()
{
	EQ::Net::DynamicPacket p;
	p.Resize(2);

	p.PutUInt16(0, HC_OP_WorldClientReady);

	DumpPacket("C->S", HC_OP_WorldClientReady, p);
	if (m_world_connection) {
		m_world_connection->QueuePacket(p);
	} else {
		++m_replay_suppressed_sends;
	}
	m_world_ready = true;
}

void EverQuest::WorldSendWorldComplete()
{
	EQ::Net::DynamicPacket p;
	p.Resize(2);

	p.PutUInt16(0, HC_OP_WorldComplete);

	DumpPacket("C->S", HC_OP_WorldComplete, p);
	if (m_world_connection) {
		m_world_connection->QueuePacket(p);
	} else {
		++m_replay_suppressed_sends;
	}
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
		if (m_world_connection) {
			m_world_connection->QueuePacket(ack);
		} else {
			++m_replay_suppressed_sends;
		}

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
	// Parse the chat server info from the world server.
	//
	// T13: the real eqemu format (world/client.cpp, fmt string
	// "{},{},{}.{},{}{:08X}") is FOUR comma-separated fields, and it is
	// era-independent — the version switch above it only picks the
	// connection-type character, not the layout:
	//
	//     host , port , shortname.charname , <connection_type><mail_key:08X>
	//
	// The connection type is a single character concatenated onto the
	// 8-hex-digit mail key with NO separator. Titanium sends 'B' on
	// OP_SetChatServer (chat) and 'b' on OP_SetChatServer2 (mail) — see
	// emu_versions.h ucsTitaniumChat / ucsTitaniumMail.
	//
	// This previously required 5 fields and read the mail key from parts[4],
	// treating the type and key as separate fields. No server produces that
	// shape, so it never populated anything and warned on every packet.
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

	if (parts.size() < 4) {
		LOG_WARN(MOD_WORLD, "Invalid chat server info format: got {} parts, expected at least 4. Raw: '{}'",
			parts.size(), chat_info);
		return;
	}

	// Field 4 is <connection_type><mail_key>, so it needs at least the type
	// character. substr(1) on an empty string would throw.
	if (parts[3].empty()) {
		LOG_WARN(MOD_WORLD, "Chat server info has an empty connection-type/mail-key field. Raw: '{}'",
			chat_info);
		return;
	}

	// T13: std::stoi throws on a non-numeric port, and this runs on the
	// network receive path where nothing catches — the same
	// crash-on-malformed-packet class T04/T05/T07 closed elsewhere. stoi only
	// throws above INT_MAX, so the uint16_t range needs its own check or a
	// port like 70000 would silently truncate.
	int parsed_port = 0;
	try {
		parsed_port = std::stoi(parts[1]);
	} catch (const std::exception&) {
		LOG_WARN(MOD_WORLD, "Chat server info has a non-numeric port '{}'. Raw: '{}'",
			parts[1], chat_info);
		return;
	}
	if (parsed_port < 1 || parsed_port > 65535) {
		LOG_WARN(MOD_WORLD, "Chat server info has an out-of-range port '{}'. Raw: '{}'",
			parts[1], chat_info);
		return;
	}

	m_ucs_host = parts[0];
	m_ucs_port = static_cast<uint16_t>(parsed_port);
	// parts[2] is shortname.charname
	m_ucs_connection_type = parts[3][0];
	m_mail_key = parts[3].substr(1);

	LOG_DEBUG(MOD_WORLD, "UCS connection info: {}:{}, type: '{}'",
		m_ucs_host, m_ucs_port, m_ucs_connection_type);

	// Connect to UCS server
	// TODO: Fix UCS implementation to use correct EQStream API
	// ConnectToUCS(m_ucs_host, m_ucs_port);
}

void EverQuest::WorldProcessZoneServerInfo(const EQ::Net::Packet &p)
{
	LOG_DEBUG(MOD_WORLD, "Received ZoneServerInfo packet");

	// NOTE: Loading progress is handled by the LoadingPhase system.
	// ConnectToZone() will set ZONE_CONNECTING phase after we process this packet.

	// Extract zone server info
	std::string new_zone_host = p.GetCString(2);
	uint16_t new_zone_port = p.GetUInt16(130);

	LOG_DEBUG(MOD_ZONE, "Zone server info received: {}:{}", new_zone_host, new_zone_port);

	// Check if this is a zone transition (we're already connected to a zone)
	bool is_zone_transition = m_zone_connected || m_zone_connection != nullptr;

	if (is_zone_transition) {
		LOG_DEBUG(MOD_ZONE, "Zone transition detected - disconnecting from current zone");

		// Set player position to pending zone coordinates if we have them.
		//
		// T52 correction: this comment used to claim m_pending_zone_x/y were
		// in SERVER format (x=server_x, y=server_y) and applied a second
		// x/y swap here. That was wrong. Every producer of m_pending_zone_x/y
		// -- ZoneProcessZoneChange (eq_zone.cpp, the server-approved-teleport
		// path), CheckZoneLine (eq_zone.cpp, the zone-line path, which stores
		// its target straight from the zone's own WLD-derived zone-line data,
		// already in client format), and the bind/death path (eq.cpp) --
		// already stores CLIENT-format coordinates. Swapping again here
		// would transpose x and y for every server-solicited teleport this
		// branch applies to. See refactor/T52_zone_position_convention.md
		// Review for the wire evidence (ZoneProcessZoneChange's own swap,
		// cross-checked against live PlayerProfile packets, which use the
		// identical client_x=server_y/client_y=server_x convention and are
		// what actually determines landing position on every zone entry).
		//
		// Separately noted, not fixed here: `is_zone_transition` above is
		// always false in the traced live flows (ProcessDeferredZoneChange
		// always calls DisconnectFromZone(), clearing m_zone_connected and
		// m_zone_connection, before WorldProcessZoneServerInfo ever runs),
		// so this whole block is currently dead code in every path this
		// unit could exercise (#goto and real zone-line crossings both
		// confirmed via live packet capture to get their landing position
		// from the subsequent PlayerProfile packet instead, never from
		// here). Fixed as a correctness hardening for whenever that
		// reachability changes, not because it explains any live symptom.
		if (m_pending_zone_id != 0) {
			LOG_DEBUG(MOD_ZONE, "Setting spawn position: client coords ({:.1f}, {:.1f}, {:.1f}) -> m_x={:.1f}, m_y={:.1f}",
				m_pending_zone_x, m_pending_zone_y, m_pending_zone_z,
				m_pending_zone_x, m_pending_zone_y);
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
