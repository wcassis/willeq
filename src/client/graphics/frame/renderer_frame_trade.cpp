// renderer_frame_trade.cpp - the trade / give window's click pass (B896,
// GitLab #212).
//
// The window's geometry is trade_layout.h / give_layout.h, its draw and hit
// test trade_panel.cpp, and the hit -> intent resolution
// StaticUIInput::processTradeInput() (trade_input.cpp, beside B442's shared
// slotClick()). This TU is only the seam with the frame: it runs the pass
// with this frame's click state and, when the point is over the window,
// spends the click so no window drawn BELOW it acts on the same press -- the
// inventory popup in particular, which the official client opens beside every
// trade (review/UIGAP-ORACLE/212/trade.tsv row 1) and which overlaps the
// oracle's (0,25) placement at 640x480.

#include "client/graphics/frame/frame_component.h"
#include "client/graphics/irrlicht_renderer.h"
#include "client/graphics/renderer_event_receiver.h"
#include "client/graphics/ui/static_ui_input.h"
#include "client/graphics/ui/trade_panel.h"

namespace EQT {
namespace Graphics {

void FrameComponent::processTradeWindowInput() {
    if (!staticUIInput_ || !ctx_.eventReceiver) return;
    if (!tradeState_.isOpen) {
        tradeState_.hoveredOwnSlot = -1;
        tradeState_.hoveredCoin = -1;
        tradeState_.acceptHovered = tradeState_.cancelHovered = tradeState_.closeHovered = false;
        return;
    }
    const bool over = staticUIInput_->processTradeInput(
        ctx_.eventReceiver->getMouseX(), ctx_.eventReceiver->getMouseY(),
        pendingUIClick_, uiLayout_, tradeState_);
    if (!over) return;
    // Spent: the windows hit-tested after this one are all below it.
    pendingUIClick_ = false;
    eqt::inventory::ClickModifiers mods;
    mods.shift = ctx_.eventReceiver->isKeyDown(irr::KEY_SHIFT);
    mods.ctrl = ctx_.eventReceiver->isKeyDown(irr::KEY_LCONTROL) ||
                ctx_.eventReceiver->isKeyDown(irr::KEY_RCONTROL);
    staticUIInput_->setFrameClickState(false, mods, &cursorState_);
}

}  // namespace Graphics
}  // namespace EQT
