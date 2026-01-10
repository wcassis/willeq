/*
 * WillEQ Skill Trainer Window Implementation
 */

#include "client/graphics/ui/skill_trainer_window.h"
#include <algorithm>
#include <sstream>

namespace eqt {
namespace ui {

// ============================================================================
// Color Definitions
// ============================================================================

irr::video::SColor SkillTrainerWindow::getRowBackground() const {
    return irr::video::SColor(255, 40, 40, 50);
}

irr::video::SColor SkillTrainerWindow::getRowAlternate() const {
    return irr::video::SColor(255, 50, 50, 60);
}

irr::video::SColor SkillTrainerWindow::getRowSelected() const {
    return irr::video::SColor(255, 80, 80, 120);
}

irr::video::SColor SkillTrainerWindow::getRowHovered() const {
    return irr::video::SColor(255, 60, 60, 90);
}

irr::video::SColor SkillTrainerWindow::getHeaderBackground() const {
    return irr::video::SColor(255, 60, 60, 80);
}

irr::video::SColor SkillTrainerWindow::getHeaderTextColor() const {
    return irr::video::SColor(255, 220, 220, 220);
}

irr::video::SColor SkillTrainerWindow::getTextColor() const {
    return irr::video::SColor(255, 200, 200, 200);
}

irr::video::SColor SkillTrainerWindow::getValueTextColor() const {
    return irr::video::SColor(255, 180, 180, 220);
}

irr::video::SColor SkillTrainerWindow::getCostTextColor() const {
    return irr::video::SColor(255, 200, 180, 100);
}

irr::video::SColor SkillTrainerWindow::getCostUnaffordableColor() const {
    return irr::video::SColor(255, 180, 80, 80);
}

irr::video::SColor SkillTrainerWindow::getMoneyTextColor() const {
    return irr::video::SColor(255, 220, 220, 220);
}

irr::video::SColor SkillTrainerWindow::getScrollbarBackground() const {
    return irr::video::SColor(255, 30, 30, 40);
}

irr::video::SColor SkillTrainerWindow::getScrollbarThumb() const {
    return irr::video::SColor(255, 100, 100, 120);
}

irr::video::SColor SkillTrainerWindow::getPlatinumColor() const {
    return irr::video::SColor(255, 180, 180, 220);
}

irr::video::SColor SkillTrainerWindow::getGoldColor() const {
    return irr::video::SColor(255, 220, 200, 100);
}

irr::video::SColor SkillTrainerWindow::getSilverColor() const {
    return irr::video::SColor(255, 180, 180, 180);
}

irr::video::SColor SkillTrainerWindow::getCopperColor() const {
    return irr::video::SColor(255, 200, 140, 100);
}

// ============================================================================
// Constructor
// ============================================================================

SkillTrainerWindow::SkillTrainerWindow()
    : WindowBase(L"Skill Training", WINDOW_WIDTH, WINDOW_HEIGHT)
{
    initializeLayout();
}

void SkillTrainerWindow::initializeLayout()
{
    int borderWidth = getBorderWidth();
    int contentPadding = getContentPadding();
    int titleBarHeight = getTitleBarHeight();

    // Calculate content area dimensions
    int contentWidth = WINDOW_WIDTH - 2 * (borderWidth + contentPadding);
    int contentHeight = WINDOW_HEIGHT - titleBarHeight - 2 * (borderWidth + contentPadding);

    // Header row for column titles
    headerBounds_ = irr::core::recti(
        0, 0,
        contentWidth - SCROLLBAR_WIDTH, HEADER_HEIGHT
    );

    // Skill list area (between header and money/buttons)
    int listHeight = contentHeight - HEADER_HEIGHT - MONEY_AREA_HEIGHT - BUTTON_AREA_HEIGHT;
    listBounds_ = irr::core::recti(
        0, HEADER_HEIGHT,
        contentWidth - SCROLLBAR_WIDTH, HEADER_HEIGHT + listHeight
    );

    // Scrollbar (full list area height)
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

    // Money display area
    int moneyY = HEADER_HEIGHT + listHeight;
    moneyBounds_ = irr::core::recti(
        0, moneyY,
        contentWidth, moneyY + MONEY_AREA_HEIGHT
    );

    // Buttons at bottom
    int buttonY = moneyY + MONEY_AREA_HEIGHT + (BUTTON_AREA_HEIGHT - BUTTON_HEIGHT) / 2;
    int totalButtonWidth = BUTTON_WIDTH * 2 + BUTTON_SPACING;
    int buttonStartX = (contentWidth - totalButtonWidth) / 2;

    trainButtonBounds_ = irr::core::recti(
        buttonStartX, buttonY,
        buttonStartX + BUTTON_WIDTH, buttonY + BUTTON_HEIGHT
    );

    doneButtonBounds_ = irr::core::recti(
        buttonStartX + BUTTON_WIDTH + BUTTON_SPACING, buttonY,
        buttonStartX + BUTTON_WIDTH * 2 + BUTTON_SPACING, buttonY + BUTTON_HEIGHT
    );
}

void SkillTrainerWindow::positionDefault(int screenWidth, int screenHeight)
{
    // Center the window on screen
    int x = (screenWidth - WINDOW_WIDTH) / 2;
    int y = (screenHeight - WINDOW_HEIGHT) / 2;
    setPosition(x, y);
}

// ============================================================================
// Window Lifecycle
// ============================================================================

void SkillTrainerWindow::open(uint32_t trainerId, const std::wstring& trainerName,
                              const std::vector<TrainerSkillEntry>& skills)
{
    trainerId_ = trainerId;
    trainerName_ = trainerName;
    skills_ = skills;

    // Update window title with trainer name
    setTitle(L"Training: " + trainerName);

    // Reset selection and scroll
    selectedIndex_ = -1;
    hoveredIndex_ = -1;
    scrollOffset_ = 0;

    show();
}

void SkillTrainerWindow::close()
{
    if (closeCallback_) {
        closeCallback_();
    }
    hide();

    // Clear state
    trainerId_ = 0;
    trainerName_.clear();
    skills_.clear();
    selectedIndex_ = -1;
    hoveredIndex_ = -1;
    scrollOffset_ = 0;
}

void SkillTrainerWindow::updateSkill(uint8_t skill_id, uint32_t new_value)
{
    for (auto& skill : skills_) {
        if (skill.skill_id == skill_id) {
            skill.current_value = new_value;
            break;
        }
    }
}

void SkillTrainerWindow::setPlayerMoney(uint32_t platinum, uint32_t gold,
                                        uint32_t silver, uint32_t copper)
{
    platinum_ = platinum;
    gold_ = gold;
    silver_ = silver;
    copper_ = copper;
    playerMoneyCopper_ = static_cast<uint64_t>(platinum) * 1000 +
                         static_cast<uint64_t>(gold) * 100 +
                         static_cast<uint64_t>(silver) * 10 +
                         static_cast<uint64_t>(copper);
}

// ============================================================================
// Rendering
// ============================================================================

void SkillTrainerWindow::render(irr::video::IVideoDriver* driver,
                                irr::gui::IGUIEnvironment* gui)
{
    if (!visible_) return;

    // Draw base window (title bar, background, borders)
    WindowBase::render(driver, gui);
}

void SkillTrainerWindow::renderContent(irr::video::IVideoDriver* driver,
                                       irr::gui::IGUIEnvironment* gui)
{
    renderTableHeader(driver, gui);
    renderSkillList(driver, gui);
    renderScrollbar(driver);
    renderPlayerMoney(driver, gui);
    renderButtons(driver, gui);
}

void SkillTrainerWindow::renderTableHeader(irr::video::IVideoDriver* driver,
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

    int textY = contentY + headerBounds_.UpperLeftCorner.Y + 5;
    int xPos = contentX + COLUMN_PADDING;

    // Skill Name column
    irr::core::recti nameCol(xPos, textY, xPos + NAME_COLUMN_WIDTH, textY + HEADER_HEIGHT - 5);
    font->draw(L"Skill", nameCol, getHeaderTextColor(), false, false);
    xPos += NAME_COLUMN_WIDTH;

    // Current column
    irr::core::recti currentCol(xPos, textY, xPos + CURRENT_COLUMN_WIDTH, textY + HEADER_HEIGHT - 5);
    font->draw(L"Cur", currentCol, getHeaderTextColor(), false, false);
    xPos += CURRENT_COLUMN_WIDTH;

    // Max column
    irr::core::recti maxCol(xPos, textY, xPos + MAX_COLUMN_WIDTH, textY + HEADER_HEIGHT - 5);
    font->draw(L"Max", maxCol, getHeaderTextColor(), false, false);
    xPos += MAX_COLUMN_WIDTH;

    // Cost column
    irr::core::recti costCol(xPos, textY, xPos + COST_COLUMN_WIDTH, textY + HEADER_HEIGHT - 5);
    font->draw(L"Cost", costCol, getHeaderTextColor(), false, false);
}

void SkillTrainerWindow::renderSkillList(irr::video::IVideoDriver* driver,
                                         irr::gui::IGUIEnvironment* gui)
{
    int visibleRows = getVisibleRowCount();
    int endIndex = std::min(scrollOffset_ + visibleRows, static_cast<int>(skills_.size()));

    for (int i = scrollOffset_; i < endIndex; ++i) {
        int rowIndex = i - scrollOffset_;
        int yPos = listBounds_.UpperLeftCorner.Y + rowIndex * ROW_HEIGHT;
        renderSkillRow(driver, gui, skills_[i], i, yPos);
    }
}

void SkillTrainerWindow::renderSkillRow(irr::video::IVideoDriver* driver,
                                        irr::gui::IGUIEnvironment* gui,
                                        const TrainerSkillEntry& skill,
                                        int rowIndex,
                                        int yPos)
{
    irr::core::recti contentArea = getContentArea();
    int contentX = contentArea.UpperLeftCorner.X;
    int contentY = contentArea.UpperLeftCorner.Y;

    // Row background based on state
    irr::video::SColor bgColor;
    if (rowIndex == selectedIndex_) {
        bgColor = getRowSelected();
    } else if (rowIndex == hoveredIndex_) {
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

    int textY = contentY + yPos + 4;
    int xPos = contentX + COLUMN_PADDING;

    // Check if skill can be trained (current < max)
    bool canTrain = skill.current_value < skill.max_trainable;
    bool canAfford = canAffordTraining(skill.skill_id);

    // Skill name
    irr::core::recti nameCol(xPos, textY, xPos + NAME_COLUMN_WIDTH - COLUMN_PADDING, textY + ROW_HEIGHT - 4);
    font->draw(skill.name.c_str(), nameCol, getTextColor(), false, false);
    xPos += NAME_COLUMN_WIDTH;

    // Current value
    std::wstring currentStr = std::to_wstring(skill.current_value);
    irr::core::recti currentCol(xPos, textY, xPos + CURRENT_COLUMN_WIDTH - COLUMN_PADDING, textY + ROW_HEIGHT - 4);
    font->draw(currentStr.c_str(), currentCol, getValueTextColor(), false, false);
    xPos += CURRENT_COLUMN_WIDTH;

    // Max trainable value
    std::wstring maxStr = std::to_wstring(skill.max_trainable);
    irr::core::recti maxCol(xPos, textY, xPos + MAX_COLUMN_WIDTH - COLUMN_PADDING, textY + ROW_HEIGHT - 4);
    font->draw(maxStr.c_str(), maxCol, getValueTextColor(), false, false);
    xPos += MAX_COLUMN_WIDTH;

    // Cost (or "Maxed" if at cap)
    std::wstring costStr;
    irr::video::SColor costColor;
    if (!canTrain) {
        costStr = L"Maxed";
        costColor = irr::video::SColor(255, 100, 180, 100);  // Green for maxed
    } else if (skill.cost == 0) {
        costStr = L"Free";
        costColor = getCostTextColor();
    } else {
        costStr = formatPrice(skill.cost);
        costColor = canAfford ? getCostTextColor() : getCostUnaffordableColor();
    }
    irr::core::recti costCol(xPos, textY, xPos + COST_COLUMN_WIDTH - COLUMN_PADDING, textY + ROW_HEIGHT - 4);
    font->draw(costStr.c_str(), costCol, costColor, false, false);
}

void SkillTrainerWindow::renderPlayerMoney(irr::video::IVideoDriver* driver,
                                           irr::gui::IGUIEnvironment* gui)
{
    irr::core::recti contentArea = getContentArea();
    int contentX = contentArea.UpperLeftCorner.X;
    int contentY = contentArea.UpperLeftCorner.Y;

    // Money area background (slightly darker)
    irr::core::recti moneyAbs(
        contentX + moneyBounds_.UpperLeftCorner.X,
        contentY + moneyBounds_.UpperLeftCorner.Y,
        contentX + moneyBounds_.LowerRightCorner.X,
        contentY + moneyBounds_.LowerRightCorner.Y
    );
    driver->draw2DRectangle(irr::video::SColor(255, 35, 35, 45), moneyAbs);

    // Draw money text
    irr::gui::IGUIFont* font = gui ? gui->getBuiltInFont() : nullptr;
    if (!font) return;

    int textY = contentY + moneyBounds_.UpperLeftCorner.Y + 8;
    int xPos = contentX + COLUMN_PADDING + 4;

    // "Your Money:" label
    irr::core::recti labelRect(xPos, textY, xPos + 80, textY + 14);
    font->draw(L"Your Money:", labelRect, getMoneyTextColor(), false, false);
    xPos += 85;

    // Platinum
    if (platinum_ > 0) {
        std::wstring ppStr = std::to_wstring(platinum_) + L"p";
        irr::core::recti ppRect(xPos, textY, xPos + 50, textY + 14);
        font->draw(ppStr.c_str(), ppRect, getPlatinumColor(), false, false);
        xPos += 45;
    }

    // Gold
    if (gold_ > 0 || platinum_ > 0) {
        std::wstring gpStr = std::to_wstring(gold_) + L"g";
        irr::core::recti gpRect(xPos, textY, xPos + 40, textY + 14);
        font->draw(gpStr.c_str(), gpRect, getGoldColor(), false, false);
        xPos += 35;
    }

    // Silver
    if (silver_ > 0 || gold_ > 0 || platinum_ > 0) {
        std::wstring spStr = std::to_wstring(silver_) + L"s";
        irr::core::recti spRect(xPos, textY, xPos + 40, textY + 14);
        font->draw(spStr.c_str(), spRect, getSilverColor(), false, false);
        xPos += 35;
    }

    // Copper
    std::wstring cpStr = std::to_wstring(copper_) + L"c";
    irr::core::recti cpRect(xPos, textY, xPos + 40, textY + 14);
    font->draw(cpStr.c_str(), cpRect, getCopperColor(), false, false);
    xPos += 45;

    // Practice points (free training sessions)
    std::wstring ppLabel = L"Sessions: ";
    irr::core::recti sessLabelRect(xPos, textY, xPos + 60, textY + 14);
    font->draw(ppLabel.c_str(), sessLabelRect, getMoneyTextColor(), false, false);
    xPos += 60;

    std::wstring sessStr = std::to_wstring(practicePoints_);
    irr::video::SColor sessColor = practicePoints_ > 0 ?
        irr::video::SColor(255, 100, 200, 100) :  // Green if has sessions
        irr::video::SColor(255, 180, 180, 180);   // Gray if none
    irr::core::recti sessRect(xPos, textY, xPos + 40, textY + 14);
    font->draw(sessStr.c_str(), sessRect, sessColor, false, false);
}

void SkillTrainerWindow::renderButtons(irr::video::IVideoDriver* driver,
                                       irr::gui::IGUIEnvironment* gui)
{
    irr::core::recti contentArea = getContentArea();
    int contentX = contentArea.UpperLeftCorner.X;
    int contentY = contentArea.UpperLeftCorner.Y;

    // Check if training is possible for selected skill
    bool canTrain = false;
    if (selectedIndex_ >= 0 && selectedIndex_ < static_cast<int>(skills_.size())) {
        const auto& skill = skills_[selectedIndex_];
        canTrain = skill.current_value < skill.max_trainable &&
                   canAffordTraining(skill.skill_id);
    }

    // Train button
    irr::core::recti trainAbs(
        contentX + trainButtonBounds_.UpperLeftCorner.X,
        contentY + trainButtonBounds_.UpperLeftCorner.Y,
        contentX + trainButtonBounds_.LowerRightCorner.X,
        contentY + trainButtonBounds_.LowerRightCorner.Y
    );
    drawButton(driver, gui, trainAbs, L"Train", trainButtonHovered_, !canTrain);

    // Done button (always enabled)
    irr::core::recti doneAbs(
        contentX + doneButtonBounds_.UpperLeftCorner.X,
        contentY + doneButtonBounds_.UpperLeftCorner.Y,
        contentX + doneButtonBounds_.LowerRightCorner.X,
        contentY + doneButtonBounds_.LowerRightCorner.Y
    );
    drawButton(driver, gui, doneAbs, L"Done", doneButtonHovered_, false);
}

void SkillTrainerWindow::renderScrollbar(irr::video::IVideoDriver* driver)
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

    // Calculate thumb size and position
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

// ============================================================================
// Input Handling
// ============================================================================

bool SkillTrainerWindow::handleMouseDown(int x, int y, bool leftButton, bool shift, bool ctrl)
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
        selectedIndex_ = skillIndex;
        return true;
    }

    // Check Train button click
    if (isTrainButtonHit(localX, localY)) {
        if (selectedIndex_ >= 0 && selectedIndex_ < static_cast<int>(skills_.size())) {
            const auto& skill = skills_[selectedIndex_];
            if (skill.current_value < skill.max_trainable &&
                canAffordTraining(skill.skill_id) &&
                trainCallback_) {
                trainCallback_(skill.skill_id);
            }
        }
        return true;
    }

    // Check Done button click
    if (isDoneButtonHit(localX, localY)) {
        close();
        return true;
    }

    return true;
}

bool SkillTrainerWindow::handleMouseUp(int x, int y, bool leftButton)
{
    if (scrollbarDragging_) {
        scrollbarDragging_ = false;
        return true;
    }

    return WindowBase::handleMouseUp(x, y, leftButton);
}

bool SkillTrainerWindow::handleMouseMove(int x, int y)
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
        hoveredIndex_ = -1;
        trainButtonHovered_ = false;
        doneButtonHovered_ = false;
        return false;
    }

    irr::core::recti contentArea = getContentArea();
    int localX = x - contentArea.UpperLeftCorner.X;
    int localY = y - contentArea.UpperLeftCorner.Y;

    // Check skill list hover
    int skillIndex = getSkillAtPosition(localX, localY);
    hoveredIndex_ = skillIndex;

    // Check button hovers
    trainButtonHovered_ = isTrainButtonHit(localX, localY);
    doneButtonHovered_ = isDoneButtonHit(localX, localY);

    return true;
}

bool SkillTrainerWindow::handleMouseWheel(float delta)
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

int SkillTrainerWindow::getSkillAtPosition(int localX, int localY) const
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

bool SkillTrainerWindow::isTrainButtonHit(int localX, int localY) const
{
    return trainButtonBounds_.isPointInside(irr::core::vector2di(localX, localY));
}

bool SkillTrainerWindow::isDoneButtonHit(int localX, int localY) const
{
    return doneButtonBounds_.isPointInside(irr::core::vector2di(localX, localY));
}

bool SkillTrainerWindow::isInScrollThumb(int localX, int localY) const
{
    return scrollThumbBounds_.isPointInside(irr::core::vector2di(localX, localY));
}

// ============================================================================
// Scrolling
// ============================================================================

void SkillTrainerWindow::scrollUp(int rows)
{
    scrollOffset_ = std::max(0, scrollOffset_ - rows);
}

void SkillTrainerWindow::scrollDown(int rows)
{
    int maxScroll = getMaxScrollOffset();
    scrollOffset_ = std::min(maxScroll, scrollOffset_ + rows);
}

int SkillTrainerWindow::getMaxScrollOffset() const
{
    int totalRows = static_cast<int>(skills_.size());
    int visibleRows = getVisibleRowCount();
    return std::max(0, totalRows - visibleRows);
}

int SkillTrainerWindow::getVisibleRowCount() const
{
    return listBounds_.getHeight() / ROW_HEIGHT;
}

// ============================================================================
// Price/Money Helpers
// ============================================================================

std::wstring SkillTrainerWindow::formatPrice(uint32_t copperAmount) const
{
    if (copperAmount == 0) {
        return L"Free";
    }

    std::wstringstream ss;
    bool hasValue = false;

    uint32_t platinum = copperAmount / 1000;
    copperAmount %= 1000;
    uint32_t gold = copperAmount / 100;
    copperAmount %= 100;
    uint32_t silver = copperAmount / 10;
    uint32_t copper = copperAmount % 10;

    if (platinum > 0) {
        ss << platinum << L"p";
        hasValue = true;
    }
    if (gold > 0) {
        if (hasValue) ss << L" ";
        ss << gold << L"g";
        hasValue = true;
    }
    if (silver > 0) {
        if (hasValue) ss << L" ";
        ss << silver << L"s";
        hasValue = true;
    }
    if (copper > 0 || !hasValue) {
        if (hasValue) ss << L" ";
        ss << copper << L"c";
    }

    return ss.str();
}

std::wstring SkillTrainerWindow::formatPlayerMoney() const
{
    std::wstringstream ss;
    ss << platinum_ << L"p " << gold_ << L"g " << silver_ << L"s " << copper_ << L"c";
    return ss.str();
}

bool SkillTrainerWindow::canAffordTraining(uint8_t skill_id) const
{
    // Practice points allow free training
    if (practicePoints_ > 0) {
        return true;
    }
    uint32_t cost = getTrainingCost(skill_id);
    return playerMoneyCopper_ >= cost;
}

uint32_t SkillTrainerWindow::getTrainingCost(uint8_t skill_id) const
{
    for (const auto& skill : skills_) {
        if (skill.skill_id == skill_id) {
            return skill.cost;
        }
    }
    return 0;
}

} // namespace ui
} // namespace eqt
