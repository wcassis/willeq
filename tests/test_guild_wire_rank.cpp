// T317 — the two guild opcodes that put a literal `2` in their rank field.
//
// eq_guild.cpp used to send:
//
//     invite.officer      = 2;  // TODO: Use actual rank   (OP_GuildInvite)
//     remove.leaderstatus = 2;  // TODO: Use actual rank   (OP_GuildRemove)
//
// The board called that "claiming officer/leader status the player may not
// have". Reading eqemu's two handlers (client_packet.cpp:8152 and :8672)
// showed that is not what either field is:
//
//   * OP_GuildInvite's `officer` is the rank GRANTED to the invitee, not the
//     sender's rank. 2 is GUILD_LEADER_TI, so every /guildinvite was asking
//     the server to bring the invitee in as guild leader. Only the sender's
//     server-side CheckPermission stood in the way.
//   * OP_GuildRemove's `leaderstatus` is never read by the server at all.
//
// Both are pinned below, together with the finding that the client CAN know
// its own rank (PlayerProfile offset 13055) and now does.
//
// WHAT THESE TESTS CANNOT DO: EverQuest::QueuePacket() no-ops without a zone
// connection and leaves nothing inspectable (the same limitation
// test_combat_manager.cpp's T23 note records), so there is no way to read the
// bytes SendGuildInvite() actually queued. The wire content is therefore
// pinned on the pure builders in client/guild_packet_bodies.h, which are the
// ONLY thing those two senders now use to fill their structs, plus a
// reachability check that the senders still run to completion.

#include <gtest/gtest.h>

#include "zone_stream_test_utils.h"
#include "client/guild_packet_bodies.h"

#include <string>
#include <vector>

using namespace zonetest;

namespace {

// PlayerProfile_Struct, Titanium: fixed 19588 bytes, base 2 in the packet.
constexpr size_t kProfileSize = 19588;
constexpr size_t kProfileBase = 2;
constexpr size_t kOffGuildId = 13036;
constexpr size_t kOffGm = 13054;
constexpr size_t kOffGuildRank = 13055;

// Drives the real ZoneProcessPlayerProfile path rather than a backdoor, the
// way test_eq_commands_bank_train.cpp's SetPlayerClass does.
void LoadProfile(EverQuest& eq, uint32_t guild_id, uint8_t guild_rank, uint8_t gm = 0) {
    auto payload = BuildPayload(kProfileBase + kProfileSize, [&](EQ::Net::DynamicPacket& p) {
        p.PutUInt32(kProfileBase + kOffGuildId, guild_id);
        p.PutUInt8(kProfileBase + kOffGm, gm);
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

class GuildWireRankTest : public ::testing::Test {
protected:
    GuildWireRankTest() : eq(MakeEq()) { eq.setBridge(&bridge); }
    ~GuildWireRankTest() override { eq.setBridge(nullptr); }

    EverQuest eq;
    TestBridge bridge;
};

// ===================================================================
// 1. The client CAN know its own rank -- it just was not reading it
// ===================================================================

TEST_F(GuildWireRankTest, GuildRankDefaultsToNoneBeforeAnyProfile) {
    // GUILD_RANK_NONE_TI == GUILD_MAX_RANK + 1 == 9. Not 0: 0 means "member".
    EXPECT_EQ(eq.GetGuildRank(), EQ::guild::kRankNone);
    EXPECT_EQ(eq.GetGuildID(), 0u);
}

TEST_F(GuildWireRankTest, GuildRankIsParsedFromPlayerProfileOffset13055) {
    // Two different ranks through the real packet path, per this unit's
    // acceptance bar.
    LoadProfile(eq, /*guild_id=*/77, /*guild_rank=*/EQ::guild::kRankMember);
    EXPECT_EQ(eq.GetGuildID(), 77u);
    EXPECT_EQ(eq.GetGuildRank(), EQ::guild::kRankMember);

    LoadProfile(eq, /*guild_id=*/77, /*guild_rank=*/EQ::guild::kRankOfficer);
    EXPECT_EQ(eq.GetGuildRank(), EQ::guild::kRankOfficer);

    LoadProfile(eq, /*guild_id=*/77, /*guild_rank=*/EQ::guild::kRankLeader);
    EXPECT_EQ(eq.GetGuildRank(), EQ::guild::kRankLeader);
}

TEST_F(GuildWireRankTest, GuildRankIsNotConfusedWithTheGmByteNextToIt) {
    // 13055 sits one byte after gm@13054, the field T148 verified live. A
    // one-byte slip would make these two track each other.
    LoadProfile(eq, /*guild_id=*/5, /*guild_rank=*/EQ::guild::kRankLeader, /*gm=*/0);
    EXPECT_EQ(eq.GetGuildRank(), EQ::guild::kRankLeader);
    EXPECT_FALSE(eq.GetGMMode());

    LoadProfile(eq, /*guild_id=*/5, /*guild_rank=*/EQ::guild::kRankMember, /*gm=*/1);
    EXPECT_EQ(eq.GetGuildRank(), EQ::guild::kRankMember);
    EXPECT_TRUE(eq.GetGMMode());
}

// ===================================================================
// 2. NEGATIVE — /guildinvite no longer asks for the invitee to be leader
// ===================================================================

TEST_F(GuildWireRankTest, InviteGrantsMemberRank_NotLeader_WhateverTheSendersOwnRankIs) {
    // The defect: officer == 2 == GUILD_LEADER_TI. eqemu maps that to
    // GUILD_LEADER and either records the invite at leader rank or, for an
    // existing member, treats it as a promotion to leader.
    const auto invite = EQ::guild::BuildGuildInvite("Bob", "Testchar", 77);

    EXPECT_NE(invite.officer, EQ::guild::kRankLeader)
        << "GUILD_LEADER_TI on a plain invite asks the server to hand the guild over";
    EXPECT_NE(invite.officer, EQ::guild::kRankOfficer);
    EXPECT_EQ(invite.officer, EQ::guild::kRankMember);

    // And the field is NOT the sender's rank, so it must not track it. A
    // leader inviting someone still invites them as a member.
    const auto from_leader = EQ::guild::BuildGuildInvite("Bob", "Testchar", 77);
    EXPECT_EQ(from_leader.officer, EQ::guild::kRankMember);

    EXPECT_STREQ(invite.othername, "Bob");
    EXPECT_STREQ(invite.myname, "Testchar");
    EXPECT_EQ(invite.guildeqid, 77u);
}

TEST_F(GuildWireRankTest, InviteBodyIsExactlyWhatTheServerSizeChecks) {
    // Handle_OP_GuildInvite returns early unless size == sizeof(GuildCommand_Struct).
    EXPECT_EQ(sizeof(EQT::GuildCommand_Struct), 136u);
    const auto invite = EQ::guild::BuildGuildInvite("Bob", "Testchar", 77);
    // Names are NUL-terminated inside their 64-byte fields, not run together.
    EXPECT_EQ(invite.othername[3], '\0');
    EXPECT_EQ(invite.myname[8], '\0');
}

TEST_F(GuildWireRankTest, InviteTruncatesAnOverlongNameInsteadOfOverrunning) {
    const std::string long_name(200, 'x');
    const auto invite = EQ::guild::BuildGuildInvite(long_name, long_name, 1);
    EXPECT_EQ(invite.othername[63], '\0');
    EXPECT_EQ(invite.myname[63], '\0');
    EXPECT_EQ(std::string(invite.othername).size(), 63u);
}

// ===================================================================
// 3. NEGATIVE — /guildremove sends the real rank, not a confident 2
// ===================================================================

TEST_F(GuildWireRankTest, RemoveCarriesThePlayersRealRankForTwoDifferentRanks) {
    const auto as_member = EQ::guild::BuildGuildRemove("Bob", "Testchar",
                                                        EQ::guild::kRankMember);
    EXPECT_EQ(as_member.leaderstatus, EQ::guild::kRankMember);
    EXPECT_NE(as_member.leaderstatus, EQ::guild::kRankLeader)
        << "the old code sent 2 (leader) regardless of the player's rank";

    const auto as_officer = EQ::guild::BuildGuildRemove("Bob", "Testchar",
                                                         EQ::guild::kRankOfficer);
    EXPECT_EQ(as_officer.leaderstatus, EQ::guild::kRankOfficer);

    const auto as_leader = EQ::guild::BuildGuildRemove("Bob", "Testchar",
                                                        EQ::guild::kRankLeader);
    EXPECT_EQ(as_leader.leaderstatus, EQ::guild::kRankLeader);

    EXPECT_STREQ(as_member.target, "Bob");
    EXPECT_STREQ(as_member.name, "Testchar");
    EXPECT_EQ(sizeof(EQT::GuildRemove_Struct), 136u);
}

TEST_F(GuildWireRankTest, RemoveTargetSitsWhereTheServerReadsOthername) {
    // Handle_OP_GuildRemove casts the body to GuildCommand_Struct and reads
    // `othername` at offset 0. GuildRemove_Struct::target must be there too,
    // or the server kicks nobody.
    EXPECT_EQ(offsetof(EQT::GuildRemove_Struct, target),
              offsetof(EQT::GuildCommand_Struct, othername));
    EXPECT_EQ(offsetof(EQT::GuildRemove_Struct, target), 0u);
}

// ===================================================================
// 4. The senders still reach the wire (guard + message unchanged)
// ===================================================================

TEST_F(GuildWireRankTest, GuildSendersStillRefuseWhenNotInAGuild) {
    bridge.drainEvents();
    eq.SendGuildInvite("Bob");
    EXPECT_TRUE(AnyContains(DrainSystemMessages(bridge), "You are not in a guild."));

    eq.SendGuildRemove("Bob");
    EXPECT_TRUE(AnyContains(DrainSystemMessages(bridge), "You are not in a guild."));
}

TEST_F(GuildWireRankTest, GuildSendersRunToCompletionOnceTheProfileGivesUsAGuild) {
    LoadProfile(eq, /*guild_id=*/77, /*guild_rank=*/EQ::guild::kRankOfficer);
    bridge.drainEvents();

    eq.SendGuildInvite("Bob");
    EXPECT_TRUE(AnyContains(DrainSystemMessages(bridge), "Inviting Bob to join your guild."));

    eq.SendGuildRemove("Bob");
    EXPECT_TRUE(AnyContains(DrainSystemMessages(bridge), "Removing Bob from the guild."));
}

}  // namespace
