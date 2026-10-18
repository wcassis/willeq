// B1075 (GitLab #395): the layout table's REQUIRED CO-OPEN SETS.
//
// Will's ruling (docs/decisions.md 2026-10-15; layout shape confirmed on #395
// note 5180): the oracle governs a window's content, the fable layout table
// (include/client/graphics/ui/ui_layout_table.h) governs where it sits, and
// no two windows of a required co-open set may overlap at any supported
// resolution. Every rect here comes from windowRectFor()
// (ui_window_rects.h), i.e. the same function each window's render body and
// hit test call, so a window that stopped reading its anchor fails here.
//
// Headless: no device, no GL, no Irrlicht beyond the rect type.

#include <gtest/gtest.h>

#include "client/graphics/ui/hotbar_layout.h"
#include "client/graphics/ui/ui_layout.h"
#include "client/graphics/ui/ui_layout_table.h"
#include "client/graphics/ui/ui_window_rects.h"

#include <algorithm>
#include <fstream>
#include <functional>
#include <sstream>
#include <string>
#include <vector>

namespace {

using namespace EQT::Graphics;
using Rect = irr::core::rect<irr::s32>;
using RectFn = std::function<Rect(UIWindow, const UILayout&)>;

UILayout layoutAt(int w, int h) {
    UILayout l;
    l.computeLayout(w, h);
    return l;
}

// Strict interior overlap: windows that share only an edge do not overlap.
bool overlaps(const Rect& a, const Rect& b) {
    return a.UpperLeftCorner.X < b.LowerRightCorner.X &&
           b.UpperLeftCorner.X < a.LowerRightCorner.X &&
           a.UpperLeftCorner.Y < b.LowerRightCorner.Y &&
           b.UpperLeftCorner.Y < a.LowerRightCorner.Y;
}

bool onScreen(const Rect& r, const UILayout& l) {
    return r.getWidth() > 0 && r.getHeight() > 0 && r.UpperLeftCorner.X >= 0 &&
           r.UpperLeftCorner.Y >= 0 && r.LowerRightCorner.X <= l.screenW &&
           r.LowerRightCorner.Y <= l.screenH;
}

std::string str(const Rect& r) {
    std::ostringstream o;
    o << "(" << r.UpperLeftCorner.X << "," << r.UpperLeftCorner.Y << ")-("
      << r.LowerRightCorner.X << "," << r.LowerRightCorner.Y << ")";
    return o.str();
}

// THE check, shared by the positive and the negative tests: every violation
// of `set` on `layout`, one line each.
std::vector<std::string> violations(const CoOpenSet& set, const UILayout& layout,
                                    const RectFn& rectOf) {
    std::vector<std::string> out;
    for (int i = 0; i < set.count; ++i) {
        const UIWindow a = set.members[i];
        const Rect ra = rectOf(a, layout);
        if (!onScreen(ra, layout)) {
            out.push_back(std::string(layoutRow(a).name) + " off screen " + str(ra));
        }
        for (int j = i + 1; j < set.count; ++j) {
            const UIWindow b = set.members[j];
            const Rect rb = rectOf(b, layout);
            if (overlaps(ra, rb)) {
                out.push_back(std::string(layoutRow(a).name) + " " + str(ra) +
                              " overlaps " + layoutRow(b).name + " " + str(rb));
            }
        }
    }
    return out;
}

TEST(UiLayoutCoOpen, EveryRequiredSetFitsAtEverySupportedResolution) {
    for (const auto& res : kSupportedResolutions) {
        const UILayout l = layoutAt(res.w, res.h);
        for (const auto& set : kRequiredCoOpenSets) {
            for (const auto& v : violations(set, l, windowRectFor)) {
                ADD_FAILURE() << res.w << "x" << res.h << " [" << set.name << "] " << v;
            }
        }
    }
}

TEST(UiLayoutCoOpen, TheRulingsNamedSetsAreInTheTable) {
    // Ruling item 2 names these; the table may hold more, never fewer.
    EXPECT_TRUE(inRequiredSet(UIWindow::Inventory, UIWindow::Trade));
    EXPECT_TRUE(inRequiredSet(UIWindow::Inventory, UIWindow::Bank));
    EXPECT_TRUE(inRequiredSet(UIWindow::Inventory, UIWindow::Merchant));
    EXPECT_TRUE(inRequiredSet(UIWindow::Inventory, UIWindow::Loot));
    EXPECT_TRUE(inRequiredSet(UIWindow::Spellbook, UIWindow::SpellGems));
    for (UIWindow hud : {UIWindow::Chat, UIWindow::Hotbar, UIWindow::Target,
                         UIWindow::Group, UIWindow::Buffs}) {
        for (const auto& row : kUILayoutTable) {
            // Every one-at-a-time centre popup (the CentreSlot ones and the
            // inventory) is in a set with every always-on HUD window.
            if (row.anchor == UIAnchor::CentreSlot || row.anchor == UIAnchor::PairRight) {
                EXPECT_TRUE(inRequiredSet(row.window, hud))
                    << row.name << " + " << layoutRow(hud).name;
            }
        }
    }
}

TEST(UiLayoutCoOpen, ACollidingTableFails) {
    // The negative: the pre-B1075 shape -- inventory and merchant sharing ONE
    // centre box -- must be reported, at every resolution.
    const RectFn shared = [](UIWindow w, const UILayout& l) {
        if (w == UIWindow::Merchant) return l.inventoryPopup;
        return windowRectFor(w, l);
    };
    const CoOpenSet* invMerchant = nullptr;
    for (const auto& s : kRequiredCoOpenSets) {
        if (std::string(s.name) == "inventory+merchant") invMerchant = &s;
    }
    ASSERT_NE(invMerchant, nullptr);
    for (const auto& res : kSupportedResolutions) {
        const UILayout l = layoutAt(res.w, res.h);
        EXPECT_FALSE(violations(*invMerchant, l, shared).empty()) << res.w << "x" << res.h;
    }

    // ...and a window pushed off the screen is reported too.
    const RectFn offScreen = [](UIWindow w, const UILayout& l) {
        Rect r = windowRectFor(w, l);
        if (w == UIWindow::Bank) r += irr::core::vector2di(l.screenW, 0);
        return r;
    };
    const CoOpenSet invBank = {"inventory+bank", 2, {UIWindow::Inventory, UIWindow::Bank}};
    EXPECT_FALSE(violations(invBank, layoutAt(1280, 720), offScreen).empty());

    // Edge-sharing is NOT an overlap (the 800x600 pair touches at x=400).
    EXPECT_FALSE(overlaps(Rect(0, 0, 10, 10), Rect(10, 0, 20, 10)));
    EXPECT_TRUE(overlaps(Rect(0, 0, 11, 10), Rect(10, 0, 20, 10)));
}

TEST(UiLayoutCoOpen, HotbarIsBottomCentreOneByTenAndTwoByFiveStaysRenderable) {
    // Ruling item 3 + #395 note 5180: bottom-centre 1x10 at every supported
    // resolution.
    for (const auto& res : kSupportedResolutions) {
        const UILayout l = layoutAt(res.w, res.h);
        EXPECT_EQ(l.hotbarColumns, 10) << res.w << "x" << res.h;
        EXPECT_EQ(l.hotbar.getWidth(), HotbarLayout::windowW(10));
        EXPECT_EQ(l.hotbar.getHeight(), HotbarLayout::windowH(10));
        // Bottom: the stock client's 30px gap under the window.
        EXPECT_EQ(l.hotbar.LowerRightCorner.Y, res.h - 30);
        // Centre: centred on the screen unless that would cover the chat.
        const int centredX = (res.w - l.hotbar.getWidth()) / 2;
        const int minX = l.chatPanel.LowerRightCorner.X + UILayout::MARGIN;
        EXPECT_EQ(l.hotbar.UpperLeftCorner.X, centredX < minX ? minX : centredX);
        // Ten cells in ONE row, inside the window.
        for (int i = 0; i < HotbarLayout::kSlotCount; ++i) {
            const Rect c = HotbarLayout::slotRect(l.hotbar, i, l.hotbarColumns);
            EXPECT_EQ(c.UpperLeftCorner.Y, l.hotbar.UpperLeftCorner.Y + HotbarLayout::kSlotFirstY);
            EXPECT_TRUE(l.hotbar.isRectCollided(c));
            EXPECT_LE(c.LowerRightCorner.X, l.hotbar.LowerRightCorner.X);
        }
    }
    // The 2x5 content is the SAME arithmetic with columns = 2: the oracle's
    // own 96x260 window and its cells (test_hotbar_layout pins them to the XML).
    EXPECT_EQ(HotbarLayout::windowW(2), HotbarLayout::kWindowW);
    EXPECT_EQ(HotbarLayout::windowH(2), HotbarLayout::kWindowH);
    const Rect stock = HotbarLayout::windowRect();
    for (int i = 0; i < HotbarLayout::kSlotCount; ++i) {
        EXPECT_EQ(HotbarLayout::slotRect(stock, i, 2), HotbarLayout::slotRect(stock, i));
    }
    // A screen too narrow for 1x10 beside the chat falls back to the table's
    // next orientation, the 2x5.
    EXPECT_EQ(layoutAt(640, 480).hotbarColumns, 2);
}

TEST(UiLayoutCoOpen, CoOpenTableReplacesTheMutualExclusionRule) {
    // The pairs the table lets stay open together...
    EXPECT_FALSE(closesOnOpen(UIWindow::Merchant, UIWindow::Inventory));
    EXPECT_FALSE(closesOnOpen(UIWindow::Loot, UIWindow::Inventory));
    EXPECT_FALSE(closesOnOpen(UIWindow::Inventory, UIWindow::Merchant));
    // ...and the centre popups in no shared set keep the one-at-a-time rule.
    EXPECT_TRUE(closesOnOpen(UIWindow::Skills, UIWindow::Inventory));
    EXPECT_TRUE(closesOnOpen(UIWindow::Skills, UIWindow::Spellbook));
    EXPECT_TRUE(closesOnOpen(UIWindow::Spellbook, UIWindow::Inventory));
    EXPECT_TRUE(closesOnOpen(UIWindow::Loot, UIWindow::Spellbook));
    EXPECT_TRUE(closesOnOpen(UIWindow::Inventory, UIWindow::Skills));
    // A window never closes itself, and the HUD is never closed by a popup.
    EXPECT_FALSE(closesOnOpen(UIWindow::Inventory, UIWindow::Inventory));
    EXPECT_FALSE(closesOnOpen(UIWindow::Inventory, UIWindow::Chat));
}

TEST(UiLayoutCoOpen, SizesAreTheWindowsOwnOnlyTheAnchorMoves) {
    for (const auto& res : kSupportedResolutions) {
        const UILayout l = layoutAt(res.w, res.h);
        // B1078 (GitLab #398): the three rescaled windows are the oracle's own
        // size at a ONE uniform factor that fits them into the middle band
        // (their headers' scalePct), capped at native. So the factor is 1
        // whenever the band can hold the native size, and the rect is the
        // oracle's number to the pixel; otherwise the WHOLE window shrinks
        // uniformly (w and h by the same factor), never anisotropically.
        struct Scaled {
            UIWindow w;
            irr::s32 nativeW;
            irr::s32 nativeH;
            irr::s32 (*f)(const UILayout&);
        };
        for (const auto& s : {Scaled{UIWindow::Inventory, 428, 460, &InventoryLayout::scalePct},
                               Scaled{UIWindow::Merchant, 374, 480, &MerchantLayout::scalePct}}) {
            const irr::s32 f = s.f(l);
            const Rect r = windowRectFor(s.w, l);
            EXPECT_EQ(r.getWidth(), s.nativeW * f / 100) << layoutRow(s.w).name;
            EXPECT_EQ(r.getHeight(), s.nativeH * f / 100) << layoutRow(s.w).name;
        }
        {
            const irr::s32 f = TradeskillLayout::scalePct(l);
            const Rect r = windowRectFor(UIWindow::Tradeskill, l);
            EXPECT_EQ(r.getWidth(), 400 * f / 100);
            EXPECT_EQ(r.getHeight(), 525 * f / 100);
        }
        // The unchanged windows keep their own sizes.
        EXPECT_EQ(windowRectFor(UIWindow::Loot, l).getWidth(), UILayout::POPUP_W);
        EXPECT_EQ(windowRectFor(UIWindow::Loot, l).getHeight(), UILayout::POPUP_H);
        // B1076 (#396): the spellbook is the oracle's own 510x360, not the
        // shared 400x350 centre popup it used to share with the skills popup.
        EXPECT_EQ(windowRectFor(UIWindow::Spellbook, l).getWidth(),
                  SpellBookLayout::kWindowW);
        EXPECT_EQ(windowRectFor(UIWindow::Spellbook, l).getHeight(),
                  SpellBookLayout::kWindowH);
        EXPECT_EQ(windowRectFor(UIWindow::Skills, l).getWidth(), UILayout::POPUP_W);
        EXPECT_EQ(windowRectFor(UIWindow::Skills, l).getHeight(), UILayout::POPUP_H);
        EXPECT_EQ(windowRectFor(UIWindow::Bank, l).getWidth(), BankLayout::WINDOW_W);
        EXPECT_EQ(windowRectFor(UIWindow::Bank, l).getHeight(), BankLayout::WINDOW_H);
        EXPECT_EQ(windowRectFor(UIWindow::Trade, l).getWidth(), TradeLayout::WINDOW_W);
        EXPECT_EQ(windowRectFor(UIWindow::Trade, l).getHeight(), TradeLayout::WINDOW_H);
        EXPECT_EQ(windowRectFor(UIWindow::Give, l).getWidth(), GiveLayout::WINDOW_W);
        EXPECT_EQ(windowRectFor(UIWindow::Give, l).getHeight(), GiveLayout::WINDOW_H);
        // B1078 anchors: the merchant and the inventory take the pair's left
        // and right slots and top-anchor at the middle band's top (the
        // 350-tall popup top predates the band); the tradeskill anchors at
        // the centre slot's left, band top. Bank/trade/give keep
        // placePairLeft (right-aligned in the left slot, top-aligned with
        // the inventory).
        const auto m = windowRectFor(UIWindow::Merchant, l);
        EXPECT_EQ(m.UpperLeftCorner.X, l.partnerPopup.UpperLeftCorner.X) << "merchant x";
        EXPECT_EQ(m.UpperLeftCorner.Y, l.middleBand.UpperLeftCorner.Y) << "merchant y";
        const auto inv = windowRectFor(UIWindow::Inventory, l);
        EXPECT_EQ(inv.UpperLeftCorner.X, l.inventoryPopup.UpperLeftCorner.X) << "inventory x";
        EXPECT_EQ(inv.UpperLeftCorner.Y, l.middleBand.UpperLeftCorner.Y) << "inventory y";
        const auto ts = windowRectFor(UIWindow::Tradeskill, l);
        EXPECT_EQ(ts.UpperLeftCorner.X, l.centerPopup.UpperLeftCorner.X) << "tradeskill x";
        EXPECT_EQ(ts.UpperLeftCorner.Y, l.middleBand.UpperLeftCorner.Y) << "tradeskill y";
        for (UIWindow w : {UIWindow::Bank, UIWindow::Trade, UIWindow::Give}) {
            const Rect r = windowRectFor(w, l);
            EXPECT_EQ(r.LowerRightCorner.X, l.partnerPopup.LowerRightCorner.X) << layoutRow(w).name;
            EXPECT_EQ(r.UpperLeftCorner.Y, l.inventoryPopup.UpperLeftCorner.Y) << layoutRow(w).name;
        }
    }
}

// The drawn-last rules read against the table's roles: in
// renderer_frame_render.cpp's static-UI pass the item display and the Options
// window are drawn after every centre popup ('C') and every PairLeft window,
// and the HUD before the popups. A text check, like test_frame_render_order:
// the pass needs a live device and cannot run here.
TEST(UiLayoutCoOpen, DrawOrderAgreesWithTheTablesRoles) {
    std::ifstream in(std::string(WILLEQ_SOURCE_DIR) +
                     "/src/client/graphics/frame/renderer_frame_render.cpp");
    ASSERT_TRUE(in.good());
    std::stringstream ss;
    ss << in.rdbuf();
    const std::string src = ss.str();
    auto pos = [&](const char* call) {
        const auto p = src.find(std::string(call) + "(uiRenderer_->layer()");
        EXPECT_NE(p, std::string::npos) << call;
        return p;
    };
    const auto hudLast = std::max({pos("renderPlayerStatus"), pos("renderTargetInfo"),
                                   pos("renderChatPanel"), pos("renderHotbar"),
                                   pos("renderBuffBar"), pos("renderGroupPanel")});
    const auto popupsFirst = std::min({pos("renderInventoryPopup"), pos("renderSpellbookPopup"),
                                       pos("renderSkillsPopup"), pos("renderLootPopup"),
                                       pos("renderMerchantPopup"), pos("renderTradeskillPopup")});
    const auto partnersLast = std::max({pos("renderInventoryPopup"), pos("renderLootPopup"),
                                        pos("renderMerchantPopup"), pos("renderTradeskillPopup"),
                                        pos("renderBankWindow"), pos("renderTradeWindow")});
    EXPECT_LT(hudLast, popupsFirst);
    EXPECT_LT(partnersLast, pos("renderItemDisplayPopup"));
    EXPECT_LT(pos("renderItemDisplayPopup"), pos("renderOptionsWindow"));
}

}  // namespace
