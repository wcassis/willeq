#pragma once

#include "ui_settings.h"
#include <irrlicht.h>
#include <string>
#include <vector>
#include <cstdint>

namespace EQ {
struct SkillData;
}

namespace eqt {
namespace ui {

class SkillTooltip {
public:
    SkillTooltip();

    // Set the skill to display (or nullptr to hide)
    void setSkill(const EQ::SkillData* skill, int mouseX, int mouseY);
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

    // The skill being displayed
    const EQ::SkillData* skill_ = nullptr;

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
    uint32_t hoverDelayMs_ = 300;  // Hover delay before showing

    // Layout constants
    static constexpr int TOOLTIP_MIN_WIDTH = 200;
    static constexpr int TOOLTIP_MAX_WIDTH = 300;
    static constexpr int LINE_HEIGHT = 14;
    static constexpr int PADDING = 6;
    static constexpr int MOUSE_OFFSET = 12;

    // Colors
    irr::video::SColor getBackground() const { return irr::video::SColor(230, 20, 20, 30); }
    irr::video::SColor getBorder() const { return irr::video::SColor(255, 100, 100, 120); }
    irr::video::SColor getSkillName() const { return irr::video::SColor(255, 255, 255, 100); }
    irr::video::SColor getCategory() const { return irr::video::SColor(255, 180, 180, 200); }
    irr::video::SColor getValue() const { return irr::video::SColor(255, 100, 255, 100); }
    irr::video::SColor getInfo() const { return irr::video::SColor(255, 200, 200, 200); }
    irr::video::SColor getCooldown() const { return irr::video::SColor(255, 255, 200, 100); }
    irr::video::SColor getActivatable() const { return irr::video::SColor(255, 100, 200, 255); }
};

} // namespace ui
} // namespace eqt
