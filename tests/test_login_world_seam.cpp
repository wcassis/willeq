// T05: login/world replay seam smoke test.
//
// Proves EverQuest::ReplayLoginPacket/ReplayWorldPacket (eq.h) reach
// DispatchLoginPacket/DispatchWorldPacket (extracted from LoginOnPacketRecv/
// WorldOnPacketRecv, eq_login_world.cpp) without a live connection, mirroring
// the Batch R zone seam exercised in test_zone_connection_flow.cpp.
//
// Deliberately narrow (see refactor/T05_login_world_seam.md "A4"): this is a
// seam smoke test, not thorough login/world flow coverage, which is
// explicitly deferred to a follow-up unit.

#include <gtest/gtest.h>

#include <cstdint>
#include <vector>

#include "client/eq.h"

namespace {

std::vector<uint8_t> EmptyPayload() {
    return {};
}

} // namespace

TEST(LoginWorldReplaySeam, LoginChatMessage_TriggersLoginSendLogin_SuppressedSend) {
    // DispatchLoginPacket's HC_OP_ChatMessage case calls LoginSendLogin(),
    // which (a) sets LoadingPhase::LOGIN_AUTHENTICATING -- an observable
    // state change -- and (b) attempts one QueuePacket on the (null, in a
    // replay-only test) m_login_connection, which increments the shared
    // m_replay_suppressed_sends counter instead of crashing.
    EverQuest eq("127.0.0.1", 0, "test", "test", "test", "Testchar");

    ASSERT_NE(eq.GetLoadingPhase(), LoadingPhase::LOGIN_AUTHENTICATING);
    auto before = eq.Net().ReplaySuppressedSendCount();

    EXPECT_NO_THROW(eq.LoginWorld().ReplayLoginPacket(HC_OP_ChatMessage, nullptr, 0));

    EXPECT_EQ(eq.GetLoadingPhase(), LoadingPhase::LOGIN_AUTHENTICATING);
    EXPECT_EQ(eq.Net().ReplaySuppressedSendCount(), before + 1);
}

TEST(LoginWorldReplaySeam, LoginUnknownOpcode_IgnoredSafely) {
    EverQuest eq("127.0.0.1", 0, "test", "test", "test", "Testchar");

    auto phase_before = eq.GetLoadingPhase();
    auto sends_before = eq.Net().ReplaySuppressedSendCount();

    EXPECT_NO_THROW(eq.LoginWorld().ReplayLoginPacket(0xFFFF, nullptr, 0));

    EXPECT_EQ(eq.GetLoadingPhase(), phase_before);
    EXPECT_EQ(eq.Net().ReplaySuppressedSendCount(), sends_before);
}

TEST(LoginWorldReplaySeam, WorldSendCharInfo_TriggersCharacterSelectPhase) {
    // DispatchWorldPacket's HC_OP_SendCharInfo case calls
    // WorldProcessCharacterSelect(), which sets
    // LoadingPhase::WORLD_CHARACTER_SELECT unconditionally before its own
    // length guard (p.Length() < 1706) -- so even an empty payload produces
    // the observable phase change without needing a full character list.
    EverQuest eq("127.0.0.1", 0, "test", "test", "test", "Testchar");

    ASSERT_NE(eq.GetLoadingPhase(), LoadingPhase::WORLD_CHARACTER_SELECT);

    EXPECT_NO_THROW(eq.LoginWorld().ReplayWorldPacket(HC_OP_SendCharInfo, EmptyPayload().data(), 0));

    EXPECT_EQ(eq.GetLoadingPhase(), LoadingPhase::WORLD_CHARACTER_SELECT);
}

TEST(LoginWorldReplaySeam, WorldChatMessage_TriggersSuppressedSend) {
    // DispatchWorldPacket's HC_OP_ChatMessage case calls
    // WorldSendClientAuth(), which attempts one QueuePacket on the (null)
    // m_world_connection -- suppressed, not crashed.
    EverQuest eq("127.0.0.1", 0, "test", "test", "test", "Testchar");

    auto before = eq.Net().ReplaySuppressedSendCount();

    EXPECT_NO_THROW(eq.LoginWorld().ReplayWorldPacket(HC_OP_ChatMessage, nullptr, 0));

    EXPECT_EQ(eq.Net().ReplaySuppressedSendCount(), before + 1);
}

TEST(LoginWorldReplaySeam, WorldUnknownOpcode_IgnoredSafely) {
    EverQuest eq("127.0.0.1", 0, "test", "test", "test", "Testchar");

    auto phase_before = eq.GetLoadingPhase();
    auto sends_before = eq.Net().ReplaySuppressedSendCount();

    EXPECT_NO_THROW(eq.LoginWorld().ReplayWorldPacket(0xFFFF, nullptr, 0));

    EXPECT_EQ(eq.GetLoadingPhase(), phase_before);
    EXPECT_EQ(eq.Net().ReplaySuppressedSendCount(), sends_before);
}
