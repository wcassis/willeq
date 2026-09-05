// T395 Leg B (refactor/T395_spell_impact_eqstr_blob_race_data.md, board B205):
// the 45-character hex blob fable printed in front of a spell name.
//
// The two strings below are the exact bytes T386 measured, twice in one
// session (review/T386/logs/fable_session_selfcast.log:1041 and :1879, read
// with `cat -A` so the 0x12 delimiters are visible as ^R). They are NOT a
// colour code: they are eqemu say-link bodies -- the spell name is wrapped in
// Saylink::Silent("#castspell <id>", <name>) by Mob::SpellOnTarget. The body
// decodes as item_id 0xFFFFF (SAYLINK_ITEM_ID) with augment_2 = 0x4F / 0x50,
// the `saylink` table row ids for the two `#castspell` phrases. See
// include/client/say_link_text.h.
//
// PRE-FIX both consumer assertions fail: the whole 45-character body reaches
// the chat line verbatim, delimiters and all.

#include <gtest/gtest.h>

#include "client/say_link_text.h"
#include "client/state/event_bus.h"
#include "zone_stream_test_utils.h"

#include <string>
#include <vector>

using namespace zonetest;

namespace {

const std::string kFireLink =
    "\x12"
    "0FFFFF000000004F00000000000000000000000000000"
    "Burst of Flame"
    "\x12";
const std::string kColdLink =
    "\x12"
    "0FFFFF000000005000000000000000000000000000000"
    "Frost Bolt"
    "\x12";

std::string FireRefusal() {
    return "Your spell " + kFireLink +
           " has failed to land on yourself because you are invulnerable.";
}
std::string ColdRefusal() {
    return "Your spell " + kColdLink +
           " has failed to land on yourself because you are invulnerable.";
}

// OP_SpecialMesg: 2 bytes opcode + 24 bytes header + NUL-terminated message,
// the offsets ZoneComponent::OnSpecialMesg reads.
std::vector<uint8_t> SpecialMesgPayload(const std::string& message) {
    constexpr size_t kMsgOffset = 2 + 24;
    const size_t total = kMsgOffset + message.size() + 1;
    return BuildPayload(total, [&](EQ::Net::DynamicPacket& p) {
        for (size_t i = 0; i < message.size(); ++i) {
            p.PutUInt8(kMsgOffset + i, static_cast<uint8_t>(message[i]));
        }
        p.PutUInt8(kMsgOffset + message.size(), 0);
    });
}

std::vector<std::string> SystemLines(eqt::bridge::GameStateBridge& bridge) {
    std::vector<std::string> out;
    for (const auto& ev : bridge.drainEvents()) {
        if (ev.type == eqt::state::GameEventType::SystemMessage ||
            ev.type == eqt::state::GameEventType::ChatMessage) {
            out.push_back(std::get<eqt::state::ChatMessageData>(ev.data).message);
        }
    }
    return out;
}

// === The helper ===

TEST(SayLinkText, TheTwoMeasuredRefusalsReadAsTheBareSpellName) {
    EXPECT_EQ(eqt::StripSayLinks(FireRefusal()),
              "Your spell Burst of Flame has failed to land on yourself "
              "because you are invulnerable.");
    EXPECT_EQ(eqt::StripSayLinks(ColdRefusal()),
              "Your spell Frost Bolt has failed to land on yourself "
              "because you are invulnerable.");
}

TEST(SayLinkText, PlainTextIsUntouched) {
    const std::string plain = "You have gained a level! Welcome to level 12!";
    EXPECT_EQ(eqt::StripSayLinks(plain), plain);
    EXPECT_EQ(eqt::StripSayLinks(""), "");
}

TEST(SayLinkText, TwoLinksInOneLineBothResolve) {
    const std::string line = "You combine " + kFireLink + " and " + kColdLink + ".";
    EXPECT_EQ(eqt::StripSayLinks(line), "You combine Burst of Flame and Frost Bolt.");
}

// An unpaired delimiter is not a link. Guessing at one would silently eat 45
// characters of a real message.
TEST(SayLinkText, AnUnpairedDelimiterIsLeftAlone) {
    const std::string broken = std::string("Your spell \x12") + "0FFFFF000 truncated";
    EXPECT_EQ(eqt::StripSayLinks(broken), broken);
}

// A payload shorter than the Titanium body width keeps its content rather
// than being dropped.
TEST(SayLinkText, AShortPayloadKeepsItsText) {
    EXPECT_EQ(eqt::StripSayLinks(std::string("[\x12") + "short" + "\x12]"), "[short]");
}

// === The consumer ===

TEST(SayLinkTextConsumer, SpecialMesgPrintsTheBareSpellName) {
    EverQuest eq = MakeEq();
    TestBridge bridge;
    eq.setBridge(&bridge);

    ReplayZone(eq, HC_OP_SpecialMesg, SpecialMesgPayload(FireRefusal()));

    auto lines = SystemLines(bridge);
    ASSERT_EQ(lines.size(), 1u);
    EXPECT_EQ(lines[0],
              "Your spell Burst of Flame has failed to land on yourself "
              "because you are invulnerable.")
        << "the say-link body reached the player's chat line";
    EXPECT_EQ(lines[0].find('\x12'), std::string::npos)
        << "a raw 0x12 link delimiter is in the printed line";
}

TEST(SayLinkTextConsumer, ChannelMessagePrintsTheBareLinkText) {
    EverQuest eq = MakeEq();
    TestBridge bridge;
    eq.setBridge(&bridge);

    // ChannelMessage_Struct: target@2, sender@66, language@130, channel@134,
    // skill@146, message@150 (the offsets ZoneProcessChannelMessage reads).
    const std::string message = "look at " + kFireLink + " then";
    const std::string sender = "Testcaster";
    const size_t total = 150 + message.size() + 1;
    auto payload = BuildPayload(total, [&](EQ::Net::DynamicPacket& p) {
        for (size_t i = 0; i < sender.size(); ++i) {
            p.PutUInt8(66 + i, static_cast<uint8_t>(sender[i]));
        }
        p.PutUInt8(66 + sender.size(), 0);
        p.PutUInt32(134, 8);  // auction, an arbitrary public channel
        for (size_t i = 0; i < message.size(); ++i) {
            p.PutUInt8(150 + i, static_cast<uint8_t>(message[i]));
        }
        p.PutUInt8(150 + message.size(), 0);
    });
    ReplayZone(eq, HC_OP_ChannelMessage, payload);

    auto lines = SystemLines(bridge);
    ASSERT_EQ(lines.size(), 1u);
    EXPECT_EQ(lines[0], "look at Burst of Flame then");
}

}  // namespace
