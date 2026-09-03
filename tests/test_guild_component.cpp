// K01 (Batch K, refactor/K01_guild_component.md) -- eqt::guild::GuildComponent.
//
// K01 dropped forwarders entirely: EverQuest no longer declares a single
// guild method, and every caller goes through EverQuest::Guild(). This file
// covers the component itself -- construction against a real EverQuest, the
// ClientContext binding, the guards, and the three data members that moved.
// test_guild_wire_rank.cpp keeps its own assertions untouched (only its call
// sites were retargeted) and still covers the wire builders.
//
// Same limitation the T317 file records: EverQuest::QueuePacket() no-ops
// without a zone connection and leaves nothing inspectable, so what is
// asserted is the observable effect the guard/echo path produces -- the
// system chat lines on the bridge -- plus the component's own moved state.

#include <gtest/gtest.h>

#include "zone_stream_test_utils.h"
#include "client/guild/guild_component.h"
#include "client/guild_packet_bodies.h"

#include <cstring>
#include <string>
#include <vector>

using namespace zonetest;

namespace {

// PlayerProfile_Struct, Titanium: fixed 19588 bytes, base 2 in the packet.
constexpr size_t kProfileSize = 19588;
constexpr size_t kProfileBase = 2;
constexpr size_t kOffGuildId = 13036;
constexpr size_t kOffGuildRank = 13055;

void LoadProfile(EverQuest& eq, uint32_t guild_id, uint8_t guild_rank) {
    auto payload = BuildPayload(kProfileBase + kProfileSize, [&](EQ::Net::DynamicPacket& p) {
        p.PutUInt32(kProfileBase + kOffGuildId, guild_id);
        p.PutUInt8(kProfileBase + kOffGuildRank, guild_rank);
    });
    ReplayZone(eq, HC_OP_PlayerProfile, payload);
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

// Builds the OP_GuildInvite body a server would send us, at the offsets
// ZoneProcessGuildInvite reads (opcode-inclusive, base 2).
std::vector<uint8_t> BuildInboundInvite(const std::string& inviter, uint32_t guild_eq_id) {
    const size_t total = 2 + sizeof(EQT::GuildCommand_Struct);
    return BuildPayload(total, [&](EQ::Net::DynamicPacket& p) {
        EQT::GuildCommand_Struct body{};
        std::memset(&body, 0, sizeof(body));
        std::strncpy(body.myname, inviter.c_str(), 63);
        body.guildeqid = guild_eq_id;
        std::memcpy(static_cast<uint8_t*>(p.Data()) + 2, &body, sizeof(body));
    });
}

class GuildComponentTest : public ::testing::Test {
protected:
    GuildComponentTest() : eq(MakeEq()) { eq.setBridge(&bridge); }
    ~GuildComponentTest() override { eq.setBridge(nullptr); }

    EverQuest eq;
    TestBridge bridge;
};

// ===================================================================
// 1. It constructs against a real EverQuest in headless mode
// ===================================================================

TEST_F(GuildComponentTest, ComponentExistsAndStartsWithNoPendingInvite) {
    EXPECT_FALSE(eq.Guild().HasPendingGuildInvite());
    // Guild() must hand back the SAME component every time, not a copy.
    EXPECT_EQ(&eq.Guild(), &eq.Guild());
}

TEST_F(GuildComponentTest, ContextPointsAtTheOwningEverQuestsOwnMembers) {
    // The context is a bundle of references, not a copy: the character name
    // the component sends must be the one EverQuest was constructed with.
    EXPECT_EQ(&eq.Context().game_state, &eq.GetGameState());
    EXPECT_EQ(eq.Context().character, eq.GetMyName());
    EXPECT_EQ(eq.Context().character, "Testchar");
}

// ===================================================================
// 2. Senders on the component (port of the T317 cases)
// ===================================================================

TEST_F(GuildComponentTest, SendersRefuseOnTheComponentWhenNotInAGuild) {
    bridge.drainEvents();

    eq.Guild().SendGuildInvite("Bob");
    EXPECT_TRUE(AnyContains(DrainSystemMessages(bridge), "You are not in a guild."));

    eq.Guild().SendGuildRemove("Bob");
    EXPECT_TRUE(AnyContains(DrainSystemMessages(bridge), "You are not in a guild."));

    eq.Guild().SendSetGuildMOTD("hello");
    EXPECT_TRUE(AnyContains(DrainSystemMessages(bridge), "You are not in a guild."));
}

TEST_F(GuildComponentTest, ComponentReadsGuildIdAndRankThroughTheOwnerHandle) {
    // m_guild_id/m_guild_rank stay on EverQuest and are written by the zone
    // family. The moved bodies reach them as owner_.m_guild_id -- so a profile
    // replayed into EverQuest must unblock the component's guards.
    LoadProfile(eq, /*guild_id=*/77, /*guild_rank=*/EQ::guild::kRankOfficer);
    ASSERT_EQ(eq.GetGuildID(), 77u);
    ASSERT_EQ(eq.GetGuildRank(), EQ::guild::kRankOfficer);
    bridge.drainEvents();

    eq.Guild().SendGuildInvite("Bob");
    EXPECT_TRUE(AnyContains(DrainSystemMessages(bridge), "Inviting Bob to join your guild."));

    eq.Guild().SendGuildRemove("Bob");
    EXPECT_TRUE(AnyContains(DrainSystemMessages(bridge), "Removing Bob from the guild."));
}

// ===================================================================
// 3. The three moved members really are the component's state now
// ===================================================================

TEST_F(GuildComponentTest, AcceptRefusesUntilAnInviteArrivesThenClearsTheState) {
    bridge.drainEvents();

    // No invite yet: the component's own m_has_pending_guild_invite guard.
    eq.Guild().SendGuildInviteAccept(true);
    EXPECT_TRUE(AnyContains(DrainSystemMessages(bridge),
                            "You don't have a pending guild invite."));

    // Now deliver one through the real dispatch path (DispatchZonePacket calls
    // m_guild->ZoneProcessGuildInvite directly since K01 dropped forwarders).
    ReplayZone(eq, HC_OP_GuildInvite, BuildInboundInvite("Bob", 77));
    EXPECT_TRUE(eq.Guild().HasPendingGuildInvite());
    EXPECT_TRUE(AnyContains(DrainSystemMessages(bridge),
                            "Bob has invited you to join a guild."));

    // Accepting consumes it -- the state that moved into the component.
    eq.Guild().SendGuildInviteAccept(true);
    EXPECT_FALSE(eq.Guild().HasPendingGuildInvite());
    EXPECT_TRUE(AnyContains(DrainSystemMessages(bridge),
                            "You have accepted the guild invite."));
}

}  // namespace
