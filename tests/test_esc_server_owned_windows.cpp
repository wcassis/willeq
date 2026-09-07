// T443 (board B454) -- ESC on a SERVER-OWNED window sends its close intent
// instead of hiding the panel.
//
// T290's rule: the UI never closes itself. A window whose open/closed state
// the SERVER also holds must push the intent that ends the server-side
// session and stay up until the server's own Closed event comes back; an
// optimistic local close desyncs the two. Four windows are in that class and
// three already obeyed the rule on ESC before this unit:
//
//   * the loot window  -> CloseLootIntent          (T290)
//   * the bank window  -> CloseBankIntent          (T436)
//   * the merchant     -> VendorToggleIntent       (T436)
//   * the tradeskill container -> TradeskillCloseIntent  -- THIS UNIT.
//
// T415 Leg A deliberately left the container out and boarded it, because
// processKey() had no established pattern for a dismissal that goes through
// the bridge. It has one now (the loot arm), so the container joins it.
//
// The pre-fix red is the source census: pre-T443 `processKey()` does not take
// a TradeskillPanelState at all, so the behaviour cases cannot compile
// against that tree -- the census cases can, and the first of them fails
// there.
//
// The merchant and bank arms live one level up, in FrameComponent's escape
// handler (they are not centre popups and processKey() never sees them), so
// they are pinned here by census rather than by a call.
//
// No device, no server.

#include <gtest/gtest.h>

#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "client/bridge/game_state_bridge.h"
#include "client/events/renderer_intents.h"
#include "client/graphics/ui/static_panels.h"
#include "client/graphics/ui/static_ui_input.h"
#include "client/graphics/ui/tradeskill_panel.h"
#include "client/graphics/ui/ui_layout.h"
#include "client/state/event_bus.h"

using namespace EQT::Graphics;

namespace {

const std::string kRoot = WILLEQ_REPO_ROOT;

std::string slurp(const std::string& rel) {
    std::ifstream in(kRoot + "/" + rel);
    EXPECT_TRUE(in.good()) << "cannot open " << rel;
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

bool has(const std::string& hay, const std::string& needle) {
    return hay.find(needle) != std::string::npos;
}

// A comment that NAMES a symbol must not satisfy a census that asks whether
// code USES it (T442's rule -- a tombstone or an explanatory note is not a
// call site).
std::string codeOnly(const std::string& src) {
    std::string out;
    out.reserve(src.size());
    std::istringstream lines(src);
    std::string line;
    bool inBlock = false;
    while (std::getline(lines, line)) {
        if (inBlock) {
            const size_t end = line.find("*/");
            if (end == std::string::npos) continue;
            line = line.substr(end + 2);
            inBlock = false;
        }
        const size_t block = line.find("/*");
        if (block != std::string::npos) {
            const size_t end = line.find("*/", block + 2);
            if (end == std::string::npos) {
                line = line.substr(0, block);
                inBlock = true;
            } else {
                line = line.substr(0, block) + line.substr(end + 2);
            }
        }
        const size_t slashes = line.find("//");
        if (slashes != std::string::npos) line = line.substr(0, slashes);
        out += line;
        out += '\n';
    }
    return out;
}

// The ESC block of a body, so a mention somewhere else in the file cannot
// pass for a mention in the handler.
std::string escBlock(const std::string& src, const std::string& fromMarker) {
    const size_t at = src.find(fromMarker);
    EXPECT_NE(at, std::string::npos) << "marker not found: " << fromMarker;
    if (at == std::string::npos) return "";
    return codeOnly(src.substr(at, 6000));
}

class CapturingBridge : public eqt::bridge::GameStateBridge {
public:
    void applyEvent(const eqt::state::GameEvent&) override {}
};

class EscTest : public ::testing::Test {
protected:
    UILayout layout;
    InventoryPanelState inventory;
    SpellbookPopupState spellbook;
    SkillsPopupState skills;
    LootPanelState loot;
    TradeskillPanelState tradeskill;
    CapturingBridge bridge;
    StaticUIInput input;

    void SetUp() override {
        layout.computeLayout(800, 600);
        input.init(&bridge);
        tradeskill.isOpen = true;
        tradeskill.objectId = 4242;
    }

    std::vector<eqt::events::RendererIntent> drain() { return bridge.drainIntents(); }

    bool esc() {
        return input.processKey(irr::KEY_ESCAPE, /*pressed=*/true, false, false,
                                inventory, spellbook, skills, loot, tradeskill);
    }
};

}  // namespace

TEST_F(EscTest, EscapePushesTradeskillCloseIntentAndLeavesThePanelUp) {
    EXPECT_TRUE(esc());
    auto intents = drain();
    ASSERT_EQ(intents.size(), 1u);
    const auto* i = std::get_if<eqt::events::TradeskillCloseIntent>(&intents[0]);
    ASSERT_NE(i, nullptr);
    EXPECT_EQ(i->dropId, 4242u);
    // The whole point: only TradeskillContainerClosed closes the panel.
    EXPECT_TRUE(tradeskill.isOpen);
}

// The container is drawn AFTER the loot popup (renderer_frame_render.cpp), so
// it is the topmost centre popup and ESC must reach it first.
TEST_F(EscTest, TheContainerIsDismissedBeforeEveryOtherCentrePopup) {
    loot.isOpen = true;
    loot.corpseId = 77;
    spellbook.isOpen = true;
    skills.isOpen = true;
    inventory.activePopup = PopupType::Inventory;

    EXPECT_TRUE(esc());
    auto intents = drain();
    ASSERT_EQ(intents.size(), 1u);
    EXPECT_NE(std::get_if<eqt::events::TradeskillCloseIntent>(&intents[0]), nullptr);
    // ESC closes one layer at a time and nothing below it moved.
    EXPECT_TRUE(loot.isOpen);
    EXPECT_TRUE(spellbook.isOpen);
    EXPECT_TRUE(skills.isOpen);
    EXPECT_EQ(inventory.activePopup, PopupType::Inventory);
}

TEST_F(EscTest, WithTheContainerClosedTheLootArmStillRuns) {
    tradeskill.isOpen = false;
    loot.isOpen = true;
    loot.corpseId = 5;
    EXPECT_TRUE(esc());
    auto intents = drain();
    ASSERT_EQ(intents.size(), 1u);
    EXPECT_NE(std::get_if<eqt::events::CloseLootIntent>(&intents[0]), nullptr);
    EXPECT_TRUE(loot.isOpen);
}

TEST_F(EscTest, AKeyReleaseAndANonEscapeKeyBothDoNothing) {
    EXPECT_FALSE(input.processKey(irr::KEY_ESCAPE, /*pressed=*/false, false, false,
                                  inventory, spellbook, skills, loot, tradeskill));
    EXPECT_FALSE(input.processKey(irr::KEY_KEY_A, /*pressed=*/true, false, false,
                                  inventory, spellbook, skills, loot, tradeskill));
    EXPECT_TRUE(drain().empty());
    EXPECT_TRUE(tradeskill.isOpen);
}

// A closed container is not a target, even though its state is passed in.
TEST_F(EscTest, AClosedContainerIsNotDismissed) {
    tradeskill.isOpen = false;
    EXPECT_FALSE(esc());
    EXPECT_TRUE(drain().empty());
}

// Without a bridge the panel must still not close itself -- the intent is
// simply lost, which is the loot arm's existing behaviour.
TEST_F(EscTest, WithNoBridgeTheContainerStaysOpenRatherThanClosingLocally) {
    StaticUIInput unwired;
    EXPECT_TRUE(unwired.processKey(irr::KEY_ESCAPE, true, false, false,
                                   inventory, spellbook, skills, loot, tradeskill));
    EXPECT_TRUE(tradeskill.isOpen);
}

// ===========================================================================
// The source census -- these compile and run against the PRE-T443 tree.
// ===========================================================================

TEST(B454Census, EscapeOnTheTradeskillContainerSendsItsCloseIntent) {
    const std::string src = slurp("src/client/graphics/ui/static_ui_input.cpp");
    const std::string block = escBlock(src, "bool StaticUIInput::processKey(");
    EXPECT_TRUE(has(block, "TradeskillCloseIntent"))
        << "processKey()'s ESC handler still ignores the tradeskill container (B454)";
    // And it does not hide the panel behind the server's back.
    EXPECT_FALSE(has(block, "tradeskill.isOpen = false"));
}

TEST(B454Census, EscapeOnTheBankAndMerchantStillSendsTheirCloseIntents) {
    // T436's arms, pinned as invariants so a reorder of this handler cannot
    // quietly turn either back into a local hide.
    const std::string src = slurp("src/client/graphics/frame/renderer_frame_input.cpp");
    const std::string block = escBlock(src, "if (ctx_.eventReceiver->escapeKeyPressed())");
    EXPECT_TRUE(has(block, "CloseBankIntent"));
    EXPECT_TRUE(has(block, "VendorToggleIntent"));
    EXPECT_FALSE(has(block, "bankState_.isOpen = false"));
    EXPECT_FALSE(has(block, "merchantState_.isOpen = false"));
    // The container reaches processKey() with the other centre popups.
    EXPECT_TRUE(has(block, "tradeskillState_"));
}

// The four server-owned windows close on the SERVER's event, nowhere else.
TEST(B454Census, TheFourServerOwnedPanelsAreClosedOnlyByTheirAppliers) {
    EXPECT_TRUE(has(slurp("src/client/graphics/ui/tradeskill_panel.cpp"),
                    "applyTradeskillContainerClosed"));
    EXPECT_TRUE(has(slurp("src/client/graphics/ui/merchant_panel.cpp"),
                    "applyVendorWindowClosed"));
}
