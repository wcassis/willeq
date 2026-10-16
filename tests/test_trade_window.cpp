// B896 (GitLab #212) -- the player trade window and the NPC give window.
//
// What this pins, in order:
//
//   1. GEOMETRY against the oracle files themselves: every <Location>/<Size>
//      (and the two buttons' anchor offsets) in
//      /mnt/projects/EverQuestP1999/uifiles/default/EQUI_TradeWnd.xml and
//      EQUI_GiveWnd.xml is parsed at test time and compared with TradeLayout /
//      GiveLayout, so a transcription slip is a red test, not a wrong pixel.
//   2. The APPLIERS, with the real publishers' data structs.
//   3. The CLICKS on a real GameStateBridge: an item held + an empty own slot
//      is B442's CursorGestureIntent with slot 3000+n (column-major); every
//      withdrawal shape pushes nothing; Trade / Give, Cancel and the close box
//      push TradeWindowIntent; a coin row with coin held pushes CoinFromCursor.
//   4. The GAME side through the real EverQuest: the auto-ack of an incoming
//      request, the held item dropping into 3000 when OUR request is acked
//      (player and NPC), the withdrawal refusal, the accept reset after our
//      own change, the coin move, the eqstr-1450 cancel line, and no
//      completion line.
//   5. The GREEN-NAME state machine, driven by the packet sequence of the
//      ledger, from BOTH clients' point of view, through the real
//      TradeManager -> MerchantComponent callbacks -> events -> appliers.
//
// Every behaviour row cites the official-client ledger
// /mnt/projects/fable/review/UIGAP-ORACLE/212/trade.tsv (two official
// Titanium clients on the local lane, 2026-10-05).
//
// No Irrlicht device, no server.

#include <gtest/gtest.h>

#define private public
#define protected public
#include "client/eq.h"
#include "client/graphics/ui/static_ui_input.h"
#undef private
#undef protected

#include "client/bridge/bridge_component.h"
#include "client/bridge/game_state_bridge.h"
#include "client/combat.h"
#include "client/eq_opcodes.h"
#include "client/events/renderer_intents.h"
#include "client/graphics/ui/cursor_attachment.h"
#include "client/graphics/ui/give_layout.h"
#include "client/graphics/ui/trade_layout.h"
#include "client/graphics/ui/trade_panel.h"
#include "client/graphics/ui/ui_layout.h"
#include "client/inventory/inventory_constants.h"
#include "client/inventory/inventory_manager.h"
#include "client/inventory/item_instance.h"
#include "client/state/event_bus.h"
#include "client/trade_manager.h"
#include "common/packet_structs.h"
#include "eqstr_test_db.h"
#include "zone_stream_test_utils.h"

#include <cstring>
#include <fstream>
#include <memory>
#include <regex>
#include <sstream>
#include <string>
#include <variant>
#include <vector>

using namespace EQT::Graphics;
namespace inv = eqt::inventory;

namespace {

const std::string kRoot = WILLEQ_REPO_ROOT;
const char* kTradeXml = "/mnt/projects/EverQuestP1999/uifiles/default/EQUI_TradeWnd.xml";
const char* kGiveXml = "/mnt/projects/EverQuestP1999/uifiles/default/EQUI_GiveWnd.xml";

std::string slurpAbs(const std::string& path) {
    std::ifstream in(path);
    if (!in.good()) return std::string();
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

struct XmlRect {
    bool found = false;
    int x = 0, y = 0, w = 0, h = 0;
};

// The element whose item="<name>" opens it: its first <Location> and <Size>.
XmlRect xmlRect(const std::string& xml, const std::string& name) {
    XmlRect r;
    const size_t a = xml.find("item = \"" + name + "\"") != std::string::npos
                         ? xml.find("item = \"" + name + "\"")
                         : xml.find("item =\"" + name + "\"");
    if (a == std::string::npos) return r;
    const size_t e = xml.find("</Size>", a);
    if (e == std::string::npos) return r;
    const std::string body = xml.substr(a, e + 7 - a);
    static const std::regex loc("<Location>\\s*<X>(-?\\d+)</X>\\s*<Y>(-?\\d+)</Y>\\s*</Location>");
    static const std::regex size("<Size>\\s*<CX>(\\d+)</CX>\\s*<CY>(\\d+)</CY>");
    std::smatch m;
    if (!std::regex_search(body, m, loc)) return r;
    r.x = std::stoi(m[1]);
    r.y = std::stoi(m[2]);
    if (!std::regex_search(body, m, size)) return r;
    r.w = std::stoi(m[1]);
    r.h = std::stoi(m[2]);
    r.found = true;
    return r;
}

// One <Tag>n</Tag> inside the element item="<name>".
int xmlInt(const std::string& xml, const std::string& name, const std::string& tag) {
    const size_t a = xml.find("item = \"" + name + "\"");
    if (a == std::string::npos) return -9999;
    const size_t end = xml.find("</Button>", a);
    const size_t t = xml.find("<" + tag + ">", a);
    if (t == std::string::npos || t > end) return -9999;
    return std::stoi(xml.substr(t + tag.size() + 2));
}

// A child rect relative to the window's client origin, from a screen rect.
XmlRect rel(const irr::core::rect<irr::s32>& r, const irr::core::vector2di& o, int titleH) {
    XmlRect x;
    x.found = true;
    x.x = r.UpperLeftCorner.X - o.X;
    x.y = r.UpperLeftCorner.Y - o.Y - titleH;
    x.w = r.getWidth();
    x.h = r.getHeight();
    return x;
}

void expectSame(const XmlRect& xml, const XmlRect& ours, const std::string& what) {
    ASSERT_TRUE(xml.found) << what << " not in the oracle file";
    EXPECT_EQ(ours.x, xml.x) << what;
    EXPECT_EQ(ours.y, xml.y) << what;
    EXPECT_EQ(ours.w, xml.w) << what;
    EXPECT_EQ(ours.h, xml.h) << what;
}

UILayout layoutAt(int w, int h) {
    UILayout l;
    l.computeLayout(w, h);
    return l;
}

irr::core::vector2di centre(const irr::core::rect<irr::s32>& r) {
    return irr::core::vector2di((r.UpperLeftCorner.X + r.LowerRightCorner.X) / 2,
                                (r.UpperLeftCorner.Y + r.LowerRightCorner.Y) / 2);
}

template <class T>
const T* findIntent(const std::vector<eqt::events::RendererIntent>& v) {
    for (const auto& i : v) {
        if (const T* p = std::get_if<T>(&i)) return p;
    }
    return nullptr;
}

template <class T>
int countIntents(const std::vector<eqt::events::RendererIntent>& v) {
    int n = 0;
    for (const auto& i : v) {
        if (std::holds_alternative<T>(i)) ++n;
    }
    return n;
}

eqt::state::TradeStartedData started(bool npc, const char* partner = "Testbuddy") {
    eqt::state::TradeStartedData d;
    d.partnerId = npc ? 332 : 329;
    d.partnerName = partner;
    d.isNpc = npc;
    return d;
}

eqt::state::InventorySlotChangedData slotEvent(int16_t id, bool has, uint32_t icon = 783) {
    eqt::state::InventorySlotChangedData d;
    d.slotId = id;
    d.hasItem = has;
    d.itemId = has ? 13015u : 0u;
    d.itemName = has ? "Low Quality Wolf Skin" : "";
    d.iconId = has ? icon : 0;
    d.quantity = has ? 1u : 0u;
    return d;
}

// What IrrlichtBridge does with the trade events and the trade slots (the
// irrlicht_bridge.cpp arms call exactly these appliers).
void applyEvents(TradePanelState& s, const std::vector<eqt::state::GameEvent>& events,
                 const std::string& ownName = "Testchar") {
    using T = eqt::state::GameEventType;
    for (const auto& e : events) {
        switch (e.type) {
            case T::TradeStarted:
                applyTradeStarted(s, std::get<eqt::state::TradeStartedData>(e.data), ownName);
                break;
            case T::TradeItemUpdated:
                applyTradeItemUpdated(s, std::get<eqt::state::TradeItemUpdatedData>(e.data));
                break;
            case T::TradeMoneyUpdated:
                applyTradeMoneyUpdated(s, std::get<eqt::state::TradeMoneyUpdatedData>(e.data));
                break;
            case T::TradeAcceptStateChanged:
                applyTradeAcceptStateChanged(
                    s, std::get<eqt::state::TradeAcceptStateChangedData>(e.data));
                break;
            case T::TradeCancelled:
            case T::TradeCompleted:
                applyTradeEnded(s);
                break;
            case T::InventorySlotChanged:
                applyTradeSlotChanged(s, std::get<eqt::state::InventorySlotChangedData>(e.data));
                break;
            default:
                break;
        }
    }
}

std::vector<std::string> systemLines(const std::vector<eqt::state::GameEvent>& events) {
    std::vector<std::string> out;
    for (const auto& e : events) {
        if (e.type == eqt::state::GameEventType::SystemMessage) {
            out.push_back(std::get<eqt::state::ChatMessageData>(e.data).message);
        }
    }
    return out;
}

}  // namespace

// ===========================================================================
// 1. Geometry, against the oracle files
// ===========================================================================

TEST(TradeLayoutOracle, TheTradeWindowIsTheScreensOwnSizeAndLocation) {
    const std::string xml = slurpAbs(kTradeXml);
    if (xml.empty()) GTEST_SKIP() << "oracle uifiles not present";
    const XmlRect s = xmlRect(xml, "TradeWnd");
    ASSERT_TRUE(s.found);
    EXPECT_EQ(s.x, TradeLayout::ORACLE_X);
    EXPECT_EQ(s.y, TradeLayout::ORACLE_Y);
    EXPECT_EQ(s.w, TradeLayout::WINDOW_W);
    EXPECT_EQ(s.h, TradeLayout::WINDOW_H);
    // Drawn unscaled: it fits the smallest supported screen.
    EXPECT_LE(TradeLayout::ORACLE_Y + TradeLayout::WINDOW_H, 480);
    // B1075 (#395): the oracle's (0,25) is the window's CONTENT history;
    // WHERE it sits is the layout table's PairLeft anchor -- right-aligned
    // against the inventory, top-aligned with it -- at the oracle's own size.
    for (auto rc : {std::pair<int, int>{800, 600}, {1280, 720}}) {
        const UILayout l = layoutAt(rc.first, rc.second);
        const auto w = TradeLayout::windowRect(l);
        EXPECT_EQ(w, l.placePairLeft(TradeLayout::WINDOW_W, TradeLayout::WINDOW_H));
        EXPECT_EQ(w.LowerRightCorner.X, l.partnerPopup.LowerRightCorner.X);
        EXPECT_EQ(w.UpperLeftCorner.Y, l.inventoryPopup.UpperLeftCorner.Y);
        EXPECT_LE(w.LowerRightCorner.X, l.inventoryPopup.UpperLeftCorner.X);
    }
}

TEST(TradeLayoutOracle, EverySlotNameAndCoinRowIsTranscribed) {
    const std::string xml = slurpAbs(kTradeXml);
    if (xml.empty()) GTEST_SKIP() << "oracle uifiles not present";
    const UILayout l = layoutAt(800, 600);
    const auto o = TradeLayout::windowOrigin(l);
    const int t = TradeLayout::TITLE_H;
    expectSame(xmlRect(xml, "TRDW_HisName"), rel(TradeLayout::hisNameRect(l), o, t), "TRDW_HisName");
    expectSame(xmlRect(xml, "TRDW_MyName"), rel(TradeLayout::myNameRect(l), o, t), "TRDW_MyName");
    for (int i = 0; i < 8; ++i) {
        const std::string mine = "TRDW_TradeSlot" + std::to_string(i);
        const std::string his = "TRDW_TradeSlot" + std::to_string(8 + i);
        expectSame(xmlRect(xml, mine), rel(TradeLayout::mySlotRect(l, i), o, t), mine);
        expectSame(xmlRect(xml, his), rel(TradeLayout::hisSlotRect(l, i), o, t), his);
        // EQType 3000+i is our slot i; 3008+i the partner's.
        EXPECT_NE(xml.find("<EQType>" + std::to_string(3000 + i) + "</EQType>"), std::string::npos);
    }
    for (int d = 0; d < 4; ++d) {
        const std::string mine = "TRDW_MyMoney" + std::to_string(d);
        const std::string his = "TRDW_HisMoney" + std::to_string(d);
        expectSame(xmlRect(xml, mine), rel(TradeLayout::myCoinRect(l, d), o, t), mine);
        expectSame(xmlRect(xml, his), rel(TradeLayout::hisCoinRect(l, d), o, t), his);
    }
}

// trade.tsv rows 1, 3, 6: our side is the RIGHT column, and the slots are
// column-major -- B's second item went to 3004, the top of the right half.
TEST(TradeLayoutOracle, OurSideIsTheRightColumnAndSlotsAreColumnMajor) {
    const UILayout l = layoutAt(800, 600);
    EXPECT_GT(TradeLayout::mySlotRect(l, 0).UpperLeftCorner.X,
              TradeLayout::hisSlotRect(l, 7).LowerRightCorner.X);
    // 3000..3003 go DOWN one column, 3004 starts the next at the top.
    for (int i = 1; i < 4; ++i) {
        EXPECT_EQ(TradeLayout::mySlotRect(l, i).UpperLeftCorner.X,
                  TradeLayout::mySlotRect(l, 0).UpperLeftCorner.X);
        EXPECT_GT(TradeLayout::mySlotRect(l, i).UpperLeftCorner.Y,
                  TradeLayout::mySlotRect(l, i - 1).UpperLeftCorner.Y);
    }
    EXPECT_EQ(TradeLayout::mySlotRect(l, 4).UpperLeftCorner.Y,
              TradeLayout::mySlotRect(l, 0).UpperLeftCorner.Y);
    EXPECT_GT(TradeLayout::mySlotRect(l, 4).UpperLeftCorner.X,
              TradeLayout::mySlotRect(l, 3).UpperLeftCorner.X);
    // The negative: a row-major reading would put 3001 beside 3000.
    EXPECT_NE(TradeLayout::mySlotRect(l, 1).UpperLeftCorner.Y,
              TradeLayout::mySlotRect(l, 0).UpperLeftCorner.Y);
}

TEST(TradeLayoutOracle, TheTwoButtonsFollowTheirAnchorOffsets) {
    const std::string xml = slurpAbs(kTradeXml);
    if (xml.empty()) GTEST_SKIP() << "oracle uifiles not present";
    const UILayout l = layoutAt(800, 600);
    const auto o = TradeLayout::windowOrigin(l);
    const int clientBottom = o.Y + TradeLayout::WINDOW_H;
    struct B { const char* item; irr::core::rect<irr::s32> r; } buttons[] = {
        {"TRDW_Trade_Button", TradeLayout::tradeButtonRect(l)},
        {"TRDW_Cancel_Button", TradeLayout::cancelButtonRect(l)}};
    for (const auto& b : buttons) {
        EXPECT_EQ(b.r.UpperLeftCorner.X - o.X, xmlInt(xml, b.item, "LeftAnchorOffset")) << b.item;
        EXPECT_EQ(b.r.LowerRightCorner.X - o.X, xmlInt(xml, b.item, "RightAnchorOffset")) << b.item;
        // TopAnchorToTop / BottomAnchorToTop false: both measured up from the bottom.
        EXPECT_EQ(clientBottom - b.r.UpperLeftCorner.Y, xmlInt(xml, b.item, "TopAnchorOffset")) << b.item;
        EXPECT_EQ(clientBottom - b.r.LowerRightCorner.Y, xmlInt(xml, b.item, "BottomAnchorOffset")) << b.item;
    }
}

TEST(GiveLayoutOracle, TheGiveWindowIsTranscribed) {
    const std::string xml = slurpAbs(kGiveXml);
    if (xml.empty()) GTEST_SKIP() << "oracle uifiles not present";
    const XmlRect s = xmlRect(xml, "GiveWnd");
    ASSERT_TRUE(s.found);
    EXPECT_EQ(s.x, GiveLayout::ORACLE_X);
    EXPECT_EQ(s.y, GiveLayout::ORACLE_Y);
    EXPECT_EQ(s.w, GiveLayout::WINDOW_W);
    EXPECT_EQ(s.h, GiveLayout::WINDOW_H);
    EXPECT_NE(xml.find("<Style_Sizable>false</Style_Sizable>"), std::string::npos);
    const UILayout l = layoutAt(800, 600);
    const auto o = GiveLayout::windowOrigin(l);
    const int t = GiveLayout::TITLE_H;
    expectSame(xmlRect(xml, "GVW_NPCName"), rel(GiveLayout::nameRect(l), o, t), "GVW_NPCName");
    for (int i = 0; i < 4; ++i) {
        const std::string n = "GVW_MyItemSlot" + std::to_string(i);
        expectSame(xmlRect(xml, n), rel(GiveLayout::slotRect(l, i), o, t), n);
        const std::string c = "GVW_MyMoney" + std::to_string(i);
        expectSame(xmlRect(xml, c), rel(GiveLayout::coinRect(l, i), o, t), c);
    }
    expectSame(xmlRect(xml, "GVW_Give_Button"), rel(GiveLayout::giveButtonRect(l), o, t), "Give");
    expectSame(xmlRect(xml, "GVW_Cancel_Button"), rel(GiveLayout::cancelButtonRect(l), o, t), "Cancel");
    // Row 9: FOUR item slots, one column; EQType 3000..3003, the trade slots.
    EXPECT_EQ(GiveLayout::SLOT_COUNT, 4);
    EXPECT_EQ(xml.find("GVW_MyItemSlot4"), std::string::npos);
}

// ===========================================================================
// 2. Appliers
// ===========================================================================

TEST(TradeApplier, StartedOpensTheTradeOrTheGiveWindow) {
    TradePanelState s;
    applyTradeStarted(s, started(false), "Testchar");
    EXPECT_TRUE(s.isOpen);
    EXPECT_FALSE(s.isNpc);
    EXPECT_EQ(s.partnerName, "Testbuddy");
    EXPECT_EQ(s.ownName, "Testchar");
    EXPECT_EQ(tradeOwnSlotCount(s), 8);
    // Row 9: an NPC is the GIVE window, 4 slots, the display name.
    applyTradeStarted(s, started(true, "Givedummy000"), "Testchar");
    EXPECT_TRUE(s.isNpc);
    EXPECT_EQ(s.partnerName, "Givedummy");
    EXPECT_EQ(tradeOwnSlotCount(s), 4);
}

TEST(TradeApplier, OwnSlotsComeFromTheTradeRangeAndNothingElse) {
    TradePanelState s;
    applyTradeStarted(s, started(false), "Testchar");
    EXPECT_TRUE(applyTradeSlotChanged(s, slotEvent(inv::TRADE_BEGIN + 4, true)));
    EXPECT_TRUE(s.own[4].hasItem);
    EXPECT_EQ(s.own[4].iconId, 783u);
    EXPECT_FALSE(applyTradeSlotChanged(s, slotEvent(inv::TRADE_END + 1, true)));
    EXPECT_FALSE(applyTradeSlotChanged(s, slotEvent(inv::GENERAL_BEGIN, true)));
    EXPECT_FALSE(applyTradeSlotChanged(s, slotEvent(inv::CURSOR_SLOT, true)));
    EXPECT_TRUE(applyTradeSlotChanged(s, slotEvent(inv::TRADE_BEGIN + 4, false)));
    EXPECT_FALSE(s.own[4].hasItem);
}

TEST(TradeApplier, PartnerItemsMoneyAndTheEnd) {
    TradePanelState s;
    applyTradeStarted(s, started(false), "Testchar");
    eqt::state::TradeItemUpdatedData item;
    item.who = 1;
    item.slot = 0;
    item.itemId = 15288;
    item.itemName = "Spell: Minor Shielding";
    item.iconId = 504;
    item.quantity = 1;
    applyTradeItemUpdated(s, item);
    EXPECT_TRUE(s.partner[0].hasItem);
    EXPECT_EQ(s.partner[0].iconId, 504u);
    EXPECT_FALSE(s.own[0].hasItem);
    // Row 2: 1 pp from the partner shows in the partner column.
    eqt::state::TradeMoneyUpdatedData money{1, 1, 0, 0, 0};
    applyTradeMoneyUpdated(s, money);
    EXPECT_EQ(s.partnerCoins[0], 1u);
    EXPECT_EQ(s.ownCoins[0], 0u);
    // Rows 7 and 8: the window closes and forgets everything.
    applyTradeEnded(s);
    EXPECT_FALSE(s.isOpen);
    EXPECT_FALSE(s.partner[0].hasItem);
    EXPECT_EQ(s.partnerCoins[0], 0u);
}

TEST(TradeApplier, NamesAreRedUntilTheirSideAcceptsAndTheGiveNameIsWhite) {
    TradePanelState s;
    applyTradeStarted(s, started(false), "Testchar");
    // Row 1: both names drawn RED.
    EXPECT_EQ(tradeOwnNameColor(s), TradeNameColor::Red);
    EXPECT_EQ(tradePartnerNameColor(s), TradeNameColor::Red);
    applyTradeAcceptStateChanged(s, {true, false});
    EXPECT_EQ(tradeOwnNameColor(s), TradeNameColor::Green);
    EXPECT_EQ(tradePartnerNameColor(s), TradeNameColor::Red);
    applyTradeAcceptStateChanged(s, {false, true});
    EXPECT_EQ(tradeOwnNameColor(s), TradeNameColor::Red);
    EXPECT_EQ(tradePartnerNameColor(s), TradeNameColor::Green);
    // Row 9 (A.R23): the give window's NPC name is the XML's white.
    applyTradeStarted(s, started(true, "Givedummy000"), "Testchar");
    EXPECT_EQ(tradePartnerNameColor(s), TradeNameColor::White);
}

// ===========================================================================
// 3. Clicks
// ===========================================================================

class TradeClickTest : public ::testing::Test {
protected:
    void SetUp() override {
        layout = layoutAt(800, 600);
        input.init(&bridge);
        applyTradeStarted(state, started(false), "Testchar");
    }
    void hold(bool item) {
        cursor.hasItem = item;
        input.setFrameClickState(false, {}, &cursor);
    }
    std::vector<eqt::events::RendererIntent> click(const irr::core::rect<irr::s32>& r) {
        const auto c = centre(r);
        consumed = input.processTradeInput(c.X, c.Y, true, layout, state);
        return bridge.drainIntents();
    }
    UILayout layout;
    zonetest::TestBridge bridge;
    StaticUIInput input;
    TradePanelState state;
    CursorAttachmentState cursor;
    bool consumed = false;
};

// Rows 3 and 6: OP_MoveItem 30 -> 3000 (top-left of our column) and 30 ->
// 3004 (top of the right half) -- B442's gesture with the wire slot.
TEST_F(TradeClickTest, AHeldItemGoesIntoTheClickedOwnSlotThroughTheGesture) {
    hold(true);
    auto intents = click(TradeLayout::mySlotRect(layout, 0));
    EXPECT_TRUE(consumed);
    const auto* g = findIntent<eqt::events::CursorGestureIntent>(intents);
    ASSERT_NE(g, nullptr) << "a trade slot click did not reach CursorGesture";
    EXPECT_EQ(g->op, eqt::events::CursorGestureOp::Click);
    EXPECT_EQ(g->slot, 3000);
    intents = click(TradeLayout::mySlotRect(layout, 4));
    g = findIntent<eqt::events::CursorGestureIntent>(intents);
    ASSERT_NE(g, nullptr);
    EXPECT_EQ(g->slot, 3004);
    // Nothing moved here: the game thread answers with InventorySlotChanged.
    EXPECT_FALSE(state.own[0].hasItem);
}

// Row 5: an offered item clicked with an empty cursor -- nothing, no packet,
// twice. And the partner's column is read-only.
TEST_F(TradeClickTest, NothingHeldOrAnOfferedItemOrThePartnersColumnPushesNothing) {
    hold(false);
    EXPECT_TRUE(click(TradeLayout::mySlotRect(layout, 2)).empty());   // empty slot, empty cursor
    applyTradeSlotChanged(state, slotEvent(inv::TRADE_BEGIN, true));
    EXPECT_TRUE(click(TradeLayout::mySlotRect(layout, 0)).empty());   // row 5
    EXPECT_TRUE(click(TradeLayout::mySlotRect(layout, 0)).empty());   // row 5, retry
    hold(true);
    EXPECT_TRUE(click(TradeLayout::mySlotRect(layout, 0)).empty());   // a swap is a withdrawal
    state.partner[1].hasItem = true;
    EXPECT_TRUE(click(TradeLayout::hisSlotRect(layout, 1)).empty());
    EXPECT_TRUE(click(TradeLayout::hisCoinRect(layout, 0)).empty());
    EXPECT_TRUE(consumed);  // still the window's
}

TEST_F(TradeClickTest, TradeCancelAndTheCloseBoxPushTheTradeIntent) {
    auto intents = click(TradeLayout::tradeButtonRect(layout));
    const auto* a = findIntent<eqt::events::TradeWindowIntent>(intents);
    ASSERT_NE(a, nullptr);
    EXPECT_EQ(a->op, eqt::events::TradeWindowOp::Accept);   // row 4
    intents = click(TradeLayout::cancelButtonRect(layout));
    a = findIntent<eqt::events::TradeWindowIntent>(intents);
    ASSERT_NE(a, nullptr);
    EXPECT_EQ(a->op, eqt::events::TradeWindowOp::Cancel);   // row 8
    intents = click(TradeLayout::closeBoxRect(layout));
    a = findIntent<eqt::events::TradeWindowIntent>(intents);
    ASSERT_NE(a, nullptr);
    EXPECT_EQ(a->op, eqt::events::TradeWindowOp::Cancel);
    // T290's rule: no button closes the window locally.
    EXPECT_TRUE(state.isOpen);
}

// Row 2: coin held from the purse, then a click on our own coin field.
TEST_F(TradeClickTest, AnOwnCoinRowTakesHeldCoinAndNothingElse) {
    hold(false);
    EXPECT_TRUE(click(TradeLayout::myCoinRect(layout, 0)).empty());   // nothing held
    // B442b: held coin is the cursor attachment's (CursorCoinChanged).
    cursor.coinType = 3;
    cursor.coinAmount = 1;
    auto intents = click(TradeLayout::myCoinRect(layout, 0));
    const auto* c = findIntent<eqt::events::TradeWindowIntent>(intents);
    ASSERT_NE(c, nullptr);
    EXPECT_EQ(c->op, eqt::events::TradeWindowOp::CoinFromCursor);
    EXPECT_TRUE(state.isOpen);   // nothing changes here; the game answers
}

// B442b + B896: a motionless world click with an ITEM held is a trade on a
// player (row 1), a give on an NPC (row 9), and only otherwise -- the ground,
// a corpse -- B442b's drop prompt (151 rows 31-33). Trade/give win.
TEST(TradeWorldClick, APlayerIsATradeAnNpcIsAGiveAndOnlyTheRestIsADrop) {
    EXPECT_EQ(classifyHeldItemWorldClick(329, /*isNpc=*/false, false), HeldItemWorldClick::Trade);
    EXPECT_EQ(classifyHeldItemWorldClick(332, /*isNpc=*/true, false), HeldItemWorldClick::Give);
    EXPECT_EQ(classifyHeldItemWorldClick(0, false, false), HeldItemWorldClick::Drop);
    EXPECT_EQ(classifyHeldItemWorldClick(0, true, false), HeldItemWorldClick::Drop);
    EXPECT_EQ(classifyHeldItemWorldClick(400, true, /*isCorpse=*/true), HeldItemWorldClick::Drop);
    EXPECT_EQ(classifyHeldItemWorldClick(401, false, /*isCorpse=*/true), HeldItemWorldClick::Drop);
}

// The drop prompt is ARMED only when the click is not on a player or NPC: the
// production seam reads the classification (renderer_frame_cursor.cpp), a
// source pin because the arming lives on the render thread with a device.
TEST(TradeWorldClick, TheDropPromptConsultsTheTradeGiveClassificationFirst) {
    const std::string src = slurpAbs(kRoot + "/src/client/graphics/frame/renderer_frame_cursor.cpp");
    ASSERT_FALSE(src.empty());
    const size_t cls = src.find("classifyHeldItemWorldClick(");
    const size_t arm = src.find("worldDropArmed_ = !uiTook");
    ASSERT_NE(cls, std::string::npos) << "the drop arming no longer asks the trade/give rule";
    ASSERT_NE(arm, std::string::npos);
    EXPECT_LT(cls, arm);
    EXPECT_NE(src.find("!onEntity", arm), std::string::npos);
    // And the world click DOES reach the game's trade request: a held item +
    // TargetIntent on a player or NPC is requestTrade (bridge_component_intents.cpp).
    const std::string intents = slurpAbs(kRoot + "/src/client/bridge/bridge_component_intents.cpp");
    EXPECT_NE(intents.find("ctx_.trade_manager->requestTrade(i.spawnId"), std::string::npos);
}

TEST_F(TradeClickTest, AClosedWindowEatsNothing) {
    applyTradeEnded(state);
    hold(true);
    auto intents = click(TradeLayout::mySlotRect(layout, 0));
    EXPECT_FALSE(consumed);
    EXPECT_TRUE(intents.empty());
}

// Rows 9-10: the give window -- four slots, Give is the accept, no partner column.
TEST_F(TradeClickTest, TheGiveWindowHasFourSlotsAndGiveIsTheAccept) {
    applyTradeStarted(state, started(true, "Givedummy000"), "Testchar");
    hold(true);
    auto intents = click(GiveLayout::slotRect(layout, 3));
    const auto* g = findIntent<eqt::events::CursorGestureIntent>(intents);
    ASSERT_NE(g, nullptr);
    EXPECT_EQ(g->slot, 3003);
    intents = click(GiveLayout::giveButtonRect(layout));
    const auto* a = findIntent<eqt::events::TradeWindowIntent>(intents);
    ASSERT_NE(a, nullptr);
    EXPECT_EQ(a->op, eqt::events::TradeWindowOp::Accept);
    intents = click(GiveLayout::cancelButtonRect(layout));
    a = findIntent<eqt::events::TradeWindowIntent>(intents);
    ASSERT_NE(a, nullptr);
    EXPECT_EQ(a->op, eqt::events::TradeWindowOp::Cancel);
    // Where the trade window's partner column would be is not the give window.
    const auto his = centre(TradeLayout::hisSlotRect(layout, 4));
    EXPECT_NE(hitTestTradeWindow(layout, state, his.X, his.Y).part, TradePart::PartnerSlot);
}

// ===========================================================================
// 4 + 5. The game side and the green-name state machine
// ===========================================================================

namespace {

class TradeHarness {
public:
    TradeHarness() : eq(zonetest::MakeEq()) {
        eq.setBridge(&bridge);
        auto* t = eq.GetTradeManager();
        t->setMySpawnId(261);
        t->setSendTradeRequest([this](const EQT::TradeRequest_Struct&) { ++requests; });
        t->setSendTradeRequestAck([this](const EQT::TradeRequestAck_Struct& a) {
            ++acks;
            lastAckTarget = a.target_spawn_id;
        });
        t->setSendTradeAcceptClick([this](const EQT::TradeAcceptClick_Struct&) { ++acceptClicks; });
        t->setSendCancelTrade([this](const EQT::CancelTrade_Struct&) { ++cancels; });
        t->setSendMoveCoin([this](const EQT::MoveCoin_Struct& m) { coinMoves.push_back(m); });
    }
    ~TradeHarness() { eq.setBridge(nullptr); }

    inv::InventoryManager& inventory() { return *eq.GetInventoryManager(); }
    TradeManager& trade() { return *eq.GetTradeManager(); }

    // A general-slot item picked onto the cursor (the gesture's own call).
    void holdItem(uint32_t id, const char* name) {
        inv::ItemInstance item;
        item.itemId = id;
        item.name = name;
        item.icon = 783;
        inventory().setItem(inv::GENERAL_BEGIN + 3, item);
        ASSERT_TRUE(inventory().pickupItem(inv::GENERAL_BEGIN + 3));
    }

    void replayRequestFrom(uint32_t from) {
        EQT::TradeRequest_Struct r{261, from};
        std::vector<uint8_t> payload(sizeof(r));
        std::memcpy(payload.data(), &r, sizeof(r));
        zonetest::ReplayZone(eq, HC_OP_TradeRequest, payload);
    }
    void replayAckFrom(uint32_t from) {
        EQT::TradeRequestAck_Struct a{261, from};
        std::vector<uint8_t> payload(sizeof(a));
        std::memcpy(payload.data(), &a, sizeof(a));
        zonetest::ReplayZone(eq, HC_OP_TradeRequestAck, payload);
    }
    void partnerAccepts(uint32_t from) {
        EQT::TradeAcceptClick_Struct c{};
        c.spawn_id = from;
        c.accepted = 1;
        trade().handleTradeAcceptClick(c);
    }
    void partnerOffers(int slot, uint32_t id, const char* name) {
        auto item = std::make_unique<inv::ItemInstance>();
        item->itemId = id;
        item->name = name;
        item->icon = 504;
        trade().handlePartnerItemPacket(slot, std::move(item));
    }
    void push(const eqt::events::RendererIntent& i) {
        bridge.pushIntent(i);
        eq.Bridge().ProcessBridgeIntents();
    }
    // Feed what the game thread published into a window, like IrrlichtBridge.
    std::vector<eqt::state::GameEvent> pump(TradePanelState& window) {
        auto ev = bridge.drainEvents();
        applyEvents(window, ev);
        return ev;
    }

    EverQuest eq;
    zonetest::TestBridge bridge;
    int requests = 0, acks = 0, acceptClicks = 0, cancels = 0;
    uint32_t lastAckTarget = 0;
    std::vector<EQT::MoveCoin_Struct> coinMoves;
};

}  // namespace

// Row 1: B's client acks automatically, no prompt; the window opens.
TEST(TradeGame, AnIncomingRequestIsAcknowledgedAtOnceAndTheWindowOpens) {
    TradeHarness b;
    TradePanelState window;
    b.replayRequestFrom(261);
    EXPECT_EQ(b.acks, 1);
    EXPECT_EQ(b.lastAckTarget, 261u);
    EXPECT_TRUE(b.trade().isTrading());
    b.pump(window);
    EXPECT_TRUE(window.isOpen);
    EXPECT_FALSE(window.isNpc);
}

// Rows 1 and 9: OUR request acked -> the held item drops into 3000 by itself,
// for a player and for an NPC (the give window).
TEST(TradeGame, OurAcknowledgedRequestDropsTheHeldItemIntoSlot3000) {
    for (bool npc : {false, true}) {
        TradeHarness a;
        TradePanelState window;
        a.holdItem(13015, "Low Quality Wolf Skin");
        a.trade().requestTrade(npc ? 332 : 329, npc ? "Givedummy000" : "Testbuddy", npc);
        EXPECT_EQ(a.requests, 1);
        a.bridge.drainIntents();
        a.replayAckFrom(npc ? 332 : 329);
        ASSERT_TRUE(a.trade().isTrading());
        ASSERT_NE(a.inventory().getItem(inv::TRADE_BEGIN), nullptr) << "npc=" << npc;
        EXPECT_EQ(a.inventory().getItem(inv::TRADE_BEGIN)->itemId, 13015u);
        EXPECT_EQ(a.inventory().getCursorItem(), nullptr);
        bool move30to3000 = false;
        for (const auto& i : a.bridge.drainIntents()) {
            if (const auto* m = std::get_if<eqt::events::MoveItemIntent>(&i)) {
                if (m->fromSlot == inv::CURSOR_SLOT && m->toSlot == inv::TRADE_BEGIN) move30to3000 = true;
            }
        }
        EXPECT_TRUE(move30to3000) << "no OP_MoveItem 30 -> 3000, npc=" << npc;
        a.pump(window);
        EXPECT_TRUE(window.isOpen);
        EXPECT_EQ(window.isNpc, npc);
        EXPECT_TRUE(window.own[0].hasItem);
    }
}

// A request WE answered (not ours) does not touch the cursor.
TEST(TradeGame, AnAcceptedIncomingRequestLeavesTheCursorAlone) {
    TradeHarness b;
    b.holdItem(13015, "Low Quality Wolf Skin");
    b.replayRequestFrom(261);
    ASSERT_TRUE(b.trade().isTrading());
    EXPECT_NE(b.inventory().getCursorItem(), nullptr);
    EXPECT_EQ(b.inventory().getItem(inv::TRADE_BEGIN), nullptr);
}

// Row 5: an offered item cannot be taken back -- no packet, no move.
TEST(TradeGame, AnOfferedItemCannotBeTakenBackOrSwappedOut) {
    TradeHarness a;
    a.replayRequestFrom(329);
    a.holdItem(13015, "Low Quality Wolf Skin");
    eqt::events::CursorGestureIntent place;
    place.slot = inv::TRADE_BEGIN;
    a.push(place);
    ASSERT_NE(a.inventory().getItem(inv::TRADE_BEGIN), nullptr);
    a.bridge.drainIntents();

    eqt::events::CursorGestureIntent take;   // empty cursor, click the offered item
    take.slot = inv::TRADE_BEGIN;
    a.push(take);
    EXPECT_EQ(countIntents<eqt::events::MoveItemIntent>(a.bridge.drainIntents()), 0);
    EXPECT_NE(a.inventory().getItem(inv::TRADE_BEGIN), nullptr);
    EXPECT_EQ(a.inventory().getCursorItem(), nullptr);

    eqt::events::CursorGestureIntent pick;   // /pickup 3000
    pick.op = eqt::events::CursorGestureOp::PickUp;
    pick.slot = inv::TRADE_BEGIN;
    pick.echo = true;
    a.push(pick);
    EXPECT_EQ(countIntents<eqt::events::MoveItemIntent>(a.bridge.drainIntents()), 0);
    const auto lines = systemLines(a.bridge.drainEvents());
    ASSERT_FALSE(lines.empty());
    EXPECT_NE(lines.back().find("cannot be taken back"), std::string::npos);

    a.holdItem(13016, "Bottle");               // a swap onto it is a withdrawal too
    a.bridge.drainIntents();
    a.push(take);
    EXPECT_EQ(countIntents<eqt::events::MoveItemIntent>(a.bridge.drainIntents()), 0);
    EXPECT_EQ(a.inventory().getItem(inv::TRADE_BEGIN)->itemId, 13015u);
    EXPECT_EQ(a.inventory().getCursorItem()->itemId, 13016u);
}

// Row 2: one OP_MoveCoin cursor(0) -> trade(3), cointype 3, amount 1; our
// column shows it.
TEST(TradeGame, HeldCoinGoesIntoTheTradeWithOneMoveCoin) {
    TradeHarness a;
    TradePanelState window;
    a.replayRequestFrom(329);
    a.inventory().pickupMoney(inv::InventoryManager::CursorMoneyType::Platinum, 1);
    a.coinMoves.clear();
    a.push(eqt::events::TradeWindowIntent{eqt::events::TradeWindowOp::CoinFromCursor});
    ASSERT_EQ(a.coinMoves.size(), 1u);
    EXPECT_EQ(a.coinMoves[0].from_slot, EQT::COINSLOT_CURSOR);
    EXPECT_EQ(a.coinMoves[0].to_slot, EQT::COINSLOT_TRADE);
    EXPECT_EQ(a.coinMoves[0].to_slot, 3);
    EXPECT_EQ(a.coinMoves[0].cointype1, EQT::COINTYPE_PP);
    EXPECT_EQ(a.coinMoves[0].amount, 1);
    EXPECT_FALSE(a.inventory().hasCursorMoney());
    EXPECT_EQ(a.trade().getOwnMoney().platinum, 1u);
    a.pump(window);
    EXPECT_EQ(window.ownCoins[0], 1u);
    // Nothing held: nothing sent.
    a.push(eqt::events::TradeWindowIntent{eqt::events::TradeWindowOp::CoinFromCursor});
    EXPECT_EQ(a.coinMoves.size(), 1u);
}

// ---------------------------------------------------------------------------
// UIGAP-residue: the world-click refusals and the coin-held open, measured by
// UIGAP-live2 (trade.tsv rows 12-14, run2.*).
// ---------------------------------------------------------------------------

namespace {

// A player (npc_type 0) or an NPC (1) spawned at our feet, and the shipped
// eqstr table loaded into the client, so the refusal lines are the real rows.
void spawnAt(TradeHarness& h, uint32_t spawnId, const char* name, uint8_t npcType,
             uint8_t classId) {
    zonetest::ReplayZone(h.eq, HC_OP_NewSpawn,
                         zonetest::NewSpawnPayloadAt(spawnId, name, 0, 0, 0, /*level=*/5,
                                                     npcType, classId));
    h.bridge.drainEvents();
}

void loadShippedStrings(TradeHarness& h) {
    ASSERT_TRUE(eqt_test::ShippedStrings().isLoaded()) << "data/willeq.db";
    ASSERT_TRUE(h.eq.Strings().LoadStringFiles(""));
}

void holdNoDrop(TradeHarness& h, bool noDrop) {
    inv::ItemInstance fang;
    fang.itemId = 13915;
    fang.name = "Gnoll Fang";
    fang.icon = 783;
    fang.noDrop = noDrop;
    h.inventory().setItem(inv::GENERAL_BEGIN + 4, fang);
    ASSERT_TRUE(h.inventory().pickupItem(inv::GENERAL_BEGIN + 4));
    h.bridge.drainIntents();
}

}  // namespace

// Row 12 (B1001, #321): a NO-DROP item held + a click on a player -> no trade
// request, the item stays on the cursor, eqstr 12921, and the click targets.
TEST(TradeRefusal, ANoDropItemHeldOnAPlayerIsRefusedClientSide) {
    TradeHarness a;
    loadShippedStrings(a);
    spawnAt(a, 329, "Testbuddy", /*npcType=*/0, /*classId=*/1);
    holdNoDrop(a, /*noDrop=*/true);
    a.push(eqt::events::TargetIntent{329});
    EXPECT_EQ(a.requests, 0) << "row 12: no OP_TradeRequest";
    EXPECT_FALSE(a.trade().isTrading());
    ASSERT_NE(a.inventory().getCursorItem(), nullptr) << "row 12: the item stays on the cursor";
    EXPECT_EQ(a.inventory().getCursorItem()->itemId, 13915u);
    const auto lines = systemLines(a.bridge.drainEvents());
    ASSERT_EQ(lines.size(), 1u);
    EXPECT_EQ(lines[0], "That item cannot be dropped, traded, or sold.") << "row 12, eqstr 12921";
    EXPECT_EQ(a.eq.GetCombatManager()->GetTargetId(), 329) << "row 12: OP_TargetMouse only";

    // The negative: a TRADABLE item on the same player opens the trade (row 1).
    TradeHarness b;
    loadShippedStrings(b);
    spawnAt(b, 329, "Testbuddy", 0, 1);
    holdNoDrop(b, /*noDrop=*/false);
    b.push(eqt::events::TargetIntent{329});
    EXPECT_EQ(b.requests, 1);
}

// A no-drop item to an NPC is a normal give (UIGAP-live2's give_npc passes
// with a Gnoll Fang): the no-drop refusal is for players only.
TEST(TradeRefusal, ANoDropItemHeldOnAQuestNpcStillOpensTheGiveWindow) {
    TradeHarness a;
    loadShippedStrings(a);
    spawnAt(a, 332, "Captain_Tillin000", /*npcType=*/1, /*classId=*/1);
    holdNoDrop(a, /*noDrop=*/true);
    a.push(eqt::events::TargetIntent{332});
    EXPECT_EQ(a.requests, 1);
    EXPECT_TRUE(systemLines(a.bridge.drainEvents()).empty());
}

// Row 13: an item held + a click on a BANKER (class 40) -> nothing opens, the
// item stays held, eqstr 8505, only targeting.
TEST(TradeRefusal, AnItemHeldOnABankerPrintsTheCannotAcceptItemsLine) {
    TradeHarness a;
    loadShippedStrings(a);
    spawnAt(a, 123, "Kendall_Copperhold000", /*npcType=*/1, /*classId=*/40);
    holdNoDrop(a, /*noDrop=*/true);
    a.push(eqt::events::TargetIntent{123});
    EXPECT_EQ(a.requests, 0) << "row 13: no give window";
    ASSERT_NE(a.inventory().getCursorItem(), nullptr);
    const auto lines = systemLines(a.bridge.drainEvents());
    ASSERT_EQ(lines.size(), 1u);
    EXPECT_EQ(lines[0],
              "This NPC cannot accept items directly, right-click if you wish to make transactions.")
        << "row 13, eqstr 8505";
    EXPECT_EQ(a.eq.GetCombatManager()->GetTargetId(), 123);
}

// Row 14: COIN held + a click on a player opens the trade exactly as an item
// does, and right after the ack the pile goes OP_MoveCoin 0 -> 3.
TEST(TradeGame, HeldCoinOnAPlayerOpensTheTradeAndMovesTheCoinIn) {
    TradeHarness a;
    TradePanelState window;
    spawnAt(a, 329, "Testbuddy", /*npcType=*/0, /*classId=*/1);
    a.inventory().pickupMoney(inv::InventoryManager::CursorMoneyType::Platinum, 5);
    a.coinMoves.clear();
    a.push(eqt::events::TargetIntent{329});
    EXPECT_EQ(a.requests, 1) << "row 14: OP_TradeRequest";
    EXPECT_TRUE(a.coinMoves.empty()) << "nothing moves before the ack";
    a.replayAckFrom(329);
    ASSERT_TRUE(a.trade().isTrading());
    ASSERT_EQ(a.coinMoves.size(), 1u);
    EXPECT_EQ(a.coinMoves[0].from_slot, 0) << "row 14: from_slot=0 (cursor)";
    EXPECT_EQ(a.coinMoves[0].to_slot, 3) << "row 14: to_slot=3 (trade)";
    EXPECT_EQ(a.coinMoves[0].cointype1, EQT::COINTYPE_PP);
    EXPECT_EQ(a.coinMoves[0].amount, 5);
    EXPECT_FALSE(a.inventory().hasCursorMoney());
    a.pump(window);
    EXPECT_TRUE(window.isOpen);
    EXPECT_EQ(window.ownCoins[0], 5u);
}

// Rows 4 and 10: Trade / Give sends OP_TradeAcceptClick, once, and says nothing.
TEST(TradeGame, TheTradeButtonSendsOneAcceptClickAndNoChatLine) {
    TradeHarness a;
    a.replayRequestFrom(329);
    a.bridge.drainEvents();
    a.push(eqt::events::TradeWindowIntent{eqt::events::TradeWindowOp::Accept});
    a.push(eqt::events::TradeWindowIntent{eqt::events::TradeWindowOp::Accept});
    EXPECT_EQ(a.acceptClicks, 1);
    EXPECT_TRUE(a.trade().isOwnAccepted());
    EXPECT_TRUE(systemLines(a.bridge.drainEvents()).empty());
}

// Row 8: Cancel sends OP_CancelTrade and prints eqstr 1450 -- and the side
// whose PARTNER cancelled prints the same line.
TEST(TradeGame, CancelPrintsEqstr1450OnBothSides) {
    TradeHarness b;
    TradePanelState window;
    b.replayRequestFrom(261);
    b.pump(window);
    b.push(eqt::events::TradeWindowIntent{eqt::events::TradeWindowOp::Cancel});
    EXPECT_EQ(b.cancels, 1);
    EXPECT_FALSE(b.trade().isTrading());
    auto ev = b.pump(window);
    auto lines = systemLines(ev);
    ASSERT_EQ(lines.size(), 1u);
    EXPECT_EQ(lines[0], "You have cancelled the trade.");
    EXPECT_FALSE(window.isOpen);

    TradeHarness a;
    a.replayRequestFrom(329);
    a.bridge.drainEvents();
    EQT::CancelTrade_Struct c{329, 7};
    a.trade().handleCancelTrade(c);
    lines = systemLines(a.bridge.drainEvents());
    ASSERT_EQ(lines.size(), 1u);
    EXPECT_EQ(lines[0], "You have cancelled the trade.");
    EXPECT_EQ(a.cancels, 1);   // our echo, T259
}

// Row 7: completion prints NO chat line and closes the window.
TEST(TradeGame, CompletionPrintsNothingAndClosesTheWindow) {
    TradeHarness a;
    TradePanelState window;
    a.replayRequestFrom(329);
    a.partnerOffers(0, 15288, "Spell: Minor Shielding");
    a.pump(window);
    ASSERT_TRUE(window.isOpen);
    a.trade().handleFinishTrade();
    auto ev = a.pump(window);
    for (const auto& l : systemLines(ev)) {
        ADD_FAILURE() << "completion printed: " << l;
    }
    EXPECT_FALSE(window.isOpen);
}

// THE GREEN-NAME STATE MACHINE, client A's window (Testchar), from the
// ledger's packet sequence: row 1 open (both red) -> row 3 B offers ->
// row 4 A presses Trade (A's OWN name green) -> row 6 B adds a second item
// (A's green resets to red) -> row 7 A presses Trade again, then B's accept
// arrives (both green) and OP_FinishTrade closes it.
TEST(TradeGreenNames, ClientASeesItsOwnNameGoGreenAndResetOnBsChange) {
    TradeHarness a;
    TradePanelState w;
    a.holdItem(13015, "Low Quality Wolf Skin");
    a.trade().requestTrade(329, "Testbuddy", false);
    a.replayAckFrom(329);                                   // row 1
    a.pump(w);
    ASSERT_TRUE(w.isOpen);
    EXPECT_EQ(w.partnerName, "Testbuddy");
    EXPECT_EQ(tradeOwnNameColor(w), TradeNameColor::Red);
    EXPECT_EQ(tradePartnerNameColor(w), TradeNameColor::Red);
    EXPECT_TRUE(w.own[0].hasItem);

    a.partnerOffers(0, 15288, "Spell: Minor Shielding");    // row 3
    a.pump(w);
    EXPECT_TRUE(w.partner[0].hasItem);
    EXPECT_EQ(w.partner[0].iconId, 504u);

    a.push(eqt::events::TradeWindowIntent{eqt::events::TradeWindowOp::Accept});  // row 4
    a.pump(w);
    EXPECT_EQ(tradeOwnNameColor(w), TradeNameColor::Green);
    EXPECT_EQ(tradePartnerNameColor(w), TradeNameColor::Red);

    a.partnerOffers(4, 15289, "Spell: Burst of Flame");     // row 6 (B's 3004)
    a.pump(w);
    EXPECT_EQ(tradeOwnNameColor(w), TradeNameColor::Red) << "row 6: any change resets it";
    EXPECT_TRUE(w.partner[4].hasItem);

    a.push(eqt::events::TradeWindowIntent{eqt::events::TradeWindowOp::Accept});  // row 7
    a.partnerAccepts(329);
    a.pump(w);
    EXPECT_EQ(tradeOwnNameColor(w), TradeNameColor::Green);
    EXPECT_EQ(tradePartnerNameColor(w), TradeNameColor::Green);
    EXPECT_EQ(a.acceptClicks, 2);

    a.trade().handleFinishTrade();                          // row 7: closes
    a.pump(w);
    EXPECT_FALSE(w.isOpen);
}

// The same rows from client B's window (Testbuddy): A's press turns the
// PARTNER name green (row 4), and B's OWN change resets it (row 6) with no
// packet coming back to B about that change.
TEST(TradeGreenNames, ClientBSeesThePartnersNameGoGreenAndItsOwnChangeResetsIt) {
    TradeHarness b;
    TradePanelState w;
    b.replayRequestFrom(261);                                // row 1
    b.pump(w);
    ASSERT_TRUE(w.isOpen);
    b.partnerOffers(0, 13015, "Low Quality Wolf Skin");      // A's 3000

    b.holdItem(15288, "Spell: Minor Shielding");             // row 3
    eqt::events::CursorGestureIntent place;
    place.slot = inv::TRADE_BEGIN;
    b.push(place);
    b.pump(w);
    EXPECT_TRUE(w.own[0].hasItem);

    b.partnerAccepts(261);                                   // row 4
    b.pump(w);
    EXPECT_EQ(tradePartnerNameColor(w), TradeNameColor::Green);
    EXPECT_EQ(tradeOwnNameColor(w), TradeNameColor::Red);

    b.holdItem(15289, "Spell: Burst of Flame");              // row 6
    place.slot = inv::TRADE_BEGIN + 4;
    b.push(place);
    b.pump(w);
    EXPECT_TRUE(w.own[4].hasItem);
    EXPECT_EQ(tradePartnerNameColor(w), TradeNameColor::Red) << "row 6: our change resets A's accept";
    EXPECT_FALSE(b.eq.GetGameState().trade().partnerAccepted());
}

// ===========================================================================
// 6. The stub is gone
// ===========================================================================

TEST(TradeWindowCensus, TheEmptyTradeWindowStubIsDeleted) {
    for (const char* rel : {"src/client/merchant/merchant_trade.cpp",
                            "include/client/merchant/merchant_component.h",
                            "src/client/bridge/bridge_component.cpp"}) {
        const std::string text = slurpAbs(kRoot + "/" + rel);
        ASSERT_FALSE(text.empty()) << rel;
        EXPECT_EQ(text.find("SetupTradeWindowCallbacks"), std::string::npos) << rel;
    }
}
