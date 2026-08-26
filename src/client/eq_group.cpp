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

#include "client/eq.h"
#include "client/bridge/game_state_bridge.h"
#include "common/logging.h"
#include "common/packet_structs.h"

#include <cstring>

// ============================================================================
// Group Methods
// ============================================================================

const GroupMember* EverQuest::GetGroupMember(int index) const
{
	// T286: GroupState is the only store.
	if (index >= 0 && index < m_game_state.group().memberCount()) {
		return m_game_state.group().getMember(index);
	}
	return nullptr;
}

void EverQuest::SendGroupInvite(const std::string& target_name)
{
	if (!m_zone_connection_manager || !IsFullyZonedIn()) {
		LOG_WARN(MOD_MAIN, "Cannot send group invite - not connected to zone");
		return;
	}

	EQT::GroupInvite_Struct packet = {};
	strncpy(packet.inviter_name, m_character.c_str(), 63);
	strncpy(packet.invitee_name, target_name.c_str(), 63);

	EQ::Net::DynamicPacket p;
	p.Resize(2 + sizeof(EQT::GroupInvite_Struct));
	p.PutUInt16(0, HC_OP_GroupInvite);
	p.PutData(2, &packet, sizeof(packet));

	m_zone_connection->QueuePacket(p, 0, true);
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
void EverQuest::SendGroupFollow(const std::string& inviter_name)
{
	if (!m_test_bypass_send_guard && (!m_zone_connection_manager || !IsFullyZonedIn())) {
		LOG_WARN(MOD_MAIN, "Cannot accept group invite - not connected to zone");
		return;
	}

	const std::string inviter = inviter_name;

	EQT::GroupFollow_Struct packet = {};
	strncpy(packet.name1, inviter.c_str(), 63);
	strncpy(packet.name2, m_character.c_str(), 63);

	EQ::Net::DynamicPacket p;
	p.Resize(2 + sizeof(EQT::GroupFollow_Struct));
	p.PutUInt16(0, HC_OP_GroupFollow);
	p.PutData(2, &packet, sizeof(packet));

	if (m_zone_connection) {
		m_zone_connection->QueuePacket(p, 0, true);
	}

	m_game_state.group().clearPendingInvite();  // T286: only store

	LOG_INFO(MOD_MAIN, "Accepted group invite from {}", inviter);
}

// T217: same aliasing hazard as SendGroupFollow() above -- copy first.
void EverQuest::SendGroupDecline(const std::string& inviter_name)
{
	if (!m_test_bypass_send_guard && (!m_zone_connection_manager || !IsFullyZonedIn())) {
		LOG_WARN(MOD_MAIN, "Cannot decline group invite - not connected to zone");
		return;
	}

	const std::string inviter = inviter_name;

	EQT::GroupCancel_Struct packet = {};
	strncpy(packet.name1, inviter.c_str(), 63);
	strncpy(packet.name2, m_character.c_str(), 63);
	packet.toggle = 0;

	EQ::Net::DynamicPacket p;
	p.Resize(2 + sizeof(EQT::GroupCancel_Struct));
	p.PutUInt16(0, HC_OP_GroupCancelInvite);
	p.PutData(2, &packet, sizeof(packet));

	if (m_zone_connection) {
		m_zone_connection->QueuePacket(p, 0, true);
	}

	m_game_state.group().clearPendingInvite();  // T286: only store

	LOG_INFO(MOD_MAIN, "Declined group invite from {}", inviter);
}

void EverQuest::SendGroupDisband()
{
	if (!m_zone_connection_manager || !IsFullyZonedIn() || !m_game_state.group().inGroup()) {
		LOG_WARN(MOD_MAIN, "Cannot disband group - not in group or not connected");
		return;
	}

	EQT::GroupDisband_Struct packet = {};
	strncpy(packet.name1, m_character.c_str(), 63);
	strncpy(packet.name2, m_character.c_str(), 63);

	EQ::Net::DynamicPacket p;
	p.Resize(2 + sizeof(EQT::GroupDisband_Struct));
	p.PutUInt16(0, HC_OP_GroupDisband);
	p.PutData(2, &packet, sizeof(packet));

	m_zone_connection->QueuePacket(p, 0, true);
	LOG_INFO(MOD_MAIN, "Sent group disband");
}

void EverQuest::SendLeaveGroup()
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
void EverQuest::AcceptGroupInvite()
{
	if (m_game_state.group().hasPendingInvite()) {
		const std::string inviter = m_game_state.group().pendingInviterName();
		SendGroupFollow(inviter);
		AddChatSystemMessage("You have joined " + inviter + "'s group");
	}
}

void EverQuest::DeclineGroupInvite()
{
	if (m_game_state.group().hasPendingInvite()) {
		SendGroupDecline(m_game_state.group().pendingInviterName());
		AddChatSystemMessage("Group invite declined");
	}
}

// ============================================================================
// Pet Methods
// ============================================================================

const Entity* EverQuest::GetPetEntity() const
{
	if (m_game_state.pet().petSpawnId() == 0) {
		return nullptr;
	}
	auto it = m_entities.find(m_game_state.pet().petSpawnId());
	if (it != m_entities.end()) {
		return &it->second;
	}
	return nullptr;
}

std::string EverQuest::GetPetName() const
{
	const Entity* pet = GetPetEntity();
	return pet ? pet->name : "";
}

uint8_t EverQuest::GetPetLevel() const
{
	const Entity* pet = GetPetEntity();
	return pet ? pet->level : 0;
}

void EverQuest::SendPetCommand(EQT::PetCommand command, uint16_t target_id)
{
	if (!m_zone_connection_manager || !IsFullyZonedIn()) {
		LOG_WARN(MOD_MAIN, "Cannot send pet command - not connected to zone");
		return;
	}

	if (!HasPet()) {
		LOG_WARN(MOD_MAIN, "Cannot send pet command - no pet");
		AddChatSystemMessage("You do not have a pet.");
		return;
	}

	EQT::PetCommand_Struct packet = {};
	packet.command = static_cast<uint32_t>(command);
	packet.target = target_id;

	EQ::Net::DynamicPacket p;
	p.Resize(2 + sizeof(EQT::PetCommand_Struct));
	p.PutUInt16(0, HC_OP_PetCommands);
	p.PutData(2, &packet, sizeof(packet));

	m_zone_connection->QueuePacket(p, 0, true);
	LOG_DEBUG(MOD_MAIN, "Sent pet command: {} (target={})", EQT::GetPetCommandName(command), target_id);

	// Optimistically update button states based on command sent.
	// T286: PetState is the only store -- these used to write EverQuest's
	// own m_pet_button_states array WITHOUT going through
	// OnPetButtonStateChanged, so the pet panel never saw a command-driven
	// button change and the two arrays drifted apart on every /pet command.
	// Follow, Guard, Sit are mutually exclusive position commands
	switch (command) {
	case EQT::PET_FOLLOWME:
		OnPetButtonStateChanged(EQT::PET_BUTTON_FOLLOW, true);
		OnPetButtonStateChanged(EQT::PET_BUTTON_GUARD, false);
		OnPetButtonStateChanged(EQT::PET_BUTTON_SIT, false);
		break;
	case EQT::PET_GUARDHERE:
		OnPetButtonStateChanged(EQT::PET_BUTTON_FOLLOW, false);
		OnPetButtonStateChanged(EQT::PET_BUTTON_GUARD, true);
		OnPetButtonStateChanged(EQT::PET_BUTTON_SIT, false);
		break;
	case EQT::PET_SITDOWN:  // PET_SIT is an alias for this
		OnPetButtonStateChanged(EQT::PET_BUTTON_FOLLOW, false);
		OnPetButtonStateChanged(EQT::PET_BUTTON_GUARD, false);
		OnPetButtonStateChanged(EQT::PET_BUTTON_SIT, true);
		break;
	case EQT::PET_STANDUP:
		// Standing up returns to follow mode
		OnPetButtonStateChanged(EQT::PET_BUTTON_FOLLOW, true);
		OnPetButtonStateChanged(EQT::PET_BUTTON_GUARD, false);
		OnPetButtonStateChanged(EQT::PET_BUTTON_SIT, false);
		break;
	// Independent toggles
	case EQT::PET_HOLD:
		OnPetButtonStateChanged(EQT::PET_BUTTON_HOLD, !m_game_state.pet().getButtonState(EQT::PET_BUTTON_HOLD));
		break;
	case EQT::PET_HOLD_ON:
		OnPetButtonStateChanged(EQT::PET_BUTTON_HOLD, true);
		break;
	case EQT::PET_HOLD_OFF:
		OnPetButtonStateChanged(EQT::PET_BUTTON_HOLD, false);
		break;
	case EQT::PET_TAUNT:
		OnPetButtonStateChanged(EQT::PET_BUTTON_TAUNT, !m_game_state.pet().getButtonState(EQT::PET_BUTTON_TAUNT));
		break;
	case EQT::PET_TAUNT_ON:
		OnPetButtonStateChanged(EQT::PET_BUTTON_TAUNT, true);
		break;
	case EQT::PET_TAUNT_OFF:
		OnPetButtonStateChanged(EQT::PET_BUTTON_TAUNT, false);
		break;
	case EQT::PET_FOCUS:
		OnPetButtonStateChanged(EQT::PET_BUTTON_FOCUS, !m_game_state.pet().getButtonState(EQT::PET_BUTTON_FOCUS));
		break;
	case EQT::PET_SPELLHOLD:
		OnPetButtonStateChanged(EQT::PET_BUTTON_SPELLHOLD, !m_game_state.pet().getButtonState(EQT::PET_BUTTON_SPELLHOLD));
		break;
	default:
		// Attack, Back Off, Get Lost, etc. don't have toggle states
		break;
	}
}

void EverQuest::DismissPet()
{
	SendPetCommand(EQT::PET_GETLOST);
}

void EverQuest::ClearGroup()
{
	// Phase 7.5 / T286: GroupState is the only store
	m_game_state.group().clearGroup();
}

int EverQuest::FindGroupMemberByName(const std::string& name) const
{
	return m_game_state.group().findMemberByName(name);
}

void EverQuest::UpdateGroupMemberFromEntity(int index)
{
	if (index < 0 || index >= m_game_state.group().memberCount()) {
		return;
	}

	GroupMember* member = m_game_state.group().getMemberMutable(index);
	if (!member) {
		return;
	}

	// Find entity by name
	for (const auto& [id, entity] : m_entities) {
		if (entity.name == member->name) {
			member->spawnId = id;
			member->level = entity.level;
			member->classId = entity.class_id;
			member->hpPercent = entity.hp_percent;
			member->inZone = true;
			return;
		}
	}

	// Entity not found - mark as out of zone
	member->spawnId = 0;
	member->inZone = false;
}

// Group packet handlers
void EverQuest::ZoneProcessGroupInvite(const EQ::Net::Packet& p)
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
	if (invitee_name == m_character) {
		// Phase 7.5 / T286: GroupState is the only store
		m_game_state.group().setPendingInvite(inviter_name);

		AddChatSystemMessage(inviter_name + " has invited you to join a group");

		// Publish GroupInviteReceived event to bridge
		if (m_bridge) {
			eqt::state::GroupInviteReceivedData data;
			data.inviterName = inviter_name;
			m_bridge->pushEvent(eqt::state::GameEvent(
				eqt::state::GameEventType::GroupInviteReceived, std::move(data)));
		}
	}
}

void EverQuest::ZoneProcessGroupFollow(const EQ::Net::Packet& p)
{
	// This is sent as confirmation that someone joined the group
	LOG_DEBUG(MOD_MAIN, "GroupFollow packet received ({} bytes)", p.Length());
}

void EverQuest::ZoneProcessGroupUpdate(const EQ::Net::Packet& p)
{
	if (p.Length() < 2 + sizeof(EQT::GroupUpdate_Struct)) {
		LOG_WARN(MOD_MAIN, "GroupUpdate packet too small: {} bytes", p.Length());
		return;
	}

	auto* data = reinterpret_cast<const EQT::GroupUpdate_Struct*>(static_cast<const char*>(p.Data()) + 2);

	switch (data->action) {
		case EQT::GROUP_ACT_UPDATE:
		case EQT::GROUP_ACT_JOIN:
		{
			// Full group update
			ClearGroup();
			const std::string leaderName(data->leadersname);
			const bool isLeader = (leaderName == m_character);

			// Phase 7.5 / T286: GroupState is the only store
			m_game_state.group().setInGroup(true);
			m_game_state.group().setLeaderName(leaderName);
			m_game_state.group().setIsLeader(isLeader);

			// Hide pending invite since we joined
			m_game_state.group().clearPendingInvite();  // Phase 7.5

			// Add self first
			GroupMember self;
			self.name = m_character;
			self.spawnId = MySpawnId();
			self.level = m_game_state.player().level();
			self.classId = static_cast<uint8_t>(m_game_state.player().classId());
			self.hpPercent = (m_game_state.player().maxHP() > 0) ? static_cast<uint8_t>(m_game_state.player().curHP() * 100 / m_game_state.player().maxHP()) : 100;
			self.manaPercent = (m_game_state.player().maxMana() > 0) ? static_cast<uint8_t>(m_game_state.player().curMana() * 100 / m_game_state.player().maxMana()) : 100;
			self.isLeader = isLeader;
			self.inZone = true;
			m_game_state.group().setMember(0, self);
			int memberCount = 1;

			// Add other members
			for (int i = 0; i < 5 && memberCount < MAX_GROUP_MEMBERS; i++) {
				std::string member_name(data->membername[i]);
				if (!member_name.empty() && member_name != m_character) {
					GroupMember member;
					member.name = member_name;
					member.isLeader = (member_name == leaderName);

					// Try to find entity for HP/mana
					for (const auto& [id, entity] : m_entities) {
						if (entity.name == member_name) {
							member.spawnId = id;
							member.level = entity.level;
							member.classId = entity.class_id;
							member.hpPercent = entity.hp_percent;
							member.inZone = true;
							break;
						}
					}

					m_game_state.group().setMember(memberCount, member);
					memberCount++;
				}
			}

			// Phase 7.5: Recalculate member count in GameState
			m_game_state.group().recalculateMemberCount();

			AddChatSystemMessage("Group updated");
			LOG_INFO(MOD_MAIN, "Group update: {} members, leader: {}",
					 m_game_state.group().memberCount(), leaderName);

			// Publish GroupChanged event to bridge
			if (m_bridge) {
				eqt::state::GroupChangedData gdata;
				gdata.inGroup = true;
				gdata.isLeader = isLeader;
				gdata.leaderName = leaderName;
				gdata.memberCount = m_game_state.group().memberCount();
				m_bridge->pushEvent(eqt::state::GameEvent(
					eqt::state::GameEventType::GroupChanged, std::move(gdata)));

				// Publish GroupMemberUpdated for each member
				for (int mi = 0; mi < m_game_state.group().memberCount(); mi++) {
					const GroupMember* gm = m_game_state.group().getMember(mi);
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
					m_bridge->pushEvent(eqt::state::GameEvent(
						eqt::state::GameEventType::GroupMemberUpdated, std::move(mdata)));
				}
			}
			break;
		}

		case EQT::GROUP_ACT_LEAVE:
		{
			// Someone left the group
			std::string member_name(data->membername[0]);
			int idx = FindGroupMemberByName(member_name);
			if (idx >= 0) {
				// Remove member by shifting (T286: GroupState is the only
				// store, so the shift happens in it directly)
				auto& group = m_game_state.group();
				const int newCount = group.memberCount() - 1;
				for (int i = idx; i < newCount; i++) {
					const GroupMember* next = group.getMember(i + 1);
					group.setMember(i, next ? *next : GroupMember());
				}
				// Clear the slot that was freed
				group.setMember(newCount, GroupMember());
				group.recalculateMemberCount();

				AddChatSystemMessage(member_name + " has left the group");

				// Publish GroupChanged event to bridge
				if (m_bridge) {
					eqt::state::GroupChangedData gdata;
					gdata.inGroup = m_game_state.group().inGroup();
					gdata.isLeader = m_game_state.group().isLeader();
					gdata.leaderName = m_game_state.group().leaderName();
					gdata.memberCount = m_game_state.group().memberCount();
					m_bridge->pushEvent(eqt::state::GameEvent(
						eqt::state::GameEventType::GroupChanged, std::move(gdata)));
				}
			}
			break;
		}

		case EQT::GROUP_ACT_DISBAND:
			ClearGroup();  // Also syncs to GameState
			AddChatSystemMessage("Your group has been disbanded");

			// Publish GroupChanged event to bridge
			if (m_bridge) {
				eqt::state::GroupChangedData gdata;
				gdata.inGroup = false;
				gdata.isLeader = false;
				gdata.leaderName = "";
				gdata.memberCount = 0;
				m_bridge->pushEvent(eqt::state::GameEvent(
					eqt::state::GameEventType::GroupChanged, std::move(gdata)));
			}
			break;

		case EQT::GROUP_ACT_MAKE_LEADER:
		{
			std::string new_leader(data->leadersname);

			// Phase 7.5 / T286: GroupState is the only store
			m_game_state.group().setLeaderName(new_leader);
			m_game_state.group().setIsLeader(new_leader == m_character);

			// Update leader flags
			for (int i = 0; i < m_game_state.group().memberCount(); i++) {
				GroupMember* gm = m_game_state.group().getMemberMutable(i);
				if (gm) {
					gm->isLeader = (gm->name == new_leader);
				}
			}

			AddChatSystemMessage(new_leader + " is now the group leader");

			// Publish GroupChanged event to bridge
			if (m_bridge) {
				eqt::state::GroupChangedData gdata;
				gdata.inGroup = m_game_state.group().inGroup();
				gdata.isLeader = m_game_state.group().isLeader();
				gdata.leaderName = m_game_state.group().leaderName();
				gdata.memberCount = m_game_state.group().memberCount();
				m_bridge->pushEvent(eqt::state::GameEvent(
					eqt::state::GameEventType::GroupChanged, std::move(gdata)));
			}
			break;
		}

		default:
			LOG_DEBUG(MOD_MAIN, "Unhandled group action: {}", data->action);
			break;
	}
}

void EverQuest::ZoneProcessGroupDisband(const EQ::Net::Packet& p)
{
	ClearGroup();
	AddChatSystemMessage("Your group has been disbanded");
	LOG_INFO(MOD_MAIN, "Group disbanded");

	// Publish GroupChanged event to bridge
	if (m_bridge) {
		eqt::state::GroupChangedData gdata;
		gdata.inGroup = false;
		gdata.isLeader = false;
		gdata.leaderName = "";
		gdata.memberCount = 0;
		m_bridge->pushEvent(eqt::state::GameEvent(
			eqt::state::GameEventType::GroupChanged, std::move(gdata)));
	}
}

void EverQuest::ZoneProcessGroupCancelInvite(const EQ::Net::Packet& p)
{
	// Phase 7.5 / T286: GroupState is the only store
	m_game_state.group().clearPendingInvite();

	AddChatSystemMessage("Group invite cancelled");
	LOG_INFO(MOD_MAIN, "Group invite cancelled");

	// Publish GroupChanged to trigger bridge hidePendingInvite
	if (m_bridge) {
		eqt::state::GroupChangedData gdata;
		gdata.inGroup = m_game_state.group().inGroup();
		gdata.isLeader = m_game_state.group().isLeader();
		gdata.leaderName = m_game_state.group().leaderName();
		gdata.memberCount = m_game_state.group().memberCount();
		m_bridge->pushEvent(eqt::state::GameEvent(
			eqt::state::GameEventType::GroupChanged, std::move(gdata)));
	}
}
