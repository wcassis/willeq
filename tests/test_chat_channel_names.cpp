// T320 (Leg C): there is ONE chat-channel name table, and this is the test
// that fails when a second one appears.
//
// eq_chat.cpp used to build the display name twice inside
// ZoneProcessChannelMessage, forty lines apart, from two hand-written ternary
// chains. They had drifted:
//
//   * the console [CHAT] chain had no CHAT_CHANNEL_AUCTION case, so every
//     auction line printed as "chan4" -- for the whole of T297's live run;
//   * the bridge's ChatMessageData::channelName chain DID have auction, so
//     the console and the UI disagreed about the same packet;
//   * neither knew channel 14, ChatChannel_TellEcho, the server's echo of an
//     outgoing tell, which printed as "chan14".
//
// Both consumers now call ChatChannelName(). Two things are pinned here: the
// names themselves, and -- the actual guard -- that eq_chat.cpp contains no
// second hand-rolled chain. Same shape as the T314 / T308 guards.

#include <gtest/gtest.h>

#include "client/eq.h"

#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#ifndef WILLEQ_REPO_ROOT
#define WILLEQ_REPO_ROOT "."
#endif

namespace {

TEST(ChatChannelNames, EveryChannelTheClientCanSeeHasAName) {
    EXPECT_EQ(ChatChannelName(CHAT_CHANNEL_GUILD), "guild");
    EXPECT_EQ(ChatChannelName(CHAT_CHANNEL_GROUP), "group");
    EXPECT_EQ(ChatChannelName(CHAT_CHANNEL_SHOUT), "shout");
    EXPECT_EQ(ChatChannelName(CHAT_CHANNEL_OOC), "ooc");
    EXPECT_EQ(ChatChannelName(CHAT_CHANNEL_BROADCAST), "broadcast");
    EXPECT_EQ(ChatChannelName(CHAT_CHANNEL_TELL), "tell");
    EXPECT_EQ(ChatChannelName(CHAT_CHANNEL_SAY), "say");
    EXPECT_EQ(ChatChannelName(CHAT_CHANNEL_PETITION), "petition");
    EXPECT_EQ(ChatChannelName(CHAT_CHANNEL_GMSAY), "gmsay");
    EXPECT_EQ(ChatChannelName(CHAT_CHANNEL_RAID), "raid");
    EXPECT_EQ(ChatChannelName(CHAT_CHANNEL_EMOTE), "emote");
}

// The two channels the drifted tables got wrong, called out by name so a
// regression says which defect came back.
TEST(ChatChannelNames, AuctionIsNamed) {
    // The console chain printed "chan4" for this for all of T297's run.
    EXPECT_EQ(ChatChannelName(CHAT_CHANNEL_AUCTION), "auction");
}

TEST(ChatChannelNames, TellEchoIsNamed) {
    // Channel 14 -- neither table knew it existed; it printed "chan14".
    EXPECT_EQ(ChatChannelName(14), "tellecho");
    EXPECT_EQ(ChatChannelName(CHAT_CHANNEL_TELL_ECHO), "tellecho");
}

// An unnamed channel keeps its NUMBER. A flat "other" is what would have
// hidden channel 14 forever; "chanN" is what made it findable in a log.
TEST(ChatChannelNames, UnknownChannelKeepsItsNumber) {
    EXPECT_EQ(ChatChannelName(99), "chan99");
    EXPECT_EQ(ChatChannelName(20), "chan20");   // UCSRelay, not a Titanium channel
}

// THE GUARD, and it earned its keep the first time it ran: written for the
// two known chains, it immediately failed on a THIRD one nobody had noticed,
// in ZoneSendChannelMessage's outgoing debug line -- drifted differently
// again (auction present, but no emote/petition/gmsay/raid/broadcast, and a
// bare "unknown" fallback that threw the channel number away).
//
// It looks for the SHAPE of a hand-rolled name table -- a channel compared
// against a CHAT_CHANNEL_* constant and immediately yielding a string
// literal, i.e. `channel == CHAT_CHANNEL_X ? "x"` -- rather than for
// comparisons in general. Comparisons that ROUTE rather than NAME (the tell
// tracking, the ungrouped-/gsay guard, the T243 emote special-case) are
// legitimate and must not have to be counted or budgeted for.
TEST(ChatChannelNames, NoSecondCopyOfTheTableInEqChat) {
    const std::string path = std::string(WILLEQ_REPO_ROOT) + "/src/client/eq_chat.cpp";
    std::ifstream in(path);
    ASSERT_TRUE(in.is_open()) << "cannot read " << path;
    std::stringstream ss;
    ss << in.rdbuf();
    const std::string src = ss.str();

    std::vector<std::string> offenders;
    const std::string needle = "== CHAT_CHANNEL_";
    for (size_t i = src.find(needle); i != std::string::npos;
         i = src.find(needle, i + 1)) {
        // Look at the rest of this line: a naming ternary reaches `? "` on it.
        const size_t eol = src.find('\n', i);
        const std::string rest = src.substr(i, (eol == std::string::npos ? src.size() : eol) - i);
        if (rest.find("? \"") != std::string::npos) {
            offenders.push_back(rest);
        }
    }

    EXPECT_TRUE(offenders.empty())
        << "eq_chat.cpp maps a channel to a NAME inline, " << offenders.size()
        << " time(s) -- e.g.: " << (offenders.empty() ? "" : offenders.front())
        << "\nThat is the T320 drift coming back (three copies existed, and all "
           "three disagreed: auction missing from one, channel 14 from all). "
           "Call ChatChannelName() instead.";
}

} // namespace
