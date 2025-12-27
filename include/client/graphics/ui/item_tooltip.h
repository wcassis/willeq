#pragma once

#include "item_instance.h"
#include "ui_settings.h"
#include <irrlicht.h>
#include <string>
#include <vector>

namespace eqt {
namespace ui {

class ItemTooltip {
public:
    ItemTooltip();

    // Set the item to display (or nullptr to hide)
    void setItem(const inventory::ItemInstance* item, int mouseX, int mouseY);
    void clear();

    // Update tooltip state (call every frame)
    void update(uint32_t currentTimeMs, int mouseX, int mouseY);

    // Render the tooltip
    void render(irr::video::IVideoDriver* driver,
               irr::gui::IGUIEnvironment* gui,
               int screenWidth, int screenHeight);

    // State
    bool isVisible() const { return visible_; }
    bool isWaitingToShow() const { return item_ != nullptr && !visible_; }

    // Configuration
    void setHoverDelay(uint32_t delayMs) { hoverDelayMs_ = delayMs; }

private:
    // Build tooltip content lines
    void buildTooltipContent();

    // Position tooltip to avoid screen edges
    void positionTooltip(int mouseX, int mouseY, int screenWidth, int screenHeight);

    // Render sections
    void renderBackground(irr::video::IVideoDriver* driver);
    void renderContent(irr::video::IVideoDriver* driver,
                      irr::gui::IGUIEnvironment* gui);

    // Helper to add a line
    void addLine(const std::wstring& text, irr::video::SColor color);
    void addLine(const std::wstring& label, const std::wstring& value,
                irr::video::SColor labelColor, irr::video::SColor valueColor);
    void addStatLine(const std::wstring& stat1Name, int stat1Value,
                    const std::wstring& stat2Name, int stat2Value);
    void addBlankLine();

    // String builders
    std::wstring buildFlagsString() const;
    std::wstring buildClassString() const;
    std::wstring buildRaceString() const;
    std::wstring buildSlotsString() const;
    std::wstring buildAugmentTypeString(uint32_t augType) const;

    // The item being displayed
    const inventory::ItemInstance* item_ = nullptr;

    // Tooltip state
    bool visible_ = false;
    uint32_t hoverStartTime_ = 0;
    int hoverX_ = 0;
    int hoverY_ = 0;

    // Tooltip bounds
    irr::core::recti bounds_;

    // Content lines
    struct TooltipLine {
        std::wstring text;
        irr::video::SColor color;
        bool isTwoColumn = false;
        std::wstring text2;
        irr::video::SColor color2;
    };
    std::vector<TooltipLine> lines_;

    // Configuration
    uint32_t hoverDelayMs_ = 300;

    // Layout constants - initialized from UISettings
    int TOOLTIP_MIN_WIDTH;
    int TOOLTIP_MAX_WIDTH;
    int LINE_HEIGHT;
    int PADDING;
    int SECTION_SPACING;
    int MOUSE_OFFSET;

    // Color accessors - read from UISettings
    irr::video::SColor getBackground() const { return UISettings::instance().itemTooltip().background; }
    irr::video::SColor getBorder() const { return UISettings::instance().itemTooltip().border; }
    irr::video::SColor getTitle() const { return UISettings::instance().itemTooltip().title; }
    irr::video::SColor getMagic() const { return UISettings::instance().itemTooltip().magic; }
    irr::video::SColor getFlags() const { return UISettings::instance().itemTooltip().flags; }
    irr::video::SColor getRestrictions() const { return UISettings::instance().itemTooltip().restrictions; }
    irr::video::SColor getStats() const { return UISettings::instance().itemTooltip().stats; }
    irr::video::SColor getStatsValue() const { return UISettings::instance().itemTooltip().statsValue; }
    irr::video::SColor getEffect() const { return UISettings::instance().itemTooltip().effect; }
    irr::video::SColor getEffectDesc() const { return UISettings::instance().itemTooltip().effectDesc; }
    irr::video::SColor getLore() const { return UISettings::instance().itemTooltip().lore; }
};

} // namespace ui
} // namespace eqt
