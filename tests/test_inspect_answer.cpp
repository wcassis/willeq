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
#include "client/inspect_answer.h"

#include <cstring>
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

TEST(InspectAnswer, AnOverlongMessageIsTruncatedNotOverflowed) {
    const std::string msg(1000, 'x');
    auto a = EQT::MakeInspectAnswer(kRequester, kMe, msg);

    EXPECT_EQ(std::strlen(a.text), sizeof(a.text) - 1);
    EXPECT_EQ(a.text[sizeof(a.text) - 1], '\0');
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
    eq.Test_SetBypassSendGuardForSeam(true);
    eq.Test_SetMySpawnIdForSeam(kMe);

    EXPECT_FALSE(eq.Test_GetLastInspectAnswer().sent);

    eq.SendInspectAnswer(kRequester);

    auto sent = eq.Test_GetLastInspectAnswer();
    ASSERT_TRUE(sent.sent);
    EXPECT_EQ(sent.target_id, kRequester);
    EXPECT_EQ(sent.playerid, kMe);
}

}  // namespace
