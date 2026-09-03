// K05 (Batch K, refactor/K05_merchant_component.md) -- eqt::merchant::MerchantComponent.
//
// K05 followed K01's no-forwarder ruling: EverQuest no longer declares a
// single merchant sender or packet handler, and every caller goes through
// EverQuest::Merchant(). This file covers the component itself -- construction
// against a real EverQuest, the ClientContext binding, the one data member
// that MOVED (m_trainer_skill_caps, reachable only through the component's own
// GetTrainerSkillCap), the static that moved with it (TrainingCost), and two
// bodies that prove the owner_ transition handle still reaches EverQuest's own
// state after the move.
//
// The family's behavioural coverage stays where it was: test_train_skill.cpp,
// test_eq_bank_coins.cpp, test_scribe_scroll.cpp, test_read_book.cpp,
// test_loot_window_actions.cpp and the vendor/trade suites keep their
// assertions and only had their call sites retargeted to eq.Merchant().

#include <gtest/gtest.h>

#include "client/merchant/merchant_component.h"
#include "zone_stream_test_utils.h"

using namespace zonetest;

namespace {

class MerchantComponentTest : public ::testing::Test {
protected:
    MerchantComponentTest() : eq(MakeEq()) {}
    EverQuest eq;
};

// ===================================================================
// 1. It constructs against a real EverQuest in headless mode
// ===================================================================

TEST_F(MerchantComponentTest, ComponentExistsAndIsStable) {
    // Merchant() must hand back the SAME component every time, not a copy.
    EXPECT_EQ(&eq.Merchant(), &eq.Merchant());
}

TEST_F(MerchantComponentTest, ContextPointsAtTheOwningEverQuestsOwnMembers) {
    // The context is a bundle of references, not a copy.
    EXPECT_EQ(&eq.Context().game_state, &eq.GetGameState());
    EXPECT_EQ(eq.Context().character, eq.GetMyName());
    EXPECT_EQ(eq.Context().character, "Testchar");
}

// ===================================================================
// 2. The one member that MOVED: m_trainer_skill_caps (T280)
// ===================================================================

TEST_F(MerchantComponentTest, TrainerCapsStartEmptyOnTheComponent) {
    // No OP_GMTraining reply has arrived, so the component's own vector is
    // empty and every skill id reads back as "this trainer does not teach it".
    EXPECT_EQ(eq.Merchant().GetTrainerSkillCap(0), 0u);
    EXPECT_EQ(eq.Merchant().GetTrainerSkillCap(1), 0u);
    EXPECT_EQ(eq.Merchant().GetTrainerSkillCap(255), 0u)
        << "an out-of-range id must be size-checked, not indexed";
}

TEST_F(MerchantComponentTest, TrainingCostIsStaticOnTheComponent) {
    // eqemu's Client::OPGMTrainSkill cubic, unchanged by the move. Reachable
    // without an instance, which is why /train can quote it.
    EXPECT_EQ(eqt::merchant::MerchantComponent::TrainingCost(0), 0u);
    EXPECT_EQ(eqt::merchant::MerchantComponent::TrainingCost(10), 0u);
    EXPECT_EQ(eqt::merchant::MerchantComponent::TrainingCost(20), 10u);
    EXPECT_EQ(eqt::merchant::MerchantComponent::TrainingCost(50), 640u);
}

// ===================================================================
// 3. owner_ still reaches EverQuest: the window guards read GameState
// ===================================================================

TEST_F(MerchantComponentTest, ClosingAWindowThatIsNotOpenIsANoOp) {
    ASSERT_FALSE(eq.IsVendorWindowOpen());
    ASSERT_FALSE(eq.IsBankWindowOpen());
    ASSERT_FALSE(eq.IsTrainerWindowOpen());

    eq.Test_SetCaptureQueuedPacketsForSeam(true);
    eq.Test_ClearQueuedPackets();

    eq.Merchant().CloseVendorWindow();
    eq.Merchant().CloseBankWindow();
    eq.Merchant().CloseTrainerWindow();

    // Each body's first statement is a GameState guard read through ctx_;
    // with no window open nothing reaches the wire.
    EXPECT_TRUE(eq.Test_GetQueuedPackets().empty());
    EXPECT_FALSE(eq.IsVendorWindowOpen());
    EXPECT_FALSE(eq.IsBankWindowOpen());
    EXPECT_FALSE(eq.IsTrainerWindowOpen());
}

TEST_F(MerchantComponentTest, ScribeRefusalReachesTheSpellManagerThroughTheContext) {
    // T276's refusal table. Spell id 0 is not a spell; whether the spell
    // system is present or not, the component must produce a refusal rather
    // than an empty string (an empty string means "go ahead and scribe").
    EXPECT_FALSE(eq.Merchant().ScribeRefusalMessage(0).empty());
}

}  // namespace

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
