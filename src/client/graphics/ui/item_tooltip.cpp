// B1004 (GitLab #323): item-name tooltips -- see item_tooltip.h for the
// measured rule and the ledger rows it cites.

#include "client/graphics/ui/item_tooltip.h"

#include <algorithm>

#include "client/graphics/text_batch.h"
#include "client/graphics/ui/bank_layout.h"
#include "client/graphics/ui/bank_panel.h"
#include "client/graphics/ui/container_layout.h"
#include "client/graphics/ui/container_panel.h"
#include "client/graphics/ui/inventory_layout.h"
#include "client/graphics/ui/static_panels.h"
#include "client/graphics/ui/trade_panel.h"
#include "client/graphics/ui/ui_renderer.h"

namespace EQT {
namespace Graphics {

namespace {

bool inside(const irr::core::rect<irr::s32>& r, int x, int y) {
    return x >= r.UpperLeftCorner.X && x < r.LowerRightCorner.X &&
           y >= r.UpperLeftCorner.Y && y < r.LowerRightCorner.Y;
}

// The inventory panel's cache for flat slot `i` (0-21 worn, 22-29 general) --
// the same split renderInventoryPopup() and StaticUIInput make.
const SlotDisplayInfo* inventorySlot(const InventoryPanelState& inv, int i) {
    if (i < 0) return nullptr;
    if (i < InventoryPanelState::EQUIP_SLOTS) return &inv.equipSlots[i];
    const int g = i - InventoryPanelState::EQUIP_SLOTS;
    if (g < InventoryPanelState::GENERAL_SLOTS) return &inv.generalSlots[g];
    return nullptr;
}

template <typename Cell>
void fill(TooltipTarget& t, const Cell& cell) {
    t.hasItem = cell.hasItem;
    if (cell.hasItem) t.itemName = cell.itemName;
}

// Keep a box on screen, preferring to slide it left/up.
irr::core::rect<irr::s32> keepOnScreen(irr::s32 x, irr::s32 y, irr::s32 w, irr::s32 h,
                                       irr::s32 screenW, irr::s32 screenH) {
    if (x + w > screenW) x = screenW - w;
    if (y + h > screenH) y = screenH - h;
    if (x < 0) x = 0;
    if (y < 0) y = 0;
    return irr::core::rect<irr::s32>(x, y, x + w, y + h);
}

// The bank's column for slot i: BW_BankSlot0..3 are the left column, 4..7 the
// right (bank_layout.h's SLOT_X).
int bankColumn(int i) { return BankLayout::SLOT_X[i] >= BankLayout::SLOT_X[4] ? 1 : 0; }

void addLabel(ItemTooltipFrame& out, const irr::core::rect<irr::s32>& slot, int column,
              const std::string& name, TooltipTextWidth widthOf, const void* ctx,
              irr::s32 lineHeight, const UILayout& layout) {
    if (name.empty()) return;
    const irr::s32 w = widthOf ? widthOf(name, ctx) : 0;
    out.altLabels.push_back({itemLabelRect(slot, column, w, lineHeight, layout.screenW,
                                           layout.screenH),
                             name});
}

void bankLabels(ItemTooltipFrame& out, const ItemTooltipScene& s, TooltipTextWidth widthOf,
                const void* ctx, irr::s32 lineHeight) {
    for (int i = 0; i < BankPanelState::SLOT_COUNT; ++i) {
        const BankSlotDisplay& cell = s.bank->slots[i];
        if (!cell.hasItem) continue;
        addLabel(out, BankLayout::slotRect(*s.layout, i), bankColumn(i), cell.itemName,
                 widthOf, ctx, lineHeight, *s.layout);
    }
}

void containerLabels(ItemTooltipFrame& out, const ItemTooltipScene& s, int window,
                     TooltipTextWidth widthOf, const void* ctx, irr::s32 lineHeight) {
    const ContainerWindow& w = s.containers->windows[window];
    const int n = containerVisibleSlots(w);
    for (int i = 0; i < n; ++i) {
        if (!w.slots[i].hasItem) continue;
        // ContainerSlot1..10 are laid out two to a row, left column first.
        addLabel(out, ContainerLayout::slotRect(*s.layout, i), i % 2, w.slots[i].itemName,
                 widthOf, ctx, lineHeight, *s.layout);
    }
}

}  // namespace

int64_t TooltipTarget::key() const {
    if (window == TooltipWindow::None || slot < 0) return -1;
    return (static_cast<int64_t>(window) << 32) |
           (static_cast<int64_t>(container + 1) << 16) |
           (static_cast<int64_t>(partnerSide ? 1 : 0) << 15) | static_cast<int64_t>(slot);
}

TooltipTarget resolveTooltipTarget(const ItemTooltipScene& s) {
    TooltipTarget t;
    if (!s.layout || s.blockedAbove || s.mouseX < 0 || s.mouseY < 0) return t;
    const int mx = s.mouseX, my = s.mouseY;

    // Topmost first: the trade / give window is drawn above the containers.
    if (s.trade && s.trade->isOpen) {
        const TradeHit hit = hitTestTradeWindow(*s.layout, *s.trade, mx, my);
        if (hit.part != TradePart::None) {
            t.window = TooltipWindow::Trade;
            if (hit.part == TradePart::OwnSlot && hit.index >= 0 &&
                hit.index < TradePanelState::SLOT_COUNT) {
                t.slot = hit.index;
                fill(t, s.trade->own[hit.index]);
            } else if (hit.part == TradePart::PartnerSlot && hit.index >= 0 &&
                       hit.index < TradePanelState::SLOT_COUNT) {
                t.slot = hit.index;
                t.partnerSide = true;
                fill(t, s.trade->partner[hit.index]);
            }
            return t;
        }
    }
    if (s.containers) {
        const ContainerHit hit = hitTestContainer(*s.layout, *s.containers, mx, my);
        if (hit.window >= 0) {
            t.window = TooltipWindow::Container;
            t.container = hit.window;
            if (hit.part == ContainerPart::Slot && hit.slot >= 0 &&
                hit.slot < ContainerLayout::SLOT_COUNT) {
                t.slot = hit.slot;
                fill(t, s.containers->windows[hit.window].slots[hit.slot]);
            }
            return t;
        }
    }
    if (s.blockedAboveBank) return t;
    if (s.bank && s.bank->isOpen && bankContains(*s.layout, *s.bank, mx, my)) {
        t.window = TooltipWindow::Bank;
        const int i = hitTestBankSlot(*s.layout, *s.bank, mx, my);
        if (i >= 0 && i < BankPanelState::SLOT_COUNT) {
            t.slot = i;
            fill(t, s.bank->slots[i]);
        }
        return t;
    }
    if (s.inventory && s.inventory->activePopup == PopupType::Inventory &&
        inside(s.layout->centerPopup, mx, my)) {
        t.window = TooltipWindow::Inventory;
        for (int i = 0; i < InventoryLayout::TOTAL_SLOTS; ++i) {
            if (!inside(InventoryLayout::slotRect(*s.layout, i), mx, my)) continue;
            if (const SlotDisplayInfo* cell = inventorySlot(*s.inventory, i)) {
                t.slot = i;
                fill(t, *cell);
            }
            break;
        }
    }
    return t;
}

irr::core::rect<irr::s32> itemLabelRect(const irr::core::rect<irr::s32>& slot, int column,
                                        irr::s32 textWidth, irr::s32 lineHeight,
                                        irr::s32 screenW, irr::s32 screenH) {
    const irr::s32 w = textWidth + 2 * ItemTooltip::kPadX;
    const irr::s32 h = lineHeight + 2 * ItemTooltip::kPadY;
    const irr::s32 x = slot.UpperLeftCorner.X + ItemTooltip::kLabelOffsetX;
    const irr::s32 y = slot.UpperLeftCorner.Y + ItemTooltip::kLabelOffsetY +
                       ((column % 2) ? ItemTooltip::kLabelRowStagger : 0);
    return keepOnScreen(x, y, w, h, screenW, screenH);
}

irr::core::rect<irr::s32> hoverTooltipRect(int mouseX, int mouseY, irr::s32 textWidth,
                                           irr::s32 lineHeight, irr::s32 screenW,
                                           irr::s32 screenH) {
    const irr::s32 w = textWidth + 2 * ItemTooltip::kPadX;
    const irr::s32 h = lineHeight + 2 * ItemTooltip::kPadY;
    // Row 35: the box's bottom edge sits on the pointer's y.
    return keepOnScreen(mouseX, mouseY - h, w, h, screenW, screenH);
}

ItemTooltipFrame updateItemTooltips(ItemTooltipState& state, const ItemTooltipScene& scene,
                                    uint64_t nowMs, TooltipTextWidth widthOf,
                                    const void* widthCtx, irr::s32 lineHeight) {
    ItemTooltipFrame out;
    const TooltipTarget t = resolveTooltipTarget(scene);

    // The dwell clock restarts whenever the pointer reaches a different slot
    // (or leaves every slot), whatever Alt is doing.
    const int64_t key = t.key();
    if (key != state.hoverKey) {
        state.hoverKey = key;
        state.hoverSinceMs = nowMs;
    }

    if (scene.altHeld) {
        // Rows 14-17, 23-24, 28, 30: the window under the pointer decides.
        if (t.window == TooltipWindow::Container) {
            containerLabels(out, scene, t.container, widthOf, widthCtx, lineHeight);
        } else if (scene.bank && scene.bank->isOpen &&
                   (t.window == TooltipWindow::Bank || t.window == TooltipWindow::Inventory)) {
            bankLabels(out, scene, widthOf, widthCtx, lineHeight);
        }
        if (!out.altLabels.empty()) return out;  // the labels replace the hover tooltip
    }

    if (key >= 0 && t.hasItem && !t.itemName.empty() &&
        nowMs - state.hoverSinceMs >= ItemTooltip::kHoverDelayMs) {
        const irr::s32 w = widthOf ? widthOf(t.itemName, widthCtx) : 0;
        out.hoverShown = true;
        out.hover.text = t.itemName;
        out.hover.rect = hoverTooltipRect(scene.mouseX, scene.mouseY, w, lineHeight,
                                          scene.layout->screenW, scene.layout->screenH);
    }
    return out;
}

void renderItemTooltips(UIRenderer& ui, const ItemTooltipFrame& frame) {
    TextBatch* text = ui.getTextBatch();
    auto draw = [&](const TooltipBox& b) {
        ui.drawRect(b.rect, ItemTooltip::boxColor());
        if (text) {
            text->addText(b.text, b.rect.UpperLeftCorner.X + ItemTooltip::kPadX,
                          b.rect.UpperLeftCorner.Y + ItemTooltip::kPadY,
                          ItemTooltip::textColor());
        }
    };
    for (const TooltipBox& b : frame.altLabels) draw(b);
    if (frame.hoverShown) draw(frame.hover);
}

}  // namespace Graphics
}  // namespace EQT
