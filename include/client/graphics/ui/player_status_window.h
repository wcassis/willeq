#pragma once

#include "window_base.h"
#include "ui_settings.h"
#include <cstdint>
#include <string>

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
    ~PlayerStatusWindow();

    // D20b4: Data-push methods (called by bridge handlers)
    void setPlayerName(const std::string& name);
    void setPlayerStats(uint32_t curHP, uint32_t maxHP, uint32_t curMana, uint32_t maxMana,
                        uint32_t curEndurance, uint32_t maxEndurance);
    void setTarget(const std::string& name, uint8_t hpPercent, uint16_t curMana, uint16_t maxMana);
    void clearTarget();
    void setAutoAttacking(bool attacking) { isAutoAttacking_ = attacking; contentDirty_ = true; }

    // Update from player state (call each frame) — now uses local cached data
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

    // RTT content cache
    irr::video::ITexture* contentRT_ = nullptr;
    irr::video::IVideoDriver* cachedDriver2_ = nullptr;  // cachedDriver_ name taken by font cache
    bool contentDirty_ = true;
    int contentRTWidth_ = 0;
    int contentRTHeight_ = 0;
    void ensureContentRT(irr::video::IVideoDriver* driver);

    // Dirty detection
    uint32_t lastCachedHP_ = 0;
    uint32_t lastCachedMaxHP_ = 0;
    uint32_t lastCachedMana_ = 0;
    uint32_t lastCachedMaxMana_ = 0;
    uint32_t lastCachedStamina_ = 0;
    uint32_t lastCachedMaxStamina_ = 0;
    bool lastCachedHasTarget_ = false;
    uint8_t lastCachedTargetHp_ = 0;
    uint16_t lastCachedTargetMana_ = 0;
    uint16_t lastCachedTargetMaxMana_ = 0;
    std::wstring lastCachedTargetName_;
    std::wstring lastCachedCastingSpell_;
    std::wstring lastCachedPlayerName_;
    bool lastWindowHovered_ = false;

    // Combat state for border animation
    bool isAutoAttacking_ = false;

    // D20b4: No EverQuest* pointer — data pushed by bridge

    // Auto-attack border animation
    uint32_t lastAnimationTime_ = 0;
    float animationOffset_ = 0.0f;  // 0.0 to 1.0, wraps around

    // Draw animated combat border when auto-attack is enabled
    void drawCombatBorder(irr::video::IVideoDriver* driver, uint32_t currentTimeMs);
};

} // namespace ui
} // namespace eqt
