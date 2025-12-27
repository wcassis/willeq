#include "client/graphics/ui/buff_tooltip.h"
#include "client/spell/buff_manager.h"
#include "client/spell/spell_database.h"

namespace eqt {
namespace ui {

BuffTooltip::BuffTooltip()
{
    // Initialize layout constants from UISettings
    const auto& tooltipSettings = UISettings::instance().buffTooltip();
    TOOLTIP_MIN_WIDTH = tooltipSettings.minWidth;
    TOOLTIP_MAX_WIDTH = tooltipSettings.maxWidth;
    LINE_HEIGHT = tooltipSettings.lineHeight;
    PADDING = tooltipSettings.padding;
    MOUSE_OFFSET = tooltipSettings.mouseOffset;
}

void BuffTooltip::setBuff(const EQ::ActiveBuff* buff, int mouseX, int mouseY)
{
    if (buff_ != buff) {
        buff_ = buff;
        visible_ = false;
        hoverStartTime_ = 0;
        lines_.clear();
    }
    hoverX_ = mouseX;
    hoverY_ = mouseY;
}

void BuffTooltip::clear()
{
    buff_ = nullptr;
    visible_ = false;
    hoverStartTime_ = 0;
    lines_.clear();
}

void BuffTooltip::update(uint32_t currentTimeMs, int mouseX, int mouseY)
{
    if (!buff_) {
        visible_ = false;
        return;
    }

    // Check if mouse moved significantly
    int dx = mouseX - hoverX_;
    int dy = mouseY - hoverY_;
    if (dx * dx + dy * dy > 100) {  // 10 pixel threshold
        hoverStartTime_ = currentTimeMs;
        hoverX_ = mouseX;
        hoverY_ = mouseY;
        visible_ = false;
    }

    // Start timer if not started
    if (hoverStartTime_ == 0) {
        hoverStartTime_ = currentTimeMs;
    }

    // Show after delay
    if (!visible_ && (currentTimeMs - hoverStartTime_) >= hoverDelayMs_) {
        visible_ = true;
        buildTooltipContent();
    }
}

void BuffTooltip::buildTooltipContent()
{
    lines_.clear();
    if (!buff_) return;

    // Spell name
    std::string spellName = "Unknown Spell";
    if (spellDb_) {
        const EQ::SpellData* spell = spellDb_->getSpell(buff_->spell_id);
        if (spell) {
            spellName = spell->name;
        }
    }
    lines_.push_back({std::wstring(spellName.begin(), spellName.end()), getSpellName()});

    // Duration
    if (buff_->isPermanent()) {
        lines_.push_back({L"Duration: Permanent", getDuration()});
    } else {
        std::string timeStr = buff_->getTimeString();
        std::wstring durationText = L"Duration: " + std::wstring(timeStr.begin(), timeStr.end());
        lines_.push_back({durationText, getDuration()});
    }

    // Caster level
    if (buff_->caster_level > 0) {
        std::wstring levelText = L"Caster Level: " + std::to_wstring(buff_->caster_level);
        lines_.push_back({levelText, getInfo()});
    }
}

void BuffTooltip::positionTooltip(int mouseX, int mouseY, int screenWidth, int screenHeight)
{
    int width = TOOLTIP_MIN_WIDTH;
    int height = static_cast<int>(lines_.size()) * LINE_HEIGHT + PADDING * 2;

    // Position to lower-right of mouse by default
    int x = mouseX + MOUSE_OFFSET;
    int y = mouseY + MOUSE_OFFSET;

    // Adjust if off screen
    if (x + width > screenWidth) {
        x = mouseX - width - MOUSE_OFFSET;
    }
    if (y + height > screenHeight) {
        y = mouseY - height - MOUSE_OFFSET;
    }
    if (x < 0) x = 0;
    if (y < 0) y = 0;

    bounds_ = irr::core::recti(x, y, x + width, y + height);
}

void BuffTooltip::render(irr::video::IVideoDriver* driver,
                          irr::gui::IGUIEnvironment* gui,
                          int screenWidth, int screenHeight)
{
    if (!visible_ || !buff_ || lines_.empty()) {
        return;
    }

    positionTooltip(hoverX_, hoverY_, screenWidth, screenHeight);

    // Draw background
    driver->draw2DRectangle(getBackground(), bounds_);

    // Draw border
    driver->draw2DRectangleOutline(bounds_, getBorder());

    // Draw content
    if (gui) {
        irr::gui::IGUIFont* font = gui->getBuiltInFont();
        if (font) {
            int y = bounds_.UpperLeftCorner.Y + PADDING;
            for (const auto& line : lines_) {
                irr::core::recti textRect(
                    bounds_.UpperLeftCorner.X + PADDING,
                    y,
                    bounds_.LowerRightCorner.X - PADDING,
                    y + LINE_HEIGHT
                );
                font->draw(line.text.c_str(), textRect, line.color);
                y += LINE_HEIGHT;
            }
        }
    }
}

} // namespace ui
} // namespace eqt
