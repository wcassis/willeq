// renderer_frame_tooltips.cpp - the item-name tooltips' frame hop (B1004,
// GitLab #323). The rule, the geometry and the draw are item_tooltip.{h,cpp};
// this file only gathers the frame's panel states, the pointer, the Alt key
// and the clock, and draws the result as its own top-most UIRenderer layer.

#include "client/graphics/frame/frame_component.h"
#include "client/graphics/renderer_event_receiver.h"
#include "client/graphics/text_batch.h"
#include "client/graphics/ui/item_tooltip.h"
#include "client/graphics/ui/static_ui_input.h"
#include "client/graphics/ui/ui_renderer.h"

#include <chrono>

namespace EQT {
namespace Graphics {

namespace {

irr::s32 textWidthOf(const std::string& text, const void* ctx) {
    const auto* batch = static_cast<const TextBatch*>(ctx);
    return batch ? batch->getTextWidth(text) : 0;
}

}  // namespace

void FrameComponent::renderItemTooltipLayer() {
    if (!uiRenderer_ || !ctx_.eventReceiver) return;
    const int mx = ctx_.eventReceiver->getMouseX();
    const int my = ctx_.eventReceiver->getMouseY();

    ItemTooltipScene scene;
    scene.layout = &uiLayout_;
    scene.inventory = &inventoryState_;
    scene.bank = &bankState_;
    scene.containers = &containerState_;
    scene.trade = &tradeState_;
    scene.mouseX = mx;
    scene.mouseY = my;
    scene.altHeld = ctx_.eventReceiver->isKeyDown(irr::KEY_LMENU) ||
                    ctx_.eventReceiver->isKeyDown(irr::KEY_RMENU);
    // The windows drawn above every item window (renderStaticUI()'s order):
    // a pointer over one of them is not over an item slot.
    bool dialogUp = false;
    if (staticUIInput_) {
        dialogUp = staticUIInput_->quantityWindow().isOpen ||
                   staticUIInput_->confirmationDialog().isOpen;
    }
    scene.blockedAbove =
        dialogUp || itemDisplayContains(uiLayout_, itemDisplayState_, mx, my) ||
        hitTestFriendsWindow(uiLayout_, friendsState_, mx, my).hit != FriendsHit::None ||
        optionsWindowContains(uiLayout_, optionsState_, mx, my) ||
        hitTestTipWindow(uiLayout_, tipState_, mx, my) != TipHit::None;
    // The trainer and the tracking window sit between the bank and the bags.
    scene.blockedAboveBank = trainerContains(uiLayout_, trainerState_, mx, my) ||
                             trackingContains(uiLayout_, trackingState_, mx, my);

    const uint64_t nowMs = static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count());
    const TextBatch* text = uiRenderer_->getTextBatch();
    const irr::s32 lineH = text ? text->getLineHeight() : 14;
    const ItemTooltipFrame frame =
        updateItemTooltips(itemTooltipState_, scene, nowMs, &textWidthOf, text, lineH);
    renderItemTooltips(uiRenderer_->layer(), frame);
}

}  // namespace Graphics
}  // namespace EQT
