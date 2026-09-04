#pragma once

// Batch K (refactor/K_component_extraction.md), unit K01 -- the pilot.
//
// The guild family, lifted out of class EverQuest. Every body in
// src/client/guild/guild_component.cpp is the body that used to live in
// src/client/eq_guild.cpp, moved verbatim; the only edits inside a body are
// the substitutions §Mechanism 4 allows (m_x -> ctx_.x / owner_.m_x,
// Foo() -> owner_.Foo()) plus the one qualification K01's Review records.
//
// T341 (B038 cluster 4): the `owner_` TRANSITION handle and the
// `friend class eqt::guild::GuildComponent` line are BOTH GONE. m_guild_id
// and m_guild_rank moved here (EverQuest's GetGuildID()/GetGuildRank()
// forward); QueuePacket() and AddChatSystemMessage() had already become
// ctx_.net.Queue() and ctx_.out.System() at K13. This family now reaches zero
// EverQuest private members and names no EverQuest-scoped private name --
// both axes of the K13 (ii) friend test are empty.

#include "client/client_context.h"

#include <cstdint>
#include <string>

class EverQuest;

namespace EQ {
namespace Net {
class Packet;
}
}

namespace eqt {
namespace guild {

class GuildComponent {
public:
	// T341: the EverQuest& transition handle is GONE -- this family reaches
	// zero EverQuest private members and names no EverQuest-scoped private
	// name (the two-axis rule, K13 correction (ii) / B039). It is the
	// second component to pass both axes, after eqt::gm::GmComponent.
	explicit GuildComponent(ClientContext& ctx) : ctx_(ctx) {}

	// Senders. Were public on EverQuest; K01 deleted those declarations and
	// retargeted the callers (eq_action_handler_extended.cpp, tests).
	void SendGuildInvite(const std::string& player_name);
	void SendGuildInviteAccept(bool accept);
	void SendGuildRemove(const std::string& player_name);
	void SendGuildDemote(const std::string& player_name);
	void SendGuildLeader(const std::string& player_name);
	void SendGetGuildMOTD();
	void SendSetGuildMOTD(const std::string& motd);

	// Packet handlers. Were private on EverQuest; DispatchZonePacket
	// (eq_zone_dispatch.cpp) now calls m_guild->ZoneProcessGuild*() directly.
	void ZoneProcessGuildInvite(const EQ::Net::Packet& p);
	void ZoneProcessGuildMOTDReply(const EQ::Net::Packet& p);
	void ZoneProcessGuildMemberUpdate(const EQ::Net::Packet& p);
	void ZoneProcessGuildMemberAdd(const EQ::Net::Packet& p);

	bool HasPendingGuildInvite() const { return m_has_pending_guild_invite; }

	// T341 (B038 cluster 4): m_guild_id/m_guild_rank moved here from
	// EverQuest -- this family is their only reader, the zone and entities
	// families their only writers. EverQuest keeps GetGuildID()/GetGuildRank()
	// as forwarders (T133/T317): ~14 and ~7 external call sites in tests and
	// client/guild_packet_bodies.h read them on that receiver.
	//
	// T133: populated from the player's own PlayerProfile packet
	// (ZoneComponent::ZoneProcessPlayerProfile). 0 means "not in a guild" --
	// every SendGuild*() sender below guards on that, correctly.
	uint32_t GetGuildID() const { return m_guild_id; }
	void SetGuildID(uint32_t guild_id) { m_guild_id = guild_id; }
	// T317: the player's own guild rank, in the TITANIUM encoding the wire
	// and the profile use -- 0 = member, 1 = officer, 2 = leader, 9
	// (GUILD_RANK_NONE_TI) = none/unknown. See eq.h's GetGuildRank() for the
	// full wire note.
	uint8_t GetGuildRank() const { return m_guild_rank; }
	void SetGuildRank(uint8_t rank) { m_guild_rank = rank; }

private:
	ClientContext& ctx_;

	// T341: the player's own guild id and rank. K01 left them on EverQuest
	// under Mechanism 3's shared-member rule (the zone family writes them);
	// T341 moved them here with SetGuildID()/SetGuildRank() for that writer.
	uint32_t m_guild_id = 0;
	// T317: Titanium rank encoding (0 member / 1 officer / 2 leader / 9 none).
	// 9 == GUILD_RANK_NONE_TI is the correct "not in a guild" default.
	uint8_t m_guild_rank = 9;

	// Guild state, moved out of EverQuest: referenced by this family only.
	bool m_has_pending_guild_invite = false;
	std::string m_guild_invite_from;
	uint32_t m_guild_invite_id = 0;
};

}  // namespace guild
}  // namespace eqt
