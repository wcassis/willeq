// K10 (Batch K, refactor/K10_login_world_component.md) --
// eqt::login_world::LoginWorldComponent.
//
// K10 follows K01's shape: no forwarders. EverQuest no longer declares a
// single Login*/World* method, nor the login/world connection managers; every
// caller goes through EverQuest::LoginWorld(). This file covers the component
// itself -- construction against a real EverQuest, the accessor identity, the
// state that moved into it (the T09/T14 observers, the two connection
// managers, the world handshake flags), and the two replay seams.
//
// test_login_world_flow.cpp and test_login_world_seam.cpp keep their own
// assertions untouched -- only their call sites were retargeted, and THEY
// remain the coverage of what the moved bodies compute. Nothing here
// re-derives a wire format.

#include <gtest/gtest.h>

#include "client/bridge/game_state_bridge.h"
#include "client/login_world/login_world_component.h"
#include "zone_stream_test_utils.h"

using namespace zonetest;

namespace {

class LoginWorldComponentTest : public ::testing::Test {
protected:
    LoginWorldComponentTest() : eq(MakeEq()) { eq.setBridge(&bridge); }
    ~LoginWorldComponentTest() override { eq.setBridge(nullptr); }

    EverQuest eq;
    TestBridge bridge;
};

// The accessor is the only way in now, and it must name one stable object.
TEST_F(LoginWorldComponentTest, LoginWorldReturnsTheSameComponentEveryTime) {
    eqt::login_world::LoginWorldComponent& a = eq.LoginWorld();
    eqt::login_world::LoginWorldComponent& b = eq.LoginWorld();
    EXPECT_EQ(&a, &b);
}

// The T09 observers moved with their members; a fresh client has none of the
// login/world handshake done.
TEST_F(LoginWorldComponentTest, FreshClientHasNoLoginOrWorldState) {
    EXPECT_EQ(eq.LoginWorld().GetAccountId(), 0u);
    EXPECT_TRUE(eq.LoginWorld().GetSessionKey().empty());
    EXPECT_FALSE(eq.LoginWorld().IsWorldReady());
    EXPECT_FALSE(eq.LoginWorld().HasSentEnterWorld());
    EXPECT_TRUE(eq.LoginWorld().GetWorldServers().empty());
    EXPECT_EQ(eq.LoginWorld().GetCharacterSelectIndex(), -1);
    EXPECT_TRUE(eq.LoginWorld().GetCharacterSelectList().empty());
}

// The UCS chat-server info (T13) moved with the OP_SetChatServer handler that
// writes it.
TEST_F(LoginWorldComponentTest, FreshClientHasNoChatServerInfo) {
    EXPECT_TRUE(eq.LoginWorld().GetChatServerHost().empty());
    EXPECT_EQ(eq.LoginWorld().GetChatServerPort(), 0);
    EXPECT_TRUE(eq.LoginWorld().GetMailKey().empty());
    EXPECT_EQ(eq.LoginWorld().GetChatConnectionType(), 0);
}

// The connection managers are the members K10 moved out of EverQuest. Nothing
// has called ConnectToLogin(), so both are null and the transition getters say
// so rather than crashing -- this is the state EverQuest::TickNetwork() and
// ~EverQuest() now read through.
TEST_F(LoginWorldComponentTest, ConnectionManagersStartNull) {
    EXPECT_EQ(eq.LoginWorld().LoginManager(), nullptr);
    EXPECT_EQ(eq.LoginWorld().LoginConnection(), nullptr);
    EXPECT_EQ(eq.LoginWorld().WorldManager(), nullptr);
    EXPECT_EQ(eq.LoginWorld().WorldConnection(), nullptr);
    EXPECT_TRUE(eq.LoginWorld().WorldServerHost().empty());
}

// TickNetwork() is the production reader of those getters; with no managers
// built it must be a safe no-op, which is what the null guards in eq.cpp are.
TEST_F(LoginWorldComponentTest, TickNetworkIsSafeWithNoManagers) {
    EXPECT_NO_THROW(eq.TickNetwork());
    EXPECT_EQ(eq.LoginWorld().LoginManager(), nullptr);
}

// The T05 replay seams moved with the opcode switches they drive. An unknown
// opcode falls through to the default arm on both streams.
TEST_F(LoginWorldComponentTest, ReplaySeamsAcceptAnUnknownOpcode) {
    EXPECT_NO_THROW(eq.LoginWorld().ReplayLoginPacket(0xFFFF, nullptr, 0));
    EXPECT_NO_THROW(eq.LoginWorld().ReplayWorldPacket(0xFFFF, nullptr, 0));
}

// A send reached through the replay seam with no live connection is counted,
// not crashed -- m_replay_suppressed_sends stayed on EverQuest, so this pins
// that the moved senders still reach it through owner_.
TEST_F(LoginWorldComponentTest, SuppressedSendsAreStillCountedOnEverQuest) {
    const uint64_t before = eq.Net().ReplaySuppressedSendCount();
    // HC_OP_ChatMessage on the login stream -> LoginSendLogin() -> one
    // QueuePacket attempt on the null m_login_connection.
    eq.LoginWorld().ReplayLoginPacket(HC_OP_ChatMessage, nullptr, 0);
    EXPECT_GT(eq.Net().ReplaySuppressedSendCount(), before);
}

}  // namespace
