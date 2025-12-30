#include "client/graphics/ui/trade_request_dialog.h"
#include "common/logging.h"

namespace eqt {
namespace ui {

TradeRequestDialog::TradeRequestDialog()
    : WindowBase(L"Trade Request", DIALOG_WIDTH, DIALOG_HEIGHT)
{
    // Dialog doesn't show title bar - it's a simple popup
    setShowTitleBar(false);
    calculateLayout();
}

void TradeRequestDialog::calculateLayout() {
    // Simple centered dialog
    setSize(DIALOG_WIDTH, DIALOG_HEIGHT);
}

void TradeRequestDialog::show(uint32_t spawnId, const std::string& playerName) {
    requesterSpawnId_ = spawnId;
    requesterName_ = playerName;
    isShown_ = true;

    // Update title to include player name
    std::wstring title = L"Trade Request from ";
    title += std::wstring(playerName.begin(), playerName.end());
    setTitle(title);

    WindowBase::show();
    LOG_INFO(MOD_UI, "Trade request dialog shown for {} (spawn {})", playerName, spawnId);
}

void TradeRequestDialog::dismiss() {
    isShown_ = false;
    requesterSpawnId_ = 0;
    requesterName_.clear();
    hoveredButton_ = ButtonId::None;

    WindowBase::hide();
    LOG_DEBUG(MOD_UI, "Trade request dialog dismissed");
}

bool TradeRequestDialog::handleMouseDown(int x, int y, bool leftButton, bool shift, bool ctrl) {
    if (!visible_ || !leftButton) {
        return false;
    }

    if (!containsPoint(x, y)) {
        return false;
    }

    // Check buttons
    ButtonId button = getButtonAtPosition(x, y);
    if (button != ButtonId::None) {
        switch (button) {
            case ButtonId::Accept:
                LOG_DEBUG(MOD_UI, "Trade request Accept clicked");
                if (onAccept_) {
                    onAccept_();
                }
                dismiss();
                break;
            case ButtonId::Decline:
                LOG_DEBUG(MOD_UI, "Trade request Decline clicked");
                if (onDecline_) {
                    onDecline_();
                }
                dismiss();
                break;
            default:
                break;
        }
        return true;
    }

    return true;  // Consume click
}

bool TradeRequestDialog::handleMouseMove(int x, int y) {
    if (!visible_ || !containsPoint(x, y)) {
        hoveredButton_ = ButtonId::None;
        return false;
    }

    hoveredButton_ = getButtonAtPosition(x, y);
    return true;
}

TradeRequestDialog::ButtonId TradeRequestDialog::getButtonAtPosition(int x, int y) const {
    for (int i = 0; i <= static_cast<int>(ButtonId::Decline); i++) {
        ButtonId btn = static_cast<ButtonId>(i);
        irr::core::recti bounds = getButtonBounds(btn);
        if (bounds.isPointInside(irr::core::vector2di(x, y))) {
            return btn;
        }
    }
    return ButtonId::None;
}

irr::core::recti TradeRequestDialog::getButtonBounds(ButtonId button) const {
    int windowX = bounds_.UpperLeftCorner.X;
    int windowY = bounds_.UpperLeftCorner.Y;
    int buttonHeight = getButtonHeight();

    // Buttons are at the bottom
    int buttonY = bounds_.LowerRightCorner.Y - PADDING - buttonHeight;

    // Center the buttons
    int totalButtonWidth = BUTTON_WIDTH * 2 + BUTTON_SPACING;
    int startX = windowX + (bounds_.getWidth() - totalButtonWidth) / 2;

    switch (button) {
        case ButtonId::Accept: {
            int x = startX;
            return irr::core::recti(x, buttonY, x + BUTTON_WIDTH, buttonY + buttonHeight);
        }
        case ButtonId::Decline: {
            int x = startX + BUTTON_WIDTH + BUTTON_SPACING;
            return irr::core::recti(x, buttonY, x + BUTTON_WIDTH, buttonY + buttonHeight);
        }
        default:
            return irr::core::recti();
    }
}

void TradeRequestDialog::renderContent(irr::video::IVideoDriver* driver,
                                        irr::gui::IGUIEnvironment* gui) {
    int offsetX = bounds_.UpperLeftCorner.X;
    int offsetY = bounds_.UpperLeftCorner.Y;

    // Draw message
    std::wstring message = std::wstring(requesterName_.begin(), requesterName_.end());
    message += L" wants to trade";

    int messageY = offsetY + PADDING;
    irr::core::recti messageRect(offsetX + PADDING, messageY,
                                  bounds_.LowerRightCorner.X - PADDING,
                                  messageY + MESSAGE_HEIGHT);
    gui->getBuiltInFont()->draw(message.c_str(), messageRect,
                                irr::video::SColor(255, 255, 255, 255),
                                true, true, &messageRect);

    // Draw buttons
    bool acceptHighlighted = (hoveredButton_ == ButtonId::Accept);
    irr::core::recti acceptBounds = getButtonBounds(ButtonId::Accept);
    drawButton(driver, gui, acceptBounds, L"Accept", acceptHighlighted);

    bool declineHighlighted = (hoveredButton_ == ButtonId::Decline);
    irr::core::recti declineBounds = getButtonBounds(ButtonId::Decline);
    drawButton(driver, gui, declineBounds, L"Decline", declineHighlighted);
}

} // namespace ui
} // namespace eqt
