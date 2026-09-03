// K04 (Batch K, refactor/K04_group_component.md) -- eqt::group::GroupComponent.
//
// K04 follows K01's shape: no forwarders. EverQuest no longer declares a
// single group, raid or pet method; every caller goes through
// EverQuest::Group(). This file covers the component itself -- construction
// against a real EverQuest, the accessor identity, the moved raid state, and
// the two cases the plan asked for (accept invite; raid command).
//
// test_group_accept_gm_hideme.cpp and test_eq_action_handler_raid_guards.cpp
// keep their own assertions untouched (only their call sites were retargeted).
//
// Same limitation the K01 file records: EverQuest::QueuePacket() no-ops
// without a zone connection and leaves nothing inspectable, so what is
// asserted is the observable effect the guard/echo path produces -- the
// system chat lines on the bridge -- plus the component's own moved state.

#include <gtest/gtest.h>

#include "client/bridge/game_state_bridge.h"
#include "client/group/group_component.h"
#include "common/packet_structs.h"
#include "zone_stream_test_utils.h"

#include <string>
#include <vector>

using namespace zonetest;

namespace {

constexpr const char* kMe = "Testchar";     // MakeEq() names the character
constexpr const char* kOther = "Raidmate";

constexpr size_t kNameLen = 64;

void PutName(EQ::Net::DynamicPacket& p, size_t offset, const std::string& name) {
    for (size_t i = 0; i < kNameLen; ++i) {
        p.PutUInt8(offset + i, i < name.size() ? static_cast<uint8_t>(name[i]) : 0);
    }
}

std::vector<uint8_t> GroupInvitePayload(const std::string& inviter, const std::string& invitee) {
    return BuildPayload(2 + sizeof(EQT::GroupInvite_Struct), [&](EQ::Net::DynamicPacket& p) {
        p.PutCString(2, invitee.c_str());       // invitee_name @0
        p.PutCString(2 + 64, inviter.c_str());  // inviter_name @64
    });
}

// emu RaidCreate_Struct, sent raw: action@0, leader_name@4, leader_id@68.
std::vector<uint8_t> RaidCreatePayload(const std::string& leader) {
    return BuildPayload(2 + 72, [&](EQ::Net::DynamicPacket& p) {
        p.PutUInt32(2 + 0, 8);
        PutName(p, 2 + 4, leader);
        p.PutUInt32(2 + 68, 1234);
    });
}

// RaidAddMember_Struct: the general body plus _class, level, isGroupLeader.
std::vector<uint8_t> RaidAddPayload(const std::string& who, uint32_t group,
                                    uint8_t class_id, uint8_t level) {
    return BuildPayload(2 + 140, [&](EQ::Net::DynamicPacket& p) {
        p.PutUInt32(2 + 0, 0);            // raidAdd
        PutName(p, 2 + 4, who);
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

class GroupComponentTest : public ::testing::Test {
protected:
    GroupComponentTest() : eq(MakeEq()) { eq.setBridge(&bridge); }
    ~GroupComponentTest() override { eq.setBridge(nullptr); }

    EverQuest eq;
    TestBridge bridge;
};

// The accessor is the only way in now, and it must name one stable object.
TEST_F(GroupComponentTest, GroupReturnsTheSameComponentEveryTime) {
    eqt::group::GroupComponent& a = eq.Group();
    eqt::group::GroupComponent& b = eq.Group();
    EXPECT_EQ(&a, &b);
}

// Ported case 1: accept invite. The T217 ordering fix lives in the moved body;
// this asserts it still reads the inviter BEFORE SendGroupFollow() clears it.
TEST_F(GroupComponentTest, AcceptInviteNamesTheInviter) {
    ReplayZone(eq, HC_OP_GroupInvite, GroupInvitePayload("Oraclerogue", kMe));
    ASSERT_TRUE(eq.HasPendingGroupInvite());
    DrainSystemMessages(bridge);

    eq.Test_SetBypassSendGuardForSeam(true);
    eq.Group().AcceptGroupInvite();

    auto msgs = DrainSystemMessages(bridge);
    ASSERT_EQ(msgs.size(), 1u);
    EXPECT_EQ(msgs[0], "You have joined Oraclerogue's group");
}

// The moved packet handler writes the moved raid members, and the moved
// accessors read them -- the whole state now lives inside the component.
TEST_F(GroupComponentTest, RaidStateMovedWithTheHandlerThatWritesIt) {
    EXPECT_FALSE(eq.Group().IsInRaid());
    EXPECT_TRUE(eq.Group().GetRaidMembers().empty());

    ReplayZone(eq, HC_OP_RaidUpdate, RaidCreatePayload(kOther));
    ReplayZone(eq, HC_OP_RaidUpdate, RaidAddPayload(kMe, 0xFFFFFFFFu, 1, 5));
    ReplayZone(eq, HC_OP_RaidUpdate, RaidAddPayload(kOther, 0xFFFFFFFFu, 9, 10));
    DrainSystemMessages(bridge);

    EXPECT_TRUE(eq.Group().IsInRaid());
    EXPECT_EQ(eq.Group().GetRaidLeader(), kOther);
    EXPECT_EQ(eq.Group().GetRaidMembers().size(), 2u);
    EXPECT_FALSE(eq.Group().IsRaidLocked());
}

// Ported case 2: raid command. No zone connection exists, so the send is a
// no-op -- what is pinned is that the moved sender is reachable through
// Group() and returns without touching the raid state it now owns.
TEST_F(GroupComponentTest, RaidCommandIsReachableAndLeavesStateAlone) {
    ReplayZone(eq, HC_OP_RaidUpdate, RaidCreatePayload(kMe));
    ReplayZone(eq, HC_OP_RaidUpdate, RaidAddPayload(kMe, 0xFFFFFFFFu, 1, 5));
    DrainSystemMessages(bridge);

    eq.Group().SendRaidCommand(3, kOther, kMe);  // raidInvite

    EXPECT_TRUE(eq.Group().IsInRaid());
    EXPECT_EQ(eq.Group().GetRaidLeader(), kMe);
    EXPECT_TRUE(DrainSystemMessages(bridge).empty());
}

// Pet queries moved too; with no pet spawned they are the empty answers.
TEST_F(GroupComponentTest, PetQueriesMovedAndAnswerEmptyWithNoPet) {
    ASSERT_FALSE(eq.HasPet());
    EXPECT_TRUE(eq.Group().GetPetName().empty());
    EXPECT_EQ(eq.Group().GetPetLevel(), 0);
}

}  // namespace
