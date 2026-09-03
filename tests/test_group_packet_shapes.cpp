// T320: OP_GroupUpdate carries TWO different structs, and only the action
// field distinguishes them.
//
//   groupActJoin (0) / groupActLeave (1) -> GroupJoin_Struct
//        action(4) yourname[64] membername[64] <padding>
//   everything else                      -> GroupUpdate_Struct
//        action(4) yourname[64] membername[5][64] leadersname[64]
//
// Both are 452 bytes on the wire, so the size check the handler used to rely
// on could never tell them apart. Reading a JOIN as a GroupUpdate_Struct
// picked `leadersname` (offset 388) out of GroupJoin_Struct's padding --
// always zeroes -- and published a group with a real member count and an
// EMPTY leader name. Measured live on the group LEADER's own client at
// formation, with no zoning involved at all:
//
//   review/T320/b1_gmon_A_testchar.log:1360
//       Group update: 2 members, leader:
//   review/T320/b1_gmon_B_oraclerogue.log:314   (same instant, the invitee)
//       Group update: 2 members, leader: Testchar
//
// These tests replay the exact wire shapes through the ZoneProcess* seam and
// assert the state the handlers leave behind. No network, no graphics, no
// eqemu.

#include <gtest/gtest.h>

#define private public
#define protected public
#include "client/eq.h"
#undef private
#undef protected

#include "client/bridge/game_state_bridge.h"
#include "common/packet_structs.h"
#include "zone_stream_test_utils.h"

#include <cstring>
#include <string>
#include <vector>

using namespace eqt;
using namespace zonetest;

namespace {

// Every GroupChanged the client published, in order, so a test can assert on
// what consumers actually saw -- not just on the settled end state. The
// empty-leader defect was a PUBLISHED event, so the event stream is the thing
// under test.
class GroupEventRecorder : public eqt::bridge::GameStateBridge {
public:
    std::vector<eqt::state::GroupChangedData> groupChanged;

    void applyEvent(const eqt::state::GameEvent&) override {}

    void drainInto() {
        for (const auto& ev : drainEvents()) {
            if (ev.type == eqt::state::GameEventType::GroupChanged) {
                groupChanged.push_back(std::get<eqt::state::GroupChangedData>(ev.data));
            }
        }
    }
};

// The wire shape eqemu's Group::AddMember / Group::DelMember actually send:
// GroupJoin_Struct fields, padded out to the 452 bytes the server allocates.
std::vector<uint8_t> GroupJoinPayload(uint32_t action, const std::string& yourname,
                                      const std::string& membername) {
    return BuildPayload(2 + sizeof(EQT::GroupUpdate_Struct), [&](EQ::Net::DynamicPacket& p) {
        p.PutUInt32(2 + 0, action);
        p.PutCString(2 + 4, yourname.c_str());
        p.PutCString(2 + 68, membername.c_str());
        // offsets 132..451 stay zero -- exactly what the server sends, and
        // exactly where the old code read `leadersname` from.
    });
}

// The wire shape Group::SendUpdate / RefreshGroupFromDB send.
std::vector<uint8_t> GroupUpdatePayload(uint32_t action, const std::string& yourname,
                                        const std::vector<std::string>& others,
                                        const std::string& leader) {
    return BuildPayload(2 + sizeof(EQT::GroupUpdate_Struct), [&](EQ::Net::DynamicPacket& p) {
        p.PutUInt32(2 + 0, action);
        p.PutCString(2 + 4, yourname.c_str());
        for (size_t i = 0; i < others.size() && i < 5; ++i) {
            p.PutCString(2 + 68 + 64 * i, others[i].c_str());
        }
        p.PutCString(2 + 388, leader.c_str());
    });
}

class GroupPacketTest : public ::testing::Test {
protected:
    GroupPacketTest() : eq("127.0.0.1", 0, "test", "test", "test", "Testchar") {
        eq.setBridge(&bridge);
    }

    void ReplayGroup(const std::vector<uint8_t>& payload) {
        ReplayZone(eq, HC_OP_GroupUpdate, payload);
        bridge.drainInto();
    }

    eqt::state::GroupState& group() { return eq.m_game_state.group(); }

    EverQuest eq;
    GroupEventRecorder bridge;
};

// --- the defect itself -----------------------------------------------------

// The leader's client forms a group. eqemu tells the INVITER only via
// groupActJoin -- the full refresh it sends the invitee is SoD+ only
// (zone/client.cpp:5206) -- so this packet is the entire story the leader
// gets, and the client has to end up knowing it leads the group.
TEST_F(GroupPacketTest, GroupJoinGivesTheInviterARealLeaderName) {
    ReplayGroup(GroupJoinPayload(EQT::GROUP_ACT_JOIN, "Testchar", "Oraclerogue"));

    EXPECT_TRUE(group().inGroup());
    EXPECT_EQ(group().memberCount(), 2);
    EXPECT_EQ(group().leaderName(), "Testchar");   // was "" before T320
    EXPECT_TRUE(group().isLeader());
    EXPECT_GE(group().findMemberByName("Testchar"), 0);
    EXPECT_GE(group().findMemberByName("Oraclerogue"), 0);
}

// The guard that fails if anything ever publishes a partial update again:
// a member count and a roster that disagree. This is the shape the defect
// took, independent of which packet produced it.
TEST_F(GroupPacketTest, NoPublishedGroupEverHasMembersWithoutALeader) {
    ReplayGroup(GroupJoinPayload(EQT::GROUP_ACT_JOIN, "Testchar", "Oraclerogue"));
    ReplayGroup(GroupJoinPayload(EQT::GROUP_ACT_JOIN, "Testchar", "Testbuddy"));
    ReplayGroup(GroupUpdatePayload(EQT::GROUP_ACT_UPDATE, "Testchar",
                                   {"Oraclerogue", "Testbuddy"}, "Testchar"));
    ReplayGroup(GroupJoinPayload(EQT::GROUP_ACT_LEAVE, "Testchar", "Testbuddy"));

    ASSERT_FALSE(bridge.groupChanged.empty());
    for (const auto& g : bridge.groupChanged) {
        if (g.inGroup && g.memberCount > 0) {
            EXPECT_FALSE(g.leaderName.empty())
                << "published GroupChanged with " << g.memberCount
                << " members and an EMPTY leader name -- the T320 defect";
        }
    }
}

// A second person joining must be ADDED, not used to rebuild the roster from
// a struct that only ever names one member.
TEST_F(GroupPacketTest, GroupJoinIsIncrementalAndKeepsTheLeader) {
    ReplayGroup(GroupJoinPayload(EQT::GROUP_ACT_JOIN, "Testchar", "Oraclerogue"));
    ReplayGroup(GroupJoinPayload(EQT::GROUP_ACT_JOIN, "Testchar", "Testbuddy"));

    EXPECT_EQ(group().memberCount(), 3);
    EXPECT_EQ(group().leaderName(), "Testchar");
    EXPECT_GE(group().findMemberByName("Testbuddy"), 0);
}

// A JOIN naming somebody already present must not duplicate them.
TEST_F(GroupPacketTest, GroupJoinForAKnownMemberDoesNotDuplicate) {
    ReplayGroup(GroupJoinPayload(EQT::GROUP_ACT_JOIN, "Testchar", "Oraclerogue"));
    ReplayGroup(GroupJoinPayload(EQT::GROUP_ACT_JOIN, "Testchar", "Oraclerogue"));

    EXPECT_EQ(group().memberCount(), 2);
}

// The invitee's side: a full groupActUpdate still parses as a
// GroupUpdate_Struct and still names the real leader -- someone else.
// `membername[]` lists the OTHER members only; self is never among them, so
// the roster here is us plus Oraclerogue, who leads.
TEST_F(GroupPacketTest, GroupUpdateReadsALeaderWhoIsNotUs) {
    ReplayGroup(GroupUpdatePayload(EQT::GROUP_ACT_UPDATE, "Testchar",
                                   {"Oraclerogue"}, "Oraclerogue"));

    EXPECT_TRUE(group().inGroup());
    EXPECT_EQ(group().memberCount(), 2);
    EXPECT_EQ(group().leaderName(), "Oraclerogue");
    EXPECT_FALSE(group().isLeader());
}

// groupActLeave is a GroupJoin_Struct too. Its single `membername` sits at
// offset 68, the same place membername[0] does, so this passed before by
// luck; pin it so a future struct change cannot break it silently.
TEST_F(GroupPacketTest, GroupLeaveRemovesTheNamedMember) {
    ReplayGroup(GroupUpdatePayload(EQT::GROUP_ACT_UPDATE, "Testchar",
                                   {"Oraclerogue", "Testbuddy"}, "Testchar"));
    ASSERT_EQ(group().memberCount(), 3);

    ReplayGroup(GroupJoinPayload(EQT::GROUP_ACT_LEAVE, "Testchar", "Testbuddy"));

    EXPECT_EQ(group().memberCount(), 2);
    EXPECT_LT(group().findMemberByName("Testbuddy"), 0);
    EXPECT_EQ(group().leaderName(), "Testchar");
}

// --- the phantom group -----------------------------------------------------

// A disband delivered while the client is between zone servers is lost, so
// the client must not carry group state across a transition. CleanupZone()
// drops it and the server's zone-entry RefreshGroupFromDB re-establishes the
// truth. Before T320 the arriving client reported "In group (2 members)" and
// would /gsay into a group that no longer existed.
TEST_F(GroupPacketTest, ZoneCleanupDropsGroupStateSoZoneInCanReestablishIt) {
    ReplayGroup(GroupUpdatePayload(EQT::GROUP_ACT_UPDATE, "Testchar",
                                   {"Oraclerogue"}, "Testchar"));
    ASSERT_TRUE(group().inGroup());
    ASSERT_EQ(group().memberCount(), 2);

    eq.Zone().CleanupZone();

    EXPECT_FALSE(group().inGroup());
    EXPECT_EQ(group().memberCount(), 0);
    EXPECT_TRUE(group().leaderName().empty());

    // ...and the server's full update on the other side restores it.
    ReplayGroup(GroupUpdatePayload(EQT::GROUP_ACT_UPDATE, "Testchar",
                                   {"Oraclerogue"}, "Testchar"));
    EXPECT_TRUE(group().inGroup());
    EXPECT_EQ(group().memberCount(), 2);
    EXPECT_EQ(group().leaderName(), "Testchar");
}

// A disband that DOES arrive still clears everything.
TEST_F(GroupPacketTest, DisbandClearsTheGroup) {
    ReplayGroup(GroupUpdatePayload(EQT::GROUP_ACT_UPDATE, "Testchar",
                                   {"Oraclerogue"}, "Testchar"));
    ASSERT_TRUE(group().inGroup());

    ReplayGroup(GroupUpdatePayload(EQT::GROUP_ACT_DISBAND, "Testchar", {}, ""));

    EXPECT_FALSE(group().inGroup());
    EXPECT_EQ(group().memberCount(), 0);
}

// --- size handling ---------------------------------------------------------

// Only `action` is common to the two layouts, so a runt packet must be
// rejected without reading either struct.
TEST_F(GroupPacketTest, RuntPacketIsIgnored) {
    std::vector<uint8_t> tiny(2, 0);
    ReplayZone(eq, HC_OP_GroupUpdate, tiny);
    EXPECT_FALSE(group().inGroup());
}

} // namespace
