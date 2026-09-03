// K13 (Batch K, refactor/K13_context_services.md) -- eqt::PacketSender, the
// third context service.
//
// QueuePacket and SafeQueueZonePacket moved verbatim out of src/client/eq.cpp,
// bringing with them the Batch R suppressed-send counter (which thereby leaves
// EverQuest, closing K09's deviation 1) and the T268 queued-packet capture
// seam. EverQuest keeps QueuePacket() as a forwarder for the 17 non-family
// call sites in combat.cpp / spell_manager.cpp / skill_manager.cpp, and keeps
// the three T268 Test_* entry points as forwarders so the existing capture
// tests are untouched.
//
// Headless: there is no zone connection, so both senders take their null
// branch. That is exactly the branch the whole replay harness runs on.

#include <gtest/gtest.h>

#include "client/packet_sender.h"
#include "zone_stream_test_utils.h"

using namespace zonetest;

namespace {

class PacketSenderTest : public ::testing::Test {
protected:
    PacketSenderTest() : eq(MakeEq()) {}
    EverQuest eq;
};

TEST_F(PacketSenderTest, ServiceIsStableAndIsTheOneTheContextCarries) {
    EXPECT_EQ(&eq.Net(), &eq.Net());
    EXPECT_EQ(&eq.Context().net, &eq.Net());
}

TEST_F(PacketSenderTest, SuppressedSendCounterStartsAtZeroAndCounts) {
    EXPECT_EQ(eq.Net().ReplaySuppressedSendCount(), 0u);
    eq.Net().NoteSuppressedSend();
    eq.Net().NoteSuppressedSend();
    EXPECT_EQ(eq.Net().ReplaySuppressedSendCount(), 2u);
}

TEST_F(PacketSenderTest, SafeQueueZoneRefusesWithNoConnection) {
    EQ::Net::DynamicPacket p;
    p.Resize(2);
    p.PutUInt16(0, 0x1234);
    EXPECT_FALSE(eq.Net().SafeQueueZone(p))
        << "with no zone connection the send must refuse, not crash";
}

TEST_F(PacketSenderTest, QueueCapturesTheWireBytesWhenTheSeamIsArmed) {
    eq.Net().Test_SetCaptureQueuedPacketsForSeam(true);
    eq.Net().Test_ClearQueuedPackets();

    EQ::Net::DynamicPacket body;
    body.Resize(4);
    body.PutUInt32(0, 0xDEADBEEF);
    eq.Net().Queue(0x4321, &body);

    const auto& q = eq.Net().Test_GetQueuedPackets();
    ASSERT_EQ(q.size(), 1u);
    EXPECT_EQ(q[0].opcode, 0x4321);
    ASSERT_EQ(q[0].bytes.size(), 4u);
    // The capture is body-only -- Queue() prepends the opcode itself.
    EXPECT_EQ(q[0].bytes[0], 0xEF);
    EXPECT_EQ(q[0].bytes[3], 0xDE);
}

TEST_F(PacketSenderTest, TheEverQuestForwarderAndTheSeamForwardersReachTheSameService) {
    eq.Test_SetCaptureQueuedPacketsForSeam(true);
    eq.Test_ClearQueuedPackets();

    EQ::Net::DynamicPacket body;
    body.Resize(2);
    body.PutUInt16(0, 0x0102);
    eq.QueuePacket(0x5555, &body);   // the sanctioned EverQuest forwarder

    // ...read back through the service, not the forwarder.
    const auto& q = eq.Net().Test_GetQueuedPackets();
    ASSERT_EQ(q.size(), 1u);
    EXPECT_EQ(q[0].opcode, 0x5555);
    EXPECT_EQ(eq.Test_GetQueuedPackets().size(), 1u);
}

TEST_F(PacketSenderTest, QueueIgnoresANullPacket) {
    eq.Net().Test_SetCaptureQueuedPacketsForSeam(true);
    eq.Net().Test_ClearQueuedPackets();
    eq.Net().Queue(0x1111, nullptr);
    EXPECT_TRUE(eq.Net().Test_GetQueuedPackets().empty());
}

}  // namespace
