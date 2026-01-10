#include "client/state/group_state.h"
#include "client/state/event_bus.h"

namespace eqt {
namespace state {

GroupState::GroupState() = default;

// ========== Group Status ==========

void GroupState::setInGroup(bool inGroup) {
    if (m_inGroup != inGroup) {
        m_inGroup = inGroup;
        if (!inGroup) {
            // Clear group when leaving
            clearGroup();
        }
        fireGroupChangedEvent();
    }
}

void GroupState::setIsLeader(bool leader) {
    if (m_isLeader != leader) {
        m_isLeader = leader;
        fireGroupChangedEvent();
    }
}

// ========== Group Members ==========

const GroupMember* GroupState::getMember(int index) const {
    if (index < 0 || index >= MAX_GROUP_MEMBERS) {
        return nullptr;
    }
    return &m_members[index];
}

GroupMember* GroupState::getMemberMutable(int index) {
    if (index < 0 || index >= MAX_GROUP_MEMBERS) {
        return nullptr;
    }
    return &m_members[index];
}

int GroupState::findMemberByName(const std::string& name) const {
    for (int i = 0; i < MAX_GROUP_MEMBERS; ++i) {
        if (m_members[i].name == name) {
            return i;
        }
    }
    return -1;
}

int GroupState::findMemberBySpawnId(uint16_t spawnId) const {
    if (spawnId == 0) {
        return -1;
    }
    for (int i = 0; i < MAX_GROUP_MEMBERS; ++i) {
        if (m_members[i].spawnId == spawnId) {
            return i;
        }
    }
    return -1;
}

void GroupState::setMember(int index, const GroupMember& member) {
    if (index < 0 || index >= MAX_GROUP_MEMBERS) {
        return;
    }
    m_members[index] = member;
    recalculateMemberCount();
    fireGroupMemberUpdatedEvent(index);
}

void GroupState::updateMemberStats(int index, uint8_t hpPercent, uint8_t manaPercent) {
    if (index < 0 || index >= MAX_GROUP_MEMBERS) {
        return;
    }
    m_members[index].hpPercent = hpPercent;
    m_members[index].manaPercent = manaPercent;
    fireGroupMemberUpdatedEvent(index);
}

void GroupState::updateMemberSpawnId(int index, uint16_t spawnId) {
    if (index < 0 || index >= MAX_GROUP_MEMBERS) {
        return;
    }
    m_members[index].spawnId = spawnId;
    m_members[index].inZone = (spawnId != 0);
    fireGroupMemberUpdatedEvent(index);
}

void GroupState::setMemberInZone(int index, bool inZone) {
    if (index < 0 || index >= MAX_GROUP_MEMBERS) {
        return;
    }
    m_members[index].inZone = inZone;
    if (!inZone) {
        m_members[index].spawnId = 0;
    }
    fireGroupMemberUpdatedEvent(index);
}

// ========== Pending Invite ==========

void GroupState::setPendingInvite(const std::string& inviterName) {
    m_hasPendingInvite = true;
    m_pendingInviterName = inviterName;

    if (m_eventBus) {
        m_eventBus->publish(GameEventType::GroupInviteReceived, GroupChangedData{
            m_inGroup, m_isLeader, m_leaderName, m_memberCount
        });
    }
}

void GroupState::clearPendingInvite() {
    m_hasPendingInvite = false;
    m_pendingInviterName.clear();
}

// ========== Clear/Reset ==========

void GroupState::clearGroup() {
    m_inGroup = false;
    m_isLeader = false;
    m_leaderName.clear();
    m_memberCount = 0;

    for (auto& member : m_members) {
        member.clear();
    }

    clearPendingInvite();
    fireGroupChangedEvent();
}

void GroupState::recalculateMemberCount() {
    m_memberCount = 0;
    for (const auto& member : m_members) {
        if (!member.isEmpty()) {
            ++m_memberCount;
        }
    }
}

// ========== Event Firing ==========

void GroupState::fireGroupChangedEvent() {
    if (m_eventBus) {
        GroupChangedData data;
        data.inGroup = m_inGroup;
        data.isLeader = m_isLeader;
        data.leaderName = m_leaderName;
        data.memberCount = m_memberCount;
        m_eventBus->publish(GameEventType::GroupChanged, data);
    }
}

void GroupState::fireGroupMemberUpdatedEvent(int index) {
    if (index < 0 || index >= MAX_GROUP_MEMBERS) {
        return;
    }

    if (m_eventBus) {
        const GroupMember& member = m_members[index];
        GroupMemberUpdatedData data;
        data.memberIndex = index;
        data.name = member.name;
        data.spawnId = member.spawnId;
        data.level = member.level;
        data.classId = member.classId;
        data.hpPercent = member.hpPercent;
        data.manaPercent = member.manaPercent;
        data.inZone = member.inZone;
        m_eventBus->publish(GameEventType::GroupMemberUpdated, data);
    }
}

} // namespace state
} // namespace eqt
