/*
 * The merchant window -- renderer-side state and draw (T436, board B429).
 *
 * WHAT WAS BROKEN. fable had no merchant window at all: `/vendor` opened a
 * server-side session, `/vendorlist` printed the stock as chat lines and
 * `/buy` / `/sell` took a slot NUMBER. Three producers filled an `iconId`
 * that nothing could draw (B429). The commerce vocabulary existed end to end
 * and was reachable only by typing.
 *
 * Same static-panel idiom as T290's loot window, T415's tradeskill panel and
 * T421's item display: a plain state struct plus free functions, so a test
 * drives the REAL publisher's data struct through the REAL applier with no
 * Irrlicht device.
 *
 * THE GEOMETRY IS NOT HERE. Every rect comes from
 * include/client/graphics/ui/merchant_layout.h, which quotes
 * EQUI_MerchantWnd.xml and writes out the anchor arithmetic.
 *
 * NO NEW PROTOCOL CODE. Buy pushes the existing `VendorBuyIntent` and Sell
 * the existing `VendorSellIntent` -- the same two intents `/buy` and `/sell`
 * push, drained by the same `BuyFromVendor()` / `SellToVendor()` dispatch.
 * Done pushes the existing `VendorToggleIntent`, whose handler closes an open
 * vendor session (`BridgeComponent::onVendorToggleIntent`), so the window
 * closes when the SERVER confirms with `VendorWindowClosed` -- never
 * optimistically, T290's rule.
 *
 * BUY AND SELL SHARE ONE RECT, because the oracle's own two elements carry
 * byte-identical anchors (see merchant_layout.h). The official client can
 * therefore only show one at a time, and which one it shows depends on
 * whether the item under consideration is the merchant's or yours. fable's
 * list holds the MERCHANT's stock (it is fed by `VendorItemAdded`) and fable
 * has no drag, so the rule here is:
 *
 *     the cursor holds an item  ->  SELL (of the cursor item)
 *     otherwise                 ->  BUY  (of the selected list row)
 *
 * which is the same disjunction the oracle draws, expressed with the one
 * "your item" fable can name. It is a DESIGN CHOICE made to fit the oracle's
 * shared rect, not a measurement; the live pair that would settle it is
 * OWED (T436 held no eqemu lock) -- board B443.
 */

#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "client/graphics/ui/ui_layout.h"

namespace eqt { namespace state {
struct WindowOpenedData;
struct WindowClosedData;
struct VendorItemAddedData;
} }

namespace EQT {
namespace Graphics {

class UIRenderer;
struct CursorAttachmentState;

/** One MW_ItemList row -- exactly the fields VendorItemAddedData carries. */
struct MerchantItemRow {
    uint16_t vendorSlot = 0;
    uint32_t itemId = 0;
    std::string itemName;
    uint32_t iconId = 0;    // T430's dragitem id, drawn in the COL_ICON cell
    int32_t price = 0;      // in copper
    int32_t quantity = 0;
};

struct MerchantPanelState {
    bool isOpen = false;

    uint16_t npcId = 0;
    std::string merchantName;   // -> MW_MerchantName

    std::vector<MerchantItemRow> items;   // -> MW_ItemList

    // MW_SelectedItem / MW_SelectedItemLabel / MW_SelectedPriceLabel all
    // describe ONE row; -1 is "nothing considered yet".
    int selectedRow = -1;

    // `Style_VScroll true` on MW_ItemList. Measured from the TOP, the
    // SkillsPopupState/LootPanelState convention.
    int scrollOffset = 0;

    int hoveredRow = -1;
    bool tradeHovered = false;   // MW_Buy_Button / MW_Sell_Button
    bool doneHovered = false;    // MW_Done_Button

    /** Highest legal `scrollOffset`, the LootPanelState::maxScrollOffset shape. */
    int maxScrollOffset() const;

    /** How many rows are actually drawn this frame. */
    int visibleRows() const;
};

// --- the bridge arms, as free functions (T290's idiom) ---------------------

/** VendorWindowOpened: opens the window and clears the previous stock. */
void applyVendorWindowOpened(MerchantPanelState& state,
                             const eqt::state::WindowOpenedData& data);

/**
 * VendorItemAdded: one row. The server sends one packet per stocked item and
 * marks no list boundary, so a repeat of a slot REPLACES that row rather than
 * appending a duplicate (the same rule T419 gave the recipe list).
 */
void applyVendorItemAdded(MerchantPanelState& state,
                          const eqt::state::VendorItemAddedData& data);

/** VendorWindowClosed: the server has confirmed the session is over. */
void applyVendorWindowClosed(MerchantPanelState& state,
                             const eqt::state::WindowClosedData& data);

// --- draw and hit test -----------------------------------------------------

/** Draw the window. Only when state.isOpen. */
void renderMerchantPopup(UIRenderer& ui, const UILayout& layout,
                         const MerchantPanelState& state,
                         const CursorAttachmentState& cursor);

/**
 * Which list row is at (mouseX,mouseY)? The ITEM index, not the on-screen
 * row -- so a scrolled list selects what it shows. -1 on a miss and on a
 * closed window (a hidden panel must not eat clicks, T290's rule).
 */
int hitTestMerchantRow(const UILayout& layout, const MerchantPanelState& state,
                       int mouseX, int mouseY);

/** MW_Buy_Button / MW_Sell_Button -- one rect, see the header comment. */
bool hitTestMerchantTradeButton(const UILayout& layout,
                                const MerchantPanelState& state, int mouseX,
                                int mouseY);

/** MW_Done_Button. */
bool hitTestMerchantDoneButton(const UILayout& layout,
                               const MerchantPanelState& state, int mouseX,
                               int mouseY);

/** Is the point anywhere inside the open window? (Click consumption.) */
bool merchantContains(const UILayout& layout, const MerchantPanelState& state,
                      int mouseX, int mouseY);

}  // namespace Graphics
}  // namespace EQT
