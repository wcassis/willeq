// T229: the two remaining T148 GM-sender defects (refactor/deferred.md,
// `## T148 (2026-08-19)`), both found LIVE and never pinned by a test.
//
// A. SendGMSummon() memset its GMSummon_Struct and filled only the names, so
//    the body's y/x/z went out as zero. eqemu's Client::OPGMSummon
//    (zone/client_process.cpp:1877) READS them -- MovePC(zone, inst, gms->x,
//    gms->y, gms->z, ...) for an in-zone player -- which is why T148's live
//    run teleported the target to the origin (`ZonePC ... x [0] y [0] z [0]`
//    in review/T148/eqemu_zone_arena_server_log.txt). The fields are floats
//    (eqemu common/eq_packet_structs.h:1949), in SERVER coordinates, y first.
//    The sender must carry the GM's own position, converted from our
//    internal (m_x = server_y, m_y = server_x) convention.
//
// B. ZoneProcessGMFind() decoded the reply through GMSummon_Struct's int32
//    y/x/z. The reply is a GMFind_Struct (eq_packet_structs.h:1960): floats,
//    x FIRST. T148's live session printed
//    `at (-998490112, 1125908480, 1112276992)` -- those are the bit patterns
//    0xC47C4000 (-1009.0f), 0x431C0000 (156.0f), 0x424C0000 (51.0f), read as
//    int32 and from the wrong slots. The decode test below rebuilds the reply
//    from exactly those three words.

#include <gtest/gtest.h>

#include <cstddef>
#include <cstring>
#include <type_traits>

#include "client/bridge/game_state_bridge.h"
#include "common/packet_structs.h"
#include "zone_stream_test_utils.h"

using namespace zonetest;

namespace {

// --- wire layout -----------------------------------------------------------

TEST(T229WireLayout, GMSummon_Struct_MatchesEqemu) {
    EXPECT_EQ(sizeof(EQT::GMSummon_Struct), 152u);
    EXPECT_EQ(offsetof(EQT::GMSummon_Struct, charname), 0u);
    EXPECT_EQ(offsetof(EQT::GMSummon_Struct, gmname), 64u);
    EXPECT_EQ(offsetof(EQT::GMSummon_Struct, success), 128u);
    EXPECT_EQ(offsetof(EQT::GMSummon_Struct, zoneID), 132u);
    EXPECT_EQ(offsetof(EQT::GMSummon_Struct, y), 136u) << "eqemu: y before x";
    EXPECT_EQ(offsetof(EQT::GMSummon_Struct, x), 140u);
    EXPECT_EQ(offsetof(EQT::GMSummon_Struct, z), 144u);
    EXPECT_EQ(offsetof(EQT::GMSummon_Struct, unknown2), 148u);
    static_assert(std::is_same<decltype(EQT::GMSummon_Struct::x), float>::value,
                  "eqemu GMSummon_Struct.x is float");
    static_assert(std::is_same<decltype(EQT::GMSummon_Struct::y), float>::value,
                  "eqemu GMSummon_Struct.y is float");
    static_assert(std::is_same<decltype(EQT::GMSummon_Struct::z), float>::value,
                  "eqemu GMSummon_Struct.z is float");
}

TEST(T229WireLayout, GMFind_Struct_MatchesEqemu) {
    EXPECT_EQ(sizeof(EQT::GMFind_Struct), 152u);
    EXPECT_EQ(offsetof(EQT::GMFind_Struct, charname), 0u);
    EXPECT_EQ(offsetof(EQT::GMFind_Struct, gmname), 64u);
    EXPECT_EQ(offsetof(EQT::GMFind_Struct, success), 128u);
    EXPECT_EQ(offsetof(EQT::GMFind_Struct, zoneID), 132u);
    EXPECT_EQ(offsetof(EQT::GMFind_Struct, x), 136u) << "eqemu: x FIRST, unlike summon";
    EXPECT_EQ(offsetof(EQT::GMFind_Struct, y), 140u);
    EXPECT_EQ(offsetof(EQT::GMFind_Struct, z), 144u);
    EXPECT_EQ(offsetof(EQT::GMFind_Struct, unknown2), 148u);
    static_assert(std::is_same<decltype(EQT::GMFind_Struct::x), float>::value,
                  "eqemu GMFind_Struct.x is float");
}

// --- A. SendGMSummon carries the GM's position -----------------------------

class GMSummonTest : public ::testing::Test {
protected:
    GMSummonTest() : eq(MakeEq()) {}
    EverQuest eq;
};

TEST_F(GMSummonTest, NothingBuiltBeforeAnyCall) {
    EXPECT_FALSE(eq.Test_GetLastGMSummon().sent);
}

TEST_F(GMSummonTest, BodyCarriesGMPositionAsServerFloats) {
    // Internal convention: m_x = server_y, m_y = server_x. Stand the GM at
    // server (x=156, y=-1009, z=51) -- Testbuddy's T148 spot.
    eq.SetPosition(-1009.0f, 156.0f, 51.0f);
    eq.SendGMSummon("Testbuddy");

    const auto& sent = eq.Test_GetLastGMSummon();
    ASSERT_TRUE(sent.sent);
    ASSERT_EQ(sent.bytes.size(), 2u + sizeof(EQT::GMSummon_Struct));
    ASSERT_EQ(sent.bytes.size(), 154u) << "Handle_OP_GMSummon size-checks 152";

    uint16_t opcode = static_cast<uint16_t>(sent.bytes[0] | (sent.bytes[1] << 8));
    EXPECT_EQ(opcode, HC_OP_GMSummon);

    EQT::GMSummon_Struct gm;
    std::memcpy(&gm, sent.bytes.data() + 2, sizeof(gm));
    EXPECT_STREQ(gm.charname, "Testbuddy");
    EXPECT_STREQ(gm.gmname, "Testchar");
    EXPECT_FLOAT_EQ(gm.x, 156.0f) << "server x = our m_y";
    EXPECT_FLOAT_EQ(gm.y, -1009.0f) << "server y = our m_x";
    EXPECT_FLOAT_EQ(gm.z, 51.0f);

    // The T148 defect, stated on the raw bytes: the three coordinate words
    // at 136..147 must not all be zero when the GM is not at the origin.
    bool all_zero = true;
    for (size_t i = 2 + 136; i < 2 + 148; ++i) {
        all_zero = all_zero && sent.bytes[i] == 0;
    }
    EXPECT_FALSE(all_zero) << "T148: summon sent zeroed coords, target landed at (0,0,0)";
}

TEST_F(GMSummonTest, NegativeCoordinatesAreIeeeFloatsNotInts) {
    eq.SetPosition(-1009.0f, 156.0f, 51.0f);
    eq.SendGMSummon("Testbuddy");
    const auto& sent = eq.Test_GetLastGMSummon();
    ASSERT_EQ(sent.bytes.size(), 154u);
    uint32_t y_word = 0;
    std::memcpy(&y_word, sent.bytes.data() + 2 + 136, 4);
    EXPECT_EQ(y_word, 0xC47C4000u) << "-1009.0f, not int32 -1009 (0xFFFFFC0F)";
}

// --- B. ZoneProcessGMFind decodes the reply's floats -----------------------

std::vector<std::string> DrainSystemMessages(eqt::bridge::GameStateBridge& bridge) {
    std::vector<std::string> out;
    for (auto& ev : bridge.drainEvents()) {
        if (ev.type == eqt::state::GameEventType::SystemMessage) {
            out.push_back(std::get<eqt::state::ChatMessageData>(ev.data).message);
        }
    }
    return out;
}

// The reply as eqemu's Handle_OP_GMFind builds it, with the three coordinate
// words taken verbatim from the T148 live session (review/T148/README.md).
std::vector<uint8_t> GMFindReply(const std::string& target, uint32_t success, uint32_t zone,
                                 uint32_t x_word, uint32_t y_word, uint32_t z_word) {
    return BuildPayload(2 + sizeof(EQT::GMFind_Struct), [&](EQ::Net::DynamicPacket& p) {
        p.PutCString(2, target.c_str());
        p.PutCString(2 + 64, "Testchar");
        p.PutUInt32(2 + 128, success);
        p.PutUInt32(2 + 132, zone);
        p.PutUInt32(2 + 136, x_word);
        p.PutUInt32(2 + 140, y_word);
        p.PutUInt32(2 + 144, z_word);
    });
}

class GMFindTest : public ::testing::Test {
protected:
    GMFindTest() : eq(MakeEq()) { eq.setBridge(&bridge); }
    ~GMFindTest() override { eq.setBridge(nullptr); }
    EverQuest eq;
    TestBridge bridge;
};

TEST_F(GMFindTest, T148CapturedReplyPrintsRealCoordinates) {
    // Server x=156 (0x431C0000), y=-1009 (0xC47C4000), z=51 (0x424C0000).
    ReplayZone(eq, HC_OP_GMFind,
               GMFindReply("Testbuddy", 1, 77, 0x431C0000u, 0xC47C4000u, 0x424C0000u));
    auto msgs = DrainSystemMessages(bridge);
    ASSERT_EQ(msgs.size(), 1u);
    // Printed in /loc's convention (our x = server y, our y = server x).
    EXPECT_EQ(msgs[0], "Testbuddy is in zone 77 at -1009.0, 156.0, 51.0");
    EXPECT_EQ(msgs[0].find("-998490112"), std::string::npos) << "T148: int32 read of a float";
    EXPECT_EQ(msgs[0].find("1125908480"), std::string::npos);
}

TEST_F(GMFindTest, NotFoundReplySaysSoInsteadOfZoneZeroAtOrigin) {
    ReplayZone(eq, HC_OP_GMFind, GMFindReply("Nobody", 0, 0, 0, 0, 0));
    auto msgs = DrainSystemMessages(bridge);
    ASSERT_EQ(msgs.size(), 1u);
    EXPECT_EQ(msgs[0], "Nobody is not in this zone.");
}

TEST_F(GMFindTest, ShortPacketIsIgnored) {
    std::vector<uint8_t> tooShort(100, 0);
    ReplayZone(eq, HC_OP_GMFind, tooShort);
    EXPECT_TRUE(DrainSystemMessages(bridge).empty());
}

}  // namespace
