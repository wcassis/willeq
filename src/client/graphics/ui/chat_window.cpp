#include "client/graphics/ui/chat_window.h"
#include "client/input/hotkey_manager.h"
#include "common/logging.h"
#include <algorithm>
#include <fstream>
#include <json/json.h>

namespace eqt {
namespace ui {

// Helper function to wrap text into lines that fit within maxWidth
static std::vector<std::wstring> wrapText(const std::string& text, irr::gui::IGUIFont* font, int maxWidth) {
    std::vector<std::wstring> lines;
    std::wstring textW(text.begin(), text.end());

    if (textW.empty()) {
        return lines;
    }

    // Get text width - if it fits in one line, return as-is
    irr::core::dimension2du dim = font->getDimension(textW.c_str());
    if (static_cast<int>(dim.Width) <= maxWidth) {
        lines.push_back(textW);
        return lines;
    }

    // Word wrap: split text into words and build lines
    std::wstring currentLine;
    size_t pos = 0;

    while (pos < textW.length()) {
        // Find next word boundary (space or end of string)
        size_t nextSpace = textW.find(L' ', pos);
        if (nextSpace == std::wstring::npos) {
            nextSpace = textW.length();
        }

        std::wstring word = textW.substr(pos, nextSpace - pos);

        // Test if adding this word would exceed maxWidth
        std::wstring testLine = currentLine.empty() ? word : currentLine + L" " + word;
        dim = font->getDimension(testLine.c_str());

        if (static_cast<int>(dim.Width) <= maxWidth) {
            // Word fits - add to current line
            currentLine = testLine;
        } else {
            // Word doesn't fit - start new line
            if (!currentLine.empty()) {
                lines.push_back(currentLine);
            }
            // Check if the word itself is too long
            dim = font->getDimension(word.c_str());
            if (static_cast<int>(dim.Width) > maxWidth) {
                // Word is too long - need to break it
                std::wstring partial;
                for (wchar_t c : word) {
                    std::wstring test = partial + c;
                    dim = font->getDimension(test.c_str());
                    if (static_cast<int>(dim.Width) > maxWidth && !partial.empty()) {
                        lines.push_back(partial);
                        partial.clear();
                    }
                    partial += c;
                }
                currentLine = partial;
            } else {
                currentLine = word;
            }
        }

        // Move past the word and the space
        pos = nextSpace + 1;
        if (nextSpace == textW.length()) break;
    }

    // Add remaining text
    if (!currentLine.empty()) {
        lines.push_back(currentLine);
    }

    return lines;
}

ChatWindow::ChatWindow()
    : WindowBase(L"Chat", 600, 120) {

    // Initialize default channel filters (all enabled)
    initDefaultChannels();

    // Initialize combat channel routing
    initCombatChannels();

    // Initialize tabs
    tabs_.push_back({"Main", {}, true});
    tabs_.push_back({"Combat", {}, false});
    activeTabIndex_ = 0;

    // Set up input field callback
    inputField_.setSubmitCallback([this](const std::string& text) {
        if (submitCallback_) {
            submitCallback_(text);
        }
    });
}

void ChatWindow::init(int screenWidth, int screenHeight) {
    screenWidth_ = screenWidth;
    screenHeight_ = screenHeight;

    // Set default position and size
    // Width: 50% of screen
    // Height: ~10% of screen (minimum 120 pixels)
    int width = screenWidth / 2;
    int height = std::max(120, screenHeight / 10);

    // Clamp to constraints
    width = std::max(minWidth_, std::min(width, maxWidth_));
    height = std::max(minHeight_, std::min(height, maxHeight_));

    // Position at bottom-left with some padding
    int x = 10;
    int y = screenHeight - height - 10;

    setPosition(x, y);
    setSize(width, height);

    // Try to load saved settings (overrides defaults if file exists)
    loadSettings();

    // Apply tab settings from UISettings (may have been set by main config)
    applyTabSettings();

    // Make sure position is valid for current screen size
    if (getX() + getWidth() > screenWidth) {
        setPosition(screenWidth - getWidth(), getY());
    }
    if (getY() + getHeight() > screenHeight) {
        setPosition(getX(), screenHeight - getHeight());
    }

    // Calculate visible lines
    visibleLines_ = calculateVisibleLines();

    // Start visible
    show();
}

void ChatWindow::onResize(int screenWidth, int screenHeight) {
    screenWidth_ = screenWidth;
    screenHeight_ = screenHeight;

    // Update max size based on screen
    maxWidth_ = static_cast<int>(screenWidth * 0.8f);
    maxHeight_ = static_cast<int>(screenHeight * 0.5f);

    // Constrain current size
    constrainSize();

    // Keep window in bounds
    int x = getX();
    int y = getY();

    if (x + getWidth() > screenWidth) {
        x = screenWidth - getWidth();
    }
    if (y + getHeight() > screenHeight) {
        y = screenHeight - getHeight();
    }
    if (x < 0) x = 0;
    if (y < 0) y = 0;

    setPosition(x, y);

    // Recalculate visible lines
    visibleLines_ = calculateVisibleLines();

    // Invalidate wrapped line cache (width may have changed)
    invalidateWrappedLineCache();
}

void ChatWindow::render(irr::video::IVideoDriver* driver,
                        irr::gui::IGUIEnvironment* gui) {
    if (!visible_) return;

    // Process any pending messages
    messageBuffer_.processPending();
    combatBuffer_.processPending();

    // Auto-scroll to bottom if we were at bottom and new messages arrived
    if (scrollOffset_ == 0 && messageBuffer_.hasNewMessages()) {
        messageBuffer_.clearNewMessageFlag();
    }
    if (combatScrollOffset_ == 0 && combatBuffer_.hasNewMessages()) {
        combatBuffer_.clearNewMessageFlag();
    }

    // Render with fade effect
    if (backgroundOpacity_ >= 0.99f) {
        // Fully opaque - render normally
        WindowBase::render(driver, gui);
    } else {
        // Faded - render custom background with opacity, then content
        renderFadedBackground(driver, backgroundOpacity_);
        renderContent(driver, gui);
    }
}

void ChatWindow::renderFadedBackground(irr::video::IVideoDriver* driver, float opacity) {
    if (opacity <= 0.01f) {
        // Fully transparent - don't draw background at all
        return;
    }

    // Get colors from UISettings and apply opacity
    irr::video::SColor borderLight = getBorderLightColor();
    irr::video::SColor borderDark = getBorderDarkColor();
    irr::video::SColor windowBg = getWindowBackground();

    // Apply opacity to alpha channel
    uint8_t alpha = static_cast<uint8_t>(opacity * 255);
    borderLight.setAlpha(static_cast<uint8_t>(borderLight.getAlpha() * opacity));
    borderDark.setAlpha(static_cast<uint8_t>(borderDark.getAlpha() * opacity));
    windowBg.setAlpha(static_cast<uint8_t>(windowBg.getAlpha() * opacity));

    int borderWidth = getBorderWidth();
    int inputFieldHeight = getInputFieldHeight();

    // Calculate the area above the input field (the part that fades)
    irr::core::recti content = getContentArea();
    int inputY = content.LowerRightCorner.Y - inputFieldHeight;

    // Draw faded borders (only for the fading portion - above input field)
    irr::core::recti fadeBounds(
        bounds_.UpperLeftCorner.X,
        bounds_.UpperLeftCorner.Y,
        bounds_.LowerRightCorner.X,
        inputY
    );

    // Top border
    driver->draw2DRectangle(borderLight,
        irr::core::recti(fadeBounds.UpperLeftCorner.X,
                        fadeBounds.UpperLeftCorner.Y,
                        fadeBounds.LowerRightCorner.X,
                        fadeBounds.UpperLeftCorner.Y + borderWidth));
    // Left border
    driver->draw2DRectangle(borderLight,
        irr::core::recti(fadeBounds.UpperLeftCorner.X,
                        fadeBounds.UpperLeftCorner.Y,
                        fadeBounds.UpperLeftCorner.X + borderWidth,
                        fadeBounds.LowerRightCorner.Y));
    // Right border
    driver->draw2DRectangle(borderDark,
        irr::core::recti(fadeBounds.LowerRightCorner.X - borderWidth,
                        fadeBounds.UpperLeftCorner.Y,
                        fadeBounds.LowerRightCorner.X,
                        fadeBounds.LowerRightCorner.Y));

    // Background fill (fading area only)
    irr::core::recti bgRect(
        fadeBounds.UpperLeftCorner.X + borderWidth,
        fadeBounds.UpperLeftCorner.Y + borderWidth,
        fadeBounds.LowerRightCorner.X - borderWidth,
        fadeBounds.LowerRightCorner.Y
    );
    driver->draw2DRectangle(windowBg, bgRect);

    // Draw input field area with full opacity (always visible)
    irr::video::SColor fullBorderLight = getBorderLightColor();
    irr::video::SColor fullBorderDark = getBorderDarkColor();
    irr::video::SColor fullWindowBg = getWindowBackground();

    irr::core::recti inputBounds(
        bounds_.UpperLeftCorner.X,
        inputY,
        bounds_.LowerRightCorner.X,
        bounds_.LowerRightCorner.Y
    );

    // Left border for input area
    driver->draw2DRectangle(fullBorderLight,
        irr::core::recti(inputBounds.UpperLeftCorner.X,
                        inputBounds.UpperLeftCorner.Y,
                        inputBounds.UpperLeftCorner.X + borderWidth,
                        inputBounds.LowerRightCorner.Y));
    // Bottom border
    driver->draw2DRectangle(fullBorderDark,
        irr::core::recti(inputBounds.UpperLeftCorner.X,
                        inputBounds.LowerRightCorner.Y - borderWidth,
                        inputBounds.LowerRightCorner.X,
                        inputBounds.LowerRightCorner.Y));
    // Right border for input area
    driver->draw2DRectangle(fullBorderDark,
        irr::core::recti(inputBounds.LowerRightCorner.X - borderWidth,
                        inputBounds.UpperLeftCorner.Y,
                        inputBounds.LowerRightCorner.X,
                        inputBounds.LowerRightCorner.Y));

    // Background for input area
    irr::core::recti inputBgRect(
        inputBounds.UpperLeftCorner.X + borderWidth,
        inputBounds.UpperLeftCorner.Y,
        inputBounds.LowerRightCorner.X - borderWidth,
        inputBounds.LowerRightCorner.Y - borderWidth
    );
    driver->draw2DRectangle(fullWindowBg, inputBgRect);
}

void ChatWindow::renderContent(irr::video::IVideoDriver* driver,
                               irr::gui::IGUIEnvironment* gui) {
    irr::core::recti content = getContentArea();
    int scrollbarWidth = getScrollbarWidth();
    int inputFieldHeight = getInputFieldHeight();

    // Get font for tab rendering
    irr::gui::IGUIFont* font = gui->getBuiltInFont();

    // Only render tabs and scrollbar when not faded
    bool isFaded = (backgroundOpacity_ < 0.99f);
    if (!isFaded) {
        renderTabs(driver, font);
    }

    // Calculate areas (message area excludes scrollbar width and starts below tabs)
    int inputY = content.LowerRightCorner.Y - inputFieldHeight;

    // When faded, extend message area to full width (no scrollbar) and start from top (no tabs)
    irr::core::recti messageArea(
        content.UpperLeftCorner.X,
        isFaded ? content.UpperLeftCorner.Y : content.UpperLeftCorner.Y + tabBarHeight_,
        isFaded ? content.LowerRightCorner.X - 2 : content.LowerRightCorner.X - scrollbarWidth - 2,
        inputY - 2  // Small gap between messages and input
    );

    irr::core::recti inputArea(
        content.UpperLeftCorner.X,
        inputY,
        content.LowerRightCorner.X - scrollbarWidth - 2,  // Match message area width
        content.LowerRightCorner.Y
    );

    // Render messages
    renderMessages(driver, gui, messageArea);

    // Render scrollbar only when not faded
    if (!isFaded) {
        renderScrollbar(driver);
    }

    // Render input field
    inputField_.render(driver, gui, inputArea);

    // Draw resize grip in upper-right corner only when not faded
    if (!isFaded) {
        const int gripSize = 12;
        int gripX = bounds_.LowerRightCorner.X - gripSize;
        int gripY = bounds_.UpperLeftCorner.Y;

        // Draw diagonal lines pattern for resize grip
        irr::video::SColor gripColor(200, 200, 200, 200);
        for (int i = 2; i < gripSize; i += 3) {
            // Diagonal lines from top-right to bottom-left direction
            driver->draw2DLine(
                irr::core::vector2di(gripX + i, gripY + 1),
                irr::core::vector2di(bounds_.LowerRightCorner.X - 1, gripY + gripSize - i),
                gripColor);
        }

        // Draw corner border for visibility
        driver->draw2DLine(
            irr::core::vector2di(gripX, gripY),
            irr::core::vector2di(gripX, gripY + gripSize),
            gripColor);
        driver->draw2DLine(
            irr::core::vector2di(gripX, gripY + gripSize),
            irr::core::vector2di(bounds_.LowerRightCorner.X, gripY + gripSize),
            gripColor);
    }
}

void ChatWindow::renderTabs(irr::video::IVideoDriver* driver, irr::gui::IGUIFont* font) {
    irr::core::recti content = getContentArea();

    // Tab bar background
    irr::core::recti tabBarRect(
        content.UpperLeftCorner.X,
        content.UpperLeftCorner.Y,
        content.LowerRightCorner.X - getScrollbarWidth(),
        content.UpperLeftCorner.Y + tabBarHeight_
    );
    driver->draw2DRectangle(irr::video::SColor(255, 40, 40, 40), tabBarRect);

    // Calculate tab widths
    int tabWidth = 60;
    int tabPadding = 2;
    int x = content.UpperLeftCorner.X + tabPadding;
    int y = content.UpperLeftCorner.Y + 2;

    for (size_t i = 0; i < tabs_.size(); ++i) {
        ChatTab& tab = tabs_[i];
        bool isActive = (static_cast<int>(i) == activeTabIndex_);

        // Update tab bounds for click detection
        tab.bounds = irr::core::recti(
            x, y,
            x + tabWidth, y + tabBarHeight_ - 4
        );
        tab.active = isActive;

        // Tab background
        irr::video::SColor tabBg = isActive ?
            irr::video::SColor(255, 80, 80, 80) :
            irr::video::SColor(255, 50, 50, 50);
        driver->draw2DRectangle(tabBg, tab.bounds);

        // Active tab indicator (bottom highlight)
        if (isActive) {
            irr::core::recti indicator(
                tab.bounds.UpperLeftCorner.X,
                tab.bounds.LowerRightCorner.Y - 2,
                tab.bounds.LowerRightCorner.X,
                tab.bounds.LowerRightCorner.Y
            );
            driver->draw2DRectangle(irr::video::SColor(255, 100, 150, 255), indicator);
        }

        // Tab text
        if (font) {
            std::wstring nameW(tab.name.begin(), tab.name.end());
            irr::core::dimension2du textDim = font->getDimension(nameW.c_str());
            int textX = tab.bounds.UpperLeftCorner.X + (tabWidth - static_cast<int>(textDim.Width)) / 2;
            int textY = tab.bounds.UpperLeftCorner.Y + (tabBarHeight_ - 4 - static_cast<int>(textDim.Height)) / 2;

            irr::video::SColor textColor = isActive ?
                irr::video::SColor(255, 255, 255, 255) :
                irr::video::SColor(255, 180, 180, 180);
            font->draw(nameW.c_str(), irr::core::recti(textX, textY, textX + 100, textY + 20), textColor);
        }

        x += tabWidth + tabPadding;
    }

    // Bottom border of tab bar
    driver->draw2DLine(
        irr::core::vector2di(tabBarRect.UpperLeftCorner.X, tabBarRect.LowerRightCorner.Y),
        irr::core::vector2di(tabBarRect.LowerRightCorner.X, tabBarRect.LowerRightCorner.Y),
        irr::video::SColor(255, 60, 60, 60)
    );
}

int ChatWindow::getTabAtPosition(int x, int y) const {
    for (size_t i = 0; i < tabs_.size(); ++i) {
        if (tabs_[i].bounds.isPointInside(irr::core::vector2di(x, y))) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

void ChatWindow::renderMessages(irr::video::IVideoDriver* driver,
                                 irr::gui::IGUIEnvironment* gui,
                                 const irr::core::recti& messageArea) {
    irr::gui::IGUIFont* font = gui->getBuiltInFont();
    if (!font) return;

    // Clear rendered links from previous frame
    renderedLinks_.clear();

    // Select buffer and cache based on active tab
    bool isCombatTab = (activeTabIndex_ == 1);
    const ChatMessageBuffer& activeBuffer = isCombatTab ? combatBuffer_ : messageBuffer_;
    std::vector<CachedWrappedMessage>& activeCache = isCombatTab ? combatWrappedLineCache_ : wrappedLineCache_;
    int& activeCacheWidth = isCombatTab ? combatWrappedLineCacheWidth_ : wrappedLineCacheWidth_;
    size_t& activeCacheMessageCount = isCombatTab ? combatWrappedLineCacheMessageCount_ : wrappedLineCacheMessageCount_;
    bool& activeCacheShowTimestamps = isCombatTab ? combatWrappedLineCacheShowTimestamps_ : wrappedLineCacheShowTimestamps_;
    int& activeScrollOffset = isCombatTab ? combatScrollOffset_ : scrollOffset_;

    const auto& messages = activeBuffer.getMessages();
    if (messages.empty()) return;

    int lineHeight = 12;  // Approximate font height
    int availableHeight = messageArea.getHeight();
    int maxWidth = messageArea.getWidth() - 8;

    visibleLines_ = availableHeight / lineHeight;
    if (visibleLines_ <= 0) return;

    // Performance optimization: check if we need to rebuild the wrapped line cache
    bool cacheValid = (activeCacheWidth == maxWidth &&
                       activeCacheMessageCount == messages.size() &&
                       activeCacheShowTimestamps == showTimestamps_);

    if (!cacheValid) {
        // Rebuild wrapped line cache
        activeCache.clear();
        activeCache.reserve(messages.size());

        for (const auto& msg : messages) {
            if (isChannelEnabled(msg.channel)) {
                std::string displayText = formatMessageForDisplay(msg, showTimestamps_);
                CachedWrappedMessage cached;
                cached.color = msg.color;
                cached.hasLinks = !msg.links.empty();
                cached.msg = &msg;

                if (msg.links.empty()) {
                    // Wrap text for messages without links
                    cached.lines = wrapText(displayText, font, maxWidth);
                } else {
                    // For messages with links, keep as single line (link rendering handles overflow)
                    std::wstring textW(displayText.begin(), displayText.end());
                    cached.lines.push_back(textW);
                }
                activeCache.push_back(std::move(cached));
            }
        }

        activeCacheWidth = maxWidth;
        activeCacheMessageCount = messages.size();
        activeCacheShowTimestamps = showTimestamps_;
    }

    // Build flat list of lines for rendering (using cached data)
    struct WrappedLine {
        std::wstring text;
        irr::video::SColor color;
        bool hasLinks;
        size_t messageIndex;
        const ChatMessage* msg;
    };
    std::vector<WrappedLine> allLines;
    allLines.reserve(activeCache.size() * 2);  // Rough estimate

    size_t msgIdx = 0;
    for (const auto& cached : activeCache) {
        for (const auto& line : cached.lines) {
            allLines.push_back({line, cached.color, cached.hasLinks, msgIdx, cached.msg});
        }
        msgIdx++;
    }

    if (allLines.empty()) return;

    // Calculate which lines to show (accounting for scroll)
    int totalLines = static_cast<int>(allLines.size());
    int startIdx = std::max(0, totalLines - visibleLines_ - activeScrollOffset);
    int endIdx = std::max(0, totalLines - activeScrollOffset);

    // Render lines from bottom to top within the visible area
    int y = messageArea.LowerRightCorner.Y - lineHeight;

    for (int i = endIdx - 1; i >= startIdx && y >= messageArea.UpperLeftCorner.Y; --i) {
        const WrappedLine& line = allLines[i];

        if (line.hasLinks && line.msg) {
            // Use link-aware rendering
            std::string displayText(line.text.begin(), line.text.end());
            renderMessageWithLinks(driver, font, *line.msg, displayText,
                                   messageArea.UpperLeftCorner.X + 4, y,
                                   maxWidth, line.messageIndex);
        } else {
            // Simple text rendering
            font->draw(line.text.c_str(),
                       irr::core::recti(messageArea.UpperLeftCorner.X + 4, y,
                                       messageArea.LowerRightCorner.X - 4, y + lineHeight),
                       line.color);
        }

        y -= lineHeight;
    }

    // Draw scroll indicator if not at bottom
    if (activeScrollOffset > 0) {
        std::wstring indicator = L"[More below - scroll down]";
        irr::core::dimension2du dim = font->getDimension(indicator.c_str());
        int indicatorX = messageArea.UpperLeftCorner.X + (messageArea.getWidth() - dim.Width) / 2;
        int indicatorY = messageArea.LowerRightCorner.Y - lineHeight;
        font->draw(indicator.c_str(),
                   irr::core::recti(indicatorX, indicatorY, indicatorX + dim.Width, indicatorY + lineHeight),
                   irr::video::SColor(200, 255, 255, 0));
    }
}

bool ChatWindow::handleMouseDown(int x, int y, bool leftButton, bool shift, bool ctrl) {
    if (!visible_) return false;

    // Check for resize grip (upper-right corner)
    if (leftButton && isOnResizeEdge(x, y)) {
        resizing_ = true;
        resizeStartPos_ = irr::core::vector2di(x, y);
        resizeStartBounds_ = bounds_;

        // Corner grip resizes both width and height
        resizeRight_ = true;
        resizeTop_ = true;

        return true;
    }

    // Check for tab click
    if (leftButton) {
        int tabIndex = getTabAtPosition(x, y);
        if (tabIndex >= 0 && tabIndex != activeTabIndex_) {
            activeTabIndex_ = tabIndex;
            // Update active state on tabs
            for (size_t i = 0; i < tabs_.size(); ++i) {
                tabs_[i].active = (static_cast<int>(i) == activeTabIndex_);
            }
            return true;
        }
    }

    // Check scrollbar interactions
    if (leftButton && isOnScrollbar(x, y)) {
        // Check scroll up button
        if (isOnScrollUpButton(x, y)) {
            scrollUp(1);
            return true;
        }

        // Check scroll down button
        if (isOnScrollDownButton(x, y)) {
            scrollDown(1);
            return true;
        }

        // Check thumb for dragging
        irr::core::recti thumb = getScrollbarThumbBounds();
        if (thumb.isPointInside(irr::core::vector2di(x, y))) {
            draggingScrollbar_ = true;
            scrollbarDragStartY_ = y;
            // Use active scroll offset for drag start
            scrollbarDragStartOffset_ = (activeTabIndex_ == 1) ? combatScrollOffset_ : scrollOffset_;
            return true;
        }

        // Click on track (not on thumb or buttons) - jump to position
        irr::core::recti track = getScrollbarTrackBounds();
        int scrollButtonHeight = getScrollButtonHeight();
        int trackTop = track.UpperLeftCorner.Y + scrollButtonHeight;
        int trackBottom = track.LowerRightCorner.Y - scrollButtonHeight;
        if (y > trackTop && y < trackBottom) {
            // Jump scroll position based on click location
            int maxOffset = getMaxScrollOffset();
            if (maxOffset > 0) {
                float clickRatio = static_cast<float>(y - trackTop) / (trackBottom - trackTop);
                // Invert because scrollOffset 0 = bottom
                int newOffset = static_cast<int>((1.0f - clickRatio) * maxOffset);
                newOffset = std::max(0, std::min(newOffset, maxOffset));
                // Update active scroll offset
                if (activeTabIndex_ == 1) {
                    combatScrollOffset_ = newOffset;
                } else {
                    scrollOffset_ = newOffset;
                }
            }
            return true;
        }

        return true;
    }

    // Check for link click in message area
    if (leftButton) {
        const MessageLink* link = getLinkAtPosition(x, y);
        if (link && linkClickCallback_) {
            linkClickCallback_(*link);
            return true;
        }
    }

    // Check if clicking in input area
    irr::core::recti content = getContentArea();
    int inputY = content.LowerRightCorner.Y - getInputFieldHeight();
    if (y >= inputY && containsPoint(x, y)) {
        focusInput();
        return true;
    }

    // Handle base window behavior (title bar dragging, etc.)
    return WindowBase::handleMouseDown(x, y, leftButton, shift);
}

bool ChatWindow::handleMouseUp(int x, int y, bool leftButton) {
    if (resizing_) {
        resizing_ = false;
        resizeRight_ = false;
        resizeTop_ = false;
        // Save settings after resize
        saveSettings();
        return true;
    }

    if (draggingScrollbar_) {
        draggingScrollbar_ = false;
        return true;
    }

    return WindowBase::handleMouseUp(x, y, leftButton);
}

bool ChatWindow::handleMouseMove(int x, int y) {
    if (resizing_) {
        int deltaX = x - resizeStartPos_.X;
        int deltaY = y - resizeStartPos_.Y;

        int newWidth = resizeStartBounds_.getWidth();
        int newHeight = resizeStartBounds_.getHeight();
        int newX = resizeStartBounds_.UpperLeftCorner.X;
        int newY = resizeStartBounds_.UpperLeftCorner.Y;

        if (resizeRight_) {
            newWidth = resizeStartBounds_.getWidth() + deltaX;
        }

        if (resizeTop_) {
            newHeight = resizeStartBounds_.getHeight() - deltaY;
            newY = resizeStartBounds_.UpperLeftCorner.Y + deltaY;
        }

        // Apply constraints
        newWidth = std::max(minWidth_, std::min(newWidth, maxWidth_));
        newHeight = std::max(minHeight_, std::min(newHeight, maxHeight_));

        // Adjust Y if height was constrained during top resize
        if (resizeTop_) {
            newY = resizeStartBounds_.LowerRightCorner.Y - newHeight;
        }

        // Keep in screen bounds
        if (newX < 0) newX = 0;
        if (newY < 0) newY = 0;
        if (newX + newWidth > screenWidth_) newWidth = screenWidth_ - newX;
        if (newY + newHeight > screenHeight_) newHeight = screenHeight_ - newY;

        setPosition(newX, newY);
        setSize(newWidth, newHeight);

        // Recalculate visible lines
        visibleLines_ = calculateVisibleLines();

        // Invalidate wrapped line cache (width may have changed)
        invalidateWrappedLineCache();

        return true;
    }

    // Handle scrollbar thumb dragging
    if (draggingScrollbar_) {
        irr::core::recti track = getScrollbarTrackBounds();
        int scrollButtonHeight = getScrollButtonHeight();
        int trackTop = track.UpperLeftCorner.Y + scrollButtonHeight;
        int trackBottom = track.LowerRightCorner.Y - scrollButtonHeight;
        int trackHeight = trackBottom - trackTop;

        if (trackHeight > 0) {
            int maxOffset = getMaxScrollOffset();
            if (maxOffset > 0) {
                int deltaY = y - scrollbarDragStartY_;
                // Invert deltaY because dragging down should decrease scroll offset
                float scrollPerPixel = static_cast<float>(maxOffset) / trackHeight;
                int newOffset = scrollbarDragStartOffset_ - static_cast<int>(deltaY * scrollPerPixel);
                newOffset = std::max(0, std::min(newOffset, maxOffset));
                // Update active scroll offset
                if (activeTabIndex_ == 1) {
                    combatScrollOffset_ = newOffset;
                } else {
                    scrollOffset_ = newOffset;
                }
            }
        }
        return true;
    }

    // Update hover states for scroll buttons
    scrollUpHovered_ = isOnScrollUpButton(x, y);
    scrollDownHovered_ = isOnScrollDownButton(x, y);

    // Update hover state for fade effect
    isHovered_ = containsPoint(x, y);

    // Update hover state for links
    lastMouseX_ = x;
    lastMouseY_ = y;
    hoveredLinkIndex_ = -1;
    irr::core::vector2di point(x, y);
    for (size_t i = 0; i < renderedLinks_.size(); ++i) {
        if (renderedLinks_[i].bounds.isPointInside(point)) {
            hoveredLinkIndex_ = static_cast<int>(i);
            break;
        }
    }

    return WindowBase::handleMouseMove(x, y);
}

bool ChatWindow::handleMouseWheel(float delta) {
    if (!visible_ || !containsPoint(0, 0)) return false;  // TODO: pass actual mouse coords

    if (delta > 0) {
        scrollUp(3);
    } else if (delta < 0) {
        scrollDown(3);
    }

    return true;
}

bool ChatWindow::handleKeyPress(irr::EKEY_CODE key, wchar_t character, bool shift, bool ctrl) {
    if (!visible_) return false;

    // Use HotkeyManager for key binding lookups
    auto& hotkeyMgr = eqt::input::HotkeyManager::instance();
    bool alt = false;  // ChatWindow doesn't receive alt state currently
    auto action = hotkeyMgr.getAction(key, ctrl, shift, alt, eqt::input::HotkeyMode::Global);

    // If input is focused, handle special keys first
    if (inputField_.isFocused()) {
        // Handle Tab for auto-completion (ChatAutocomplete action)
        if (action.has_value() && action.value() == eqt::input::HotkeyAction::ChatAutocomplete) {
            std::string currentText = inputField_.getText();
            std::string completed;

            if (shift) {
                completed = autoComplete_.applyPrevCompletion(currentText);
            } else {
                completed = autoComplete_.applyNextCompletion(currentText);
            }

            if (!completed.empty() && completed != currentText) {
                inputField_.setText(completed);
                lastInputText_ = completed;
            }
            return true;
        }

        // ESC unfocuses input (check key directly since CancelInput may conflict with ClearTarget)
        if (key == irr::KEY_ESCAPE) {
            unfocusInput();
            return true;
        }

        // Check if input text changed (for resetting auto-complete)
        std::string textBefore = inputField_.getText();

        // Route to input field
        bool handled = inputField_.handleKeyPress(key, character, shift, ctrl);

        // Reset auto-complete if text changed (but not from Tab)
        std::string textAfter = inputField_.getText();
        if (textAfter != lastInputText_) {
            autoComplete_.reset();
            lastInputText_ = textAfter;
        }

        return handled;
    }

    // Page up/down for scrolling when not typing
    if (key == irr::KEY_PRIOR) {  // Page Up
        scrollUp(visibleLines_);
        return true;
    }
    if (key == irr::KEY_NEXT) {  // Page Down
        scrollDown(visibleLines_);
        return true;
    }

    return false;
}

void ChatWindow::focusInput() {
    inputField_.setFocused(true);
}

void ChatWindow::unfocusInput() {
    inputField_.setFocused(false);
}

void ChatWindow::insertText(const std::string& text) {
    inputField_.insertText(text);
}

void ChatWindow::addMessage(ChatMessage msg) {
    LOG_DEBUG(MOD_UI, "[ChatWindow] addMessage: channel={}, len={}, text='{}'",
              static_cast<int>(msg.channel), msg.text.length(), msg.text);

    // Route message to appropriate buffer based on channel
    if (isCombatChannel(msg.channel)) {
        // If echoToMain is enabled, add to both buffers
        if (echoToMain_) {
            ChatMessage msgCopy = msg;  // Make a copy for main buffer
            messageBuffer_.addMessage(std::move(msgCopy));
        }
        combatBuffer_.addMessage(std::move(msg));
    } else {
        messageBuffer_.addMessage(std::move(msg));
    }
}

void ChatWindow::addSystemMessage(const std::string& text, ChatChannel channel) {
    LOG_DEBUG(MOD_UI, "[ChatWindow] addSystemMessage: channel={}, text='{}', isCombat={}, echoToMain={}",
              static_cast<int>(channel), text, isCombatChannel(channel), echoToMain_);

    // Route system message to appropriate buffer based on channel
    if (isCombatChannel(channel)) {
        // If echoToMain is enabled, add to both buffers
        if (echoToMain_) {
            LOG_DEBUG(MOD_UI, "[ChatWindow] Adding to MAIN buffer (echo)");
            messageBuffer_.addSystemMessage(text, channel);
        }
        LOG_DEBUG(MOD_UI, "[ChatWindow] Adding to COMBAT buffer");
        combatBuffer_.addSystemMessage(text, channel);
    } else {
        messageBuffer_.addSystemMessage(text, channel);
    }
}

void ChatWindow::setSubmitCallback(ChatSubmitCallback callback) {
    submitCallback_ = callback;
}

void ChatWindow::setCommandRegistry(CommandRegistry* registry) {
    autoComplete_.setCommandRegistry(registry);
}

void ChatWindow::setEntityNameProvider(EntityNameProvider provider) {
    autoComplete_.setEntityNameProvider(provider);
}

void ChatWindow::setLinkClickCallback(LinkClickCallback callback) {
    linkClickCallback_ = callback;
}

const MessageLink* ChatWindow::getLinkAtPosition(int x, int y) const {
    irr::core::vector2di point(x, y);
    for (const auto& rendered : renderedLinks_) {
        if (rendered.bounds.isPointInside(point)) {
            return &rendered.link;
        }
    }
    return nullptr;
}

void ChatWindow::update(uint32_t currentTimeMs) {
    inputField_.update(currentTimeMs);
    updateFadeAnimation(currentTimeMs);
}

void ChatWindow::updateFadeAnimation(uint32_t currentTimeMs) {
    // Calculate delta time
    float deltaTime = 0.0f;
    if (lastUpdateTimeMs_ > 0) {
        deltaTime = (currentTimeMs - lastUpdateTimeMs_) / 1000.0f;
    }
    lastUpdateTimeMs_ = currentTimeMs;

    // Update hover state based on last known mouse position
    // (needed because handleMouseMove won't be called when mouse is outside window)
    isHovered_ = containsPoint(lastMouseX_, lastMouseY_);

    // Determine target opacity: 1.0 if input focused or hovered, 0.0 otherwise
    float targetOpacity = (inputField_.isFocused() || isHovered_) ? 1.0f : 0.0f;

    // Animate towards target
    if (backgroundOpacity_ < targetOpacity) {
        backgroundOpacity_ = std::min(backgroundOpacity_ + fadeSpeed_ * deltaTime, targetOpacity);
    } else if (backgroundOpacity_ > targetOpacity) {
        backgroundOpacity_ = std::max(backgroundOpacity_ - fadeSpeed_ * deltaTime, targetOpacity);
    }
}

void ChatWindow::scrollUp(int lines) {
    int maxScroll = getMaxScrollOffset();
    int& activeScrollOffset = (activeTabIndex_ == 1) ? combatScrollOffset_ : scrollOffset_;
    activeScrollOffset = std::min(activeScrollOffset + lines, maxScroll);
}

void ChatWindow::scrollDown(int lines) {
    int& activeScrollOffset = (activeTabIndex_ == 1) ? combatScrollOffset_ : scrollOffset_;
    activeScrollOffset = std::max(0, activeScrollOffset - lines);
}

void ChatWindow::scrollToBottom() {
    int& activeScrollOffset = (activeTabIndex_ == 1) ? combatScrollOffset_ : scrollOffset_;
    activeScrollOffset = 0;
}

bool ChatWindow::isScrolledToBottom() const {
    int activeScrollOffset = (activeTabIndex_ == 1) ? combatScrollOffset_ : scrollOffset_;
    return activeScrollOffset == 0;
}

int ChatWindow::getMaxScrollOffset() const {
    // Select buffer based on active tab
    const ChatMessageBuffer& activeBuffer = (activeTabIndex_ == 1) ? combatBuffer_ : messageBuffer_;

    // Filter messages by enabled channels
    int totalMessages = 0;
    for (const auto& msg : activeBuffer.getMessages()) {
        if (enabledChannels_.count(msg.channel) > 0) {
            totalMessages++;
        }
    }
    return std::max(0, totalMessages - visibleLines_);
}

irr::core::recti ChatWindow::getScrollbarTrackBounds() const {
    irr::core::recti content = getContentArea();
    int scrollbarWidth = getScrollbarWidth();
    int inputY = content.LowerRightCorner.Y - getInputFieldHeight();

    // Scrollbar is on the right side of the content area, below the tab bar
    int x = content.LowerRightCorner.X - scrollbarWidth;
    int y = content.UpperLeftCorner.Y + tabBarHeight_;  // Start below tab bar
    int height = inputY - y - 2;

    return irr::core::recti(x, y, x + scrollbarWidth, y + height);
}

irr::core::recti ChatWindow::getScrollbarThumbBounds() const {
    irr::core::recti track = getScrollbarTrackBounds();
    int scrollButtonHeight = getScrollButtonHeight();

    // Account for scroll buttons
    int trackTop = track.UpperLeftCorner.Y + scrollButtonHeight;
    int trackBottom = track.LowerRightCorner.Y - scrollButtonHeight;
    int trackHeight = trackBottom - trackTop;

    if (trackHeight <= 0) {
        return irr::core::recti(track.UpperLeftCorner.X, trackTop,
                               track.LowerRightCorner.X, trackTop + 10);
    }

    int maxOffset = getMaxScrollOffset();
    if (maxOffset <= 0) {
        // No scrolling needed - thumb fills track
        return irr::core::recti(track.UpperLeftCorner.X, trackTop,
                               track.LowerRightCorner.X, trackBottom);
    }

    // Calculate thumb size (minimum 20 pixels)
    int thumbHeight = std::max(20, trackHeight * visibleLines_ / (visibleLines_ + maxOffset));

    // Use active scroll offset based on current tab
    int activeScrollOffset = (activeTabIndex_ == 1) ? combatScrollOffset_ : scrollOffset_;

    // Calculate thumb position (inverted because scrollOffset 0 = bottom)
    float scrollRatio = (maxOffset > 0) ? static_cast<float>(maxOffset - activeScrollOffset) / maxOffset : 0.0f;
    int thumbY = trackTop + static_cast<int>((trackHeight - thumbHeight) * scrollRatio);

    return irr::core::recti(track.UpperLeftCorner.X, thumbY,
                           track.LowerRightCorner.X, thumbY + thumbHeight);
}

bool ChatWindow::isOnScrollbar(int x, int y) const {
    irr::core::recti track = getScrollbarTrackBounds();
    return track.isPointInside(irr::core::vector2di(x, y));
}

bool ChatWindow::isOnScrollUpButton(int x, int y) const {
    irr::core::recti track = getScrollbarTrackBounds();
    int scrollButtonHeight = getScrollButtonHeight();
    irr::core::recti upBtn(track.UpperLeftCorner.X, track.UpperLeftCorner.Y,
                          track.LowerRightCorner.X, track.UpperLeftCorner.Y + scrollButtonHeight);
    return upBtn.isPointInside(irr::core::vector2di(x, y));
}

bool ChatWindow::isOnScrollDownButton(int x, int y) const {
    irr::core::recti track = getScrollbarTrackBounds();
    int scrollButtonHeight = getScrollButtonHeight();
    irr::core::recti downBtn(track.UpperLeftCorner.X, track.LowerRightCorner.Y - scrollButtonHeight,
                            track.LowerRightCorner.X, track.LowerRightCorner.Y);
    return downBtn.isPointInside(irr::core::vector2di(x, y));
}

void ChatWindow::renderScrollbar(irr::video::IVideoDriver* driver) {
    irr::core::recti track = getScrollbarTrackBounds();
    int scrollButtonHeight = getScrollButtonHeight();

    // Colors from UISettings
    const auto& scrollbarSettings = UISettings::instance().scrollbar();
    irr::video::SColor trackColor = scrollbarSettings.trackColor;
    irr::video::SColor thumbColor = scrollbarSettings.thumbColor;
    irr::video::SColor thumbHoverColor = scrollbarSettings.thumbHoverColor;
    irr::video::SColor buttonColor = scrollbarSettings.buttonColor;
    irr::video::SColor buttonHoverColor = scrollbarSettings.buttonHoverColor;
    irr::video::SColor arrowColor = scrollbarSettings.arrowColor;

    // Draw track background
    driver->draw2DRectangle(trackColor, track);

    // Draw up button
    irr::core::recti upBtn(track.UpperLeftCorner.X, track.UpperLeftCorner.Y,
                          track.LowerRightCorner.X, track.UpperLeftCorner.Y + scrollButtonHeight);
    driver->draw2DRectangle(scrollUpHovered_ ? buttonHoverColor : buttonColor, upBtn);

    // Draw up arrow
    int upCenterX = (upBtn.UpperLeftCorner.X + upBtn.LowerRightCorner.X) / 2;
    int upCenterY = (upBtn.UpperLeftCorner.Y + upBtn.LowerRightCorner.Y) / 2;
    driver->draw2DLine(irr::core::vector2di(upCenterX, upCenterY - 3),
                       irr::core::vector2di(upCenterX - 4, upCenterY + 2), arrowColor);
    driver->draw2DLine(irr::core::vector2di(upCenterX, upCenterY - 3),
                       irr::core::vector2di(upCenterX + 4, upCenterY + 2), arrowColor);

    // Draw down button
    irr::core::recti downBtn(track.UpperLeftCorner.X, track.LowerRightCorner.Y - scrollButtonHeight,
                            track.LowerRightCorner.X, track.LowerRightCorner.Y);
    driver->draw2DRectangle(scrollDownHovered_ ? buttonHoverColor : buttonColor, downBtn);

    // Draw down arrow
    int downCenterX = (downBtn.UpperLeftCorner.X + downBtn.LowerRightCorner.X) / 2;
    int downCenterY = (downBtn.UpperLeftCorner.Y + downBtn.LowerRightCorner.Y) / 2;
    driver->draw2DLine(irr::core::vector2di(downCenterX, downCenterY + 3),
                       irr::core::vector2di(downCenterX - 4, downCenterY - 2), arrowColor);
    driver->draw2DLine(irr::core::vector2di(downCenterX, downCenterY + 3),
                       irr::core::vector2di(downCenterX + 4, downCenterY - 2), arrowColor);

    // Draw thumb (only if there's content to scroll)
    int maxOffset = getMaxScrollOffset();
    if (maxOffset > 0) {
        irr::core::recti thumb = getScrollbarThumbBounds();
        irr::video::SColor currentThumbColor = draggingScrollbar_ ? thumbHoverColor : thumbColor;
        driver->draw2DRectangle(currentThumbColor, thumb);
    }
}

void ChatWindow::setMinSize(int width, int height) {
    minWidth_ = width;
    minHeight_ = height;
    constrainSize();
}

void ChatWindow::setMaxSize(int width, int height) {
    maxWidth_ = width;
    maxHeight_ = height;
    constrainSize();
}

int ChatWindow::calculateVisibleLines() const {
    irr::core::recti content = getContentArea();
    int messageAreaHeight = content.getHeight() - getInputFieldHeight() - 2;
    int lineHeight = 12;  // Approximate font height
    return std::max(1, messageAreaHeight / lineHeight);
}

bool ChatWindow::isOnResizeEdge(int x, int y) const {
    // Check upper-right corner grip area
    const int gripSize = 12;
    int gripX = bounds_.LowerRightCorner.X - gripSize;
    int gripY = bounds_.UpperLeftCorner.Y;

    return (x >= gripX && x <= bounds_.LowerRightCorner.X &&
            y >= gripY && y <= gripY + gripSize);
}

void ChatWindow::constrainSize() {
    int width = getWidth();
    int height = getHeight();

    width = std::max(minWidth_, std::min(width, maxWidth_));
    height = std::max(minHeight_, std::min(height, maxHeight_));

    setSize(width, height);
}

void ChatWindow::saveSettings(const std::string& filename) {
    Json::Value root;

    // Window position and size
    root["x"] = getX();
    root["y"] = getY();
    root["width"] = getWidth();
    root["height"] = getHeight();

    // Display options
    root["showTimestamps"] = showTimestamps_;

    // Channel filter settings
    Json::Value channelFilters;
    // Save all possible channels with their enabled state
    std::vector<ChatChannel> allChannels = {
        ChatChannel::Guild, ChatChannel::Group, ChatChannel::Shout,
        ChatChannel::Auction, ChatChannel::OOC, ChatChannel::Broadcast,
        ChatChannel::Tell, ChatChannel::Say, ChatChannel::Emote,
        ChatChannel::GMSay, ChatChannel::Raid, ChatChannel::Combat,
        ChatChannel::CombatSelf, ChatChannel::CombatMiss, ChatChannel::Experience,
        ChatChannel::Loot, ChatChannel::Spell, ChatChannel::System,
        ChatChannel::Error, ChatChannel::NPCDialogue
    };
    for (auto ch : allChannels) {
        channelFilters[std::to_string(static_cast<int>(ch))] = isChannelEnabled(ch);
    }
    root["channelFilters"] = channelFilters;

    // Write to file
    std::ofstream file(filename);
    if (file.is_open()) {
        Json::StreamWriterBuilder builder;
        builder["indentation"] = "  ";
        std::unique_ptr<Json::StreamWriter> writer(builder.newStreamWriter());
        writer->write(root, &file);
    }
}

void ChatWindow::loadSettings(const std::string& filename) {
    std::ifstream file(filename);
    if (!file.is_open()) {
        return;  // No settings file yet, use defaults
    }

    Json::Value root;
    Json::CharReaderBuilder builder;
    std::string errors;

    if (!Json::parseFromStream(builder, file, &root, &errors)) {
        return;  // Parse error, use defaults
    }

    // Load window position and size
    if (root.isMember("x") && root.isMember("y")) {
        int x = root["x"].asInt();
        int y = root["y"].asInt();
        setPosition(x, y);
    }

    if (root.isMember("width") && root.isMember("height")) {
        int width = root["width"].asInt();
        int height = root["height"].asInt();
        setSize(width, height);
        constrainSize();
    }

    // Load display options
    if (root.isMember("showTimestamps")) {
        showTimestamps_ = root["showTimestamps"].asBool();
    }

    // Load channel filter settings
    // Note: We don't clear enabledChannels_ here - new channels added after the
    // settings file was saved should remain at their default (enabled) state.
    // We only override channels that are explicitly listed in the settings file.
    if (root.isMember("channelFilters") && root["channelFilters"].isObject()) {
        const Json::Value& filters = root["channelFilters"];
        for (const auto& key : filters.getMemberNames()) {
            int channelValue = std::stoi(key);
            ChatChannel channel = static_cast<ChatChannel>(channelValue);
            if (filters[key].asBool()) {
                enabledChannels_.insert(channel);
            } else {
                enabledChannels_.erase(channel);
            }
        }
    }

    // Recalculate visible lines
    visibleLines_ = calculateVisibleLines();
}

void ChatWindow::initDefaultChannels() {
    // Enable all channels by default
    enabledChannels_.insert(ChatChannel::Guild);
    enabledChannels_.insert(ChatChannel::Group);
    enabledChannels_.insert(ChatChannel::Shout);
    enabledChannels_.insert(ChatChannel::Auction);
    enabledChannels_.insert(ChatChannel::OOC);
    enabledChannels_.insert(ChatChannel::Broadcast);
    enabledChannels_.insert(ChatChannel::Tell);
    enabledChannels_.insert(ChatChannel::Say);
    enabledChannels_.insert(ChatChannel::Emote);
    enabledChannels_.insert(ChatChannel::GMSay);
    enabledChannels_.insert(ChatChannel::Raid);
    enabledChannels_.insert(ChatChannel::Combat);
    enabledChannels_.insert(ChatChannel::CombatSelf);
    enabledChannels_.insert(ChatChannel::CombatMiss);
    enabledChannels_.insert(ChatChannel::Experience);
    enabledChannels_.insert(ChatChannel::Loot);
    enabledChannels_.insert(ChatChannel::Spell);
    enabledChannels_.insert(ChatChannel::System);
    enabledChannels_.insert(ChatChannel::Error);
    enabledChannels_.insert(ChatChannel::NPCDialogue);
}

void ChatWindow::initCombatChannels() {
    // Default channels routed to combat tab
    combatChannels_.insert(ChatChannel::Combat);
    combatChannels_.insert(ChatChannel::CombatSelf);
    combatChannels_.insert(ChatChannel::CombatMiss);
    combatChannels_.insert(ChatChannel::Spell);
    combatChannels_.insert(ChatChannel::Loot);
    combatChannels_.insert(ChatChannel::Experience);
}

void ChatWindow::applyTabSettings() {
    // Apply tab settings from UISettings
    // This allows the main config to override the default combat channels
    const auto& chatSettings = UISettings::instance().chat();

    // Look for "combat" tab settings
    auto it = chatSettings.tabs.find("combat");
    if (it != chatSettings.tabs.end()) {
        const auto& combatTabSettings = it->second;

        if (combatTabSettings.enabled) {
            // Clear defaults and apply configured channels
            combatChannels_.clear();
            for (int ch : combatTabSettings.channels) {
                combatChannels_.insert(static_cast<ChatChannel>(ch));
            }

            // Apply echo setting
            echoToMain_ = combatTabSettings.echoToMain;
        } else {
            // Combat tab disabled - all messages go to main
            combatChannels_.clear();
            echoToMain_ = false;
        }
    }
}

bool ChatWindow::isCombatChannel(ChatChannel channel) const {
    return combatChannels_.find(channel) != combatChannels_.end();
}

void ChatWindow::setChannelEnabled(ChatChannel channel, bool enabled) {
    if (enabled) {
        enabledChannels_.insert(channel);
    } else {
        enabledChannels_.erase(channel);
    }
    // Invalidate cache since visible messages changed
    invalidateWrappedLineCache();
}

bool ChatWindow::isChannelEnabled(ChatChannel channel) const {
    bool enabled = enabledChannels_.find(channel) != enabledChannels_.end();
    // Debug: log channel enable status for combat channels
    if (static_cast<int>(channel) >= 100 && static_cast<int>(channel) <= 105) {
        LOG_DEBUG(MOD_UI, "[ChatWindow] isChannelEnabled: channel={} enabled={} (enabledChannels_.size={})",
                  static_cast<int>(channel), enabled, enabledChannels_.size());
    }
    return enabled;
}

void ChatWindow::toggleChannel(ChatChannel channel) {
    if (isChannelEnabled(channel)) {
        enabledChannels_.erase(channel);
    } else {
        enabledChannels_.insert(channel);
    }
    // Invalidate cache since visible messages changed
    invalidateWrappedLineCache();
}

void ChatWindow::enableAllChannels() {
    initDefaultChannels();
    // Invalidate cache since visible messages changed
    invalidateWrappedLineCache();
}

void ChatWindow::disableAllChannels() {
    enabledChannels_.clear();
    // Always keep system and error messages visible
    enabledChannels_.insert(ChatChannel::System);
    enabledChannels_.insert(ChatChannel::Error);
    // Invalidate cache since visible messages changed
    invalidateWrappedLineCache();
}

void ChatWindow::renderMessageWithLinks(irr::video::IVideoDriver* driver,
                                         irr::gui::IGUIFont* font,
                                         const ChatMessage& msg,
                                         const std::string& formattedText,
                                         int x, int y, int maxWidth,
                                         size_t messageIndex) {
    // Calculate offset from formatting (timestamp, channel prefix, etc.)
    // For NPCDialogue, the text is displayed as-is, so we need to find where
    // msg.text starts within formattedText
    size_t textOffset = 0;
    size_t textPos = formattedText.find(msg.text);
    if (textPos != std::string::npos) {
        textOffset = textPos;
    }

    int lineHeight = 12;
    int currentX = x;

    // Track how much of the formatted text we've rendered
    size_t pos = 0;
    size_t linkIdx = 0;

    while (pos < formattedText.length() && currentX < x + maxWidth) {
        // Find the next link that starts at or after current position
        const MessageLink* nextLink = nullptr;
        size_t nextLinkStart = formattedText.length();

        if (linkIdx < msg.links.size()) {
            // Adjust link position by the text offset
            size_t adjustedStart = msg.links[linkIdx].startPos + textOffset;
            if (adjustedStart >= pos && adjustedStart < formattedText.length()) {
                nextLink = &msg.links[linkIdx];
                nextLinkStart = adjustedStart;
            }
        }

        // Render text before the next link (or to the end)
        if (pos < nextLinkStart) {
            std::string segment = formattedText.substr(pos, nextLinkStart - pos);
            std::wstring segmentW(segment.begin(), segment.end());

            // Measure and render
            irr::core::dimension2du dim = font->getDimension(segmentW.c_str());

            // Check if it fits
            if (currentX + static_cast<int>(dim.Width) > x + maxWidth) {
                // Truncate
                while (segmentW.length() > 1) {
                    segmentW = segmentW.substr(0, segmentW.length() - 1);
                    dim = font->getDimension(segmentW.c_str());
                    if (currentX + static_cast<int>(dim.Width) <= x + maxWidth) break;
                }
            }

            if (!segmentW.empty()) {
                font->draw(segmentW.c_str(),
                           irr::core::recti(currentX, y, currentX + dim.Width, y + lineHeight),
                           msg.color);
                currentX += dim.Width;
            }

            pos = nextLinkStart;
        }

        // Render the link if there is one
        if (nextLink && pos < formattedText.length()) {
            size_t adjustedEnd = nextLink->endPos + textOffset;
            if (adjustedEnd > formattedText.length()) {
                adjustedEnd = formattedText.length();
            }

            std::string linkText = formattedText.substr(pos, adjustedEnd - pos);
            std::wstring linkTextW(linkText.begin(), linkText.end());

            // Measure the link
            irr::core::dimension2du dim = font->getDimension(linkTextW.c_str());

            // Check if it fits
            bool truncated = false;
            if (currentX + static_cast<int>(dim.Width) > x + maxWidth) {
                // Truncate
                while (linkTextW.length() > 1) {
                    linkTextW = linkTextW.substr(0, linkTextW.length() - 1);
                    dim = font->getDimension(linkTextW.c_str());
                    if (currentX + static_cast<int>(dim.Width) <= x + maxWidth) break;
                }
                truncated = true;
            }

            if (!linkTextW.empty()) {
                // Choose color based on link type
                irr::video::SColor linkColor = (nextLink->type == LinkType::Item)
                    ? getLinkColorItem() : getLinkColorNpc();

                // Draw the link text
                font->draw(linkTextW.c_str(),
                           irr::core::recti(currentX, y, currentX + dim.Width, y + lineHeight),
                           linkColor);

                // Record the rendered link position for click detection
                RenderedLink rendered;
                rendered.bounds = irr::core::recti(currentX, y, currentX + dim.Width, y + lineHeight);
                rendered.link = *nextLink;
                rendered.messageIndex = messageIndex;
                renderedLinks_.push_back(rendered);

                // Check if mouse is hovering over this link and draw underline
                irr::core::vector2di mousePos(lastMouseX_, lastMouseY_);
                if (rendered.bounds.isPointInside(mousePos)) {
                    // Draw underline
                    int underlineY = y + lineHeight - 2;
                    driver->draw2DLine(
                        irr::core::vector2di(currentX, underlineY),
                        irr::core::vector2di(currentX + dim.Width, underlineY),
                        linkColor);
                }

                currentX += dim.Width;
            }

            pos = adjustedEnd;
            linkIdx++;

            if (truncated) break;  // Stop rendering if we had to truncate
        }
    }
}

} // namespace ui
} // namespace eqt
