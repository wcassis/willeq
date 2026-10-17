// K02 (Batch K, refactor/K02_gm_component.md) -- eqt::gm::GmComponent.
//
// K02 dropped forwarders entirely, following K01's ruling: EverQuest no
// longer declares a single GM sender or packet handler, and every caller
// goes through EverQuest::Gm(). This file covers the component itself --
// construction against a real EverQuest, the ClientContext binding, and two
// ported cases (T217's OP_GMHideMe wire pin, T229's OP_GMSummon position
// body) that exercise the component's own test seams directly.
// test_group_accept_gm_hideme.cpp and test_gm_summon_find.cpp keep their own
// (now larger) coverage; only their call sites were retargeted to eq.Gm().

#include <gtest/gtest.h>

#include <cstring>

#include "client/gm/gm_component.h"
#include "zone_stream_test_utils.h"

using namespace zonetest;

namespace {

// Minimal fake bridge, the same idiom GMFindTest's (test_gm_summon_find.cpp):
// only the drain side is exercised -- the denial message's route
// (ChatOutput::System, chat_output.cpp:24-33).
class TestBridge : public eqt::bridge::GameStateBridge {
public:
    void applyEvent(const eqt::state::GameEvent&) override {}
};

class GmComponentTest : public ::testing::Test {
protected:
    GmComponentTest() : eq(MakeEq()) { eq.setBridge(&bridge); }
    ~GmComponentTest() override { eq.setBridge(nullptr); }
    EverQuest eq;
    TestBridge bridge;
};

// The denial path publishes through ChatOutput (chat_output.cpp:24-33),
// which needs a bridge to be observable; same idiom as GMFindTest's.
std::vector<std::string> DrainSystemMessages(eqt::bridge::GameStateBridge& b) {
    std::vector<std::string> out;
    for (auto& ev : b.drainEvents()) {
        if (ev.type == eqt::state::GameEventType::SystemMessage) {
            out.push_back(std::get<eqt::state::ChatMessageData>(ev.data).message);
        }
    }
    return out;
}

// ===================================================================
// 1. It constructs against a real EverQuest in headless mode
// ===================================================================

TEST_F(GmComponentTest, ComponentExistsAndIsStable) {
    // Gm() must hand back the SAME component every time, not a copy.
    EXPECT_EQ(&eq.Gm(), &eq.Gm());
}

TEST_F(GmComponentTest, ContextPointsAtTheOwningEverQuestsOwnMembers) {
    // The context is a bundle of references, not a copy: the character name
    // the component sends must be the one EverQuest was constructed with.
    EXPECT_EQ(&eq.Context().game_state, &eq.GetGameState());
    EXPECT_EQ(eq.Context().character, eq.GetMyName());
    EXPECT_EQ(eq.Context().character, "Testchar");
}

// ===================================================================
// 2. T217: SendGMHideMe's wire body, ported onto the component directly
// ===================================================================

TEST_F(GmComponentTest, HideMeSeamStartsEmpty) {
    EXPECT_FALSE(eq.Gm().Test_GetLastGMHideMe().sent);
}

TEST_F(GmComponentTest, HideOnSendsOpGMHideMeWithVisibilityZero) {
    eq.Gm().SendGMHideMe(true);
    const auto& sent = eq.Gm().Test_GetLastGMHideMe();
    ASSERT_TRUE(sent.sent);
    // 2-byte opcode + the 8-byte SpawnAppearance_Struct Handle_OP_GMHideMe
    // size-checks against.
    ASSERT_EQ(sent.bytes.size(), 2u + sizeof(EQT::SpawnAppearance_Struct));

    uint16_t opcode = static_cast<uint16_t>(sent.bytes[0] | (sent.bytes[1] << 8));
    EXPECT_EQ(opcode, HC_OP_GMHideMe);
    EXPECT_NE(opcode, HC_OP_SpawnAppearance) << "the T148 misroute";

    EQT::SpawnAppearance_Struct sa;
    std::memcpy(&sa, sent.bytes.data() + 2, sizeof(sa));
    EXPECT_EQ(sa.type, AT_GM);
    EXPECT_EQ(sa.parameter, 0u) << "parameter is a VISIBILITY flag: 0 hides";
}

// ===================================================================
// 3. T229: SendGMSummon carries the GM's position, ported onto the
//    component directly -- proves owner_.PosX()/PosY()/PosZ() still reach
//    EverQuest's real position after the move.
// ===================================================================

TEST_F(GmComponentTest, SummonSeamStartsEmpty) {
    EXPECT_FALSE(eq.Gm().Test_GetLastGMSummon().sent);
}

TEST_F(GmComponentTest, SummonBodyCarriesGMPositionAsServerFloats) {
    // Internal convention: m_x = server_y, m_y = server_x.
    eq.SetPosition(-1009.0f, 156.0f, 51.0f);
    eq.Gm().SendGMSummon("Testbuddy");

    const auto& sent = eq.Gm().Test_GetLastGMSummon();
    ASSERT_TRUE(sent.sent);
    ASSERT_EQ(sent.bytes.size(), 2u + sizeof(EQT::GMSummon_Struct));

    EQT::GMSummon_Struct gm;
    std::memcpy(&gm, sent.bytes.data() + 2, sizeof(gm));
    EXPECT_STREQ(gm.charname, "Testbuddy");
    EXPECT_STREQ(gm.gmname, "Testchar");
    EXPECT_FLOAT_EQ(gm.x, 156.0f) << "server x = our m_y";
    EXPECT_FLOAT_EQ(gm.y, -1009.0f) << "server y = our m_x";
    EXPECT_FLOAT_EQ(gm.z, 51.0f);
}

// ===================================================================
// 4. B1081 (#401): an APPROVED GM zone reply triggers the zone-confirm
//    packet. eqemu's Handle_OP_GMZoneRequest2 (zone/client_packet.cpp:
//    7132-7143) is the half that performs the change -- GoToSafeCoords
//    (zoning.cpp:1074). Until B1081, ZoneProcessGMZoneRequest only
//    logged the approval, so a live /gmzone <char> <zone> never zoned
//    and the zone_change_no_flash scenario could never reach its
//    transition arm.
// ===================================================================

// The approval reply as eqemu's Handle_OP_GMZoneRequest builds it: the
// same 88-byte GMZoneRequest_Struct with the target zone's safe coords
// and success=1 (client_packet.cpp:7108-7126).
std::vector<uint8_t> GMZoneApprovalReply(const std::string& charname, uint32_t zone_id,
                                         uint32_t success, float x = 0.0f,
                                         float y = 0.0f, float z = 0.0f, float heading = 0.0f) {
    return BuildPayload(2 + sizeof(EQT::GMZoneRequest_Struct), [&](EQ::Net::DynamicPacket& p) {
        p.PutCString(2, charname.c_str());
        p.PutUInt32(2 + 64, zone_id);
        p.PutFloat(2 + 68, x);
        p.PutFloat(2 + 72, y);
        p.PutFloat(2 + 76, z);
        p.PutFloat(2 + 80, heading);
        p.PutUInt32(2 + 84, success);
    });
}

TEST_F(GmComponentTest, GMZoneRequest2SeamStartsEmpty) {
    EXPECT_FALSE(eq.Gm().Test_GetLastGMZoneRequest2().sent);
}

TEST_F(GmComponentTest, ApprovedGMZoneReplySendsZoneConfirm) {
    ReplayZone(eq, HC_OP_GMZoneRequest, GMZoneApprovalReply("Testchar", 303, 1));

    const auto& sent = eq.Gm().Test_GetLastGMZoneRequest2();
    ASSERT_TRUE(sent.sent) << "an approved reply must confirm (the oracle's second packet)";
    ASSERT_EQ(sent.bytes.size(), 6u) << "OP_GMZoneRequest2 is a uint32 zone id";
    EXPECT_EQ(sent.zone_id, 303u);

    uint16_t opcode = static_cast<uint16_t>(sent.bytes[0] | (sent.bytes[1] << 8));
    EXPECT_EQ(opcode, HC_OP_GMZoneRequest2);
    EXPECT_EQ(opcode, 0x244c) << "oracle opcode (patch_Titanium.conf: OP_GMZoneRequest2=0x244c)";
    EXPECT_NE(opcode, HC_OP_GMZoneRequest) << "the confirm is NOT a re-query";

    uint32_t zone_word = 0;
    std::memcpy(&zone_word, sent.bytes.data() + 2, 4);
    EXPECT_EQ(zone_word, 303u) << "eqemu reads exactly this word: GoToSafeCoords(zonereq, 0)";
}

TEST_F(GmComponentTest, DeniedGMZoneReplySendsNoConfirm) {
    // success=0: the server refused (unknown zone, insufficient GM rank).
    // Confirming anyway would be a lie the server's own GM check rejects.
    ReplayZone(eq, HC_OP_GMZoneRequest, GMZoneApprovalReply("Testchar", 303, 0));

    const auto& sent = eq.Gm().Test_GetLastGMZoneRequest2();
    EXPECT_FALSE(sent.sent);
    EXPECT_TRUE(sent.bytes.empty());

    // The denial must still reach the player -- that was the handler's
    // only pre-B1081 behaviour and it is unchanged.
    auto msgs = DrainSystemMessages(bridge);
    ASSERT_EQ(msgs.size(), 1u);
    EXPECT_EQ(msgs[0], "Zone request denied.");
}

TEST_F(GmComponentTest, ShortGMZoneReplySendsNoConfirm) {
    std::vector<uint8_t> tooShort(100, 0);
    ReplayZone(eq, HC_OP_GMZoneRequest, tooShort);
    EXPECT_FALSE(eq.Gm().Test_GetLastGMZoneRequest2().sent);
// 5. B1088: an APPROVED GM zone request initiates the transition
//    (OP_ZoneChange handshake), a denied one does not
// ===================================================================

// Builds the S->C OP_GMZoneRequest reply (GMZoneRequest_Struct, 88 bytes +
// 2-byte opcode = 90) the way eqemu's Handle_OP_GMZoneRequest sends it:
// charname, zone_id, then the TARGET zone's safe coordinates in WIRE order
// (server x first), heading, success.
std::vector<uint8_t> BuildGMZoneReply(const std::string& charname, uint32_t zone_id,
                                      float wire_x, float wire_y, float wire_z,
                                      float wire_heading, uint32_t success) {
    EQ::Net::DynamicPacket p;
    p.Resize(90);
    std::memset(p.Data(), 0, 90);
    p.PutUInt16(0, 0x1306);  // HC_OP_GMZoneRequest (the opcode Replay re-adds)
    std::strncpy(static_cast<char*>(p.Data()) + 2, charname.c_str(), 63);
    p.PutUInt32(2 + 64, zone_id);
    p.PutFloat(2 + 68, wire_x);
    p.PutFloat(2 + 72, wire_y);
    p.PutFloat(2 + 76, wire_z);
    p.PutFloat(2 + 80, wire_heading);
    p.PutUInt32(2 + 84, success);
    std::vector<uint8_t> payload(88);
    std::memcpy(payload.data(), static_cast<const uint8_t*>(p.Data()) + 2, payload.size());
    return payload;
}

TEST_F(GmComponentTest, ApprovedGMZoneInitiatesTheZoneChangeHandshake) {
    auto before = eq.Zone().Test_GetLastZoneChangeRequest();

    // Target zone 2, safe point server (x=146, y=-1009, z=51), approved.
    eq.ReplayZonePacket(HC_OP_GMZoneRequest,
                        BuildGMZoneReply("Testchar", 2, 146.0f, -1009.0f, 51.0f, 64.0f, 1).data(), 88);

    auto after = eq.Zone().Test_GetLastZoneChangeRequest();
    EXPECT_EQ(after.attempts, before.attempts + 1)
        << "an approved GM zone request must answer with OP_ZoneChange, the "
           "same handshake death-to-bind performs (player_state.cpp:464)";
    EXPECT_EQ(after.zone_id, 2u);

    // The reply's WIRE-order safe coords become our internal order: our x =
    // server y, our y = server x -- the same swap ZonePlayerToBind applies
    // (player_state.cpp:401-405). The pending zone carries them, and B1088's
    // loading-screen refresh reads the target id from it.
    EXPECT_FLOAT_EQ(eq.Context().game_state.world().pendingZoneX(), -1009.0f) << "our x = server y";
    EXPECT_FLOAT_EQ(eq.Context().game_state.world().pendingZoneY(), 146.0f) << "our y = server x";
    EXPECT_FLOAT_EQ(eq.Context().game_state.world().pendingZoneZ(), 51.0f);
}

TEST_F(GmComponentTest, DeniedGMZoneDoesNotInitiateAZoneChange) {
    auto before = eq.Zone().Test_GetLastZoneChangeRequest();

    eq.ReplayZonePacket(HC_OP_GMZoneRequest,
                        BuildGMZoneReply("Testchar", 2, 146.0f, -1009.0f, 51.0f, 64.0f, 0).data(), 88);

    auto after = eq.Zone().Test_GetLastZoneChangeRequest();
    EXPECT_EQ(after.attempts, before.attempts)
        << "a denied GM zone request must not answer with OP_ZoneChange";
}

}  // namespace
