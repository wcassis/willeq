/*
 * WillEQ Skills Window
 *
 * Displays player skills with name, category, and current/max values.
 * Supports skill selection, activation, and hotbar button creation.
 */

#pragma once

#include "window_base.h"
#include "ui_settings.h"
#include "skill_tooltip.h"
#include <vector>
#include <functional>
#include <cstdint>

namespace EQ {
class SkillManager;
struct SkillData;
}

namespace eqt {
namespace ui {

// Callback types
using SkillActivateCallback = std::function<void(uint8_t skill_id)>;
using HotbarCreateCallback = std::function<void(uint8_t skill_id)>;

class SkillsWindow : public WindowBase {
public:
    SkillsWindow();
    ~SkillsWindow() = default;

    // Set skill manager reference
    void setSkillManager(EQ::SkillManager* mgr) { skillMgr_ = mgr; }

    // Position window in default location
    void positionDefault(int screenWidth, int screenHeight);

    // Refresh skill list from manager
    void refresh();

    // Rendering
    void render(irr::video::IVideoDriver* driver,
                irr::gui::IGUIEnvironment* gui) override;

    // Input handling
    bool handleMouseDown(int x, int y, bool leftButton, bool shift, bool ctrl = false) override;
    bool handleMouseUp(int x, int y, bool leftButton) override;
    bool handleMouseMove(int x, int y) override;
    bool handleMouseWheel(float delta);

    // Selection
    int8_t getSelectedSkillId() const { return selectedSkillId_; }
    void clearSelection();

    // Callbacks
    void setActivateCallback(SkillActivateCallback cb) { activateCallback_ = std::move(cb); }
    void setHotbarCallback(HotbarCreateCallback cb) { hotbarCallback_ = std::move(cb); }

protected:
    void renderContent(irr::video::IVideoDriver* driver,
                      irr::gui::IGUIEnvironment* gui) override;

private:
    // Layout initialization
    void initializeLayout();

    // Rendering helpers
    void renderColumnHeaders(irr::video::IVideoDriver* driver,
                            irr::gui::IGUIEnvironment* gui);
    void renderSkillList(irr::video::IVideoDriver* driver,
                        irr::gui::IGUIEnvironment* gui);
    void renderSkillRow(irr::video::IVideoDriver* driver,
                       irr::gui::IGUIEnvironment* gui,
                       const EQ::SkillData* skill,
                       int rowIndex,
                       int yPos);
    void renderActionButtons(irr::video::IVideoDriver* driver,
                            irr::gui::IGUIEnvironment* gui);
    void renderScrollbar(irr::video::IVideoDriver* driver);

    // Hit testing
    int getSkillAtPosition(int x, int y) const;
    bool isInScrollbar(int x, int y) const;
    bool isInScrollThumb(int x, int y) const;

    // Scrolling
    void scrollUp(int rows = 1);
    void scrollDown(int rows = 1);
    int getMaxScrollOffset() const;
    void clampScrollOffset();

    // Calculate visible rows
    int getVisibleRowCount() const;

    // Skill manager reference
    EQ::SkillManager* skillMgr_ = nullptr;

    // Cached skill list (sorted)
    std::vector<const EQ::SkillData*> skills_;

    // Selection state
    int8_t selectedSkillId_ = -1;
    int8_t hoveredSkillId_ = -1;

    // Scroll state
    int scrollOffset_ = 0;          // Index of first visible skill
    bool scrollbarDragging_ = false;
    int scrollbarDragStartY_ = 0;
    int scrollbarDragStartOffset_ = 0;

    // Button hover states
    bool activateButtonHovered_ = false;
    bool hotbarButtonHovered_ = false;

    // Layout bounds
    irr::core::recti headerBounds_;
    irr::core::recti listBounds_;
    irr::core::recti scrollbarBounds_;
    irr::core::recti scrollUpButtonBounds_;
    irr::core::recti scrollDownButtonBounds_;
    irr::core::recti scrollTrackBounds_;
    irr::core::recti scrollThumbBounds_;
    irr::core::recti activateButtonBounds_;
    irr::core::recti hotbarButtonBounds_;

    // Layout constants
    static constexpr int WINDOW_WIDTH = 340;
    static constexpr int WINDOW_HEIGHT = 400;
    static constexpr int ROW_HEIGHT = 20;
    static constexpr int HEADER_HEIGHT = 22;
    static constexpr int BUTTON_AREA_HEIGHT = 36;
    static constexpr int SCROLLBAR_WIDTH = 14;
    static constexpr int SCROLLBAR_BUTTON_HEIGHT = 14;
    static constexpr int NAME_COLUMN_WIDTH = 140;
    static constexpr int CATEGORY_COLUMN_WIDTH = 70;
    static constexpr int VALUE_COLUMN_WIDTH = 80;
    static constexpr int COLUMN_PADDING = 4;
    static constexpr int BUTTON_WIDTH = 100;
    static constexpr int BUTTON_HEIGHT = 24;
    static constexpr int BUTTON_SPACING = 20;

    // Colors
    irr::video::SColor getRowBackground() const;
    irr::video::SColor getRowAlternate() const;
    irr::video::SColor getRowSelected() const;
    irr::video::SColor getRowHovered() const;
    irr::video::SColor getHeaderBackground() const;
    irr::video::SColor getHeaderTextColor() const;
    irr::video::SColor getTextColor() const;
    irr::video::SColor getCategoryTextColor() const;
    irr::video::SColor getValueTextColor() const;
    irr::video::SColor getScrollbarBackground() const;
    irr::video::SColor getScrollbarThumb() const;

    // Callbacks
    SkillActivateCallback activateCallback_;
    HotbarCreateCallback hotbarCallback_;

    // Tooltip
    SkillTooltip tooltip_;
    uint32_t currentTimeMs_ = 0;
};

} // namespace ui
} // namespace eqt
