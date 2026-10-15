/*
 * B1025-H2 (GitLab #345) -- the stock Titanium hotbar window. Its own TU
 * (M00), replacing renderHotbar() in static_panels.cpp. Geometry is
 * include/client/graphics/ui/hotbar_layout.h, which quotes
 * EQUI_HotButtonWnd.xml; this file computes no rect.
 */

#include "client/graphics/ui/hotbar_panel.h"

#include <string>

#include "client/graphics/text_batch.h"
#include "client/graphics/ui/hotbar_layout.h"
#include "client/graphics/ui/ui_layout.h"

namespace EQT {
namespace Graphics {

namespace {

const irr::video::SColor kText(255, 225, 225, 225);
const irr::video::SColor kKeyLabel(180, 200, 200, 100);
const irr::video::SColor kFrame(255, 80, 80, 80);
const irr::video::SColor kSlotBg(200, 26, 26, 34);

bool inside(const irr::core::rect<irr::s32>& r, int x, int y) {
    return r.isPointInside(irr::core::vector2di(x, y));
}

// The oracle's A_BtnSmall left/right decal, drawn like friends_panel.cpp's
// buttons: a flat rect under a frame.
void drawPageButton(UIRenderer& ui, TextBatch* tb,
                    const irr::core::rect<irr::s32>& r, const char* glyph) {
    ui.drawRect(r, irr::video::SColor(200, 55, 55, 66));
    ui.drawFrame(r, kFrame);
    if (tb) tb->addTextCentered(glyph, r, kText);
}

}  // namespace

HotbarHit hitTestHotbar(const UILayout& layout, HotbarPanelState& state,
                        int x, int y) {
    HotbarHit hit = HotbarHit::None;
    if (!state.isOpen) return hit;
    const auto window = layout.hotbar;
    if (!inside(window, x, y)) {
        state.closeHovered = false;
        return hit;
    }

    state.closeHovered = inside(HotbarLayout::closeBoxRect(window), x, y);
    if (state.closeHovered) return HotbarHit::Close;
    if (inside(HotbarLayout::pageLeftRect(window), x, y)) return HotbarHit::PageLeft;
    if (inside(HotbarLayout::pageRightRect(window), x, y)) return HotbarHit::PageRight;
    for (int i = 0; i < HotbarPanelState::SLOT_COUNT; ++i) {
        if (inside(HotbarLayout::slotRect(window, i), x, y)) {
            state.hoveredSlot = i;
            return HotbarHit::Slot;
        }
    }
    return HotbarHit::Window;
}

void renderHotbar(UIRenderer& ui, const UILayout& layout,
                  const HotbarPanelState& state) {
    if (!state.isOpen) return;
    auto* tb = ui.getTextBatch();
    auto window = layout.hotbar;

    // WDT_RoundedNoTitle: the whole window, then the page-row strip.
    ui.drawPanel(window);
    auto strip = window;
    strip.LowerRightCorner.Y = window.UpperLeftCorner.Y + HotbarLayout::kCloseBox;
    ui.drawRect(strip, irr::video::SColor(200, 20, 20, 28));

    // HB_PageLeftButton / HB_CurrentPageLabel / HB_PageRightButton. The label
    // is the model's current page, 1-based like the oracle prints it.
    drawPageButton(ui, tb, HotbarLayout::pageLeftRect(window), "<");
    if (tb) {
        tb->addTextCentered(std::to_string(state.currentPage + 1),
                            HotbarLayout::pageLabelRect(window), kText);
    }
    drawPageButton(ui, tb, HotbarLayout::pageRightRect(window), ">");

    const auto close = HotbarLayout::closeBoxRect(window);
    ui.drawRect(close, state.closeHovered ? irr::video::SColor(220, 120, 60, 60)
                                          : irr::video::SColor(200, 60, 60, 72));
    if (tb) tb->addTextCentered("X", close, kText);

    static const char* keyLabels[HotbarPanelState::SLOT_COUNT] = {
        "1", "2", "3", "4", "5", "6", "7", "8", "9", "0"};

    for (int i = 0; i < HotbarPanelState::SLOT_COUNT; ++i) {
        const auto slotRect = HotbarLayout::slotRect(window, i);
        ui.drawRect(slotRect, kSlotBg);
        ui.drawFrame(slotRect, state.hoveredSlot == i
            ? irr::video::SColor(255, 160, 170, 200) : kFrame);

        const auto& slot = state.slots[i];
        if (slot.type == eqt::HotbarSlotType::Empty) {
            // The empty cell: key label only.
            if (tb) tb->addText(keyLabels[i], slotRect.LowerRightCorner.X - 9,
                                slotRect.LowerRightCorner.Y - 12, kKeyLabel);
            continue;
        }

        // Per-kind art. Item slots with a valid icon id draw the dragitem
        // sheets' own decal (UIRenderer::drawItemIcon, ids 500..1723);
        // everything else -- spell (no icon texture in the shipped atlas),
        // skill, emote, macro, combat -- draws its name.
        const bool drewIcon = slot.type == eqt::HotbarSlotType::Item &&
                              slot.iconId != 0 && ui.drawItemIcon(slotRect, slot.iconId);
        if (!drewIcon && tb && !slot.name.empty()) {
            const std::string label = slot.name;
            const std::string abbrev = label.substr(0, 6);
            tb->addTextCentered(abbrev, slotRect, kText);
        }

        // Cooldown overlay (darken the slot), kept from the old render.
        if (slot.isOnCooldown()) {
            float darkPct = 1.0f - slot.getCooldownProgress();
            uint8_t alpha = static_cast<uint8_t>(darkPct * 160);
            ui.drawRect(slotRect, irr::video::SColor(alpha, 0, 0, 0));
        }

        if (tb) {
            tb->addText(keyLabels[i], slotRect.LowerRightCorner.X - 9,
                        slotRect.LowerRightCorner.Y - 12, kKeyLabel);
        }
    }
}

}  // namespace Graphics
}  // namespace EQT
