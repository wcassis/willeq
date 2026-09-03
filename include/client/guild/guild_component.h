#pragma once

// Batch K (refactor/K_component_extraction.md), unit K01 -- the pilot.
//
// The guild family, lifted out of class EverQuest. Every body in
// src/client/guild/guild_component.cpp is the body that used to live in
// src/client/eq_guild.cpp, moved verbatim; the only edits inside a body are
// the substitutions §Mechanism 4 allows (m_x -> ctx_.x / owner_.m_x,
// Foo() -> owner_.Foo()) plus the one qualification K01's Review records.
//
// `owner_` is the TRANSITION handle: the moved code still reaches EverQuest
// members and methods that have not been extracted yet (m_guild_id,
// m_guild_rank, QueuePacket(), AddChatSystemMessage()). EverQuest declares
// `friend class eqt::guild::GuildComponent` for exactly that. K-final removes
// both the handle and the friendship.

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
	GuildComponent(ClientContext& ctx, EverQuest& owner) : ctx_(ctx), owner_(owner) {}

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

private:
	ClientContext& ctx_;
	EverQuest& owner_;

	// Guild state, moved out of EverQuest: referenced by this family only.
	bool m_has_pending_guild_invite = false;
	std::string m_guild_invite_from;
	uint32_t m_guild_invite_id = 0;
};

}  // namespace guild
}  // namespace eqt
