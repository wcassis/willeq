#include "client/graphics/ui/window_base.h"
#include "client/graphics/ui/ui_settings.h"
#include "common/logging.h"
#include <codecvt>
#include <locale>

namespace eqt {
namespace ui {

// Helper to convert wstring to string for logging
static std::string wstringToString(const std::wstring& wstr) {
    if (wstr.empty()) return "";
    std::wstring_convert<std::codecvt_utf8<wchar_t>> converter;
    return converter.to_bytes(wstr);
}

// Legacy color definitions - kept for backwards compatibility
// New code should use the accessor methods that read from UISettings
const irr::video::SColor WindowBase::COLOR_WINDOW_BG(255, 64, 64, 64);
const irr::video::SColor WindowBase::COLOR_TITLE_BAR(255, 48, 48, 48);
const irr::video::SColor WindowBase::COLOR_TITLE_TEXT(255, 200, 180, 140);
const irr::video::SColor WindowBase::COLOR_BORDER_LIGHT(255, 100, 100, 100);
const irr::video::SColor WindowBase::COLOR_BORDER_DARK(255, 32, 32, 32);
const irr::video::SColor WindowBase::COLOR_BUTTON_BG(255, 72, 72, 72);
const irr::video::SColor WindowBase::COLOR_BUTTON_HIGHLIGHT(255, 96, 96, 96);
const irr::video::SColor WindowBase::COLOR_BUTTON_TEXT(255, 200, 200, 200);

// ============================================================================
// Color Accessors - Read from UISettings
// ============================================================================

irr::video::SColor WindowBase::getWindowBackground() const {
    return UISettings::instance().globalTheme().windowBackground;
}

irr::video::SColor WindowBase::getTitleBarBackground() const {
    return UISettings::instance().globalTheme().titleBarBackground;
}

irr::video::SColor WindowBase::getTitleBarTextColor() const {
    return UISettings::instance().globalTheme().titleBarText;
}

irr::video::SColor WindowBase::getBorderLightColor() const {
    return UISettings::instance().globalTheme().borderLight;
}

irr::video::SColor WindowBase::getBorderDarkColor() const {
    return UISettings::instance().globalTheme().borderDark;
}

irr::video::SColor WindowBase::getButtonBackground() const {
    return UISettings::instance().globalTheme().buttonBackground;
}

irr::video::SColor WindowBase::getButtonHighlightColor() const {
    return UISettings::instance().globalTheme().buttonHighlight;
}

irr::video::SColor WindowBase::getButtonTextColor() const {
    return UISettings::instance().globalTheme().buttonText;
}

irr::video::SColor WindowBase::getButtonDisabledBackground() const {
    return UISettings::instance().globalTheme().buttonDisabledBg;
}

irr::video::SColor WindowBase::getButtonDisabledTextColor() const {
    return UISettings::instance().globalTheme().buttonDisabledText;
}

// ============================================================================
// Layout Accessors - Read from UISettings
// ============================================================================

int WindowBase::getTitleBarHeight() const {
    return UISettings::instance().globalTheme().titleBarHeight;
}

int WindowBase::getBorderWidth() const {
    return UISettings::instance().globalTheme().borderWidth;
}

int WindowBase::getButtonHeight() const {
    return UISettings::instance().globalTheme().buttonHeight;
}

int WindowBase::getButtonPadding() const {
    return UISettings::instance().globalTheme().buttonPadding;
}

int WindowBase::getContentPadding() const {
    return UISettings::instance().globalTheme().contentPadding;
}

// ============================================================================
// Lock State
// ============================================================================

bool WindowBase::isLocked() const {
    // Window is locked if:
    // 1. Global UI lock is enabled, OR
    // 2. This window's alwaysLocked flag is set
    return UISettings::instance().isUILocked() || alwaysLocked_;
}

WindowBase::WindowBase(const std::wstring& title, int width, int height)
    : title_(title)
{
    setSize(width, height);
}

void WindowBase::render(irr::video::IVideoDriver* driver,
                        irr::gui::IGUIEnvironment* gui) {
    if (!visible_) {
        return;
    }

    drawWindow(driver);

    // Draw unlock highlight if UI is unlocked and window is hovered/can be moved
    drawUnlockedHighlight(driver);

    if (showTitleBar_) {
        drawTitleBar(driver, gui);
    }
    renderContent(driver, gui);
}

bool WindowBase::handleMouseDown(int x, int y, bool leftButton, bool shift, bool ctrl) {
    if (!visible_ || !leftButton) {
        return false;
    }

    if (!containsPoint(x, y)) {
        return false;
    }

    // Check title bar for dragging (only if not locked)
    if (titleBarContainsPoint(x, y)) {
        if (canMove()) {
            dragging_ = true;
            dragOffset_.X = x - bounds_.UpperLeftCorner.X;
            dragOffset_.Y = y - bounds_.UpperLeftCorner.Y;
        }
        return true;  // Consume click on title bar even if locked
    }

    // For windows without title bar, allow dragging anywhere when UI is unlocked
    if (!showTitleBar_ && canMove()) {
        dragging_ = true;
        dragOffset_.X = x - bounds_.UpperLeftCorner.X;
        dragOffset_.Y = y - bounds_.UpperLeftCorner.Y;
        return true;
    }

    // Return false to allow derived classes to handle content area clicks
    return false;
}

bool WindowBase::handleMouseUp(int x, int y, bool leftButton) {
    if (dragging_ && leftButton) {
        dragging_ = false;
        LOG_INFO(MOD_UI, "Window '{}' moved to position ({}, {}) bounds=({},{} - {},{})",
                 wstringToString(title_),
                 bounds_.UpperLeftCorner.X, bounds_.UpperLeftCorner.Y,
                 bounds_.UpperLeftCorner.X, bounds_.UpperLeftCorner.Y,
                 bounds_.LowerRightCorner.X, bounds_.LowerRightCorner.Y);

        // Save position to settings if this window has a settings key
        if (!settingsKey_.empty()) {
            UISettings::instance().updateWindowPosition(settingsKey_,
                bounds_.UpperLeftCorner.X, bounds_.UpperLeftCorner.Y);
            UISettings::instance().saveIfNeeded();
        }
        return true;
    }
    return false;
}

bool WindowBase::handleMouseMove(int x, int y) {
    if (dragging_) {
        int newX = x - dragOffset_.X;
        int newY = y - dragOffset_.Y;
        setPosition(newX, newY);
        return true;
    }
    return false;
}

void WindowBase::setPosition(int x, int y) {
    int width = bounds_.getWidth();
    int height = bounds_.getHeight();
    bounds_.UpperLeftCorner.X = x;
    bounds_.UpperLeftCorner.Y = y;
    bounds_.LowerRightCorner.X = x + width;
    bounds_.LowerRightCorner.Y = y + height;

    // Update title bar using accessor methods for layout values
    int borderWidth = getBorderWidth();
    int titleBarHeight = getTitleBarHeight();
    titleBar_.UpperLeftCorner.X = x + borderWidth;
    titleBar_.UpperLeftCorner.Y = y + borderWidth;
    titleBar_.LowerRightCorner.X = x + width - borderWidth;
    titleBar_.LowerRightCorner.Y = y + borderWidth + titleBarHeight;
}

void WindowBase::setSize(int width, int height) {
    int x = bounds_.UpperLeftCorner.X;
    int y = bounds_.UpperLeftCorner.Y;
    bounds_.LowerRightCorner.X = x + width;
    bounds_.LowerRightCorner.Y = y + height;

    // Update title bar using accessor methods for layout values
    int borderWidth = getBorderWidth();
    int titleBarHeight = getTitleBarHeight();
    titleBar_.UpperLeftCorner.X = x + borderWidth;
    titleBar_.UpperLeftCorner.Y = y + borderWidth;
    titleBar_.LowerRightCorner.X = x + width - borderWidth;
    titleBar_.LowerRightCorner.Y = y + borderWidth + titleBarHeight;
}

void WindowBase::show() {
    visible_ = true;
}

void WindowBase::hide() {
    visible_ = false;
    dragging_ = false;
}

void WindowBase::setVisible(bool visible) {
    if (visible) {
        show();
    } else {
        hide();
    }
}

void WindowBase::bringToFront() {
    // z-order is managed by WindowManager
}

bool WindowBase::containsPoint(int x, int y) const {
    return bounds_.isPointInside(irr::core::vector2di(x, y));
}

bool WindowBase::titleBarContainsPoint(int x, int y) const {
    if (!showTitleBar_) return false;
    return titleBar_.isPointInside(irr::core::vector2di(x, y));
}

irr::core::recti WindowBase::getContentArea() const {
    int borderWidth = getBorderWidth();
    int contentPadding = getContentPadding();
    int topOffset = borderWidth + contentPadding;
    if (showTitleBar_) {
        topOffset += getTitleBarHeight();
    }
    return irr::core::recti(
        bounds_.UpperLeftCorner.X + borderWidth + contentPadding,
        bounds_.UpperLeftCorner.Y + topOffset,
        bounds_.LowerRightCorner.X - borderWidth - contentPadding,
        bounds_.LowerRightCorner.Y - borderWidth - contentPadding
    );
}

void WindowBase::drawWindow(irr::video::IVideoDriver* driver) {
    // Get colors and layout from UISettings
    irr::video::SColor borderLight = getBorderLightColor();
    irr::video::SColor borderDark = getBorderDarkColor();
    irr::video::SColor windowBg = getWindowBackground();
    int borderWidth = getBorderWidth();

    // Draw outer border (light on top-left, dark on bottom-right for 3D effect)
    // Top border
    driver->draw2DRectangle(borderLight,
        irr::core::recti(bounds_.UpperLeftCorner.X,
                        bounds_.UpperLeftCorner.Y,
                        bounds_.LowerRightCorner.X,
                        bounds_.UpperLeftCorner.Y + borderWidth));
    // Left border
    driver->draw2DRectangle(borderLight,
        irr::core::recti(bounds_.UpperLeftCorner.X,
                        bounds_.UpperLeftCorner.Y,
                        bounds_.UpperLeftCorner.X + borderWidth,
                        bounds_.LowerRightCorner.Y));
    // Bottom border
    driver->draw2DRectangle(borderDark,
        irr::core::recti(bounds_.UpperLeftCorner.X,
                        bounds_.LowerRightCorner.Y - borderWidth,
                        bounds_.LowerRightCorner.X,
                        bounds_.LowerRightCorner.Y));
    // Right border
    driver->draw2DRectangle(borderDark,
        irr::core::recti(bounds_.LowerRightCorner.X - borderWidth,
                        bounds_.UpperLeftCorner.Y,
                        bounds_.LowerRightCorner.X,
                        bounds_.LowerRightCorner.Y));

    // Draw window background
    irr::core::recti bgRect(
        bounds_.UpperLeftCorner.X + borderWidth,
        bounds_.UpperLeftCorner.Y + borderWidth,
        bounds_.LowerRightCorner.X - borderWidth,
        bounds_.LowerRightCorner.Y - borderWidth
    );
    driver->draw2DRectangle(windowBg, bgRect);
}

void WindowBase::drawUnlockedHighlight(irr::video::IVideoDriver* driver) {
    // Only draw highlight if:
    // 1. UI is unlocked (window can be moved)
    // 2. Window is hovered OR being dragged
    if (!canMove()) {
        return;
    }

    if (!hovered_ && !dragging_) {
        return;
    }

    // Draw a bright highlight border around the window
    // Use a yellow/gold color to indicate movable state
    irr::video::SColor highlightColor(200, 255, 200, 0);  // Semi-transparent yellow

    // Draw highlight as a 2px outline around the window
    const int highlightWidth = 2;

    // Top
    driver->draw2DRectangle(highlightColor,
        irr::core::recti(bounds_.UpperLeftCorner.X - highlightWidth,
                        bounds_.UpperLeftCorner.Y - highlightWidth,
                        bounds_.LowerRightCorner.X + highlightWidth,
                        bounds_.UpperLeftCorner.Y));

    // Bottom
    driver->draw2DRectangle(highlightColor,
        irr::core::recti(bounds_.UpperLeftCorner.X - highlightWidth,
                        bounds_.LowerRightCorner.Y,
                        bounds_.LowerRightCorner.X + highlightWidth,
                        bounds_.LowerRightCorner.Y + highlightWidth));

    // Left
    driver->draw2DRectangle(highlightColor,
        irr::core::recti(bounds_.UpperLeftCorner.X - highlightWidth,
                        bounds_.UpperLeftCorner.Y,
                        bounds_.UpperLeftCorner.X,
                        bounds_.LowerRightCorner.Y));

    // Right
    driver->draw2DRectangle(highlightColor,
        irr::core::recti(bounds_.LowerRightCorner.X,
                        bounds_.UpperLeftCorner.Y,
                        bounds_.LowerRightCorner.X + highlightWidth,
                        bounds_.LowerRightCorner.Y));
}

void WindowBase::drawTitleBar(irr::video::IVideoDriver* driver,
                              irr::gui::IGUIEnvironment* gui) {
    // Get colors from UISettings
    irr::video::SColor titleBarBg = getTitleBarBackground();
    irr::video::SColor borderDark = getBorderDarkColor();
    irr::video::SColor titleTextColor = getTitleBarTextColor();

    // Draw title bar background
    driver->draw2DRectangle(titleBarBg, titleBar_);

    // Draw title bar bottom border
    driver->draw2DRectangle(borderDark,
        irr::core::recti(titleBar_.UpperLeftCorner.X,
                        titleBar_.LowerRightCorner.Y - 1,
                        titleBar_.LowerRightCorner.X,
                        titleBar_.LowerRightCorner.Y));

    // Draw title text centered
    if (!title_.empty()) {
        irr::gui::IGUIFont* font = gui->getBuiltInFont();
        if (font) {
            irr::core::dimension2du textSize = font->getDimension(title_.c_str());
            int textX = titleBar_.UpperLeftCorner.X +
                       (titleBar_.getWidth() - textSize.Width) / 2;
            int textY = titleBar_.UpperLeftCorner.Y +
                       (titleBar_.getHeight() - textSize.Height) / 2;
            font->draw(title_.c_str(),
                      irr::core::recti(textX, textY,
                                      textX + textSize.Width,
                                      textY + textSize.Height),
                      titleTextColor);
        }
    }
}

void WindowBase::drawButton(irr::video::IVideoDriver* driver,
                           irr::gui::IGUIEnvironment* gui,
                           const irr::core::recti& buttonBounds,
                           const std::wstring& text,
                           bool highlighted,
                           bool disabled) {
    // Get colors from UISettings
    irr::video::SColor buttonBg = getButtonBackground();
    irr::video::SColor buttonHighlight = getButtonHighlightColor();
    irr::video::SColor buttonDisabledBg = getButtonDisabledBackground();
    irr::video::SColor borderLight = getBorderLightColor();
    irr::video::SColor borderDark = getBorderDarkColor();
    irr::video::SColor buttonText = getButtonTextColor();
    irr::video::SColor buttonDisabledText = getButtonDisabledTextColor();

    // Draw button background
    irr::video::SColor bgColor;
    if (disabled) {
        bgColor = buttonDisabledBg;
    } else if (highlighted) {
        bgColor = buttonHighlight;
    } else {
        bgColor = buttonBg;
    }
    driver->draw2DRectangle(bgColor, buttonBounds);

    // Draw button border (3D effect)
    irr::video::SColor topLeft, bottomRight;
    if (disabled) {
        // Use slightly different shades for disabled state
        topLeft = irr::video::SColor(255, 64, 64, 64);
        bottomRight = irr::video::SColor(255, 32, 32, 32);
    } else if (highlighted) {
        // Inverted for pressed appearance
        topLeft = borderDark;
        bottomRight = borderLight;
    } else {
        topLeft = borderLight;
        bottomRight = borderDark;
    }

    // Top
    driver->draw2DRectangle(topLeft,
        irr::core::recti(buttonBounds.UpperLeftCorner.X,
                        buttonBounds.UpperLeftCorner.Y,
                        buttonBounds.LowerRightCorner.X,
                        buttonBounds.UpperLeftCorner.Y + 1));
    // Left
    driver->draw2DRectangle(topLeft,
        irr::core::recti(buttonBounds.UpperLeftCorner.X,
                        buttonBounds.UpperLeftCorner.Y,
                        buttonBounds.UpperLeftCorner.X + 1,
                        buttonBounds.LowerRightCorner.Y));
    // Bottom
    driver->draw2DRectangle(bottomRight,
        irr::core::recti(buttonBounds.UpperLeftCorner.X,
                        buttonBounds.LowerRightCorner.Y - 1,
                        buttonBounds.LowerRightCorner.X,
                        buttonBounds.LowerRightCorner.Y));
    // Right
    driver->draw2DRectangle(bottomRight,
        irr::core::recti(buttonBounds.LowerRightCorner.X - 1,
                        buttonBounds.UpperLeftCorner.Y,
                        buttonBounds.LowerRightCorner.X,
                        buttonBounds.LowerRightCorner.Y));

    // Draw button text centered
    if (!text.empty()) {
        irr::gui::IGUIFont* font = gui->getBuiltInFont();
        if (font) {
            irr::core::dimension2du textSize = font->getDimension(text.c_str());
            int textX = buttonBounds.UpperLeftCorner.X +
                       (buttonBounds.getWidth() - textSize.Width) / 2;
            int textY = buttonBounds.UpperLeftCorner.Y +
                       (buttonBounds.getHeight() - textSize.Height) / 2;
            // Use grayed text for disabled buttons
            irr::video::SColor textColor = disabled ? buttonDisabledText : buttonText;
            font->draw(text.c_str(),
                      irr::core::recti(textX, textY,
                                      textX + textSize.Width,
                                      textY + textSize.Height),
                      textColor);
        }
    }
}

} // namespace ui
} // namespace eqt
