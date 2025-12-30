/*
 * WillEQ Note Window Implementation
 */

#include "client/graphics/ui/note_window.h"
#include <algorithm>
#include <codecvt>
#include <locale>

namespace eqt {
namespace ui {

// ============================================================================
// Color Definitions
// ============================================================================

irr::video::SColor NoteWindow::getTextColor() const {
    return irr::video::SColor(255, 220, 200, 170);  // Parchment-like text color
}

irr::video::SColor NoteWindow::getTextBackground() const {
    return irr::video::SColor(255, 35, 30, 25);  // Dark parchment background
}

irr::video::SColor NoteWindow::getScrollbarBackground() const {
    return irr::video::SColor(255, 30, 30, 40);
}

irr::video::SColor NoteWindow::getScrollbarThumb() const {
    return irr::video::SColor(255, 100, 100, 120);
}

// ============================================================================
// Constructor
// ============================================================================

NoteWindow::NoteWindow()
    : WindowBase(L"Note", WINDOW_WIDTH, WINDOW_HEIGHT)
{
    initializeLayout();
}

void NoteWindow::initializeLayout()
{
    int borderWidth = getBorderWidth();
    int contentPadding = getContentPadding();
    int titleBarHeight = getTitleBarHeight();

    // Calculate content area dimensions (relative to window)
    int contentWidth = WINDOW_WIDTH - 2 * (borderWidth + contentPadding);
    int contentHeight = WINDOW_HEIGHT - titleBarHeight - 2 * (borderWidth + contentPadding);

    // Text area (left side, excluding scrollbar)
    textBounds_ = irr::core::recti(
        TEXT_PADDING, 0,
        contentWidth - SCROLLBAR_WIDTH - TEXT_PADDING, contentHeight - BUTTON_AREA_HEIGHT
    );

    // Scrollbar area
    scrollbarBounds_ = irr::core::recti(
        contentWidth - SCROLLBAR_WIDTH, 0,
        contentWidth, contentHeight - BUTTON_AREA_HEIGHT
    );

    // Scrollbar up button
    scrollUpButtonBounds_ = irr::core::recti(
        contentWidth - SCROLLBAR_WIDTH, 0,
        contentWidth, SCROLLBAR_BUTTON_HEIGHT
    );

    // Scrollbar down button
    int scrollbarHeight = contentHeight - BUTTON_AREA_HEIGHT;
    scrollDownButtonBounds_ = irr::core::recti(
        contentWidth - SCROLLBAR_WIDTH, scrollbarHeight - SCROLLBAR_BUTTON_HEIGHT,
        contentWidth, scrollbarHeight
    );

    // Scrollbar track (between buttons)
    scrollTrackBounds_ = irr::core::recti(
        contentWidth - SCROLLBAR_WIDTH, SCROLLBAR_BUTTON_HEIGHT,
        contentWidth, scrollbarHeight - SCROLLBAR_BUTTON_HEIGHT
    );

    // Close button at bottom center
    int buttonX = (contentWidth - BUTTON_WIDTH) / 2;
    int buttonY = contentHeight - BUTTON_AREA_HEIGHT + (BUTTON_AREA_HEIGHT - BUTTON_HEIGHT) / 2;
    closeButtonBounds_ = irr::core::recti(
        buttonX, buttonY,
        buttonX + BUTTON_WIDTH, buttonY + BUTTON_HEIGHT
    );
}

// ============================================================================
// Window Lifecycle
// ============================================================================

void NoteWindow::open(const std::string& text, uint8_t type)
{
    bookType_ = type;

    // Set title based on type
    switch (type) {
        case 1:
            setTitle(L"Book");
            break;
        case 2:
            setTitle(L"Item Info");
            break;
        default:
            setTitle(L"Note");
            break;
    }

    // Process and wrap text
    processText(text);

    // Reset scroll position
    scrollOffset_ = 0;

    // Show the window
    show();
    bringToFront();
}

void NoteWindow::close()
{
    hide();
    lines_.clear();
    scrollOffset_ = 0;
}

void NoteWindow::positionDefault(int screenWidth, int screenHeight)
{
    // Center on screen
    int x = (screenWidth - WINDOW_WIDTH) / 2;
    int y = (screenHeight - WINDOW_HEIGHT) / 2;
    setPosition(x, y);
}

// ============================================================================
// Text Processing
// ============================================================================

void NoteWindow::processText(const std::string& rawText)
{
    lines_.clear();

    // Convert to wide string
    std::wstring_convert<std::codecvt_utf8<wchar_t>> converter;
    std::wstring wideText;
    try {
        wideText = converter.from_bytes(rawText);
    } catch (...) {
        // Fallback: simple ASCII conversion
        wideText.reserve(rawText.size());
        for (char c : rawText) {
            wideText += static_cast<wchar_t>(static_cast<unsigned char>(c));
        }
    }

    // Replace '^' with newline (EQ convention)
    for (wchar_t& c : wideText) {
        if (c == L'^') {
            c = L'\n';
        }
    }

    // Split into lines by newline
    std::wstring currentLine;
    for (wchar_t c : wideText) {
        if (c == L'\n') {
            lines_.push_back(currentLine);
            currentLine.clear();
        } else {
            currentLine += c;
        }
    }

    // Add last line if non-empty
    if (!currentLine.empty()) {
        lines_.push_back(currentLine);
    }
}

std::vector<std::wstring> NoteWindow::wrapText(const std::wstring& text, irr::gui::IGUIFont* font, int maxWidth)
{
    std::vector<std::wstring> result;

    if (!font || text.empty()) {
        if (!text.empty()) {
            result.push_back(text);
        }
        return result;
    }

    std::wstring currentLine;
    std::wstring currentWord;

    for (size_t i = 0; i <= text.size(); ++i) {
        wchar_t c = (i < text.size()) ? text[i] : L' ';

        if (c == L' ' || i == text.size()) {
            // End of word - try to add to current line
            if (currentWord.empty()) {
                continue;
            }

            std::wstring testLine = currentLine.empty()
                ? currentWord
                : currentLine + L" " + currentWord;

            irr::core::dimension2du size = font->getDimension(testLine.c_str());

            if (static_cast<int>(size.Width) <= maxWidth) {
                // Fits on current line
                currentLine = testLine;
            } else {
                // Doesn't fit - push current line and start new one
                if (!currentLine.empty()) {
                    result.push_back(currentLine);
                }
                currentLine = currentWord;

                // If single word is too long, force it
                size = font->getDimension(currentLine.c_str());
                if (static_cast<int>(size.Width) > maxWidth && currentLine.length() > 1) {
                    // Word is too long - just use it anyway
                    result.push_back(currentLine);
                    currentLine.clear();
                }
            }
            currentWord.clear();
        } else {
            currentWord += c;
        }
    }

    // Add remaining line
    if (!currentLine.empty()) {
        result.push_back(currentLine);
    }

    return result;
}

// ============================================================================
// Rendering
// ============================================================================

void NoteWindow::render(irr::video::IVideoDriver* driver, irr::gui::IGUIEnvironment* gui)
{
    if (!isVisible()) {
        return;
    }

    WindowBase::render(driver, gui);
}

void NoteWindow::renderContent(irr::video::IVideoDriver* driver, irr::gui::IGUIEnvironment* gui)
{
    irr::core::recti contentArea = getContentArea();

    // Draw text background
    irr::core::recti textBg(
        contentArea.UpperLeftCorner.X + textBounds_.UpperLeftCorner.X - TEXT_PADDING,
        contentArea.UpperLeftCorner.Y + textBounds_.UpperLeftCorner.Y,
        contentArea.UpperLeftCorner.X + textBounds_.LowerRightCorner.X + TEXT_PADDING,
        contentArea.UpperLeftCorner.Y + textBounds_.LowerRightCorner.Y
    );
    driver->draw2DRectangle(getTextBackground(), textBg);

    // Render text
    renderTextContent(driver, gui);

    // Render scrollbar if needed
    if (getMaxScrollOffset() > 0) {
        renderScrollbar(driver);
    }

    // Render close button
    renderCloseButton(driver, gui);
}

void NoteWindow::renderTextContent(irr::video::IVideoDriver* driver, irr::gui::IGUIEnvironment* gui)
{
    irr::gui::IGUIFont* font = gui->getBuiltInFont();
    if (!font || lines_.empty()) {
        return;
    }

    irr::core::recti contentArea = getContentArea();

    // Calculate absolute text bounds
    irr::core::recti absTextBounds(
        contentArea.UpperLeftCorner.X + textBounds_.UpperLeftCorner.X,
        contentArea.UpperLeftCorner.Y + textBounds_.UpperLeftCorner.Y,
        contentArea.UpperLeftCorner.X + textBounds_.LowerRightCorner.X,
        contentArea.UpperLeftCorner.Y + textBounds_.LowerRightCorner.Y
    );

    int textWidth = absTextBounds.getWidth();
    irr::video::SColor textColor = getTextColor();

    // Render visible lines
    int visibleLines = getVisibleLineCount();
    int y = absTextBounds.UpperLeftCorner.Y;

    for (int i = 0; i < visibleLines && (scrollOffset_ + i) < static_cast<int>(lines_.size()); ++i) {
        const std::wstring& line = lines_[scrollOffset_ + i];

        // Wrap this line if needed
        std::vector<std::wstring> wrappedLines = wrapText(line, font, textWidth);

        for (const std::wstring& wrappedLine : wrappedLines) {
            if (y + LINE_HEIGHT > absTextBounds.LowerRightCorner.Y) {
                break;  // No more room
            }

            irr::core::recti lineRect(
                absTextBounds.UpperLeftCorner.X,
                y,
                absTextBounds.LowerRightCorner.X,
                y + LINE_HEIGHT
            );

            font->draw(wrappedLine.c_str(), lineRect, textColor, false, false, &absTextBounds);
            y += LINE_HEIGHT;
        }
    }
}

void NoteWindow::renderScrollbar(irr::video::IVideoDriver* driver)
{
    irr::core::recti contentArea = getContentArea();

    // Calculate absolute positions
    auto absScrollbar = irr::core::recti(
        contentArea.UpperLeftCorner.X + scrollbarBounds_.UpperLeftCorner.X,
        contentArea.UpperLeftCorner.Y + scrollbarBounds_.UpperLeftCorner.Y,
        contentArea.UpperLeftCorner.X + scrollbarBounds_.LowerRightCorner.X,
        contentArea.UpperLeftCorner.Y + scrollbarBounds_.LowerRightCorner.Y
    );

    // Draw scrollbar background
    driver->draw2DRectangle(getScrollbarBackground(), absScrollbar);

    // Draw up/down buttons
    irr::video::SColor buttonColor(255, 70, 70, 90);

    auto absUpBtn = irr::core::recti(
        contentArea.UpperLeftCorner.X + scrollUpButtonBounds_.UpperLeftCorner.X,
        contentArea.UpperLeftCorner.Y + scrollUpButtonBounds_.UpperLeftCorner.Y,
        contentArea.UpperLeftCorner.X + scrollUpButtonBounds_.LowerRightCorner.X,
        contentArea.UpperLeftCorner.Y + scrollUpButtonBounds_.LowerRightCorner.Y
    );
    driver->draw2DRectangle(buttonColor, absUpBtn);

    auto absDownBtn = irr::core::recti(
        contentArea.UpperLeftCorner.X + scrollDownButtonBounds_.UpperLeftCorner.X,
        contentArea.UpperLeftCorner.Y + scrollDownButtonBounds_.UpperLeftCorner.Y,
        contentArea.UpperLeftCorner.X + scrollDownButtonBounds_.LowerRightCorner.X,
        contentArea.UpperLeftCorner.Y + scrollDownButtonBounds_.LowerRightCorner.Y
    );
    driver->draw2DRectangle(buttonColor, absDownBtn);

    // Draw thumb
    int maxOffset = getMaxScrollOffset();
    if (maxOffset > 0) {
        auto absTrack = irr::core::recti(
            contentArea.UpperLeftCorner.X + scrollTrackBounds_.UpperLeftCorner.X,
            contentArea.UpperLeftCorner.Y + scrollTrackBounds_.UpperLeftCorner.Y,
            contentArea.UpperLeftCorner.X + scrollTrackBounds_.LowerRightCorner.X,
            contentArea.UpperLeftCorner.Y + scrollTrackBounds_.LowerRightCorner.Y
        );

        int trackHeight = absTrack.getHeight();
        int thumbHeight = std::max(20, trackHeight * getVisibleLineCount() / static_cast<int>(lines_.size()));
        int thumbY = absTrack.UpperLeftCorner.Y +
            (trackHeight - thumbHeight) * scrollOffset_ / maxOffset;

        irr::core::recti thumbRect(
            absTrack.UpperLeftCorner.X,
            thumbY,
            absTrack.LowerRightCorner.X,
            thumbY + thumbHeight
        );

        driver->draw2DRectangle(getScrollbarThumb(), thumbRect);

        // Update thumb bounds for hit testing
        scrollThumbBounds_ = irr::core::recti(
            scrollTrackBounds_.UpperLeftCorner.X,
            thumbY - contentArea.UpperLeftCorner.Y,
            scrollTrackBounds_.LowerRightCorner.X,
            thumbY - contentArea.UpperLeftCorner.Y + thumbHeight
        );
    }
}

void NoteWindow::renderCloseButton(irr::video::IVideoDriver* driver, irr::gui::IGUIEnvironment* gui)
{
    irr::core::recti contentArea = getContentArea();

    irr::core::recti absButton(
        contentArea.UpperLeftCorner.X + closeButtonBounds_.UpperLeftCorner.X,
        contentArea.UpperLeftCorner.Y + closeButtonBounds_.UpperLeftCorner.Y,
        contentArea.UpperLeftCorner.X + closeButtonBounds_.LowerRightCorner.X,
        contentArea.UpperLeftCorner.Y + closeButtonBounds_.LowerRightCorner.Y
    );

    drawButton(driver, gui, absButton, L"Close", closeButtonHovered_);
}

// ============================================================================
// Input Handling
// ============================================================================

bool NoteWindow::handleMouseDown(int x, int y, bool leftButton, bool shift, bool ctrl)
{
    if (!isVisible() || !containsPoint(x, y)) {
        return false;
    }

    // Let base class handle title bar dragging
    if (WindowBase::handleMouseDown(x, y, leftButton, shift, ctrl)) {
        return true;
    }

    if (!leftButton) {
        return true;
    }

    irr::core::recti contentArea = getContentArea();
    int relX = x - contentArea.UpperLeftCorner.X;
    int relY = y - contentArea.UpperLeftCorner.Y;

    // Check close button
    if (isInCloseButton(relX, relY)) {
        close();
        if (closeCallback_) {
            closeCallback_();
        }
        return true;
    }

    // Check scrollbar buttons
    if (scrollUpButtonBounds_.isPointInside(irr::core::vector2di(relX, relY))) {
        scrollUp(3);
        return true;
    }

    if (scrollDownButtonBounds_.isPointInside(irr::core::vector2di(relX, relY))) {
        scrollDown(3);
        return true;
    }

    // Check scrollbar thumb for dragging
    if (isInScrollThumb(relX, relY)) {
        scrollbarDragging_ = true;
        scrollbarDragStartY_ = y;
        scrollbarDragStartOffset_ = scrollOffset_;
        return true;
    }

    // Check scrollbar track for page scroll
    if (scrollTrackBounds_.isPointInside(irr::core::vector2di(relX, relY))) {
        int thumbCenterY = scrollThumbBounds_.UpperLeftCorner.Y +
            scrollThumbBounds_.getHeight() / 2;
        if (relY < thumbCenterY) {
            scrollUp(getVisibleLineCount());
        } else {
            scrollDown(getVisibleLineCount());
        }
        return true;
    }

    return true;
}

bool NoteWindow::handleMouseUp(int x, int y, bool leftButton)
{
    if (scrollbarDragging_) {
        scrollbarDragging_ = false;
        return true;
    }

    return WindowBase::handleMouseUp(x, y, leftButton);
}

bool NoteWindow::handleMouseMove(int x, int y)
{
    if (!isVisible()) {
        return false;
    }

    // Handle scrollbar dragging
    if (scrollbarDragging_) {
        int maxOffset = getMaxScrollOffset();
        if (maxOffset > 0) {
            int trackHeight = scrollTrackBounds_.getHeight();
            int deltaY = y - scrollbarDragStartY_;
            int deltaOffset = deltaY * maxOffset / trackHeight;
            scrollOffset_ = scrollbarDragStartOffset_ + deltaOffset;
            clampScrollOffset();
        }
        return true;
    }

    // Update hover states
    irr::core::recti contentArea = getContentArea();
    int relX = x - contentArea.UpperLeftCorner.X;
    int relY = y - contentArea.UpperLeftCorner.Y;

    closeButtonHovered_ = isInCloseButton(relX, relY);

    return WindowBase::handleMouseMove(x, y);
}

bool NoteWindow::handleMouseWheel(float delta)
{
    if (!isVisible()) {
        return false;
    }

    if (delta > 0) {
        scrollUp(3);
    } else if (delta < 0) {
        scrollDown(3);
    }

    return true;
}

// ============================================================================
// Hit Testing
// ============================================================================

bool NoteWindow::isInScrollbar(int x, int y) const
{
    return scrollbarBounds_.isPointInside(irr::core::vector2di(x, y));
}

bool NoteWindow::isInScrollThumb(int x, int y) const
{
    return scrollThumbBounds_.isPointInside(irr::core::vector2di(x, y));
}

bool NoteWindow::isInCloseButton(int x, int y) const
{
    return closeButtonBounds_.isPointInside(irr::core::vector2di(x, y));
}

// ============================================================================
// Scrolling
// ============================================================================

void NoteWindow::scrollUp(int lines)
{
    scrollOffset_ = std::max(0, scrollOffset_ - lines);
}

void NoteWindow::scrollDown(int lines)
{
    scrollOffset_ = std::min(getMaxScrollOffset(), scrollOffset_ + lines);
}

int NoteWindow::getMaxScrollOffset() const
{
    int totalLines = static_cast<int>(lines_.size());
    int visibleLines = getVisibleLineCount();
    return std::max(0, totalLines - visibleLines);
}

void NoteWindow::clampScrollOffset()
{
    scrollOffset_ = std::max(0, std::min(scrollOffset_, getMaxScrollOffset()));
}

int NoteWindow::getVisibleLineCount() const
{
    return textBounds_.getHeight() / LINE_HEIGHT;
}

} // namespace ui
} // namespace eqt
