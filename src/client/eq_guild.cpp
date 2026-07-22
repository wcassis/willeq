// EverQuest guild command cluster.
//
// Split out of eq.cpp (Batch F, unit F04): guild invite/accept/remove/demote/
// leader/MOTD senders plus the guild Process* packet handlers. Moved
// verbatim, no behavior change. See refactor/F04_ucs_guild_gm.md.

#include "client/eq.h"
#include "common/logging.h"
#include "common/packet_structs.h"
#include <cstring>

void EverQuest::SendGuildInvite(const std::string& player_name)
{
	if (m_guild_id == 0) {
		AddChatSystemMessage("You are not in a guild.");
		return;
	}

	EQT::GuildCommand_Struct invite;
	memset(&invite, 0, sizeof(invite));
	strncpy(invite.othername, player_name.c_str(), 63);
	strncpy(invite.myname, m_character.c_str(), 63);
	invite.guildeqid = static_cast<uint16_t>(m_guild_id);
	invite.officer = 2;  // TODO: Use actual rank

	EQ::Net::DynamicPacket packet;
	packet.Resize(2 + sizeof(EQT::GuildCommand_Struct));
	packet.PutUInt16(0, HC_OP_GuildInvite);
	memcpy(static_cast<uint8_t*>(packet.Data()) + 2, &invite, sizeof(EQT::GuildCommand_Struct));
	QueuePacket(HC_OP_GuildInvite, &packet);

	AddChatSystemMessage(fmt::format("Inviting {} to join your guild.", player_name));
}

void EverQuest::SendGuildInviteAccept(bool accept)
{
	if (!m_has_pending_guild_invite) {
		AddChatSystemMessage("You don't have a pending guild invite.");
		return;
	}

	EQT::GuildInviteAccept_Struct response;
	memset(&response, 0, sizeof(response));
	strncpy(response.inviter, m_guild_invite_from.c_str(), 63);
	strncpy(response.newmember, m_character.c_str(), 63);
	response.response = accept ? 1 : 0;
	response.guildeqid = m_guild_invite_id;

	EQ::Net::DynamicPacket packet;
	packet.Resize(2 + sizeof(EQT::GuildInviteAccept_Struct));
	packet.PutUInt16(0, HC_OP_GuildInviteAccept);
	memcpy(static_cast<uint8_t*>(packet.Data()) + 2, &response, sizeof(EQT::GuildInviteAccept_Struct));
	QueuePacket(HC_OP_GuildInviteAccept, &packet);

	m_has_pending_guild_invite = false;
	m_guild_invite_from.clear();
	m_guild_invite_id = 0;

	if (accept) {
		AddChatSystemMessage("You have accepted the guild invite.");
	} else {
		AddChatSystemMessage("You have declined the guild invite.");
	}
}

void EverQuest::SendGuildRemove(const std::string& player_name)
{
	if (m_guild_id == 0) {
		AddChatSystemMessage("You are not in a guild.");
		return;
	}

	EQT::GuildRemove_Struct remove;
	memset(&remove, 0, sizeof(remove));
	strncpy(remove.target, player_name.c_str(), 63);
	strncpy(remove.name, m_character.c_str(), 63);
	remove.leaderstatus = 2;  // TODO: Use actual rank

	EQ::Net::DynamicPacket packet;
	packet.Resize(2 + sizeof(EQT::GuildRemove_Struct));
	packet.PutUInt16(0, HC_OP_GuildRemove);
	memcpy(static_cast<uint8_t*>(packet.Data()) + 2, &remove, sizeof(EQT::GuildRemove_Struct));
	QueuePacket(HC_OP_GuildRemove, &packet);

	AddChatSystemMessage(fmt::format("Removing {} from the guild.", player_name));
}

void EverQuest::SendGuildDemote(const std::string& player_name)
{
	if (m_guild_id == 0) {
		AddChatSystemMessage("You are not in a guild.");
		return;
	}

	EQT::GuildDemote_Struct demote;
	memset(&demote, 0, sizeof(demote));
	strncpy(demote.name, m_character.c_str(), 63);
	strncpy(demote.target, player_name.c_str(), 63);

	EQ::Net::DynamicPacket packet;
	packet.Resize(2 + sizeof(EQT::GuildDemote_Struct));
	packet.PutUInt16(0, HC_OP_GuildDemote);
	memcpy(static_cast<uint8_t*>(packet.Data()) + 2, &demote, sizeof(EQT::GuildDemote_Struct));
	QueuePacket(HC_OP_GuildDemote, &packet);

	AddChatSystemMessage(fmt::format("Demoting {} in the guild.", player_name));
}

void EverQuest::SendGuildLeader(const std::string& player_name)
{
	if (m_guild_id == 0) {
		AddChatSystemMessage("You are not in a guild.");
		return;
	}

	EQT::GuildMakeLeader_Struct leader;
	memset(&leader, 0, sizeof(leader));
	strncpy(leader.name, m_character.c_str(), 63);
	strncpy(leader.target, player_name.c_str(), 63);

	EQ::Net::DynamicPacket packet;
	packet.Resize(2 + sizeof(EQT::GuildMakeLeader_Struct));
	packet.PutUInt16(0, HC_OP_GuildLeader);
	memcpy(static_cast<uint8_t*>(packet.Data()) + 2, &leader, sizeof(EQT::GuildMakeLeader_Struct));
	QueuePacket(HC_OP_GuildLeader, &packet);

	AddChatSystemMessage(fmt::format("Transferring guild leadership to {}.", player_name));
}

void EverQuest::SendGetGuildMOTD()
{
	// Empty packet to request MOTD
	EQ::Net::DynamicPacket packet;
	packet.PutUInt16(0, HC_OP_GetGuildMOTD);
	QueuePacket(HC_OP_GetGuildMOTD, &packet);
}

void EverQuest::SendSetGuildMOTD(const std::string& motd)
{
	if (m_guild_id == 0) {
		AddChatSystemMessage("You are not in a guild.");
		return;
	}

	EQT::GuildMOTD_Struct gmotd;
	memset(&gmotd, 0, sizeof(gmotd));
	strncpy(gmotd.name, m_character.c_str(), 63);
	strncpy(gmotd.setby_name, m_character.c_str(), 63);
	strncpy(gmotd.motd, motd.c_str(), 511);

	EQ::Net::DynamicPacket packet;
	packet.Resize(2 + sizeof(EQT::GuildMOTD_Struct));
	packet.PutUInt16(0, HC_OP_SetGuildMOTD);
	memcpy(static_cast<uint8_t*>(packet.Data()) + 2, &gmotd, sizeof(EQT::GuildMOTD_Struct));
	QueuePacket(HC_OP_SetGuildMOTD, &packet);

	AddChatSystemMessage("Guild MOTD updated.");
}

void EverQuest::ZoneProcessGuildInvite(const EQ::Net::Packet &p)
{
	// Received a guild invite
	if (p.Length() < 2 + sizeof(EQT::GuildCommand_Struct)) {
		LOG_WARN(MOD_MAIN, "OP_GuildInvite packet too small: {} bytes", p.Length());
		return;
	}

	const EQT::GuildCommand_Struct* invite = reinterpret_cast<const EQT::GuildCommand_Struct*>(
		static_cast<const uint8_t*>(p.Data()) + 2);

	std::string inviter(invite->myname, strnlen(invite->myname, 64));

	m_has_pending_guild_invite = true;
	m_guild_invite_from = inviter;
	m_guild_invite_id = invite->guildeqid;

	LOG_INFO(MOD_MAIN, "Guild invite received from {}", inviter);
	AddChatSystemMessage(fmt::format("{} has invited you to join a guild.", inviter));
	AddChatSystemMessage("Type /guildaccept to accept or /guilddecline to decline.");
}

void EverQuest::ZoneProcessGuildMOTDReply(const EQ::Net::Packet &p)
{
	// Received guild MOTD
	if (p.Length() < 2 + sizeof(EQT::GuildMOTD_Struct)) {
		LOG_WARN(MOD_MAIN, "OP_GetGuildMOTDReply packet too small: {} bytes", p.Length());
		return;
	}

	const EQT::GuildMOTD_Struct* motd = reinterpret_cast<const EQT::GuildMOTD_Struct*>(
		static_cast<const uint8_t*>(p.Data()) + 2);

	std::string setby(motd->setby_name, strnlen(motd->setby_name, 64));
	std::string message(motd->motd, strnlen(motd->motd, 512));

	LOG_INFO(MOD_MAIN, "Guild MOTD (set by {}): {}", setby, message);
	AddChatSystemMessage(fmt::format("Guild MOTD (set by {}): {}", setby, message));
}

void EverQuest::ZoneProcessGuildMemberUpdate(const EQ::Net::Packet &p)
{
	// Guild member status update (zone change, etc.)
	if (p.Length() < 2 + sizeof(EQT::GuildMemberUpdate_Struct)) {
		return;
	}

	const EQT::GuildMemberUpdate_Struct* update = reinterpret_cast<const EQT::GuildMemberUpdate_Struct*>(
		static_cast<const uint8_t*>(p.Data()) + 2);

	std::string member_name(update->member_name, strnlen(update->member_name, 64));

	if (s_debug_level >= 2) {
		LOG_DEBUG(MOD_MAIN, "Guild member {} changed zone to {}", member_name, update->zone_id);
	}
}

void EverQuest::ZoneProcessGuildMemberAdd(const EQ::Net::Packet &p)
{
	// New guild member added
	if (p.Length() < 2 + 92) {  // Minimum size for GuildJoin_Struct
		return;
	}

	// Parse the packet to get the new member name
	const uint8_t* data = static_cast<const uint8_t*>(p.Data()) + 2;
	// Name is at offset 28 (after guild_id, unknown04, level, class, rank, zoneid, unknown24)
	const char* name = reinterpret_cast<const char*>(data + 28);
	std::string member_name(name, strnlen(name, 64));

	LOG_INFO(MOD_MAIN, "{} has joined the guild", member_name);
	AddChatSystemMessage(fmt::format("{} has joined the guild.", member_name));
}
