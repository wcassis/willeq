// T217: two senders whose defects were both found LIVE and never had a unit
// test that could have caught them.
//
// A. AcceptGroupInvite() used to read m_pending_inviter_name AFTER
//    SendGroupFollow() had cleared it, so the local chat line was
//    "You have joined 's group" (review/T99/). SendGroupFollow() itself had
//    the same aliasing bug in its log line, because the parameter IS the
//    member. The clear only happens past the zone-connection guard, which is
//    why no headless test ever saw it: we use the T88 send-guard bypass so
//    the clear-then-read sequence actually runs here. Reverting the ordering
//    fix in AcceptGroupInvite() turns the first test red (measured in
//    refactor/T217_group_name_gmhideme.md).
//
// B. SendGMHideMe() used to send OP_SpawnAppearance type 20, which eqemu
//    drops ("Unknown SpawnAppearance type: 0x0014", review/T148/). It must
//    send OP_GMHideMe with an 8-byte SpawnAppearance_Struct body whose
//    `parameter` is a VISIBILITY flag (Handle_OP_GMHideMe does
//    SetHideMe(!sa->parameter)). Asserted on the raw bytes the sender built.

#include <gtest/gtest.h>

#include "client/bridge/game_state_bridge.h"
#include "common/packet_structs.h"
#include "zone_stream_test_utils.h"

using namespace zonetest;

namespace {

std::vector<uint8_t> GroupInvitePayload(const std::string& inviter, const std::string& invitee) {
    return BuildPayload(2 + sizeof(EQT::GroupInvite_Struct), [&](EQ::Net::DynamicPacket& p) {
        p.PutCString(2, invitee.c_str());       // invitee_name @0
        p.PutCString(2 + 64, inviter.c_str());  // inviter_name @64
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

class GroupAcceptTest : public ::testing::Test {
protected:
    GroupAcceptTest() : eq(MakeEq()) { eq.setBridge(&bridge); }
    ~GroupAcceptTest() override { eq.setBridge(nullptr); }

    EverQuest eq;
    TestBridge bridge;
};

TEST_F(GroupAcceptTest, AcceptNamesTheInviterEvenThoughFollowClearsThePendingName) {
    ReplayZone(eq, HC_OP_GroupInvite, GroupInvitePayload("Oraclerogue", "Testchar"));
    ASSERT_TRUE(eq.HasPendingGroupInvite());
    auto invite_msgs = DrainSystemMessages(bridge);
    ASSERT_EQ(invite_msgs.size(), 1u);
    EXPECT_EQ(invite_msgs[0], "Oraclerogue has invited you to join a group");

    // Let SendGroupFollow() run past its zone-connection guard so it clears
    // m_pending_inviter_name exactly as it does live. No connection exists,
    // so nothing is actually sent.
    eq.Test_SetBypassSendGuardForSeam(true);
    eq.Group().AcceptGroupInvite();

    auto msgs = DrainSystemMessages(bridge);
    ASSERT_EQ(msgs.size(), 1u);
    EXPECT_EQ(msgs[0], "You have joined Oraclerogue's group");

    // The follow must still have consumed the private pending-invite flag
    // (HasPendingGroupInvite() reads the state layer, which only the server's
    // GroupUpdate clears -- unchanged by T217): a second accept says nothing.
    eq.Group().AcceptGroupInvite();
    EXPECT_TRUE(DrainSystemMessages(bridge).empty());
}

TEST_F(GroupAcceptTest, AcceptWithoutPendingInviteSaysNothing) {
    eq.Test_SetBypassSendGuardForSeam(true);
    eq.Group().AcceptGroupInvite();
    EXPECT_TRUE(DrainSystemMessages(bridge).empty());
}

TEST_F(GroupAcceptTest, DeclineStillConsumesThePendingInvite) {
    ReplayZone(eq, HC_OP_GroupInvite, GroupInvitePayload("Oraclerogue", "Testchar"));
    ASSERT_TRUE(eq.HasPendingGroupInvite());
    DrainSystemMessages(bridge);

    eq.Test_SetBypassSendGuardForSeam(true);
    eq.Group().DeclineGroupInvite();

    auto msgs = DrainSystemMessages(bridge);
    ASSERT_EQ(msgs.size(), 1u);
    EXPECT_EQ(msgs[0], "Group invite declined");
    eq.Group().DeclineGroupInvite();
    EXPECT_TRUE(DrainSystemMessages(bridge).empty()) << "second decline must be a no-op";
}

TEST_F(GroupAcceptTest, GuardStillBlocksWhenNotZonedIn) {
    // Production behaviour with the seam unset: not zoned in -> nothing sent,
    // the invite stays pending (unchanged from before T217).
    ReplayZone(eq, HC_OP_GroupInvite, GroupInvitePayload("Oraclerogue", "Testchar"));
    DrainSystemMessages(bridge);
    eq.Group().SendGroupFollow("Oraclerogue");
    // Guard returned early, so the private flag is still set and a later
    // accept still fires its chat line.
    eq.Test_SetBypassSendGuardForSeam(true);
    eq.Group().AcceptGroupInvite();
    auto msgs = DrainSystemMessages(bridge);
    ASSERT_EQ(msgs.size(), 1u);
    EXPECT_EQ(msgs[0], "You have joined Oraclerogue's group");
}

class GMHideMeTest : public ::testing::Test {
protected:
    GMHideMeTest() : eq(MakeEq()) {}
    EverQuest eq;
};

TEST_F(GMHideMeTest, NothingBuiltBeforeAnyCall) {
    EXPECT_FALSE(eq.Test_GetLastGMHideMe().sent);
}

TEST_F(GMHideMeTest, HideOnSendsOpGMHideMeWithVisibilityZero) {
    eq.SendGMHideMe(true);
    const auto& sent = eq.Test_GetLastGMHideMe();
    ASSERT_TRUE(sent.sent);
    // 2-byte opcode + the 8-byte SpawnAppearance_Struct Handle_OP_GMHideMe
    // size-checks against.
    ASSERT_EQ(sent.bytes.size(), 2u + sizeof(EQT::SpawnAppearance_Struct));
    ASSERT_EQ(sent.bytes.size(), 10u);

    uint16_t opcode = static_cast<uint16_t>(sent.bytes[0] | (sent.bytes[1] << 8));
    EXPECT_EQ(opcode, HC_OP_GMHideMe);
    EXPECT_EQ(opcode, 0x15b2);
    EXPECT_NE(opcode, HC_OP_SpawnAppearance) << "the T148 misroute";

    EQT::SpawnAppearance_Struct sa;
    std::memcpy(&sa, sent.bytes.data() + 2, sizeof(sa));
    EXPECT_EQ(sa.type, AT_GM);
    EXPECT_EQ(sa.parameter, 0u) << "parameter is a VISIBILITY flag: 0 hides";
}

TEST_F(GMHideMeTest, HideOffSendsVisibilityOne) {
    eq.SendGMHideMe(false);
    const auto& sent = eq.Test_GetLastGMHideMe();
    ASSERT_TRUE(sent.sent);
    ASSERT_EQ(sent.bytes.size(), 10u);
    uint16_t opcode = static_cast<uint16_t>(sent.bytes[0] | (sent.bytes[1] << 8));
    EXPECT_EQ(opcode, HC_OP_GMHideMe);

    EQT::SpawnAppearance_Struct sa;
    std::memcpy(&sa, sent.bytes.data() + 2, sizeof(sa));
    EXPECT_EQ(sa.parameter, 1u) << "parameter is a VISIBILITY flag: 1 shows";
}

TEST_F(GMHideMeTest, DoesNotGoOutAsSpawnAppearance) {
    // The old path routed through SendSpawnAppearance(), whose guard is
    // IsFullyZonedIn(); with no connection it would have built nothing at all.
    // The new sender always builds the packet, so the seam sees it.
    eq.SendGMHideMe(true);
    EXPECT_TRUE(eq.Test_GetLastGMHideMe().sent);
}

}  // namespace
