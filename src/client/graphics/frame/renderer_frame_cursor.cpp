// renderer_frame_cursor.cpp - the click-to-cursor gesture's per-frame seams in
// FrameComponent (B442b, GitLab #151 remainder).
//
// StaticUIInput resolves every click on a static window; three things are
// not a click on a window and live here instead, beside the event receiver
// and the panel states only FrameComponent owns:
//
//   - the WORLD click with an item held (rows 31-33). The official client
//     asks "ARE YOU SURE you wish to drop your %1 on the ground?" (eqstr 9057)
//     only for a MOTIONLESS click: a press that moves before its release is a
//     camera drag and does nothing (G39, G40). So a press on the world is
//     ARMED here and the prompt is requested on its release, at the press's
//     own pixel. The prompt itself is composed on the game thread
//     (CursorGestureOp::RequestDrop -> ConfirmationRequested).
//   - the item display's two lifetimes (rows 24-26): a RIGHT press shows it
//     only while the button is held; ALT+LEFT keeps it until Escape.
//   - the QuantityWnd box's keyboard (row 5), the friends window's gate shape.

#include "client/graphics/frame/frame_component.h"
#include "client/graphics/irrlicht_renderer.h"
#include "client/graphics/renderer_event_receiver.h"
#include "client/graphics/ui/confirmation_layout.h"
#include "client/graphics/ui/friends_panel.h"
#include "client/graphics/ui/item_display_panel.h"
#include "client/graphics/ui/quantity_layout.h"
#include "client/graphics/ui/static_ui_input.h"
#include "client/events/renderer_intents.h"
#include "client/bridge/game_state_bridge.h"
#include "common/logging.h"

namespace EQT {
namespace Graphics {

bool FrameComponent::pointOverSelfConsumingWindow(int x, int y) const {
    const irr::core::position2di p(x, y);
    if (staticUIInput_) {
        if (staticUIInput_->confirmationDialog().isOpen &&
            ConfirmationLayout::windowRect(uiLayout_).isPointInside(p)) return true;
        if (staticUIInput_->quantityWindow().isOpen &&
            QuantityLayout::windowRect(uiLayout_).isPointInside(p)) return true;
    }
    if (hitTestFriendsWindow(uiLayout_, friendsState_, x, y).hit != FriendsHit::None) return true;
    // The always-drawn HUD panels: a click on them is not a click on the
    // ground, whether or not their entry point reports it.
    return uiLayout_.chatPanel.isPointInside(p) || uiLayout_.playerStatus.isPointInside(p) ||
           uiLayout_.hotbar.isPointInside(p) || uiLayout_.spellGems.isPointInside(p) ||
           uiLayout_.xpBar.isPointInside(p);
}

void FrameComponent::processCursorGestureFrame(bool uiTook) {
    if (!staticUIInput_ || !ctx_.eventReceiver) return;

    // --- rows 24-26: the item display's lifetime ---
    switch (staticUIInput_->takeDisplayRequest()) {
        case StaticUIInput::DisplayRequest::Held:
            noteItemDisplayRequest(itemDisplayState_, /*held=*/true);
            break;
        case StaticUIInput::DisplayRequest::Kept:
            noteItemDisplayRequest(itemDisplayState_, /*held=*/false);
            break;
        case StaticUIInput::DisplayRequest::None:
        default:
            break;
    }
    updateHeldItemDisplay(itemDisplayState_, ctx_.eventReceiver->isRightButtonDown());

    // --- rows 31-33: a motionless world click with an item held ---
    const bool dialogUp = staticUIInput_->confirmationDialog().isOpen ||
                          staticUIInput_->quantityWindow().isOpen;
    if (pendingUIClick_) {
        const int px = ctx_.eventReceiver->getClickMouseX();
        const int py = ctx_.eventReceiver->getClickMouseY();
        worldDropArmed_ = !uiTook && !dialogUp && cursorState_.hasItem &&
                          !pointOverSelfConsumingWindow(px, py);
        worldDropX_ = px;
        worldDropY_ = py;
    }
    if (worldDropArmed_ && pendingUIRelease_) {
        worldDropArmed_ = false;
        const bool motionless = ctx_.eventReceiver->getMouseX() == worldDropX_ &&
                                ctx_.eventReceiver->getMouseY() == worldDropY_;
        if (motionless && cursorState_.hasItem && ctx_.bridge) {
            eqt::events::CursorGestureIntent ask;
            ask.op = eqt::events::CursorGestureOp::RequestDrop;
            ctx_.bridge->pushIntent(ask);
            LOG_DEBUG(MOD_UI, "World click with '{}' held: asking to drop it",
                      cursorState_.itemName);
        }
    }
    pendingUIRelease_ = false;
}

bool FrameComponent::processQuantityKeyInput() {
    QuantityWindowState* q = staticUIInput_ ? &staticUIInput_->quantityWindow() : nullptr;
    const bool active = q != nullptr && q->isOpen && q->input.inputActive;
    if (!active) {
        if (quantityGateHeld_) {
            quantityGateHeld_ = false;
            ctx_.eventReceiver->setChatInputFocused(false);
        }
        return false;
    }
    if (!quantityGateHeld_) {
        quantityGateHeld_ = true;
        ctx_.eventReceiver->setChatInputFocused(true);
        ctx_.eventReceiver->clearPendingKeyEvents();
        return true;
    }
    while (ctx_.eventReceiver->hasPendingKeyEvents()) {
        auto ke = ctx_.eventReceiver->popKeyEvent();
        staticUIInput_->processQuantityKey(ke.key, ke.character);
        if (!q->isOpen || !q->input.inputActive) break;
    }
    return true;
}

}  // namespace Graphics
}  // namespace EQT
