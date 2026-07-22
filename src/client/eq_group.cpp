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
	if (index >= 0 && index < m_group_member_count) {
		return &m_group_members[index];
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

void EverQuest::SendGroupFollow(const std::string& inviter_name)
{
	if (!m_zone_connection_manager || !IsFullyZonedIn()) {
		LOG_WARN(MOD_MAIN, "Cannot accept group invite - not connected to zone");
		return;
	}

	EQT::GroupFollow_Struct packet = {};
	strncpy(packet.name1, inviter_name.c_str(), 63);
	strncpy(packet.name2, m_character.c_str(), 63);

	EQ::Net::DynamicPacket p;
	p.Resize(2 + sizeof(EQT::GroupFollow_Struct));
	p.PutUInt16(0, HC_OP_GroupFollow);
	p.PutData(2, &packet, sizeof(packet));

	m_zone_connection->QueuePacket(p, 0, true);

	m_has_pending_invite = false;
	m_pending_inviter_name.clear();

	LOG_INFO(MOD_MAIN, "Accepted group invite from {}", inviter_name);
}

void EverQuest::SendGroupDecline(const std::string& inviter_name)
{
	if (!m_zone_connection_manager || !IsFullyZonedIn()) {
		LOG_WARN(MOD_MAIN, "Cannot decline group invite - not connected to zone");
		return;
	}

	EQT::GroupCancel_Struct packet = {};
	strncpy(packet.name1, inviter_name.c_str(), 63);
	strncpy(packet.name2, m_character.c_str(), 63);
	packet.toggle = 0;

	EQ::Net::DynamicPacket p;
	p.Resize(2 + sizeof(EQT::GroupCancel_Struct));
	p.PutUInt16(0, HC_OP_GroupCancelInvite);
	p.PutData(2, &packet, sizeof(packet));

	m_zone_connection->QueuePacket(p, 0, true);

	m_has_pending_invite = false;
	m_pending_inviter_name.clear();

	LOG_INFO(MOD_MAIN, "Declined group invite from {}", inviter_name);
}

void EverQuest::SendGroupDisband()
{
	if (!m_zone_connection_manager || !IsFullyZonedIn() || !m_in_group) {
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

void EverQuest::SendRaidInvite(const std::string& target_name)
{
	if (!m_zone_connection_manager || !IsFullyZonedIn()) {
		LOG_WARN(MOD_MAIN, "Cannot send raid invite - not connected to zone");
		return;
	}

	if (target_name.empty()) {
		LOG_WARN(MOD_MAIN, "Cannot send raid invite - no target name specified");
		return;
	}

	LOG_INFO(MOD_MAIN, "Sending raid invite to {}", target_name);

	// RaidInvite packet uses a name string (64 bytes max)
	EQ::Net::DynamicPacket p;
	p.Resize(2 + 64);  // opcode + name
	p.PutUInt16(0, HC_OP_RaidInvite);

	// Copy name with null termination
	std::string padded_name = target_name;
	padded_name.resize(64, '\0');
	p.PutData(2, padded_name.data(), 64);

	m_zone_connection->QueuePacket(p, 0, true);
	AddChatSystemMessage(fmt::format("Inviting {} to raid.", target_name));
}

void EverQuest::AcceptGroupInvite()
{
	if (m_has_pending_invite) {
		SendGroupFollow(m_pending_inviter_name);
		AddChatSystemMessage("You have joined " + m_pending_inviter_name + "'s group");
	}
}

void EverQuest::DeclineGroupInvite()
{
	if (m_has_pending_invite) {
		SendGroupDecline(m_pending_inviter_name);
		AddChatSystemMessage("Group invite declined");
	}
}

// ============================================================================
// Pet Methods
// ============================================================================

const Entity* EverQuest::GetPetEntity() const
{
	if (m_pet_spawn_id == 0) {
		return nullptr;
	}
	auto it = m_entities.find(m_pet_spawn_id);
	if (it != m_entities.end()) {
		return &it->second;
	}
	return nullptr;
}

uint8_t EverQuest::GetPetHpPercent() const
{
	const Entity* pet = GetPetEntity();
	return pet ? pet->hp_percent : 0;
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

bool EverQuest::GetPetButtonState(EQT::PetButton button) const
{
	if (button >= EQT::PET_BUTTON_COUNT) {
		return false;
	}
	return m_pet_button_states[button];
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

	// Optimistically update button states based on command sent
	// Follow, Guard, Sit are mutually exclusive position commands
	switch (command) {
	case EQT::PET_FOLLOWME:
		m_pet_button_states[EQT::PET_BUTTON_FOLLOW] = true;
		m_pet_button_states[EQT::PET_BUTTON_GUARD] = false;
		m_pet_button_states[EQT::PET_BUTTON_SIT] = false;
		break;
	case EQT::PET_GUARDHERE:
		m_pet_button_states[EQT::PET_BUTTON_FOLLOW] = false;
		m_pet_button_states[EQT::PET_BUTTON_GUARD] = true;
		m_pet_button_states[EQT::PET_BUTTON_SIT] = false;
		break;
	case EQT::PET_SITDOWN:  // PET_SIT is an alias for this
		m_pet_button_states[EQT::PET_BUTTON_FOLLOW] = false;
		m_pet_button_states[EQT::PET_BUTTON_GUARD] = false;
		m_pet_button_states[EQT::PET_BUTTON_SIT] = true;
		break;
	case EQT::PET_STANDUP:
		// Standing up returns to follow mode
		m_pet_button_states[EQT::PET_BUTTON_FOLLOW] = true;
		m_pet_button_states[EQT::PET_BUTTON_GUARD] = false;
		m_pet_button_states[EQT::PET_BUTTON_SIT] = false;
		break;
	// Independent toggles
	case EQT::PET_HOLD:
		m_pet_button_states[EQT::PET_BUTTON_HOLD] = !m_pet_button_states[EQT::PET_BUTTON_HOLD];
		break;
	case EQT::PET_HOLD_ON:
		m_pet_button_states[EQT::PET_BUTTON_HOLD] = true;
		break;
	case EQT::PET_HOLD_OFF:
		m_pet_button_states[EQT::PET_BUTTON_HOLD] = false;
		break;
	case EQT::PET_TAUNT:
		m_pet_button_states[EQT::PET_BUTTON_TAUNT] = !m_pet_button_states[EQT::PET_BUTTON_TAUNT];
		break;
	case EQT::PET_TAUNT_ON:
		m_pet_button_states[EQT::PET_BUTTON_TAUNT] = true;
		break;
	case EQT::PET_TAUNT_OFF:
		m_pet_button_states[EQT::PET_BUTTON_TAUNT] = false;
		break;
	case EQT::PET_FOCUS:
		m_pet_button_states[EQT::PET_BUTTON_FOCUS] = !m_pet_button_states[EQT::PET_BUTTON_FOCUS];
		break;
	case EQT::PET_SPELLHOLD:
		m_pet_button_states[EQT::PET_BUTTON_SPELLHOLD] = !m_pet_button_states[EQT::PET_BUTTON_SPELLHOLD];
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
	m_in_group = false;
	m_is_group_leader = false;
	m_group_leader_name.clear();
	m_group_member_count = 0;
	for (auto& member : m_group_members) {
		member = GroupMember();
	}

	// Phase 7.5: Sync to GameState
	m_game_state.group().clearGroup();
}

int EverQuest::FindGroupMemberByName(const std::string& name) const
{
	for (int i = 0; i < m_group_member_count; i++) {
		if (m_group_members[i].name == name) {
			return i;
		}
	}
	return -1;
}

void EverQuest::UpdateGroupMemberFromEntity(int index)
{
	if (index < 0 || index >= m_group_member_count) {
		return;
	}

	GroupMember& member = m_group_members[index];

	// Find entity by name
	for (const auto& [id, entity] : m_entities) {
		if (entity.name == member.name) {
			member.spawn_id = id;
			member.level = entity.level;
			member.class_id = entity.class_id;
			member.hp_percent = entity.hp_percent;
			member.in_zone = true;
			return;
		}
	}

	// Entity not found - mark as out of zone
	member.spawn_id = 0;
	member.in_zone = false;
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
		m_has_pending_invite = true;
		m_pending_inviter_name = inviter_name;

		// Phase 7.5: Sync to GameState
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
			ClearGroup();  // Also syncs to GameState
			m_in_group = true;
			m_group_leader_name = std::string(data->leadersname);
			m_is_group_leader = (m_group_leader_name == m_character);

			// Phase 7.5: Sync group status to GameState
			m_game_state.group().setInGroup(true);
			m_game_state.group().setLeaderName(m_group_leader_name);
			m_game_state.group().setIsLeader(m_is_group_leader);

			// Hide pending invite since we joined
			m_has_pending_invite = false;
			m_pending_inviter_name.clear();
			m_game_state.group().clearPendingInvite();  // Phase 7.5

			// Add self first
			GroupMember self;
			self.name = m_character;
			self.spawn_id = m_my_spawn_id;
			self.level = m_level;
			self.class_id = static_cast<uint8_t>(m_class);
			self.hp_percent = (m_max_hp > 0) ? static_cast<uint8_t>(m_cur_hp * 100 / m_max_hp) : 100;
			self.mana_percent = (m_max_mana > 0) ? static_cast<uint8_t>(m_mana * 100 / m_max_mana) : 100;
			self.is_leader = m_is_group_leader;
			self.in_zone = true;
			m_group_members[0] = self;
			m_group_member_count = 1;
			SyncGroupMemberToGameState(0, self);  // Phase 7.5

			// Add other members
			for (int i = 0; i < 5 && m_group_member_count < MAX_GROUP_MEMBERS; i++) {
				std::string member_name(data->membername[i]);
				if (!member_name.empty() && member_name != m_character) {
					GroupMember member;
					member.name = member_name;
					member.is_leader = (member_name == m_group_leader_name);

					// Try to find entity for HP/mana
					for (const auto& [id, entity] : m_entities) {
						if (entity.name == member_name) {
							member.spawn_id = id;
							member.level = entity.level;
							member.class_id = entity.class_id;
							member.hp_percent = entity.hp_percent;
							member.in_zone = true;
							break;
						}
					}

					m_group_members[m_group_member_count] = member;
					SyncGroupMemberToGameState(m_group_member_count, member);  // Phase 7.5
					m_group_member_count++;
				}
			}

			// Phase 7.5: Recalculate member count in GameState
			m_game_state.group().recalculateMemberCount();

			AddChatSystemMessage("Group updated");
			LOG_INFO(MOD_MAIN, "Group update: {} members, leader: {}",
					 m_group_member_count, m_group_leader_name);

			// Publish GroupChanged event to bridge
			if (m_bridge) {
				eqt::state::GroupChangedData gdata;
				gdata.inGroup = true;
				gdata.isLeader = m_is_group_leader;
				gdata.leaderName = m_group_leader_name;
				gdata.memberCount = m_group_member_count;
				m_bridge->pushEvent(eqt::state::GameEvent(
					eqt::state::GameEventType::GroupChanged, std::move(gdata)));

				// Publish GroupMemberUpdated for each member
				for (int mi = 0; mi < m_group_member_count; mi++) {
					eqt::state::GroupMemberUpdatedData mdata;
					mdata.memberIndex = mi;
					mdata.name = m_group_members[mi].name;
					mdata.spawnId = m_group_members[mi].spawn_id;
					mdata.level = m_group_members[mi].level;
					mdata.classId = m_group_members[mi].class_id;
					mdata.hpPercent = m_group_members[mi].hp_percent;
					mdata.manaPercent = m_group_members[mi].mana_percent;
					mdata.inZone = m_group_members[mi].in_zone;
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
				// Remove member by shifting
				for (int i = idx; i < m_group_member_count - 1; i++) {
					m_group_members[i] = m_group_members[i + 1];
				}
				m_group_member_count--;
				m_group_members[m_group_member_count] = GroupMember();

				// Phase 7.5: Rebuild GameState members after shift
				for (int i = 0; i < m_group_member_count; i++) {
					SyncGroupMemberToGameState(i, m_group_members[i]);
				}
				// Clear the slot that was freed
				eqt::state::GroupMember emptyMember;
				m_game_state.group().setMember(m_group_member_count, emptyMember);
				m_game_state.group().recalculateMemberCount();

				AddChatSystemMessage(member_name + " has left the group");

				// Publish GroupChanged event to bridge
				if (m_bridge) {
					eqt::state::GroupChangedData gdata;
					gdata.inGroup = m_in_group;
					gdata.isLeader = m_is_group_leader;
					gdata.leaderName = m_group_leader_name;
					gdata.memberCount = m_group_member_count;
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
			m_group_leader_name = new_leader;
			m_is_group_leader = (new_leader == m_character);

			// Phase 7.5: Sync leader change to GameState
			m_game_state.group().setLeaderName(new_leader);
			m_game_state.group().setIsLeader(m_is_group_leader);

			// Update leader flags
			for (int i = 0; i < m_group_member_count; i++) {
				m_group_members[i].is_leader = (m_group_members[i].name == new_leader);
				SyncGroupMemberToGameState(i, m_group_members[i]);  // Phase 7.5
			}

			AddChatSystemMessage(new_leader + " is now the group leader");

			// Publish GroupChanged event to bridge
			if (m_bridge) {
				eqt::state::GroupChangedData gdata;
				gdata.inGroup = m_in_group;
				gdata.isLeader = m_is_group_leader;
				gdata.leaderName = m_group_leader_name;
				gdata.memberCount = m_group_member_count;
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
	m_has_pending_invite = false;
	m_pending_inviter_name.clear();

	// Phase 7.5: Sync to GameState
	m_game_state.group().clearPendingInvite();

	AddChatSystemMessage("Group invite cancelled");
	LOG_INFO(MOD_MAIN, "Group invite cancelled");

	// Publish GroupChanged to trigger bridge hidePendingInvite
	if (m_bridge) {
		eqt::state::GroupChangedData gdata;
		gdata.inGroup = m_in_group;
		gdata.isLeader = m_is_group_leader;
		gdata.leaderName = m_group_leader_name;
		gdata.memberCount = static_cast<int>(m_group_members.size());
		m_bridge->pushEvent(eqt::state::GameEvent(
			eqt::state::GameEventType::GroupChanged, std::move(gdata)));
	}
}
