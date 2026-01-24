/*
 * WillEQ Options Window Implementation
 */

#include "client/graphics/ui/options_window.h"
#include <json/json.h>
#include <fstream>
#include <algorithm>
#include <filesystem>

namespace eqt {
namespace ui {

// ============================================================================
// Color Definitions
// ============================================================================

irr::video::SColor OptionsWindow::getTabBackground() const {
    return irr::video::SColor(255, 50, 50, 55);
}

irr::video::SColor OptionsWindow::getTabActiveBackground() const {
    return irr::video::SColor(255, 70, 70, 80);
}

irr::video::SColor OptionsWindow::getTabHoverBackground() const {
    return irr::video::SColor(255, 60, 60, 70);
}

irr::video::SColor OptionsWindow::getTabTextColor() const {
    return irr::video::SColor(255, 200, 200, 200);
}

irr::video::SColor OptionsWindow::getSectionHeaderColor() const {
    return irr::video::SColor(255, 55, 55, 65);
}

irr::video::SColor OptionsWindow::getSectionHeaderTextColor() const {
    return irr::video::SColor(255, 220, 200, 160);
}

irr::video::SColor OptionsWindow::getCheckboxBackground() const {
    return irr::video::SColor(255, 40, 40, 45);
}

irr::video::SColor OptionsWindow::getCheckboxBorder() const {
    return irr::video::SColor(255, 100, 100, 110);
}

irr::video::SColor OptionsWindow::getCheckboxCheck() const {
    return irr::video::SColor(255, 100, 200, 100);
}

irr::video::SColor OptionsWindow::getSliderTrackColor() const {
    return irr::video::SColor(255, 40, 40, 50);
}

irr::video::SColor OptionsWindow::getSliderFillColor() const {
    return irr::video::SColor(255, 80, 120, 180);
}

irr::video::SColor OptionsWindow::getSliderThumbColor() const {
    return irr::video::SColor(255, 150, 150, 160);
}

irr::video::SColor OptionsWindow::getQualityButtonBackground() const {
    return irr::video::SColor(255, 50, 50, 60);
}

irr::video::SColor OptionsWindow::getQualityButtonActiveBackground() const {
    return irr::video::SColor(255, 70, 100, 140);
}

irr::video::SColor OptionsWindow::getQualityButtonHoverBackground() const {
    return irr::video::SColor(255, 60, 70, 90);
}

irr::video::SColor OptionsWindow::getLabelTextColor() const {
    return irr::video::SColor(255, 180, 180, 180);
}

irr::video::SColor OptionsWindow::getValueTextColor() const {
    return irr::video::SColor(255, 200, 200, 220);
}

// ============================================================================
// Constructor
// ============================================================================

OptionsWindow::OptionsWindow()
    : WindowBase(L"Options", WINDOW_WIDTH, WINDOW_HEIGHT)
{
    initializeLayout();
    loadSettings();
}

void OptionsWindow::initializeLayout()
{
    updateLayout();
}

void OptionsWindow::updateLayout()
{
    int contentPadding = getContentPadding();

    // Tab bar at top of content area
    tabBarBounds_ = irr::core::recti(
        0, 0,
        WINDOW_WIDTH - 2 * (getBorderWidth() + contentPadding),
        TAB_HEIGHT
    );

    // Individual tab bounds
    tabBounds_.clear();
    const wchar_t* tabNames[] = { L"Display", L"Audio", L"Controls", L"Game" };
    int tabCount = 4;
    int tabWidth = tabBarBounds_.getWidth() / tabCount;

    for (int i = 0; i < tabCount; i++) {
        tabBounds_.push_back(irr::core::recti(
            i * tabWidth, 0,
            (i + 1) * tabWidth, TAB_HEIGHT
        ));
    }

    // Content area below tabs
    contentAreaBounds_ = irr::core::recti(
        0, TAB_HEIGHT + 4,
        tabBarBounds_.getWidth(),
        WINDOW_HEIGHT - getTitleBarHeight() - 2 * (getBorderWidth() + contentPadding) - TAB_HEIGHT - 4
    );
}

void OptionsWindow::positionDefault(int screenWidth, int screenHeight)
{
    int x = (screenWidth - WINDOW_WIDTH) / 2;
    int y = (screenHeight - WINDOW_HEIGHT) / 2;
    setPosition(x, y);
}

// ============================================================================
// Tab Selection
// ============================================================================

void OptionsWindow::selectTab(Tab tab)
{
    currentTab_ = tab;
    scrollOffset_ = 0;
}

// ============================================================================
// Rendering
// ============================================================================

void OptionsWindow::render(irr::video::IVideoDriver* driver,
                          irr::gui::IGUIEnvironment* gui)
{
    if (!isVisible()) return;
    WindowBase::render(driver, gui);
}

void OptionsWindow::renderContent(irr::video::IVideoDriver* driver,
                                 irr::gui::IGUIEnvironment* gui)
{
    irr::core::recti content = getContentArea();

    // Render tabs
    renderTabs(driver, gui);

    // Render current tab content
    switch (currentTab_) {
        case Tab::Display:
            renderDisplayTab(driver, gui);
            break;
        case Tab::Audio:
            renderAudioTab(driver, gui);
            break;
        case Tab::Controls:
            renderControlsTab(driver, gui);
            break;
        case Tab::Game:
            renderGameTab(driver, gui);
            break;
    }
}

void OptionsWindow::renderTabs(irr::video::IVideoDriver* driver,
                              irr::gui::IGUIEnvironment* gui)
{
    irr::core::recti content = getContentArea();
    const wchar_t* tabNames[] = { L"Display", L"Audio", L"Controls", L"Game" };

    for (size_t i = 0; i < tabBounds_.size(); i++) {
        irr::core::recti tabRect = tabBounds_[i];
        tabRect += irr::core::vector2di(content.UpperLeftCorner.X, content.UpperLeftCorner.Y);

        // Determine background color
        irr::video::SColor bgColor;
        if (static_cast<int>(currentTab_) == static_cast<int>(i)) {
            bgColor = getTabActiveBackground();
        } else if (hoveredTab_ == static_cast<int>(i)) {
            bgColor = getTabHoverBackground();
        } else {
            bgColor = getTabBackground();
        }

        driver->draw2DRectangle(bgColor, tabRect);

        // Draw border
        driver->draw2DRectangleOutline(tabRect, getBorderLightColor());

        // Draw text centered
        if (gui->getBuiltInFont()) {
            irr::core::dimension2du textSize = gui->getBuiltInFont()->getDimension(tabNames[i]);
            int textX = tabRect.UpperLeftCorner.X + (tabRect.getWidth() - textSize.Width) / 2;
            int textY = tabRect.UpperLeftCorner.Y + (tabRect.getHeight() - textSize.Height) / 2;
            gui->getBuiltInFont()->draw(tabNames[i],
                irr::core::recti(textX, textY, textX + textSize.Width, textY + textSize.Height),
                getTabTextColor());
        }
    }
}

void OptionsWindow::renderDisplayTab(irr::video::IVideoDriver* driver,
                                    irr::gui::IGUIEnvironment* gui)
{
    irr::core::recti content = getContentArea();
    int baseX = content.UpperLeftCorner.X + CONTENT_PADDING;
    int baseY = content.UpperLeftCorner.Y + TAB_HEIGHT + CONTENT_PADDING + 4;
    int y = baseY;

    // Render Distance Section
    renderSectionHeader(driver, gui, L"Render Distance", y);
    y += SECTION_HEADER_HEIGHT + ROW_SPACING;

    // Render distance slider (max 2000 = sky dome cutoff)
    renderSlider(driver, gui, L"Distance:", displaySettings_.renderDistance / 2000.0f,
                baseX, y, 200, hoveredSlider_ == 3);
    y += ROW_HEIGHT + ROW_SPACING * 2;

    // Environment Effects Section
    renderSectionHeader(driver, gui, L"Environment Effects", y);
    y += SECTION_HEADER_HEIGHT + ROW_SPACING;

    // Quality selector
    renderQualitySelector(driver, gui, L"Quality:", displaySettings_.environmentQuality,
                         baseX, y, hoveredQualityOption_);
    y += ROW_HEIGHT + ROW_SPACING;

    // Density slider
    renderSlider(driver, gui, L"Density:", displaySettings_.environmentDensity,
                baseX, y, 200, hoveredSlider_ == 0);
    y += ROW_HEIGHT + ROW_SPACING;

    // Individual toggles (indented)
    int indentX = baseX + INDENT;

    renderCheckbox(driver, gui, L"Atmospheric Particles", displaySettings_.atmosphericParticles,
                  indentX, y, hoveredCheckbox_ == 0);
    y += ROW_HEIGHT + ROW_SPACING;

    renderCheckbox(driver, gui, L"Ambient Creatures", displaySettings_.ambientCreatures,
                  indentX, y, hoveredCheckbox_ == 1);
    y += ROW_HEIGHT + ROW_SPACING;

    renderCheckbox(driver, gui, L"Shoreline Waves", displaySettings_.shorelineWaves,
                  indentX, y, hoveredCheckbox_ == 2);
    y += ROW_HEIGHT + ROW_SPACING;

    renderCheckbox(driver, gui, L"Reactive Foliage", displaySettings_.reactiveFoliage,
                  indentX, y, hoveredCheckbox_ == 3);
    y += ROW_HEIGHT + ROW_SPACING;

    renderCheckbox(driver, gui, L"Rolling Objects", displaySettings_.rollingObjects,
                  indentX, y, hoveredCheckbox_ == 4);
    y += ROW_HEIGHT + ROW_SPACING * 2;

    // Detail Objects Section
    renderSectionHeader(driver, gui, L"Detail Objects (Grass, Plants, Debris)", y);
    y += SECTION_HEADER_HEIGHT + ROW_SPACING;

    renderCheckbox(driver, gui, L"Enable Detail Objects", displaySettings_.detailObjectsEnabled,
                  baseX, y, hoveredCheckbox_ == 5);
    y += ROW_HEIGHT + ROW_SPACING;

    // Density slider
    renderSlider(driver, gui, L"Density:", displaySettings_.detailDensity,
                baseX, y, 200, hoveredSlider_ == 1);
    y += ROW_HEIGHT + ROW_SPACING;

    // View distance slider (max 2000 = sky dome cutoff)
    renderSlider(driver, gui, L"View Distance:", displaySettings_.detailViewDistance / 2000.0f,
                baseX, y, 200, hoveredSlider_ == 2);
    y += ROW_HEIGHT + ROW_SPACING;

    // Category toggles (indented)
    renderCheckbox(driver, gui, L"Grass", displaySettings_.detailGrass,
                  indentX, y, hoveredCheckbox_ == 6);
    y += ROW_HEIGHT + ROW_SPACING;

    renderCheckbox(driver, gui, L"Plants", displaySettings_.detailPlants,
                  indentX, y, hoveredCheckbox_ == 7);
    y += ROW_HEIGHT + ROW_SPACING;

    renderCheckbox(driver, gui, L"Rocks", displaySettings_.detailRocks,
                  indentX, y, hoveredCheckbox_ == 8);
    y += ROW_HEIGHT + ROW_SPACING;

    renderCheckbox(driver, gui, L"Debris", displaySettings_.detailDebris,
                  indentX, y, hoveredCheckbox_ == 9);
}

void OptionsWindow::renderAudioTab(irr::video::IVideoDriver* driver,
                                  irr::gui::IGUIEnvironment* gui)
{
    irr::core::recti content = getContentArea();
    int baseX = content.UpperLeftCorner.X + CONTENT_PADDING;
    int baseY = content.UpperLeftCorner.Y + TAB_HEIGHT + CONTENT_PADDING + 4;

    // Placeholder
    if (gui->getBuiltInFont()) {
        gui->getBuiltInFont()->draw(L"Audio settings coming soon...",
            irr::core::recti(baseX, baseY, baseX + 300, baseY + 20),
            getLabelTextColor());
    }
}

void OptionsWindow::renderControlsTab(irr::video::IVideoDriver* driver,
                                     irr::gui::IGUIEnvironment* gui)
{
    irr::core::recti content = getContentArea();
    int baseX = content.UpperLeftCorner.X + CONTENT_PADDING;
    int baseY = content.UpperLeftCorner.Y + TAB_HEIGHT + CONTENT_PADDING + 4;

    // Placeholder
    if (gui->getBuiltInFont()) {
        gui->getBuiltInFont()->draw(L"Control settings coming soon...",
            irr::core::recti(baseX, baseY, baseX + 300, baseY + 20),
            getLabelTextColor());
    }
}

void OptionsWindow::renderGameTab(irr::video::IVideoDriver* driver,
                                 irr::gui::IGUIEnvironment* gui)
{
    irr::core::recti content = getContentArea();
    int baseX = content.UpperLeftCorner.X + CONTENT_PADDING;
    int baseY = content.UpperLeftCorner.Y + TAB_HEIGHT + CONTENT_PADDING + 4;

    // Placeholder
    if (gui->getBuiltInFont()) {
        gui->getBuiltInFont()->draw(L"Game settings coming soon...",
            irr::core::recti(baseX, baseY, baseX + 300, baseY + 20),
            getLabelTextColor());
    }
}

// ============================================================================
// UI Element Rendering
// ============================================================================

void OptionsWindow::renderSectionHeader(irr::video::IVideoDriver* driver,
                                       irr::gui::IGUIEnvironment* gui,
                                       const std::wstring& text,
                                       int y)
{
    irr::core::recti content = getContentArea();
    irr::core::recti headerRect(
        content.UpperLeftCorner.X,
        y,
        content.LowerRightCorner.X,
        y + SECTION_HEADER_HEIGHT
    );

    driver->draw2DRectangle(getSectionHeaderColor(), headerRect);

    if (gui->getBuiltInFont()) {
        irr::core::dimension2du textSize = gui->getBuiltInFont()->getDimension(text.c_str());
        int textY = y + (SECTION_HEADER_HEIGHT - textSize.Height) / 2;
        gui->getBuiltInFont()->draw(text.c_str(),
            irr::core::recti(content.UpperLeftCorner.X + CONTENT_PADDING, textY,
                            content.LowerRightCorner.X, textY + textSize.Height),
            getSectionHeaderTextColor());
    }
}

void OptionsWindow::renderCheckbox(irr::video::IVideoDriver* driver,
                                  irr::gui::IGUIEnvironment* gui,
                                  const std::wstring& label,
                                  bool checked,
                                  int x, int y,
                                  bool hovered)
{
    // Checkbox box
    irr::core::recti boxRect(x, y + (ROW_HEIGHT - CHECKBOX_SIZE) / 2,
                            x + CHECKBOX_SIZE, y + (ROW_HEIGHT + CHECKBOX_SIZE) / 2);

    irr::video::SColor bgColor = hovered ?
        irr::video::SColor(255, 50, 50, 60) : getCheckboxBackground();
    driver->draw2DRectangle(bgColor, boxRect);
    driver->draw2DRectangleOutline(boxRect, getCheckboxBorder());

    // Checkmark if checked
    if (checked) {
        irr::core::recti checkRect(boxRect.UpperLeftCorner.X + 3, boxRect.UpperLeftCorner.Y + 3,
                                  boxRect.LowerRightCorner.X - 3, boxRect.LowerRightCorner.Y - 3);
        driver->draw2DRectangle(getCheckboxCheck(), checkRect);
    }

    // Label
    if (gui->getBuiltInFont()) {
        int textX = x + CHECKBOX_SIZE + 8;
        int textY = y + (ROW_HEIGHT - 12) / 2;
        gui->getBuiltInFont()->draw(label.c_str(),
            irr::core::recti(textX, textY, textX + 200, textY + 14),
            getLabelTextColor());
    }
}

void OptionsWindow::renderSlider(irr::video::IVideoDriver* driver,
                                irr::gui::IGUIEnvironment* gui,
                                const std::wstring& label,
                                float value,
                                int x, int y, int width,
                                bool hovered)
{
    // Label
    if (gui->getBuiltInFont()) {
        gui->getBuiltInFont()->draw(label.c_str(),
            irr::core::recti(x, y + (ROW_HEIGHT - 12) / 2, x + 80, y + ROW_HEIGHT),
            getLabelTextColor());
    }

    // Slider track
    int trackX = x + 90;
    int trackY = y + (ROW_HEIGHT - SLIDER_TRACK_HEIGHT) / 2;
    int trackWidth = width - 90 - 40;

    irr::core::recti trackRect(trackX, trackY, trackX + trackWidth, trackY + SLIDER_TRACK_HEIGHT);
    driver->draw2DRectangle(getSliderTrackColor(), trackRect);

    // Filled portion
    int fillWidth = static_cast<int>(trackWidth * std::clamp(value, 0.0f, 1.0f));
    if (fillWidth > 0) {
        irr::core::recti fillRect(trackX, trackY, trackX + fillWidth, trackY + SLIDER_TRACK_HEIGHT);
        driver->draw2DRectangle(getSliderFillColor(), fillRect);
    }

    // Thumb
    int thumbX = trackX + fillWidth - 4;
    int thumbY = y + (ROW_HEIGHT - SLIDER_HEIGHT) / 2;
    irr::core::recti thumbRect(thumbX, thumbY, thumbX + 8, thumbY + SLIDER_HEIGHT);
    driver->draw2DRectangle(hovered ? irr::video::SColor(255, 180, 180, 190) : getSliderThumbColor(), thumbRect);

    // Value text
    if (gui->getBuiltInFont()) {
        wchar_t valueStr[16];
        swprintf(valueStr, 16, L"%d%%", static_cast<int>(value * 100));
        int textX = trackX + trackWidth + 8;
        gui->getBuiltInFont()->draw(valueStr,
            irr::core::recti(textX, y + (ROW_HEIGHT - 12) / 2, textX + 40, y + ROW_HEIGHT),
            getValueTextColor());
    }
}

void OptionsWindow::renderQualitySelector(irr::video::IVideoDriver* driver,
                                         irr::gui::IGUIEnvironment* gui,
                                         const std::wstring& label,
                                         EffectQuality quality,
                                         int x, int y,
                                         int hoveredOption)
{
    // Label
    if (gui->getBuiltInFont()) {
        gui->getBuiltInFont()->draw(label.c_str(),
            irr::core::recti(x, y + (ROW_HEIGHT - 12) / 2, x + 60, y + ROW_HEIGHT),
            getLabelTextColor());
    }

    // Quality buttons
    const wchar_t* options[] = { L"Off", L"Low", L"Med", L"High" };
    int buttonX = x + 70;

    for (int i = 0; i < 4; i++) {
        irr::core::recti btnRect(
            buttonX, y + (ROW_HEIGHT - QUALITY_BUTTON_HEIGHT) / 2,
            buttonX + QUALITY_BUTTON_WIDTH, y + (ROW_HEIGHT + QUALITY_BUTTON_HEIGHT) / 2
        );

        irr::video::SColor bgColor;
        if (static_cast<int>(quality) == i) {
            bgColor = getQualityButtonActiveBackground();
        } else if (hoveredOption == i) {
            bgColor = getQualityButtonHoverBackground();
        } else {
            bgColor = getQualityButtonBackground();
        }

        driver->draw2DRectangle(bgColor, btnRect);
        driver->draw2DRectangleOutline(btnRect, getBorderLightColor());

        if (gui->getBuiltInFont()) {
            irr::core::dimension2du textSize = gui->getBuiltInFont()->getDimension(options[i]);
            int textX = btnRect.UpperLeftCorner.X + (btnRect.getWidth() - textSize.Width) / 2;
            int textY = btnRect.UpperLeftCorner.Y + (btnRect.getHeight() - textSize.Height) / 2;
            gui->getBuiltInFont()->draw(options[i],
                irr::core::recti(textX, textY, textX + textSize.Width, textY + textSize.Height),
                getTabTextColor());
        }

        buttonX += QUALITY_BUTTON_WIDTH + QUALITY_BUTTON_SPACING;
    }
}

// ============================================================================
// Input Handling
// ============================================================================

bool OptionsWindow::handleMouseDown(int x, int y, bool leftButton, bool shift, bool ctrl)
{
    if (!isVisible() || !containsPoint(x, y)) return false;

    // Let base class handle title bar dragging
    if (WindowBase::handleMouseDown(x, y, leftButton, shift, ctrl)) {
        return true;
    }

    if (!leftButton) return true;

    irr::core::recti content = getContentArea();
    int localX = x - content.UpperLeftCorner.X;
    int localY = y - content.UpperLeftCorner.Y;

    // Check tab clicks
    int tabIndex = getTabAtPosition(localX, localY);
    if (tabIndex >= 0 && tabIndex < 4) {
        selectTab(static_cast<Tab>(tabIndex));
        return true;
    }

    // Handle Display tab interactions
    if (currentTab_ == Tab::Display) {
        int baseX = CONTENT_PADDING;
        int baseY = TAB_HEIGHT + CONTENT_PADDING + 4;
        int rowY = baseY;
        int sliderTrackX = baseX + 90;
        int sliderTrackWidth = 200 - 90 - 40;

        // Render Distance section header
        rowY += SECTION_HEADER_HEIGHT + ROW_SPACING;

        // Render distance slider (max 2000)
        if (localY >= rowY && localY < rowY + ROW_HEIGHT &&
            localX >= sliderTrackX && localX < sliderTrackX + sliderTrackWidth) {
            float newValue = static_cast<float>(localX - sliderTrackX) / sliderTrackWidth;
            displaySettings_.renderDistance = std::clamp(newValue * 2000.0f, 50.0f, 2000.0f);
            draggingSlider_ = true;
            draggingSliderIndex_ = 3;
            notifyDisplaySettingsChanged();
            return true;
        }
        rowY += ROW_HEIGHT + ROW_SPACING * 2;

        // Environment Effects section header
        rowY += SECTION_HEADER_HEIGHT + ROW_SPACING;

        // Quality selector
        int qualityOpt = getQualityOptionAtPosition(baseX + 70, rowY, localX, localY);
        if (qualityOpt >= 0) {
            displaySettings_.environmentQuality = static_cast<EffectQuality>(qualityOpt);
            notifyDisplaySettingsChanged();
            return true;
        }
        rowY += ROW_HEIGHT + ROW_SPACING;

        // Density slider - check if clicked
        if (localY >= rowY && localY < rowY + ROW_HEIGHT &&
            localX >= sliderTrackX && localX < sliderTrackX + sliderTrackWidth) {
            float newValue = static_cast<float>(localX - sliderTrackX) / sliderTrackWidth;
            displaySettings_.environmentDensity = std::clamp(newValue, 0.0f, 1.0f);
            draggingSlider_ = true;
            draggingSliderIndex_ = 0;
            notifyDisplaySettingsChanged();
            return true;
        }
        rowY += ROW_HEIGHT + ROW_SPACING;

        // Checkboxes
        int indentX = baseX + INDENT;
        int checkboxIndex = 0;

        // Atmospheric Particles
        if (isInCheckbox(indentX, rowY, localX, localY)) {
            displaySettings_.atmosphericParticles = !displaySettings_.atmosphericParticles;
            notifyDisplaySettingsChanged();
            return true;
        }
        rowY += ROW_HEIGHT + ROW_SPACING;

        // Ambient Creatures
        if (isInCheckbox(indentX, rowY, localX, localY)) {
            displaySettings_.ambientCreatures = !displaySettings_.ambientCreatures;
            notifyDisplaySettingsChanged();
            return true;
        }
        rowY += ROW_HEIGHT + ROW_SPACING;

        // Shoreline Waves
        if (isInCheckbox(indentX, rowY, localX, localY)) {
            displaySettings_.shorelineWaves = !displaySettings_.shorelineWaves;
            notifyDisplaySettingsChanged();
            return true;
        }
        rowY += ROW_HEIGHT + ROW_SPACING;

        // Reactive Foliage
        if (isInCheckbox(indentX, rowY, localX, localY)) {
            displaySettings_.reactiveFoliage = !displaySettings_.reactiveFoliage;
            notifyDisplaySettingsChanged();
            return true;
        }
        rowY += ROW_HEIGHT + ROW_SPACING;

        // Rolling Objects
        if (isInCheckbox(indentX, rowY, localX, localY)) {
            displaySettings_.rollingObjects = !displaySettings_.rollingObjects;
            notifyDisplaySettingsChanged();
            return true;
        }
        rowY += ROW_HEIGHT + ROW_SPACING * 2;

        // Detail Objects section
        rowY += SECTION_HEADER_HEIGHT + ROW_SPACING;

        // Enable Detail Objects
        if (isInCheckbox(baseX, rowY, localX, localY)) {
            displaySettings_.detailObjectsEnabled = !displaySettings_.detailObjectsEnabled;
            notifyDisplaySettingsChanged();
            return true;
        }
        rowY += ROW_HEIGHT + ROW_SPACING;

        // Detail density slider
        if (localY >= rowY && localY < rowY + ROW_HEIGHT &&
            localX >= sliderTrackX && localX < sliderTrackX + sliderTrackWidth) {
            float newValue = static_cast<float>(localX - sliderTrackX) / sliderTrackWidth;
            displaySettings_.detailDensity = std::clamp(newValue, 0.0f, 1.0f);
            draggingSlider_ = true;
            draggingSliderIndex_ = 1;
            notifyDisplaySettingsChanged();
            return true;
        }
        rowY += ROW_HEIGHT + ROW_SPACING;

        // View distance slider (max 2000 = sky dome cutoff)
        if (localY >= rowY && localY < rowY + ROW_HEIGHT &&
            localX >= sliderTrackX && localX < sliderTrackX + sliderTrackWidth) {
            float newValue = static_cast<float>(localX - sliderTrackX) / sliderTrackWidth;
            displaySettings_.detailViewDistance = std::clamp(newValue * 2000.0f, 0.0f, 2000.0f);
            draggingSlider_ = true;
            draggingSliderIndex_ = 2;
            notifyDisplaySettingsChanged();
            return true;
        }
        rowY += ROW_HEIGHT + ROW_SPACING;

        // Category checkboxes
        if (isInCheckbox(indentX, rowY, localX, localY)) {
            displaySettings_.detailGrass = !displaySettings_.detailGrass;
            notifyDisplaySettingsChanged();
            return true;
        }
        rowY += ROW_HEIGHT + ROW_SPACING;

        if (isInCheckbox(indentX, rowY, localX, localY)) {
            displaySettings_.detailPlants = !displaySettings_.detailPlants;
            notifyDisplaySettingsChanged();
            return true;
        }
        rowY += ROW_HEIGHT + ROW_SPACING;

        if (isInCheckbox(indentX, rowY, localX, localY)) {
            displaySettings_.detailRocks = !displaySettings_.detailRocks;
            notifyDisplaySettingsChanged();
            return true;
        }
        rowY += ROW_HEIGHT + ROW_SPACING;

        if (isInCheckbox(indentX, rowY, localX, localY)) {
            displaySettings_.detailDebris = !displaySettings_.detailDebris;
            notifyDisplaySettingsChanged();
            return true;
        }
    }

    return true;
}

bool OptionsWindow::handleMouseUp(int x, int y, bool leftButton)
{
    draggingSlider_ = false;
    draggingSliderIndex_ = -1;
    return WindowBase::handleMouseUp(x, y, leftButton);
}

bool OptionsWindow::handleMouseMove(int x, int y)
{
    if (!isVisible()) return false;

    // Handle slider dragging
    if (draggingSlider_ && draggingSliderIndex_ >= 0) {
        irr::core::recti content = getContentArea();
        int localX = x - content.UpperLeftCorner.X;

        int sliderTrackX = CONTENT_PADDING + 90;
        int sliderTrackWidth = 200 - 90 - 40;

        float newValue = static_cast<float>(localX - sliderTrackX) / sliderTrackWidth;
        newValue = std::clamp(newValue, 0.0f, 1.0f);

        switch (draggingSliderIndex_) {
            case 0:
                displaySettings_.environmentDensity = newValue;
                break;
            case 1:
                displaySettings_.detailDensity = newValue;
                break;
            case 2:
                displaySettings_.detailViewDistance = newValue * 2000.0f;
                break;
            case 3:
                displaySettings_.renderDistance = std::clamp(newValue * 2000.0f, 50.0f, 2000.0f);
                break;
        }
        notifyDisplaySettingsChanged();
        return true;
    }

    // Update hover states
    hoveredTab_ = -1;
    hoveredCheckbox_ = -1;
    hoveredSlider_ = -1;
    hoveredQualityOption_ = -1;

    if (!containsPoint(x, y)) return false;

    irr::core::recti content = getContentArea();
    int localX = x - content.UpperLeftCorner.X;
    int localY = y - content.UpperLeftCorner.Y;

    // Check tab hover
    hoveredTab_ = getTabAtPosition(localX, localY);

    // Check Display tab element hovers
    if (currentTab_ == Tab::Display) {
        int baseX = CONTENT_PADDING;
        int baseY = TAB_HEIGHT + CONTENT_PADDING + 4;
        int rowY = baseY + SECTION_HEADER_HEIGHT + ROW_SPACING;

        // Quality selector hover
        hoveredQualityOption_ = getQualityOptionAtPosition(baseX + 70, rowY, localX, localY);
    }

    return WindowBase::handleMouseMove(x, y);
}

bool OptionsWindow::handleMouseWheel(float delta)
{
    // Could implement scrolling for long content
    return false;
}

// ============================================================================
// Hit Testing
// ============================================================================

int OptionsWindow::getTabAtPosition(int x, int y) const
{
    if (y < 0 || y >= TAB_HEIGHT) return -1;

    for (size_t i = 0; i < tabBounds_.size(); i++) {
        if (x >= tabBounds_[i].UpperLeftCorner.X && x < tabBounds_[i].LowerRightCorner.X) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

bool OptionsWindow::isInCheckbox(int checkboxX, int checkboxY, int mouseX, int mouseY) const
{
    int boxY = checkboxY + (ROW_HEIGHT - CHECKBOX_SIZE) / 2;
    return mouseX >= checkboxX && mouseX < checkboxX + CHECKBOX_SIZE &&
           mouseY >= boxY && mouseY < boxY + CHECKBOX_SIZE;
}

int OptionsWindow::getQualityOptionAtPosition(int baseX, int baseY, int mouseX, int mouseY) const
{
    int btnY = baseY + (ROW_HEIGHT - QUALITY_BUTTON_HEIGHT) / 2;
    if (mouseY < btnY || mouseY >= btnY + QUALITY_BUTTON_HEIGHT) return -1;

    for (int i = 0; i < 4; i++) {
        int btnX = baseX + i * (QUALITY_BUTTON_WIDTH + QUALITY_BUTTON_SPACING);
        if (mouseX >= btnX && mouseX < btnX + QUALITY_BUTTON_WIDTH) {
            return i;
        }
    }
    return -1;
}

// ============================================================================
// Settings Management
// ============================================================================

void OptionsWindow::setDisplaySettings(const DisplaySettings& settings)
{
    displaySettings_ = settings;
    notifyDisplaySettingsChanged();
}

void OptionsWindow::notifyDisplaySettingsChanged()
{
    saveSettings();
    if (displaySettingsChangedCallback_) {
        displaySettingsChangedCallback_();
    }
}

bool OptionsWindow::loadSettings(const std::string& path)
{
    std::string loadPath = path.empty() ? settingsPath_ : path;

    std::ifstream file(loadPath);
    if (!file.is_open()) {
        // Try parent directory (for when running from build/)
        std::string altPath = "../" + loadPath;
        file.open(altPath);
        if (!file.is_open()) {
            return false;
        }
        // Update settings path to use the found location for saving
        settingsPath_ = altPath;
    }

    Json::Value root;
    Json::CharReaderBuilder builder;
    std::string errors;

    if (!Json::parseFromStream(builder, file, &root, &errors)) {
        return false;
    }

    // Render Distance
    displaySettings_.renderDistance = root.get("renderDistance", 300.0).asFloat();

    // Environment Effects
    if (root.isMember("environmentEffects")) {
        const Json::Value& env = root["environmentEffects"];

        std::string quality = env.get("quality", "medium").asString();
        if (quality == "off") displaySettings_.environmentQuality = EffectQuality::Off;
        else if (quality == "low") displaySettings_.environmentQuality = EffectQuality::Low;
        else if (quality == "medium") displaySettings_.environmentQuality = EffectQuality::Medium;
        else if (quality == "high") displaySettings_.environmentQuality = EffectQuality::High;

        displaySettings_.atmosphericParticles = env.get("atmosphericParticles", true).asBool();
        displaySettings_.ambientCreatures = env.get("ambientCreatures", true).asBool();
        displaySettings_.shorelineWaves = env.get("shorelineWaves", true).asBool();
        displaySettings_.reactiveFoliage = env.get("reactiveFoliage", true).asBool();
        displaySettings_.rollingObjects = env.get("rollingObjects", true).asBool();
        displaySettings_.environmentDensity = env.get("density", 0.5).asFloat();
    }

    // Detail Objects
    if (root.isMember("detailObjects")) {
        const Json::Value& detail = root["detailObjects"];

        displaySettings_.detailObjectsEnabled = detail.get("enabled", true).asBool();
        displaySettings_.detailDensity = detail.get("density", 1.0).asFloat();
        displaySettings_.detailViewDistance = detail.get("viewDistance", 150.0).asFloat();
        displaySettings_.detailGrass = detail.get("grass", true).asBool();
        displaySettings_.detailPlants = detail.get("plants", true).asBool();
        displaySettings_.detailRocks = detail.get("rocks", true).asBool();
        displaySettings_.detailDebris = detail.get("debris", true).asBool();
    }

    settingsPath_ = loadPath;
    return true;
}

bool OptionsWindow::saveSettings(const std::string& path)
{
    std::string savePath = path.empty() ? settingsPath_ : path;

    // Create parent directory if it doesn't exist
    std::filesystem::path filePath(savePath);
    if (filePath.has_parent_path()) {
        std::error_code ec;
        std::filesystem::create_directories(filePath.parent_path(), ec);
        // Ignore errors - we'll fail on file open if directory creation failed
    }

    Json::Value root;

    // Render Distance
    root["renderDistance"] = displaySettings_.renderDistance;

    // Environment Effects
    Json::Value env;
    switch (displaySettings_.environmentQuality) {
        case EffectQuality::Off: env["quality"] = "off"; break;
        case EffectQuality::Low: env["quality"] = "low"; break;
        case EffectQuality::Medium: env["quality"] = "medium"; break;
        case EffectQuality::High: env["quality"] = "high"; break;
    }
    env["atmosphericParticles"] = displaySettings_.atmosphericParticles;
    env["ambientCreatures"] = displaySettings_.ambientCreatures;
    env["shorelineWaves"] = displaySettings_.shorelineWaves;
    env["reactiveFoliage"] = displaySettings_.reactiveFoliage;
    env["rollingObjects"] = displaySettings_.rollingObjects;
    env["density"] = displaySettings_.environmentDensity;
    root["environmentEffects"] = env;

    // Detail Objects
    Json::Value detail;
    detail["enabled"] = displaySettings_.detailObjectsEnabled;
    detail["density"] = displaySettings_.detailDensity;
    detail["viewDistance"] = displaySettings_.detailViewDistance;
    detail["grass"] = displaySettings_.detailGrass;
    detail["plants"] = displaySettings_.detailPlants;
    detail["rocks"] = displaySettings_.detailRocks;
    detail["debris"] = displaySettings_.detailDebris;
    root["detailObjects"] = detail;

    std::ofstream file(savePath);
    if (!file.is_open()) {
        return false;
    }

    Json::StreamWriterBuilder builder;
    builder["indentation"] = "  ";
    std::unique_ptr<Json::StreamWriter> writer(builder.newStreamWriter());
    writer->write(root, &file);

    return true;
}

} // namespace ui
} // namespace eqt
