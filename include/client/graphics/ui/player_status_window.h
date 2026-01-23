#pragma once

#include "window_base.h"
#include "ui_settings.h"
#include <cstdint>
#include <string>

// Forward declaration
class EverQuest;

namespace eqt {
namespace ui {

/**
 * PlayerStatusWindow - Displays the player's and target's vital stats.
 *
 * Shows:
 * - Player's full name (first and last)
 * - Health bar (red) with current/max values
 * - Mana bar (blue) with current/max values
 * - Stamina/Endurance bar (yellow) with current/max values
 * - Target's name, health, mana, and current casting spell
 *
 * The window has no title bar and is positioned in the upper left corner.
 */
class PlayerStatusWindow : public WindowBase {
public:
    PlayerStatusWindow();
    ~PlayerStatusWindow() = default;

    // Set EverQuest reference for player data
    void setEQ(EverQuest* eq) { eq_ = eq; }

    // Update from player state (call each frame)
    void update();

    // Target casting spell tracking
    void setTargetCastingSpell(const std::string& spellName);
    void clearTargetCastingSpell();

    // Rendering
    void render(irr::video::IVideoDriver* driver,
                irr::gui::IGUIEnvironment* gui) override;

protected:
    void renderContent(irr::video::IVideoDriver* driver,
                      irr::gui::IGUIEnvironment* gui) override;

private:
    void initializeLayout();

    // Draw a stat bar
    void drawStatBar(irr::video::IVideoDriver* driver,
                    irr::gui::IGUIEnvironment* gui,
                    const irr::core::recti& bounds,
                    uint32_t current, uint32_t max,
                    const irr::video::SColor& bgColor,
                    const irr::video::SColor& fillColor);

    // Draw stat text (current/max) centered in bar
    void drawBarText(irr::gui::IGUIEnvironment* gui,
                    const irr::core::recti& bounds,
                    uint32_t current, uint32_t max);

    // Draw a percent bar (0-100)
    void drawPercentBar(irr::video::IVideoDriver* driver,
                       const irr::core::recti& bounds,
                       uint8_t percent,
                       const irr::video::SColor& bgColor,
                       const irr::video::SColor& fillColor);

    // Draw percent text centered in bar
    void drawPercentBarText(irr::gui::IGUIEnvironment* gui,
                           const irr::core::recti& bounds,
                           uint8_t percent);

    // Truncate text to fit within maxWidth, adding "..." if truncated
    std::wstring truncateText(irr::gui::IGUIFont* font,
                              const std::wstring& text,
                              int maxWidth) const;

    // Layout constants - initialized from UISettings
    int NAME_HEIGHT;
    int BAR_HEIGHT;
    int BAR_SPACING;
    int BAR_LABEL_WIDTH;
    int PADDING;

    // Color accessors - read from UISettings
    irr::video::SColor getNameTextColor() const { return UISettings::instance().playerStatus().nameText; }
    irr::video::SColor getHpBackground() const { return UISettings::instance().playerStatus().hpBackground; }
    irr::video::SColor getHpFill() const { return UISettings::instance().playerStatus().hpFill; }
    irr::video::SColor getManaBackground() const { return UISettings::instance().playerStatus().manaBackground; }
    irr::video::SColor getManaFill() const { return UISettings::instance().playerStatus().manaFill; }
    irr::video::SColor getStaminaBackground() const { return UISettings::instance().playerStatus().staminaBackground; }
    irr::video::SColor getStaminaFill() const { return UISettings::instance().playerStatus().staminaFill; }
    irr::video::SColor getBarTextColor() const { return UISettings::instance().playerStatus().barText; }

    // Cached player data
    std::wstring playerName_;
    uint32_t currentHP_ = 0;
    uint32_t maxHP_ = 0;
    uint32_t currentMana_ = 0;
    uint32_t maxMana_ = 0;
    uint32_t currentStamina_ = 0;
    uint32_t maxStamina_ = 0;

    // Cached target data
    bool hasTarget_ = false;
    std::wstring targetName_;
    uint8_t targetHpPercent_ = 0;
    uint16_t targetCurrentMana_ = 0;
    uint16_t targetMaxMana_ = 0;
    std::wstring targetCastingSpell_;

    // Cached truncated display names (recalculated on resize or name change)
    std::wstring displayPlayerName_;
    std::wstring displayTargetName_;
    std::wstring displayCastingSpell_;
    int cachedContentWidth_ = 0;
    irr::gui::IGUIFont* cachedFont_ = nullptr;

    // Update cached display names when needed
    void updateDisplayNames(irr::gui::IGUIFont* font, int contentWidth);

    // EverQuest reference
    EverQuest* eq_ = nullptr;

    // Auto-attack border animation
    uint32_t lastAnimationTime_ = 0;
    float animationOffset_ = 0.0f;  // 0.0 to 1.0, wraps around

    // Draw animated combat border when auto-attack is enabled
    void drawCombatBorder(irr::video::IVideoDriver* driver, uint32_t currentTimeMs);
};

} // namespace ui
} // namespace eqt
