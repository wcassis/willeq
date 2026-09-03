// EverQuest guild command cluster.
//
// Split out of eq.cpp (Batch F, unit F04): guild invite/accept/remove/demote/
// leader/MOTD senders plus the guild Process* packet handlers. Moved
// verbatim, no behavior change. See refactor/F04_ucs_guild_gm.md.
//
// K01 (Batch K, refactor/K_component_extraction.md): moved again, from
// src/client/eq_guild.cpp to here, and the bodies rebased from EverQuest onto
// eqt::guild::GuildComponent. Still verbatim -- only the substitutions
// §Mechanism 4 allows (m_character -> ctx_.character; m_guild_id/m_guild_rank
// -> owner_.*, they stay on EverQuest; QueuePacket/AddChatSystemMessage ->
// owner_.*), plus one qualification the batch method did not anticipate:
// s_debug_level is EverQuest's private static, reached through the friendship
// as EverQuest::s_debug_level. See the K01 Review.

#include "client/eq.h"
#include "client/guild/guild_component.h"
#include "common/logging.h"
#include "common/packet_structs.h"
#include "client/guild_packet_bodies.h"
#include <cstring>

// T133: every sender below used to build its OWN opcode-prefixed packet
// (Resize(2 + sizeof(Struct)) + PutUInt16(0, opcode) + memcpy at +2) and
// THEN call EverQuest::QueuePacket(opcode, &packet) -- which ALSO prepends
// a 2-byte opcode (eq.cpp: `p.Resize(packet->Length() + 2); p.PutUInt16(0,
// opcode); memcpy(...+2, packet->Data(), packet->Length());`). That is a
// double opcode prefix: the wire packet ends up as
// [real opcode(2)][sender's own duplicate opcode(2)][struct bytes], i.e.
// struct_size + 2 bytes of body where the server expects exactly
// struct_size -- confirmed live from the eqemu zone log this unit's live
// leg produced: "Wrong size: OP_GuildInvite, size=138, expected 136" and
// "Error: app size of 650 != size of GuildMOTD_Struct of 648" (136+2=138,
// 648+2=650). QueuePacket's OWN callers elsewhere in this codebase (e.g.
// combat.cpp's ConsiderTarget: `packet.Resize(28); packet.PutUInt32(0,
// ...); ...; m_eq->QueuePacket(HC_OP_Consider, &packet);` -- no opcode
// written into the packet at all) confirm the correct calling convention:
// QueuePacket(opcode, packet) wants packet to be the BODY ONLY.
//
// This bug predates T133 and was invisible until now: every guarded
// sender here was blocked by `m_guild_id == 0` before ever reaching
// QueuePacket (T132's finding), and the one unguarded sender
// (SendGetGuildMOTD) has no fixed-size struct for the server to size-check
// against, so its two harmless extra bytes were silently ignored. Fixing
// m_guild_id's population (this unit) is what finally let a real
// SendGuildInvite/SendSetGuildMOTD reach the server and expose this --
// without also fixing this, the acceptance bar this unit's plan sets
// ("a guild command reaches the server and gets a response") is
// unreachable no matter how correct m_guild_id is. In territory
// (src/client/eq_guild.cpp), not a new packet handler, so fixed here
// rather than reported as a STOP.

void eqt::guild::GuildComponent::SendGuildInvite(const std::string& player_name)
{
	if (owner_.m_guild_id == 0) {
		ctx_.out.System("You are not in a guild.");
		return;
	}

	// T317: this used to be `invite.officer = 2;  // TODO: Use actual rank`.
	// eqemu does not read this field as the SENDER's rank at all -- it is the
	// rank being granted to `othername`, and 2 is GUILD_LEADER_TI, so every
	// /guildinvite was asking the server to install the invitee as guild
	// leader (or, for an existing member, to promote them to leader). Only the
	// server-side CheckPermission on our own real rank stopped it. A plain
	// invite grants GUILD_MEMBER_TI. Full handler trace in
	// client/guild_packet_bodies.h.
	EQT::GuildCommand_Struct invite =
		EQ::guild::BuildGuildInvite(player_name, ctx_.character, owner_.m_guild_id);

	EQ::Net::DynamicPacket packet;
	packet.Resize(sizeof(EQT::GuildCommand_Struct));
	memcpy(packet.Data(), &invite, sizeof(EQT::GuildCommand_Struct));
	ctx_.net.Queue(HC_OP_GuildInvite, &packet);

	ctx_.out.System(fmt::format("Inviting {} to join your guild.", player_name));
}

void eqt::guild::GuildComponent::SendGuildInviteAccept(bool accept)
{
	if (!m_has_pending_guild_invite) {
		ctx_.out.System("You don't have a pending guild invite.");
		return;
	}

	// T318: `response` is a RANK, not a yes/no flag. `accept ? 1 : 0` sent
	// GUILD_OFFICER_TI on accept, and the server joined the player to the
	// guild as an OFFICER. See guild_packet_bodies.h for the handler's remap
	// table, and for why no Titanium byte can actually decline.
	EQT::GuildInviteAccept_Struct response =
		EQ::guild::BuildGuildInviteAccept(m_guild_invite_from, ctx_.character,
		                                  m_guild_invite_id, accept);

	EQ::Net::DynamicPacket packet;
	packet.Resize(sizeof(EQT::GuildInviteAccept_Struct));
	memcpy(packet.Data(), &response, sizeof(EQT::GuildInviteAccept_Struct));
	ctx_.net.Queue(HC_OP_GuildInviteAccept, &packet);

	m_has_pending_guild_invite = false;
	m_guild_invite_from.clear();
	m_guild_invite_id = 0;

	if (accept) {
		ctx_.out.System("You have accepted the guild invite.");
	} else {
		ctx_.out.System("You have declined the guild invite.");
	}
}

void eqt::guild::GuildComponent::SendGuildRemove(const std::string& player_name)
{
	if (owner_.m_guild_id == 0) {
		ctx_.out.System("You are not in a guild.");
		return;
	}

	// T317: was `remove.leaderstatus = 2;  // TODO: Use actual rank`. eqemu's
	// Handle_OP_GuildRemove casts the body to GuildCommand_Struct and reads
	// only `othername`; leaderstatus is never touched, and the remover's
	// authority comes from the server's own GuildRank(). Sending the player's
	// real rank (PlayerProfile offset 13055) changes nothing on the server --
	// it just stops the client asserting something it had not checked.
	EQT::GuildRemove_Struct remove =
		EQ::guild::BuildGuildRemove(player_name, ctx_.character, owner_.m_guild_rank);

	EQ::Net::DynamicPacket packet;
	packet.Resize(sizeof(EQT::GuildRemove_Struct));
	memcpy(packet.Data(), &remove, sizeof(EQT::GuildRemove_Struct));
	ctx_.net.Queue(HC_OP_GuildRemove, &packet);

	ctx_.out.System(fmt::format("Removing {} from the guild.", player_name));
}

void eqt::guild::GuildComponent::SendGuildDemote(const std::string& player_name)
{
	if (owner_.m_guild_id == 0) {
		ctx_.out.System("You are not in a guild.");
		return;
	}

	EQT::GuildDemote_Struct demote;
	memset(&demote, 0, sizeof(demote));
	strncpy(demote.name, ctx_.character.c_str(), 63);
	strncpy(demote.target, player_name.c_str(), 63);

	EQ::Net::DynamicPacket packet;
	packet.Resize(sizeof(EQT::GuildDemote_Struct));
	memcpy(packet.Data(), &demote, sizeof(EQT::GuildDemote_Struct));
	ctx_.net.Queue(HC_OP_GuildDemote, &packet);

	ctx_.out.System(fmt::format("Demoting {} in the guild.", player_name));
}

void eqt::guild::GuildComponent::SendGuildLeader(const std::string& player_name)
{
	if (owner_.m_guild_id == 0) {
		ctx_.out.System("You are not in a guild.");
		return;
	}

	EQT::GuildMakeLeader_Struct leader;
	memset(&leader, 0, sizeof(leader));
	strncpy(leader.name, ctx_.character.c_str(), 63);
	strncpy(leader.target, player_name.c_str(), 63);

	EQ::Net::DynamicPacket packet;
	packet.Resize(sizeof(EQT::GuildMakeLeader_Struct));
	memcpy(packet.Data(), &leader, sizeof(EQT::GuildMakeLeader_Struct));
	ctx_.net.Queue(HC_OP_GuildLeader, &packet);

	ctx_.out.System(fmt::format("Transferring guild leadership to {}.", player_name));
}

void eqt::guild::GuildComponent::SendGetGuildMOTD()
{
	// Empty packet to request MOTD -- body-only convention (see file
	// header comment): QueuePacket() adds the opcode itself, so an empty
	// DynamicPacket here means "opcode, zero-byte body" on the wire.
	EQ::Net::DynamicPacket packet;
	ctx_.net.Queue(HC_OP_GetGuildMOTD, &packet);
}

void eqt::guild::GuildComponent::SendSetGuildMOTD(const std::string& motd)
{
	if (owner_.m_guild_id == 0) {
		ctx_.out.System("You are not in a guild.");
		return;
	}

	EQT::GuildMOTD_Struct gmotd;
	memset(&gmotd, 0, sizeof(gmotd));
	strncpy(gmotd.name, ctx_.character.c_str(), 63);
	strncpy(gmotd.setby_name, ctx_.character.c_str(), 63);
	strncpy(gmotd.motd, motd.c_str(), 511);

	EQ::Net::DynamicPacket packet;
	packet.Resize(sizeof(EQT::GuildMOTD_Struct));
	memcpy(packet.Data(), &gmotd, sizeof(EQT::GuildMOTD_Struct));
	ctx_.net.Queue(HC_OP_SetGuildMOTD, &packet);

	ctx_.out.System("Guild MOTD updated.");
}

void eqt::guild::GuildComponent::ZoneProcessGuildInvite(const EQ::Net::Packet &p)
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
	ctx_.out.System(fmt::format("{} has invited you to join a guild.", inviter));
	ctx_.out.System("Type /guildaccept to accept or /guilddecline to decline.");
}

void eqt::guild::GuildComponent::ZoneProcessGuildMOTDReply(const EQ::Net::Packet &p)
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
	ctx_.out.System(fmt::format("Guild MOTD (set by {}): {}", setby, message));
}

void eqt::guild::GuildComponent::ZoneProcessGuildMemberUpdate(const EQ::Net::Packet &p)
{
	// Guild member status update (zone change, etc.)
	if (p.Length() < 2 + sizeof(EQT::GuildMemberUpdate_Struct)) {
		return;
	}

	const EQT::GuildMemberUpdate_Struct* update = reinterpret_cast<const EQT::GuildMemberUpdate_Struct*>(
		static_cast<const uint8_t*>(p.Data()) + 2);

	std::string member_name(update->member_name, strnlen(update->member_name, 64));

	if (EverQuest::s_debug_level >= 2) {
		LOG_DEBUG(MOD_MAIN, "Guild member {} changed zone to {}", member_name, update->zone_id);
	}
}

void eqt::guild::GuildComponent::ZoneProcessGuildMemberAdd(const EQ::Net::Packet &p)
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
	ctx_.out.System(fmt::format("{} has joined the guild.", member_name));
}
