#pragma once

// Batch K (refactor/K_component_extraction.md), unit K04.
//
// The group family -- group, raid and pet -- lifted out of class EverQuest.
// Every body in src/client/group/group_component.cpp is the body that used to
// live in src/client/eq_group.cpp, moved verbatim; the only edits inside a
// body are the substitutions Mechanism 4 allows (m_x -> ctx_.x / owner_.m_x,
// Foo() -> owner_.Foo()).
//
// T366 (B038 CLOSED): the `owner_` TRANSITION handle and the
// `friend class eqt::group::GroupComponent` line are both GONE. The last use
// was the private m_test_bypass_send_guard (the T88 seam), which now lives on
// eqt::PacketSender and is read as ctx_.net.TestBypassSendGuard().
//
// K04 deleted two bodies outright under Mechanism 5 -- DismissPet() and
// UpdateGroupMemberFromEntity() had zero callers anywhere, inside the family
// or out. See the K04 Review.

#include "client/client_context.h"
#include "client/pet_constants.h"
#include "client/raid_update.h"
#include "client/state/group_state.h"

#include <cstdint>
#include <string>
#include <vector>

class EverQuest;

namespace EQ {
namespace Net {
class Packet;
}
}

namespace eqt {
namespace group {

class GroupComponent {
public:
	// T366 (B038): the EverQuest& transition handle is gone -- this component
	// names nothing on EverQuest any more.
	explicit GroupComponent(ClientContext& ctx) : ctx_(ctx) {}

	// Group queries / actions. Were public on EverQuest; K04 deleted those
	// declarations and retargeted the callers (eq_action_handler.cpp,
	// eq_zone.cpp, eq.cpp, tests).
	const GroupMember* GetGroupMember(int index) const;
	void SendGroupInvite(const std::string& target_name);
	void SendGroupFollow(const std::string& inviter_name);  // Accept invite
	void SendGroupDecline(const std::string& inviter_name); // Decline invite
	void SendLeaveGroup();
	void AcceptGroupInvite();
	void ClearGroup();

	// Pet queries and commands.
	std::string GetPetName() const;
	uint8_t GetPetLevel() const;
	void SendPetCommand(EQT::PetCommand command, uint16_t target_id = 0);

	// T179: raid commands. Sends OP_RaidCommand (Titanium OP_RaidInvite, 0x5891).
	void SendRaidCommand(uint32_t action, const std::string& player_name,
	                     const std::string& leader_name, uint32_t parameter = 0);
	// Raid state, maintained from the decoded OP_RaidUpdate stream.
	bool IsInRaid() const { return m_in_raid; }
	const std::string& GetRaidLeader() const { return m_raid_leader; }
	const std::string& GetPendingRaidInviter() const { return m_pending_raid_inviter; }
	void ClearPendingRaidInvite() { m_pending_raid_inviter.clear(); }
	// T180: the member list, assembled from the action-0 stream -- Titanium's
	// ENCODE drops eqemu's roster packet, so this is the only roster there is.
	const std::vector<EQT::RaidMemberInfo>& GetRaidMembers() const {
		return m_raid_roster.Members();
	}
	// T182: raid lock state, from actions 17/18. Re-established on zone-in --
	// eqemu's rejoin path sends SendRaidLockTo only when the raid is locked.
	bool IsRaidLocked() const { return m_raid_locked; }

	// Packet handlers. Were private on EverQuest; DispatchZonePacket
	// (eq_zone_dispatch.cpp) now calls m_group->ZoneProcess*() directly.
	void ZoneProcessGroupInvite(const EQ::Net::Packet& p);
	void ZoneProcessGroupFollow(const EQ::Net::Packet& p);
	void ZoneProcessGroupUpdate(const EQ::Net::Packet& p);
	void ZoneProcessGroupDisband(const EQ::Net::Packet& p);
	void ZoneProcessGroupCancelInvite(const EQ::Net::Packet& p);
	void ZoneProcessRaidUpdate(const EQ::Net::Packet& p);

private:
	// Reached only from inside this family (measured, K04).
	void SendGroupDisband();
	const Entity* GetPetEntity() const;
	int FindGroupMemberByName(const std::string& name) const;
	void FillGroupMemberFromEntities(GroupMember& member) const;
	// T320: groupActJoin carries a GroupJoin_Struct, not a GroupUpdate_Struct
	// -- both are 452 bytes, so only the action can tell them apart.
	void ZoneProcessGroupJoin(const EQ::Net::Packet& p);

	ClientContext& ctx_;

	// T179/T180: raid state, derived from ZoneProcessRaidUpdate. Moved out of
	// EverQuest by K04: referenced by this family only.
	bool m_in_raid = false;
	std::string m_raid_leader;
	std::string m_pending_raid_inviter;
	EQT::RaidRoster m_raid_roster;
	bool m_raid_locked = false;
};

}  // namespace group
}  // namespace eqt
