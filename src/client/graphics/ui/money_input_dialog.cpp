#include "client/graphics/ui/money_input_dialog.h"
#include "common/logging.h"
#include <sstream>
#include <algorithm>

namespace eqt {
namespace ui {

MoneyInputDialog::MoneyInputDialog()
    : WindowBase(L"Select Amount", DIALOG_WIDTH, DIALOG_HEIGHT) {
    // Center the dialog on screen (will be repositioned when shown)
    setPosition(400, 300);

    // Dialog is not draggable
    calculateLayout();
}

void MoneyInputDialog::calculateLayout() {
    int borderWidth = getBorderWidth();
    int titleBarHeight = getTitleBarHeight();
    int buttonHeight = getButtonHeight();

    int contentY = borderWidth + titleBarHeight + PADDING;
    int contentWidth = DIALOG_WIDTH - borderWidth * 2 - PADDING * 2;

    // Slider track bounds (after label)
    int sliderY = contentY + LABEL_HEIGHT + 5;
    sliderTrackBounds_ = irr::core::recti(
        borderWidth + PADDING,
        sliderY,
        borderWidth + PADDING + contentWidth,
        sliderY + SLIDER_HEIGHT
    );

    // Input field bounds (after slider)
    int inputY = sliderY + SLIDER_HEIGHT + 10;
    int inputX = borderWidth + PADDING + (contentWidth - INPUT_WIDTH) / 2;
    inputFieldBounds_ = irr::core::recti(
        inputX,
        inputY,
        inputX + INPUT_WIDTH,
        inputY + INPUT_HEIGHT
    );
}

void MoneyInputDialog::show(CurrencyType type, uint32_t maxAmount) {
    currencyType_ = type;
    maxAmount_ = maxAmount;
    selectedAmount_ = maxAmount;  // Default to max
    sliderPosition_ = 1.0f;
    inputText_ = std::to_wstring(selectedAmount_);
    inputActive_ = true;  // Auto-focus the input field
    isShown_ = true;

    // Update title
    std::wstring title = L"Select ";
    title += getCurrencyName();
    title += L" Amount";
    setTitle(title);

    WindowBase::show();
    LOG_DEBUG(MOD_UI, "MoneyInputDialog shown for {} (max {})",
              static_cast<int>(type), maxAmount);
}

void MoneyInputDialog::dismiss() {
    isShown_ = false;
    draggingSlider_ = false;
    inputActive_ = false;
    WindowBase::hide();
}

std::wstring MoneyInputDialog::getCurrencyName() const {
    switch (currencyType_) {
        case CurrencyType::Platinum: return L"Platinum";
        case CurrencyType::Gold: return L"Gold";
        case CurrencyType::Silver: return L"Silver";
        case CurrencyType::Copper: return L"Copper";
    }
    return L"Currency";
}

irr::video::SColor MoneyInputDialog::getCurrencyColor() const {
    switch (currencyType_) {
        case CurrencyType::Platinum: return irr::video::SColor(255, 180, 180, 220);
        case CurrencyType::Gold: return irr::video::SColor(255, 255, 215, 0);
        case CurrencyType::Silver: return irr::video::SColor(255, 192, 192, 192);
        case CurrencyType::Copper: return irr::video::SColor(255, 184, 115, 51);
    }
    return irr::video::SColor(255, 200, 200, 200);
}

void MoneyInputDialog::updateSliderFromAmount() {
    if (maxAmount_ > 0) {
        sliderPosition_ = static_cast<float>(selectedAmount_) / static_cast<float>(maxAmount_);
    } else {
        sliderPosition_ = 0.0f;
    }
    sliderPosition_ = std::clamp(sliderPosition_, 0.0f, 1.0f);
}

void MoneyInputDialog::updateAmountFromSlider() {
    selectedAmount_ = static_cast<uint32_t>(sliderPosition_ * maxAmount_ + 0.5f);
    selectedAmount_ = std::min(selectedAmount_, maxAmount_);
    inputText_ = std::to_wstring(selectedAmount_);
}

bool MoneyInputDialog::handleMouseDown(int x, int y, bool leftButton, bool shift, bool ctrl) {
    if (!visible_ || !leftButton) {
        return false;
    }

    if (!containsPoint(x, y)) {
        return false;
    }

    // Check title bar for dragging
    if (titleBarContainsPoint(x, y)) {
        dragging_ = true;
        dragOffset_.X = x - bounds_.UpperLeftCorner.X;
        dragOffset_.Y = y - bounds_.UpperLeftCorner.Y;
        return true;
    }

    int relX = x - bounds_.UpperLeftCorner.X;
    int relY = y - bounds_.UpperLeftCorner.Y;

    // Check buttons
    ButtonId button = getButtonAtPosition(x, y);
    if (button != ButtonId::None) {
        switch (button) {
            case ButtonId::OK:
                LOG_DEBUG(MOD_UI, "MoneyInputDialog OK clicked, amount={}", selectedAmount_);
                if (onConfirm_ && selectedAmount_ > 0) {
                    onConfirm_(currencyType_, selectedAmount_);
                }
                dismiss();
                break;
            case ButtonId::Cancel:
                LOG_DEBUG(MOD_UI, "MoneyInputDialog Cancel clicked");
                dismiss();
                break;
            default:
                break;
        }
        return true;
    }

    // Check slider
    if (sliderTrackBounds_.isPointInside(irr::core::vector2di(relX, relY))) {
        draggingSlider_ = true;
        // Update position based on click
        int trackWidth = sliderTrackBounds_.getWidth() - SLIDER_HANDLE_WIDTH;
        int clickX = relX - sliderTrackBounds_.UpperLeftCorner.X - SLIDER_HANDLE_WIDTH / 2;
        sliderPosition_ = static_cast<float>(clickX) / static_cast<float>(trackWidth);
        sliderPosition_ = std::clamp(sliderPosition_, 0.0f, 1.0f);
        updateAmountFromSlider();
        inputActive_ = false;
        return true;
    }

    // Check input field
    if (inputFieldBounds_.isPointInside(irr::core::vector2di(relX, relY))) {
        inputActive_ = true;
        return true;
    }

    inputActive_ = false;
    return true;
}

bool MoneyInputDialog::handleMouseUp(int x, int y, bool leftButton) {
    draggingSlider_ = false;
    return WindowBase::handleMouseUp(x, y, leftButton);
}

bool MoneyInputDialog::handleMouseMove(int x, int y) {
    if (dragging_) {
        return WindowBase::handleMouseMove(x, y);
    }

    if (draggingSlider_) {
        int relX = x - bounds_.UpperLeftCorner.X;
        int trackWidth = sliderTrackBounds_.getWidth() - SLIDER_HANDLE_WIDTH;
        int clickX = relX - sliderTrackBounds_.UpperLeftCorner.X - SLIDER_HANDLE_WIDTH / 2;
        sliderPosition_ = static_cast<float>(clickX) / static_cast<float>(trackWidth);
        sliderPosition_ = std::clamp(sliderPosition_, 0.0f, 1.0f);
        updateAmountFromSlider();
        return true;
    }

    if (!visible_ || !containsPoint(x, y)) {
        hoveredButton_ = ButtonId::None;
        return false;
    }

    hoveredButton_ = getButtonAtPosition(x, y);
    return true;
}

bool MoneyInputDialog::handleKeyInput(wchar_t key, bool isBackspace, bool isEnter, bool isEscape) {
    if (!isShown_) {
        return false;
    }

    // Escape removes focus from input field
    if (isEscape) {
        if (inputActive_) {
            inputActive_ = false;
            return true;
        }
        return false;  // Let caller handle ESC if input not active
    }

    if (!inputActive_) {
        return false;
    }

    if (isEnter) {
        // Parse input, confirm and dismiss (same as OK button)
        try {
            uint32_t value = static_cast<uint32_t>(std::stoul(std::wstring(inputText_)));
            selectedAmount_ = std::min(value, maxAmount_);
        } catch (...) {
            selectedAmount_ = 0;
        }
        LOG_DEBUG(MOD_UI, "MoneyInputDialog Enter pressed, amount={}", selectedAmount_);
        if (onConfirm_ && selectedAmount_ > 0) {
            onConfirm_(currencyType_, selectedAmount_);
        }
        dismiss();
        return true;
    }

    if (isBackspace) {
        if (!inputText_.empty()) {
            inputText_.pop_back();
        }
    } else if (key >= L'0' && key <= L'9') {
        // Limit input length
        if (inputText_.length() < 10) {
            inputText_ += key;
        }
    }

    // Update amount from input
    try {
        uint32_t value = inputText_.empty() ? 0 : static_cast<uint32_t>(std::stoul(std::wstring(inputText_)));
        selectedAmount_ = std::min(value, maxAmount_);
        updateSliderFromAmount();
    } catch (...) {
        // Invalid input, keep current amount
    }

    return true;
}

MoneyInputDialog::ButtonId MoneyInputDialog::getButtonAtPosition(int x, int y) const {
    irr::core::recti okBounds = getButtonBounds(ButtonId::OK);
    irr::core::recti cancelBounds = getButtonBounds(ButtonId::Cancel);

    // Offset to screen coordinates
    int offsetX = bounds_.UpperLeftCorner.X;
    int offsetY = bounds_.UpperLeftCorner.Y;

    okBounds += irr::core::vector2di(offsetX, offsetY);
    cancelBounds += irr::core::vector2di(offsetX, offsetY);

    irr::core::vector2di point(x, y);
    if (okBounds.isPointInside(point)) {
        return ButtonId::OK;
    }
    if (cancelBounds.isPointInside(point)) {
        return ButtonId::Cancel;
    }
    return ButtonId::None;
}

irr::core::recti MoneyInputDialog::getButtonBounds(ButtonId button) const {
    int borderWidth = getBorderWidth();
    int buttonHeight = getButtonHeight();

    int buttonY = DIALOG_HEIGHT - borderWidth - PADDING - buttonHeight;
    int totalButtonWidth = BUTTON_WIDTH * 2 + BUTTON_SPACING;
    int startX = (DIALOG_WIDTH - totalButtonWidth) / 2;

    switch (button) {
        case ButtonId::OK:
            return irr::core::recti(
                startX, buttonY,
                startX + BUTTON_WIDTH, buttonY + buttonHeight
            );
        case ButtonId::Cancel:
            return irr::core::recti(
                startX + BUTTON_WIDTH + BUTTON_SPACING, buttonY,
                startX + BUTTON_WIDTH * 2 + BUTTON_SPACING, buttonY + buttonHeight
            );
        default:
            return irr::core::recti();
    }
}

void MoneyInputDialog::renderContent(irr::video::IVideoDriver* driver,
                                     irr::gui::IGUIEnvironment* gui) {
    int offsetX = bounds_.UpperLeftCorner.X;
    int offsetY = bounds_.UpperLeftCorner.Y;
    int borderWidth = getBorderWidth();
    int titleBarHeight = getTitleBarHeight();

    irr::gui::IGUIFont* font = gui->getBuiltInFont();
    if (!font) {
        return;
    }

    irr::video::SColor textColor(255, 200, 200, 200);
    irr::video::SColor currencyColor = getCurrencyColor();

    // Label showing max amount
    int labelY = offsetY + borderWidth + titleBarHeight + PADDING;
    std::wstringstream labelSS;
    labelSS << L"Available: " << maxAmount_;
    irr::core::recti labelRect(
        offsetX + borderWidth + PADDING,
        labelY,
        offsetX + DIALOG_WIDTH - borderWidth - PADDING,
        labelY + LABEL_HEIGHT
    );
    font->draw(labelSS.str().c_str(), labelRect, currencyColor, false, false, &labelRect);

    // Slider track
    irr::core::recti trackRect(
        sliderTrackBounds_.UpperLeftCorner.X + offsetX,
        sliderTrackBounds_.UpperLeftCorner.Y + offsetY,
        sliderTrackBounds_.LowerRightCorner.X + offsetX,
        sliderTrackBounds_.LowerRightCorner.Y + offsetY
    );
    driver->draw2DRectangle(irr::video::SColor(255, 60, 60, 60), trackRect);
    driver->draw2DRectangleOutline(trackRect, irr::video::SColor(255, 100, 100, 100));

    // Slider fill (up to handle position)
    int trackWidth = sliderTrackBounds_.getWidth() - SLIDER_HANDLE_WIDTH;
    int handleX = sliderTrackBounds_.UpperLeftCorner.X + static_cast<int>(sliderPosition_ * trackWidth);
    irr::core::recti fillRect(
        trackRect.UpperLeftCorner.X,
        trackRect.UpperLeftCorner.Y,
        handleX + offsetX + SLIDER_HANDLE_WIDTH / 2,
        trackRect.LowerRightCorner.Y
    );
    driver->draw2DRectangle(currencyColor, fillRect);

    // Slider handle
    irr::core::recti handleRect(
        handleX + offsetX,
        trackRect.UpperLeftCorner.Y - 2,
        handleX + offsetX + SLIDER_HANDLE_WIDTH,
        trackRect.LowerRightCorner.Y + 2
    );
    driver->draw2DRectangle(irr::video::SColor(255, 180, 180, 180), handleRect);
    driver->draw2DRectangleOutline(handleRect, irr::video::SColor(255, 220, 220, 220));

    // Input field
    irr::core::recti inputRect(
        inputFieldBounds_.UpperLeftCorner.X + offsetX,
        inputFieldBounds_.UpperLeftCorner.Y + offsetY,
        inputFieldBounds_.LowerRightCorner.X + offsetX,
        inputFieldBounds_.LowerRightCorner.Y + offsetY
    );
    irr::video::SColor inputBg = inputActive_ ?
        irr::video::SColor(255, 40, 40, 60) : irr::video::SColor(255, 30, 30, 40);
    driver->draw2DRectangle(inputBg, inputRect);
    driver->draw2DRectangleOutline(inputRect,
        inputActive_ ? irr::video::SColor(255, 100, 100, 150) : irr::video::SColor(255, 80, 80, 80));

    // Input text
    std::wstring displayText = inputText_;
    if (inputActive_) {
        displayText += L"_";  // Cursor
    }
    irr::core::recti textRect = inputRect;
    textRect.UpperLeftCorner.X += 4;
    font->draw(displayText.c_str(), textRect, textColor, false, true, &textRect);

    // Buttons
    irr::core::recti okBounds = getButtonBounds(ButtonId::OK);
    okBounds += irr::core::vector2di(offsetX, offsetY);
    drawButton(driver, gui, okBounds, L"OK", hoveredButton_ == ButtonId::OK);

    irr::core::recti cancelBounds = getButtonBounds(ButtonId::Cancel);
    cancelBounds += irr::core::vector2di(offsetX, offsetY);
    drawButton(driver, gui, cancelBounds, L"Cancel", hoveredButton_ == ButtonId::Cancel);
}

} // namespace ui
} // namespace eqt
