// B441 (GitLab #26, UIGAP-small) -- the bank window's Change button.
//
// The oracle, from the client's own files: no EQUI_*.xml under
// uifiles/default declares a coin-conversion dialog, and the Bank Tip window
// (help/tips/ctxbank.html) says "Click on the "Change" button to convert all
// your coin, both in the bank and on your person, to the fewest possible
// coins." So Change is one click; T436 drew it greyed because it assumed a
// dialog fable did not have.
//
// Pinned here:
//   1. the PACKET SEQUENCE: BuildMakeChangeMoves() yields OP_MoveCoin steps
//      copper -> silver -> gold -> platinum within one purse, each amount a
//      whole multiple of 10;
//   2. that eqemu ACCEPTS them as sent: replaying the steps through a port
//      of Client::OPMoveCoin's conversion arithmetic (zone/client_process.cpp:
//      clamp to the bucket, add take * coppers(from)/coppers(to) truncating,
//      take back only what was added) lands on exactly the purse the client
//      projects -- the server sends nothing back, so the two must agree;
//   3. the button: hit-testable, and a click pushes BankMakeChangeIntent.
// `/bank change` is pinned in test_command_processor_world.cpp.

#include <gtest/gtest.h>

#include <cmath>
#include <fstream>
#include <sstream>
#include <variant>

#include "client/bridge/game_state_bridge.h"
#include "client/events/renderer_intents.h"
#include "client/graphics/ui/bank_layout.h"
#include "client/graphics/ui/bank_panel.h"
#include "client/graphics/ui/cursor_attachment.h"
#include "client/graphics/ui/merchant_panel.h"
#include "client/graphics/ui/static_ui_input.h"
#include "client/graphics/ui/ui_layout.h"
#include "client/merchant/coin_change.h"

using eqt::merchant::BuildMakeChangeMoves;
using eqt::merchant::CoinPurse;

namespace {

// eqemu's CoinTypeCoppers().
int64_t Coppers(int32_t type) {
    static const int64_t k[4] = {1, 10, 100, 1000};
    return k[type];
}

// Client::OPMoveCoin for from_slot == to_slot, the arithmetic verbatim
// (float ratios and all).
void ServerApply(const EQT::MoveCoin_Struct& mc, CoinPurse& purse) {
    ASSERT_GE(mc.amount, 0);
    ASSERT_EQ(mc.from_slot, mc.to_slot);
    uint64_t take = purse[mc.cointype1] < static_cast<uint32_t>(mc.amount)
                        ? purse[mc.cointype1] : static_cast<uint64_t>(mc.amount);
    const uint64_t add = static_cast<uint64_t>(
        take * (static_cast<float>(Coppers(mc.cointype1)) / static_cast<float>(Coppers(mc.cointype2))));
    take = static_cast<uint64_t>(
        add * (static_cast<float>(Coppers(mc.cointype2)) / static_cast<float>(Coppers(mc.cointype1))));
    purse[mc.cointype1] -= static_cast<uint32_t>(take);
    purse[mc.cointype2] += static_cast<uint32_t>(add);
}

int64_t Value(const CoinPurse& p) {
    return p[0] + 10 * int64_t(p[1]) + 100 * int64_t(p[2]) + 1000 * int64_t(p[3]);
}

}  // namespace

TEST(BankChangeMoves, CopperUpToPlatinumCarryingEachStep) {
    CoinPurse purse = {25, 13, 9, 1};  // cp, sp, gp, pp
    const auto moves = BuildMakeChangeMoves(EQT::COINSLOT_BANK, purse);
    ASSERT_EQ(moves.size(), 3u);
    // 25 cp -> 2 sp (20 taken); 13 + 2 = 15 sp -> 1 gp (10); 9 + 1 = 10 gp -> 1 pp.
    const int32_t want[3][3] = {{0, 1, 20}, {1, 2, 10}, {2, 3, 10}};
    for (int i = 0; i < 3; ++i) {
        EXPECT_EQ(moves[i].from_slot, EQT::COINSLOT_BANK);
        EXPECT_EQ(moves[i].to_slot, EQT::COINSLOT_BANK);
        EXPECT_EQ(moves[i].cointype1, want[i][0]);
        EXPECT_EQ(moves[i].cointype2, want[i][1]);
        EXPECT_EQ(moves[i].amount, want[i][2]);
    }
    EXPECT_EQ(purse, (CoinPurse{5, 5, 0, 2}));
}

TEST(BankChangeMoves, NothingToChangeSendsNothing) {
    CoinPurse purse = {9, 9, 9, 123};
    EXPECT_TRUE(BuildMakeChangeMoves(EQT::COINSLOT_INVENTORY, purse).empty());
    EXPECT_EQ(purse, (CoinPurse{9, 9, 9, 123}));
}

TEST(BankChangeMoves, TheCarriedPurseUsesSlotOne) {
    CoinPurse purse = {100, 0, 0, 0};
    const auto moves = BuildMakeChangeMoves(EQT::COINSLOT_INVENTORY, purse);
    ASSERT_EQ(moves.size(), 2u);  // 100 cp -> 10 sp -> 1 gp
    for (const auto& m : moves) {
        EXPECT_EQ(m.from_slot, EQT::COINSLOT_INVENTORY);
        EXPECT_EQ(m.to_slot, EQT::COINSLOT_INVENTORY);
    }
    EXPECT_EQ(purse, (CoinPurse{0, 0, 1, 0}));
}

// The server answers OP_MoveCoin with silence, so what it computes from our
// steps must be exactly what the client projects, and no coin may vanish.
TEST(BankChangeMoves, TheServerArithmeticLandsOnTheProjectedPurse) {
    const CoinPurse cases[] = {
        {25, 13, 9, 1}, {0, 0, 0, 0}, {9, 9, 9, 9}, {1234567, 89, 4, 0},
        {10, 0, 0, 0}, {999, 999, 999, 999}, {0, 0, 10, 0},
    };
    for (const CoinPurse& start : cases) {
        CoinPurse projected = start;
        const auto moves = BuildMakeChangeMoves(EQT::COINSLOT_BANK, projected);
        CoinPurse server = start;
        for (const auto& m : moves) {
            EXPECT_EQ(m.amount % 10, 0) << "a step the server would truncate";
            EXPECT_EQ(m.cointype2, m.cointype1 + 1);
            ServerApply(m, server);
        }
        EXPECT_EQ(server, projected) << start[0] << "cp " << start[1] << "sp";
        EXPECT_EQ(Value(server), Value(start)) << "coin lost";
        for (int t = 0; t < 3; ++t) EXPECT_LT(projected[t], 10u) << "not the fewest coins";
    }
}

// --- the button ------------------------------------------------------------

namespace {
class CapturingBridge : public eqt::bridge::GameStateBridge {
public:
    void applyEvent(const eqt::state::GameEvent&) override {}
};
}  // namespace

TEST(BankChangeButton, AClickPushesBankMakeChangeIntent) {
    using namespace EQT::Graphics;
    UILayout layout;
    layout.computeLayout(800, 600);
    BankPanelState bank;
    bank.isOpen = true;
    MerchantPanelState merchant;
    CursorAttachmentState cursor;
    CapturingBridge bridge;
    StaticUIInput input;
    input.init(&bridge);

    const auto r = BankLayout::changeButtonRect(layout);
    const int x = r.getCenter().X, y = r.getCenter().Y;
    EXPECT_TRUE(hitTestBankChangeButton(layout, bank, x, y));

    // Hover only: highlighted, nothing sent.
    EXPECT_TRUE(input.processCommerceInput(x, y, false, layout, merchant, bank, cursor));
    EXPECT_TRUE(bank.changeHovered);
    EXPECT_TRUE(bridge.drainIntents().empty());

    EXPECT_TRUE(input.processCommerceInput(x, y, true, layout, merchant, bank, cursor));
    auto intents = bridge.drainIntents();
    ASSERT_EQ(intents.size(), 1u);
    EXPECT_NE(std::get_if<eqt::events::BankMakeChangeIntent>(&intents[0]), nullptr);

    // A closed bank has no button.
    bank.isOpen = false;
    EXPECT_FALSE(hitTestBankChangeButton(layout, bank, x, y));
}

// The arm drives both purses through the one builder and sends every step.
TEST(BankChangeCensus, TheArmSendsTheBuildersSteps) {
    std::ifstream in(std::string(WILLEQ_REPO_ROOT) + "/src/client/bridge/bridge_component_intents.cpp");
    std::stringstream ss;
    ss << in.rdbuf();
    const std::string src = ss.str();
    const size_t at = src.find("BridgeComponent::onBankMakeChangeIntent(");
    ASSERT_NE(at, std::string::npos);
    const std::string body = src.substr(at, 1800);
    EXPECT_NE(body.find("BuildMakeChangeMoves"), std::string::npos);
    EXPECT_NE(body.find("SendMoveCoin"), std::string::npos);
    EXPECT_NE(body.find("COINSLOT_BANK"), std::string::npos);
    EXPECT_NE(body.find("COINSLOT_INVENTORY"), std::string::npos);
    EXPECT_NE(body.find("IsBankWindowOpen"), std::string::npos);
}
