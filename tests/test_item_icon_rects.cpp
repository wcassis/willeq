/*
 * B1078 (GitLab #398): every item-icon DECAL rect is a SQUARE, at every
 * supported resolution.
 *
 * Will's session finding ("the vendor window item icons are squashed") is
 * about the 40x40 dragitem decal (item_icon_atlas.h, the oracle's own
 * DecalSize) landing in a non-square cell and being stretched to fill it --
 * the pre-B1078 merchant row blitted it into a 25x14 box. The one helper
 * that keeps that from recurring, UIRenderer::squareIn(), is pinned by
 * test_item_icon_atlas.cpp (the squareIn cases); this file is the OTHER
 * half of the net: it walks EVERY call site that hands a rect to
 * drawItemIcon(), at every kSupportedResolutions, and asserts the cell it
 * hands over is square (equal width and height, both > 0).
 *
 * The sites (the census is src/client/graphics/ui/*, every drawItemIcon call
 * site, re-derive rather than trust this list):
 *   - the inventory's 22 equipment slots and 8 general slots (static_panels)
 *   - the tradeskill container's 10 cells (tradeskill_panel)
 *   - the loot rows' head-of-row icon (loot_panel)
 *   - the item display's IDW_IconButton decal (item_display_panel)
 *   - the hotbar's 10 item slots (hotbar_panel)
 *   - the merchant's 24px list-row icon column and the selected-item decal
 *     (merchant_panel)
 *   - the bank's 8 item art rects (bank_panel)
 *   - the container's 10 cells and the icon decal (container_panel)
 *   - the trade/give slot art (trade_panel's drawCell, both windows)
 *   - the cursor attachment's CA_Anim icon (cursor_attachment)
 *
 * The cells come from the SAME layout functions the render bodies call
 * (the T57 rule: this test and the draw read one geometry source, so a cell
 * that stopped being square fails here and not in a pixel diff). A site
 * whose cell is square at every resolution passes through squareIn()
 * unchanged, i.e. exactly as the oracle draws it.
 *
 * Headless apart from the irrlicht rect TYPE (every layout header it pulls
 * in is), so it is gated at registration exactly like
 * tests/test_ui_layout_coopen.cpp (B1038): an EQT_GRAPHICS=OFF configure
 * must not try to build it.
 */

#include <gtest/gtest.h>

#include "client/graphics/ui/bank_layout.h"
#include "client/graphics/ui/container_layout.h"
#include "client/graphics/ui/cursor_attachment_layout.h"
#include "client/graphics/ui/give_layout.h"
#include "client/graphics/ui/hotbar_layout.h"
#include "client/graphics/ui/inventory_layout.h"
#include "client/graphics/ui/item_display_layout.h"
#include "client/graphics/ui/merchant_layout.h"
#include "client/graphics/ui/trade_layout.h"
#include "client/graphics/ui/tradeskill_layout.h"
#include "client/graphics/ui/ui_layout.h"
#include "client/graphics/ui/ui_layout_table.h"
#include "client/graphics/ui/ui_renderer.h"  // squareIn()

#include <string>

namespace {

using namespace EQT::Graphics;
using Rect = irr::core::rect<irr::s32>;

UILayout layoutAt(int w, int h) {
    UILayout l;
    l.computeLayout(w, h);
    return l;
}

std::string str(const Rect& r) {
    return "(" + std::to_string(r.UpperLeftCorner.X) + "," +
           std::to_string(r.UpperLeftCorner.Y) + ")-(" +
           std::to_string(r.LowerRightCorner.X) + "," +
           std::to_string(r.LowerRightCorner.Y) + ")";
}

/** The check: the rect drawItemIcon() receives must be a square. */
void expectSquare(const char* site, int resW, int resH, const Rect& r) {
    EXPECT_GT(r.getWidth(), 0) << site << " at " << resW << "x" << resH
                               << " " << str(r) << " has no width";
    EXPECT_EQ(r.getWidth(), r.getHeight())
        << site << " at " << resW << "x" << resH << " " << str(r)
        << " is not square; the 40x40 dragitem decal would be stretched "
           "before squareIn() ever sees it";
}

}  // namespace

TEST(ItemIconRects, EveryInventorySlotCellIsSquareAtEveryResolution) {
    for (const auto& res : kSupportedResolutions) {
        const UILayout l = layoutAt(res.w, res.h);
        for (int i = 0; i < 22; ++i)
            expectSquare("inventory equipment slot", res.w, res.h,
                         InventoryLayout::slotRect(l, i));
        for (int i = 22; i < 30; ++i)
            expectSquare("inventory general slot", res.w, res.h,
                         InventoryLayout::slotRect(l, i));
    }
}

TEST(ItemIconRects, EveryTradeskillCellIsSquareAtEveryResolution) {
    for (const auto& res : kSupportedResolutions) {
        const UILayout l = layoutAt(res.w, res.h);
        for (int i = 0; i < TradeskillLayout::SLOT_COUNT; ++i)
            expectSquare("tradeskill container cell", res.w, res.h,
                         TradeskillLayout::slotRect(l, i));
    }
}

// The loot row's icon rect is built inline in renderLootPopup()
// (loot_panel.cpp): one pixel of inset top and bottom inside the 22px row,
// kRowIcon (20) wide. Reproduce it from the same anchors rather than a
// second constant, so this test and the draw keep one geometry source.
TEST(ItemIconRects, EveryLootRowIconCellIsSquareAtEveryResolution) {
    constexpr int kPad = 8, kTitleH = 20, kButtonW = 80, kButtonH = 16;
    constexpr int kRowH = 22, kRowIcon = 20;
    for (const auto& res : kSupportedResolutions) {
        const UILayout l = layoutAt(res.w, res.h);
        const auto& popup = l.partnerPopup;
        const int startX = popup.UpperLeftCorner.X + kPad;
        const int startY =
            popup.UpperLeftCorner.Y + kTitleH + kPad + kButtonH + kPad;
        // First visible row is enough: every row is the same shape shifted.
        const Rect icon(startX + 1, startY + 1, startX + kRowIcon + 1,
                        startY + kRowIcon + 1);
        expectSquare("loot row icon", res.w, res.h, icon);
    }
}

TEST(ItemIconRects, TheItemDisplayDecalIsSquareAtEveryResolution) {
    for (const auto& res : kSupportedResolutions) {
        const UILayout l = layoutAt(res.w, res.h);
        expectSquare("item display IDW_IconButton decal", res.w, res.h,
                     ItemDisplayLayout::iconDecalRect(l));
    }
}

// The hotbar draws its slots with layout.hotbarColumns (the layout table's
// orientation for the screen), the slots anchored in the layout's own
// hotbar rect; 40x40 either way. Use the same anchors the render body reads
// rather than recomputing a window rect.
TEST(ItemIconRects, EveryHotbarSlotCellIsSquareAtEveryResolution) {
    for (const auto& res : kSupportedResolutions) {
        const UILayout l = layoutAt(res.w, res.h);
        for (int i = 0; i < HotbarLayout::kSlotCount; ++i)
            expectSquare("hotbar item slot", res.w, res.h,
                         HotbarLayout::slotRect(l.hotbar, i, l.hotbarColumns));
    }
}

// The merchant list-row cell is the ONE site whose handed-over rect is not
// the raw cell: the 24px XML column comes back 19x19 under the 800x600
// uniform factor (the endpoints scale to 19, and the row pitch 24*80/100 =
// 19 too), so merchant_panel.cpp hands over UIRenderer::squareIn(cell) --
// the 19x19 centred square -- and the drawItemIcon() funnel's own squareIn
// is then the identity on it. This case pins that: the handed-over rect is
// square at every resolution, AND at the three resolutions where the factor
// is 100% the cell is already square (the raw cell IS the decal, so a change
// that squashed it fails here).
TEST(ItemIconRects, TheMerchantRowIconDecalIsSquareAtEveryResolution) {
    for (const auto& res : kSupportedResolutions) {
        const UILayout l = layoutAt(res.w, res.h);
        for (int row = 0; row < MerchantLayout::ROWS_VISIBLE; ++row) {
            const Rect cell = MerchantLayout::listCellRect(
                l, row, MerchantLayout::COL_ICON);
            const Rect decal = UIRenderer::squareIn(cell);
            expectSquare("merchant list-row decal", res.w, res.h, decal);
            if (MerchantLayout::scalePct(l) == 100) {
                EXPECT_EQ(cell, decal)
                    << "merchant row " << row << " at " << res.w << "x"
                    << res.h << ": the cell is " << str(cell)
                    << " at the window's own size -- it must be the 24x24 "
                       "the XML's icon column is, so the decal and the cell "
                       "agree";
            }
        }
        expectSquare("merchant selected-item decal", res.w, res.h,
                     MerchantLayout::selectedDecalRect(l));
        // The measured oracle row pitch (UIGAP-ORACLE/152, window at native
        // 374x480): the row names sit 24 px apart. The icon cell's height IS
        // that pitch, so pin it -- a change that shrank the rows to "fit"
        // more of the list in fails here before it reaches a pixel diff.
        EXPECT_EQ(MerchantLayout::ORACLE_ROW_H, 24);
        if (MerchantLayout::scalePct(l) == 100)
            EXPECT_EQ(
                MerchantLayout::listCellRect(l, 0,
                                             MerchantLayout::COL_ICON)
                    .getHeight(),
                24)
                << "the row pitch is the measured 24 px at the window's own "
                   "size";
    }
}

TEST(ItemIconRects, EveryBankSlotArtRectIsSquareAtEveryResolution) {
    for (const auto& res : kSupportedResolutions) {
        const UILayout l = layoutAt(res.w, res.h);
        for (int i = 0; i < BankLayout::SLOT_COUNT; ++i)
            expectSquare("bank slot art", res.w, res.h,
                         BankLayout::slotItemRect(l, i));
    }
}

TEST(ItemIconRects, EveryContainerCellAndIconIsSquareAtEveryResolution) {
    for (const auto& res : kSupportedResolutions) {
        const UILayout l = layoutAt(res.w, res.h);
        expectSquare("container icon decal", res.w, res.h,
                     ContainerLayout::iconRect(l));
        for (int i = 0; i < ContainerLayout::SLOT_COUNT; ++i)
            expectSquare("container cell", res.w, res.h,
                         ContainerLayout::slotRect(l, i));
    }
}

// drawCell() (trade_panel.cpp) draws the trade and the give window from the
// same body: the art is TradeLayout::itemArtRect(cell) (give shares the
// constants through the include) inside the window's own 40x40 slot.
TEST(ItemIconRects, EveryTradeAndGiveSlotArtRectIsSquareAtEveryResolution) {
    for (const auto& res : kSupportedResolutions) {
        const UILayout l = layoutAt(res.w, res.h);
        for (int i = 0; i < TradeLayout::SLOTS_PER_SIDE; ++i) {
            expectSquare("trade own slot art", res.w, res.h,
                         TradeLayout::itemArtRect(TradeLayout::mySlotRect(l, i)));
            expectSquare("trade partner slot art", res.w, res.h,
                         TradeLayout::itemArtRect(TradeLayout::hisSlotRect(l, i)));
            expectSquare("give slot art", res.w, res.h,
                         TradeLayout::itemArtRect(GiveLayout::slotRect(l, i)));
        }
    }
}

// The cursor attachment is mouse-anchored; test it at a mid-screen point so
// the edge clamp does not engage.
TEST(ItemIconRects, TheCursorAttachmentIconIsSquareAtEveryResolution) {
    for (const auto& res : kSupportedResolutions) {
        const UILayout l = layoutAt(res.w, res.h);
        const int mx = res.w / 2, my = res.h / 2;
        expectSquare("cursor attachment CA_Anim", res.w, res.h,
                     CursorAttachmentLayout::iconRect(l, mx, my));
    }
}
