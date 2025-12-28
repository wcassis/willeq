/*
 * WillEQ Skills Window Implementation
 */

#include "client/graphics/ui/skills_window.h"
#include "client/skill/skill_manager.h"
#include "client/skill/skill_data.h"
#include "client/skill/skill_constants.h"
#include <algorithm>
#include <chrono>

namespace eqt {
namespace ui {

// ============================================================================
// Color Definitions
// ============================================================================

irr::video::SColor SkillsWindow::getRowBackground() const {
    return irr::video::SColor(255, 40, 40, 50);
}

irr::video::SColor SkillsWindow::getRowAlternate() const {
    return irr::video::SColor(255, 50, 50, 60);
}

irr::video::SColor SkillsWindow::getRowSelected() const {
    return irr::video::SColor(255, 80, 80, 120);
}

irr::video::SColor SkillsWindow::getRowHovered() const {
    return irr::video::SColor(255, 60, 60, 90);
}

irr::video::SColor SkillsWindow::getHeaderBackground() const {
    return irr::video::SColor(255, 60, 60, 80);
}

irr::video::SColor SkillsWindow::getHeaderTextColor() const {
    return irr::video::SColor(255, 220, 220, 220);
}

irr::video::SColor SkillsWindow::getTextColor() const {
    return irr::video::SColor(255, 200, 200, 200);
}

irr::video::SColor SkillsWindow::getCategoryTextColor() const {
    return irr::video::SColor(255, 150, 180, 150);
}

irr::video::SColor SkillsWindow::getValueTextColor() const {
    return irr::video::SColor(255, 180, 180, 220);
}

irr::video::SColor SkillsWindow::getScrollbarBackground() const {
    return irr::video::SColor(255, 30, 30, 40);
}

irr::video::SColor SkillsWindow::getScrollbarThumb() const {
    return irr::video::SColor(255, 100, 100, 120);
}

// ============================================================================
// Constructor
// ============================================================================

SkillsWindow::SkillsWindow()
    : WindowBase(L"Skills", WINDOW_WIDTH, WINDOW_HEIGHT)
{
    initializeLayout();
}

void SkillsWindow::initializeLayout()
{
    int borderWidth = getBorderWidth();
    int contentPadding = getContentPadding();
    int titleBarHeight = getTitleBarHeight();

    // Calculate content area bounds (relative to window)
    int contentX = borderWidth + contentPadding;
    int contentY = titleBarHeight + borderWidth + contentPadding;
    int contentWidth = WINDOW_WIDTH - 2 * (borderWidth + contentPadding);
    int contentHeight = WINDOW_HEIGHT - titleBarHeight - 2 * (borderWidth + contentPadding);

    // Header row
    headerBounds_ = irr::core::recti(
        0, 0,
        contentWidth - SCROLLBAR_WIDTH, HEADER_HEIGHT
    );

    // Skill list area (between header and buttons)
    int listHeight = contentHeight - HEADER_HEIGHT - BUTTON_AREA_HEIGHT;
    listBounds_ = irr::core::recti(
        0, HEADER_HEIGHT,
        contentWidth - SCROLLBAR_WIDTH, HEADER_HEIGHT + listHeight
    );

    // Scrollbar (full area including buttons)
    scrollbarBounds_ = irr::core::recti(
        contentWidth - SCROLLBAR_WIDTH, HEADER_HEIGHT,
        contentWidth, HEADER_HEIGHT + listHeight
    );

    // Scrollbar up button
    scrollUpButtonBounds_ = irr::core::recti(
        contentWidth - SCROLLBAR_WIDTH, HEADER_HEIGHT,
        contentWidth, HEADER_HEIGHT + SCROLLBAR_BUTTON_HEIGHT
    );

    // Scrollbar down button
    scrollDownButtonBounds_ = irr::core::recti(
        contentWidth - SCROLLBAR_WIDTH, HEADER_HEIGHT + listHeight - SCROLLBAR_BUTTON_HEIGHT,
        contentWidth, HEADER_HEIGHT + listHeight
    );

    // Scrollbar track (between buttons)
    scrollTrackBounds_ = irr::core::recti(
        contentWidth - SCROLLBAR_WIDTH, HEADER_HEIGHT + SCROLLBAR_BUTTON_HEIGHT,
        contentWidth, HEADER_HEIGHT + listHeight - SCROLLBAR_BUTTON_HEIGHT
    );

    // Buttons at bottom
    int buttonY = HEADER_HEIGHT + listHeight + (BUTTON_AREA_HEIGHT - BUTTON_HEIGHT) / 2;
    int totalButtonWidth = BUTTON_WIDTH * 2 + BUTTON_SPACING;
    int buttonStartX = (contentWidth - totalButtonWidth) / 2;

    activateButtonBounds_ = irr::core::recti(
        buttonStartX, buttonY,
        buttonStartX + BUTTON_WIDTH, buttonY + BUTTON_HEIGHT
    );

    hotbarButtonBounds_ = irr::core::recti(
        buttonStartX + BUTTON_WIDTH + BUTTON_SPACING, buttonY,
        buttonStartX + BUTTON_WIDTH * 2 + BUTTON_SPACING, buttonY + BUTTON_HEIGHT
    );
}

void SkillsWindow::positionDefault(int screenWidth, int screenHeight)
{
    // Position in center-left of screen
    int x = 50;
    int y = (screenHeight - WINDOW_HEIGHT) / 2;
    setPosition(x, y);
}

// ============================================================================
// Skill Management
// ============================================================================

void SkillsWindow::refresh()
{
    skills_.clear();

    if (!skillMgr_) {
        return;
    }

    // Get all skills from manager
    skills_ = skillMgr_->getAllSkills();

    // Sort by category, then by name
    std::sort(skills_.begin(), skills_.end(),
        [](const EQ::SkillData* a, const EQ::SkillData* b) {
            if (a->category != b->category) {
                return static_cast<int>(a->category) < static_cast<int>(b->category);
            }
            return a->name < b->name;
        });

    // Clamp scroll offset
    clampScrollOffset();
}

void SkillsWindow::clearSelection()
{
    selectedSkillId_ = -1;
}

// ============================================================================
// Rendering
// ============================================================================

void SkillsWindow::render(irr::video::IVideoDriver* driver,
                          irr::gui::IGUIEnvironment* gui)
{
    if (!visible_) return;

    // Draw base window (title bar, background, borders)
    WindowBase::render(driver, gui);
}

void SkillsWindow::renderContent(irr::video::IVideoDriver* driver,
                                 irr::gui::IGUIEnvironment* gui)
{
    // Refresh skill list if needed
    if (skills_.empty() && skillMgr_) {
        refresh();
    }

    renderColumnHeaders(driver, gui);
    renderSkillList(driver, gui);
    renderScrollbar(driver);
    renderActionButtons(driver, gui);

    // Update and render tooltip
    auto now = std::chrono::steady_clock::now();
    currentTimeMs_ = static_cast<uint32_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count()
    );
    tooltip_.update(currentTimeMs_, 0, 0);  // Mouse pos already set in handleMouseMove

    // Get screen dimensions from driver
    if (driver) {
        auto screenSize = driver->getScreenSize();
        tooltip_.render(driver, gui, screenSize.Width, screenSize.Height);
    }
}

void SkillsWindow::renderColumnHeaders(irr::video::IVideoDriver* driver,
                                       irr::gui::IGUIEnvironment* gui)
{
    irr::core::recti contentArea = getContentArea();
    int contentX = contentArea.UpperLeftCorner.X;
    int contentY = contentArea.UpperLeftCorner.Y;

    // Header background
    irr::core::recti headerAbs(
        contentX + headerBounds_.UpperLeftCorner.X,
        contentY + headerBounds_.UpperLeftCorner.Y,
        contentX + headerBounds_.LowerRightCorner.X,
        contentY + headerBounds_.LowerRightCorner.Y
    );
    driver->draw2DRectangle(getHeaderBackground(), headerAbs);

    // Draw column headers
    irr::gui::IGUIFont* font = gui ? gui->getBuiltInFont() : nullptr;
    if (!font) return;

    int textY = contentY + headerBounds_.UpperLeftCorner.Y + 4;

    // Name column
    irr::core::recti nameCol(
        contentX + COLUMN_PADDING,
        textY,
        contentX + NAME_COLUMN_WIDTH,
        textY + HEADER_HEIGHT - 4
    );
    font->draw(L"Skill Name", nameCol, getHeaderTextColor(), false, false);

    // Category column
    irr::core::recti catCol(
        contentX + NAME_COLUMN_WIDTH + COLUMN_PADDING,
        textY,
        contentX + NAME_COLUMN_WIDTH + CATEGORY_COLUMN_WIDTH,
        textY + HEADER_HEIGHT - 4
    );
    font->draw(L"Category", catCol, getHeaderTextColor(), false, false);

    // Value column
    irr::core::recti valCol(
        contentX + NAME_COLUMN_WIDTH + CATEGORY_COLUMN_WIDTH + COLUMN_PADDING,
        textY,
        contentX + headerBounds_.LowerRightCorner.X,
        textY + HEADER_HEIGHT - 4
    );
    font->draw(L"Value", valCol, getHeaderTextColor(), false, false);
}

void SkillsWindow::renderSkillList(irr::video::IVideoDriver* driver,
                                   irr::gui::IGUIEnvironment* gui)
{
    irr::core::recti contentArea = getContentArea();
    int contentX = contentArea.UpperLeftCorner.X;
    int contentY = contentArea.UpperLeftCorner.Y;

    // Clip to list area
    irr::core::recti listAbs(
        contentX + listBounds_.UpperLeftCorner.X,
        contentY + listBounds_.UpperLeftCorner.Y,
        contentX + listBounds_.LowerRightCorner.X,
        contentY + listBounds_.LowerRightCorner.Y
    );

    int visibleRows = getVisibleRowCount();
    int endIndex = std::min(scrollOffset_ + visibleRows, static_cast<int>(skills_.size()));

    for (int i = scrollOffset_; i < endIndex; ++i) {
        int rowIndex = i - scrollOffset_;
        int yPos = listBounds_.UpperLeftCorner.Y + rowIndex * ROW_HEIGHT;
        renderSkillRow(driver, gui, skills_[i], i, yPos);
    }
}

void SkillsWindow::renderSkillRow(irr::video::IVideoDriver* driver,
                                  irr::gui::IGUIEnvironment* gui,
                                  const EQ::SkillData* skill,
                                  int rowIndex,
                                  int yPos)
{
    if (!skill) return;

    irr::core::recti contentArea = getContentArea();
    int contentX = contentArea.UpperLeftCorner.X;
    int contentY = contentArea.UpperLeftCorner.Y;

    // Row background
    irr::video::SColor bgColor;
    if (skill->skill_id == selectedSkillId_) {
        bgColor = getRowSelected();
    } else if (skill->skill_id == hoveredSkillId_) {
        bgColor = getRowHovered();
    } else if (rowIndex % 2 == 0) {
        bgColor = getRowBackground();
    } else {
        bgColor = getRowAlternate();
    }

    irr::core::recti rowBounds(
        contentX + listBounds_.UpperLeftCorner.X,
        contentY + yPos,
        contentX + listBounds_.LowerRightCorner.X,
        contentY + yPos + ROW_HEIGHT
    );
    driver->draw2DRectangle(bgColor, rowBounds);

    // Draw text
    irr::gui::IGUIFont* font = gui ? gui->getBuiltInFont() : nullptr;
    if (!font) return;

    int textY = contentY + yPos + 3;

    // Skill name
    std::wstring skillName(skill->name.begin(), skill->name.end());
    irr::core::recti nameCol(
        contentX + COLUMN_PADDING,
        textY,
        contentX + NAME_COLUMN_WIDTH,
        textY + ROW_HEIGHT - 3
    );
    font->draw(skillName.c_str(), nameCol, getTextColor(), false, false);

    // Category
    const char* catName = EQ::getSkillCategoryName(skill->category);
    std::wstring categoryName(catName, catName + strlen(catName));
    irr::core::recti catCol(
        contentX + NAME_COLUMN_WIDTH + COLUMN_PADDING,
        textY,
        contentX + NAME_COLUMN_WIDTH + CATEGORY_COLUMN_WIDTH,
        textY + ROW_HEIGHT - 3
    );
    font->draw(categoryName.c_str(), catCol, getCategoryTextColor(), false, false);

    // Value (current / max)
    std::wstring valueStr = std::to_wstring(skill->current_value) + L" / " +
                           std::to_wstring(skill->max_value);
    irr::core::recti valCol(
        contentX + NAME_COLUMN_WIDTH + CATEGORY_COLUMN_WIDTH + COLUMN_PADDING,
        textY,
        contentX + listBounds_.LowerRightCorner.X - COLUMN_PADDING,
        textY + ROW_HEIGHT - 3
    );
    font->draw(valueStr.c_str(), valCol, getValueTextColor(), false, false);

    // Cooldown indicator for activatable skills
    if (skill->is_activatable) {
        int indicatorX = contentX + listBounds_.LowerRightCorner.X - SCROLLBAR_WIDTH - 18;
        int indicatorY = contentY + yPos + 3;
        int indicatorSize = ROW_HEIGHT - 6;

        if (skill->isOnCooldown()) {
            // Draw red cooldown indicator
            irr::core::recti cdRect(indicatorX, indicatorY,
                                   indicatorX + indicatorSize, indicatorY + indicatorSize);
            driver->draw2DRectangle(irr::video::SColor(200, 200, 60, 60), cdRect);

            // Draw cooldown progress bar inside
            uint32_t remaining = skill->getCooldownRemaining();
            uint32_t total = skill->recast_time_ms;
            if (total > 0) {
                float progress = 1.0f - (static_cast<float>(remaining) / total);
                int progressHeight = static_cast<int>(indicatorSize * progress);
                irr::core::recti progressRect(
                    indicatorX, indicatorY + indicatorSize - progressHeight,
                    indicatorX + indicatorSize, indicatorY + indicatorSize
                );
                driver->draw2DRectangle(irr::video::SColor(200, 60, 200, 60), progressRect);
            }
        } else {
            // Draw green ready indicator
            irr::core::recti readyRect(indicatorX, indicatorY,
                                      indicatorX + indicatorSize, indicatorY + indicatorSize);
            driver->draw2DRectangle(irr::video::SColor(200, 60, 180, 60), readyRect);
        }
    }
}

void SkillsWindow::renderScrollbar(irr::video::IVideoDriver* driver)
{
    irr::core::recti contentArea = getContentArea();
    int contentX = contentArea.UpperLeftCorner.X;
    int contentY = contentArea.UpperLeftCorner.Y;

    irr::video::SColor bgColor = getScrollbarBackground();
    irr::video::SColor thumbColor = getScrollbarThumb();
    irr::video::SColor buttonColor(255, 70, 70, 90);
    irr::video::SColor arrowColor(255, 180, 180, 180);

    // Scrollbar track background
    irr::core::recti trackAbs(
        contentX + scrollTrackBounds_.UpperLeftCorner.X,
        contentY + scrollTrackBounds_.UpperLeftCorner.Y,
        contentX + scrollTrackBounds_.LowerRightCorner.X,
        contentY + scrollTrackBounds_.LowerRightCorner.Y
    );
    driver->draw2DRectangle(bgColor, trackAbs);

    // Up button
    irr::core::recti upBtnAbs(
        contentX + scrollUpButtonBounds_.UpperLeftCorner.X,
        contentY + scrollUpButtonBounds_.UpperLeftCorner.Y,
        contentX + scrollUpButtonBounds_.LowerRightCorner.X,
        contentY + scrollUpButtonBounds_.LowerRightCorner.Y
    );
    driver->draw2DRectangle(buttonColor, upBtnAbs);

    // Draw up arrow
    int arrowCenterX = upBtnAbs.UpperLeftCorner.X + SCROLLBAR_WIDTH / 2;
    int arrowCenterY = upBtnAbs.UpperLeftCorner.Y + SCROLLBAR_BUTTON_HEIGHT / 2;
    driver->draw2DLine(
        irr::core::vector2di(arrowCenterX, arrowCenterY - 3),
        irr::core::vector2di(arrowCenterX - 4, arrowCenterY + 2),
        arrowColor
    );
    driver->draw2DLine(
        irr::core::vector2di(arrowCenterX, arrowCenterY - 3),
        irr::core::vector2di(arrowCenterX + 4, arrowCenterY + 2),
        arrowColor
    );

    // Down button
    irr::core::recti downBtnAbs(
        contentX + scrollDownButtonBounds_.UpperLeftCorner.X,
        contentY + scrollDownButtonBounds_.UpperLeftCorner.Y,
        contentX + scrollDownButtonBounds_.LowerRightCorner.X,
        contentY + scrollDownButtonBounds_.LowerRightCorner.Y
    );
    driver->draw2DRectangle(buttonColor, downBtnAbs);

    // Draw down arrow
    arrowCenterX = downBtnAbs.UpperLeftCorner.X + SCROLLBAR_WIDTH / 2;
    arrowCenterY = downBtnAbs.UpperLeftCorner.Y + SCROLLBAR_BUTTON_HEIGHT / 2;
    driver->draw2DLine(
        irr::core::vector2di(arrowCenterX, arrowCenterY + 3),
        irr::core::vector2di(arrowCenterX - 4, arrowCenterY - 2),
        arrowColor
    );
    driver->draw2DLine(
        irr::core::vector2di(arrowCenterX, arrowCenterY + 3),
        irr::core::vector2di(arrowCenterX + 4, arrowCenterY - 2),
        arrowColor
    );

    // Calculate thumb size and position within track
    int totalRows = static_cast<int>(skills_.size());
    int visibleRows = getVisibleRowCount();
    int maxScroll = getMaxScrollOffset();

    if (maxScroll <= 0 || skills_.empty()) {
        // No scrolling needed - thumb fills entire track
        scrollThumbBounds_ = scrollTrackBounds_;
        driver->draw2DRectangle(thumbColor, trackAbs);
        return;
    }

    int trackHeight = scrollTrackBounds_.getHeight();
    int thumbHeight = std::max(20, trackHeight * visibleRows / totalRows);
    int thumbTravel = trackHeight - thumbHeight;
    int thumbY = scrollTrackBounds_.UpperLeftCorner.Y +
                 (thumbTravel * scrollOffset_ / maxScroll);

    scrollThumbBounds_ = irr::core::recti(
        scrollTrackBounds_.UpperLeftCorner.X,
        thumbY,
        scrollTrackBounds_.LowerRightCorner.X,
        thumbY + thumbHeight
    );

    irr::core::recti thumbAbs(
        contentX + scrollThumbBounds_.UpperLeftCorner.X,
        contentY + scrollThumbBounds_.UpperLeftCorner.Y,
        contentX + scrollThumbBounds_.LowerRightCorner.X,
        contentY + scrollThumbBounds_.LowerRightCorner.Y
    );
    driver->draw2DRectangle(thumbColor, thumbAbs);
}

void SkillsWindow::renderActionButtons(irr::video::IVideoDriver* driver,
                                       irr::gui::IGUIEnvironment* gui)
{
    irr::core::recti contentArea = getContentArea();
    int contentX = contentArea.UpperLeftCorner.X;
    int contentY = contentArea.UpperLeftCorner.Y;

    // Check if a skill is selected and activatable
    bool canActivate = false;
    if (selectedSkillId_ >= 0 && skillMgr_) {
        const EQ::SkillData* skill = skillMgr_->getSkill(selectedSkillId_);
        canActivate = skill && skill->is_activatable;
    }

    // Activate button (highlighted=false, we don't track pressed state)
    irr::core::recti activateAbs(
        contentX + activateButtonBounds_.UpperLeftCorner.X,
        contentY + activateButtonBounds_.UpperLeftCorner.Y,
        contentX + activateButtonBounds_.LowerRightCorner.X,
        contentY + activateButtonBounds_.LowerRightCorner.Y
    );
    drawButton(driver, gui, activateAbs, L"Activate",
               false, !canActivate);

    // Hotbar button (highlighted=false, we don't track pressed state)
    bool canHotbar = selectedSkillId_ >= 0;
    irr::core::recti hotbarAbs(
        contentX + hotbarButtonBounds_.UpperLeftCorner.X,
        contentY + hotbarButtonBounds_.UpperLeftCorner.Y,
        contentX + hotbarButtonBounds_.LowerRightCorner.X,
        contentY + hotbarButtonBounds_.LowerRightCorner.Y
    );
    drawButton(driver, gui, hotbarAbs, L"Add to Hotbar",
               false, !canHotbar);
}

// ============================================================================
// Input Handling
// ============================================================================

bool SkillsWindow::handleMouseDown(int x, int y, bool leftButton, bool shift, bool ctrl)
{
    if (!visible_ || !containsPoint(x, y)) {
        return false;
    }

    // Let base class handle title bar dragging
    if (WindowBase::handleMouseDown(x, y, leftButton, shift, ctrl)) {
        return true;
    }

    if (!leftButton) return true;

    irr::core::recti contentArea = getContentArea();
    int localX = x - contentArea.UpperLeftCorner.X;
    int localY = y - contentArea.UpperLeftCorner.Y;

    // Check scrollbar up button
    if (scrollUpButtonBounds_.isPointInside(irr::core::vector2di(localX, localY))) {
        scrollUp(1);
        return true;
    }

    // Check scrollbar down button
    if (scrollDownButtonBounds_.isPointInside(irr::core::vector2di(localX, localY))) {
        scrollDown(1);
        return true;
    }

    // Check scrollbar thumb drag
    if (isInScrollThumb(localX, localY)) {
        scrollbarDragging_ = true;
        scrollbarDragStartY_ = y;
        scrollbarDragStartOffset_ = scrollOffset_;
        return true;
    }

    // Check scrollbar track click (page up/down)
    if (scrollTrackBounds_.isPointInside(irr::core::vector2di(localX, localY))) {
        int thumbMidY = scrollThumbBounds_.UpperLeftCorner.Y +
                        scrollThumbBounds_.getHeight() / 2;
        if (localY < thumbMidY) {
            scrollUp(getVisibleRowCount());
        } else {
            scrollDown(getVisibleRowCount());
        }
        return true;
    }

    // Check skill list click
    int skillIndex = getSkillAtPosition(localX, localY);
    if (skillIndex >= 0 && skillIndex < static_cast<int>(skills_.size())) {
        selectedSkillId_ = skills_[skillIndex]->skill_id;
        return true;
    }

    // Check button clicks
    irr::core::recti activateAbs(
        activateButtonBounds_.UpperLeftCorner.X,
        activateButtonBounds_.UpperLeftCorner.Y,
        activateButtonBounds_.LowerRightCorner.X,
        activateButtonBounds_.LowerRightCorner.Y
    );
    if (activateAbs.isPointInside(irr::core::vector2di(localX, localY))) {
        if (selectedSkillId_ >= 0 && activateCallback_) {
            const EQ::SkillData* skill = skillMgr_ ? skillMgr_->getSkill(selectedSkillId_) : nullptr;
            if (skill && skill->is_activatable) {
                activateCallback_(selectedSkillId_);
            }
        }
        return true;
    }

    irr::core::recti hotbarAbs(
        hotbarButtonBounds_.UpperLeftCorner.X,
        hotbarButtonBounds_.UpperLeftCorner.Y,
        hotbarButtonBounds_.LowerRightCorner.X,
        hotbarButtonBounds_.LowerRightCorner.Y
    );
    if (hotbarAbs.isPointInside(irr::core::vector2di(localX, localY))) {
        if (selectedSkillId_ >= 0 && hotbarCallback_) {
            hotbarCallback_(selectedSkillId_);
        }
        return true;
    }

    return true;
}

bool SkillsWindow::handleMouseUp(int x, int y, bool leftButton)
{
    if (scrollbarDragging_) {
        scrollbarDragging_ = false;
        return true;
    }

    return WindowBase::handleMouseUp(x, y, leftButton);
}

bool SkillsWindow::handleMouseMove(int x, int y)
{
    if (!visible_) {
        return false;
    }

    // Handle scrollbar dragging
    if (scrollbarDragging_) {
        int deltaY = y - scrollbarDragStartY_;
        int trackHeight = scrollTrackBounds_.getHeight();
        int thumbHeight = scrollThumbBounds_.getHeight();
        int thumbTravel = trackHeight - thumbHeight;

        if (thumbTravel > 0) {
            int maxScroll = getMaxScrollOffset();
            int newOffset = scrollbarDragStartOffset_ +
                           (deltaY * maxScroll / thumbTravel);
            scrollOffset_ = std::max(0, std::min(newOffset, maxScroll));
        }
        return true;
    }

    // Let base class handle dragging
    if (WindowBase::handleMouseMove(x, y)) {
        return true;
    }

    if (!containsPoint(x, y)) {
        hoveredSkillId_ = -1;
        activateButtonHovered_ = false;
        hotbarButtonHovered_ = false;
        tooltip_.clear();
        return false;
    }

    irr::core::recti contentArea = getContentArea();
    int localX = x - contentArea.UpperLeftCorner.X;
    int localY = y - contentArea.UpperLeftCorner.Y;

    // Check skill list hover
    int skillIndex = getSkillAtPosition(localX, localY);
    if (skillIndex >= 0 && skillIndex < static_cast<int>(skills_.size())) {
        hoveredSkillId_ = skills_[skillIndex]->skill_id;
        // Set tooltip for hovered skill
        tooltip_.setSkill(skills_[skillIndex], x, y);
    } else {
        hoveredSkillId_ = -1;
        tooltip_.clear();
    }

    // Check button hovers
    irr::core::recti activateAbs(
        activateButtonBounds_.UpperLeftCorner.X,
        activateButtonBounds_.UpperLeftCorner.Y,
        activateButtonBounds_.LowerRightCorner.X,
        activateButtonBounds_.LowerRightCorner.Y
    );
    activateButtonHovered_ = activateAbs.isPointInside(irr::core::vector2di(localX, localY));

    irr::core::recti hotbarAbs(
        hotbarButtonBounds_.UpperLeftCorner.X,
        hotbarButtonBounds_.UpperLeftCorner.Y,
        hotbarButtonBounds_.LowerRightCorner.X,
        hotbarButtonBounds_.LowerRightCorner.Y
    );
    hotbarButtonHovered_ = hotbarAbs.isPointInside(irr::core::vector2di(localX, localY));

    return true;
}

bool SkillsWindow::handleMouseWheel(float delta)
{
    if (!visible_) {
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

int SkillsWindow::getSkillAtPosition(int localX, int localY) const
{
    if (!listBounds_.isPointInside(irr::core::vector2di(localX, localY))) {
        return -1;
    }

    int rowY = localY - listBounds_.UpperLeftCorner.Y;
    int rowIndex = rowY / ROW_HEIGHT;
    int skillIndex = scrollOffset_ + rowIndex;

    if (skillIndex < 0 || skillIndex >= static_cast<int>(skills_.size())) {
        return -1;
    }

    return skillIndex;
}

bool SkillsWindow::isInScrollbar(int localX, int localY) const
{
    return scrollTrackBounds_.isPointInside(irr::core::vector2di(localX, localY));
}

bool SkillsWindow::isInScrollThumb(int localX, int localY) const
{
    return scrollThumbBounds_.isPointInside(irr::core::vector2di(localX, localY));
}

// ============================================================================
// Scrolling
// ============================================================================

void SkillsWindow::scrollUp(int rows)
{
    scrollOffset_ = std::max(0, scrollOffset_ - rows);
}

void SkillsWindow::scrollDown(int rows)
{
    int maxScroll = getMaxScrollOffset();
    scrollOffset_ = std::min(maxScroll, scrollOffset_ + rows);
}

int SkillsWindow::getMaxScrollOffset() const
{
    int totalRows = static_cast<int>(skills_.size());
    int visibleRows = getVisibleRowCount();
    return std::max(0, totalRows - visibleRows);
}

void SkillsWindow::clampScrollOffset()
{
    int maxScroll = getMaxScrollOffset();
    scrollOffset_ = std::max(0, std::min(scrollOffset_, maxScroll));
}

int SkillsWindow::getVisibleRowCount() const
{
    return listBounds_.getHeight() / ROW_HEIGHT;
}

} // namespace ui
} // namespace eqt
