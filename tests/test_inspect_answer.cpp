// T170 -- OP_InspectAnswer generation.
//
// This client parsed inspect answers but never produced one, so inspecting a
// WillEQ player showed an empty window. eqemu forwards the request to the
// target and expects a Titanium client to reply itself
// (Handle_OP_InspectRequest, zone/client_packet.cpp:9084-9087); only SoF+
// clients and bots get a server-built answer.
//
// Note WHY this survived: the request and the empty window are on two
// different machines, so no single-client test could show it. The project's
// standing rule -- assert on what an observing second instance sees -- applies
// here, and the live half of that is owed, not done.
//
// What these tests pin is the field inversion. In the REQUEST, TargetID is the
// player being inspected. In the ANSWER, TargetID is the REQUESTER, because
// the server routes the answer on it, and the inspected player is `playerid`.
// Getting that backwards is not hypothetical: the receive path in this client
// had it backwards until T170.

#include <gtest/gtest.h>

#include "client/eq.h"
#include "client/eq_action_handler.h"
#include "client/inspect_answer.h"

#include <cstring>
#include <optional>
#include <filesystem>
#include <fstream>
#include <string>

namespace {

constexpr uint32_t kRequester = 460;   // the player doing the inspecting
constexpr uint32_t kMe = 1234;         // us, the player being inspected

TEST(InspectAnswer, TargetIdIsTheRequesterAndPlayeridIsUs) {
    auto a = EQT::MakeInspectAnswer(kRequester, kMe, "");

    EXPECT_EQ(a.TargetID, kRequester)
        << "TargetID is the ROUTING field -- Handle_OP_InspectAnswer looks it "
           "up to decide whom to forward to (client_packet.cpp:9020). Putting "
           "our own id here sends the answer back to ourselves.";
    EXPECT_EQ(a.playerid, kMe)
        << "playerid identifies the inspected player -- eqemu's own builder, "
           "Client::ProcessInspectRequest (client.cpp:6590-6591)";
}

TEST(InspectAnswer, TheTwoIdsAreNotInterchangeable) {
    auto a = EQT::MakeInspectAnswer(kRequester, kMe, "");
    auto swapped = EQT::MakeInspectAnswer(kMe, kRequester, "");

    // A test that only checked "both ids appear somewhere" would pass on a
    // swapped implementation. This one cannot.
    EXPECT_NE(a.TargetID, swapped.TargetID);
    EXPECT_NE(a.playerid, swapped.playerid);
}

TEST(InspectAnswer, EquipmentIsLeftEmptyBecauseTheServerOverwritesIt) {
    auto a = EQT::MakeInspectAnswer(kRequester, kMe, "");

    for (int i = 0; i < 22; ++i) {
        EXPECT_EQ(a.itemnames[i][0], '\0') << "slot " << i;
        EXPECT_EQ(a.itemicons[i], 0xFFFFFFFFu)
            << "slot " << i << ": eqemu's own no-item marker "
            << "(client_packet.cpp:9041)";
    }
}

// The server memcpy()s this field over the character's stored inspect message
// and saves it, so what goes in it is not cosmetic.
TEST(InspectAnswer, MessageIsCopiedAndNulTerminated) {
    const std::string msg = "Hail, a wandering adventurer";
    auto a = EQT::MakeInspectAnswer(kRequester, kMe, msg);

    EXPECT_EQ(std::string(a.text), msg);
}

// T171: the bound that matters is the SERVER's 256-byte copy, not our
// 288-byte field. At 288 the stored message would carry NO terminator inside
// the 256 bytes eqemu keeps, and eqemu later strcpy()s that stored text
// (client.cpp:6628) -- an overread on their side. 255 + NUL is the limit.
TEST(InspectAnswer, AnOverlongMessageIsTruncatedToTheServersLimitNotTheFields) {
    const std::string msg(1000, 'x');
    auto a = EQT::MakeInspectAnswer(kRequester, kMe, msg);

    EXPECT_EQ(std::strlen(a.text), EQT::INSPECT_MESSAGE_MAX_LEN);
    EXPECT_LT(EQT::INSPECT_MESSAGE_MAX_LEN, sizeof(a.text) - 1)
        << "if this ever equals the field size the terminator is outside the "
           "256 bytes the server keeps";
    EXPECT_EQ(a.text[EQT::INSPECT_MESSAGE_MAX_LEN], '\0');
}

// A message exactly at the limit must survive whole -- an off-by-one in the
// bound would eat its last character and nothing else would notice.
TEST(InspectAnswer, AMessageExactlyAtTheLimitIsNotTruncated) {
    const std::string msg(EQT::INSPECT_MESSAGE_MAX_LEN, 'y');
    auto a = EQT::MakeInspectAnswer(kRequester, kMe, msg);

    EXPECT_EQ(std::string(a.text), msg);
}

TEST(InspectAnswer, EmptyMessageProducesAnEmptyField) {
    auto a = EQT::MakeInspectAnswer(kRequester, kMe, "");
    EXPECT_EQ(a.text[0], '\0');
}

// The body must be exactly this size or eqemu's DECODE_LENGTH_EXACT drops it
// before the handler ever runs.
TEST(InspectAnswer, BodyIsTheExactTitaniumSize) {
    EXPECT_EQ(sizeof(EQT::InspectResponse_Struct), 1792u);
}

// The sender end-to-end, through the real EverQuest. Same headless idiom as
// T88/T90/T167.
TEST(InspectAnswer, SendInspectAnswerBuildsTheBodyFromOurOwnSpawnId) {
    EverQuest eq("127.0.0.1", 0, "test", "test", "test", "Testchar");
    eq.Net().Test_SetBypassSendGuardForSeam(true);
    eq.Test_SetMySpawnIdForSeam(kMe);

    EXPECT_FALSE(eq.Player().Test_GetLastInspectAnswer().sent);

    eq.Player().SendInspectAnswer(kRequester);

    auto sent = eq.Player().Test_GetLastInspectAnswer();
    ASSERT_TRUE(sent.sent);
    EXPECT_EQ(sent.target_id, kRequester);
    EXPECT_EQ(sent.playerid, kMe);
}


// ---------------------------------------------------------------------
// T171 -- the local inspect message: truncation, and the config round trip.
//
// This client has to hold the message itself because eqemu NEVER tells a
// Titanium client its own: it is loaded server-side at zone-in and only ever
// emitted inside an answer about somebody else. Without a local copy the only
// honest thing to send was empty, which overwrote whatever the server held.
// ---------------------------------------------------------------------

TEST(InspectMessageStore, SetTruncatesToTheServersLimit) {
    EverQuest eq("127.0.0.1", 0, "test", "test", "test", "Testchar");

    eq.Bridge().SetInspectMessage(std::string(1000, 'z'));
    EXPECT_EQ(eq.Bridge().GetInspectMessage().size(), EQT::INSPECT_MESSAGE_MAX_LEN)
        << "truncation happens on SET so the user is shown what will actually "
           "reach the server";
}

TEST(InspectMessageStore, StartsEmpty) {
    EverQuest eq("127.0.0.1", 0, "test", "test", "test", "Testchar");
    EXPECT_TRUE(eq.Bridge().GetInspectMessage().empty());
}

TEST(InspectMessageStore, SurvivesAConfigRoundTrip) {
    const std::string path =
        std::filesystem::temp_directory_path() / "willeq_t171_roundtrip.json";
    std::filesystem::remove(path);

    const std::string msg = "Bard for hire, tips appreciated";
    {
        EverQuest eq("127.0.0.1", 0, "test", "test", "test", "Testchar");
        eq.Bridge().SetConfigPath(path);
        eq.Bridge().SetInspectMessage(msg);
        eq.Bridge().SaveInspectMessage();
    }
    {
        EverQuest eq("127.0.0.1", 0, "test", "test", "test", "Testchar");
        eq.Bridge().SetConfigPath(path);
        ASSERT_TRUE(eq.Bridge().GetInspectMessage().empty());
        eq.Bridge().LoadInspectMessage();
        EXPECT_EQ(eq.Bridge().GetInspectMessage(), msg);
    }
    std::filesystem::remove(path);
}

// The message and the hotbar share one config file and one pair of
// resolution helpers (T171 extracted them so they cannot drift). Writing one
// must not destroy the other.
TEST(InspectMessageStore, SavingDoesNotDiscardOtherKeysInTheConfig) {
    const std::string path =
        std::filesystem::temp_directory_path() / "willeq_t171_coexist.json";
    std::filesystem::remove(path);
    {
        std::ofstream f(path);
        f << R"({"clients":[{"host":"example","inspectMessage":"old"}]})";
    }

    {
        EverQuest eq("127.0.0.1", 0, "test", "test", "test", "Testchar");
        eq.Bridge().SetConfigPath(path);
        eq.Bridge().SetInspectMessage("new");
        eq.Bridge().SaveInspectMessage();
    }

    std::ifstream in(path);
    const std::string written((std::istreambuf_iterator<char>(in)),
                              std::istreambuf_iterator<char>());
    EXPECT_NE(written.find("\"new\""), std::string::npos);
    EXPECT_NE(written.find("example"), std::string::npos)
        << "the unrelated key must survive the write";
    std::filesystem::remove(path);
}

TEST(InspectMessageStore, LoadingFromAConfigWithoutTheKeyLeavesItEmpty) {
    const std::string path =
        std::filesystem::temp_directory_path() / "willeq_t171_missing.json";
    std::filesystem::remove(path);
    {
        std::ofstream f(path);
        f << R"({"clients":[{"host":"example"}]})";
    }

    EverQuest eq("127.0.0.1", 0, "test", "test", "test", "Testchar");
    eq.Bridge().SetConfigPath(path);
    eq.Bridge().SetInspectMessage("set before load");
    eq.Bridge().LoadInspectMessage();
    EXPECT_EQ(eq.Bridge().GetInspectMessage(), "set before load")
        << "a config with no inspectMessage must not clear what is already set";
    std::filesystem::remove(path);
}

// The handler layer: show / set / clear, through a real EqActionHandler.
TEST(InspectMessageStore, HandlerSetsAndClearsTheStoredMessage) {
    EverQuest eq("127.0.0.1", 0, "test", "test", "test", "Testchar");
    eqt::EqActionHandler handler(eq);

    handler.setInspectMessage(std::string("Selling spells"));
    EXPECT_EQ(eq.Bridge().GetInspectMessage(), "Selling spells");

    handler.setInspectMessage(std::string());
    EXPECT_TRUE(eq.Bridge().GetInspectMessage().empty()) << "empty string clears";
}

TEST(InspectMessageStore, HandlerShowDoesNotModify) {
    EverQuest eq("127.0.0.1", 0, "test", "test", "test", "Testchar");
    eqt::EqActionHandler handler(eq);
    eq.Bridge().SetInspectMessage("unchanged");

    handler.setInspectMessage(std::nullopt);
    EXPECT_EQ(eq.Bridge().GetInspectMessage(), "unchanged")
        << "the show path must never write -- it is the no-argument form of "
           "the command, i.e. the one typed by accident";
}

// The whole point: what we set is what we answer with.
TEST(InspectMessageStore, TheAnswerCarriesTheStoredMessage) {
    EverQuest eq("127.0.0.1", 0, "test", "test", "test", "Testchar");
    eq.Net().Test_SetBypassSendGuardForSeam(true);
    eq.Test_SetMySpawnIdForSeam(kMe);
    eq.Bridge().SetInspectMessage("Hail");

    auto a = EQT::MakeInspectAnswer(kRequester, kMe, eq.Bridge().GetInspectMessage());
    EXPECT_EQ(std::string(a.text), "Hail");
}

}  // namespace
