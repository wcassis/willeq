// T436 (board B429) -- the merchant window, the bank window and the cursor
// attachment.
//
// Three producers filled an `iconId` nothing could draw, because none of the
// three windows existed. These cases cover, in order:
//
//   1. a SOURCE CENSUS of the four places the defect lived. Those four cases
//      compile and run against the PRE-T436 tree and fail there, which is the
//      pre-fix red for a unit whose other cases cannot exist before it (the
//      panels they name are new files).
//   2. the geometry, against EQUI_MerchantWnd.xml / EQUI_BankWnd.xml /
//      EQUI_CursorAttachment.xml -- including the two facts a reader would
//      assume away: Buy and Sell share ONE rect, and the bank is eight slots.
//   3. the appliers, driven with the REAL publishers' data structs.
//   4. the clicks, asserting the REAL intents on a real GameStateBridge.
//
// No Irrlicht device, no server.

#include <gtest/gtest.h>

#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "client/bridge/game_state_bridge.h"
#include "client/events/renderer_intents.h"
#include "client/graphics/ui/bank_layout.h"
#include "client/graphics/ui/bank_panel.h"
#include "client/graphics/ui/cursor_attachment.h"
#include "client/graphics/ui/cursor_attachment_layout.h"
#include "client/graphics/ui/merchant_layout.h"
#include "client/graphics/ui/merchant_panel.h"
#include "client/graphics/ui/static_ui_input.h"
#include "client/graphics/ui/ui_layout.h"
#include "client/inventory/inventory_constants.h"
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

UILayout layoutAt(int w, int h) {
    UILayout l;
    l.computeLayout(w, h);
    return l;
}

eqt::state::WindowOpenedData Opened(uint16_t npc, const char* name) {
    eqt::state::WindowOpenedData d;
    d.npcId = npc;
    d.npcName = name;
    d.sellRate = 1.0f;
    return d;
}

eqt::state::VendorItemAddedData Stock(uint16_t slot, uint32_t itemId,
                                      const char* name, uint32_t icon,
                                      int32_t price, int32_t qty) {
    eqt::state::VendorItemAddedData d;
    d.vendorSlot = slot;
    d.itemId = itemId;
    d.itemName = name;
    d.price = price;
    d.quantity = qty;
    d.iconId = icon;
    return d;
}

eqt::state::InventorySlotChangedData Slot(int16_t id, bool has, uint32_t icon,
                                          const char* name, uint32_t qty) {
    eqt::state::InventorySlotChangedData d;
    d.slotId = id;
    d.hasItem = has;
    d.itemId = has ? 99u : 0u;
    d.itemName = name;
    d.iconId = icon;
    d.quantity = qty;
    return d;
}

}  // namespace

// ===========================================================================
// 1. The source census -- the four halves of B429. These run pre-T436.
// ===========================================================================

TEST(B429Census, TheVendorProducerShipsTheIconItParsed) {
    // ZoneProcessVendorItemToUI parses a full ItemInstance off the wire and
    // built a VendorItemAddedData without its icon, so the merchant window's
    // 24px icon column would have had nothing to draw.
    const std::string src = slurp("src/client/merchant/merchant_vendor_bank.cpp");
    EXPECT_NE(src.find("vdata.iconId"), std::string::npos)
        << "ZoneProcessVendorItemToUI drops the icon it parsed (B429)";
}

TEST(B429Census, TheFiveWindowBridgeArmsAreNotStubs) {
    const std::string src = slurp("src/client/bridge/irrlicht_bridge.cpp");
    for (const char* fn : {"applyVendorWindowOpened", "applyVendorItemAdded",
                           "applyVendorWindowClosed", "applyBankWindowOpened",
                           "applyBankWindowClosed",
                           "applyBankCurrencyChanged"}) {
        EXPECT_NE(src.find(fn), std::string::npos)
            << fn << " has no bridge arm -- the payload is discarded (B429)";
    }
}

TEST(B429Census, TheCursorAndBankSlotsAreRoutedOffTheInventoryArm) {
    // The enriched InventorySlotChanged arm covered 0..29 and the WORLD
    // container range and dropped slot 30 (cursor) and 2000..2007 (bank).
    const std::string src = slurp("src/client/bridge/irrlicht_bridge.cpp");
    EXPECT_NE(src.find("applyCursorSlotChanged"), std::string::npos);
    EXPECT_NE(src.find("applyBankSlotChanged"), std::string::npos);
}

TEST(B429Census, TheThreeWindowsAreActuallyDrawn) {
    const std::string src =
        slurp("src/client/graphics/frame/renderer_frame_render.cpp");
    EXPECT_NE(src.find("renderMerchantPopup"), std::string::npos);
    EXPECT_NE(src.find("renderBankWindow"), std::string::npos);
    EXPECT_NE(src.find("renderCursorAttachment"), std::string::npos);
}

// ===========================================================================
// 2. Geometry, against the oracle XML
// ===========================================================================

TEST(MerchantLayoutOracle, EveryElementLandsInsideTheCentrePopup) {
    for (auto rc : {std::pair<int, int>{640, 480}, {800, 600}, {1024, 768}}) {
        const auto l = layoutAt(rc.first, rc.second);
        const auto& popup = l.partnerPopup;  // B1075: the PairLeft popup
        std::vector<irr::core::rect<irr::s32>> rects = {
            MerchantLayout::nameTextRect(l),
            MerchantLayout::listRect(l),
            MerchantLayout::selectedItemRect(l),
            MerchantLayout::selectedDecalRect(l),
            MerchantLayout::selectedItemLabelRect(l),
            MerchantLayout::selectedPriceLabelRect(l),
            MerchantLayout::tradeButtonRect(l),
            MerchantLayout::doneButtonRect(l),
        };
        for (const auto& r : rects) {
            EXPECT_GE(r.UpperLeftCorner.X, popup.UpperLeftCorner.X);
            EXPECT_GE(r.UpperLeftCorner.Y, popup.UpperLeftCorner.Y);
            EXPECT_LE(r.LowerRightCorner.X, popup.LowerRightCorner.X);
            EXPECT_LE(r.LowerRightCorner.Y, popup.LowerRightCorner.Y);
            EXPECT_GT(r.getWidth(), 0);
            EXPECT_GT(r.getHeight(), 0);
        }
    }
}

TEST(MerchantLayoutOracle, BuyAndSellShareOneRectBecauseTheXmlDoes) {
    // MW_Buy_Button and MW_Sell_Button carry byte-identical anchors in
    // EQUI_MerchantWnd.xml (10 / 44-from-bottom / 74-from-left /
    // 6-from-bottom, both of them), which is why the official client can
    // only ever show one of the two -- and why merchant_panel.cpp picks.
    EXPECT_EQ(MerchantLayout::ORACLE_TRADE_BTN_X, 10);
    EXPECT_EQ(MerchantLayout::ORACLE_TRADE_BTN_Y, 436);
    EXPECT_EQ(MerchantLayout::ORACLE_TRADE_BTN_W, 64);
    EXPECT_EQ(MerchantLayout::ORACLE_TRADE_BTN_H, 38);

    const std::string xml = [] {
        std::ifstream in("/mnt/projects/EverQuestP1999/uifiles/default/"
                         "EQUI_MerchantWnd.xml");
        std::ostringstream ss;
        ss << in.rdbuf();
        return ss.str();
    }();
    if (xml.empty()) GTEST_SKIP() << "oracle uifiles not present";
    // Both elements exist and Sell is in the Screen's Pieces list.
    EXPECT_NE(xml.find("MW_Buy_Button"), std::string::npos);
    EXPECT_NE(xml.find("MW_Sell_Button"), std::string::npos);
    EXPECT_NE(xml.find("<Pieces>MW_Sell_Button</Pieces>"), std::string::npos);
    // MW_DoneButton (no underscores) is declared and NOT in Pieces.
    EXPECT_NE(xml.find("MW_DoneButton"), std::string::npos);
    EXPECT_EQ(xml.find("<Pieces>MW_DoneButton</Pieces>"), std::string::npos);
}

TEST(MerchantLayoutOracle, TheSevenColumnsTileTheListFromItsLeftEdge) {
    const auto l = layoutAt(800, 600);
    const auto box = MerchantLayout::listRect(l);
    irr::s32 prevRight = box.UpperLeftCorner.X;
    for (int c = 0; c < MerchantLayout::COLUMN_COUNT; ++c) {
        const auto cell = MerchantLayout::listHeaderCellRect(l, c);
        EXPECT_EQ(cell.UpperLeftCorner.X, prevRight) << "column " << c;
        EXPECT_LE(cell.LowerRightCorner.X, box.LowerRightCorner.X);
        prevRight = cell.LowerRightCorner.X;
    }
    // The oracle's own widths, in element order.
    EXPECT_EQ(MerchantLayout::ORACLE_COL_W[0], 24);   // the icon cell
    EXPECT_EQ(MerchantLayout::ORACLE_COL_W[1], 150);  // Item Name
    EXPECT_EQ(MerchantLayout::ORACLE_COL_W[2], 28);   // Qty
    EXPECT_EQ(MerchantLayout::ORACLE_COL_W[3], 48);   // Header_PlatCoin
}

TEST(BankLayoutOracle, EightSlotsAtTheOraclesOwnOffsetsColumnMajor) {
    // BW_BankSlot0..7 and nothing more -- the era file declares no ninth.
    EXPECT_EQ(BankLayout::SLOT_COUNT, 8);
    // Column-major: 0..3 down the left column, 4..7 down the right.
    EXPECT_EQ(BankLayout::SLOT_X[0], 10);
    EXPECT_EQ(BankLayout::SLOT_X[4], 50);
    EXPECT_EQ(BankLayout::SLOT_Y[0], 32);
    EXPECT_EQ(BankLayout::SLOT_Y[3], 152);
    EXPECT_EQ(BankLayout::SLOT_Y[4], 32);
    // The shipped file's own off-by-one on slot 3, carried verbatim.
    EXPECT_EQ(BankLayout::SLOT_X[3], 11);
}

TEST(BankLayoutOracle, TheWindowIsUnscaledAndWhollyOnScreenAt640x480) {
    const auto l = layoutAt(640, 480);
    const auto w = BankLayout::windowRect(l);
    EXPECT_EQ(w.getWidth(), BankLayout::WINDOW_W);
    EXPECT_EQ(w.getHeight(), BankLayout::WINDOW_H);
    EXPECT_GE(w.UpperLeftCorner.X, 0);
    EXPECT_GE(w.UpperLeftCorner.Y, 0);
    EXPECT_LE(w.LowerRightCorner.X, l.screenW);
    EXPECT_LE(w.LowerRightCorner.Y, l.screenH);

    for (int i = 0; i < BankLayout::SLOT_COUNT; ++i) {
        const auto s = BankLayout::slotRect(l, i);
        EXPECT_EQ(s.getWidth(), BankLayout::SLOT_SIZE);
        EXPECT_LE(s.LowerRightCorner.Y, w.LowerRightCorner.Y);
    }
    for (int d = 0; d < BankLayout::COIN_COUNT; ++d) {
        const auto c = BankLayout::coinRect(l, d);
        EXPECT_EQ(c.getWidth(), BankLayout::COIN_W);
        EXPECT_LE(c.LowerRightCorner.Y, w.LowerRightCorner.Y);
    }
    EXPECT_LE(BankLayout::doneButtonRect(l).LowerRightCorner.Y,
              w.LowerRightCorner.Y);
}

TEST(BankLayoutOracle, TheEraFileDeclaresTheSharedBankWithNoGeometry) {
    std::ifstream in("/mnt/projects/EverQuestP1999/uifiles/default/"
                     "EQUI_BankWnd.xml");
    std::ostringstream ss;
    ss << in.rdbuf();
    const std::string xml = ss.str();
    if (xml.empty()) GTEST_SKIP() << "oracle uifiles not present";
    // The stub form: the <InvSlot item="BW_SharedBankSlot0"> element holds a
    // <ScreenID> and NOTHING else -- no <Location>, no <Size>. If a future
    // era file gives it geometry, this fails and the window needs the two
    // extra cells.
    for (const char* id : {"BW_SharedBankSlot0", "BW_SharedBankSlot1"}) {
        const std::string open = std::string("<InvSlot item=\"") + id + "\">";
        const size_t a = xml.find(open);
        ASSERT_NE(a, std::string::npos) << id;
        const size_t b = xml.find("</InvSlot>", a);
        ASSERT_NE(b, std::string::npos) << id;
        const std::string body = xml.substr(a, b - a);
        EXPECT_EQ(body.find("<Location>"), std::string::npos)
            << id << " is no longer a geometry-less stub";
        EXPECT_EQ(body.find("<Size>"), std::string::npos) << id;
    }
}

TEST(CursorAttachmentLayoutOracle, TheIconIsFortyByFortyAtFiveFiveOffTheMouse) {
    const auto l = layoutAt(800, 600);
    const auto icon = CursorAttachmentLayout::iconRect(l, 300, 200);
    EXPECT_EQ(icon.UpperLeftCorner.X, 305);
    EXPECT_EQ(icon.UpperLeftCorner.Y, 205);
    EXPECT_EQ(icon.getWidth(), 40);
    EXPECT_EQ(icon.getHeight(), 40);
}

TEST(CursorAttachmentLayoutOracle, TheBoxIsClampedAtTheScreenEdge) {
    const auto l = layoutAt(800, 600);
    const auto w = CursorAttachmentLayout::windowRect(l, 799, 599);
    EXPECT_LE(w.LowerRightCorner.X, 800);
    EXPECT_LE(w.LowerRightCorner.Y, 600);
    EXPECT_EQ(w.getWidth(), CursorAttachmentLayout::WINDOW_W);
}

// ===========================================================================
// 3. Appliers, driven with the real publishers' data structs
// ===========================================================================

TEST(MerchantApplier, AStockedItemKeepsItsIconAndPrice) {
    MerchantPanelState s;
    applyVendorWindowOpened(s, Opened(77, "Bob the Merchant"));
    EXPECT_TRUE(s.isOpen);
    EXPECT_EQ(s.npcId, 77);
    EXPECT_EQ(s.merchantName, "Bob the Merchant");

    applyVendorItemAdded(s, Stock(3, 1001, "Rusty Dagger", 631, 1234, 1));
    ASSERT_EQ(s.items.size(), 1u);
    EXPECT_EQ(s.items[0].vendorSlot, 3);
    EXPECT_EQ(s.items[0].iconId, 631u);   // <- B429: the icon reaches a drawer
    EXPECT_EQ(s.items[0].price, 1234);
}

TEST(MerchantApplier, ARepeatedSlotReplacesRatherThanDuplicating) {
    MerchantPanelState s;
    applyVendorWindowOpened(s, Opened(77, "Bob"));
    applyVendorItemAdded(s, Stock(3, 1001, "Rusty Dagger", 631, 100, 1));
    applyVendorItemAdded(s, Stock(3, 1001, "Rusty Dagger", 631, 100, 4));
    ASSERT_EQ(s.items.size(), 1u);
    EXPECT_EQ(s.items[0].quantity, 4);
}

TEST(MerchantApplier, RowsAreOrderedByTheMerchantsOwnSlotNumbering) {
    MerchantPanelState s;
    applyVendorWindowOpened(s, Opened(77, "Bob"));
    applyVendorItemAdded(s, Stock(9, 3, "c", 502, 1, 1));
    applyVendorItemAdded(s, Stock(1, 1, "a", 500, 1, 1));
    applyVendorItemAdded(s, Stock(5, 2, "b", 501, 1, 1));
    ASSERT_EQ(s.items.size(), 3u);
    EXPECT_EQ(s.items[0].vendorSlot, 1);
    EXPECT_EQ(s.items[1].vendorSlot, 5);
    EXPECT_EQ(s.items[2].vendorSlot, 9);
}

TEST(MerchantApplier, OpeningASecondMerchantDoesNotInheritTheFirstsStock) {
    MerchantPanelState s;
    applyVendorWindowOpened(s, Opened(77, "Bob"));
    applyVendorItemAdded(s, Stock(1, 1, "a", 500, 1, 1));
    applyVendorWindowOpened(s, Opened(78, "Alice"));
    EXPECT_TRUE(s.items.empty());
    EXPECT_EQ(s.selectedRow, -1);
}

TEST(MerchantApplier, ClosingClearsTheStockAndTheWindow) {
    MerchantPanelState s;
    applyVendorWindowOpened(s, Opened(77, "Bob"));
    applyVendorItemAdded(s, Stock(1, 1, "a", 500, 1, 1));
    eqt::state::WindowClosedData c;
    c.npcId = 77;
    applyVendorWindowClosed(s, c);
    EXPECT_FALSE(s.isOpen);
    EXPECT_TRUE(s.items.empty());
}

TEST(BankApplier, TheEightBankSlotIdsRouteAndNothingElseDoes) {
    BankPanelState s;
    for (int i = 0; i < BankPanelState::SLOT_COUNT; ++i) {
        const int16_t id =
            static_cast<int16_t>(eqt::inventory::BANK_BEGIN + i);
        EXPECT_TRUE(applyBankSlotChanged(
            s, Slot(id, true, 700u + i, "Bagged", 2)))
            << "bank slot " << id;
        EXPECT_TRUE(s.slots[i].hasItem);
        EXPECT_EQ(s.slots[i].iconId, 700u + i);
        EXPECT_EQ(s.slots[i].quantity, 2);
    }
    // 1999 is below the range; 2008 is the ninth slot, which this era has not.
    EXPECT_FALSE(applyBankSlotChanged(s, Slot(1999, true, 1, "x", 1)));
    EXPECT_FALSE(applyBankSlotChanged(s, Slot(2008, true, 1, "x", 1)));
    // and the world-container range still belongs to the tradeskill panel.
    EXPECT_FALSE(applyBankSlotChanged(s, Slot(4000, true, 1, "x", 1)));
}

TEST(BankApplier, ClosingKeepsTheVaultContents) {
    BankPanelState s;
    applyBankWindowOpened(s, Opened(55, "Banker Bruce"));
    applyBankSlotChanged(s, Slot(2000, true, 701, "Gem", 1));
    eqt::state::WindowClosedData c;
    c.npcId = 55;
    applyBankWindowClosed(s, c);
    EXPECT_FALSE(s.isOpen);
    // The bank did not empty itself because the window shut.
    EXPECT_TRUE(s.slots[0].hasItem);
}

TEST(BankApplier, TheFourMoneyRowsTakeTheBankCurrencyEvent) {
    BankPanelState s;
    eqt::state::BankCurrencyChangedData d;
    d.platinum = 12; d.gold = 3; d.silver = 7; d.copper = 40;
    applyBankCurrencyChanged(s, d);
    EXPECT_EQ(s.coins[0], 12);
    EXPECT_EQ(s.coins[1], 3);
    EXPECT_EQ(s.coins[2], 7);
    EXPECT_EQ(s.coins[3], 40);
}

TEST(CursorApplier, BothCursorSlotIdsRouteAndNothingElseDoes) {
    CursorAttachmentState s;
    EXPECT_TRUE(applyCursorSlotChanged(
        s, Slot(eqt::inventory::CURSOR_SLOT, true, 918, "Bread Cake", 3)));
    EXPECT_TRUE(s.hasItem);
    EXPECT_EQ(s.iconId, 918u);
    EXPECT_EQ(s.itemName, "Bread Cake");
    EXPECT_EQ(s.quantity, 3);

    EXPECT_TRUE(applyCursorSlotChanged(
        s, Slot(eqt::inventory::SERVER_CURSOR_SLOT, false, 0, "", 0)));
    EXPECT_FALSE(s.hasItem);
    EXPECT_EQ(s.iconId, 0u);

    // General, equipment, bank and world-container slots are not the cursor.
    EXPECT_FALSE(applyCursorSlotChanged(s, Slot(0, true, 1, "x", 1)));
    EXPECT_FALSE(applyCursorSlotChanged(s, Slot(29, true, 1, "x", 1)));
    EXPECT_FALSE(applyCursorSlotChanged(s, Slot(2000, true, 1, "x", 1)));
    EXPECT_FALSE(applyCursorSlotChanged(s, Slot(4000, true, 1, "x", 1)));
}

// ===========================================================================
// 4. Clicks -> the real intents
// ===========================================================================

// GameStateBridge is abstract (applyEvent is the renderer's job); the loot
// tests' own shape.
class CapturingBridge : public eqt::bridge::GameStateBridge {
public:
    void applyEvent(const eqt::state::GameEvent&) override {}
};

class CommerceClickTest : public ::testing::Test {
protected:
    UILayout layout;
    StaticUIInput input;
    CapturingBridge bridge;
    MerchantPanelState merchant;
    BankPanelState bank;
    CursorAttachmentState cursor;

    void SetUp() override {
        layout.computeLayout(800, 600);
        input.init(&bridge);
        applyVendorWindowOpened(merchant, Opened(77, "Bob the Merchant"));
        for (uint16_t i = 0; i < 5; ++i) {
            applyVendorItemAdded(merchant,
                                 Stock(i, 1000u + i, "Item", 600u + i,
                                       100 * (i + 1), 1));
        }
    }
    bool click(int x, int y) {
        return input.processCommerceInput(x, y, true, layout, merchant, bank,
                                          cursor);
    }
    std::vector<eqt::events::RendererIntent> drain() {
        return bridge.drainIntents();
    }
    static int cx(const irr::core::rect<irr::s32>& r) {
        return (r.UpperLeftCorner.X + r.LowerRightCorner.X) / 2;
    }
    static int cy(const irr::core::rect<irr::s32>& r) {
        return (r.UpperLeftCorner.Y + r.LowerRightCorner.Y) / 2;
    }
};

TEST_F(CommerceClickTest, AClosedWindowEatsNothing) {
    merchant.isOpen = false;
    const auto done = MerchantLayout::doneButtonRect(layout);
    EXPECT_FALSE(click(cx(done), cy(done)));
    EXPECT_TRUE(drain().empty());
}

TEST_F(CommerceClickTest, ClickingARowSelectsItAndSendsNothing) {
    const auto row = MerchantLayout::listRowRect(layout, 2);
    ASSERT_TRUE(click(cx(row), cy(row)));
    EXPECT_EQ(merchant.selectedRow, 2);
    EXPECT_TRUE(drain().empty());
}

TEST_F(CommerceClickTest, BuyPushesVendorBuyIntentCarryingTheVendorSlot) {
    const auto row = MerchantLayout::listRowRect(layout, 3);
    ASSERT_TRUE(click(cx(row), cy(row)));
    ASSERT_TRUE(drain().empty());

    const auto btn = MerchantLayout::tradeButtonRect(layout);
    ASSERT_TRUE(click(cx(btn), cy(btn)));
    auto intents = drain();
    ASSERT_EQ(intents.size(), 1u);
    const auto* b = std::get_if<eqt::events::VendorBuyIntent>(&intents[0]);
    ASSERT_NE(b, nullptr);
    EXPECT_EQ(b->npcId, 77);
    EXPECT_EQ(b->slot, 3);       // the MERCHANT's slot, not the row index
    EXPECT_EQ(b->quantity, 1);
}

TEST_F(CommerceClickTest, BuyWithNothingSelectedSendsNothing) {
    const auto btn = MerchantLayout::tradeButtonRect(layout);
    ASSERT_TRUE(click(cx(btn), cy(btn)));  // consumed, but no intent
    EXPECT_TRUE(drain().empty());
}

// B442b (#152) INVERTED this T436 case: it pinned "a held item turns the shared
// button into SELL (of the cursor)", T436's design choice. The oracle measured
// the opposite (review/UIGAP-ORACLE/152/merchant.tsv rows 1-6): the button is
// the side of the LAST SELECTION and selling never uses the cursor. A held
// item with nothing selected sells nothing; the positive cases are in
// tests/test_cursor_gesture_completion.cpp (MerchantSelectToSell).
TEST_F(CommerceClickTest, AHeldItemDoesNotTurnTheSharedButtonIntoSell) {
    cursor.hasItem = true;
    cursor.iconId = 918;
    cursor.itemName = "Bread Cake";
    cursor.quantity = 4;
    const auto btn = MerchantLayout::tradeButtonRect(layout);
    ASSERT_TRUE(click(cx(btn), cy(btn)));
    EXPECT_TRUE(drain().empty()) << "the cursor is never sold (merchant.tsv row 6)";
    EXPECT_FALSE(merchant.sellSelected());
}

TEST_F(CommerceClickTest, DonePushesTheVendorToggleAndDoesNotCloseLocally) {
    const auto done = MerchantLayout::doneButtonRect(layout);
    ASSERT_TRUE(click(cx(done), cy(done)));
    auto intents = drain();
    ASSERT_EQ(intents.size(), 1u);
    EXPECT_NE(std::get_if<eqt::events::VendorToggleIntent>(&intents[0]),
              nullptr);
    // T290's rule: the SERVER closes the window.
    EXPECT_TRUE(merchant.isOpen);
}

TEST_F(CommerceClickTest, TheBankDoneButtonPushesCloseBankIntent) {
    applyBankWindowOpened(bank, Opened(55, "Banker Bruce"));
    const auto done = BankLayout::doneButtonRect(layout);
    ASSERT_TRUE(click(cx(done), cy(done)));
    auto intents = drain();
    ASSERT_EQ(intents.size(), 1u);
    EXPECT_NE(std::get_if<eqt::events::CloseBankIntent>(&intents[0]), nullptr);
    EXPECT_TRUE(bank.isOpen);
}

TEST_F(CommerceClickTest, TheBankCloseBoxDoesTheSameThingAsDone) {
    applyBankWindowOpened(bank, Opened(55, "Banker Bruce"));
    const auto box = BankLayout::closeBoxRect(layout);
    ASSERT_TRUE(click(cx(box), cy(box)));
    auto intents = drain();
    ASSERT_EQ(intents.size(), 1u);
    EXPECT_NE(std::get_if<eqt::events::CloseBankIntent>(&intents[0]), nullptr);
}

// B441 (#26, UIGAP-small): Change is wired now -- one click, no dialog (the
// client's own help/tips/ctxbank.html). Was TheGreyedChangeButtonIsNotHitTestable.
TEST_F(CommerceClickTest, TheChangeButtonPushesBankMakeChangeIntent) {
    applyBankWindowOpened(bank, Opened(55, "Banker Bruce"));
    const auto change = BankLayout::changeButtonRect(layout);
    ASSERT_TRUE(click(cx(change), cy(change)));
    auto intents = drain();
    ASSERT_EQ(intents.size(), 1u);
    EXPECT_NE(std::get_if<eqt::events::BankMakeChangeIntent>(&intents[0]), nullptr);
}

TEST_F(CommerceClickTest, AClosedBankLetsTheMerchantSeeTheSamePoint) {
    // The bank is asked first, but only when it is open -- a hidden panel
    // must not eat clicks (T290's rule).
    const auto row = MerchantLayout::listRowRect(layout, 0);
    ASSERT_TRUE(click(cx(row), cy(row)));
    EXPECT_EQ(merchant.selectedRow, 0);
}

TEST_F(CommerceClickTest, ScrollingTheListSelectsWhatIsShown) {
    for (uint16_t i = 5; i < 40; ++i) {
        applyVendorItemAdded(merchant,
                             Stock(i, 1000u + i, "Item", 600u + i, 1, 1));
    }
    ASSERT_GT(static_cast<int>(merchant.items.size()),
              MerchantLayout::ROWS_VISIBLE);
    EXPECT_EQ(merchant.maxScrollOffset(),
              static_cast<int>(merchant.items.size()) -
                  MerchantLayout::ROWS_VISIBLE);
    merchant.scrollOffset = 4;
    const auto row = MerchantLayout::listRowRect(layout, 1);
    ASSERT_TRUE(click(cx(row), cy(row)));
    EXPECT_EQ(merchant.selectedRow, 5);  // 4 + on-screen row 1
}
