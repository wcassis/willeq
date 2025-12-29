#include "client/graphics/ui/skill_tooltip.h"
#include "client/skill/skill_data.h"
#include "client/skill/skill_constants.h"
#include <fmt/format.h>

namespace eqt {
namespace ui {

SkillTooltip::SkillTooltip()
{
}

void SkillTooltip::setSkill(const EQ::SkillData* skill, int mouseX, int mouseY)
{
    if (skill_ != skill) {
        skill_ = skill;
        visible_ = false;
        hoverStartTime_ = 0;
        lines_.clear();
    }
    hoverX_ = mouseX;
    hoverY_ = mouseY;
}

void SkillTooltip::clear()
{
    skill_ = nullptr;
    visible_ = false;
    hoverStartTime_ = 0;
    lines_.clear();
}

void SkillTooltip::update(uint32_t currentTimeMs, int mouseX, int mouseY)
{
    if (!skill_) {
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

void SkillTooltip::buildTooltipContent()
{
    lines_.clear();
    if (!skill_) return;

    // Skill name (large, yellow)
    lines_.push_back({std::wstring(skill_->name.begin(), skill_->name.end()), getSkillName()});

    // Category
    const char* categoryStr = EQ::getSkillCategoryName(skill_->category);
    std::string categoryLine = fmt::format("Category: {}", categoryStr);
    lines_.push_back({std::wstring(categoryLine.begin(), categoryLine.end()), getCategory()});

    // Current / Max value
    std::string valueStr = fmt::format("Skill: {} / {}", skill_->current_value, skill_->max_value);
    lines_.push_back({std::wstring(valueStr.begin(), valueStr.end()), getValue()});

    // Base value if different
    if (skill_->base_value != skill_->current_value && skill_->base_value > 0) {
        std::string baseStr = fmt::format("Base: {}", skill_->base_value);
        lines_.push_back({std::wstring(baseStr.begin(), baseStr.end()), getInfo()});
    }

    // Activatable status
    if (skill_->is_activatable) {
        lines_.push_back({L"[Activatable]", getActivatable()});

        // Cooldown info
        if (skill_->isOnCooldown()) {
            uint32_t remaining = skill_->getCooldownRemaining();
            std::string cdStr = fmt::format("Cooldown: {:.1f}s", remaining / 1000.0f);
            lines_.push_back({std::wstring(cdStr.begin(), cdStr.end()), getCooldown()});
        } else if (skill_->recast_time_ms > 0) {
            std::string recastStr = fmt::format("Recast: {:.1f}s", skill_->recast_time_ms / 1000.0f);
            lines_.push_back({std::wstring(recastStr.begin(), recastStr.end()), getInfo()});
        }

        // Requirements
        if (skill_->requires_target) {
            lines_.push_back({L"Requires target", getInfo()});
        }
        if (skill_->requires_combat) {
            lines_.push_back({L"Combat only", getInfo()});
        }
        if (skill_->requires_behind) {
            lines_.push_back({L"Requires behind target", getInfo()});
        }

        // Resource costs
        if (skill_->endurance_cost > 0) {
            std::string endStr = fmt::format("Endurance: {}", skill_->endurance_cost);
            lines_.push_back({std::wstring(endStr.begin(), endStr.end()), getInfo()});
        }
        if (skill_->mana_cost > 0) {
            std::string manaStr = fmt::format("Mana: {}", skill_->mana_cost);
            lines_.push_back({std::wstring(manaStr.begin(), manaStr.end()), getInfo()});
        }
    }

    // Toggle state
    if (skill_->is_toggle) {
        std::wstring toggleStr = skill_->is_active ? L"[Active]" : L"[Inactive]";
        lines_.push_back({toggleStr, skill_->is_active ? getActivatable() : getInfo()});
    }
}

void SkillTooltip::positionTooltip(int mouseX, int mouseY, int screenWidth, int screenHeight)
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

void SkillTooltip::render(irr::video::IVideoDriver* driver,
                          irr::gui::IGUIEnvironment* gui,
                          int screenWidth, int screenHeight)
{
    if (!visible_ || !skill_ || lines_.empty()) {
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
