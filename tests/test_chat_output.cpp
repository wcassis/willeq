// K13 (Batch K, refactor/K13_context_services.md) -- eqt::ChatOutput, the
// first of the three context services.
//
// The three bodies moved verbatim out of src/client/eq_chat.cpp; EverQuest
// kept AddChatSystemMessage/AddChatCombatMessage/AddChatMissMessage as
// forwarders for the callers outside the EverQuest family. What is worth
// pinning is (a) the service is reachable and is the SAME object the context
// hands a component, (b) the forwarder and the service publish the same
// event, and (c) both are safe with no bridge wired -- the null-bridge branch
// is the one every headless caller runs through.

#include <gtest/gtest.h>

#include "client/chat_output.h"
#include "zone_stream_test_utils.h"

#include <string>
#include <vector>

using namespace zonetest;

namespace {

std::vector<eqt::state::ChatMessageData> DrainChatLike(eqt::bridge::GameStateBridge& bridge) {
    std::vector<eqt::state::ChatMessageData> out;
    for (auto& ev : bridge.drainEvents()) {
        if (ev.type == eqt::state::GameEventType::ChatMessage ||
            ev.type == eqt::state::GameEventType::SystemMessage) {
            out.push_back(std::get<eqt::state::ChatMessageData>(ev.data));
        }
    }
    return out;
}

bool AnyIs(const std::vector<eqt::state::ChatMessageData>& msgs, const std::string& text,
           const std::string& channel) {
    for (const auto& m : msgs) {
        if (m.message == text && m.channelName == channel) return true;
    }
    return false;
}

class ChatOutputTest : public ::testing::Test {
protected:
    ChatOutputTest() : eq(MakeEq()) {}
    EverQuest eq;
};

TEST_F(ChatOutputTest, ServiceIsStableAndIsTheOneTheContextCarries) {
    EXPECT_EQ(&eq.Out(), &eq.Out());
    EXPECT_EQ(&eq.Context().out, &eq.Out());
}

TEST_F(ChatOutputTest, PublishesOnTheThreeChannelsItOwns) {
    TestBridge bridge;
    eq.setBridge(&bridge);

    eq.Out().System("hello from the service");
    eq.Out().Combat("you hit a rat for 2 points of damage", true);
    eq.Out().Combat("a rat hits you for 1 point of damage", false);
    eq.Out().Miss("you try to hit a rat, but miss!");

    auto msgs = DrainChatLike(bridge);
    EXPECT_TRUE(AnyIs(msgs, "hello from the service", "system"));
    EXPECT_TRUE(AnyIs(msgs, "you hit a rat for 2 points of damage", "combat_self"));
    EXPECT_TRUE(AnyIs(msgs, "a rat hits you for 1 point of damage", "combat"));
    EXPECT_TRUE(AnyIs(msgs, "you try to hit a rat, but miss!", "combat_miss"));

    eq.setBridge(nullptr);
}

TEST_F(ChatOutputTest, EverQuestForwardersReachTheSameService) {
    TestBridge bridge;
    eq.setBridge(&bridge);

    eq.AddChatSystemMessage("through the forwarder");
    eq.AddChatMissMessage("missed, through the forwarder");

    auto msgs = DrainChatLike(bridge);
    EXPECT_TRUE(AnyIs(msgs, "through the forwarder", "system"))
        << "EverQuest::AddChatSystemMessage is the sanctioned forwarder for the "
           "non-family callers and must still publish";
    EXPECT_TRUE(AnyIs(msgs, "missed, through the forwarder", "combat_miss"));

    eq.setBridge(nullptr);
}

TEST_F(ChatOutputTest, SafeWithNoBridgeWired) {
    // Console-only, no publish, no crash -- the branch every headless caller
    // in the tree takes.
    eq.Out().System("no bridge");
    eq.Out().Combat("no bridge", true);
    eq.Out().Miss("no bridge");
    SUCCEED();
}

}  // namespace
