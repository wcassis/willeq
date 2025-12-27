#pragma once

#include "ui_settings.h"
#include <irrlicht.h>
#include <string>
#include <vector>
#include <cstdint>

namespace EQ {
struct ActiveBuff;
class SpellDatabase;
}

namespace eqt {
namespace ui {

class BuffTooltip {
public:
    BuffTooltip();

    // Set spell database for looking up spell info
    void setSpellDatabase(EQ::SpellDatabase* db) { spellDb_ = db; }

    // Set the buff to display (or nullptr to hide)
    void setBuff(const EQ::ActiveBuff* buff, int mouseX, int mouseY);
    void clear();

    // Update tooltip state (call every frame)
    void update(uint32_t currentTimeMs, int mouseX, int mouseY);

    // Render the tooltip
    void render(irr::video::IVideoDriver* driver,
               irr::gui::IGUIEnvironment* gui,
               int screenWidth, int screenHeight);

    // State
    bool isVisible() const { return visible_; }

    // Configuration
    void setHoverDelay(uint32_t delayMs) { hoverDelayMs_ = delayMs; }

private:
    // Build tooltip content
    void buildTooltipContent();

    // Position tooltip to avoid screen edges
    void positionTooltip(int mouseX, int mouseY, int screenWidth, int screenHeight);

    // The buff being displayed
    const EQ::ActiveBuff* buff_ = nullptr;
    EQ::SpellDatabase* spellDb_ = nullptr;

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
    };
    std::vector<TooltipLine> lines_;

    // Configuration
    uint32_t hoverDelayMs_ = 200;  // Faster than item tooltip

    // Layout constants - initialized from UISettings
    int TOOLTIP_MIN_WIDTH;
    int TOOLTIP_MAX_WIDTH;
    int LINE_HEIGHT;
    int PADDING;
    int MOUSE_OFFSET;

    // Color accessors - read from UISettings
    irr::video::SColor getBackground() const { return UISettings::instance().buffTooltip().background; }
    irr::video::SColor getBorder() const { return UISettings::instance().buffTooltip().border; }
    irr::video::SColor getSpellName() const { return UISettings::instance().buffTooltip().spellName; }
    irr::video::SColor getDuration() const { return UISettings::instance().buffTooltip().duration; }
    irr::video::SColor getInfo() const { return UISettings::instance().buffTooltip().info; }
};

} // namespace ui
} // namespace eqt
