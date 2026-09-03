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

class GmComponentTest : public ::testing::Test {
protected:
    GmComponentTest() : eq(MakeEq()) {}
    EverQuest eq;
};

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

}  // namespace
