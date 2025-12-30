/*
 * WillEQ Note Window
 *
 * Displays note and book item text content in a scrollable window.
 * Used when players right-click on readable items.
 */

#pragma once

#include "window_base.h"
#include <cstdint>
#include <vector>
#include <string>

namespace eqt {
namespace ui {

class NoteWindow : public WindowBase {
public:
    NoteWindow();
    ~NoteWindow() = default;

    // Open window with text content
    // type: 0=scroll/note, 1=book, 2=item info
    void open(const std::string& text, uint8_t type);

    // Close the window
    void close();

    // Position window in center of screen
    void positionDefault(int screenWidth, int screenHeight);

    // Rendering
    void render(irr::video::IVideoDriver* driver,
                irr::gui::IGUIEnvironment* gui) override;

    // Input handling
    bool handleMouseDown(int x, int y, bool leftButton, bool shift, bool ctrl = false) override;
    bool handleMouseUp(int x, int y, bool leftButton) override;
    bool handleMouseMove(int x, int y) override;
    bool handleMouseWheel(float delta);

protected:
    void renderContent(irr::video::IVideoDriver* driver,
                      irr::gui::IGUIEnvironment* gui) override;

private:
    // Layout initialization
    void initializeLayout();

    // Text processing
    void processText(const std::string& rawText);
    std::vector<std::wstring> wrapText(const std::wstring& text, irr::gui::IGUIFont* font, int maxWidth);

    // Rendering helpers
    void renderTextContent(irr::video::IVideoDriver* driver,
                          irr::gui::IGUIEnvironment* gui);
    void renderScrollbar(irr::video::IVideoDriver* driver);
    void renderCloseButton(irr::video::IVideoDriver* driver,
                          irr::gui::IGUIEnvironment* gui);

    // Hit testing
    bool isInScrollbar(int x, int y) const;
    bool isInScrollThumb(int x, int y) const;
    bool isInCloseButton(int x, int y) const;

    // Scrolling
    void scrollUp(int lines = 1);
    void scrollDown(int lines = 1);
    int getMaxScrollOffset() const;
    void clampScrollOffset();
    int getVisibleLineCount() const;

    // Content
    std::vector<std::wstring> lines_;      // Wrapped text lines
    uint8_t bookType_ = 0;                 // 0=scroll, 1=book, 2=item info

    // Scroll state
    int scrollOffset_ = 0;                 // Index of first visible line
    bool scrollbarDragging_ = false;
    int scrollbarDragStartY_ = 0;
    int scrollbarDragStartOffset_ = 0;

    // Button state
    bool closeButtonHovered_ = false;

    // Layout bounds (relative to content area)
    irr::core::recti textBounds_;
    irr::core::recti scrollbarBounds_;
    irr::core::recti scrollUpButtonBounds_;
    irr::core::recti scrollDownButtonBounds_;
    irr::core::recti scrollTrackBounds_;
    irr::core::recti scrollThumbBounds_;
    irr::core::recti closeButtonBounds_;

    // Layout constants
    static constexpr int WINDOW_WIDTH = 360;
    static constexpr int WINDOW_HEIGHT = 400;
    static constexpr int LINE_HEIGHT = 16;
    static constexpr int SCROLLBAR_WIDTH = 14;
    static constexpr int SCROLLBAR_BUTTON_HEIGHT = 14;
    static constexpr int TEXT_PADDING = 8;
    static constexpr int BUTTON_WIDTH = 80;
    static constexpr int BUTTON_HEIGHT = 24;
    static constexpr int BUTTON_AREA_HEIGHT = 36;

    // Colors
    irr::video::SColor getTextColor() const;
    irr::video::SColor getTextBackground() const;
    irr::video::SColor getScrollbarBackground() const;
    irr::video::SColor getScrollbarThumb() const;
};

} // namespace ui
} // namespace eqt
