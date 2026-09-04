// K03 (Batch K, refactor/K03_chat_component.md) -- eqt::chat::ChatComponent.
//
// K03 dropped forwarders entirely: EverQuest no longer declares
// SendChatMessage, ZoneSendEmote, ProcessChatInput, DispatchSlashCommand,
// SetUnrecognizedCommandFallback or any of the five chat packet handlers, and
// every caller goes through EverQuest::Chat(). This file covers the component
// itself -- construction against a real EverQuest, the ClientContext binding,
// the one data member that moved (m_unrecognized_command_fallback), the
// members that stayed behind and are reached through owner_
// (m_last_tell_sender, m_string_db), and the dispatch path from
// DispatchZonePacket into m_chat->ZoneProcess*().
//
// test_zone_chat.cpp and test_chat_channel_names.cpp keep their own
// assertions untouched; they still cover the wire decode and the
// channel-name table.
//
// Same limitation the guild file records: the wire senders no-op without a
// zone connection, so what is asserted is the observable effect on the
// bridge plus the component's own moved state.

#include <gtest/gtest.h>

#include "zone_stream_test_utils.h"
#include "client/chat/chat_component.h"

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

bool AnyContains(const std::vector<eqt::state::ChatMessageData>& msgs, const std::string& needle) {
    for (const auto& m : msgs) {
        if (m.message.find(needle) != std::string::npos) return true;
    }
    return false;
}

// OP_ChannelMessage body at the offsets ZoneProcessChannelMessage reads:
// target[64]@2, sender[64]@66, language@130, chan_num@134, skill@146,
// message cstring@150.
std::vector<uint8_t> BuildInboundChannelMessage(const std::string& sender, uint32_t channel,
                                                 const std::string& message,
                                                 const std::string& target = "") {
    return BuildPayload(150 + message.size() + 1, [&](EQ::Net::DynamicPacket& p) {
        if (!target.empty()) p.PutCString(2, target.c_str());
        p.PutCString(66, sender.c_str());
        p.PutUInt32(130, 0);
        p.PutUInt32(134, channel);
        p.PutUInt32(146, 100);
        p.PutCString(150, message.c_str());
    });
}

// OP_Emote: type u32@2, message cstring@6 (already carries the actor name).
std::vector<uint8_t> BuildInboundEmote(const std::string& text) {
    return BuildPayload(6 + text.size() + 1, [&](EQ::Net::DynamicPacket& p) {
        p.PutUInt32(2, 0);
        p.PutCString(6, text.c_str());
    });
}

class ChatComponentTest : public ::testing::Test {
protected:
    ChatComponentTest() : eq(MakeEq()) { eq.setBridge(&bridge); }
    ~ChatComponentTest() override { eq.setBridge(nullptr); }

    EverQuest eq;
    TestBridge bridge;
};

// ===================================================================
// 1. It constructs against a real EverQuest in headless mode
// ===================================================================

TEST_F(ChatComponentTest, ChatReturnsTheSameComponentEveryTime) {
    EXPECT_EQ(&eq.Chat(), &eq.Chat());
}

TEST_F(ChatComponentTest, ContextPointsAtTheOwningEverQuestsOwnMembers) {
    EXPECT_EQ(&eq.Context().game_state, &eq.GetGameState());
    EXPECT_EQ(eq.Context().character, eq.GetMyName());
    EXPECT_EQ(eq.Context().character, "Testchar");
}

// ===================================================================
// 2. The member that MOVED: m_unrecognized_command_fallback
// ===================================================================

TEST_F(ChatComponentTest, UnrecognizedCommandFallbackLivesOnTheComponent) {
    std::vector<std::string> seen;
    eq.Chat().SetUnrecognizedCommandFallback([&](const std::string& cmd) {
        seen.push_back(cmd);
        return true;   // claim it: no "Unknown command" line should be emitted
    });
    bridge.drainEvents();

    eq.Chat().ProcessChatInput("/definitelynotacommand alpha");

    ASSERT_EQ(seen.size(), 1u);
    EXPECT_EQ(seen[0], "definitelynotacommand alpha");
    EXPECT_FALSE(AnyContains(DrainChatLike(bridge), "Unknown command"));
}

TEST_F(ChatComponentTest, WithNoFallbackAnUnknownCommandStillReportsItself) {
    bridge.drainEvents();
    eq.Chat().ProcessChatInput("/definitelynotacommand");
    EXPECT_TRUE(AnyContains(DrainChatLike(bridge),
                            "Unknown command: /definitelynotacommand"));
}

TEST_F(ChatComponentTest, AFallbackThatDeclinesStillGetsTheUnknownCommandLine) {
    eq.Chat().SetUnrecognizedCommandFallback([](const std::string&) { return false; });
    bridge.drainEvents();
    eq.Chat().ProcessChatInput("/stillnotacommand");
    EXPECT_TRUE(AnyContains(DrainChatLike(bridge), "Unknown command: /stillnotacommand"));
}

TEST_F(ChatComponentTest, EmptyInputIsANoOp) {
    bridge.drainEvents();
    eq.Chat().ProcessChatInput("");
    EXPECT_TRUE(DrainChatLike(bridge).empty());
}

// ===================================================================
// 3. Members the chat family owns, read back through EverQuest's forwarder
// ===================================================================

TEST_F(ChatComponentTest, ComponentWritesLastTellSenderReadBackThroughEverQuest) {
    // T341: m_last_tell_sender is this component's own member now; action/
    // still reads it via EverQuest::GetLastTellSender(), which forwards here.
    EXPECT_TRUE(eq.GetLastTellSender().empty());
    ReplayZone(eq, HC_OP_ChannelMessage,
               BuildInboundChannelMessage("Friend", CHAT_CHANNEL_TELL, "hello there", "Testchar"));
    EXPECT_EQ(eq.GetLastTellSender(), "Friend");
}

TEST_F(ChatComponentTest, OurOwnTellEchoDoesNotBecomeTheLastTellSender) {
    ReplayZone(eq, HC_OP_ChannelMessage,
               BuildInboundChannelMessage("Testchar", CHAT_CHANNEL_TELL, "echo", "Friend"));
    EXPECT_TRUE(eq.GetLastTellSender().empty());
}

// ===================================================================
// 4. DispatchZonePacket now calls m_chat->ZoneProcess*() directly
// ===================================================================

TEST_F(ChatComponentTest, ChannelMessageReachesTheComponentAndTheBridge) {
    bridge.drainEvents();
    ReplayZone(eq, HC_OP_ChannelMessage,
               BuildInboundChannelMessage("Bob", CHAT_CHANNEL_OOC, "well met"));
    auto msgs = DrainChatLike(bridge);
    ASSERT_FALSE(msgs.empty());
    EXPECT_TRUE(AnyContains(msgs, "well met"));
    bool named_ooc = false;
    for (auto& m : msgs) {
        if (m.message == "well met") {
            named_ooc = (m.channelName == "ooc");
            EXPECT_EQ(m.sender, "Bob");
        }
    }
    EXPECT_TRUE(named_ooc);
}

TEST_F(ChatComponentTest, EmoteReachesTheComponentAndTheBridge) {
    bridge.drainEvents();
    ReplayZone(eq, HC_OP_Emote, BuildInboundEmote("Testchar waves"));
    auto msgs = DrainChatLike(bridge);
    EXPECT_TRUE(AnyContains(msgs, "Testchar waves"));
}

// ===================================================================
// 5. Senders refuse cleanly with no zone connection
// ===================================================================

TEST_F(ChatComponentTest, SendChatMessageRejectsAnUnknownChannelWithoutTouchingTheBridge) {
    bridge.drainEvents();
    eq.Chat().SendChatMessage("hi", "notachannel");
    EXPECT_TRUE(DrainChatLike(bridge).empty());
}

TEST_F(ChatComponentTest, TellWithNoTargetIsRefused) {
    bridge.drainEvents();
    eq.Chat().SendChatMessage("hi", "tell");
    EXPECT_TRUE(DrainChatLike(bridge).empty());
}

TEST_F(ChatComponentTest, AKnownChannelStopsAtTheZoneConnectionGuard) {
    // The component reads ctx_.zone_connection and ctx_.game_state; with no
    // zone connection the guard fires before anything is queued or published.
    // (T320's "You must be in a group" line sits BEHIND this guard, so it is
    // not reachable from a headless fixture -- it needs a live zone.)
    bridge.drainEvents();
    eq.Chat().SendChatMessage("hi", "group");
    eq.Chat().SendChatMessage("hi", "say");
    EXPECT_TRUE(DrainChatLike(bridge).empty());
}

TEST_F(ChatComponentTest, DispatchSlashCommandIsSafeAndReportsUnrecognised) {
    EXPECT_FALSE(eq.Chat().DispatchSlashCommand("definitelynotacommand"));
}

}  // namespace
