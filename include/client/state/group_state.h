#pragma once

#include <cstdint>
#include <string>
#include <array>

namespace eqt {
namespace state {

// Forward declaration
class EventBus;

// Maximum group size (leader + 5 members)
static constexpr int MAX_GROUP_MEMBERS = 6;

/**
 * GroupMember - Information about a group member.
 */
struct GroupMember {
    std::string name;
    uint16_t spawnId = 0;       // 0 if not in zone
    uint8_t level = 0;
    uint8_t classId = 0;
    uint8_t hpPercent = 100;
    uint8_t manaPercent = 100;  // Stored as percent for display
    bool isLeader = false;
    bool inZone = false;        // True if we can see them in entity list

    void clear() {
        name.clear();
        spawnId = 0;
        level = 0;
        classId = 0;
        hpPercent = 100;
        manaPercent = 100;
        isLeader = false;
        inZone = false;
    }

    bool isEmpty() const { return name.empty(); }
};

/**
 * GroupState - Contains group-related state data.
 *
 * This class encapsulates state related to the player's group including
 * membership, leader status, member information, and pending invites.
 */
class GroupState {
public:
    GroupState();
    ~GroupState() = default;

    // Set the event bus for state change notifications
    void setEventBus(EventBus* bus) { m_eventBus = bus; }

    // ========== Group Status ==========

    bool inGroup() const { return m_inGroup; }
    void setInGroup(bool inGroup);

    bool isLeader() const { return m_isLeader; }
    void setIsLeader(bool leader);

    const std::string& leaderName() const { return m_leaderName; }
    void setLeaderName(const std::string& name) { m_leaderName = name; }

    int memberCount() const { return m_memberCount; }

    // ========== Group Members ==========

    /**
     * Get a group member by index (0-5).
     * @param index Member index (0 = first member, may be leader)
     * @return Pointer to member or nullptr if index invalid
     */
    const GroupMember* getMember(int index) const;

    /**
     * Get a mutable group member by index.
     * @param index Member index
     * @return Pointer to member or nullptr if index invalid
     */
    GroupMember* getMemberMutable(int index);

    /**
     * Find a group member by name.
     * @param name Name to search for
     * @return Index of member or -1 if not found
     */
    int findMemberByName(const std::string& name) const;

    /**
     * Find a group member by spawn ID.
     * @param spawnId Spawn ID to search for
     * @return Index of member or -1 if not found
     */
    int findMemberBySpawnId(uint16_t spawnId) const;

    /**
     * Add or update a group member.
     * @param index Member index (0-5)
     * @param member Member data
     */
    void setMember(int index, const GroupMember& member);

    /**
     * Update a member's stats (HP/mana).
     * @param index Member index
     * @param hpPercent HP percentage
     * @param manaPercent Mana percentage
     */
    void updateMemberStats(int index, uint8_t hpPercent, uint8_t manaPercent);

    /**
     * Update a member's spawn ID (when they zone in/out).
     * @param index Member index
     * @param spawnId New spawn ID (0 if not in zone)
     */
    void updateMemberSpawnId(int index, uint16_t spawnId);

    /**
     * Set member's in-zone status.
     * @param index Member index
     * @param inZone Whether member is in the same zone
     */
    void setMemberInZone(int index, bool inZone);

    // ========== Pending Invite ==========

    bool hasPendingInvite() const { return m_hasPendingInvite; }
    const std::string& pendingInviterName() const { return m_pendingInviterName; }

    void setPendingInvite(const std::string& inviterName);
    void clearPendingInvite();

    // ========== Clear/Reset ==========

    /**
     * Clear the entire group (leave group).
     */
    void clearGroup();

    /**
     * Recalculate member count based on filled slots.
     */
    void recalculateMemberCount();

private:
    void fireGroupChangedEvent();
    void fireGroupMemberUpdatedEvent(int index);

    EventBus* m_eventBus = nullptr;

    // Group status
    bool m_inGroup = false;
    bool m_isLeader = false;
    std::string m_leaderName;
    int m_memberCount = 0;

    // Group members
    std::array<GroupMember, MAX_GROUP_MEMBERS> m_members;

    // Pending invite
    bool m_hasPendingInvite = false;
    std::string m_pendingInviterName;
};

} // namespace state
} // namespace eqt
