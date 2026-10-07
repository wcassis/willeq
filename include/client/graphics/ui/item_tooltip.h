/*
 * Item-name tooltips (B1004, GitLab #323) -- the Alt-held name labels and the
 * hover tooltip over item slots.
 *
 * MEASURED on the official Titanium client (review/UIGAP-ORACLE/323/,
 * tooltips.tsv; the row numbers below are that ledger's):
 *   - Holding ALT with the pointer over a BAG window draws a name label on
 *     EVERY occupied slot of that bag at once (rows 14-17); the pointer need
 *     not be on an item (15-16). Release -> gone on the next frame. A tap does
 *     nothing: a HOLD, not a toggle (row 12).
 *   - The pointer leaving the bag (to the world or onto another window) hides
 *     them while Alt is still held; back over the bag shows them again (23-24).
 *   - The BANK window does the same (row 28), and with the bank open, Alt with
 *     the pointer over the INVENTORY window shows the BANK's labels (row 30,
 *     4/4 trials). The inventory's own slots never get labels (rows 9-11, 18);
 *     the merchant shows none (row 33); the trade window was not captured
 *     (row 34), so it gets none here.
 *   - Text: the item NAME ONLY, never the stack count (rows 17, 28).
 *   - Placement, 2-column window: left column at the slot's (+35, +1), right
 *     column at (+35, +21) -- half a slot lower, so neighbours do not overlap
 *     (rows 19-21). Both fable windows that get labels (bag, bank) draw the
 *     oracle's own 40x40 slots unscaled, so the offsets apply as measured.
 *   - Style: the client's one tooltip box -- navy fill, white text, one line
 *     (row 22).
 *
 * THE HOVER TOOLTIP is DOCUMENTED, NOT OBSERVED: the client's own
 * help/tips/ctxbank.html says "To see the name of an item, either in your
 * inventory or in the bank, just point your mouse cursor at the item" (row
 * 36), but 14 hover trials on the oracle rig never showed one (rows 1-8, 13,
 * 26, 27, 32). Will ruled it in (2026-10-06: "the hover tooltip should be
 * implemented"), so it takes the parameters the same rig MEASURED for the
 * client's static control tooltips (row 35): shown after the pointer rests
 * kHoverDelayMs on the slot, the box's bottom edge on the pointer's y, kept on
 * screen. Its left edge AT the pointer's x is fable's choice (every measured
 * static tooltip was clamped by the screen edge, so the unclamped x is
 * unknown). Not shown while Alt holds the labels up.
 *
 * No ini key and no Options row governs either (row 37), so nothing persists.
 *
 * Device-free: the frame hands in the panel states and the pointer; the result
 * is a list of boxes the renderer draws as its own top-most UIRenderer layer
 * (B992). No texture: a drawRect() and a text entry per box.
 */

#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include <irrlicht.h>

#include "client/graphics/ui/ui_layout.h"

namespace EQT {
namespace Graphics {

class UIRenderer;
struct InventoryPanelState;
struct BankPanelState;
struct ContainerWindowsState;
struct TradePanelState;

namespace ItemTooltip {

/** Static tooltip dwell, MEASURED 0.54-0.65 s (ledger row 35). */
constexpr uint32_t kHoverDelayMs = 600;

/** Label offsets from a 40x40 slot's top-left (rows 19-21). */
constexpr irr::s32 kLabelOffsetX = 35;
constexpr irr::s32 kLabelOffsetY = 1;
constexpr irr::s32 kLabelRowStagger = 20;   // right column: +21

/** Box padding around the text: the oracle's 18-19 px box over its font. */
constexpr irr::s32 kPadX = 3;
constexpr irr::s32 kPadY = 2;

/** The navy box (row 22's sampled fill, ~RGB 10,8,105) and white text. */
inline irr::video::SColor boxColor() { return irr::video::SColor(255, 10, 8, 105); }
inline irr::video::SColor textColor() { return irr::video::SColor(255, 255, 255, 255); }

}  // namespace ItemTooltip

/** Which window the pointer is over, topmost first. */
enum class TooltipWindow : uint8_t {
    None = 0,
    Inventory,
    Bank,
    Container,
    Trade,
};

/** What is under the pointer: a window and, when on one, an item slot. */
struct TooltipTarget {
    TooltipWindow window = TooltipWindow::None;
    int container = -1;     // ContainerWindowsState index when window == Container
    int slot = -1;          // the window's own slot index, -1 = on no slot
    bool partnerSide = false;  // trade: the partner's column
    bool hasItem = false;
    std::string itemName;

    /** A stable key for the dwell clock: same key, same slot. */
    int64_t key() const;
};

/** Everything the tooltip logic reads in one frame. */
struct ItemTooltipScene {
    const UILayout* layout = nullptr;
    const InventoryPanelState* inventory = nullptr;
    const BankPanelState* bank = nullptr;
    const ContainerWindowsState* containers = nullptr;
    const TradePanelState* trade = nullptr;
    int mouseX = -1;
    int mouseY = -1;
    bool altHeld = false;
    // The pointer is over a window drawn ABOVE every window here (item
    // display, Friends, Options, a tip, the Quantity / confirmation dialogs).
    bool blockedAbove = false;
    // The pointer is over a window drawn above the bank but below the
    // container windows (the trainer, the tracking window).
    bool blockedAboveBank = false;
};

/** One drawn box: its rect and its one line of text. */
struct TooltipBox {
    irr::core::rect<irr::s32> rect;
    std::string text;
};

/** The dwell clock, kept by the frame across frames. */
struct ItemTooltipState {
    int64_t hoverKey = -1;
    uint64_t hoverSinceMs = 0;
};

/** What to draw this frame. */
struct ItemTooltipFrame {
    std::vector<TooltipBox> altLabels;   // the Alt-held labels, slot order
    bool hoverShown = false;
    TooltipBox hover;
};

/** Pixel width of `text` in the UI font. The renderer passes TextBatch's. */
using TooltipTextWidth = irr::s32 (*)(const std::string& text, const void* ctx);

/** The window and slot under the pointer, topmost window first. */
TooltipTarget resolveTooltipTarget(const ItemTooltipScene& scene);

/**
 * The box of one Alt label for a slot at `slot` (screen rect) in column
 * `column` (0 = left) carrying `textWidth` pixels of text on a `lineHeight`
 * line, kept on a screenW x screenH screen.
 */
irr::core::rect<irr::s32> itemLabelRect(const irr::core::rect<irr::s32>& slot, int column,
                                        irr::s32 textWidth, irr::s32 lineHeight,
                                        irr::s32 screenW, irr::s32 screenH);

/** The hover tooltip's box for a pointer at (mouseX, mouseY). */
irr::core::rect<irr::s32> hoverTooltipRect(int mouseX, int mouseY, irr::s32 textWidth,
                                           irr::s32 lineHeight, irr::s32 screenW,
                                           irr::s32 screenH);

/**
 * One frame: advance the dwell clock in `state` and say what to draw. `nowMs`
 * is a monotonic clock; `widthOf(text, widthCtx)` measures; `lineHeight` is
 * the font's.
 */
ItemTooltipFrame updateItemTooltips(ItemTooltipState& state, const ItemTooltipScene& scene,
                                    uint64_t nowMs, TooltipTextWidth widthOf,
                                    const void* widthCtx, irr::s32 lineHeight);

/** Draw the frame's boxes into `ui` (call it on its own top-most layer). */
void renderItemTooltips(UIRenderer& ui, const ItemTooltipFrame& frame);

}  // namespace Graphics
}  // namespace EQT
