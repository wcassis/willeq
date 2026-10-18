/*
 * T436 (board B429) -- the merchant window.
 *
 * A separate TU from static_panels.cpp for the same reason
 * item_display_panel.cpp and tradeskill_recipe_list.cpp are: substantial new
 * code goes in a new TU (M00). The geometry is in
 * include/client/graphics/ui/merchant_layout.h, which quotes
 * EQUI_MerchantWnd.xml; this file computes no rect.
 */

#include "client/graphics/ui/merchant_panel.h"

#include "client/graphics/ui/cursor_attachment.h"
#include "client/graphics/ui/merchant_layout.h"
#include "client/graphics/ui/ui_atlas.h"
#include "client/graphics/ui/ui_renderer.h"
#include "client/graphics/text_batch.h"
#include "client/state/event_bus.h"

#include <algorithm>
#include <string>

namespace EQT {
namespace Graphics {

namespace {

// The four coin columns. A price is copper; the oracle's own MW_ItemList
// splits it across Header_PlatCoin/Gold/Silv/Copp, which is the same 1000/
// 100/10/1 scale InventoryLayout's purse uses.
int coinDigit(int32_t priceCopper, int denom) {
    if (priceCopper < 0) priceCopper = 0;
    switch (denom) {
        case 0: return priceCopper / 1000;
        case 1: return (priceCopper / 100) % 10;
        case 2: return (priceCopper / 10) % 10;
        default: return priceCopper % 10;
    }
}

const irr::video::SColor kText(255, 225, 225, 225);
const irr::video::SColor kHeading(255, 255, 215, 0);
const irr::video::SColor kFieldBg(200, 18, 18, 24);
const irr::video::SColor kFrame(255, 80, 80, 80);

}  // namespace

int MerchantPanelState::visibleRows() const {
    const int remaining = static_cast<int>(items.size()) - scrollOffset;
    if (remaining <= 0) return 0;
    return std::min(remaining, MerchantLayout::ROWS_VISIBLE);
}

int MerchantPanelState::maxScrollOffset() const {
    const int over = static_cast<int>(items.size()) - MerchantLayout::ROWS_VISIBLE;
    return over > 0 ? over : 0;
}

// --- bridge arms -----------------------------------------------------------

void applyVendorWindowOpened(MerchantPanelState& state,
                             const eqt::state::WindowOpenedData& data) {
    state.isOpen = true;
    state.npcId = data.npcId;
    state.merchantName = data.npcName;
    // A new session's stock arrives item by item after this; the previous
    // merchant's rows must not survive into it.
    state.items.clear();
    state.selectedRow = -1;
    state.clearPlayerItem();
    state.lastSelected = MerchantPanelState::Side::None;   // row 1: "Buy" on open
    state.scrollOffset = 0;
    state.hoveredRow = -1;
}

void applyVendorItemAdded(MerchantPanelState& state,
                          const eqt::state::VendorItemAddedData& data) {
    MerchantItemRow row;
    row.vendorSlot = data.vendorSlot;
    row.itemId = data.itemId;
    row.itemName = data.itemName;
    row.iconId = data.iconId;
    row.price = data.price;
    row.quantity = data.quantity;

    // The server marks no list boundary and may resend a slot (a restock, a
    // buy that emptied a stack), so a repeat REPLACES rather than appends --
    // the rule T419 gave the recipe list.
    for (auto& existing : state.items) {
        if (existing.vendorSlot == row.vendorSlot) {
            existing = row;
            return;
        }
    }
    state.items.push_back(std::move(row));
    // The oracle's list is ordered by the merchant's own slot numbering.
    std::sort(state.items.begin(), state.items.end(),
              [](const MerchantItemRow& a, const MerchantItemRow& b) {
                  return a.vendorSlot < b.vendorSlot;
              });
}

void applyVendorWindowClosed(MerchantPanelState& state,
                             const eqt::state::WindowClosedData& data) {
    (void)data;
    state.isOpen = false;
    state.npcId = 0;
    state.items.clear();
    state.selectedRow = -1;
    state.clearPlayerItem();
    state.lastSelected = MerchantPanelState::Side::None;
    state.scrollOffset = 0;
    state.hoveredRow = -1;
}

// --- draw ------------------------------------------------------------------

void renderMerchantPopup(UIRenderer& ui, const UILayout& layout,
                         const MerchantPanelState& state,
                         const CursorAttachmentState& cursor) {
    if (!state.isOpen) return;

    auto* tb = ui.getTextBatch();
    ui.drawPanel(MerchantLayout::windowRect(layout));

    // --- MW_MerchantName ---
    // A <Label> paints no background; the quoted rect is 452 tall and the
    // text goes at its top (see merchant_layout.h), AlignCenter true.
    if (tb) {
        tb->addTextCentered(
            state.merchantName.empty() ? std::string("Merchant")
                                       : state.merchantName,
            MerchantLayout::nameTextRect(layout), kHeading);
    }

    // --- MW_ItemList: Style_Border true, Style_VScroll true ---
    const auto listBox = MerchantLayout::listRect(layout);
    ui.drawRect(listBox, kFieldBg);
    ui.drawFrame(listBox, kFrame);

    // The <Columns> headings. The four coin columns carry a <Header>
    // (Header_PlatCoin ...) rather than a <Heading>, i.e. an ART header, so
    // fable puts the coin decal there -- the same four atlas sprites T398
    // lifted for the purse.
    static const char* kHeadings[MerchantLayout::COLUMN_COUNT] = {
        "", "Item Name", "Qty", nullptr, nullptr, nullptr, nullptr};
    for (int c = 0; c < MerchantLayout::COLUMN_COUNT; ++c) {
        const auto cell = MerchantLayout::listHeaderCellRect(layout, c);
        if (kHeadings[c]) {
            if (tb && kHeadings[c][0] != '\0') {
                tb->addText(kHeadings[c], cell.UpperLeftCorner.X + 2,
                            cell.UpperLeftCorner.Y + 2, kHeading);
            }
            continue;
        }
        const int denom = c - MerchantLayout::COL_PLAT;
        ui.drawSprite(cell, static_cast<uint8_t>(
            static_cast<int>(UISprite::CoinPlatinum) + denom));
    }

    const int rows = state.visibleRows();
    for (int row = 0; row < rows; ++row) {
        const size_t idx = static_cast<size_t>(state.scrollOffset + row);
        const auto& item = state.items[idx];
        const auto rowRect = MerchantLayout::listRowRect(layout, row);
        if (static_cast<int>(idx) == state.selectedRow) {
            ui.drawRect(rowRect, irr::video::SColor(90, 90, 110, 160));
        } else if (state.hoveredRow == static_cast<int>(idx)) {
            ui.drawRect(rowRect, irr::video::SColor(60, 255, 255, 255));
        }

        // The first <Columns> entry is 24 wide with an EMPTY <Heading> --
        // that is the icon cell, and it is why B429 called the missing
        // window the reason three producers filled an iconId nothing drew.
        const auto iconCell = MerchantLayout::listCellRect(
            layout, row, MerchantLayout::COL_ICON);
        // The decal's ACTUAL blit destination: squareIn() of the cell. The
        // cell is square at the window's own size (24x24, the XML's column
        // and the measured 24px row pitch) but comes back 19x19 under the
        // 800x600 uniform factor, and the helper then blits the centred
        // square -- pin that shape (test test_item_icon_rects) rather than
        // an assumption.
        const auto decal = UIRenderer::squareIn(iconCell);
        const bool drewIcon = ui.drawItemIcon(decal, item.iconId);
        if (!tb) continue;
        if (!drewIcon && item.iconId != 0) {
            // The pre-T430 fallback every icon site keeps: a client with no
            // pack still says which icon it would have drawn.
            tb->addText(item.itemName.substr(
                            0, std::min<size_t>(3, item.itemName.size())),
                        decal.UpperLeftCorner.X + 2,
                        decal.UpperLeftCorner.Y + 2, kText);
        }

        const auto nameCell = MerchantLayout::listCellRect(
            layout, row, MerchantLayout::COL_NAME);
        tb->addText(item.itemName, nameCell.UpperLeftCorner.X + 2,
                    nameCell.UpperLeftCorner.Y + 2, kText);

        const auto qtyCell = MerchantLayout::listCellRect(
            layout, row, MerchantLayout::COL_QTY);
        if (item.quantity > 1) {
            tb->addText(std::to_string(item.quantity),
                        qtyCell.UpperLeftCorner.X + 2,
                        qtyCell.UpperLeftCorner.Y + 2, kText);
        }

        for (int denom = 0; denom < 4; ++denom) {
            const auto cell = MerchantLayout::listCellRect(
                layout, row, MerchantLayout::COL_PLAT + denom);
            tb->addText(std::to_string(coinDigit(item.price, denom)),
                        cell.UpperLeftCorner.X + 2, cell.UpperLeftCorner.Y + 2,
                        kText);
        }
    }

    // --- MW_SelectedItem: A_RecessedBox + a 40x40 A_DragItem decal ---
    const auto sel = MerchantLayout::selectedItemRect(layout);
    ui.drawSprite(sel, static_cast<uint8_t>(UISprite::SlotBackground));
    ui.drawFrame(sel, kFrame);

    // What the two labels and the trade button describe: the side of the
    // LAST selection (B442b, #152 merchant.tsv rows 1-4) -- your selected
    // item (Sell) or the selected list row (Buy). The cursor is not read:
    // the oracle never sells from it.
    (void)cursor;
    const bool sellMode = state.sellSelected();
    const MerchantItemRow* selected =
        (!sellMode && state.selectedRow >= 0 &&
         state.selectedRow < static_cast<int>(state.items.size()))
            ? &state.items[static_cast<size_t>(state.selectedRow)]
            : nullptr;

    const uint32_t decalIcon = sellMode ? state.playerIconId
                                        : (selected ? selected->iconId : 0);
    if (decalIcon != 0) {
        const auto decal = MerchantLayout::selectedDecalRect(layout);
        if (!ui.drawItemIcon(decal, decalIcon) && tb) {
            tb->addTextCentered(std::to_string(decalIcon), decal, kText);
        }
    }

    if (tb) {
        // MW_SelectedItemLabel -- the oracle's placeholder text is "Item
        // Being Considered", so an empty selection draws nothing rather than
        // a fake name.
        const std::string label = sellMode ? state.playerItemName
                                           : (selected ? selected->itemName
                                                       : std::string());
        if (!label.empty()) {
            const auto r = MerchantLayout::selectedItemLabelRect(layout);
            tb->addText(label, r.UpperLeftCorner.X, r.UpperLeftCorner.Y, kText);
        }
        // MW_SelectedPriceLabel. The merchant's price is only known for a
        // row the merchant sent. The oracle shows an offer for YOUR item
        // ("Bottle 4c", merchant.tsv row 3) that the client computes from
        // the item's value and the merchant's rate; fable carries neither to
        // this panel, so the price is blank in sell mode rather than
        // invented (owed, with the eqstr 12026 offer line).
        if (selected) {
            const auto r = MerchantLayout::selectedPriceLabelRect(layout);
            tb->addText(std::to_string(selected->price) + "cp",
                        r.UpperLeftCorner.X, r.UpperLeftCorner.Y, kText);
        }
    }

    // --- MW_Buy_Button / MW_Sell_Button: ONE rect, one of the two ---
    const auto trade = MerchantLayout::tradeButtonRect(layout);
    const bool tradeEnabled = sellMode || selected != nullptr;
    ui.drawRect(trade, tradeEnabled
                           ? (state.tradeHovered
                                  ? irr::video::SColor(220, 80, 96, 130)
                                  : irr::video::SColor(200, 56, 62, 84))
                           : irr::video::SColor(160, 40, 40, 46));
    ui.drawFrame(trade, kFrame);
    if (tb) {
        tb->addTextCentered(sellMode ? "Sell" : "Buy", trade,
                            tradeEnabled ? kText
                                         : irr::video::SColor(255, 120, 120, 128));
    }

    // --- MW_Done_Button ---
    const auto done = MerchantLayout::doneButtonRect(layout);
    ui.drawRect(done, state.doneHovered ? irr::video::SColor(220, 120, 60, 60)
                                        : irr::video::SColor(200, 56, 62, 84));
    ui.drawFrame(done, kFrame);
    if (tb) tb->addTextCentered("Done", done, kText);
}

// --- hit tests -------------------------------------------------------------

int hitTestMerchantRow(const UILayout& layout, const MerchantPanelState& state,
                       int mouseX, int mouseY) {
    if (!state.isOpen) return -1;
    const irr::core::vector2di p(mouseX, mouseY);
    const int rows = state.visibleRows();
    for (int row = 0; row < rows; ++row) {
        if (MerchantLayout::listRowRect(layout, row).isPointInside(p)) {
            return state.scrollOffset + row;
        }
    }
    return -1;
}

bool hitTestMerchantTradeButton(const UILayout& layout,
                                const MerchantPanelState& state, int mouseX,
                                int mouseY) {
    if (!state.isOpen) return false;
    return MerchantLayout::tradeButtonRect(layout).isPointInside(
        irr::core::vector2di(mouseX, mouseY));
}

bool hitTestMerchantDoneButton(const UILayout& layout,
                               const MerchantPanelState& state, int mouseX,
                               int mouseY) {
    if (!state.isOpen) return false;
    return MerchantLayout::doneButtonRect(layout).isPointInside(
        irr::core::vector2di(mouseX, mouseY));
}

bool merchantContains(const UILayout& layout, const MerchantPanelState& state,
                      int mouseX, int mouseY) {
    if (!state.isOpen) return false;
    return MerchantLayout::windowRect(layout).isPointInside(
        irr::core::vector2di(mouseX, mouseY));
}

}  // namespace Graphics
}  // namespace EQT
