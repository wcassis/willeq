// T18: chat-domain zone-stream tests (eq_chat.cpp, previously ~0% direct
// coverage). ZoneProcessChannelMessage's observable outputs are the
// GameStateBridge ChatMessage event (the game->renderer contract) and the
// GetLastTellSender() accessor; both are asserted here. Packet layout per
// the handler (eq_chat.cpp:90): target cstr@2, sender cstr@66, language
// u32@130, channel u32@134, skill u32@146, message cstr@150; guard rejects
// length < 150.

#include <gtest/gtest.h>

#include "client/bridge/game_state_bridge.h"
#include "client/eq_action_handler.h"
#include "zone_stream_test_utils.h"

using namespace zonetest;

namespace {

EverQuest MakeEq() {
    return EverQuest("127.0.0.1", 0, "test", "test", "test", "Testchar");
}

std::vector<uint8_t> ChannelMessagePayload(const std::string& sender,
                                           uint32_t channel,
                                           const std::string& message,
                                           const std::string& target = "") {
    // Total size: 150-byte fixed header + message + NUL.
    return BuildPayload(150 + message.size() + 1, [&](EQ::Net::DynamicPacket& p) {
        p.PutCString(2, target.c_str());
        p.PutCString(66, sender.c_str());
        p.PutUInt32(130, 0);        // language
        p.PutUInt32(134, channel);
        p.PutUInt32(146, 0);        // language skill
        p.PutCString(150, message.c_str());
    });
}

// Drains the bridge and returns the ChatMessage events only.
std::vector<eqt::state::ChatMessageData> DrainChat(eqt::bridge::GameStateBridge& bridge) {
    std::vector<eqt::state::ChatMessageData> out;
    for (auto& ev : bridge.drainEvents()) {
        if (ev.type == eqt::state::GameEventType::ChatMessage) {
            out.push_back(std::get<eqt::state::ChatMessageData>(ev.data));
        }
    }
    return out;
}

class ZoneChatTest : public ::testing::Test {
protected:
    ZoneChatTest() : eq(MakeEq()) { eq.setBridge(&bridge); }
    ~ZoneChatTest() override { eq.setBridge(nullptr); }

    EverQuest eq;
    TestBridge bridge;
};

TEST_F(ZoneChatTest, ChannelMessage_PushesChatEventWithMappedChannelName) {
    struct Case { uint32_t channel; const char* expectedName; };
    const Case cases[] = {
        {CHAT_CHANNEL_SAY, "say"},       {CHAT_CHANNEL_TELL, "tell"},
        {CHAT_CHANNEL_SHOUT, "shout"},   {CHAT_CHANNEL_OOC, "ooc"},
        {CHAT_CHANNEL_GROUP, "group"},   {CHAT_CHANNEL_GUILD, "guild"},
        {CHAT_CHANNEL_AUCTION, "auction"}, {CHAT_CHANNEL_EMOTE, "emote"},
    };

    for (const auto& c : cases) {
        auto payload = ChannelMessagePayload("Speaker", c.channel, "hello there");
        ASSERT_NO_THROW(ReplayZone(eq, HC_OP_ChannelMessage, payload));

        auto chats = DrainChat(bridge);
        ASSERT_EQ(chats.size(), 1u) << "channel " << c.channel;
        EXPECT_EQ(chats[0].sender, "Speaker");
        EXPECT_EQ(chats[0].message, "hello there");
        EXPECT_EQ(chats[0].channelType, c.channel);
        EXPECT_EQ(chats[0].channelName, c.expectedName);
    }
}

TEST_F(ZoneChatTest, ChannelMessage_UnknownChannelMapsToOther) {
    auto payload = ChannelMessagePayload("Speaker", 77, "mystery");
    ASSERT_NO_THROW(ReplayZone(eq, HC_OP_ChannelMessage, payload));

    auto chats = DrainChat(bridge);
    ASSERT_EQ(chats.size(), 1u);
    EXPECT_EQ(chats[0].channelName, "other");
}

TEST_F(ZoneChatTest, ChannelMessage_TellFromOtherUpdatesLastTellSender) {
    EXPECT_TRUE(eq.GetLastTellSender().empty());

    auto payload = ChannelMessagePayload("Friend", CHAT_CHANNEL_TELL, "psst", "Testchar");
    ASSERT_NO_THROW(ReplayZone(eq, HC_OP_ChannelMessage, payload));

    EXPECT_EQ(eq.GetLastTellSender(), "Friend");
}

// T222: the real handler's hasTellToReplyTo() tracks the same state, so a
// received tell flips the action layer's /reply gate from refuse to allow.
TEST_F(ZoneChatTest, ActionHandler_HasTellToReplyTo_FollowsReceivedTell) {
    eqt::EqActionHandler handler(eq);
    EXPECT_FALSE(handler.hasTellToReplyTo());

    auto payload = ChannelMessagePayload("Friend", CHAT_CHANNEL_TELL, "psst", "Testchar");
    ASSERT_NO_THROW(ReplayZone(eq, HC_OP_ChannelMessage, payload));

    EXPECT_TRUE(handler.hasTellToReplyTo());
}

TEST_F(ZoneChatTest, ChannelMessage_OwnTellEchoDoesNotUpdateLastTellSender) {
    // The handler excludes sender == m_character ("Testchar") so a server
    // echo of our own outbound tell doesn't clobber /reply.
    auto payload = ChannelMessagePayload("Testchar", CHAT_CHANNEL_TELL, "echo", "Friend");
    ASSERT_NO_THROW(ReplayZone(eq, HC_OP_ChannelMessage, payload));

    EXPECT_TRUE(eq.GetLastTellSender().empty());
}

TEST_F(ZoneChatTest, ChannelMessage_SayDoesNotUpdateLastTellSender) {
    auto payload = ChannelMessagePayload("Shouty", CHAT_CHANNEL_SAY, "hi all");
    ASSERT_NO_THROW(ReplayZone(eq, HC_OP_ChannelMessage, payload));

    EXPECT_TRUE(eq.GetLastTellSender().empty());
}

TEST_F(ZoneChatTest, ChannelMessage_UndersizedRejectedWithoutEvent) {
    // One byte under the 150-byte guard.
    auto payload = BuildPayload(149, [&](EQ::Net::DynamicPacket& p) {
        p.PutCString(66, "Speaker");
    });
    EXPECT_NO_THROW(ReplayZone(eq, HC_OP_ChannelMessage, payload));

    EXPECT_TRUE(DrainChat(bridge).empty());
}

TEST_F(ZoneChatTest, ChannelMessage_ExactMinimumSizeRejectedWithoutThrowing) {
    // T18 found that the old `< 150` guard admitted an exactly-150-byte
    // packet, which then read the message cstring at offset 150 == packet
    // end and threw an uncaught std::out_of_range on the zone receive path.
    // T20 changed the guard to `<= 150`: the packet is rejected like any
    // other undersized one -- no throw, no event.
    auto payload = BuildPayload(150, [&](EQ::Net::DynamicPacket& p) {
        p.PutCString(2, "");
        p.PutCString(66, "EdgeSender");
        p.PutUInt32(134, CHAT_CHANNEL_SAY);
    });
    EXPECT_NO_THROW(ReplayZone(eq, HC_OP_ChannelMessage, payload));

    EXPECT_TRUE(DrainChat(bridge).empty());
}

TEST_F(ZoneChatTest, ChannelMessage_SmallestValidSizePublishesEmptyMessage) {
    // The boundary just above: 151 bytes is the smallest real payload (an
    // empty message is still a NUL terminator). It must parse and publish,
    // proving the T20 guard didn't over-reject.
    auto payload = BuildPayload(151, [&](EQ::Net::DynamicPacket& p) {
        p.PutCString(2, "");
        p.PutCString(66, "EdgeSender");
        p.PutUInt32(134, CHAT_CHANNEL_SAY);
        p.PutCString(150, "");
    });
    ASSERT_NO_THROW(ReplayZone(eq, HC_OP_ChannelMessage, payload));

    auto chats = DrainChat(bridge);
    ASSERT_EQ(chats.size(), 1u);
    EXPECT_EQ(chats[0].sender, "EdgeSender");
    EXPECT_TRUE(chats[0].message.empty());
}

TEST_F(ZoneChatTest, RealCapture_ChannelMessagesReplayAndPublish) {
    size_t chatRecords = 0;
    size_t published = 0;

    bool haveCapture = ForEachCaptureRecord(
        EQT::Net::SessionStream::Zone,
        [&](EQT::Net::SessionDirection dir, uint16_t opcode,
            const std::vector<uint8_t>& payload) {
            if (dir != EQT::Net::SessionDirection::ServerToClient ||
                opcode != HC_OP_ChannelMessage) {
                return;
            }
            ++chatRecords;
            ASSERT_NO_THROW(ReplayZone(eq, opcode, payload));
            published += DrainChat(bridge).size();
        });
    if (!haveCapture) {
        GTEST_SKIP() << "No session capture at " << DefaultSessionLogPath();
    }
    if (chatRecords == 0) {
        GTEST_SKIP() << "Capture has no zone ChannelMessage records.";
    }

    // Every well-formed captured chat record must publish exactly one event.
    EXPECT_EQ(published, chatRecords);
}

}  // namespace
