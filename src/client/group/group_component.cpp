// EverQuest group/raid/pet cluster.
//
// Split out of eq.cpp (Batch F, unit F12): group invite/accept/decline/
// disband senders, raid invite stub, pet accessors and command sender,
// group state helpers (ClearGroup, FindGroupMemberByName,
// UpdateGroupMemberFromEntity), and the group packet handlers
// (ZoneProcessGroupInvite/Follow/Update/Disband/CancelInvite). Moved
// verbatim, no behavior change. See refactor/F12_group.md.
//
// Note: the group ACCESSORS (IsInGroup, IsGroupLeader, GetGroupMemberCount,
// GetGroupLeaderName, HasPendingGroupInvite, GetPendingInviterName,
// SyncGroupMemberToGameState) were already moved to eq_player_info.cpp in
// F07 and are not part of this file.
//
// Batch J unit J08 relocated ZoneProcessRaidUpdate and SendRaidCommand here
// from eq_entities.cpp, where F11 had left them; they are appended below the
// group handlers under their own banner. Moved verbatim, no behavior change.
// See refactor/J08_eq_entities_split.md.
//
// K04 (Batch K, refactor/K_component_extraction.md): moved again, from
// src/client/eq_group.cpp to here, and the bodies rebased from EverQuest onto
// eqt::group::GroupComponent. Still verbatim -- only the substitutions
// Mechanism 4 allows (m_game_state/m_bridge/m_character/m_zone_connection/
// m_zone_connection_manager/m_entities -> ctx_.*; the T88 send-guard bypass ->
// ctx_.net (T366); IsFullyZonedIn/AddChatSystemMessage/HasPet/
// MySpawnId/OnPetButtonStateChanged -> owner_.*). The raid state members
// (m_in_raid, m_raid_leader, m_pending_raid_inviter, m_raid_roster,
// m_raid_locked) moved into the component and are unqualified here as before.
// DismissPet() and UpdateGroupMemberFromEntity() were deleted, not moved:
// zero callers anywhere. See the K04 Review.

#include "client/group/group_component.h"
#include "client/eq.h"
#include "client/bridge/game_state_bridge.h"
#include "client/raid_update.h"
#include "common/logging.h"
#include "common/packet_structs.h"

#include <cstring>

// ============================================================================
// Group Methods
// ============================================================================

const GroupMember* eqt::group::GroupComponent::GetGroupMember(int index) const
{
	// T286: GroupState is the only store.
	if (index >= 0 && index < ctx_.game_state.group().memberCount()) {
		return ctx_.game_state.group().getMember(index);
	}
	return nullptr;
}

void eqt::group::GroupComponent::SendGroupInvite(const std::string& target_name)
{
	if (!ctx_.zone_connection_manager || !ctx_.self.IsFullyZonedIn()) {
		LOG_WARN(MOD_MAIN, "Cannot send group invite - not connected to zone");
		return;
	}

	EQT::GroupInvite_Struct packet = {};
	strncpy(packet.inviter_name, ctx_.character.c_str(), 63);
	strncpy(packet.invitee_name, target_name.c_str(), 63);

	EQ::Net::DynamicPacket p;
	p.Resize(2 + sizeof(EQT::GroupInvite_Struct));
	p.PutUInt16(0, HC_OP_GroupInvite);
	p.PutData(2, &packet, sizeof(packet));

	ctx_.net.SafeQueueZone(p, 0, true);
	LOG_INFO(MOD_MAIN, "Sent group invite to {}", target_name);
}

// T217: `inviter_name` is, in practice, a reference to our own
// m_pending_inviter_name (AcceptGroupInvite() passes the member straight
// in), and this function clears that member before its final log line -- so
// the log printed "Accepted group invite from " with nothing after it (T99
// live evidence, review/T99/). Copy the name up front so the parameter can
// alias the member safely. The T88 send-guard bypass lets a headless test
// run the clear-then-read sequence that only a live zone connection used to
// reach; with the seam unset the guard is exactly what it was.
void eqt::group::GroupComponent::SendGroupFollow(const std::string& inviter_name)
{
	if (!ctx_.net.TestBypassSendGuard() && (!ctx_.zone_connection_manager || !ctx_.self.IsFullyZonedIn())) {
		LOG_WARN(MOD_MAIN, "Cannot accept group invite - not connected to zone");
		return;
	}

	const std::string inviter = inviter_name;

	EQT::GroupFollow_Struct packet = {};
	strncpy(packet.name1, inviter.c_str(), 63);
	strncpy(packet.name2, ctx_.character.c_str(), 63);

	EQ::Net::DynamicPacket p;
	p.Resize(2 + sizeof(EQT::GroupFollow_Struct));
	p.PutUInt16(0, HC_OP_GroupFollow);
	p.PutData(2, &packet, sizeof(packet));

	ctx_.net.SafeQueueZone(p, 0, true);

	ctx_.game_state.group().clearPendingInvite();  // T286: only store

	LOG_INFO(MOD_MAIN, "Accepted group invite from {}", inviter);
}

// T217: same aliasing hazard as SendGroupFollow() above -- copy first.
void eqt::group::GroupComponent::SendGroupDecline(const std::string& inviter_name)
{
	if (!ctx_.net.TestBypassSendGuard() && (!ctx_.zone_connection_manager || !ctx_.self.IsFullyZonedIn())) {
		LOG_WARN(MOD_MAIN, "Cannot decline group invite - not connected to zone");
		return;
	}

	const std::string inviter = inviter_name;

	EQT::GroupCancel_Struct packet = {};
	strncpy(packet.name1, inviter.c_str(), 63);
	strncpy(packet.name2, ctx_.character.c_str(), 63);
	packet.toggle = 0;

	EQ::Net::DynamicPacket p;
	p.Resize(2 + sizeof(EQT::GroupCancel_Struct));
	p.PutUInt16(0, HC_OP_GroupCancelInvite);
	p.PutData(2, &packet, sizeof(packet));

	ctx_.net.SafeQueueZone(p, 0, true);

	ctx_.game_state.group().clearPendingInvite();  // T286: only store

	LOG_INFO(MOD_MAIN, "Declined group invite from {}", inviter);
}

void eqt::group::GroupComponent::SendGroupDisband()
{
	if (!ctx_.zone_connection_manager || !ctx_.self.IsFullyZonedIn() || !ctx_.game_state.group().inGroup()) {
		LOG_WARN(MOD_MAIN, "Cannot disband group - not in group or not connected");
		return;
	}

	EQT::GroupDisband_Struct packet = {};
	strncpy(packet.name1, ctx_.character.c_str(), 63);
	strncpy(packet.name2, ctx_.character.c_str(), 63);

	EQ::Net::DynamicPacket p;
	p.Resize(2 + sizeof(EQT::GroupDisband_Struct));
	p.PutUInt16(0, HC_OP_GroupDisband);
	p.PutData(2, &packet, sizeof(packet));

	ctx_.net.SafeQueueZone(p, 0, true);
	LOG_INFO(MOD_MAIN, "Sent group disband");
}

void eqt::group::GroupComponent::SendLeaveGroup()
{
	// Same packet as disband for non-leader
	SendGroupDisband();
}

// T168: SendRaidInvite was DELETED here. It sent a bare 64-byte name.
// eqemu routes OP_RaidInvite to Handle_OP_RaidCommand
// (zone/client_packet.cpp:12298), which requires at least
// sizeof(RaidGeneral_Struct) == 140 bytes and reads an `action` field at
// offset 0 that our body did not have at all -- so it was rejected with
// "Wrong size" every time and could never have invited anyone.
//
// Deleted rather than fixed because the raid RESPONSE side is also wrong:
// ZoneProcessRaidUpdate decodes 4 of 6 raid actions incorrectly and action 20
// -- what an invite actually produces -- falls through to "Unknown". A
// correct invite whose result the client cannot read is not a feature. The
// exact body a future raid unit needs is recorded on HC_OP_RaidInvite in
// eq.h and in refactor/T168_sender_triage.md.

// T217: capture the inviter BEFORE SendGroupFollow(), which clears
// m_pending_inviter_name as its last act. Reading the member afterwards
// printed "You have joined 's group" (T99 live evidence, review/T99/).
void eqt::group::GroupComponent::AcceptGroupInvite()
{
	if (ctx_.game_state.group().hasPendingInvite()) {
		const std::string inviter = ctx_.game_state.group().pendingInviterName();
		SendGroupFollow(inviter);
		ctx_.out.System("You have joined " + inviter + "'s group");
	}
}

void eqt::group::GroupComponent::DeclineGroupInvite()
{
	if (ctx_.game_state.group().hasPendingInvite()) {
		SendGroupDecline(ctx_.game_state.group().pendingInviterName());
		ctx_.out.System("Group invite declined");
	}
}

// ============================================================================
// Pet Methods
// ============================================================================

const Entity* eqt::group::GroupComponent::GetPetEntity() const
{
	if (ctx_.game_state.pet().petSpawnId() == 0) {
		return nullptr;
	}
	auto it = ctx_.entities.find(ctx_.game_state.pet().petSpawnId());
	if (it != ctx_.entities.end()) {
		return &it->second;
	}
	return nullptr;
}

std::string eqt::group::GroupComponent::GetPetName() const
{
	const Entity* pet = GetPetEntity();
	return pet ? pet->name : "";
}

uint8_t eqt::group::GroupComponent::GetPetLevel() const
{
	const Entity* pet = GetPetEntity();
	return pet ? pet->level : 0;
}

void eqt::group::GroupComponent::SendPetCommand(EQT::PetCommand command, uint16_t target_id)
{
	if (!ctx_.zone_connection_manager || !ctx_.self.IsFullyZonedIn()) {
		LOG_WARN(MOD_MAIN, "Cannot send pet command - not connected to zone");
		return;
	}

	if (!ctx_.self.HasPet()) {
		LOG_WARN(MOD_MAIN, "Cannot send pet command - no pet");
		ctx_.out.System("You do not have a pet.");
		return;
	}

	EQT::PetCommand_Struct packet = {};
	packet.command = static_cast<uint32_t>(command);
	packet.target = target_id;

	EQ::Net::DynamicPacket p;
	p.Resize(2 + sizeof(EQT::PetCommand_Struct));
	p.PutUInt16(0, HC_OP_PetCommands);
	p.PutData(2, &packet, sizeof(packet));

	ctx_.net.SafeQueueZone(p, 0, true);
	LOG_DEBUG(MOD_MAIN, "Sent pet command: {} (target={})", EQT::GetPetCommandName(command), target_id);

	// Optimistically update button states based on command sent.
	// T286: PetState is the only store -- these used to write EverQuest's
	// own m_pet_button_states array WITHOUT going through
	// OnPetButtonStateChanged, so the pet panel never saw a command-driven
	// button change and the two arrays drifted apart on every /pet command.
	// Follow, Guard, Sit are mutually exclusive position commands
	switch (command) {
	case EQT::PET_FOLLOWME:
		ctx_.Bridge().OnPetButtonStateChanged(EQT::PET_BUTTON_FOLLOW, true);
		ctx_.Bridge().OnPetButtonStateChanged(EQT::PET_BUTTON_GUARD, false);
		ctx_.Bridge().OnPetButtonStateChanged(EQT::PET_BUTTON_SIT, false);
		break;
	case EQT::PET_GUARDHERE:
		ctx_.Bridge().OnPetButtonStateChanged(EQT::PET_BUTTON_FOLLOW, false);
		ctx_.Bridge().OnPetButtonStateChanged(EQT::PET_BUTTON_GUARD, true);
		ctx_.Bridge().OnPetButtonStateChanged(EQT::PET_BUTTON_SIT, false);
		break;
	case EQT::PET_SITDOWN:  // PET_SIT is an alias for this
		ctx_.Bridge().OnPetButtonStateChanged(EQT::PET_BUTTON_FOLLOW, false);
		ctx_.Bridge().OnPetButtonStateChanged(EQT::PET_BUTTON_GUARD, false);
		ctx_.Bridge().OnPetButtonStateChanged(EQT::PET_BUTTON_SIT, true);
		break;
	case EQT::PET_STANDUP:
		// Standing up returns to follow mode
		ctx_.Bridge().OnPetButtonStateChanged(EQT::PET_BUTTON_FOLLOW, true);
		ctx_.Bridge().OnPetButtonStateChanged(EQT::PET_BUTTON_GUARD, false);
		ctx_.Bridge().OnPetButtonStateChanged(EQT::PET_BUTTON_SIT, false);
		break;
	// Independent toggles
	case EQT::PET_HOLD:
		ctx_.Bridge().OnPetButtonStateChanged(EQT::PET_BUTTON_HOLD, !ctx_.game_state.pet().getButtonState(EQT::PET_BUTTON_HOLD));
		break;
	case EQT::PET_HOLD_ON:
		ctx_.Bridge().OnPetButtonStateChanged(EQT::PET_BUTTON_HOLD, true);
		break;
	case EQT::PET_HOLD_OFF:
		ctx_.Bridge().OnPetButtonStateChanged(EQT::PET_BUTTON_HOLD, false);
		break;
	case EQT::PET_TAUNT:
		ctx_.Bridge().OnPetButtonStateChanged(EQT::PET_BUTTON_TAUNT, !ctx_.game_state.pet().getButtonState(EQT::PET_BUTTON_TAUNT));
		break;
	case EQT::PET_TAUNT_ON:
		ctx_.Bridge().OnPetButtonStateChanged(EQT::PET_BUTTON_TAUNT, true);
		break;
	case EQT::PET_TAUNT_OFF:
		ctx_.Bridge().OnPetButtonStateChanged(EQT::PET_BUTTON_TAUNT, false);
		break;
	case EQT::PET_FOCUS:
		ctx_.Bridge().OnPetButtonStateChanged(EQT::PET_BUTTON_FOCUS, !ctx_.game_state.pet().getButtonState(EQT::PET_BUTTON_FOCUS));
		break;
	case EQT::PET_SPELLHOLD:
		ctx_.Bridge().OnPetButtonStateChanged(EQT::PET_BUTTON_SPELLHOLD, !ctx_.game_state.pet().getButtonState(EQT::PET_BUTTON_SPELLHOLD));
		break;
	default:
		// Attack, Back Off, Get Lost, etc. don't have toggle states
		break;
	}
}

void eqt::group::GroupComponent::ClearGroup()
{
	// Phase 7.5 / T286: GroupState is the only store
	ctx_.game_state.group().clearGroup();
}

int eqt::group::GroupComponent::FindGroupMemberByName(const std::string& name) const
{
	return ctx_.game_state.group().findMemberByName(name);
}

// Group packet handlers
void eqt::group::GroupComponent::ZoneProcessGroupInvite(const EQ::Net::Packet& p)
{
	if (p.Length() < 2 + sizeof(EQT::GroupInvite_Struct)) {
		LOG_WARN(MOD_MAIN, "GroupInvite packet too small: {} bytes", p.Length());
		return;
	}

	auto* data = reinterpret_cast<const EQT::GroupInvite_Struct*>(static_cast<const char*>(p.Data()) + 2);
	std::string inviter_name(data->inviter_name);
	std::string invitee_name(data->invitee_name);

	LOG_INFO(MOD_MAIN, "Group invite received: {} invited {}", inviter_name, invitee_name);

	// Check if this invite is for us
	if (invitee_name == ctx_.character) {
		// Phase 7.5 / T286: GroupState is the only store
		ctx_.game_state.group().setPendingInvite(inviter_name);

		ctx_.out.System(inviter_name + " has invited you to join a group");

		// Publish GroupInviteReceived event to bridge
		if (ctx_.bridge) {
			eqt::state::GroupInviteReceivedData data;
			data.inviterName = inviter_name;
			ctx_.bridge->pushEvent(eqt::state::GameEvent(
				eqt::state::GameEventType::GroupInviteReceived, std::move(data)));
		}
	}
}

void eqt::group::GroupComponent::ZoneProcessGroupFollow(const EQ::Net::Packet& p)
{
	// This is sent as confirmation that someone joined the group
	LOG_DEBUG(MOD_MAIN, "GroupFollow packet received ({} bytes)", p.Length());
}

// T320: fill in a member's live stats from the entity list, if they are in
// this zone. Shared by the JOIN and UPDATE paths.
void eqt::group::GroupComponent::FillGroupMemberFromEntities(GroupMember& member) const
{
	for (const auto& [id, entity] : ctx_.entities) {
		if (entity.name == member.name) {
			member.spawnId = id;
			member.level = entity.level;
			member.classId = entity.class_id;
			member.hpPercent = entity.hp_percent;
			member.inZone = true;
			return;
		}
	}
}

// T320: eqemu does NOT use one struct for every OP_GroupUpdate action.
//
//   groupActJoin (0) / groupActLeave (1) -> GroupJoin_Struct
//        action(4) yourname[64] membername[64] <padding to 452>
//   everything else                      -> GroupUpdate_Struct / GroupUpdate2
//        action(4) yourname[64] membername[5][64] leadersname[64]
//
// Both are 452 bytes on the wire (eqemu zone/groups.cpp: AddMember and
// DelMember allocate sizeof(GroupJoin_Struct), which is also 452 in
// common/eq_packet_structs.h), so a SIZE CHECK CANNOT TELL THEM APART. This
// handler used to read every action as a GroupUpdate_Struct, which meant a
// JOIN had its `leadersname` read out of GroupJoin_Struct's padding: all
// zeroes. That is the measured "In group (2 members), leader: <empty>" --
// count and roster disagreeing because the leader came from padding.
//
// It is reachable with NO zoning at all. On Titanium the INVITER is told a
// member joined with groupActJoin and nothing else -- eqemu only sends the
// inviter a full refresh on SoD+ (zone/client.cpp:5206,
// `RefreshGroupFromDB(inviter)` guarded by ClientVersion() >= SoD) -- so
// every group this client formed had an empty leader from the moment it was
// formed. Live evidence: review/T320/b1_gmon_A_testchar.log line 1360
// ("Group update: 2 members, leader: ") against the same instant on the
// invitee, review/T320/b1_gmon_B_oraclerogue.log line 314 ("leader:
// Testchar"), and T297's review before it.
void eqt::group::GroupComponent::ZoneProcessGroupUpdate(const EQ::Net::Packet& p)
{
	// Only `action` is common to both layouts, so read that first and let
	// each case demand the size ITS struct needs.
	if (p.Length() < 2 + sizeof(uint32_t)) {
		LOG_WARN(MOD_MAIN, "GroupUpdate packet too small: {} bytes", p.Length());
		return;
	}
	const char* const body = static_cast<const char*>(p.Data()) + 2;
	uint32_t action = 0;
	std::memcpy(&action, body, sizeof(action));

	if (action == EQT::GROUP_ACT_JOIN) {
		ZoneProcessGroupJoin(p);
		return;
	}

	if (p.Length() < 2 + sizeof(EQT::GroupUpdate_Struct)) {
		LOG_WARN(MOD_MAIN, "GroupUpdate packet too small: {} bytes", p.Length());
		return;
	}

	auto* data = reinterpret_cast<const EQT::GroupUpdate_Struct*>(body);

	switch (data->action) {
		case EQT::GROUP_ACT_UPDATE:
		{
			// Full group update
			ClearGroup();
			const std::string leaderName(data->leadersname);
			const bool isLeader = (leaderName == ctx_.character);

			// T320: populate the roster and the leader BEFORE flipping
			// inGroup. setInGroup(true) fires a GroupChanged, and it used to
			// fire it here -- immediately after ClearGroup() -- so the first
			// thing every consumer saw for a new group was inGroup=true with
			// memberCount 0 and an empty leader name. Ordering it last makes
			// the only in-group GroupChanged a complete one.
			ctx_.game_state.group().setLeaderName(leaderName);
			ctx_.game_state.group().setIsLeader(isLeader);

			// Hide pending invite since we joined
			ctx_.game_state.group().clearPendingInvite();  // Phase 7.5

			// Add self first
			GroupMember self;
			self.name = ctx_.character;
			self.spawnId = ctx_.self.MySpawnId();
			self.level = ctx_.game_state.player().level();
			self.classId = static_cast<uint8_t>(ctx_.game_state.player().classId());
			self.hpPercent = (ctx_.game_state.player().maxHP() > 0) ? static_cast<uint8_t>(ctx_.game_state.player().curHP() * 100 / ctx_.game_state.player().maxHP()) : 100;
			self.manaPercent = (ctx_.game_state.player().maxMana() > 0) ? static_cast<uint8_t>(ctx_.game_state.player().curMana() * 100 / ctx_.game_state.player().maxMana()) : 100;
			self.isLeader = isLeader;
			self.inZone = true;
			ctx_.game_state.group().setMember(0, self);
			int memberCount = 1;

			// Add other members
			for (int i = 0; i < 5 && memberCount < MAX_GROUP_MEMBERS; i++) {
				std::string member_name(data->membername[i]);
				if (!member_name.empty() && member_name != ctx_.character) {
					GroupMember member;
					member.name = member_name;
					member.isLeader = (member_name == leaderName);

					// Try to find entity for HP/mana
					FillGroupMemberFromEntities(member);

					ctx_.game_state.group().setMember(memberCount, member);
					memberCount++;
				}
			}

			// Phase 7.5: Recalculate member count in GameState
			ctx_.game_state.group().recalculateMemberCount();

			// T320: flip inGroup LAST -- leader and roster are real now.
			ctx_.game_state.group().setInGroup(true);

			ctx_.out.System("Group updated");
			LOG_INFO(MOD_MAIN, "Group update: {} members, leader: {}",
					 ctx_.game_state.group().memberCount(), leaderName);

			// Publish GroupChanged event to bridge
			if (ctx_.bridge) {
				eqt::state::GroupChangedData gdata;
				gdata.inGroup = true;
				gdata.isLeader = isLeader;
				gdata.leaderName = leaderName;
				gdata.memberCount = ctx_.game_state.group().memberCount();
				ctx_.bridge->pushEvent(eqt::state::GameEvent(
					eqt::state::GameEventType::GroupChanged, std::move(gdata)));

				// Publish GroupMemberUpdated for each member
				for (int mi = 0; mi < ctx_.game_state.group().memberCount(); mi++) {
					const GroupMember* gm = ctx_.game_state.group().getMember(mi);
					if (!gm) continue;
					eqt::state::GroupMemberUpdatedData mdata;
					mdata.memberIndex = mi;
					mdata.name = gm->name;
					mdata.spawnId = gm->spawnId;
					mdata.level = gm->level;
					mdata.classId = gm->classId;
					mdata.hpPercent = gm->hpPercent;
					mdata.manaPercent = gm->manaPercent;
					mdata.inZone = gm->inZone;
					ctx_.bridge->pushEvent(eqt::state::GameEvent(
						eqt::state::GameEventType::GroupMemberUpdated, std::move(mdata)));
				}
			}
			break;
		}

		case EQT::GROUP_ACT_LEAVE:
		{
			// Someone left the group. T320: this action carries a
			// GroupJoin_Struct too (eqemu zone/groups.cpp DelMember), not a
			// GroupUpdate_Struct -- read it as one. The single `membername`
			// happens to sit at the same offset 68 as membername[0] does, so
			// the old cast read the right bytes by luck; say so out loud
			// rather than leave the next reader to rediscover it.
			auto* join = reinterpret_cast<const EQT::GroupJoin_Struct*>(body);
			std::string member_name(join->membername,
									strnlen(join->membername, sizeof(join->membername)));
			int idx = FindGroupMemberByName(member_name);
			if (idx >= 0) {
				// Remove member by shifting (T286: GroupState is the only
				// store, so the shift happens in it directly)
				auto& group = ctx_.game_state.group();
				const int newCount = group.memberCount() - 1;
				for (int i = idx; i < newCount; i++) {
					const GroupMember* next = group.getMember(i + 1);
					group.setMember(i, next ? *next : GroupMember());
				}
				// Clear the slot that was freed
				group.setMember(newCount, GroupMember());
				group.recalculateMemberCount();

				ctx_.out.System(member_name + " has left the group");

				// Publish GroupChanged event to bridge
				if (ctx_.bridge) {
					eqt::state::GroupChangedData gdata;
					gdata.inGroup = ctx_.game_state.group().inGroup();
					gdata.isLeader = ctx_.game_state.group().isLeader();
					gdata.leaderName = ctx_.game_state.group().leaderName();
					gdata.memberCount = ctx_.game_state.group().memberCount();
					ctx_.bridge->pushEvent(eqt::state::GameEvent(
						eqt::state::GameEventType::GroupChanged, std::move(gdata)));
				}
			}
			break;
		}

		case EQT::GROUP_ACT_DISBAND:
			ClearGroup();  // Also syncs to GameState
			ctx_.out.System("Your group has been disbanded");

			// Publish GroupChanged event to bridge
			if (ctx_.bridge) {
				eqt::state::GroupChangedData gdata;
				gdata.inGroup = false;
				gdata.isLeader = false;
				gdata.leaderName = "";
				gdata.memberCount = 0;
				ctx_.bridge->pushEvent(eqt::state::GameEvent(
					eqt::state::GameEventType::GroupChanged, std::move(gdata)));
			}
			break;

		case EQT::GROUP_ACT_MAKE_LEADER:
		{
			std::string new_leader(data->leadersname);

			// Phase 7.5 / T286: GroupState is the only store
			ctx_.game_state.group().setLeaderName(new_leader);
			ctx_.game_state.group().setIsLeader(new_leader == ctx_.character);

			// Update leader flags
			for (int i = 0; i < ctx_.game_state.group().memberCount(); i++) {
				GroupMember* gm = ctx_.game_state.group().getMemberMutable(i);
				if (gm) {
					gm->isLeader = (gm->name == new_leader);
				}
			}

			ctx_.out.System(new_leader + " is now the group leader");

			// Publish GroupChanged event to bridge
			if (ctx_.bridge) {
				eqt::state::GroupChangedData gdata;
				gdata.inGroup = ctx_.game_state.group().inGroup();
				gdata.isLeader = ctx_.game_state.group().isLeader();
				gdata.leaderName = ctx_.game_state.group().leaderName();
				gdata.memberCount = ctx_.game_state.group().memberCount();
				ctx_.bridge->pushEvent(eqt::state::GameEvent(
					eqt::state::GameEventType::GroupChanged, std::move(gdata)));
			}
			break;
		}

		default:
			LOG_DEBUG(MOD_MAIN, "Unhandled group action: {}", data->action);
			break;
	}
}

// T320: groupActJoin -- "this one person just joined your group".
// eqemu sends it, as a GroupJoin_Struct, only to members who were ALREADY in
// the group (zone/groups.cpp Group::AddMember skips the new member). The new
// member instead gets a full groupActUpdate via RefreshGroupFromDB. So this
// is an INCREMENTAL add: it must not clear the roster and must not touch the
// leader, because the packet does not carry one.
void eqt::group::GroupComponent::ZoneProcessGroupJoin(const EQ::Net::Packet& p)
{
	if (p.Length() < 2 + sizeof(EQT::GroupJoin_Struct)) {
		LOG_WARN(MOD_MAIN, "GroupJoin packet too small: {} bytes", p.Length());
		return;
	}

	auto* data = reinterpret_cast<const EQT::GroupJoin_Struct*>(
		static_cast<const char*>(p.Data()) + 2);
	const std::string joiner(data->membername,
							 strnlen(data->membername, sizeof(data->membername)));

	if (joiner.empty() || joiner == ctx_.character) {
		// Nothing to add. Our own name here would mean the server told us we
		// joined ourselves; the full update is what informs a new member.
		return;
	}

	auto& group = ctx_.game_state.group();

	if (!group.inGroup()) {
		// A JOIN while ungrouped means WE created this group by inviting --
		// AddMember only notifies existing members, and the only way to be an
		// existing member of a brand-new group is to be the leader who made
		// it. Titanium gets no other packet saying so (the inviter's
		// RefreshGroupFromDB is SoD+ only), so this is where the client learns
		// it leads the group. Set the leader BEFORE any member, so no
		// consumer ever sees members with no leader.
		group.setLeaderName(ctx_.character);
		group.setIsLeader(true);
		group.clearPendingInvite();

		GroupMember self;
		self.name = ctx_.character;
		self.spawnId = ctx_.self.MySpawnId();
		self.level = ctx_.game_state.player().level();
		self.classId = static_cast<uint8_t>(ctx_.game_state.player().classId());
		self.hpPercent = (ctx_.game_state.player().maxHP() > 0)
			? static_cast<uint8_t>(ctx_.game_state.player().curHP() * 100 / ctx_.game_state.player().maxHP()) : 100;
		self.manaPercent = (ctx_.game_state.player().maxMana() > 0)
			? static_cast<uint8_t>(ctx_.game_state.player().curMana() * 100 / ctx_.game_state.player().maxMana()) : 100;
		self.isLeader = true;
		self.inZone = true;
		group.setMember(0, self);
	}

	int slot = group.findMemberByName(joiner);
	if (slot < 0) {
		for (int i = 0; i < MAX_GROUP_MEMBERS; ++i) {
			const GroupMember* gm = group.getMember(i);
			if (gm && gm->name.empty()) { slot = i; break; }
		}
	}
	if (slot < 0) {
		LOG_WARN(MOD_MAIN, "Group join for {} but the group is full", joiner);
		return;
	}

	GroupMember member;
	member.name = joiner;
	member.isLeader = (joiner == group.leaderName());
	FillGroupMemberFromEntities(member);
	group.setMember(slot, member);

	group.recalculateMemberCount();
	// Last, so the one in-group GroupChanged this publishes is complete.
	group.setInGroup(true);

	ctx_.out.System(joiner + " has joined the group");
	LOG_INFO(MOD_MAIN, "Group join: {} ({} members, leader: {})",
			 joiner, group.memberCount(), group.leaderName());

	if (ctx_.bridge) {
		eqt::state::GroupChangedData gdata;
		gdata.inGroup = true;
		gdata.isLeader = group.isLeader();
		gdata.leaderName = group.leaderName();
		gdata.memberCount = group.memberCount();
		ctx_.bridge->pushEvent(eqt::state::GameEvent(
			eqt::state::GameEventType::GroupChanged, std::move(gdata)));

		for (int mi = 0; mi < MAX_GROUP_MEMBERS; ++mi) {
			const GroupMember* gm = group.getMember(mi);
			if (!gm || gm->name.empty()) continue;
			eqt::state::GroupMemberUpdatedData mdata;
			mdata.memberIndex = mi;
			mdata.name = gm->name;
			mdata.spawnId = gm->spawnId;
			mdata.level = gm->level;
			mdata.classId = gm->classId;
			mdata.hpPercent = gm->hpPercent;
			mdata.manaPercent = gm->manaPercent;
			mdata.inZone = gm->inZone;
			ctx_.bridge->pushEvent(eqt::state::GameEvent(
				eqt::state::GameEventType::GroupMemberUpdated, std::move(mdata)));
		}
	}
}

void eqt::group::GroupComponent::ZoneProcessGroupDisband(const EQ::Net::Packet& p)
{
	ClearGroup();
	ctx_.out.System("Your group has been disbanded");
	LOG_INFO(MOD_MAIN, "Group disbanded");

	// Publish GroupChanged event to bridge
	if (ctx_.bridge) {
		eqt::state::GroupChangedData gdata;
		gdata.inGroup = false;
		gdata.isLeader = false;
		gdata.leaderName = "";
		gdata.memberCount = 0;
		ctx_.bridge->pushEvent(eqt::state::GameEvent(
			eqt::state::GameEventType::GroupChanged, std::move(gdata)));
	}
}

void eqt::group::GroupComponent::ZoneProcessGroupCancelInvite(const EQ::Net::Packet& p)
{
	// Phase 7.5 / T286: GroupState is the only store
	ctx_.game_state.group().clearPendingInvite();

	ctx_.out.System("Group invite cancelled");
	LOG_INFO(MOD_MAIN, "Group invite cancelled");

	// Publish GroupChanged to trigger bridge hidePendingInvite
	if (ctx_.bridge) {
		eqt::state::GroupChangedData gdata;
		gdata.inGroup = ctx_.game_state.group().inGroup();
		gdata.isLeader = ctx_.game_state.group().isLeader();
		gdata.leaderName = ctx_.game_state.group().leaderName();
		gdata.memberCount = ctx_.game_state.group().memberCount();
		ctx_.bridge->pushEvent(eqt::state::GameEvent(
			eqt::state::GameEventType::GroupChanged, std::move(gdata)));
	}
}

// ============================================================================
// Raid Methods
// ============================================================================

// T169: every raid membership event arrives here, and until now this
// function read one uint32 and switched on six guessed values -- four of
// which disagreed with eqemu's own enum (common/raid.h). What that cost:
//   * action 2 was reported as "member joined"; it is raidMemberNameChange.
//   * action 3 was reported as "the raid leader has changed"; it is
//     raidRemove1, a member LEAVING.
//   * action 6 was reported as "the raid has disbanded"; it is raidMembers,
//     the full member list. The real disband is action 5, which was unhandled.
//   * action 10 was reported as a lock-state update; it is raidNoRaid, the
//     server telling us we are not in a raid. Lock/unlock are 17 and 18.
//   * action 20 -- what a raid INVITE actually produces -- fell through to
//     "Unknown raid action", so an invited player was told NOTHING AT ALL.
// The body was never parsed, so no message could name anyone.
//
// Decoding now lives in DecodeRaidUpdate() (raid_update.cpp), where the
// per-action body shapes are documented and pinned by test. This function is
// only the rendering.
//
// T179 added the accept path and the /raid commands; T180 added the roster
// below and split the zone-in handshake back out of action 10. The member-list
// parse T169 expected (action 6) turned out to be unavailable on Titanium --
// its ENCODE has no case for raidMembers and ships the general with the list
// stripped -- so the roster is assembled from the action-0 stream instead.
// See raid_update.h.
void eqt::group::GroupComponent::ZoneProcessRaidUpdate(const EQ::Net::Packet &p)
{
	const EQT::RaidUpdate u = EQT::DecodeRaidUpdate(p, 2);

	if (!u.valid) {
		LOG_WARN(MOD_ZONE, "RaidUpdate too short for action {} ({}): {} bytes",
		         u.raw_action, EQT::RaidActionName(u.raw_action), p.Length());
		return;
	}

	// T180: not every OP_RaidUpdate is a raid event. Client::CompleteConnect
	// reuses this opcode for the zone-in name handshake, whose first uint32
	// happens to be 0x0A -- the raidNoRaid code. It arrives on EVERY zone
	// entry, with or without a raid. Until T180 this produced a WARN per
	// zone-in ("too short for action 10"); reporting it as "You are not in a
	// raid" instead would have been worse, since it would have cleared real
	// raid membership every time the player zoned.
	if (u.zone_in_name) {
		LOG_DEBUG(MOD_ZONE, "RaidUpdate: zone-in name handshake for '{}' (not a raid event)",
		          u.player_name);
		return;
	}

	LOG_DEBUG(MOD_ZONE, "RaidUpdate action={} ({}) player='{}' leader='{}' parameter={}",
	          u.raw_action, EQT::RaidActionName(u.raw_action),
	          u.player_name, u.leader_name, u.parameter);

	// Membership first, then the message: /raid list must be right the instant
	// the player is told something changed. The policy lives in RaidRoster so
	// it is testable without a live client.
	m_raid_roster.Apply(u, ctx_.character);

	// Every event names a member, and several of them are about US. eqemu
	// sends the same packet to the whole raid, so without this comparison the
	// client would announce "You have joined the raid" for every member.
	const bool about_me = !u.player_name.empty() && u.player_name == ctx_.character;

	switch (u.action) {
	case EQT::RaidAction::Add:
		if (about_me) {
			m_in_raid = true;
			m_pending_raid_inviter.clear();  // accepted / already in
			ctx_.out.System("You have joined the raid.");
		} else {
			ctx_.out.System(fmt::format("{} has joined the raid.", u.player_name));
		}
		break;

	case EQT::RaidAction::Remove1:
	case EQT::RaidAction::Remove2:
		if (about_me) {
			m_in_raid = false;
			m_raid_leader.clear();
			m_raid_locked = false;
			ctx_.out.System("You have left the raid.");
		} else {
			ctx_.out.System(fmt::format("{} has left the raid.", u.player_name));
		}
		break;

	case EQT::RaidAction::Disband:
		m_in_raid = false;
		m_raid_leader.clear();
		m_pending_raid_inviter.clear();
		m_raid_locked = false;
		ctx_.out.System("The raid has disbanded.");
		break;

	case EQT::RaidAction::Create:
		m_in_raid = true;
		m_raid_leader = u.leader_name;
		// A fresh raid is unlocked; Create also starts a fresh roster stream.
		m_raid_locked = false;
		ctx_.out.System(fmt::format("{} has formed a raid.", u.leader_name));
		break;

	case EQT::RaidAction::NoRaid:
		// Only a genuine 140-byte raidNoRaid reaches here; the 136-byte zone-in
		// handshake that also presents 10 returned above (T180).
		m_in_raid = false;
		m_raid_leader.clear();
		m_pending_raid_inviter.clear();
		m_raid_locked = false;
		ctx_.out.System("You are not in a raid.");
		break;

	case EQT::RaidAction::SetLeader:
		// The invite. eqemu builds this in Handle_OP_RaidCommand's
		// RaidCommandInvite arm (client_packet.cpp:12356): leader_name is the
		// player inviting, player_name is us.
		//
		// There is deliberately no accept here. Accepting means sending
		// OP_RaidCommand with action 1 (RaidCommandAcceptInvite),
		// player_name = the inviter, leader_name = us -- a sender this client
		// does not have (T168 deleted the broken raid sender rather than
		// leave it parked). Saying so out loud beats the previous behaviour,
		// which was to say nothing and log "Unknown raid action: 20".
		// T179: leader_name is the inviter (see the decode header). Remember it
		// so /raid accept can answer, and tell the player how.
		m_pending_raid_inviter = u.leader_name;
		ctx_.out.System(fmt::format(
			"{} invites you to join a raid. Type /raid accept to join.",
			u.leader_name));
		break;

	case EQT::RaidAction::MakeLeader:
		m_raid_leader = u.player_name;
		ctx_.out.System(fmt::format("{} is now the raid leader.", u.player_name));
		break;

	case EQT::RaidAction::NoLongerLeader:
		ctx_.out.System("You are no longer the raid leader.");
		break;

	case EQT::RaidAction::Lock:
	case EQT::RaidAction::Unlock: {
		// T182. eqemu sends the SAME action whether the state just CHANGED
		// (LockRaid -> SendRaidLock, broadcast to the raid) or was ALREADY
		// what you asked for (Handle_OP_RaidCommand -> SendRaidLockTo, to you
		// alone). The packets are identical, so "now locked" vs "already
		// locked" can only come from the state we track -- which is also why
		// we track it. It is re-established on zone-in: the rejoin path sends
		// SendRaidLockTo only when the raid is locked.
		const bool locking = (u.action == EQT::RaidAction::Lock);
		const bool changed = (m_raid_locked != locking);
		m_raid_locked = locking;
		if (changed) {
			ctx_.out.System(locking ? "The raid is now locked."
			                             : "The raid is now unlocked.");
		} else {
			ctx_.out.System(locking ? "The raid is already locked."
			                             : "The raid is not locked.");
		}
		break;
	}

	case EQT::RaidAction::SetMotd:
		if (!u.text.empty()) {
			ctx_.out.System(fmt::format("Raid MOTD: {}", u.text));
		}
		break;

	case EQT::RaidAction::SetNote:
		LOG_DEBUG(MOD_ZONE, "Raid note for {}: '{}'", u.player_name, u.text);
		break;

	case EQT::RaidAction::Members:
		// The roster (details + member list, 395+ bytes). Parsing it is the
		// raid unit's job; announcing something we have not parsed would be
		// the same class of guess this unit is removing.
		LOG_DEBUG(MOD_ZONE, "Raid member list received ({} bytes) -- not parsed", p.Length());
		break;

	default:
		// Named, not guessed: known-but-unhandled and genuinely unknown are
		// different facts and now read differently in the log.
		LOG_DEBUG(MOD_ZONE, "Raid action {} ({}) not handled by this client",
		          u.raw_action, EQT::RaidActionName(u.raw_action));
		break;
	}
}

// T179: send an OP_RaidCommand (Titanium wire opcode OP_RaidInvite, 0x5891,
// routed by eqemu to Handle_OP_RaidCommand). The body is the 140-byte internal
// RaidGeneral_Struct built by EQT::encodeRaidCommand -- the handler rejects
// anything shorter. Called only from the action layer (/raid commands).
void eqt::group::GroupComponent::SendRaidCommand(uint32_t action, const std::string& player_name,
                               const std::string& leader_name, uint32_t parameter)
{
	if (!ctx_.zone_connection || !ctx_.game_state.world().isZoneConnected()) {
		LOG_WARN(MOD_ZONE, "SendRaidCommand: not connected");
		return;
	}

	const std::vector<uint8_t> body =
		EQT::encodeRaidCommand(action, player_name, leader_name, parameter);

	EQ::Net::DynamicPacket p;
	p.PutUInt16(0, HC_OP_RaidInvite);
	p.PutData(2, const_cast<uint8_t*>(body.data()), body.size());
	ctx_.net.SafeQueueZone(p, 0, true);

	LOG_INFO(MOD_ZONE, "Sent OP_RaidCommand action={} player='{}' leader='{}'",
	         action, player_name, leader_name);
}
