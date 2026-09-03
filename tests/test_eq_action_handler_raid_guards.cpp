// T183 -- real-EverQuest/EqActionHandler coverage for the RAID COMMAND GUARDS.
//
// These are the checks that exist because the server gives the player nothing:
//   * MakeLeader IS gated server-side and a non-leader's command falls out of
//     Handle_OP_RaidCommand with NO reply at all -- silence is the failure.
//   * LootType, AddLooter/RemoveLooter, Lock/Unlock and Disband are NOT gated
//     server-side. We gate them anyway, because the real Titanium client
//     offers those controls only to the raid leader. That is OUR policy, and
//     T183 is the unit that made it consistent -- /raid remove was the one
//     left permissive (T181), which T182 flagged.
//
// The guards read raid state that lives on EverQuest and is maintained ONLY by
// the decoded OP_RaidUpdate stream, so every test here establishes that state
// through the production packet path (ReplayZone + HC_OP_RaidUpdate), never by
// poking a setter. That is the point: it pins the guards against the packets
// that really produce the state, so a decode regression breaks these too.
//
// Argument arity (/raid remove with no name, etc.) is NOT retested here -- it
// lives in action::CommandProcessor and is covered in test_command_processor.

#include <gtest/gtest.h>

#include "client/bridge/game_state_bridge.h"
#include "client/eq_action_handler.h"
#include "zone_stream_test_utils.h"

#include <string>
#include <vector>

using namespace zonetest;

namespace {

// MakeEq() names the character "Testchar"; the raid packets below are built
// around that so "us" and "them" are unambiguous.
constexpr const char* kMe = "Testchar";
constexpr const char* kOther = "Raidmate";

constexpr size_t kNameLen = 64;

void PutName(EQ::Net::DynamicPacket& p, size_t offset, const std::string& name) {
    for (size_t i = 0; i < kNameLen; ++i) {
        p.PutUInt8(offset + i, i < name.size() ? static_cast<uint8_t>(name[i]) : 0);
    }
}

// Titanium RaidGeneral_Struct on the wire: action@0, player_name@4,
// leader_name@68, parameter@132 -- offsets relative to the body, which starts
// at 2 (the opcode precedes it).
std::vector<uint8_t> RaidGeneralPayload(uint32_t action, const std::string& player,
                                        const std::string& leader, uint32_t parameter = 0) {
    return BuildPayload(2 + 136, [&](EQ::Net::DynamicPacket& p) {
        p.PutUInt32(2 + 0, action);
        PutName(p, 2 + 4, player);
        PutName(p, 2 + 68, leader);
        p.PutUInt32(2 + 132, parameter);
    });
}

// RaidAddMember_Struct = the general body plus _class, level, isGroupLeader.
std::vector<uint8_t> RaidAddPayload(const std::string& who, uint32_t group,
                                    uint8_t class_id, uint8_t level) {
    return BuildPayload(2 + 140, [&](EQ::Net::DynamicPacket& p) {
        p.PutUInt32(2 + 0, 0);            // raidAdd
        PutName(p, 2 + 4, who);           // eqemu puts the member in BOTH names
        PutName(p, 2 + 68, who);
        p.PutUInt32(2 + 132, group);
        p.PutUInt8(2 + 136, class_id);
        p.PutUInt8(2 + 137, level);
        p.PutUInt8(2 + 138, 0);
    });
}

std::vector<std::string> DrainSystemMessages(eqt::bridge::GameStateBridge& bridge) {
    std::vector<std::string> out;
    for (auto& ev : bridge.drainEvents()) {
        if (ev.type == eqt::state::GameEventType::SystemMessage) {
            out.push_back(std::get<eqt::state::ChatMessageData>(ev.data).message);
        }
    }
    return out;
}

bool AnyContains(const std::vector<std::string>& msgs, const std::string& needle) {
    for (const auto& m : msgs) {
        if (m.find(needle) != std::string::npos) return true;
    }
    return false;
}

class EqActionHandlerRaidGuardTest : public ::testing::Test {
protected:
    EqActionHandlerRaidGuardTest() : eq(MakeEq()), handler(eq) { eq.setBridge(&bridge); }
    ~EqActionHandlerRaidGuardTest() override { eq.setBridge(nullptr); }

    // A two-member raid, built exactly as eqemu builds one for a joining
    // client: raidCreate naming the leader, then one raidAdd per member.
    void FormRaid(const std::string& leader) {
        ReplayZone(eq, HC_OP_RaidUpdate, RaidCreatePayload(leader));
        ReplayZone(eq, HC_OP_RaidUpdate, RaidAddPayload(kMe, 0xFFFFFFFFu, 1, 5));
        ReplayZone(eq, HC_OP_RaidUpdate, RaidAddPayload(kOther, 0xFFFFFFFFu, 9, 10));
        bridge.drainEvents();  // setup chatter is not what these tests assert on
    }

    // emu RaidCreate_Struct, sent raw: action@0, leader_name@4, leader_id@68.
    static std::vector<uint8_t> RaidCreatePayload(const std::string& leader) {
        return BuildPayload(2 + 72, [&](EQ::Net::DynamicPacket& p) {
            p.PutUInt32(2 + 0, 8);
            PutName(p, 2 + 4, leader);
            p.PutUInt32(2 + 68, 1234);
        });
    }

    EverQuest eq;
    eqt::EqActionHandler handler;
    TestBridge bridge;
};

// ---------------------------------------------------------------------------
// Not in a raid at all.
// ---------------------------------------------------------------------------

TEST_F(EqActionHandlerRaidGuardTest, EveryLeaderOpRefusesWhenNotInARaid) {
    ASSERT_FALSE(eq.Group().IsInRaid());

    handler.raidRemove(kOther);
    EXPECT_TRUE(AnyContains(DrainSystemMessages(bridge), "not in a raid"));
    handler.raidMakeLeader(kOther);
    EXPECT_TRUE(AnyContains(DrainSystemMessages(bridge), "not in a raid"));
    handler.raidLootType("all");
    EXPECT_TRUE(AnyContains(DrainSystemMessages(bridge), "not in a raid"));
    handler.raidLooter(true, kOther);
    EXPECT_TRUE(AnyContains(DrainSystemMessages(bridge), "not in a raid"));
    handler.raidLock(true);
    EXPECT_TRUE(AnyContains(DrainSystemMessages(bridge), "not in a raid"));
}

// ---------------------------------------------------------------------------
// In the raid, but NOT the leader. This is T183's subject: /raid remove used
// to be allowed here while its siblings were refused.
// ---------------------------------------------------------------------------

TEST_F(EqActionHandlerRaidGuardTest, NonLeaderRemoveIsRefused) {
    FormRaid(kOther);  // somebody else leads
    ASSERT_TRUE(eq.Group().IsInRaid());
    ASSERT_EQ(eq.Group().GetRaidLeader(), kOther);

    handler.raidRemove(kOther);

    auto msgs = DrainSystemMessages(bridge);
    EXPECT_TRUE(AnyContains(msgs, "Only the raid leader"));
    EXPECT_FALSE(AnyContains(msgs, "You remove"))
        << "the removal must not also be announced as if it happened";
}

TEST_F(EqActionHandlerRaidGuardTest, NonLeaderIsRefusedEveryLeaderOpConsistently) {
    FormRaid(kOther);

    handler.raidMakeLeader(kMe);
    EXPECT_TRUE(AnyContains(DrainSystemMessages(bridge), "Only the raid leader"));
    handler.raidLootType("all");
    EXPECT_TRUE(AnyContains(DrainSystemMessages(bridge), "Only the raid leader"));
    handler.raidLooter(true, kOther);
    EXPECT_TRUE(AnyContains(DrainSystemMessages(bridge), "Only the raid leader"));
    handler.raidLooter(false, kOther);
    EXPECT_TRUE(AnyContains(DrainSystemMessages(bridge), "Only the raid leader"));
    handler.raidLock(true);
    EXPECT_TRUE(AnyContains(DrainSystemMessages(bridge), "Only the raid leader"));
    handler.raidLock(false);
    EXPECT_TRUE(AnyContains(DrainSystemMessages(bridge), "Only the raid leader"));
}

// Ordering matters: a NON-leader naming themselves wants /raid leave, and
// answering "only the leader can do that" would send them the wrong way.
TEST_F(EqActionHandlerRaidGuardTest, NonLeaderRemovingThemselfIsPointedAtRaidLeave) {
    FormRaid(kOther);

    handler.raidRemove(kMe);

    auto msgs = DrainSystemMessages(bridge);
    EXPECT_TRUE(AnyContains(msgs, "/raid leave"));
    EXPECT_FALSE(AnyContains(msgs, "Only the raid leader"))
        << "the self case must be answered before the leadership gate";
}

// ---------------------------------------------------------------------------
// In the raid AND the leader: the ops go through.
// ---------------------------------------------------------------------------

TEST_F(EqActionHandlerRaidGuardTest, LeaderRemoveIsAllowed) {
    FormRaid(kMe);  // we lead
    ASSERT_EQ(eq.Group().GetRaidLeader(), kMe);

    handler.raidRemove(kOther);

    auto msgs = DrainSystemMessages(bridge);
    EXPECT_TRUE(AnyContains(msgs, "You remove"));
    EXPECT_FALSE(AnyContains(msgs, "Only the raid leader"));
}

TEST_F(EqActionHandlerRaidGuardTest, LeaderRemovingSomebodyNotInTheRaidIsRefusedByName) {
    FormRaid(kMe);

    handler.raidRemove("Nobody");

    EXPECT_TRUE(AnyContains(DrainSystemMessages(bridge), "Nobody is not in your raid"));
}

// The leadership the guard reads is the CURRENT leader, not a login-time
// snapshot -- action 30 moves it, and the guard must move with it. This is the
// behaviour the T181 probe demonstrated live; here it is pinned.
TEST_F(EqActionHandlerRaidGuardTest, LosingLeadershipMidSessionStartsRefusing) {
    FormRaid(kMe);
    handler.raidRemove(kOther);
    ASSERT_TRUE(AnyContains(DrainSystemMessages(bridge), "You remove"));

    // raidMakeLeader (30): eqemu puts the NEW leader in both name fields.
    ReplayZone(eq, HC_OP_RaidUpdate, RaidGeneralPayload(30, kOther, kOther));
    bridge.drainEvents();
    ASSERT_EQ(eq.Group().GetRaidLeader(), kOther);

    handler.raidRemove(kOther);
    EXPECT_TRUE(AnyContains(DrainSystemMessages(bridge), "Only the raid leader"));
}

// ---------------------------------------------------------------------------
// T184 -- raid groups. Both failure cases below are ones eqemu answers with
// SILENCE, and the roster is the only reason the client can say anything.
// ---------------------------------------------------------------------------

TEST_F(EqActionHandlerRaidGuardTest, GroupMoveIntoAFullGroupIsRefusedWithTheCount) {
    FormRaid(kMe);
    // Fill wire group 0 to its limit of six. eqemu's arm falls out of
    // `if (group_count < 6)` without a word.
    for (int i = 0; i < 6; ++i) {
        ReplayZone(eq, HC_OP_RaidUpdate,
                   RaidAddPayload("Filler" + std::to_string(i), 0, 1, 10));
    }
    bridge.drainEvents();

    handler.raidGroup(kOther, "1");  // user-facing 1 == wire group 0

    auto msgs = DrainSystemMessages(bridge);
    EXPECT_TRUE(AnyContains(msgs, "Group 1 is full"));
    EXPECT_TRUE(AnyContains(msgs, "6 of 6"));
    EXPECT_FALSE(AnyContains(msgs, "You move"));
}

TEST_F(EqActionHandlerRaidGuardTest, GroupMoveToTheGroupTheyAreAlreadyInIsRefused) {
    FormRaid(kMe);
    // Put kOther in wire group 2 the way the server would: Remove then Add.
    ReplayZone(eq, HC_OP_RaidUpdate, RaidGeneralPayload(1, kOther, kOther));
    ReplayZone(eq, HC_OP_RaidUpdate, RaidAddPayload(kOther, 2, 9, 10));
    bridge.drainEvents();

    handler.raidGroup(kOther, "3");  // user-facing 3 == wire group 2

    auto msgs = DrainSystemMessages(bridge);
    EXPECT_TRUE(AnyContains(msgs, "already in Group 3"));
    EXPECT_FALSE(AnyContains(msgs, "You move"));
}

TEST_F(EqActionHandlerRaidGuardTest, GroupMoveToARoomyGroupGoesThroughAndNamesItOneBased) {
    FormRaid(kMe);

    handler.raidGroup(kOther, "4");

    auto msgs = DrainSystemMessages(bridge);
    EXPECT_TRUE(AnyContains(msgs, "You move Raidmate to Group 4"));
}

TEST_F(EqActionHandlerRaidGuardTest, GroupMoveOutOfAGroupIsNotSubjectToTheFullCheck) {
    FormRaid(kMe);
    // The ungrouped bank has no limit; every member starts there, so a naive
    // "is the target full" check that did not exempt it would refuse this.
    for (int i = 0; i < 8; ++i) {
        ReplayZone(eq, HC_OP_RaidUpdate,
                   RaidAddPayload("Filler" + std::to_string(i), 0xFFFFFFFFu, 1, 10));
    }
    ReplayZone(eq, HC_OP_RaidUpdate, RaidGeneralPayload(1, kOther, kOther));
    ReplayZone(eq, HC_OP_RaidUpdate, RaidAddPayload(kOther, 1, 9, 10));
    bridge.drainEvents();

    handler.raidGroup(kOther, "none");

    auto msgs = DrainSystemMessages(bridge);
    EXPECT_TRUE(AnyContains(msgs, "out of their group"));
    EXPECT_FALSE(AnyContains(msgs, "full"));
}

TEST_F(EqActionHandlerRaidGuardTest, GroupMoveIsLeaderGatedAndRejectsBadGroupsFirst) {
    FormRaid(kOther);  // we are NOT the leader

    // A bad group is answered on its own terms even for a non-leader: the
    // specific complaint is the more useful one.
    handler.raidGroup(kMe, "0");
    auto msgs = DrainSystemMessages(bridge);
    EXPECT_TRUE(AnyContains(msgs, "is not a raid group"));
    EXPECT_FALSE(AnyContains(msgs, "Only the raid leader"));

    handler.raidGroup(kMe, "2");
    EXPECT_TRUE(AnyContains(DrainSystemMessages(bridge), "Only the raid leader"));
}

// A bad loot type is refused for everyone, leader or not, and BEFORE the
// leadership gate -- the specific complaint is the more useful one, and the
// server would have stored the bad value verbatim.
TEST_F(EqActionHandlerRaidGuardTest, BadLootTypeIsRefusedOnItsOwnTerms) {
    FormRaid(kMe);

    handler.raidLootType("frobnicate");

    auto msgs = DrainSystemMessages(bridge);
    EXPECT_TRUE(AnyContains(msgs, "is not a raid loot type"));
    EXPECT_FALSE(AnyContains(msgs, "You set raid looting"));
}

}  // namespace
